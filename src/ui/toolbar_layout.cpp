#include "ui/toolbar_layout.hpp"

#include <algorithm>
#include <array>

namespace saycheese::ui {

namespace {

tools::RectF ToRectF(const RECT& r) noexcept {
    return tools::RectF{
        static_cast<float>(r.left),
        static_cast<float>(r.top),
        static_cast<float>(r.right),
        static_cast<float>(r.bottom)
    };
}

tools::RectF PlacePopupBesideBar(
    tools::RectF barRect,
    float anchorCenterY,
    float popupW,
    float popupH,
    float gap,
    tools::RectF monBounds) noexcept {
    float left = barRect.right + gap;
    if (left + popupW > monBounds.right - gap) {
        left = barRect.left - gap - popupW;
    }
    if (left < monBounds.left + gap) {
        left = monBounds.left + gap;
    }

    float top = anchorCenterY - 0.5f * popupH;
    top = std::clamp(top, monBounds.top + gap, std::max(monBounds.top + gap, monBounds.bottom - gap - popupH));
    return tools::ClampRectToBounds(tools::RectF{left, top, left + popupW, top + popupH}, monBounds);
}

} // namespace

ToolbarLayoutResult ComputeToolbarLayout(
    tools::RectF selection,
    const std::vector<platform::MonitorInfo>& monitors,
    tools::RectF canvasBounds,
    const ToolbarLayoutState& state) {
    ToolbarLayoutResult res{};
    selection = selection.Normalized();

    const tools::PointF anchor{
        std::clamp(selection.right - 1.0f, canvasBounds.left, std::max(canvasBounds.left, canvasBounds.right - 1.0f)),
        std::clamp(selection.bottom - 1.0f, canvasBounds.top, std::max(canvasBounds.top, canvasBounds.bottom - 1.0f))
    };

    const platform::MonitorInfo* mon = platform::FindMonitorContainingCanvasPoint(monitors, anchor.x, anchor.y);
    const tools::RectF monBounds = (mon != nullptr) ? ToRectF(mon->canvasRect) : canvasBounds;
    const float scale = (mon != nullptr) ? std::clamp(mon->Scale(), 1.0f, 3.0f) : 1.0f;
    res.uiScale = scale;

    const float gap = 8.0f * scale;
    const float pad = 5.0f * scale;
    const float btnSize = 30.0f * scale;
    const float btnGap = 3.0f * scale;
    const float sepGap = 8.0f * scale;

    // 1. Vertical Tool Bar: 7 drawing tools + separator + 4 utility buttons (Undo, Redo, Color, Stroke)
    constexpr std::array<ToolbarButtonId, 7> kDrawTools{
        ToolbarButtonId::ToolPen,
        ToolbarButtonId::ToolLine,
        ToolbarButtonId::ToolArrow,
        ToolbarButtonId::ToolRect,
        ToolbarButtonId::ToolHighlighter,
        ToolbarButtonId::ToolText,
        ToolbarButtonId::ToolCensor
    };
    constexpr std::array<ToolbarButtonId, 4> kUtilityTools{
        ToolbarButtonId::Undo,
        ToolbarButtonId::Redo,
        ToolbarButtonId::ColorPicker,
        ToolbarButtonId::StrokeWidth
    };

    const float toolBarW = pad * 2.0f + btnSize;
    const float toolBarH = pad * 2.0f +
                           static_cast<float>(kDrawTools.size() + kUtilityTools.size()) * btnSize +
                           static_cast<float>(kDrawTools.size() + kUtilityTools.size() - 2) * btnGap +
                           sepGap;

    // Priority: right of selection -> left of selection -> tucked inside right edge of monitor
    float tbLeft = selection.right + gap;
    if (tbLeft + toolBarW > monBounds.right - gap) {
        const float leftCandidate = selection.left - gap - toolBarW;
        if (leftCandidate >= monBounds.left + gap) {
            tbLeft = leftCandidate;
        } else {
            tbLeft = std::max(monBounds.left + gap, monBounds.right - gap - toolBarW);
        }
    }
    float tbTop = selection.bottom - toolBarH;
    tbTop = std::clamp(tbTop, monBounds.top + gap, std::max(monBounds.top + gap, monBounds.bottom - gap - toolBarH));
    res.toolBarRect = tools::ClampRectToBounds(
        tools::RectF{tbLeft, tbTop, tbLeft + toolBarW, tbTop + toolBarH}, monBounds);

    float y = res.toolBarRect.top + pad;
    const float bx = res.toolBarRect.left + pad;
    float censorBtnCenterY = y + 0.5f * btnSize;
    float colorBtnCenterY = y + 0.5f * btnSize;
    float strokeBtnCenterY = y + 0.5f * btnSize;

    for (size_t i = 0; i < kDrawTools.size(); ++i) {
        const tools::RectF br{bx, y, bx + btnSize, y + btnSize};
        res.buttons.push_back(ToolbarButton{kDrawTools[i], br, false});
        if (kDrawTools[i] == ToolbarButtonId::ToolCensor) {
            censorBtnCenterY = 0.5f * (br.top + br.bottom);
        }
        y += btnSize + (i + 1 < kDrawTools.size() ? btnGap : sepGap);
    }
    for (size_t i = 0; i < kUtilityTools.size(); ++i) {
        const tools::RectF br{bx, y, bx + btnSize, y + btnSize};
        res.buttons.push_back(ToolbarButton{kUtilityTools[i], br, false});
        if (kUtilityTools[i] == ToolbarButtonId::ColorPicker) {
            colorBtnCenterY = 0.5f * (br.top + br.bottom);
        } else if (kUtilityTools[i] == ToolbarButtonId::StrokeWidth) {
            strokeBtnCenterY = 0.5f * (br.top + br.bottom);
        }
        y += btnSize + (i + 1 < kUtilityTools.size() ? btnGap : 0.0f);
    }

    // 2. Horizontal Action Bar: Copy, Save, Cancel
    constexpr std::array<ToolbarButtonId, 3> kActionTools{
        ToolbarButtonId::Copy,
        ToolbarButtonId::Save,
        ToolbarButtonId::Cancel
    };
    const float actionBtnW = 34.0f * scale;
    const float actionBarW = pad * 2.0f +
                             static_cast<float>(kActionTools.size()) * actionBtnW +
                             static_cast<float>(kActionTools.size() - 1) * btnGap;
    const float actionBarH = pad * 2.0f + btnSize;

    // Priority: below bottom-right -> above top-right -> tucked inside bottom-right (clear of toolBar)
    float abTop = selection.bottom + gap;
    bool tuckedInside = false;
    if (abTop + actionBarH > monBounds.bottom - gap) {
        const float aboveCandidate = selection.top - gap - actionBarH;
        if (aboveCandidate >= monBounds.top + gap) {
            abTop = aboveCandidate;
        } else {
            abTop = std::max(monBounds.top + gap, monBounds.bottom - gap - actionBarH);
            tuckedInside = true;
        }
    }

    float abLeft = selection.right - actionBarW;
    if (tuckedInside && res.toolBarRect.right >= monBounds.right - gap - toolBarW - 2.0f) {
        abLeft = res.toolBarRect.left - gap - actionBarW;
    }
    abLeft = std::clamp(abLeft, monBounds.left + gap, std::max(monBounds.left + gap, monBounds.right - gap - actionBarW));
    res.actionBarRect = tools::ClampRectToBounds(
        tools::RectF{abLeft, abTop, abLeft + actionBarW, abTop + actionBarH}, monBounds);

    float ax = res.actionBarRect.left + pad;
    const float ay = res.actionBarRect.top + pad;
    for (size_t i = 0; i < kActionTools.size(); ++i) {
        res.buttons.push_back(ToolbarButton{
            kActionTools[i],
            tools::RectF{ax, ay, ax + actionBtnW, ay + btnSize},
            false
        });
        ax += actionBtnW + btnGap;
    }

    // 3. Live Dimension Badge (W x H px)
    const float badgeW = 98.0f * scale;
    const float badgeH = 24.0f * scale;
    float badgeTop = selection.top - gap - badgeH;
    if (badgeTop < monBounds.top + gap) {
        badgeTop = std::min(selection.top + gap, monBounds.bottom - gap - badgeH);
    }
    float badgeLeft = std::clamp(selection.left, monBounds.left + gap,
                                 std::max(monBounds.left + gap, monBounds.right - gap - badgeW));
    res.badgeRect = tools::ClampRectToBounds(
        tools::RectF{badgeLeft, badgeTop, badgeLeft + badgeW, badgeTop + badgeH}, monBounds);

    // 4. Contextual Censor Mode Pill (Mosaic | Blur)
    if (state.censorActive) {
        const float pillBtnW = 54.0f * scale;
        const float pillH = pad * 2.0f + 26.0f * scale;
        const float pillW = pad * 2.0f + pillBtnW * 2.0f + btnGap;
        res.censorPillRect = PlacePopupBesideBar(res.toolBarRect, censorBtnCenterY, pillW, pillH, gap, monBounds);

        const float px = res.censorPillRect.left + pad;
        const float py = res.censorPillRect.top + pad;
        const float ph = res.censorPillRect.Height() - pad * 2.0f;
        res.buttons.push_back(ToolbarButton{
            ToolbarButtonId::CensorMosaic,
            tools::RectF{px, py, px + pillBtnW, py + ph},
            true
        });
        res.buttons.push_back(ToolbarButton{
            ToolbarButtonId::CensorBlur,
            tools::RectF{px + pillBtnW + btnGap, py, px + pillBtnW * 2.0f + btnGap, py + ph},
            true
        });
    }

    // 5. Contextual Color Palette Popup (6 swatches + custom button)
    if (state.colorPopupOpen) {
        constexpr std::array<ToolbarButtonId, 7> kColorButtons{
            ToolbarButtonId::ColorSwatch0,
            ToolbarButtonId::ColorSwatch1,
            ToolbarButtonId::ColorSwatch2,
            ToolbarButtonId::ColorSwatch3,
            ToolbarButtonId::ColorSwatch4,
            ToolbarButtonId::ColorSwatch5,
            ToolbarButtonId::ColorCustom
        };
        const float sw = 24.0f * scale;
        const float popW = pad * 2.0f + static_cast<float>(kColorButtons.size()) * sw +
                           static_cast<float>(kColorButtons.size() - 1) * btnGap;
        const float popH = pad * 2.0f + sw;
        res.colorPopupRect = PlacePopupBesideBar(res.toolBarRect, colorBtnCenterY, popW, popH, gap, monBounds);

        float cx = res.colorPopupRect.left + pad;
        const float cy = res.colorPopupRect.top + pad;
        for (const auto id : kColorButtons) {
            res.buttons.push_back(ToolbarButton{id, tools::RectF{cx, cy, cx + sw, cy + sw}, true});
            cx += sw + btnGap;
        }
    }

    // 6. Contextual Stroke / Size Presets Popup (Thin | Medium)
    if (state.strokePopupOpen) {
        constexpr std::array<ToolbarButtonId, 2> kPresets{
            ToolbarButtonId::PresetThin,
            ToolbarButtonId::PresetMedium
        };
        const float pw = 34.0f * scale;
        const float ph = 26.0f * scale;
        const float popW = pad * 2.0f + static_cast<float>(kPresets.size()) * pw +
                           static_cast<float>(kPresets.size() - 1) * btnGap;
        const float popH = pad * 2.0f + ph;
        res.strokePopupRect = PlacePopupBesideBar(res.toolBarRect, strokeBtnCenterY, popW, popH, gap, monBounds);

        float sx = res.strokePopupRect.left + pad;
        const float sy = res.strokePopupRect.top + pad;
        for (const auto id : kPresets) {
            res.buttons.push_back(ToolbarButton{id, tools::RectF{sx, sy, sx + pw, sy + ph}, true});
            sx += pw + btnGap;
        }
    }

    return res;
}

ToolbarButtonId HitTestToolbar(const ToolbarLayoutResult& layout, tools::PointF pt) noexcept {
    // Check popup buttons first so they win if cramped on a tiny display.
    for (const auto& btn : layout.buttons) {
        if (btn.isPopup && btn.rect.Contains(pt)) {
            return btn.id;
        }
    }
    for (const auto& btn : layout.buttons) {
        if (!btn.isPopup && btn.rect.Contains(pt)) {
            return btn.id;
        }
    }
    return ToolbarButtonId::None;
}

} // namespace saycheese::ui
