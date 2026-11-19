#pragma once

#include "capture/captured_frame.hpp"
#include "platform/com_debug.hpp"

#include <windows.h>

#include <cstdint>
#include <string>

namespace saycheese::app {

struct LeakTestResult {
    bool passed = false;
    bool dxgiSubtestSkipped = false;
    bool ranSteadyStateConfirmWindow = false;
    capture::CaptureBackend backendUsed = capture::CaptureBackend::None;
    platform::ProcessResourceSnapshot cycle2Baseline{};
    platform::ProcessResourceSnapshot cycle22Final{};
    platform::ProcessResourceSnapshot cycle42Confirm{};
    bool crtCheckAvailable = false;
    int64_t crtLeakedBytes = 0;
    platform::DxgiLiveObjectReport dxgiReport{};
    std::string reportText;
};

LeakTestResult RunLeakSelfTest(
    HINSTANCE hInstance,
    int warmupCycles = 2,
    int measuredCycles = 20);

} // namespace saycheese::app
