#pragma once

#include <vector>

namespace saycheese::tools {

struct PointF {
    float x = 0.0f;
    float y = 0.0f;
};

struct RectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    float Width() const noexcept { return right - left; }
    float Height() const noexcept { return bottom - top; }
    bool IsEmpty() const noexcept { return Width() <= 0.0f || Height() <= 0.0f; }

    bool Contains(PointF p) const noexcept {
        return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
    }

    RectF Normalized() const noexcept;
};

enum class ResizeHandle {
    None = 0,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
    MoveBody
};

struct SelectionBox {
    RectF rect{};
    bool active = false;
};

struct ArrowheadTriangle {
    PointF tip{};
    PointF left{};
    PointF right{};
    PointF shaftEnd{};
};

struct QuadSegment {
    PointF control{};
    PointF end{};
};

RectF ClampRectToBounds(RectF r, RectF bounds) noexcept;

ResizeHandle HitTestSelection(const SelectionBox& sel, PointF pt, float handleRadiusPx) noexcept;

RectF ApplySelectionDrag(
    RectF startRect,
    ResizeHandle handle,
    PointF delta,
    RectF canvasBounds,
    float minSize = 4.0f) noexcept;

PointF SnapLine45Deg(PointF origin, PointF target) noexcept;

RectF ConstrainSquare(PointF origin, PointF target) noexcept;

ArrowheadTriangle ComputeArrowhead(PointF start, PointF end, float strokeWidth) noexcept;

std::vector<QuadSegment> SmoothPolylineQuadratic(const std::vector<PointF>& points);

} // namespace saycheese::tools
