#include "app/tray_icon.hpp"

#include <cstring>

namespace saycheese::app {

bool TrayIcon::Create(HWND hwnd, HICON icon, UINT callbackMessage) {
    Destroy();
    if (hwnd == nullptr) {
        return false;
    }

    nid_ = {};
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = callbackMessage;
    nid_.hIcon = icon;
    ::wcsncpy_s(nid_.szTip, ARRAYSIZE(nid_.szTip), L"SayCheese — Screenshot & Annotate", _TRUNCATE);

    added_ = (::Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE);
    return added_;
}

bool TrayIcon::Recreate() {
    if (nid_.hWnd == nullptr) {
        return false;
    }
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    added_ = (::Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE);
    return added_;
}

void TrayIcon::ShowBalloon(const std::wstring& title, const std::wstring& message, DWORD infoFlags) {
    if (!added_ || nid_.hWnd == nullptr) {
        return;
    }
    NOTIFYICONDATAW balloon = nid_;
    balloon.uFlags = NIF_INFO;
    balloon.dwInfoFlags = infoFlags;
    ::wcsncpy_s(balloon.szInfoTitle, ARRAYSIZE(balloon.szInfoTitle), title.c_str(), _TRUNCATE);
    ::wcsncpy_s(balloon.szInfo, ARRAYSIZE(balloon.szInfo), message.c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

void TrayIcon::Destroy() noexcept {
    if (added_ && nid_.hWnd != nullptr) {
        ::Shell_NotifyIconW(NIM_DELETE, &nid_);
    }
    added_ = false;
}

} // namespace saycheese::app
