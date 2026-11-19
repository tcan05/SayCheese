#pragma once

#include <windows.h>

#include <vector>

namespace saycheese::platform {

struct MonitorInfo {
    RECT virtualRect{0, 0, 0, 0};
    RECT canvasRect{0, 0, 0, 0};
    UINT dpiX = 96;
    UINT dpiY = 96;
    bool isPrimary = false;

    float Scale() const noexcept {
        return dpiX > 0 ? static_cast<float>(dpiX) / 96.0f : 1.0f;
    }
};

struct VirtualDesktopMetrics {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    std::vector<MonitorInfo> monitors;
};

VirtualDesktopMetrics QueryVirtualDesktopMetrics();

POINT VirtualToCanvas(POINT pt, int originX, int originY) noexcept;

const MonitorInfo* FindMonitorContainingCanvasPoint(
    const std::vector<MonitorInfo>& monitors,
    float x,
    float y) noexcept;

} // namespace saycheese::platform
