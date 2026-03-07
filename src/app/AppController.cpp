#include "app/AppController.h"

#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDateTimeEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QAbstractButton>
#include <QApplication>
#include <QPushButton>
#include <QSet>
#include <QScreen>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QLoggingCategory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "app/AppStateWorkflow.h"
#include "common/Constants.h"
#include "storage/AutoSaveCoordinator.h"
#include "storage/JsonStorageService.h"
#include "theme/ThemeHelper.h"
#include "ui/MainWindow.h"

namespace glassnote {

namespace {

Q_LOGGING_CATEGORY(lcAppController, "glassnote.app.controller")

#ifdef Q_OS_WIN
constexpr int kAddNoteHotkeyId = 0x4A31;
constexpr UINT kAddNoteHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
constexpr UINT kAddNoteHotkeyVirtualKey = 0x4E;  // N
constexpr int kQuickCaptureHotkeyId = 0x4A32;
constexpr UINT kQuickCaptureHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
constexpr UINT kQuickCaptureHotkeyVirtualKey = 0x51;  // Q
#endif

constexpr int kReminderPollToleranceMsec = 500;
constexpr int kReminderMaxIntervalMsec = 24 * 24 * 60 * 60 * 1000;
constexpr int kClipboardInboxPreviewLength = 36;

bool hasMeaningfulText(const QString &text) {
    QTextDocument document;
    if (Qt::mightBeRichText(text)) {
        document.setHtml(text);
    } else {
        document.setPlainText(text);
    }
    return !document.toPlainText().trimmed().isEmpty();
}

bool promptReminderDateTime(QWidget *parent, const QDateTime &initialValue, qint64 *selectedEpochMsec) {
    if (selectedEpochMsec == nullptr) {
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("设置事项提醒"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *dateTimeEdit = new QDateTimeEdit(&dialog);
    dateTimeEdit->setCalendarPopup(true);
    dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    dateTimeEdit->setDateTime(initialValue);
    dateTimeEdit->setMinimumDateTime(QDateTime::currentDateTime().addSecs(60));
    layout->addWidget(dateTimeEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    *selectedEpochMsec = dateTimeEdit->dateTime().toMSecsSinceEpoch();
    return true;
}

bool promptQuickCaptureText(QWidget *parent,
                            UiStyle uiStyle,
                            QString *capturedText,
                            const QString &initialText = QString()) {
    if (capturedText == nullptr) {
        return false;
    }

    const NotePalette palette = ThemeHelper::paletteFor(uiStyle, -1, false);
    const WindowPalette windowPalette = ThemeHelper::windowPalette(uiStyle);
    QColor panelTop = windowPalette.fillTop;
    panelTop.setAlpha(qBound(72, panelTop.alpha() + 56, 220));
    QColor panelBottom = windowPalette.fillBottom;
    panelBottom.setAlpha(qBound(58, panelBottom.alpha() + 46, 210));
    QColor fieldFill = palette.fillBottom;
    fieldFill.setAlpha(qBound(54, fieldFill.alpha() + 20, 180));
    QColor actionFill = palette.highlightTop;
    actionFill.setAlpha(qBound(80, actionFill.alpha() + 44, 220));
    const bool actionIsDark = actionFill.lightness() < 142;
    const QColor selectionText = actionIsDark ? QColor(255, 255, 255, 242) : QColor(24, 28, 34, 232);
    const int dialogRadius = uiStyle == UiStyle::Pixel ? 10 : 16;

    QDialog dialog(parent, Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setWindowTitle(QStringLiteral("极速捕获"));
    dialog.setModal(true);
    dialog.setAttribute(Qt::WA_TranslucentBackground, true);
    dialog.setObjectName(QStringLiteral("quickCaptureDialog"));
    dialog.setMinimumWidth(420);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("极速输入条"), &dialog);
    title->setObjectName(QStringLiteral("quickCaptureTitle"));
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    title->setFont(titleFont);

    auto *hint = new QLabel(QStringLiteral("输入后按 Enter 立即创建事项（Esc 取消）"), &dialog);
    hint->setObjectName(QStringLiteral("quickCaptureHint"));
    auto *lineEdit = new QLineEdit(&dialog);
    lineEdit->setPlaceholderText(QStringLiteral("记一条..."));
    lineEdit->setClearButtonEnabled(true);
    lineEdit->setMinimumHeight(34);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    if (QAbstractButton *cancelButton = buttons->button(QDialogButtonBox::Cancel)) {
        cancelButton->setText(QStringLiteral("取消"));
    }
    auto *captureButton = new QPushButton(QStringLiteral("创建"), &dialog);
    captureButton->setObjectName(QStringLiteral("quickCaptureConfirmButton"));
    buttons->addButton(captureButton, QDialogButtonBox::AcceptRole);
    captureButton->setDefault(true);

    dialog.setStyleSheet(
        QStringLiteral("QDialog#quickCaptureDialog {"
                       "border: 1px solid %1;"
                       "border-radius: %10px;"
                       "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %2,stop:1 %3);"
                       "}"
                       "QLabel#quickCaptureTitle {"
                       "color: %4;"
                       "}"
                       "QLabel#quickCaptureHint {"
                       "color: %5;"
                       "}"
                       "QLineEdit {"
                       "color: %4;"
                       "background: %6;"
                       "border: 1px solid %1;"
                       "border-radius: 9px;"
                       "padding: 6px 10px;"
                       "selection-background-color: %7;"
                       "selection-color: %8;"
                       "}"
                       "QLineEdit:focus {"
                       "border: 1px solid %7;"
                       "}"
                       "QPushButton {"
                       "color: %4;"
                       "border-radius: 9px;"
                       "padding: 6px 14px;"
                       "border: 1px solid %1;"
                       "background: transparent;"
                       "}"
                       "QPushButton#quickCaptureConfirmButton {"
                       "background: %7;"
                       "border: 1px solid %1;"
                       "}"
                       "QPushButton:hover {"
                       "background: %9;"
                       "}")
            .arg(palette.border.name(QColor::HexArgb))
            .arg(panelTop.name(QColor::HexArgb))
            .arg(panelBottom.name(QColor::HexArgb))
            .arg(palette.text.name(QColor::HexArgb))
            .arg(palette.placeholder.name(QColor::HexArgb))
            .arg(fieldFill.name(QColor::HexArgb))
            .arg(actionFill.name(QColor::HexArgb))
            .arg(selectionText.name(QColor::HexArgb))
            .arg(palette.highlightTop.name(QColor::HexArgb))
            .arg(dialogRadius));

    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addWidget(lineEdit);
    layout->addWidget(buttons);

    QObject::connect(lineEdit, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    lineEdit->setText(initialText);
    if (!initialText.isEmpty()) {
        lineEdit->selectAll();
    }

    dialog.adjustSize();
    const QSize dialogSize = dialog.size();
    const QScreen *targetScreen = parent != nullptr
                                      ? parent->screen()
                                      : QGuiApplication::screenAt(QCursor::pos());
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (targetScreen != nullptr) {
        const QRect available = targetScreen->availableGeometry();
        const int centeredX = available.center().x() - (dialogSize.width() / 2);
        const int centeredY = available.center().y() - (dialogSize.height() / 2);
        const int maxX = qMax(available.left(), available.right() - dialogSize.width() + 1);
        const int maxY = qMax(available.top(), available.bottom() - dialogSize.height() + 1);
        dialog.move(qBound(available.left(), centeredX, maxX),
                    qBound(available.top(), centeredY, maxY));
    }

    lineEdit->setFocus(Qt::OtherFocusReason);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    *capturedText = lineEdit->text();
    return true;
}

}  // namespace

AppController::AppController(QObject *parent)
    : QObject(parent) {
    m_autoSaveCoordinator = new AutoSaveCoordinator(this);
    m_storageFileWatcher = new QFileSystemWatcher(this);
    m_reminderTimer = new QTimer(this);
    m_reminderTimer->setSingleShot(true);

    connect(m_autoSaveCoordinator, &AutoSaveCoordinator::saveRequested, this, &AppController::saveState);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &AppController::saveState);
    connect(m_storageFileWatcher, &QFileSystemWatcher::fileChanged, this, &AppController::handleStorageFileChanged);
    connect(m_reminderTimer, &QTimer::timeout, this, &AppController::handleReminderTimeout);

    if (qApp != nullptr) {
        qApp->installNativeEventFilter(this);
        m_nativeEventFilterInstalled = true;
    }
}

AppController::~AppController() {
    unregisterGlobalHotkey();
    if (m_nativeEventFilterInstalled && qApp != nullptr && !QCoreApplication::closingDown()) {
        qApp->removeNativeEventFilter(this);
        m_nativeEventFilterInstalled = false;
    }
}

void AppController::initialize() {
    if (m_mainWindow != nullptr) {
        return;
    }

    const JsonStorageService::LoadResult loadResult = m_storageService.loadWithRecovery();
    qCInfo(lcAppController) << "startup load status" << static_cast<int>(loadResult.status)
                            << "message" << loadResult.message;
    m_state = loadResult.state;
    ensureAtLeastOneNote();
    m_clipboardInboxEnabled = m_state.clipboardInboxEnabled;
    m_ocrExperimentalEnabled = m_state.ocrExperimentalEnabled;

    m_mainWindow = new MainWindow();
    initializeSystemTray();
    applyStateToWindow();
    setExternalFileSyncEnabled(m_externalFileSyncEnabled);
    refreshStorageFileWatch();
    registerGlobalHotkey();

    connect(m_mainWindow, &MainWindow::noteTextCommitted, this, &AppController::handleNoteTextCommitted);
    connect(m_mainWindow, &MainWindow::windowMoved, this, &AppController::handleWindowMoved);
    connect(m_mainWindow, &MainWindow::windowResized, this, &AppController::handleWindowResized);
    connect(m_mainWindow, &MainWindow::addNoteRequested, this, &AppController::handleAddNoteRequested);
    connect(m_mainWindow, &MainWindow::clearEmptyRequested, this, &AppController::handleClearEmptyRequested);
    connect(m_mainWindow, &MainWindow::noteDeleteRequested, this, &AppController::handleDeleteNoteRequested);
    connect(m_mainWindow, &MainWindow::noteHueChangeRequested, this, &AppController::handleNoteHueChangeRequested);
    connect(m_mainWindow, &MainWindow::uiStyleChangeRequested, this, &AppController::handleUiStyleChangeRequested);
    connect(m_mainWindow, &MainWindow::scaleInRequested, this, &AppController::handleScaleInRequested);
    connect(m_mainWindow, &MainWindow::scaleOutRequested, this, &AppController::handleScaleOutRequested);
    connect(m_mainWindow, &MainWindow::scaleResetRequested, this, &AppController::handleScaleResetRequested);
    connect(m_mainWindow,
            &MainWindow::baseLayerOpacitySetRequested,
            this,
            &AppController::handleBaseLayerOpacitySetRequested);
    connect(m_mainWindow, &MainWindow::exportJsonRequested, this, &AppController::handleExportJsonRequested);
    connect(m_mainWindow, &MainWindow::importJsonRequested, this, &AppController::handleImportJsonRequested);
    connect(m_mainWindow, &MainWindow::backupSnapshotRequested, this, &AppController::handleBackupSnapshotRequested);
    connect(m_mainWindow,
            &MainWindow::restoreLatestBackupRequested,
            this,
            &AppController::handleRestoreLatestBackupRequested);
    connect(m_mainWindow, &MainWindow::externalFileSyncToggled, this, &AppController::handleExternalFileSyncToggled);
    connect(m_mainWindow, &MainWindow::alwaysOnTopToggled, this, &AppController::handleAlwaysOnTopToggled);
    connect(m_mainWindow, &MainWindow::windowLockToggled, this, &AppController::handleWindowLockToggled);
    connect(m_mainWindow, &MainWindow::reminderSetRequested, this, &AppController::handleReminderSetRequested);
    connect(m_mainWindow,
            &MainWindow::reminderClearedRequested,
            this,
            &AppController::handleReminderClearedRequested);
    connect(m_mainWindow, &MainWindow::openStorageDirectoryRequested, this, &AppController::openStorageDirectory);
    connect(m_mainWindow, &MainWindow::quitRequested, this, &AppController::quitApplication);
    connect(m_mainWindow,
            &MainWindow::edgeDropCaptureRequested,
            this,
            &AppController::handleEdgeDropCaptureRequested);
    connect(m_mainWindow,
            &MainWindow::quickCaptureDropRequested,
            this,
            &AppController::handleQuickCaptureDropRequested);

    m_clipboard = QGuiApplication::clipboard();
    if (m_clipboard != nullptr) {
        m_lastClipboardText = m_clipboard->text(QClipboard::Clipboard);
        connect(m_clipboard, &QClipboard::dataChanged, this, &AppController::handleClipboardDataChanged);
    }

    m_mainWindow->show();
    updateTrayMenuText();
    scheduleNextReminder();
    showLoadRecoveryMessage(loadResult, false);
}

NoteItem AppController::createEmptyNote(int order) const {
    return appstate::createEmptyNote(order);
}

void AppController::ensureAtLeastOneNote() {
    appstate::ensureAtLeastOneNote(&m_state);
}

void AppController::syncNoteOrder() {
    appstate::syncNoteOrder(&m_state.notes);
}

void AppController::refreshWindow() {
    if (m_mainWindow == nullptr) {
        return;
    }

    syncNoteOrder();
    m_mainWindow->setUiScale(m_state.uiScale);
    m_mainWindow->setUiStyle(m_state.uiStyle);
    m_mainWindow->setBaseLayerOpacity(m_state.baseLayerOpacity);
    m_mainWindow->setAlwaysOnTopEnabled(m_state.alwaysOnTop);
    m_mainWindow->setWindowLocked(m_state.windowLocked);
    m_mainWindow->setNotes(m_state.notes);
}

void AppController::applyStateToWindow() {
    if (m_mainWindow == nullptr) {
        return;
    }

    m_clipboardInboxEnabled = m_state.clipboardInboxEnabled;
    m_ocrExperimentalEnabled = m_state.ocrExperimentalEnabled;
    if (!m_clipboardInboxEnabled) {
        m_pendingClipboardInboxText.clear();
    }

    refreshWindow();

    if (m_state.hasSavedWindowPosition) {
        m_mainWindow->restoreWindowPosition(m_state.windowPosition);
    }
    if (m_state.hasSavedWindowSize) {
        m_mainWindow->restoreWindowSize(m_state.windowSize);
    }
}

void AppController::updateStateFromWindow() {
    if (m_mainWindow != nullptr) {
        m_state.notes = m_mainWindow->notes();
        m_state.windowPosition = m_mainWindow->pos();
        m_state.windowSize = m_mainWindow->persistedWindowSize();
        m_state.hasSavedWindowPosition = true;
        m_state.hasSavedWindowSize = true;
    }

    ensureAtLeastOneNote();
    m_state.clipboardInboxEnabled = m_clipboardInboxEnabled;
    m_state.ocrExperimentalEnabled = m_ocrExperimentalEnabled;
}

bool AppController::appendCapturedNote(const QString &text) {
    if (!hasMeaningfulText(text)) {
        return false;
    }

    ensureAtLeastOneNote();

    NoteItem note = createEmptyNote(m_state.notes.size());
    note.text = text.trimmed();
    m_state.notes.append(note);
    refreshWindow();
    m_autoSaveCoordinator->requestSave();
    updateTrayMenuText();
    return true;
}

void AppController::handleNoteTextCommitted(const QString &noteId, const QString &text) {
    for (NoteItem &note : m_state.notes) {
        if (note.id == noteId) {
            note.text = text;
            m_autoSaveCoordinator->requestSave();
            return;
        }
    }

    m_autoSaveCoordinator->requestSave();
}

void AppController::handleWindowMoved(const QPoint &position) {
    m_state.windowPosition = position;
    m_state.hasSavedWindowPosition = true;
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleWindowResized(const QSize &size) {
    m_state.windowSize = size;
    m_state.hasSavedWindowSize = true;
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleAddNoteRequested() {
    ensureAtLeastOneNote();

    const NoteItem newNote = createEmptyNote(m_state.notes.size());
    m_state.notes.append(newNote);
    refreshWindow();
    m_mainWindow->focusNoteEditor(newNote.id);
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleClearEmptyRequested() {
    QVector<NoteItem> filtered;
    filtered.reserve(m_state.notes.size());

    for (const NoteItem &note : std::as_const(m_state.notes)) {
        if (hasMeaningfulText(note.text)) {
            filtered.append(note);
        }
    }

    m_state.notes = filtered;
    ensureAtLeastOneNote();
    refreshWindow();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleDeleteNoteRequested(const QString &noteId) {
    for (auto it = m_state.notes.begin(); it != m_state.notes.end(); ++it) {
        if (it->id == noteId) {
            m_state.notes.erase(it);
            break;
        }
    }

    ensureAtLeastOneNote();
    refreshWindow();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleNoteHueChangeRequested(const QString &noteId, int hue) {
    for (NoteItem &note : m_state.notes) {
        if (note.id == noteId) {
            note.hue = hue;
            break;
        }
    }

    refreshWindow();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleUiStyleChangeRequested(UiStyle uiStyle) {
    if (m_state.uiStyle == uiStyle) {
        return;
    }

    m_state.uiStyle = uiStyle;
    refreshWindow();
    updateTrayMenuText();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleScaleInRequested() {
    m_state.uiScale = qMin(constants::kMaxUiScale, m_state.uiScale + constants::kUiScaleStep);
    refreshWindow();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleScaleOutRequested() {
    m_state.uiScale = qMax(constants::kMinUiScale, m_state.uiScale - constants::kUiScaleStep);
    refreshWindow();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleScaleResetRequested() {
    m_state.uiScale = constants::kDefaultUiScale;
    refreshWindow();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleBaseLayerOpacitySetRequested(qreal opacity) {
    m_state.baseLayerOpacity = qBound(constants::kMinBaseLayerOpacity,
                                      opacity,
                                      constants::kMaxBaseLayerOpacity);
    refreshWindow();
    m_autoSaveCoordinator->requestSave();
}

void AppController::openStorageDirectory() {
    const QString directoryPath = QFileInfo(m_storageService.storageFilePath()).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(directoryPath));
}

void AppController::quitApplication() {
    if (m_quitInProgress) {
        return;
    }
    m_quitInProgress = true;

    QTimer::singleShot(0, this, [this]() {
        saveState();

        if (m_reminderTimer != nullptr) {
            m_reminderTimer->stop();
        }

        if (m_storageFileWatcher != nullptr) {
            const QStringList watchedFiles = m_storageFileWatcher->files();
            if (!watchedFiles.isEmpty()) {
                m_storageFileWatcher->removePaths(watchedFiles);
            }
        }

        unregisterGlobalHotkey();
        if (m_nativeEventFilterInstalled && qApp != nullptr) {
            qApp->removeNativeEventFilter(this);
            m_nativeEventFilterInstalled = false;
        }

        if (m_trayIcon != nullptr) {
            m_trayIcon->hide();
            m_trayIcon->setContextMenu(nullptr);
        }

        if (m_mainWindow != nullptr) {
            m_mainWindow->close();
            m_mainWindow->deleteLater();
            m_mainWindow = nullptr;
        }

        if (m_trayMenu != nullptr) {
            m_trayMenu->deleteLater();
            m_trayMenu = nullptr;
        }
        if (m_trayIcon != nullptr) {
            m_trayIcon->deleteLater();
            m_trayIcon = nullptr;
        }

        QCoreApplication::quit();
    });
}

void AppController::handleExportJsonRequested() {
    if (m_mainWindow == nullptr) {
        return;
    }

    updateStateFromWindow();
    const QString suggestName = QStringLiteral("glassNote-export-%1.json")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString startDir = QFileInfo(m_storageService.storageFilePath()).absolutePath();
    const QString filePath = QFileDialog::getSaveFileName(m_mainWindow,
                                                          QStringLiteral("导出 JSON"),
                                                          QDir(startDir).filePath(suggestName),
                                                          QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!m_storageService.exportState(m_state, filePath, &errorMessage)) {
        QMessageBox::warning(m_mainWindow,
                             QStringLiteral("导出失败"),
                             QStringLiteral("导出 JSON 失败。\n%1").arg(errorMessage));
        return;
    }

    QMessageBox::information(m_mainWindow,
                             QStringLiteral("导出完成"),
                             QStringLiteral("已导出到：\n%1").arg(filePath));
}

void AppController::handleImportJsonRequested() {
    if (m_mainWindow == nullptr) {
        return;
    }

    const QString startDir = QFileInfo(m_storageService.storageFilePath()).absolutePath();
    const QString filePath = QFileDialog::getOpenFileName(m_mainWindow,
                                                          QStringLiteral("导入 JSON"),
                                                          startDir,
                                                          QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    AppState importedState;
    QString importError;
    if (!m_storageService.importState(filePath, &importedState, &importError)) {
        QMessageBox::warning(m_mainWindow,
                             QStringLiteral("导入失败"),
                             QStringLiteral("无法读取 JSON。\n%1").arg(importError));
        return;
    }

    QMessageBox modeDialog(m_mainWindow);
    modeDialog.setIcon(QMessageBox::Question);
    modeDialog.setWindowTitle(QStringLiteral("选择导入方式"));
    modeDialog.setText(QStringLiteral("请选择导入模式："));
    modeDialog.setInformativeText(QStringLiteral("合并：追加导入事项；覆盖：完全替换当前数据。"));
    QPushButton *mergeButton = modeDialog.addButton(QStringLiteral("合并"), QMessageBox::AcceptRole);
    QPushButton *overwriteButton = modeDialog.addButton(QStringLiteral("覆盖"), QMessageBox::DestructiveRole);
    modeDialog.addButton(QMessageBox::Cancel);
    modeDialog.exec();

    QAbstractButton *clicked = modeDialog.clickedButton();
    if (clicked == nullptr || clicked == modeDialog.button(QMessageBox::Cancel)) {
        return;
    }

    if (clicked == static_cast<QAbstractButton *>(overwriteButton)) {
        m_state = importedState;
        ensureAtLeastOneNote();
        applyStateToWindow();
        updateTrayMenuText();
        scheduleNextReminder();
        m_autoSaveCoordinator->requestSave();
        QMessageBox::information(m_mainWindow,
                                 QStringLiteral("导入完成"),
                                 QStringLiteral("已覆盖当前数据。"));
        return;
    }

    if (clicked == static_cast<QAbstractButton *>(mergeButton)) {
        QSet<QString> usedIds;
        usedIds.reserve(m_state.notes.size() + importedState.notes.size());
        for (const NoteItem &note : std::as_const(m_state.notes)) {
            usedIds.insert(note.id);
        }

        int nextOrder = m_state.notes.size();
        for (const NoteItem &source : std::as_const(importedState.notes)) {
            NoteItem merged = source;
            if (merged.id.isEmpty() || usedIds.contains(merged.id)) {
                merged.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
            usedIds.insert(merged.id);
            merged.order = nextOrder;
            ++nextOrder;
            m_state.notes.append(merged);
        }

        ensureAtLeastOneNote();
        refreshWindow();
        scheduleNextReminder();
        m_autoSaveCoordinator->requestSave();
        QMessageBox::information(m_mainWindow,
                                 QStringLiteral("导入完成"),
                                 QStringLiteral("已合并导入事项。"));
    }
}

void AppController::handleBackupSnapshotRequested() {
    if (m_mainWindow == nullptr) {
        return;
    }

    updateStateFromWindow();
    QString snapshotFilePath;
    QString errorMessage;
    if (!m_storageService.createBackupSnapshot(m_state, &snapshotFilePath, &errorMessage)) {
        QMessageBox::warning(m_mainWindow,
                             QStringLiteral("备份失败"),
                             QStringLiteral("创建备份快照失败。\n%1").arg(errorMessage));
        return;
    }

    QMessageBox::information(m_mainWindow,
                             QStringLiteral("备份完成"),
                             QStringLiteral("已创建备份快照：\n%1").arg(snapshotFilePath));
}

void AppController::handleRestoreLatestBackupRequested() {
    if (m_mainWindow == nullptr) {
        return;
    }

    if (!m_storageService.hasLatestBackup()) {
        QMessageBox::information(m_mainWindow,
                                 QStringLiteral("无可用备份"),
                                 QStringLiteral("当前没有可恢复的最近备份。"));
        return;
    }

    const auto choice = QMessageBox::question(m_mainWindow,
                                               QStringLiteral("确认恢复"),
                                               QStringLiteral("恢复最近备份将覆盖当前内存中的状态，是否继续？"),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }

    AppState restoredState;
    QString backupFilePath;
    QString errorMessage;
    if (!m_storageService.restoreFromLatestBackup(&restoredState, &backupFilePath, &errorMessage)) {
        QMessageBox::warning(m_mainWindow,
                             QStringLiteral("恢复失败"),
                             QStringLiteral("恢复最近备份失败。\n%1").arg(errorMessage));
        return;
    }

    m_state = restoredState;
    ensureAtLeastOneNote();
    applyStateToWindow();
    updateTrayMenuText();
    scheduleNextReminder();
    m_autoSaveCoordinator->requestSave();

    QMessageBox::information(m_mainWindow,
                             QStringLiteral("恢复完成"),
                             QStringLiteral("已从最近备份恢复：\n%1").arg(backupFilePath));
}

void AppController::handleExternalFileSyncToggled(bool enabled) {
    setExternalFileSyncEnabled(enabled);
}

void AppController::handleAlwaysOnTopToggled(bool enabled) {
    m_state.alwaysOnTop = enabled;
    if (m_mainWindow != nullptr) {
        m_mainWindow->setAlwaysOnTopEnabled(enabled);
    }
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleWindowLockToggled(bool enabled) {
    m_state.windowLocked = enabled;
    if (m_mainWindow != nullptr) {
        m_mainWindow->setWindowLocked(enabled);
    }
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleReminderSetRequested(const QString &noteId) {
    if (m_mainWindow == nullptr) {
        return;
    }

    for (NoteItem &note : m_state.notes) {
        if (note.id != noteId) {
            continue;
        }

        const qint64 nowEpochMsec = QDateTime::currentMSecsSinceEpoch();
        const QDateTime initialDateTime = note.reminderEpochMsec > nowEpochMsec
                                              ? QDateTime::fromMSecsSinceEpoch(note.reminderEpochMsec)
                                              : QDateTime::currentDateTime().addSecs(10 * 60);

        qint64 selectedEpochMsec = 0;
        if (!promptReminderDateTime(m_mainWindow, initialDateTime, &selectedEpochMsec)) {
            return;
        }

        if (selectedEpochMsec <= nowEpochMsec) {
            QMessageBox::warning(m_mainWindow,
                                 QStringLiteral("提醒时间无效"),
                                 QStringLiteral("请选择当前时间之后的提醒时间。"));
            return;
        }

        note.reminderEpochMsec = selectedEpochMsec;
        refreshWindow();
        scheduleNextReminder();
        m_autoSaveCoordinator->requestSave();
        return;
    }
}

void AppController::handleReminderClearedRequested(const QString &noteId) {
    for (NoteItem &note : m_state.notes) {
        if (note.id != noteId) {
            continue;
        }

        note.reminderEpochMsec = 0;
        refreshWindow();
        scheduleNextReminder();
        m_autoSaveCoordinator->requestSave();
        return;
    }
}

void AppController::handleReminderTimeout() {
    const qint64 nowEpochMsec = QDateTime::currentMSecsSinceEpoch();
    bool stateChanged = false;

    for (NoteItem &note : m_state.notes) {
        if (note.reminderEpochMsec <= 0) {
            continue;
        }

        if (note.reminderEpochMsec > nowEpochMsec + kReminderPollToleranceMsec) {
            continue;
        }

        const QString preview = reminderPreviewText(note);
        if (m_trayIcon != nullptr && m_trayIcon->isVisible()) {
            m_trayIcon->showMessage(QStringLiteral("glassNote 事项提醒"),
                                    preview,
                                    QSystemTrayIcon::Information,
                                    8000);
        } else if (m_mainWindow != nullptr) {
            QMessageBox::information(m_mainWindow,
                                     QStringLiteral("事项提醒"),
                                     preview);
        }

        note.reminderEpochMsec = 0;
        stateChanged = true;
    }

    if (stateChanged) {
        refreshWindow();
        m_autoSaveCoordinator->requestSave();
    }

    scheduleNextReminder();
}

void AppController::handleQuickCaptureRequested() {
    QString capturedText;
    if (!promptQuickCaptureText(m_mainWindow, m_state.uiStyle, &capturedText)) {
        return;
    }

    appendCapturedNote(capturedText);
}

void AppController::handleQuickCaptureDropRequested(const QString &text) {
    const QString droppedText = text.trimmed();
    if (droppedText.isEmpty()) {
        return;
    }

    QString capturedText;
    if (!promptQuickCaptureText(m_mainWindow, m_state.uiStyle, &capturedText, droppedText)) {
        return;
    }

    appendCapturedNote(capturedText);
}

void AppController::handleClipboardDataChanged() {
    if (!m_clipboardInboxEnabled || m_clipboard == nullptr) {
        return;
    }

    const QString clipboardText = m_clipboard->text(QClipboard::Clipboard);
    if (clipboardText == m_lastClipboardText) {
        return;
    }
    m_lastClipboardText = clipboardText;

    if (!hasMeaningfulText(clipboardText)) {
        return;
    }

    m_pendingClipboardInboxText = clipboardText;
    updateTrayMenuText();
}

void AppController::handleClipboardInboxImportRequested() {
    if (!m_clipboardInboxEnabled || m_pendingClipboardInboxText.trimmed().isEmpty()) {
        return;
    }

    if (appendCapturedNote(m_pendingClipboardInboxText)) {
        m_pendingClipboardInboxText.clear();
        updateTrayMenuText();
    }
}

void AppController::handleEdgeDropCaptureRequested(const QString &payload) {
    appendCapturedNote(payload);
}

void AppController::handleClipboardInboxToggled(bool enabled) {
    m_clipboardInboxEnabled = enabled;
    m_state.clipboardInboxEnabled = enabled;
    if (!enabled) {
        m_pendingClipboardInboxText.clear();
    } else if (m_clipboard != nullptr) {
        m_lastClipboardText = m_clipboard->text(QClipboard::Clipboard);
    }

    updateTrayMenuText();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleOcrExperimentalToggled(bool enabled) {
    m_ocrExperimentalEnabled = enabled;
    m_state.ocrExperimentalEnabled = enabled;
    updateTrayMenuText();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleOcrCaptureRequested() {
    if (m_mainWindow == nullptr) {
        return;
    }

    QMessageBox::information(m_mainWindow,
                             QStringLiteral("OCR 实验能力"),
                             QStringLiteral("OCR 仍处于实验阶段，当前版本仅提供入口开关。"));
}

void AppController::handleStorageFileChanged(const QString &filePath) {
    qCInfo(lcAppController) << "storage file changed" << filePath;

    if (!m_externalFileSyncEnabled || m_mainWindow == nullptr) {
        refreshStorageFileWatch();
        return;
    }

    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    if (nowMsec - m_lastInternalSaveMsec < 1200) {
        refreshStorageFileWatch();
        return;
    }

    const JsonStorageService::LoadResult loadResult = m_storageService.loadWithRecovery();
    qCInfo(lcAppController) << "external reload status" << static_cast<int>(loadResult.status)
                            << "message" << loadResult.message;
    m_state = loadResult.state;
    ensureAtLeastOneNote();
    applyStateToWindow();
    updateTrayMenuText();
    scheduleNextReminder();
    showLoadRecoveryMessage(loadResult, true);
    refreshStorageFileWatch();
}

void AppController::showLoadRecoveryMessage(const JsonStorageService::LoadResult &result, bool fromExternalSync) {
    if (m_mainWindow == nullptr) {
        return;
    }

    if (result.status == JsonStorageService::LoadStatus::RecoveredFromBackup) {
        qCWarning(lcAppController) << "recovered from backup" << result.message
                                   << "backup" << result.backupFilePath
                                   << "corrupt" << result.corruptFilePath;
        QString detail = result.message;
        if (!result.backupFilePath.isEmpty()) {
            detail += QStringLiteral("\n备份文件：%1").arg(result.backupFilePath);
        }
        if (!result.corruptFilePath.isEmpty()) {
            detail += QStringLiteral("\n已重命名损坏文件：%1").arg(result.corruptFilePath);
        }
        QMessageBox::warning(m_mainWindow,
                             fromExternalSync ? QStringLiteral("检测到外部数据异常") : QStringLiteral("启动恢复提示"),
                             detail);
        return;
    }

    if (result.status == JsonStorageService::LoadStatus::FallbackToDefault) {
        qCCritical(lcAppController) << "fallback to default state" << result.message;
        QMessageBox::critical(m_mainWindow,
                              fromExternalSync ? QStringLiteral("外部变更加载失败") : QStringLiteral("启动加载失败"),
                              QStringLiteral("已回退到默认数据。\n%1").arg(result.message));
    }
}

void AppController::refreshStorageFileWatch() {
    if (m_storageFileWatcher == nullptr) {
        return;
    }

    const QStringList watchedFiles = m_storageFileWatcher->files();
    if (!watchedFiles.isEmpty()) {
        m_storageFileWatcher->removePaths(watchedFiles);
    }

    if (!m_externalFileSyncEnabled) {
        return;
    }

    const QString storagePath = m_storageService.storageFilePath();
    if (QFileInfo::exists(storagePath)) {
        m_storageFileWatcher->addPath(storagePath);
    }
}

void AppController::setExternalFileSyncEnabled(bool enabled) {
    m_externalFileSyncEnabled = enabled;
    if (m_mainWindow != nullptr) {
        m_mainWindow->setExternalFileSyncEnabled(enabled);
    }
    refreshStorageFileWatch();
}

bool AppController::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType)

#ifdef Q_OS_WIN
    MSG *msg = static_cast<MSG *>(message);
    if (msg != nullptr && msg->message == WM_HOTKEY) {
        const int hotkeyId = static_cast<int>(msg->wParam);
        if (hotkeyId == kAddNoteHotkeyId) {
            if (m_mainWindow != nullptr && !m_mainWindow->isVisible()) {
                m_mainWindow->show();
            }
            handleAddNoteRequested();
            if (m_mainWindow != nullptr) {
                m_mainWindow->raise();
                m_mainWindow->activateWindow();
            }
            updateTrayMenuText();
            if (result != nullptr) {
                *result = 0;
            }
            return true;
        }

        if (hotkeyId == kQuickCaptureHotkeyId) {
            handleQuickCaptureRequested();
            if (result != nullptr) {
                *result = 0;
            }
            return true;
        }
    }
#else
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif

    return false;
}

void AppController::initializeSystemTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable() || m_trayIcon != nullptr) {
        return;
    }

    QIcon trayIcon = QApplication::windowIcon();
    if (trayIcon.isNull()) {
        trayIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    }

    m_trayMenu = new QMenu();
    QAction *toggleVisibilityAction = m_trayMenu->addAction(QStringLiteral("隐藏窗口"));
    toggleVisibilityAction->setObjectName(QStringLiteral("trayToggleVisibilityAction"));
    QAction *quickAddAction = m_trayMenu->addAction(QStringLiteral("快速新建事项"));
    QAction *quickCaptureAction = m_trayMenu->addAction(QStringLiteral("极速捕获（Ctrl+Alt+Q）"));
    m_trayClipboardImportAction = m_trayMenu->addAction(QStringLiteral("导入剪贴板收集箱（暂无内容）"));
    m_trayClipboardImportAction->setObjectName(QStringLiteral("trayClipboardImportAction"));
    m_trayMenu->addSeparator();
    m_trayClipboardInboxToggleAction = m_trayMenu->addAction(QStringLiteral("启用剪贴板收集箱"));
    m_trayClipboardInboxToggleAction->setCheckable(true);
    m_trayClipboardInboxToggleAction->setChecked(m_clipboardInboxEnabled);
    m_trayOcrToggleAction = m_trayMenu->addAction(QStringLiteral("启用 OCR（实验）"));
    m_trayOcrToggleAction->setCheckable(true);
    m_trayOcrToggleAction->setChecked(m_ocrExperimentalEnabled);
    QAction *ocrCaptureAction = m_trayMenu->addAction(QStringLiteral("OCR 图像捕获（实验）"));
    ocrCaptureAction->setObjectName(QStringLiteral("trayOcrCaptureAction"));
    m_trayMenu->addSeparator();
    QAction *quitAction = m_trayMenu->addAction(QStringLiteral("退出软件"));
    ThemeHelper::polishMenu(m_trayMenu, m_state.uiStyle);

    connect(toggleVisibilityAction, &QAction::triggered, this, &AppController::toggleMainWindowVisibility);
    connect(quickAddAction, &QAction::triggered, this, [this]() {
        if (m_mainWindow != nullptr && !m_mainWindow->isVisible()) {
            m_mainWindow->show();
        }
        handleAddNoteRequested();
        if (m_mainWindow != nullptr) {
            m_mainWindow->raise();
            m_mainWindow->activateWindow();
        }
        updateTrayMenuText();
    });
    connect(quickCaptureAction, &QAction::triggered, this, &AppController::handleQuickCaptureRequested);
    connect(m_trayClipboardImportAction,
            &QAction::triggered,
            this,
            &AppController::handleClipboardInboxImportRequested);
    connect(m_trayClipboardInboxToggleAction,
            &QAction::toggled,
            this,
            &AppController::handleClipboardInboxToggled);
    connect(m_trayOcrToggleAction, &QAction::toggled, this, &AppController::handleOcrExperimentalToggled);
    connect(ocrCaptureAction, &QAction::triggered, this, &AppController::handleOcrCaptureRequested);
    connect(quitAction, &QAction::triggered, this, &AppController::quitApplication);

    m_trayIcon = new QSystemTrayIcon(trayIcon, this);
    m_trayIcon->setToolTip(QStringLiteral("glassNote"));
    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            toggleMainWindowVisibility();
        }
    });
    m_trayIcon->show();
}

void AppController::updateTrayMenuText() {
    if (m_trayMenu == nullptr) {
        return;
    }

    ThemeHelper::polishMenu(m_trayMenu, m_state.uiStyle);

    QAction *toggleVisibilityAction = m_trayMenu->findChild<QAction *>(QStringLiteral("trayToggleVisibilityAction"));
    if (toggleVisibilityAction == nullptr) {
        return;
    }

    const bool mainWindowVisible = m_mainWindow != nullptr && m_mainWindow->isVisible();
    toggleVisibilityAction->setText(mainWindowVisible
                                        ? QStringLiteral("隐藏窗口")
                                        : QStringLiteral("显示窗口"));

    if (m_trayClipboardImportAction != nullptr) {
        QString preview = m_pendingClipboardInboxText.simplified();
        if (preview.size() > kClipboardInboxPreviewLength) {
            preview = preview.left(kClipboardInboxPreviewLength) + QStringLiteral("...");
        }

        if (preview.isEmpty()) {
            m_trayClipboardImportAction->setText(QStringLiteral("导入剪贴板收集箱（暂无内容）"));
            m_trayClipboardImportAction->setEnabled(false);
        } else {
            m_trayClipboardImportAction->setText(QStringLiteral("导入剪贴板：%1").arg(preview));
            m_trayClipboardImportAction->setEnabled(m_clipboardInboxEnabled);
        }
    }

    if (m_trayClipboardInboxToggleAction != nullptr) {
        m_trayClipboardInboxToggleAction->setChecked(m_clipboardInboxEnabled);
    }

    if (m_trayOcrToggleAction != nullptr) {
        m_trayOcrToggleAction->setChecked(m_ocrExperimentalEnabled);
    }

    QAction *ocrCaptureAction = m_trayMenu->findChild<QAction *>(QStringLiteral("trayOcrCaptureAction"));
    if (ocrCaptureAction != nullptr) {
        ocrCaptureAction->setEnabled(m_ocrExperimentalEnabled);
    }
}

void AppController::toggleMainWindowVisibility() {
    if (m_mainWindow == nullptr) {
        return;
    }

    if (m_mainWindow->isVisible()) {
        m_mainWindow->hide();
    } else {
        m_mainWindow->show();
        m_mainWindow->raise();
        m_mainWindow->activateWindow();
    }

    updateTrayMenuText();
}

void AppController::registerGlobalHotkey() {
#ifdef Q_OS_WIN
    if (!m_addHotkeyRegistered) {
        m_addHotkeyRegistered = RegisterHotKey(nullptr,
                                               kAddNoteHotkeyId,
                                               kAddNoteHotkeyModifiers,
                                               kAddNoteHotkeyVirtualKey) != 0;
    }

    if (!m_quickCaptureHotkeyRegistered) {
        m_quickCaptureHotkeyRegistered = RegisterHotKey(nullptr,
                                                        kQuickCaptureHotkeyId,
                                                        kQuickCaptureHotkeyModifiers,
                                                        kQuickCaptureHotkeyVirtualKey) != 0;
    }
#endif
}

void AppController::unregisterGlobalHotkey() {
#ifdef Q_OS_WIN
    if (m_addHotkeyRegistered) {
        UnregisterHotKey(nullptr, kAddNoteHotkeyId);
        m_addHotkeyRegistered = false;
    }

    if (m_quickCaptureHotkeyRegistered) {
        UnregisterHotKey(nullptr, kQuickCaptureHotkeyId);
        m_quickCaptureHotkeyRegistered = false;
    }
#endif
}

void AppController::scheduleNextReminder() {
    if (m_reminderTimer == nullptr) {
        return;
    }

    qint64 nearestEpochMsec = 0;
    for (const NoteItem &note : std::as_const(m_state.notes)) {
        if (note.reminderEpochMsec <= 0) {
            continue;
        }

        if (nearestEpochMsec <= 0 || note.reminderEpochMsec < nearestEpochMsec) {
            nearestEpochMsec = note.reminderEpochMsec;
        }
    }

    if (nearestEpochMsec <= 0) {
        m_reminderTimer->stop();
        return;
    }

    const qint64 nowEpochMsec = QDateTime::currentMSecsSinceEpoch();
    const qint64 dueAfterMsec = qMax<qint64>(1, nearestEpochMsec - nowEpochMsec);
    const int interval = static_cast<int>(qMin<qint64>(dueAfterMsec, kReminderMaxIntervalMsec));
    m_reminderTimer->start(interval);
}

QString AppController::reminderPreviewText(const NoteItem &note) const {
    QTextDocument document;
    if (Qt::mightBeRichText(note.text)) {
        document.setHtml(note.text);
    } else {
        document.setPlainText(note.text);
    }

    QString plainText = document.toPlainText().simplified();
    if (plainText.isEmpty()) {
        plainText = QStringLiteral("（空白事项）");
    }
    if (plainText.size() > 60) {
        plainText = plainText.left(60) + QStringLiteral("...");
    }

    return QStringLiteral("提醒：%1").arg(plainText);
}

void AppController::saveState() {
    updateStateFromWindow();
    QString errorMessage;
    if (!m_storageService.save(m_state, &errorMessage)) {
        qCCritical(lcAppController) << "save failed" << errorMessage;
        if (m_mainWindow != nullptr) {
            QMessageBox::warning(m_mainWindow,
                                 QStringLiteral("保存失败"),
                                 QStringLiteral("自动保存失败，请检查数据目录写入权限。"));
        }
        return;
    }

    qCDebug(lcAppController) << "save succeeded";

    m_lastInternalSaveMsec = QDateTime::currentMSecsSinceEpoch();
    refreshStorageFileWatch();
}

}  // namespace glassnote
