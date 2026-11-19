#pragma once

#include "platform/dpi_monitor.hpp"
#include "tools/geometry.hpp"

#include <vector>

namespace saycheese::ui {

enum class ToolbarButtonId {
    None = 0,
    ToolPen,
    ToolLine,
    ToolArrow,
    ToolRect,
    ToolHighlighter,
    ToolText,
    ToolCensor,
    Undo,
    Redo,
    ColorPicker,
    StrokeWidth,
    Copy,
    Save,
    Cancel,
    ColorSwatch0,
    ColorSwatch1,
    ColorSwatch2,
    ColorSwatch3,
    ColorSwatch4,
    ColorSwatch5,
    ColorCustom,
    CensorMosaic,
    CensorBlur,
    PresetThin,
    PresetMedium,
    PresetThick
};

struct ToolbarButton {
    ToolbarButtonId id = ToolbarButtonId::None;
    tools::RectF rect{};
    bool isPopup = false;
};

struct ToolbarLayoutState {
    bool colorPopupOpen = false;
    bool strokePopupOpen = false;
    bool censorActive = false;
};

struct ToolbarLayoutResult {
    float uiScale = 1.0f;
    tools::RectF toolBarRect{};
    tools::RectF actionBarRect{};
    tools::RectF badgeRect{};
    tools::RectF colorPopupRect{};
    tools::RectF strokePopupRect{};
    tools::RectF censorPillRect{};
    std::vector<ToolbarButton> buttons;
};

ToolbarLayoutResult ComputeToolbarLayout(
    tools::RectF selection,
    const std::vector<platform::MonitorInfo>& monitors,
    tools::RectF canvasBounds,
    const ToolbarLayoutState& state);

ToolbarButtonId HitTestToolbar(const ToolbarLayoutResult& layout, tools::PointF pt) noexcept;

} // namespace saycheese::ui
