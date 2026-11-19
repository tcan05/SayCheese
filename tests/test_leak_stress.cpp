#include "app/leak_self_test.hpp"
#include "platform/win32_raii.hpp"

#include <windows.h>
#include <crtdbg.h>

#include <iostream>

int main() {
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    saycheese::platform::ComInitScope comInit(COINIT_APARTMENTTHREADED);
    if (!comInit.Succeeded()) {
        std::cerr << "Failed to initialize COM.\n";
        return 1;
    }

    const auto result = saycheese::app::RunLeakSelfTest(::GetModuleHandleW(nullptr), 2, 20);
    std::cout << result.reportText << std::flush;
    return result.passed ? 0 : 1;
}
