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
        item.lane = static_cast<NoteLane>(index % 4);
        item.hue = (index * 13) % 360;
        item.sticker = (index % 2 == 0) ? QStringLiteral("⭐") : QStringLiteral("🔥");
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
    void serialization_roundTripPersistsTimelineSnapshots();
    void serialization_acceptsLegacyNumericUiStyle();
    void workflow_syncNoteOrderReindexes();
    void workflow_syncNoteOrderReindexesPerLane();
    void workflow_ensureAtLeastOneNoteCreatesDefault();
    void workflow_recordDailyTimelineSnapshotDeduplicatesByContent();
    void workflow_resolveTimelineNotesForDateSupportsFallback();
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
    QCOMPARE(loaded.launchAtStartup, true);
    QCOMPARE(loaded.autoCheckUpdates, false);
    QCOMPARE(loaded.ignoredUpdateVersion, QStringLiteral("0.2.0"));
    QCOMPARE(loaded.lastUpdateCheckEpochMsec, 1700001234567LL);
    QCOMPARE(loaded.windowLocked, true);
    QCOMPARE(loaded.clipboardInboxEnabled, false);
    QCOMPARE(loaded.ocrExperimentalEnabled, true);
    QCOMPARE(loaded.notes.size(), 3);
    QCOMPARE(loaded.notes.at(0).id, QStringLiteral("id-0"));
    QCOMPARE(loaded.notes.at(0).text, QStringLiteral("note-0"));
    QCOMPARE(loaded.notes.at(0).lane, NoteLane::Today);
    QCOMPARE(loaded.notes.at(1).lane, NoteLane::Next);
    QCOMPARE(loaded.notes.at(0).hue, 0);
    QCOMPARE(loaded.notes.at(0).sticker, QStringLiteral("⭐"));
    QCOMPARE(loaded.notes.at(0).reminderEpochMsec, 1700000000000LL);
}

void GlassNoteUnitTests::serialization_roundTripPersistsTimelineSnapshots() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = QDir(tempDir.path()).filePath(QStringLiteral("timeline-roundtrip.json"));
    JsonStorageService service;

    AppState original = makeState(2);
    DailyTimelineSnapshot s1;
    s1.dateKey = QStringLiteral("2026-03-01");
    s1.notes = original.notes;

    DailyTimelineSnapshot s2;
    s2.dateKey = QStringLiteral("2026-03-02");
    s2.notes = original.notes;
    s2.notes[0].text = QStringLiteral("changed");

    original.timelineSnapshots = {s1, s2};

    QString errorMessage;
    QVERIFY2(service.exportState(original, filePath, &errorMessage), qPrintable(errorMessage));

    AppState loaded;
    QVERIFY2(service.importState(filePath, &loaded, &errorMessage), qPrintable(errorMessage));

    QCOMPARE(loaded.timelineSnapshots.size(), 2);
    QCOMPARE(loaded.timelineSnapshots.at(0).dateKey, QStringLiteral("2026-03-01"));
    QCOMPARE(loaded.timelineSnapshots.at(1).dateKey, QStringLiteral("2026-03-02"));
    QCOMPARE(loaded.timelineSnapshots.at(0).notes.at(0).text, QStringLiteral("note-0"));
    QCOMPARE(loaded.timelineSnapshots.at(1).notes.at(0).text, QStringLiteral("changed"));
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
    QCOMPARE(loaded.launchAtStartup, false);
    QCOMPARE(loaded.autoCheckUpdates, true);
    QCOMPARE(loaded.ignoredUpdateVersion, QString());
    QCOMPARE(loaded.lastUpdateCheckEpochMsec, 0);
    QCOMPARE(loaded.notes.at(0).lane, NoteLane::Today);
    QCOMPARE(loaded.notes.at(0).sticker, QString());
}

void GlassNoteUnitTests::workflow_syncNoteOrderReindexes() {
    QVector<NoteItem> notes;
    notes.append(NoteItem{QStringLiteral("a"), QStringLiteral("A"), 99, NoteLane::Today, -1, QString(), 0});
    notes.append(NoteItem{QStringLiteral("b"), QStringLiteral("B"), 88, NoteLane::Today, -1, QString(), 0});
    notes.append(NoteItem{QStringLiteral("c"), QStringLiteral("C"), 77, NoteLane::Today, -1, QString(), 0});

    appstate::syncNoteOrder(&notes);

    QCOMPARE(notes.at(0).order, 0);
    QCOMPARE(notes.at(1).order, 1);
    QCOMPARE(notes.at(2).order, 2);
}

void GlassNoteUnitTests::workflow_syncNoteOrderReindexesPerLane() {
    QVector<NoteItem> notes;
    notes.append(NoteItem{QStringLiteral("a"), QStringLiteral("A"), 99, NoteLane::Today, -1, QString(), 0});
    notes.append(NoteItem{QStringLiteral("b"), QStringLiteral("B"), 88, NoteLane::Waiting, -1, QString(), 0});
    notes.append(NoteItem{QStringLiteral("c"), QStringLiteral("C"), 77, NoteLane::Today, -1, QString(), 0});
    notes.append(NoteItem{QStringLiteral("d"), QStringLiteral("D"), 66, NoteLane::Waiting, -1, QString(), 0});

    appstate::syncNoteOrder(&notes);

    QCOMPARE(notes.at(0).order, 0);
    QCOMPARE(notes.at(1).order, 0);
    QCOMPARE(notes.at(2).order, 1);
    QCOMPARE(notes.at(3).order, 1);
}

void GlassNoteUnitTests::workflow_ensureAtLeastOneNoteCreatesDefault() {
    AppState state;
    QVERIFY(state.notes.isEmpty());

    appstate::ensureAtLeastOneNote(&state);

    QCOMPARE(state.notes.size(), 1);
    QVERIFY(!state.notes.constFirst().id.isEmpty());
    QCOMPARE(state.notes.constFirst().order, 0);
    QCOMPARE(state.notes.constFirst().lane, NoteLane::Today);
    QCOMPARE(state.notes.constFirst().hue, -1);
    QCOMPARE(state.notes.constFirst().sticker, QString());
    QCOMPARE(state.notes.constFirst().reminderEpochMsec, 0);
}

void GlassNoteUnitTests::workflow_recordDailyTimelineSnapshotDeduplicatesByContent() {
    AppState state = makeState(2);

    QVERIFY(appstate::recordDailyTimelineSnapshot(&state, QStringLiteral("2026-03-01")));
    QCOMPARE(state.timelineSnapshots.size(), 1);

    QVERIFY(!appstate::recordDailyTimelineSnapshot(&state, QStringLiteral("2026-03-02")));
    QCOMPARE(state.timelineSnapshots.size(), 1);

    state.notes[0].text = QStringLiteral("updated");
    QVERIFY(appstate::recordDailyTimelineSnapshot(&state, QStringLiteral("2026-03-03")));
    QCOMPARE(state.timelineSnapshots.size(), 2);
    QCOMPARE(state.timelineSnapshots.at(1).dateKey, QStringLiteral("2026-03-03"));
}

void GlassNoteUnitTests::workflow_resolveTimelineNotesForDateSupportsFallback() {
    AppState state = makeState(2);

    QVERIFY(appstate::recordDailyTimelineSnapshot(&state, QStringLiteral("2026-03-01")));
    state.notes[0].text = QStringLiteral("v2");
    QVERIFY(appstate::recordDailyTimelineSnapshot(&state, QStringLiteral("2026-03-03")));

    QVector<NoteItem> resolved;
    QString resolvedDateKey;

    QVERIFY(appstate::resolveTimelineNotesForDate(
        state, QStringLiteral("2026-03-03"), &resolved, &resolvedDateKey));
    QCOMPARE(resolvedDateKey, QStringLiteral("2026-03-03"));
    QCOMPARE(resolved.at(0).text, QStringLiteral("v2"));

    QVERIFY(appstate::resolveTimelineNotesForDate(
        state, QStringLiteral("2026-03-02"), &resolved, &resolvedDateKey));
    QCOMPARE(resolvedDateKey, QStringLiteral("2026-03-01"));
    QCOMPARE(resolved.at(0).text, QStringLiteral("note-0"));

    QVERIFY(!appstate::resolveTimelineNotesForDate(
        state, QStringLiteral("2026-02-28"), &resolved, &resolvedDateKey));
}

}  // namespace glassnote

QTEST_MAIN(glassnote::GlassNoteUnitTests)

#include "test_serialization_and_workflow.moc"
