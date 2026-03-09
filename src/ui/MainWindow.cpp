#include "ui/MainWindow.h"

#include <QApplication>
#include <QAbstractScrollArea>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFrame>
#include <QMimeData>
#include <QMoveEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

#include "common/Constants.h"
#include "platform/windows/WindowsBlurHelper.h"
#include "theme/ThemeHelper.h"
#include "ui/NotesBoardWidget.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace glassnote {

namespace {

class EdgeFadeWidget final : public QWidget {
public:
    explicit EdgeFadeWidget(bool topEdge, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_topEdge(topEdge) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

    void setOpacity(qreal opacity) {
        const qreal clamped = qBound(0.0, opacity, 1.0);
        if (qFuzzyCompare(m_opacity, clamped)) {
            return;
        }
        m_opacity = clamped;
        update();
    }

    void setBaseColor(const QColor &baseColor) {
        if (m_baseColor == baseColor) {
            return;
        }
        m_baseColor = baseColor;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event)

        if (m_opacity <= 0.0) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        QColor dense = m_baseColor;
        dense.setAlpha(static_cast<int>(44.0 * m_opacity));
        QColor mid = m_baseColor;
        mid.setAlpha(static_cast<int>(26.0 * m_opacity));
        QColor light = m_baseColor;
        light.setAlpha(static_cast<int>(12.0 * m_opacity));
        QColor soft = m_baseColor;
        soft.setAlpha(static_cast<int>(5.0 * m_opacity));
        QColor transparent = m_baseColor;
        transparent.setAlpha(0);
        QLinearGradient gradient(rect().topLeft(), rect().bottomLeft());
        if (m_topEdge) {
            gradient.setColorAt(0.0, dense);
            gradient.setColorAt(0.22, mid);
            gradient.setColorAt(0.58, light);
            gradient.setColorAt(0.86, soft);
            gradient.setColorAt(1.0, transparent);
        } else {
            gradient.setColorAt(0.0, transparent);
            gradient.setColorAt(0.14, soft);
            gradient.setColorAt(0.42, light);
            gradient.setColorAt(0.78, mid);
            gradient.setColorAt(1.0, dense);
        }
        painter.fillRect(rect(), gradient);
    }

private:
    bool m_topEdge = false;
    qreal m_opacity = 0.0;
    QColor m_baseColor = QColor(15, 18, 24, 255);
};

int boardPaddingForScale(qreal scale) {
    if (scale < 0.95) {
        return static_cast<int>(constants::kBoardPadding * scale * 0.9);
    }
    if (scale > 1.2) {
        return static_cast<int>(constants::kBoardPadding * scale * 1.1);
    }
    return static_cast<int>(constants::kBoardPadding * scale);
}

int scrollBarWidthForScale(qreal scale) {
    if (scale < 0.95) {
        return 7;
    }
    if (scale > 1.2) {
        return 10;
    }
    return 8;
}

bool styleUsesWindowBackdrop(UiStyle uiStyle) {
    return uiStyle == UiStyle::Glass || uiStyle == UiStyle::Mist || uiStyle == UiStyle::Sunrise;
}

constexpr int kEdgeDropCaptureZonePx = 36;

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent) {
    initializeWindow();
    initializeLayout();

    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }

    m_geometryAnimation = new QPropertyAnimation(this, "geometry", this);
    m_geometryAnimation->setDuration(200);
    m_geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

MainWindow::~MainWindow() {
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
}

QVector<NoteItem> MainWindow::notes() const {
    return m_boardWidget->notes();
}

void MainWindow::setNotes(const QVector<NoteItem> &notes) {
    m_boardWidget->setNotes(notes);
    updateWindowGeometry();
}

void MainWindow::setUiScale(qreal scale) {
    const qreal previousScale = m_uiScale;
    m_uiScale = scale;

    if (m_hasSavedSize && previousScale > 0.0 && !qFuzzyCompare(previousScale, scale)) {
        const qreal ratio = scale / previousScale;
        m_savedWindowSize = QSize(qMax(static_cast<int>(scaledMinimumSize().width()), static_cast<int>(m_savedWindowSize.width() * ratio)),
                                  qMax(static_cast<int>(scaledMinimumSize().height()), static_cast<int>(m_savedWindowSize.height() * ratio)));
    }

    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    if (layout != nullptr) {
        const int scaledPadding = boardPaddingForScale(scale);
        layout->setContentsMargins(scaledPadding, scaledPadding, scaledPadding, scaledPadding);
    }

    if (m_scrollArea != nullptr) {
        const int scrollBarWidth = scrollBarWidthForScale(scale);
        m_scrollArea->setStyleSheet(ThemeHelper::scrollAreaStyleSheet(m_uiStyle, scrollBarWidth));
    }

    m_boardWidget->setUiScale(scale);
    updateWindowGeometry();
}

void MainWindow::setUiStyle(UiStyle uiStyle) {
    if (m_uiStyle == uiStyle) {
        return;
    }

    m_uiStyle = uiStyle;

    if (m_scrollArea != nullptr) {
        const int scrollBarWidth = scrollBarWidthForScale(m_uiScale);
        m_scrollArea->setStyleSheet(ThemeHelper::scrollAreaStyleSheet(m_uiStyle, scrollBarWidth));
    }
    if (m_boardWidget != nullptr) {
        m_boardWidget->setUiStyle(m_uiStyle);
    }
    updateWindowBackdrop();
    updateEdgeFadeWidgets();
    update();
}

void MainWindow::setBaseLayerOpacity(qreal opacity) {
    const qreal clamped = qBound(constants::kMinBaseLayerOpacity, opacity, constants::kMaxBaseLayerOpacity);
    if (qFuzzyCompare(m_baseLayerOpacity, clamped)) {
        return;
    }

    m_baseLayerOpacity = clamped;
    m_boardWidget->setBaseLayerOpacity(clamped);
    update();
}

void MainWindow::setExternalFileSyncEnabled(bool enabled) {
    if (m_boardWidget == nullptr) {
        return;
    }
    m_boardWidget->setExternalFileSyncEnabled(enabled);
}

void MainWindow::setAlwaysOnTopEnabled(bool enabled) {
    if (m_alwaysOnTopEnabled == enabled) {
        return;
    }

    m_alwaysOnTopEnabled = enabled;
    const QRect currentGeometry = geometry();
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (wasVisible) {
        show();
    }
    setGeometry(currentGeometry);

    if (m_boardWidget != nullptr) {
        m_boardWidget->setAlwaysOnTopEnabled(enabled);
    }
}

void MainWindow::setLaunchAtStartupEnabled(bool enabled) {
    if (m_boardWidget == nullptr) {
        return;
    }
    m_boardWidget->setLaunchAtStartupEnabled(enabled);
}

void MainWindow::setAutoCheckUpdatesEnabled(bool enabled) {
    if (m_boardWidget == nullptr) {
        return;
    }
    m_boardWidget->setAutoCheckUpdatesEnabled(enabled);
}

void MainWindow::setWindowLocked(bool enabled) {
    if (m_windowLocked == enabled) {
        return;
    }

    m_windowLocked = enabled;
    if (enabled) {
        m_draggingWindow = false;
        m_resizingWindow = false;
        m_resizeEdges = Qt::Edges();
    }

    if (m_boardWidget != nullptr) {
        m_boardWidget->setWindowLocked(enabled);
    }
}

void MainWindow::focusNoteEditor(const QString &noteId) {
    m_boardWidget->focusNoteEditor(noteId);
}

void MainWindow::restoreWindowPosition(const QPoint &position) {
    const QRect targetGeometry(position, size());
    setGeometry(clampedGeometry(targetGeometry));
    m_hasPositioned = true;
    m_hasRestoredPosition = true;
}

void MainWindow::restoreWindowSize(const QSize &size) {
    if (!size.isValid()) {
        return;
    }

    m_savedWindowSize = size.expandedTo(scaledMinimumSize());
    m_hasSavedSize = true;
    updateWindowGeometry();
}

QSize MainWindow::persistedWindowSize() const {
    return m_hasSavedSize ? m_savedWindowSize : size();
}

bool MainWindow::isInResizeZone(const QPoint &globalPos) const {
#ifdef Q_OS_WIN
    return resizeHitTest(globalPos) != HTCLIENT;
#else
    Q_UNUSED(globalPos)
    return false;
#endif
}

bool MainWindow::startResizeIfNeeded(const QPoint &globalPos) {
#ifdef Q_OS_WIN
    if (m_windowLocked) {
        return false;
    }

    const Qt::Edges edges = edgesForGlobalPos(globalPos);
    if (edges == Qt::Edges()) {
        return false;
    }

    m_draggingWindow = false;
    m_resizingWindow = true;
    m_resizeEdges = edges;
    m_resizeStartGlobalPos = globalPos;
    m_resizeStartGeometry = geometry();
    m_geometryAnimation->stop();
    return true;
#else
    Q_UNUSED(globalPos)
    return false;
#endif
}

bool MainWindow::isResizingWindow() const {
    return m_resizingWindow;
}

void MainWindow::updateManualResize(const QPoint &globalPos) {
    if (!m_resizingWindow) {
        return;
    }

    const QPoint delta = globalPos - m_resizeStartGlobalPos;
    QRect target = m_resizeStartGeometry;

    if (m_resizeEdges.testFlag(Qt::LeftEdge)) {
        target.setLeft(target.left() + delta.x());
    }
    if (m_resizeEdges.testFlag(Qt::RightEdge)) {
        target.setRight(target.right() + delta.x());
    }
    if (m_resizeEdges.testFlag(Qt::TopEdge)) {
        target.setTop(target.top() + delta.y());
    }
    if (m_resizeEdges.testFlag(Qt::BottomEdge)) {
        target.setBottom(target.bottom() + delta.y());
    }

    const QRect adjusted = adjustedResizeGeometry(target, m_resizeEdges);
    m_updatingGeometryInternally = true;
    setGeometry(adjusted);
    m_updatingGeometryInternally = false;
}

void MainWindow::finishManualResize() {
    m_resizingWindow = false;
    m_resizeEdges = Qt::Edges();
    m_savedWindowSize = size();
    m_hasSavedSize = true;
    emit windowResized(m_savedWindowSize);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event != nullptr && event->type() == QEvent::Wheel) {
        auto *watchedWidget = qobject_cast<QWidget *>(watched);
        if (watchedWidget != nullptr && watchedWidget->window() == this) {
            if (forwardWheelToScrollArea(static_cast<QWheelEvent *>(event))) {
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event == nullptr) {
        return;
    }

    if (isTextDropMimeData(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }

    if (canHandleDropMimeData(event->mimeData()) && isInEdgeDropZone(event->position().toPoint())) {
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event) {
    if (event == nullptr) {
        return;
    }

    if (isTextDropMimeData(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }

    if (canHandleDropMimeData(event->mimeData()) && isInEdgeDropZone(event->position().toPoint())) {
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event) {
    if (event != nullptr) {
        event->accept();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    if (event == nullptr) {
        return;
    }

    if (isTextDropMimeData(event->mimeData())) {
        const QString droppedText = event->mimeData()->text().trimmed();
        if (droppedText.isEmpty()) {
            event->ignore();
            return;
        }

        emit quickCaptureDropRequested(droppedText);
        event->acceptProposedAction();
        return;
    }

    if (!canHandleDropMimeData(event->mimeData()) || !isInEdgeDropZone(event->position().toPoint())) {
        event->ignore();
        return;
    }

    const QString payload = payloadFromDropMimeData(event->mimeData());
    if (payload.trimmed().isEmpty()) {
        event->ignore();
        return;
    }

    emit edgeDropCaptureRequested(payload);
    event->acceptProposedAction();
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_windowLocked) {
            event->ignore();
            return;
        }

        if (startResizeIfNeeded(event->globalPosition().toPoint())) {
            event->accept();
            return;
        }

        m_draggingWindow = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_windowLocked) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_resizingWindow) {
        updateManualResize(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    if (m_draggingWindow && (event->buttons() & Qt::LeftButton) != 0) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_draggingWindow = false;
        if (m_resizingWindow) {
            finishManualResize();
            event->accept();
            return;
        }
    }

    QWidget::mouseReleaseEvent(event);
}

void MainWindow::wheelEvent(QWheelEvent *event) {
    if (forwardWheelToScrollArea(event)) {
        event->accept();
        return;
    }

    QWidget::wheelEvent(event);
}

void MainWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);

    if (isVisible()) {
        emit windowMoved(pos());
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    updateEdgeFadeWidgets();

    if (!m_updatingGeometryInternally && isVisible()) {
        m_savedWindowSize = event->size();
        m_hasSavedSize = true;
        emit windowResized(m_savedWindowSize);
    }
}

void MainWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frameRect = rect().adjusted(1.0, 1.0, -1.0, -1.0);
    qreal radiusBase = 26.0;
    switch (m_uiStyle) {
    case UiStyle::Pixel:
        radiusBase = 3.0;
        break;
    case UiStyle::Graphite:
    case UiStyle::Neon:
        radiusBase = 16.0;
        break;
    case UiStyle::Mist:
    case UiStyle::Meadow:
        radiusBase = 20.0;
        break;
    case UiStyle::Sunrise:
    case UiStyle::Paper:
    case UiStyle::Clay:
        radiusBase = 22.0;
        break;
    case UiStyle::Glass:
    default:
        radiusBase = 24.0;
        break;
    }
    const qreal radius = radiusBase * m_uiScale;
    const qreal alphaScale = qBound(constants::kMinBaseLayerOpacity,
                                    m_baseLayerOpacity,
                                    constants::kMaxBaseLayerOpacity);
    const auto scaledAlpha = [alphaScale](int alpha) {
        return qBound(0, static_cast<int>(alpha * alphaScale), 255);
    };
    const WindowPalette windowPalette = ThemeHelper::windowPalette(m_uiStyle);

    QLinearGradient fill(frameRect.topLeft(), frameRect.bottomLeft());
    fill.setColorAt(0.0, QColor(windowPalette.fillTop.red(), windowPalette.fillTop.green(), windowPalette.fillTop.blue(), scaledAlpha(windowPalette.fillTop.alpha())));
    fill.setColorAt(0.35, QColor(windowPalette.fillMiddle.red(), windowPalette.fillMiddle.green(), windowPalette.fillMiddle.blue(), scaledAlpha(windowPalette.fillMiddle.alpha())));
    fill.setColorAt(1.0, QColor(windowPalette.fillBottom.red(), windowPalette.fillBottom.green(), windowPalette.fillBottom.blue(), scaledAlpha(windowPalette.fillBottom.alpha())));

    painter.setPen(QPen(QColor(windowPalette.border.red(), windowPalette.border.green(), windowPalette.border.blue(), scaledAlpha(windowPalette.border.alpha())), 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(frameRect, radius, radius);

    QPainterPath clipPath;
    clipPath.addRoundedRect(frameRect, radius, radius);
    painter.save();
    painter.setClipPath(clipPath);

    switch (m_uiStyle) {
    case UiStyle::Glass: {
        QLinearGradient sheen(frameRect.topLeft(), frameRect.bottomLeft());
        sheen.setColorAt(0.0, QColor(238, 248, 255, scaledAlpha(26)));
        sheen.setColorAt(0.48, QColor(204, 222, 242, scaledAlpha(10)));
        sheen.setColorAt(1.0, QColor(24, 38, 58, scaledAlpha(14)));
        painter.fillPath(clipPath, sheen);

        QLinearGradient sideGlow(frameRect.topLeft(), frameRect.topRight());
        sideGlow.setColorAt(0.0, QColor(224, 240, 255, scaledAlpha(14)));
        sideGlow.setColorAt(0.5, QColor(224, 240, 255, 0));
        sideGlow.setColorAt(1.0, QColor(180, 210, 238, scaledAlpha(10)));
        painter.fillPath(clipPath, sideGlow);
        break;
    }
    case UiStyle::Mist: {
        QLinearGradient frost(frameRect.topLeft(), frameRect.bottomLeft());
        frost.setColorAt(0.0, QColor(190, 214, 238, scaledAlpha(26)));
        frost.setColorAt(0.35, QColor(146, 178, 208, scaledAlpha(14)));
        frost.setColorAt(1.0, QColor(92, 126, 162, scaledAlpha(8)));
        painter.fillPath(clipPath, frost);

        QLinearGradient veil(frameRect.topLeft(), frameRect.bottomLeft());
        veil.setColorAt(0.0, QColor(255, 255, 255, scaledAlpha(8)));
        veil.setColorAt(0.5, QColor(255, 255, 255, 0));
        veil.setColorAt(1.0, QColor(4, 8, 14, scaledAlpha(34)));
        painter.fillPath(clipPath, veil);

        painter.setPen(QPen(QColor(170, 196, 222, scaledAlpha(14)), 1.0));
        const int noiseStep = qMax(4, static_cast<int>(5.0 * m_uiScale));
        for (int y = static_cast<int>(frameRect.top()) + 3; y < static_cast<int>(frameRect.bottom()) - 2; y += noiseStep) {
            for (int x = static_cast<int>(frameRect.left()) + 3; x < static_cast<int>(frameRect.right()) - 2; x += noiseStep) {
                const int hash = ((x * 31) ^ (y * 17)) & 63;
                if (hash <= 1) {
                    painter.drawPoint(x, y);
                }
            }
        }
        break;
    }
    case UiStyle::Sunrise: {
        QLinearGradient sky(frameRect.topLeft(), frameRect.bottomLeft());
        sky.setColorAt(0.0, QColor(98, 76, 130, scaledAlpha(42)));
        sky.setColorAt(0.46, QColor(212, 126, 144, scaledAlpha(28)));
        sky.setColorAt(1.0, QColor(252, 164, 112, scaledAlpha(18)));
        painter.fillPath(clipPath, sky);

        QRadialGradient sun(frameRect.center().x(),
                            frameRect.bottom() - (frameRect.height() * 0.08),
                            frameRect.width() * 0.9);
        sun.setColorAt(0.0, QColor(255, 228, 166, scaledAlpha(60)));
        sun.setColorAt(0.4, QColor(255, 180, 120, scaledAlpha(22)));
        sun.setColorAt(1.0, QColor(255, 146, 96, 0));
        painter.fillPath(clipPath, sun);

        QLinearGradient glaze(frameRect.topLeft(), frameRect.bottomLeft());
        glaze.setColorAt(0.0, QColor(255, 255, 255, scaledAlpha(10)));
        glaze.setColorAt(0.6, QColor(255, 240, 220, scaledAlpha(6)));
        glaze.setColorAt(1.0, QColor(60, 34, 28, scaledAlpha(20)));
        painter.fillPath(clipPath, glaze);

        painter.setPen(QPen(QColor(255, 224, 198, scaledAlpha(12)), 1.0));
        const int noiseStep = qMax(4, static_cast<int>(5.0 * m_uiScale));
        for (int y = static_cast<int>(frameRect.top()) + 2; y < static_cast<int>(frameRect.bottom()) - 2; y += noiseStep) {
            for (int x = static_cast<int>(frameRect.left()) + 2; x < static_cast<int>(frameRect.right()) - 2; x += noiseStep) {
                const int hash = ((x * 17) ^ (y * 29)) & 63;
                if (hash <= 1) {
                    painter.drawPoint(x, y);
                }
            }
        }
        break;
    }
    case UiStyle::Meadow: {
        QRadialGradient canopy(frameRect.right() - (frameRect.width() * 0.24),
                               frameRect.top() + (frameRect.height() * 0.18),
                               frameRect.width() * 0.9);
        canopy.setColorAt(0.0, QColor(164, 220, 176, scaledAlpha(34)));
        canopy.setColorAt(0.55, QColor(96, 160, 120, scaledAlpha(16)));
        canopy.setColorAt(1.0, QColor(32, 66, 48, 0));
        painter.fillPath(clipPath, canopy);

        QLinearGradient depth(frameRect.topLeft(), frameRect.bottomLeft());
        depth.setColorAt(0.0, QColor(234, 248, 236, scaledAlpha(8)));
        depth.setColorAt(1.0, QColor(20, 62, 40, scaledAlpha(20)));
        painter.fillPath(clipPath, depth);
        break;
    }
    case UiStyle::Graphite: {
        QLinearGradient sheen(frameRect.topLeft(), frameRect.bottomLeft());
        sheen.setColorAt(0.0, QColor(214, 224, 242, scaledAlpha(18)));
        sheen.setColorAt(0.4, QColor(140, 156, 182, scaledAlpha(8)));
        sheen.setColorAt(1.0, QColor(0, 0, 0, scaledAlpha(34)));
        painter.fillPath(clipPath, sheen);

        painter.setPen(QPen(QColor(166, 182, 206, scaledAlpha(8)), 1.0));
        const int lineStep = qMax(5, static_cast<int>(7.0 * m_uiScale));
        for (int y = static_cast<int>(frameRect.top()) + lineStep; y < static_cast<int>(frameRect.bottom()); y += lineStep) {
            painter.drawLine(static_cast<int>(frameRect.left()) + 2,
                             y,
                             static_cast<int>(frameRect.right()) - 2,
                             y);
        }
        break;
    }
    case UiStyle::Paper: {
        QLinearGradient paperDepth(frameRect.topLeft(), frameRect.bottomLeft());
        paperDepth.setColorAt(0.0, QColor(255, 252, 242, scaledAlpha(10)));
        paperDepth.setColorAt(1.0, QColor(130, 102, 70, scaledAlpha(12)));
        painter.fillPath(clipPath, paperDepth);

        QColor grain = QColor(122, 98, 68, scaledAlpha(24));
        painter.setPen(QPen(grain, 1.0));
        const int step = qMax(5, static_cast<int>(7.0 * m_uiScale));
        for (int y = static_cast<int>(frameRect.top()) + step; y < static_cast<int>(frameRect.bottom()); y += step) {
            painter.drawLine(static_cast<int>(frameRect.left()) + 6,
                             y,
                             static_cast<int>(frameRect.right()) - 6,
                             y);
        }
        break;
    }
    case UiStyle::Pixel: {
        painter.fillPath(clipPath, QColor(6, 10, 7, scaledAlpha(188)));

        const int cell = qMax(6, static_cast<int>(10.0 * m_uiScale));
        painter.setPen(QPen(QColor(72, 132, 82, scaledAlpha(34)), 1.0));
        for (int y = static_cast<int>(frameRect.top()) + cell; y < static_cast<int>(frameRect.bottom()); y += cell) {
            painter.drawLine(static_cast<int>(frameRect.left()) + 1,
                             y,
                             static_cast<int>(frameRect.right()) - 1,
                             y);
        }
        for (int x = static_cast<int>(frameRect.left()) + cell; x < static_cast<int>(frameRect.right()); x += cell) {
            painter.drawLine(x,
                             static_cast<int>(frameRect.top()) + 1,
                             x,
                             static_cast<int>(frameRect.bottom()) - 1);
        }

        painter.setPen(QPen(QColor(108, 182, 118, scaledAlpha(26)), 1.0));
        const int noiseStep = qMax(3, static_cast<int>(4.0 * m_uiScale));
        for (int y = static_cast<int>(frameRect.top()) + 2; y < static_cast<int>(frameRect.bottom()) - 1; y += noiseStep) {
            for (int x = static_cast<int>(frameRect.left()) + 2; x < static_cast<int>(frameRect.right()) - 1; x += noiseStep) {
                const int hash = ((x * 13) ^ (y * 17)) & 31;
                if (hash <= 1) {
                    painter.drawPoint(x, y);
                }
            }
        }

        QLinearGradient scanline(frameRect.topLeft(), frameRect.bottomLeft());
        scanline.setColorAt(0.0, QColor(160, 255, 170, scaledAlpha(12)));
        scanline.setColorAt(0.5, QColor(40, 70, 46, scaledAlpha(4)));
        scanline.setColorAt(1.0, QColor(6, 14, 8, 0));
        painter.fillPath(clipPath, scanline);
        break;
    }
    case UiStyle::Neon: {
        QRadialGradient pulse(frameRect.center().x(), frameRect.center().y(), frameRect.width() * 0.9);
        pulse.setColorAt(0.0, QColor(224, 128, 255, scaledAlpha(38)));
        pulse.setColorAt(0.5, QColor(98, 230, 255, scaledAlpha(18)));
        pulse.setColorAt(1.0, QColor(20, 12, 36, 0));
        painter.fillPath(clipPath, pulse);

        QLinearGradient edge(frameRect.topLeft(), frameRect.bottomLeft());
        edge.setColorAt(0.0, QColor(255, 184, 255, scaledAlpha(22)));
        edge.setColorAt(1.0, QColor(0, 255, 240, scaledAlpha(12)));
        painter.fillPath(clipPath, edge);
        break;
    }
    case UiStyle::Clay: {
        QLinearGradient matte(frameRect.topLeft(), frameRect.bottomLeft());
        matte.setColorAt(0.0, QColor(255, 238, 218, scaledAlpha(34)));
        matte.setColorAt(0.4, QColor(222, 182, 146, scaledAlpha(16)));
        matte.setColorAt(1.0, QColor(108, 68, 46, scaledAlpha(22)));
        painter.fillPath(clipPath, matte);

        QRadialGradient soft(frameRect.left() + (frameRect.width() * 0.28),
                             frameRect.top() + (frameRect.height() * 0.2),
                             frameRect.width() * 0.84);
        soft.setColorAt(0.0, QColor(255, 228, 196, scaledAlpha(18)));
        soft.setColorAt(1.0, QColor(188, 120, 82, 0));
        painter.fillPath(clipPath, soft);
        break;
    }
    }

    painter.restore();
}

void MainWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);

    if (!m_blurInitialized) {
#ifdef Q_OS_WIN
        const auto hwnd = reinterpret_cast<HWND>(winId());
        if (hwnd != nullptr) {
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);
            SetWindowPos(hwnd,
                         nullptr,
                         0,
                         0,
                         0,
                         0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
#endif
        m_blurInitialized = true;
    }

    updateWindowBackdrop();

    if (!m_hasPositioned && !m_hasRestoredPosition) {
        moveToDefaultPosition();
        m_hasPositioned = true;
    }
}

void MainWindow::updateWindowBackdrop() {
    if (!m_blurInitialized) {
        return;
    }

    if (styleUsesWindowBackdrop(m_uiStyle)) {
        WindowsBlurHelper::enableForWindow(this);
    } else {
        WindowsBlurHelper::disableForWindow(this);
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_geometryAnimation != nullptr) {
        m_geometryAnimation->stop();
    }
    if (m_scrollBarHideTimer != nullptr) {
        m_scrollBarHideTimer->stop();
    }
    if (m_edgeFadeUpdateTimer != nullptr) {
        m_edgeFadeUpdateTimer->stop();
    }
    if (m_blurInitialized) {
        WindowsBlurHelper::disableForWindow(this);
        m_blurInitialized = false;
    }
    QWidget::closeEvent(event);
}

void MainWindow::initializeWindow() {
    setWindowTitle(QStringLiteral("glassNote"));
    resize(constants::kInitialWindowWidth, constants::kInitialWindowHeight);
    setMinimumSize(constants::kInitialWindowWidth, constants::kInitialWindowHeight);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAcceptDrops(true);
}

void MainWindow::initializeLayout() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(constants::kBoardPadding,
                               constants::kBoardPadding,
                               constants::kBoardPadding,
                               constants::kBoardPadding);
    layout->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    const int scrollBarWidth = scrollBarWidthForScale(m_uiScale);
    m_scrollArea->setStyleSheet(ThemeHelper::scrollAreaStyleSheet(m_uiStyle, scrollBarWidth));

    m_boardWidget = new NotesBoardWidget();
    m_scrollArea->setWidget(m_boardWidget);
    layout->addWidget(m_scrollArea);

    m_topFade = new EdgeFadeWidget(true, m_scrollArea->viewport());
    m_bottomFade = new EdgeFadeWidget(false, m_scrollArea->viewport());
    const WindowPalette windowPalette = ThemeHelper::windowPalette(m_uiStyle);
    static_cast<EdgeFadeWidget *>(m_topFade)->setBaseColor(windowPalette.edgeFadeBase);
    static_cast<EdgeFadeWidget *>(m_bottomFade)->setBaseColor(windowPalette.edgeFadeBase);
    m_topFade->hide();
    m_bottomFade->hide();

    auto *verticalScrollBar = m_scrollArea->verticalScrollBar();
    m_scrollArea->viewport()->installEventFilter(this);
    m_boardWidget->installEventFilter(this);
    verticalScrollBar->hide();

    m_scrollBarHideTimer = new QTimer(this);
    m_scrollBarHideTimer->setSingleShot(true);
    m_scrollBarHideTimer->setInterval(900);
    connect(m_scrollBarHideTimer, &QTimer::timeout, this, [this]() {
        if (m_scrollArea == nullptr) {
            return;
        }

        QScrollBar *bar = m_scrollArea->verticalScrollBar();
        if (bar != nullptr && !bar->isSliderDown()) {
            bar->hide();
        }
    });

    m_edgeFadeUpdateTimer = new QTimer(this);
    m_edgeFadeUpdateTimer->setSingleShot(true);
    m_edgeFadeUpdateTimer->setInterval(16);
    connect(m_edgeFadeUpdateTimer, &QTimer::timeout, this, [this]() {
        updateEdgeFadeWidgets();
    });

    connect(verticalScrollBar, &QScrollBar::valueChanged, this, [this]() {
        scheduleEdgeFadeUpdate();
    });
    connect(verticalScrollBar, &QScrollBar::rangeChanged, this, [this]() {
        if (m_scrollArea == nullptr) {
            return;
        }

        QScrollBar *bar = m_scrollArea->verticalScrollBar();
        if (bar != nullptr && bar->maximum() <= 0) {
            bar->hide();
        }
        scheduleEdgeFadeUpdate();
    });

    connect(m_boardWidget, &NotesBoardWidget::noteTextCommitted, this, [this](const QString &noteId, const QString &text) {
        emit noteTextCommitted(noteId, text);
        updateWindowGeometry();
    });
    connect(m_boardWidget, &NotesBoardWidget::layoutRefreshRequested, this, [this]() {
        updateWindowGeometry();
    });
    connect(m_boardWidget, &NotesBoardWidget::addNoteRequested, this, &MainWindow::addNoteRequested);
    connect(m_boardWidget, &NotesBoardWidget::clearEmptyRequested, this, &MainWindow::clearEmptyRequested);
    connect(m_boardWidget, &NotesBoardWidget::scaleInRequested, this, &MainWindow::scaleInRequested);
    connect(m_boardWidget, &NotesBoardWidget::scaleOutRequested, this, &MainWindow::scaleOutRequested);
    connect(m_boardWidget, &NotesBoardWidget::scaleResetRequested, this, &MainWindow::scaleResetRequested);
    connect(m_boardWidget,
            &NotesBoardWidget::baseLayerOpacitySetRequested,
            this,
            &MainWindow::baseLayerOpacitySetRequested);
    connect(m_boardWidget, &NotesBoardWidget::exportJsonRequested, this, &MainWindow::exportJsonRequested);
    connect(m_boardWidget, &NotesBoardWidget::importJsonRequested, this, &MainWindow::importJsonRequested);
    connect(m_boardWidget, &NotesBoardWidget::backupSnapshotRequested, this, &MainWindow::backupSnapshotRequested);
    connect(m_boardWidget,
            &NotesBoardWidget::restoreLatestBackupRequested,
            this,
            &MainWindow::restoreLatestBackupRequested);
    connect(m_boardWidget, &NotesBoardWidget::externalFileSyncToggled, this, &MainWindow::externalFileSyncToggled);
    connect(m_boardWidget, &NotesBoardWidget::alwaysOnTopToggled, this, &MainWindow::alwaysOnTopToggled);
    connect(m_boardWidget, &NotesBoardWidget::launchAtStartupToggled, this, &MainWindow::launchAtStartupToggled);
    connect(m_boardWidget, &NotesBoardWidget::autoCheckUpdatesToggled, this, &MainWindow::autoCheckUpdatesToggled);
    connect(m_boardWidget, &NotesBoardWidget::windowLockToggled, this, &MainWindow::windowLockToggled);
    connect(m_boardWidget, &NotesBoardWidget::checkForUpdatesRequested, this, &MainWindow::checkForUpdatesRequested);
    connect(m_boardWidget, &NotesBoardWidget::reminderSetRequested, this, &MainWindow::reminderSetRequested);
    connect(m_boardWidget,
            &NotesBoardWidget::reminderClearedRequested,
            this,
            &MainWindow::reminderClearedRequested);
    connect(m_boardWidget, &NotesBoardWidget::timelineReplayRequested, this, &MainWindow::timelineReplayRequested);
    connect(m_boardWidget, &NotesBoardWidget::openStorageDirectoryRequested, this, &MainWindow::openStorageDirectoryRequested);
    connect(m_boardWidget, &NotesBoardWidget::quitRequested, this, &MainWindow::quitRequested);
    connect(m_boardWidget, &NotesBoardWidget::noteDeleteRequested, this, &MainWindow::noteDeleteRequested);
    connect(m_boardWidget, &NotesBoardWidget::noteHueChangeRequested, this, &MainWindow::noteHueChangeRequested);
    connect(m_boardWidget,
            &NotesBoardWidget::noteStickerChangeRequested,
            this,
            &MainWindow::noteStickerChangeRequested);
    connect(m_boardWidget, &NotesBoardWidget::noteLaneChangeRequested, this, &MainWindow::noteLaneChangeRequested);
    connect(m_boardWidget, &NotesBoardWidget::uiStyleChangeRequested, this, &MainWindow::uiStyleChangeRequested);
}

void MainWindow::moveToDefaultPosition() {
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }

    const QRect available = screen->availableGeometry();
    const int targetX = available.left();
    const int targetY = available.bottom() - height();
    setGeometry(clampedGeometry(QRect(QPoint(targetX, targetY), size())));
}

void MainWindow::updateWindowGeometry() {
    if (m_boardWidget == nullptr || m_scrollArea == nullptr) {
        return;
    }

    const QScreen *currentScreen = this->screen() != nullptr ? this->screen() : QGuiApplication::primaryScreen();
    if (currentScreen == nullptr) {
        return;
    }

    const int scaledPadding = boardPaddingForScale(m_uiScale);
    const QSize minimumSizeHint = scaledMinimumSize();
    const int targetWidth = m_hasSavedSize ? qMax(m_savedWindowSize.width(), minimumSizeHint.width()) : minimumSizeHint.width();
    const int minimumHeight = minimumSizeHint.height();
    const QRect available = currentScreen->availableGeometry();
    const int maximumHeight = qMax(minimumHeight, available.height() - (constants::kWindowScreenMargin * 2));
    const int boardHeight = m_boardWidget->totalContentHeight();
    const int preferredHeight = m_hasSavedSize ? qMax(m_savedWindowSize.height(), minimumHeight)
                                               : qMax(minimumHeight, boardHeight + (scaledPadding * 2));
    const int desiredHeight = qMax(minimumHeight, preferredHeight);
    const int targetHeight = qMin(desiredHeight, maximumHeight);

    setMinimumSize(minimumSizeHint);
    setMaximumHeight(maximumHeight);
    const QRect targetGeometry = clampedGeometry(QRect(pos(), QSize(targetWidth, targetHeight)));

    if (!isVisible()) {
        m_updatingGeometryInternally = true;
        setGeometry(targetGeometry);
        m_updatingGeometryInternally = false;
        updateEdgeFadeWidgets();
        return;
    }

    if (geometry() == targetGeometry) {
        return;
    }

    m_updatingGeometryInternally = true;
    m_geometryAnimation->stop();
    m_geometryAnimation->setStartValue(geometry());
    m_geometryAnimation->setEndValue(targetGeometry);
    connect(m_geometryAnimation, &QPropertyAnimation::finished, this, [this]() {
        m_updatingGeometryInternally = false;
        updateEdgeFadeWidgets();
    }, Qt::SingleShotConnection);
    m_geometryAnimation->start();
}

void MainWindow::updateEdgeFadeWidgets() {
    if (m_scrollArea == nullptr || m_topFade == nullptr || m_bottomFade == nullptr || m_scrollArea->viewport() == nullptr) {
        return;
    }

    QWidget *viewport = m_scrollArea->viewport();
    const QRect viewportRect = viewport->rect();
    const int fadeHeight = qMax(18, static_cast<int>(44.0 * m_uiScale));
    m_topFade->setGeometry(0, 0, viewportRect.width(), fadeHeight);
    m_bottomFade->setGeometry(0, qMax(0, viewportRect.height() - fadeHeight), viewportRect.width(), fadeHeight);

    QScrollBar *verticalBar = m_scrollArea->verticalScrollBar();
    const int scrollMaximum = verticalBar != nullptr ? verticalBar->maximum() : 0;
    const int scrollValue = verticalBar != nullptr ? verticalBar->value() : 0;
    const bool canScroll = scrollMaximum > 0;
    const qreal edgeRampDistance = qMax(22.0, static_cast<qreal>(fadeHeight) * 1.25);
    const qreal topFactor = canScroll
                                ? qBound(0.0,
                                         static_cast<qreal>(scrollValue) / edgeRampDistance,
                                         1.0)
                                : 0.0;
    const qreal bottomFactor = canScroll
                                   ? qBound(0.0,
                                            static_cast<qreal>(scrollMaximum - scrollValue) / edgeRampDistance,
                                            1.0)
                                   : 0.0;
    const qreal maxFadeOpacity = 0.62;
    const qreal topOpacity = topFactor * maxFadeOpacity;
    const qreal bottomOpacity = bottomFactor * maxFadeOpacity;

    auto *topFade = static_cast<EdgeFadeWidget *>(m_topFade);
    auto *bottomFade = static_cast<EdgeFadeWidget *>(m_bottomFade);
    const WindowPalette windowPalette = ThemeHelper::windowPalette(m_uiStyle);
    topFade->setBaseColor(windowPalette.edgeFadeBase);
    bottomFade->setBaseColor(windowPalette.edgeFadeBase);
    topFade->setOpacity(topOpacity);
    bottomFade->setOpacity(bottomOpacity);
    m_topFade->setVisible(topOpacity > 0.01);
    m_bottomFade->setVisible(bottomOpacity > 0.01);
    m_topFade->raise();
    m_bottomFade->raise();
}

void MainWindow::scheduleEdgeFadeUpdate() {
    if (m_edgeFadeUpdateTimer == nullptr) {
        updateEdgeFadeWidgets();
        return;
    }

    if (!m_edgeFadeUpdateTimer->isActive()) {
        m_edgeFadeUpdateTimer->start();
    }
}

void MainWindow::revealScrollBarTemporarily() {
    if (m_scrollArea == nullptr) {
        return;
    }

    QScrollBar *bar = m_scrollArea->verticalScrollBar();
    if (bar == nullptr || bar->maximum() <= 0) {
        return;
    }

    if (!bar->isVisible()) {
        bar->show();
    }
    scheduleEdgeFadeUpdate();
    if (m_scrollBarHideTimer != nullptr) {
        m_scrollBarHideTimer->start();
    }
}

bool MainWindow::forwardWheelToScrollArea(QWheelEvent *event) {
    if (event == nullptr) {
        return false;
    }

    const QPoint globalPos = event->globalPosition().toPoint();

    const QPoint pixelDelta = event->pixelDelta();
    if (!pixelDelta.isNull()) {
        return scrollBoardByPixelDelta(pixelDelta.y()) || scrollFallbackByPixelDelta(globalPos, pixelDelta.y());
    }

    const QPoint angleDelta = event->angleDelta();
    if (!angleDelta.isNull()) {
        return scrollBoardByAngleDelta(angleDelta.y()) || scrollFallbackByAngleDelta(globalPos, angleDelta.y());
    }

    return false;
}

bool MainWindow::isTextDropMimeData(const QMimeData *mimeData) const {
    if (mimeData == nullptr) {
        return false;
    }

    if (!mimeData->hasText() || mimeData->hasUrls() || mimeData->hasImage()) {
        return false;
    }

    return !mimeData->text().trimmed().isEmpty();
}

bool MainWindow::canHandleDropMimeData(const QMimeData *mimeData) const {
    if (mimeData == nullptr) {
        return false;
    }

    if (mimeData->hasText() && !mimeData->text().trimmed().isEmpty()) {
        return true;
    }

    if (mimeData->hasUrls() && !mimeData->urls().isEmpty()) {
        return true;
    }

    return mimeData->hasImage();
}

bool MainWindow::isInEdgeDropZone(const QPoint &pos) const {
    if (!rect().contains(pos)) {
        return false;
    }

    const bool nearLeft = pos.x() <= kEdgeDropCaptureZonePx;
    const bool nearRight = pos.x() >= rect().width() - kEdgeDropCaptureZonePx;
    const bool nearTop = pos.y() <= kEdgeDropCaptureZonePx;
    const bool nearBottom = pos.y() >= rect().height() - kEdgeDropCaptureZonePx;
    return nearLeft || nearRight || nearTop || nearBottom;
}

QString MainWindow::payloadFromDropMimeData(const QMimeData *mimeData) const {
    if (mimeData == nullptr) {
        return QString();
    }

    const QString droppedText = mimeData->text().trimmed();
    if (!droppedText.isEmpty()) {
        return droppedText;
    }

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            if (!url.isLocalFile()) {
                continue;
            }

            const QString localPath = url.toLocalFile();
            const QString suffix = QFileInfo(localPath).suffix().toLower();
            if (suffix == QStringLiteral("png")
                || suffix == QStringLiteral("jpg")
                || suffix == QStringLiteral("jpeg")
                || suffix == QStringLiteral("bmp")
                || suffix == QStringLiteral("gif")
                || suffix == QStringLiteral("webp")) {
                return QStringLiteral("[拖拽图片] %1").arg(localPath);
            }
            return localPath;
        }
    }

    if (mimeData->hasImage()) {
        return QStringLiteral("[拖拽图片] 已捕获图像数据（实验能力可接入 OCR）");
    }

    return QString();
}

bool MainWindow::scrollBoardByPixelDelta(int deltaY) {
    if (deltaY == 0 || m_scrollArea == nullptr) {
        return false;
    }

    QScrollBar *bar = m_scrollArea->verticalScrollBar();
    if (bar == nullptr || bar->maximum() <= 0) {
        return false;
    }

    const int previousValue = bar->value();
    bar->setValue(previousValue - deltaY);
    if (bar->value() == previousValue) {
        return false;
    }

    revealScrollBarTemporarily();
    scheduleEdgeFadeUpdate();
    return true;
}

bool MainWindow::scrollBoardByAngleDelta(int deltaY) {
    if (deltaY == 0 || m_scrollArea == nullptr) {
        return false;
    }

    QScrollBar *bar = m_scrollArea->verticalScrollBar();
    if (bar == nullptr || bar->maximum() <= 0) {
        return false;
    }

    m_angleWheelRemainder += deltaY;
    const int steps = m_angleWheelRemainder / WHEEL_DELTA;
    m_angleWheelRemainder %= WHEEL_DELTA;
    if (steps == 0) {
        return false;
    }

    const int scrollStep = qMax(1, bar->singleStep() * 3);
    const int previousValue = bar->value();
    bar->setValue(previousValue - (steps * scrollStep));
    if (bar->value() == previousValue) {
        return false;
    }

    revealScrollBarTemporarily();
    scheduleEdgeFadeUpdate();
    return true;
}

bool MainWindow::scrollFallbackByPixelDelta(const QPoint &globalPos, int deltaY) {
    if (deltaY == 0) {
        return false;
    }

    QScrollBar *bar = resolveFallbackVerticalScrollBar(globalPos);
    if (bar == nullptr) {
        return false;
    }

    const int previousValue = bar->value();
    bar->setValue(previousValue - deltaY);
    return bar->value() != previousValue;
}

bool MainWindow::scrollFallbackByAngleDelta(const QPoint &globalPos, int deltaY) {
    if (deltaY == 0) {
        return false;
    }

    QScrollBar *bar = resolveFallbackVerticalScrollBar(globalPos);
    if (bar == nullptr) {
        return false;
    }

    m_fallbackAngleWheelRemainder += deltaY;
    const int steps = m_fallbackAngleWheelRemainder / WHEEL_DELTA;
    m_fallbackAngleWheelRemainder %= WHEEL_DELTA;
    if (steps == 0) {
        return false;
    }

    const int scrollStep = qMax(1, bar->singleStep() * 3);
    const int previousValue = bar->value();
    bar->setValue(previousValue - (steps * scrollStep));
    return bar->value() != previousValue;
}

QScrollBar *MainWindow::resolveFallbackVerticalScrollBar(const QPoint &globalPos) const {
    const auto resolveFromWidget = [this](QWidget *start) -> QScrollBar * {
        for (QWidget *candidate = start; candidate != nullptr; candidate = candidate->parentWidget()) {
            auto *scrollArea = qobject_cast<QAbstractScrollArea *>(candidate);
            if (scrollArea == nullptr || scrollArea == m_scrollArea) {
                continue;
            }
            if (scrollArea->window() != this) {
                continue;
            }

            QScrollBar *bar = scrollArea->verticalScrollBar();
            if (bar != nullptr && bar->isEnabled() && bar->maximum() > 0) {
                return bar;
            }
        }
        return nullptr;
    };

    if (!globalPos.isNull()) {
        QWidget *hoveredWidget = QApplication::widgetAt(globalPos);
        if (hoveredWidget != nullptr && hoveredWidget->window() == this) {
            if (QScrollBar *bar = resolveFromWidget(hoveredWidget)) {
                return bar;
            }
        }
    }

    QWidget *focusWidget = QApplication::focusWidget();
    if (focusWidget != nullptr && focusWidget->window() == this) {
        if (QScrollBar *bar = resolveFromWidget(focusWidget)) {
            return bar;
        }
    }

    return nullptr;
}

QRect MainWindow::clampedGeometry(const QRect &targetGeometry) const {
    const QScreen *currentScreen = this->screen() != nullptr ? this->screen() : QGuiApplication::primaryScreen();
    if (currentScreen == nullptr) {
        return targetGeometry;
    }

    const QRect available = currentScreen->availableGeometry();
    const int width = qMin(targetGeometry.width(), available.width());
    const int height = qMin(targetGeometry.height(), available.height());
    const int maxX = qMax(available.left(), available.right() - width + 1);
    const int maxY = qMax(available.top(), available.bottom() - height + 1);
    const int x = qBound(available.left(), targetGeometry.x(), maxX);
    const int y = qBound(available.top(), targetGeometry.y(), maxY);
    return QRect(x, y, width, height);
}

QRect MainWindow::adjustedResizeGeometry(const QRect &targetGeometry, Qt::Edges activeEdges) const {
    const QSize minimumSizeHint = scaledMinimumSize();
    QRect adjusted = targetGeometry;

    if (adjusted.width() < minimumSizeHint.width()) {
        if (activeEdges.testFlag(Qt::LeftEdge)) {
            adjusted.setLeft(adjusted.right() - minimumSizeHint.width() + 1);
        } else {
            adjusted.setWidth(minimumSizeHint.width());
        }
    }

    if (adjusted.height() < minimumSizeHint.height()) {
        if (activeEdges.testFlag(Qt::TopEdge)) {
            adjusted.setTop(adjusted.bottom() - minimumSizeHint.height() + 1);
        } else {
            adjusted.setHeight(minimumSizeHint.height());
        }
    }

    return clampedGeometry(adjusted);
}

QSize MainWindow::scaledMinimumSize() const {
    const int baseWidth = static_cast<int>(constants::kInitialWindowWidth * m_uiScale);
    const int baseHeight = static_cast<int>(constants::kInitialWindowHeight * m_uiScale);
    const int scaledPadding = boardPaddingForScale(m_uiScale);
    const int contentWidth = m_boardWidget != nullptr ? m_boardWidget->requiredContentWidth() : 0;
    const int scrollAllowance = static_cast<int>(12.0 * m_uiScale);
    const int minimumWidth = qMax(baseWidth, contentWidth + (scaledPadding * 2) + scrollAllowance);
    return QSize(minimumWidth, baseHeight);
}

#ifdef Q_OS_WIN
QRectF MainWindow::resizeReferenceRect() const {
    if (m_scrollArea != nullptr) {
        return m_scrollArea->geometry().adjusted(-1, -1, 1, 1);
    }

    return rect().adjusted(constants::kBoardPadding,
                           constants::kBoardPadding,
                           -constants::kBoardPadding,
                           -constants::kBoardPadding);
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType)

    MSG *msg = static_cast<MSG *>(message);
    if (msg == nullptr) {
        return QWidget::nativeEvent(eventType, message, result);
    }

    if (msg->message == WM_MOUSEWHEEL) {
        const int wheelDelta = GET_WHEEL_DELTA_WPARAM(msg->wParam);
        const QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        if (scrollBoardByAngleDelta(wheelDelta) || scrollFallbackByAngleDelta(globalPos, wheelDelta)) {
            if (result != nullptr) {
                *result = 0;
            }
            return true;
        }
    }

    if (msg->message != WM_NCHITTEST) {
        return QWidget::nativeEvent(eventType, message, result);
    }

    const QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
    const int hit = resizeHitTest(globalPos);
    if (hit != HTCLIENT) {
        *result = hit;
        return true;
    }

    return QWidget::nativeEvent(eventType, message, result);
}

int MainWindow::resizeHitTest(const QPoint &globalPos) const {
    if (isMaximized() || m_windowLocked) {
        return HTCLIENT;
    }

    const QPoint local = mapFromGlobal(globalPos);
    const QRectF outerRect = resizeReferenceRect();
    const qreal borderWidth = qMax<qreal>(8.0, 10.0 * m_uiScale);
    const qreal radius = qMax<qreal>(14.0, (26.0 * m_uiScale) - (constants::kBoardPadding * 0.5));

    if (!outerRect.contains(QPointF(local))) {
        return HTCLIENT;
    }

    QPainterPath outerPath;
    outerPath.addRoundedRect(outerRect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    if (!outerPath.contains(QPointF(local))) {
        return HTCLIENT;
    }

    const QRectF innerRect = outerRect.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
    QPainterPath innerPath;
    const qreal innerRadius = qMax<qreal>(0.0, radius - borderWidth);
    if (innerRect.width() > 0.0 && innerRect.height() > 0.0) {
        innerPath.addRoundedRect(innerRect, innerRadius, innerRadius);
    }

    if (innerPath.contains(QPointF(local))) {
        return HTCLIENT;
    }

    const bool left = local.x() >= outerRect.left() && local.x() <= outerRect.left() + borderWidth;
    const bool right = local.x() <= outerRect.right() && local.x() >= outerRect.right() - borderWidth;
    const bool top = local.y() >= outerRect.top() && local.y() <= outerRect.top() + borderWidth;
    const bool bottom = local.y() <= outerRect.bottom() && local.y() >= outerRect.bottom() - borderWidth;

    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }

    return HTCLIENT;
}

Qt::Edges MainWindow::edgesForGlobalPos(const QPoint &globalPos) const {
    const int hit = resizeHitTest(globalPos);
    switch (hit) {
    case HTLEFT:
        return Qt::LeftEdge;
    case HTRIGHT:
        return Qt::RightEdge;
    case HTTOP:
        return Qt::TopEdge;
    case HTBOTTOM:
        return Qt::BottomEdge;
    case HTTOPLEFT:
        return Qt::TopEdge | Qt::LeftEdge;
    case HTTOPRIGHT:
        return Qt::TopEdge | Qt::RightEdge;
    case HTBOTTOMLEFT:
        return Qt::BottomEdge | Qt::LeftEdge;
    case HTBOTTOMRIGHT:
        return Qt::BottomEdge | Qt::RightEdge;
    default:
        return Qt::Edges();
    }
}
#endif

}  // namespace glassnote
