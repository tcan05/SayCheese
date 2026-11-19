#include "tools/tool_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <memory>
#include <utility>

namespace saycheese::tools {

namespace {

class AddAnnotationCommand final : public ICommand {
public:
    AddAnnotationCommand(std::vector<Annotation>& target, Annotation annotation)
        : target_(target), annotation_(std::move(annotation)) {}

    void Execute() override {
        target_.push_back(annotation_);
    }

    void Undo() override {
        if (!target_.empty()) {
            target_.pop_back();
        }
    }

private:
    std::vector<Annotation>& target_;
    Annotation annotation_;
};

bool HasNonWhitespace(const std::wstring& s) noexcept {
    for (wchar_t ch : s) {
        if (!std::iswspace(static_cast<wint_t>(ch))) {
            return true;
        }
    }
    return false;
}

} // namespace

void ToolController::SetActiveTool(ToolType tool) {
    if (textEditor_.active && tool != ToolType::Text) {
        CommitActiveText();
    }
    if (isDragging_) {
        isDragging_ = false;
        activePreview_.reset();
    }
    activeTool_ = tool;
}

void ToolController::SetCensorMode(CensorMode mode) noexcept {
    censorMode_ = mode;
}

void ToolController::SetActiveColor(ColorRGBA color) noexcept {
    activeColor_ = color;
    if (textEditor_.active) {
        textEditor_.color = color;
    }
}

float ToolController::ActiveSize() const noexcept {
    switch (activeTool_) {
    case ToolType::Text:
        return fontSize_;
    case ToolType::Censor:
        return censorMode_ == CensorMode::Mosaic ? mosaicBlockSize_ : blurSigma_;
    default:
        return strokeWidth_;
    }
}

void ToolController::AdjustSizeByWheel(int wheelSteps) noexcept {
    if (wheelSteps == 0) {
        return;
    }
    switch (activeTool_) {
    case ToolType::Text:
        fontSize_ = std::clamp(fontSize_ + static_cast<float>(wheelSteps) * 2.0f, 14.0f, 72.0f);
        if (textEditor_.active) {
            textEditor_.fontSize = fontSize_;
        }
        break;
    case ToolType::Censor:
        if (censorMode_ == CensorMode::Mosaic) {
            mosaicBlockSize_ = std::clamp(mosaicBlockSize_ + static_cast<float>(wheelSteps) * 2.0f, 6.0f, 32.0f);
        } else {
            blurSigma_ = std::clamp(blurSigma_ + static_cast<float>(wheelSteps) * 1.5f, 4.0f, 24.0f);
        }
        break;
    default:
        strokeWidth_ = std::clamp(strokeWidth_ + static_cast<float>(wheelSteps) * 1.0f, 1.0f, 24.0f);
        break;
    }
}

void ToolController::ApplySizePreset(int presetIndex) noexcept {
    const int idx = std::clamp(presetIndex, 0, 1);
    static constexpr float kStrokePresets[2] = {2.0f, 5.0f};
    static constexpr float kFontPresets[2] = {18.0f, 28.0f};
    static constexpr float kMosaicPresets[2] = {8.0f, 14.0f};
    static constexpr float kBlurPresets[2] = {6.0f, 12.0f};

    switch (activeTool_) {
    case ToolType::Text:
        fontSize_ = kFontPresets[idx];
        if (textEditor_.active) {
            textEditor_.fontSize = fontSize_;
        }
        break;
    case ToolType::Censor:
        if (censorMode_ == CensorMode::Mosaic) {
            mosaicBlockSize_ = kMosaicPresets[idx];
        } else {
            blurSigma_ = kBlurPresets[idx];
        }
        break;
    default:
        strokeWidth_ = kStrokePresets[idx];
        break;
    }
}

void ToolController::OnPointerDown(PointF pt, bool shiftHeld) {
    if (activeTool_ == ToolType::Select) {
        return;
    }

    if (activeTool_ == ToolType::Text) {
        if (textEditor_.active) {
            CommitActiveText();
        }
        textEditor_.active = true;
        textEditor_.anchor = pt;
        textEditor_.text.clear();
        textEditor_.caretIndex = 0;
        textEditor_.color = activeColor_;
        textEditor_.fontSize = fontSize_;
        return;
    }

    isDragging_ = true;
    dragOrigin_ = pt;

    Annotation ann{};
    ann.id = AllocateId();
    ann.type = activeTool_;
    ann.color = activeColor_;
    ann.strokeWidth = strokeWidth_;
    ann.start = pt;
    ann.end = pt;
    ann.rect = RectF{pt.x, pt.y, pt.x, pt.y};

    if (activeTool_ == ToolType::Highlighter) {
        ann.color.a = 89; // ~0.35 alpha
        ann.strokeWidth = std::max(12.0f, strokeWidth_ * 4.0f);
        ann.points.push_back(pt);
    } else if (activeTool_ == ToolType::Pen) {
        ann.points.push_back(pt);
    } else if (activeTool_ == ToolType::Censor) {
        ann.censorMode = censorMode_;
        ann.censorIntensity = (censorMode_ == CensorMode::Mosaic) ? mosaicBlockSize_ : blurSigma_;
    }

    activePreview_ = std::move(ann);
    UpdateActivePreviewGeometry(pt, shiftHeld);
}

void ToolController::OnPointerMove(PointF pt, bool shiftHeld) {
    if (!isDragging_ || !activePreview_.has_value()) {
        return;
    }
    UpdateActivePreviewGeometry(pt, shiftHeld);
}

void ToolController::OnPointerUp(PointF pt, bool shiftHeld) {
    if (!isDragging_ || !activePreview_.has_value()) {
        return;
    }
    UpdateActivePreviewGeometry(pt, shiftHeld);
    isDragging_ = false;

    Annotation committed = std::move(*activePreview_);
    activePreview_.reset();

    // Ignore degenerate / zero-area shapes to prevent empty commands or 0x0 targets.
    bool valid = false;
    switch (committed.type) {
    case ToolType::Pen:
    case ToolType::Highlighter:
        if (committed.points.size() == 1) {
            // Allow deliberate single-dot tap for pen, ignore zero-length drag? Actually require >= 2 points or tiny dot.
            committed.points.push_back(PointF{pt.x + 0.5f, pt.y});
        }
        valid = committed.points.size() >= 2;
        break;
    case ToolType::Line:
    case ToolType::Arrow: {
        const float dist = std::hypot(committed.end.x - committed.start.x, committed.end.y - committed.start.y);
        valid = dist >= 2.0f;
        break;
    }
    case ToolType::Rectangle:
    case ToolType::Censor:
        committed.rect = committed.rect.Normalized();
        valid = committed.rect.Width() >= 2.0f && committed.rect.Height() >= 2.0f;
        break;
    default:
        valid = false;
        break;
    }

    if (valid) {
        commandStack_.PushAndExecute(std::make_unique<AddAnnotationCommand>(annotations_, std::move(committed)));
    }
}

void ToolController::CancelActiveDrag() noexcept {
    isDragging_ = false;
    activePreview_.reset();
}

void ToolController::UpdateActivePreviewGeometry(PointF currentPt, bool shiftHeld) {
    if (!activePreview_.has_value()) {
        return;
    }
    auto& ann = *activePreview_;
    switch (ann.type) {
    case ToolType::Pen:
    case ToolType::Highlighter:
        if (ann.points.empty()) {
            ann.points.push_back(currentPt);
        } else {
            const PointF& last = ann.points.back();
            const float dist = std::hypot(currentPt.x - last.x, currentPt.y - last.y);
            if (dist >= 1.5f) {
                ann.points.push_back(currentPt);
            }
        }
        break;
    case ToolType::Line:
    case ToolType::Arrow:
        ann.start = dragOrigin_;
        ann.end = shiftHeld ? SnapLine45Deg(dragOrigin_, currentPt) : currentPt;
        break;
    case ToolType::Rectangle:
        ann.rect = shiftHeld
            ? ConstrainSquare(dragOrigin_, currentPt)
            : RectF{dragOrigin_.x, dragOrigin_.y, currentPt.x, currentPt.y}.Normalized();
        break;
    case ToolType::Censor:
        ann.rect = RectF{dragOrigin_.x, dragOrigin_.y, currentPt.x, currentPt.y}.Normalized();
        break;
    default:
        break;
    }
}

void ToolController::OnTextChar(wchar_t ch) {
    if (!textEditor_.active) {
        return;
    }
    // Ignore control chars handled separately (ESC, Backspace, Enter, Ctrl combos).
    if (ch < 0x20 || ch == 0x7F) {
        return;
    }
    textEditor_.caretIndex = std::min(textEditor_.caretIndex, textEditor_.text.size());
    textEditor_.text.insert(textEditor_.caretIndex, 1, ch);
    ++textEditor_.caretIndex;
}

void ToolController::OnTextBackspace() {
    if (!textEditor_.active || textEditor_.caretIndex == 0 || textEditor_.text.empty()) {
        return;
    }
    textEditor_.caretIndex = std::min(textEditor_.caretIndex, textEditor_.text.size());
    if (textEditor_.caretIndex > 0) {
        textEditor_.text.erase(textEditor_.caretIndex - 1, 1);
        --textEditor_.caretIndex;
    }
}

void ToolController::OnTextDelete() {
    if (!textEditor_.active || textEditor_.caretIndex >= textEditor_.text.size()) {
        return;
    }
    textEditor_.text.erase(textEditor_.caretIndex, 1);
}

void ToolController::OnTextNewline() {
    if (!textEditor_.active) {
        return;
    }
    textEditor_.caretIndex = std::min(textEditor_.caretIndex, textEditor_.text.size());
    textEditor_.text.insert(textEditor_.caretIndex, 1, L'\n');
    ++textEditor_.caretIndex;
}

void ToolController::OnTextCaretLeft() noexcept {
    if (!textEditor_.active || textEditor_.caretIndex == 0) {
        return;
    }
    --textEditor_.caretIndex;
}

void ToolController::OnTextCaretRight() noexcept {
    if (!textEditor_.active) {
        return;
    }
    if (textEditor_.caretIndex < textEditor_.text.size()) {
        ++textEditor_.caretIndex;
    }
}

void ToolController::CommitActiveText() {
    if (!textEditor_.active) {
        return;
    }
    const bool hasContent = HasNonWhitespace(textEditor_.text);
    if (hasContent) {
        Annotation ann{};
        ann.id = AllocateId();
        ann.type = ToolType::Text;
        ann.color = textEditor_.color;
        ann.start = textEditor_.anchor;
        ann.text = textEditor_.text;
        ann.fontSize = textEditor_.fontSize;
        commandStack_.PushAndExecute(std::make_unique<AddAnnotationCommand>(annotations_, std::move(ann)));
    }
    textEditor_.active = false;
    textEditor_.text.clear();
    textEditor_.caretIndex = 0;
}

EscapeAction ToolController::HandleEscape() {
    if (textEditor_.active) {
        CommitActiveText();
        return EscapeAction::CommittedText;
    }
    if (isDragging_) {
        isDragging_ = false;
        activePreview_.reset();
    }
    return EscapeAction::CloseOverlay;
}

bool ToolController::Undo() {
    if (textEditor_.active) {
        CommitActiveText();
    }
    return commandStack_.Undo();
}

bool ToolController::Redo() {
    if (textEditor_.active) {
        CommitActiveText();
    }
    return commandStack_.Redo();
}

void ToolController::AddAnnotationDirect(Annotation ann) {
    if (ann.id == 0) {
        ann.id = AllocateId();
    }
    commandStack_.PushAndExecute(std::make_unique<AddAnnotationCommand>(annotations_, std::move(ann)));
}

void ToolController::ClearAll() noexcept {
    isDragging_ = false;
    activePreview_.reset();
    textEditor_.active = false;
    textEditor_.text.clear();
    textEditor_.caretIndex = 0;
    annotations_.clear();
    commandStack_.Clear();
}

} // namespace saycheese::tools
