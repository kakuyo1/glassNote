#pragma once

#include <QMoveEvent>
#include <QMouseEvent>
#include <QByteArray>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QPaintEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPoint>
#include <QPropertyAnimation>
#include <QRect>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QWidget>

#include "model/NoteItem.h"
#include "model/UiStyle.h"

namespace glassnote {

class NotesBoardWidget;

class MainWindow final : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    QVector<NoteItem> notes() const;
    void setNotes(const QVector<NoteItem> &notes);
    void setUiScale(qreal scale);
    void setUiStyle(UiStyle uiStyle);
    void setBaseLayerOpacity(qreal opacity);
    void setExternalFileSyncEnabled(bool enabled);
    void setAlwaysOnTopEnabled(bool enabled);
    void setWindowLocked(bool enabled);
    void focusNoteEditor(const QString &noteId);
    void restoreWindowPosition(const QPoint &position);
    void restoreWindowSize(const QSize &size);
    QSize persistedWindowSize() const;
    bool isInResizeZone(const QPoint &globalPos) const;
    bool startResizeIfNeeded(const QPoint &globalPos);
    bool isResizingWindow() const;
    void updateManualResize(const QPoint &globalPos);
    void finishManualResize();

signals:
    void noteTextCommitted(const QString &noteId, const QString &text);
    void windowMoved(const QPoint &position);
    void windowResized(const QSize &size);
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
    void timelineReplayRequested();
    void openStorageDirectoryRequested();
    void quitRequested();
    void noteDeleteRequested(const QString &noteId);
    void noteHueChangeRequested(const QString &noteId, int hue);
    void noteStickerChangeRequested(const QString &noteId, const QString &sticker);
    void noteLaneChangeRequested(const QString &noteId, NoteLane lane);
    void uiStyleChangeRequested(UiStyle uiStyle);
    void edgeDropCaptureRequested(const QString &payload);
    void quickCaptureDropRequested(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private:
    void initializeWindow();
    void initializeLayout();
    void moveToDefaultPosition();
    void updateWindowBackdrop();
    void updateWindowGeometry();
    void scheduleEdgeFadeUpdate();
    void updateEdgeFadeWidgets();
    void revealScrollBarTemporarily();
    bool scrollBoardByPixelDelta(int deltaY);
    bool scrollBoardByAngleDelta(int deltaY);
    bool scrollFallbackByPixelDelta(const QPoint &globalPos, int deltaY);
    bool scrollFallbackByAngleDelta(const QPoint &globalPos, int deltaY);
    QScrollBar *resolveFallbackVerticalScrollBar(const QPoint &globalPos) const;
    bool forwardWheelToScrollArea(QWheelEvent *event);
    bool isTextDropMimeData(const QMimeData *mimeData) const;
    bool canHandleDropMimeData(const QMimeData *mimeData) const;
    bool isInEdgeDropZone(const QPoint &pos) const;
    QString payloadFromDropMimeData(const QMimeData *mimeData) const;
    QRect clampedGeometry(const QRect &targetGeometry) const;
    QRect adjustedResizeGeometry(const QRect &targetGeometry, Qt::Edges activeEdges) const;
    QSize scaledMinimumSize() const;
#ifdef Q_OS_WIN
    QRectF resizeReferenceRect() const;
    int resizeHitTest(const QPoint &globalPos) const;
    Qt::Edges edgesForGlobalPos(const QPoint &globalPos) const;
#endif

    NotesBoardWidget *m_boardWidget = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    bool m_hasPositioned = false;
    bool m_hasRestoredPosition = false;
    bool m_blurInitialized = false;
    bool m_draggingWindow = false;
    bool m_hasSavedSize = false;
    bool m_resizingWindow = false;
    bool m_updatingGeometryInternally = false;
    bool m_alwaysOnTopEnabled = false;
    bool m_windowLocked = false;
    qreal m_uiScale = 1.0;
    qreal m_baseLayerOpacity = 1.0;
    UiStyle m_uiStyle = UiStyle::Glass;
    QPoint m_dragOffset;
    QPoint m_resizeStartGlobalPos;
    QSize m_savedWindowSize;
    QRect m_resizeStartGeometry;
    Qt::Edges m_resizeEdges;
    QPropertyAnimation *m_geometryAnimation = nullptr;
    QWidget *m_topFade = nullptr;
    QWidget *m_bottomFade = nullptr;
    QTimer *m_scrollBarHideTimer = nullptr;
    QTimer *m_edgeFadeUpdateTimer = nullptr;
    int m_angleWheelRemainder = 0;
    int m_fallbackAngleWheelRemainder = 0;
};

}  // namespace glassnote
