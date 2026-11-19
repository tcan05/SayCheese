#pragma once

#include "platform/dpi_monitor.hpp"

#include <cstdint>
#include <vector>

namespace saycheese::capture {

enum class CaptureBackend {
    None = 0,
    DxgiDesktopDuplication,
    DxgiPartialWithGdiFallback,
    GdiBitBltFallback,
    Synthetic
};

struct CapturedFrame {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<uint8_t> bgraPixels;
    std::vector<platform::MonitorInfo> monitors;
    CaptureBackend backend = CaptureBackend::None;

    bool IsValid() const noexcept {
        return width > 0 && height > 0 && stride >= width * 4 &&
               bgraPixels.size() >= static_cast<size_t>(stride) * static_cast<size_t>(height);
    }
};

CapturedFrame CreateSyntheticTestFrame(int width, int height);

} // namespace saycheese::capture
