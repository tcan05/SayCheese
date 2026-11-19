#include "platform/com_debug.hpp"

#include <psapi.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#include <wrl/client.h>

#include <sstream>
#include <vector>
#endif

namespace saycheese::platform {

ProcessResourceSnapshot CaptureProcessResourceSnapshot() noexcept {
    ProcessResourceSnapshot snap{};
    const HANDLE proc = ::GetCurrentProcess();
    snap.gdiObjects = ::GetGuiResources(proc, GR_GDIOBJECTS);
    snap.userObjects = ::GetGuiResources(proc, GR_USEROBJECTS);
    ::GetProcessHandleCount(proc, &snap.handleCount);

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (::GetProcessMemoryInfo(proc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        snap.privateUsageBytes = pmc.PrivateUsage;
    }
    return snap;
}

DxgiLiveObjectReport ReportDebugDxgiLiveObjects() {
    DxgiLiveObjectReport report{};
#ifdef _DEBUG
    using DXGIGetDebugInterface1Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);

    HMODULE dxgiDebugMod = ::GetModuleHandleW(L"dxgidebug.dll");
    bool loadedHere = false;
    if (dxgiDebugMod == nullptr) {
        dxgiDebugMod = ::LoadLibraryW(L"dxgidebug.dll");
        loadedHere = (dxgiDebugMod != nullptr);
    }
    if (dxgiDebugMod == nullptr) {
        report.available = false;
        report.summary = "dxgidebug.dll not installed on system";
        return report;
    }

    struct ModuleRelease {
        HMODULE mod = nullptr;
        bool own = false;
        ~ModuleRelease() noexcept {
            if (own && mod != nullptr) {
                ::FreeLibrary(mod);
            }
        }
    } modGuard{dxgiDebugMod, loadedHere};

    auto* getDebugInterface1 = reinterpret_cast<DXGIGetDebugInterface1Fn>(
        ::GetProcAddress(dxgiDebugMod, "DXGIGetDebugInterface1"));
    if (getDebugInterface1 == nullptr) {
        report.available = false;
        report.summary = "DXGIGetDebugInterface1 unavailable";
        return report;
    }

    Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
    if (FAILED(getDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))) || !dxgiDebug) {
        report.available = false;
        report.summary = "Failed to acquire IDXGIDebug1";
        return report;
    }

    report.available = true;

    Microsoft::WRL::ComPtr<IDXGIInfoQueue> infoQueue;
    if (SUCCEEDED(getDebugInterface1(0, IID_PPV_ARGS(&infoQueue))) && infoQueue) {
        infoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
    }

    dxgiDebug->ReportLiveObjects(
        DXGI_DEBUG_ALL,
        static_cast<DXGI_DEBUG_RLO_FLAGS>(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));

    if (infoQueue) {
        const UINT64 count = infoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
        std::ostringstream oss;
        size_t liveAppCount = 0;
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T msgLen = 0;
            if (FAILED(infoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &msgLen)) || msgLen == 0) {
                continue;
            }
            std::vector<uint8_t> buf(msgLen);
            auto* msg = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(buf.data());
            if (SUCCEEDED(infoQueue->GetMessage(DXGI_DEBUG_ALL, i, msg, &msgLen)) && msg->pDescription) {
                const std::string desc(msg->pDescription);
                // Live object lines contain "Live " and a non-zero Refcount (excluding summary/header lines).
                if (desc.find("Live ") != std::string::npos &&
                    desc.find("Refcount: 0") == std::string::npos) {
                    ++liveAppCount;
                    oss << "  " << desc << "\n";
                }
            }
        }
        report.liveApplicationObjects = liveAppCount;
        report.summary = liveAppCount == 0 ? "0 live application DXGI/D3D11 objects" : oss.str();
    } else {
        report.summary = "Reported live objects to debug output (IDXGIInfoQueue unavailable)";
    }
#else
    report.available = false;
    report.summary = "DXGI debug layer disabled in Release build";
#endif
    return report;
}

} // namespace saycheese::platform
