#include "theme/ThemeHelper.h"

#include <QMenu>

namespace glassnote {

namespace {

QColor colorFromHsl(int hue, int saturation, int lightness, int alpha) {
    QColor color;
    color.setHsl(hue, saturation, lightness, alpha);
    return color;
}

int normalizedHue(int hue) {
    const int value = hue % 360;
    return value < 0 ? value + 360 : value;
}

int applyHueOffset(int hue, int offset) {
    return normalizedHue(hue + offset);
}

QColor neutralTopForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Meadow:
        return hovered ? QColor(214, 232, 218, 128) : QColor(198, 220, 204, 112);
    case UiStyle::Graphite:
        return hovered ? QColor(58, 66, 80, 196) : QColor(50, 58, 72, 182);
    case UiStyle::Paper:
        return hovered ? QColor(252, 248, 240, 218) : QColor(248, 243, 232, 206);
    case UiStyle::Pixel:
        return hovered ? QColor(12, 18, 12, 238) : QColor(10, 16, 10, 226);
    case UiStyle::Neon:
        return hovered ? QColor(46, 34, 82, 220) : QColor(40, 30, 74, 206);
    case UiStyle::Clay:
        return hovered ? QColor(246, 220, 198, 222) : QColor(238, 210, 186, 208);
    case UiStyle::Glass:
    default:
        return hovered ? QColor(232, 242, 255, 112) : QColor(222, 236, 250, 96);
    }
}

QColor neutralBottomForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Meadow:
        return hovered ? QColor(166, 194, 172, 112) : QColor(152, 180, 160, 96);
    case UiStyle::Graphite:
        return hovered ? QColor(24, 30, 44, 186) : QColor(20, 26, 40, 172);
    case UiStyle::Paper:
        return hovered ? QColor(236, 228, 212, 196) : QColor(232, 224, 206, 182);
    case UiStyle::Pixel:
        return hovered ? QColor(6, 12, 8, 232) : QColor(5, 10, 7, 220);
    case UiStyle::Neon:
        return hovered ? QColor(12, 8, 34, 200) : QColor(10, 6, 30, 186);
    case UiStyle::Clay:
        return hovered ? QColor(216, 180, 150, 194) : QColor(206, 168, 140, 178);
    case UiStyle::Glass:
    default:
        return hovered ? QColor(148, 174, 206, 84) : QColor(138, 162, 194, 72);
    }
}

QColor neutralBorderForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Meadow:
        return hovered ? QColor(170, 204, 180, 176) : QColor(156, 190, 166, 156);
    case UiStyle::Graphite:
        return hovered ? QColor(132, 146, 170, 198) : QColor(118, 132, 156, 178);
    case UiStyle::Paper:
        return hovered ? QColor(190, 172, 142, 196) : QColor(178, 162, 134, 178);
    case UiStyle::Pixel:
        return hovered ? QColor(126, 206, 126, 232) : QColor(110, 188, 110, 216);
    case UiStyle::Neon:
        return hovered ? QColor(194, 126, 255, 230) : QColor(172, 108, 244, 212);
    case UiStyle::Clay:
        return hovered ? QColor(178, 124, 88, 214) : QColor(164, 112, 78, 198);
    case UiStyle::Glass:
    default:
        return hovered ? QColor(194, 214, 238, 162) : QColor(178, 198, 224, 142);
    }
}

QColor neutralHighlightForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Meadow:
        return hovered ? QColor(224, 242, 228, 96) : QColor(210, 230, 216, 82);
    case UiStyle::Graphite:
        return hovered ? QColor(92, 106, 128, 138) : QColor(78, 92, 114, 120);
    case UiStyle::Paper:
        return hovered ? QColor(255, 248, 236, 148) : QColor(250, 242, 226, 132);
    case UiStyle::Pixel:
        return hovered ? QColor(116, 214, 116, 118) : QColor(98, 198, 98, 98);
    case UiStyle::Neon:
        return hovered ? QColor(230, 174, 255, 176) : QColor(202, 146, 255, 156);
    case UiStyle::Clay:
        return hovered ? QColor(252, 234, 212, 146) : QColor(244, 222, 198, 128);
    case UiStyle::Glass:
    default:
        return hovered ? QColor(255, 255, 255, 96) : QColor(246, 252, 255, 82);
    }
}

int hueOffsetForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Meadow:
        return 16;
    case UiStyle::Graphite:
        return -6;
    case UiStyle::Paper:
        return -14;
    case UiStyle::Pixel:
        return -6;
    case UiStyle::Neon:
        return 28;
    case UiStyle::Clay:
        return -8;
    case UiStyle::Glass:
    default:
        return 0;
    }
}

int hueSaturationForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Meadow:
        return hovered ? 98 : 88;
    case UiStyle::Graphite:
        return hovered ? 72 : 64;
    case UiStyle::Paper:
        return hovered ? 80 : 70;
    case UiStyle::Pixel:
        return hovered ? 136 : 122;
    case UiStyle::Neon:
        return hovered ? 186 : 170;
    case UiStyle::Clay:
        return hovered ? 84 : 74;
    case UiStyle::Glass:
    default:
        return hovered ? 98 : 88;
    }
}

int menuPanelRadiusForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Pixel:
        return 2;
    case UiStyle::Graphite:
        return 6;
    case UiStyle::Paper:
        return 8;
    case UiStyle::Neon:
        return 10;
    case UiStyle::Clay:
        return 14;
    case UiStyle::Meadow:
        return 13;
    case UiStyle::Glass:
    default:
        return 14;
    }
}

int menuItemRadiusForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Pixel:
        return 1;
    case UiStyle::Graphite:
        return 3;
    case UiStyle::Paper:
        return 4;
    case UiStyle::Neon:
        return 6;
    case UiStyle::Clay:
        return 10;
    case UiStyle::Meadow:
        return 9;
    case UiStyle::Glass:
    default:
        return 9;
    }
}

int menuIndicatorRadiusForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Pixel:
        return 1;
    case UiStyle::Graphite:
        return 2;
    case UiStyle::Paper:
        return 2;
    case UiStyle::Neon:
        return 3;
    case UiStyle::Clay:
        return 5;
    case UiStyle::Meadow:
        return 5;
    case UiStyle::Glass:
    default:
        return 6;
    }
}

int menuPaddingForStyle(UiStyle uiStyle) {
    return uiStyle == UiStyle::Pixel ? 6 : 8;
}

int menuItemVerticalPaddingForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Pixel:
        return 4;
    case UiStyle::Graphite:
        return 7;
    case UiStyle::Paper:
        return 6;
    case UiStyle::Neon:
        return 7;
    case UiStyle::Clay:
        return 8;
    case UiStyle::Meadow:
        return 8;
    case UiStyle::Glass:
    default:
        return 8;
    }
}

int menuItemHorizontalPaddingForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Pixel:
        return 8;
    case UiStyle::Graphite:
        return 12;
    case UiStyle::Paper:
        return 12;
    case UiStyle::Neon:
        return 13;
    case UiStyle::Clay:
        return 14;
    case UiStyle::Meadow:
        return 14;
    case UiStyle::Glass:
    default:
        return 14;
    }
}

QString menuFontDeclarationForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Pixel:
        return QStringLiteral("font-family: 'Consolas', 'Courier New', monospace; letter-spacing: 0.4px;");
    case UiStyle::Paper:
        return QStringLiteral("font-family: 'Georgia', 'Times New Roman', serif; letter-spacing: 0.3px;");
    case UiStyle::Graphite:
        return QStringLiteral("font-family: 'Bahnschrift SemiCondensed', 'Segoe UI', sans-serif; font-weight: 600; letter-spacing: 0.3px;");
    case UiStyle::Neon:
        return QStringLiteral("font-family: 'Consolas', 'Courier New', monospace; letter-spacing: 0.6px;");
    case UiStyle::Clay:
        return QStringLiteral("font-family: 'Palatino Linotype', 'Georgia', serif;");
    case UiStyle::Meadow:
        return QStringLiteral("font-family: 'Candara', 'Segoe UI', sans-serif;");
    case UiStyle::Glass:
    default:
        return QStringLiteral("font-family: 'Segoe UI', sans-serif;");
    }
}

QString menuBorderStyleFor(UiStyle uiStyle) {
    return (uiStyle == UiStyle::Pixel || uiStyle == UiStyle::Graphite)
               ? QStringLiteral("solid")
               : QStringLiteral("solid");
}

QColor checkedItemBackgroundFor(UiStyle uiStyle, const QColor &selection) {
    switch (uiStyle) {
    case UiStyle::Neon:
        return selection.lighter(118);
    case UiStyle::Graphite:
        return selection.darker(106);
    case UiStyle::Paper:
        return selection.lighter(102);
    case UiStyle::Clay:
        return selection.lighter(108);
    case UiStyle::Pixel:
        return selection.lighter(104);
    case UiStyle::Glass:
    case UiStyle::Meadow:
    default:
        return selection.lighter(106);
    }
}

QColor scrollTrackForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Graphite:
        return QColor(12, 18, 26, 176);
    case UiStyle::Paper:
        return QColor(144, 118, 84, 56);
    case UiStyle::Neon:
        return QColor(8, 6, 24, 196);
    case UiStyle::Clay:
        return QColor(126, 84, 60, 88);
    case UiStyle::Meadow:
        return QColor(26, 54, 36, 112);
    case UiStyle::Glass:
    default:
        return QColor(0, 0, 0, 0);
    }
}

QColor scrollTrackBorderForStyle(UiStyle uiStyle, const WindowPalette &palette) {
    switch (uiStyle) {
    case UiStyle::Graphite:
        return palette.border.lighter(105);
    case UiStyle::Paper:
        return palette.border.darker(108);
    case UiStyle::Neon:
        return palette.border.lighter(114);
    case UiStyle::Clay:
        return palette.border.darker(112);
    case UiStyle::Meadow:
        return palette.border.darker(105);
    case UiStyle::Glass:
    default:
        return QColor(0, 0, 0, 0);
    }
}

WindowPalette windowPaletteForStyle(UiStyle uiStyle) {
    WindowPalette palette;
    switch (uiStyle) {
    case UiStyle::Meadow:
        palette.fillTop = QColor(26, 44, 34, 224);
        palette.fillMiddle = QColor(20, 36, 28, 214);
        palette.fillBottom = QColor(14, 28, 22, 206);
        palette.border = QColor(134, 176, 148, 120);
        palette.edgeFadeBase = QColor(10, 20, 14, 255);
        palette.scrollHandle = QColor(138, 184, 154, 166);
        break;
    case UiStyle::Graphite:
        palette.fillTop = QColor(34, 40, 52, 236);
        palette.fillMiddle = QColor(24, 30, 40, 228);
        palette.fillBottom = QColor(16, 22, 32, 220);
        palette.border = QColor(120, 134, 160, 144);
        palette.edgeFadeBase = QColor(8, 12, 18, 255);
        palette.scrollHandle = QColor(130, 146, 174, 176);
        break;
    case UiStyle::Paper:
        palette.fillTop = QColor(248, 242, 230, 224);
        palette.fillMiddle = QColor(240, 232, 214, 216);
        palette.fillBottom = QColor(228, 218, 196, 206);
        palette.border = QColor(170, 152, 124, 162);
        palette.edgeFadeBase = QColor(88, 72, 48, 255);
        palette.scrollHandle = QColor(158, 134, 98, 178);
        break;
    case UiStyle::Pixel:
        palette.fillTop = QColor(12, 16, 12, 238);
        palette.fillMiddle = QColor(8, 12, 9, 232);
        palette.fillBottom = QColor(5, 8, 6, 226);
        palette.border = QColor(108, 186, 108, 214);
        palette.edgeFadeBase = QColor(4, 8, 6, 255);
        palette.scrollHandle = QColor(126, 198, 126, 196);
        break;
    case UiStyle::Neon:
        palette.fillTop = QColor(34, 20, 64, 236);
        palette.fillMiddle = QColor(24, 14, 50, 226);
        palette.fillBottom = QColor(14, 10, 36, 218);
        palette.border = QColor(186, 104, 255, 206);
        palette.edgeFadeBase = QColor(8, 8, 18, 255);
        palette.scrollHandle = QColor(190, 120, 255, 216);
        break;
    case UiStyle::Clay:
        palette.fillTop = QColor(238, 210, 184, 220);
        palette.fillMiddle = QColor(224, 188, 156, 210);
        palette.fillBottom = QColor(206, 166, 134, 202);
        palette.border = QColor(154, 104, 74, 194);
        palette.edgeFadeBase = QColor(62, 38, 28, 255);
        palette.scrollHandle = QColor(168, 114, 80, 202);
        break;
    case UiStyle::Glass:
    default:
        palette.fillTop = QColor(24, 32, 44, 132);
        palette.fillMiddle = QColor(18, 26, 38, 118);
        palette.fillBottom = QColor(12, 20, 32, 108);
        palette.border = QColor(194, 216, 242, 92);
        palette.edgeFadeBase = QColor(12, 18, 28, 255);
        palette.scrollHandle = QColor(172, 198, 226, 154);
        break;
    }

    return palette;
}

}  // namespace

NotePalette ThemeHelper::paletteFor(UiStyle uiStyle, int hue, bool hovered) {
    uiStyle = normalizedUiStyle(uiStyle);
    const bool hasHue = hue >= 0;
    const int fallbackHue = uiStyle == UiStyle::Meadow ? 138
                            : uiStyle == UiStyle::Paper ? 42
                            : uiStyle == UiStyle::Pixel ? 132
                            : uiStyle == UiStyle::Neon ? 286
                            : uiStyle == UiStyle::Clay ? 24
                            : 206;
    const int baseHue = hasHue
                            ? applyHueOffset(normalizedHue(hue), hueOffsetForStyle(uiStyle))
                            : fallbackHue;

    NotePalette palette;
    palette.shadow = hovered ? QColor(0, 0, 0, 48) : QColor(0, 0, 0, 34);
    if (uiStyle == UiStyle::Meadow) {
        palette.shadow = hovered ? QColor(10, 34, 18, 56) : QColor(8, 28, 16, 46);
    } else if (uiStyle == UiStyle::Glass) {
        palette.shadow = hovered ? QColor(10, 18, 28, 62) : QColor(8, 14, 24, 50);
    } else if (uiStyle == UiStyle::Paper) {
        palette.shadow = hovered ? QColor(68, 56, 34, 32) : QColor(66, 52, 30, 24);
    } else if (uiStyle == UiStyle::Graphite) {
        palette.shadow = hovered ? QColor(0, 0, 0, 88) : QColor(0, 0, 0, 74);
    } else if (uiStyle == UiStyle::Neon) {
        palette.shadow = hovered ? QColor(0, 0, 0, 92) : QColor(0, 0, 0, 78);
    } else if (uiStyle == UiStyle::Pixel) {
        palette.shadow = hovered ? QColor(20, 80, 20, 52) : QColor(14, 62, 14, 38);
    } else if (uiStyle == UiStyle::Clay) {
        palette.shadow = hovered ? QColor(86, 58, 42, 52) : QColor(76, 52, 36, 42);
    }

    if (!hasHue) {
        palette.fillTop = neutralTopForStyle(uiStyle, hovered);
        palette.fillBottom = neutralBottomForStyle(uiStyle, hovered);
        palette.border = neutralBorderForStyle(uiStyle, hovered);
        palette.highlightTop = neutralHighlightForStyle(uiStyle, hovered);
        if (uiStyle == UiStyle::Glass) {
            palette.text = QColor(236, 246, 255, 236);
            palette.placeholder = QColor(176, 198, 224, 172);
        } else if (uiStyle == UiStyle::Meadow) {
            palette.text = QColor(238, 248, 240, 236);
            palette.placeholder = QColor(186, 206, 190, 172);
        } else if (uiStyle == UiStyle::Paper) {
            palette.text = QColor(56, 44, 30, 232);
            palette.placeholder = QColor(86, 72, 54, 156);
        } else if (uiStyle == UiStyle::Graphite) {
            palette.text = QColor(242, 245, 250, 236);
            palette.placeholder = QColor(214, 220, 234, 142);
        } else if (uiStyle == UiStyle::Pixel) {
            palette.text = QColor(192, 255, 192, 244);
            palette.placeholder = QColor(132, 188, 132, 188);
        } else if (uiStyle == UiStyle::Neon) {
            palette.text = QColor(248, 238, 255, 244);
            palette.placeholder = QColor(214, 182, 252, 178);
        } else if (uiStyle == UiStyle::Clay) {
            palette.text = QColor(72, 46, 30, 238);
            palette.placeholder = QColor(112, 80, 56, 172);
        } else {
            palette.text = QColor(255, 255, 255, 235);
            palette.placeholder = QColor(255, 255, 255, 128);
        }
        return palette;
    }

    const int saturation = hueSaturationForStyle(uiStyle, hovered);
    const int topLight = hovered ? 224 : 216;
    const int bottomLight = hovered ? 204 : 196;
    const int borderLight = hovered ? 232 : 224;
    const int highlightLight = hovered ? 242 : 234;

    if (uiStyle == UiStyle::Meadow) {
        palette.fillTop = colorFromHsl(baseHue, qMin(255, saturation), hovered ? 198 : 190, hovered ? 126 : 110);
        palette.fillBottom = colorFromHsl(baseHue, qMax(18, saturation - 24), hovered ? 176 : 168, hovered ? 102 : 88);
        palette.border = colorFromHsl(baseHue, qMin(255, saturation + 18), hovered ? 214 : 206, hovered ? 176 : 158);
        palette.highlightTop = colorFromHsl(baseHue, qMin(255, saturation + 24), hovered ? 224 : 214, hovered ? 112 : 94);
        palette.text = QColor(236, 246, 238, 236);
        palette.placeholder = colorFromHsl(baseHue, qMax(18, saturation - 30), 178, 176);
        return palette;
    }

    if (uiStyle == UiStyle::Pixel) {
        palette.fillTop = colorFromHsl(baseHue, qMin(255, saturation), hovered ? 24 : 20, hovered ? 236 : 224);
        palette.fillBottom = colorFromHsl(baseHue, qMax(24, saturation - 24), hovered ? 14 : 10, hovered ? 228 : 216);
        palette.border = colorFromHsl(baseHue, qMin(255, saturation + 24), hovered ? 170 : 156, hovered ? 232 : 216);
        palette.highlightTop = colorFromHsl(baseHue, qMin(255, saturation + 32), hovered ? 128 : 114, hovered ? 118 : 96);
        palette.text = QColor(192, 255, 192, 244);
        palette.placeholder = colorFromHsl(baseHue, qMax(24, saturation - 36), 156, 188);
        return palette;
    }

    palette.fillTop = colorFromHsl(baseHue, saturation, topLight, hovered ? 108 : 86);
    palette.fillBottom = colorFromHsl(baseHue, qMax(16, saturation - 20), bottomLight, hovered ? 66 : 50);
    palette.border = colorFromHsl(baseHue, qMin(255, saturation + 12), borderLight, hovered ? 136 : 106);
    palette.highlightTop = colorFromHsl(baseHue, qMin(255, saturation + 18), highlightLight, hovered ? 78 : 60);
    if (uiStyle == UiStyle::Paper) {
        palette.text = QColor(50, 38, 26, 236);
        palette.placeholder = colorFromHsl(baseHue, 42, 130, 166);
    } else if (uiStyle == UiStyle::Clay) {
        palette.text = QColor(66, 42, 28, 238);
        palette.placeholder = colorFromHsl(baseHue, qMax(24, saturation - 32), 132, 172);
    } else if (uiStyle == UiStyle::Glass) {
        palette.text = QColor(238, 246, 255, 238);
        palette.placeholder = colorFromHsl(baseHue, qMax(20, saturation - 30), 206, 170);
    } else {
        palette.text = QColor(255, 255, 255, 238);
        palette.placeholder = colorFromHsl(baseHue, qMax(24, saturation - 26), 236, 140);
    }
    return palette;
}

WindowPalette ThemeHelper::windowPalette(UiStyle uiStyle) {
    uiStyle = normalizedUiStyle(uiStyle);
    return windowPaletteForStyle(uiStyle);
}

QString ThemeHelper::scrollAreaStyleSheet(UiStyle uiStyle, int scrollBarWidth) {
    uiStyle = normalizedUiStyle(uiStyle);
    const WindowPalette palette = windowPaletteForStyle(uiStyle);
    if (uiStyle == UiStyle::Pixel) {
        const QColor track = QColor(8, 14, 10, 196);
        const QColor handle = palette.scrollHandle;
        const QColor handleBorder = palette.border;
        const QColor handleHover = palette.scrollHandle.lighter(114);
        return QStringLiteral(
                   "QScrollArea { background: transparent; border: none; }"
                   "QScrollArea > QWidget > QWidget { background: transparent; }"
                   "QScrollBar:vertical { background: %1; width: %2px; margin: 4px 2px 4px 0px; border: 1px solid %3; }"
                   "QScrollBar::handle:vertical { background: %4; border: 1px solid %5; border-radius: 1px; min-height: 24px; }"
                   "QScrollBar::handle:vertical:hover { background: %6; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
                   "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; height: 0px; }")
            .arg(track.name(QColor::HexArgb))
            .arg(scrollBarWidth)
            .arg(palette.border.darker(130).name(QColor::HexArgb))
            .arg(handle.name(QColor::HexArgb))
            .arg(handleBorder.name(QColor::HexArgb))
            .arg(handleHover.name(QColor::HexArgb));
    }

    const QColor track = scrollTrackForStyle(uiStyle);
    const QColor trackBorder = scrollTrackBorderForStyle(uiStyle, palette);
    const QColor handle = palette.scrollHandle;
    const QColor handleBorder = palette.border.darker(118);
    const QColor handleHover = palette.scrollHandle.lighter(112);
    const QColor handlePressed = palette.scrollHandle.darker(110);
    int handleRadius = scrollBarWidth > 8 ? (scrollBarWidth / 2) : 4;
    if (uiStyle == UiStyle::Graphite) {
        handleRadius = 3;
    } else if (uiStyle == UiStyle::Paper) {
        handleRadius = qMax(4, scrollBarWidth / 2);
    } else if (uiStyle == UiStyle::Neon) {
        handleRadius = qMax(5, scrollBarWidth / 2);
    } else if (uiStyle == UiStyle::Clay) {
        handleRadius = qMax(6, scrollBarWidth / 2);
    }
    const int trackRadius = qMax(2, handleRadius - 1);
    return QStringLiteral(
               "QScrollArea { background: transparent; border: none; }"
               "QScrollArea > QWidget > QWidget { background: transparent; }"
               "QScrollBar:vertical { background: %7; width: %1px; margin: 6px 2px 6px 0px; border: 1px solid %8; border-radius: %9px; }"
               "QScrollBar::handle:vertical { background: %2; border: 1px solid %3; border-radius: %4px; min-height: 28px; }"
               "QScrollBar::handle:vertical:hover { background: %5; }"
               "QScrollBar::handle:vertical:pressed { background: %6; }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; height: 0px; }")
        .arg(scrollBarWidth)
        .arg(handle.name(QColor::HexArgb))
        .arg(handleBorder.name(QColor::HexArgb))
        .arg(handleRadius)
        .arg(handleHover.name(QColor::HexArgb))
        .arg(handlePressed.name(QColor::HexArgb))
        .arg(track.name(QColor::HexArgb))
        .arg(trackBorder.name(QColor::HexArgb))
        .arg(trackRadius);
}

void ThemeHelper::polishMenu(QMenu *menu, UiStyle uiStyle, int hue) {
    if (menu == nullptr) {
        return;
    }

    uiStyle = normalizedUiStyle(uiStyle);

    const NotePalette palette = paletteFor(uiStyle, hue, false);
    const WindowPalette window = windowPaletteForStyle(uiStyle);
    const QColor panelTop = hue >= 0 ? palette.fillTop : window.fillTop.darker(115);
    const QColor panelBottom = hue >= 0 ? palette.fillBottom : window.fillBottom.darker(120);
    const QColor border = hue >= 0 ? palette.border : window.border;
    const QColor selection = hue >= 0 ? palette.highlightTop : window.fillMiddle.lighter(125);
    const QColor selectionBorder = hue >= 0 ? palette.border : window.border.lighter(120);
    const QColor separator = hue >= 0 ? palette.border.darker(130) : window.border.darker(140);
    const QColor disabledText = hue >= 0 ? palette.placeholder : palette.placeholder;
    const int panelRadius = menuPanelRadiusForStyle(uiStyle);
    const int itemRadius = menuItemRadiusForStyle(uiStyle);
    const int indicatorRadius = menuIndicatorRadiusForStyle(uiStyle);
    const int menuPadding = menuPaddingForStyle(uiStyle);
    const int itemVerticalPadding = menuItemVerticalPaddingForStyle(uiStyle);
    const int itemHorizontalPadding = menuItemHorizontalPaddingForStyle(uiStyle);
    const QString menuFont = menuFontDeclarationForStyle(uiStyle);
    const QString borderStyle = menuBorderStyleFor(uiStyle);
    const QColor checkedItemBackground = checkedItemBackgroundFor(uiStyle, selection);

    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    menu->setStyleSheet(QStringLiteral(
                            "QMenu {"
                            "background-color: transparent;"
                            "%13"
                            "padding: %14px;"
                            "border: 1px %17 %1;"
                            "border-radius: %9px;"
                            "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                            "stop:0 %2, stop:1 %3);"
                            "}"
                            "QMenu::item {"
                            "padding: %15px %16px;"
                            "margin: 2px 0px;"
                            "border-radius: %10px;"
                            "color: %4;"
                           "background: transparent;"
                           "}"
                           "QMenu::item:selected {"
                           "background: %5;"
                           "border: 1px solid %6;"
                            "}"
                            "QMenu::item:checked {"
                            "background: %11;"
                            "border: 1px solid %6;"
                           "}"
                           "QMenu::item:disabled {"
                           "color: %7;"
                           "background: transparent;"
                           "}"
                           "QMenu::separator {"
                           "height: 1px;"
                           "margin: 8px 8px;"
                           "background: %8;"
                           "}"
                           "QMenu::right-arrow {"
                           "width: 10px;"
                           "height: 10px;"
                           "margin-right: 6px;"
                           "image: none;"
                           "border-top: 2px solid %4;"
                           "border-right: 2px solid %4;"
                           "}"
                           "QMenu::indicator { width: 12px; height: 12px; }"
                           "QMenu::indicator:checked {"
                           "background: %6;"
                           "border: 1px solid %4;"
                           "border-radius: %12px;"
                           "}"
                           "QMenu::indicator:unchecked {"
                           "border: 1px solid %7;"
                           "border-radius: %12px;"
                           "background: transparent;"
                           "}")
                           .arg(border.name(QColor::HexArgb))
                           .arg(panelTop.name(QColor::HexArgb))
                           .arg(panelBottom.name(QColor::HexArgb))
                           .arg(palette.text.name(QColor::HexArgb))
                           .arg(selection.name(QColor::HexArgb))
                           .arg(selectionBorder.name(QColor::HexArgb))
                           .arg(disabledText.name(QColor::HexArgb))
                            .arg(separator.name(QColor::HexArgb))
                            .arg(panelRadius)
                            .arg(itemRadius)
                            .arg(checkedItemBackground.name(QColor::HexArgb))
                            .arg(indicatorRadius)
                            .arg(menuFont)
                            .arg(menuPadding)
                            .arg(itemVerticalPadding)
                            .arg(itemHorizontalPadding)
                            .arg(borderStyle));
}

}  // namespace glassnote
