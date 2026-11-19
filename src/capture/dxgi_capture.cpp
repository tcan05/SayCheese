#include "capture/dxgi_capture.hpp"

#include "capture/pixel_convert.hpp"

#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace saycheese::capture {

namespace {

using Microsoft::WRL::ComPtr;

class ReleaseFrameGuard {
public:
    explicit ReleaseFrameGuard(IDXGIOutputDuplication* dup) noexcept : dup_(dup) {}
    ~ReleaseFrameGuard() noexcept {
        if (dup_ != nullptr) {
            dup_->ReleaseFrame();
        }
    }
    ReleaseFrameGuard(const ReleaseFrameGuard&) = delete;
    ReleaseFrameGuard& operator=(const ReleaseFrameGuard&) = delete;

private:
    IDXGIOutputDuplication* dup_ = nullptr;
};

bool MapDxgiFormat(DXGI_FORMAT fmt, SurfaceFormat& out) noexcept {
    switch (fmt) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        out = SurfaceFormat::BGRA8;
        return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        out = SurfaceFormat::RGBA8;
        return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        out = SurfaceFormat::R10G10B10A2;
        return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        out = SurfaceFormat::RGBA16F;
        return true;
    default:
        return false;
    }
}

SurfaceRotation MapRotation(DXGI_MODE_ROTATION rot) noexcept {
    switch (rot) {
    case DXGI_MODE_ROTATION_ROTATE90:
        return SurfaceRotation::Rotate90;
    case DXGI_MODE_ROTATION_ROTATE180:
        return SurfaceRotation::Rotate180;
    case DXGI_MODE_ROTATION_ROTATE270:
        return SurfaceRotation::Rotate270;
    default:
        return SurfaceRotation::Identity;
    }
}

float QueryMonitorSdrWhiteMultiplier(const wchar_t* deviceName) noexcept {
    if (deviceName == nullptr || deviceName[0] == L'\0') {
        return 1.0f;
    }
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS ||
        pathCount == 0) {
        return 1.0f;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                             &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) {
        return 1.0f;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME srcName{};
        srcName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        srcName.header.size = sizeof(srcName);
        srcName.header.adapterId = paths[i].sourceInfo.adapterId;
        srcName.header.id = paths[i].sourceInfo.id;
        if (::DisplayConfigGetDeviceInfo(&srcName.header) != ERROR_SUCCESS) {
            continue;
        }
        if (::wcscmp(srcName.viewGdiDeviceName, deviceName) != 0) {
            continue;
        }

        DISPLAYCONFIG_SDR_WHITE_LEVEL whiteLevel{};
        whiteLevel.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        whiteLevel.header.size = sizeof(whiteLevel);
        whiteLevel.header.adapterId = paths[i].targetInfo.adapterId;
        whiteLevel.header.id = paths[i].targetInfo.id;
        if (::DisplayConfigGetDeviceInfo(&whiteLevel.header) == ERROR_SUCCESS &&
            whiteLevel.SDRWhiteLevel >= 1000) {
            return static_cast<float>(whiteLevel.SDRWhiteLevel) / 1000.0f;
        }
    }
    return 1.0f;
}

HRESULT CreateAdapterD3D11Device(
    IDXGIAdapter1* adapter,
    ComPtr<ID3D11Device>& outDevice,
    ComPtr<ID3D11DeviceContext>& outContext) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT hr = ::D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        kLevels,
        ARRAYSIZE(kLevels),
        D3D11_SDK_VERSION,
        &outDevice,
        nullptr,
        &outContext);

#ifdef _DEBUG
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = ::D3D11CreateDevice(
            adapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            kLevels,
            ARRAYSIZE(kLevels),
            D3D11_SDK_VERSION,
            &outDevice,
            nullptr,
            &outContext);
    }
#endif
    return hr;
}

bool CaptureSingleOutput(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    IDXGIOutput* output,
    const DXGI_OUTPUT_DESC& outputDesc,
    CapturedFrame& dstFrame) {
    ComPtr<IDXGIOutputDuplication> duplication;

    // Prefer DuplicateOutput1 to properly receive HDR / 10-bit surfaces when enabled.
    ComPtr<IDXGIOutput5> output5;
    if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output5))) && output5) {
        static constexpr DXGI_FORMAT kPreferredFormats[] = {
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R10G10B10A2_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT
        };
        output5->DuplicateOutput1(device, 0, ARRAYSIZE(kPreferredFormats), kPreferredFormats, &duplication);
    }

    if (!duplication) {
        ComPtr<IDXGIOutput1> output1;
        if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output1))) || !output1) {
            return false;
        }
        if (FAILED(output1->DuplicateOutput(device, &duplication)) || !duplication) {
            return false;
        }
    }

    DXGI_OUTDUPL_DESC duplDesc{};
    duplication->GetDesc(&duplDesc);

    ComPtr<ID3D11Texture2D> stagingTex;
    D3D11_TEXTURE2D_DESC stagedDesc{};
    bool hasStagedFrame = false;

    // Bounded non-blocking acquisition: max 5 attempts, <=100ms timeout per call.
    constexpr int kMaxAttempts = 5;
    constexpr UINT kAcquireTimeoutMs = 100;

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        ComPtr<IDXGIResource> desktopRes;
        const UINT timeoutMs = hasStagedFrame ? 50u : kAcquireTimeoutMs;
        const HRESULT hr = duplication->AcquireNextFrame(timeoutMs, &frameInfo, &desktopRes);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (hasStagedFrame) {
                // Static desktop: keep valid staged frame from earlier S_OK.
                break;
            }
            continue;
        }
        if (hr == DXGI_ERROR_ACCESS_LOST || FAILED(hr)) {
            return false;
        }

        ReleaseFrameGuard releaseGuard(duplication.Get());

        ComPtr<ID3D11Texture2D> desktopTex;
        if (FAILED(desktopRes.As(&desktopTex)) || !desktopTex) {
            return false;
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        desktopTex->GetDesc(&texDesc);

        SurfaceFormat mappedFmt = SurfaceFormat::BGRA8;
        if (!MapDxgiFormat(texDesc.Format, mappedFmt)) {
            return false;
        }

        if (!stagingTex || stagedDesc.Width != texDesc.Width ||
            stagedDesc.Height != texDesc.Height || stagedDesc.Format != texDesc.Format) {
            stagingTex.Reset();
            stagedDesc = texDesc;
            stagedDesc.Usage = D3D11_USAGE_STAGING;
            stagedDesc.BindFlags = 0;
            stagedDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagedDesc.MiscFlags = 0;
            if (FAILED(device->CreateTexture2D(&stagedDesc, nullptr, &stagingTex)) || !stagingTex) {
                return false;
            }
        }

        context->CopyResource(stagingTex.Get(), desktopTex.Get());
        hasStagedFrame = true;

        // If desktop pixels were updated (or we already completed our short static-desktop check), finish now.
        if (frameInfo.LastPresentTime.QuadPart != 0 || frameInfo.AccumulatedFrames > 0 || attempt >= 1) {
            break;
        }
    }

    if (!hasStagedFrame || !stagingTex) {
        return false;
    }

    SurfaceFormat fmt = SurfaceFormat::BGRA8;
    if (!MapDxgiFormat(stagedDesc.Format, fmt)) {
        return false;
    }

    const int desktopW = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    const int desktopH = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    const SurfaceRotation reportedRot = MapRotation(
        duplDesc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED
            ? duplDesc.Rotation
            : outputDesc.Rotation);
    const SurfaceRotation rotation = ResolveCaptureRotation(
        static_cast<int>(stagedDesc.Width),
        static_cast<int>(stagedDesc.Height),
        desktopW,
        desktopH,
        reportedRot);

    const float sdrWhite = (fmt == SurfaceFormat::RGBA16F)
        ? QueryMonitorSdrWhiteMultiplier(outputDesc.DeviceName)
        : 1.0f;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }

    const int dstOffsetX = outputDesc.DesktopCoordinates.left - dstFrame.originX;
    const int dstOffsetY = outputDesc.DesktopCoordinates.top - dstFrame.originY;
    BlitConvertedSurfaceToFrame(
        static_cast<const uint8_t*>(mapped.pData),
        static_cast<int>(stagedDesc.Width),
        static_cast<int>(stagedDesc.Height),
        static_cast<int>(mapped.RowPitch),
        fmt,
        rotation,
        sdrWhite,
        dstOffsetX,
        dstOffsetY,
        dstFrame);

    context->Unmap(stagingTex.Get(), 0);
    return true;
}

} // namespace

DxgiCaptureSummary CaptureMonitorsDxgi(CapturedFrame& dstFrame) {
    DxgiCaptureSummary summary{};
    if (!dstFrame.IsValid()) {
        return summary;
    }

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
        return summary;
    }

    for (UINT adapterIdx = 0;; ++adapterIdx) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hrAdapter = factory->EnumAdapters1(adapterIdx, &adapter);
        if (hrAdapter == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hrAdapter) || !adapter) {
            continue;
        }

        DXGI_ADAPTER_DESC1 adapterDesc{};
        if (FAILED(adapter->GetDesc1(&adapterDesc))) {
            continue;
        }
        if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        // Collect attached outputs before creating a D3D11 device on this adapter.
        std::vector<std::pair<ComPtr<IDXGIOutput>, DXGI_OUTPUT_DESC>> attachedOutputs;
        for (UINT outIdx = 0;; ++outIdx) {
            ComPtr<IDXGIOutput> output;
            const HRESULT hrOut = adapter->EnumOutputs(outIdx, &output);
            if (hrOut == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hrOut) || !output) {
                continue;
            }
            DXGI_OUTPUT_DESC outDesc{};
            if (SUCCEEDED(output->GetDesc(&outDesc)) && outDesc.AttachedToDesktop) {
                attachedOutputs.emplace_back(std::move(output), outDesc);
            }
        }

        if (attachedOutputs.empty()) {
            continue;
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        if (FAILED(CreateAdapterD3D11Device(adapter.Get(), device, context)) || !device || !context) {
            summary.outputsAttempted += static_cast<int>(attachedOutputs.size());
            continue;
        }

        for (auto& [output, outDesc] : attachedOutputs) {
            ++summary.outputsAttempted;
            if (CaptureSingleOutput(device.Get(), context.Get(), output.Get(), outDesc, dstFrame)) {
                ++summary.outputsSucceeded;
                summary.capturedVirtualRects.push_back(outDesc.DesktopCoordinates);
            }
        }

        // Ensure deterministic immediate release of all capture GPU resources and driver caches.
        attachedOutputs.clear();
        context->ClearState();
        context->Flush();
        ComPtr<IDXGIDevice3> dxgiDev3;
        if (SUCCEEDED(device.As(&dxgiDev3)) && dxgiDev3) {
            dxgiDev3->Trim();
        }
        context.Reset();
        device.Reset();
    }

    return summary;
}

} // namespace saycheese::capture
