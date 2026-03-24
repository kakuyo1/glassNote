#pragma once

#include <QString>
#include <QtGlobal>
#include <QPoint>
#include <QResizeEvent>
#include <QTextCharFormat>
#include <QTextListFormat>
#include <QVector>
#include <QWidget>

#include "model/NoteItem.h"
#include "model/UiStyle.h"

class QLabel;
class QColor;
class QKeyEvent;
class QLineEdit;
class QPropertyAnimation;
class QAction;
class QGraphicsDropShadowEffect;
class QMenu;
class QTextEdit;
class QToolButton;
class QTimer;

namespace glassnote {

class NoteCardWidget final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal dropHoverProgress READ dropHoverProgress WRITE setDropHoverProgress)

public:
    explicit NoteCardWidget(QWidget *parent = nullptr);

    QString noteId() const;
    QString text() const;
    QString plainText() const;
    int hue() const;
    QString sticker() const;
    NoteLane lane() const;
    UiStyle uiStyle() const;

    void setNoteId(const QString &noteId);
    void setText(const QString &text);
    void setHue(int hue);
    void setSticker(const QString &sticker);
    void setImportedStickerLibrary(const QVector<QString> &stickers);
    void setLane(NoteLane lane);
    void setUiScale(qreal scale);
    void setBaseLayerOpacity(qreal opacity);
    void setExternalFileSyncEnabled(bool enabled);
    void setAlwaysOnTopEnabled(bool enabled);
    void setLaunchAtStartupEnabled(bool enabled);
    void setAutoCheckUpdatesEnabled(bool enabled);
    void setWindowLocked(bool enabled);
    void setUiStyle(UiStyle uiStyle);
    void setNoteDragActive(bool active);
    void setDropHoverActive(bool active);
    void startEditing();
    qreal hoverProgress() const;
    void setHoverProgress(qreal progress);
    qreal dropHoverProgress() const;
    void setDropHoverProgress(qreal progress);

signals:
    void textCommitted(const QString &noteId, const QString &text);
    void layoutRefreshRequested();
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
    void launchAtStartupToggled(bool enabled);
    void autoCheckUpdatesToggled(bool enabled);
    void windowLockToggled(bool enabled);
    void checkForUpdatesRequested();
    void timelineReplayRequested();
    void openStorageDirectoryRequested();
    void quitRequested();
    void deleteRequested(const QString &noteId);
    void hueChangeRequested(const QString &noteId, int hue);
    void stickerChangeRequested(const QString &noteId, const QString &sticker);
    void laneChangeRequested(const QString &noteId, NoteLane lane);
    void uiStyleChangeRequested(UiStyle uiStyle);
    void dragHoldStarted(const QString &noteId, const QPoint &globalPos);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void animateHoverTo(qreal target);
    void applyScale();
    int baseCardHeight() const;
    QString elidedDisplayText(const QString &text) const;
    void updateDisplayText();
    void finishEditing();
    void syncEditorHeight();
    void mergeEditorCharFormat(const QTextCharFormat &format);
    QTextCharFormat defaultEditorCharFormat() const;
    void applyEditorFontFamily(const QString &family);
    void applyEditorFontPointSize(qreal pointSize);
    void applyEditorTextColor(const QColor &color);
    void toggleListStyle(QTextListFormat::Style style);
    void toggleChecklistItems();
    void clearEditorFormatting();
    void setParagraphAlignment(Qt::Alignment alignment);
    void setSearchVisible(bool visible);
    void findInEditor(bool backward);
    void updateSearchHighlights();
    bool handleEditorShortcut(QKeyEvent *event);
    bool handleListAndChecklistEnter();
    bool handleListAndChecklistBackspace();
    bool handleSoftLineBreakInListContext();
    void updateFormattingToolbarState();
    void animateDropHoverTo(qreal target);

    QString m_noteId;
    QLabel *m_displayLabel = nullptr;
    QGraphicsDropShadowEffect *m_displayGlowEffect = nullptr;
    QWidget *m_formatBar = nullptr;
    QTextEdit *m_editor = nullptr;
    QToolButton *m_boldButton = nullptr;
    QToolButton *m_italicButton = nullptr;
    QToolButton *m_underlineButton = nullptr;
    QToolButton *m_strikeButton = nullptr;
    QToolButton *m_fontFamilyButton = nullptr;
    QMenu *m_fontFamilyMenu = nullptr;
    QToolButton *m_fontSizeButton = nullptr;
    QMenu *m_fontSizeMenu = nullptr;
    QToolButton *m_textColorButton = nullptr;
    QToolButton *m_bulletListButton = nullptr;
    QToolButton *m_numberedListButton = nullptr;
    QToolButton *m_checkListButton = nullptr;
    QToolButton *m_clearFormatButton = nullptr;
    QToolButton *m_alignButton = nullptr;
    QMenu *m_alignMenu = nullptr;
    QAction *m_alignLeftAction = nullptr;
    QAction *m_alignCenterAction = nullptr;
    QAction *m_alignRightAction = nullptr;
    QAction *m_alignJustifyAction = nullptr;
    QToolButton *m_searchToggleButton = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QToolButton *m_searchPrevButton = nullptr;
    QToolButton *m_searchNextButton = nullptr;
    int m_hue = -1;
    QString m_sticker;
    QVector<QString> m_importedStickerLibrary;
    NoteLane m_lane = NoteLane::Today;
    qreal m_uiScale = 1.0;
    qreal m_baseLayerOpacity = 1.0;
    qreal m_hoverProgress = 0.0;
    qreal m_dropHoverProgress = 0.0;
    QPropertyAnimation *m_hoverAnimation = nullptr;
    QPropertyAnimation *m_dropHoverAnimation = nullptr;
    QTimer *m_dragHoldTimer = nullptr;
    bool m_draggingWindow = false;
    bool m_noteDragActive = false;
    bool m_dropHoverActive = false;
    bool m_pressActivated = false;
    bool m_externalFileSyncEnabled = true;
    bool m_alwaysOnTopEnabled = false;
    bool m_launchAtStartupEnabled = false;
    bool m_autoCheckUpdatesEnabled = true;
    bool m_windowLocked = false;
    UiStyle m_uiStyle = UiStyle::Glass;
    QPoint m_pressGlobalPos;
    QPoint m_dragOffset;
};

}  // namespace glassnote
