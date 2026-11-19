#pragma once

#include "capture/captured_frame.hpp"

#include <cstdint>

namespace saycheese::capture {

enum class SurfaceFormat {
    BGRA8 = 0,
    RGBA8,
    R10G10B10A2,
    RGBA16F
};

enum class SurfaceRotation {
    Identity = 0,
    Rotate90,
    Rotate180,
    Rotate270
};

float HalfToFloat(uint16_t h) noexcept;

void BlitConvertedSurfaceToFrame(
    const uint8_t* srcData,
    int srcWidth,
    int srcHeight,
    int srcRowPitch,
    SurfaceFormat format,
    SurfaceRotation rotation,
    float sdrWhiteMultiplier,
    int dstOffsetX,
    int dstOffsetY,
    CapturedFrame& dstFrame) noexcept;

} // namespace saycheese::capture
