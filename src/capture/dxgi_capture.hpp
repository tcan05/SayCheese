#pragma once

#include "capture/captured_frame.hpp"

#include <windows.h>

#include <vector>

namespace saycheese::capture {

struct DxgiCaptureSummary {
    int outputsSucceeded = 0;
    int outputsAttempted = 0;
    std::vector<RECT> capturedVirtualRects;
};

DxgiCaptureSummary CaptureMonitorsDxgi(CapturedFrame& dstFrame);

} // namespace saycheese::capture
