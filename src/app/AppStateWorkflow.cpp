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

}  // namespace glassnote::appstate
