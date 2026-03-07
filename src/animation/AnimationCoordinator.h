#pragma once

#include <functional>

#include <QPoint>

class QWidget;

namespace glassnote {

class AnimationCoordinator final {
public:
    static void animateCardEntrance(QWidget *widget);
    static void animateCardRemoval(QWidget *widget, const std::function<void()> &onFinished);
    static void animateCardReposition(QWidget *widget, const QPoint &from, const QPoint &to);
};

}  // namespace glassnote
