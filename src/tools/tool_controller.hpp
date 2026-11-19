#pragma once

#include "tools/annotation.hpp"
#include "tools/command_stack.hpp"
#include "tools/geometry.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace saycheese::tools {

struct TextEditorState {
    bool active = false;
    PointF anchor{};
    std::wstring text;
    size_t caretIndex = 0;
    ColorRGBA color{255, 59, 48, 255};
    float fontSize = 28.0f;
};

enum class EscapeAction {
    CommittedText = 0,
    CloseOverlay
};

constexpr bool IsPureCtrlShortcut(bool ctrlDown, bool altDown) noexcept {
    return ctrlDown && !altDown;
}

class ToolController {
public:
    const std::vector<Annotation>& Annotations() const noexcept { return annotations_; }
    const Annotation* ActivePreview() const noexcept { return activePreview_.has_value() ? &(*activePreview_) : nullptr; }
    const TextEditorState& TextEditor() const noexcept { return textEditor_; }

    ToolType ActiveTool() const noexcept { return activeTool_; }
    void SetActiveTool(ToolType tool);

    CensorMode ActiveCensorMode() const noexcept { return censorMode_; }
    void SetCensorMode(CensorMode mode) noexcept;

    ColorRGBA ActiveColor() const noexcept { return activeColor_; }
    void SetActiveColor(ColorRGBA color) noexcept;

    float ActiveSize() const noexcept;
    void AdjustSizeByWheel(int wheelSteps) noexcept;
    void ApplySizePreset(int presetIndex) noexcept;

    void OnPointerDown(PointF pt, bool shiftHeld);
    void OnPointerMove(PointF pt, bool shiftHeld);
    void OnPointerUp(PointF pt, bool shiftHeld);

    void OnTextChar(wchar_t ch);
    void OnTextBackspace();
    void OnTextDelete();
    void OnTextNewline();
    void OnTextCaretLeft() noexcept;
    void OnTextCaretRight() noexcept;
    void CommitActiveText();

    EscapeAction HandleEscape();

    bool Undo();
    bool Redo();
    bool CanUndo() const noexcept { return commandStack_.CanUndo(); }
    bool CanRedo() const noexcept { return commandStack_.CanRedo(); }

    void AddAnnotationDirect(Annotation ann);
    void ClearAll() noexcept;

private:
    uint64_t AllocateId() noexcept { return ++nextId_; }
    void UpdateActivePreviewGeometry(PointF currentPt, bool shiftHeld);

    ToolType activeTool_ = ToolType::Select;
    CensorMode censorMode_ = CensorMode::Mosaic;
    ColorRGBA activeColor_ = kQuickPalette[0];

    float strokeWidth_ = 4.0f;
    float fontSize_ = 28.0f;
    float mosaicBlockSize_ = 14.0f;
    float blurSigma_ = 12.0f;

    uint64_t nextId_ = 0;
    bool isDragging_ = false;
    PointF dragOrigin_{};

    std::vector<Annotation> annotations_;
    std::optional<Annotation> activePreview_;
    TextEditorState textEditor_{};
    CommandStack commandStack_;
};

} // namespace saycheese::tools
