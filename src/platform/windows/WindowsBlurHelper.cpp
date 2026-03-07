#include "platform/windows/WindowsBlurHelper.h"

#ifdef Q_OS_WIN

#include <windows.h>
#include <dwmapi.h>

#include <QtGlobal>
#include <QWidget>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

namespace glassnote {

namespace {

enum class DwmSystemBackdropType {
    Auto = 0,
    None = 1,
    MainWindow = 2,
    TransientWindow = 3,
    TabbedWindow = 4,
};

enum class AccentState {
    Disabled = 0,
    EnableGradient = 1,
    EnableTransparentGradient = 2,
    EnableBlurBehind = 3,
    EnableAcrylicBlurBehind = 4,
    EnableHostBackdrop = 5,
};

enum class WindowCompositionAttribute {
    WcaAccentPolicy = 19,
};

struct AccentPolicy {
    AccentState accentState;
    int accentFlags;
    int gradientColor;
    int animationId;
};

struct WindowCompositionAttributeData {
    WindowCompositionAttribute attribute;
    PVOID data;
    SIZE_T sizeOfData;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI *)(HWND, WindowCompositionAttributeData *);

SetWindowCompositionAttributeFn resolveSetWindowCompositionAttribute() {
    HMODULE user32Module = GetModuleHandleW(L"user32.dll");
    if (user32Module == nullptr) {
        return nullptr;
    }

    return reinterpret_cast<SetWindowCompositionAttributeFn>(
        GetProcAddress(user32Module, "SetWindowCompositionAttribute"));
}

bool applyAccentPolicy(HWND hwnd, AccentState state, unsigned int gradientColor = 0U) {
    auto *setWindowCompositionAttribute = resolveSetWindowCompositionAttribute();
    if (setWindowCompositionAttribute == nullptr) {
        return false;
    }

    AccentPolicy policy = {};
    policy.accentState = state;
    policy.accentFlags = 0;
    policy.gradientColor = static_cast<int>(gradientColor);
    policy.animationId = 0;

    WindowCompositionAttributeData data = {};
    data.attribute = WindowCompositionAttribute::WcaAccentPolicy;
    data.data = &policy;
    data.sizeOfData = sizeof(policy);

    return setWindowCompositionAttribute(hwnd, &data) != FALSE;
}

bool tryEnableAccentBackdrop(HWND hwnd) {
    if (applyAccentPolicy(hwnd, AccentState::EnableHostBackdrop)) {
        return true;
    }

    bool shouldTryAcrylic = false;
    const int acrylicFlag = qEnvironmentVariableIntValue("GLASSNOTE_ENABLE_ACRYLIC", &shouldTryAcrylic);
    if (shouldTryAcrylic && acrylicFlag > 0) {
        if (applyAccentPolicy(hwnd, AccentState::EnableAcrylicBlurBehind, 0x99FFFFFFU)) {
            return true;
        }
    }

    return applyAccentPolicy(hwnd, AccentState::EnableBlurBehind);
}

bool tryEnableSystemBackdrop(HWND hwnd) {
    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    const auto backdropType = static_cast<int>(DwmSystemBackdropType::TransientWindow);
    const HRESULT result = DwmSetWindowAttribute(hwnd,
                                                 DWMWA_SYSTEMBACKDROP_TYPE,
                                                 &backdropType,
                                                 sizeof(backdropType));
    return SUCCEEDED(result);
}

bool tryEnableLegacyBlur(HWND hwnd) {
    DWM_BLURBEHIND blurBehind = {};
    blurBehind.dwFlags = DWM_BB_ENABLE;
    blurBehind.fEnable = TRUE;
    if (FAILED(DwmEnableBlurBehindWindow(hwnd, &blurBehind))) {
        return false;
    }

    const MARGINS margins = {-1, -1, -1, -1};
    return SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &margins));
}

void disableSystemBackdrop(HWND hwnd) {
    const auto backdropType = static_cast<int>(DwmSystemBackdropType::None);
    DwmSetWindowAttribute(hwnd,
                          DWMWA_SYSTEMBACKDROP_TYPE,
                          &backdropType,
                          sizeof(backdropType));
}

void disableAccentBackdrop(HWND hwnd) {
    applyAccentPolicy(hwnd, AccentState::Disabled);
}

void disableLegacyBlur(HWND hwnd) {
    DWM_BLURBEHIND blurBehind = {};
    blurBehind.dwFlags = DWM_BB_ENABLE;
    blurBehind.fEnable = FALSE;
    DwmEnableBlurBehindWindow(hwnd, &blurBehind);
}

}  // namespace

bool WindowsBlurHelper::enableForWindow(QWidget *widget) {
    if (widget == nullptr) {
        return false;
    }

    const auto hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd == nullptr) {
        return false;
    }

    if (tryEnableSystemBackdrop(hwnd)) {
        return true;
    }

    if (tryEnableAccentBackdrop(hwnd)) {
        return true;
    }

    return tryEnableLegacyBlur(hwnd);
}

void WindowsBlurHelper::disableForWindow(QWidget *widget) {
    if (widget == nullptr) {
        return;
    }

    const auto hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd == nullptr) {
        return;
    }

    disableSystemBackdrop(hwnd);
    disableLegacyBlur(hwnd);
    disableAccentBackdrop(hwnd);
}

}  // namespace glassnote

#else

#include <QWidget>

namespace glassnote {

bool WindowsBlurHelper::enableForWindow(QWidget *widget) {
    Q_UNUSED(widget)
    return false;
}

void WindowsBlurHelper::disableForWindow(QWidget *widget) {
    Q_UNUSED(widget)
}

}  // namespace glassnote

#endif
