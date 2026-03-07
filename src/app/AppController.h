#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QFileSystemWatcher>
#include <QString>

#include "model/AppState.h"
#include "storage/JsonStorageService.h"

class QMenu;
class QSystemTrayIcon;
class QTimer;
class QAction;
class QClipboard;

namespace glassnote {

class MainWindow;
class AutoSaveCoordinator;

class AppController final : public QObject, private QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    void initialize();

private:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    NoteItem createEmptyNote(int order) const;
    void ensureAtLeastOneNote();
    void syncNoteOrder();
    void refreshWindow();
    void applyStateToWindow();
    void updateStateFromWindow();
    void handleNoteTextCommitted(const QString &noteId, const QString &text);
    void handleWindowMoved(const QPoint &position);
    void handleWindowResized(const QSize &size);
    void handleAddNoteRequested();
    void handleClearEmptyRequested();
    void handleDeleteNoteRequested(const QString &noteId);
    void handleNoteHueChangeRequested(const QString &noteId, int hue);
    void handleUiStyleChangeRequested(UiStyle uiStyle);
    void handleScaleInRequested();
    void handleScaleOutRequested();
    void handleScaleResetRequested();
    void handleBaseLayerOpacitySetRequested(qreal opacity);
    void handleExportJsonRequested();
    void handleImportJsonRequested();
    void handleBackupSnapshotRequested();
    void handleRestoreLatestBackupRequested();
    void handleExternalFileSyncToggled(bool enabled);
    void handleAlwaysOnTopToggled(bool enabled);
    void handleWindowLockToggled(bool enabled);
    void handleReminderSetRequested(const QString &noteId);
    void handleReminderClearedRequested(const QString &noteId);
    void handleStorageFileChanged(const QString &filePath);
    void handleReminderTimeout();
    void handleQuickCaptureRequested();
    void handleClipboardDataChanged();
    void handleClipboardInboxImportRequested();
    void handleEdgeDropCaptureRequested(const QString &payload);
    void handleClipboardInboxToggled(bool enabled);
    void handleOcrExperimentalToggled(bool enabled);
    void handleOcrCaptureRequested();
    void initializeSystemTray();
    void updateTrayMenuText();
    void toggleMainWindowVisibility();
    bool appendCapturedNote(const QString &text);
    void registerGlobalHotkey();
    void unregisterGlobalHotkey();
    void scheduleNextReminder();
    QString reminderPreviewText(const NoteItem &note) const;
    void openStorageDirectory();
    void quitApplication();
    void showLoadRecoveryMessage(const JsonStorageService::LoadResult &result, bool fromExternalSync);
    void refreshStorageFileWatch();
    void setExternalFileSyncEnabled(bool enabled);
    void saveState();

    AppState m_state;
    MainWindow *m_mainWindow = nullptr;
    AutoSaveCoordinator *m_autoSaveCoordinator = nullptr;
    QFileSystemWatcher *m_storageFileWatcher = nullptr;
    JsonStorageService m_storageService;
    bool m_externalFileSyncEnabled = true;
    qint64 m_lastInternalSaveMsec = 0;
    QTimer *m_reminderTimer = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QClipboard *m_clipboard = nullptr;
    QAction *m_trayClipboardImportAction = nullptr;
    QAction *m_trayClipboardInboxToggleAction = nullptr;
    QAction *m_trayOcrToggleAction = nullptr;
    QString m_lastClipboardText;
    QString m_pendingClipboardInboxText;
    bool m_clipboardInboxEnabled = true;
    bool m_ocrExperimentalEnabled = false;
    bool m_addHotkeyRegistered = false;
    bool m_quickCaptureHotkeyRegistered = false;
    bool m_nativeEventFilterInstalled = false;
    bool m_quitInProgress = false;
};

}  // namespace glassnote
