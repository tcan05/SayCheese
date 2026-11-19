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
    using DXGIGetDebugInterfaceFn = HRESULT(WINAPI*)(REFIID, void**);

    struct ModuleRelease {
        HMODULE mod = nullptr;
        ModuleRelease() noexcept = default;
        ModuleRelease(const ModuleRelease&) = delete;
        ModuleRelease& operator=(const ModuleRelease&) = delete;
        void Own(HMODULE m) noexcept { mod = m; }
        ~ModuleRelease() noexcept {
            if (mod != nullptr) {
                ::FreeLibrary(mod);
            }
        }
    };

    // Declare module guards BEFORE ComPtrs so COM Release() always runs before FreeLibrary().
    ModuleRelease dxgiGuard;
    ModuleRelease dbgGuard;
    Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
    Microsoft::WRL::ComPtr<IDXGIInfoQueue> infoQueue;

    // DXGIGetDebugInterface1 is exported by dxgi.dll on Windows 8.1+.
    HMODULE dxgiMod = ::GetModuleHandleW(L"dxgi.dll");
    if (dxgiMod == nullptr) {
        dxgiMod = ::LoadLibraryW(L"dxgi.dll");
        dxgiGuard.Own(dxgiMod);
    }

    if (dxgiMod != nullptr) {
        auto* getDebug1 = reinterpret_cast<DXGIGetDebugInterface1Fn>(
            ::GetProcAddress(dxgiMod, "DXGIGetDebugInterface1"));
        if (getDebug1 != nullptr) {
            getDebug1(0, IID_PPV_ARGS(&dxgiDebug));
            getDebug1(0, IID_PPV_ARGS(&infoQueue));
        }
    }

    // Fallback to dxgidebug.dll's DXGIGetDebugInterface if needed.
    if (!dxgiDebug) {
        HMODULE dbgMod = ::GetModuleHandleW(L"dxgidebug.dll");
        if (dbgMod == nullptr) {
            dbgMod = ::LoadLibraryW(L"dxgidebug.dll");
            dbgGuard.Own(dbgMod);
        }
        if (dbgMod != nullptr) {
            auto* getDebug = reinterpret_cast<DXGIGetDebugInterfaceFn>(
                ::GetProcAddress(dbgMod, "DXGIGetDebugInterface"));
            if (getDebug != nullptr) {
                getDebug(IID_PPV_ARGS(&dxgiDebug));
                if (!infoQueue) {
                    getDebug(IID_PPV_ARGS(&infoQueue));
                }
            }
        }
    }

    if (!dxgiDebug) {
        infoQueue.Reset();
        report.available = false;
        report.summary = "IDXGIDebug1 unavailable (Graphics Tools optional feature not installed)";
        return report;
    }

    report.available = true;

    if (infoQueue) {
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
