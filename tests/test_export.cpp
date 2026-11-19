#include "test_runner.hpp"

#include "capture/captured_frame.hpp"
#include "export/clipboard_export.hpp"
#include "export/wic_exporter.hpp"
#include "platform/com_debug.hpp"
#include "platform/win32_raii.hpp"
#include "render/d2d_context.hpp"
#include "render/scene_renderer.hpp"
#include "tools/annotation.hpp"

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace saycheese::export_img;
using namespace saycheese::render;
using namespace saycheese::capture;
using namespace saycheese::tools;
using namespace saycheese::platform;

SC_TEST("BuildDibV5Payload and BuildDibPayload pack odd-width and 1x1 buffers bottom-up with opaque alpha") {
    // 137 x 91 odd-width buffer with distinct top/bottom row markers and zero source alpha.
    constexpr uint32_t kW = 137;
    constexpr uint32_t kH = 91;
    constexpr uint32_t kStride = kW * 4;
    std::vector<uint8_t> src(static_cast<size_t>(kStride) * kH, 0);
    // Top row (y = 0, x = 0): B=11, G=22, R=33, A=0
    src[0] = 11; src[1] = 22; src[2] = 33; src[3] = 0;
    // Bottom row (y = kH - 1, x = kW - 1): B=77, G=88, R=99, A=0
    const size_t lastPx = static_cast<size_t>(kH - 1) * kStride + static_cast<size_t>(kW - 1) * 4u;
    src[lastPx + 0] = 77; src[lastPx + 1] = 88; src[lastPx + 2] = 99; src[lastPx + 3] = 0;

    const std::vector<uint8_t> dibV5 = BuildDibV5Payload(kW, kH, kStride, src.data());
    SC_EXPECT_EQ(dibV5.size(), sizeof(BITMAPV5HEADER) + static_cast<size_t>(kW) * kH * 4u);

    BITMAPV5HEADER hdr5{};
    std::memcpy(&hdr5, dibV5.data(), sizeof(hdr5));
    SC_EXPECT_EQ(hdr5.bV5Size, static_cast<DWORD>(sizeof(BITMAPV5HEADER)));
    SC_EXPECT_EQ(hdr5.bV5Width, static_cast<LONG>(kW));
    SC_EXPECT_EQ(hdr5.bV5Height, static_cast<LONG>(kH)); // Positive = bottom-up
    SC_EXPECT_EQ(hdr5.bV5BitCount, static_cast<WORD>(32));
    SC_EXPECT_EQ(hdr5.bV5Compression, static_cast<DWORD>(BI_BITFIELDS));
    SC_EXPECT_EQ(hdr5.bV5RedMask, static_cast<DWORD>(0x00FF0000));
    SC_EXPECT_EQ(hdr5.bV5GreenMask, static_cast<DWORD>(0x0000FF00));
    SC_EXPECT_EQ(hdr5.bV5BlueMask, static_cast<DWORD>(0x000000FF));
    SC_EXPECT_EQ(hdr5.bV5AlphaMask, static_cast<DWORD>(0xFF000000));
    SC_EXPECT_EQ(hdr5.bV5CSType, static_cast<DWORD>(LCS_sRGB));

    // Source top row (y=0) must be stored at destination bottom-up row (kH - 1).
    const uint8_t* dib5Pixels = dibV5.data() + sizeof(BITMAPV5HEADER);
    const size_t dstTopRowOffset = static_cast<size_t>(kH - 1) * kStride;
    SC_EXPECT_EQ(static_cast<int>(dib5Pixels[dstTopRowOffset + 0]), 11);
    SC_EXPECT_EQ(static_cast<int>(dib5Pixels[dstTopRowOffset + 1]), 22);
    SC_EXPECT_EQ(static_cast<int>(dib5Pixels[dstTopRowOffset + 2]), 33);
    SC_EXPECT_EQ(static_cast<int>(dib5Pixels[dstTopRowOffset + 3]), 255);

    // Source bottom-right pixel must be at destination row 0, col kW - 1.
    const size_t dstBotRightOffset = static_cast<size_t>(kW - 1) * 4u;
    SC_EXPECT_EQ(static_cast<int>(dib5Pixels[dstBotRightOffset + 0]), 77);
    SC_EXPECT_EQ(static_cast<int>(dib5Pixels[dstBotRightOffset + 1]), 88);
    SC_EXPECT_EQ(static_cast<int>(dib5Pixels[dstBotRightOffset + 2]), 99);
    SC_EXPECT_EQ(static_cast<int>(dib5Pixels[dstBotRightOffset + 3]), 255);

    const std::vector<uint8_t> dib = BuildDibPayload(kW, kH, kStride, src.data());
    SC_EXPECT_EQ(dib.size(), sizeof(BITMAPINFOHEADER) + static_cast<size_t>(kW) * kH * 4u);
    BITMAPINFOHEADER hdr{};
    std::memcpy(&hdr, dib.data(), sizeof(hdr));
    SC_EXPECT_EQ(hdr.biSize, static_cast<DWORD>(sizeof(BITMAPINFOHEADER)));
    SC_EXPECT_EQ(hdr.biWidth, static_cast<LONG>(kW));
    SC_EXPECT_EQ(hdr.biHeight, static_cast<LONG>(kH));
    SC_EXPECT_EQ(hdr.biCompression, static_cast<DWORD>(BI_RGB));

    // 1x1 edge case
    const uint8_t px1[4] = {1, 2, 3, 0};
    const auto dib1 = BuildDibPayload(1, 1, 4, px1);
    SC_EXPECT_EQ(dib1.size(), sizeof(BITMAPINFOHEADER) + 4u);
    SC_EXPECT_EQ(static_cast<int>(dib1[sizeof(BITMAPINFOHEADER) + 3]), 255);
}

SC_TEST("Headless WARP WYSIWYG offscreen render exports PNG/JPEG cleanly with zero DXGI leaks") {
    ComInitScope comScope;
    SC_EXPECT_TRUE(comScope.Succeeded());

    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path();
    const std::filesystem::path pngPath = tmpDir / L"saycheese_test_export.png";
    const std::filesystem::path jpgPath = tmpDir / L"saycheese_test_export.jpg";
    std::error_code ec;
    std::filesystem::remove(pngPath, ec);
    std::filesystem::remove(jpgPath, ec);

    {
        D2DContext ctx;
        SC_EXPECT_TRUE(ctx.InitializeHeadless(true));

        const CapturedFrame frame = CreateSyntheticTestFrame(320, 240);
        SC_EXPECT_TRUE(ctx.UploadDesktopBitmap(frame));

        SceneRenderer renderer;
        const RectF crop{40.0f, 30.0f, 177.0f, 121.0f}; // 137 x 91 odd crop

        // Baseline unannotated export
        const ExportedImage baseline = RenderCropToWicBitmap(ctx, renderer, frame, crop, {});
        SC_EXPECT_TRUE(baseline.IsValid());
        SC_EXPECT_EQ(baseline.width, 137u);
        SC_EXPECT_EQ(baseline.height, 91u);

        // Full 7-tool annotation suite inside crop
        std::vector<Annotation> anns;
        Annotation pen{};
        pen.id = 1; pen.type = ToolType::Pen; pen.color = ColorRGBA{255, 0, 0, 255}; pen.strokeWidth = 4.0f;
        pen.points = {{50.0f, 40.0f}, {70.0f, 55.0f}, {95.0f, 45.0f}};
        anns.push_back(pen);

        Annotation line{};
        line.id = 2; line.type = ToolType::Line; line.color = ColorRGBA{0, 255, 0, 255}; line.strokeWidth = 3.0f;
        line.start = {50.0f, 70.0f}; line.end = {110.0f, 70.0f};
        anns.push_back(line);

        Annotation arrow{};
        arrow.id = 3; arrow.type = ToolType::Arrow; arrow.color = ColorRGBA{0, 122, 255, 255}; arrow.strokeWidth = 3.0f;
        arrow.start = {50.0f, 85.0f}; arrow.end = {120.0f, 85.0f};
        anns.push_back(arrow);

        Annotation rect{};
        rect.id = 4; rect.type = ToolType::Rectangle; rect.color = ColorRGBA{255, 255, 255, 255}; rect.strokeWidth = 2.0f;
        rect.rect = {55.0f, 45.0f, 90.0f, 65.0f};
        anns.push_back(rect);

        Annotation hl{};
        hl.id = 5; hl.type = ToolType::Highlighter; hl.color = ColorRGBA{255, 204, 0, 89}; hl.strokeWidth = 14.0f;
        hl.points = {{45.0f, 100.0f}, {150.0f, 100.0f}};
        anns.push_back(hl);

        Annotation txt{};
        txt.id = 6; txt.type = ToolType::Text; txt.color = ColorRGBA{255, 255, 255, 255};
        txt.start = {50.0f, 92.0f}; txt.text = L"OK"; txt.fontSize = 18.0f;
        anns.push_back(txt);

        Annotation mosaic{};
        mosaic.id = 7; mosaic.type = ToolType::Censor; mosaic.censorMode = CensorMode::Mosaic;
        mosaic.censorIntensity = 10.0f; mosaic.rect = {115.0f, 35.0f, 145.0f, 65.0f};
        anns.push_back(mosaic);

        Annotation blur{};
        blur.id = 8; blur.type = ToolType::Censor; blur.censorMode = CensorMode::GaussianBlur;
        blur.censorIntensity = 6.0f; blur.rect = {145.0f, 65.0f, 175.0f, 115.0f};
        anns.push_back(blur);

        const ExportedImage annotated = RenderCropToWicBitmap(ctx, renderer, frame, crop, anns);
        SC_EXPECT_TRUE(annotated.IsValid());
        SC_EXPECT_EQ(annotated.width, 137u);
        SC_EXPECT_EQ(annotated.height, 91u);

        // Verify opaque alpha on every exported pixel and measurable pixel diff from baseline.
        size_t diffPixels = 0;
        for (size_t i = 0; i < annotated.bgraPixels.size(); i += 4) {
            SC_EXPECT_EQ(static_cast<int>(annotated.bgraPixels[i + 3]), 255);
            if (annotated.bgraPixels[i + 0] != baseline.bgraPixels[i + 0] ||
                annotated.bgraPixels[i + 1] != baseline.bgraPixels[i + 1] ||
                annotated.bgraPixels[i + 2] != baseline.bgraPixels[i + 2]) {
                ++diffPixels;
            }
        }
        SC_EXPECT_TRUE(diffPixels > 500);

        SC_EXPECT_TRUE(SaveWicBitmapToFile(ctx.WicFactory(), annotated.wicBitmap.Get(), pngPath.wstring(), ImageFileFormat::Png));
        SC_EXPECT_TRUE(SaveWicBitmapToFile(ctx.WicFactory(), annotated.wicBitmap.Get(), jpgPath.wstring(), ImageFileFormat::Jpeg));

        // Verify PNG magic bytes (\x89PNG) and JPEG magic bytes (\xFF\xD8\xFF).
        {
            std::ifstream pngIn(pngPath, std::ios::binary);
            uint8_t sig[4]{};
            pngIn.read(reinterpret_cast<char*>(sig), 4);
            SC_EXPECT_EQ(static_cast<int>(sig[0]), 0x89);
            SC_EXPECT_EQ(static_cast<int>(sig[1]), 'P');
            SC_EXPECT_EQ(static_cast<int>(sig[2]), 'N');
            SC_EXPECT_EQ(static_cast<int>(sig[3]), 'G');
        }
        {
            std::ifstream jpgIn(jpgPath, std::ios::binary);
            uint8_t sig[3]{};
            jpgIn.read(reinterpret_cast<char*>(sig), 3);
            SC_EXPECT_EQ(static_cast<int>(sig[0]), 0xFF);
            SC_EXPECT_EQ(static_cast<int>(sig[1]), 0xD8);
            SC_EXPECT_EQ(static_cast<int>(sig[2]), 0xFF);
        }

        renderer.ClearCache();
        ctx.Reset();
    }

    std::filesystem::remove(pngPath, ec);
    std::filesystem::remove(jpgPath, ec);

    const DxgiLiveObjectReport dxgi = ReportDebugDxgiLiveObjects();
    if (dxgi.available) {
        SC_EXPECT_EQ(dxgi.liveApplicationObjects, static_cast<size_t>(0));
    }
}

SC_TEST("GaussianBlur and Mosaic censors visibly modify their exact non-zero-origin target rectangle") {
    ComInitScope comScope;
    SC_EXPECT_TRUE(comScope.Succeeded());

    // Build a high-contrast 2x2 black/white checkerboard so both Blur and Mosaic must alter pixels.
    CapturedFrame frame{};
    frame.width = 200;
    frame.height = 200;
    frame.stride = 200 * 4;
    frame.backend = CaptureBackend::Synthetic;
    frame.bgraPixels.resize(static_cast<size_t>(frame.stride) * 200u);
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            const uint8_t v = (((x / 4) + (y / 4)) & 1) ? 255 : 0;
            const size_t i = static_cast<size_t>(y * frame.stride + x * 4);
            frame.bgraPixels[i + 0] = v;
            frame.bgraPixels[i + 1] = v;
            frame.bgraPixels[i + 2] = v;
            frame.bgraPixels[i + 3] = 255;
        }
    }

    D2DContext ctx;
    SC_EXPECT_TRUE(ctx.InitializeHeadless(true));
    SC_EXPECT_TRUE(ctx.UploadDesktopBitmap(frame));
    SceneRenderer renderer;

    const RectF crop{0.0f, 0.0f, 200.0f, 200.0f};
    Annotation blur{};
    blur.id = 10;
    blur.type = ToolType::Censor;
    blur.censorMode = CensorMode::GaussianBlur;
    blur.censorIntensity = 8.0f;
    blur.rect = {80.0f, 70.0f, 160.0f, 150.0f}; // Non-zero origin!

    const ExportedImage out = RenderCropToWicBitmap(ctx, renderer, frame, crop, {blur});
    SC_EXPECT_TRUE(out.IsValid());

    // Center of blur box (120, 110) must be blurred to mid-gray (~127), NOT pure 0 or 255!
    const size_t centerIdx = static_cast<size_t>(110 * out.stride + 120 * 4);
    const uint8_t blurredLuma = out.bgraPixels[centerIdx];
    SC_EXPECT_TRUE(blurredLuma > 60 && blurredLuma < 195);

    // Pixel outside blur box (20, 20) must remain untouched pure 0 or 255.
    const size_t outIdx = static_cast<size_t>(20 * out.stride + 20 * 4);
    SC_EXPECT_TRUE(out.bgraPixels[outIdx] == 0 || out.bgraPixels[outIdx] == 255);

    renderer.ClearCache();
    ctx.Reset();
}

int main() {
    return saycheese::test::RunAll("export");
}
