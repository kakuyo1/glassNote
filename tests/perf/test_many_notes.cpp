#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "app/AppStateWorkflow.h"
#include "storage/JsonStorageService.h"

namespace glassnote {

namespace {

AppState makeState(int noteCount) {
    AppState state;
    state.uiScale = 1.0;
    state.baseLayerOpacity = 1.0;

    state.notes.reserve(noteCount);
    for (int index = 0; index < noteCount; ++index) {
        NoteItem item;
        item.id = QStringLiteral("perf-%1").arg(index);
        item.text = QStringLiteral("item-%1").arg(index);
        item.order = noteCount - index;
        item.hue = -1;
        item.reminderEpochMsec = 0;
        state.notes.append(item);
    }

    return state;
}

}  // namespace

class GlassNotePerfTests final : public QObject {
    Q_OBJECT

private slots:
    void syncNoteOrderManyNotes();
    void saveManyNotes();
};

void GlassNotePerfTests::syncNoteOrderManyNotes() {
    QVector<NoteItem> notes;
    notes.reserve(1500);
    for (int index = 0; index < 1500; ++index) {
        NoteItem note;
        note.id = QStringLiteral("perf-%1").arg(index);
        note.text = QStringLiteral("item-%1").arg(index);
        note.order = 1500 - index;
        notes.append(note);
    }

    QBENCHMARK {
        appstate::syncNoteOrder(&notes);
    }

    QCOMPARE(notes.constFirst().order, 0);
    QCOMPARE(notes.constLast().order, notes.size() - 1);
}

void GlassNotePerfTests::saveManyNotes() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString storagePath = QDir(tempDir.path()).filePath(QStringLiteral("state.json"));
    JsonStorageService service(storagePath);
    const AppState state = makeState(800);

    QBENCHMARK {
        QVERIFY(service.save(state, nullptr));
    }

    const JsonStorageService::LoadResult loaded = service.loadWithRecovery();
    QCOMPARE(loaded.status, JsonStorageService::LoadStatus::Success);
    QCOMPARE(loaded.state.notes.size(), 800);
}

}  // namespace glassnote

QTEST_MAIN(glassnote::GlassNotePerfTests)

#include "test_many_notes.moc"
