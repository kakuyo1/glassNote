#pragma once

#include "model/AppState.h"

namespace glassnote::appstate {

NoteItem createEmptyNote(int order);
void syncNoteOrder(QVector<NoteItem> *notes);
void ensureAtLeastOneNote(AppState *state);

}  // namespace glassnote::appstate
