#include "app/hotkey_manager.hpp"

namespace saycheese::app {

bool HotkeyManager::RegisterPrimaryOrFallback(HWND hwnd) noexcept {
    Unregister(hwnd);
    if (hwnd == nullptr) {
        return false;
    }

    // Never install WH_KEYBOARD_LL; use OS-managed RegisterHotKey only.
    if (::RegisterHotKey(hwnd, kHotkeyId, MOD_NOREPEAT, VK_SNAPSHOT)) {
        registered_ = true;
        usingFallback_ = false;
        return true;
    }

    if (::RegisterHotKey(hwnd, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'S')) {
        registered_ = true;
        usingFallback_ = true;
        return true;
    }

    registered_ = false;
    usingFallback_ = false;
    return false;
}

bool HotkeyManager::RetryPrimary(HWND hwnd) noexcept {
    if (hwnd == nullptr) {
        return false;
    }
    if (registered_ && !usingFallback_) {
        return true;
    }
    Unregister(hwnd);
    if (::RegisterHotKey(hwnd, kHotkeyId, MOD_NOREPEAT, VK_SNAPSHOT)) {
        registered_ = true;
        usingFallback_ = false;
        return true;
    }
    // Restore fallback if PrintScreen is still held by Windows Snipping Tool.
    if (::RegisterHotKey(hwnd, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'S')) {
        registered_ = true;
        usingFallback_ = true;
    }
    return false;
}

void HotkeyManager::Unregister(HWND hwnd) noexcept {
    if (registered_ && hwnd != nullptr) {
        ::UnregisterHotKey(hwnd, kHotkeyId);
    }
    registered_ = false;
    usingFallback_ = false;
}

std::wstring HotkeyManager::DescribeStatus() const {
    if (!registered_) {
        return L"Hotkey: Unavailable (use tray menu)";
    }
    if (usingFallback_) {
        return L"Hotkey: Ctrl+Shift+S (Retry PrintScreen)";
    }
    return L"Hotkey: PrintScreen (Active)";
}

} // namespace saycheese::app
