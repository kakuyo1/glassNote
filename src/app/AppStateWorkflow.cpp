#include "app/AppStateWorkflow.h"

#include <QUuid>

namespace glassnote::appstate {

NoteItem createEmptyNote(int order) {
    NoteItem note;
    note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    note.text.clear();
    note.order = order;
    note.hue = -1;
    note.reminderEpochMsec = 0;
    return note;
}

void syncNoteOrder(QVector<NoteItem> *notes) {
    if (notes == nullptr) {
        return;
    }

    for (int index = 0; index < notes->size(); ++index) {
        (*notes)[index].order = index;
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

}  // namespace glassnote::appstate
