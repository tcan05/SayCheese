#include "test_runner.hpp"

#include "platform/com_debug.hpp"
#include "platform/dpi_monitor.hpp"
#include "platform/single_instance.hpp"
#include "platform/win32_raii.hpp"

#include <windows.h>

using namespace saycheese::platform;

SC_TEST("VirtualToCanvas maps negative virtual origins to local canvas coordinates") {
    const POINT p1 = VirtualToCanvas(POINT{-1920, -200}, -1920, -200);
    SC_EXPECT_EQ(p1.x, 0);
    SC_EXPECT_EQ(p1.y, 0);

    const POINT p2 = VirtualToCanvas(POINT{0, 0}, -1920, -200);
    SC_EXPECT_EQ(p2.x, 1920);
    SC_EXPECT_EQ(p2.y, 200);

    const POINT p3 = VirtualToCanvas(POINT{100, -50}, -1920, -200);
    SC_EXPECT_EQ(p3.x, 2020);
    SC_EXPECT_EQ(p3.y, 150);
}

SC_TEST("FindMonitorContainingCanvasPoint resolves mixed-DPI monitors and falls back safely") {
    std::vector<MonitorInfo> monitors;
    monitors.push_back(MonitorInfo{
        RECT{-1920, 0, 0, 1080},
        RECT{0, 0, 1920, 1080},
        144,
        144,
        false
    });
    monitors.push_back(MonitorInfo{
        RECT{0, 0, 1920, 1080},
        RECT{1920, 0, 3840, 1080},
        96,
        96,
        true
    });

    const MonitorInfo* m0 = FindMonitorContainingCanvasPoint(monitors, 100.0f, 100.0f);
    SC_EXPECT_TRUE(m0 != nullptr);
    SC_EXPECT_NEAR(m0->Scale(), 1.5f, 1e-4);
    SC_EXPECT_FALSE(m0->isPrimary);

    const MonitorInfo* m1 = FindMonitorContainingCanvasPoint(monitors, 2000.0f, 300.0f);
    SC_EXPECT_TRUE(m1 != nullptr);
    SC_EXPECT_NEAR(m1->Scale(), 1.0f, 1e-4);
    SC_EXPECT_TRUE(m1->isPrimary);

    // Point outside all monitors falls back to primary monitor.
    const MonitorInfo* fallback = FindMonitorContainingCanvasPoint(monitors, -500.0f, 5000.0f);
    SC_EXPECT_TRUE(fallback != nullptr);
    SC_EXPECT_TRUE(fallback->isPrimary);
}

SC_TEST("SingleInstanceGuard enforces exclusive ownership and releases on destruction") {
    const wchar_t* testMutex = L"Local\\SayCheese_UnitTest_Mutex_9F2A11";
    {
        SingleInstanceGuard first(testMutex);
        SC_EXPECT_TRUE(first.IsPrimaryInstance());

        SingleInstanceGuard second(testMutex);
        SC_EXPECT_FALSE(second.IsPrimaryInstance());
    }
    SingleInstanceGuard third(testMutex);
    SC_EXPECT_TRUE(third.IsPrimaryInstance());
}

SC_TEST("UniqueHandle move semantics and zero OS handle leak across repeated allocations") {
    // Warm up snapshot counters.
    (void)CaptureProcessResourceSnapshot();
    const ProcessResourceSnapshot before = CaptureProcessResourceSnapshot();

    for (int i = 0; i < 50; ++i) {
        UniqueHandle ev(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        SC_EXPECT_TRUE(ev.IsValid());
        UniqueHandle moved = std::move(ev);
        SC_EXPECT_FALSE(ev.IsValid());
        SC_EXPECT_TRUE(moved.IsValid());
    }

    const ProcessResourceSnapshot after = CaptureProcessResourceSnapshot();
    SC_EXPECT_EQ(after.handleCount, before.handleCount);
    SC_EXPECT_EQ(after.gdiObjects, before.gdiObjects);
    SC_EXPECT_EQ(after.userObjects, before.userObjects);
}

int main() {
    return saycheese::test::RunAll("platform");
}
