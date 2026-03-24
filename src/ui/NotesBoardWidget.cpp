#include "ui/NotesBoardWidget.h"

#include <algorithm>
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QHash>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <utility>

#include "animation/AnimationCoordinator.h"
#include "common/Constants.h"
#include "ui/NoteCardWidget.h"

namespace glassnote {

namespace {

int boardSpacingForScale(qreal scale) {
    if (scale < 0.95) {
        return static_cast<int>(constants::kBoardSpacing * scale * 0.9);
    }
    if (scale > 1.2) {
        return static_cast<int>(constants::kBoardSpacing * scale * 1.08);
    }
    return static_cast<int>(constants::kBoardSpacing * scale);
}

qreal contentFontScaleForUiScale(qreal scale) {
    if (scale < 0.95) {
        return 0.96;
    }
    if (scale > 1.2) {
        return 1.04;
    }
    return 1.0;
}

void applyLaneHeaderStyle(QLabel *header, UiStyle uiStyle, qreal uiScale) {
    if (header == nullptr) {
        return;
    }

    uiStyle = normalizedUiStyle(uiStyle);

    QFont headerFont = QApplication::font();
    headerFont.setBold(true);
    headerFont.setPointSizeF(10.0 * uiScale);
    if (uiStyle == UiStyle::Pixel) {
        headerFont.setFamily(QStringLiteral("Consolas"));
        headerFont.setStyleHint(QFont::TypeWriter);
        headerFont.setFixedPitch(true);
    } else {
        headerFont.setFamily(QStringLiteral("Segoe UI"));
        headerFont.setStyleHint(QFont::SansSerif);
        headerFont.setFixedPitch(false);
    }
    header->setFont(headerFont);

    if (uiStyle == UiStyle::Pixel) {
        header->setStyleSheet(QStringLiteral(
            "QLabel {"
            "color: rgba(176, 240, 176, 228);"
            "padding: 4px 2px 2px 2px;"
            "font-family: 'Consolas', 'Courier New', monospace;"
            "letter-spacing: 1px;"
            "}"));
        return;
    } else if (uiStyle == UiStyle::Paper || uiStyle == UiStyle::Clay) {
        header->setStyleSheet(QStringLiteral(
            "QLabel {"
            "color: rgba(94, 68, 46, 218);"
            "padding: 4px 2px 2px 2px;"
            "font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif;"
            "}"));
        return;
    } else if (uiStyle == UiStyle::Neon) {
        header->setStyleSheet(QStringLiteral(
            "QLabel {"
            "color: rgba(238, 192, 255, 226);"
            "padding: 4px 2px 2px 2px;"
            "font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif;"
            "}"));
        return;
    } else if (uiStyle == UiStyle::Graphite) {
        header->setStyleSheet(QStringLiteral(
            "QLabel {"
            "color: rgba(220, 230, 246, 212);"
            "padding: 4px 2px 2px 2px;"
            "font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif;"
            "}"));
        return;
    }

    header->setStyleSheet(QStringLiteral("QLabel { color: rgba(255, 255, 255, 172); padding: 4px 2px 2px 2px; }"));
}

QLabel *createLaneHeaderWidget(NoteLane lane, QWidget *parent, qreal uiScale, UiStyle uiStyle) {
    auto *header = new QLabel(noteLaneDisplayName(lane), parent);
    applyLaneHeaderStyle(header, uiStyle, uiScale);
    return header;
}

bool noteLessThanByLaneAndOrder(const NoteItem &left, const NoteItem &right) {
    const int leftLane = noteLaneSortIndex(left.lane);
    const int rightLane = noteLaneSortIndex(right.lane);
    if (leftLane != rightLane) {
        return leftLane < rightLane;
    }
    if (left.order != right.order) {
        return left.order < right.order;
    }
    return left.id < right.id;
}

bool laneFromHeaderText(const QString &text, NoteLane *lane) {
    if (lane == nullptr) {
        return false;
    }

    const QString normalizedText = text.trimmed();
    const NoteLane laneCandidates[] = {
        NoteLane::Today,
        NoteLane::Next,
        NoteLane::Waiting,
        NoteLane::Someday,
    };
    for (NoteLane candidate : laneCandidates) {
        if (normalizedText.compare(noteLaneDisplayName(candidate), Qt::CaseInsensitive) == 0) {
            *lane = candidate;
            return true;
        }
    }

    return false;
}

bool noteArrangementEquivalent(const QVector<NoteItem> &left, const QVector<NoteItem> &right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (qsizetype index = 0; index < left.size(); ++index) {
        const NoteItem &lhs = left.at(index);
        const NoteItem &rhs = right.at(index);
        if (lhs.id != rhs.id || normalizedNoteLane(lhs.lane) != normalizedNoteLane(rhs.lane)) {
            return false;
        }
    }

    return true;
}

}  // namespace

NotesBoardWidget::NotesBoardWidget(QWidget *parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(constants::kBoardSpacing);

    m_dragAutoScrollTimer = new QTimer(this);
    m_dragAutoScrollTimer->setInterval(16);
    connect(m_dragAutoScrollTimer, &QTimer::timeout, this, [this]() {
        if (!m_noteDragInProgress) {
            stopDragAutoScroll();
            return;
        }

        QScrollArea *scrollArea = hostScrollArea();
        if (scrollArea == nullptr) {
            stopDragAutoScroll();
            return;
        }

        QScrollBar *bar = scrollArea->verticalScrollBar();
        if (bar == nullptr || bar->maximum() <= 0 || m_dragAutoScrollVelocity == 0) {
            stopDragAutoScroll();
            return;
        }

        const int value = bar->value();
        const int nextValue = qBound(bar->minimum(), value + m_dragAutoScrollVelocity, bar->maximum());
        if (nextValue == value) {
            stopDragAutoScroll();
            return;
        }

        bar->setValue(nextValue);
        updateCardDragPreview(m_lastDragGlobalPos);
    });

    m_dragPreviewUpdateTimer = new QTimer(this);
    m_dragPreviewUpdateTimer->setSingleShot(true);
    m_dragPreviewUpdateTimer->setInterval(8);
    connect(m_dragPreviewUpdateTimer, &QTimer::timeout, this, [this]() {
        if (!m_noteDragInProgress) {
            return;
        }
        updateCardDragPreview(m_pendingDragPreviewGlobalPos);
    });
}

NotesBoardWidget::~NotesBoardWidget() {
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
}

int NotesBoardWidget::bestVisibleContentHeight(int maxHeight) const {
    if (m_cards.isEmpty()) {
        return 0;
    }

    const int spacing = m_layout->spacing();
    const int firstItemHeight = m_cards.constFirst()->sizeHint().height();
    if (maxHeight <= firstItemHeight) {
        return firstItemHeight;
    }

    int accumulatedHeight = 0;
    QVector<int> snapHeights;
    snapHeights.reserve(m_cards.size());

    for (int index = 0; index < m_cards.size(); ++index) {
        const int itemHeight = m_cards.at(index)->sizeHint().height();
        const int nextHeight = accumulatedHeight + itemHeight + (index > 0 ? spacing : 0);
        accumulatedHeight = nextHeight;
        snapHeights.append(accumulatedHeight);
    }

    if (snapHeights.isEmpty()) {
        return firstItemHeight;
    }

    if (maxHeight >= snapHeights.constLast()) {
        return snapHeights.constLast();
    }

    int bestHeight = snapHeights.constFirst();
    int bestDistance = qAbs(bestHeight - maxHeight);
    for (int snapHeight : std::as_const(snapHeights)) {
        const int distance = qAbs(snapHeight - maxHeight);
        if (distance < bestDistance) {
            bestHeight = snapHeight;
            bestDistance = distance;
        }
    }

    return bestHeight;
}

int NotesBoardWidget::totalContentHeight() const {
    if (m_layout == nullptr || m_layout->count() == 0) {
        return 0;
    }

    const int spacing = m_layout->spacing();
    int totalHeight = 0;
    for (int index = 0; index < m_layout->count(); ++index) {
        QLayoutItem *item = m_layout->itemAt(index);
        if (item == nullptr || item->widget() == nullptr) {
            continue;
        }
        totalHeight += item->widget()->sizeHint().height();
        if (index > 0) {
            totalHeight += spacing;
        }
    }

    return totalHeight;
}

int NotesBoardWidget::requiredContentWidth() const {
    QFont displayFont = QApplication::font();
    displayFont.setPointSizeF(11.0 * m_uiScale * contentFontScaleForUiScale(m_uiScale));
    if (m_uiStyle == UiStyle::Pixel) {
        displayFont.setFamily(QStringLiteral("Consolas"));
        displayFont.setStyleHint(QFont::TypeWriter);
        displayFont.setFixedPitch(true);
    } else {
        displayFont.setFamily(QStringLiteral("Segoe UI"));
        displayFont.setStyleHint(QFont::SansSerif);
        displayFont.setFixedPitch(false);
    }
    const QFontMetrics metrics(displayFont);

    int maxLineWidth = metrics.horizontalAdvance(QStringLiteral("双击输入内容"));
    int maxCardHintWidth = 0;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        const QString text = card->plainText();
        const QString source = text.trimmed().isEmpty() ? QStringLiteral("双击输入内容") : text;
        const QStringList lines = source.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        for (const QString &line : lines) {
            maxLineWidth = qMax(maxLineWidth, metrics.horizontalAdvance(line));
        }
        maxCardHintWidth = qMax(maxCardHintWidth, card->sizeHint().width());
    }

    for (QWidget *laneHeader : std::as_const(m_laneHeaders)) {
        if (laneHeader == nullptr) {
            continue;
        }
        maxCardHintWidth = qMax(maxCardHintWidth, laneHeader->sizeHint().width());
    }

    const int horizontalPadding = static_cast<int>((constants::kCardPaddingHorizontal * m_uiScale) * 2.0);
    const int cardFrameSlack = static_cast<int>(8.0 * m_uiScale);
    const int textBasedWidth = maxLineWidth + horizontalPadding + cardFrameSlack;
    return qMax(textBasedWidth, maxCardHintWidth);
}

QVector<NoteItem> NotesBoardWidget::notes() const {
    QVector<NoteItem> values;
    values.reserve(m_cards.size());

    for (qsizetype index = 0; index < m_cards.size(); ++index) {
        NoteCardWidget *card = m_cards.at(index);
        NoteItem item;
        item.id = card->noteId();
        item.text = card->text();
        item.lane = card->lane();
        item.hue = card->hue();
        item.sticker = card->sticker();
        item.order = static_cast<int>(index);
        values.append(item);
    }

    return values;
}

void NotesBoardWidget::setNotes(const QVector<NoteItem> &notes) {
    if (m_noteDragInProgress) {
        finishCardDrag(QCursor::pos(), true);
    }
    rebuildCards(notes);
}

void NotesBoardWidget::setImportedStickers(const QVector<QString> &stickers) {
    m_importedStickerLibrary = stickers;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setImportedStickerLibrary(m_importedStickerLibrary);
    }
}

void NotesBoardWidget::focusNoteEditor(const QString &noteId) {
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card->noteId() == noteId) {
            card->startEditing();
            return;
        }
    }
}

void NotesBoardWidget::setUiScale(qreal scale) {
    m_uiScale = scale;
    m_layout->setSpacing(boardSpacingForScale(m_uiScale));
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setUiScale(scale);
    }

    for (QWidget *laneHeader : std::as_const(m_laneHeaders)) {
        auto *label = qobject_cast<QLabel *>(laneHeader);
        if (label == nullptr) {
            continue;
        }
        applyLaneHeaderStyle(label, m_uiStyle, m_uiScale);
    }
}

void NotesBoardWidget::setBaseLayerOpacity(qreal opacity) {
    const qreal clamped = qBound(constants::kMinBaseLayerOpacity,
                                 opacity,
                                 constants::kMaxBaseLayerOpacity);
    if (qFuzzyCompare(m_baseLayerOpacity, clamped)) {
        return;
    }

    m_baseLayerOpacity = clamped;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setBaseLayerOpacity(clamped);
    }
}

void NotesBoardWidget::setUiStyle(UiStyle uiStyle) {
    const UiStyle normalizedStyle = normalizedUiStyle(uiStyle);
    if (m_uiStyle == normalizedStyle) {
        return;
    }

    m_uiStyle = normalizedStyle;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setUiStyle(normalizedStyle);
    }
    for (QWidget *laneHeader : std::as_const(m_laneHeaders)) {
        auto *label = qobject_cast<QLabel *>(laneHeader);
        if (label != nullptr) {
            applyLaneHeaderStyle(label, m_uiStyle, m_uiScale);
        }
    }
}

void NotesBoardWidget::setExternalFileSyncEnabled(bool enabled) {
    m_externalFileSyncEnabled = enabled;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setExternalFileSyncEnabled(enabled);
    }
}

void NotesBoardWidget::setAlwaysOnTopEnabled(bool enabled) {
    m_alwaysOnTopEnabled = enabled;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setAlwaysOnTopEnabled(enabled);
    }
}

void NotesBoardWidget::setLaunchAtStartupEnabled(bool enabled) {
    m_launchAtStartupEnabled = enabled;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setLaunchAtStartupEnabled(enabled);
    }
}

void NotesBoardWidget::setAutoCheckUpdatesEnabled(bool enabled) {
    m_autoCheckUpdatesEnabled = enabled;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setAutoCheckUpdatesEnabled(enabled);
    }
}

void NotesBoardWidget::setWindowLocked(bool enabled) {
    m_windowLocked = enabled;
    if (enabled && m_noteDragInProgress) {
        finishCardDrag(QCursor::pos(), true);
    }
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setWindowLocked(enabled);
    }
}

bool NotesBoardWidget::eventFilter(QObject *watched, QEvent *event) {
    Q_UNUSED(watched)

    if (m_noteDragInProgress && event != nullptr) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if ((mouseEvent->buttons() & Qt::LeftButton) == 0) {
                finishCardDrag(mouseEvent->globalPosition().toPoint(), false);
            } else {
                scheduleDragPreviewUpdate(mouseEvent->globalPosition().toPoint());
            }
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                finishCardDrag(mouseEvent->globalPosition().toPoint(), false);
                return true;
            }
            break;
        }
        case QEvent::KeyPress: {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                finishCardDrag(QCursor::pos(), true);
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void NotesBoardWidget::scheduleDragPreviewUpdate(const QPoint &globalPos) {
    if (!m_noteDragInProgress) {
        return;
    }

    m_lastDragGlobalPos = globalPos;
    m_pendingDragPreviewGlobalPos = globalPos;
    updateDragProxyPosition(globalPos);
    updateDragAutoScroll(globalPos);

    if (m_dragPreviewUpdateTimer != nullptr && !m_dragPreviewUpdateTimer->isActive()) {
        m_dragPreviewUpdateTimer->start();
    }
}

void NotesBoardWidget::handleCardDragHoldStarted(const QString &noteId, const QPoint &globalPos) {
    auto *senderCard = qobject_cast<NoteCardWidget *>(sender());
    if (m_noteDragInProgress || m_windowLocked || m_deleteAnimationRunning) {
        if (senderCard != nullptr) {
            senderCard->setNoteDragActive(false);
        }
        return;
    }

    NoteCardWidget *card = findCardById(noteId);
    if (card == nullptr) {
        if (senderCard != nullptr) {
            senderCard->setNoteDragActive(false);
        }
        return;
    }

    m_noteDragInProgress = true;
    m_draggedCard = card;
    m_draggedNoteId = noteId;
    m_dragOriginNotes = notes();
    m_dragPreviewNotes = m_dragOriginNotes;
    m_dragPreviewLane = card->lane();
    m_dragPreviewOrder = insertionOrderForLane(m_dragPreviewLane, card->geometry().center().y());
    m_hiddenCardId = m_draggedNoteId;
    m_lastDragGlobalPos = globalPos;
    m_pendingDragPreviewGlobalPos = globalPos;

    const QPoint cardGlobalTopLeft = card->mapToGlobal(QPoint(0, 0));
    const QPoint anchorOffset = globalPos - cardGlobalTopLeft;
    const QSize cardSize = card->size();
    const qreal anchorX = cardSize.width() > 0
                              ? qBound(0.0,
                                       static_cast<qreal>(anchorOffset.x()) / static_cast<qreal>(cardSize.width()),
                                       1.0)
                              : 0.5;
    const qreal anchorY = cardSize.height() > 0
                              ? qBound(0.0,
                                       static_cast<qreal>(anchorOffset.y()) / static_cast<qreal>(cardSize.height()),
                                       1.0)
                              : 0.5;
    m_dragProxyAnchorRatio = QPointF(anchorX, anchorY);
    m_dragProxySourcePixmap = card->grab();
    if (m_dragProxySourcePixmap.isNull()) {
        if (senderCard != nullptr) {
            senderCard->setNoteDragActive(false);
        }
        m_noteDragInProgress = false;
        m_draggedCard = nullptr;
        m_draggedNoteId.clear();
        m_hiddenCardId.clear();
        m_dragOriginNotes.clear();
        m_dragPreviewNotes.clear();
        return;
    }

    if (m_dragProxy == nullptr) {
        QWidget *ownerWindow = window();
        m_dragProxy = new QLabel(ownerWindow);
        Qt::WindowFlags flags = Qt::Tool
                                | Qt::FramelessWindowHint
                                | Qt::WindowStaysOnTopHint
                                | Qt::NoDropShadowWindowHint
                                | Qt::WindowDoesNotAcceptFocus;
        m_dragProxy->setWindowFlags(flags);
        m_dragProxy->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_dragProxy->setAttribute(Qt::WA_NoSystemBackground, true);
        m_dragProxy->setAttribute(Qt::WA_TranslucentBackground, true);
        m_dragProxy->setFocusPolicy(Qt::NoFocus);
    }
    if (m_dragProxyShadowEffect == nullptr) {
        m_dragProxyShadowEffect = new QGraphicsDropShadowEffect(m_dragProxy);
        m_dragProxyShadowEffect->setColor(QColor(18, 22, 28, 136));
        m_dragProxy->setGraphicsEffect(m_dragProxyShadowEffect);
    }
    m_dragProxyShadowEffect->setBlurRadius(8.0);
    m_dragProxyShadowEffect->setOffset(0.0, 2.0);

    refreshDragProxyPixmap(1.0);
    m_dragProxy->show();
    m_dragProxy->raise();

    if (m_dragProxyScaleAnimation == nullptr) {
        m_dragProxyScaleAnimation = new QVariantAnimation(this);
        connect(m_dragProxyScaleAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            refreshDragProxyPixmap(value.toReal());
            updateDragProxyPosition(m_lastDragGlobalPos);
        });
    }
    m_dragProxyScaleAnimation->stop();
    m_dragProxyScaleAnimation->setDuration(120);
    m_dragProxyScaleAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dragProxyScaleAnimation->setStartValue(1.0);
    m_dragProxyScaleAnimation->setEndValue(1.045);

    if (m_dragProxyShadowBlurAnimation == nullptr) {
        m_dragProxyShadowBlurAnimation = new QPropertyAnimation(m_dragProxyShadowEffect, "blurRadius", this);
    }
    if (m_dragProxyShadowOffsetAnimation == nullptr) {
        m_dragProxyShadowOffsetAnimation = new QPropertyAnimation(m_dragProxyShadowEffect, "offset", this);
    }
    m_dragProxyShadowBlurAnimation->stop();
    m_dragProxyShadowBlurAnimation->setDuration(130);
    m_dragProxyShadowBlurAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dragProxyShadowBlurAnimation->setStartValue(8.0);
    m_dragProxyShadowBlurAnimation->setEndValue(22.0);

    m_dragProxyShadowOffsetAnimation->stop();
    m_dragProxyShadowOffsetAnimation->setDuration(130);
    m_dragProxyShadowOffsetAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dragProxyShadowOffsetAnimation->setStartValue(QPointF(0.0, 2.0));
    m_dragProxyShadowOffsetAnimation->setEndValue(QPointF(0.0, 8.0));

    m_dragProxyScaleAnimation->start();
    m_dragProxyShadowBlurAnimation->start();
    m_dragProxyShadowOffsetAnimation->start();

    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }

    rebuildCards(m_dragPreviewNotes);
    m_draggedCard = findCardById(m_draggedNoteId);
    if (m_draggedCard != nullptr) {
        m_draggedCard->setNoteDragActive(true);
    }
    updateCardDragPreview(globalPos);
}

QScrollArea *NotesBoardWidget::hostScrollArea() const {
    QWidget *node = parentWidget();
    while (node != nullptr) {
        auto *scrollArea = qobject_cast<QScrollArea *>(node);
        if (scrollArea != nullptr) {
            return scrollArea;
        }
        node = node->parentWidget();
    }

    return nullptr;
}

void NotesBoardWidget::refreshDragProxyPixmap(qreal scale) {
    if (m_dragProxy == nullptr || m_dragProxySourcePixmap.isNull()) {
        return;
    }

    m_dragProxyScale = qBound(1.0, scale, 1.08);
    const QSize sourceSize = m_dragProxySourcePixmap.size();
    const QSize targetSize(qMax(1, qRound(static_cast<qreal>(sourceSize.width()) * m_dragProxyScale)),
                           qMax(1, qRound(static_cast<qreal>(sourceSize.height()) * m_dragProxyScale)));
    const QPixmap scaled = m_dragProxySourcePixmap.scaled(targetSize,
                                                          Qt::KeepAspectRatio,
                                                          Qt::SmoothTransformation);
    m_dragProxy->setPixmap(scaled);
    m_dragProxy->resize(scaled.size());
}

void NotesBoardWidget::updateDragProxyPosition(const QPoint &globalPos) {
    if (m_dragProxy == nullptr) {
        return;
    }

    const QPoint proxyOffset(static_cast<int>(m_dragProxyAnchorRatio.x() * static_cast<qreal>(m_dragProxy->width())),
                             static_cast<int>(m_dragProxyAnchorRatio.y() * static_cast<qreal>(m_dragProxy->height())));
    const QPoint proxyTopLeftGlobal = globalPos - proxyOffset;
    m_dragProxy->move(proxyTopLeftGlobal);
}

void NotesBoardWidget::updateDragAutoScroll(const QPoint &globalPos) {
    m_lastDragGlobalPos = globalPos;
    if (!m_noteDragInProgress) {
        stopDragAutoScroll();
        return;
    }

    QScrollArea *scrollArea = hostScrollArea();
    if (scrollArea == nullptr || scrollArea->viewport() == nullptr) {
        stopDragAutoScroll();
        return;
    }

    QScrollBar *bar = scrollArea->verticalScrollBar();
    if (bar == nullptr || bar->maximum() <= 0) {
        stopDragAutoScroll();
        return;
    }

    const QWidget *viewport = scrollArea->viewport();
    const QRect viewportGlobalRect(viewport->mapToGlobal(QPoint(0, 0)), viewport->size());
    const int edgeThreshold = qMax(22, static_cast<int>(42.0 * m_uiScale));
    const int globalY = globalPos.y();
    int velocity = 0;

    if (globalY < viewportGlobalRect.top() + edgeThreshold) {
        const qreal strength = qBound(0.0,
                                      static_cast<qreal>((viewportGlobalRect.top() + edgeThreshold) - globalY)
                                          / static_cast<qreal>(edgeThreshold),
                                      1.0);
        velocity = -qMax(2, static_cast<int>(4.0 + (strength * 20.0)));
    } else if (globalY > viewportGlobalRect.bottom() - edgeThreshold) {
        const qreal strength = qBound(0.0,
                                      static_cast<qreal>(globalY - (viewportGlobalRect.bottom() - edgeThreshold))
                                          / static_cast<qreal>(edgeThreshold),
                                      1.0);
        velocity = qMax(2, static_cast<int>(4.0 + (strength * 20.0)));
    }

    m_dragAutoScrollVelocity = velocity;
    if (m_dragAutoScrollVelocity == 0) {
        stopDragAutoScroll();
        return;
    }

    if (m_dragAutoScrollTimer != nullptr && !m_dragAutoScrollTimer->isActive()) {
        m_dragAutoScrollTimer->start();
    }
}

void NotesBoardWidget::stopDragAutoScroll() {
    m_dragAutoScrollVelocity = 0;
    if (m_dragAutoScrollTimer != nullptr && m_dragAutoScrollTimer->isActive()) {
        m_dragAutoScrollTimer->stop();
    }
}

NoteLane NotesBoardWidget::laneForDragLocalY(int localY) const {
    struct HeaderAnchor {
        int top = 0;
        NoteLane lane = NoteLane::Today;
    };

    QVector<HeaderAnchor> anchors;
    anchors.reserve(m_laneHeaders.size());
    for (QWidget *laneHeader : std::as_const(m_laneHeaders)) {
        auto *label = qobject_cast<QLabel *>(laneHeader);
        if (label == nullptr) {
            continue;
        }
        NoteLane lane = NoteLane::Today;
        if (!laneFromHeaderText(label->text(), &lane)) {
            continue;
        }
        anchors.append(HeaderAnchor{label->geometry().top(), lane});
    }

    std::sort(anchors.begin(), anchors.end(), [](const HeaderAnchor &left, const HeaderAnchor &right) {
        return left.top < right.top;
    });

    if (anchors.isEmpty()) {
        return m_draggedCard != nullptr ? m_draggedCard->lane() : NoteLane::Today;
    }

    if (localY < anchors.constFirst().top) {
        return anchors.constFirst().lane;
    }

    for (qsizetype index = 0; index + 1 < anchors.size(); ++index) {
        if (localY < anchors.at(index + 1).top) {
            return anchors.at(index).lane;
        }
    }

    return anchors.constLast().lane;
}

int NotesBoardWidget::insertionOrderForLane(NoteLane lane, int localY) const {
    int order = 0;
    const NoteLane normalizedLane = normalizedNoteLane(lane);
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card == nullptr || card->noteId() == m_draggedNoteId) {
            continue;
        }
        if (normalizedNoteLane(card->lane()) != normalizedLane) {
            continue;
        }

        if (localY < card->geometry().center().y()) {
            return order;
        }
        ++order;
    }

    return order;
}

int NotesBoardWidget::stabilizedInsertionOrderForLane(NoteLane lane, int rawOrder, int localY) const {
    const NoteLane normalizedLane = normalizedNoteLane(lane);
    if (normalizedLane != normalizedNoteLane(m_dragPreviewLane) || m_dragPreviewOrder < 0) {
        return rawOrder;
    }

    QVector<const NoteCardWidget *> laneCards;
    laneCards.reserve(m_cards.size());
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card == nullptr || card->noteId() == m_draggedNoteId) {
            continue;
        }
        if (normalizedNoteLane(card->lane()) == normalizedLane) {
            laneCards.append(card);
        }
    }

    const int clampedCurrentOrder = qBound(0, m_dragPreviewOrder, laneCards.size());
    const int clampedRawOrder = qBound(0, rawOrder, laneCards.size());
    if (clampedRawOrder == clampedCurrentOrder) {
        return clampedRawOrder;
    }

    const int hysteresis = qMax(10, static_cast<int>(18.0 * m_uiScale));
    if (clampedRawOrder > clampedCurrentOrder && clampedCurrentOrder < laneCards.size()) {
        const int boundaryCenter = laneCards.at(clampedCurrentOrder)->geometry().center().y();
        if (localY <= boundaryCenter + hysteresis) {
            return clampedCurrentOrder;
        }
    } else if (clampedRawOrder < clampedCurrentOrder && clampedCurrentOrder > 0) {
        const int boundaryCenter = laneCards.at(clampedCurrentOrder - 1)->geometry().center().y();
        if (localY >= boundaryCenter - hysteresis) {
            return clampedCurrentOrder;
        }
    }

    return clampedRawOrder;
}

QString NotesBoardWidget::insertionTargetCardIdForLaneOrder(NoteLane lane, int laneOrder) const {
    const NoteLane normalizedLane = normalizedNoteLane(lane);
    int laneIndex = 0;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card == nullptr || card->noteId() == m_draggedNoteId) {
            continue;
        }
        if (normalizedNoteLane(card->lane()) != normalizedLane) {
            continue;
        }
        if (laneIndex == laneOrder) {
            return card->noteId();
        }
        ++laneIndex;
    }

    return QString();
}

QVector<NoteItem> NotesBoardWidget::notesWithDraggedCardPreview(NoteLane lane, int laneOrder) const {
    const QVector<NoteItem> current = !m_dragPreviewNotes.isEmpty()
                                          ? m_dragPreviewNotes
                                          : (!m_dragOriginNotes.isEmpty() ? m_dragOriginNotes : notes());
    if (current.isEmpty() || m_draggedNoteId.isEmpty()) {
        return current;
    }

    NoteItem draggedItem;
    bool foundDraggedItem = false;
    QVector<NoteItem> todayNotes;
    QVector<NoteItem> nextNotes;
    QVector<NoteItem> waitingNotes;
    QVector<NoteItem> somedayNotes;
    todayNotes.reserve(current.size());
    nextNotes.reserve(current.size());
    waitingNotes.reserve(current.size());
    somedayNotes.reserve(current.size());

    for (const NoteItem &item : current) {
        if (item.id == m_draggedNoteId) {
            draggedItem = item;
            foundDraggedItem = true;
            continue;
        }

        switch (normalizedNoteLane(item.lane)) {
        case NoteLane::Today:
            todayNotes.append(item);
            break;
        case NoteLane::Next:
            nextNotes.append(item);
            break;
        case NoteLane::Waiting:
            waitingNotes.append(item);
            break;
        case NoteLane::Someday:
            somedayNotes.append(item);
            break;
        }
    }

    if (!foundDraggedItem) {
        return current;
    }

    draggedItem.lane = normalizedNoteLane(lane);
    auto insertDragged = [&](QVector<NoteItem> *laneNotes) {
        if (laneNotes == nullptr) {
            return;
        }
        const int clampedOrder = qBound(0, laneOrder, laneNotes->size());
        laneNotes->insert(clampedOrder, draggedItem);
    };

    switch (draggedItem.lane) {
    case NoteLane::Today:
        insertDragged(&todayNotes);
        break;
    case NoteLane::Next:
        insertDragged(&nextNotes);
        break;
    case NoteLane::Waiting:
        insertDragged(&waitingNotes);
        break;
    case NoteLane::Someday:
        insertDragged(&somedayNotes);
        break;
    }

    QVector<NoteItem> preview;
    preview.reserve(current.size());
    auto appendLane = [&preview](QVector<NoteItem> *laneNotes, NoteLane laneValue) {
        if (laneNotes == nullptr) {
            return;
        }
        for (int index = 0; index < laneNotes->size(); ++index) {
            NoteItem item = laneNotes->at(index);
            item.lane = laneValue;
            item.order = index;
            preview.append(item);
        }
    };

    appendLane(&todayNotes, NoteLane::Today);
    appendLane(&nextNotes, NoteLane::Next);
    appendLane(&waitingNotes, NoteLane::Waiting);
    appendLane(&somedayNotes, NoteLane::Someday);
    return preview;
}

NoteCardWidget *NotesBoardWidget::findCardById(const QString &noteId) const {
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card != nullptr && card->noteId() == noteId) {
            return card;
        }
    }

    return nullptr;
}

void NotesBoardWidget::setDropHoverTargetCardId(const QString &noteId) {
    if (m_dropHoverTargetCardId == noteId) {
        return;
    }

    if (!m_dropHoverTargetCardId.isEmpty()) {
        NoteCardWidget *oldTarget = findCardById(m_dropHoverTargetCardId);
        if (oldTarget != nullptr) {
            oldTarget->setDropHoverActive(false);
        }
    }

    m_dropHoverTargetCardId = noteId;
    if (!m_dropHoverTargetCardId.isEmpty()) {
        NoteCardWidget *newTarget = findCardById(m_dropHoverTargetCardId);
        if (newTarget != nullptr) {
            newTarget->setDropHoverActive(true);
        }
    }
}

void NotesBoardWidget::clearDropHoverTarget() {
    setDropHoverTargetCardId(QString());
}

void NotesBoardWidget::updateCardDragPreview(const QPoint &globalPos) {
    if (!m_noteDragInProgress) {
        return;
    }
    const QPoint localPos = mapFromGlobal(globalPos);
    const NoteLane targetLane = laneForDragLocalY(localPos.y());
    const int rawTargetOrder = insertionOrderForLane(targetLane, localPos.y());
    const int targetOrder = stabilizedInsertionOrderForLane(targetLane, rawTargetOrder, localPos.y());
    const QString targetCardId = insertionTargetCardIdForLaneOrder(targetLane, targetOrder);
    setDropHoverTargetCardId(targetCardId);

    if (targetLane == m_dragPreviewLane && targetOrder == m_dragPreviewOrder) {
        return;
    }

    QVector<NoteItem> preview = notesWithDraggedCardPreview(targetLane, targetOrder);
    if (preview.isEmpty()) {
        return;
    }

    m_dragPreviewLane = targetLane;
    m_dragPreviewOrder = targetOrder;
    m_dragPreviewNotes = preview;
    rebuildCards(m_dragPreviewNotes);
    m_draggedCard = findCardById(m_draggedNoteId);
    if (m_draggedCard != nullptr) {
        m_draggedCard->setNoteDragActive(true);
    }
    if (m_dragProxy != nullptr) {
        m_dragProxy->raise();
    }
    updateDragProxyPosition(globalPos);
}

void NotesBoardWidget::finishCardDrag(const QPoint &globalPos, bool canceled) {
    if (!m_noteDragInProgress) {
        return;
    }

    if (m_dragPreviewUpdateTimer != nullptr) {
        m_dragPreviewUpdateTimer->stop();
    }

    if (!canceled) {
        updateCardDragPreview(globalPos);
    }

    stopDragAutoScroll();
    clearDropHoverTarget();

    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }

    const QVector<NoteItem> finalNotes = canceled
                                             ? m_dragOriginNotes
                                             : (m_dragPreviewNotes.isEmpty() ? notes() : m_dragPreviewNotes);

    m_noteDragInProgress = false;
    m_hiddenCardId.clear();
    clearDragProxy();
    rebuildCards(finalNotes);

    m_draggedCard = findCardById(m_draggedNoteId);
    if (m_draggedCard != nullptr) {
        m_draggedCard->setNoteDragActive(false);
    }

    if (!canceled) {
        const QVector<NoteItem> reorderedNotes = notes();
        if (!noteArrangementEquivalent(m_dragOriginNotes, reorderedNotes)) {
            emit noteReorderRequested();
        }
    }

    m_draggedCard = nullptr;
    m_draggedNoteId.clear();
    m_dropHoverTargetCardId.clear();
    m_dragOriginNotes.clear();
    m_dragPreviewNotes.clear();
    m_dragPreviewOrder = -1;
}

void NotesBoardWidget::clearDragProxy() {
    if (m_dragProxy == nullptr) {
        return;
    }

    if (m_dragProxyScaleAnimation != nullptr) {
        m_dragProxyScaleAnimation->stop();
    }
    if (m_dragProxyShadowBlurAnimation != nullptr) {
        m_dragProxyShadowBlurAnimation->stop();
    }
    if (m_dragProxyShadowOffsetAnimation != nullptr) {
        m_dragProxyShadowOffsetAnimation->stop();
    }
    if (m_dragPreviewUpdateTimer != nullptr) {
        m_dragPreviewUpdateTimer->stop();
    }

    m_dragProxy->hide();
    m_dragProxy->clear();
    m_dragProxySourcePixmap = QPixmap();
    m_dragProxyScale = 1.0;
}

bool NotesBoardWidget::rebuildCardsFastIfPossible(const QVector<NoteItem> &notes) {
    if (notes.size() != m_cards.size()) {
        return false;
    }

    QVector<NoteItem> orderedNotes = notes;
    std::stable_sort(orderedNotes.begin(), orderedNotes.end(), noteLessThanByLaneAndOrder);

    QHash<QString, NoteCardWidget *> cardsById;
    cardsById.reserve(m_cards.size());
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card == nullptr || cardsById.contains(card->noteId())) {
            return false;
        }
        cardsById.insert(card->noteId(), card);
    }

    QVector<NoteCardWidget *> orderedCards;
    orderedCards.reserve(orderedNotes.size());
    for (const NoteItem &item : orderedNotes) {
        NoteCardWidget *card = cardsById.value(item.id, nullptr);
        if (card == nullptr) {
            return false;
        }
        orderedCards.append(card);
    }

    QHash<NoteLane, QLabel *> headerByLane;
    for (QWidget *laneHeader : std::as_const(m_laneHeaders)) {
        auto *label = qobject_cast<QLabel *>(laneHeader);
        if (label == nullptr) {
            continue;
        }
        NoteLane lane = NoteLane::Today;
        if (!laneFromHeaderText(label->text(), &lane)) {
            continue;
        }
        headerByLane.insert(normalizedNoteLane(lane), label);
    }

    for (QWidget *laneHeader : std::as_const(m_laneHeaders)) {
        if (laneHeader != nullptr) {
            m_layout->removeWidget(laneHeader);
        }
    }
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card != nullptr) {
            m_layout->removeWidget(card);
        }
    }

    QVector<QWidget *> reorderedHeaders;
    reorderedHeaders.reserve(4);
    NoteLane activeLane = NoteLane::Today;
    bool laneInitialized = false;

    for (const NoteItem &item : orderedNotes) {
        const NoteLane itemLane = normalizedNoteLane(item.lane);
        if (!laneInitialized || activeLane != itemLane) {
            activeLane = itemLane;
            laneInitialized = true;
            QLabel *laneHeader = headerByLane.take(itemLane);
            if (laneHeader == nullptr) {
                laneHeader = createLaneHeaderWidget(itemLane, this, m_uiScale, m_uiStyle);
            } else {
                applyLaneHeaderStyle(laneHeader, m_uiStyle, m_uiScale);
            }
            m_layout->addWidget(laneHeader);
            reorderedHeaders.append(laneHeader);
        }

        NoteCardWidget *card = cardsById.value(item.id, nullptr);
        if (card == nullptr) {
            return false;
        }

        card->setText(item.text);
        card->setHue(item.hue);
        card->setSticker(item.sticker);
        card->setImportedStickerLibrary(m_importedStickerLibrary);
        card->setUiScale(m_uiScale);
        card->setBaseLayerOpacity(m_baseLayerOpacity);
        card->setUiStyle(m_uiStyle);
        card->setExternalFileSyncEnabled(m_externalFileSyncEnabled);
        card->setAlwaysOnTopEnabled(m_alwaysOnTopEnabled);
        card->setLaunchAtStartupEnabled(m_launchAtStartupEnabled);
        card->setAutoCheckUpdatesEnabled(m_autoCheckUpdatesEnabled);
        card->setWindowLocked(m_windowLocked);

        if (normalizedNoteLane(card->lane()) != itemLane) {
            card->setLane(itemLane);
        }
        const bool keepHiddenForDrag = item.id == m_hiddenCardId;
        if (keepHiddenForDrag) {
            card->hide();
        } else {
            card->show();
        }
        const bool isDropHoverTarget = m_noteDragInProgress
                                       && !keepHiddenForDrag
                                       && item.id == m_dropHoverTargetCardId;
        card->setDropHoverActive(isDropHoverTarget);

        m_layout->addWidget(card);
    }

    for (QLabel *staleHeader : std::as_const(headerByLane)) {
        if (staleHeader != nullptr) {
            m_layout->removeWidget(staleHeader);
            staleHeader->deleteLater();
        }
    }

    m_laneHeaders = reorderedHeaders;
    m_cards = orderedCards;
    if (layout() != nullptr) {
        layout()->activate();
    }

    return true;
}

void NotesBoardWidget::rebuildCards(const QVector<NoteItem> &notes) {
    if (rebuildCardsFastIfPossible(notes)) {
        return;
    }

    QHash<QString, NoteCardWidget *> existingCards;
    existingCards.reserve(m_cards.size());
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        existingCards.insert(card->noteId(), card);
        m_layout->removeWidget(card);
    }

    for (QWidget *laneHeader : std::as_const(m_laneHeaders)) {
        if (laneHeader == nullptr) {
            continue;
        }
        m_layout->removeWidget(laneHeader);
        laneHeader->deleteLater();
    }
    m_laneHeaders.clear();

    QVector<NoteItem> orderedNotes = notes;
    std::stable_sort(orderedNotes.begin(), orderedNotes.end(), noteLessThanByLaneAndOrder);

    QVector<NoteCardWidget *> orderedCards;
    orderedCards.reserve(orderedNotes.size());
    QVector<NoteCardWidget *> cardsPendingEntranceAnimation;
    cardsPendingEntranceAnimation.reserve(orderedNotes.size());

    NoteLane activeLane = NoteLane::Today;
    bool laneInitialized = false;

    for (int index = 0; index < orderedNotes.size(); ++index) {
        const NoteItem &item = orderedNotes.at(index);
        const NoteLane itemLane = normalizedNoteLane(item.lane);
        if (!laneInitialized || activeLane != itemLane) {
            activeLane = itemLane;
            laneInitialized = true;
            QLabel *laneHeader = createLaneHeaderWidget(itemLane, this, m_uiScale, m_uiStyle);
            m_layout->addWidget(laneHeader);
            m_laneHeaders.append(laneHeader);
        }

        NoteCardWidget *card = existingCards.take(item.id);
        const bool isNewCard = (card == nullptr);
        if (isNewCard) {
            card = new NoteCardWidget(this);
            connect(card, &NoteCardWidget::textCommitted, this, &NotesBoardWidget::noteTextCommitted);
            connect(card, &NoteCardWidget::layoutRefreshRequested, this, &NotesBoardWidget::layoutRefreshRequested);
            connect(card, &NoteCardWidget::addNoteRequested, this, &NotesBoardWidget::addNoteRequested);
            connect(card, &NoteCardWidget::clearEmptyRequested, this, &NotesBoardWidget::clearEmptyRequested);
            connect(card, &NoteCardWidget::scaleInRequested, this, &NotesBoardWidget::scaleInRequested);
            connect(card, &NoteCardWidget::scaleOutRequested, this, &NotesBoardWidget::scaleOutRequested);
            connect(card, &NoteCardWidget::scaleResetRequested, this, &NotesBoardWidget::scaleResetRequested);
            connect(card,
                    &NoteCardWidget::baseLayerOpacitySetRequested,
                    this,
                    &NotesBoardWidget::baseLayerOpacitySetRequested);
            connect(card, &NoteCardWidget::exportJsonRequested, this, &NotesBoardWidget::exportJsonRequested);
            connect(card, &NoteCardWidget::importJsonRequested, this, &NotesBoardWidget::importJsonRequested);
            connect(card, &NoteCardWidget::backupSnapshotRequested, this, &NotesBoardWidget::backupSnapshotRequested);
            connect(card,
                    &NoteCardWidget::restoreLatestBackupRequested,
                    this,
                    &NotesBoardWidget::restoreLatestBackupRequested);
            connect(card,
                    &NoteCardWidget::externalFileSyncToggled,
                    this,
                    &NotesBoardWidget::externalFileSyncToggled);
            connect(card, &NoteCardWidget::alwaysOnTopToggled, this, &NotesBoardWidget::alwaysOnTopToggled);
            connect(card, &NoteCardWidget::launchAtStartupToggled, this, &NotesBoardWidget::launchAtStartupToggled);
            connect(card, &NoteCardWidget::autoCheckUpdatesToggled, this, &NotesBoardWidget::autoCheckUpdatesToggled);
            connect(card, &NoteCardWidget::windowLockToggled, this, &NotesBoardWidget::windowLockToggled);
            connect(card, &NoteCardWidget::checkForUpdatesRequested, this, &NotesBoardWidget::checkForUpdatesRequested);
            connect(card, &NoteCardWidget::timelineReplayRequested, this, &NotesBoardWidget::timelineReplayRequested);
            connect(card, &NoteCardWidget::openStorageDirectoryRequested, this, &NotesBoardWidget::openStorageDirectoryRequested);
            connect(card, &NoteCardWidget::quitRequested, this, &NotesBoardWidget::quitRequested);
            connect(card, &NoteCardWidget::deleteRequested, this, &NotesBoardWidget::handleCardDeleteRequested);
            connect(card, &NoteCardWidget::hueChangeRequested, this, &NotesBoardWidget::noteHueChangeRequested);
            connect(card, &NoteCardWidget::stickerChangeRequested, this, &NotesBoardWidget::noteStickerChangeRequested);
            connect(card, &NoteCardWidget::laneChangeRequested, this, &NotesBoardWidget::noteLaneChangeRequested);
            connect(card, &NoteCardWidget::uiStyleChangeRequested, this, &NotesBoardWidget::uiStyleChangeRequested);
            connect(card, &NoteCardWidget::dragHoldStarted, this, &NotesBoardWidget::handleCardDragHoldStarted);
        }

        card->setNoteId(item.id);
        card->setText(item.text);
        card->setHue(item.hue);
        card->setSticker(item.sticker);
        card->setImportedStickerLibrary(m_importedStickerLibrary);
        card->setLane(itemLane);
        card->setUiScale(m_uiScale);
        card->setBaseLayerOpacity(m_baseLayerOpacity);
        card->setUiStyle(m_uiStyle);
        card->setExternalFileSyncEnabled(m_externalFileSyncEnabled);
        card->setAlwaysOnTopEnabled(m_alwaysOnTopEnabled);
        card->setLaunchAtStartupEnabled(m_launchAtStartupEnabled);
        card->setAutoCheckUpdatesEnabled(m_autoCheckUpdatesEnabled);
        card->setWindowLocked(m_windowLocked);
        const bool keepHiddenForDrag = m_noteDragInProgress && item.id == m_hiddenCardId;
        if (keepHiddenForDrag) {
            card->hide();
        } else {
            card->show();
        }
        const bool isDropHoverTarget = m_noteDragInProgress
                                       && !keepHiddenForDrag
                                       && item.id == m_dropHoverTargetCardId;
        card->setDropHoverActive(isDropHoverTarget);

        m_layout->addWidget(card);
        orderedCards.append(card);

        if (isNewCard) {
            cardsPendingEntranceAnimation.append(card);
        }
    }

    for (NoteCardWidget *staleCard : existingCards) {
        m_layout->removeWidget(staleCard);
        staleCard->deleteLater();
    }

    m_cards = orderedCards;

    if (layout() != nullptr) {
        layout()->activate();
    }

    if (isVisible() && !m_noteDragInProgress) {
        for (NoteCardWidget *card : std::as_const(cardsPendingEntranceAnimation)) {
            AnimationCoordinator::animateCardEntrance(card);
        }
    }
}

void NotesBoardWidget::handleCardDeleteRequested(const QString &noteId) {
    if (m_deleteAnimationRunning || m_noteDragInProgress) {
        return;
    }

    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card->noteId() == noteId) {
            m_deleteAnimationRunning = true;
            AnimationCoordinator::animateCardRemoval(card, [this, noteId]() {
                m_deleteAnimationRunning = false;
                emit noteDeleteRequested(noteId);
            });
            return;
        }
    }
}

}  // namespace glassnote
