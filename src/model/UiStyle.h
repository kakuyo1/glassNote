#pragma once

namespace glassnote {

enum class UiStyle {
    Glass = 0,
    Mist = 1,
    Sunrise = 2,
    Meadow = 3,
    Graphite = 4,
    Paper = 5,
    Pixel = 6,
    Neon = 7,
    Clay = 8,
};

inline constexpr UiStyle normalizedUiStyle(UiStyle style) {
    switch (style) {
    case UiStyle::Mist:
        return UiStyle::Glass;
    case UiStyle::Sunrise:
        return UiStyle::Clay;
    default:
        return style;
    }
}

}  // namespace glassnote
