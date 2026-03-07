#pragma once

#include <QObject>

class QTimer;

namespace glassnote {

class AutoSaveCoordinator final : public QObject {
    Q_OBJECT

public:
    explicit AutoSaveCoordinator(QObject *parent = nullptr);

    void requestSave();
    void flush();

signals:
    void saveRequested();

private:
    QTimer *m_timer = nullptr;
};

}  // namespace glassnote
