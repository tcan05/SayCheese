#include "app/app_controller.hpp"
#include "platform/win32_raii.hpp"

#include <windows.h>
#include <crtdbg.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    saycheese::platform::ComInitScope comInit(COINIT_APARTMENTTHREADED);
    if (!comInit.Succeeded()) {
        return 1;
    }

    saycheese::app::AppController app;
    return app.Run(hInstance);
}
