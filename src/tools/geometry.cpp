#include "tools/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace saycheese::tools {

RectF RectF::Normalized() const noexcept {
    return RectF{
        std::min(left, right),
        std::min(top, bottom),
        std::max(left, right),
        std::max(top, bottom)
    };
}

RectF ClampRectToBounds(RectF r, RectF bounds) noexcept {
    r = r.Normalized();
    const float w = r.Width();
    const float h = r.Height();
    const float bw = std::max(0.0f, bounds.Width());
    const float bh = std::max(0.0f, bounds.Height());

    if (w >= bw) {
        r.left = bounds.left;
        r.right = bounds.right;
    } else {
        if (r.left < bounds.left) {
            r.left = bounds.left;
            r.right = bounds.left + w;
        } else if (r.right > bounds.right) {
            r.right = bounds.right;
            r.left = bounds.right - w;
        }
    }

    if (h >= bh) {
        r.top = bounds.top;
        r.bottom = bounds.bottom;
    } else {
        if (r.top < bounds.top) {
            r.top = bounds.top;
            r.bottom = bounds.top + h;
        } else if (r.bottom > bounds.bottom) {
            r.bottom = bounds.bottom;
            r.top = bounds.bottom - h;
        }
    }

    return r;
}

ResizeHandle HitTestSelection(const SelectionBox& sel, PointF pt, float handleRadiusPx) noexcept {
    if (!sel.active || sel.rect.IsEmpty()) {
        return ResizeHandle::None;
    }

    const RectF r = sel.rect.Normalized();
    const float cx = 0.5f * (r.left + r.right);
    const float cy = 0.5f * (r.top + r.bottom);
    const float radiusSq = handleRadiusPx * handleRadiusPx;

    auto nearPoint = [&](float hx, float hy) noexcept {
        const float dx = pt.x - hx;
        const float dy = pt.y - hy;
        return (dx * dx + dy * dy) <= radiusSq;
    };

    // Corners take priority over edge midpoints on small boxes.
    if (nearPoint(r.left, r.top)) return ResizeHandle::TopLeft;
    if (nearPoint(r.right, r.top)) return ResizeHandle::TopRight;
    if (nearPoint(r.right, r.bottom)) return ResizeHandle::BottomRight;
    if (nearPoint(r.left, r.bottom)) return ResizeHandle::BottomLeft;

    if (nearPoint(cx, r.top)) return ResizeHandle::Top;
    if (nearPoint(r.right, cy)) return ResizeHandle::Right;
    if (nearPoint(cx, r.bottom)) return ResizeHandle::Bottom;
    if (nearPoint(r.left, cy)) return ResizeHandle::Left;

    if (r.Contains(pt)) {
        return ResizeHandle::MoveBody;
    }
    return ResizeHandle::None;
}

RectF ApplySelectionDrag(
    RectF startRect,
    ResizeHandle handle,
    PointF delta,
    RectF canvasBounds,
    float minSize) noexcept {
    RectF r = startRect.Normalized();
    minSize = std::max(1.0f, minSize);

    if (handle == ResizeHandle::MoveBody) {
        r.left += delta.x;
        r.right += delta.x;
        r.top += delta.y;
        r.bottom += delta.y;
        return ClampRectToBounds(r, canvasBounds);
    }

    switch (handle) {
    case ResizeHandle::TopLeft:
        r.left = std::clamp(r.left + delta.x, canvasBounds.left, r.right - minSize);
        r.top = std::clamp(r.top + delta.y, canvasBounds.top, r.bottom - minSize);
        break;
    case ResizeHandle::Top:
        r.top = std::clamp(r.top + delta.y, canvasBounds.top, r.bottom - minSize);
        break;
    case ResizeHandle::TopRight:
        r.right = std::clamp(r.right + delta.x, r.left + minSize, canvasBounds.right);
        r.top = std::clamp(r.top + delta.y, canvasBounds.top, r.bottom - minSize);
        break;
    case ResizeHandle::Right:
        r.right = std::clamp(r.right + delta.x, r.left + minSize, canvasBounds.right);
        break;
    case ResizeHandle::BottomRight:
        r.right = std::clamp(r.right + delta.x, r.left + minSize, canvasBounds.right);
        r.bottom = std::clamp(r.bottom + delta.y, r.top + minSize, canvasBounds.bottom);
        break;
    case ResizeHandle::Bottom:
        r.bottom = std::clamp(r.bottom + delta.y, r.top + minSize, canvasBounds.bottom);
        break;
    case ResizeHandle::BottomLeft:
        r.left = std::clamp(r.left + delta.x, canvasBounds.left, r.right - minSize);
        r.bottom = std::clamp(r.bottom + delta.y, r.top + minSize, canvasBounds.bottom);
        break;
    case ResizeHandle::Left:
        r.left = std::clamp(r.left + delta.x, canvasBounds.left, r.right - minSize);
        break;
    case ResizeHandle::None:
    case ResizeHandle::MoveBody:
        break;
    }

    return r;
}

PointF SnapLine45Deg(PointF origin, PointF target) noexcept {
    const float dx = target.x - origin.x;
    const float dy = target.y - origin.y;
    const float len = std::hypot(dx, dy);
    if (len < 1e-4f) {
        return origin;
    }

    constexpr float kStep = std::numbers::pi_v<float> / 4.0f;
    const float angle = std::atan2(dy, dx);
    const float snapped = std::round(angle / kStep) * kStep;
    return PointF{
        origin.x + std::cos(snapped) * len,
        origin.y + std::sin(snapped) * len
    };
}

RectF ConstrainSquare(PointF origin, PointF target) noexcept {
    const float dx = target.x - origin.x;
    const float dy = target.y - origin.y;
    const float side = std::max(std::fabs(dx), std::fabs(dy));
    const float sx = dx < 0.0f ? -1.0f : 1.0f;
    const float sy = dy < 0.0f ? -1.0f : 1.0f;
    return RectF{origin.x, origin.y, origin.x + sx * side, origin.y + sy * side}.Normalized();
}

ArrowheadTriangle ComputeArrowhead(PointF start, PointF end, float strokeWidth) noexcept {
    ArrowheadTriangle tri{};
    tri.tip = end;

    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float len = std::hypot(dx, dy);
    if (len < 1e-4f) {
        tri.left = end;
        tri.right = end;
        tri.shaftEnd = end;
        return tri;
    }

    const float ux = dx / len;
    const float uy = dy / len;
    // Screen-space (Y-down) left perpendicular of travel direction (ux, uy) is (uy, -ux).
    const float px = uy;
    const float py = -ux;

    const float desiredLen = std::max(12.0f, strokeWidth * 4.0f);
    const float headLen = std::min(desiredLen, len * 0.75f);
    const float halfWidth = headLen * 0.44f;

    const float baseX = end.x - ux * headLen;
    const float baseY = end.y - uy * headLen;

    tri.left = PointF{baseX + px * halfWidth, baseY + py * halfWidth};
    tri.right = PointF{baseX - px * halfWidth, baseY - py * halfWidth};
    // Stop shaft slightly inside arrowhead base so round cap never protrudes past tip or leaves gap.
    const float shaftRetreat = headLen * 0.78f;
    tri.shaftEnd = PointF{end.x - ux * shaftRetreat, end.y - uy * shaftRetreat};
    return tri;
}

std::vector<QuadSegment> SmoothPolylineQuadratic(const std::vector<PointF>& points) {
    std::vector<QuadSegment> out;
    if (points.size() < 2) {
        return out;
    }
    if (points.size() == 2) {
        const PointF mid{
            0.5f * (points[0].x + points[1].x),
            0.5f * (points[0].y + points[1].y)
        };
        out.push_back(QuadSegment{mid, points[1]});
        return out;
    }

    out.reserve(points.size() - 1);
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        const PointF mid{
            0.5f * (points[i].x + points[i + 1].x),
            0.5f * (points[i].y + points[i + 1].y)
        };
        out.push_back(QuadSegment{points[i], mid});
    }
    const PointF& prev = points[points.size() - 2];
    const PointF& last = points.back();
    out.push_back(QuadSegment{prev, last});
    return out;
}

} // namespace saycheese::tools
