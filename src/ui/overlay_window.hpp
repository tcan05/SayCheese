#pragma once

#include "capture/captured_frame.hpp"
#include "platform/win32_raii.hpp"
#include "render/d2d_context.hpp"
#include "render/scene_renderer.hpp"
#include "render/ui_renderer.hpp"
#include "tools/geometry.hpp"
#include "tools/tool_controller.hpp"
#include "ui/toolbar_layout.hpp"

#include <windows.h>

#include <array>
#include <functional>
#include <string>

namespace saycheese::ui {

enum class OverlayExitReason {
    None = 0,
    Cancelled,
    CopiedToClipboard,
    SavedToFile,
    RenderError,
    ProgrammaticClose
};

struct OverlayCallbacks {
    std::function<void(const std::wstring& title, const std::wstring& message)> notifyUser;
    std::function<void(OverlayExitReason reason)> onClosed;
    std::wstring* lastSaveDir = nullptr;
};

class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow() noexcept { DestroyAndRelease(); }

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool CreateAndShow(
        HINSTANCE hInstance,
        capture::CapturedFrame frame,
        OverlayCallbacks callbacks,
        bool headlessTestMode = false);

    void RenderFrame();
    void DestroyAndRelease() noexcept;

    bool IsOpen() const noexcept { return hwnd_.IsValid(); }
    HWND Handle() const noexcept { return hwnd_.Get(); }
    OverlayExitReason ExitReason() const noexcept { return exitReason_; }

    // Deterministic verification hooks used by --self-test-leaks and CTest.
    void ApplySyntheticTestAnnotations();
    bool RunOffscreenExportCheck();

private:
    enum class DragMode {
        None = 0,
        CreatingSelection,
        ResizingSelection,
        MovingSelection,
        DrawingAnnotation
    };

    static ATOM RegisterOverlayClass(HINSTANCE hInstance);
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void RecomputeLayout();
    void InvalidateCanvas() noexcept;
    void StartCaretTimer();
    void StopCaretTimer() noexcept;
    void TriggerSizeHud();
    void HandleToolbarClick(ToolbarButtonId id);
    void OpenCustomColorDialog();
    void PerformCopy();
    void PerformSave();
    void SelectMonitorAt(tools::PointF pt);
    void SelectFullVirtualScreen();
    void FinishAndClose(OverlayExitReason reason);
    bool TryRecoverDeviceLost();

    static constexpr UINT_PTR kCaretTimerId = 1;
    static constexpr UINT_PTR kSizeHudTimerId = 2;

    HINSTANCE hInstance_ = nullptr;
    platform::UniqueHwnd hwnd_;
    capture::CapturedFrame frame_;
    OverlayCallbacks callbacks_;
    bool headlessTestMode_ = false;
    bool destroying_ = false;
    bool inModalDialog_ = false;
    bool recoveryAttempted_ = false;
    OverlayExitReason exitReason_ = OverlayExitReason::None;

    render::D2DContext d2d_;
    render::SceneRenderer sceneRenderer_;
    render::UIRenderer uiRenderer_;

    tools::SelectionBox selection_{};
    tools::ToolController toolController_;
    ToolbarLayoutState toolbarState_{};
    ToolbarLayoutResult layout_{};
    ToolbarButtonId hoveredButton_ = ToolbarButtonId::None;

    DragMode dragMode_ = DragMode::None;
    tools::ResizeHandle activeResizeHandle_ = tools::ResizeHandle::None;
    tools::PointF dragStartPt_{};
    tools::RectF dragStartRect_{};

    bool showCaret_ = true;
    bool showSizeHud_ = false;
    std::array<COLORREF, 16> customColors_{};
};

} // namespace saycheese::ui
