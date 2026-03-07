#include <QtTest>

#include <QDir>
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
    state.uiStyle = UiStyle::Neon;
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

    state.notes.reserve(noteCount);
    for (int index = 0; index < noteCount; ++index) {
        NoteItem item;
        item.id = QStringLiteral("id-%1").arg(index);
        item.text = QStringLiteral("note-%1").arg(index);
        item.order = index;
        item.hue = (index * 13) % 360;
        item.sticker = (index % 2 == 0) ? QStringLiteral("⭐") : QStringLiteral("🔥");
        item.reminderEpochMsec = 1700000000000LL + static_cast<qint64>(index);
        state.notes.append(item);
    }

    return state;
}

}  // namespace

class GlassNoteIntegrationTests final : public QObject {
    Q_OBJECT

private slots:
    void loadEditSaveReloadFlow();
};

void GlassNoteIntegrationTests::loadEditSaveReloadFlow() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString storagePath = QDir(tempDir.path()).filePath(QStringLiteral("state.json"));
    JsonStorageService service(storagePath);

    AppState initial = makeState(2);

    QString errorMessage;
    QVERIFY2(service.save(initial, &errorMessage), qPrintable(errorMessage));

    JsonStorageService::LoadResult loaded = service.loadWithRecovery();
    QCOMPARE(loaded.status, JsonStorageService::LoadStatus::Success);
    QCOMPARE(loaded.state.notes.size(), 2);
    QCOMPARE(loaded.state.uiStyle, UiStyle::Neon);
    QCOMPARE(loaded.state.launchAtStartup, true);
    QCOMPARE(loaded.state.autoCheckUpdates, false);
    QCOMPARE(loaded.state.ignoredUpdateVersion, QStringLiteral("0.2.0"));
    QCOMPARE(loaded.state.lastUpdateCheckEpochMsec, 1700001234567LL);
    QCOMPARE(loaded.state.notes.at(0).sticker, QStringLiteral("⭐"));

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
    QCOMPARE(reloaded.state.notes.at(0).sticker, QStringLiteral("⭐"));
    QCOMPARE(reloaded.state.uiStyle, UiStyle::Neon);
    QCOMPARE(reloaded.state.launchAtStartup, true);
    QCOMPARE(reloaded.state.autoCheckUpdates, false);
    QCOMPARE(reloaded.state.ignoredUpdateVersion, QStringLiteral("0.2.0"));
    QCOMPARE(reloaded.state.lastUpdateCheckEpochMsec, 1700001234567LL);
}

}  // namespace glassnote

QTEST_MAIN(glassnote::GlassNoteIntegrationTests)

#include "test_load_edit_save_reload.moc"
