#pragma once

#include <QString>

#include "model/AppState.h"

namespace glassnote {

class JsonStorageService final {
public:
    enum class LoadStatus {
        Success,
        MissingPrimary,
        RecoveredFromBackup,
        FallbackToDefault,
    };

    struct LoadResult {
        AppState state;
        LoadStatus status = LoadStatus::Success;
        QString message;
        QString backupFilePath;
        QString corruptFilePath;
    };

    JsonStorageService();
    explicit JsonStorageService(const QString &storageFilePathOverride);

    LoadResult loadWithRecovery() const;
    bool save(const AppState &state, QString *errorMessage = nullptr) const;
    bool exportState(const AppState &state, const QString &filePath, QString *errorMessage = nullptr) const;
    bool importState(const QString &filePath, AppState *state, QString *errorMessage = nullptr) const;
    bool createBackupSnapshot(const AppState &state,
                              QString *snapshotFilePath = nullptr,
                              QString *errorMessage = nullptr) const;
    bool restoreFromLatestBackup(AppState *state,
                                 QString *backupFilePath = nullptr,
                                 QString *errorMessage = nullptr) const;
    bool hasLatestBackup() const;
    QString storageFilePath() const;
    QString backupDirectoryPath() const;
    QString latestBackupFilePath() const;

private:
    enum class ReadStatus {
        Success,
        MissingFile,
        OpenFailed,
        ParseFailed,
        InvalidRoot,
    };

    struct ReadResult {
        ReadStatus status = ReadStatus::MissingFile;
        AppState state;
        QString errorMessage;
    };

    AppState defaultState() const;
    ReadResult readStateFromFile(const QString &filePath) const;
    bool writeStateToFile(const AppState &state,
                          const QString &filePath,
                          QString *errorMessage = nullptr) const;
    QString nextCorruptFilePath() const;

    QString m_storageFilePathOverride;
};

}  // namespace glassnote
