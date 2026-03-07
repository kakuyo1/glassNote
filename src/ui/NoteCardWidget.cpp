#include "ui/NoteCardWidget.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QColorDialog>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QKeySequence>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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
#include <QVBoxLayout>
#include <QWindow>

#include "common/Constants.h"
#include "theme/ThemeHelper.h"
#include "ui/MainWindow.h"

namespace glassnote {

namespace {

const QString kChecklistUncheckedPrefix = QStringLiteral("☐ ");
const QString kChecklistCheckedPrefix = QStringLiteral("☑ ");

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

    QFont displayFont = m_displayLabel->font();
    displayFont.setPointSize(11);
    m_displayLabel->setFont(displayFont);

    m_editor = new QTextEdit(this);
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

NoteLane NoteCardWidget::lane() const {
    return m_lane;
}

qint64 NoteCardWidget::reminderEpochMsec() const {
    return m_reminderEpochMsec;
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

void NoteCardWidget::setWindowLocked(bool enabled) {
    m_windowLocked = enabled;
    if (enabled) {
        m_draggingWindow = false;
        m_pressActivated = false;
    }
}

void NoteCardWidget::setReminderEpochMsec(qint64 reminderEpochMsec) {
    m_reminderEpochMsec = qMax<qint64>(0, reminderEpochMsec);
}

void NoteCardWidget::setUiStyle(UiStyle uiStyle) {
    if (m_uiStyle == uiStyle) {
        return;
    }

    m_uiStyle = uiStyle;
    updateDisplayText();
    update();
}

void NoteCardWidget::setHoverProgress(qreal progress) {
    const qreal clamped = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_hoverProgress, clamped)) {
        return;
    }

    m_hoverProgress = clamped;
    update();
}

void NoteCardWidget::startEditing() {
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

    QMenu *themeMenu = menu.addMenu(QStringLiteral("修改配色"));
    ThemeHelper::polishMenu(themeMenu, m_uiStyle, m_hue);
    struct HueAction {
        const char *label;
        int hue;
    };
    const HueAction hueActions[] = {
        {"默认", -1},
        {"暖橙", 28},
        {"琥珀", 42},
        {"奶油黄", 52},
        {"柠绿", 95},
        {"薄荷", 140},
        {"湖青", 176},
        {"青蓝", 192},
        {"天蓝", 210},
        {"深海", 228},
        {"紫藤", 268},
        {"洋李", 290},
        {"玫红", 338},
        {"珊瑚", 8},
    };

    for (const HueAction &entry : hueActions) {
        QAction *action = themeMenu->addAction(QString::fromUtf8(entry.label));
        action->setCheckable(true);
        action->setChecked(m_hue == entry.hue);
        action->setData(entry.hue);
    }
    themeMenu->addSeparator();
    QAction *customThemeAction = themeMenu->addAction(QStringLiteral("自定义颜色..."));

    QMenu *uiStyleMenu = menu.addMenu(QStringLiteral("界面风格"));
    ThemeHelper::polishMenu(uiStyleMenu, m_uiStyle);
    struct UiStyleAction {
        const char *label;
        UiStyle style;
    };
    const UiStyleAction uiStyleActions[] = {
        {"玻璃", UiStyle::Glass},
        {"轻雾", UiStyle::Mist},
        {"晨曦", UiStyle::Sunrise},
        {"草甸", UiStyle::Meadow},
        {"石墨", UiStyle::Graphite},
        {"纸感", UiStyle::Paper},
        {"像素", UiStyle::Pixel},
        {"霓虹", UiStyle::Neon},
        {"陶土", UiStyle::Clay},
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
    const int minPercent = static_cast<int>(constants::kMinBaseLayerOpacity * 100.0);
    const int maxPercent = static_cast<int>(constants::kMaxBaseLayerOpacity * 100.0);
    const int stepPercent = static_cast<int>(constants::kBaseLayerOpacityStep * 100.0);
    for (int percent = minPercent; percent <= maxPercent; percent += stepPercent) {
        QAction *action = opacityMenu->addAction(QStringLiteral("%1%").arg(percent));
        action->setCheckable(true);
        action->setData(percent);
        const qreal optionOpacity = static_cast<qreal>(percent) / 100.0;
        action->setChecked(qAbs(optionOpacity - m_baseLayerOpacity) < (constants::kBaseLayerOpacityStep * 0.5));
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
    QAction *fileSyncAction = dataMenu->addAction(QStringLiteral("监听外部文件变更"));
    fileSyncAction->setCheckable(true);
    fileSyncAction->setChecked(m_externalFileSyncEnabled);

    menu.addSeparator();
    QMenu *efficiencyMenu = menu.addMenu(QStringLiteral("效率功能"));
    ThemeHelper::polishMenu(efficiencyMenu, m_uiStyle, m_hue);
    QAction *alwaysOnTopAction = efficiencyMenu->addAction(QStringLiteral("窗口置顶"));
    alwaysOnTopAction->setCheckable(true);
    alwaysOnTopAction->setChecked(m_alwaysOnTopEnabled);
    QAction *windowLockAction = efficiencyMenu->addAction(QStringLiteral("锁定窗口位置与尺寸"));
    windowLockAction->setCheckable(true);
    windowLockAction->setChecked(m_windowLocked);

    efficiencyMenu->addSeparator();
    QMenu *reminderMenu = efficiencyMenu->addMenu(QStringLiteral("事项提醒"));
    ThemeHelper::polishMenu(reminderMenu, m_uiStyle, m_hue);
    if (m_reminderEpochMsec > 0) {
        const QString reminderText = QDateTime::fromMSecsSinceEpoch(m_reminderEpochMsec)
                                         .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        QAction *currentReminderAction = reminderMenu->addAction(QStringLiteral("当前：%1").arg(reminderText));
        currentReminderAction->setEnabled(false);
    }
    QAction *setReminderAction = reminderMenu->addAction(m_reminderEpochMsec > 0
                                                              ? QStringLiteral("修改提醒时间...")
                                                              : QStringLiteral("设置提醒时间..."));
    QAction *clearReminderAction = reminderMenu->addAction(QStringLiteral("清除提醒"));
    clearReminderAction->setEnabled(m_reminderEpochMsec > 0);

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

    if (chosen->parent() == uiStyleMenu) {
        const UiStyle selectedStyle = static_cast<UiStyle>(chosen->data().toInt());
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

    if (chosen == alwaysOnTopAction) {
        m_alwaysOnTopEnabled = alwaysOnTopAction->isChecked();
        emit alwaysOnTopToggled(m_alwaysOnTopEnabled);
        return;
    }

    if (chosen == windowLockAction) {
        m_windowLocked = windowLockAction->isChecked();
        emit windowLockToggled(m_windowLocked);
        return;
    }

    if (chosen == setReminderAction) {
        emit reminderSetRequested(m_noteId);
        return;
    }

    if (chosen == clearReminderAction) {
        emit reminderClearedRequested(m_noteId);
        return;
    }

    if (chosen == openDirAction) {
        emit openStorageDirectoryRequested();
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
    const QRectF bodyRect = rect().adjusted(outlineInset, outlineInset, -outlineInset, -outlineInset);
    const qreal radius = (m_uiStyle == UiStyle::Pixel ? 6.0 : constants::kCardCornerRadius) * m_uiScale;
    const NotePalette palette = blendPalette(ThemeHelper::paletteFor(m_uiStyle, m_hue, false),
                                             ThemeHelper::paletteFor(m_uiStyle, m_hue, true),
                                             m_hoverProgress);

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
        sheen.setColorAt(0.0, QColor(255, 255, 255, 36));
        sheen.setColorAt(0.35, QColor(255, 255, 255, 12));
        sheen.setColorAt(1.0, QColor(255, 255, 255, 2));
        painter.fillPath(bodyPath, sheen);
        break;
    }
    case UiStyle::Mist: {
        QLinearGradient haze(bodyRect.topLeft(), bodyRect.bottomLeft());
        haze.setColorAt(0.0, QColor(242, 250, 255, 42));
        haze.setColorAt(0.5, QColor(230, 242, 252, 16));
        haze.setColorAt(1.0, QColor(220, 232, 246, 8));
        painter.fillPath(bodyPath, haze);
        break;
    }
    case UiStyle::Sunrise: {
        QRadialGradient warm(bodyRect.left() + (bodyRect.width() * 0.2),
                             bodyRect.top() + (bodyRect.height() * 0.16),
                             bodyRect.width() * 0.9);
        warm.setColorAt(0.0, QColor(255, 222, 164, 48));
        warm.setColorAt(0.45, QColor(255, 195, 128, 18));
        warm.setColorAt(1.0, QColor(255, 166, 104, 0));
        painter.fillPath(bodyPath, warm);
        break;
    }
    case UiStyle::Meadow: {
        QRadialGradient canopy(bodyRect.right() - (bodyRect.width() * 0.22),
                               bodyRect.top() + (bodyRect.height() * 0.22),
                               bodyRect.width() * 0.85);
        canopy.setColorAt(0.0, QColor(186, 230, 194, 38));
        canopy.setColorAt(0.55, QColor(146, 202, 164, 14));
        canopy.setColorAt(1.0, QColor(82, 130, 100, 0));
        painter.fillPath(bodyPath, canopy);
        break;
    }
    case UiStyle::Graphite: {
        QLinearGradient metallic(bodyRect.topLeft(), bodyRect.bottomLeft());
        metallic.setColorAt(0.0, QColor(255, 255, 255, 20));
        metallic.setColorAt(0.45, QColor(255, 255, 255, 6));
        metallic.setColorAt(1.0, QColor(0, 0, 0, 30));
        painter.fillPath(bodyPath, metallic);
        break;
    }
    case UiStyle::Paper: {
        painter.setPen(QPen(QColor(132, 106, 74, 36), 1.0));
        const int step = qMax(4, static_cast<int>(6.0 * m_uiScale));
        for (int y = static_cast<int>(bodyRect.top()) + step; y < static_cast<int>(bodyRect.bottom()); y += step) {
            painter.drawLine(static_cast<int>(bodyRect.left()) + 4,
                             y,
                             static_cast<int>(bodyRect.right()) - 4,
                             y);
        }
        break;
    }
    case UiStyle::Pixel: {
        painter.setPen(QPen(QColor(255, 255, 255, 34), 1.0));
        const int cell = qMax(4, static_cast<int>(5.0 * m_uiScale));
        for (int y = static_cast<int>(bodyRect.top()) + cell; y < static_cast<int>(bodyRect.bottom()); y += cell) {
            painter.drawLine(static_cast<int>(bodyRect.left()) + 2,
                             y,
                             static_cast<int>(bodyRect.right()) - 2,
                             y);
        }
        for (int x = static_cast<int>(bodyRect.left()) + cell; x < static_cast<int>(bodyRect.right()); x += cell) {
            painter.drawLine(x,
                             static_cast<int>(bodyRect.top()) + 2,
                             x,
                             static_cast<int>(bodyRect.bottom()) - 2);
        }
        break;
    }
    case UiStyle::Neon: {
        QRadialGradient bloom(bodyRect.center().x(), bodyRect.center().y(), bodyRect.width() * 0.9);
        bloom.setColorAt(0.0, QColor(214, 124, 255, 52));
        bloom.setColorAt(0.45, QColor(94, 226, 255, 24));
        bloom.setColorAt(1.0, QColor(14, 8, 28, 0));
        painter.fillPath(bodyPath, bloom);

        QLinearGradient neonEdge(bodyRect.topLeft(), bodyRect.bottomLeft());
        neonEdge.setColorAt(0.0, QColor(255, 176, 255, 24));
        neonEdge.setColorAt(1.0, QColor(0, 255, 232, 14));
        painter.fillPath(bodyPath, neonEdge);
        break;
    }
    case UiStyle::Clay: {
        QLinearGradient matte(bodyRect.topLeft(), bodyRect.bottomLeft());
        matte.setColorAt(0.0, QColor(255, 238, 218, 30));
        matte.setColorAt(0.4, QColor(228, 188, 150, 14));
        matte.setColorAt(1.0, QColor(112, 72, 50, 18));
        painter.fillPath(bodyPath, matte);
        break;
    }
    }

    painter.restore();
}

void NoteCardWidget::enterEvent(QEnterEvent *event) {
    animateHoverTo(1.0);
    QWidget::enterEvent(event);
}

void NoteCardWidget::leaveEvent(QEvent *event) {
    if (!m_draggingWindow) {
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
            m_draggingWindow = true;
            m_pressActivated = true;
            m_dragOffset = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
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

    auto *mainWindow = qobject_cast<MainWindow *>(window());
    if (mainWindow != nullptr && mainWindow->isResizingWindow()) {
        mainWindow->updateManualResize(event->globalPosition().toPoint());
        event->accept();
        return;
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

                if ((modifiers & Qt::ShiftModifier) == 0) {
                    if (handleListAndChecklistEnter()) {
                        return true;
                    }
                    if (m_editor->textCursor().currentList() == nullptr) {
                        finishEditing();
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

    m_displayLabel->setText(elidedDisplayText(currentText));
    m_displayLabel->setStyleSheet(isEmpty
                                      ? QStringLiteral("QLabel { color: %1; background: transparent; }")
                                            .arg(palette.placeholder.name(QColor::HexArgb))
                                      : QStringLiteral("QLabel { color: %1; background: transparent; }")
                                            .arg(palette.text.name(QColor::HexArgb)));

    m_editor->setStyleSheet(
        QStringLiteral("QTextEdit { color: %1; background: transparent; border: none;"
                       "selection-background-color: %2; selection-color: %3; }")
            .arg(palette.text.name(QColor::HexArgb))
            .arg(toolbarChecked.name(QColor::HexArgb))
            .arg(QColor(255, 255, 255, 245).name(QColor::HexArgb)));

    m_formatBar->setStyleSheet(
        QStringLiteral("QWidget#formatBar {"
                       "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                       "stop:0 %1, stop:1 %2);"
                       "border: 1px solid %3;"
                       "border-radius: 12px;"
                       "}"
                       "QToolButton {"
                       "color: %4;"
                       "background: transparent;"
                       "border: 1px solid transparent;"
                       "border-radius: 8px;"
                       "padding: 2px 6px;"
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
                       "background: rgba(255,255,255,0.08);"
                       "border: 1px solid %3;"
                       "border-radius: 8px;"
                       "padding: 2px 8px;"
                       "selection-background-color: %7;"
                       "selection-color: %9;"
                       "}"
                       "QLineEdit:focus {"
                       "border: 1px solid %8;"
                       "}")
            .arg(toolbarTop.name(QColor::HexArgb))
            .arg(toolbarBottom.name(QColor::HexArgb))
            .arg(palette.border.name(QColor::HexArgb))
            .arg(palette.text.name(QColor::HexArgb))
            .arg(toolbarHover.name(QColor::HexArgb))
            .arg(palette.border.name(QColor::HexArgb))
            .arg(toolbarChecked.name(QColor::HexArgb))
            .arg(palette.border.lighter(110).name(QColor::HexArgb))
            .arg(QColor(255, 255, 255, 245).name(QColor::HexArgb)));

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

    QTextCharFormat clearFormat;
    clearFormat.setFontWeight(QFont::Normal);
    clearFormat.setFontItalic(false);
    clearFormat.setFontUnderline(false);
    clearFormat.setFontStrikeOut(false);
    clearFormat.clearBackground();
    clearFormat.clearForeground();
    cursor.mergeCharFormat(clearFormat);
    m_editor->mergeCurrentCharFormat(clearFormat);

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

    QFont displayFont = m_displayLabel->font();
    displayFont.setPointSizeF(11.0 * m_uiScale * typographyScale);
    m_displayLabel->setFont(displayFont);
    m_editor->setFont(displayFont);

    const int buttonWidth = qMax(24, static_cast<int>(32.0 * m_uiScale * spacingScale));
    const int buttonHeight = qMax(20, static_cast<int>(26.0 * m_uiScale * spacingScale));
    const QToolButton *allButtons[] = {
        m_boldButton,
        m_italicButton,
        m_underlineButton,
        m_strikeButton,
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

}  // namespace glassnote
