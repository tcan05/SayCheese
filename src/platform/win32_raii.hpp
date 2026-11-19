#pragma once

#include <windows.h>
#include <objbase.h>

#include <utility>

namespace saycheese::platform {

class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}

    ~UniqueHandle() noexcept { Reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    HANDLE Get() const noexcept { return handle_; }
    bool IsValid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    explicit operator bool() const noexcept { return IsValid(); }

    HANDLE Release() noexcept { return std::exchange(handle_, nullptr); }

    void Reset(HANDLE newHandle = nullptr) noexcept {
        if (IsValid() && handle_ != newHandle) {
            ::CloseHandle(handle_);
        }
        handle_ = newHandle;
    }

private:
    HANDLE handle_ = nullptr;
};

class UniqueHwnd {
public:
    UniqueHwnd() noexcept = default;
    explicit UniqueHwnd(HWND hwnd) noexcept : hwnd_(hwnd) {}

    ~UniqueHwnd() noexcept { Reset(); }

    UniqueHwnd(const UniqueHwnd&) = delete;
    UniqueHwnd& operator=(const UniqueHwnd&) = delete;

    UniqueHwnd(UniqueHwnd&& other) noexcept : hwnd_(std::exchange(other.hwnd_, nullptr)) {}

    UniqueHwnd& operator=(UniqueHwnd&& other) noexcept {
        if (this != &other) {
            Reset(std::exchange(other.hwnd_, nullptr));
        }
        return *this;
    }

    HWND Get() const noexcept { return hwnd_; }
    bool IsValid() const noexcept { return hwnd_ != nullptr && ::IsWindow(hwnd_) != FALSE; }
    explicit operator bool() const noexcept { return hwnd_ != nullptr; }

    HWND Release() noexcept { return std::exchange(hwnd_, nullptr); }

    void Reset(HWND newHwnd = nullptr) noexcept {
        if (hwnd_ != nullptr && hwnd_ != newHwnd) {
            if (::IsWindow(hwnd_)) {
                ::DestroyWindow(hwnd_);
            }
        }
        hwnd_ = newHwnd;
    }

private:
    HWND hwnd_ = nullptr;
};

class UniqueHdc {
public:
    UniqueHdc() noexcept = default;
    explicit UniqueHdc(HDC hdc) noexcept : hdc_(hdc) {}

    ~UniqueHdc() noexcept { Reset(); }

    UniqueHdc(const UniqueHdc&) = delete;
    UniqueHdc& operator=(const UniqueHdc&) = delete;

    UniqueHdc(UniqueHdc&& other) noexcept : hdc_(std::exchange(other.hdc_, nullptr)) {}

    UniqueHdc& operator=(UniqueHdc&& other) noexcept {
        if (this != &other) {
            Reset(std::exchange(other.hdc_, nullptr));
        }
        return *this;
    }

    HDC Get() const noexcept { return hdc_; }
    explicit operator bool() const noexcept { return hdc_ != nullptr; }

    void Reset(HDC newHdc = nullptr) noexcept {
        if (hdc_ != nullptr && hdc_ != newHdc) {
            ::DeleteDC(hdc_);
        }
        hdc_ = newHdc;
    }

private:
    HDC hdc_ = nullptr;
};

class UniqueWindowDc {
public:
    UniqueWindowDc() noexcept = default;
    UniqueWindowDc(HWND hwnd, HDC hdc) noexcept : hwnd_(hwnd), hdc_(hdc) {}

    ~UniqueWindowDc() noexcept { Reset(); }

    UniqueWindowDc(const UniqueWindowDc&) = delete;
    UniqueWindowDc& operator=(const UniqueWindowDc&) = delete;

    UniqueWindowDc(UniqueWindowDc&& other) noexcept
        : hwnd_(std::exchange(other.hwnd_, nullptr)),
          hdc_(std::exchange(other.hdc_, nullptr)) {}

    UniqueWindowDc& operator=(UniqueWindowDc&& other) noexcept {
        if (this != &other) {
            Reset();
            hwnd_ = std::exchange(other.hwnd_, nullptr);
            hdc_ = std::exchange(other.hdc_, nullptr);
        }
        return *this;
    }

    HDC Get() const noexcept { return hdc_; }
    explicit operator bool() const noexcept { return hdc_ != nullptr; }

    void Reset() noexcept {
        if (hdc_ != nullptr) {
            ::ReleaseDC(hwnd_, hdc_);
            hdc_ = nullptr;
            hwnd_ = nullptr;
        }
    }

private:
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
};

class UniqueHbitmap {
public:
    UniqueHbitmap() noexcept = default;
    explicit UniqueHbitmap(HBITMAP bmp) noexcept : bmp_(bmp) {}

    ~UniqueHbitmap() noexcept { Reset(); }

    UniqueHbitmap(const UniqueHbitmap&) = delete;
    UniqueHbitmap& operator=(const UniqueHbitmap&) = delete;

    UniqueHbitmap(UniqueHbitmap&& other) noexcept : bmp_(std::exchange(other.bmp_, nullptr)) {}

    UniqueHbitmap& operator=(UniqueHbitmap&& other) noexcept {
        if (this != &other) {
            Reset(std::exchange(other.bmp_, nullptr));
        }
        return *this;
    }

    HBITMAP Get() const noexcept { return bmp_; }
    explicit operator bool() const noexcept { return bmp_ != nullptr; }

    void Reset(HBITMAP newBmp = nullptr) noexcept {
        if (bmp_ != nullptr && bmp_ != newBmp) {
            ::DeleteObject(bmp_);
        }
        bmp_ = newBmp;
    }

private:
    HBITMAP bmp_ = nullptr;
};

class UniqueHicon {
public:
    UniqueHicon() noexcept = default;
    explicit UniqueHicon(HICON icon) noexcept : icon_(icon) {}

    ~UniqueHicon() noexcept { Reset(); }

    UniqueHicon(const UniqueHicon&) = delete;
    UniqueHicon& operator=(const UniqueHicon&) = delete;

    UniqueHicon(UniqueHicon&& other) noexcept : icon_(std::exchange(other.icon_, nullptr)) {}

    UniqueHicon& operator=(UniqueHicon&& other) noexcept {
        if (this != &other) {
            Reset(std::exchange(other.icon_, nullptr));
        }
        return *this;
    }

    HICON Get() const noexcept { return icon_; }
    explicit operator bool() const noexcept { return icon_ != nullptr; }

    void Reset(HICON newIcon = nullptr) noexcept {
        if (icon_ != nullptr && icon_ != newIcon) {
            ::DestroyIcon(icon_);
        }
        icon_ = newIcon;
    }

private:
    HICON icon_ = nullptr;
};

class UniqueHmenu {
public:
    UniqueHmenu() noexcept = default;
    explicit UniqueHmenu(HMENU menu) noexcept : menu_(menu) {}

    ~UniqueHmenu() noexcept { Reset(); }

    UniqueHmenu(const UniqueHmenu&) = delete;
    UniqueHmenu& operator=(const UniqueHmenu&) = delete;

    UniqueHmenu(UniqueHmenu&& other) noexcept : menu_(std::exchange(other.menu_, nullptr)) {}

    UniqueHmenu& operator=(UniqueHmenu&& other) noexcept {
        if (this != &other) {
            Reset(std::exchange(other.menu_, nullptr));
        }
        return *this;
    }

    HMENU Get() const noexcept { return menu_; }
    explicit operator bool() const noexcept { return menu_ != nullptr; }

    void Reset(HMENU newMenu = nullptr) noexcept {
        if (menu_ != nullptr && menu_ != newMenu) {
            ::DestroyMenu(menu_);
        }
        menu_ = newMenu;
    }

private:
    HMENU menu_ = nullptr;
};

class UniqueGlobalMem {
public:
    UniqueGlobalMem() noexcept = default;
    explicit UniqueGlobalMem(HGLOBAL mem) noexcept : mem_(mem) {}

    ~UniqueGlobalMem() noexcept { Reset(); }

    UniqueGlobalMem(const UniqueGlobalMem&) = delete;
    UniqueGlobalMem& operator=(const UniqueGlobalMem&) = delete;

    UniqueGlobalMem(UniqueGlobalMem&& other) noexcept : mem_(std::exchange(other.mem_, nullptr)) {}

    UniqueGlobalMem& operator=(UniqueGlobalMem&& other) noexcept {
        if (this != &other) {
            Reset(std::exchange(other.mem_, nullptr));
        }
        return *this;
    }

    HGLOBAL Get() const noexcept { return mem_; }
    explicit operator bool() const noexcept { return mem_ != nullptr; }

    HGLOBAL Release() noexcept { return std::exchange(mem_, nullptr); }

    void Reset(HGLOBAL newMem = nullptr) noexcept {
        if (mem_ != nullptr && mem_ != newMem) {
            ::GlobalFree(mem_);
        }
        mem_ = newMem;
    }

private:
    HGLOBAL mem_ = nullptr;
};

class SelectObjectGuard {
public:
    SelectObjectGuard(HDC hdc, HGDIOBJ obj) noexcept : hdc_(hdc) {
        if (hdc_ != nullptr && obj != nullptr) {
            prev_ = ::SelectObject(hdc_, obj);
        }
    }

    ~SelectObjectGuard() noexcept {
        if (hdc_ != nullptr && prev_ != nullptr && prev_ != HGDI_ERROR) {
            ::SelectObject(hdc_, prev_);
        }
    }

    SelectObjectGuard(const SelectObjectGuard&) = delete;
    SelectObjectGuard& operator=(const SelectObjectGuard&) = delete;

    bool IsValid() const noexcept { return prev_ != nullptr && prev_ != HGDI_ERROR; }

private:
    HDC hdc_ = nullptr;
    HGDIOBJ prev_ = nullptr;
};

class ComInitScope {
public:
    explicit ComInitScope(DWORD coInitFlags = COINIT_APARTMENTTHREADED) noexcept {
        hr_ = ::CoInitializeEx(nullptr, coInitFlags);
        initialized_ = (hr_ == S_OK || hr_ == S_FALSE);
    }

    ~ComInitScope() noexcept {
        if (initialized_) {
            ::CoUninitialize();
        }
    }

    ComInitScope(const ComInitScope&) = delete;
    ComInitScope& operator=(const ComInitScope&) = delete;

    bool Succeeded() const noexcept { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
    HRESULT Result() const noexcept { return hr_; }

private:
    HRESULT hr_ = E_FAIL;
    bool initialized_ = false;
};

class ClipboardGuard {
public:
    explicit ClipboardGuard(HWND owner) noexcept {
        opened_ = (::OpenClipboard(owner) != FALSE);
    }

    ~ClipboardGuard() noexcept {
        if (opened_) {
            ::CloseClipboard();
        }
    }

    ClipboardGuard(const ClipboardGuard&) = delete;
    ClipboardGuard& operator=(const ClipboardGuard&) = delete;

    bool IsOpen() const noexcept { return opened_; }

private:
    bool opened_ = false;
};

} // namespace saycheese::platform
