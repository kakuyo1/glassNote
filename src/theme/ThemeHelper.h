#pragma once

#include <QColor>
#include <QString>

#include "model/UiStyle.h"

class QMenu;

namespace glassnote {

struct NotePalette {
    QColor shadow;
    QColor fillTop;
    QColor fillBottom;
    QColor border;
    QColor highlightTop;
    QColor text;
    QColor placeholder;
};

struct WindowPalette {
    QColor fillTop;
    QColor fillMiddle;
    QColor fillBottom;
    QColor border;
    QColor edgeFadeBase;
    QColor scrollHandle;
};

class ThemeHelper final {
public:
    static NotePalette paletteFor(UiStyle uiStyle, int hue, bool hovered);
    static WindowPalette windowPalette(UiStyle uiStyle);
    static QString scrollAreaStyleSheet(UiStyle uiStyle, int scrollBarWidth);
    static void polishMenu(QMenu *menu, UiStyle uiStyle, int hue = -1);
};

}  // namespace glassnote
