#include "capture/gdi_capture.hpp"

#include "capture/pixel_convert.hpp"
#include "platform/win32_raii.hpp"

#include <windows.h>

namespace saycheese::capture {

bool CaptureRegionBitBlt(
    int virtualX,
    int virtualY,
    int width,
    int height,
    int dstOffsetX,
    int dstOffsetY,
    CapturedFrame& dstFrame) {
    if (width <= 0 || height <= 0 || !dstFrame.IsValid()) {
        return false;
    }

    platform::UniqueWindowDc screenDc(nullptr, ::GetDC(nullptr));
    if (!screenDc) {
        return false;
    }

    platform::UniqueHdc memDc(::CreateCompatibleDC(screenDc.Get()));
    if (!memDc) {
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* dibBits = nullptr;
    platform::UniqueHbitmap dib(::CreateDIBSection(
        memDc.Get(), &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0));
    if (!dib || dibBits == nullptr) {
        return false;
    }

    platform::SelectObjectGuard selectGuard(memDc.Get(), dib.Get());
    if (!selectGuard.IsValid()) {
        return false;
    }

    if (!::BitBlt(memDc.Get(), 0, 0, width, height,
                  screenDc.Get(), virtualX, virtualY, SRCCOPY | CAPTUREBLT)) {
        return false;
    }

    BlitConvertedSurfaceToFrame(
        static_cast<const uint8_t*>(dibBits),
        width,
        height,
        width * 4,
        SurfaceFormat::BGRA8,
        SurfaceRotation::Identity,
        1.0f,
        dstOffsetX,
        dstOffsetY,
        dstFrame);
    return true;
}

} // namespace saycheese::capture
