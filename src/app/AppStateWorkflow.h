#pragma once

#include "model/AppState.h"

namespace glassnote::appstate {

NoteItem createEmptyNote(int order);
void syncNoteOrder(QVector<NoteItem> *notes);
void ensureAtLeastOneNote(AppState *state);
bool recordDailyTimelineSnapshot(AppState *state, const QString &dateKey);
bool resolveTimelineNotesForDate(const AppState &state,
                                 const QString &dateKey,
                                 QVector<NoteItem> *notes,
                                 QString *resolvedDateKey = nullptr);

}  // namespace glassnote::appstate
