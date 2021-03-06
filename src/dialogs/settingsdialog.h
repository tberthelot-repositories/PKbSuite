#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <entities/notefolder.h>

#include "masterdialog.h"

namespace Ui {
class SettingsDialog;
}

class QAbstractButton;
class QListWidgetItem;
class QListWidget;
class QKeySequenceWidget;
class QTreeWidgetItem;
class QLineEdit;
class QStatusBar;
class QButtonGroup;
class QCheckBox;
class NoteFolder;
class QSplitter;

class SettingsDialog : public MasterDialog {
    Q_OBJECT

   public:
    enum OKLabelStatus {
        Unknown,
        Warning,
        OK,
        Failure,
    };

    enum SettingsPages {
        NoteFolderPage,
        InterfacePage,
        ShortcutPage,
        ScriptingPage,
        GeneralPage,
        EditorFontColorPage,
        PreviewFontPage,
        ToolbarPage,
        EditorPage,
        PanelsPage,
        LocalTrashPage,
        LayoutPage,
        WebCompanionPage
    };

    explicit SettingsDialog(int page = 0, QWidget *parent = 0);

    ~SettingsDialog();

    void setCurrentPage(int page);

    void readSettings();

   protected:
    void closeEvent(QCloseEvent *event);

   private slots:

    void on_buttonBox_clicked(QAbstractButton *button);

    void on_noteTextEditButton_clicked();

    void on_noteTextViewButton_clicked();

    void on_reinitializeDatabaseButton_clicked();

    void on_clearAppDataAndExitButton_clicked();

    void on_noteTextEditCodeButton_clicked();

    void on_noteTextEditResetButton_clicked();

    void on_noteTextEditCodeResetButton_clicked();

    void on_noteTextViewResetButton_clicked();

    void on_noteTextViewCodeButton_clicked();

    void on_noteTextViewCodeResetButton_clicked();

    void on_setExternalEditorPathToolButton_clicked();

    void on_noteFolderListWidget_currentItemChanged(QListWidgetItem *current,
                                                    QListWidgetItem *previous);

    void on_noteFolderAddButton_clicked();

    void on_noteFolderRemoveButton_clicked();

    void on_noteFolderNameLineEdit_editingFinished();

    void on_noteFolderLocalPathButton_clicked();

    void on_noteFolderActiveCheckBox_stateChanged(int arg1);

    void on_addCustomNoteFileExtensionButton_clicked();

    void on_removeCustomNoteFileExtensionButton_clicked();

    void on_defaultNoteFileExtensionListWidget_itemChanged(
        QListWidgetItem *item);

    void on_defaultNoteFileExtensionListWidget_currentRowChanged(
        int currentRow);

    void on_darkModeCheckBox_toggled();

    void on_noteFolderShowSubfoldersCheckBox_toggled(bool checked);

    void on_shortcutSearchLineEdit_textChanged(const QString &arg1);

    void on_settingsTreeWidget_currentItemChanged(QTreeWidgetItem *current,
                                                  QTreeWidgetItem *previous);

    void on_settingsStackedWidget_currentChanged(int index);

    void on_itemHeightResetButton_clicked();

    void on_toolbarIconSizeResetButton_clicked();

    void on_ignoreNonTodoCalendarsCheckBox_toggled(bool checked);

    void on_applyToolbarButton_clicked();

    void on_resetToolbarPushButton_clicked();

    void on_imageScaleDownCheckBox_toggled(bool checked);

    void on_searchLineEdit_textChanged(const QString &arg1);

    void on_clearLogFileButton_clicked();

    void noteNotificationButtonGroupPressed(QAbstractButton *button);

    void noteNotificationNoneCheckBoxCheck();

    void needRestart();

    void on_interfaceStyleComboBox_currentTextChanged(const QString &arg1);

    void on_cursorWidthResetButton_clicked();

    void on_showSystemTrayCheckBox_toggled(bool checked);

    void on_resetMessageBoxesButton_clicked();

    void on_markdownHighlightingCheckBox_toggled(bool checked);

    void on_localTrashEnabledCheckBox_toggled(bool checked);

    void on_localTrashClearCheckBox_toggled(bool checked);

    void keySequenceEvent(const QString &objectName);

    void on_ignoreNoteSubFoldersResetButton_clicked();

    void on_interfaceFontSizeSpinBox_valueChanged(int arg1);

    void on_overrideInterfaceFontSizeGroupBox_toggled(bool arg1);

    void on_webSocketServerServicePortResetButton_clicked();

    void on_enableSocketServerCheckBox_toggled();

    void on_internalIconThemeCheckBox_toggled(bool checked);

    void on_systemIconThemeCheckBox_toggled(bool checked);

    void on_webSocketTokenButton_clicked();

    void on_allowDifferentNoteFileNameCheckBox_toggled(bool checked);

    void on_languageSearchLineEdit_textChanged(const QString &arg1);

    void on_noteTextViewUseEditorStylesCheckBox_toggled(bool checked);

private:
    Ui::SettingsDialog *ui;
    QStatusBar *noteFolderRemotePathTreeStatusBar;
    QFont noteTextEditFont;
    QFont noteTextEditCodeFont;
    QFont noteTextViewFont;
    QFont noteTextViewCodeFont;
    bool appIsValid;
    QString appVersion;
    QString serverVersion;
    QString notesPathExistsText;
    QString connectionErrorMessage;
    NoteFolder _selectedNoteFolder;
    static const int _defaultMarkdownHighlightingInterval = 200;
    QSplitter *_mainSplitter;
    QButtonGroup *_noteNotificationButtonGroup;
    QCheckBox *_noteNotificationNoneCheckBox;
    QString _newScriptName;

    void storeSettings();

    void setFontLabel(QLineEdit *label, const QFont &font);

    static void selectListWidgetValue(QListWidget *listWidget,
                                      const QString &value);

    static bool listWidgetValueExists(QListWidget *listWidget,
                                      const QString &value);

    static QString getSelectedListWidgetValue(QListWidget *listWidget);

    void setupNoteFolderPage();

    void addPathToNoteFolderRemotePathTreeWidget(QTreeWidgetItem *parent,
                                                 const QString &path);

    QString generatePathFromCurrentNoteFolderRemotePathItem(
        QTreeWidgetItem *item);

    QListWidgetItem *addCustomNoteFileExtension(const QString &fileExtension);

    void loadShortcutSettings();

    void storeShortcutSettings();

    QTreeWidgetItem *findSettingsTreeWidgetItemByPage(int page);

    void initMainSplitter();

    void storeSplitterSettings();

    void storeFontSettings();

    int findSettingsPageIndexOfWidget(QWidget *widget);

    void addToSearchIndexList(QWidget *widget, QList<int> &pageIndexList);

    void removeLogFile() const;

    void reloadScriptList() const;

    void reloadCurrentScriptPage();

    void readPanelSettings();

    void storePanelSettings();

    void loadInterfaceStyleComboBox() const;

    void initSearchEngineComboBox() const;

    QKeySequenceWidget *findKeySequenceWidget(const QString &objectName);

    void handleDarkModeCheckBoxToggled(bool updateCheckBoxes = false,
                                       bool updateSchema = false);
    void resetOKLabelData();
};

#endif    // SETTINGSDIALOG_H
