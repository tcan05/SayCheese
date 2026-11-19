#pragma once

#include "capture/captured_frame.hpp"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace saycheese::capture {

struct DxgiCaptureSummary {
    int outputsSucceeded = 0;
    int outputsAttempted = 0;
    std::vector<RECT> capturedVirtualRects;
};

bool IsDxgiDesktopFrameUpdated(int64_t lastPresentTime) noexcept;
bool AnyActiveMonitorUsesHdr() noexcept;
DxgiCaptureSummary CaptureMonitorsDxgi(CapturedFrame& dstFrame, bool hdrOutputsOnly = false);

} // namespace saycheese::capture
