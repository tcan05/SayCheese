#pragma once

#include "tools/geometry.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace saycheese::tools {

enum class ToolType {
    Select = 0,
    Pen,
    Line,
    Arrow,
    Rectangle,
    Highlighter,
    Text,
    Censor
};

enum class CensorMode {
    Mosaic = 0,
    GaussianBlur
};

struct ColorRGBA {
    uint8_t r = 255;
    uint8_t g = 59;
    uint8_t b = 48;
    uint8_t a = 255;

    bool operator==(const ColorRGBA&) const noexcept = default;
};

inline constexpr std::array<ColorRGBA, 6> kQuickPalette{{
    ColorRGBA{255, 59, 48, 255},   // Red
    ColorRGBA{255, 204, 0, 255},   // Yellow
    ColorRGBA{52, 199, 89, 255},   // Green
    ColorRGBA{0, 122, 255, 255},   // Blue
    ColorRGBA{255, 255, 255, 255}, // White
    ColorRGBA{28, 28, 30, 255}     // Black
}};

struct Annotation {
    uint64_t id = 0;
    ToolType type = ToolType::Pen;
    ColorRGBA color{};
    float strokeWidth = 4.0f;
    std::vector<PointF> points;
    PointF start{};
    PointF end{};
    RectF rect{};
    std::wstring text;
    float fontSize = 28.0f;
    CensorMode censorMode = CensorMode::Mosaic;
    float censorIntensity = 14.0f;
};

} // namespace saycheese::tools
