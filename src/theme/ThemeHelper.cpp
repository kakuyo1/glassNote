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
    case UiStyle::Mist:
        return hovered ? QColor(240, 247, 255, 88) : QColor(236, 245, 255, 72);
    case UiStyle::Sunrise:
        return hovered ? QColor(255, 242, 232, 98) : QColor(255, 239, 226, 82);
    case UiStyle::Meadow:
        return hovered ? QColor(234, 248, 238, 92) : QColor(228, 245, 234, 76);
    case UiStyle::Graphite:
        return hovered ? QColor(70, 76, 89, 150) : QColor(58, 64, 76, 132);
    case UiStyle::Paper:
        return hovered ? QColor(248, 244, 234, 176) : QColor(246, 242, 232, 160);
    case UiStyle::Pixel:
        return hovered ? QColor(232, 242, 255, 214) : QColor(226, 238, 252, 196);
    case UiStyle::Neon:
        return hovered ? QColor(54, 42, 88, 190) : QColor(46, 36, 80, 172);
    case UiStyle::Clay:
        return hovered ? QColor(244, 220, 198, 204) : QColor(236, 212, 188, 188);
    case UiStyle::Glass:
    default:
        return hovered ? QColor(255, 255, 255, 76) : QColor(255, 255, 255, 60);
    }
}

QColor neutralBottomForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Mist:
        return hovered ? QColor(220, 234, 248, 66) : QColor(214, 228, 244, 50);
    case UiStyle::Sunrise:
        return hovered ? QColor(255, 220, 196, 78) : QColor(255, 214, 186, 62);
    case UiStyle::Meadow:
        return hovered ? QColor(204, 234, 214, 72) : QColor(196, 228, 208, 56);
    case UiStyle::Graphite:
        return hovered ? QColor(36, 42, 55, 138) : QColor(28, 34, 46, 122);
    case UiStyle::Paper:
        return hovered ? QColor(240, 232, 214, 156) : QColor(236, 228, 208, 144);
    case UiStyle::Pixel:
        return hovered ? QColor(198, 214, 236, 186) : QColor(188, 206, 230, 170);
    case UiStyle::Neon:
        return hovered ? QColor(18, 14, 42, 166) : QColor(14, 10, 34, 148);
    case UiStyle::Clay:
        return hovered ? QColor(222, 184, 154, 176) : QColor(212, 174, 146, 160);
    case UiStyle::Glass:
    default:
        return hovered ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 30);
    }
}

QColor neutralBorderForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Mist:
        return hovered ? QColor(235, 245, 255, 126) : QColor(224, 236, 250, 98);
    case UiStyle::Sunrise:
        return hovered ? QColor(255, 229, 205, 140) : QColor(252, 218, 192, 112);
    case UiStyle::Meadow:
        return hovered ? QColor(212, 241, 222, 136) : QColor(198, 232, 210, 108);
    case UiStyle::Graphite:
        return hovered ? QColor(146, 156, 182, 152) : QColor(128, 138, 164, 130);
    case UiStyle::Paper:
        return hovered ? QColor(198, 178, 146, 160) : QColor(186, 168, 136, 144);
    case UiStyle::Pixel:
        return hovered ? QColor(126, 152, 194, 228) : QColor(114, 140, 184, 204);
    case UiStyle::Neon:
        return hovered ? QColor(206, 108, 255, 208) : QColor(172, 94, 246, 182);
    case UiStyle::Clay:
        return hovered ? QColor(176, 122, 86, 198) : QColor(164, 112, 78, 176);
    case UiStyle::Glass:
    default:
        return hovered ? QColor(255, 255, 255, 104) : QColor(255, 255, 255, 74);
    }
}

QColor neutralHighlightForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Mist:
        return hovered ? QColor(255, 255, 255, 70) : QColor(245, 252, 255, 54);
    case UiStyle::Sunrise:
        return hovered ? QColor(255, 244, 226, 78) : QColor(255, 236, 212, 62);
    case UiStyle::Meadow:
        return hovered ? QColor(236, 250, 240, 74) : QColor(224, 244, 230, 58);
    case UiStyle::Graphite:
        return hovered ? QColor(96, 106, 126, 104) : QColor(84, 94, 114, 86);
    case UiStyle::Paper:
        return hovered ? QColor(255, 249, 238, 122) : QColor(252, 244, 228, 102);
    case UiStyle::Pixel:
        return hovered ? QColor(255, 255, 255, 108) : QColor(238, 246, 255, 88);
    case UiStyle::Neon:
        return hovered ? QColor(236, 168, 255, 156) : QColor(198, 126, 255, 132);
    case UiStyle::Clay:
        return hovered ? QColor(252, 230, 208, 124) : QColor(246, 220, 196, 104);
    case UiStyle::Glass:
    default:
        return hovered ? QColor(255, 255, 255, 58) : QColor(255, 255, 255, 42);
    }
}

int hueOffsetForStyle(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Mist:
        return 8;
    case UiStyle::Sunrise:
        return -10;
    case UiStyle::Meadow:
        return 20;
    case UiStyle::Graphite:
        return 0;
    case UiStyle::Paper:
        return -14;
    case UiStyle::Pixel:
        return 0;
    case UiStyle::Neon:
        return 22;
    case UiStyle::Clay:
        return -8;
    case UiStyle::Glass:
    default:
        return 0;
    }
}

int hueSaturationForStyle(UiStyle uiStyle, bool hovered) {
    switch (uiStyle) {
    case UiStyle::Mist:
        return hovered ? 84 : 76;
    case UiStyle::Sunrise:
        return hovered ? 118 : 104;
    case UiStyle::Meadow:
        return hovered ? 110 : 96;
    case UiStyle::Graphite:
        return hovered ? 72 : 64;
    case UiStyle::Paper:
        return hovered ? 98 : 84;
    case UiStyle::Pixel:
        return hovered ? 90 : 80;
    case UiStyle::Neon:
        return hovered ? 164 : 144;
    case UiStyle::Clay:
        return hovered ? 88 : 74;
    case UiStyle::Glass:
    default:
        return hovered ? 104 : 96;
    }
}

WindowPalette windowPaletteForStyle(UiStyle uiStyle) {
    WindowPalette palette;
    switch (uiStyle) {
    case UiStyle::Mist:
        palette.fillTop = QColor(230, 241, 255, 18);
        palette.fillMiddle = QColor(215, 230, 246, 11);
        palette.fillBottom = QColor(204, 220, 238, 6);
        palette.border = QColor(230, 240, 255, 44);
        palette.edgeFadeBase = QColor(25, 42, 62, 255);
        palette.scrollHandle = QColor(220, 234, 248, 110);
        break;
    case UiStyle::Sunrise:
        palette.fillTop = QColor(255, 236, 214, 62);
        palette.fillMiddle = QColor(255, 226, 198, 48);
        palette.fillBottom = QColor(255, 214, 182, 36);
        palette.border = QColor(255, 229, 202, 86);
        palette.edgeFadeBase = QColor(58, 32, 20, 255);
        palette.scrollHandle = QColor(255, 224, 190, 120);
        break;
    case UiStyle::Meadow:
        palette.fillTop = QColor(224, 242, 228, 56);
        palette.fillMiddle = QColor(210, 234, 216, 42);
        palette.fillBottom = QColor(192, 222, 202, 30);
        palette.border = QColor(218, 238, 224, 82);
        palette.edgeFadeBase = QColor(23, 48, 34, 255);
        palette.scrollHandle = QColor(198, 226, 206, 116);
        break;
    case UiStyle::Graphite:
        palette.fillTop = QColor(74, 82, 97, 54);
        palette.fillMiddle = QColor(56, 64, 79, 44);
        palette.fillBottom = QColor(36, 42, 56, 36);
        palette.border = QColor(148, 158, 182, 88);
        palette.edgeFadeBase = QColor(10, 13, 20, 255);
        palette.scrollHandle = QColor(150, 164, 186, 126);
        break;
    case UiStyle::Paper:
        palette.fillTop = QColor(248, 242, 228, 82);
        palette.fillMiddle = QColor(242, 234, 216, 68);
        palette.fillBottom = QColor(234, 224, 204, 58);
        palette.border = QColor(188, 172, 146, 122);
        palette.edgeFadeBase = QColor(88, 74, 52, 255);
        palette.scrollHandle = QColor(170, 150, 118, 130);
        break;
    case UiStyle::Pixel:
        palette.fillTop = QColor(224, 236, 252, 122);
        palette.fillMiddle = QColor(210, 224, 244, 104);
        palette.fillBottom = QColor(192, 210, 234, 90);
        palette.border = QColor(116, 140, 180, 188);
        palette.edgeFadeBase = QColor(22, 38, 66, 255);
        palette.scrollHandle = QColor(112, 142, 198, 190);
        break;
    case UiStyle::Neon:
        palette.fillTop = QColor(42, 30, 72, 126);
        palette.fillMiddle = QColor(26, 18, 54, 112);
        palette.fillBottom = QColor(14, 10, 36, 104);
        palette.border = QColor(188, 96, 255, 176);
        palette.edgeFadeBase = QColor(6, 6, 16, 255);
        palette.scrollHandle = QColor(184, 108, 255, 188);
        break;
    case UiStyle::Clay:
        palette.fillTop = QColor(242, 218, 196, 122);
        palette.fillMiddle = QColor(230, 198, 168, 106);
        palette.fillBottom = QColor(214, 178, 146, 94);
        palette.border = QColor(162, 110, 76, 170);
        palette.edgeFadeBase = QColor(70, 42, 30, 255);
        palette.scrollHandle = QColor(170, 118, 84, 176);
        break;
    case UiStyle::Glass:
    default:
        palette.fillTop = QColor(255, 255, 255, 10);
        palette.fillMiddle = QColor(255, 255, 255, 5);
        palette.fillBottom = QColor(255, 255, 255, 2);
        palette.border = QColor(255, 255, 255, 28);
        palette.edgeFadeBase = QColor(15, 18, 24, 255);
        palette.scrollHandle = QColor(255, 255, 255, 70);
        break;
    }

    return palette;
}

}  // namespace

NotePalette ThemeHelper::paletteFor(UiStyle uiStyle, int hue, bool hovered) {
    const bool hasHue = hue >= 0;
    const int fallbackHue = uiStyle == UiStyle::Sunrise ? 26
                            : uiStyle == UiStyle::Meadow ? 134
                            : uiStyle == UiStyle::Paper ? 38
                            : uiStyle == UiStyle::Pixel ? 212
                            : uiStyle == UiStyle::Neon ? 292
                            : uiStyle == UiStyle::Clay ? 24
                            : 210;
    const int baseHue = hasHue
                            ? applyHueOffset(normalizedHue(hue), hueOffsetForStyle(uiStyle))
                            : fallbackHue;

    NotePalette palette;
    palette.shadow = hovered ? QColor(0, 0, 0, 48) : QColor(0, 0, 0, 34);
    if (uiStyle == UiStyle::Paper) {
        palette.shadow = hovered ? QColor(68, 56, 34, 32) : QColor(66, 52, 30, 24);
    } else if (uiStyle == UiStyle::Graphite) {
        palette.shadow = hovered ? QColor(0, 0, 0, 76) : QColor(0, 0, 0, 62);
    } else if (uiStyle == UiStyle::Neon) {
        palette.shadow = hovered ? QColor(0, 0, 0, 86) : QColor(0, 0, 0, 70);
    } else if (uiStyle == UiStyle::Pixel) {
        palette.shadow = hovered ? QColor(40, 58, 90, 52) : QColor(34, 50, 82, 42);
    } else if (uiStyle == UiStyle::Clay) {
        palette.shadow = hovered ? QColor(82, 56, 42, 46) : QColor(74, 50, 36, 36);
    }

    if (!hasHue) {
        palette.fillTop = neutralTopForStyle(uiStyle, hovered);
        palette.fillBottom = neutralBottomForStyle(uiStyle, hovered);
        palette.border = neutralBorderForStyle(uiStyle, hovered);
        palette.highlightTop = neutralHighlightForStyle(uiStyle, hovered);
        if (uiStyle == UiStyle::Paper) {
            palette.text = QColor(56, 44, 30, 232);
            palette.placeholder = QColor(86, 72, 54, 156);
        } else if (uiStyle == UiStyle::Graphite) {
            palette.text = QColor(242, 245, 250, 236);
            palette.placeholder = QColor(214, 220, 234, 142);
        } else if (uiStyle == UiStyle::Pixel) {
            palette.text = QColor(24, 38, 62, 244);
            palette.placeholder = QColor(58, 82, 118, 174);
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

    palette.fillTop = colorFromHsl(baseHue, saturation, topLight, hovered ? 108 : 86);
    palette.fillBottom = colorFromHsl(baseHue, qMax(16, saturation - 20), bottomLight, hovered ? 66 : 50);
    palette.border = colorFromHsl(baseHue, qMin(255, saturation + 12), borderLight, hovered ? 136 : 106);
    palette.highlightTop = colorFromHsl(baseHue, qMin(255, saturation + 18), highlightLight, hovered ? 78 : 60);
    if (uiStyle == UiStyle::Paper) {
        palette.text = QColor(50, 38, 26, 236);
        palette.placeholder = colorFromHsl(baseHue, 42, 130, 166);
    } else if (uiStyle == UiStyle::Pixel) {
        palette.text = QColor(18, 34, 56, 244);
        palette.placeholder = colorFromHsl(baseHue, qMax(20, saturation - 30), 118, 184);
    } else if (uiStyle == UiStyle::Clay) {
        palette.text = QColor(66, 42, 28, 238);
        palette.placeholder = colorFromHsl(baseHue, qMax(24, saturation - 32), 132, 172);
    } else {
        palette.text = QColor(255, 255, 255, 238);
        palette.placeholder = colorFromHsl(baseHue, qMax(24, saturation - 26), 236, 140);
    }
    return palette;
}

WindowPalette ThemeHelper::windowPalette(UiStyle uiStyle) {
    return windowPaletteForStyle(uiStyle);
}

QString ThemeHelper::scrollAreaStyleSheet(UiStyle uiStyle, int scrollBarWidth) {
    const WindowPalette palette = windowPaletteForStyle(uiStyle);
    return QStringLiteral(
               "QScrollArea { background: transparent; border: none; }"
               "QScrollArea > QWidget > QWidget { background: transparent; }"
               "QScrollBar:vertical { background: transparent; width: %1px; margin: 4px 0 4px 0; }"
               "QScrollBar::handle:vertical { background: %2; border-radius: 4px; min-height: 24px; }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; height: 0px; }")
        .arg(scrollBarWidth)
        .arg(palette.scrollHandle.name(QColor::HexArgb));
}

void ThemeHelper::polishMenu(QMenu *menu, UiStyle uiStyle, int hue) {
    if (menu == nullptr) {
        return;
    }

    const NotePalette palette = paletteFor(uiStyle, hue, false);
    const WindowPalette window = windowPaletteForStyle(uiStyle);
    const QColor panelTop = hue >= 0 ? palette.fillTop : window.fillTop.darker(115);
    const QColor panelBottom = hue >= 0 ? palette.fillBottom : window.fillBottom.darker(120);
    const QColor border = hue >= 0 ? palette.border : window.border;
    const QColor selection = hue >= 0 ? palette.highlightTop : window.fillMiddle.lighter(125);
    const QColor selectionBorder = hue >= 0 ? palette.border : window.border.lighter(120);
    const QColor separator = hue >= 0 ? palette.border.darker(130) : window.border.darker(140);
    const QColor disabledText = hue >= 0 ? palette.placeholder : palette.placeholder;

    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    menu->setStyleSheet(QStringLiteral(
                           "QMenu {"
                           "background-color: transparent;"
                           "padding: 10px;"
                           "border: 1px solid %1;"
                           "border-radius: 16px;"
                           "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                           "stop:0 %2, stop:1 %3);"
                           "}"
                           "QMenu::item {"
                           "padding: 9px 16px;"
                           "margin: 3px 0px;"
                           "border-radius: 10px;"
                           "color: %4;"
                           "background: transparent;"
                           "}"
                           "QMenu::item:selected {"
                           "background: %5;"
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
                           "border-radius: 6px;"
                           "}"
                           "QMenu::indicator:unchecked {"
                           "border: 1px solid %7;"
                           "border-radius: 6px;"
                           "background: transparent;"
                           "}")
                           .arg(border.name(QColor::HexArgb))
                           .arg(panelTop.name(QColor::HexArgb))
                           .arg(panelBottom.name(QColor::HexArgb))
                           .arg(palette.text.name(QColor::HexArgb))
                           .arg(selection.name(QColor::HexArgb))
                           .arg(selectionBorder.name(QColor::HexArgb))
                           .arg(disabledText.name(QColor::HexArgb))
                           .arg(separator.name(QColor::HexArgb)));
}

}  // namespace glassnote
