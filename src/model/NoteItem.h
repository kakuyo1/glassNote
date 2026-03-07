#pragma once

#include <QString>
#include <QtGlobal>

namespace glassnote {

struct NoteItem {
    QString id;
    QString text;
    int order = 0;
    int hue = -1;
    qint64 reminderEpochMsec = 0;
};

}  // namespace glassnote
