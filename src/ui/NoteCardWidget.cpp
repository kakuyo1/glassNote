#include "ui/NoteCardWidget.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QColorDialog>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QKeySequence>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSignalBlocker>
#include <QFontMetrics>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextList>
#include <QTextListFormat>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QSet>

#include <limits>

#include "common/Constants.h"
#include "theme/ThemeHelper.h"
#include "ui/MainWindow.h"

namespace glassnote {

namespace {

class PlainTextPasteTextEdit final : public QTextEdit {
public:
    using QTextEdit::QTextEdit;

protected:
    void insertFromMimeData(const QMimeData *source) override {
        if (source == nullptr) {
            return;
        }

        if (source->hasText()) {
            QTextCursor cursor = textCursor();
            cursor.insertText(source->text());
            setTextCursor(cursor);
            return;
        }

        QTextEdit::insertFromMimeData(source);
    }
};

const QString kChecklistUncheckedPrefix = QStringLiteral("☐ ");
const QString kChecklistCheckedPrefix = QStringLiteral("☑ ");
const QString kImageStickerToken = QStringLiteral("__tobyfox__");
const QString kImportedImageStickerPrefix = QStringLiteral("__image_sticker__:");
const QString kImageStickerResource = QStringLiteral(":/icons/tobyfox-small.png");
constexpr int kMinBaseLayerOpacityPercent = 0;
constexpr int kMaxBaseLayerOpacityPercent = 100;
constexpr int kBaseLayerOpacityStepPercent = 10;

bool isTobyfoxStickerValue(const QString &value) {
    const QString normalized = value.trimmed();
    return normalized == kImageStickerToken
           || normalized == QStringLiteral(":/icons/tobyfox-small.png")
           || normalized == QStringLiteral(":/icons/tobyfox.png");
}

bool isImportedImageStickerValue(const QString &value) {
    return value.trimmed().startsWith(kImportedImageStickerPrefix);
}

QString importedImageStickerPath(const QString &value) {
    if (!isImportedImageStickerValue(value)) {
        return QString();
    }
    return value.trimmed().mid(kImportedImageStickerPrefix.size()).trimmed();
}

QString encodeImportedImageStickerPath(const QString &path) {
    return kImportedImageStickerPrefix + QDir::toNativeSeparators(path.trimmed());
}

QPixmap stickerPixmapForValue(const QString &value) {
    if (isTobyfoxStickerValue(value)) {
        QPixmap stickerPixmap(kImageStickerResource);
        if (stickerPixmap.isNull()) {
            stickerPixmap = QPixmap(QStringLiteral(":/icons/tobyfox.png"));
        }
        return stickerPixmap;
    }

    if (isImportedImageStickerValue(value)) {
        return QPixmap(importedImageStickerPath(value));
    }

    return QPixmap();
}

QString fallbackStickerText(const QString &value) {
    if (isImportedImageStickerValue(value)) {
        return QStringLiteral("🖼");
    }
    if (isTobyfoxStickerValue(value)) {
        return QStringLiteral("TobyFox");
    }
    return value.trimmed();
}

enum class ChecklistState {
    None,
    Unchecked,
    Checked,
};

QString displayTextFor(const QString &text) {
    return text.trimmed().isEmpty() ? QStringLiteral("双击输入内容") : text;
}

ChecklistState checklistStateForText(const QString &text) {
    if (text.startsWith(kChecklistUncheckedPrefix)) {
        return ChecklistState::Unchecked;
    }
    if (text.startsWith(kChecklistCheckedPrefix)) {
        return ChecklistState::Checked;
    }
    return ChecklistState::None;
}

int checklistPrefixLength(ChecklistState state) {
    if (state == ChecklistState::Unchecked) {
        return kChecklistUncheckedPrefix.size();
    }
    if (state == ChecklistState::Checked) {
        return kChecklistCheckedPrefix.size();
    }
    return 0;
}

QString checklistTextWithoutPrefix(const QString &text) {
    const ChecklistState state = checklistStateForText(text);
    if (state == ChecklistState::None) {
        return text;
    }
    return text.mid(checklistPrefixLength(state));
}

bool isBulletListStyle(QTextListFormat::Style style) {
    return style == QTextListFormat::ListDisc
           || style == QTextListFormat::ListCircle
           || style == QTextListFormat::ListSquare;
}

bool isNumberedListStyle(QTextListFormat::Style style) {
    return style == QTextListFormat::ListDecimal
           || style == QTextListFormat::ListLowerAlpha
           || style == QTextListFormat::ListUpperAlpha
           || style == QTextListFormat::ListLowerRoman
           || style == QTextListFormat::ListUpperRoman;
}

QColor blendColor(const QColor &from, const QColor &to, qreal progress) {
    const qreal clamped = qBound(0.0, progress, 1.0);
    return QColor::fromRgbF(from.redF() + ((to.redF() - from.redF()) * clamped),
                            from.greenF() + ((to.greenF() - from.greenF()) * clamped),
                            from.blueF() + ((to.blueF() - from.blueF()) * clamped),
                            from.alphaF() + ((to.alphaF() - from.alphaF()) * clamped));
}

NotePalette blendPalette(const NotePalette &from, const NotePalette &to, qreal progress) {
    NotePalette palette;
    palette.shadow = blendColor(from.shadow, to.shadow, progress);
    palette.fillTop = blendColor(from.fillTop, to.fillTop, progress);
    palette.fillBottom = blendColor(from.fillBottom, to.fillBottom, progress);
    palette.border = blendColor(from.border, to.border, progress);
    palette.highlightTop = blendColor(from.highlightTop, to.highlightTop, progress);
    palette.text = blendColor(from.text, to.text, progress);
    palette.placeholder = blendColor(from.placeholder, to.placeholder, progress);
    return palette;
}

qreal typographyScaleForUiScale(qreal scale) {
    if (scale < 0.95) {
        return 0.96;
    }
    if (scale > 1.2) {
        return 1.04;
    }
    return 1.0;
}

qreal spacingScaleForUiScale(qreal scale) {
    if (scale < 0.95) {
        return 0.9;
    }
    if (scale > 1.2) {
        return 1.08;
    }
    return 1.0;
}

int baseHeightForScale(qreal scale) {
    const int height = static_cast<int>(constants::kCardMinimumHeight * scale);
    if (scale < 0.95) {
        return qMax(72, height - 4);
    }
    if (scale > 1.2) {
        return height + 6;
    }
    return height;
}

}  // namespace

NoteCardWidget::NoteCardWidget(QWidget *parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setMinimumHeight(constants::kCardMinimumHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(constants::kCardPaddingHorizontal,
                               constants::kCardPaddingVertical,
                               constants::kCardPaddingHorizontal,
                               constants::kCardPaddingVertical);
    layout->setSpacing(0);

    m_displayLabel = new QLabel(this);
    m_displayLabel->setWordWrap(false);
    m_displayLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_displayLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_displayLabel->setStyleSheet(
        "QLabel { color: rgba(255, 255, 255, 230); background: transparent; }");

    m_displayGlowEffect = new QGraphicsDropShadowEffect(this);
    m_displayGlowEffect->setBlurRadius(8.0);
    m_displayGlowEffect->setOffset(0.0, 0.0);
    m_displayGlowEffect->setColor(QColor(140, 220, 140, 120));
    m_displayGlowEffect->setEnabled(false);
    m_displayLabel->setGraphicsEffect(m_displayGlowEffect);

    QFont displayFont = m_displayLabel->font();
    displayFont.setPointSize(11);
    m_displayLabel->setFont(displayFont);

    m_editor = new PlainTextPasteTextEdit(this);
    m_editor->setAcceptRichText(true);
    m_editor->setFrameStyle(QFrame::NoFrame);
    m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setStyleSheet(
        "QTextEdit { color: rgba(255, 255, 255, 245); background: transparent; border: none; }");
    m_editor->setPlaceholderText(QStringLiteral("输入便签内容"));
    m_editor->hide();
    m_editor->installEventFilter(this);
    m_editor->viewport()->installEventFilter(this);

    m_formatBar = new QWidget(this);
    m_formatBar->setObjectName(QStringLiteral("formatBar"));
    m_formatBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_formatBar->setFocusPolicy(Qt::NoFocus);
    m_formatBar->hide();

    auto *formatLayout = new QHBoxLayout(m_formatBar);
    formatLayout->setContentsMargins(8, 6, 8, 6);
    formatLayout->setSpacing(6);

    auto createFormatButton = [this, formatLayout](const QString &label,
                                                    const QString &toolTip,
                                                    bool checkable) {
        auto *button = new QToolButton(m_formatBar);
        button->setText(label);
        button->setToolTip(toolTip);
        button->setCheckable(checkable);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        formatLayout->addWidget(button);
        return button;
    };

    m_boldButton = createFormatButton(QStringLiteral("B"), QStringLiteral("加粗"), true);
    m_italicButton = createFormatButton(QStringLiteral("I"), QStringLiteral("斜体"), true);
    m_underlineButton = createFormatButton(QStringLiteral("U"), QStringLiteral("下划线"), true);
    m_strikeButton = createFormatButton(QStringLiteral("S"), QStringLiteral("删除线"), true);
    m_fontFamilyButton = createFormatButton(QStringLiteral("字"), QStringLiteral("字体"), false);
    m_fontFamilyButton->setPopupMode(QToolButton::InstantPopup);
    m_fontFamilyMenu = new QMenu(m_fontFamilyButton);
    ThemeHelper::polishMenu(m_fontFamilyMenu, m_uiStyle, m_hue);
    struct FontFamilyAction {
        const char *label;
        const char *family;
    };
    const FontFamilyAction fontFamilyActions[] = {
        {"默认", ""},
        {"微软雅黑", "Microsoft YaHei UI"},
        {"Segoe UI", "Segoe UI"},
        {"Consolas", "Consolas"},
        {"Georgia", "Georgia"},
    };
    for (const FontFamilyAction &entry : fontFamilyActions) {
        QAction *action = m_fontFamilyMenu->addAction(QString::fromUtf8(entry.label));
        action->setCheckable(true);
        action->setData(QString::fromUtf8(entry.family));
    }
    m_fontFamilyButton->setMenu(m_fontFamilyMenu);

    m_fontSizeButton = createFormatButton(QStringLiteral("号"), QStringLiteral("字号"), false);
    m_fontSizeButton->setPopupMode(QToolButton::InstantPopup);
    m_fontSizeMenu = new QMenu(m_fontSizeButton);
    ThemeHelper::polishMenu(m_fontSizeMenu, m_uiStyle, m_hue);
    const int fontSizes[] = {10, 11, 12, 14, 16, 18, 22, 26, 32};
    for (int size : fontSizes) {
        QAction *action = m_fontSizeMenu->addAction(QStringLiteral("%1 pt").arg(size));
        action->setCheckable(true);
        action->setData(size);
    }
    m_fontSizeButton->setMenu(m_fontSizeMenu);

    m_textColorButton = createFormatButton(QStringLiteral("色"), QStringLiteral("文字颜色"), false);
    m_bulletListButton = createFormatButton(QStringLiteral("•"), QStringLiteral("项目符号列表"), true);
    m_numberedListButton = createFormatButton(QStringLiteral("1."), QStringLiteral("数字编号列表"), true);
    m_checkListButton = createFormatButton(QStringLiteral("☐"), QStringLiteral("复选清单"), true);
    m_clearFormatButton = createFormatButton(QStringLiteral("Tx"), QStringLiteral("清除格式"), false);
    m_alignButton = createFormatButton(QStringLiteral("左"), QStringLiteral("段落对齐"), false);
    m_alignButton->setPopupMode(QToolButton::InstantPopup);
    m_alignMenu = new QMenu(m_alignButton);
    ThemeHelper::polishMenu(m_alignMenu, m_uiStyle, m_hue);
    m_alignLeftAction = m_alignMenu->addAction(QStringLiteral("左对齐"));
    m_alignCenterAction = m_alignMenu->addAction(QStringLiteral("居中"));
    m_alignRightAction = m_alignMenu->addAction(QStringLiteral("右对齐"));
    m_alignJustifyAction = m_alignMenu->addAction(QStringLiteral("两端对齐"));
    const QAction *alignActions[] = {
        m_alignLeftAction,
        m_alignCenterAction,
        m_alignRightAction,
        m_alignJustifyAction,
    };
    for (const QAction *actionConst : alignActions) {
        auto *action = const_cast<QAction *>(actionConst);
        action->setCheckable(true);
    }
    m_alignButton->setMenu(m_alignMenu);

    formatLayout->addStretch(1);
    m_searchToggleButton = createFormatButton(QStringLiteral("查"), QStringLiteral("搜索（Ctrl+F）"), true);
    m_searchEdit = new QLineEdit(m_formatBar);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索事项"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFocusPolicy(Qt::StrongFocus);
    m_searchEdit->hide();
    formatLayout->addWidget(m_searchEdit);
    m_searchPrevButton = createFormatButton(QStringLiteral("↑"), QStringLiteral("上一个（Shift+F3）"), false);
    m_searchNextButton = createFormatButton(QStringLiteral("↓"), QStringLiteral("下一个（F3）"), false);
    m_searchPrevButton->hide();
    m_searchNextButton->hide();

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(140);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_dropHoverAnimation = new QPropertyAnimation(this, "dropHoverProgress", this);
    m_dropHoverAnimation->setDuration(120);
    m_dropHoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_dragHoldTimer = new QTimer(this);
    m_dragHoldTimer->setSingleShot(true);
    m_dragHoldTimer->setInterval(260);
    connect(m_dragHoldTimer, &QTimer::timeout, this, [this]() {
        if (!m_pressActivated || m_draggingWindow || m_noteDragActive || m_windowLocked) {
            return;
        }
        if (m_editor != nullptr && m_editor->isVisible()) {
            return;
        }

        m_pressActivated = false;
        m_draggingWindow = false;
        setNoteDragActive(true);
        emit dragHoldStarted(m_noteId, QCursor::pos());
    });

    connect(m_editor, &QTextEdit::textChanged, this, &NoteCardWidget::syncEditorHeight);
    connect(m_editor,
            &QTextEdit::cursorPositionChanged,
            this,
            &NoteCardWidget::updateFormattingToolbarState);
    connect(m_editor,
            &QTextEdit::currentCharFormatChanged,
            this,
            [this](const QTextCharFormat &) {
                updateFormattingToolbarState();
            });

    connect(m_boldButton, &QToolButton::toggled, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontWeight(checked ? QFont::Bold : QFont::Normal);
        mergeEditorCharFormat(format);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_italicButton, &QToolButton::toggled, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontItalic(checked);
        mergeEditorCharFormat(format);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_underlineButton, &QToolButton::toggled, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontUnderline(checked);
        mergeEditorCharFormat(format);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_strikeButton, &QToolButton::toggled, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontStrikeOut(checked);
        mergeEditorCharFormat(format);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    if (m_fontFamilyMenu != nullptr) {
        connect(m_fontFamilyMenu, &QMenu::triggered, this, [this](QAction *action) {
            if (action == nullptr) {
                return;
            }
            applyEditorFontFamily(action->data().toString());
            m_editor->setFocus(Qt::OtherFocusReason);
        });
    }
    if (m_fontSizeMenu != nullptr) {
        connect(m_fontSizeMenu, &QMenu::triggered, this, [this](QAction *action) {
            if (action == nullptr) {
                return;
            }
            applyEditorFontPointSize(action->data().toDouble());
            m_editor->setFocus(Qt::OtherFocusReason);
        });
    }
    connect(m_textColorButton, &QToolButton::clicked, this, [this]() {
        QColor initialColor = m_editor->currentCharFormat().foreground().color();
        if (!initialColor.isValid()) {
            initialColor = ThemeHelper::paletteFor(m_uiStyle, m_hue, false).text;
        }

        const QColor picked = QColorDialog::getColor(initialColor,
                                                     this,
                                                     QStringLiteral("选择文字颜色"));
        if (!picked.isValid()) {
            return;
        }

        applyEditorTextColor(picked);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_bulletListButton, &QToolButton::clicked, this, [this]() {
        toggleListStyle(QTextListFormat::ListDisc);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_numberedListButton, &QToolButton::clicked, this, [this]() {
        toggleListStyle(QTextListFormat::ListDecimal);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_checkListButton, &QToolButton::clicked, this, [this]() {
        toggleChecklistItems();
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_clearFormatButton, &QToolButton::clicked, this, [this]() {
        clearEditorFormatting();
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_alignLeftAction, &QAction::triggered, this, [this]() {
        setParagraphAlignment(Qt::AlignLeft);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_alignCenterAction, &QAction::triggered, this, [this]() {
        setParagraphAlignment(Qt::AlignHCenter);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_alignRightAction, &QAction::triggered, this, [this]() {
        setParagraphAlignment(Qt::AlignRight);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_alignJustifyAction, &QAction::triggered, this, [this]() {
        setParagraphAlignment(Qt::AlignJustify);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_searchToggleButton, &QToolButton::toggled, this, [this](bool checked) {
        setSearchVisible(checked);
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        updateSearchHighlights();
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        findInEditor(false);
    });
    connect(m_searchPrevButton, &QToolButton::clicked, this, [this]() {
        findInEditor(true);
    });
    connect(m_searchNextButton, &QToolButton::clicked, this, [this]() {
        findInEditor(false);
    });

    layout->addWidget(m_displayLabel);
    layout->addWidget(m_formatBar);
    layout->addWidget(m_editor);

    applyScale();
    updateDisplayText();
}

QString NoteCardWidget::noteId() const {
    return m_noteId;
}

QString NoteCardWidget::text() const {
    return m_editor->toHtml();
}

QString NoteCardWidget::plainText() const {
    return m_editor->toPlainText();
}

int NoteCardWidget::hue() const {
    return m_hue;
}

QString NoteCardWidget::sticker() const {
    return m_sticker;
}

NoteLane NoteCardWidget::lane() const {
    return m_lane;
}

UiStyle NoteCardWidget::uiStyle() const {
    return m_uiStyle;
}

qreal NoteCardWidget::hoverProgress() const {
    return m_hoverProgress;
}

void NoteCardWidget::setNoteId(const QString &noteId) {
    m_noteId = noteId;
}

void NoteCardWidget::setText(const QString &text) {
    if (Qt::mightBeRichText(text)) {
        if (text == m_editor->toHtml()) {
            return;
        }
        m_editor->setHtml(text);
    } else {
        if (text == m_editor->toPlainText()) {
            return;
        }
        m_editor->setPlainText(text);
    }
    updateDisplayText();
    syncEditorHeight();
    emit layoutRefreshRequested();
}

void NoteCardWidget::setHue(int hue) {
    if (m_hue == hue) {
        return;
    }

    m_hue = hue;
    updateDisplayText();
    update();
}

void NoteCardWidget::setSticker(const QString &sticker) {
    if (m_sticker == sticker) {
        return;
    }

    m_sticker = sticker;
    update();
}

void NoteCardWidget::setImportedStickerLibrary(const QVector<QString> &stickers) {
    QVector<QString> normalized;
    normalized.reserve(stickers.size());

    QSet<QString> seen;
    seen.reserve(stickers.size());
    for (const QString &entry : stickers) {
        QString candidate;
        if (isImportedImageStickerValue(entry)) {
            const QString path = importedImageStickerPath(entry);
            if (path.isEmpty()) {
                continue;
            }
            candidate = encodeImportedImageStickerPath(path);
        } else if (!entry.trimmed().isEmpty()) {
            candidate = encodeImportedImageStickerPath(entry);
        }

        if (candidate.isEmpty()) {
            continue;
        }

        const QString key = candidate.toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        normalized.append(candidate);
    }

    m_importedStickerLibrary = normalized;
}

void NoteCardWidget::setLane(NoteLane lane) {
    m_lane = normalizedNoteLane(lane);
}

void NoteCardWidget::setUiScale(qreal scale) {
    if (qFuzzyCompare(m_uiScale, scale)) {
        return;
    }

    m_uiScale = scale;
    applyScale();
    syncEditorHeight();
    update();
}

void NoteCardWidget::setBaseLayerOpacity(qreal opacity) {
    const qreal clamped = qBound(constants::kMinBaseLayerOpacity,
                                 opacity,
                                 constants::kMaxBaseLayerOpacity);
    if (qFuzzyCompare(m_baseLayerOpacity, clamped)) {
        return;
    }

    m_baseLayerOpacity = clamped;
}

void NoteCardWidget::setExternalFileSyncEnabled(bool enabled) {
    m_externalFileSyncEnabled = enabled;
}

void NoteCardWidget::setAlwaysOnTopEnabled(bool enabled) {
    m_alwaysOnTopEnabled = enabled;
}

void NoteCardWidget::setLaunchAtStartupEnabled(bool enabled) {
    m_launchAtStartupEnabled = enabled;
}

void NoteCardWidget::setAutoCheckUpdatesEnabled(bool enabled) {
    m_autoCheckUpdatesEnabled = enabled;
}

void NoteCardWidget::setWindowLocked(bool enabled) {
    m_windowLocked = enabled;
    if (enabled) {
        m_draggingWindow = false;
        m_pressActivated = false;
        setNoteDragActive(false);
        if (m_dragHoldTimer != nullptr) {
            m_dragHoldTimer->stop();
        }
    }
}

void NoteCardWidget::setUiStyle(UiStyle uiStyle) {
    const UiStyle normalizedStyle = normalizedUiStyle(uiStyle);
    if (m_uiStyle == normalizedStyle) {
        return;
    }

    m_uiStyle = normalizedStyle;
    updateDisplayText();
    update();
}

void NoteCardWidget::setNoteDragActive(bool active) {
    if (m_noteDragActive == active) {
        return;
    }

    m_noteDragActive = active;
    if (m_dragHoldTimer != nullptr) {
        m_dragHoldTimer->stop();
    }

    if (active) {
        m_draggingWindow = false;
        m_pressActivated = false;
        setDropHoverActive(false);
        setCursor(Qt::ClosedHandCursor);
    } else {
        unsetCursor();
    }
}

void NoteCardWidget::setDropHoverActive(bool active) {
    if (active && m_noteDragActive) {
        active = false;
    }

    if (m_dropHoverActive == active) {
        return;
    }

    m_dropHoverActive = active;
    animateDropHoverTo(active ? 1.0 : 0.0);
}

void NoteCardWidget::setHoverProgress(qreal progress) {
    const qreal clamped = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_hoverProgress, clamped)) {
        return;
    }

    m_hoverProgress = clamped;
    update();
}

qreal NoteCardWidget::dropHoverProgress() const {
    return m_dropHoverProgress;
}

void NoteCardWidget::setDropHoverProgress(qreal progress) {
    const qreal clamped = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_dropHoverProgress, clamped)) {
        return;
    }

    m_dropHoverProgress = clamped;
    update();
}

void NoteCardWidget::startEditing() {
    setNoteDragActive(false);
    if (m_dragHoldTimer != nullptr) {
        m_dragHoldTimer->stop();
    }
    setMinimumHeight(baseCardHeight());
    setMaximumHeight(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_displayLabel->hide();
    m_formatBar->show();
    m_editor->show();
    if (m_searchToggleButton != nullptr && m_searchToggleButton->isChecked()) {
        setSearchVisible(true);
    }
    updateFormattingToolbarState();
    syncEditorHeight();
    updateGeometry();
    m_editor->setFocus();
    m_editor->moveCursor(QTextCursor::End);
    emit layoutRefreshRequested();
}

void NoteCardWidget::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    ThemeHelper::polishMenu(&menu, m_uiStyle, m_hue);

    QAction *newAction = menu.addAction(QStringLiteral("新建事项"));
    QAction *clearEmptyAction = menu.addAction(QStringLiteral("清空所有空事项"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(QStringLiteral("删除该事项"));

    QMenu *laneMenu = menu.addMenu(QStringLiteral("移动到泳道"));
    ThemeHelper::polishMenu(laneMenu, m_uiStyle, m_hue);
    struct LaneAction {
        const char *label;
        NoteLane lane;
    };
    const LaneAction laneActions[] = {
        {"Today", NoteLane::Today},
        {"Next", NoteLane::Next},
        {"Waiting", NoteLane::Waiting},
        {"Someday", NoteLane::Someday},
    };
    for (const LaneAction &entry : laneActions) {
        QAction *action = laneMenu->addAction(QString::fromUtf8(entry.label));
        action->setCheckable(true);
        action->setChecked(m_lane == entry.lane);
        action->setData(static_cast<int>(entry.lane));
    }

    QMenu *themeMenu = menu.addMenu(QStringLiteral("高级调色"));
    ThemeHelper::polishMenu(themeMenu, m_uiStyle, m_hue);
    struct HueAction {
        const char *label;
        int hue;
    };
    const HueAction hueActions[] = {
        {"跟随风格", -1},
        {"晨光琥珀", 38},
        {"桃雾珊瑚", 14},
        {"森林薄荷", 148},
        {"海盐青蓝", 196},
        {"深空蓝", 220},
        {"电光青", 176},
        {"赛博洋红", 310},
        {"勃艮第", 344},
    };

    for (const HueAction &entry : hueActions) {
        QAction *action = themeMenu->addAction(QString::fromUtf8(entry.label));
        action->setCheckable(true);
        action->setChecked(m_hue == entry.hue);
        action->setData(entry.hue);
    }
    themeMenu->addSeparator();
    QAction *customThemeAction = themeMenu->addAction(QStringLiteral("自定义颜色..."));

    QMenu *stickerMenu = menu.addMenu(QStringLiteral("贴纸"));
    ThemeHelper::polishMenu(stickerMenu, m_uiStyle, m_hue);
    struct StickerAction {
        const char *label;
        const char *value;
    };
    const StickerAction stickerActions[] = {
        {"无", ""},
        {"⭐ 星标", "⭐"},
        {"📌 标注", "📌"},
        {"✅ 完成", "✅"},
        {"💡 灵感", "💡"},
        {"🔥 紧急", "🔥"},
        {"TobyFox", "__tobyfox__"},
    };
    for (const StickerAction &entry : stickerActions) {
        const QString stickerValue = QString::fromUtf8(entry.value);
        QAction *action = stickerMenu->addAction(QString::fromUtf8(entry.label));
        action->setCheckable(true);
        const bool selected = isTobyfoxStickerValue(stickerValue)
                                  ? isTobyfoxStickerValue(m_sticker)
                                  : (m_sticker == stickerValue);
        action->setChecked(selected);
        action->setData(stickerValue);
        if (isTobyfoxStickerValue(stickerValue)) {
            action->setIcon(QIcon(kImageStickerResource));
        }
    }

    for (const QString &storedSticker : std::as_const(m_importedStickerLibrary)) {
        if (!isImportedImageStickerValue(storedSticker)) {
            continue;
        }

        const QString encodedSticker = encodeImportedImageStickerPath(importedImageStickerPath(storedSticker));
        const QString path = importedImageStickerPath(encodedSticker);
        if (path.isEmpty()) {
            continue;
        }

        QString displayName = QFileInfo(path).fileName();
        if (displayName.isEmpty()) {
            displayName = path;
        }

        QAction *action = stickerMenu->addAction(QStringLiteral("🖼 %1").arg(displayName));
        action->setCheckable(true);
        action->setData(encodedSticker);
        action->setToolTip(path);
        action->setChecked(importedImageStickerPath(m_sticker).compare(path, Qt::CaseInsensitive) == 0);
    }

    stickerMenu->addSeparator();
    QAction *importStickerAction = stickerMenu->addAction(QStringLiteral("🖼 导入贴纸..."));
    importStickerAction->setCheckable(true);
    importStickerAction->setChecked(isImportedImageStickerValue(m_sticker));
    const QString importedPath = importedImageStickerPath(m_sticker);
    if (!importedPath.isEmpty()) {
        importStickerAction->setToolTip(QFileInfo(importedPath).fileName());
    }

    QMenu *uiStyleMenu = menu.addMenu(QStringLiteral("界面风格"));
    ThemeHelper::polishMenu(uiStyleMenu, m_uiStyle);
    struct UiStyleAction {
        const char *label;
        UiStyle style;
    };
    const UiStyleAction uiStyleActions[] = {
        {"玻璃拟态", UiStyle::Glass},
        {"森林氧感", UiStyle::Meadow},
        {"石墨商务", UiStyle::Graphite},
        {"纸张手账", UiStyle::Paper},
        {"终端像素", UiStyle::Pixel},
        {"霓虹夜板", UiStyle::Neon},
        {"陶土质感", UiStyle::Clay},
    };
    for (const UiStyleAction &entry : uiStyleActions) {
        QAction *action = uiStyleMenu->addAction(QString::fromUtf8(entry.label));
        action->setCheckable(true);
        action->setChecked(m_uiStyle == entry.style);
        action->setData(static_cast<int>(entry.style));
    }

    menu.addSeparator();
    QMenu *scaleMenu = menu.addMenu(QStringLiteral("整体缩放"));
    ThemeHelper::polishMenu(scaleMenu, m_uiStyle, m_hue);
    QAction *scaleUpAction = scaleMenu->addAction(QStringLiteral("放大"));
    QAction *scaleDownAction = scaleMenu->addAction(QStringLiteral("缩小"));
    QAction *scaleResetAction = scaleMenu->addAction(QStringLiteral("重置"));

    QMenu *opacityMenu = menu.addMenu(QStringLiteral("底层透明度"));
    ThemeHelper::polishMenu(opacityMenu, m_uiStyle, m_hue);
    for (int percent = kMinBaseLayerOpacityPercent;
         percent <= kMaxBaseLayerOpacityPercent;
         percent += kBaseLayerOpacityStepPercent) {
        QAction *action = opacityMenu->addAction(QStringLiteral("%1%").arg(percent));
        action->setCheckable(true);
        action->setData(percent);
        const qreal optionOpacity = static_cast<qreal>(percent) / 100.0;
        action->setChecked(qAbs(optionOpacity - m_baseLayerOpacity)
                           < (static_cast<qreal>(kBaseLayerOpacityStepPercent) / 200.0));
    }

    menu.addSeparator();
    QMenu *dataMenu = menu.addMenu(QStringLiteral("数据与可靠性"));
    ThemeHelper::polishMenu(dataMenu, m_uiStyle, m_hue);
    QAction *exportJsonAction = dataMenu->addAction(QStringLiteral("导出 JSON..."));
    QAction *importJsonAction = dataMenu->addAction(QStringLiteral("导入 JSON..."));
    dataMenu->addSeparator();
    QAction *backupSnapshotAction = dataMenu->addAction(QStringLiteral("创建备份快照"));
    QAction *restoreBackupAction = dataMenu->addAction(QStringLiteral("从最近备份恢复"));
    dataMenu->addSeparator();
    QAction *timelineReplayAction = dataMenu->addAction(QStringLiteral("时间轴回放（按日期）..."));
    dataMenu->addSeparator();
    QAction *fileSyncAction = dataMenu->addAction(QStringLiteral("监听外部文件变更"));
    fileSyncAction->setCheckable(true);
    fileSyncAction->setChecked(m_externalFileSyncEnabled);
    dataMenu->addSeparator();
    QAction *checkUpdatesAction = dataMenu->addAction(QStringLiteral("检查更新..."));
    QAction *autoCheckUpdatesAction = dataMenu->addAction(QStringLiteral("启动时自动检查更新"));
    autoCheckUpdatesAction->setCheckable(true);
    autoCheckUpdatesAction->setChecked(m_autoCheckUpdatesEnabled);

    menu.addSeparator();
    QMenu *efficiencyMenu = menu.addMenu(QStringLiteral("效率功能"));
    ThemeHelper::polishMenu(efficiencyMenu, m_uiStyle, m_hue);
    QAction *alwaysOnTopAction = efficiencyMenu->addAction(QStringLiteral("窗口置顶"));
    alwaysOnTopAction->setCheckable(true);
    alwaysOnTopAction->setChecked(m_alwaysOnTopEnabled);
    QAction *launchAtStartupAction = efficiencyMenu->addAction(QStringLiteral("开机自启"));
    launchAtStartupAction->setCheckable(true);
    launchAtStartupAction->setChecked(m_launchAtStartupEnabled);
    QAction *windowLockAction = efficiencyMenu->addAction(QStringLiteral("锁定窗口位置与尺寸"));
    windowLockAction->setCheckable(true);
    windowLockAction->setChecked(m_windowLocked);

    menu.addSeparator();
    QAction *openDirAction = menu.addAction(QStringLiteral("打开 JSON 数据目录"));
    QAction *quitAction = menu.addAction(QStringLiteral("退出软件"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == nullptr) {
        return;
    }

    if (chosen == newAction) {
        emit addNoteRequested();
        return;
    }

    if (chosen == clearEmptyAction) {
        emit clearEmptyRequested();
        return;
    }

    if (chosen == deleteAction) {
        emit deleteRequested(m_noteId);
        return;
    }

    if (chosen->parent() == laneMenu) {
        const NoteLane selectedLane = normalizedNoteLane(static_cast<NoteLane>(chosen->data().toInt()));
        emit laneChangeRequested(m_noteId, selectedLane);
        return;
    }

    if (chosen->parent() == themeMenu) {
        if (chosen == customThemeAction) {
            const QColor initialColor = m_hue >= 0 ? QColor::fromHsl(m_hue, 130, 150) : QColor(120, 160, 255);
            const QColor picked = QColorDialog::getColor(initialColor,
                                                         this,
                                                         QStringLiteral("选择事项配色"),
                                                         QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) {
                return;
            }

            int hue = picked.hslHue();
            if (hue < 0) {
                hue = -1;
            }
            emit hueChangeRequested(m_noteId, hue);
            return;
        }

        emit hueChangeRequested(m_noteId, chosen->data().toInt());
        return;
    }

    if (chosen == importStickerAction) {
        const QString filePath = QFileDialog::getOpenFileName(this,
                                                              QStringLiteral("选择贴纸图片"),
                                                              QString(),
                                                              QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg)"));
        if (filePath.isEmpty()) {
            return;
        }

        emit stickerChangeRequested(m_noteId, encodeImportedImageStickerPath(filePath));
        return;
    }

    if (chosen->parent() == stickerMenu) {
        emit stickerChangeRequested(m_noteId, chosen->data().toString());
        return;
    }

    if (chosen->parent() == uiStyleMenu) {
        const UiStyle selectedStyle = normalizedUiStyle(static_cast<UiStyle>(chosen->data().toInt()));
        emit uiStyleChangeRequested(selectedStyle);
        return;
    }

    if (chosen == scaleUpAction) {
        emit scaleInRequested();
        return;
    }

    if (chosen == scaleDownAction) {
        emit scaleOutRequested();
        return;
    }

    if (chosen == scaleResetAction) {
        emit scaleResetRequested();
        return;
    }

    if (chosen->parent() == opacityMenu) {
        emit baseLayerOpacitySetRequested(static_cast<qreal>(chosen->data().toInt()) / 100.0);
        return;
    }

    if (chosen == exportJsonAction) {
        emit exportJsonRequested();
        return;
    }

    if (chosen == importJsonAction) {
        emit importJsonRequested();
        return;
    }

    if (chosen == backupSnapshotAction) {
        emit backupSnapshotRequested();
        return;
    }

    if (chosen == restoreBackupAction) {
        emit restoreLatestBackupRequested();
        return;
    }

    if (chosen == fileSyncAction) {
        m_externalFileSyncEnabled = fileSyncAction->isChecked();
        emit externalFileSyncToggled(m_externalFileSyncEnabled);
        return;
    }

    if (chosen == checkUpdatesAction) {
        emit checkForUpdatesRequested();
        return;
    }

    if (chosen == autoCheckUpdatesAction) {
        m_autoCheckUpdatesEnabled = autoCheckUpdatesAction->isChecked();
        emit autoCheckUpdatesToggled(m_autoCheckUpdatesEnabled);
        return;
    }

    if (chosen == alwaysOnTopAction) {
        m_alwaysOnTopEnabled = alwaysOnTopAction->isChecked();
        emit alwaysOnTopToggled(m_alwaysOnTopEnabled);
        return;
    }

    if (chosen == launchAtStartupAction) {
        m_launchAtStartupEnabled = launchAtStartupAction->isChecked();
        emit launchAtStartupToggled(m_launchAtStartupEnabled);
        return;
    }

    if (chosen == windowLockAction) {
        m_windowLocked = windowLockAction->isChecked();
        emit windowLockToggled(m_windowLocked);
        return;
    }

    if (chosen == openDirAction) {
        emit openStorageDirectoryRequested();
        return;
    }

    if (chosen == timelineReplayAction) {
        emit timelineReplayRequested();
        return;
    }

    if (chosen == quitAction) {
        emit quitRequested();
    }
}

void NoteCardWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal outlineInset = 3.0 * m_uiScale;
    QRectF bodyRect = rect().adjusted(outlineInset, outlineInset, -outlineInset, -outlineInset);
    const qreal dropLift = (2.8 * m_uiScale) * m_dropHoverProgress;
    bodyRect.translate(0.0, -dropLift);
    qreal radiusBase = constants::kCardCornerRadius;
    switch (m_uiStyle) {
    case UiStyle::Pixel:
        radiusBase = 2.0;
        break;
    case UiStyle::Graphite:
        radiusBase = 12.0;
        break;
    case UiStyle::Paper:
        radiusBase = 16.0;
        break;
    case UiStyle::Neon:
        radiusBase = 14.0;
        break;
    case UiStyle::Clay:
        radiusBase = 16.0;
        break;
    case UiStyle::Glass:
        radiusBase = 16.0;
        break;
    case UiStyle::Meadow:
    default:
        radiusBase = constants::kCardCornerRadius;
        break;
    }
    const qreal radius = radiusBase * m_uiScale;
    const qreal visualHoverProgress = qBound(0.0, m_hoverProgress + (m_dropHoverProgress * 0.42), 1.0);
    const NotePalette palette = blendPalette(ThemeHelper::paletteFor(m_uiStyle, m_hue, false),
                                             ThemeHelper::paletteFor(m_uiStyle, m_hue, true),
                                             visualHoverProgress);

    if (m_uiStyle != UiStyle::Pixel) {
        QColor shadowColor = palette.shadow;
        shadowColor.setAlpha(qBound(18,
                                    static_cast<int>((shadowColor.alpha() * 0.56)
                                                     + (36.0 * m_dropHoverProgress)),
                                    148));
        painter.setPen(Qt::NoPen);
        painter.setBrush(shadowColor);
        const QRectF shadowRect = bodyRect.adjusted(0.0, 1.5 * m_uiScale, 0.0, 1.5 * m_uiScale);
        painter.drawRoundedRect(shadowRect, radius + (0.8 * m_uiScale), radius + (0.8 * m_uiScale));
    }

    QLinearGradient fillGradient(bodyRect.topLeft(), bodyRect.bottomLeft());
    fillGradient.setColorAt(0.0, palette.fillTop);
    fillGradient.setColorAt(1.0, palette.fillBottom);
    painter.setBrush(fillGradient);
    painter.setPen(QPen(palette.border, 1.0));
    painter.drawRoundedRect(bodyRect, radius, radius);

    QPainterPath bodyPath;
    bodyPath.addRoundedRect(bodyRect, radius, radius);
    painter.save();
    painter.setClipPath(bodyPath);

    QRectF highlightRect = bodyRect.adjusted(1.0, 1.0, -1.0, -(bodyRect.height() * 0.52));
    QLinearGradient highlightGradient(highlightRect.topLeft(), highlightRect.bottomLeft());
    highlightGradient.setColorAt(0.0, palette.highlightTop);
    highlightGradient.setColorAt(1.0, QColor(255, 255, 255, 0));

    QPainterPath highlightPath;
    highlightPath.addRoundedRect(highlightRect, radius - 2.0, radius - 2.0);
    painter.fillPath(highlightPath, highlightGradient);

    switch (m_uiStyle) {
    case UiStyle::Glass: {
        QLinearGradient sheen(bodyRect.topLeft(), bodyRect.bottomLeft());
        sheen.setColorAt(0.0, QColor(234, 244, 255, 28));
        sheen.setColorAt(0.35, QColor(206, 224, 244, 12));
        sheen.setColorAt(1.0, QColor(18, 30, 46, 6));
        painter.fillPath(bodyPath, sheen);

        painter.setPen(QPen(QColor(226, 242, 255, 10), 1.0));
        const int prismStep = qMax(16, static_cast<int>(20.0 * m_uiScale));
        for (int x = static_cast<int>(bodyRect.left()) - prismStep;
             x < static_cast<int>(bodyRect.right()) + prismStep;
             x += prismStep) {
            painter.drawLine(x,
                             static_cast<int>(bodyRect.top()) + 1,
                             x + prismStep,
                             static_cast<int>(bodyRect.bottom()) - 1);
        }
        break;
    }
    case UiStyle::Meadow: {
        QRadialGradient canopy(bodyRect.right() - (bodyRect.width() * 0.22),
                               bodyRect.top() + (bodyRect.height() * 0.22),
                               bodyRect.width() * 0.85);
        canopy.setColorAt(0.0, QColor(174, 224, 184, 34));
        canopy.setColorAt(0.55, QColor(100, 164, 122, 16));
        canopy.setColorAt(1.0, QColor(30, 66, 46, 0));
        painter.fillPath(bodyPath, canopy);

        QLinearGradient depth(bodyRect.topLeft(), bodyRect.bottomLeft());
        depth.setColorAt(0.0, QColor(232, 246, 236, 8));
        depth.setColorAt(1.0, QColor(18, 62, 40, 16));
        painter.fillPath(bodyPath, depth);

        painter.setPen(QPen(QColor(178, 222, 184, 10), 1.0));
        const int contourStep = qMax(8, static_cast<int>(11.0 * m_uiScale));
        for (int y = static_cast<int>(bodyRect.top()) + contourStep;
             y < static_cast<int>(bodyRect.bottom()) - 2;
             y += contourStep) {
            if (((y / contourStep) & 1) == 0) {
                painter.drawLine(static_cast<int>(bodyRect.left()) + 3,
                                 y,
                                 static_cast<int>(bodyRect.right()) - 3,
                                 y);
            }
        }
        break;
    }
    case UiStyle::Graphite: {
        QLinearGradient metallic(bodyRect.topLeft(), bodyRect.bottomLeft());
        metallic.setColorAt(0.0, QColor(204, 218, 240, 16));
        metallic.setColorAt(0.45, QColor(148, 164, 190, 6));
        metallic.setColorAt(1.0, QColor(0, 0, 0, 32));
        painter.fillPath(bodyPath, metallic);

        painter.setPen(QPen(QColor(168, 186, 212, 8), 1.0));
        const int lineStep = qMax(5, static_cast<int>(7.0 * m_uiScale));
        for (int y = static_cast<int>(bodyRect.top()) + lineStep; y < static_cast<int>(bodyRect.bottom()); y += lineStep) {
            painter.drawLine(static_cast<int>(bodyRect.left()) + 2,
                             y,
                             static_cast<int>(bodyRect.right()) - 2,
                             y);
        }

        painter.setPen(QPen(QColor(200, 216, 238, 18), 1.0));
        const int cornerLen = qMax(6, static_cast<int>(9.0 * m_uiScale));
        const int left = static_cast<int>(bodyRect.left()) + 3;
        const int right = static_cast<int>(bodyRect.right()) - 3;
        const int top = static_cast<int>(bodyRect.top()) + 3;
        const int bottom = static_cast<int>(bodyRect.bottom()) - 3;
        painter.drawLine(left, top, left + cornerLen, top);
        painter.drawLine(left, top, left, top + cornerLen);
        painter.drawLine(right - cornerLen, top, right, top);
        painter.drawLine(right, top, right, top + cornerLen);
        painter.drawLine(left, bottom - cornerLen, left, bottom);
        painter.drawLine(left, bottom, left + cornerLen, bottom);
        painter.drawLine(right - cornerLen, bottom, right, bottom);
        painter.drawLine(right, bottom - cornerLen, right, bottom);
        break;
    }
    case UiStyle::Paper: {
        QLinearGradient paperDepth(bodyRect.topLeft(), bodyRect.bottomLeft());
        paperDepth.setColorAt(0.0, QColor(255, 252, 240, 10));
        paperDepth.setColorAt(1.0, QColor(122, 96, 70, 12));
        painter.fillPath(bodyPath, paperDepth);

        painter.setPen(QPen(QColor(132, 106, 74, 36), 1.0));
        const int step = qMax(4, static_cast<int>(6.0 * m_uiScale));
        for (int y = static_cast<int>(bodyRect.top()) + step; y < static_cast<int>(bodyRect.bottom()); y += step) {
            painter.drawLine(static_cast<int>(bodyRect.left()) + 4,
                             y,
                             static_cast<int>(bodyRect.right()) - 4,
                             y);
        }

        painter.setPen(QPen(QColor(168, 132, 94, 28), 1.0));
        const int marginX = static_cast<int>(bodyRect.left()) + qMax(12, static_cast<int>(14.0 * m_uiScale));
        painter.drawLine(marginX,
                         static_cast<int>(bodyRect.top()) + 4,
                         marginX,
                         static_cast<int>(bodyRect.bottom()) - 4);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(176, 142, 106, 20));
        const qreal punchRadius = qMax<qreal>(1.4, 1.8 * m_uiScale);
        const qreal punchX = bodyRect.left() + qMax<qreal>(5.0, 7.0 * m_uiScale);
        painter.drawEllipse(QPointF(punchX, bodyRect.top() + (bodyRect.height() * 0.28)), punchRadius, punchRadius);
        painter.drawEllipse(QPointF(punchX, bodyRect.top() + (bodyRect.height() * 0.72)), punchRadius, punchRadius);
        break;
    }
    case UiStyle::Pixel: {
        painter.fillPath(bodyPath, QColor(4, 8, 5, 158));

        const int cell = qMax(5, static_cast<int>(8.0 * m_uiScale));
        painter.setPen(QPen(QColor(96, 168, 104, 30), 1.0));
        for (int y = static_cast<int>(bodyRect.top()) + cell; y < static_cast<int>(bodyRect.bottom()); y += cell) {
            painter.drawLine(static_cast<int>(bodyRect.left()) + 1,
                             y,
                             static_cast<int>(bodyRect.right()) - 1,
                             y);
        }
        for (int x = static_cast<int>(bodyRect.left()) + cell; x < static_cast<int>(bodyRect.right()); x += cell) {
            painter.drawLine(x,
                             static_cast<int>(bodyRect.top()) + 1,
                             x,
                             static_cast<int>(bodyRect.bottom()) - 1);
        }

        painter.setPen(QPen(QColor(138, 216, 146, 24), 1.0));
        const int noiseStep = qMax(3, static_cast<int>(4.0 * m_uiScale));
        for (int y = static_cast<int>(bodyRect.top()) + 1; y < static_cast<int>(bodyRect.bottom()) - 1; y += noiseStep) {
            for (int x = static_cast<int>(bodyRect.left()) + 1; x < static_cast<int>(bodyRect.right()) - 1; x += noiseStep) {
                const int hash = ((x * 11) ^ (y * 29)) & 31;
                if (hash == 0) {
                    painter.drawPoint(x, y);
                }
            }
        }
        break;
    }
    case UiStyle::Neon: {
        QRadialGradient bloom(bodyRect.center().x(), bodyRect.center().y(), bodyRect.width() * 0.9);
        bloom.setColorAt(0.0, QColor(224, 136, 255, 56));
        bloom.setColorAt(0.45, QColor(94, 232, 255, 26));
        bloom.setColorAt(1.0, QColor(14, 8, 28, 0));
        painter.fillPath(bodyPath, bloom);

        QLinearGradient neonEdge(bodyRect.topLeft(), bodyRect.bottomLeft());
        neonEdge.setColorAt(0.0, QColor(255, 176, 255, 28));
        neonEdge.setColorAt(1.0, QColor(0, 255, 232, 16));
        painter.fillPath(bodyPath, neonEdge);

        painter.setPen(QPen(QColor(164, 255, 246, 14), 1.0));
        const int scanStep = qMax(6, static_cast<int>(8.0 * m_uiScale));
        for (int y = static_cast<int>(bodyRect.top()) + scanStep;
             y < static_cast<int>(bodyRect.bottom()) - 2;
             y += scanStep) {
            if (((y / scanStep) & 1) == 0) {
                painter.drawLine(static_cast<int>(bodyRect.left()) + 2,
                                 y,
                                 static_cast<int>(bodyRect.right()) - 2,
                                 y);
            }
        }
        break;
    }
    case UiStyle::Clay: {
        QLinearGradient matte(bodyRect.topLeft(), bodyRect.bottomLeft());
        matte.setColorAt(0.0, QColor(255, 238, 218, 34));
        matte.setColorAt(0.4, QColor(224, 184, 148, 16));
        matte.setColorAt(1.0, QColor(108, 68, 46, 22));
        painter.fillPath(bodyPath, matte);

        QRadialGradient soft(bodyRect.left() + (bodyRect.width() * 0.28),
                             bodyRect.top() + (bodyRect.height() * 0.2),
                             bodyRect.width() * 0.84);
        soft.setColorAt(0.0, QColor(255, 228, 196, 18));
        soft.setColorAt(1.0, QColor(188, 120, 82, 0));
        painter.fillPath(bodyPath, soft);

        QLinearGradient rim(bodyRect.topLeft(), bodyRect.bottomLeft());
        rim.setColorAt(0.0, QColor(255, 246, 230, 22));
        rim.setColorAt(0.58, QColor(255, 246, 230, 0));
        rim.setColorAt(1.0, QColor(94, 58, 38, 20));
        painter.fillPath(bodyPath, rim);
        break;
    }
    }

    if (m_dropHoverProgress > 0.001) {
        QColor dropGlowTop = palette.highlightTop.lighter(128);
        dropGlowTop.setAlpha(qBound(0,
                                    static_cast<int>(42.0 * m_dropHoverProgress),
                                    96));
        QRectF dropGlowRect = bodyRect.adjusted(1.0, 1.0, -1.0, -(bodyRect.height() * 0.54));
        QLinearGradient dropGlowGradient(dropGlowRect.topLeft(), dropGlowRect.bottomLeft());
        dropGlowGradient.setColorAt(0.0, dropGlowTop);
        dropGlowGradient.setColorAt(1.0, QColor(dropGlowTop.red(),
                                                dropGlowTop.green(),
                                                dropGlowTop.blue(),
                                                0));
        QPainterPath dropGlowPath;
        dropGlowPath.addRoundedRect(dropGlowRect,
                                    qMax<qreal>(1.0, radius - (2.0 * m_uiScale)),
                                    qMax<qreal>(1.0, radius - (2.0 * m_uiScale)));
        painter.fillPath(dropGlowPath, dropGlowGradient);

        QColor dropOutline = palette.highlightTop.lighter(136);
        dropOutline.setAlpha(qBound(0,
                                    static_cast<int>(64.0 + (84.0 * m_dropHoverProgress)),
                                    190));
        const qreal outlineWidth = (1.0 + (1.1 * m_dropHoverProgress)) * m_uiScale;
        painter.setPen(QPen(dropOutline, outlineWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bodyRect.adjusted(0.5, 0.5, -0.5, -0.5),
                                qMax<qreal>(1.0, radius - (0.4 * m_uiScale)),
                                qMax<qreal>(1.0, radius - (0.4 * m_uiScale)));
    }

    if (!m_sticker.trimmed().isEmpty()) {
        QFont stickerFont = painter.font();
        stickerFont.setPointSizeF(qMax<qreal>(10.0, 14.0 * m_uiScale));
        stickerFont.setBold(true);
        painter.setFont(stickerFont);

        const QString stickerText = m_sticker.trimmed();
        const int iconPadding = qMax(6, static_cast<int>(8.0 * m_uiScale));
        const QRect anchorRect = bodyRect.toRect().adjusted(iconPadding,
                                                             iconPadding,
                                                             -iconPadding,
                                                             -iconPadding);

        QString displayStickerText = fallbackStickerText(stickerText);
        QSize contentSize;
        QPixmap stickerPixmap = stickerPixmapForValue(stickerText);
        if (!stickerPixmap.isNull()) {
            const int iconSize = qMax(14, static_cast<int>(18.0 * m_uiScale));
            stickerPixmap = stickerPixmap.scaled(iconSize,
                                                 iconSize,
                                                 Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
            contentSize = stickerPixmap.size();
        }

        if (contentSize.isEmpty()) {
            const QFontMetrics metrics(stickerFont);
            contentSize = metrics.size(Qt::TextSingleLine, displayStickerText);
        }

        const QRect stickerRect(anchorRect.right() - contentSize.width() + 1,
                                anchorRect.top(),
                                contentSize.width(),
                                contentSize.height());

        QColor stickerColor = palette.text;
        stickerColor.setAlpha(236);

        if (!stickerPixmap.isNull()) {
            const int iconX = stickerRect.x() + ((stickerRect.width() - stickerPixmap.width()) / 2);
            const int iconY = stickerRect.y() + ((stickerRect.height() - stickerPixmap.height()) / 2);
            painter.drawPixmap(iconX, iconY, stickerPixmap);
        } else {
            painter.setPen(stickerColor);
            painter.drawText(stickerRect, Qt::AlignCenter, displayStickerText);
        }
    }

    painter.restore();
}

void NoteCardWidget::enterEvent(QEnterEvent *event) {
    animateHoverTo(1.0);
    QWidget::enterEvent(event);
}

void NoteCardWidget::leaveEvent(QEvent *event) {
    if (!m_draggingWindow && !m_noteDragActive) {
        animateHoverTo(0.0);
    }
    QWidget::leaveEvent(event);
}

void NoteCardWidget::mousePressEvent(QMouseEvent *event) {
    if (!m_editor->isVisible() && event->button() == Qt::LeftButton && !m_windowLocked) {
        auto *mainWindow = qobject_cast<MainWindow *>(window());
        if (mainWindow != nullptr && mainWindow->startResizeIfNeeded(event->globalPosition().toPoint())) {
            event->accept();
            return;
        }

        if (window() != nullptr) {
            m_draggingWindow = false;
            setNoteDragActive(false);
            m_pressActivated = true;
            m_pressGlobalPos = event->globalPosition().toPoint();
            m_dragOffset = m_pressGlobalPos - window()->frameGeometry().topLeft();
            if (m_dragHoldTimer != nullptr) {
                m_dragHoldTimer->start();
            }
            animateHoverTo(1.0);
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void NoteCardWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_windowLocked) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_noteDragActive) {
        event->accept();
        return;
    }

    auto *mainWindow = qobject_cast<MainWindow *>(window());
    if (mainWindow != nullptr && mainWindow->isResizingWindow()) {
        mainWindow->updateManualResize(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    if (m_pressActivated && (event->buttons() & Qt::LeftButton) != 0 && window() != nullptr) {
        const int dragDistance = (event->globalPosition().toPoint() - m_pressGlobalPos).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            if (m_dragHoldTimer != nullptr) {
                m_dragHoldTimer->stop();
            }
            m_draggingWindow = true;
        }
    }

    if (m_draggingWindow && (event->buttons() & Qt::LeftButton) != 0 && window() != nullptr) {
        window()->move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void NoteCardWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_dragHoldTimer != nullptr) {
            m_dragHoldTimer->stop();
        }

        if (m_noteDragActive) {
            event->accept();
            return;
        }

        if (m_windowLocked) {
            m_draggingWindow = false;
            m_pressActivated = false;
            QWidget::mouseReleaseEvent(event);
            return;
        }

        auto *mainWindow = qobject_cast<MainWindow *>(window());
        if (mainWindow != nullptr && mainWindow->isResizingWindow()) {
            mainWindow->finishManualResize();
            event->accept();
            return;
        }

        m_draggingWindow = false;
        if (m_pressActivated) {
            m_pressActivated = false;
            animateHoverTo(rect().contains(mapFromGlobal(QCursor::pos())) ? 1.0 : 0.0);
            event->accept();
            return;
        }
    }

    QWidget::mouseReleaseEvent(event);
}

void NoteCardWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        startEditing();
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void NoteCardWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateDisplayText();
}

bool NoteCardWidget::eventFilter(QObject *watched, QEvent *event) {
    if (m_editor != nullptr && (watched == m_editor || watched == m_editor->viewport())) {
        if (event->type() == QEvent::FocusOut) {
            QWidget *focusWidget = QApplication::focusWidget();
            const bool focusInToolbar = focusWidget != nullptr
                                        && (focusWidget == m_formatBar || m_formatBar->isAncestorOf(focusWidget));

            QWidget *widgetUnderCursor = QApplication::widgetAt(QCursor::pos());
            const bool cursorInToolbar = widgetUnderCursor != nullptr
                                         && (widgetUnderCursor == m_formatBar || m_formatBar->isAncestorOf(widgetUnderCursor));

            if (focusInToolbar || cursorInToolbar) {
                return QWidget::eventFilter(watched, event);
            }

            finishEditing();
        } else if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);

            if (handleEditorShortcut(keyEvent)) {
                return true;
            }

            if (keyEvent->key() == Qt::Key_Escape) {
                finishEditing();
                return true;
            }

            if (keyEvent->key() == Qt::Key_Backspace) {
                if (handleListAndChecklistBackspace()) {
                    return true;
                }
            }

            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                const Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
                if ((modifiers & Qt::ControlModifier) != 0) {
                    finishEditing();
                    return true;
                }

                if ((modifiers & Qt::ShiftModifier) != 0) {
                    if (handleSoftLineBreakInListContext()) {
                        return true;
                    }
                    return QWidget::eventFilter(watched, event);
                }

                if ((modifiers & Qt::ShiftModifier) == 0) {
                    if (handleListAndChecklistEnter()) {
                        return true;
                    }
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && mouseEvent->modifiers() == Qt::NoModifier) {
                QPoint localPos = mouseEvent->position().toPoint();
                if (watched == m_editor) {
                    localPos = m_editor->viewport()->mapFrom(m_editor, localPos);
                }

                const QTextCursor clicked = m_editor->cursorForPosition(localPos);
                const QString blockText = clicked.block().text();
                const ChecklistState state = checklistStateForText(blockText);
                const int prefixLength = checklistPrefixLength(state);
                if (state != ChecklistState::None && clicked.positionInBlock() <= prefixLength) {
                    QTextCursor blockCursor(clicked.block());
                    blockCursor.movePosition(QTextCursor::StartOfBlock);
                    blockCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                    const QString toggledText = state == ChecklistState::Unchecked
                                                   ? (kChecklistCheckedPrefix + checklistTextWithoutPrefix(blockText))
                                                   : (kChecklistUncheckedPrefix + checklistTextWithoutPrefix(blockText));
                    blockCursor.insertText(toggledText);
                    m_editor->setTextCursor(blockCursor);
                    updateFormattingToolbarState();
                    return true;
                }
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void NoteCardWidget::updateDisplayText() {
    const QString currentText = m_editor->toPlainText();
    const bool isEmpty = currentText.trimmed().isEmpty();
    const NotePalette palette = blendPalette(ThemeHelper::paletteFor(m_uiStyle, m_hue, false),
                                             ThemeHelper::paletteFor(m_uiStyle, m_hue, true),
                                             m_hoverProgress);

    QColor toolbarTop = palette.fillTop;
    toolbarTop.setAlpha(qBound(40, toolbarTop.alpha() + 26, 255));
    QColor toolbarBottom = palette.fillBottom;
    toolbarBottom.setAlpha(qBound(30, toolbarBottom.alpha() + 18, 255));
    QColor toolbarHover = palette.highlightTop;
    toolbarHover.setAlpha(qBound(24, toolbarHover.alpha() + 14, 255));
    QColor toolbarChecked = palette.highlightTop;
    toolbarChecked.setAlpha(qBound(40, toolbarChecked.alpha() + 28, 255));

    if (m_alignMenu != nullptr) {
        ThemeHelper::polishMenu(m_alignMenu, m_uiStyle, m_hue);
    }
    if (m_fontFamilyMenu != nullptr) {
        ThemeHelper::polishMenu(m_fontFamilyMenu, m_uiStyle, m_hue);
    }
    if (m_fontSizeMenu != nullptr) {
        ThemeHelper::polishMenu(m_fontSizeMenu, m_uiStyle, m_hue);
    }

    const bool pixelStyle = m_uiStyle == UiStyle::Pixel;
    m_displayLabel->setText(elidedDisplayText(currentText));
    if (pixelStyle) {
        const QColor textColor = isEmpty ? palette.placeholder : palette.text;
        const QColor selectionBg = QColor(120, 206, 120, 140);
        const QColor selectionFg = QColor(8, 18, 9, 244);
        const QColor editorBg = QColor(4, 9, 6, 204);

        if (m_displayGlowEffect != nullptr) {
            m_displayGlowEffect->setEnabled(true);
            m_displayGlowEffect->setBlurRadius(8.0 * m_uiScale);
            m_displayGlowEffect->setColor(isEmpty ? QColor(108, 168, 108, 84) : QColor(146, 236, 146, 132));
        }

        m_displayLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(textColor.name(QColor::HexArgb)));

        m_editor->setCursorWidth(qMax(8, static_cast<int>(8.0 * m_uiScale)));
        m_editor->setOverwriteMode(true);
        m_editor->setStyleSheet(
            QStringLiteral("QTextEdit {"
                           "color: %1;"
                           "background: %2;"
                           "border: 1px solid %3;"
                           "border-radius: 1px;"
                           "padding: 4px;"
                           "selection-background-color: %4;"
                           "selection-color: %5;"
                           "font-family: 'Consolas', 'Courier New', monospace;"
                           "}")
                .arg(palette.text.name(QColor::HexArgb))
                .arg(editorBg.name(QColor::HexArgb))
                .arg(palette.border.name(QColor::HexArgb))
                .arg(selectionBg.name(QColor::HexArgb))
                .arg(selectionFg.name(QColor::HexArgb)));

        m_formatBar->setStyleSheet(
            QStringLiteral("QWidget#formatBar {"
                           "background: rgba(4, 10, 6, 222);"
                           "border: 1px solid %1;"
                           "border-radius: 1px;"
                           "}"
                           "QToolButton {"
                           "color: %2;"
                           "background: transparent;"
                           "border: 1px solid transparent;"
                           "border-radius: 1px;"
                           "padding: 1px 6px;"
                           "font-family: 'Consolas', 'Courier New', monospace;"
                           "}"
                           "QToolButton:hover {"
                           "background: rgba(30, 70, 34, 150);"
                           "border: 1px solid %1;"
                           "}"
                           "QToolButton:checked, QToolButton:pressed {"
                           "background: rgba(98, 170, 102, 148);"
                           "border: 1px solid %1;"
                           "color: rgba(10, 20, 11, 240);"
                           "}"
                           "QLineEdit {"
                           "color: %2;"
                           "background: rgba(2, 8, 5, 224);"
                           "border: 1px solid %1;"
                           "border-radius: 1px;"
                           "padding: 1px 7px;"
                           "selection-background-color: %3;"
                           "selection-color: %4;"
                           "font-family: 'Consolas', 'Courier New', monospace;"
                           "}"
                           "QLineEdit:focus {"
                           "border: 1px solid %5;"
                           "}")
                .arg(palette.border.name(QColor::HexArgb))
                .arg(palette.text.name(QColor::HexArgb))
                .arg(selectionBg.name(QColor::HexArgb))
                .arg(selectionFg.name(QColor::HexArgb))
                .arg(palette.border.lighter(120).name(QColor::HexArgb)));
    } else {
        if (m_displayGlowEffect != nullptr) {
            m_displayGlowEffect->setEnabled(false);
        }

        QColor styleToolbarTop = toolbarTop;
        QColor styleToolbarBottom = toolbarBottom;
        QColor styleToolbarHover = toolbarHover;
        QColor styleToolbarChecked = toolbarChecked;
        QColor styleToolbarBorder = palette.border;
        QColor styleEditorBackground = QColor(255, 255, 255, 0);
        QColor styleEditorBorder = QColor(255, 255, 255, 0);
        QColor styleSearchBackground = QColor(255, 255, 255, 20);
        int toolbarRadius = 12;
        int buttonRadius = 8;
        int lineEditRadius = 8;
        int editorRadius = 10;
        int editorPadding = 5;
        QString editorBorderStyle = QStringLiteral("none");
        QString toolbarBorderStyle = QStringLiteral("solid");
        QString styleFontFamily = QStringLiteral("'Segoe UI', 'Microsoft YaHei UI', sans-serif");

        switch (m_uiStyle) {
        case UiStyle::Graphite:
            styleToolbarTop = blendColor(toolbarTop, QColor(216, 226, 242, 170), 0.42);
            styleToolbarBottom = blendColor(toolbarBottom, QColor(38, 48, 68, 152), 0.36);
            styleToolbarHover = blendColor(toolbarHover, QColor(128, 154, 188, 128), 0.45);
            styleToolbarChecked = blendColor(toolbarChecked, QColor(134, 164, 204, 156), 0.5);
            styleToolbarBorder = blendColor(palette.border, QColor(158, 176, 204, 190), 0.48);
            styleEditorBackground = QColor(10, 16, 24, 92);
            styleEditorBorder = QColor(168, 186, 212, 88);
            styleSearchBackground = QColor(16, 24, 36, 132);
            toolbarRadius = 8;
            buttonRadius = 5;
            lineEditRadius = 6;
            editorRadius = 6;
            editorPadding = 6;
            editorBorderStyle = QStringLiteral("1px solid %1").arg(styleEditorBorder.name(QColor::HexArgb));
            styleFontFamily = QStringLiteral("'Segoe UI', 'Bahnschrift', 'Microsoft YaHei UI', sans-serif");
            break;
        case UiStyle::Paper:
            styleToolbarTop = blendColor(toolbarTop, QColor(255, 248, 230, 226), 0.54);
            styleToolbarBottom = blendColor(toolbarBottom, QColor(210, 184, 146, 138), 0.44);
            styleToolbarHover = blendColor(toolbarHover, QColor(220, 184, 146, 126), 0.52);
            styleToolbarChecked = blendColor(toolbarChecked, QColor(204, 162, 122, 138), 0.55);
            styleToolbarBorder = blendColor(palette.border, QColor(160, 124, 90, 170), 0.55);
            styleEditorBackground = QColor(255, 250, 238, 118);
            styleEditorBorder = QColor(150, 116, 82, 110);
            styleSearchBackground = QColor(255, 248, 232, 168);
            toolbarRadius = 7;
            buttonRadius = 3;
            lineEditRadius = 3;
            editorRadius = 4;
            editorPadding = 6;
            editorBorderStyle = QStringLiteral("1px dotted %1").arg(styleEditorBorder.name(QColor::HexArgb));
            toolbarBorderStyle = QStringLiteral("dashed");
            styleFontFamily = QStringLiteral("'Georgia', 'Times New Roman', 'Microsoft YaHei UI', serif");
            break;
        case UiStyle::Neon:
            styleToolbarTop = blendColor(toolbarTop, QColor(198, 88, 255, 120), 0.46);
            styleToolbarBottom = blendColor(toolbarBottom, QColor(22, 198, 232, 98), 0.4);
            styleToolbarHover = blendColor(toolbarHover, QColor(144, 255, 250, 148), 0.6);
            styleToolbarChecked = blendColor(toolbarChecked, QColor(224, 138, 255, 162), 0.62);
            styleToolbarBorder = blendColor(palette.border, QColor(152, 248, 242, 170), 0.5);
            styleEditorBackground = QColor(18, 10, 30, 132);
            styleEditorBorder = QColor(136, 234, 255, 116);
            styleSearchBackground = QColor(32, 20, 54, 168);
            toolbarRadius = 13;
            buttonRadius = 10;
            lineEditRadius = 10;
            editorRadius = 12;
            editorPadding = 6;
            editorBorderStyle = QStringLiteral("1px solid %1").arg(styleEditorBorder.name(QColor::HexArgb));
            styleFontFamily = QStringLiteral("'Bahnschrift', 'Segoe UI', 'Microsoft YaHei UI', sans-serif");
            break;
        case UiStyle::Clay:
            styleToolbarTop = blendColor(toolbarTop, QColor(255, 234, 208, 198), 0.52);
            styleToolbarBottom = blendColor(toolbarBottom, QColor(190, 142, 112, 138), 0.44);
            styleToolbarHover = blendColor(toolbarHover, QColor(230, 174, 132, 120), 0.5);
            styleToolbarChecked = blendColor(toolbarChecked, QColor(216, 156, 116, 140), 0.52);
            styleToolbarBorder = blendColor(palette.border, QColor(168, 122, 90, 178), 0.54);
            styleEditorBackground = QColor(255, 242, 224, 96);
            styleEditorBorder = QColor(176, 124, 86, 102);
            styleSearchBackground = QColor(252, 236, 212, 148);
            toolbarRadius = 16;
            buttonRadius = 11;
            lineEditRadius = 11;
            editorRadius = 14;
            editorPadding = 7;
            editorBorderStyle = QStringLiteral("1px solid %1").arg(styleEditorBorder.name(QColor::HexArgb));
            styleFontFamily = QStringLiteral("'Trebuchet MS', 'Segoe UI', 'Microsoft YaHei UI', sans-serif");
            break;
        case UiStyle::Meadow:
            styleToolbarTop = blendColor(toolbarTop, QColor(214, 242, 220, 170), 0.42);
            styleToolbarBottom = blendColor(toolbarBottom, QColor(58, 108, 74, 148), 0.38);
            styleToolbarHover = blendColor(toolbarHover, QColor(132, 198, 144, 128), 0.52);
            styleToolbarChecked = blendColor(toolbarChecked, QColor(144, 208, 152, 150), 0.56);
            styleToolbarBorder = blendColor(palette.border, QColor(130, 188, 142, 182), 0.48);
            styleEditorBackground = QColor(16, 36, 22, 82);
            styleEditorBorder = QColor(154, 212, 162, 92);
            styleSearchBackground = QColor(22, 44, 30, 136);
            toolbarRadius = 14;
            buttonRadius = 9;
            lineEditRadius = 10;
            editorRadius = 12;
            editorPadding = 6;
            editorBorderStyle = QStringLiteral("1px solid %1").arg(styleEditorBorder.name(QColor::HexArgb));
            styleFontFamily = QStringLiteral("'Calibri', 'Segoe UI', 'Microsoft YaHei UI', sans-serif");
            break;
        case UiStyle::Glass:
        default:
            styleToolbarTop = blendColor(toolbarTop, QColor(228, 242, 255, 176), 0.4);
            styleToolbarBottom = blendColor(toolbarBottom, QColor(48, 78, 112, 124), 0.34);
            styleToolbarHover = blendColor(toolbarHover, QColor(186, 214, 242, 120), 0.45);
            styleToolbarChecked = blendColor(toolbarChecked, QColor(194, 226, 248, 144), 0.5);
            styleToolbarBorder = blendColor(palette.border, QColor(182, 210, 236, 168), 0.45);
            styleEditorBackground = QColor(14, 24, 36, 52);
            styleEditorBorder = QColor(166, 196, 228, 84);
            styleSearchBackground = QColor(18, 30, 46, 118);
            toolbarRadius = 14;
            buttonRadius = 9;
            lineEditRadius = 9;
            editorRadius = 11;
            editorPadding = 6;
            editorBorderStyle = QStringLiteral("1px solid %1").arg(styleEditorBorder.name(QColor::HexArgb));
            styleFontFamily = QStringLiteral("'Corbel', 'Segoe UI', 'Microsoft YaHei UI', sans-serif");
            break;
        }

        const QColor selectionTextColor = (styleToolbarChecked.lightness() < 136)
                                              ? QColor(255, 255, 255, 245)
                                              : QColor(56, 36, 24, 238);
        m_displayLabel->setStyleSheet(isEmpty
                                          ? QStringLiteral("QLabel { color: %1; background: transparent; }")
                                                .arg(palette.placeholder.name(QColor::HexArgb))
                                          : QStringLiteral("QLabel { color: %1; background: transparent; }")
                                                .arg(palette.text.name(QColor::HexArgb)));

        m_editor->setCursorWidth(1);
        m_editor->setOverwriteMode(false);
        m_editor->setStyleSheet(
            QStringLiteral("QTextEdit {"
                           "color: %1;"
                           "background: %2;"
                           "border: %3;"
                           "border-radius: %4px;"
                           "padding: %5px;"
                           "selection-background-color: %6;"
                           "selection-color: %7;"
                           "font-family: %8;"
                           "}")
                .arg(palette.text.name(QColor::HexArgb))
                .arg(styleEditorBackground.name(QColor::HexArgb))
                .arg(editorBorderStyle)
                .arg(editorRadius)
                .arg(editorPadding)
                .arg(styleToolbarChecked.name(QColor::HexArgb))
                .arg(selectionTextColor.name(QColor::HexArgb))
                .arg(styleFontFamily));

        m_formatBar->setStyleSheet(
            QStringLiteral("QWidget#formatBar {"
                           "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                           "stop:0 %1, stop:1 %2);"
                           "border: 1px %10 %3;"
                           "border-radius: %11px;"
                           "}"
                           "QToolButton {"
                           "color: %4;"
                           "background: transparent;"
                           "border: 1px solid transparent;"
                           "border-radius: %12px;"
                           "padding: 2px 7px;"
                           "font-family: %13;"
                           "}"
                           "QToolButton:hover {"
                           "background: %5;"
                           "border: 1px solid %6;"
                           "}"
                           "QToolButton:checked {"
                           "background: %7;"
                           "border: 1px solid %8;"
                           "}"
                           "QToolButton:pressed {"
                           "background: %7;"
                           "}"
                           "QLineEdit {"
                           "color: %4;"
                           "background: %14;"
                           "border: 1px solid %3;"
                           "border-radius: %15px;"
                           "padding: 2px 8px;"
                           "selection-background-color: %7;"
                           "selection-color: %9;"
                           "font-family: %13;"
                           "}"
                           "QLineEdit:focus {"
                           "border: 1px solid %8;"
                           "}")
                .arg(styleToolbarTop.name(QColor::HexArgb))
                .arg(styleToolbarBottom.name(QColor::HexArgb))
                .arg(styleToolbarBorder.name(QColor::HexArgb))
                .arg(palette.text.name(QColor::HexArgb))
                .arg(styleToolbarHover.name(QColor::HexArgb))
                .arg(styleToolbarBorder.name(QColor::HexArgb))
                .arg(styleToolbarChecked.name(QColor::HexArgb))
                .arg(styleToolbarBorder.lighter(112).name(QColor::HexArgb))
                .arg(selectionTextColor.name(QColor::HexArgb))
                .arg(toolbarBorderStyle)
                .arg(toolbarRadius)
                .arg(buttonRadius)
                .arg(styleFontFamily)
                .arg(styleSearchBackground.name(QColor::HexArgb))
                .arg(lineEditRadius));
    }

    updateFormattingToolbarState();
}

void NoteCardWidget::finishEditing() {
    if (!m_editor->isVisible()) {
        return;
    }

    setSearchVisible(false);
    m_editor->hide();
    m_formatBar->hide();
    m_displayLabel->show();
    setMaximumHeight(baseCardHeight());
    setMinimumHeight(baseCardHeight());
    setFixedHeight(baseCardHeight());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateDisplayText();
    updateGeometry();
    emit textCommitted(m_noteId, text());
    emit layoutRefreshRequested();
}

void NoteCardWidget::syncEditorHeight() {
    const int editorBaseHeight = qMax(1, baseCardHeight() -
                                             static_cast<int>((constants::kCardPaddingVertical * m_uiScale) * 2.0));
    const int editorHeight = qMax(editorBaseHeight,
                                  static_cast<int>(m_editor->document()->size().height() + (8.0 * m_uiScale)));
    m_editor->setFixedHeight(editorHeight);

    if (m_editor->isVisible()) {
        const auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
        const int verticalMargins = layout != nullptr ? layout->contentsMargins().top() + layout->contentsMargins().bottom() : 0;
        const int spacing = layout != nullptr ? layout->spacing() : 0;
        const int toolbarHeight = m_formatBar != nullptr && m_formatBar->isVisible()
                                      ? m_formatBar->sizeHint().height() + spacing
                                      : 0;
        const int totalHeight = editorHeight + verticalMargins + toolbarHeight;
        setMinimumHeight(totalHeight);
        resize(width(), totalHeight);
        updateGeometry();
    }
}

void NoteCardWidget::mergeEditorCharFormat(const QTextCharFormat &format) {
    QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        cursor.select(QTextCursor::WordUnderCursor);
    }
    cursor.mergeCharFormat(format);
    m_editor->mergeCurrentCharFormat(format);
    updateFormattingToolbarState();
}

QTextCharFormat NoteCardWidget::defaultEditorCharFormat() const {
    QTextCharFormat format;
    QFont baseFont = (m_editor != nullptr) ? m_editor->font() : QApplication::font();
    baseFont.setWeight(QFont::Normal);
    baseFont.setBold(false);
    baseFont.setItalic(false);
    baseFont.setUnderline(false);
    baseFont.setStrikeOut(false);
    format.setFont(baseFont, QTextCharFormat::FontPropertiesAll);
    format.setFontLetterSpacingType(QFont::PercentageSpacing);
    format.setFontLetterSpacing(100.0);
    format.clearForeground();
    format.clearBackground();
    format.setVerticalAlignment(QTextCharFormat::AlignNormal);
    return format;
}

void NoteCardWidget::applyEditorFontFamily(const QString &family) {
    QString selectedFamily = family.trimmed();
    if (selectedFamily.isEmpty()) {
        selectedFamily = m_editor->font().family();
    }

    QTextCharFormat format;
    format.setFontFamilies(QStringList{selectedFamily});
    mergeEditorCharFormat(format);
}

void NoteCardWidget::applyEditorFontPointSize(qreal pointSize) {
    const qreal basePointSize = m_editor->font().pointSizeF() > 0.0 ? m_editor->font().pointSizeF() : 11.0;
    const qreal selectedPointSize = pointSize > 0.0 ? pointSize : basePointSize;

    QTextCharFormat format;
    format.setFontPointSize(selectedPointSize);
    mergeEditorCharFormat(format);
}

void NoteCardWidget::applyEditorTextColor(const QColor &color) {
    if (!color.isValid()) {
        return;
    }

    QTextCharFormat format;
    format.setForeground(color);
    mergeEditorCharFormat(format);
}

void NoteCardWidget::toggleListStyle(QTextListFormat::Style style) {
    QTextCursor cursor = m_editor->textCursor();
    cursor.beginEditBlock();

    QTextList *currentList = cursor.currentList();
    if (currentList != nullptr && currentList->format().style() == style) {
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setObjectIndex(-1);
        blockFormat.setIndent(0);
        cursor.setBlockFormat(blockFormat);
    } else {
        QTextListFormat listFormat;
        if (currentList != nullptr) {
            listFormat = currentList->format();
        } else {
            listFormat.setIndent(qMax(1, cursor.blockFormat().indent() + 1));
        }
        listFormat.setStyle(style);
        cursor.createList(listFormat);
    }

    cursor.endEditBlock();
    m_editor->setTextCursor(cursor);
    updateFormattingToolbarState();
}

void NoteCardWidget::toggleChecklistItems() {
    QTextCursor cursor = m_editor->textCursor();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();

    QTextBlock startBlock = m_editor->document()->findBlock(start);
    QTextBlock endBlock = m_editor->document()->findBlock(qMax(start, end - 1));

    bool allChecklist = true;
    for (QTextBlock block = startBlock; block.isValid(); block = block.next()) {
        if (checklistStateForText(block.text()) == ChecklistState::None) {
            allChecklist = false;
            break;
        }
        if (block == endBlock) {
            break;
        }
    }

    cursor.beginEditBlock();
    for (QTextBlock block = startBlock; block.isValid(); block = block.next()) {
        QTextCursor blockCursor(block);
        blockCursor.movePosition(QTextCursor::StartOfBlock);
        blockCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        const QString blockText = block.text();
        const ChecklistState state = checklistStateForText(blockText);

        QString updatedText = blockText;
        if (allChecklist) {
            updatedText = checklistTextWithoutPrefix(blockText);
        } else if (state == ChecklistState::None) {
            updatedText = kChecklistUncheckedPrefix + blockText;
        }

        if (updatedText != blockText) {
            blockCursor.insertText(updatedText);
        }

        if (block == endBlock) {
            break;
        }
    }
    cursor.endEditBlock();
    updateFormattingToolbarState();
}

void NoteCardWidget::clearEditorFormatting() {
    QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        cursor.select(QTextCursor::Document);
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    QTextBlock startBlock = m_editor->document()->findBlock(selectionStart);
    QTextBlock endBlock = m_editor->document()->findBlock(qMax(selectionStart, selectionEnd - 1));

    cursor.beginEditBlock();

    const QTextCharFormat clearFormat = defaultEditorCharFormat();
    cursor.mergeCharFormat(clearFormat);
    m_editor->mergeCurrentCharFormat(clearFormat);
    m_editor->setCurrentCharFormat(clearFormat);

    for (QTextBlock block = startBlock; block.isValid(); block = block.next()) {
        QTextCursor blockCursor(block);
        QTextBlockFormat blockFormat = blockCursor.blockFormat();
        blockFormat.setObjectIndex(-1);
        blockFormat.setIndent(0);
        blockFormat.setAlignment(Qt::AlignLeft);
        blockCursor.setBlockFormat(blockFormat);
        if (block == endBlock) {
            break;
        }
    }

    cursor.endEditBlock();
    m_editor->setTextCursor(cursor);
    updateFormattingToolbarState();
}

void NoteCardWidget::setParagraphAlignment(Qt::Alignment alignment) {
    QTextCursor cursor = m_editor->textCursor();
    QTextBlockFormat blockFormat;
    blockFormat.setAlignment(alignment);
    cursor.mergeBlockFormat(blockFormat);
    m_editor->setTextCursor(cursor);
    updateFormattingToolbarState();
}

void NoteCardWidget::setSearchVisible(bool visible) {
    if (m_searchToggleButton != nullptr) {
        const QSignalBlocker blocker(m_searchToggleButton);
        m_searchToggleButton->setChecked(visible);
    }

    if (m_searchEdit == nullptr || m_searchPrevButton == nullptr || m_searchNextButton == nullptr) {
        return;
    }

    m_searchEdit->setVisible(visible);
    m_searchPrevButton->setVisible(visible);
    m_searchNextButton->setVisible(visible);

    if (visible) {
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
    }

    updateSearchHighlights();
    updateGeometry();
    emit layoutRefreshRequested();
}

void NoteCardWidget::findInEditor(bool backward) {
    if (m_searchEdit == nullptr) {
        return;
    }

    const QString keyword = m_searchEdit->text();
    if (keyword.isEmpty()) {
        updateSearchHighlights();
        return;
    }

    QTextDocument::FindFlags flags;
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }

    if (!m_editor->find(keyword, flags)) {
        QTextCursor wrapCursor = m_editor->textCursor();
        wrapCursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        m_editor->setTextCursor(wrapCursor);
        m_editor->find(keyword, flags);
    }

    updateSearchHighlights();
}

void NoteCardWidget::updateSearchHighlights() {
    if (m_searchEdit == nullptr || !m_searchEdit->isVisible()) {
        m_editor->setExtraSelections({});
        return;
    }

    const QString keyword = m_searchEdit->text();
    if (keyword.isEmpty()) {
        m_editor->setExtraSelections({});
        return;
    }

    QList<QTextEdit::ExtraSelection> selections;
    QTextDocument *document = m_editor->document();
    QTextCursor findCursor(document);

    QTextCharFormat matchedFormat;
    matchedFormat.setBackground(QColor(255, 236, 133, 168));
    matchedFormat.setForeground(QColor(20, 20, 20, 240));

    while (true) {
        findCursor = document->find(keyword, findCursor, QTextDocument::FindFlags());
        if (findCursor.isNull()) {
            break;
        }
        QTextEdit::ExtraSelection selection;
        selection.cursor = findCursor;
        selection.format = matchedFormat;
        selections.append(selection);
    }

    QTextCursor currentCursor = m_editor->textCursor();
    if (currentCursor.hasSelection() && currentCursor.selectedText().compare(keyword, Qt::CaseInsensitive) == 0) {
        QTextEdit::ExtraSelection currentSelection;
        currentSelection.cursor = currentCursor;
        QTextCharFormat currentFormat = matchedFormat;
        currentFormat.setBackground(QColor(255, 180, 90, 190));
        currentSelection.format = currentFormat;
        selections.append(currentSelection);
    }

    m_editor->setExtraSelections(selections);
}

bool NoteCardWidget::handleEditorShortcut(QKeyEvent *event) {
    if (event == nullptr) {
        return false;
    }

    if (event->matches(QKeySequence::Bold)) {
        m_boldButton->toggle();
        return true;
    }

    if (event->matches(QKeySequence::Italic)) {
        m_italicButton->toggle();
        return true;
    }

    if (event->matches(QKeySequence::Underline)) {
        m_underlineButton->toggle();
        return true;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (event->key() == Qt::Key_X
        && (modifiers & Qt::ControlModifier) != 0
        && (modifiers & Qt::ShiftModifier) != 0) {
        m_strikeButton->toggle();
        return true;
    }

    if (event->matches(QKeySequence::Find)) {
        setSearchVisible(true);
        return true;
    }

    if (event->key() == Qt::Key_F3) {
        findInEditor((modifiers & Qt::ShiftModifier) != 0);
        return true;
    }

    if (event->key() == Qt::Key_Escape && m_searchEdit != nullptr && m_searchEdit->isVisible()) {
        setSearchVisible(false);
        m_editor->setFocus(Qt::OtherFocusReason);
        return true;
    }

    return false;
}

bool NoteCardWidget::handleListAndChecklistEnter() {
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        return false;
    }

    QTextBlock block = cursor.block();
    const QString blockText = block.text();
    const ChecklistState checklistState = checklistStateForText(blockText);
    if (checklistState != ChecklistState::None) {
        const QString content = checklistTextWithoutPrefix(blockText);
        cursor.beginEditBlock();
        if (content.trimmed().isEmpty()) {
            QTextCursor blockCursor(block);
            blockCursor.movePosition(QTextCursor::StartOfBlock);
            blockCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            blockCursor.insertText(QString());

            cursor.setPosition(block.position());
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertBlock();
        } else {
            cursor.insertBlock();
            cursor.insertText(kChecklistUncheckedPrefix);
        }
        cursor.endEditBlock();
        m_editor->setTextCursor(cursor);
        updateFormattingToolbarState();
        return true;
    }

    QTextList *currentList = cursor.currentList();
    if (currentList != nullptr && blockText.trimmed().isEmpty()) {
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setObjectIndex(-1);
        blockFormat.setIndent(0);
        cursor.setBlockFormat(blockFormat);
        m_editor->setTextCursor(cursor);
        updateFormattingToolbarState();
        return true;
    }

    return false;
}

bool NoteCardWidget::handleSoftLineBreakInListContext() {
    QTextCursor cursor = m_editor->textCursor();
    const bool inChecklist = checklistStateForText(cursor.block().text()) != ChecklistState::None;
    const bool inRichTextList = cursor.currentList() != nullptr;
    if (!inChecklist && !inRichTextList) {
        return false;
    }

    cursor.insertText(QString(QChar::LineSeparator));
    m_editor->setTextCursor(cursor);
    updateFormattingToolbarState();
    return true;
}

bool NoteCardWidget::handleListAndChecklistBackspace() {
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        return false;
    }

    const QTextBlock block = cursor.block();
    const QString blockText = block.text();
    const ChecklistState checklistState = checklistStateForText(blockText);
    const int positionInBlock = cursor.positionInBlock();

    if (checklistState != ChecklistState::None) {
        const int prefixLength = checklistPrefixLength(checklistState);
        if (positionInBlock <= prefixLength) {
            QTextCursor prefixCursor(block);
            prefixCursor.movePosition(QTextCursor::StartOfBlock);
            prefixCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, prefixLength);
            prefixCursor.removeSelectedText();
            cursor = prefixCursor;
            cursor.movePosition(QTextCursor::StartOfBlock);
            m_editor->setTextCursor(cursor);
            updateFormattingToolbarState();
            return true;
        }
    }

    if (cursor.currentList() != nullptr && positionInBlock == 0) {
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setObjectIndex(-1);
        blockFormat.setIndent(0);
        cursor.setBlockFormat(blockFormat);
        m_editor->setTextCursor(cursor);
        updateFormattingToolbarState();
        return true;
    }

    return false;
}

void NoteCardWidget::updateFormattingToolbarState() {
    const bool editable = m_editor->isVisible();
    const QToolButton *allButtons[] = {
        m_boldButton,
        m_italicButton,
        m_underlineButton,
        m_strikeButton,
        m_fontFamilyButton,
        m_fontSizeButton,
        m_textColorButton,
        m_bulletListButton,
        m_numberedListButton,
        m_checkListButton,
        m_clearFormatButton,
        m_alignButton,
        m_searchToggleButton,
        m_searchPrevButton,
        m_searchNextButton,
    };
    for (const QToolButton *buttonConst : allButtons) {
        auto *button = const_cast<QToolButton *>(buttonConst);
        if (button != nullptr) {
            button->setEnabled(editable);
        }
    }

    QTextCharFormat currentFormat = m_editor->currentCharFormat();
    QTextCursor cursor = m_editor->textCursor();
    QTextList *currentList = cursor.currentList();
    const QTextListFormat::Style listStyle = currentList != nullptr
                                                 ? currentList->format().style()
                                                 : QTextListFormat::ListStyleUndefined;
    const ChecklistState checklistState = checklistStateForText(cursor.block().text());
    Qt::Alignment blockAlignment = cursor.blockFormat().alignment();
    if ((blockAlignment & Qt::AlignHorizontal_Mask) == 0) {
        blockAlignment = Qt::AlignLeft;
    }

    {
        const QSignalBlocker blocker(m_boldButton);
        m_boldButton->setChecked(currentFormat.fontWeight() >= QFont::Bold);
    }
    {
        const QSignalBlocker blocker(m_italicButton);
        m_italicButton->setChecked(currentFormat.fontItalic());
    }
    {
        const QSignalBlocker blocker(m_underlineButton);
        m_underlineButton->setChecked(currentFormat.fontUnderline());
    }
    {
        const QSignalBlocker blocker(m_strikeButton);
        m_strikeButton->setChecked(currentFormat.fontStrikeOut());
    }
    {
        const QSignalBlocker blocker(m_bulletListButton);
        m_bulletListButton->setChecked(isBulletListStyle(listStyle));
    }
    {
        const QSignalBlocker blocker(m_numberedListButton);
        m_numberedListButton->setChecked(isNumberedListStyle(listStyle));
    }
    {
        const QSignalBlocker blocker(m_checkListButton);
        m_checkListButton->setChecked(checklistState != ChecklistState::None);
    }

    const QString editorDefaultFamily = m_editor->font().family();
    const QStringList currentFamilies = currentFormat.fontFamilies().toStringList();
    QString selectedFamily = currentFamilies.isEmpty()
                                 ? QString()
                                 : currentFamilies.value(0).trimmed();
    if (selectedFamily.isEmpty()) {
        selectedFamily = editorDefaultFamily;
    }
    if (m_fontFamilyMenu != nullptr) {
        bool explicitFamilyMatched = false;
        const QList<QAction *> familyActions = m_fontFamilyMenu->actions();
        for (QAction *action : familyActions) {
            if (action == nullptr) {
                continue;
            }
            const QString actionFamily = action->data().toString().trimmed();
            const bool isDefaultAction = actionFamily.isEmpty();
            const bool shouldCheck = !isDefaultAction
                                     && selectedFamily.compare(actionFamily, Qt::CaseInsensitive) == 0;
            {
                const QSignalBlocker blocker(action);
                action->setChecked(shouldCheck);
            }
            if (shouldCheck) {
                explicitFamilyMatched = true;
            }
        }
        if (!explicitFamilyMatched) {
            for (QAction *action : familyActions) {
                if (action == nullptr || !action->data().toString().trimmed().isEmpty()) {
                    continue;
                }
                const QSignalBlocker blocker(action);
                action->setChecked(true);
                break;
            }
        }
    }

    const qreal editorDefaultPointSize = m_editor->font().pointSizeF() > 0.0
                                             ? m_editor->font().pointSizeF()
                                             : 11.0;
    const qreal selectedPointSize = currentFormat.fontPointSize() > 0.0
                                        ? currentFormat.fontPointSize()
                                        : editorDefaultPointSize;
    if (m_fontSizeMenu != nullptr) {
        QAction *closestAction = nullptr;
        qreal closestDiff = std::numeric_limits<qreal>::max();
        const QList<QAction *> sizeActions = m_fontSizeMenu->actions();
        for (QAction *action : sizeActions) {
            if (action == nullptr) {
                continue;
            }
            const qreal actionSize = action->data().toDouble();
            const qreal diff = qAbs(actionSize - selectedPointSize);
            const bool shouldCheck = diff < 0.25;
            {
                const QSignalBlocker blocker(action);
                action->setChecked(shouldCheck);
            }
            if (closestAction == nullptr || diff < closestDiff) {
                closestAction = action;
                closestDiff = diff;
            }
        }
        if (closestAction != nullptr && closestDiff >= 0.25) {
            const QSignalBlocker blocker(closestAction);
            closestAction->setChecked(true);
        }
    }

    if (m_fontFamilyButton != nullptr) {
        const bool useDefaultFamily = selectedFamily.compare(editorDefaultFamily, Qt::CaseInsensitive) == 0;
        m_fontFamilyButton->setText(useDefaultFamily ? QStringLiteral("字") : selectedFamily.left(1));
        m_fontFamilyButton->setToolTip(useDefaultFamily
                                           ? QStringLiteral("字体（默认）")
                                           : QStringLiteral("字体：%1").arg(selectedFamily));
    }

    if (m_fontSizeButton != nullptr) {
        m_fontSizeButton->setText(QStringLiteral("%1").arg(qRound(selectedPointSize)));
        m_fontSizeButton->setToolTip(QStringLiteral("字号：%1 pt").arg(qRound(selectedPointSize)));
    }

    QColor currentColor = currentFormat.foreground().color();
    if (!currentColor.isValid()) {
        currentColor = ThemeHelper::paletteFor(m_uiStyle, m_hue, false).text;
    }
    if (m_textColorButton != nullptr) {
        m_textColorButton->setToolTip(QStringLiteral("文字颜色：%1").arg(currentColor.name(QColor::HexRgb)));
    }

    if (m_alignLeftAction != nullptr) {
        const QSignalBlocker blocker(m_alignLeftAction);
        m_alignLeftAction->setChecked((blockAlignment & Qt::AlignHorizontal_Mask) == Qt::AlignLeft);
    }
    if (m_alignCenterAction != nullptr) {
        const QSignalBlocker blocker(m_alignCenterAction);
        m_alignCenterAction->setChecked((blockAlignment & Qt::AlignHorizontal_Mask) == Qt::AlignHCenter);
    }
    if (m_alignRightAction != nullptr) {
        const QSignalBlocker blocker(m_alignRightAction);
        m_alignRightAction->setChecked((blockAlignment & Qt::AlignHorizontal_Mask) == Qt::AlignRight);
    }
    if (m_alignJustifyAction != nullptr) {
        const QSignalBlocker blocker(m_alignJustifyAction);
        m_alignJustifyAction->setChecked((blockAlignment & Qt::AlignHorizontal_Mask) == Qt::AlignJustify);
    }

    if (m_alignButton != nullptr) {
        QString alignmentLabel = QStringLiteral("左");
        if ((blockAlignment & Qt::AlignHorizontal_Mask) == Qt::AlignHCenter) {
            alignmentLabel = QStringLiteral("中");
        } else if ((blockAlignment & Qt::AlignHorizontal_Mask) == Qt::AlignRight) {
            alignmentLabel = QStringLiteral("右");
        } else if ((blockAlignment & Qt::AlignHorizontal_Mask) == Qt::AlignJustify) {
            alignmentLabel = QStringLiteral("齐");
        }
        m_alignButton->setText(alignmentLabel);
    }

    const bool hasKeyword = m_searchEdit != nullptr && !m_searchEdit->text().isEmpty();
    if (m_searchPrevButton != nullptr) {
        m_searchPrevButton->setEnabled(editable && hasKeyword);
    }
    if (m_searchNextButton != nullptr) {
        m_searchNextButton->setEnabled(editable && hasKeyword);
    }
}

void NoteCardWidget::applyScale() {
    const qreal spacingScale = spacingScaleForUiScale(m_uiScale);
    const qreal typographyScale = typographyScaleForUiScale(m_uiScale);

    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    if (layout != nullptr) {
        layout->setContentsMargins(static_cast<int>(constants::kCardPaddingHorizontal * m_uiScale * spacingScale),
                                   static_cast<int>(constants::kCardPaddingVertical * m_uiScale * spacingScale),
                                   static_cast<int>(constants::kCardPaddingHorizontal * m_uiScale * spacingScale),
                                   static_cast<int>(constants::kCardPaddingVertical * m_uiScale * spacingScale));
        layout->setSpacing(static_cast<int>(6.0 * m_uiScale * spacingScale));
    }

    auto *formatLayout = m_formatBar != nullptr ? qobject_cast<QHBoxLayout *>(m_formatBar->layout()) : nullptr;
    if (formatLayout != nullptr) {
        const int horizontalMargin = static_cast<int>(8.0 * m_uiScale * spacingScale);
        const int verticalMargin = static_cast<int>(6.0 * m_uiScale * spacingScale);
        formatLayout->setContentsMargins(horizontalMargin,
                                         verticalMargin,
                                         horizontalMargin,
                                         verticalMargin);
        formatLayout->setSpacing(static_cast<int>(6.0 * m_uiScale * spacingScale));
    }

    setMinimumHeight(baseCardHeight());
    if (!m_editor->isVisible()) {
        setMaximumHeight(baseCardHeight());
        setFixedHeight(baseCardHeight());
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QFont displayFont = QApplication::font();
    displayFont.setPointSizeF(11.0 * m_uiScale * typographyScale);
    displayFont.setWeight(QFont::Normal);
    displayFont.setLetterSpacing(QFont::PercentageSpacing, 100.0);
    switch (m_uiStyle) {
    case UiStyle::Pixel:
        displayFont.setFamily(QStringLiteral("Consolas"));
        displayFont.setStyleHint(QFont::TypeWriter);
        displayFont.setFixedPitch(true);
        break;
    case UiStyle::Paper:
        displayFont.setFamily(QStringLiteral("Georgia"));
        displayFont.setStyleHint(QFont::Serif);
        displayFont.setFixedPitch(false);
        displayFont.setWeight(QFont::Medium);
        break;
    case UiStyle::Clay:
        displayFont.setFamily(QStringLiteral("Trebuchet MS"));
        displayFont.setStyleHint(QFont::SansSerif);
        displayFont.setFixedPitch(false);
        displayFont.setWeight(QFont::Medium);
        break;
    case UiStyle::Meadow:
        displayFont.setFamily(QStringLiteral("Calibri"));
        displayFont.setStyleHint(QFont::SansSerif);
        displayFont.setFixedPitch(false);
        break;
    case UiStyle::Graphite:
        displayFont.setFamily(QStringLiteral("Segoe UI"));
        displayFont.setStyleHint(QFont::SansSerif);
        displayFont.setFixedPitch(false);
        displayFont.setWeight(QFont::DemiBold);
        displayFont.setLetterSpacing(QFont::PercentageSpacing, 101.0);
        break;
    case UiStyle::Neon:
        displayFont.setFamily(QStringLiteral("Bahnschrift"));
        displayFont.setStyleHint(QFont::SansSerif);
        displayFont.setFixedPitch(false);
        displayFont.setWeight(QFont::DemiBold);
        displayFont.setLetterSpacing(QFont::PercentageSpacing, 104.0);
        break;
    case UiStyle::Glass:
    default:
        displayFont.setFamily(QStringLiteral("Corbel"));
        displayFont.setStyleHint(QFont::SansSerif);
        displayFont.setFixedPitch(false);
        break;
    }
    m_displayLabel->setFont(displayFont);
    m_editor->setFont(displayFont);

    int buttonWidth = qMax(24, static_cast<int>(32.0 * m_uiScale * spacingScale));
    int buttonHeight = qMax(20, static_cast<int>(26.0 * m_uiScale * spacingScale));
    switch (m_uiStyle) {
    case UiStyle::Pixel:
    case UiStyle::Paper:
        buttonWidth += 2;
        break;
    case UiStyle::Neon:
        buttonWidth += 1;
        buttonHeight += 1;
        break;
    case UiStyle::Clay:
        buttonWidth += 2;
        buttonHeight += 1;
        break;
    case UiStyle::Graphite:
        buttonWidth += 1;
        break;
    case UiStyle::Meadow:
    case UiStyle::Glass:
    default:
        break;
    }
    const QToolButton *allButtons[] = {
        m_boldButton,
        m_italicButton,
        m_underlineButton,
        m_strikeButton,
        m_fontFamilyButton,
        m_fontSizeButton,
        m_textColorButton,
        m_bulletListButton,
        m_numberedListButton,
        m_checkListButton,
        m_clearFormatButton,
        m_alignButton,
        m_searchToggleButton,
        m_searchPrevButton,
        m_searchNextButton,
    };
    for (const QToolButton *buttonConst : allButtons) {
        auto *button = const_cast<QToolButton *>(buttonConst);
        if (button == nullptr) {
            continue;
        }
        button->setFixedSize(buttonWidth, buttonHeight);
    }

    QFont boldFont = displayFont;
    boldFont.setPointSizeF(10.0 * m_uiScale * typographyScale);
    boldFont.setBold(true);
    m_boldButton->setFont(boldFont);

    QFont italicFont = displayFont;
    italicFont.setPointSizeF(10.0 * m_uiScale * typographyScale);
    italicFont.setItalic(true);
    m_italicButton->setFont(italicFont);

    QFont underlineFont = displayFont;
    underlineFont.setPointSizeF(10.0 * m_uiScale * typographyScale);
    underlineFont.setUnderline(true);
    m_underlineButton->setFont(underlineFont);

    QFont strikeFont = displayFont;
    strikeFont.setPointSizeF(10.0 * m_uiScale * typographyScale);
    strikeFont.setStrikeOut(true);
    m_strikeButton->setFont(strikeFont);

    QFont listFont = displayFont;
    listFont.setPointSizeF(10.0 * m_uiScale * typographyScale);
    if (m_fontFamilyButton != nullptr) {
        m_fontFamilyButton->setFont(listFont);
    }
    if (m_fontSizeButton != nullptr) {
        m_fontSizeButton->setFont(listFont);
    }
    if (m_textColorButton != nullptr) {
        m_textColorButton->setFont(listFont);
    }
    m_bulletListButton->setFont(listFont);
    m_numberedListButton->setFont(listFont);
    if (m_checkListButton != nullptr) {
        m_checkListButton->setFont(listFont);
    }
    if (m_clearFormatButton != nullptr) {
        m_clearFormatButton->setFont(listFont);
    }
    if (m_alignButton != nullptr) {
        m_alignButton->setFont(listFont);
    }
    if (m_searchToggleButton != nullptr) {
        m_searchToggleButton->setFont(listFont);
    }
    if (m_searchPrevButton != nullptr) {
        m_searchPrevButton->setFont(listFont);
    }
    if (m_searchNextButton != nullptr) {
        m_searchNextButton->setFont(listFont);
    }

    if (m_searchEdit != nullptr) {
        QFont searchFont = displayFont;
        searchFont.setPointSizeF(9.5 * m_uiScale * typographyScale);
        m_searchEdit->setFont(searchFont);
        m_searchEdit->setFixedHeight(buttonHeight);
        m_searchEdit->setFixedWidth(qMax(96, static_cast<int>(160.0 * m_uiScale * spacingScale)));
    }
}

int NoteCardWidget::baseCardHeight() const {
    return baseHeightForScale(m_uiScale);
}

QString NoteCardWidget::elidedDisplayText(const QString &text) const {
    const QString display = displayTextFor(text);
    const auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    const int horizontalMargins = layout != nullptr ? layout->contentsMargins().left() + layout->contentsMargins().right() : 0;
    const int verticalMargins = layout != nullptr ? layout->contentsMargins().top() + layout->contentsMargins().bottom() : 0;
    const int availableWidth = qMax(40, width() - horizontalMargins);
    const int availableHeight = qMax(20, height() - verticalMargins);
    const QFontMetrics metrics(m_displayLabel->font());
    const int lineHeight = qMax(1, metrics.lineSpacing());
    const int maxLines = qMax(1, availableHeight / lineHeight);

    const QStringList inputLines = display.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    if (inputLines.isEmpty()) {
        return display;
    }

    QStringList visibleLines;
    visibleLines.reserve(maxLines);

    const int visibleCount = qMin(maxLines, inputLines.size());
    for (int index = 0; index < visibleCount; ++index) {
        visibleLines.append(metrics.elidedText(inputLines.at(index), Qt::ElideRight, availableWidth));
    }

    if (visibleLines.isEmpty()) {
        visibleLines.append(metrics.elidedText(display, Qt::ElideRight, availableWidth));
    }

    if (inputLines.size() > maxLines) {
        const QString overflowLine = visibleLines.constLast() + QStringLiteral(" …");
        visibleLines.last() = metrics.elidedText(overflowLine, Qt::ElideRight, availableWidth);
    }

    return visibleLines.join(QLatin1Char('\n'));
}

void NoteCardWidget::animateHoverTo(qreal target) {
    if (m_hoverAnimation != nullptr) {
        m_hoverAnimation->stop();
    }
    setHoverProgress(target);
    updateDisplayText();
}

void NoteCardWidget::animateDropHoverTo(qreal target) {
    if (m_dropHoverAnimation == nullptr) {
        setDropHoverProgress(target);
        return;
    }

    const qreal clampedTarget = qBound(0.0, target, 1.0);
    if (qFuzzyCompare(m_dropHoverProgress, clampedTarget)) {
        return;
    }

    m_dropHoverAnimation->stop();
    m_dropHoverAnimation->setStartValue(m_dropHoverProgress);
    m_dropHoverAnimation->setEndValue(clampedTarget);
    m_dropHoverAnimation->start();
}

}  // namespace glassnote
