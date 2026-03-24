#pragma once

#include <QString>

namespace glassnote {

enum class NoteLane {
    Today = 0,
    Next = 1,
    Waiting = 2,
    Someday = 3,
};

inline NoteLane normalizedNoteLane(NoteLane lane) {
    switch (lane) {
    case NoteLane::Today:
    case NoteLane::Next:
    case NoteLane::Waiting:
    case NoteLane::Someday:
        return lane;
    }
    return NoteLane::Today;
}

inline int noteLaneSortIndex(NoteLane lane) {
    switch (normalizedNoteLane(lane)) {
    case NoteLane::Today:
        return 0;
    case NoteLane::Next:
        return 1;
    case NoteLane::Waiting:
        return 2;
    case NoteLane::Someday:
        return 3;
    }
    return 0;
}

inline QString noteLaneDisplayName(NoteLane lane) {
    switch (normalizedNoteLane(lane)) {
    case NoteLane::Today:
        return QStringLiteral("Today");
    case NoteLane::Next:
        return QStringLiteral("Next");
    case NoteLane::Waiting:
        return QStringLiteral("Waiting");
    case NoteLane::Someday:
        return QStringLiteral("Someday");
    }
    return QStringLiteral("Today");
}

struct NoteItem {
    QString id;
    QString text;
    int order = 0;
    NoteLane lane = NoteLane::Today;
    int hue = -1;
    QString sticker;
};

}  // namespace glassnote
