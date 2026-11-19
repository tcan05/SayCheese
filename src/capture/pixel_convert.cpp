#include "capture/pixel_convert.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace saycheese::capture {

namespace {

uint8_t LinearScRgbToSrgb8(float linearVal, float sdrWhiteMultiplier) noexcept {
    if (std::isnan(linearVal) || linearVal <= 0.0f) {
        return 0;
    }
    if (std::isinf(linearVal)) {
        return 255;
    }

    const float denom = std::max(0.1f, sdrWhiteMultiplier);
    float x = linearVal / denom;

    // Preserve SDR [0, 0.92] faithfully; softly roll off HDR highlights (>0.92) into (0.92, 1.0].
    constexpr float kKnee = 0.92f;
    if (x > kKnee) {
        x = kKnee + (1.0f - kKnee) * (1.0f - std::exp(-(x - kKnee) * 3.0f));
    }
    x = std::clamp(x, 0.0f, 1.0f);

    float srgb = 0.0f;
    if (x <= 0.0031308f) {
        srgb = 12.92f * x;
    } else {
        srgb = 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
    }
    const int quantized = static_cast<int>(std::lround(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
    return static_cast<uint8_t>(std::clamp(quantized, 0, 255));
}

void DecodePixelToBgra8(
    const uint8_t* rowPtr,
    int sx,
    SurfaceFormat format,
    float sdrWhiteMultiplier,
    uint8_t& outB,
    uint8_t& outG,
    uint8_t& outR) noexcept {
    switch (format) {
    case SurfaceFormat::BGRA8: {
        const uint8_t* p = rowPtr + static_cast<size_t>(sx) * 4u;
        outB = p[0];
        outG = p[1];
        outR = p[2];
        break;
    }
    case SurfaceFormat::RGBA8: {
        const uint8_t* p = rowPtr + static_cast<size_t>(sx) * 4u;
        outR = p[0];
        outG = p[1];
        outB = p[2];
        break;
    }
    case SurfaceFormat::R10G10B10A2: {
        uint32_t packed = 0;
        std::memcpy(&packed, rowPtr + static_cast<size_t>(sx) * 4u, sizeof(packed));
        const uint32_t r10 = packed & 0x3FFu;
        const uint32_t g10 = (packed >> 10) & 0x3FFu;
        const uint32_t b10 = (packed >> 20) & 0x3FFu;
        outR = static_cast<uint8_t>((r10 * 255u + 511u) / 1023u);
        outG = static_cast<uint8_t>((g10 * 255u + 511u) / 1023u);
        outB = static_cast<uint8_t>((b10 * 255u + 511u) / 1023u);
        break;
    }
    case SurfaceFormat::RGBA16F: {
        uint16_t comps[4]{};
        std::memcpy(comps, rowPtr + static_cast<size_t>(sx) * 8u, sizeof(comps));
        outR = LinearScRgbToSrgb8(HalfToFloat(comps[0]), sdrWhiteMultiplier);
        outG = LinearScRgbToSrgb8(HalfToFloat(comps[1]), sdrWhiteMultiplier);
        outB = LinearScRgbToSrgb8(HalfToFloat(comps[2]), sdrWhiteMultiplier);
        break;
    }
    }
}

} // namespace

float HalfToFloat(uint16_t h) noexcept {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;

    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            // Normalize subnormal binary16.
            uint32_t m = mant;
            int32_t e = -14;
            while ((m & 0x0400u) == 0) {
                m <<= 1;
                --e;
            }
            m &= 0x03FFu;
            bits = sign | (static_cast<uint32_t>(e + 127) << 23) | (m << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127u - 15u)) << 23) | (mant << 13);
    }

    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

SurfaceRotation ResolveCaptureRotation(
    int stagedWidth,
    int stagedHeight,
    int desktopWidth,
    int desktopHeight,
    SurfaceRotation reportedRotation) noexcept {
    if (reportedRotation == SurfaceRotation::Rotate180) {
        return SurfaceRotation::Rotate180;
    }
    if ((reportedRotation == SurfaceRotation::Rotate90 ||
         reportedRotation == SurfaceRotation::Rotate270) &&
        (stagedWidth != desktopWidth || stagedHeight != desktopHeight)) {
        return reportedRotation;
    }
    return SurfaceRotation::Identity;
}

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
    CapturedFrame& dstFrame) noexcept {
    if (srcData == nullptr || srcWidth <= 0 || srcHeight <= 0 || srcRowPitch <= 0 || !dstFrame.IsValid()) {
        return;
    }

    // Fast path for unrotated BGRA8.
    if (format == SurfaceFormat::BGRA8 && rotation == SurfaceRotation::Identity) {
        const int xStart = std::max(0, -dstOffsetX);
        const int yStart = std::max(0, -dstOffsetY);
        const int xEnd = std::min(srcWidth, dstFrame.width - dstOffsetX);
        const int yEnd = std::min(srcHeight, dstFrame.height - dstOffsetY);
        if (xStart >= xEnd || yStart >= yEnd) {
            return;
        }
        for (int sy = yStart; sy < yEnd; ++sy) {
            const uint8_t* srcRow = srcData + static_cast<size_t>(sy) * static_cast<size_t>(srcRowPitch);
            uint8_t* dstRow = dstFrame.bgraPixels.data() +
                              static_cast<size_t>(sy + dstOffsetY) * static_cast<size_t>(dstFrame.stride);
            for (int sx = xStart; sx < xEnd; ++sx) {
                const size_t si = static_cast<size_t>(sx) * 4u;
                const size_t di = static_cast<size_t>(sx + dstOffsetX) * 4u;
                dstRow[di + 0] = srcRow[si + 0];
                dstRow[di + 1] = srcRow[si + 1];
                dstRow[di + 2] = srcRow[si + 2];
                dstRow[di + 3] = 255;
            }
        }
        return;
    }

    for (int sy = 0; sy < srcHeight; ++sy) {
        const uint8_t* srcRow = srcData + static_cast<size_t>(sy) * static_cast<size_t>(srcRowPitch);
        for (int sx = 0; sx < srcWidth; ++sx) {
            int rx = sx;
            int ry = sy;
            switch (rotation) {
            case SurfaceRotation::Identity:
                break;
            case SurfaceRotation::Rotate90:
                rx = (srcHeight - 1) - sy;
                ry = sx;
                break;
            case SurfaceRotation::Rotate180:
                rx = (srcWidth - 1) - sx;
                ry = (srcHeight - 1) - sy;
                break;
            case SurfaceRotation::Rotate270:
                rx = sy;
                ry = (srcWidth - 1) - sx;
                break;
            }

            const int dx = dstOffsetX + rx;
            const int dy = dstOffsetY + ry;
            if (dx < 0 || dy < 0 || dx >= dstFrame.width || dy >= dstFrame.height) {
                continue;
            }

            uint8_t b = 0, g = 0, r = 0;
            DecodePixelToBgra8(srcRow, sx, format, sdrWhiteMultiplier, b, g, r);

            uint8_t* dstPx = dstFrame.bgraPixels.data() +
                             static_cast<size_t>(dy) * static_cast<size_t>(dstFrame.stride) +
                             static_cast<size_t>(dx) * 4u;
            dstPx[0] = b;
            dstPx[1] = g;
            dstPx[2] = r;
            dstPx[3] = 255;
        }
    }
}

CapturedFrame CreateSyntheticTestFrame(int width, int height) {
    CapturedFrame frame{};
    if (width <= 0 || height <= 0) {
        return frame;
    }
    frame.originX = 0;
    frame.originY = 0;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.backend = CaptureBackend::Synthetic;
    frame.bgraPixels.resize(static_cast<size_t>(frame.stride) * static_cast<size_t>(height));

    for (int y = 0; y < height; ++y) {
        uint8_t* row = frame.bgraPixels.data() + static_cast<size_t>(y) * static_cast<size_t>(frame.stride);
        for (int x = 0; x < width; ++x) {
            const bool checker = (((x / 32) + (y / 32)) & 1) != 0;
            const uint8_t b = static_cast<uint8_t>((x * 200) / std::max(1, width) + (checker ? 30 : 10));
            const uint8_t g = static_cast<uint8_t>((y * 180) / std::max(1, height) + (checker ? 40 : 15));
            const uint8_t r = static_cast<uint8_t>(checker ? 90 : 45);
            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = 255;
        }
    }

    platform::MonitorInfo mon{};
    mon.virtualRect = RECT{0, 0, width, height};
    mon.canvasRect = RECT{0, 0, width, height};
    mon.dpiX = 96;
    mon.dpiY = 96;
    mon.isPrimary = true;
    frame.monitors.push_back(mon);
    return frame;
}

} // namespace saycheese::capture
