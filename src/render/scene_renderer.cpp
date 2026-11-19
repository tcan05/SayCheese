#include "render/scene_renderer.hpp"

#include <d2d1_1helper.h>
#include <d2d1effects.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace saycheese::render {

namespace {

using Microsoft::WRL::ComPtr;

D2D1_COLOR_F ToD2DColor(tools::ColorRGBA c) noexcept {
    return D2D1::ColorF(
        static_cast<float>(c.r) / 255.0f,
        static_cast<float>(c.g) / 255.0f,
        static_cast<float>(c.b) / 255.0f,
        static_cast<float>(c.a) / 255.0f);
}

D2D1_RECT_F ToD2DRect(tools::RectF r) noexcept {
    return D2D1::RectF(r.left, r.top, r.right, r.bottom);
}

} // namespace

void SceneRenderer::ClearCache() noexcept {
    mosaicCache_.clear();
}

void SceneRenderer::RenderScene(
    ID2D1DeviceContext* dc,
    ID2D1Factory1* d2dFactory,
    IDWriteFactory* dwriteFactory,
    ID2D1Bitmap* desktopBitmap,
    const capture::CapturedFrame& sourceFrame,
    tools::RectF clipRect,
    const std::vector<tools::Annotation>& annotations,
    const tools::Annotation* activePreview) {
    if (dc == nullptr || desktopBitmap == nullptr) {
        return;
    }

    const D2D1_SIZE_F bmpSize = desktopBitmap->GetSize();
    dc->DrawBitmap(
        desktopBitmap,
        D2D1::RectF(0.0f, 0.0f, bmpSize.width, bmpSize.height),
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    const bool hasClip = !clipRect.IsEmpty();
    if (hasClip) {
        dc->PushAxisAlignedClip(ToD2DRect(clipRect.Normalized()), D2D1_ANTIALIAS_MODE_ALIASED);
    }

    for (const auto& ann : annotations) {
        DrawAnnotation(dc, d2dFactory, dwriteFactory, desktopBitmap, sourceFrame, ann);
    }
    if (activePreview != nullptr) {
        DrawAnnotation(dc, d2dFactory, dwriteFactory, desktopBitmap, sourceFrame, *activePreview);
    }

    if (hasClip) {
        dc->PopAxisAlignedClip();
    }
}

void SceneRenderer::DrawAnnotation(
    ID2D1DeviceContext* dc,
    ID2D1Factory1* d2dFactory,
    IDWriteFactory* dwriteFactory,
    ID2D1Bitmap* desktopBitmap,
    const capture::CapturedFrame& sourceFrame,
    const tools::Annotation& ann) {
    switch (ann.type) {
    case tools::ToolType::Pen:
        DrawSmoothStroke(dc, d2dFactory, ann, false);
        break;

    case tools::ToolType::Highlighter:
        DrawSmoothStroke(dc, d2dFactory, ann, true);
        break;

    case tools::ToolType::Line: {
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(dc->CreateSolidColorBrush(ToD2DColor(ann.color), &brush)) || !brush) {
            return;
        }
        ComPtr<ID2D1StrokeStyle> style;
        if (d2dFactory != nullptr) {
            D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
            d2dFactory->CreateStrokeStyle(props, nullptr, 0, &style);
        }
        dc->DrawLine(D2D1::Point2F(ann.start.x, ann.start.y),
                     D2D1::Point2F(ann.end.x, ann.end.y),
                     brush.Get(), ann.strokeWidth, style.Get());
        break;
    }

    case tools::ToolType::Arrow: {
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(dc->CreateSolidColorBrush(ToD2DColor(ann.color), &brush)) || !brush) {
            return;
        }
        const tools::ArrowheadTriangle head = tools::ComputeArrowhead(ann.start, ann.end, ann.strokeWidth);

        ComPtr<ID2D1StrokeStyle> style;
        if (d2dFactory != nullptr) {
            D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
            d2dFactory->CreateStrokeStyle(props, nullptr, 0, &style);
        }
        dc->DrawLine(D2D1::Point2F(ann.start.x, ann.start.y),
                     D2D1::Point2F(head.shaftEnd.x, head.shaftEnd.y),
                     brush.Get(), ann.strokeWidth, style.Get());

        if (d2dFactory != nullptr) {
            ComPtr<ID2D1PathGeometry> geo;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(d2dFactory->CreatePathGeometry(&geo)) &&
                SUCCEEDED(geo->Open(&sink))) {
                sink->BeginFigure(D2D1::Point2F(head.tip.x, head.tip.y), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(head.left.x, head.left.y));
                sink->AddLine(D2D1::Point2F(head.right.x, head.right.y));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) {
                    dc->FillGeometry(geo.Get(), brush.Get());
                }
            }
        }
        break;
    }

    case tools::ToolType::Rectangle: {
        const tools::RectF r = ann.rect.Normalized();
        if (r.IsEmpty()) {
            return;
        }
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(dc->CreateSolidColorBrush(ToD2DColor(ann.color), &brush)) || !brush) {
            return;
        }
        dc->DrawRectangle(ToD2DRect(r), brush.Get(), ann.strokeWidth);
        break;
    }

    case tools::ToolType::Text: {
        if (dwriteFactory == nullptr || ann.text.empty()) {
            return;
        }
        ComPtr<IDWriteTextFormat> fmt;
        if (FAILED(dwriteFactory->CreateTextFormat(
                L"Segoe UI",
                nullptr,
                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                std::clamp(ann.fontSize, 10.0f, 128.0f),
                L"",
                &fmt)) || !fmt) {
            return;
        }
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory->CreateTextLayout(
                ann.text.c_str(),
                static_cast<UINT32>(ann.text.size()),
                fmt.Get(),
                10000.0f,
                10000.0f,
                &layout)) || !layout) {
            return;
        }

        // Subtle dark drop shadow for crisp readability on light and dark desktops alike.
        ComPtr<ID2D1SolidColorBrush> shadowBrush;
        if (SUCCEEDED(dc->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &shadowBrush)) &&
            shadowBrush) {
            dc->DrawTextLayout(D2D1::Point2F(ann.start.x + 1.0f, ann.start.y + 1.5f),
                               layout.Get(), shadowBrush.Get());
        }

        ComPtr<ID2D1SolidColorBrush> textBrush;
        if (SUCCEEDED(dc->CreateSolidColorBrush(ToD2DColor(ann.color), &textBrush)) && textBrush) {
            dc->DrawTextLayout(D2D1::Point2F(ann.start.x, ann.start.y), layout.Get(), textBrush.Get());
        }
        break;
    }

    case tools::ToolType::Censor:
        if (ann.censorMode == tools::CensorMode::Mosaic) {
            DrawCensorMosaic(dc, sourceFrame, ann);
        } else {
            DrawCensorGaussianBlur(dc, desktopBitmap, ann);
        }
        break;

    case tools::ToolType::Select:
        break;
    }
}

void SceneRenderer::DrawSmoothStroke(
    ID2D1DeviceContext* dc,
    ID2D1Factory1* d2dFactory,
    const tools::Annotation& ann,
    bool isHighlighter) {
    if (d2dFactory == nullptr || ann.points.size() < 2) {
        return;
    }

    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(d2dFactory->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) {
        return;
    }

    sink->BeginFigure(D2D1::Point2F(ann.points[0].x, ann.points[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
    const auto segments = tools::SmoothPolylineQuadratic(ann.points);
    for (const auto& seg : segments) {
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
            D2D1::Point2F(seg.control.x, seg.control.y),
            D2D1::Point2F(seg.end.x, seg.end.y)));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) {
        return;
    }

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(ToD2DColor(ann.color), &brush)) || !brush) {
        return;
    }

    const D2D1_CAP_STYLE cap = isHighlighter ? D2D1_CAP_STYLE_FLAT : D2D1_CAP_STYLE_ROUND;
    ComPtr<ID2D1StrokeStyle> style;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        cap, cap, cap, D2D1_LINE_JOIN_ROUND);
    d2dFactory->CreateStrokeStyle(props, nullptr, 0, &style);

    // Drawing the entire smoothed path in one DrawGeometry call ensures uniform highlighter opacity without overlap seams.
    dc->DrawGeometry(geo.Get(), brush.Get(), ann.strokeWidth, style.Get());
}

void SceneRenderer::DrawCensorMosaic(
    ID2D1DeviceContext* dc,
    const capture::CapturedFrame& sourceFrame,
    const tools::Annotation& ann) {
    if (!sourceFrame.IsValid()) {
        return;
    }
    const tools::RectF norm = ann.rect.Normalized();
    const int left = std::clamp(static_cast<int>(std::floor(norm.left)), 0, sourceFrame.width);
    const int top = std::clamp(static_cast<int>(std::floor(norm.top)), 0, sourceFrame.height);
    const int right = std::clamp(static_cast<int>(std::ceil(norm.right)), 0, sourceFrame.width);
    const int bottom = std::clamp(static_cast<int>(std::ceil(norm.bottom)), 0, sourceFrame.height);
    const int rw = right - left;
    const int rh = bottom - top;
    if (rw <= 0 || rh <= 0) {
        return;
    }

    const int block = std::clamp(static_cast<int>(std::lround(ann.censorIntensity)), 4, 64);

    ID2D1Bitmap* cachedBmp = nullptr;
    if (ann.id != 0) {
        auto it = mosaicCache_.find(ann.id);
        if (it != mosaicCache_.end() &&
            it->second.left == left && it->second.top == top &&
            it->second.right == right && it->second.bottom == bottom &&
            it->second.blockSize == block && it->second.bitmap) {
            cachedBmp = it->second.bitmap.Get();
        }
    }

    ComPtr<ID2D1Bitmap> tempBmp;
    if (cachedBmp == nullptr) {
        const int cols = (rw + block - 1) / block;
        const int rows = (rh + block - 1) / block;
        std::vector<uint8_t> smallPixels(static_cast<size_t>(cols) * static_cast<size_t>(rows) * 4u, 255);

        for (int by = 0; by < rows; ++by) {
            const int y0 = top + by * block;
            const int y1 = std::min(bottom, y0 + block);
            for (int bx = 0; bx < cols; ++bx) {
                const int x0 = left + bx * block;
                const int x1 = std::min(right, x0 + block);
                uint64_t sumB = 0;
                uint64_t sumG = 0;
                uint64_t sumR = 0;
                uint64_t count = 0;
                for (int py = y0; py < y1; ++py) {
                    const uint8_t* rowPtr = sourceFrame.bgraPixels.data() +
                                            static_cast<size_t>(py) * static_cast<size_t>(sourceFrame.stride);
                    for (int px = x0; px < x1; ++px) {
                        const size_t si = static_cast<size_t>(px) * 4u;
                        sumB += rowPtr[si + 0];
                        sumG += rowPtr[si + 1];
                        sumR += rowPtr[si + 2];
                        ++count;
                    }
                }
                const size_t di = (static_cast<size_t>(by) * static_cast<size_t>(cols) + static_cast<size_t>(bx)) * 4u;
                if (count > 0) {
                    smallPixels[di + 0] = static_cast<uint8_t>(sumB / count);
                    smallPixels[di + 1] = static_cast<uint8_t>(sumG / count);
                    smallPixels[di + 2] = static_cast<uint8_t>(sumR / count);
                    smallPixels[di + 3] = 255;
                }
            }
        }

        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        if (FAILED(dc->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(cols), static_cast<UINT32>(rows)),
                                    smallPixels.data(),
                                    static_cast<UINT32>(cols * 4),
                                    &props,
                                    &tempBmp)) || !tempBmp) {
            return;
        }
        cachedBmp = tempBmp.Get();
        if (ann.id != 0) {
            mosaicCache_[ann.id] = MosaicCacheEntry{left, top, right, bottom, block, tempBmp};
        }
    }

    dc->DrawBitmap(
        cachedBmp,
        D2D1::RectF(static_cast<float>(left), static_cast<float>(top),
                    static_cast<float>(right), static_cast<float>(bottom)),
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

void SceneRenderer::DrawCensorGaussianBlur(
    ID2D1DeviceContext* dc,
    ID2D1Bitmap* desktopBitmap,
    const tools::Annotation& ann) {
    if (desktopBitmap == nullptr) {
        return;
    }

    const D2D1_SIZE_F bmpSize = desktopBitmap->GetSize();
    const tools::RectF norm = ann.rect.Normalized();
    const D2D1_RECT_F clamped = D2D1::RectF(
        std::clamp(norm.left, 0.0f, bmpSize.width),
        std::clamp(norm.top, 0.0f, bmpSize.height),
        std::clamp(norm.right, 0.0f, bmpSize.width),
        std::clamp(norm.bottom, 0.0f, bmpSize.height));
    if (clamped.right <= clamped.left || clamped.bottom <= clamped.top) {
        return;
    }

    ComPtr<ID2D1Effect> cropEffect;
    ComPtr<ID2D1Effect> borderEffect;
    ComPtr<ID2D1Effect> blurEffect;
    if (FAILED(dc->CreateEffect(CLSID_D2D1Crop, &cropEffect)) ||
        FAILED(dc->CreateEffect(CLSID_D2D1Border, &borderEffect)) ||
        FAILED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &blurEffect))) {
        return;
    }

    const float sigma = std::clamp(ann.censorIntensity, 2.0f, 36.0f);

    cropEffect->SetInput(0, desktopBitmap);
    cropEffect->SetValue(D2D1_CROP_PROP_RECT, D2D1::Vector4F(clamped.left, clamped.top, clamped.right, clamped.bottom));

    borderEffect->SetInputEffect(0, cropEffect.Get());
    borderEffect->SetValue(D2D1_BORDER_PROP_EDGE_MODE_X, D2D1_BORDER_EDGE_MODE_CLAMP);
    borderEffect->SetValue(D2D1_BORDER_PROP_EDGE_MODE_Y, D2D1_BORDER_EDGE_MODE_CLAMP);

    blurEffect->SetInputEffect(0, borderEffect.Get());
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma);
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);

    dc->PushAxisAlignedClip(clamped, D2D1_ANTIALIAS_MODE_ALIASED);
    dc->DrawImage(blurEffect.Get(), D2D1::Point2F(0.0f, 0.0f), clamped, D2D1_INTERPOLATION_MODE_LINEAR);
    dc->PopAxisAlignedClip();
}

} // namespace saycheese::render
