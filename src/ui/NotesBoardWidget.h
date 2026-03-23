#pragma once

#include <QEvent>
#include <QLabel>
#include <QPoint>
#include <QPointF>
#include <QPixmap>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include "model/NoteItem.h"
#include "model/UiStyle.h"

class QGraphicsDropShadowEffect;
class QPropertyAnimation;
class QScrollArea;
class QTimer;
class QVariantAnimation;

namespace glassnote {

class NoteCardWidget;

class NotesBoardWidget final : public QWidget {
    Q_OBJECT

public:
    explicit NotesBoardWidget(QWidget *parent = nullptr);
    ~NotesBoardWidget() override;

    int bestVisibleContentHeight(int maxHeight) const;
    int totalContentHeight() const;
    int requiredContentWidth() const;
    QVector<NoteItem> notes() const;
    void setNotes(const QVector<NoteItem> &notes);
    void setImportedStickers(const QVector<QString> &stickers);
    void focusNoteEditor(const QString &noteId);
    void setUiScale(qreal scale);
    void setBaseLayerOpacity(qreal opacity);
    void setUiStyle(UiStyle uiStyle);
    void setExternalFileSyncEnabled(bool enabled);
    void setAlwaysOnTopEnabled(bool enabled);
    void setLaunchAtStartupEnabled(bool enabled);
    void setAutoCheckUpdatesEnabled(bool enabled);
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
    void launchAtStartupToggled(bool enabled);
    void autoCheckUpdatesToggled(bool enabled);
    void windowLockToggled(bool enabled);
    void checkForUpdatesRequested();
    void reminderSetRequested(const QString &noteId);
    void reminderClearedRequested(const QString &noteId);
    void timelineReplayRequested();
    void openStorageDirectoryRequested();
    void quitRequested();
    void noteDeleteRequested(const QString &noteId);
    void noteHueChangeRequested(const QString &noteId, int hue);
    void noteStickerChangeRequested(const QString &noteId, const QString &sticker);
    void noteLaneChangeRequested(const QString &noteId, NoteLane lane);
    void uiStyleChangeRequested(UiStyle uiStyle);
    void noteReorderRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void handleCardDragHoldStarted(const QString &noteId, const QPoint &globalPos);
    void scheduleDragPreviewUpdate(const QPoint &globalPos);
    void updateCardDragPreview(const QPoint &globalPos);
    void finishCardDrag(const QPoint &globalPos, bool canceled);
    void updateDragAutoScroll(const QPoint &globalPos);
    void stopDragAutoScroll();
    NoteLane laneForDragLocalY(int localY) const;
    int insertionOrderForLane(NoteLane lane, int localY) const;
    int stabilizedInsertionOrderForLane(NoteLane lane, int rawOrder, int localY) const;
    QString insertionTargetCardIdForLaneOrder(NoteLane lane, int laneOrder) const;
    QVector<NoteItem> notesWithDraggedCardPreview(NoteLane lane, int laneOrder) const;
    NoteCardWidget *findCardById(const QString &noteId) const;
    void setDropHoverTargetCardId(const QString &noteId);
    void clearDropHoverTarget();
    QScrollArea *hostScrollArea() const;
    void refreshDragProxyPixmap(qreal scale);
    void updateDragProxyPosition(const QPoint &globalPos);
    void clearDragProxy();
    bool rebuildCardsFastIfPossible(const QVector<NoteItem> &notes);
    void rebuildCards(const QVector<NoteItem> &notes);
    void handleCardDeleteRequested(const QString &noteId);

    QVBoxLayout *m_layout = nullptr;
    QVector<QWidget *> m_laneHeaders;
    QVector<NoteCardWidget *> m_cards;
    QVector<QString> m_importedStickerLibrary;
    qreal m_uiScale = 1.0;
    qreal m_baseLayerOpacity = 1.0;
    UiStyle m_uiStyle = UiStyle::Glass;
    bool m_externalFileSyncEnabled = true;
    bool m_alwaysOnTopEnabled = false;
    bool m_launchAtStartupEnabled = false;
    bool m_autoCheckUpdatesEnabled = true;
    bool m_windowLocked = false;
    bool m_deleteAnimationRunning = false;
    bool m_noteDragInProgress = false;
    QString m_hiddenCardId;
    QString m_draggedNoteId;
    NoteCardWidget *m_draggedCard = nullptr;
    QLabel *m_dragProxy = nullptr;
    QPointF m_dragProxyAnchorRatio = QPointF(0.5, 0.5);
    QPixmap m_dragProxySourcePixmap;
    qreal m_dragProxyScale = 1.0;
    QGraphicsDropShadowEffect *m_dragProxyShadowEffect = nullptr;
    QVariantAnimation *m_dragProxyScaleAnimation = nullptr;
    QPropertyAnimation *m_dragProxyShadowBlurAnimation = nullptr;
    QPropertyAnimation *m_dragProxyShadowOffsetAnimation = nullptr;
    QTimer *m_dragPreviewUpdateTimer = nullptr;
    QTimer *m_dragAutoScrollTimer = nullptr;
    int m_dragAutoScrollVelocity = 0;
    QPoint m_lastDragGlobalPos;
    QPoint m_pendingDragPreviewGlobalPos;
    NoteLane m_dragPreviewLane = NoteLane::Today;
    int m_dragPreviewOrder = -1;
    QString m_dropHoverTargetCardId;
    QVector<NoteItem> m_dragOriginNotes;
    QVector<NoteItem> m_dragPreviewNotes;
};

}  // namespace glassnote
