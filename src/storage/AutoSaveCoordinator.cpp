#include "storage/AutoSaveCoordinator.h"

#include <QTimer>

namespace glassnote {

AutoSaveCoordinator::AutoSaveCoordinator(QObject *parent)
    : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(400);
    m_timer->setSingleShot(true);

    connect(m_timer, &QTimer::timeout, this, &AutoSaveCoordinator::saveRequested);
}

void AutoSaveCoordinator::requestSave() {
    m_timer->start();
}

void AutoSaveCoordinator::flush() {
    if (m_timer->isActive()) {
        m_timer->stop();
    }

    emit saveRequested();
}

}  // namespace glassnote
