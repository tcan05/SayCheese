#pragma once

#include "platform/win32_raii.hpp"

#include <windows.h>

namespace saycheese::platform {

class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(const wchar_t* mutexName) noexcept {
        HANDLE handle = ::CreateMutexW(nullptr, FALSE, mutexName);
        const DWORD lastError = ::GetLastError();
        if (handle != nullptr) {
            mutex_.Reset(handle);
            isPrimary_ = (lastError != ERROR_ALREADY_EXISTS);
        }
    }

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard(SingleInstanceGuard&&) noexcept = default;
    SingleInstanceGuard& operator=(SingleInstanceGuard&&) noexcept = default;

    bool IsPrimaryInstance() const noexcept { return isPrimary_; }

private:
    UniqueHandle mutex_;
    bool isPrimary_ = false;
};

} // namespace saycheese::platform
