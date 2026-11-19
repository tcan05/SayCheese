#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

namespace saycheese::platform {

struct ProcessResourceSnapshot {
    DWORD gdiObjects = 0;
    DWORD userObjects = 0;
    DWORD handleCount = 0;
    SIZE_T privateUsageBytes = 0;
};

struct DxgiLiveObjectReport {
    bool available = false;
    size_t liveApplicationObjects = 0;
    std::string summary;
};

ProcessResourceSnapshot CaptureProcessResourceSnapshot() noexcept;

DxgiLiveObjectReport ReportDebugDxgiLiveObjects();

} // namespace saycheese::platform
