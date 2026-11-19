#pragma once

#include "tools/annotation.hpp"
#include "tools/geometry.hpp"
#include "tools/tool_controller.hpp"
#include "ui/toolbar_layout.hpp"

#include <d2d1_1.h>
#include <dwrite.h>

namespace saycheese::render {

struct UIRenderState {
    tools::SelectionBox selection{};
    ui::ToolbarLayoutResult layout{};
    tools::ToolType activeTool = tools::ToolType::Select;
    tools::CensorMode censorMode = tools::CensorMode::Mosaic;
    tools::ColorRGBA activeColor{};
    float activeSize = 4.0f;
    bool canUndo = false;
    bool canRedo = false;
    ui::ToolbarButtonId hoveredButton = ui::ToolbarButtonId::None;
    tools::TextEditorState textEditor{};
    bool showCaret = true;
    bool showSizeHud = false;
};

class UIRenderer {
public:
    void RenderOverlayChrome(
        ID2D1DeviceContext* dc,
        ID2D1Factory1* d2dFactory,
        IDWriteFactory* dwriteFactory,
        float canvasWidth,
        float canvasHeight,
        const UIRenderState& state);
};

} // namespace saycheese::render
