#include "render/d2d_context.hpp"

#include <d2d1_1helper.h>
#include <dxgi1_3.h>

namespace saycheese::render {

namespace {

using Microsoft::WRL::ComPtr;

HRESULT CreateD3DDeviceInternal(
    D3D_DRIVER_TYPE driverType,
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
        nullptr,
        driverType,
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
            nullptr,
            driverType,
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

} // namespace

bool D2DContext::CreateCoreDevices(bool forceWarp) {
    Reset();

    D2D1_FACTORY_OPTIONS opts{};
    opts.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                   &opts, &d2dFactory_)) || !d2dFactory_) {
        Reset();
        return false;
    }

    if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()))) ||
        !dwriteFactory_) {
        Reset();
        return false;
    }

    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&wicFactory_))) || !wicFactory_) {
        Reset();
        return false;
    }

    HRESULT hr = E_FAIL;
    if (!forceWarp) {
        hr = CreateD3DDeviceInternal(D3D_DRIVER_TYPE_HARDWARE, d3dDevice_, d3dContext_);
        usingWarp_ = false;
    }
    if (FAILED(hr) || !d3dDevice_) {
        hr = CreateD3DDeviceInternal(D3D_DRIVER_TYPE_WARP, d3dDevice_, d3dContext_);
        usingWarp_ = true;
    }
    if (FAILED(hr) || !d3dDevice_ || !d3dContext_) {
        Reset();
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3dDevice_.As(&dxgiDevice)) || !dxgiDevice) {
        Reset();
        return false;
    }

    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_)) || !d2dDevice_) {
        Reset();
        return false;
    }

    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_)) ||
        !d2dContext_) {
        Reset();
        return false;
    }

    // Lock 1 DIP == 1 physical pixel across mixed-DPI virtual desktops.
    d2dContext_->SetDpi(96.0f, 96.0f);
    d2dContext_->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
    d2dContext_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

bool D2DContext::InitializeHeadless(bool forceWarp) {
    return CreateCoreDevices(forceWarp);
}

bool D2DContext::InitializeForWindow(HWND hwnd, uint32_t width, uint32_t height, bool forceWarp) {
    if (hwnd == nullptr || width == 0 || height == 0) {
        return false;
    }
    if (!CreateCoreDevices(forceWarp)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(d3dDevice_.As(&dxgiDevice)) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)))) {
        Reset();
        return false;
    }

    // Opt out of Independent Flip / Hardware MPO / FreeSync handoff blackouts by using
    // DWM-composed Blt presentation with GDI compatibility when supported.
    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = width;
    scDesc.Height = height;
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 1;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;

    HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
        d3dDevice_.Get(), hwnd, &scDesc, nullptr, nullptr, &swapChain_);
    if (FAILED(hr) || !swapChain_) {
        scDesc.Flags = 0;
        hr = dxgiFactory->CreateSwapChainForHwnd(
            d3dDevice_.Get(), hwnd, &scDesc, nullptr, nullptr, &swapChain_);
    }
    if (FAILED(hr) || !swapChain_) {
        scDesc.BufferCount = 2;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        scDesc.Flags = 0;
        hr = dxgiFactory->CreateSwapChainForHwnd(
            d3dDevice_.Get(), hwnd, &scDesc, nullptr, nullptr, &swapChain_);
    }
    if (FAILED(hr) || !swapChain_) {
        Reset();
        return false;
    }

    dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    ComPtr<IDXGISurface> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) {
        Reset();
        return false;
    }

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);

    if (FAILED(d2dContext_->CreateBitmapFromDxgiSurface(backBuffer.Get(), &props, &swapTargetBitmap_)) ||
        !swapTargetBitmap_) {
        Reset();
        return false;
    }

    d2dContext_->SetTarget(swapTargetBitmap_.Get());
    return true;
}

bool D2DContext::UploadDesktopBitmap(const capture::CapturedFrame& frame) {
    if (!d2dContext_ || !frame.IsValid()) {
        return false;
    }
    desktopBitmap_.Reset();

    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(frame.width),
        static_cast<UINT32>(frame.height));

    // Use PREMULTIPLIED alpha (all pixels are 0xFF opaque) so D2D1 effects (GaussianBlur/Crop/Border) accept it.
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);

    return SUCCEEDED(d2dContext_->CreateBitmap(
        size,
        frame.bgraPixels.data(),
        static_cast<UINT32>(frame.stride),
        &props,
        &desktopBitmap_)) && desktopBitmap_ != nullptr;
}

HRESULT D2DContext::Present() {
    if (!swapChain_) {
        return S_OK;
    }
    return swapChain_->Present(0, 0);
}

void D2DContext::Reset() noexcept {
    if (d2dContext_) {
        d2dContext_->SetTarget(nullptr);
    }
    desktopBitmap_.Reset();
    swapTargetBitmap_.Reset();
    swapChain_.Reset();
    d2dContext_.Reset();
    if (d2dDevice_) {
        d2dDevice_->ClearResources(0);
    }
    d2dDevice_.Reset();
    if (d3dContext_) {
        d3dContext_->ClearState();
        d3dContext_->Flush();
    }
    if (d3dDevice_) {
        ComPtr<IDXGIDevice3> dxgiDev3;
        if (SUCCEEDED(d3dDevice_.As(&dxgiDev3)) && dxgiDev3) {
            dxgiDev3->Trim();
        }
    }
    d3dContext_.Reset();
    d3dDevice_.Reset();
    wicFactory_.Reset();
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
    usingWarp_ = false;
}

bool D2DContext::IsDeviceLostError(HRESULT hr) noexcept {
    return hr == D2DERR_RECREATE_TARGET ||
           hr == DXGI_ERROR_DEVICE_REMOVED ||
           hr == DXGI_ERROR_DEVICE_RESET;
}

} // namespace saycheese::render
