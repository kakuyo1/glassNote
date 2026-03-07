#include "ui/NotesBoardWidget.h"

#include <QHash>
#include <QFont>
#include <QFontMetrics>
#include <QSet>
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

bool shouldAnimateReposition(qreal uiScale, int cardCount, const QPoint &from, const QPoint &to) {
    if (cardCount > 14) {
        return false;
    }

    const int distance = (from - to).manhattanLength();
    const int minDistance = qMax(4, static_cast<int>(6.0 * uiScale));
    const int maxDistance = static_cast<int>(220.0 * uiScale);
    return distance >= minDistance && distance <= maxDistance;
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
    if (m_cards.isEmpty()) {
        return 0;
    }

    const int spacing = m_layout->spacing();
    int totalHeight = 0;
    for (int index = 0; index < m_cards.size(); ++index) {
        totalHeight += m_cards.at(index)->sizeHint().height();
        if (index > 0) {
            totalHeight += spacing;
        }
    }

    return totalHeight;
}

int NotesBoardWidget::requiredContentWidth() const {
    QFont displayFont;
    displayFont.setPointSizeF(11.0 * m_uiScale * contentFontScaleForUiScale(m_uiScale));
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
        item.hue = card->hue();
        item.reminderEpochMsec = card->reminderEpochMsec();
        item.order = static_cast<int>(index);
        values.append(item);
    }

    return values;
}

void NotesBoardWidget::setNotes(const QVector<NoteItem> &notes) {
    rebuildCards(notes);
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
    if (m_uiStyle == uiStyle) {
        return;
    }

    m_uiStyle = uiStyle;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setUiStyle(uiStyle);
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

void NotesBoardWidget::setWindowLocked(bool enabled) {
    m_windowLocked = enabled;
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        card->setWindowLocked(enabled);
    }
}

void NotesBoardWidget::rebuildCards(const QVector<NoteItem> &notes) {
    QHash<QString, NoteCardWidget *> existingCards;
    QHash<QString, QPoint> previousPositions;
    existingCards.reserve(m_cards.size());
    previousPositions.reserve(m_cards.size());
    for (NoteCardWidget *card : std::as_const(m_cards)) {
        existingCards.insert(card->noteId(), card);
        previousPositions.insert(card->noteId(), card->pos());
    }

    QVector<NoteCardWidget *> orderedCards;
    orderedCards.reserve(notes.size());
    QSet<QString> newCardIds;
    QVector<NoteCardWidget *> cardsPendingEntranceAnimation;
    cardsPendingEntranceAnimation.reserve(notes.size());

    for (int index = 0; index < notes.size(); ++index) {
        const NoteItem &item = notes.at(index);
        NoteCardWidget *card = existingCards.take(item.id);
        const bool isNewCard = (card == nullptr);
        if (isNewCard) {
            newCardIds.insert(item.id);
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
            connect(card, &NoteCardWidget::windowLockToggled, this, &NotesBoardWidget::windowLockToggled);
            connect(card, &NoteCardWidget::reminderSetRequested, this, &NotesBoardWidget::reminderSetRequested);
            connect(card, &NoteCardWidget::reminderClearedRequested, this, &NotesBoardWidget::reminderClearedRequested);
            connect(card, &NoteCardWidget::openStorageDirectoryRequested, this, &NotesBoardWidget::openStorageDirectoryRequested);
            connect(card, &NoteCardWidget::quitRequested, this, &NotesBoardWidget::quitRequested);
            connect(card, &NoteCardWidget::deleteRequested, this, &NotesBoardWidget::handleCardDeleteRequested);
            connect(card, &NoteCardWidget::hueChangeRequested, this, &NotesBoardWidget::noteHueChangeRequested);
            connect(card, &NoteCardWidget::uiStyleChangeRequested, this, &NotesBoardWidget::uiStyleChangeRequested);
        }

        card->setNoteId(item.id);
        card->setText(item.text);
        card->setHue(item.hue);
        card->setUiScale(m_uiScale);
        card->setBaseLayerOpacity(m_baseLayerOpacity);
        card->setUiStyle(m_uiStyle);
        card->setExternalFileSyncEnabled(m_externalFileSyncEnabled);
        card->setAlwaysOnTopEnabled(m_alwaysOnTopEnabled);
        card->setWindowLocked(m_windowLocked);
        card->setReminderEpochMsec(item.reminderEpochMsec);
        card->show();

        m_layout->removeWidget(card);
        m_layout->insertWidget(index, card);
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

    if (isVisible()) {
        for (NoteCardWidget *card : std::as_const(cardsPendingEntranceAnimation)) {
            AnimationCoordinator::animateCardEntrance(card);
        }
    }

    const bool animateRepositionForPass = isVisible() && !m_deleteAnimationRunning;
    if (!animateRepositionForPass) {
        return;
    }

    for (NoteCardWidget *card : std::as_const(m_cards)) {
        if (card == nullptr || newCardIds.contains(card->noteId())) {
            continue;
        }

        const QPoint from = previousPositions.value(card->noteId(), card->pos());
        const QPoint to = card->pos();
        if (shouldAnimateReposition(m_uiScale, m_cards.size(), from, to)) {
            AnimationCoordinator::animateCardReposition(card, from, to);
        }
    }
}

void NotesBoardWidget::handleCardDeleteRequested(const QString &noteId) {
    if (m_deleteAnimationRunning) {
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
