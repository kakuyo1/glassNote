#pragma once

#include <QEvent>
#include <QPoint>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include "model/NoteItem.h"
#include "model/UiStyle.h"

namespace glassnote {

class NoteCardWidget;

class NotesBoardWidget final : public QWidget {
    Q_OBJECT

public:
    explicit NotesBoardWidget(QWidget *parent = nullptr);

    int bestVisibleContentHeight(int maxHeight) const;
    int totalContentHeight() const;
    int requiredContentWidth() const;
    QVector<NoteItem> notes() const;
    void setNotes(const QVector<NoteItem> &notes);
    void focusNoteEditor(const QString &noteId);
    void setUiScale(qreal scale);
    void setBaseLayerOpacity(qreal opacity);
    void setUiStyle(UiStyle uiStyle);
    void setExternalFileSyncEnabled(bool enabled);
    void setAlwaysOnTopEnabled(bool enabled);
    void setWindowLocked(bool enabled);

signals:
    void noteTextCommitted(const QString &noteId, const QString &text);
    void layoutRefreshRequested();
    void addNoteRequested();
    void clearEmptyRequested();
    void scaleInRequested();
    void scaleOutRequested();
    void scaleResetRequested();
    void baseLayerOpacitySetRequested(qreal opacity);
    void exportJsonRequested();
    void importJsonRequested();
    void backupSnapshotRequested();
    void restoreLatestBackupRequested();
    void externalFileSyncToggled(bool enabled);
    void alwaysOnTopToggled(bool enabled);
    void windowLockToggled(bool enabled);
    void reminderSetRequested(const QString &noteId);
    void reminderClearedRequested(const QString &noteId);
    void openStorageDirectoryRequested();
    void quitRequested();
    void noteDeleteRequested(const QString &noteId);
    void noteHueChangeRequested(const QString &noteId, int hue);
    void noteLaneChangeRequested(const QString &noteId, NoteLane lane);
    void uiStyleChangeRequested(UiStyle uiStyle);

protected:
private:
    void rebuildCards(const QVector<NoteItem> &notes);
    void handleCardDeleteRequested(const QString &noteId);

    QVBoxLayout *m_layout = nullptr;
    QVector<QWidget *> m_laneHeaders;
    QVector<NoteCardWidget *> m_cards;
    qreal m_uiScale = 1.0;
    qreal m_baseLayerOpacity = 1.0;
    UiStyle m_uiStyle = UiStyle::Glass;
    bool m_externalFileSyncEnabled = true;
    bool m_alwaysOnTopEnabled = false;
    bool m_windowLocked = false;
    bool m_deleteAnimationRunning = false;
};

}  // namespace glassnote
