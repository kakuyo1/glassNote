#pragma once

#include <QString>
#include <QtGlobal>
#include <QResizeEvent>
#include <QTextCharFormat>
#include <QTextListFormat>
#include <QWidget>

#include "model/UiStyle.h"

class QLabel;
class QKeyEvent;
class QLineEdit;
class QPropertyAnimation;
class QAction;
class QMenu;
class QTextEdit;
class QToolButton;

namespace glassnote {

class NoteCardWidget final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    explicit NoteCardWidget(QWidget *parent = nullptr);

    QString noteId() const;
    QString text() const;
    QString plainText() const;
    int hue() const;
    qint64 reminderEpochMsec() const;
    UiStyle uiStyle() const;

    void setNoteId(const QString &noteId);
    void setText(const QString &text);
    void setHue(int hue);
    void setUiScale(qreal scale);
    void setBaseLayerOpacity(qreal opacity);
    void setExternalFileSyncEnabled(bool enabled);
    void setAlwaysOnTopEnabled(bool enabled);
    void setWindowLocked(bool enabled);
    void setReminderEpochMsec(qint64 reminderEpochMsec);
    void setUiStyle(UiStyle uiStyle);
    void startEditing();
    qreal hoverProgress() const;
    void setHoverProgress(qreal progress);

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
    void windowLockToggled(bool enabled);
    void reminderSetRequested(const QString &noteId);
    void reminderClearedRequested(const QString &noteId);
    void openStorageDirectoryRequested();
    void quitRequested();
    void deleteRequested(const QString &noteId);
    void hueChangeRequested(const QString &noteId, int hue);
    void uiStyleChangeRequested(UiStyle uiStyle);

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
    void updateFormattingToolbarState();

    QString m_noteId;
    QLabel *m_displayLabel = nullptr;
    QWidget *m_formatBar = nullptr;
    QTextEdit *m_editor = nullptr;
    QToolButton *m_boldButton = nullptr;
    QToolButton *m_italicButton = nullptr;
    QToolButton *m_underlineButton = nullptr;
    QToolButton *m_strikeButton = nullptr;
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
    qreal m_uiScale = 1.0;
    qreal m_baseLayerOpacity = 1.0;
    qreal m_hoverProgress = 0.0;
    QPropertyAnimation *m_hoverAnimation = nullptr;
    bool m_draggingWindow = false;
    bool m_pressActivated = false;
    bool m_externalFileSyncEnabled = true;
    bool m_alwaysOnTopEnabled = false;
    bool m_windowLocked = false;
    qint64 m_reminderEpochMsec = 0;
    UiStyle m_uiStyle = UiStyle::Glass;
    QPoint m_dragOffset;
};

}  // namespace glassnote
