#include "BlendMode.h"

namespace velox {
namespace {

const char* idOf(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal: return "normal";
    case BlendMode::Multiply: return "multiply";
    case BlendMode::Screen: return "screen";
    case BlendMode::Overlay: return "overlay";
    case BlendMode::Darken: return "darken";
    case BlendMode::Lighten: return "lighten";
    case BlendMode::ColorDodge: return "color-dodge";
    case BlendMode::ColorBurn: return "color-burn";
    case BlendMode::HardLight: return "hard-light";
    case BlendMode::SoftLight: return "soft-light";
    case BlendMode::Difference: return "difference";
    case BlendMode::Exclusion: return "exclusion";
    }
    return "normal";
}

const char* nameOf(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal: return "Normal";
    case BlendMode::Multiply: return "Multiply";
    case BlendMode::Screen: return "Screen";
    case BlendMode::Overlay: return "Overlay";
    case BlendMode::Darken: return "Darken";
    case BlendMode::Lighten: return "Lighten";
    case BlendMode::ColorDodge: return "Color Dodge";
    case BlendMode::ColorBurn: return "Color Burn";
    case BlendMode::HardLight: return "Hard Light";
    case BlendMode::SoftLight: return "Soft Light";
    case BlendMode::Difference: return "Difference";
    case BlendMode::Exclusion: return "Exclusion";
    }
    return "Normal";
}

} // namespace

QString blendModeId(BlendMode mode)
{
    return QString::fromLatin1(idOf(mode));
}

QString blendModeDisplayName(BlendMode mode)
{
    return QString::fromLatin1(nameOf(mode));
}

std::optional<BlendMode> blendModeFromId(const QString& id)
{
    for (int i = 0; i < kBlendModeCount; ++i) {
        const BlendMode mode = static_cast<BlendMode>(i);
        if (id.compare(QString::fromLatin1(idOf(mode)), Qt::CaseInsensitive) == 0) {
            return mode;
        }
    }
    return std::nullopt;
}

std::optional<BlendMode> blendModeFromInt(int value)
{
    if (value < 0 || value >= kBlendModeCount) {
        return std::nullopt;
    }
    return static_cast<BlendMode>(value);
}

} // namespace velox
