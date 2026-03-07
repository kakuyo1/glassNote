#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "app/AppStateWorkflow.h"
#include "model/UiStyle.h"
#include "storage/JsonStorageService.h"

namespace glassnote {

namespace {

AppState makeState(int noteCount) {
    AppState state;
    state.uiScale = 1.2;
    state.baseLayerOpacity = 0.9;
    state.uiStyle = UiStyle::Pixel;
    state.alwaysOnTop = true;
    state.windowLocked = true;
    state.clipboardInboxEnabled = false;
    state.ocrExperimentalEnabled = true;
    state.windowPosition = QPoint(88, 99);
    state.windowSize = QSize(512, 420);
    state.hasSavedWindowPosition = true;
    state.hasSavedWindowSize = true;

    state.notes.reserve(noteCount);
    for (int index = 0; index < noteCount; ++index) {
        NoteItem item;
        item.id = QStringLiteral("id-%1").arg(index);
        item.text = QStringLiteral("note-%1").arg(index);
        item.order = noteCount - index;
        item.hue = (index * 13) % 360;
        item.reminderEpochMsec = 1700000000000LL + static_cast<qint64>(index);
        state.notes.append(item);
    }

    return state;
}

}  // namespace

class GlassNoteUnitTests final : public QObject {
    Q_OBJECT

private slots:
    void serialization_roundTripPersistsFields();
    void serialization_acceptsLegacyNumericUiStyle();
    void workflow_syncNoteOrderReindexes();
    void workflow_ensureAtLeastOneNoteCreatesDefault();
};

void GlassNoteUnitTests::serialization_roundTripPersistsFields() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = QDir(tempDir.path()).filePath(QStringLiteral("roundtrip.json"));
    JsonStorageService service;
    const AppState original = makeState(3);

    QString errorMessage;
    QVERIFY2(service.exportState(original, filePath, &errorMessage), qPrintable(errorMessage));

    AppState loaded;
    QVERIFY2(service.importState(filePath, &loaded, &errorMessage), qPrintable(errorMessage));

    QCOMPARE(loaded.uiStyle, UiStyle::Pixel);
    QCOMPARE(loaded.alwaysOnTop, true);
    QCOMPARE(loaded.windowLocked, true);
    QCOMPARE(loaded.clipboardInboxEnabled, false);
    QCOMPARE(loaded.ocrExperimentalEnabled, true);
    QCOMPARE(loaded.notes.size(), 3);
    QCOMPARE(loaded.notes.at(0).id, QStringLiteral("id-0"));
    QCOMPARE(loaded.notes.at(0).text, QStringLiteral("note-0"));
    QCOMPARE(loaded.notes.at(0).hue, 0);
    QCOMPARE(loaded.notes.at(0).reminderEpochMsec, 1700000000000LL);
}

void GlassNoteUnitTests::serialization_acceptsLegacyNumericUiStyle() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = QDir(tempDir.path()).filePath(QStringLiteral("legacy.json"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
  "version": 1,
  "uiStyle": 6,
  "notes": [
    { "id": "n1", "text": "legacy", "order": 0, "hue": -1, "reminderEpochMsec": 0 }
  ]
})");
    file.close();

    JsonStorageService service;
    AppState loaded;
    QString errorMessage;
    QVERIFY2(service.importState(filePath, &loaded, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(loaded.uiStyle, UiStyle::Pixel);
}

void GlassNoteUnitTests::workflow_syncNoteOrderReindexes() {
    QVector<NoteItem> notes;
    notes.append(NoteItem{QStringLiteral("a"), QStringLiteral("A"), 99, -1, 0});
    notes.append(NoteItem{QStringLiteral("b"), QStringLiteral("B"), 88, -1, 0});
    notes.append(NoteItem{QStringLiteral("c"), QStringLiteral("C"), 77, -1, 0});

    appstate::syncNoteOrder(&notes);

    QCOMPARE(notes.at(0).order, 0);
    QCOMPARE(notes.at(1).order, 1);
    QCOMPARE(notes.at(2).order, 2);
}

void GlassNoteUnitTests::workflow_ensureAtLeastOneNoteCreatesDefault() {
    AppState state;
    QVERIFY(state.notes.isEmpty());

    appstate::ensureAtLeastOneNote(&state);

    QCOMPARE(state.notes.size(), 1);
    QVERIFY(!state.notes.constFirst().id.isEmpty());
    QCOMPARE(state.notes.constFirst().order, 0);
    QCOMPARE(state.notes.constFirst().hue, -1);
    QCOMPARE(state.notes.constFirst().reminderEpochMsec, 0);
}

}  // namespace glassnote

QTEST_MAIN(glassnote::GlassNoteUnitTests)

#include "test_serialization_and_workflow.moc"
