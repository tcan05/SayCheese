#include "test_runner.hpp"

#include "tools/tool_controller.hpp"

using namespace saycheese::tools;

SC_TEST("Pen and Highlighter strokes commit unique annotations with expected style") {
    ToolController tc;
    tc.SetActiveTool(ToolType::Pen);
    tc.SetActiveColor(ColorRGBA{255, 59, 48, 255});
    tc.ApplySizePreset(1); // Medium = 5px

    tc.OnPointerDown({10.0f, 10.0f}, false);
    tc.OnPointerMove({25.0f, 20.0f}, false);
    tc.OnPointerMove({40.0f, 35.0f}, false);
    SC_EXPECT_TRUE(tc.ActivePreview() != nullptr);
    tc.OnPointerUp({40.0f, 35.0f}, false);
    SC_EXPECT_TRUE(tc.ActivePreview() == nullptr);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(1));
    SC_EXPECT_EQ(static_cast<int>(tc.Annotations()[0].type), static_cast<int>(ToolType::Pen));
    SC_EXPECT_NEAR(tc.Annotations()[0].strokeWidth, 5.0f, 1e-3);

    tc.SetActiveTool(ToolType::Highlighter);
    tc.SetActiveColor(ColorRGBA{255, 204, 0, 255});
    tc.OnPointerDown({50.0f, 50.0f}, false);
    tc.OnPointerMove({100.0f, 50.0f}, false);
    tc.OnPointerUp({100.0f, 50.0f}, false);

    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(2));
    const auto& hl = tc.Annotations()[1];
    SC_EXPECT_EQ(static_cast<int>(hl.type), static_cast<int>(ToolType::Highlighter));
    SC_EXPECT_EQ(static_cast<int>(hl.color.a), 89); // ~0.35 * 255
    SC_EXPECT_NEAR(hl.strokeWidth, 20.0f, 1e-3);    // max(12, 5 * 4)
    SC_EXPECT_TRUE(hl.id != tc.Annotations()[0].id);
}

SC_TEST("Line, Arrow, and Rectangle obey Shift modifiers and ignore zero-area clicks") {
    ToolController tc;

    // Zero-area click-release pushes 0 commands for Line, Arrow, Rectangle, Censor.
    for (ToolType t : {ToolType::Line, ToolType::Arrow, ToolType::Rectangle, ToolType::Censor}) {
        tc.SetActiveTool(t);
        tc.OnPointerDown({100.0f, 100.0f}, false);
        tc.OnPointerUp({100.0f, 100.0f}, false);
    }
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(0));

    // Line with Shift snaps to 45-degree increment.
    tc.SetActiveTool(ToolType::Line);
    tc.OnPointerDown({0.0f, 0.0f}, true);
    tc.OnPointerMove({100.0f, 15.0f}, true);
    tc.OnPointerUp({100.0f, 15.0f}, true);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(1));
    SC_EXPECT_NEAR(tc.Annotations()[0].end.y, 0.0f, 1e-3);

    // Arrow with Shift snaps cleanly.
    tc.SetActiveTool(ToolType::Arrow);
    tc.OnPointerDown({0.0f, 0.0f}, true);
    tc.OnPointerUp({90.0f, 80.0f}, true);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(2));
    SC_EXPECT_NEAR(tc.Annotations()[1].end.x, tc.Annotations()[1].end.y, 1e-3);

    // Rectangle with Shift creates square.
    tc.SetActiveTool(ToolType::Rectangle);
    tc.OnPointerDown({50.0f, 50.0f}, true);
    tc.OnPointerUp({130.0f, 90.0f}, true);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(3));
    SC_EXPECT_NEAR(tc.Annotations()[2].rect.Width(), tc.Annotations()[2].rect.Height(), 1e-3);
    SC_EXPECT_NEAR(tc.Annotations()[2].rect.Width(), 80.0f, 1e-3);
}

SC_TEST("Inline Text editor handles empty bounds, caret movement, newlines, color updates, and ESC contract") {
    ToolController tc;
    tc.SetActiveTool(ToolType::Text);
    tc.SetActiveColor(ColorRGBA{255, 255, 255, 255});

    // Click to open inline text editor.
    tc.OnPointerDown({120.0f, 80.0f}, false);
    tc.OnPointerUp({120.0f, 80.0f}, false);
    SC_EXPECT_TRUE(tc.TextEditor().active);

    // Safe backspace/delete/caret navigation on empty buffer.
    tc.OnTextBackspace();
    tc.OnTextDelete();
    tc.OnTextCaretLeft();
    tc.OnTextCaretRight();
    SC_EXPECT_EQ(tc.TextEditor().caretIndex, static_cast<size_t>(0));

    // Committing empty text pushes 0 annotations.
    tc.CommitActiveText();
    SC_EXPECT_FALSE(tc.TextEditor().active);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(0));

    // ESC when no text editor is active signals immediate overlay close.
    SC_EXPECT_EQ(static_cast<int>(tc.HandleEscape()), static_cast<int>(EscapeAction::CloseOverlay));

    // Re-open, type "Hi", move caret left, insert '!', Enter, type "2".
    tc.OnPointerDown({150.0f, 90.0f}, false);
    tc.OnTextChar(L'H');
    tc.OnTextChar(L'i');
    tc.OnTextCaretLeft();
    tc.OnTextChar(L'!');
    tc.OnTextCaretRight();
    tc.OnTextNewline();
    tc.OnTextChar(L'2');
    SC_EXPECT_TRUE(tc.TextEditor().text == L"H!i\n2");

    // Changing active color updates open editor live.
    tc.SetActiveColor(ColorRGBA{52, 199, 89, 255});
    SC_EXPECT_EQ(static_cast<int>(tc.TextEditor().color.g), 199);

    // First ESC commits active text, second ESC closes overlay.
    SC_EXPECT_EQ(static_cast<int>(tc.HandleEscape()), static_cast<int>(EscapeAction::CommittedText));
    SC_EXPECT_FALSE(tc.TextEditor().active);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(1));
    SC_EXPECT_TRUE(tc.Annotations()[0].text == L"H!i\n2");
    SC_EXPECT_EQ(static_cast<int>(tc.Annotations()[0].color.g), 199);

    SC_EXPECT_EQ(static_cast<int>(tc.HandleEscape()), static_cast<int>(EscapeAction::CloseOverlay));
}

SC_TEST("Censor mode toggle, wheel sizing clamps, presets, and Undo/Redo work consistently") {
    ToolController tc;

    // Stroke presets: 2 / 5 / 10, clamped 1..24
    tc.SetActiveTool(ToolType::Pen);
    tc.ApplySizePreset(0);
    SC_EXPECT_NEAR(tc.ActiveSize(), 2.0f, 1e-3);
    tc.ApplySizePreset(1);
    SC_EXPECT_NEAR(tc.ActiveSize(), 5.0f, 1e-3);
    tc.ApplySizePreset(2);
    SC_EXPECT_NEAR(tc.ActiveSize(), 10.0f, 1e-3);
    tc.AdjustSizeByWheel(100);
    SC_EXPECT_NEAR(tc.ActiveSize(), 24.0f, 1e-3);
    tc.AdjustSizeByWheel(-100);
    SC_EXPECT_NEAR(tc.ActiveSize(), 1.0f, 1e-3);

    // Text presets: 18 / 28 / 42, clamped 14..72
    tc.SetActiveTool(ToolType::Text);
    tc.ApplySizePreset(0);
    SC_EXPECT_NEAR(tc.ActiveSize(), 18.0f, 1e-3);
    tc.ApplySizePreset(1);
    SC_EXPECT_NEAR(tc.ActiveSize(), 28.0f, 1e-3);
    tc.ApplySizePreset(2);
    SC_EXPECT_NEAR(tc.ActiveSize(), 42.0f, 1e-3);
    tc.AdjustSizeByWheel(100);
    SC_EXPECT_NEAR(tc.ActiveSize(), 72.0f, 1e-3);
    tc.AdjustSizeByWheel(-100);
    SC_EXPECT_NEAR(tc.ActiveSize(), 14.0f, 1e-3);

    // Censor Mosaic presets: 8 / 14 / 22 (clamped 6..32); Blur presets: 6 / 12 / 18 (clamped 4..24)
    tc.SetActiveTool(ToolType::Censor);
    tc.SetCensorMode(CensorMode::Mosaic);
    tc.ApplySizePreset(0);
    SC_EXPECT_NEAR(tc.ActiveSize(), 8.0f, 1e-3);
    tc.ApplySizePreset(2);
    SC_EXPECT_NEAR(tc.ActiveSize(), 22.0f, 1e-3);
    tc.OnPointerDown({10.0f, 10.0f}, false);
    tc.OnPointerUp({80.0f, 60.0f}, false);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(1));
    SC_EXPECT_EQ(static_cast<int>(tc.Annotations()[0].censorMode), static_cast<int>(CensorMode::Mosaic));

    tc.SetCensorMode(CensorMode::GaussianBlur);
    tc.ApplySizePreset(1);
    SC_EXPECT_NEAR(tc.ActiveSize(), 12.0f, 1e-3);
    tc.OnPointerDown({90.0f, 10.0f}, false);
    tc.OnPointerUp({160.0f, 60.0f}, false);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(2));
    SC_EXPECT_EQ(static_cast<int>(tc.Annotations()[1].censorMode), static_cast<int>(CensorMode::GaussianBlur));

    SC_EXPECT_TRUE(tc.Undo());
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(1));
    SC_EXPECT_TRUE(tc.Redo());
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(2));
}

int main() {
    return saycheese::test::RunAll("tool_controller");
}
