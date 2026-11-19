#include "app/app_controller.hpp"

#include "capture/screen_capture.hpp"
#include "platform/single_instance.hpp"
#include "resource.h"

namespace saycheese::app {

namespace {

constexpr wchar_t kTrayClassName[] = L"SayCheeseTrayWndClass";
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\SayCheese_SingleInstance_Mutex";

} // namespace

int AppController::Run(HINSTANCE hInstance) {
    platform::SingleInstanceGuard singleInstance(kSingleInstanceMutex);
    if (!singleInstance.IsPrimaryInstance()) {
        return 0;
    }

    hInstance_ = hInstance;
    taskbarCreatedMsg_ = ::RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &AppController::StaticWndProc;
    wc.hInstance = hInstance_;
    wc.hIcon = static_cast<HICON>(::LoadImageW(
        hInstance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        ::GetSystemMetrics(SM_CXICON),
        ::GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(::LoadImageW(
        hInstance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON),
        ::GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    wc.lpszClassName = kTrayClassName;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    HWND created = ::CreateWindowExW(
        0,
        kTrayClassName,
        L"SayCheese",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        hInstance_,
        this);
    if (created == nullptr) {
        return 1;
    }
    trayHwnd_.Reset(created);

    HICON loadedIcon = static_cast<HICON>(::LoadImageW(
        hInstance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON),
        ::GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (loadedIcon != nullptr) {
        trayIconHandle_.Reset(loadedIcon);
    }

    HICON iconForTray = trayIconHandle_ ? trayIconHandle_.Get() : ::LoadIconW(nullptr, IDI_APPLICATION);
    trayIcon_.Create(trayHwnd_.Get(), iconForTray, kWmTrayIcon);

    if (!hotkeys_.RegisterPrimaryOrFallback(trayHwnd_.Get())) {
        trayIcon_.ShowBalloon(
            L"SayCheese",
            L"Could not register PrintScreen or Ctrl+Shift+S. Use the tray icon menu to capture.",
            NIIF_WARNING);
    } else if (hotkeys_.IsUsingFallback()) {
        trayIcon_.ShowBalloon(
            L"SayCheese — Fallback Hotkey Active",
            L"PrintScreen is currently in use (e.g., Windows Snipping Tool). Press Ctrl+Shift+S to capture.",
            NIIF_INFO);
    }

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    Shutdown();
    return static_cast<int>(msg.wParam);
}

void AppController::Shutdown() noexcept {
    overlay_.DestroyAndRelease();
    if (trayHwnd_.IsValid()) {
        hotkeys_.Unregister(trayHwnd_.Get());
    }
    trayIcon_.Destroy();
    trayHwnd_.Reset();
    trayIconHandle_.Reset();
}

void AppController::StartCaptureSession() {
    if (capturing_ || overlay_.IsOpen()) {
        return;
    }
    capturing_ = true;

    capture::CapturedFrame frame = capture::CaptureVirtualDesktop();
    if (!frame.IsValid()) {
        capturing_ = false;
        trayIcon_.ShowBalloon(
            L"SayCheese",
            L"Screen capture failed on the current display session.",
            NIIF_ERROR);
        return;
    }

    ui::OverlayCallbacks cbs{};
    cbs.lastSaveDir = &lastSaveDir_;
    cbs.notifyUser = [this](const std::wstring& title, const std::wstring& message) {
        trayIcon_.ShowBalloon(title, message, NIIF_INFO);
    };
    cbs.onClosed = [this](ui::OverlayExitReason reason) {
        capturing_ = false;
        if (reason == ui::OverlayExitReason::RenderError) {
            trayIcon_.ShowBalloon(
                L"SayCheese",
                L"Display graphics device reset during capture. Overlay closed safely.",
                NIIF_WARNING);
        }
    };

    if (!overlay_.CreateAndShow(hInstance_, std::move(frame), std::move(cbs), false)) {
        capturing_ = false;
        trayIcon_.ShowBalloon(
            L"SayCheese",
            L"Failed to initialize Direct2D overlay window.",
            NIIF_ERROR);
    }
}

void AppController::ShowContextMenu() {
    platform::UniqueHmenu menu(::CreatePopupMenu());
    if (!menu) {
        return;
    }

    const std::wstring hotkeyText = hotkeys_.DescribeStatus();
    ::AppendMenuW(menu.Get(), MF_STRING, kCmdCaptureNow, L"Capture Now");
    ::AppendMenuW(menu.Get(), MF_STRING, kCmdHotkeyStatus, hotkeyText.c_str());
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu.Get(), MF_STRING, kCmdExit, L"Exit");
    ::SetMenuDefaultItem(menu.Get(), kCmdCaptureNow, FALSE);

    POINT pt{};
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(trayHwnd_.Get());
    ::TrackPopupMenu(menu.Get(), TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                     pt.x, pt.y, 0, trayHwnd_.Get(), nullptr);
    ::PostMessageW(trayHwnd_.Get(), WM_NULL, 0, 0);
}

void AppController::HandleHotkeyStatusClick() {
    if (hotkeys_.IsUsingFallback() || !hotkeys_.IsRegistered()) {
        if (hotkeys_.RetryPrimary(trayHwnd_.Get())) {
            trayIcon_.ShowBalloon(
                L"SayCheese",
                L"PrintScreen hotkey is now active!",
                NIIF_INFO);
        } else if (hotkeys_.IsUsingFallback()) {
            trayIcon_.ShowBalloon(
                L"SayCheese",
                L"PrintScreen is still held by another app. Fallback hotkey Ctrl+Shift+S remains active.",
                NIIF_INFO);
        } else {
            trayIcon_.ShowBalloon(
                L"SayCheese",
                L"No global hotkey could be registered. Use 'Capture Now' from the tray menu.",
                NIIF_WARNING);
        }
    } else {
        trayIcon_.ShowBalloon(
            L"SayCheese",
            L"Global hotkey PrintScreen is active and ready.",
            NIIF_INFO);
    }
}

LRESULT CALLBACK AppController::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppController* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AppController*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppController*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AppController::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        trayIcon_.Recreate();
        return 0;
    }

    switch (msg) {
    case WM_HOTKEY:
        if (static_cast<int>(wParam) == HotkeyManager::kHotkeyId) {
            StartCaptureSession();
        }
        return 0;

    case kWmTrayIcon:
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu();
            break;
        case WM_LBUTTONDBLCLK:
            StartCaptureSession();
            break;
        default:
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kCmdCaptureNow:
            StartCaptureSession();
            break;
        case kCmdHotkeyStatus:
            HandleHotkeyStatusClick();
            break;
        case kCmdExit:
            overlay_.DestroyAndRelease();
            ::PostQuitMessage(0);
            break;
        default:
            break;
        }
        return 0;

    case WM_DESTROY:
        overlay_.DestroyAndRelease();
        hotkeys_.Unregister(trayHwnd_.Get());
        trayIcon_.Destroy();
        ::PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace saycheese::app
