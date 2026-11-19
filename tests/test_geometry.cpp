#include "test_runner.hpp"

#include "platform/dpi_monitor.hpp"
#include "tools/geometry.hpp"
#include "ui/toolbar_layout.hpp"

#include <cmath>
#include <vector>

using namespace saycheese::tools;
using namespace saycheese::ui;
using namespace saycheese::platform;

namespace {

bool RectInside(RectF inner, RectF outer, float eps = 0.5f) {
    return inner.left >= outer.left - eps &&
           inner.top >= outer.top - eps &&
           inner.right <= outer.right + eps &&
           inner.bottom <= outer.bottom + eps;
}

} // namespace

SC_TEST("RectF normalizes reversed drags and detects empty rects") {
    const RectF reversed{300.0f, 200.0f, 100.0f, 50.0f};
    const RectF norm = reversed.Normalized();
    SC_EXPECT_NEAR(norm.left, 100.0f, 1e-4);
    SC_EXPECT_NEAR(norm.top, 50.0f, 1e-4);
    SC_EXPECT_NEAR(norm.right, 300.0f, 1e-4);
    SC_EXPECT_NEAR(norm.bottom, 200.0f, 1e-4);
    SC_EXPECT_NEAR(norm.Width(), 200.0f, 1e-4);
    SC_EXPECT_NEAR(norm.Height(), 150.0f, 1e-4);
    SC_EXPECT_FALSE(norm.IsEmpty());

    SC_EXPECT_TRUE((RectF{10.0f, 10.0f, 10.0f, 50.0f}.IsEmpty()));
    SC_EXPECT_TRUE((RectF{10.0f, 20.0f, 50.0f, 20.0f}.IsEmpty()));
}

SC_TEST("HitTestSelection resolves all 8 resize handles, body, and outside") {
    SelectionBox sel{RectF{100.0f, 100.0f, 300.0f, 200.0f}, true};
    const float r = 8.0f;

    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {100.0f, 100.0f}, r)), static_cast<int>(ResizeHandle::TopLeft));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {200.0f, 100.0f}, r)), static_cast<int>(ResizeHandle::Top));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {300.0f, 100.0f}, r)), static_cast<int>(ResizeHandle::TopRight));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {300.0f, 150.0f}, r)), static_cast<int>(ResizeHandle::Right));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {300.0f, 200.0f}, r)), static_cast<int>(ResizeHandle::BottomRight));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {200.0f, 200.0f}, r)), static_cast<int>(ResizeHandle::Bottom));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {100.0f, 200.0f}, r)), static_cast<int>(ResizeHandle::BottomLeft));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {100.0f, 150.0f}, r)), static_cast<int>(ResizeHandle::Left));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {200.0f, 150.0f}, r)), static_cast<int>(ResizeHandle::MoveBody));
    SC_EXPECT_EQ(static_cast<int>(HitTestSelection(sel, {50.0f, 50.0f}, r)), static_cast<int>(ResizeHandle::None));
}

SC_TEST("ApplySelectionDrag clamps move without shrinking and enforces minSize on resize") {
    const RectF bounds{0.0f, 0.0f, 1920.0f, 1080.0f};
    const RectF start{1700.0f, 900.0f, 1900.0f, 1050.0f};

    const RectF moved = ApplySelectionDrag(start, ResizeHandle::MoveBody, {200.0f, 200.0f}, bounds);
    SC_EXPECT_NEAR(moved.Width(), 200.0f, 1e-3);
    SC_EXPECT_NEAR(moved.Height(), 150.0f, 1e-3);
    SC_EXPECT_NEAR(moved.right, 1920.0f, 1e-3);
    SC_EXPECT_NEAR(moved.bottom, 1080.0f, 1e-3);

    const RectF movedNeg = ApplySelectionDrag(RectF{10.0f, 10.0f, 110.0f, 60.0f}, ResizeHandle::MoveBody, {-100.0f, -100.0f}, bounds);
    SC_EXPECT_NEAR(movedNeg.left, 0.0f, 1e-3);
    SC_EXPECT_NEAR(movedNeg.top, 0.0f, 1e-3);
    SC_EXPECT_NEAR(movedNeg.Width(), 100.0f, 1e-3);
    SC_EXPECT_NEAR(movedNeg.Height(), 50.0f, 1e-3);

    // Dragging left edge past right edge clamps to minSize.
    const RectF shrunk = ApplySelectionDrag(RectF{100.0f, 100.0f, 200.0f, 200.0f}, ResizeHandle::Left, {500.0f, 0.0f}, bounds, 8.0f);
    SC_EXPECT_NEAR(shrunk.right, 200.0f, 1e-3);
    SC_EXPECT_NEAR(shrunk.Width(), 8.0f, 1e-3);
}

SC_TEST("ApplySelectionDrag handles small edge-adjacent selections without invalid clamp bounds") {
    const RectF bounds{0.0f, 0.0f, 1920.0f, 1080.0f};

    // 5x5 selection pinned to top-left corner resized with minSize=8 must stay inside bounds without UB/crash.
    const RectF tlSmall{0.0f, 0.0f, 5.0f, 5.0f};
    const RectF rLeft = ApplySelectionDrag(tlSmall, ResizeHandle::Left, {-10.0f, 0.0f}, bounds, 8.0f);
    SC_EXPECT_TRUE(rLeft.left >= bounds.left && rLeft.right <= bounds.right);
    SC_EXPECT_TRUE(rLeft.left <= rLeft.right);

    const RectF rTop = ApplySelectionDrag(tlSmall, ResizeHandle::Top, {0.0f, -10.0f}, bounds, 8.0f);
    SC_EXPECT_TRUE(rTop.top >= bounds.top && rTop.bottom <= bounds.bottom);
    SC_EXPECT_TRUE(rTop.top <= rTop.bottom);

    const RectF rTl = ApplySelectionDrag(tlSmall, ResizeHandle::TopLeft, {20.0f, 20.0f}, bounds, 8.0f);
    SC_EXPECT_TRUE(rTl.left >= bounds.left && rTl.top >= bounds.top && rTl.left <= rTl.right && rTl.top <= rTl.bottom);

    // 5x5 selection pinned to bottom-right corner resized with minSize=8.
    const RectF brSmall{1915.0f, 1075.0f, 1920.0f, 1080.0f};
    const RectF rRight = ApplySelectionDrag(brSmall, ResizeHandle::Right, {10.0f, 0.0f}, bounds, 8.0f);
    SC_EXPECT_TRUE(rRight.left >= bounds.left && rRight.right <= bounds.right && rRight.left <= rRight.right);

    const RectF rBot = ApplySelectionDrag(brSmall, ResizeHandle::Bottom, {0.0f, 10.0f}, bounds, 8.0f);
    SC_EXPECT_TRUE(rBot.top >= bounds.top && rBot.bottom <= bounds.bottom && rBot.top <= rBot.bottom);

    const RectF rBr = ApplySelectionDrag(brSmall, ResizeHandle::BottomRight, {-20.0f, -20.0f}, bounds, 8.0f);
    SC_EXPECT_TRUE(rBr.right <= bounds.right && rBr.bottom <= bounds.bottom && rBr.left <= rBr.right && rBr.top <= rBr.bottom);
}

SC_TEST("SnapLine45Deg snaps angles to nearest 45-degree increment") {
    const PointF horiz = SnapLine45Deg({0.0f, 0.0f}, {100.0f, 20.0f});
    SC_EXPECT_NEAR(horiz.y, 0.0f, 1e-3);
    SC_EXPECT_TRUE(horiz.x > 95.0f);

    const PointF diag = SnapLine45Deg({0.0f, 0.0f}, {100.0f, 90.0f});
    SC_EXPECT_NEAR(diag.x, diag.y, 1e-3);
    SC_EXPECT_TRUE(diag.x > 90.0f);

    const PointF vert = SnapLine45Deg({0.0f, 0.0f}, {-5.0f, -100.0f});
    SC_EXPECT_NEAR(vert.x, 0.0f, 1e-3);
    SC_EXPECT_TRUE(vert.y < -95.0f);
}

SC_TEST("ConstrainSquare locks aspect ratio in all drag quadrants") {
    const RectF sq = ConstrainSquare({100.0f, 100.0f}, {60.0f, 180.0f});
    SC_EXPECT_NEAR(sq.Width(), 80.0f, 1e-3);
    SC_EXPECT_NEAR(sq.Height(), 80.0f, 1e-3);
    SC_EXPECT_NEAR(sq.right, 100.0f, 1e-3);
    SC_EXPECT_NEAR(sq.top, 100.0f, 1e-3);
    SC_EXPECT_NEAR(sq.left, 20.0f, 1e-3);
    SC_EXPECT_NEAR(sq.bottom, 180.0f, 1e-3);
}

SC_TEST("ComputeArrowhead scales cleanly and terminates shaft behind tip") {
    const ArrowheadTriangle head = ComputeArrowhead({0.0f, 0.0f}, {100.0f, 0.0f}, 5.0f);
    SC_EXPECT_NEAR(head.tip.x, 100.0f, 1e-3);
    SC_EXPECT_NEAR(head.tip.y, 0.0f, 1e-3);
    const float headLen = head.tip.x - head.left.x;
    SC_EXPECT_TRUE(headLen >= 20.0f - 1e-3);
    SC_EXPECT_TRUE(head.left.x < head.shaftEnd.x && head.shaftEnd.x < head.tip.x);
    SC_EXPECT_TRUE(head.left.y < 0.0f);
    SC_EXPECT_TRUE(head.right.y > 0.0f);
    SC_EXPECT_NEAR(head.left.y, -head.right.y, 1e-3);
}

SC_TEST("SmoothPolylineQuadratic handles short and multi-point strokes safely") {
    SC_EXPECT_EQ(SmoothPolylineQuadratic({}).size(), static_cast<size_t>(0));
    SC_EXPECT_EQ(SmoothPolylineQuadratic({{5.0f, 5.0f}}).size(), static_cast<size_t>(0));

    const auto seg2 = SmoothPolylineQuadratic({{0.0f, 0.0f}, {10.0f, 20.0f}});
    SC_EXPECT_EQ(seg2.size(), static_cast<size_t>(1));
    SC_EXPECT_NEAR(seg2[0].end.x, 10.0f, 1e-3);
    SC_EXPECT_NEAR(seg2[0].end.y, 20.0f, 1e-3);

    const auto seg4 = SmoothPolylineQuadratic({{0.0f, 0.0f}, {10.0f, 0.0f}, {20.0f, 10.0f}, {30.0f, 10.0f}});
    SC_EXPECT_EQ(seg4.size(), static_cast<size_t>(3));
    SC_EXPECT_NEAR(seg4[0].control.x, 10.0f, 1e-3);
    SC_EXPECT_NEAR(seg4[0].end.x, 15.0f, 1e-3);
    SC_EXPECT_NEAR(seg4[0].end.y, 5.0f, 1e-3);
    SC_EXPECT_NEAR(seg4.back().end.x, 30.0f, 1e-3);
    SC_EXPECT_NEAR(seg4.back().end.y, 10.0f, 1e-3);
}

SC_TEST("Toolbar layout clamps inside mixed-DPI negative-origin monitor and handles full-monitor selection") {
    // Monitor 0: virtual [-1920..0, 0..1080] @ 150% DPI -> canvas [0..1920, 0..1080]
    // Monitor 1: virtual [0..1920, 0..1080] @ 100% DPI -> canvas [1920..3840, 0..1080]
    std::vector<MonitorInfo> monitors{
        MonitorInfo{RECT{-1920, 0, 0, 1080}, RECT{0, 0, 1920, 1080}, 144, 144, false},
        MonitorInfo{RECT{0, 0, 1920, 1080}, RECT{1920, 0, 3840, 1080}, 96, 96, true}
    };
    const RectF canvas{0.0f, 0.0f, 3840.0f, 1080.0f};
    const RectF mon0Bounds{0.0f, 0.0f, 1920.0f, 1080.0f};
    const RectF mon1Bounds{1920.0f, 0.0f, 3840.0f, 1080.0f};

    // Case A: Selection near right & bottom edge of 150% DPI Monitor 0.
    ToolbarLayoutState state{};
    state.colorPopupOpen = true;
    state.censorActive = true;
    const RectF nearEdgeSel{1600.0f, 750.0f, 1915.0f, 1075.0f};
    const ToolbarLayoutResult lay0 = ComputeToolbarLayout(nearEdgeSel, monitors, canvas, state);

    SC_EXPECT_NEAR(lay0.uiScale, 1.5f, 1e-3);
    SC_EXPECT_TRUE(RectInside(lay0.toolBarRect, mon0Bounds));
    SC_EXPECT_TRUE(RectInside(lay0.actionBarRect, mon0Bounds));
    SC_EXPECT_TRUE(RectInside(lay0.badgeRect, mon0Bounds));
    SC_EXPECT_TRUE(RectInside(lay0.colorPopupRect, mon0Bounds));
    SC_EXPECT_TRUE(RectInside(lay0.censorPillRect, mon0Bounds));
    // Vertical tool bar should flip left of selection when right side has no room.
    SC_EXPECT_TRUE(lay0.toolBarRect.right <= nearEdgeSel.left + 1.0f);

    // Hit test every generated button center.
    SC_EXPECT_TRUE(lay0.buttons.size() >= 20);
    for (const auto& btn : lay0.buttons) {
        const PointF center{0.5f * (btn.rect.left + btn.rect.right), 0.5f * (btn.rect.top + btn.rect.bottom)};
        SC_EXPECT_EQ(static_cast<int>(HitTestToolbar(lay0, center)), static_cast<int>(btn.id));
    }
    SC_EXPECT_EQ(static_cast<int>(HitTestToolbar(lay0, {5.0f, 5.0f})), static_cast<int>(ToolbarButtonId::None));

    // Case B: Full-monitor selection covering 100% of Monitor 1.
    ToolbarLayoutState stateFull{};
    stateFull.strokePopupOpen = true;
    const ToolbarLayoutResult lay1 = ComputeToolbarLayout(mon1Bounds, monitors, canvas, stateFull);
    SC_EXPECT_NEAR(lay1.uiScale, 1.0f, 1e-3);
    SC_EXPECT_TRUE(RectInside(lay1.toolBarRect, mon1Bounds));
    SC_EXPECT_TRUE(RectInside(lay1.actionBarRect, mon1Bounds));
    SC_EXPECT_TRUE(RectInside(lay1.badgeRect, mon1Bounds));
    SC_EXPECT_TRUE(RectInside(lay1.strokePopupRect, mon1Bounds));

    int popupButtonCount = 0;
    bool hasPresetThin = false;
    bool hasPresetMedium = false;
    for (const auto& btn : lay1.buttons) {
        if (btn.isPopup) {
            ++popupButtonCount;
            if (btn.id == ToolbarButtonId::PresetThin) hasPresetThin = true;
            if (btn.id == ToolbarButtonId::PresetMedium) hasPresetMedium = true;
        }
    }
    SC_EXPECT_EQ(popupButtonCount, 2);
    SC_EXPECT_TRUE(hasPresetThin);
    SC_EXPECT_TRUE(hasPresetMedium);
}

int main() {
    return saycheese::test::RunAll("geometry_and_toolbar_layout");
}
