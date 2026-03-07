#pragma once

#include <QPoint>
#include <QSize>
#include <QVector>

#include "model/NoteItem.h"
#include "model/UiStyle.h"

namespace glassnote {

struct AppState {
    QVector<NoteItem> notes;
    QPoint windowPosition;
    QSize windowSize;
    bool hasSavedWindowPosition = false;
    bool hasSavedWindowSize = false;
    qreal uiScale = 1.0;
    qreal baseLayerOpacity = 1.0;
    UiStyle uiStyle = UiStyle::Glass;
    bool alwaysOnTop = false;
    bool windowLocked = false;
    bool clipboardInboxEnabled = true;
    bool ocrExperimentalEnabled = false;
};

}  // namespace glassnote
