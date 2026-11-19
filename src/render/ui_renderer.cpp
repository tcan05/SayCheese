#include "render/ui_renderer.hpp"

#include <d2d1_1helper.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace saycheese::render {

namespace {

using Microsoft::WRL::ComPtr;

D2D1_RECT_F ToD2DRect(tools::RectF r) noexcept {
    return D2D1::RectF(r.left, r.top, r.right, r.bottom);
}

D2D1_COLOR_F ToD2DColor(tools::ColorRGBA c) noexcept {
    return D2D1::ColorF(
        static_cast<float>(c.r) / 255.0f,
        static_cast<float>(c.g) / 255.0f,
        static_cast<float>(c.b) / 255.0f,
        static_cast<float>(c.a) / 255.0f);
}

void DrawGlassPanel(
    ID2D1DeviceContext* dc,
    tools::RectF rect,
    float radius,
    ID2D1Brush* bgBrush,
    ID2D1Brush* borderBrush,
    float scale) {
    if (rect.IsEmpty()) {
        return;
    }
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(ToD2DRect(rect), radius, radius);
    dc->FillRoundedRectangle(rr, bgBrush);
    dc->DrawRoundedRectangle(rr, borderBrush, 1.0f * scale);
}

void DrawCenteredLabel(
    ID2D1DeviceContext* dc,
    IDWriteFactory* dwriteFactory,
    const std::wstring& text,
    tools::RectF rect,
    float fontSize,
    ID2D1Brush* brush,
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD) {
    if (dwriteFactory == nullptr || text.empty() || rect.IsEmpty()) {
        return;
    }
    ComPtr<IDWriteTextFormat> fmt;
    if (FAILED(dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &fmt)) || !fmt) {
        return;
    }
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt.Get(), ToD2DRect(rect), brush);
}

void DrawButtonIcon(
    ID2D1DeviceContext* dc,
    ID2D1Factory1* d2dFactory,
    IDWriteFactory* dwriteFactory,
    ui::ToolbarButtonId id,
    tools::RectF r,
    float scale,
    ID2D1Brush* fgBrush,
    ID2D1SolidColorBrush* scratchBrush,
    const UIRenderState& state) {
    const float cx = 0.5f * (r.left + r.right);
    const float cy = 0.5f * (r.top + r.bottom);
    const float s = std::min(r.Width(), r.Height()) * 0.30f;
    const float stroke = std::max(1.4f, 1.75f * scale);

    switch (id) {
    case ui::ToolbarButtonId::ToolPen: {
        // Diagonal pen nib
        dc->DrawLine(D2D1::Point2F(cx - s, cy + s), D2D1::Point2F(cx + s * 0.7f, cy - s * 0.7f), fgBrush, stroke * 1.15f);
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - s, cy + s), stroke * 0.7f, stroke * 0.7f), fgBrush);
        break;
    }
    case ui::ToolbarButtonId::ToolLine:
        dc->DrawLine(D2D1::Point2F(cx - s, cy + s), D2D1::Point2F(cx + s, cy - s), fgBrush, stroke);
        break;

    case ui::ToolbarButtonId::ToolArrow: {
        const D2D1_POINT_2F p0 = D2D1::Point2F(cx - s, cy + s);
        const D2D1_POINT_2F p1 = D2D1::Point2F(cx + s, cy - s);
        dc->DrawLine(p0, p1, fgBrush, stroke);
        dc->DrawLine(p1, D2D1::Point2F(cx + s * 0.15f, cy - s), fgBrush, stroke);
        dc->DrawLine(p1, D2D1::Point2F(cx + s, cy - s * 0.15f), fgBrush, stroke);
        break;
    }
    case ui::ToolbarButtonId::ToolRect:
        dc->DrawRectangle(D2D1::RectF(cx - s, cy - s * 0.78f, cx + s, cy + s * 0.78f), fgBrush, stroke);
        break;

    case ui::ToolbarButtonId::ToolHighlighter: {
        if (scratchBrush != nullptr) {
            scratchBrush->SetColor(D2D1::ColorF(1.0f, 0.85f, 0.15f, 0.55f));
            dc->FillRectangle(D2D1::RectF(cx - s, cy + s * 0.15f, cx + s, cy + s * 0.85f), scratchBrush);
        }
        dc->DrawLine(D2D1::Point2F(cx - s * 0.7f, cy + s * 0.35f),
                     D2D1::Point2F(cx + s * 0.65f, cy - s * 0.85f), fgBrush, stroke * 1.6f);
        break;
    }
    case ui::ToolbarButtonId::ToolText:
        DrawCenteredLabel(dc, dwriteFactory, L"T", r, 15.0f * scale, fgBrush, DWRITE_FONT_WEIGHT_BOLD);
        break;

    case ui::ToolbarButtonId::ToolCensor: {
        // 3x3 mosaic checker icon
        const float cell = (s * 2.0f) / 3.0f;
        const float x0 = cx - s;
        const float y0 = cy - s;
        for (int gy = 0; gy < 3; ++gy) {
            for (int gx = 0; gx < 3; ++gx) {
                if (((gx + gy) & 1) == 0) {
                    dc->FillRectangle(D2D1::RectF(x0 + gx * cell, y0 + gy * cell,
                                                  x0 + (gx + 1) * cell, y0 + (gy + 1) * cell), fgBrush);
                }
            }
        }
        dc->DrawRectangle(D2D1::RectF(x0, y0, x0 + s * 2.0f, y0 + s * 2.0f), fgBrush, 1.0f * scale);
        break;
    }
    case ui::ToolbarButtonId::Undo: {
        // Curved left undo arrow
        dc->DrawLine(D2D1::Point2F(cx - s, cy - s * 0.15f), D2D1::Point2F(cx + s * 0.75f, cy - s * 0.15f), fgBrush, stroke);
        dc->DrawLine(D2D1::Point2F(cx + s * 0.75f, cy - s * 0.15f), D2D1::Point2F(cx + s * 0.75f, cy + s * 0.65f), fgBrush, stroke);
        dc->DrawLine(D2D1::Point2F(cx - s, cy - s * 0.15f), D2D1::Point2F(cx - s * 0.35f, cy - s * 0.75f), fgBrush, stroke);
        dc->DrawLine(D2D1::Point2F(cx - s, cy - s * 0.15f), D2D1::Point2F(cx - s * 0.35f, cy + s * 0.45f), fgBrush, stroke);
        break;
    }
    case ui::ToolbarButtonId::Redo: {
        dc->DrawLine(D2D1::Point2F(cx + s, cy - s * 0.15f), D2D1::Point2F(cx - s * 0.75f, cy - s * 0.15f), fgBrush, stroke);
        dc->DrawLine(D2D1::Point2F(cx - s * 0.75f, cy - s * 0.15f), D2D1::Point2F(cx - s * 0.75f, cy + s * 0.65f), fgBrush, stroke);
        dc->DrawLine(D2D1::Point2F(cx + s, cy - s * 0.15f), D2D1::Point2F(cx + s * 0.35f, cy - s * 0.75f), fgBrush, stroke);
        dc->DrawLine(D2D1::Point2F(cx + s, cy - s * 0.15f), D2D1::Point2F(cx + s * 0.35f, cy + s * 0.45f), fgBrush, stroke);
        break;
    }
    case ui::ToolbarButtonId::ColorPicker: {
        if (scratchBrush != nullptr) {
            scratchBrush->SetColor(ToD2DColor(state.activeColor));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.95f, s * 0.95f), scratchBrush);
        }
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.95f, s * 0.95f), fgBrush, 1.4f * scale);
        break;
    }
    case ui::ToolbarButtonId::StrokeWidth: {
        const float dotR = std::clamp(state.activeSize * 0.25f * scale, 2.2f * scale, s * 0.9f);
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), dotR, dotR), fgBrush);
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.95f, s * 0.95f), fgBrush, 1.0f * scale);
        break;
    }
    case ui::ToolbarButtonId::Copy: {
        // Overlapping document sheets
        dc->DrawRectangle(D2D1::RectF(cx - s * 0.85f, cy - s * 0.85f, cx + s * 0.25f, cy + s * 0.35f), fgBrush, stroke);
        dc->DrawRectangle(D2D1::RectF(cx - s * 0.25f, cy - s * 0.35f, cx + s * 0.85f, cy + s * 0.85f), fgBrush, stroke);
        break;
    }
    case ui::ToolbarButtonId::Save: {
        // Floppy / disk outline
        dc->DrawRectangle(D2D1::RectF(cx - s, cy - s, cx + s, cy + s), fgBrush, stroke);
        dc->DrawRectangle(D2D1::RectF(cx - s * 0.55f, cy - s, cx + s * 0.45f, cy - s * 0.2f), fgBrush, 1.1f * scale);
        dc->DrawRectangle(D2D1::RectF(cx - s * 0.6f, cy + s * 0.15f, cx + s * 0.6f, cy + s), fgBrush, 1.1f * scale);
        break;
    }
    case ui::ToolbarButtonId::Cancel: {
        dc->DrawLine(D2D1::Point2F(cx - s * 0.85f, cy - s * 0.85f), D2D1::Point2F(cx + s * 0.85f, cy + s * 0.85f), fgBrush, stroke);
        dc->DrawLine(D2D1::Point2F(cx + s * 0.85f, cy - s * 0.85f), D2D1::Point2F(cx - s * 0.85f, cy + s * 0.85f), fgBrush, stroke);
        break;
    }
    case ui::ToolbarButtonId::ColorSwatch0:
    case ui::ToolbarButtonId::ColorSwatch1:
    case ui::ToolbarButtonId::ColorSwatch2:
    case ui::ToolbarButtonId::ColorSwatch3:
    case ui::ToolbarButtonId::ColorSwatch4:
    case ui::ToolbarButtonId::ColorSwatch5: {
        const size_t idx = static_cast<size_t>(id) - static_cast<size_t>(ui::ToolbarButtonId::ColorSwatch0);
        if (idx < tools::kQuickPalette.size() && scratchBrush != nullptr) {
            scratchBrush->SetColor(ToD2DColor(tools::kQuickPalette[idx]));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 1.05f, s * 1.05f), scratchBrush);
            const bool selected = (state.activeColor == tools::kQuickPalette[idx]);
            dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 1.05f, s * 1.05f),
                            fgBrush, (selected ? 2.2f : 1.0f) * scale);
        }
        break;
    }
    case ui::ToolbarButtonId::ColorCustom:
        DrawCenteredLabel(dc, dwriteFactory, L"+", r, 15.0f * scale, fgBrush, DWRITE_FONT_WEIGHT_BOLD);
        break;

    case ui::ToolbarButtonId::CensorMosaic:
        DrawCenteredLabel(dc, dwriteFactory, L"Mosaic", r, 11.5f * scale, fgBrush);
        break;

    case ui::ToolbarButtonId::CensorBlur:
        DrawCenteredLabel(dc, dwriteFactory, L"Blur", r, 11.5f * scale, fgBrush);
        break;

    case ui::ToolbarButtonId::PresetThin:
        DrawCenteredLabel(dc, dwriteFactory, L"S", r, 12.0f * scale, fgBrush, DWRITE_FONT_WEIGHT_BOLD);
        break;

    case ui::ToolbarButtonId::PresetMedium:
        DrawCenteredLabel(dc, dwriteFactory, L"M", r, 12.0f * scale, fgBrush, DWRITE_FONT_WEIGHT_BOLD);
        break;

    case ui::ToolbarButtonId::PresetThick:
        DrawCenteredLabel(dc, dwriteFactory, L"L", r, 12.0f * scale, fgBrush, DWRITE_FONT_WEIGHT_BOLD);
        break;

    case ui::ToolbarButtonId::None:
        break;
    }
    (void)d2dFactory;
}

bool IsButtonSelected(ui::ToolbarButtonId id, const UIRenderState& state) noexcept {
    switch (id) {
    case ui::ToolbarButtonId::ToolPen: return state.activeTool == tools::ToolType::Pen;
    case ui::ToolbarButtonId::ToolLine: return state.activeTool == tools::ToolType::Line;
    case ui::ToolbarButtonId::ToolArrow: return state.activeTool == tools::ToolType::Arrow;
    case ui::ToolbarButtonId::ToolRect: return state.activeTool == tools::ToolType::Rectangle;
    case ui::ToolbarButtonId::ToolHighlighter: return state.activeTool == tools::ToolType::Highlighter;
    case ui::ToolbarButtonId::ToolText: return state.activeTool == tools::ToolType::Text;
    case ui::ToolbarButtonId::ToolCensor: return state.activeTool == tools::ToolType::Censor;
    case ui::ToolbarButtonId::CensorMosaic: return state.censorMode == tools::CensorMode::Mosaic;
    case ui::ToolbarButtonId::CensorBlur: return state.censorMode == tools::CensorMode::GaussianBlur;
    default: return false;
    }
}

bool IsButtonEnabled(ui::ToolbarButtonId id, const UIRenderState& state) noexcept {
    if (id == ui::ToolbarButtonId::Undo) return state.canUndo;
    if (id == ui::ToolbarButtonId::Redo) return state.canRedo;
    return true;
}

} // namespace

void UIRenderer::RenderOverlayChrome(
    ID2D1DeviceContext* dc,
    ID2D1Factory1* d2dFactory,
    IDWriteFactory* dwriteFactory,
    float canvasWidth,
    float canvasHeight,
    const UIRenderState& state) {
    if (dc == nullptr) {
        return;
    }

    ComPtr<ID2D1SolidColorBrush> dimBrush;
    ComPtr<ID2D1SolidColorBrush> whiteBrush;
    ComPtr<ID2D1SolidColorBrush> mutedBrush;
    ComPtr<ID2D1SolidColorBrush> panelBgBrush;
    ComPtr<ID2D1SolidColorBrush> panelBorderBrush;
    ComPtr<ID2D1SolidColorBrush> hoverBrush;
    ComPtr<ID2D1SolidColorBrush> activeBrush;
    ComPtr<ID2D1SolidColorBrush> accentBrush;
    ComPtr<ID2D1SolidColorBrush> cancelBrush;

    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &dimBrush)) ||
        FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.96f), &whiteBrush)) ||
        FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.70f, 0.78f, 0.45f), &mutedBrush)) ||
        FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.11f, 0.15f, 0.92f), &panelBgBrush)) ||
        FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f), &panelBorderBrush)) ||
        FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f), &hoverBrush)) ||
        FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.74f, 0.97f, 0.34f), &activeBrush)) ||
        FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.74f, 0.97f, 1.0f), &accentBrush)) ||
        FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.35f, 0.35f, 0.95f), &cancelBrush))) {
        return;
    }

    // 1. Darkened backdrop outside active selection.
    if (!state.selection.active || state.selection.rect.IsEmpty()) {
        dc->FillRectangle(D2D1::RectF(0.0f, 0.0f, canvasWidth, canvasHeight), dimBrush.Get());
        return;
    }

    const tools::RectF sel = state.selection.rect.Normalized();
    const float scale = std::clamp(state.layout.uiScale, 1.0f, 3.0f);

    // Top, Bottom, Left, Right dim bands around selection (crisp 0-overlap cutout).
    if (sel.top > 0.0f) {
        dc->FillRectangle(D2D1::RectF(0.0f, 0.0f, canvasWidth, sel.top), dimBrush.Get());
    }
    if (sel.bottom < canvasHeight) {
        dc->FillRectangle(D2D1::RectF(0.0f, sel.bottom, canvasWidth, canvasHeight), dimBrush.Get());
    }
    if (sel.left > 0.0f) {
        dc->FillRectangle(D2D1::RectF(0.0f, sel.top, sel.left, sel.bottom), dimBrush.Get());
    }
    if (sel.right < canvasWidth) {
        dc->FillRectangle(D2D1::RectF(sel.right, sel.top, canvasWidth, sel.bottom), dimBrush.Get());
    }

    // 2. Selection border + 8 resize handles.
    dc->DrawRectangle(ToD2DRect(sel), accentBrush.Get(), 1.5f * scale);

    const float hr = 4.0f * scale;
    const float cx = 0.5f * (sel.left + sel.right);
    const float cy = 0.5f * (sel.top + sel.bottom);
    const D2D1_POINT_2F handles[8] = {
        {sel.left, sel.top}, {cx, sel.top}, {sel.right, sel.top},
        {sel.right, cy}, {sel.right, sel.bottom}, {cx, sel.bottom},
        {sel.left, sel.bottom}, {sel.left, cy}
    };
    for (const auto& h : handles) {
        const D2D1_RECT_F hRect = D2D1::RectF(h.x - hr, h.y - hr, h.x + hr, h.y + hr);
        dc->FillRectangle(hRect, whiteBrush.Get());
        dc->DrawRectangle(hRect, accentBrush.Get(), 1.2f * scale);
    }

    // 3. Inline active Text editor preview + dashed border + blinking caret (not exported).
    if (state.textEditor.active && dwriteFactory != nullptr) {
        const float fontSize = std::clamp(state.textEditor.fontSize, 10.0f, 128.0f);
        ComPtr<IDWriteTextFormat> fmt;
        if (SUCCEEDED(dwriteFactory->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                fontSize, L"", &fmt)) && fmt) {
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            const std::wstring& txt = state.textEditor.text;
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                    txt.c_str(), static_cast<UINT32>(txt.size()),
                    fmt.Get(), 10000.0f, 10000.0f, &layout)) && layout) {
                DWRITE_TEXT_METRICS tm{};
                layout->GetMetrics(&tm);
                const float boxW = std::max(28.0f * scale, tm.widthIncludingTrailingWhitespace + 12.0f * scale);
                const float boxH = std::max(fontSize * 1.35f, tm.height + 8.0f * scale);
                const D2D1_RECT_F boxRect = D2D1::RectF(
                    state.textEditor.anchor.x - 4.0f * scale,
                    state.textEditor.anchor.y - 3.0f * scale,
                    state.textEditor.anchor.x + boxW,
                    state.textEditor.anchor.y + boxH);

                ComPtr<ID2D1StrokeStyle> dashStyle;
                if (d2dFactory != nullptr) {
                    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
                        D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                        D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_DASH, 0.0f);
                    d2dFactory->CreateStrokeStyle(sp, nullptr, 0, &dashStyle);
                }
                dc->FillRectangle(boxRect, dimBrush.Get());
                dc->DrawRectangle(boxRect, accentBrush.Get(), 1.2f * scale, dashStyle.Get());

                ComPtr<ID2D1SolidColorBrush> textBrush;
                if (SUCCEEDED(dc->CreateSolidColorBrush(ToD2DColor(state.textEditor.color), &textBrush)) && textBrush) {
                    dc->DrawTextLayout(D2D1::Point2F(state.textEditor.anchor.x, state.textEditor.anchor.y),
                                       layout.Get(), textBrush.Get());
                }

                if (state.showCaret) {
                    FLOAT caretX = 0.0f;
                    FLOAT caretY = 0.0f;
                    DWRITE_HIT_TEST_METRICS htm{};
                    const UINT32 idx = static_cast<UINT32>(std::min(state.textEditor.caretIndex, txt.size()));
                    if (SUCCEEDED(layout->HitTestTextPosition(idx, FALSE, &caretX, &caretY, &htm))) {
                        const float h = htm.height > 1.0f ? htm.height : fontSize * 1.2f;
                        dc->DrawLine(
                            D2D1::Point2F(state.textEditor.anchor.x + caretX, state.textEditor.anchor.y + caretY),
                            D2D1::Point2F(state.textEditor.anchor.x + caretX, state.textEditor.anchor.y + caretY + h),
                            whiteBrush.Get(),
                            1.8f * scale);
                    }
                }
            }
        }
    }

    // 4. Live dimension badge (W x H px).
    const float cornerR = 6.0f * scale;
    if (!state.layout.badgeRect.IsEmpty()) {
        DrawGlassPanel(dc, state.layout.badgeRect, 5.0f * scale, panelBgBrush.Get(), panelBorderBrush.Get(), scale);
        wchar_t buf[64]{};
        const int wPx = std::max(1, static_cast<int>(std::lround(sel.Width())));
        const int hPx = std::max(1, static_cast<int>(std::lround(sel.Height())));
        std::swprintf(buf, ARRAYSIZE(buf), L"%d \u00D7 %d px", wPx, hPx);
        DrawCenteredLabel(dc, dwriteFactory, buf, state.layout.badgeRect, 11.5f * scale, whiteBrush.Get());
    }

    // 5. Floating Tool Bar, Action Bar, and active popups.
    DrawGlassPanel(dc, state.layout.toolBarRect, cornerR, panelBgBrush.Get(), panelBorderBrush.Get(), scale);
    DrawGlassPanel(dc, state.layout.actionBarRect, cornerR, panelBgBrush.Get(), panelBorderBrush.Get(), scale);
    DrawGlassPanel(dc, state.layout.censorPillRect, cornerR, panelBgBrush.Get(), panelBorderBrush.Get(), scale);
    DrawGlassPanel(dc, state.layout.colorPopupRect, cornerR, panelBgBrush.Get(), panelBorderBrush.Get(), scale);
    DrawGlassPanel(dc, state.layout.strokePopupRect, cornerR, panelBgBrush.Get(), panelBorderBrush.Get(), scale);

    ComPtr<ID2D1SolidColorBrush> scratchBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &scratchBrush);

    for (const auto& btn : state.layout.buttons) {
        const bool enabled = IsButtonEnabled(btn.id, state);
        const bool selected = IsButtonSelected(btn.id, state);
        const bool hovered = enabled && (state.hoveredButton == btn.id);

        if (selected || hovered) {
            const D2D1_ROUNDED_RECT br = D2D1::RoundedRect(ToD2DRect(btn.rect), 4.0f * scale, 4.0f * scale);
            dc->FillRoundedRectangle(br, selected ? activeBrush.Get() : hoverBrush.Get());
            if (selected) {
                dc->DrawRoundedRectangle(br, accentBrush.Get(), 1.0f * scale);
            }
        }

        ID2D1Brush* fg = !enabled ? mutedBrush.Get()
                                  : (btn.id == ui::ToolbarButtonId::Cancel ? cancelBrush.Get() : whiteBrush.Get());
        DrawButtonIcon(dc, d2dFactory, dwriteFactory, btn.id, btn.rect, scale, fg, scratchBrush.Get(), state);
    }

    // 6. Subtle live wheel-sizing HUD pill next to the badge when scrolling size.
    if (state.showSizeHud && !state.layout.badgeRect.IsEmpty()) {
        const float hudW = 72.0f * scale;
        const float hudLeft = std::min(state.layout.badgeRect.right + 6.0f * scale,
                                       std::max(0.0f, canvasWidth - hudW - 6.0f * scale));
        const tools::RectF hudRect{hudLeft, state.layout.badgeRect.top, hudLeft + hudW, state.layout.badgeRect.bottom};
        DrawGlassPanel(dc, hudRect, 5.0f * scale, panelBgBrush.Get(), panelBorderBrush.Get(), scale);
        wchar_t sizeBuf[32]{};
        std::swprintf(sizeBuf, ARRAYSIZE(sizeBuf), L"%.0f px", std::round(state.activeSize));
        DrawCenteredLabel(dc, dwriteFactory, sizeBuf, hudRect, 11.5f * scale, accentBrush.Get());
    }
}

} // namespace saycheese::render
