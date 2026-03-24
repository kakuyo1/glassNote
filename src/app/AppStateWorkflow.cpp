#include "app/AppStateWorkflow.h"

#include <QDate>
#include <QUuid>

#include <algorithm>

namespace glassnote::appstate {

namespace {

bool notesEquivalent(const QVector<NoteItem> &left, const QVector<NoteItem> &right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (qsizetype index = 0; index < left.size(); ++index) {
        const NoteItem &lhs = left.at(index);
        const NoteItem &rhs = right.at(index);
        if (lhs.id != rhs.id
            || lhs.text != rhs.text
            || lhs.order != rhs.order
            || normalizedNoteLane(lhs.lane) != normalizedNoteLane(rhs.lane)
            || lhs.hue != rhs.hue
            || lhs.sticker != rhs.sticker) {
            return false;
        }
    }

    return true;
}

void sortAndCompactSnapshots(QVector<DailyTimelineSnapshot> *snapshots) {
    if (snapshots == nullptr) {
        return;
    }

    QVector<DailyTimelineSnapshot> filtered;
    filtered.reserve(snapshots->size());
    for (const DailyTimelineSnapshot &snapshot : std::as_const(*snapshots)) {
        if (snapshot.dateKey.isEmpty() || snapshot.notes.isEmpty()) {
            continue;
        }

        const QDate date = QDate::fromString(snapshot.dateKey, QStringLiteral("yyyy-MM-dd"));
        if (!date.isValid()) {
            continue;
        }
        filtered.append(snapshot);
    }

    std::sort(filtered.begin(), filtered.end(), [](const DailyTimelineSnapshot &left,
                                                   const DailyTimelineSnapshot &right) {
        return left.dateKey < right.dateKey;
    });

    QVector<DailyTimelineSnapshot> uniqueDates;
    uniqueDates.reserve(filtered.size());
    for (const DailyTimelineSnapshot &snapshot : std::as_const(filtered)) {
        if (!uniqueDates.isEmpty() && uniqueDates.constLast().dateKey == snapshot.dateKey) {
            uniqueDates.last().notes = snapshot.notes;
        } else {
            uniqueDates.append(snapshot);
        }
    }

    QVector<DailyTimelineSnapshot> deduplicated;
    deduplicated.reserve(uniqueDates.size());
    for (const DailyTimelineSnapshot &snapshot : std::as_const(uniqueDates)) {
        if (deduplicated.isEmpty() || !notesEquivalent(deduplicated.constLast().notes, snapshot.notes)) {
            deduplicated.append(snapshot);
        }
    }

    *snapshots = deduplicated;
}

}  // namespace

NoteItem createEmptyNote(int order) {
    NoteItem note;
    note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    note.text.clear();
    note.order = order;
    note.hue = -1;
    note.sticker.clear();
    return note;
}

void syncNoteOrder(QVector<NoteItem> *notes) {
    if (notes == nullptr) {
        return;
    }

    int todayOrder = 0;
    int nextOrder = 0;
    int waitingOrder = 0;
    int somedayOrder = 0;

    for (int index = 0; index < notes->size(); ++index) {
        NoteItem &note = (*notes)[index];
        note.lane = normalizedNoteLane(note.lane);
        switch (note.lane) {
        case NoteLane::Today:
            note.order = todayOrder;
            ++todayOrder;
            break;
        case NoteLane::Next:
            note.order = nextOrder;
            ++nextOrder;
            break;
        case NoteLane::Waiting:
            note.order = waitingOrder;
            ++waitingOrder;
            break;
        case NoteLane::Someday:
            note.order = somedayOrder;
            ++somedayOrder;
            break;
        }
    }
}

void ensureAtLeastOneNote(AppState *state) {
    if (state == nullptr) {
        return;
    }

    if (state->notes.isEmpty()) {
        state->notes.append(createEmptyNote(0));
    }

    syncNoteOrder(&state->notes);
}

bool recordDailyTimelineSnapshot(AppState *state, const QString &dateKey) {
    if (state == nullptr) {
        return false;
    }

    const QString trimmedDateKey = dateKey.trimmed();
    const QDate date = QDate::fromString(trimmedDateKey, QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid()) {
        return false;
    }

    syncNoteOrder(&state->notes);
    sortAndCompactSnapshots(&state->timelineSnapshots);

    const QVector<NoteItem> currentNotes = state->notes;

    int existingIndex = -1;
    int previousIndex = -1;
    for (qsizetype index = 0; index < state->timelineSnapshots.size(); ++index) {
        const DailyTimelineSnapshot &snapshot = state->timelineSnapshots.at(index);
        if (snapshot.dateKey == trimmedDateKey) {
            existingIndex = static_cast<int>(index);
        }
        if (snapshot.dateKey < trimmedDateKey) {
            previousIndex = static_cast<int>(index);
        }
    }

    bool changed = false;
    if (existingIndex >= 0) {
        DailyTimelineSnapshot &snapshot = state->timelineSnapshots[existingIndex];
        if (!notesEquivalent(snapshot.notes, currentNotes)) {
            snapshot.notes = currentNotes;
            changed = true;
        }
    } else {
        const bool sameAsPrevious = previousIndex >= 0
                                    && notesEquivalent(state->timelineSnapshots.at(previousIndex).notes, currentNotes);
        if (!sameAsPrevious) {
            state->timelineSnapshots.append(DailyTimelineSnapshot{trimmedDateKey, currentNotes});
            changed = true;
        }
    }

    if (changed) {
        sortAndCompactSnapshots(&state->timelineSnapshots);
    }
    return changed;
}

bool resolveTimelineNotesForDate(const AppState &state,
                                 const QString &dateKey,
                                 QVector<NoteItem> *notes,
                                 QString *resolvedDateKey) {
    if (notes == nullptr) {
        return false;
    }

    const QString trimmedDateKey = dateKey.trimmed();
    const QDate date = QDate::fromString(trimmedDateKey, QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid()) {
        return false;
    }

    QVector<DailyTimelineSnapshot> snapshots = state.timelineSnapshots;
    sortAndCompactSnapshots(&snapshots);

    const DailyTimelineSnapshot *resolvedSnapshot = nullptr;
    for (const DailyTimelineSnapshot &snapshot : std::as_const(snapshots)) {
        if (snapshot.dateKey > trimmedDateKey) {
            break;
        }
        resolvedSnapshot = &snapshot;
    }

    if (resolvedSnapshot == nullptr) {
        return false;
    }

    *notes = resolvedSnapshot->notes;
    if (resolvedDateKey != nullptr) {
        *resolvedDateKey = resolvedSnapshot->dateKey;
    }
    return true;
}

}  // namespace glassnote::appstate
