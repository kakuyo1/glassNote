#include "storage/JsonStorageService.h"

#include <QDate>
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
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include "common/Constants.h"
#include "model/UiStyle.h"

namespace glassnote {

namespace {

Q_LOGGING_CATEGORY(lcStorage, "glassnote.storage")

constexpr int kStorageVersion = 4;
const auto kLatestBackupFileName = QStringLiteral("latest.json");
const auto kImportedImageStickerPrefix = QStringLiteral("__image_sticker__:");

QString noteLaneToStorageValue(NoteLane lane) {
    switch (normalizedNoteLane(lane)) {
    case NoteLane::Today:
        return QStringLiteral("today");
    case NoteLane::Next:
        return QStringLiteral("next");
    case NoteLane::Waiting:
        return QStringLiteral("waiting");
    case NoteLane::Someday:
        return QStringLiteral("someday");
    }
    return QStringLiteral("today");
}

NoteLane noteLaneFromStorageValue(const QJsonValue &value) {
    if (value.isString()) {
        const QString lane = value.toString().trimmed().toLower();
        if (lane == QStringLiteral("next")) {
            return NoteLane::Next;
        }
        if (lane == QStringLiteral("waiting")) {
            return NoteLane::Waiting;
        }
        if (lane == QStringLiteral("someday")) {
            return NoteLane::Someday;
        }
        return NoteLane::Today;
    }

    switch (value.toInt(0)) {
    case 1:
        return NoteLane::Next;
    case 2:
        return NoteLane::Waiting;
    case 3:
        return NoteLane::Someday;
    default:
        return NoteLane::Today;
    }
}

QString uiStyleToStorageValue(UiStyle uiStyle) {
    switch (normalizedUiStyle(uiStyle)) {
    case UiStyle::Glass:
        return QStringLiteral("glass");
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
            return UiStyle::Glass;
        }
        if (style == QStringLiteral("sunrise")) {
            return UiStyle::Clay;
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
        return UiStyle::Glass;
    case static_cast<int>(UiStyle::Sunrise):
        return UiStyle::Clay;
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

bool isImportedImageStickerValue(const QString &value) {
    return value.trimmed().startsWith(kImportedImageStickerPrefix);
}

QString importedImageStickerPath(const QString &value) {
    if (!isImportedImageStickerValue(value)) {
        return QString();
    }
    return value.trimmed().mid(kImportedImageStickerPrefix.size()).trimmed();
}

QString encodeImportedImageStickerPath(const QString &path) {
    return kImportedImageStickerPrefix + QDir::toNativeSeparators(path.trimmed());
}

QString normalizeImportedStickerEntry(const QString &value) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QString path = isImportedImageStickerValue(trimmed) ? importedImageStickerPath(trimmed) : trimmed;
    if (path.isEmpty()) {
        return QString();
    }

    return encodeImportedImageStickerPath(path);
}

QVector<QString> normalizeImportedStickers(const QVector<QString> &stickers) {
    QVector<QString> normalized;
    normalized.reserve(stickers.size());
    QSet<QString> dedupe;
    dedupe.reserve(stickers.size());

    for (const QString &entry : stickers) {
        const QString candidate = normalizeImportedStickerEntry(entry);
        if (candidate.isEmpty()) {
            continue;
        }

        const QString key = candidate.toCaseFolded();
        if (dedupe.contains(key)) {
            continue;
        }

        dedupe.insert(key);
        normalized.append(candidate);
    }

    return normalized;
}

QJsonObject noteToJson(const NoteItem &note) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), note.id);
    object.insert(QStringLiteral("text"), note.text);
    object.insert(QStringLiteral("order"), note.order);
    object.insert(QStringLiteral("lane"), noteLaneToStorageValue(note.lane));
    object.insert(QStringLiteral("hue"), note.hue);
    object.insert(QStringLiteral("sticker"), note.sticker);
    object.insert(QStringLiteral("reminderEpochMsec"), note.reminderEpochMsec);
    return object;
}

NoteItem noteFromJson(const QJsonObject &object, int fallbackOrder);

bool notesEquivalent(const QVector<NoteItem> &left, const QVector<NoteItem> &right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (qsizetype index = 0; index < left.size(); ++index) {
        const NoteItem &lhs = left.at(index);
        const NoteItem &rhs = right.at(index);
        if (lhs.id != rhs.id
            || lhs.text != rhs.text
            || lhs.order != rhs.order
            || normalizedNoteLane(lhs.lane) != normalizedNoteLane(rhs.lane)
            || lhs.hue != rhs.hue
            || lhs.sticker != rhs.sticker
            || lhs.reminderEpochMsec != rhs.reminderEpochMsec) {
            return false;
        }
    }

    return true;
}

QJsonObject timelineSnapshotToJson(const DailyTimelineSnapshot &snapshot) {
    QJsonObject object;
    object.insert(QStringLiteral("dateKey"), snapshot.dateKey);

    QJsonArray notesArray;
    for (const NoteItem &note : snapshot.notes) {
        notesArray.append(noteToJson(note));
    }
    object.insert(QStringLiteral("notes"), notesArray);

    return object;
}

DailyTimelineSnapshot timelineSnapshotFromJson(const QJsonObject &object) {
    DailyTimelineSnapshot snapshot;
    snapshot.dateKey = object.value(QStringLiteral("dateKey")).toString().trimmed();
    const QDate date = QDate::fromString(snapshot.dateKey, QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid()) {
        snapshot.dateKey.clear();
        return snapshot;
    }

    const QJsonArray notesArray = object.value(QStringLiteral("notes")).toArray();
    snapshot.notes.reserve(notesArray.size());
    for (qsizetype index = 0; index < notesArray.size(); ++index) {
        const QJsonValue value = notesArray.at(index);
        if (!value.isObject()) {
            continue;
        }
        snapshot.notes.append(noteFromJson(value.toObject(), static_cast<int>(index)));
    }

    if (snapshot.notes.isEmpty()) {
        snapshot.dateKey.clear();
    }

    return snapshot;
}

NoteItem noteFromJson(const QJsonObject &object, int fallbackOrder) {
    NoteItem note;
    note.id = object.value(QStringLiteral("id")).toString();
    if (note.id.isEmpty()) {
        note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    note.text = object.value(QStringLiteral("text")).toString();
    note.order = object.value(QStringLiteral("order")).toInt(fallbackOrder);
    note.lane = noteLaneFromStorageValue(object.value(QStringLiteral("lane")));
    note.hue = object.value(QStringLiteral("hue")).toInt(-1);
    note.sticker = object.value(QStringLiteral("sticker")).toString();

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
    root.insert(QStringLiteral("launchAtStartup"), state.launchAtStartup);
    root.insert(QStringLiteral("windowLocked"), state.windowLocked);
    root.insert(QStringLiteral("autoCheckUpdates"), state.autoCheckUpdates);
    root.insert(QStringLiteral("ignoredUpdateVersion"), state.ignoredUpdateVersion);
    root.insert(QStringLiteral("lastUpdateCheckEpochMsec"), QString::number(state.lastUpdateCheckEpochMsec));
    root.insert(QStringLiteral("clipboardInboxEnabled"), state.clipboardInboxEnabled);
    root.insert(QStringLiteral("ocrExperimentalEnabled"), state.ocrExperimentalEnabled);

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

    QJsonArray importedStickersArray;
    const QVector<QString> normalizedImportedStickers = normalizeImportedStickers(state.importedStickers);
    for (const QString &sticker : normalizedImportedStickers) {
        importedStickersArray.append(sticker);
    }
    root.insert(QStringLiteral("importedStickers"), importedStickersArray);

    QJsonArray timelineArray;
    for (const DailyTimelineSnapshot &snapshot : state.timelineSnapshots) {
        if (snapshot.dateKey.isEmpty() || snapshot.notes.isEmpty()) {
            continue;
        }
        timelineArray.append(timelineSnapshotToJson(snapshot));
    }
    root.insert(QStringLiteral("timelineSnapshots"), timelineArray);

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
    state.launchAtStartup = root.value(QStringLiteral("launchAtStartup")).toBool(false);
    state.windowLocked = root.value(QStringLiteral("windowLocked")).toBool(false);
    state.autoCheckUpdates = root.value(QStringLiteral("autoCheckUpdates")).toBool(true);
    state.ignoredUpdateVersion = root.value(QStringLiteral("ignoredUpdateVersion")).toString().trimmed();
    const QJsonValue lastUpdateCheckValue = root.value(QStringLiteral("lastUpdateCheckEpochMsec"));
    if (lastUpdateCheckValue.isString()) {
        state.lastUpdateCheckEpochMsec = lastUpdateCheckValue.toString().toLongLong();
    } else {
        state.lastUpdateCheckEpochMsec = static_cast<qint64>(lastUpdateCheckValue.toDouble(0.0));
    }
    if (state.lastUpdateCheckEpochMsec < 0) {
        state.lastUpdateCheckEpochMsec = 0;
    }
    state.clipboardInboxEnabled = root.value(QStringLiteral("clipboardInboxEnabled")).toBool(true);
    state.ocrExperimentalEnabled = root.value(QStringLiteral("ocrExperimentalEnabled")).toBool(false);

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

    QVector<QString> importedStickers;
    if (root.contains(QStringLiteral("importedStickers"))) {
        const QJsonArray importedStickersArray = root.value(QStringLiteral("importedStickers")).toArray();
        importedStickers.reserve(importedStickersArray.size());
        for (const QJsonValue &value : importedStickersArray) {
            if (!value.isString()) {
                continue;
            }
            importedStickers.append(value.toString());
        }
    } else {
        importedStickers.reserve(state.notes.size());
        for (const NoteItem &note : std::as_const(state.notes)) {
            if (!isImportedImageStickerValue(note.sticker)) {
                continue;
            }
            importedStickers.append(note.sticker);
        }
    }
    state.importedStickers = normalizeImportedStickers(importedStickers);

    const QJsonArray timelineArray = root.value(QStringLiteral("timelineSnapshots")).toArray();
    state.timelineSnapshots.reserve(timelineArray.size());
    for (const QJsonValue &value : timelineArray) {
        if (!value.isObject()) {
            continue;
        }

        const DailyTimelineSnapshot snapshot = timelineSnapshotFromJson(value.toObject());
        if (snapshot.dateKey.isEmpty() || snapshot.notes.isEmpty()) {
            continue;
        }

        bool replaced = false;
        for (DailyTimelineSnapshot &existing : state.timelineSnapshots) {
            if (existing.dateKey == snapshot.dateKey) {
                existing.notes = snapshot.notes;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            state.timelineSnapshots.append(snapshot);
        }
    }

    std::sort(state.timelineSnapshots.begin(), state.timelineSnapshots.end(), [](const DailyTimelineSnapshot &left,
                                                                                 const DailyTimelineSnapshot &right) {
        return left.dateKey < right.dateKey;
    });

    QVector<DailyTimelineSnapshot> deduplicated;
    deduplicated.reserve(state.timelineSnapshots.size());
    for (const DailyTimelineSnapshot &snapshot : std::as_const(state.timelineSnapshots)) {
        if (deduplicated.isEmpty() || !notesEquivalent(deduplicated.constLast().notes, snapshot.notes)) {
            deduplicated.append(snapshot);
        }
    }
    state.timelineSnapshots = deduplicated;

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
    note.lane = NoteLane::Today;
    note.hue = -1;
    note.sticker.clear();
    note.reminderEpochMsec = 0;
    state.notes.append(note);
    state.baseLayerOpacity = constants::kDefaultBaseLayerOpacity;
    state.uiScale = constants::kDefaultUiScale;
    return state;
}

}  // namespace glassnote
