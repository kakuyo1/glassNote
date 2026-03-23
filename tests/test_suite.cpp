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
    state.launchAtStartup = true;
    state.autoCheckUpdates = false;
    state.ignoredUpdateVersion = QStringLiteral("0.2.0");
    state.lastUpdateCheckEpochMsec = 1700001234567LL;
    state.windowLocked = true;
    state.windowPosition = QPoint(88, 99);
    state.windowSize = QSize(512, 420);
    state.hasSavedWindowPosition = true;
    state.hasSavedWindowSize = true;
    state.importedStickers = {
        QStringLiteral("__image_sticker__:%1").arg(QDir::toNativeSeparators(QStringLiteral("C:/stickers/cat.png"))),
        QStringLiteral("__image_sticker__:%1").arg(QDir::toNativeSeparators(QStringLiteral("D:/media/dog.jpg")))};

    state.notes.reserve(noteCount);
    for (int index = 0; index < noteCount; ++index) {
        NoteItem item;
        item.id = QStringLiteral("id-%1").arg(index);
        item.text = QStringLiteral("note-%1").arg(index);
        item.order = noteCount - index;
        item.hue = (index * 13) % 360;
        item.sticker = (index % 2 == 0) ? QStringLiteral("⭐") : QStringLiteral("🔥");
        item.reminderEpochMsec = 1700000000000LL + static_cast<qint64>(index);
        state.notes.append(item);
    }
    return state;
}

}  // namespace

class GlassNoteTests final : public QObject {
    Q_OBJECT

private slots:
    void serialization_roundTripPersistsFields();
    void serialization_acceptsLegacyNumericUiStyle();
    void serialization_legacyBackfillsImportedStickers();
    void controllerLogic_syncNoteOrderReindexes();
    void controllerLogic_ensureAtLeastOneNoteCreatesDefault();
    void integration_loadEditSaveReloadFlow();
    void performance_syncNoteOrderManyNotes();
    void performance_saveManyNotes();
};

void GlassNoteTests::serialization_roundTripPersistsFields() {
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
    QCOMPARE(loaded.launchAtStartup, true);
    QCOMPARE(loaded.autoCheckUpdates, false);
    QCOMPARE(loaded.ignoredUpdateVersion, QStringLiteral("0.2.0"));
    QCOMPARE(loaded.lastUpdateCheckEpochMsec, 1700001234567LL);
    QCOMPARE(loaded.windowLocked, true);
    QCOMPARE(loaded.notes.size(), 3);
    QCOMPARE(loaded.notes.at(0).id, QStringLiteral("id-0"));
    QCOMPARE(loaded.notes.at(0).text, QStringLiteral("note-0"));
    QCOMPARE(loaded.notes.at(0).hue, 0);
    QCOMPARE(loaded.notes.at(0).sticker, QStringLiteral("⭐"));
    QCOMPARE(loaded.notes.at(0).reminderEpochMsec, 1700000000000LL);
    QCOMPARE(loaded.importedStickers.size(), 2);
    QCOMPARE(loaded.importedStickers.at(0),
             QStringLiteral("__image_sticker__:%1").arg(QDir::toNativeSeparators(QStringLiteral("C:/stickers/cat.png"))));
    QCOMPARE(loaded.importedStickers.at(1),
             QStringLiteral("__image_sticker__:%1").arg(QDir::toNativeSeparators(QStringLiteral("D:/media/dog.jpg"))));
}

void GlassNoteTests::serialization_acceptsLegacyNumericUiStyle() {
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
    QCOMPARE(loaded.launchAtStartup, false);
    QCOMPARE(loaded.autoCheckUpdates, true);
    QCOMPARE(loaded.ignoredUpdateVersion, QString());
    QCOMPARE(loaded.lastUpdateCheckEpochMsec, 0);
    QCOMPARE(loaded.notes.at(0).sticker, QString());
    QVERIFY(loaded.importedStickers.isEmpty());
}

void GlassNoteTests::serialization_legacyBackfillsImportedStickers() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = QDir(tempDir.path()).filePath(QStringLiteral("legacy-stickers.json"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
  "version": 2,
  "notes": [
    { "id": "n1", "text": "legacy", "order": 0, "sticker": "__image_sticker__:C:/legacy/a.png" },
    { "id": "n2", "text": "legacy-2", "order": 1, "sticker": "__image_sticker__:c:/legacy/A.png" },
    { "id": "n3", "text": "legacy-3", "order": 2, "sticker": "⭐" }
  ]
})");
    file.close();

    JsonStorageService service;
    AppState loaded;
    QString errorMessage;
    QVERIFY2(service.importState(filePath, &loaded, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(loaded.importedStickers.size(), 1);
    QCOMPARE(loaded.importedStickers.constFirst(),
             QStringLiteral("__image_sticker__:%1").arg(QDir::toNativeSeparators(QStringLiteral("C:/legacy/a.png"))));
}

void GlassNoteTests::controllerLogic_syncNoteOrderReindexes() {
    QVector<NoteItem> notes;
    notes.append(NoteItem{QStringLiteral("a"), QStringLiteral("A"), 99, NoteLane::Today, -1, QString(), 0});
    notes.append(NoteItem{QStringLiteral("b"), QStringLiteral("B"), 88, NoteLane::Today, -1, QString(), 0});
    notes.append(NoteItem{QStringLiteral("c"), QStringLiteral("C"), 77, NoteLane::Today, -1, QString(), 0});

    appstate::syncNoteOrder(&notes);

    QCOMPARE(notes.at(0).order, 0);
    QCOMPARE(notes.at(1).order, 1);
    QCOMPARE(notes.at(2).order, 2);
}

void GlassNoteTests::controllerLogic_ensureAtLeastOneNoteCreatesDefault() {
    AppState state;
    QVERIFY(state.notes.isEmpty());

    appstate::ensureAtLeastOneNote(&state);

    QCOMPARE(state.notes.size(), 1);
    QVERIFY(!state.notes.constFirst().id.isEmpty());
    QCOMPARE(state.notes.constFirst().order, 0);
    QCOMPARE(state.notes.constFirst().hue, -1);
    QCOMPARE(state.notes.constFirst().sticker, QString());
    QCOMPARE(state.notes.constFirst().reminderEpochMsec, 0);
}

void GlassNoteTests::integration_loadEditSaveReloadFlow() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString storagePath = QDir(tempDir.path()).filePath(QStringLiteral("state.json"));
    JsonStorageService service(storagePath);

    AppState initial = makeState(2);
    initial.uiStyle = UiStyle::Neon;

    QString errorMessage;
    QVERIFY2(service.save(initial, &errorMessage), qPrintable(errorMessage));

    JsonStorageService::LoadResult loaded = service.loadWithRecovery();
    QCOMPARE(loaded.status, JsonStorageService::LoadStatus::Success);
    QCOMPARE(loaded.state.notes.size(), 2);
    QCOMPARE(loaded.state.uiStyle, UiStyle::Neon);

    loaded.state.notes[0].text = QStringLiteral("edited-text");
    loaded.state.notes.append(appstate::createEmptyNote(0));
    loaded.state.notes.last().text = QStringLiteral("new-note");
    appstate::syncNoteOrder(&loaded.state.notes);

    QVERIFY2(service.save(loaded.state, &errorMessage), qPrintable(errorMessage));

    const JsonStorageService::LoadResult reloaded = service.loadWithRecovery();
    QCOMPARE(reloaded.status, JsonStorageService::LoadStatus::Success);
    QCOMPARE(reloaded.state.notes.size(), 3);
    QCOMPARE(reloaded.state.notes.at(0).text, QStringLiteral("edited-text"));
    QCOMPARE(reloaded.state.notes.at(2).text, QStringLiteral("new-note"));
    QCOMPARE(reloaded.state.notes.at(2).order, 2);
    QCOMPARE(reloaded.state.uiStyle, UiStyle::Neon);
}

void GlassNoteTests::performance_syncNoteOrderManyNotes() {
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

void GlassNoteTests::performance_saveManyNotes() {
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

QTEST_MAIN(glassnote::GlassNoteTests)

#include "test_suite.moc"
