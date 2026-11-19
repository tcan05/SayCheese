#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace saycheese::app {

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon() noexcept { Destroy(); }

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HWND hwnd, HICON icon, UINT callbackMessage);
    bool Recreate();
    void ShowBalloon(const std::wstring& title, const std::wstring& message, DWORD infoFlags = NIIF_INFO);
    void Destroy() noexcept;

private:
    NOTIFYICONDATAW nid_{};
    bool added_ = false;
};

} // namespace saycheese::app
