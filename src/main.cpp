#include "app/app_controller.hpp"
#include "app/leak_self_test.hpp"
#include "platform/win32_raii.hpp"

#include <windows.h>
#include <crtdbg.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

bool HasCliFlag(const wchar_t* flag) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return false;
    }
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (::wcscmp(argv[i], flag) == 0) {
            found = true;
            break;
        }
    }
    ::LocalFree(argv);
    return found;
}

void WriteStdoutText(const std::string& text) {
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    bool attachedConsole = false;
    if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE || ::GetFileType(hOut) == FILE_TYPE_UNKNOWN) {
        if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
            attachedConsole = true;
            hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
        }
    }
    if (hOut != nullptr && hOut != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ::WriteFile(hOut, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    }
    if (attachedConsole) {
        ::FreeConsole();
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    saycheese::platform::ComInitScope comInit(COINIT_APARTMENTTHREADED);
    if (!comInit.Succeeded()) {
        return 1;
    }

    if (HasCliFlag(L"--self-test-leaks")) {
        const auto result = saycheese::app::RunLeakSelfTest(hInstance, 2, 20);
        WriteStdoutText(result.reportText);
        return result.passed ? 0 : 1;
    }

    saycheese::app::AppController app;
    return app.Run(hInstance);
}
