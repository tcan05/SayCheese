#pragma once

#include <windows.h>

#include <string>

namespace saycheese::app {

class HotkeyManager {
public:
    static constexpr int kHotkeyId = 1;

    bool RegisterPrimaryOrFallback(HWND hwnd) noexcept;
    bool RetryPrimary(HWND hwnd) noexcept;
    void Unregister(HWND hwnd) noexcept;

    bool IsRegistered() const noexcept { return registered_; }
    bool IsUsingFallback() const noexcept { return usingFallback_; }
    std::wstring DescribeStatus() const;

private:
    bool registered_ = false;
    bool usingFallback_ = false;
};

} // namespace saycheese::app
