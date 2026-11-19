#include "platform/dpi_monitor.hpp"

#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace saycheese::platform {

namespace {

struct MonitorEnumContext {
    int originX = 0;
    int originY = 0;
    std::vector<MonitorInfo>* monitors = nullptr;
};

BOOL CALLBACK EnumMonitorsCallback(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
    auto* ctx = reinterpret_cast<MonitorEnumContext*>(lParam);
    if (ctx == nullptr || ctx->monitors == nullptr) {
        return FALSE;
    }

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(hMonitor, &mi)) {
        return TRUE;
    }

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (FAILED(::GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || dpiX == 0 || dpiY == 0) {
        dpiX = 96;
        dpiY = 96;
    }

    MonitorInfo info{};
    info.virtualRect = mi.rcMonitor;
    info.canvasRect.left = mi.rcMonitor.left - ctx->originX;
    info.canvasRect.top = mi.rcMonitor.top - ctx->originY;
    info.canvasRect.right = mi.rcMonitor.right - ctx->originX;
    info.canvasRect.bottom = mi.rcMonitor.bottom - ctx->originY;
    info.dpiX = dpiX;
    info.dpiY = dpiY;
    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    ctx->monitors->push_back(info);
    return TRUE;
}

} // namespace

VirtualDesktopMetrics QueryVirtualDesktopMetrics() {
    VirtualDesktopMetrics metrics{};
    metrics.originX = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    metrics.originY = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    metrics.width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    metrics.height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (metrics.width <= 0 || metrics.height <= 0) {
        metrics.originX = 0;
        metrics.originY = 0;
        metrics.width = ::GetSystemMetrics(SM_CXSCREEN);
        metrics.height = ::GetSystemMetrics(SM_CYSCREEN);
    }

    MonitorEnumContext ctx{metrics.originX, metrics.originY, &metrics.monitors};
    ::EnumDisplayMonitors(nullptr, nullptr, &EnumMonitorsCallback, reinterpret_cast<LPARAM>(&ctx));

    if (metrics.monitors.empty() && metrics.width > 0 && metrics.height > 0) {
        MonitorInfo fallback{};
        fallback.virtualRect = RECT{metrics.originX, metrics.originY,
                                    metrics.originX + metrics.width, metrics.originY + metrics.height};
        fallback.canvasRect = RECT{0, 0, metrics.width, metrics.height};
        fallback.dpiX = 96;
        fallback.dpiY = 96;
        fallback.isPrimary = true;
        metrics.monitors.push_back(fallback);
    }

    return metrics;
}

POINT VirtualToCanvas(POINT pt, int originX, int originY) noexcept {
    return POINT{pt.x - originX, pt.y - originY};
}

const MonitorInfo* FindMonitorContainingCanvasPoint(
    const std::vector<MonitorInfo>& monitors,
    float x,
    float y) noexcept {
    if (monitors.empty()) {
        return nullptr;
    }

    const MonitorInfo* primary = nullptr;
    for (const auto& m : monitors) {
        if (m.isPrimary && primary == nullptr) {
            primary = &m;
        }
        if (x >= static_cast<float>(m.canvasRect.left) &&
            x < static_cast<float>(m.canvasRect.right) &&
            y >= static_cast<float>(m.canvasRect.top) &&
            y < static_cast<float>(m.canvasRect.bottom)) {
            return &m;
        }
    }

    if (primary != nullptr) {
        return primary;
    }

    // Fallback to nearest monitor center if no primary flag was marked.
    const MonitorInfo* best = &monitors.front();
    double bestDistSq = std::numeric_limits<double>::max();
    for (const auto& m : monitors) {
        const double cx = 0.5 * static_cast<double>(m.canvasRect.left + m.canvasRect.right);
        const double cy = 0.5 * static_cast<double>(m.canvasRect.top + m.canvasRect.bottom);
        const double dx = static_cast<double>(x) - cx;
        const double dy = static_cast<double>(y) - cy;
        const double distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best = &m;
        }
    }
    return best;
}

} // namespace saycheese::platform
