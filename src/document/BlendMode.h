#pragma once

#include <QString>
#include <optional>

namespace velox {

// Layer blend modes. The numeric values are part of the .vlx file format, so
// never reorder or renumber existing entries; only append.
enum class BlendMode : int {
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    Overlay = 3,
    Darken = 4,
    Lighten = 5,
    ColorDodge = 6,
    ColorBurn = 7,
    HardLight = 8,
    SoftLight = 9,
    Difference = 10,
    Exclusion = 11,
};

constexpr int kBlendModeCount = 12;

// Stable identifier used in files (for example "multiply").
QString blendModeId(BlendMode mode);

// Human readable name for menus (for example "Color Dodge").
QString blendModeDisplayName(BlendMode mode);

std::optional<BlendMode> blendModeFromId(const QString& id);
std::optional<BlendMode> blendModeFromInt(int value);

} // namespace velox
