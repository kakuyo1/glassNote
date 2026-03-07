#include "storage/JsonStorageService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include "common/Constants.h"
#include "model/UiStyle.h"

namespace glassnote {

namespace {

Q_LOGGING_CATEGORY(lcStorage, "glassnote.storage")

constexpr int kStorageVersion = 1;
const auto kLatestBackupFileName = QStringLiteral("latest.json");

QString uiStyleToStorageValue(UiStyle uiStyle) {
    switch (uiStyle) {
    case UiStyle::Glass:
        return QStringLiteral("glass");
    case UiStyle::Mist:
        return QStringLiteral("mist");
    case UiStyle::Sunrise:
        return QStringLiteral("sunrise");
    case UiStyle::Meadow:
        return QStringLiteral("meadow");
    case UiStyle::Graphite:
        return QStringLiteral("graphite");
    case UiStyle::Paper:
        return QStringLiteral("paper");
    case UiStyle::Pixel:
        return QStringLiteral("pixel");
    case UiStyle::Neon:
        return QStringLiteral("neon");
    case UiStyle::Clay:
        return QStringLiteral("clay");
    }
    return QStringLiteral("glass");
}

UiStyle uiStyleFromStorageValue(const QJsonValue &value) {
    if (value.isString()) {
        const QString style = value.toString().trimmed().toLower();
        if (style == QStringLiteral("mist")) {
            return UiStyle::Mist;
        }
        if (style == QStringLiteral("sunrise")) {
            return UiStyle::Sunrise;
        }
        if (style == QStringLiteral("meadow")) {
            return UiStyle::Meadow;
        }
        if (style == QStringLiteral("graphite")) {
            return UiStyle::Graphite;
        }
        if (style == QStringLiteral("paper")) {
            return UiStyle::Paper;
        }
        if (style == QStringLiteral("pixel")) {
            return UiStyle::Pixel;
        }
        if (style == QStringLiteral("neon")) {
            return UiStyle::Neon;
        }
        if (style == QStringLiteral("clay")) {
            return UiStyle::Clay;
        }
        return UiStyle::Glass;
    }

    const int numeric = value.toInt(0);
    switch (numeric) {
    case static_cast<int>(UiStyle::Mist):
        return UiStyle::Mist;
    case static_cast<int>(UiStyle::Sunrise):
        return UiStyle::Sunrise;
    case static_cast<int>(UiStyle::Meadow):
        return UiStyle::Meadow;
    case static_cast<int>(UiStyle::Graphite):
        return UiStyle::Graphite;
    case static_cast<int>(UiStyle::Paper):
        return UiStyle::Paper;
    case static_cast<int>(UiStyle::Pixel):
        return UiStyle::Pixel;
    case static_cast<int>(UiStyle::Neon):
        return UiStyle::Neon;
    case static_cast<int>(UiStyle::Clay):
        return UiStyle::Clay;
    default:
        return UiStyle::Glass;
    }
}

QJsonObject noteToJson(const NoteItem &note) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), note.id);
    object.insert(QStringLiteral("text"), note.text);
    object.insert(QStringLiteral("order"), note.order);
    object.insert(QStringLiteral("hue"), note.hue);
    object.insert(QStringLiteral("reminderEpochMsec"), note.reminderEpochMsec);
    return object;
}

NoteItem noteFromJson(const QJsonObject &object, int fallbackOrder) {
    NoteItem note;
    note.id = object.value(QStringLiteral("id")).toString();
    if (note.id.isEmpty()) {
        note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    note.text = object.value(QStringLiteral("text")).toString();
    note.order = object.value(QStringLiteral("order")).toInt(fallbackOrder);
    note.hue = object.value(QStringLiteral("hue")).toInt(-1);

    const QJsonValue reminderValue = object.value(QStringLiteral("reminderEpochMsec"));
    if (reminderValue.isString()) {
        note.reminderEpochMsec = reminderValue.toString().toLongLong();
    } else {
        note.reminderEpochMsec = static_cast<qint64>(reminderValue.toDouble(0.0));
    }
    if (note.reminderEpochMsec < 0) {
        note.reminderEpochMsec = 0;
    }

    return note;
}

QJsonObject stateToJson(const AppState &state) {
    QJsonObject root;
    root.insert(QStringLiteral("version"), kStorageVersion);
    root.insert(QStringLiteral("uiScale"), state.uiScale);
    root.insert(QStringLiteral("baseLayerOpacity"), state.baseLayerOpacity);
    root.insert(QStringLiteral("uiStyle"), uiStyleToStorageValue(state.uiStyle));
    root.insert(QStringLiteral("alwaysOnTop"), state.alwaysOnTop);
    root.insert(QStringLiteral("windowLocked"), state.windowLocked);

    QJsonObject windowObject;
    windowObject.insert(QStringLiteral("x"), state.windowPosition.x());
    windowObject.insert(QStringLiteral("y"), state.windowPosition.y());
    windowObject.insert(QStringLiteral("width"), state.windowSize.width());
    windowObject.insert(QStringLiteral("height"), state.windowSize.height());
    root.insert(QStringLiteral("window"), windowObject);

    QJsonArray notesArray;
    for (const NoteItem &note : state.notes) {
        notesArray.append(noteToJson(note));
    }
    root.insert(QStringLiteral("notes"), notesArray);

    return root;
}

AppState stateFromJson(const QJsonObject &root) {
    AppState state;
    state.uiScale = qBound(constants::kMinUiScale,
                           root.value(QStringLiteral("uiScale")).toDouble(constants::kDefaultUiScale),
                           constants::kMaxUiScale);
    state.baseLayerOpacity = qBound(constants::kMinBaseLayerOpacity,
                                    root.value(QStringLiteral("baseLayerOpacity")).toDouble(constants::kDefaultBaseLayerOpacity),
                                    constants::kMaxBaseLayerOpacity);
    state.uiStyle = uiStyleFromStorageValue(root.value(QStringLiteral("uiStyle")));
    state.alwaysOnTop = root.value(QStringLiteral("alwaysOnTop")).toBool(false);
    state.windowLocked = root.value(QStringLiteral("windowLocked")).toBool(false);

    const QJsonObject windowObject = root.value(QStringLiteral("window")).toObject();
    if (windowObject.contains(QStringLiteral("x")) && windowObject.contains(QStringLiteral("y"))) {
        state.windowPosition = QPoint(windowObject.value(QStringLiteral("x")).toInt(),
                                      windowObject.value(QStringLiteral("y")).toInt());
        state.hasSavedWindowPosition = true;
    }
    if (windowObject.contains(QStringLiteral("width")) && windowObject.contains(QStringLiteral("height"))) {
        state.windowSize = QSize(windowObject.value(QStringLiteral("width")).toInt(),
                                 windowObject.value(QStringLiteral("height")).toInt());
        state.hasSavedWindowSize = state.windowSize.width() > 0 && state.windowSize.height() > 0;
    }

    const QJsonArray notesArray = root.value(QStringLiteral("notes")).toArray();
    state.notes.reserve(notesArray.size());
    for (qsizetype index = 0; index < notesArray.size(); ++index) {
        const QJsonValue value = notesArray.at(index);
        if (!value.isObject()) {
            continue;
        }
        state.notes.append(noteFromJson(value.toObject(), static_cast<int>(index)));
    }

    return state;
}

}  // namespace

JsonStorageService::JsonStorageService() = default;

JsonStorageService::JsonStorageService(const QString &storageFilePathOverride)
    : m_storageFilePathOverride(storageFilePathOverride) {}

JsonStorageService::LoadResult JsonStorageService::loadWithRecovery() const {
    const QString primaryPath = storageFilePath();
    const ReadResult primaryRead = readStateFromFile(primaryPath);

    if (primaryRead.status == ReadStatus::Success) {
        LoadResult result;
        result.state = primaryRead.state;
        result.status = LoadStatus::Success;
        return result;
    }

    if (primaryRead.status == ReadStatus::MissingFile) {
        LoadResult result;
        result.state = defaultState();
        result.status = LoadStatus::MissingPrimary;
        result.message = QStringLiteral("未找到数据文件，已创建默认便签。\n%1").arg(primaryPath);
        return result;
    }

    AppState backupState;
    QString backupPath;
    QString backupError;
    if (restoreFromLatestBackup(&backupState, &backupPath, &backupError)) {
        LoadResult result;
        result.state = backupState;
        result.status = LoadStatus::RecoveredFromBackup;
        result.backupFilePath = backupPath;

        if (QFileInfo::exists(primaryPath)) {
            const QString corruptPath = nextCorruptFilePath();
            if (QFile::rename(primaryPath, corruptPath)) {
                result.corruptFilePath = corruptPath;
            }
        }

        writeStateToFile(backupState, primaryPath, nullptr);
        result.message = QStringLiteral("检测到主数据文件异常，已自动回退到最近备份。");
        return result;
    }

    LoadResult result;
    result.state = defaultState();
    result.status = LoadStatus::FallbackToDefault;
    result.message = QStringLiteral("主数据文件读取失败，且无可用备份。\n%1\n%2")
                         .arg(primaryRead.errorMessage, backupError);
    return result;
}

bool JsonStorageService::save(const AppState &state, QString *errorMessage) const {
    if (!writeStateToFile(state, storageFilePath(), errorMessage)) {
        qCWarning(lcStorage) << "failed to save primary state" << storageFilePath()
                             << (errorMessage != nullptr ? *errorMessage : QString());
        return false;
    }

    QString backupError;
    if (!writeStateToFile(state, latestBackupFilePath(), &backupError)) {
        qCWarning(lcStorage) << "failed to update latest backup" << latestBackupFilePath() << backupError;
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

bool JsonStorageService::exportState(const AppState &state,
                                     const QString &filePath,
                                     QString *errorMessage) const {
    return writeStateToFile(state, filePath, errorMessage);
}

bool JsonStorageService::importState(const QString &filePath,
                                     AppState *state,
                                     QString *errorMessage) const {
    if (state == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("导入目标状态为空。");
        }
        return false;
    }

    const ReadResult result = readStateFromFile(filePath);
    if (result.status != ReadStatus::Success) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    *state = result.state;
    return true;
}

bool JsonStorageService::createBackupSnapshot(const AppState &state,
                                              QString *snapshotFilePath,
                                              QString *errorMessage) const {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString suffix = QStringLiteral(".json");
    QDir backupDir(backupDirectoryPath());
    QString candidatePath = backupDir.filePath(QStringLiteral("snapshot-%1%2").arg(stamp, suffix));
    int serial = 1;
    while (QFileInfo::exists(candidatePath)) {
        candidatePath = backupDir.filePath(QStringLiteral("snapshot-%1-%2%3").arg(stamp).arg(serial).arg(suffix));
        ++serial;
    }

    if (!writeStateToFile(state, candidatePath, errorMessage)) {
        return false;
    }

    writeStateToFile(state, latestBackupFilePath(), nullptr);
    if (snapshotFilePath != nullptr) {
        *snapshotFilePath = candidatePath;
    }
    return true;
}

bool JsonStorageService::restoreFromLatestBackup(AppState *state,
                                                 QString *backupFilePath,
                                                 QString *errorMessage) const {
    if (state == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("恢复目标状态为空。");
        }
        return false;
    }

    const QString filePath = latestBackupFilePath();
    const ReadResult result = readStateFromFile(filePath);
    if (result.status != ReadStatus::Success) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    *state = result.state;
    if (backupFilePath != nullptr) {
        *backupFilePath = filePath;
    }
    return true;
}

bool JsonStorageService::hasLatestBackup() const {
    return QFileInfo::exists(latestBackupFilePath());
}

QString JsonStorageService::storageFilePath() const {
    if (!m_storageFilePathOverride.isEmpty()) {
        return m_storageFilePathOverride;
    }

    const QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(basePath).filePath(QStringLiteral("state.json"));
}

QString JsonStorageService::backupDirectoryPath() const {
    const QFileInfo info(storageFilePath());
    return QDir(info.absolutePath()).filePath(QStringLiteral("backups"));
}

QString JsonStorageService::latestBackupFilePath() const {
    return QDir(backupDirectoryPath()).filePath(kLatestBackupFileName);
}

JsonStorageService::ReadResult JsonStorageService::readStateFromFile(const QString &filePath) const {
    ReadResult result;
    QFile file(filePath);
    if (!file.exists()) {
        result.status = ReadStatus::MissingFile;
        result.errorMessage = QStringLiteral("文件不存在：%1").arg(filePath);
        qCInfo(lcStorage) << "storage file missing" << filePath;
        return result;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        result.status = ReadStatus::OpenFailed;
        result.errorMessage = QStringLiteral("无法打开文件：%1\n%2").arg(filePath, file.errorString());
        qCWarning(lcStorage) << "open failed" << filePath << file.errorString();
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.status = ReadStatus::ParseFailed;
        result.errorMessage = QStringLiteral("JSON 解析失败：%1\n%2").arg(filePath, parseError.errorString());
        qCWarning(lcStorage) << "parse failed" << filePath << parseError.errorString();
        return result;
    }

    if (!document.isObject()) {
        result.status = ReadStatus::InvalidRoot;
        result.errorMessage = QStringLiteral("JSON 根节点必须是对象：%1").arg(filePath);
        qCWarning(lcStorage) << "invalid root object" << filePath;
        return result;
    }

    result.status = ReadStatus::Success;
    result.state = stateFromJson(document.object());
    return result;
}

bool JsonStorageService::writeStateToFile(const AppState &state,
                                          const QString &filePath,
                                          QString *errorMessage) const {
    const QFileInfo fileInfo(filePath);
    QDir directory(fileInfo.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建目录：%1").arg(fileInfo.absolutePath());
        }
        qCWarning(lcStorage) << "mkdir failed" << fileInfo.absolutePath();
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法写入文件：%1\n%2").arg(filePath, file.errorString());
        }
        qCWarning(lcStorage) << "open for write failed" << filePath << file.errorString();
        return false;
    }

    const QByteArray payload = QJsonDocument(stateToJson(state)).toJson(QJsonDocument::Indented);
    const qint64 written = file.write(payload);
    if (written != payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("写入失败：%1").arg(filePath);
        }
        qCWarning(lcStorage) << "short write" << filePath << written << payload.size();
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("保存失败：%1\n%2").arg(filePath, file.errorString());
        }
        qCWarning(lcStorage) << "commit failed" << filePath << file.errorString();
        return false;
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

QString JsonStorageService::nextCorruptFilePath() const {
    const QFileInfo storageInfo(storageFilePath());
    const QString suffix = storageInfo.completeSuffix().isEmpty()
                               ? QString()
                               : QStringLiteral(".") + storageInfo.completeSuffix();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString baseName = storageInfo.completeBaseName();

    QDir directory(storageInfo.absolutePath());
    QString candidate = directory.filePath(QStringLiteral("%1.corrupt-%2%3").arg(baseName, stamp, suffix));
    int serial = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = directory.filePath(
            QStringLiteral("%1.corrupt-%2-%3%4").arg(baseName, stamp).arg(serial).arg(suffix));
        ++serial;
    }

    return candidate;
}

AppState JsonStorageService::defaultState() const {
    AppState state;
    NoteItem note;
    note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    note.text.clear();
    note.order = 0;
    note.hue = -1;
    note.reminderEpochMsec = 0;
    state.notes.append(note);
    state.baseLayerOpacity = constants::kDefaultBaseLayerOpacity;
    state.uiScale = constants::kDefaultUiScale;
    return state;
}

}  // namespace glassnote
