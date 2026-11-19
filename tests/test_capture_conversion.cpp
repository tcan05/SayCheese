#include "test_runner.hpp"

#include "capture/captured_frame.hpp"
#include "capture/pixel_convert.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace saycheese::capture;

namespace {

uint16_t FloatToHalf(float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exp = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mant = bits & 0x007FFFFFu;
    if (((bits >> 23) & 0xFFu) == 0xFFu) {
        // Inf / NaN
        return static_cast<uint16_t>(sign | 0x7C00u | (mant != 0 ? 0x0200u : 0u));
    }
    if (exp <= 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

CapturedFrame MakeBlankFrame(int w, int h) {
    CapturedFrame f{};
    f.width = w;
    f.height = h;
    f.stride = w * 4;
    f.bgraPixels.assign(static_cast<size_t>(f.stride) * static_cast<size_t>(h), 0);
    return f;
}

void ExpectBgraAt(const CapturedFrame& f, int x, int y, uint8_t b, uint8_t g, uint8_t r, uint8_t a, int tol = 0) {
    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(f.stride) + static_cast<size_t>(x) * 4u;
    SC_EXPECT_NEAR(f.bgraPixels[idx + 0], b, tol);
    SC_EXPECT_NEAR(f.bgraPixels[idx + 1], g, tol);
    SC_EXPECT_NEAR(f.bgraPixels[idx + 2], r, tol);
    SC_EXPECT_EQ(static_cast<int>(f.bgraPixels[idx + 3]), static_cast<int>(a));
}

} // namespace

SC_TEST("BGRA8 and RGBA8 conversions preserve channels and force opaque alpha") {
    CapturedFrame dst = MakeBlankFrame(4, 2);

    const uint8_t bgraSrc[8] = {10, 20, 30, 0, 40, 50, 60, 128};
    BlitConvertedSurfaceToFrame(bgraSrc, 2, 1, 8, SurfaceFormat::BGRA8, SurfaceRotation::Identity, 1.0f, 0, 0, dst);
    ExpectBgraAt(dst, 0, 0, 10, 20, 30, 255);
    ExpectBgraAt(dst, 1, 0, 40, 50, 60, 255);

    const uint8_t rgbaSrc[8] = {200, 100, 50, 0, 10, 20, 30, 12};
    BlitConvertedSurfaceToFrame(rgbaSrc, 2, 1, 8, SurfaceFormat::RGBA8, SurfaceRotation::Identity, 1.0f, 2, 1, dst);
    ExpectBgraAt(dst, 2, 1, 50, 100, 200, 255);
    ExpectBgraAt(dst, 3, 1, 30, 20, 10, 255);
}

SC_TEST("R10G10B10A2 conversion maps 10-bit channels accurately to 8-bit BGRA") {
    CapturedFrame dst = MakeBlankFrame(2, 1);
    const uint32_t p0 = (1023u << 0) | (512u << 10) | (0u << 20) | (0u << 30);    // R=1023, G=512, B=0
    const uint32_t p1 = (0u << 0) | (1023u << 10) | (1023u << 20) | (3u << 30);   // R=0, G=1023, B=1023
    const uint32_t src[2] = {p0, p1};

    BlitConvertedSurfaceToFrame(
        reinterpret_cast<const uint8_t*>(src), 2, 1, 8,
        SurfaceFormat::R10G10B10A2, SurfaceRotation::Identity, 1.0f, 0, 0, dst);

    ExpectBgraAt(dst, 0, 0, 0, 128, 255, 255, 1);
    ExpectBgraAt(dst, 1, 0, 255, 255, 0, 255, 1);
}

SC_TEST("RGBA16F HDR scRGB conversion handles 0, 1.0, HDR > 1.0, negative, NaN, and Inf safely") {
    CapturedFrame dst = MakeBlankFrame(4, 1);
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    const float infVal = std::numeric_limits<float>::infinity();

    const uint16_t src[16] = {
        FloatToHalf(0.0f), FloatToHalf(0.0f), FloatToHalf(0.0f), FloatToHalf(0.0f),       // Black
        FloatToHalf(1.0f), FloatToHalf(1.0f), FloatToHalf(1.0f), FloatToHalf(1.0f),       // SDR white
        FloatToHalf(4.0f), FloatToHalf(-0.5f), FloatToHalf(0.214f), FloatToHalf(1.0f),    // HDR high + negative + mid
        FloatToHalf(nanVal), FloatToHalf(infVal), FloatToHalf(-infVal), FloatToHalf(0.0f) // Special floats
    };

    BlitConvertedSurfaceToFrame(
        reinterpret_cast<const uint8_t*>(src), 4, 1, 32,
        SurfaceFormat::RGBA16F, SurfaceRotation::Identity, 1.0f, 0, 0, dst);

    ExpectBgraAt(dst, 0, 0, 0, 0, 0, 255);
    // SDR white (1.0) maps cleanly near white (>200 even under soft rolloff, 255 with linear clamp).
    SC_EXPECT_TRUE(dst.bgraPixels[4 + 0] >= 200);
    SC_EXPECT_TRUE(dst.bgraPixels[4 + 1] >= 200);
    SC_EXPECT_TRUE(dst.bgraPixels[4 + 2] >= 200);
    SC_EXPECT_EQ(static_cast<int>(dst.bgraPixels[4 + 3]), 255);

    // Pixel 2: R=4.0 (bright), G=-0.5 -> 0, B=0.214 linear -> ~127 sRGB
    SC_EXPECT_TRUE(dst.bgraPixels[8 + 2] >= 235);
    SC_EXPECT_EQ(static_cast<int>(dst.bgraPixels[8 + 1]), 0);
    SC_EXPECT_TRUE(dst.bgraPixels[8 + 0] >= 100 && dst.bgraPixels[8 + 0] <= 155);
    SC_EXPECT_EQ(static_cast<int>(dst.bgraPixels[8 + 3]), 255);

    // Pixel 3: NaN -> 0, +Inf -> 255, -Inf -> 0, alpha -> 255
    ExpectBgraAt(dst, 3, 0, 0, 255, 0, 255);
}

SC_TEST("Monitor rotations 90/180/270 transpose pixels cleanly and clip safely at edges") {
    // 2x3 source surface (srcW=2, srcH=3) with unique corner markers:
    // (0,0)=Red, (1,0)=Green, (0,2)=Blue, (1,2)=White
    uint8_t src[3 * 2 * 4]{};
    auto setSrc = [&](int x, int y, uint8_t b, uint8_t g, uint8_t r) {
        const size_t i = static_cast<size_t>(y * 2 + x) * 4u;
        src[i + 0] = b; src[i + 1] = g; src[i + 2] = r; src[i + 3] = 255;
    };
    setSrc(0, 0, 0, 0, 255);     // Red
    setSrc(1, 0, 0, 255, 0);     // Green
    setSrc(0, 2, 255, 0, 0);     // Blue
    setSrc(1, 2, 255, 255, 255); // White

    // Rotate90: desktop output is 3x2 (dstW=srcH=3, dstH=srcW=2)
    // Top-left of desktop (0,0) comes from src (0, srcH-1) = Blue; (2,0) from src (0,0) = Red.
    CapturedFrame rot90 = MakeBlankFrame(3, 2);
    BlitConvertedSurfaceToFrame(src, 2, 3, 8, SurfaceFormat::BGRA8, SurfaceRotation::Rotate90, 1.0f, 0, 0, rot90);
    ExpectBgraAt(rot90, 0, 0, 255, 0, 0, 255);     // Blue
    ExpectBgraAt(rot90, 2, 0, 0, 0, 255, 255);     // Red
    ExpectBgraAt(rot90, 2, 1, 0, 255, 0, 255);     // Green
    ExpectBgraAt(rot90, 0, 1, 255, 255, 255, 255); // White

    // Rotate180: desktop is 2x3; (0,0) comes from src (1,2) = White
    CapturedFrame rot180 = MakeBlankFrame(2, 3);
    BlitConvertedSurfaceToFrame(src, 2, 3, 8, SurfaceFormat::BGRA8, SurfaceRotation::Rotate180, 1.0f, 0, 0, rot180);
    ExpectBgraAt(rot180, 0, 0, 255, 255, 255, 255);
    ExpectBgraAt(rot180, 1, 2, 0, 0, 255, 255);

    // Rotate270: desktop is 3x2; (0,0) comes from src (1,0) = Green
    CapturedFrame rot270 = MakeBlankFrame(3, 2);
    BlitConvertedSurfaceToFrame(src, 2, 3, 8, SurfaceFormat::BGRA8, SurfaceRotation::Rotate270, 1.0f, 0, 0, rot270);
    ExpectBgraAt(rot270, 0, 0, 0, 255, 0, 255);     // Green
    ExpectBgraAt(rot270, 0, 1, 0, 0, 255, 255);     // Red
    ExpectBgraAt(rot270, 2, 0, 255, 255, 255, 255); // White
    ExpectBgraAt(rot270, 2, 1, 255, 0, 0, 255);     // Blue

    // Out-of-bounds offset clips cleanly without memory corruption.
    CapturedFrame tiny = MakeBlankFrame(2, 2);
    BlitConvertedSurfaceToFrame(src, 2, 3, 8, SurfaceFormat::BGRA8, SurfaceRotation::Identity, 1.0f, 1, 1, tiny);
    ExpectBgraAt(tiny, 1, 1, 0, 0, 255, 255); // src (0,0) landed at (1,1)
}

SC_TEST("Negative-origin multi-monitor compositing places left and primary monitors side-by-side") {
    CapturedFrame virtualFrame = MakeBlankFrame(4, 1);
    virtualFrame.originX = -2;
    virtualFrame.originY = 0;

    // Left monitor at virtual (-2, 0) -> offset (0, 0)
    const uint8_t leftMon[8] = {10, 20, 30, 255, 11, 21, 31, 255};
    // Primary monitor at virtual (0, 0) -> offset (2, 0)
    const uint8_t rightMon[8] = {200, 150, 100, 255, 201, 151, 101, 255};

    BlitConvertedSurfaceToFrame(leftMon, 2, 1, 8, SurfaceFormat::BGRA8, SurfaceRotation::Identity, 1.0f, 0, 0, virtualFrame);
    BlitConvertedSurfaceToFrame(rightMon, 2, 1, 8, SurfaceFormat::BGRA8, SurfaceRotation::Identity, 1.0f, 2, 0, virtualFrame);

    ExpectBgraAt(virtualFrame, 0, 0, 10, 20, 30, 255);
    ExpectBgraAt(virtualFrame, 2, 0, 200, 150, 100, 255);
}

SC_TEST("ResolveCaptureRotation preserves Rotate180 even when staged dimensions match desktop dimensions") {
    SC_EXPECT_EQ(static_cast<int>(ResolveCaptureRotation(1920, 1080, 1920, 1080, SurfaceRotation::Rotate180)),
                 static_cast<int>(SurfaceRotation::Rotate180));
    SC_EXPECT_EQ(static_cast<int>(ResolveCaptureRotation(1920, 1080, 1080, 1920, SurfaceRotation::Rotate90)),
                 static_cast<int>(SurfaceRotation::Rotate90));
    SC_EXPECT_EQ(static_cast<int>(ResolveCaptureRotation(1920, 1080, 1080, 1920, SurfaceRotation::Rotate270)),
                 static_cast<int>(SurfaceRotation::Rotate270));
    // Driver already pre-rotated portrait texture: dimensions match desktop -> Identity.
    SC_EXPECT_EQ(static_cast<int>(ResolveCaptureRotation(1080, 1920, 1080, 1920, SurfaceRotation::Rotate90)),
                 static_cast<int>(SurfaceRotation::Identity));
    SC_EXPECT_EQ(static_cast<int>(ResolveCaptureRotation(1920, 1080, 1920, 1080, SurfaceRotation::Identity)),
                 static_cast<int>(SurfaceRotation::Identity));
}

int main() {
    return saycheese::test::RunAll("capture_conversion");
}
