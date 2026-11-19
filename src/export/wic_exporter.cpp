#include "export/wic_exporter.hpp"

#include <commdlg.h>
#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>

namespace saycheese::export_img {

namespace {

using Microsoft::WRL::ComPtr;

class TargetRestoreGuard {
public:
    TargetRestoreGuard(ID2D1DeviceContext* dc, ID2D1Image* prev) noexcept
        : dc_(dc), prev_(prev) {}
    ~TargetRestoreGuard() noexcept {
        if (dc_ != nullptr) {
            dc_->SetTransform(D2D1::Matrix3x2F::Identity());
            dc_->SetTarget(prev_);
        }
    }
    TargetRestoreGuard(const TargetRestoreGuard&) = delete;
    TargetRestoreGuard& operator=(const TargetRestoreGuard&) = delete;

private:
    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1Image* prev_ = nullptr;
};

std::wstring ToLowerCopy(std::wstring s) {
    for (wchar_t& ch : s) {
        ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    }
    return s;
}

} // namespace

ExportedImage RenderCropToWicBitmap(
    render::D2DContext& ctx,
    render::SceneRenderer& renderer,
    const capture::CapturedFrame& sourceFrame,
    tools::RectF cropRect,
    const std::vector<tools::Annotation>& annotations) {
    ExportedImage result{};
    ID2D1DeviceContext* dc = ctx.DeviceContext();
    ID2D1Bitmap1* desktopBmp = ctx.DesktopBitmap();
    IWICImagingFactory* wic = ctx.WicFactory();

    if (dc == nullptr || desktopBmp == nullptr || wic == nullptr || !sourceFrame.IsValid()) {
        return result;
    }

    const tools::RectF norm = cropRect.Normalized();
    const int left = std::clamp(static_cast<int>(std::lround(norm.left)), 0, sourceFrame.width);
    const int top = std::clamp(static_cast<int>(std::lround(norm.top)), 0, sourceFrame.height);
    const int right = std::clamp(static_cast<int>(std::lround(norm.right)), 0, sourceFrame.width);
    const int bottom = std::clamp(static_cast<int>(std::lround(norm.bottom)), 0, sourceFrame.height);
    if (right <= left || bottom <= top) {
        return result;
    }

    const uint32_t cropW = static_cast<uint32_t>(right - left);
    const uint32_t cropH = static_cast<uint32_t>(bottom - top);
    const D2D1_SIZE_U cropSize = D2D1::SizeU(cropW, cropH);

    const D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);

    ComPtr<ID2D1Bitmap1> offscreenTarget;
    if (FAILED(dc->CreateBitmap(cropSize, nullptr, 0, &targetProps, &offscreenTarget)) || !offscreenTarget) {
        return result;
    }

    ComPtr<ID2D1Image> prevTarget;
    dc->GetTarget(&prevTarget);
    TargetRestoreGuard restoreGuard(dc, prevTarget.Get());

    dc->SetTarget(offscreenTarget.Get());
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Translation(-static_cast<float>(left), -static_cast<float>(top)));
    renderer.RenderScene(
        dc,
        ctx.D2DFactory(),
        ctx.DWriteFactory(),
        desktopBmp,
        sourceFrame,
        tools::RectF{static_cast<float>(left), static_cast<float>(top),
                     static_cast<float>(right), static_cast<float>(bottom)},
        annotations,
        nullptr);
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    if (FAILED(dc->EndDraw())) {
        return result;
    }

    // Restore target before reading back staging bitmap.
    dc->SetTarget(prevTarget.Get());

    const D2D1_BITMAP_PROPERTIES1 stagingProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);

    ComPtr<ID2D1Bitmap1> stagingBmp;
    if (FAILED(dc->CreateBitmap(cropSize, nullptr, 0, &stagingProps, &stagingBmp)) || !stagingBmp) {
        return result;
    }

    if (FAILED(stagingBmp->CopyFromBitmap(nullptr, offscreenTarget.Get(), nullptr))) {
        return result;
    }

    D2D1_MAPPED_RECT mapped{};
    if (FAILED(stagingBmp->Map(D2D1_MAP_OPTIONS_READ, &mapped)) || mapped.bits == nullptr) {
        return result;
    }

    result.width = cropW;
    result.height = cropH;
    result.stride = cropW * 4u;
    result.bgraPixels.resize(static_cast<size_t>(result.stride) * cropH);

    for (uint32_t y = 0; y < cropH; ++y) {
        const uint8_t* srcRow = mapped.bits + static_cast<size_t>(y) * mapped.pitch;
        uint8_t* dstRow = result.bgraPixels.data() + static_cast<size_t>(y) * result.stride;
        for (uint32_t x = 0; x < cropW; ++x) {
            const size_t i = static_cast<size_t>(x) * 4u;
            dstRow[i + 0] = srcRow[i + 0];
            dstRow[i + 1] = srcRow[i + 1];
            dstRow[i + 2] = srcRow[i + 2];
            dstRow[i + 3] = 255;
        }
    }

    stagingBmp->Unmap();

    const UINT bufSize = static_cast<UINT>(result.bgraPixels.size());
    if (FAILED(wic->CreateBitmapFromMemory(
            cropW,
            cropH,
            GUID_WICPixelFormat32bppBGRA,
            result.stride,
            bufSize,
            result.bgraPixels.data(),
            &result.wicBitmap))) {
        result = ExportedImage{};
        return result;
    }

    return result;
}

bool SaveWicBitmapToFile(
    IWICImagingFactory* wicFactory,
    IWICBitmap* bitmap,
    const std::wstring& filePath,
    ImageFileFormat format) {
    if (wicFactory == nullptr || bitmap == nullptr || filePath.empty()) {
        return false;
    }

    ComPtr<IWICStream> stream;
    if (FAILED(wicFactory->CreateStream(&stream)) || !stream) {
        return false;
    }
    if (FAILED(stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE))) {
        return false;
    }

    const GUID containerGuid = (format == ImageFileFormat::Jpeg)
        ? GUID_ContainerFormatJpeg
        : GUID_ContainerFormatPng;

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(wicFactory->CreateEncoder(containerGuid, nullptr, &encoder)) || !encoder) {
        return false;
    }
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(encoder->CreateNewFrame(&frame, &props)) || !frame) {
        return false;
    }

    if (format == ImageFileFormat::Jpeg && props) {
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT varValue{};
        VariantInit(&varValue);
        varValue.vt = VT_R4;
        varValue.fltVal = 0.92f;
        props->Write(1, &option, &varValue);
    }

    if (FAILED(frame->Initialize(props.Get()))) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(bitmap->GetSize(&width, &height)) || width == 0 || height == 0) {
        return false;
    }
    if (FAILED(frame->SetSize(width, height))) {
        return false;
    }

    if (format == ImageFileFormat::Jpeg) {
        WICPixelFormatGUID pixelFmt = GUID_WICPixelFormat24bppBGR;
        if (FAILED(frame->SetPixelFormat(&pixelFmt))) {
            return false;
        }
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(wicFactory->CreateFormatConverter(&converter)) || !converter) {
            return false;
        }
        if (FAILED(converter->Initialize(
                bitmap,
                pixelFmt,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom))) {
            return false;
        }
        if (FAILED(frame->WriteSource(converter.Get(), nullptr))) {
            return false;
        }
    } else {
        WICPixelFormatGUID pixelFmt = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&pixelFmt))) {
            return false;
        }
        if (FAILED(frame->WriteSource(bitmap, nullptr))) {
            return false;
        }
    }

    if (FAILED(frame->Commit())) {
        return false;
    }
    return SUCCEEDED(encoder->Commit());
}

std::wstring BuildDefaultFilename() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t buf[64]{};
    std::swprintf(buf, ARRAYSIZE(buf), L"SayCheese_%04u%02u%02u_%02u%02u%02u.png",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

bool PromptAndSaveImage(
    HWND ownerHwnd,
    IWICImagingFactory* wicFactory,
    IWICBitmap* bitmap,
    std::wstring& inOutLastDirectory,
    std::wstring* outSavedPath) {
    if (wicFactory == nullptr || bitmap == nullptr) {
        return false;
    }

    wchar_t fileBuf[MAX_PATH * 2]{};
    const std::wstring defName = BuildDefaultFilename();
    ::wcsncpy_s(fileBuf, ARRAYSIZE(fileBuf), defName.c_str(), _TRUNCATE);

    static constexpr wchar_t kFilter[] =
        L"PNG Image (*.png)\0*.png\0JPEG Image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerHwnd;
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = ARRAYSIZE(fileBuf);
    ofn.lpstrInitialDir = inOutLastDirectory.empty() ? nullptr : inOutLastDirectory.c_str();
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!::GetSaveFileNameW(&ofn)) {
        return false;
    }

    std::wstring chosenPath(fileBuf);
    ImageFileFormat fmt = (ofn.nFilterIndex == 2) ? ImageFileFormat::Jpeg : ImageFileFormat::Png;

    const std::wstring lower = ToLowerCopy(chosenPath);
    if (lower.ends_with(L".jpg") || lower.ends_with(L".jpeg")) {
        fmt = ImageFileFormat::Jpeg;
    } else if (lower.ends_with(L".png")) {
        fmt = ImageFileFormat::Png;
    }

    if (!SaveWicBitmapToFile(wicFactory, bitmap, chosenPath, fmt)) {
        return false;
    }

    const size_t slashPos = chosenPath.find_last_of(L"\\/");
    if (slashPos != std::wstring::npos) {
        inOutLastDirectory = chosenPath.substr(0, slashPos);
    }
    if (outSavedPath != nullptr) {
        *outSavedPath = std::move(chosenPath);
    }
    return true;
}

} // namespace saycheese::export_img
