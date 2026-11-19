#pragma once

#include "capture/captured_frame.hpp"

namespace saycheese::capture {

bool CaptureRegionBitBlt(
    int virtualX,
    int virtualY,
    int width,
    int height,
    int dstOffsetX,
    int dstOffsetY,
    CapturedFrame& dstFrame);

} // namespace saycheese::capture
