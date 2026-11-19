#include "test_runner.hpp"

#include "capture/captured_frame.hpp"
#include "platform/win32_raii.hpp"
#include "tools/tool_controller.hpp"
#include "ui/overlay_window.hpp"

#include <cmath>

using namespace saycheese::tools;
using namespace saycheese::ui;
using namespace saycheese::capture;
using namespace saycheese::platform;

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

    // Stroke presets: 2 / 5 (out-of-range index clamps to Medium), wheel clamps 1..24
    tc.SetActiveTool(ToolType::Pen);
    tc.ApplySizePreset(0);
    SC_EXPECT_NEAR(tc.ActiveSize(), 2.0f, 1e-3);
    tc.ApplySizePreset(1);
    SC_EXPECT_NEAR(tc.ActiveSize(), 5.0f, 1e-3);
    tc.ApplySizePreset(2);
    SC_EXPECT_NEAR(tc.ActiveSize(), 5.0f, 1e-3);
    tc.AdjustSizeByWheel(100);
    SC_EXPECT_NEAR(tc.ActiveSize(), 24.0f, 1e-3);
    tc.AdjustSizeByWheel(-100);
    SC_EXPECT_NEAR(tc.ActiveSize(), 1.0f, 1e-3);

    // Text presets: 18 / 28, clamped 14..72
    tc.SetActiveTool(ToolType::Text);
    tc.ApplySizePreset(0);
    SC_EXPECT_NEAR(tc.ActiveSize(), 18.0f, 1e-3);
    tc.ApplySizePreset(1);
    SC_EXPECT_NEAR(tc.ActiveSize(), 28.0f, 1e-3);
    tc.ApplySizePreset(2);
    SC_EXPECT_NEAR(tc.ActiveSize(), 28.0f, 1e-3);
    tc.AdjustSizeByWheel(100);
    SC_EXPECT_NEAR(tc.ActiveSize(), 72.0f, 1e-3);
    tc.AdjustSizeByWheel(-100);
    SC_EXPECT_NEAR(tc.ActiveSize(), 14.0f, 1e-3);

    // Censor Mosaic presets: 8 / 14 (clamped 6..32); Blur presets: 6 / 12 (clamped 4..24)
    tc.SetActiveTool(ToolType::Censor);
    tc.SetCensorMode(CensorMode::Mosaic);
    tc.ApplySizePreset(0);
    SC_EXPECT_NEAR(tc.ActiveSize(), 8.0f, 1e-3);
    tc.ApplySizePreset(1);
    SC_EXPECT_NEAR(tc.ActiveSize(), 14.0f, 1e-3);
    tc.ApplySizePreset(2);
    SC_EXPECT_NEAR(tc.ActiveSize(), 14.0f, 1e-3);
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

SC_TEST("IsPureCtrlShortcut distinguishes Ctrl shortcuts from AltGr (Ctrl+Alt)") {
    SC_EXPECT_TRUE(IsPureCtrlShortcut(true, false));
    SC_EXPECT_FALSE(IsPureCtrlShortcut(true, true));   // AltGr sets both Ctrl and Alt
    SC_EXPECT_FALSE(IsPureCtrlShortcut(false, false));
    SC_EXPECT_FALSE(IsPureCtrlShortcut(false, true));
}

SC_TEST("Undo while active text editor has content commits and undoes cleanly") {
    ToolController tc;
    tc.SetActiveTool(ToolType::Text);
    tc.OnPointerDown({100.0f, 100.0f}, false);
    tc.OnTextChar(L'@'); // AltGr character on European keyboards
    tc.OnTextChar(L'x');
    SC_EXPECT_TRUE(tc.TextEditor().active);
    SC_EXPECT_TRUE(tc.TextEditor().text == L"@x");
    // Undo commits active text and immediately undoes it cleanly.
    SC_EXPECT_TRUE(tc.Undo());
    SC_EXPECT_FALSE(tc.TextEditor().active);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(0));
    SC_EXPECT_TRUE(tc.Redo());
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(1));
    SC_EXPECT_TRUE(tc.Annotations()[0].text == L"@x");
}

SC_TEST("CancelActiveDrag clears preview without disturbing committed annotations") {
    ToolController tc;
    tc.SetActiveTool(ToolType::Rectangle);
    tc.OnPointerDown({20.0f, 20.0f}, false);
    tc.OnPointerMove({80.0f, 70.0f}, false);
    tc.OnPointerUp({80.0f, 70.0f}, false);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(1));

    tc.OnPointerDown({100.0f, 100.0f}, false);
    tc.OnPointerMove({180.0f, 160.0f}, false);
    SC_EXPECT_TRUE(tc.ActivePreview() != nullptr);

    tc.CancelActiveDrag();
    SC_EXPECT_TRUE(tc.ActivePreview() == nullptr);
    SC_EXPECT_EQ(tc.Annotations().size(), static_cast<size_t>(1));
}

SC_TEST("OverlayWindow commits sequential drawings across tools and handles capture loss cleanly") {
    ComInitScope comScope;
    SC_EXPECT_TRUE(comScope.Succeeded());

    OverlayWindow overlay;
    CapturedFrame frame = CreateSyntheticTestFrame(640, 480);
    OverlayCallbacks cbs{};
    SC_EXPECT_TRUE(overlay.CreateAndShow(::GetModuleHandleW(nullptr), std::move(frame), std::move(cbs), true));
    HWND hwnd = overlay.Handle();
    SC_EXPECT_TRUE(hwnd != nullptr);

    auto sendMouse = [&](UINT msg, int x, int y) {
        ::SendMessageW(hwnd, msg, 0, MAKELPARAM(x, y));
    };

    auto clickButton = [&](ToolbarButtonId id) {
        for (const auto& btn : overlay.Layout().buttons) {
            if (btn.id == id) {
                const int bx = static_cast<int>(std::lround(0.5f * (btn.rect.left + btn.rect.right)));
                const int by = static_cast<int>(std::lround(0.5f * (btn.rect.top + btn.rect.bottom)));
                sendMouse(WM_LBUTTONDOWN, bx, by);
                sendMouse(WM_LBUTTONUP, bx, by);
                return true;
            }
        }
        return false;
    };

    // Tiny <8x8 drag must clear selection on WM_LBUTTONUP (regression check for ReleaseCapture order).
    sendMouse(WM_LBUTTONDOWN, 100, 100);
    sendMouse(WM_MOUSEMOVE, 103, 103);
    sendMouse(WM_LBUTTONUP, 103, 103);
    SC_EXPECT_FALSE(overlay.Selection().active);

    // Valid selection (50, 50) -> (450, 350).
    sendMouse(WM_LBUTTONDOWN, 50, 50);
    sendMouse(WM_MOUSEMOVE, 450, 350);
    sendMouse(WM_LBUTTONUP, 450, 350);
    SC_EXPECT_TRUE(overlay.Selection().active);

    // Draw two consecutive Pen strokes -> both must persist (Bug 3 regression check).
    SC_EXPECT_TRUE(clickButton(ToolbarButtonId::ToolPen));
    sendMouse(WM_LBUTTONDOWN, 100, 100);
    sendMouse(WM_MOUSEMOVE, 150, 130);
    sendMouse(WM_LBUTTONUP, 150, 130);
    SC_EXPECT_EQ(overlay.Tools().Annotations().size(), static_cast<size_t>(1));
    SC_EXPECT_TRUE(overlay.Tools().ActivePreview() == nullptr);

    sendMouse(WM_LBUTTONDOWN, 180, 110);
    sendMouse(WM_MOUSEMOVE, 240, 140);
    sendMouse(WM_LBUTTONUP, 240, 140);
    SC_EXPECT_EQ(overlay.Tools().Annotations().size(), static_cast<size_t>(2));
    SC_EXPECT_TRUE(overlay.Tools().ActivePreview() == nullptr);

    // Draw one shape with each remaining non-text drawing tool and confirm all persist.
    const ToolbarButtonId otherTools[] = {
        ToolbarButtonId::ToolLine,
        ToolbarButtonId::ToolArrow,
        ToolbarButtonId::ToolRect,
        ToolbarButtonId::ToolHighlighter,
        ToolbarButtonId::ToolCensor
    };
    int offsetY = 160;
    for (ToolbarButtonId toolBtn : otherTools) {
        SC_EXPECT_TRUE(clickButton(toolBtn));
        sendMouse(WM_LBUTTONDOWN, 110, offsetY);
        sendMouse(WM_MOUSEMOVE, 220, offsetY + 24);
        sendMouse(WM_LBUTTONUP, 220, offsetY + 24);
        offsetY += 30;
    }
    SC_EXPECT_EQ(overlay.Tools().Annotations().size(), static_cast<size_t>(7));
    SC_EXPECT_TRUE(overlay.Tools().ActivePreview() == nullptr);

    // Unexpected WM_CAPTURECHANGED mid-stroke discards only the active preview, keeping all 7 committed items.
    SC_EXPECT_TRUE(clickButton(ToolbarButtonId::ToolPen));
    sendMouse(WM_LBUTTONDOWN, 280, 160);
    sendMouse(WM_MOUSEMOVE, 340, 200);
    SC_EXPECT_TRUE(overlay.Tools().ActivePreview() != nullptr);
    ::SendMessageW(hwnd, WM_CAPTURECHANGED, 0, 0);
    SC_EXPECT_TRUE(overlay.Tools().ActivePreview() == nullptr);
    SC_EXPECT_EQ(overlay.Tools().Annotations().size(), static_cast<size_t>(7));

    overlay.DestroyAndRelease();
}

int main() {
    return saycheese::test::RunAll("tool_controller");
}
