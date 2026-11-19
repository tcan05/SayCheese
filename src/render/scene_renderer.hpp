#pragma once

#include "capture/captured_frame.hpp"
#include "tools/annotation.hpp"
#include "tools/geometry.hpp"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace saycheese::render {

class SceneRenderer {
public:
    SceneRenderer() = default;
    ~SceneRenderer() noexcept { ClearCache(); }

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void ClearCache() noexcept;

    void RenderScene(
        ID2D1DeviceContext* dc,
        ID2D1Factory1* d2dFactory,
        IDWriteFactory* dwriteFactory,
        ID2D1Bitmap* desktopBitmap,
        const capture::CapturedFrame& sourceFrame,
        tools::RectF clipRect,
        const std::vector<tools::Annotation>& annotations,
        const tools::Annotation* activePreview);

private:
    struct MosaicCacheEntry {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int blockSize = 0;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };

    void DrawAnnotation(
        ID2D1DeviceContext* dc,
        ID2D1Factory1* d2dFactory,
        IDWriteFactory* dwriteFactory,
        ID2D1Bitmap* desktopBitmap,
        const capture::CapturedFrame& sourceFrame,
        const tools::Annotation& ann);

    void DrawSmoothStroke(
        ID2D1DeviceContext* dc,
        ID2D1Factory1* d2dFactory,
        const tools::Annotation& ann,
        bool isHighlighter);

    void DrawCensorMosaic(
        ID2D1DeviceContext* dc,
        const capture::CapturedFrame& sourceFrame,
        const tools::Annotation& ann);

    void DrawCensorGaussianBlur(
        ID2D1DeviceContext* dc,
        ID2D1Bitmap* desktopBitmap,
        const tools::Annotation& ann);

    struct BlurCacheEntry {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> subBitmap;
    };

    std::unordered_map<uint64_t, MosaicCacheEntry> mosaicCache_;
    std::unordered_map<uint64_t, BlurCacheEntry> blurSubBitmapCache_;
    ID2D1DeviceContext* cachedEffectDc_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Effect> blurEffect_;
};

} // namespace saycheese::render
