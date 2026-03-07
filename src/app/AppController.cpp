#include "app/AppController.h"

#include <QDateTime>
#include <QDate>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDateTimeEdit>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QProgressDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QAbstractButton>
#include <QApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QScreen>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QLoggingCategory>

#include <memory>

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
constexpr qint64 kAutoUpdateCheckMinIntervalMsec = 24LL * 60LL * 60LL * 1000LL;

const auto kDefaultUpdateManifestUrl = QStringLiteral(
    "https://github.com/kakuyo1/glassNote/releases/latest/download/update-manifest.json");
const auto kDefaultUpdatePageUrl = QStringLiteral("https://github.com/kakuyo1/glassNote/releases/latest");

#ifdef Q_OS_WIN
const auto kWindowsRunKeyPath = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const auto kWindowsRunValueName = QStringLiteral("glassNote");

QString startupCommandForCurrentExecutable() {
    const QString executablePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QStringLiteral("\"%1\"").arg(executablePath);
}

QString startupCommandExecutablePath(const QString &command) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    if (trimmed.startsWith(QLatin1Char('"'))) {
        const int endQuote = trimmed.indexOf(QLatin1Char('"'), 1);
        if (endQuote > 1) {
            return trimmed.mid(1, endQuote - 1);
        }
    }

    const int firstSpace = trimmed.indexOf(QLatin1Char(' '));
    return firstSpace > 0 ? trimmed.left(firstSpace) : trimmed;
}

bool launchAtStartupEnabledForCurrentExecutable() {
    QSettings settings(kWindowsRunKeyPath, QSettings::NativeFormat);
    const QString configuredCommand = settings.value(kWindowsRunValueName).toString();
    if (configuredCommand.trimmed().isEmpty()) {
        return false;
    }

    const QString configuredExecutable = QDir::fromNativeSeparators(
        startupCommandExecutablePath(configuredCommand));
    const QString currentExecutable = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
    if (configuredExecutable.isEmpty()) {
        return false;
    }

    return QFileInfo(configuredExecutable).absoluteFilePath().compare(
               QFileInfo(currentExecutable).absoluteFilePath(),
               Qt::CaseInsensitive) == 0;
}

bool setLaunchAtStartupEnabledForCurrentExecutable(bool enabled, QString *errorMessage) {
    QSettings settings(kWindowsRunKeyPath, QSettings::NativeFormat);
    if (enabled) {
        settings.setValue(kWindowsRunValueName, startupCommandForCurrentExecutable());
    } else {
        settings.remove(kWindowsRunValueName);
    }
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("写入开机启动配置失败。请检查系统权限。");
        }
        return false;
    }

    const bool actualEnabled = launchAtStartupEnabledForCurrentExecutable();
    if (actualEnabled != enabled) {
        if (errorMessage != nullptr) {
            *errorMessage = enabled
                                ? QStringLiteral("开机自启设置未生效，请稍后重试。")
                                : QStringLiteral("取消开机自启未生效，请稍后重试。");
        }
        return false;
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}
#endif

struct UpdateManifestPayload {
    QString version;
    QString notes;
    QUrl releasePageUrl;
    QUrl installerUrl;
    QString installerSha256;
    qint64 installerSize = 0;
};

bool isSha256Hex(const QString &value) {
    if (value.size() != 64) {
        return false;
    }

    for (QChar ch : value) {
        const ushort code = ch.unicode();
        const bool isDigit = code >= '0' && code <= '9';
        const bool isLowerHex = code >= 'a' && code <= 'f';
        const bool isUpperHex = code >= 'A' && code <= 'F';
        if (!isDigit && !isLowerHex && !isUpperHex) {
            return false;
        }
    }

    return true;
}

QString configuredUpdateManifestUrl() {
    const QString overrideValue = qEnvironmentVariable("GLASSNOTE_UPDATE_MANIFEST_URL").trimmed();
    return overrideValue.isEmpty() ? kDefaultUpdateManifestUrl : overrideValue;
}

QString normalizedVersionToken(const QString &value) {
    QString token = value.trimmed();
    if (token.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        token = token.mid(1);
    }
    return token;
}

QVector<int> versionSegments(const QString &version) {
    QVector<int> segments;
    const QString normalized = normalizedVersionToken(version);
    const QStringList parts = normalized.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    segments.reserve(parts.size());
    for (const QString &part : parts) {
        int numeric = 0;
        bool hasDigit = false;
        for (QChar ch : part) {
            if (!ch.isDigit()) {
                break;
            }
            hasDigit = true;
            numeric = (numeric * 10) + ch.digitValue();
        }
        segments.append(hasDigit ? numeric : 0);
    }
    return segments;
}

int compareVersionStrings(const QString &left, const QString &right) {
    const QVector<int> leftSegments = versionSegments(left);
    const QVector<int> rightSegments = versionSegments(right);
    const int segmentCount = qMax(leftSegments.size(), rightSegments.size());
    for (int index = 0; index < segmentCount; ++index) {
        const int lhs = index < leftSegments.size() ? leftSegments.at(index) : 0;
        const int rhs = index < rightSegments.size() ? rightSegments.at(index) : 0;
        if (lhs < rhs) {
            return -1;
        }
        if (lhs > rhs) {
            return 1;
        }
    }
    return 0;
}

QUrl manifestUrlValue(const QJsonObject &root,
                      const QString &primaryKey,
                      const QString &secondaryKey = QString()) {
    const QString value = root.value(primaryKey).toString().trimmed();
    if (!value.isEmpty()) {
        return QUrl(value);
    }
    if (!secondaryKey.isEmpty()) {
        const QString fallbackValue = root.value(secondaryKey).toString().trimmed();
        if (!fallbackValue.isEmpty()) {
            return QUrl(fallbackValue);
        }
    }
    return QUrl();
}

QString manifestNotesValue(const QJsonObject &root) {
    const QJsonValue notesValue = root.value(QStringLiteral("notes"));
    if (notesValue.isString()) {
        return notesValue.toString().trimmed();
    }

    if (notesValue.isArray()) {
        QStringList lines;
        const QJsonArray entries = notesValue.toArray();
        lines.reserve(entries.size());
        for (const QJsonValue &entry : entries) {
            const QString text = entry.toString().trimmed();
            if (!text.isEmpty()) {
                lines.append(text);
            }
        }
        return lines.join(QLatin1Char('\n'));
    }

    return QString();
}

bool parseUpdateManifestPayload(const QByteArray &payload,
                                UpdateManifestPayload *manifest,
                                QString *errorMessage) {
    if (manifest == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("更新数据解析目标为空。");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("更新数据格式无效：%1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QString remoteVersion = root.value(QStringLiteral("version")).toString().trimmed();
    if (remoteVersion.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("更新数据缺少版本号。");
        }
        return false;
    }

    const QString normalizedRemoteVersion = normalizedVersionToken(remoteVersion);
    if (normalizedRemoteVersion.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("更新版本号格式无效。");
        }
        return false;
    }

    QUrl releaseUrl = manifestUrlValue(root, QStringLiteral("releasePageUrl"), QStringLiteral("downloadUrl"));

    const QJsonObject windowsObject = root.value(QStringLiteral("windows")).toObject();
    const QJsonObject x64Object = windowsObject.value(QStringLiteral("x64")).toObject();

    QString installerUrlText = x64Object.value(QStringLiteral("installerUrl")).toString().trimmed();
    if (installerUrlText.isEmpty()) {
        installerUrlText = root.value(QStringLiteral("installerUrl")).toString().trimmed();
    }

    if (installerUrlText.isEmpty()) {
        const QString legacyDownloadUrl = root.value(QStringLiteral("downloadUrl")).toString().trimmed();
        if (legacyDownloadUrl.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            installerUrlText = legacyDownloadUrl;
        }
    }

    QUrl installerUrl;
    if (!installerUrlText.isEmpty()) {
        installerUrl = QUrl(installerUrlText);
        if (!installerUrl.isValid()
            || installerUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("安装包链接无效（仅支持 HTTPS）。");
            }
            return false;
        }
    }

    QString installerSha256 = x64Object.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    if (installerSha256.isEmpty()) {
        installerSha256 = root.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    }

    if (!installerSha256.isEmpty() && !isSha256Hex(installerSha256)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("安装包 SHA256 格式无效。");
        }
        return false;
    }

    qint64 installerSize = static_cast<qint64>(x64Object.value(QStringLiteral("size")).toDouble(0.0));
    if (installerSize <= 0) {
        installerSize = static_cast<qint64>(root.value(QStringLiteral("size")).toDouble(0.0));
    }

    if ((!releaseUrl.isValid() || releaseUrl.isEmpty()) && installerUrl.isValid()) {
        releaseUrl = installerUrl;
    }

    if (!releaseUrl.isValid() || releaseUrl.isEmpty()) {
        releaseUrl = QUrl(kDefaultUpdatePageUrl);
    }

    if (!releaseUrl.isValid() || releaseUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("更新链接无效（仅支持 HTTPS）。");
        }
        return false;
    }

    manifest->version = normalizedRemoteVersion;
    manifest->notes = manifestNotesValue(root);
    manifest->releasePageUrl = releaseUrl;
    manifest->installerUrl = installerUrl;
    manifest->installerSha256 = installerSha256;
    manifest->installerSize = installerSize > 0 ? installerSize : 0;
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

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

QDate latestSnapshotDate(const QVector<DailyTimelineSnapshot> &snapshots) {
    QDate latestDate;
    for (const DailyTimelineSnapshot &snapshot : snapshots) {
        const QDate parsedDate = QDate::fromString(snapshot.dateKey, QStringLiteral("yyyy-MM-dd"));
        if (!parsedDate.isValid()) {
            continue;
        }
        if (!latestDate.isValid() || parsedDate > latestDate) {
            latestDate = parsedDate;
        }
    }
    return latestDate;
}

QString timelineSnapshotHintText(const QVector<DailyTimelineSnapshot> &snapshots) {
    QDate oldestDate;
    QDate latestDate;
    int validSnapshotCount = 0;

    for (const DailyTimelineSnapshot &snapshot : snapshots) {
        const QDate parsedDate = QDate::fromString(snapshot.dateKey, QStringLiteral("yyyy-MM-dd"));
        if (!parsedDate.isValid()) {
            continue;
        }

        ++validSnapshotCount;
        if (!oldestDate.isValid() || parsedDate < oldestDate) {
            oldestDate = parsedDate;
        }
        if (!latestDate.isValid() || parsedDate > latestDate) {
            latestDate = parsedDate;
        }
    }

    if (validSnapshotCount <= 0 || !oldestDate.isValid() || !latestDate.isValid()) {
        return QStringLiteral("当前暂无历史快照，回放前请先保存至少一天的事项。\n"
                              "若所选日期无快照，将自动回放该日期之前最近的一次快照。");
    }

    return QStringLiteral("已记录快照范围：%1 到 %2（%3 条）。\n"
                          "若所选日期无快照，将自动回放该日期之前最近的一次快照。")
        .arg(oldestDate.toString(QStringLiteral("yyyy-MM-dd")),
             latestDate.toString(QStringLiteral("yyyy-MM-dd"))
             , QString::number(validSnapshotCount));
}

bool promptTimelineReplayDate(QWidget *parent, const AppState &state, QString *selectedDateKey) {
    if (selectedDateKey == nullptr) {
        return false;
    }

    const QDate fallbackDate = latestSnapshotDate(state.timelineSnapshots);
    const QDate initialDate = fallbackDate.isValid() ? fallbackDate : QDate::currentDate();

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("时间轴回放（按日期）"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *hintLabel = new QLabel(QStringLiteral("选择要回放的日期："), &dialog);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto *rangeLabel = new QLabel(timelineSnapshotHintText(state.timelineSnapshots), &dialog);
    rangeLabel->setWordWrap(true);
    layout->addWidget(rangeLabel);

    auto *dateEdit = new QDateEdit(&dialog);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dateEdit->setDate(initialDate);
    layout->addWidget(dateEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (QAbstractButton *okButton = buttons->button(QDialogButtonBox::Ok)) {
        okButton->setText(QStringLiteral("回放"));
    }
    if (QAbstractButton *cancelButton = buttons->button(QDialogButtonBox::Cancel)) {
        cancelButton->setText(QStringLiteral("取消"));
    }
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    *selectedDateKey = dateEdit->date().toString(QStringLiteral("yyyy-MM-dd"));
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
    const bool pixelStyle = uiStyle == UiStyle::Pixel;
    const int dialogRadius = pixelStyle ? 3 : 16;
    const int controlRadius = pixelStyle ? 1 : 9;
    const QString terminalFont = pixelStyle
                                     ? QStringLiteral("font-family: 'Consolas', 'Courier New', monospace;")
                                     : QString();

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
                       "%12"
                       "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %2,stop:1 %3);"
                       "}"
                       "QLabel#quickCaptureTitle {"
                       "%12"
                       "color: %4;"
                       "}"
                       "QLabel#quickCaptureHint {"
                       "%12"
                       "color: %5;"
                       "}"
                       "QLineEdit {"
                       "%12"
                       "color: %4;"
                       "background: %6;"
                       "border: 1px solid %1;"
                       "border-radius: %11px;"
                       "padding: 6px 10px;"
                       "selection-background-color: %7;"
                       "selection-color: %8;"
                       "}"
                       "QLineEdit:focus {"
                       "border: 1px solid %7;"
                       "}"
                       "QPushButton {"
                       "%12"
                       "color: %4;"
                       "border-radius: %11px;"
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
            .arg(dialogRadius)
            .arg(controlRadius)
            .arg(terminalFont));

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
    m_updateNetworkManager = new QNetworkAccessManager(this);
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
#ifdef Q_OS_WIN
    m_state.launchAtStartup = launchAtStartupEnabledForCurrentExecutable();
#else
    m_state.launchAtStartup = false;
#endif

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
    connect(m_mainWindow,
            &MainWindow::noteStickerChangeRequested,
            this,
            &AppController::handleNoteStickerChangeRequested);
    connect(m_mainWindow, &MainWindow::noteLaneChangeRequested, this, &AppController::handleNoteLaneChangeRequested);
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
    connect(m_mainWindow,
            &MainWindow::launchAtStartupToggled,
            this,
            &AppController::handleLaunchAtStartupToggled);
    connect(m_mainWindow,
            &MainWindow::autoCheckUpdatesToggled,
            this,
            &AppController::handleAutoCheckUpdatesToggled);
    connect(m_mainWindow,
            &MainWindow::checkForUpdatesRequested,
            this,
            &AppController::handleCheckForUpdatesRequested);
    connect(m_mainWindow, &MainWindow::windowLockToggled, this, &AppController::handleWindowLockToggled);
    connect(m_mainWindow, &MainWindow::reminderSetRequested, this, &AppController::handleReminderSetRequested);
    connect(m_mainWindow,
            &MainWindow::reminderClearedRequested,
            this,
            &AppController::handleReminderClearedRequested);
    connect(m_mainWindow, &MainWindow::timelineReplayRequested, this, &AppController::handleTimelineReplayRequested);
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
    scheduleAutomaticUpdateCheck();
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
    m_mainWindow->setLaunchAtStartupEnabled(m_state.launchAtStartup);
    m_mainWindow->setAutoCheckUpdatesEnabled(m_state.autoCheckUpdates);
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

void AppController::handleNoteStickerChangeRequested(const QString &noteId, const QString &sticker) {
    for (NoteItem &note : m_state.notes) {
        if (note.id == noteId) {
            note.sticker = sticker;
            break;
        }
    }

    refreshWindow();
    m_autoSaveCoordinator->requestSave();
}

void AppController::handleNoteLaneChangeRequested(const QString &noteId, NoteLane lane) {
    const NoteLane normalizedLane = normalizedNoteLane(lane);
    for (NoteItem &note : m_state.notes) {
        if (note.id == noteId) {
            note.lane = normalizedLane;
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

void AppController::handleLaunchAtStartupToggled(bool enabled) {
#ifdef Q_OS_WIN
    QString errorMessage;
    if (!setLaunchAtStartupEnabledForCurrentExecutable(enabled, &errorMessage)) {
        qCWarning(lcAppController) << "set launch at startup failed" << errorMessage;
        m_state.launchAtStartup = launchAtStartupEnabledForCurrentExecutable();
        refreshWindow();
        if (m_mainWindow != nullptr) {
            QMessageBox::warning(m_mainWindow,
                                 QStringLiteral("开机自启设置失败"),
                                 errorMessage.isEmpty()
                                     ? QStringLiteral("无法更新开机自启状态，请稍后重试。")
                                     : errorMessage);
        }
        return;
    }

    m_state.launchAtStartup = launchAtStartupEnabledForCurrentExecutable();
    refreshWindow();
    m_autoSaveCoordinator->requestSave();
#else
    Q_UNUSED(enabled)
    m_state.launchAtStartup = false;
    refreshWindow();
    if (m_mainWindow != nullptr) {
        QMessageBox::information(m_mainWindow,
                                 QStringLiteral("开机自启"),
                                 QStringLiteral("当前平台暂不支持开机自启设置。"));
    }
#endif
}

void AppController::handleAutoCheckUpdatesToggled(bool enabled) {
    if (m_state.autoCheckUpdates == enabled) {
        return;
    }

    m_state.autoCheckUpdates = enabled;
    refreshWindow();
    m_autoSaveCoordinator->requestSave();

    if (enabled) {
        scheduleAutomaticUpdateCheck();
    }
}

void AppController::handleCheckForUpdatesRequested() {
    requestUpdateCheck(true);
}

void AppController::scheduleAutomaticUpdateCheck() {
    if (!m_state.autoCheckUpdates) {
        return;
    }

    QTimer::singleShot(10 * 1000, this, [this]() {
        requestUpdateCheck(false);
    });
}

void AppController::requestUpdateCheck(bool manual) {
    if (m_updateNetworkManager == nullptr) {
        if (manual && m_mainWindow != nullptr) {
            QMessageBox::warning(m_mainWindow,
                                 QStringLiteral("检查更新失败"),
                                 QStringLiteral("更新服务未初始化，请重启后重试。"));
        }
        return;
    }

    if (m_updateCheckInProgress) {
        if (manual && m_mainWindow != nullptr) {
            QMessageBox::information(m_mainWindow,
                                     QStringLiteral("检查更新"),
                                     QStringLiteral("正在检查更新，请稍候。"));
        }
        return;
    }

    const qint64 nowEpochMsec = QDateTime::currentMSecsSinceEpoch();
    if (!manual && (nowEpochMsec - m_state.lastUpdateCheckEpochMsec) < kAutoUpdateCheckMinIntervalMsec) {
        return;
    }

    const QUrl manifestUrl(configuredUpdateManifestUrl());
    if (!manifestUrl.isValid() || manifestUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        qCWarning(lcAppController) << "invalid update manifest url" << manifestUrl;
        if (manual && m_mainWindow != nullptr) {
            QMessageBox::warning(m_mainWindow,
                                 QStringLiteral("检查更新失败"),
                                 QStringLiteral("更新地址配置无效（仅支持 HTTPS）。"));
        }
        return;
    }

    m_updateCheckInProgress = true;
    m_state.lastUpdateCheckEpochMsec = nowEpochMsec;
    m_autoSaveCoordinator->requestSave();

    QNetworkRequest request(manifestUrl);
    const QString appVersion = normalizedVersionToken(QCoreApplication::applicationVersion());
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("glassNote/%1").arg(appVersion.isEmpty() ? QStringLiteral("dev") : appVersion));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_updateNetworkManager->get(request);
    reply->setProperty("manualCheck", manual);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const bool manualCheck = reply->property("manualCheck").toBool();
        m_updateCheckInProgress = false;

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcAppController) << "update check network error" << reply->errorString();
            if (manualCheck && m_mainWindow != nullptr) {
                QMessageBox::warning(m_mainWindow,
                                     QStringLiteral("检查更新失败"),
                                     QStringLiteral("请求更新信息失败：%1").arg(reply->errorString()));
            }
            reply->deleteLater();
            return;
        }

        UpdateManifestPayload manifest;
        QString parseError;
        if (!parseUpdateManifestPayload(reply->readAll(), &manifest, &parseError)) {
            qCWarning(lcAppController) << "update manifest parse error" << parseError;
            if (manualCheck && m_mainWindow != nullptr) {
                QMessageBox::warning(m_mainWindow,
                                     QStringLiteral("检查更新失败"),
                                     QStringLiteral("更新信息解析失败：%1").arg(parseError));
            }
            reply->deleteLater();
            return;
        }

        const QString currentVersion = normalizedVersionToken(QCoreApplication::applicationVersion());
        const QString localVersion = currentVersion.isEmpty() ? QStringLiteral("0.0.0") : currentVersion;
        const int versionCompare = compareVersionStrings(localVersion, manifest.version);
        if (versionCompare >= 0) {
            if (manualCheck && m_mainWindow != nullptr) {
                QMessageBox::information(m_mainWindow,
                                         QStringLiteral("检查更新"),
                                         QStringLiteral("当前已是最新版本（%1）。").arg(localVersion));
            }
            reply->deleteLater();
            return;
        }

        if (!manualCheck && m_state.ignoredUpdateVersion == manifest.version) {
            qCInfo(lcAppController) << "skip ignored update version" << manifest.version;
            reply->deleteLater();
            return;
        }

        if (m_mainWindow == nullptr) {
            reply->deleteLater();
            return;
        }

        QMessageBox dialog(m_mainWindow);
        dialog.setIcon(QMessageBox::Information);
        dialog.setWindowTitle(QStringLiteral("发现新版本"));
        dialog.setText(QStringLiteral("检测到新版本：%1（当前 %2）")
                           .arg(manifest.version, localVersion));

        QString informativeText;
        if (m_state.ignoredUpdateVersion == manifest.version) {
            informativeText = QStringLiteral("该版本此前已被忽略。\n");
        }
        const bool canDownloadInApp = manifest.installerUrl.isValid() && !manifest.installerSha256.isEmpty();
        informativeText += canDownloadInApp
                               ? QStringLiteral("可直接在应用内下载更新包，完成 SHA256 校验后启动安装器。")
                               : QStringLiteral("该版本未提供应用内安装信息，点击“前往下载页”可手动更新。");
        if (!manifest.notes.isEmpty()) {
            informativeText += QStringLiteral("\n\n更新说明：\n%1").arg(manifest.notes);
        }
        dialog.setInformativeText(informativeText);

        QPushButton *downloadInstallButton = nullptr;
        if (canDownloadInApp) {
            downloadInstallButton = dialog.addButton(QStringLiteral("下载并安装"), QMessageBox::AcceptRole);
        }
        QPushButton *openPageButton = dialog.addButton(QStringLiteral("前往下载页"), QMessageBox::ActionRole);
        QPushButton *ignoreButton = dialog.addButton(QStringLiteral("忽略此版本"), QMessageBox::DestructiveRole);
        dialog.addButton(QStringLiteral("稍后"), QMessageBox::RejectRole);
        dialog.exec();

        QAbstractButton *clicked = dialog.clickedButton();
        if (downloadInstallButton != nullptr
            && clicked == static_cast<QAbstractButton *>(downloadInstallButton)) {
            reply->deleteLater();
            downloadAndInstallUpdate(manifest.version, manifest.installerUrl, manifest.installerSha256);
            return;
        }

        if (clicked == static_cast<QAbstractButton *>(openPageButton)) {
            QDesktopServices::openUrl(manifest.releasePageUrl);
            reply->deleteLater();
            return;
        }

        if (clicked == static_cast<QAbstractButton *>(ignoreButton)) {
            m_state.ignoredUpdateVersion = manifest.version;
            m_autoSaveCoordinator->requestSave();
        }

        reply->deleteLater();
    });
}

void AppController::downloadAndInstallUpdate(const QString &version,
                                             const QUrl &installerUrl,
                                             const QString &expectedSha256) {
    if (m_mainWindow == nullptr || m_updateNetworkManager == nullptr) {
        return;
    }

    const QString normalizedExpectedSha256 = expectedSha256.trimmed().toLower();
    if (!installerUrl.isValid()
        || installerUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || !isSha256Hex(normalizedExpectedSha256)) {
        QMessageBox::warning(m_mainWindow,
                             QStringLiteral("下载安装失败"),
                             QStringLiteral("安装包链接或校验信息无效。"));
        return;
    }

    QString tempBasePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempBasePath.trimmed().isEmpty()) {
        tempBasePath = QDir::tempPath();
    }

    const QDir tempBaseDir(tempBasePath);
    const QString updateDirPath = tempBaseDir.filePath(QStringLiteral("glassNote-updater"));
    QDir updateDir(updateDirPath);
    if (!updateDir.exists() && !updateDir.mkpath(QStringLiteral("."))) {
        QMessageBox::warning(m_mainWindow,
                             QStringLiteral("下载安装失败"),
                             QStringLiteral("无法创建更新缓存目录。"));
        return;
    }

    QString safeVersion = normalizedVersionToken(version);
    if (safeVersion.isEmpty()) {
        safeVersion = QStringLiteral("latest");
    }
    for (QChar &ch : safeVersion) {
        if (!(ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('-') || ch == QLatin1Char('_'))) {
            ch = QLatin1Char('_');
        }
    }

    const QString installerFileName = QStringLiteral("glassNote-%1-win64-setup.exe").arg(safeVersion);
    const QString installerPath = updateDir.filePath(installerFileName);
    QFile::remove(installerPath);

    QNetworkRequest request(installerUrl);
    const QString appVersion = normalizedVersionToken(QCoreApplication::applicationVersion());
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("glassNote/%1 updater").arg(appVersion.isEmpty() ? QStringLiteral("dev") : appVersion));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_updateNetworkManager->get(request);
    auto *downloadFile = new QSaveFile(installerPath, reply);
    if (!downloadFile->open(QIODevice::WriteOnly)) {
        const QString errorMessage = downloadFile->errorString();
        delete downloadFile;
        reply->abort();
        reply->deleteLater();
        QMessageBox::warning(m_mainWindow,
                             QStringLiteral("下载安装失败"),
                             QStringLiteral("无法写入更新文件：%1").arg(errorMessage));
        return;
    }

    auto downloadHash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto *progressDialog = new QProgressDialog(QStringLiteral("正在下载更新包 %1 ...").arg(safeVersion),
                                               QStringLiteral("取消"),
                                               0,
                                               100,
                                               m_mainWindow);
    progressDialog->setWindowTitle(QStringLiteral("下载更新"));
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setMinimumDuration(0);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->setValue(0);
    progressDialog->show();

    connect(progressDialog, &QProgressDialog::canceled, reply, [reply]() {
        reply->abort();
    });

    connect(reply, &QNetworkReply::readyRead, reply, [reply, downloadFile, downloadHash]() {
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty()) {
            return;
        }

        downloadHash->addData(chunk);
        const qint64 written = downloadFile->write(chunk);
        if (written != chunk.size()) {
            reply->setProperty("writeFailed", true);
            reply->setProperty("writeError", downloadFile->errorString());
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::downloadProgress, progressDialog, [progressDialog](qint64 received, qint64 total) {
        if (total > 0) {
            const int percent = static_cast<int>((received * 100) / total);
            if (progressDialog->maximum() != 100) {
                progressDialog->setRange(0, 100);
            }
            progressDialog->setValue(qBound(0, percent, 100));
        } else {
            progressDialog->setRange(0, 0);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this,
                                                     reply,
                                                     downloadFile,
                                                     downloadHash,
                                                     progressDialog,
                                                     installerPath,
                                                     normalizedExpectedSha256]() {
        const bool writeFailed = reply->property("writeFailed").toBool();
        const bool canceled = progressDialog->wasCanceled() || reply->error() == QNetworkReply::OperationCanceledError;

        if (reply->error() != QNetworkReply::NoError || writeFailed) {
            downloadFile->cancelWriting();
            QFile::remove(installerPath);

            if (!canceled && m_mainWindow != nullptr) {
                const QString writeError = reply->property("writeError").toString().trimmed();
                const QString detail = writeFailed
                                           ? (writeError.isEmpty()
                                                  ? QStringLiteral("写入安装包文件失败。")
                                                  : QStringLiteral("写入安装包失败：%1").arg(writeError))
                                           : QStringLiteral("下载更新包失败：%1").arg(reply->errorString());
                QMessageBox::warning(m_mainWindow,
                                     QStringLiteral("下载安装失败"),
                                     detail);
            }

            progressDialog->deleteLater();
            reply->deleteLater();
            return;
        }

        if (!downloadFile->commit()) {
            const QString commitError = downloadFile->errorString();
            QFile::remove(installerPath);
            if (m_mainWindow != nullptr) {
                QMessageBox::warning(m_mainWindow,
                                     QStringLiteral("下载安装失败"),
                                     QStringLiteral("保存安装包失败：%1").arg(commitError));
            }
            progressDialog->deleteLater();
            reply->deleteLater();
            return;
        }

        const QString actualSha256 = QString::fromLatin1(downloadHash->result().toHex()).toLower();
        if (actualSha256 != normalizedExpectedSha256) {
            QFile::remove(installerPath);
            if (m_mainWindow != nullptr) {
                QMessageBox::warning(m_mainWindow,
                                     QStringLiteral("校验失败"),
                                     QStringLiteral("安装包 SHA256 校验失败，已删除下载文件。\n期望：%1\n实际：%2")
                                         .arg(normalizedExpectedSha256, actualSha256));
            }
            progressDialog->deleteLater();
            reply->deleteLater();
            return;
        }

        progressDialog->setRange(0, 100);
        progressDialog->setValue(100);
        progressDialog->deleteLater();

        if (m_mainWindow == nullptr) {
            reply->deleteLater();
            return;
        }

        const auto installChoice = QMessageBox::question(
            m_mainWindow,
            QStringLiteral("更新包已下载"),
            QStringLiteral("更新包已下载并校验完成，是否立即启动安装器？\n%1")
                .arg(QDir::toNativeSeparators(installerPath)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

        if (installChoice != QMessageBox::Yes) {
            reply->deleteLater();
            return;
        }

        const bool started = QProcess::startDetached(QDir::toNativeSeparators(installerPath),
                                                     QStringList(),
                                                     QFileInfo(installerPath).absolutePath());
        if (!started) {
            QMessageBox::warning(m_mainWindow,
                                 QStringLiteral("启动安装器失败"),
                                 QStringLiteral("无法启动更新安装器，请手动运行：\n%1")
                                     .arg(QDir::toNativeSeparators(installerPath)));
            reply->deleteLater();
            return;
        }

        reply->deleteLater();
        quitApplication();
    });
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

void AppController::handleTimelineReplayRequested() {
    if (m_mainWindow == nullptr) {
        return;
    }

    QString dateKey;
    if (!promptTimelineReplayDate(m_mainWindow, m_state, &dateKey)) {
        return;
    }

    QVector<NoteItem> resolvedNotes;
    QString resolvedDateKey;
    if (!appstate::resolveTimelineNotesForDate(m_state, dateKey, &resolvedNotes, &resolvedDateKey)) {
        QMessageBox::information(m_mainWindow,
                                 QStringLiteral("时间轴回放"),
                                 QStringLiteral("未找到可回放快照，请先保存至少一天的事项。"));
        return;
    }

    const QString confirmText = resolvedDateKey == dateKey
                                    ? QStringLiteral("将回放 %1 的快照并覆盖当前事项，是否继续？").arg(resolvedDateKey)
                                    : QStringLiteral("%1 无快照，将回放最近历史快照 %2 并覆盖当前事项，是否继续？")
                                          .arg(dateKey, resolvedDateKey);
    QMessageBox confirmBox(QMessageBox::Question,
                           QStringLiteral("确认时间轴回放"),
                           confirmText,
                           QMessageBox::NoButton,
                           m_mainWindow);
    QPushButton *replayButton = confirmBox.addButton(QStringLiteral("开始回放"), QMessageBox::AcceptRole);
    confirmBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    confirmBox.setDefaultButton(replayButton);
    confirmBox.exec();
    if (confirmBox.clickedButton() != replayButton) {
        return;
    }

    m_state.notes = resolvedNotes;
    ensureAtLeastOneNote();
    refreshWindow();
    scheduleNextReminder();
    m_autoSaveCoordinator->requestSave();

    if (resolvedDateKey == dateKey) {
        QMessageBox::information(m_mainWindow,
                                 QStringLiteral("时间轴回放"),
                                 QStringLiteral("已回放 %1 的快照。").arg(resolvedDateKey));
        return;
    }

    QMessageBox::information(m_mainWindow,
                             QStringLiteral("时间轴回放"),
                             QStringLiteral("%1 无快照，已回放最近历史快照：%2。")
                                 .arg(dateKey, resolvedDateKey));
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
    QAction *checkUpdatesAction = m_trayMenu->addAction(QStringLiteral("检查更新..."));
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
    connect(checkUpdatesAction, &QAction::triggered, this, &AppController::handleCheckForUpdatesRequested);
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
    appstate::recordDailyTimelineSnapshot(&m_state,
                                          QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
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
