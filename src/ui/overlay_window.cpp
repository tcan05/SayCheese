#include "ui/overlay_window.hpp"

#include "export/clipboard_export.hpp"
#include "export/wic_exporter.hpp"
#include "platform/dpi_monitor.hpp"

#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace saycheese::ui {

namespace {

constexpr wchar_t kOverlayClassName[] = L"SayCheeseOverlayWndClass";

tools::PointF PointFromLParam(LPARAM lParam) noexcept {
    return tools::PointF{
        static_cast<float>(GET_X_LPARAM(lParam)),
        static_cast<float>(GET_Y_LPARAM(lParam))
    };
}

bool IsShiftDown() noexcept {
    return (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

bool IsCtrlDown() noexcept {
    return (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

bool IsAltDown() noexcept {
    return (::GetKeyState(VK_MENU) & 0x8000) != 0;
}

bool IsSpaceDown() noexcept {
    return (::GetKeyState(VK_SPACE) & 0x8000) != 0;
}

LPCWSTR CursorForHandle(tools::ResizeHandle handle) noexcept {
    switch (handle) {
    case tools::ResizeHandle::TopLeft:
    case tools::ResizeHandle::BottomRight:
        return IDC_SIZENWSE;
    case tools::ResizeHandle::TopRight:
    case tools::ResizeHandle::BottomLeft:
        return IDC_SIZENESW;
    case tools::ResizeHandle::Top:
    case tools::ResizeHandle::Bottom:
        return IDC_SIZENS;
    case tools::ResizeHandle::Left:
    case tools::ResizeHandle::Right:
        return IDC_SIZEWE;
    case tools::ResizeHandle::MoveBody:
        return IDC_SIZEALL;
    case tools::ResizeHandle::None:
        return IDC_CROSS;
    }
    return IDC_CROSS;
}

} // namespace

ATOM OverlayWindow::RegisterOverlayClass(HINSTANCE hInstance) {
    WNDCLASSEXW existing{};
    if (::GetClassInfoExW(hInstance, kOverlayClassName, &existing)) {
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &OverlayWindow::StaticWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_CROSS);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kOverlayClassName;
    return ::RegisterClassExW(&wc);
}

bool OverlayWindow::CreateAndShow(
    HINSTANCE hInstance,
    capture::CapturedFrame frame,
    OverlayCallbacks callbacks,
    bool headlessTestMode) {
    DestroyAndRelease();

    if (!frame.IsValid()) {
        return false;
    }

    hInstance_ = hInstance;
    frame_ = std::move(frame);
    callbacks_ = std::move(callbacks);
    headlessTestMode_ = headlessTestMode;
    destroying_ = false;
    inModalDialog_ = false;
    recoveryAttempted_ = false;
    exitReason_ = OverlayExitReason::None;
    selection_ = {};
    toolController_.ClearAll();
    toolController_.SetActiveTool(tools::ToolType::Select);
    toolbarState_ = {};
    layout_ = {};
    hoveredButton_ = ToolbarButtonId::None;
    dragMode_ = DragMode::None;
    showCaret_ = true;
    showSizeHud_ = false;

    if (RegisterOverlayClass(hInstance_) == 0) {
        DestroyAndRelease();
        return false;
    }

    const DWORD exStyle = headlessTestMode_
        ? WS_EX_TOOLWINDOW
        : (WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    const DWORD style = WS_POPUP;

    HWND created = ::CreateWindowExW(
        exStyle,
        kOverlayClassName,
        L"SayCheese Overlay",
        style,
        frame_.originX,
        frame_.originY,
        frame_.width,
        frame_.height,
        nullptr,
        nullptr,
        hInstance_,
        this);

    if (created == nullptr) {
        DestroyAndRelease();
        return false;
    }
    hwnd_.Reset(created);

    if (!d2d_.InitializeForWindow(
            hwnd_.Get(),
            static_cast<uint32_t>(frame_.width),
            static_cast<uint32_t>(frame_.height),
            false) ||
        !d2d_.UploadDesktopBitmap(frame_)) {
        exitReason_ = OverlayExitReason::RenderError;
        DestroyAndRelease();
        return false;
    }

    if (!headlessTestMode_) {
        ::ShowWindow(hwnd_.Get(), SW_SHOW);
        ::SetWindowPos(
            hwnd_.Get(),
            HWND_TOPMOST,
            frame_.originX,
            frame_.originY,
            frame_.width,
            frame_.height,
            SWP_SHOWWINDOW);
        ::SetForegroundWindow(hwnd_.Get());
        ::SetFocus(hwnd_.Get());
    }

    RenderFrame();
    return true;
}

void OverlayWindow::DestroyAndRelease() noexcept {
    if (destroying_) {
        return;
    }
    destroying_ = true;

    if (hwnd_.IsValid()) {
        StopCaretTimer();
        ::KillTimer(hwnd_.Get(), kSizeHudTimerId);
        if (::GetCapture() == hwnd_.Get()) {
            ::ReleaseCapture();
        }
        ::ShowWindow(hwnd_.Get(), SW_HIDE);
    }

    // Release all Direct2D / DXGI / D3D11 resources BEFORE destroying the HWND.
    sceneRenderer_.ClearCache();
    d2d_.Reset();

    hwnd_.Reset();

    toolController_.ClearAll();
    selection_ = {};
    layout_ = {};
    frame_ = {};
    dragMode_ = DragMode::None;

    const auto onClosed = std::move(callbacks_.onClosed);
    callbacks_ = {};
    const OverlayExitReason reason = exitReason_;

    destroying_ = false;
    if (onClosed) {
        onClosed(reason);
    }
}

void OverlayWindow::FinishAndClose(OverlayExitReason reason) {
    exitReason_ = reason;
    DestroyAndRelease();
}

void OverlayWindow::RecomputeLayout() {
    if (!selection_.active || selection_.rect.IsEmpty()) {
        layout_ = {};
        return;
    }
    toolbarState_.censorActive = (toolController_.ActiveTool() == tools::ToolType::Censor);
    const tools::RectF canvasBounds{
        0.0f, 0.0f,
        static_cast<float>(frame_.width),
        static_cast<float>(frame_.height)
    };
    layout_ = ComputeToolbarLayout(selection_.rect, frame_.monitors, canvasBounds, toolbarState_);
}

void OverlayWindow::InvalidateCanvas() noexcept {
    if (hwnd_.IsValid()) {
        ::InvalidateRect(hwnd_.Get(), nullptr, FALSE);
    }
}

void OverlayWindow::StartCaretTimer() {
    showCaret_ = true;
    if (hwnd_.IsValid() && !headlessTestMode_) {
        ::SetTimer(hwnd_.Get(), kCaretTimerId, 530, nullptr);
    }
}

void OverlayWindow::StopCaretTimer() noexcept {
    if (hwnd_.IsValid()) {
        ::KillTimer(hwnd_.Get(), kCaretTimerId);
    }
}

void OverlayWindow::TriggerSizeHud() {
    showSizeHud_ = true;
    if (hwnd_.IsValid() && !headlessTestMode_) {
        ::SetTimer(hwnd_.Get(), kSizeHudTimerId, 1100, nullptr);
    }
}

bool OverlayWindow::TryRecoverDeviceLost() {
    if (recoveryAttempted_ || !hwnd_.IsValid() || !frame_.IsValid()) {
        return false;
    }
    recoveryAttempted_ = true;
    sceneRenderer_.ClearCache();
    d2d_.Reset();

    if (!d2d_.InitializeForWindow(
            hwnd_.Get(),
            static_cast<uint32_t>(frame_.width),
            static_cast<uint32_t>(frame_.height),
            false) ||
        !d2d_.UploadDesktopBitmap(frame_)) {
        return false;
    }
    return true;
}

void OverlayWindow::RenderFrame() {
    ID2D1DeviceContext* dc = d2d_.DeviceContext();
    ID2D1Bitmap1* desktopBmp = d2d_.DesktopBitmap();
    if (dc == nullptr || desktopBmp == nullptr || !frame_.IsValid()) {
        return;
    }

    RecomputeLayout();

    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());

    const tools::RectF clipRect = selection_.active ? selection_.rect.Normalized() : tools::RectF{};
    sceneRenderer_.RenderScene(
        dc,
        d2d_.D2DFactory(),
        d2d_.DWriteFactory(),
        desktopBmp,
        frame_,
        clipRect,
        toolController_.Annotations(),
        toolController_.ActivePreview());

    render::UIRenderState uiState{};
    uiState.selection = selection_;
    uiState.layout = layout_;
    uiState.activeTool = toolController_.ActiveTool();
    uiState.censorMode = toolController_.ActiveCensorMode();
    uiState.activeColor = toolController_.ActiveColor();
    uiState.activeSize = toolController_.ActiveSize();
    uiState.canUndo = toolController_.CanUndo();
    uiState.canRedo = toolController_.CanRedo();
    uiState.hoveredButton = hoveredButton_;
    uiState.textEditor = toolController_.TextEditor();
    uiState.showCaret = showCaret_;
    uiState.showSizeHud = showSizeHud_;

    uiRenderer_.RenderOverlayChrome(
        dc,
        d2d_.D2DFactory(),
        d2d_.DWriteFactory(),
        static_cast<float>(frame_.width),
        static_cast<float>(frame_.height),
        uiState);

    HRESULT hr = dc->EndDraw();
    if (SUCCEEDED(hr)) {
        hr = d2d_.Present();
    }

    if (render::D2DContext::IsDeviceLostError(hr)) {
        if (TryRecoverDeviceLost()) {
            RenderFrame();
            return;
        }
        FinishAndClose(OverlayExitReason::RenderError);
    }
}

void OverlayWindow::OpenCustomColorDialog() {
    if (!hwnd_.IsValid()) {
        return;
    }
    if (::GetCapture() == hwnd_.Get()) {
        ::ReleaseCapture();
    }

    inModalDialog_ = true;
    ::SetWindowPos(hwnd_.Get(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    const tools::ColorRGBA cur = toolController_.ActiveColor();
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd_.Get();
    cc.rgbResult = RGB(cur.r, cur.g, cur.b);
    cc.lpCustColors = customColors_.data();
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    const BOOL ok = ::ChooseColorW(&cc);
    inModalDialog_ = false;

    if (!hwnd_.IsValid()) {
        return;
    }

    ::SetWindowPos(hwnd_.Get(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ::SetForegroundWindow(hwnd_.Get());
    ::SetFocus(hwnd_.Get());

    if (ok) {
        toolController_.SetActiveColor(tools::ColorRGBA{
            GetRValue(cc.rgbResult),
            GetGValue(cc.rgbResult),
            GetBValue(cc.rgbResult),
            255
        });
    }
    toolbarState_.colorPopupOpen = false;
    RenderFrame();
}

void OverlayWindow::PerformCopy() {
    if (!selection_.active || selection_.rect.IsEmpty()) {
        return;
    }
    if (toolController_.TextEditor().active) {
        toolController_.CommitActiveText();
        StopCaretTimer();
    }

    const export_img::ExportedImage exported = export_img::RenderCropToWicBitmap(
        d2d_, sceneRenderer_, frame_, selection_.rect, toolController_.Annotations());

    if (!exported.IsValid()) {
        if (callbacks_.notifyUser) {
            callbacks_.notifyUser(L"SayCheese", L"Failed to render screenshot for clipboard.");
        }
        return;
    }

    if (export_img::CopyImageToClipboard(hwnd_.Get(), exported)) {
        const auto notify = callbacks_.notifyUser;
        const uint32_t w = exported.width;
        const uint32_t h = exported.height;
        FinishAndClose(OverlayExitReason::CopiedToClipboard);
        if (notify) {
            wchar_t msg[96]{};
            std::swprintf(msg, ARRAYSIZE(msg), L"Copied %u \u00D7 %u px image to clipboard.", w, h);
            notify(L"SayCheese", msg);
        }
    } else if (callbacks_.notifyUser) {
        callbacks_.notifyUser(L"SayCheese", L"Could not write image to Windows clipboard.");
    }
}

void OverlayWindow::PerformSave() {
    if (!selection_.active || selection_.rect.IsEmpty() || !hwnd_.IsValid()) {
        return;
    }
    if (toolController_.TextEditor().active) {
        toolController_.CommitActiveText();
        StopCaretTimer();
    }

    const export_img::ExportedImage exported = export_img::RenderCropToWicBitmap(
        d2d_, sceneRenderer_, frame_, selection_.rect, toolController_.Annotations());
    if (!exported.IsValid()) {
        if (callbacks_.notifyUser) {
            callbacks_.notifyUser(L"SayCheese", L"Failed to render screenshot for export.");
        }
        return;
    }

    if (::GetCapture() == hwnd_.Get()) {
        ::ReleaseCapture();
    }

    inModalDialog_ = true;
    ::SetWindowPos(hwnd_.Get(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    std::wstring fallbackDir;
    std::wstring& lastDir = (callbacks_.lastSaveDir != nullptr) ? *callbacks_.lastSaveDir : fallbackDir;
    std::wstring savedPath;
    const bool saved = export_img::PromptAndSaveImage(
        hwnd_.Get(), d2d_.WicFactory(), exported.wicBitmap.Get(), lastDir, &savedPath);

    inModalDialog_ = false;
    if (!hwnd_.IsValid()) {
        return;
    }

    if (saved) {
        const auto notify = callbacks_.notifyUser;
        FinishAndClose(OverlayExitReason::SavedToFile);
        if (notify && !savedPath.empty()) {
            notify(L"SayCheese", L"Saved screenshot to:\n" + savedPath);
        }
    } else {
        ::SetWindowPos(hwnd_.Get(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetForegroundWindow(hwnd_.Get());
        ::SetFocus(hwnd_.Get());
        RenderFrame();
    }
}

void OverlayWindow::HandleToolbarClick(ToolbarButtonId id) {
    auto selectTool = [&](tools::ToolType t) {
        toolbarState_.colorPopupOpen = false;
        toolbarState_.strokePopupOpen = false;
        const bool wasText = toolController_.TextEditor().active;
        toolController_.SetActiveTool(t);
        if (wasText && !toolController_.TextEditor().active) {
            StopCaretTimer();
        }
    };

    switch (id) {
    case ToolbarButtonId::ToolPen: selectTool(tools::ToolType::Pen); break;
    case ToolbarButtonId::ToolLine: selectTool(tools::ToolType::Line); break;
    case ToolbarButtonId::ToolArrow: selectTool(tools::ToolType::Arrow); break;
    case ToolbarButtonId::ToolRect: selectTool(tools::ToolType::Rectangle); break;
    case ToolbarButtonId::ToolHighlighter: selectTool(tools::ToolType::Highlighter); break;
    case ToolbarButtonId::ToolText: selectTool(tools::ToolType::Text); break;
    case ToolbarButtonId::ToolCensor: selectTool(tools::ToolType::Censor); break;

    case ToolbarButtonId::Undo:
        toolController_.Undo();
        StopCaretTimer();
        break;
    case ToolbarButtonId::Redo:
        toolController_.Redo();
        StopCaretTimer();
        break;

    case ToolbarButtonId::ColorPicker:
        toolbarState_.colorPopupOpen = !toolbarState_.colorPopupOpen;
        toolbarState_.strokePopupOpen = false;
        break;

    case ToolbarButtonId::StrokeWidth:
        toolbarState_.strokePopupOpen = !toolbarState_.strokePopupOpen;
        toolbarState_.colorPopupOpen = false;
        break;

    case ToolbarButtonId::Copy:
        PerformCopy();
        return;

    case ToolbarButtonId::Save:
        PerformSave();
        return;

    case ToolbarButtonId::Cancel:
        FinishAndClose(OverlayExitReason::Cancelled);
        return;

    case ToolbarButtonId::ColorSwatch0:
    case ToolbarButtonId::ColorSwatch1:
    case ToolbarButtonId::ColorSwatch2:
    case ToolbarButtonId::ColorSwatch3:
    case ToolbarButtonId::ColorSwatch4:
    case ToolbarButtonId::ColorSwatch5: {
        const size_t idx = static_cast<size_t>(id) - static_cast<size_t>(ToolbarButtonId::ColorSwatch0);
        if (idx < tools::kQuickPalette.size()) {
            toolController_.SetActiveColor(tools::kQuickPalette[idx]);
        }
        toolbarState_.colorPopupOpen = false;
        break;
    }
    case ToolbarButtonId::ColorCustom:
        OpenCustomColorDialog();
        return;

    case ToolbarButtonId::CensorMosaic:
        toolController_.SetCensorMode(tools::CensorMode::Mosaic);
        break;

    case ToolbarButtonId::CensorBlur:
        toolController_.SetCensorMode(tools::CensorMode::GaussianBlur);
        break;

    case ToolbarButtonId::PresetThin:
        toolController_.ApplySizePreset(0);
        toolbarState_.strokePopupOpen = false;
        TriggerSizeHud();
        break;

    case ToolbarButtonId::PresetMedium:
        toolController_.ApplySizePreset(1);
        toolbarState_.strokePopupOpen = false;
        TriggerSizeHud();
        break;

    case ToolbarButtonId::PresetThick:
        toolController_.ApplySizePreset(2);
        toolbarState_.strokePopupOpen = false;
        TriggerSizeHud();
        break;

    case ToolbarButtonId::None:
        break;
    }

    RenderFrame();
}

void OverlayWindow::SelectMonitorAt(tools::PointF pt) {
    const platform::MonitorInfo* mon = platform::FindMonitorContainingCanvasPoint(frame_.monitors, pt.x, pt.y);
    if (mon != nullptr) {
        selection_.rect = tools::RectF{
            static_cast<float>(mon->canvasRect.left),
            static_cast<float>(mon->canvasRect.top),
            static_cast<float>(mon->canvasRect.right),
            static_cast<float>(mon->canvasRect.bottom)
        };
    } else {
        selection_.rect = tools::RectF{0.0f, 0.0f, static_cast<float>(frame_.width), static_cast<float>(frame_.height)};
    }
    selection_.active = !selection_.rect.IsEmpty();
    RenderFrame();
}

void OverlayWindow::SelectFullVirtualScreen() {
    selection_.rect = tools::RectF{0.0f, 0.0f, static_cast<float>(frame_.width), static_cast<float>(frame_.height)};
    selection_.active = !selection_.rect.IsEmpty();
    RenderFrame();
}

LRESULT CALLBACK OverlayWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OverlayWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        ::BeginPaint(hwnd, &ps);
        RenderFrame();
        ::EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == kCaretTimerId) {
            showCaret_ = !showCaret_;
            RenderFrame();
        } else if (wParam == kSizeHudTimerId) {
            showSizeHud_ = false;
            ::KillTimer(hwnd_.Get(), kSizeHudTimerId);
            RenderFrame();
        }
        return 0;

    case WM_LBUTTONDBLCLK: {
        const tools::PointF pt = PointFromLParam(lParam);
        if (selection_.active) {
            const ToolbarButtonId hitBtn = HitTestToolbar(layout_, pt);
            if (hitBtn != ToolbarButtonId::None) {
                HandleToolbarClick(hitBtn);
                return 0;
            }
        }
        if (toolController_.ActiveTool() == tools::ToolType::Select && !toolController_.TextEditor().active) {
            SelectMonitorAt(pt);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const tools::PointF pt = PointFromLParam(lParam);

        if (selection_.active) {
            const ToolbarButtonId hitBtn = HitTestToolbar(layout_, pt);
            if (hitBtn != ToolbarButtonId::None) {
                HandleToolbarClick(hitBtn);
                return 0;
            }
        }

        // Clicking outside popups closes them cleanly.
        toolbarState_.colorPopupOpen = false;
        toolbarState_.strokePopupOpen = false;

        if (selection_.active) {
            const float handleRadius = 8.0f * std::max(1.0f, layout_.uiScale);
            const tools::ResizeHandle handle = tools::HitTestSelection(selection_, pt, handleRadius);

            if (handle != tools::ResizeHandle::None && handle != tools::ResizeHandle::MoveBody) {
                if (toolController_.TextEditor().active) {
                    toolController_.CommitActiveText();
                    StopCaretTimer();
                }
                dragMode_ = DragMode::ResizingSelection;
                activeResizeHandle_ = handle;
                dragStartPt_ = pt;
                dragStartRect_ = selection_.rect.Normalized();
                ::SetCapture(hwnd_.Get());
                RenderFrame();
                return 0;
            }

            if (handle == tools::ResizeHandle::MoveBody) {
                if (toolController_.ActiveTool() == tools::ToolType::Select || IsSpaceDown()) {
                    if (toolController_.TextEditor().active) {
                        toolController_.CommitActiveText();
                        StopCaretTimer();
                    }
                    dragMode_ = DragMode::MovingSelection;
                    activeResizeHandle_ = tools::ResizeHandle::MoveBody;
                    dragStartPt_ = pt;
                    dragStartRect_ = selection_.rect.Normalized();
                    ::SetCapture(hwnd_.Get());
                    RenderFrame();
                    return 0;
                }

                toolController_.OnPointerDown(pt, IsShiftDown());
                if (toolController_.ActiveTool() == tools::ToolType::Text) {
                    StartCaretTimer();
                } else {
                    dragMode_ = DragMode::DrawingAnnotation;
                    ::SetCapture(hwnd_.Get());
                }
                RenderFrame();
                return 0;
            }

            // Clicked outside selection: commit text if active, otherwise start fresh selection in Select mode.
            if (toolController_.TextEditor().active) {
                toolController_.CommitActiveText();
                StopCaretTimer();
                RenderFrame();
                return 0;
            }
            if (toolController_.ActiveTool() == tools::ToolType::Select) {
                dragMode_ = DragMode::CreatingSelection;
                dragStartPt_ = pt;
                selection_.rect = tools::RectF{pt.x, pt.y, pt.x, pt.y};
                selection_.active = true;
                ::SetCapture(hwnd_.Get());
                RenderFrame();
                return 0;
            }
            RenderFrame();
            return 0;
        }

        // Initial selection creation.
        dragMode_ = DragMode::CreatingSelection;
        dragStartPt_ = pt;
        selection_.rect = tools::RectF{pt.x, pt.y, pt.x, pt.y};
        selection_.active = true;
        ::SetCapture(hwnd_.Get());
        RenderFrame();
        return 0;
    }

    case WM_MOUSEMOVE: {
        const tools::PointF pt = PointFromLParam(lParam);
        const tools::RectF canvasBounds{0.0f, 0.0f, static_cast<float>(frame_.width), static_cast<float>(frame_.height)};

        if (dragMode_ == DragMode::CreatingSelection) {
            const float cx = std::clamp(pt.x, canvasBounds.left, canvasBounds.right);
            const float cy = std::clamp(pt.y, canvasBounds.top, canvasBounds.bottom);
            selection_.rect = tools::RectF{dragStartPt_.x, dragStartPt_.y, cx, cy}.Normalized();
            RenderFrame();
            return 0;
        }
        if (dragMode_ == DragMode::ResizingSelection || dragMode_ == DragMode::MovingSelection) {
            const tools::PointF delta{pt.x - dragStartPt_.x, pt.y - dragStartPt_.y};
            selection_.rect = tools::ApplySelectionDrag(dragStartRect_, activeResizeHandle_, delta, canvasBounds, 8.0f);
            RenderFrame();
            return 0;
        }
        if (dragMode_ == DragMode::DrawingAnnotation) {
            toolController_.OnPointerMove(pt, IsShiftDown());
            RenderFrame();
            return 0;
        }

        const ToolbarButtonId newHover = selection_.active ? HitTestToolbar(layout_, pt) : ToolbarButtonId::None;
        if (newHover != hoveredButton_) {
            hoveredButton_ = newHover;
            RenderFrame();
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        const tools::PointF pt = PointFromLParam(lParam);
        if (::GetCapture() == hwnd_.Get()) {
            ::ReleaseCapture();
        }

        if (dragMode_ == DragMode::CreatingSelection) {
            dragMode_ = DragMode::None;
            selection_.rect = selection_.rect.Normalized();
            if (selection_.rect.Width() < 8.0f || selection_.rect.Height() < 8.0f) {
                selection_.active = false;
                selection_.rect = {};
            }
            RenderFrame();
            return 0;
        }
        if (dragMode_ == DragMode::ResizingSelection || dragMode_ == DragMode::MovingSelection) {
            dragMode_ = DragMode::None;
            RenderFrame();
            return 0;
        }
        if (dragMode_ == DragMode::DrawingAnnotation) {
            dragMode_ = DragMode::None;
            toolController_.OnPointerUp(pt, IsShiftDown());
            RenderFrame();
            return 0;
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        const tools::PointF pt = PointFromLParam(lParam);
        if (toolController_.TextEditor().active) {
            toolController_.CommitActiveText();
            StopCaretTimer();
        }
        if (!selection_.active || !selection_.rect.Normalized().Contains(pt)) {
            FinishAndClose(OverlayExitReason::Cancelled);
            return 0;
        }
        RenderFrame();
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int steps = delta / WHEEL_DELTA;
        if (steps != 0) {
            toolController_.AdjustSizeByWheel(steps);
            TriggerSizeHud();
            RenderFrame();
        }
        return 0;
    }

    case WM_SETCURSOR: {
        if (LOWORD(lParam) != HTCLIENT) {
            break;
        }
        POINT screenPt{};
        ::GetCursorPos(&screenPt);
        ::ScreenToClient(hwnd_.Get(), &screenPt);
        const tools::PointF pt{static_cast<float>(screenPt.x), static_cast<float>(screenPt.y)};

        LPCWSTR cursorId = IDC_CROSS;
        if (selection_.active) {
            if (HitTestToolbar(layout_, pt) != ToolbarButtonId::None) {
                cursorId = IDC_HAND;
            } else {
                const float handleRadius = 8.0f * std::max(1.0f, layout_.uiScale);
                const tools::ResizeHandle h = tools::HitTestSelection(selection_, pt, handleRadius);
                if (h != tools::ResizeHandle::None && h != tools::ResizeHandle::MoveBody) {
                    cursorId = CursorForHandle(h);
                } else if (h == tools::ResizeHandle::MoveBody) {
                    if (toolController_.ActiveTool() == tools::ToolType::Select || IsSpaceDown()) {
                        cursorId = IDC_SIZEALL;
                    } else if (toolController_.ActiveTool() == tools::ToolType::Text) {
                        cursorId = IDC_IBEAM;
                    } else {
                        cursorId = IDC_CROSS;
                    }
                }
            }
        }
        ::SetCursor(::LoadCursorW(nullptr, cursorId));
        return TRUE;
    }

    case WM_KEYDOWN: {
        const bool pureCtrl = tools::IsPureCtrlShortcut(IsCtrlDown(), IsAltDown());
        if (wParam == VK_ESCAPE) {
            const tools::EscapeAction action = toolController_.HandleEscape();
            if (action == tools::EscapeAction::CloseOverlay) {
                FinishAndClose(OverlayExitReason::Cancelled);
            } else {
                StopCaretTimer();
                RenderFrame();
            }
            return 0;
        }

        if (pureCtrl) {
            if (wParam == 'Z') {
                toolController_.Undo();
                StopCaretTimer();
                RenderFrame();
                return 0;
            }
            if (wParam == 'Y') {
                toolController_.Redo();
                StopCaretTimer();
                RenderFrame();
                return 0;
            }
            if (wParam == 'C') {
                PerformCopy();
                return 0;
            }
            if (wParam == 'S') {
                PerformSave();
                return 0;
            }
            if (wParam == 'A' && !toolController_.TextEditor().active) {
                SelectFullVirtualScreen();
                return 0;
            }
        }

        if (toolController_.TextEditor().active) {
            switch (wParam) {
            case VK_BACK:
                toolController_.OnTextBackspace();
                StartCaretTimer();
                RenderFrame();
                return 0;
            case VK_DELETE:
                toolController_.OnTextDelete();
                StartCaretTimer();
                RenderFrame();
                return 0;
            case VK_LEFT:
                toolController_.OnTextCaretLeft();
                StartCaretTimer();
                RenderFrame();
                return 0;
            case VK_RIGHT:
                toolController_.OnTextCaretRight();
                StartCaretTimer();
                RenderFrame();
                return 0;
            case VK_RETURN:
                toolController_.OnTextNewline();
                StartCaretTimer();
                RenderFrame();
                return 0;
            default:
                break;
            }
        }
        return 0;
    }

    case WM_CHAR:
        // Do NOT filter by IsCtrlDown() here: AltGr on European/international keyboards sets
        // both VK_CONTROL and VK_MENU. Control codes (Ctrl+A..Z) are already excluded by ch >= 0x20.
        if (toolController_.TextEditor().active) {
            const wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch >= 0x20 && ch != 0x7F) {
                toolController_.OnTextChar(ch);
                StartCaretTimer();
                RenderFrame();
            }
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (dragMode_ != DragMode::None && !inModalDialog_) {
            dragMode_ = DragMode::None;
            RenderFrame();
        }
        return 0;

    case WM_CLOSE:
        FinishAndClose(OverlayExitReason::Cancelled);
        return 0;

    default:
        break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OverlayWindow::ApplySyntheticTestAnnotations() {
    if (!frame_.IsValid()) {
        return;
    }
    const float w = static_cast<float>(frame_.width);
    const float h = static_cast<float>(frame_.height);
    const float marginX = std::min(60.0f, w * 0.1f);
    const float marginY = std::min(60.0f, h * 0.1f);
    selection_.rect = tools::RectF{marginX, marginY, std::max(marginX + 64.0f, w - marginX), std::max(marginY + 64.0f, h - marginY)};
    selection_.active = true;

    toolController_.ClearAll();

    // Exercise all 7 tools including both Mosaic and GaussianBlur censors.
    tools::Annotation pen{};
    pen.type = tools::ToolType::Pen;
    pen.color = tools::kQuickPalette[0];
    pen.strokeWidth = 4.0f;
    pen.points = {{marginX + 20.0f, marginY + 20.0f}, {marginX + 60.0f, marginY + 45.0f}, {marginX + 110.0f, marginY + 25.0f}};
    toolController_.AddAnnotationDirect(std::move(pen));

    tools::Annotation line{};
    line.type = tools::ToolType::Line;
    line.color = tools::kQuickPalette[2];
    line.strokeWidth = 3.0f;
    line.start = {marginX + 20.0f, marginY + 70.0f};
    line.end = {marginX + 140.0f, marginY + 70.0f};
    toolController_.AddAnnotationDirect(std::move(line));

    tools::Annotation arrow{};
    arrow.type = tools::ToolType::Arrow;
    arrow.color = tools::kQuickPalette[3];
    arrow.strokeWidth = 4.0f;
    arrow.start = {marginX + 20.0f, marginY + 100.0f};
    arrow.end = {marginX + 150.0f, marginY + 125.0f};
    toolController_.AddAnnotationDirect(std::move(arrow));

    tools::Annotation rect{};
    rect.type = tools::ToolType::Rectangle;
    rect.color = tools::kQuickPalette[4];
    rect.strokeWidth = 3.0f;
    rect.rect = {marginX + 170.0f, marginY + 20.0f, marginX + 260.0f, marginY + 80.0f};
    toolController_.AddAnnotationDirect(std::move(rect));

    tools::Annotation hl{};
    hl.type = tools::ToolType::Highlighter;
    hl.color = tools::ColorRGBA{255, 204, 0, 89};
    hl.strokeWidth = 18.0f;
    hl.points = {{marginX + 20.0f, marginY + 150.0f}, {marginX + 180.0f, marginY + 150.0f}};
    toolController_.AddAnnotationDirect(std::move(hl));

    tools::Annotation text{};
    text.type = tools::ToolType::Text;
    text.color = tools::kQuickPalette[4];
    text.start = {marginX + 25.0f, marginY + 175.0f};
    text.text = L"SayCheese Leak Test";
    text.fontSize = 24.0f;
    toolController_.AddAnnotationDirect(std::move(text));

    tools::Annotation mosaic{};
    mosaic.type = tools::ToolType::Censor;
    mosaic.censorMode = tools::CensorMode::Mosaic;
    mosaic.censorIntensity = 12.0f;
    mosaic.rect = {marginX + 180.0f, marginY + 95.0f, marginX + 270.0f, marginY + 155.0f};
    toolController_.AddAnnotationDirect(std::move(mosaic));

    tools::Annotation blur{};
    blur.type = tools::ToolType::Censor;
    blur.censorMode = tools::CensorMode::GaussianBlur;
    blur.censorIntensity = 8.0f;
    blur.rect = {marginX + 280.0f, marginY + 95.0f, marginX + 370.0f, marginY + 155.0f};
    toolController_.AddAnnotationDirect(std::move(blur));
}

bool OverlayWindow::RunOffscreenExportCheck() {
    if (!selection_.active || selection_.rect.IsEmpty()) {
        return false;
    }
    const export_img::ExportedImage img = export_img::RenderCropToWicBitmap(
        d2d_, sceneRenderer_, frame_, selection_.rect, toolController_.Annotations());
    return img.IsValid();
}

} // namespace saycheese::ui
