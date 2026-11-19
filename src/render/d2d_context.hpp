#pragma once

#include "capture/captured_frame.hpp"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>

namespace saycheese::render {

class D2DContext {
public:
    D2DContext() = default;
    ~D2DContext() noexcept { Reset(); }

    D2DContext(const D2DContext&) = delete;
    D2DContext& operator=(const D2DContext&) = delete;

    bool InitializeForWindow(HWND hwnd, uint32_t width, uint32_t height, bool forceWarp = false);
    bool InitializeHeadless(bool forceWarp = true);
    void Reset() noexcept;

    bool UploadDesktopBitmap(const capture::CapturedFrame& frame);
    HRESULT Present();

    ID3D11Device* D3DDevice() const noexcept { return d3dDevice_.Get(); }
    ID2D1Factory1* D2DFactory() const noexcept { return d2dFactory_.Get(); }
    ID2D1Device* D2DDevice() const noexcept { return d2dDevice_.Get(); }
    ID2D1DeviceContext* DeviceContext() const noexcept { return d2dContext_.Get(); }
    IDWriteFactory* DWriteFactory() const noexcept { return dwriteFactory_.Get(); }
    IWICImagingFactory* WicFactory() const noexcept { return wicFactory_.Get(); }
    ID2D1Bitmap1* DesktopBitmap() const noexcept { return desktopBitmap_.Get(); }
    ID2D1Bitmap1* SwapChainTargetBitmap() const noexcept { return swapTargetBitmap_.Get(); }
    bool IsWarp() const noexcept { return usingWarp_; }

    static bool IsDeviceLostError(HRESULT hr) noexcept;

private:
    bool CreateCoreDevices(bool forceWarp);

    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> swapTargetBitmap_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> desktopBitmap_;

    bool usingWarp_ = false;
};

} // namespace saycheese::render
