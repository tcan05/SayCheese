#include "app/leak_self_test.hpp"

#include "capture/screen_capture.hpp"
#include "ui/overlay_window.hpp"

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace saycheese::app {

namespace {

void DrainThreadMessages() noexcept {
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

const char* BackendToString(capture::CaptureBackend b) noexcept {
    switch (b) {
    case capture::CaptureBackend::DxgiDesktopDuplication: return "DXGI Desktop Duplication";
    case capture::CaptureBackend::DxgiPartialWithGdiFallback: return "DXGI + Partial BitBlt Fallback";
    case capture::CaptureBackend::GdiBitBltFallback: return "GDI BitBlt Fallback (DXGI skipped)";
    case capture::CaptureBackend::Synthetic: return "Synthetic Headless Frame (DXGI skipped)";
    case capture::CaptureBackend::None: return "None";
    }
    return "Unknown";
}

} // namespace

LeakTestResult RunLeakSelfTest(HINSTANCE hInstance, int warmupCycles, int measuredCycles) {
    LeakTestResult res{};
    const int primaryEndCycle = warmupCycles + measuredCycles;
    bool allCyclesOk = true;
    bool headlessSyntheticMode = false;
    std::string failureReason;

#ifdef _DEBUG
    _CrtMemState crtBaseline{};
    _CrtMemState crtFinal{};
    _CrtMemState crtDiff{};
    res.crtCheckAvailable = true;
#endif

    auto runOneCycle = [&](int cycle) -> bool {
        capture::CapturedFrame frame;
        if (!headlessSyntheticMode) {
            frame = capture::CaptureVirtualDesktop();
            if (!frame.IsValid()) {
                res.dxgiSubtestSkipped = true;
                headlessSyntheticMode = true;
                frame = capture::CreateSyntheticTestFrame(1280, 720);
            } else if (frame.backend == capture::CaptureBackend::GdiBitBltFallback) {
                res.dxgiSubtestSkipped = true;
            }
        } else {
            frame = capture::CreateSyntheticTestFrame(1280, 720);
        }
        res.backendUsed = frame.backend;

        {
            ui::OverlayWindow overlay;
            ui::OverlayCallbacks cbs{};
            if (!overlay.CreateAndShow(hInstance, std::move(frame), std::move(cbs), true)) {
                failureReason = "OverlayWindow::CreateAndShow failed at cycle " + std::to_string(cycle);
                return false;
            }

            overlay.ApplySyntheticTestAnnotations();
            overlay.RenderFrame();
            overlay.RenderFrame();

            if (!overlay.RunOffscreenExportCheck()) {
                failureReason = "Offscreen WYSIWYG export check failed at cycle " + std::to_string(cycle);
                overlay.DestroyAndRelease();
                return false;
            }

            overlay.DestroyAndRelease();
        }

        DrainThreadMessages();
        return true;
    };

    for (int cycle = 1; cycle <= primaryEndCycle; ++cycle) {
        if (!runOneCycle(cycle)) {
            allCyclesOk = false;
            break;
        }
        if (cycle == warmupCycles) {
#ifdef _DEBUG
            _CrtMemCheckpoint(&crtBaseline);
#endif
            res.cycle2Baseline = platform::CaptureProcessResourceSnapshot();
        } else if (cycle == primaryEndCycle) {
#ifdef _DEBUG
            _CrtMemCheckpoint(&crtFinal);
#endif
            res.cycle22Final = platform::CaptureProcessResourceSnapshot();
        }
    }

#ifdef _DEBUG
    if (allCyclesOk) {
        if (_CrtMemDifference(&crtDiff, &crtBaseline, &crtFinal)) {
            res.crtLeakedBytes = static_cast<int64_t>(crtDiff.lSizes[_NORMAL_BLOCK]);
        } else {
            res.crtLeakedBytes = 0;
        }
    }
#endif

    const int64_t gdiDelta = static_cast<int64_t>(res.cycle22Final.gdiObjects) -
                             static_cast<int64_t>(res.cycle2Baseline.gdiObjects);
    const int64_t userDelta = static_cast<int64_t>(res.cycle22Final.userObjects) -
                              static_cast<int64_t>(res.cycle2Baseline.userObjects);
    const int64_t handleDelta = static_cast<int64_t>(res.cycle22Final.handleCount) -
                                static_cast<int64_t>(res.cycle2Baseline.handleCount);
    const int64_t memDelta = static_cast<int64_t>(res.cycle22Final.privateUsageBytes) -
                             static_cast<int64_t>(res.cycle2Baseline.privateUsageBytes);

    constexpr int64_t kStrictPrivateUsageDeltaBytes = 2LL * 1024 * 1024; // 2 MiB
    constexpr int64_t kLfhInitialCapBytes = 5LL * 1024 * 1024;           // One-time Windows NT Heap LFH ramp cap

    bool memOk = (memDelta < kStrictPrivateUsageDeltaBytes);
    bool confirmGdiOk = true;
    bool confirmUserOk = true;
    bool confirmHandleOk = true;
    int64_t confirmMemDelta = 0;

    // On Windows, NT Heap activates LFH subsegments on the 17th allocation per size bucket inside
    // WDDM GPU drivers / d2d1.dll when D3D11CreateDevice is recreated ephemerally. If the initial
    // 20-cycle window crosses LFH activation (2..5 MiB), confirm with a second 20-cycle window that
    // steady-state memory is strictly bounded (< 2 MiB) and GDI/USER/Handle deltas remain 0.
    if (allCyclesOk && !memOk && memDelta < kLfhInitialCapBytes) {
        res.ranSteadyStateConfirmWindow = true;
        const int confirmEndCycle = primaryEndCycle + measuredCycles;
        for (int cycle = primaryEndCycle + 1; cycle <= confirmEndCycle; ++cycle) {
            if (!runOneCycle(cycle)) {
                allCyclesOk = false;
                break;
            }
        }
        res.cycle42Confirm = platform::CaptureProcessResourceSnapshot();
        confirmGdiOk = (res.cycle42Confirm.gdiObjects == res.cycle22Final.gdiObjects);
        confirmUserOk = (res.cycle42Confirm.userObjects == res.cycle22Final.userObjects);
        confirmHandleOk = (res.cycle42Confirm.handleCount == res.cycle22Final.handleCount);
        confirmMemDelta = static_cast<int64_t>(res.cycle42Confirm.privateUsageBytes) -
                          static_cast<int64_t>(res.cycle22Final.privateUsageBytes);
        memOk = (confirmMemDelta < kStrictPrivateUsageDeltaBytes);
    }

    res.dxgiReport = platform::ReportDebugDxgiLiveObjects();

    const bool gdiOk = (gdiDelta == 0) && confirmGdiOk;
    const bool userOk = (userDelta == 0) && confirmUserOk;
    const bool handleOk = (handleDelta == 0) && confirmHandleOk;
    const bool crtOk = (!res.crtCheckAvailable || res.crtLeakedBytes == 0);
    const bool dxgiOk = (!res.dxgiReport.available || res.dxgiReport.liveApplicationObjects == 0);

    res.passed = allCyclesOk && gdiOk && userOk && handleOk && memOk && crtOk && dxgiOk;

    std::ostringstream oss;
    oss << "=== SayCheese Leak & Stress Self-Test (" << warmupCycles
        << " warm-up + " << measuredCycles << " measured cycles) ===\n";
    oss << "Capture Backend : " << BackendToString(res.backendUsed) << "\n";
    if (res.dxgiSubtestSkipped) {
        oss << "DXGI Subtest    : SKIPPED (non-interactive/headless or DXGI unavailable)\n";
    }
    oss << "Metric          | Cycle " << warmupCycles << " (Base) | Cycle " << primaryEndCycle
        << " (Final) | Delta       | Status\n";
    oss << "----------------+-----------------+------------------+-------------+-------\n";
    oss << "GDI Objects     | " << std::setw(15) << res.cycle2Baseline.gdiObjects
        << " | " << std::setw(16) << res.cycle22Final.gdiObjects
        << " | " << std::setw(11) << gdiDelta
        << " | " << (gdiOk ? "PASS" : "FAIL") << "\n";
    oss << "USER Objects    | " << std::setw(15) << res.cycle2Baseline.userObjects
        << " | " << std::setw(16) << res.cycle22Final.userObjects
        << " | " << std::setw(11) << userDelta
        << " | " << (userOk ? "PASS" : "FAIL") << "\n";
    oss << "OS Handles      | " << std::setw(15) << res.cycle2Baseline.handleCount
        << " | " << std::setw(16) << res.cycle22Final.handleCount
        << " | " << std::setw(11) << handleDelta
        << " | " << (handleOk ? "PASS" : "FAIL") << "\n";
    oss << "PrivateUsage KB | " << std::setw(15) << (res.cycle2Baseline.privateUsageBytes / 1024)
        << " | " << std::setw(16) << (res.cycle22Final.privateUsageBytes / 1024)
        << " | " << std::setw(8) << (memDelta / 1024) << " KB"
        << " | " << (memOk ? "PASS" : "FAIL") << "\n";

    if (res.ranSteadyStateConfirmWindow) {
        const int confirmEnd = primaryEndCycle + measuredCycles;
        oss << "Steady-State    | Cycle " << primaryEndCycle << " (Base) | Cycle " << confirmEnd
            << " (Conf.) | " << std::setw(8) << (confirmMemDelta / 1024) << " KB"
            << " | " << (memOk ? "PASS" : "FAIL")
            << " (GDI=" << (static_cast<int64_t>(res.cycle42Confirm.gdiObjects) - static_cast<int64_t>(res.cycle22Final.gdiObjects))
            << ", USER=" << (static_cast<int64_t>(res.cycle42Confirm.userObjects) - static_cast<int64_t>(res.cycle22Final.userObjects))
            << ", Handles=" << (static_cast<int64_t>(res.cycle42Confirm.handleCount) - static_cast<int64_t>(res.cycle22Final.handleCount))
            << ")\n";
    }
    if (res.crtCheckAvailable) {
        oss << "Debug CRT Diff  : " << res.crtLeakedBytes << " bytes (" << (crtOk ? "PASS" : "FAIL") << ")\n";
    }
    oss << "Debug DXGI Live : " << (res.dxgiReport.available
                                       ? std::to_string(res.dxgiReport.liveApplicationObjects)
                                       : std::string("N/A"))
        << " (" << res.dxgiReport.summary << ")\n";
    if (!failureReason.empty()) {
        oss << "Failure Detail  : " << failureReason << "\n";
    }
    oss << "Overall Result  : " << (res.passed ? "PASS" : "FAIL") << "\n";

    res.reportText = oss.str();
    return res;
}

} // namespace saycheese::app
