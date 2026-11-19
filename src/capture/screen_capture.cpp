#include "capture/screen_capture.hpp"

#include "capture/dxgi_capture.hpp"
#include "capture/gdi_capture.hpp"
#include "platform/dpi_monitor.hpp"

#include <algorithm>

namespace saycheese::capture {

namespace {

bool RectsEqual(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool WasMonitorCaptured(const RECT& monitorVirtualRect, const std::vector<RECT>& captured) noexcept {
    for (const auto& r : captured) {
        if (RectsEqual(monitorVirtualRect, r)) {
            return true;
        }
    }
    return false;
}

bool FinishDxgiWithPartialGdiFallback(CapturedFrame& frame, const DxgiCaptureSummary& dxgi) {
    if (dxgi.outputsSucceeded <= 0) {
        return false;
    }

    bool usedPartialGdi = false;
    for (const auto& mon : frame.monitors) {
        if (!WasMonitorCaptured(mon.virtualRect, dxgi.capturedVirtualRects)) {
            const int w = mon.virtualRect.right - mon.virtualRect.left;
            const int h = mon.virtualRect.bottom - mon.virtualRect.top;
            if (CaptureRegionBitBlt(mon.virtualRect.left, mon.virtualRect.top, w, h,
                                    mon.canvasRect.left, mon.canvasRect.top, frame)) {
                usedPartialGdi = true;
            }
        }
    }
    frame.backend = usedPartialGdi
        ? CaptureBackend::DxgiPartialWithGdiFallback
        : CaptureBackend::DxgiDesktopDuplication;
    return true;
}

} // namespace

CapturedFrame CaptureVirtualDesktop() {
    const platform::VirtualDesktopMetrics metrics = platform::QueryVirtualDesktopMetrics();
    if (metrics.width <= 0 || metrics.height <= 0) {
        return CapturedFrame{};
    }

    CapturedFrame frame{};
    frame.originX = metrics.originX;
    frame.originY = metrics.originY;
    frame.width = metrics.width;
    frame.height = metrics.height;
    frame.stride = metrics.width * 4;
    frame.monitors = metrics.monitors;
    frame.bgraPixels.assign(static_cast<size_t>(frame.stride) * static_cast<size_t>(frame.height), 0);

    // Fill alpha channel opaque upfront for any non-rectangular multi-monitor dead zones.
    for (size_t i = 3; i < frame.bgraPixels.size(); i += 4) {
        frame.bgraPixels[i] = 255;
    }

    const bool anyHdr = AnyActiveMonitorUsesHdr();
    if (!anyHdr) {
        // SDR fast path: instant GDI BitBlt avoids D3D11/DXGI device startup and AMD MPO/FreeSync stalls.
        if (CaptureRegionBitBlt(frame.originX, frame.originY, frame.width, frame.height, 0, 0, frame)) {
            frame.backend = CaptureBackend::GdiBitBltFallback;
            return frame;
        }
        const DxgiCaptureSummary dxgi = CaptureMonitorsDxgi(frame, false);
        if (FinishDxgiWithPartialGdiFallback(frame, dxgi)) {
            return frame;
        }
        return CapturedFrame{};
    }

    const DxgiCaptureSummary dxgi = CaptureMonitorsDxgi(frame, true);
    if (FinishDxgiWithPartialGdiFallback(frame, dxgi)) {
        return frame;
    }

    if (CaptureRegionBitBlt(frame.originX, frame.originY, frame.width, frame.height, 0, 0, frame)) {
        frame.backend = CaptureBackend::GdiBitBltFallback;
        return frame;
    }

    return CapturedFrame{};
}

} // namespace saycheese::capture
