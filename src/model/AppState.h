#pragma once

#include <QPoint>
#include <QSize>
#include <QString>
#include <QVector>

#include "model/NoteItem.h"
#include "model/UiStyle.h"

namespace glassnote {

struct DailyTimelineSnapshot {
    QString dateKey;
    QVector<NoteItem> notes;
};

struct AppState {
    QVector<NoteItem> notes;
    QVector<QString> importedStickers;
    QPoint windowPosition;
    QSize windowSize;
    bool hasSavedWindowPosition = false;
    bool hasSavedWindowSize = false;
    qreal uiScale = 1.0;
    qreal baseLayerOpacity = 1.0;
    UiStyle uiStyle = UiStyle::Glass;
    bool alwaysOnTop = false;
    bool launchAtStartup = false;
    bool windowLocked = false;
    bool autoCheckUpdates = true;
    QString ignoredUpdateVersion;
    qint64 lastUpdateCheckEpochMsec = 0;
    bool clipboardInboxEnabled = true;
    bool ocrExperimentalEnabled = false;
    QVector<DailyTimelineSnapshot> timelineSnapshots;
};

}  // namespace glassnote
