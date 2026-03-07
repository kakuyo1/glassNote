#pragma once

class QWidget;

namespace glassnote {

class WindowsBlurHelper final {
public:
    static bool enableForWindow(QWidget *widget);
    static void disableForWindow(QWidget *widget);
};

}  // namespace glassnote
