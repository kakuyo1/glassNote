#include "animation/AnimationCoordinator.h"

#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QWidget>

namespace glassnote {

namespace {

constexpr int kHoverShiftY = 8;
constexpr int kEntranceDurationMsec = 210;
constexpr int kRemovalDurationMsec = 170;
constexpr int kRepositionDurationMsec = 190;

}  // namespace

void AnimationCoordinator::animateCardEntrance(QWidget *widget) {
    if (widget == nullptr) {
        return;
    }

    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
    if (effect == nullptr) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }

    effect->setOpacity(0.0);

    auto *opacityAnimation = new QPropertyAnimation(effect, "opacity", widget);
    opacityAnimation->setDuration(kEntranceDurationMsec);
    opacityAnimation->setStartValue(0.0);
    opacityAnimation->setEndValue(1.0);
    opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);

    const QPoint endPos = widget->pos();
    const QPoint startPos(endPos.x(), endPos.y() + kHoverShiftY);
    widget->move(startPos);

    auto *positionAnimation = new QPropertyAnimation(widget, "pos", widget);
    positionAnimation->setDuration(kEntranceDurationMsec);
    positionAnimation->setStartValue(startPos);
    positionAnimation->setEndValue(endPos);
    positionAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto *group = new QParallelAnimationGroup(widget);
    group->addAnimation(positionAnimation);
    group->addAnimation(opacityAnimation);
    QObject::connect(group, &QParallelAnimationGroup::finished, widget, [widget, endPos]() {
        widget->move(endPos);
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationCoordinator::animateCardRemoval(QWidget *widget, const std::function<void()> &onFinished) {
    if (widget == nullptr) {
        if (onFinished) {
            onFinished();
        }
        return;
    }

    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
    if (effect == nullptr) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }

    const QPoint startPos = widget->pos();
    const QPoint endPos(startPos.x(), startPos.y() + kHoverShiftY);

    auto *positionAnimation = new QPropertyAnimation(widget, "pos", widget);
    positionAnimation->setDuration(kRemovalDurationMsec);
    positionAnimation->setStartValue(startPos);
    positionAnimation->setEndValue(endPos);
    positionAnimation->setEasingCurve(QEasingCurve::InCubic);

    auto *opacityAnimation = new QPropertyAnimation(effect, "opacity", widget);
    opacityAnimation->setDuration(kRemovalDurationMsec);
    opacityAnimation->setStartValue(1.0);
    opacityAnimation->setEndValue(0.0);
    opacityAnimation->setEasingCurve(QEasingCurve::InCubic);

    auto *group = new QParallelAnimationGroup(widget);
    group->addAnimation(positionAnimation);
    group->addAnimation(opacityAnimation);
    QObject::connect(group, &QParallelAnimationGroup::finished, widget, [widget, onFinished]() {
        widget->hide();
        if (onFinished) {
            onFinished();
        }
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationCoordinator::animateCardReposition(QWidget *widget, const QPoint &from, const QPoint &to) {
    if (widget == nullptr || from == to) {
        return;
    }

    widget->move(from);

    auto *animation = new QPropertyAnimation(widget, "pos", widget);
    animation->setDuration(kRepositionDurationMsec);
    animation->setStartValue(from);
    animation->setEndValue(to);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(animation, &QPropertyAnimation::finished, widget, [widget, to]() {
        widget->move(to);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

}  // namespace glassnote
