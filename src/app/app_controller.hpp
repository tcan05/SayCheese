#pragma once

#include "app/hotkey_manager.hpp"
#include "app/tray_icon.hpp"
#include "platform/win32_raii.hpp"
#include "ui/overlay_window.hpp"

#include <windows.h>

#include <string>

namespace saycheese::app {

class AppController {
public:
    AppController() = default;
    ~AppController() noexcept { Shutdown(); }

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    int Run(HINSTANCE hInstance);

private:
    static constexpr UINT kWmTrayIcon = WM_APP + 10;
    static constexpr UINT kCmdCaptureNow = 1001;
    static constexpr UINT kCmdHotkeyStatus = 1002;
    static constexpr UINT kCmdExit = 1003;

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void StartCaptureSession();
    void ShowContextMenu();
    void HandleHotkeyStatusClick();
    void Shutdown() noexcept;

    HINSTANCE hInstance_ = nullptr;
    platform::UniqueHwnd trayHwnd_;
    platform::UniqueHicon trayIconHandle_;
    UINT taskbarCreatedMsg_ = 0;

    HotkeyManager hotkeys_;
    TrayIcon trayIcon_;
    ui::OverlayWindow overlay_;
    std::wstring lastSaveDir_;
    bool capturing_ = false;
};

} // namespace saycheese::app
