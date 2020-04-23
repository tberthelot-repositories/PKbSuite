#include "dialogs/settingsdialog.h"

#include <QtNetwork/qnetworkproxy.h>
#include <entities/notefolder.h>
#include <entities/notesubfolder.h>
#include <entities/script.h>
#include <helpers/toolbarcontainer.h>
#include <libraries/qkeysequencewidget/qkeysequencewidget/src/qkeysequencewidget.h>
#include <services/cryptoservice.h>
#include <services/scriptingservice.h>
#include <services/websocketserverservice.h>
#include <utils/gui.h>
#include <utils/misc.h>
#include <widgets/scriptsettingwidget.h>

#include <QAction>
#include <QButtonGroup>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFontDialog>
#include <QInputDialog>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTextBrowser>
#include <QToolBar>
#include <utility>

#include "build_number.h"
#include "dialogs/websockettokendialog.h"
#include "filedialog.h"
#include "mainwindow.h"
#include "release.h"
#include "scriptrepositorydialog.h"
#include "services/databaseservice.h"
#include "ui_settingsdialog.h"
#include "version.h"

SettingsDialog::SettingsDialog(int page, QWidget *parent)
    : MasterDialog(parent), ui(new Ui::SettingsDialog) {
    ui->setupUi(this);

    bool fromWelcomeDialog =
        parent->objectName() == QLatin1String("WelcomeDialog");

    MainWindow *mainWindow = MainWindow::instance();

    // if there was no size set yet and we already have a main window we'll
    // mimic that size
    if (mainWindow != Q_NULLPTR) {
        resize(mainWindow->width(), mainWindow->height());
    } else {
        // we must not use resize(1, 1) because XFCE really resizes the window
        // to 1x1
        resize(800, 600);
    }

    QList<QWidget *> pageWidgets;

    // get a list of every settings page in the correct order
    for (int index = 0; index < ui->settingsStackedWidget->count(); index++) {
        pageWidgets.append(ui->settingsStackedWidget->widget(index));
    }

    Q_FOREACH (QWidget *pageWidget, pageWidgets) {
        // make sure the margin of every page is 0
        QLayout *layout = pageWidget->layout();
        layout->setContentsMargins(0, 0, 0, 0);

        // inject a scroll area to make each page scrollable
        auto *scrollArea = new QScrollArea(ui->settingsStackedWidget);
        scrollArea->setWidget(pageWidget);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        ui->settingsStackedWidget->addWidget(scrollArea);
    }

    ui->darkModeInfoLabel->hide();
    ui->noteSaveIntervalTime->setToolTip(
        ui->noteSaveIntervalTimeLabel->toolTip());
    ui->removeCustomNoteFileExtensionButton->setDisabled(true);
    _newScriptName = tr("New script");

    ui->automaticNoteFolderDatabaseClosingCheckBox->hide();

    _noteNotificationButtonGroup = new QButtonGroup(this);
    _noteNotificationButtonGroup->addButton(
        ui->notifyAllExternalModificationsCheckBox);
    _noteNotificationButtonGroup->addButton(
        ui->ignoreAllExternalModificationsCheckBox);
    _noteNotificationButtonGroup->addButton(
        ui->acceptAllExternalModificationsCheckBox);

    // create a hidden checkbox so we can un-check above checkboxes
    _noteNotificationNoneCheckBox = new QCheckBox(this);
    _noteNotificationNoneCheckBox->setHidden(true);
    _noteNotificationButtonGroup->addButton(_noteNotificationNoneCheckBox);
    connect(_noteNotificationButtonGroup,
            SIGNAL(buttonPressed(QAbstractButton *)), this,
            SLOT(noteNotificationButtonGroupPressed(QAbstractButton *)));

    if (!fromWelcomeDialog) {
        // setup the note folder tab
        setupNoteFolderPage();

        // setup the scripting tab
        setupScriptingPage();
    }

    readSettings();

    // initializes the main splitter
    initMainSplitter();

    // set the current page
    // must be done in the end so that the settings are loaded first when
    // doing a connection test
    setCurrentPage(page);

#ifdef Q_OS_MAC
    // we don't need app instance settings on OS X
    ui->appInstanceGroupBox->setVisible(false);
    ui->allowOnlyOneAppInstanceCheckBox->setChecked(false);

    // Qt::TargetMoveAction seems to be broken on macOS, the item vanishes after
    // dropping Qt::CopyAction seens to be the only action that works
    ui->noteFolderListWidget->setDefaultDropAction(Qt::CopyAction);
    ui->scriptListWidget->setDefaultDropAction(Qt::CopyAction);
#endif

    // disable the shortcut page if there is no main window yet
    if (mainWindow == Q_NULLPTR) {
        QTreeWidgetItem *item = findSettingsTreeWidgetItemByPage(ShortcutPage);
        if (item != Q_NULLPTR) {
            item->setDisabled(true);
        }
    }

    // expand all items in the settings tree widget
    ui->settingsTreeWidget->expandAll();

    // init the toolbar editor
    ui->toolbarEditor->setTargetWindow(MainWindow::instance());
    ui->toolbarEditor->setCustomToolbarRemovalOnly(true);

    QStringList disabledToolbarNames(QStringList()
                                     << QStringLiteral("windowToolbar")
                                     << QStringLiteral("customActionsToolbar"));
    ui->toolbarEditor->setDisabledToolbarNames(disabledToolbarNames);

    QStringList disabledMenuNames(QStringList()
                                  << QStringLiteral("noteFoldersMenu"));
    ui->toolbarEditor->setDisabledMenuNames(disabledMenuNames);

    //    QStringList disabledMenuActionNames(QStringList() << "");
    //    ui->toolbarEditor->setDisabledMenuActionNames(disabledMenuActionNames);

    ui->toolbarEditor->updateBars();

    // declare that we need to restart the application if certain settings
    // are changed
    connect(ui->languageListWidget, SIGNAL(itemSelectionChanged()), this,
            SLOT(needRestart()));
    connect(ui->internalIconThemeCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->systemIconThemeCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->darkModeTrayIconCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->darkModeIconThemeCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->darkModeColorsCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->darkModeCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->allowOnlyOneAppInstanceCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->showSystemTrayCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->startHiddenCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->fullyHighlightedBlockquotesCheckBox, SIGNAL(toggled(bool)),
            this, SLOT(needRestart()));
    connect(ui->noteEditCentralWidgetCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->noteFolderButtonsCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->noteListPreviewCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->vimModeCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->disableCursorBlinkingCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    connect(ui->ignoreNoteSubFoldersLineEdit, SIGNAL(textChanged(QString)),
            this, SLOT(needRestart()));
    connect(ui->enableSocketServerCheckBox, SIGNAL(toggled(bool)), this,
            SLOT(needRestart()));
    //    connect(ui->layoutWidget, SIGNAL(settingsStored()),
    //            this, SLOT(needRestart()));

    // connect the panel sort radio buttons
    connect(ui->notesPanelSortAlphabeticalRadioButton, SIGNAL(toggled(bool)),
            ui->notesPanelOrderGroupBox, SLOT(setEnabled(bool)));
    connect(ui->noteSubfoldersPanelShowRootFolderNameCheckBox,
            SIGNAL(toggled(bool)), ui->noteSubfoldersPanelShowFullPathCheckBox,
            SLOT(setEnabled(bool)));
    connect(ui->noteSubfoldersPanelSortAlphabeticalRadioButton,
            SIGNAL(toggled(bool)), ui->noteSubfoldersPanelOrderGroupBox,
            SLOT(setEnabled(bool)));
    connect(ui->tagsPanelSortAlphabeticalRadioButton, SIGNAL(toggled(bool)),
            ui->tagsPanelOrderGroupBox, SLOT(setEnabled(bool)));

    // setup the search engine combo-box
    initSearchEngineComboBox();

#ifdef Q_OS_MAC
    // there is no system tray in OS X
    ui->systemTrayGroupBox->setTitle(tr("Menu bar"));
    ui->showSystemTrayCheckBox->setText(tr("Show menu bar item"));
#endif

    if (fromWelcomeDialog) {
        // hide the whole left side frame with the settings menu tree
        ui->leftSideFrame->setVisible(false);
    }

    if (!ui->noteListPreviewCheckBox->text().contains(
            QLatin1String("(experimental)"))) {
        ui->noteListPreviewCheckBox->setText(
            ui->noteListPreviewCheckBox->text() + " (experimental)");
    }

    if (!ui->enableNoteTreeCheckBox->text().contains(
            QLatin1String("work in progress"))) {
        ui->enableNoteTreeCheckBox->setText(ui->enableNoteTreeCheckBox->text() +
                                            " (work in progress)");
    }

    ui->webCompannionLabel->setText(ui->webCompannionLabel->text().arg(
        "https://github.com/pkbsuite/web-companion",
        "https://chrome.google.com/webstore/detail/pkbsuite-web-companion/"
        "pkgkfnampapjbopomdpnkckbjdnpkbkp",
        "https://addons.mozilla.org/firefox/addon/pkbsuite-web-companion"));
    ui->bookmarkTagLabel->setText(ui->bookmarkTagLabel->text().arg(
        "https://www.pkbsuite.org/Knowledge-base/"
        "PKbSuite-Web-Companion-browser-extension"));

#ifndef Q_OS_LINUX
    ui->systemIconThemeCheckBox->setHidden(true);
    ui->systemIconThemeCheckBox->setChecked(false);
#endif
}

/**
 * Check the _noteNotificationNoneCheckBox when the checkboxes should all be
 * unchecked
 *
 * @param button
 */
void SettingsDialog::noteNotificationButtonGroupPressed(
    QAbstractButton *button) {
    if (button->isChecked()) {
        QTimer::singleShot(100, this,
                           SLOT(noteNotificationNoneCheckBoxCheck()));
    }
}

/**
 * Check the _noteNotificationNoneCheckBox
 */
void SettingsDialog::noteNotificationNoneCheckBoxCheck() {
    _noteNotificationNoneCheckBox->setChecked(true);
}

/**
 * Sets the current page
 *
 * @param page
 */
void SettingsDialog::setCurrentPage(int page) {
    ui->settingsStackedWidget->setCurrentIndex(page);

    // update other stuff for the settings tree widget
    if (ui->settingsStackedWidget->currentIndex() == page) {
        on_settingsStackedWidget_currentChanged(page);
    }
}

SettingsDialog::~SettingsDialog() { delete ui; }

void SettingsDialog::storeSettings() {
    QSettings settings;

	settings.setValue(QStringLiteral("insertTimeFormat"),
                      ui->timeFormatLineEdit->text());
    settings.setValue(QStringLiteral("notifyAllExternalModifications"),
                      ui->notifyAllExternalModificationsCheckBox->isChecked());
    settings.setValue(QStringLiteral("ignoreAllExternalModifications"),
                      ui->ignoreAllExternalModificationsCheckBox->isChecked());
    settings.setValue(QStringLiteral("acceptAllExternalModifications"),
                      ui->acceptAllExternalModificationsCheckBox->isChecked());
    settings.setValue(
        QStringLiteral("ignoreAllExternalNoteFolderChanges"),
        ui->ignoreAllExternalNoteFolderChangesCheckBox->isChecked());
    settings.setValue(QStringLiteral("newNoteAskHeadline"),
                      ui->newNoteAskHeadlineCheckBox->isChecked());
    settings.setValue(QStringLiteral("useUNIXNewline"),
                      ui->useUNIXNewlineCheckBox->isChecked());
    settings.setValue(QStringLiteral("restoreCursorPosition"),
                      ui->restoreCursorPositionCheckBox->isChecked());
    settings.setValue(QStringLiteral("restoreLastNoteAtStartup"),
                      ui->restoreLastNoteAtStartupCheckBox->isChecked());
    settings.setValue(QStringLiteral("noteSaveIntervalTime"),
                      ui->noteSaveIntervalTime->value());
    settings.setValue(
        QStringLiteral("defaultNoteFileExtension"),
        getSelectedListWidgetValue(ui->defaultNoteFileExtensionListWidget));
    settings.setValue(QStringLiteral("localTrash/supportEnabled"),
                      ui->localTrashEnabledCheckBox->isChecked());
    settings.setValue(QStringLiteral("localTrash/autoCleanupEnabled"),
                      ui->localTrashClearCheckBox->isChecked());
    settings.setValue(QStringLiteral("localTrash/autoCleanupDays"),
                      ui->localTrashClearTimeSpinBox->value());
    settings.setValue(QStringLiteral("enableSocketServer"),
                      ui->enableSocketServerCheckBox->isChecked());

    // make the path relative to the portable data path if we are in
    // portable mode
    settings.setValue(QStringLiteral("externalEditorPath"),
                      ui->externalEditorPathLineEdit->text());

    settings.setValue(QStringLiteral("overrideInterfaceFontSize"),
                      ui->overrideInterfaceFontSizeGroupBox->isChecked());
    settings.setValue(QStringLiteral("interfaceFontSize"),
                      ui->interfaceFontSizeSpinBox->value());
    settings.setValue(QStringLiteral("itemHeight"),
                      ui->itemHeightSpinBox->value());
    settings.setValue(QStringLiteral("MainWindow/mainToolBar.iconSize"),
                      ui->toolbarIconSizeSpinBox->value());
    settings.setValue(QStringLiteral("allowOnlyOneAppInstance"),
                      ui->allowOnlyOneAppInstanceCheckBox->isChecked());
    settings.setValue(QStringLiteral("interfaceLanguage"),
                      getSelectedListWidgetValue(ui->languageListWidget));
    settings.setValue(QStringLiteral("markdownHighlightingEnabled"),
                      ui->markdownHighlightingCheckBox->isChecked());
    settings.setValue(QStringLiteral("fullyHighlightedBlockquotes"),
                      ui->fullyHighlightedBlockquotesCheckBox->isChecked());
    settings.setValue(QStringLiteral("noteEditIsCentralWidget"),
                      ui->noteEditCentralWidgetCheckBox->isChecked());
    settings.setValue(QStringLiteral("useNoteFolderButtons"),
                      ui->noteFolderButtonsCheckBox->isChecked());
    settings.setValue(QStringLiteral("MainWindow/noteTextView.rtl"),
                      ui->noteTextViewRTLCheckBox->isChecked());
    settings.setValue(
        QStringLiteral("MainWindow/noteTextView.ignoreCodeFontSize"),
        ui->noteTextViewIgnoreCodeFontSizeCheckBox->isChecked());
    settings.setValue(QStringLiteral("MainWindow/noteTextView.underline"),
                      ui->noteTextViewUnderlineCheckBox->isChecked());
    settings.setValue(QStringLiteral("MainWindow/noteTextView.useEditorStyles"),
                      ui->noteTextViewUseEditorStylesCheckBox->isChecked());
    settings.setValue(
        QStringLiteral("MainWindow/noteTextView.useInternalExportStyling"),
        ui->useInternalExportStylingCheckBox->isChecked());
    settings.setValue(QStringLiteral("Editor/autoBracketClosing"),
                      ui->autoBracketClosingCheckBox->isChecked());
    settings.setValue(QStringLiteral("Editor/autoBracketRemoval"),
                      ui->autoBracketRemovalCheckBox->isChecked());
    settings.setValue(QStringLiteral("Editor/highlightCurrentLine"),
                      ui->highlightCurrentLineCheckBox->isChecked());
    settings.setValue(QStringLiteral("Editor/editorWidthInDFMOnly"),
                      ui->editorWidthInDFMOnlyCheckBox->isChecked());
    settings.setValue(QStringLiteral("Editor/vimMode"),
                      ui->vimModeCheckBox->isChecked());
    settings.setValue(QStringLiteral("Editor/disableCursorBlinking"),
                      ui->disableCursorBlinkingCheckBox->isChecked());
    settings.setValue(QStringLiteral("Editor/useTabIndent"),
                      ui->useTabIndentCheckBox->isChecked());
    settings.setValue(QStringLiteral("Editor/indentSize"),
                      ui->indentSizeSpinBox->value());

    settings.setValue(QStringLiteral("darkModeColors"),
                      ui->darkModeColorsCheckBox->isChecked());

    settings.setValue(QStringLiteral("darkMode"),
                      ui->darkModeCheckBox->isChecked());

    settings.setValue(QStringLiteral("darkModeTrayIcon"),
                      ui->darkModeTrayIconCheckBox->isChecked());

    settings.setValue(QStringLiteral("darkModeIconTheme"),
                      ui->darkModeIconThemeCheckBox->isChecked());

    settings.setValue(QStringLiteral("internalIconTheme"),
                      ui->internalIconThemeCheckBox->isChecked());

    settings.setValue(QStringLiteral("systemIconTheme"),
                      ui->systemIconThemeCheckBox->isChecked());

    // store the custom note file extensions
    QStringList customNoteFileExtensionList;
    for (int i = 2; i < ui->defaultNoteFileExtensionListWidget->count(); i++) {
        QListWidgetItem *item = ui->defaultNoteFileExtensionListWidget->item(i);

        customNoteFileExtensionList.append(item->whatsThis());
    }
    customNoteFileExtensionList.removeDuplicates();
    settings.setValue(QStringLiteral("customNoteFileExtensionList"),
                      customNoteFileExtensionList);

    // store the font settings
    storeFontSettings();

    // store the shortcut settings
    storeShortcutSettings();

    // store the splitter settings
    storeSplitterSettings();

    // apply and store the toolbar configuration
    on_applyToolbarButton_clicked();

    // store the enabled state of the scripts
    storeScriptListEnabledState();

    // store image scaling settings
    settings.setValue(QStringLiteral("imageScaleDown"),
                      ui->imageScaleDownCheckBox->isChecked());
    settings.setValue(QStringLiteral("imageScaleDownMaximumHeight"),
                      ui->maximumImageHeightSpinBox->value());
    settings.setValue(QStringLiteral("imageScaleDownMaximumWidth"),
                      ui->maximumImageWidthSpinBox->value());

    // store Panels settings
    storePanelSettings();

    // store the interface style settings
    if (ui->interfaceStyleComboBox->currentIndex() > 0) {
        settings.setValue(QStringLiteral("interfaceStyle"),
                          ui->interfaceStyleComboBox->currentText());
    } else {
        settings.remove(QStringLiteral("interfaceStyle"));
    }

    // store the cursor width
    settings.setValue(QStringLiteral("cursorWidth"),
                      ui->cursorWidthSpinBox->value());

    settings.setValue(QStringLiteral("SearchEngineId"),
                      ui->searchEngineSelectionComboBox->currentData().toInt());

    settings.setValue(QStringLiteral("ShowSystemTray"),
                      ui->showSystemTrayCheckBox->isChecked());
    settings.setValue(QStringLiteral("StartHidden"),
                      ui->startHiddenCheckBox->isChecked());
    settings.setValue(
        QStringLiteral("automaticNoteFolderDatabaseClosing"),
        ui->automaticNoteFolderDatabaseClosingCheckBox->isChecked());
    settings.setValue(QStringLiteral("legacyLinking"),
                      ui->legacyLinkingCheckBox->isChecked());

    settings.setValue(QStringLiteral("webSocketServerService/port"),
                      ui->webSocketServerServicePortSpinBox->value());
    settings.setValue(QStringLiteral("webSocketServerService/bookmarksTag"),
                      ui->bookmarksTagLineEdit->text());
    settings.setValue(
        QStringLiteral("webSocketServerService/bookmarksNoteName"),
        ui->bookmarksNoteNameLineEdit->text());
}

/**
 * @brief Stores the Panel settings
 */
void SettingsDialog::storePanelSettings() {
    QSettings settings;
    // Notes Panel Options
    ui->notesPanelSortAlphabeticalRadioButton->isChecked()
        ? settings.setValue(QStringLiteral("notesPanelSort"), SORT_ALPHABETICAL)
        : settings.setValue(QStringLiteral("notesPanelSort"),
                            SORT_BY_LAST_CHANGE);
    ui->notesPanelOrderDescendingRadioButton->isChecked()
        ? settings.setValue(QStringLiteral("notesPanelOrder"), ORDER_DESCENDING)
        : settings.setValue(QStringLiteral("notesPanelOrder"), ORDER_ASCENDING);

    // Note Subfolders Panel Options
    settings.setValue(QStringLiteral("noteSubfoldersPanelHideSearch"),
                      ui->noteSubfoldersPanelHideSearchCheckBox->isChecked());

    settings.setValue(
        QStringLiteral("noteSubfoldersPanelDisplayAsFullTree"),
        ui->noteSubfoldersPanelDisplayAsFullTreeCheckBox->isChecked());

    settings.setValue(
        QStringLiteral("noteSubfoldersPanelShowRootFolderName"),
        ui->noteSubfoldersPanelShowRootFolderNameCheckBox->isChecked());

    settings.setValue(
        QStringLiteral("noteSubfoldersPanelShowNotesRecursively"),
        ui->noteSubfoldersPanelShowNotesRecursivelyCheckBox->isChecked());

    settings.setValue(
        QStringLiteral("disableSavedSearchesAutoCompletion"),
        ui->disableSavedSearchesAutoCompletionCheckBox->isChecked());

    settings.setValue(QStringLiteral("showMatches"),
                      ui->showMatchesCheckBox->isChecked());

    settings.setValue(QStringLiteral("noteSubfoldersPanelShowFullPath"),
                      ui->noteSubfoldersPanelShowFullPathCheckBox->isChecked());

    ui->noteSubfoldersPanelSortAlphabeticalRadioButton->isChecked()
        ? settings.setValue(QStringLiteral("noteSubfoldersPanelSort"),
                            SORT_ALPHABETICAL)
        : settings.setValue(QStringLiteral("noteSubfoldersPanelSort"),
                            SORT_BY_LAST_CHANGE);

    ui->noteSubfoldersPanelOrderDescendingRadioButton->isChecked()
        ? settings.setValue(QStringLiteral("noteSubfoldersPanelOrder"),
                            ORDER_DESCENDING)
        : settings.setValue(QStringLiteral("noteSubfoldersPanelOrder"),
                            ORDER_ASCENDING);

    const QSignalBlocker blocker(ui->ignoreNoteSubFoldersLineEdit);
    settings.setValue(QStringLiteral("ignoreNoteSubFolders"),
                      ui->ignoreNoteSubFoldersLineEdit->text());

    // Tags Panel Options
    settings.setValue(QStringLiteral("tagsPanelHideSearch"),
                      ui->tagsPanelHideSearchCheckBox->isChecked());

    settings.setValue(QStringLiteral("taggingShowNotesRecursively"),
                      ui->taggingShowNotesRecursivelyCheckBox->isChecked());
    settings.setValue(QStringLiteral("noteListPreview"),
                      ui->noteListPreviewCheckBox->isChecked());

    ui->tagsPanelSortAlphabeticalRadioButton->isChecked()
        ? settings.setValue(QStringLiteral("tagsPanelSort"), SORT_ALPHABETICAL)
        : settings.setValue(QStringLiteral("tagsPanelSort"),
                            SORT_BY_LAST_CHANGE);

    ui->tagsPanelOrderDescendingRadioButton->isChecked()
        ? settings.setValue(QStringLiteral("tagsPanelOrder"), ORDER_DESCENDING)
        : settings.setValue(QStringLiteral("tagsPanelOrder"), ORDER_ASCENDING);

    // Navigation Panel Options
    settings.setValue(QStringLiteral("navigationPanelHideSearch"),
                      ui->navigationPanelHideSearchCheckBox->isChecked());

    settings.setValue(QStringLiteral("enableNoteTree"),
                      ui->enableNoteTreeCheckBox->isChecked());
}

/**
 * Stores the font settings
 */
void SettingsDialog::storeFontSettings() {
    QSettings settings;
    settings.setValue(QStringLiteral("MainWindow/noteTextEdit.font"),
                      noteTextEditFont.toString());
    settings.setValue(QStringLiteral("MainWindow/noteTextEdit.code.font"),
                      noteTextEditCodeFont.toString());
    settings.setValue(QStringLiteral("MainWindow/noteTextView.font"),
                      noteTextViewFont.toString());
    settings.setValue(QStringLiteral("MainWindow/noteTextView.code.font"),
                      noteTextViewCodeFont.toString());
}

void SettingsDialog::readSettings() {
    QSettings settings;

    // set current note folder list item
    QListWidgetItem *noteFolderListItem = Utils::Gui::getListWidgetItemWithUserData(
                ui->noteFolderListWidget, NoteFolder::currentNoteFolderId());
    if (noteFolderListItem != nullptr) {
        ui->noteFolderListWidget->setCurrentItem(noteFolderListItem);
    }

    ui->externalEditorPathLineEdit->setText(QStringLiteral("externalEditorPath"));

    ui->notifyAllExternalModificationsCheckBox->setChecked(
        settings.value(QStringLiteral("notifyAllExternalModifications"))
            .toBool());
    ui->ignoreAllExternalModificationsCheckBox->setChecked(
        settings.value(QStringLiteral("ignoreAllExternalModifications"))
            .toBool());
    ui->acceptAllExternalModificationsCheckBox->setChecked(
        settings.value(QStringLiteral("acceptAllExternalModifications"))
            .toBool());
    ui->ignoreAllExternalNoteFolderChangesCheckBox->setChecked(
        settings.value(QStringLiteral("ignoreAllExternalNoteFolderChanges"))
            .toBool());
    ui->newNoteAskHeadlineCheckBox->setChecked(
        settings.value(QStringLiteral("newNoteAskHeadline")).toBool());
    ui->useUNIXNewlineCheckBox->setChecked(
        settings.value(QStringLiteral("useUNIXNewline")).toBool());
    ui->localTrashEnabledCheckBox->setChecked(
        settings.value(QStringLiteral("localTrash/supportEnabled"), true)
            .toBool());
    ui->localTrashClearCheckBox->setChecked(
        settings.value(QStringLiteral("localTrash/autoCleanupEnabled"), true)
            .toBool());
    ui->localTrashClearTimeSpinBox->setValue(
        settings.value(QStringLiteral("localTrash/autoCleanupDays"), 30)
            .toInt());
    ui->enableSocketServerCheckBox->setChecked(
        Utils::Misc::isSocketServerEnabled());
    on_enableSocketServerCheckBox_toggled();

#ifdef Q_OS_MAC
    bool restoreCursorPositionDefault = false;
#else
    bool restoreCursorPositionDefault = true;
#endif

    ui->restoreCursorPositionCheckBox->setChecked(
        settings
            .value(QStringLiteral("restoreCursorPosition"),
                   restoreCursorPositionDefault)
            .toBool());
    ui->restoreLastNoteAtStartupCheckBox->setChecked(
        settings.value(QStringLiteral("restoreLastNoteAtStartup"), true)
            .toBool());
    ui->noteSaveIntervalTime->setValue(
        settings.value(QStringLiteral("noteSaveIntervalTime"), 10).toInt());
    ui->noteTextViewRTLCheckBox->setChecked(
        settings.value(QStringLiteral("MainWindow/noteTextView.rtl")).toBool());
    ui->noteTextViewIgnoreCodeFontSizeCheckBox->setChecked(
        settings
            .value(QStringLiteral("MainWindow/noteTextView.ignoreCodeFontSize"),
                   true)
            .toBool());
    ui->noteTextViewUnderlineCheckBox->setChecked(
        settings
            .value(QStringLiteral("MainWindow/noteTextView.underline"), true)
            .toBool());
    ui->noteTextViewUseEditorStylesCheckBox->setChecked(
        settings
            .value(QStringLiteral("MainWindow/noteTextView.useEditorStyles"),
                   true)
            .toBool());
    ui->useInternalExportStylingCheckBox->setChecked(
        Utils::Misc::useInternalExportStylingForPreview());
    ui->autoBracketClosingCheckBox->setChecked(
        settings.value(QStringLiteral("Editor/autoBracketClosing"), true)
            .toBool());
    ui->autoBracketRemovalCheckBox->setChecked(
        settings.value(QStringLiteral("Editor/autoBracketRemoval"), true)
            .toBool());
    ui->highlightCurrentLineCheckBox->setChecked(
        settings.value(QStringLiteral("Editor/highlightCurrentLine"), true)
            .toBool());
    ui->editorWidthInDFMOnlyCheckBox->setChecked(
        settings.value(QStringLiteral("Editor/editorWidthInDFMOnly"), true)
            .toBool());
    ui->vimModeCheckBox->setChecked(
        settings.value(QStringLiteral("Editor/vimMode")).toBool());
    ui->disableCursorBlinkingCheckBox->setChecked(
        settings.value(QStringLiteral("Editor/disableCursorBlinking"))
            .toBool());
    ui->useTabIndentCheckBox->setChecked(
        settings.value(QStringLiteral("Editor/useTabIndent")).toBool());
    ui->indentSizeSpinBox->setValue(Utils::Misc::indentSize());
    ui->markdownHighlightingCheckBox->setChecked(
        settings.value(QStringLiteral("markdownHighlightingEnabled"), true)
            .toBool());
    ui->fullyHighlightedBlockquotesCheckBox->setChecked(
        settings.value(QStringLiteral("fullyHighlightedBlockquotes")).toBool());
    ui->noteEditCentralWidgetCheckBox->setChecked(
        settings.value(QStringLiteral("noteEditIsCentralWidget"), true)
            .toBool());
    ui->noteFolderButtonsCheckBox->setChecked(
        settings.value(QStringLiteral("useNoteFolderButtons")).toBool());
    ui->allowOnlyOneAppInstanceCheckBox->setChecked(
        settings.value(QStringLiteral("allowOnlyOneAppInstance")).toBool());
    ui->toolbarIconSizeSpinBox->setValue(
        settings.value(QStringLiteral("MainWindow/mainToolBar.iconSize"))
            .toInt());

    const QSignalBlocker overrideInterfaceFontSizeGroupBoxBlocker(
        ui->overrideInterfaceFontSizeGroupBox);
    Q_UNUSED(overrideInterfaceFontSizeGroupBoxBlocker)
    const QSignalBlocker interfaceFontSizeSpinBoxBlocker(
        ui->interfaceFontSizeSpinBox);
    Q_UNUSED(interfaceFontSizeSpinBoxBlocker)
    ui->overrideInterfaceFontSizeGroupBox->setChecked(
        settings.value(QStringLiteral("overrideInterfaceFontSize"), false)
            .toBool());
    ui->interfaceFontSizeSpinBox->setValue(
        settings.value(QStringLiteral("interfaceFontSize"), 11).toInt());

    QTreeWidget treeWidget(this);
    auto *treeWidgetItem = new QTreeWidgetItem();
    treeWidget.addTopLevelItem(treeWidgetItem);
    int height = treeWidget.visualItemRect(treeWidgetItem).height();

    ui->itemHeightSpinBox->setValue(
        settings.value(QStringLiteral("itemHeight"), height).toInt());

    selectListWidgetValue(
        ui->languageListWidget,
        settings.value(QStringLiteral("interfaceLanguage")).toString());

    ui->darkModeColorsCheckBox->setChecked(
        settings.value(QStringLiteral("darkModeColors")).toBool());

    const QSignalBlocker darkModeCheckBoxBlocker(ui->darkModeCheckBox);
    Q_UNUSED(darkModeCheckBoxBlocker)
    ui->darkModeCheckBox->setChecked(
        settings.value(QStringLiteral("darkMode")).toBool());

    ui->darkModeTrayIconCheckBox->setChecked(
        settings.value(QStringLiteral("darkModeTrayIcon")).toBool());

    ui->darkModeIconThemeCheckBox->setChecked(
        Utils::Misc::isDarkModeIconTheme());

    ui->internalIconThemeCheckBox->setChecked(
        settings.value(QStringLiteral("internalIconTheme")).toBool());

    ui->systemIconThemeCheckBox->setChecked(
        settings.value(QStringLiteral("systemIconTheme")).toBool());

    // toggle the dark mode colors check box with the dark mode checkbox
    handleDarkModeCheckBoxToggled();

    noteTextEditFont.fromString(
        settings.value(QStringLiteral("MainWindow/noteTextEdit.font"))
            .toString());
    setFontLabel(ui->noteTextEditFontLabel, noteTextEditFont);

    noteTextEditCodeFont.fromString(
        settings.value(QStringLiteral("MainWindow/noteTextEdit.code.font"))
            .toString());
    setFontLabel(ui->noteTextEditCodeFontLabel, noteTextEditCodeFont);

    // load note text view font
    QString fontString =
        settings.value(QStringLiteral("MainWindow/noteTextView.font"))
            .toString();

    // store the current font if there isn't any set yet
    if (fontString.isEmpty()) {
        auto *textEdit = new QTextEdit();
        fontString = textEdit->font().toString();
        settings.setValue(QStringLiteral("MainWindow/noteTextView.font"),
                          fontString);
        delete textEdit;
    }

    noteTextViewFont.fromString(fontString);
    setFontLabel(ui->noteTextViewFontLabel, noteTextViewFont);

    // load note text view code font
    fontString =
        settings.value(QStringLiteral("MainWindow/noteTextView.code.font"))
            .toString();

    // set a default note text view code font
    if (fontString.isEmpty()) {
        // reset the note text view code font
        on_noteTextViewCodeResetButton_clicked();

        fontString = noteTextViewCodeFont.toString();
        settings.setValue(QStringLiteral("MainWindow/noteTextView.code.font"),
                          fontString);
    } else {
        noteTextViewCodeFont.fromString(fontString);
    }

    setFontLabel(ui->noteTextViewCodeFontLabel, noteTextViewCodeFont);

    // loads the custom note file extensions
    QListIterator<QString> itr(Note::customNoteFileExtensionList());
    while (itr.hasNext()) {
        QString fileExtension = itr.next();
        addCustomNoteFileExtension(fileExtension);
    }

    selectListWidgetValue(ui->defaultNoteFileExtensionListWidget,
                          Note::defaultNoteFileExtension());

    // load the shortcut settings
    loadShortcutSettings();

    // load image scaling settings
    bool scaleImageDown =
        settings.value(QStringLiteral("imageScaleDown"), false).toBool();
    ui->maximumImageHeightSpinBox->setValue(
        settings.value(QStringLiteral("imageScaleDownMaximumHeight"), 1024)
            .toInt());
    ui->maximumImageWidthSpinBox->setValue(
        settings.value(QStringLiteral("imageScaleDownMaximumWidth"), 1024)
            .toInt());
    ui->imageScaleDownCheckBox->setChecked(scaleImageDown);
    ui->imageScalingFrame->setVisible(scaleImageDown);

    // read panel settings
    readPanelSettings();

    // load the settings for the interface style combo box
    loadInterfaceStyleComboBox();

    // set the cursor width spinbox value
    ui->cursorWidthSpinBox->setValue(
        settings.value(QStringLiteral("cursorWidth"), 1).toInt());

    const QSignalBlocker blocker8(this->ui->showSystemTrayCheckBox);
    Q_UNUSED(blocker8)
    bool showSystemTray =
        settings.value(QStringLiteral("ShowSystemTray")).toBool();
    ui->showSystemTrayCheckBox->setChecked(showSystemTray);
    ui->startHiddenCheckBox->setEnabled(showSystemTray);
    ui->startHiddenCheckBox->setChecked(
        settings.value(QStringLiteral("StartHidden")).toBool());
    if (!showSystemTray) {
        ui->startHiddenCheckBox->setChecked(false);
    }

    ui->automaticNoteFolderDatabaseClosingCheckBox->setChecked(
        Utils::Misc::doAutomaticNoteFolderDatabaseClosing());
    ui->legacyLinkingCheckBox->setChecked(
        settings.value(QStringLiteral("legacyLinking")).toBool());

    ui->webSocketServerServicePortSpinBox->setValue(
        WebSocketServerService::getSettingsPort());
    ui->bookmarksTagLineEdit->setText(
        WebSocketServerService::getBookmarksTag());
    ui->bookmarksNoteNameLineEdit->setText(
        WebSocketServerService::getBookmarksNoteName());
}

/**
 * Does the setup for the search engine combo-box
 */
void SettingsDialog::initSearchEngineComboBox() const {
    QSettings settings;

    // Iterates over the search engines and adds them
    // to the combobox
    QHash<int, Utils::Misc::SearchEngine> searchEngines =
        Utils::Misc::getSearchEnginesHashMap();

    ui->searchEngineSelectionComboBox->clear();

    Q_FOREACH (int id, Utils::Misc::getSearchEnginesIds()) {
        Utils::Misc::SearchEngine searchEngine = searchEngines[id];
        ui->searchEngineSelectionComboBox->addItem(searchEngine.name,
                                                   QString::number(id));
    }

    // Sets the current selected item to the search engine
    // selected previously
    // while also handling the case in which the saved key has
    // been removed from the hash table
    int savedEngineId = settings
                            .value(QStringLiteral("SearchEngineId"),
                                   Utils::Misc::getDefaultSearchEngineId())
                            .toInt();
    int savedEngineIndex = ui->searchEngineSelectionComboBox->findData(
        QVariant(savedEngineId).toString());
    savedEngineIndex = (savedEngineIndex == -1) ? 0 : savedEngineIndex;
    ui->searchEngineSelectionComboBox->setCurrentIndex(savedEngineIndex);
}

/**
 * Loads the settings for the interface style combo box
 */
void SettingsDialog::loadInterfaceStyleComboBox() const {
    const QSignalBlocker blocker(ui->interfaceStyleComboBox);
    Q_UNUSED(blocker)

    ui->interfaceStyleComboBox->clear();
    ui->interfaceStyleComboBox->addItem(tr("Automatic (needs restart)"));

    Q_FOREACH (QString style, QStyleFactory::keys()) {
        ui->interfaceStyleComboBox->addItem(style);
    }

    QSettings settings;
    QString interfaceStyle =
        settings.value(QStringLiteral("interfaceStyle")).toString();

    if (!interfaceStyle.isEmpty()) {
        ui->interfaceStyleComboBox->setCurrentText(interfaceStyle);
        QApplication::setStyle(interfaceStyle);
    } else {
        ui->interfaceStyleComboBox->setCurrentIndex(0);
    }
}

/**
 * @brief Read the Panel Settings
 */
void SettingsDialog::readPanelSettings() {
    QSettings settings;
    // Notes Panel Options
    if (settings.value(QStringLiteral("notesPanelSort"), SORT_BY_LAST_CHANGE)
            .toInt() == SORT_ALPHABETICAL) {
        ui->notesPanelSortAlphabeticalRadioButton->setChecked(true);
        ui->notesPanelOrderGroupBox->setEnabled(true);
    } else {
        ui->notesPanelSortByLastChangeRadioButton->setChecked(true);
        ui->notesPanelOrderGroupBox->setEnabled(false);
    }
    settings.value(QStringLiteral("notesPanelOrder")).toInt() ==
            ORDER_DESCENDING
        ? ui->notesPanelOrderDescendingRadioButton->setChecked(true)
        : ui->notesPanelOrderAscendingRadioButton->setChecked(true);

    // Note Subfoldes Panel Options
    ui->noteSubfoldersPanelHideSearchCheckBox->setChecked(
        settings.value(QStringLiteral("noteSubfoldersPanelHideSearch"))
            .toBool());

    ui->noteSubfoldersPanelDisplayAsFullTreeCheckBox->setChecked(
        settings
            .value(QStringLiteral("noteSubfoldersPanelDisplayAsFullTree"), true)
            .toBool());

    ui->noteSubfoldersPanelShowNotesRecursivelyCheckBox->setChecked(
        settings
            .value(QStringLiteral("noteSubfoldersPanelShowNotesRecursively"))
            .toBool());

    ui->disableSavedSearchesAutoCompletionCheckBox->setChecked(
        settings.value(QStringLiteral("disableSavedSearchesAutoCompletion"))
            .toBool());

    ui->showMatchesCheckBox->setChecked(
        settings.value(QStringLiteral("showMatches"), true).toBool());

    if (settings
            .value(QStringLiteral("noteSubfoldersPanelShowRootFolderName"),
                   true)
            .toBool()) {
        ui->noteSubfoldersPanelShowRootFolderNameCheckBox->setChecked(true);
        ui->noteSubfoldersPanelShowFullPathCheckBox->setEnabled(true);
    } else {
        ui->noteSubfoldersPanelShowRootFolderNameCheckBox->setChecked(false);
        ui->noteSubfoldersPanelShowFullPathCheckBox->setEnabled(false);
    }

    ui->noteSubfoldersPanelShowFullPathCheckBox->setChecked(
        settings.value(QStringLiteral("noteSubfoldersPanelShowFullPath"))
            .toBool());

    if (settings.value(QStringLiteral("noteSubfoldersPanelSort")).toInt() ==
        SORT_ALPHABETICAL) {
        ui->noteSubfoldersPanelSortAlphabeticalRadioButton->setChecked(true);
        ui->noteSubfoldersPanelOrderGroupBox->setEnabled(true);
    } else {
        ui->noteSubfoldersPanelSortByLastChangeRadioButton->setChecked(true);
        ui->noteSubfoldersPanelOrderGroupBox->setEnabled(false);
    }

    settings.value(QStringLiteral("noteSubfoldersPanelOrder")).toInt() ==
            ORDER_DESCENDING
        ? ui->noteSubfoldersPanelOrderDescendingRadioButton->setChecked(true)
        : ui->noteSubfoldersPanelOrderAscendingRadioButton->setChecked(true);

    // Tags Panel Options
    ui->tagsPanelHideSearchCheckBox->setChecked(
        settings.value(QStringLiteral("tagsPanelHideSearch")).toBool());

    ui->taggingShowNotesRecursivelyCheckBox->setChecked(
        settings.value(QStringLiteral("taggingShowNotesRecursively")).toBool());
    ui->noteListPreviewCheckBox->setChecked(Utils::Misc::isNoteListPreview());

    if (settings.value(QStringLiteral("tagsPanelSort")).toInt() ==
        SORT_ALPHABETICAL) {
        ui->tagsPanelSortAlphabeticalRadioButton->setChecked(true);
        ui->tagsPanelOrderGroupBox->setEnabled(true);
    } else {
        ui->tagsPanelSortByLastChangeRadioButton->setChecked(true);
        ui->tagsPanelOrderGroupBox->setEnabled(false);
    }

    settings.value(QStringLiteral("tagsPanelOrder")).toInt() == ORDER_DESCENDING
        ? ui->tagsPanelOrderDescendingRadioButton->setChecked(true)
        : ui->tagsPanelOrderAscendingRadioButton->setChecked(true);

    ui->ignoreNoteSubFoldersLineEdit->setText(
        settings
            .value(QStringLiteral("ignoreNoteSubFolders"),
                   IGNORED_NOTE_SUBFOLDERS_DEFAULT)
            .toString());

    // Navigation Panel Options
    ui->navigationPanelHideSearchCheckBox->setChecked(
        settings.value(QStringLiteral("navigationPanelHideSearch")).toBool());

    ui->enableNoteTreeCheckBox->setChecked(Utils::Misc::isEnableNoteTree());
}

/**
 * Loads the shortcut settings
 */
void SettingsDialog::loadShortcutSettings() {
    MainWindow *mainWindow = MainWindow::instance();

    if (mainWindow == Q_NULLPTR) {
        return;
    }

    QSettings settings;
    bool darkMode = settings.value(QStringLiteral("darkMode")).toBool();

    QPalette palette;
    QColor shortcutButtonActiveColor =
        darkMode ? Qt::white : palette.color(QPalette::ButtonText);
    QColor shortcutButtonInactiveColor =
        darkMode ? Qt::darkGray : palette.color(QPalette::Mid);

    QList<QMenu *> menus = mainWindow->menuList();
    ui->shortcutSearchLineEdit->clear();
    ui->shortcutTreeWidget->clear();
    ui->shortcutTreeWidget->setColumnCount(3);

    // shortcuts on toolbars and note folders don't work yet
    auto disabledMenuNames = QStringList() << QStringLiteral("menuToolbars")
                                           << QStringLiteral("noteFoldersMenu");

    // loop through all menus
    foreach (QMenu *menu, menus) {
        if (disabledMenuNames.contains(menu->objectName())) {
            continue;
        }

        auto *menuItem = new QTreeWidgetItem();
        int actionCount = 0;

        // loop through all actions of the menu
        foreach (QAction *action, menu->actions()) {
            const QString &actionObjectName = action->objectName();

            // we don't need empty objects
            if (actionObjectName.isEmpty()) {
                continue;
            }

            // create the tree widget item
            auto *actionItem = new QTreeWidgetItem();
            actionItem->setText(0, action->text().remove(QStringLiteral("&")));
            actionItem->setToolTip(0, actionObjectName);
            actionItem->setData(1, Qt::UserRole, actionObjectName);
            menuItem->addChild(actionItem);

            // create the key widget for the local shortcut
            auto *keyWidget = new QKeySequenceWidget();
            keyWidget->setFixedWidth(240);
            keyWidget->setClearButtonIcon(
                QIcon::fromTheme(QStringLiteral("edit-clear"),
                                 QIcon(":/icons/breeze-pkbsuite/16x16/"
                                       "edit-clear.svg")));
            keyWidget->setNoneText(tr("Undefined key"));
            keyWidget->setShortcutButtonActiveColor(shortcutButtonActiveColor);
            keyWidget->setShortcutButtonInactiveColor(
                shortcutButtonInactiveColor);
            keyWidget->setToolTip(tr("Assign a new key"),
                                  tr("Reset to default key"));
            keyWidget->setDefaultKeySequence(action->data().toString());
            keyWidget->setKeySequence(action->shortcut());

            connect(
                keyWidget, &QKeySequenceWidget::keySequenceAccepted, this,
                [this, actionObjectName]() { keySequenceEvent(actionObjectName); });

            ui->shortcutTreeWidget->setItemWidget(actionItem, 1, keyWidget);

            actionCount++;
        }

        if (actionCount > 0) {
            menuItem->setText(0, menu->title().remove(QStringLiteral("&")));
            menuItem->setToolTip(0, menu->objectName());
            ui->shortcutTreeWidget->addTopLevelItem(menuItem);
            menuItem->setExpanded(true);
        }
    }

    ui->shortcutTreeWidget->resizeColumnToContents(0);
    ui->shortcutTreeWidget->resizeColumnToContents(1);
    ui->shortcutTreeWidget->resizeColumnToContents(2);
}

/**
 * Show an information if a shortcut was already used elsewhere
 *
 * @param objectName
 */
void SettingsDialog::keySequenceEvent(const QString &objectName) {
    QKeySequenceWidget *keySequenceWidget = findKeySequenceWidget(objectName);

    if (keySequenceWidget == Q_NULLPTR) {
        return;
    }

    QKeySequence eventKeySequence = keySequenceWidget->keySequence();

    // skip events with empty key sequence
    if (eventKeySequence.isEmpty()) {
        return;
    }

    // loop all top level tree widget items (menus)
    for (int i = 0; i < ui->shortcutTreeWidget->topLevelItemCount(); i++) {
        QTreeWidgetItem *menuItem = ui->shortcutTreeWidget->topLevelItem(i);

        // loop all tree widget items of the menu (action shortcuts)
        for (int j = 0; j < menuItem->childCount(); j++) {
            QTreeWidgetItem *shortcutItem = menuItem->child(j);

            // skip the item that threw the event
            if (shortcutItem->data(1, Qt::UserRole).toString() == objectName) {
                continue;
            }

            auto keyWidget = static_cast<QKeySequenceWidget *>(
                ui->shortcutTreeWidget->itemWidget(shortcutItem, 1));

            if (keyWidget == Q_NULLPTR) {
                continue;
            }

            QKeySequence keySequence = keyWidget->keySequence();
            QKeySequence defaultKeySequence = keyWidget->defaultKeySequence();

            // show an information if the shortcut was already used elsewhere
            if (keySequence == eventKeySequence) {
                if (Utils::Gui::information(
                        this, tr("Shortcut already assigned"),
                        tr("The shortcut <strong>%1</strong> is already "
                           "assigned to <strong>%2</strong>! Do you want to "
                           "jump to the shortcut?")
                            .arg(eventKeySequence.toString(),
                                 shortcutItem->text(0)),
                        QStringLiteral("settings-shortcut-already-assigned"),
                        QMessageBox::Yes | QMessageBox::Cancel,
                        QMessageBox::Yes) == QMessageBox::Yes) {
                    ui->shortcutTreeWidget->scrollToItem(shortcutItem);
                    ui->shortcutTreeWidget->clearSelection();
                    shortcutItem->setSelected(true);
                }

                return;
            }
        }
    }
}

/**
 * Finds a QKeySequenceWidget in the shortcutTreeWidget by the objectName
 * of the assigned menu action
 */
QKeySequenceWidget *SettingsDialog::findKeySequenceWidget(
    const QString &objectName) {
    // loop all top level tree widget items (menus)
    for (int i = 0; i < ui->shortcutTreeWidget->topLevelItemCount(); i++) {
        QTreeWidgetItem *menuItem = ui->shortcutTreeWidget->topLevelItem(i);

        // loop all tree widget items of the menu (action shortcuts)
        for (int j = 0; j < menuItem->childCount(); j++) {
            QTreeWidgetItem *shortcutItem = menuItem->child(j);

            QString name = shortcutItem->data(1, Qt::UserRole).toString();

            if (name == objectName) {
                return static_cast<QKeySequenceWidget *>(
                    ui->shortcutTreeWidget->itemWidget(shortcutItem, 1));
            }
        }
    }

    return Q_NULLPTR;
}

/**
 * Stores the local and global keyboard shortcut settings
 */
void SettingsDialog::storeShortcutSettings() {
    QSettings settings;

    // loop all top level tree widget items (menus)
    for (int i = 0; i < ui->shortcutTreeWidget->topLevelItemCount(); i++) {
        QTreeWidgetItem *menuItem = ui->shortcutTreeWidget->topLevelItem(i);

        // loop all tree widget items of the menu (action shortcuts)
        for (int j = 0; j < menuItem->childCount(); j++) {
            QTreeWidgetItem *shortcutItem = menuItem->child(j);

            auto *keyWidget = dynamic_cast<QKeySequenceWidget *>(
                ui->shortcutTreeWidget->itemWidget(shortcutItem, 1));

            auto *globalShortcutKeyWidget = dynamic_cast<QKeySequenceWidget *>(
                ui->shortcutTreeWidget->itemWidget(shortcutItem, 2));

            if (keyWidget == nullptr || globalShortcutKeyWidget == nullptr) {
                continue;
            }

            const QString actionObjectName =
                shortcutItem->data(1, Qt::UserRole).toString();

            // handle local shortcut
            QKeySequence keySequence = keyWidget->keySequence();
            QKeySequence defaultKeySequence = keyWidget->defaultKeySequence();
            QString settingsKey = "Shortcuts/MainWindow-" + actionObjectName;

            // remove or store the setting for the shortcut if it's not default
            if (keySequence == defaultKeySequence) {
                settings.remove(settingsKey);
            } else if (!keySequence.isEmpty()) {
                settings.setValue(settingsKey, keySequence);
            }

            // handle global shortcut
            keySequence = globalShortcutKeyWidget->keySequence();
            settingsKey = "GlobalShortcuts/MainWindow-" + actionObjectName;

            // remove or store the setting for the shortcut if it's not empty
            if (keySequence.isEmpty()) {
                settings.remove(settingsKey);
            } else {
                settings.setValue(settingsKey, keySequence);
            }
        }
    }
}

/**
 * Selects a value in a list widget, that is hidden in the whatsThis parameter
 */
void SettingsDialog::selectListWidgetValue(QListWidget *listWidget,
                                           const QString &value) {
    // get all items from the list widget
    QList<QListWidgetItem *> items = listWidget->findItems(
        QStringLiteral("*"), Qt::MatchWrap | Qt::MatchWildcard);
    // select the right item in the selector
    Q_FOREACH (QListWidgetItem *item, items) {
        if (item->whatsThis() == value) {
            const QSignalBlocker blocker(listWidget);
            Q_UNUSED(blocker)

            item->setSelected(true);
            break;
        }
    }
}

/**
 * Checks if a value, that is hidden in the whatsThis parameter exists in a
 * list widget
 */
bool SettingsDialog::listWidgetValueExists(QListWidget *listWidget,
                                           const QString &value) {
    // get all items from the list widget
    QList<QListWidgetItem *> items = listWidget->findItems(
        QStringLiteral("*"), Qt::MatchWrap | Qt::MatchWildcard);
    // checks if the value exists
    Q_FOREACH (QListWidgetItem *item, items) {
        if (item->whatsThis() == value) {
            return true;
        }
    }

    return false;
}

/**
 * Returns the selected value in list widget, that is hidden in
 * the whatsThis parameter
 */
QString SettingsDialog::getSelectedListWidgetValue(QListWidget *listWidget) {
    QList<QListWidgetItem *> items = listWidget->selectedItems();

    if (items.count() >= 1) {
        return items.first()->whatsThis();
    }

    return QString();
}

void SettingsDialog::setFontLabel(QLineEdit *label, const QFont &font) {
    label->setText(font.family() + " (" + QString::number(font.pointSize()) +
                   ")");
    label->setFont(font);
}

/* * * * * * * * * * * * * * * *
 *
 * Slot implementations
 *
 * * * * * * * * * * * * * * * */

void SettingsDialog::on_buttonBox_clicked(QAbstractButton *button) {
    if (button == ui->buttonBox->button(QDialogButtonBox::Ok)) {
        storeSettings();
    }
}

void SettingsDialog::on_noteTextEditButton_clicked() {
    bool ok;
    QFont font = Utils::Gui::fontDialogGetFont(&ok, noteTextEditFont, this);

    qDebug() << __func__ << " - 'font': " << font;

    if (ok) {
        noteTextEditFont = font;
        setFontLabel(ui->noteTextEditFontLabel, noteTextEditFont);

        // store the font settings
        storeFontSettings();

        // update the text items after the font was changed
        ui->editorFontColorWidget->updateAllTextItems();
    }
}

void SettingsDialog::on_noteTextEditCodeButton_clicked() {
    bool ok;
    QFont font =
        Utils::Gui::fontDialogGetFont(&ok, noteTextEditCodeFont, this,
                                      QString(), QFontDialog::MonospacedFonts);
    if (ok) {
        noteTextEditCodeFont = font;
        setFontLabel(ui->noteTextEditCodeFontLabel, noteTextEditCodeFont);

        // store the font settings
        storeFontSettings();

        // update the text items after the font was changed
        ui->editorFontColorWidget->updateAllTextItems();
    }
}

void SettingsDialog::on_noteTextViewButton_clicked() {
    bool ok;
    QFont font = Utils::Gui::fontDialogGetFont(&ok, noteTextViewFont, this);
    if (ok) {
        noteTextViewFont = font;
        setFontLabel(ui->noteTextViewFontLabel, noteTextViewFont);
    }
}

void SettingsDialog::on_noteTextViewCodeButton_clicked() {
    bool ok;
    QFont font =
        Utils::Gui::fontDialogGetFont(&ok, noteTextViewCodeFont, this,
                                      QString(), QFontDialog::MonospacedFonts);
    if (ok) {
        noteTextViewCodeFont = font;
        setFontLabel(ui->noteTextViewCodeFontLabel, noteTextViewCodeFont);
    }
}

void SettingsDialog::on_reinitializeDatabaseButton_clicked() {
    if (QMessageBox::information(
            this, tr("Database"),
            tr("Do you really want to clear the local database? "
               "This will also remove your configured note "
               "folders and your cached todo items!"),
            tr("Clear &database"), tr("&Cancel"), QString(), 1) == 0) {
        DatabaseService::reinitializeDiskDatabase();
        NoteFolder::migrateToNoteFolders();

        Utils::Gui::information(this, tr("Database"),
                                tr("The Database was reinitialized."),
                                QStringLiteral("database-reinitialized"));
    }
}

/**
 * Allows the user to clear all settings and the database and exit the app
 */
void SettingsDialog::on_clearAppDataAndExitButton_clicked() {
    if (QMessageBox::information(
            this, tr("Clear app data and exit"),
            tr("Do you really want to clear all settings, remove the "
               "database and exit PKbSuite?\n\n"
               "Your notes will stay intact!"),
            tr("Clear and &exit"), tr("&Cancel"), QString(), 1) == 0) {
        QSettings settings;
        settings.clear();
        DatabaseService::removeDiskDatabase();

        // remove the log file
        removeLogFile();

        // make sure no settings get written after after are quitting
        qApp->setProperty("clearAppDataAndExit", true);
        qApp->quit();
    }
}

/**
 * Removes the log file
 */
void SettingsDialog::removeLogFile() const {
    // remove log file if exists
    QFile file(Utils::Misc::logFilePath());
    if (file.exists()) {
        // remove the file
        bool result = file.remove();
        QString text = result ? "Removed" : "Could not remove";

        // in case that the settings are cleared logging to log file is
        // disabled by default and it will not be created again
        qWarning() << text + " log file: " << file.fileName();
    }
}

/**
 * Resets the font for the note text edit
 */
void SettingsDialog::on_noteTextEditResetButton_clicked() {
    QTextEdit textEdit;
    noteTextEditFont = textEdit.font();
    setFontLabel(ui->noteTextEditFontLabel, noteTextEditFont);

    // store the font settings
    storeFontSettings();

    // update the text items after the font was changed
    ui->editorFontColorWidget->updateAllTextItems();
}

/**
 * Resets the font for the note text code edit
 */
void SettingsDialog::on_noteTextEditCodeResetButton_clicked() {
    noteTextEditCodeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFontLabel(ui->noteTextEditCodeFontLabel, noteTextEditCodeFont);

    // store the font settings
    storeFontSettings();

    // update the text items after the font was changed
    ui->editorFontColorWidget->updateAllTextItems();
}

/**
 * Resets the font for the note markdown view
 */
void SettingsDialog::on_noteTextViewResetButton_clicked() {
    QTextBrowser textView;
    noteTextViewFont = textView.font();
    setFontLabel(ui->noteTextViewFontLabel, noteTextViewFont);
}

/**
 * Resets the font for the note markdown code view
 */
void SettingsDialog::on_noteTextViewCodeResetButton_clicked() {
    noteTextViewCodeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFontLabel(ui->noteTextViewCodeFontLabel, noteTextViewCodeFont);
}

/**
 * Sets a path to an external editor
 */
void SettingsDialog::on_setExternalEditorPathToolButton_clicked() {
    QString path = ui->externalEditorPathLineEdit->text();
    QString dirPath = path;

    // get the path of the directory if a editor path was set
    if (!path.isEmpty()) {
        dirPath = QFileInfo(path).dir().path();
    }

    QStringList mimeTypeFilters;
    mimeTypeFilters << QStringLiteral("application/x-executable")
                    << QStringLiteral("application/octet-stream");

    FileDialog dialog(QStringLiteral("ExternalEditor"));

    if (!dirPath.isEmpty()) {
        dialog.setDirectory(dirPath);
    }

    if (!path.isEmpty()) {
        dialog.selectFile(path);
    }

    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.setWindowTitle(tr("Select editor application"));
    int ret = dialog.exec();

    if (ret == QDialog::Accepted) {
        QStringList fileNames = dialog.selectedFiles();
        if (fileNames.empty()) {
            return;
        }

        const QString &filePath(fileNames.at(0));
        ui->externalEditorPathLineEdit->setText(filePath);
    }
}

/**
 * Does the note folder page setup
 */
void SettingsDialog::setupNoteFolderPage() {
//    const QSignalBlocker blocker(ui->noteFolderListWidget);
    //Q_UNUSED(blocker)

    // hide the owncloud server settings
    ui->noteFolderEditFrame->setEnabled(NoteFolder::countAll() > 0);

    QList<NoteFolder> noteFolders = NoteFolder::fetchAll();
    int noteFoldersCount = noteFolders.count();

    // populate the note folder list
    if (noteFoldersCount > 0) {
        Q_FOREACH (NoteFolder noteFolder, noteFolders) {
            auto *item = new QListWidgetItem(noteFolder.getName());
            item->setData(Qt::UserRole, noteFolder.getId());
            ui->noteFolderListWidget->addItem(item);

            // set the current row
            if (noteFolder.getId() == NoteFolder::currentNoteFolderId()) {
                ui->noteFolderListWidget->setCurrentItem(item);
            }
        }
    }

    // disable the remove button if there is only one item
    ui->noteFolderRemoveButton->setEnabled(noteFoldersCount > 1);

    // set local path placeholder text
    ui->noteFolderLocalPathLineEdit->setPlaceholderText(
        Utils::Misc::defaultNotesPath());

    noteFolderRemotePathTreeStatusBar = new QStatusBar(this);
    ui->noteFolderRemotePathTreeWidgetFrame->layout()->addWidget(
        noteFolderRemotePathTreeStatusBar);
}

void SettingsDialog::on_noteFolderListWidget_currentItemChanged(
    QListWidgetItem *current, QListWidgetItem *previous) {
    Q_UNUSED(previous)

    int noteFolderId = current->data(Qt::UserRole).toInt();
    _selectedNoteFolder = NoteFolder::fetch(noteFolderId);
    if (_selectedNoteFolder.isFetched()) {
        ui->noteFolderNameLineEdit->setText(_selectedNoteFolder.getName());
        ui->noteFolderLocalPathLineEdit->setText(
            _selectedNoteFolder.getLocalPath());
        ui->noteFolderShowSubfoldersCheckBox->setChecked(
            _selectedNoteFolder.isShowSubfolders());
        ui->allowDifferentNoteFileNameCheckBox->setChecked(
            _selectedNoteFolder.settingsValue(QStringLiteral("allowDifferentNoteFileName")).toBool());

        const QSignalBlocker blocker(ui->noteFolderActiveCheckBox);
        Q_UNUSED(blocker)
        ui->noteFolderActiveCheckBox->setChecked(
            _selectedNoteFolder.isCurrent());
    }
}

void SettingsDialog::on_noteFolderAddButton_clicked() {
    const QString currentPath = _selectedNoteFolder.getLocalPath();

    _selectedNoteFolder = NoteFolder();
    _selectedNoteFolder.setName(tr("new folder"));
    _selectedNoteFolder.setLocalPath(currentPath);
    _selectedNoteFolder.setPriority(ui->noteFolderListWidget->count());
    _selectedNoteFolder.store();

    if (_selectedNoteFolder.isFetched()) {
        auto *item = new QListWidgetItem(_selectedNoteFolder.getName());
        item->setData(Qt::UserRole, _selectedNoteFolder.getId());
        ui->noteFolderListWidget->addItem(item);

        // set the current row
        ui->noteFolderListWidget->setCurrentRow(
            ui->noteFolderListWidget->count() - 1);

        // enable the remove button
        ui->noteFolderRemoveButton->setEnabled(true);

        // focus the folder name edit and select the text
        ui->noteFolderNameLineEdit->setFocus();
        ui->noteFolderNameLineEdit->selectAll();
    }
}

/**
 * Removes the current note folder
 */
void SettingsDialog::on_noteFolderRemoveButton_clicked() {
    if (ui->noteFolderListWidget->count() < 2) {
        return;
    }

    if (Utils::Gui::question(
            this, tr("Remove note folder"),
            tr("Remove the current note folder <strong>%1</strong>?")
                .arg(_selectedNoteFolder.getName()),
            QStringLiteral("remove-note-folder")) == QMessageBox::Yes) {
        bool wasCurrent = _selectedNoteFolder.isCurrent();

        QSettings settings;

        // remove saved searches
        QString settingsKey = "savedSearches/noteFolder-" +
                              QString::number(_selectedNoteFolder.getId());
        settings.remove(settingsKey);

        // remove tree widget expand state setting
        settingsKey = NoteSubFolder::treeWidgetExpandStateSettingsKey(
            _selectedNoteFolder.getId());
        settings.remove(settingsKey);

        // remove the note folder from the database
        _selectedNoteFolder.remove();

        // remove the list item
        ui->noteFolderListWidget->takeItem(
            ui->noteFolderListWidget->currentRow());

        // disable the remove button if there is only one item left
        ui->noteFolderRemoveButton->setEnabled(
            ui->noteFolderListWidget->count() > 1);

        // if the removed note folder was the current folder we set the first
        // note folder as new current one
        if (wasCurrent) {
            QList<NoteFolder> noteFolders = NoteFolder::fetchAll();
            if (noteFolders.count() > 0) {
                noteFolders[0].setAsCurrent();
            }
        }
    }
}

/**
 * Updates the name of the current note folder edit
 */
void SettingsDialog::on_noteFolderNameLineEdit_editingFinished() {
    QString text = ui->noteFolderNameLineEdit->text()
                       .remove(QStringLiteral("\n"))
                       .trimmed();
    text.truncate(50);
    _selectedNoteFolder.setName(text);
    _selectedNoteFolder.store();

    ui->noteFolderListWidget->currentItem()->setText(text);
}

void SettingsDialog::on_noteFolderLocalPathButton_clicked() {
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Please select the folder where your notes will get stored to"),
        _selectedNoteFolder.getLocalPath(), QFileDialog::ShowDirsOnly);

    QDir d = QDir(dir);

    if (d.exists() && (!dir.isEmpty())) {
        ui->noteFolderLocalPathLineEdit->setText(dir);
        _selectedNoteFolder.setLocalPath(dir);
        _selectedNoteFolder.store();
    }
}

/**
 * Sets the current note folder as active note folder
 */
void SettingsDialog::on_noteFolderActiveCheckBox_stateChanged(int arg1) {
    Q_UNUSED(arg1)

    if (!ui->noteFolderActiveCheckBox->isChecked()) {
        const QSignalBlocker blocker(ui->noteFolderActiveCheckBox);
        Q_UNUSED(blocker)
        ui->noteFolderActiveCheckBox->setChecked(true);
    } else {
        _selectedNoteFolder.setAsCurrent();
        MainWindow::instance()->resetBrokenTagNotesLinkFlag();
    }
}

/**
 * Recursively generates the path string from the tree widget items
 */
QString SettingsDialog::generatePathFromCurrentNoteFolderRemotePathItem(
    QTreeWidgetItem *item) {
    if (item == nullptr) {
        return QString();
    }

    QTreeWidgetItem *parent = item->parent();
    if (parent != nullptr) {
        return generatePathFromCurrentNoteFolderRemotePathItem(parent) +
               QStringLiteral("/") + item->text(0);
    }

    return item->text(0);
}

/**
 * Does the scripting page setup
 */
void SettingsDialog::setupScriptingPage() {
    // reload the script list
    reloadScriptList();

    QString issueUrl =
        QStringLiteral("https://github.com/pbek/PKbSuite/issues");
    QString documentationUrl = QStringLiteral(
        "https://docs.pkbsuite.org/en/develop/scripting/README.html");
    ui->scriptInfoLabel->setText(
        tr("Take a look at the <a href=\"%1\">Scripting documentation</a> "
           "to get started fast.")
            .arg(documentationUrl) +
        "<br>" +
        tr("If you need access to a certain functionality in "
           "PKbSuite please open an issue on the "
           "<a href=\"%1\"> PKbSuite issue page</a>.")
            .arg(issueUrl));

    /*
     * Setup the "add script" button menu
     */
    auto *addScriptMenu = new QMenu(this);

    QAction *searchScriptAction =
        addScriptMenu->addAction(tr("Search script repository"));
    searchScriptAction->setIcon(
        QIcon::fromTheme(QStringLiteral("edit-find"),
                         QIcon(":icons/breeze-pkbsuite/16x16/edit-find.svg")));
    searchScriptAction->setToolTip(
        tr("Find a script in the script "
           "repository"));
    connect(searchScriptAction, SIGNAL(triggered()), this,
            SLOT(searchScriptInRepository()));

    QAction *updateScriptAction =
        addScriptMenu->addAction(tr("Check for script updates"));
    updateScriptAction->setIcon(QIcon::fromTheme(
        QStringLiteral("svn-update"),
        QIcon(":icons/breeze-pkbsuite/16x16/svn-update.svg")));
    connect(updateScriptAction, SIGNAL(triggered()), this,
            SLOT(checkForScriptUpdates()));

    QAction *addAction = addScriptMenu->addAction(tr("Add local script"));
    addAction->setIcon(QIcon::fromTheme(
        QStringLiteral("document-new"),
        QIcon(":icons/breeze-pkbsuite/16x16/document-new.svg")));
    addAction->setToolTip(tr("Add an existing, local script"));
    connect(addAction, SIGNAL(triggered()), this, SLOT(addLocalScript()));

    ui->scriptAddButton->setMenu(addScriptMenu);
}

/**
 * Reloads the script list
 */
void SettingsDialog::reloadScriptList() const {
    QList<Script> scripts = Script::fetchAll();
    int scriptsCount = scripts.count();
    ui->scriptListWidget->clear();

    // populate the script list
    if (scriptsCount > 0) {
        Q_FOREACH (Script script, scripts) {
            auto *item = new QListWidgetItem(script.getName());
            item->setData(Qt::UserRole, script.getId());
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(script.getEnabled() ? Qt::Checked
                                                    : Qt::Unchecked);
            ui->scriptListWidget->addItem(item);
        }

        // set the current row
        ui->scriptListWidget->setCurrentRow(0);
    }

    // disable the edit frame if there is no item
    ui->scriptEditFrame->setEnabled(scriptsCount > 0);

    // disable the remove button if there is no item
    ui->scriptRemoveButton->setEnabled(scriptsCount > 0);
}

/**
 * Adds a new script
 */
void SettingsDialog::addLocalScript() {
    _selectedScript = Script();
    _selectedScript.setName(_newScriptName);
    _selectedScript.setPriority(ui->scriptListWidget->count());
    _selectedScript.store();

    if (_selectedScript.isFetched()) {
        auto *item = new QListWidgetItem(_selectedScript.getName());
        item->setData(Qt::UserRole, _selectedScript.getId());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        ui->scriptListWidget->addItem(item);

        // set the current row
        ui->scriptListWidget->setCurrentRow(ui->scriptListWidget->count() - 1);

        // enable the remove button
        ui->scriptRemoveButton->setEnabled(true);

        // focus the script name edit and select the text
        ui->scriptNameLineEdit->setFocus();
        ui->scriptNameLineEdit->selectAll();

        // open the dialog to select the script
        on_scriptPathButton_clicked();
    }
}

/**
 * Removes the current script
 */
void SettingsDialog::on_scriptRemoveButton_clicked() {
    if (ui->scriptListWidget->count() < 1) {
        return;
    }

    if (Utils::Gui::question(
            this, tr("Remove script"),
            tr("Remove the current script <strong>%1</strong>?")
                .arg(_selectedScript.getName()),
            QStringLiteral("remove-script")) == QMessageBox::Yes) {
        // remove the script from the database
        _selectedScript.remove();

        // remove the list item
        ui->scriptListWidget->takeItem(ui->scriptListWidget->currentRow());

        bool scriptsAvailable = ui->scriptListWidget->count() > 0;
        // disable the remove button if there is only no item left
        ui->scriptRemoveButton->setEnabled(scriptsAvailable);

        // disable the edit frame if there is no item
        ui->scriptEditFrame->setEnabled(scriptsAvailable);

        // reload the scripting engine
        ScriptingService::instance()->reloadEngine();
    }
}

/**
 * Allows to choose the script path
 */
void SettingsDialog::on_scriptPathButton_clicked() {
    QString path = ui->scriptPathLineEdit->text();
    QString dirPath = path;

    // get the path of the script if a script was set
    if (!path.isEmpty()) {
        dirPath = QFileInfo(path).dir().path();
    }

    FileDialog dialog(QStringLiteral("ScriptPath"));

    if (!dirPath.isEmpty()) {
        dialog.setDirectory(dirPath);
    }

    if (!path.isEmpty()) {
        dialog.selectFile(path);
    }

    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilter(tr("QML files") + " (*.qml)");
    dialog.setWindowTitle(tr("Please select your QML file"));
    int ret = dialog.exec();

    if (ret == QDialog::Accepted) {
        path = dialog.selectedFile();

        QFile file(path);

        if (file.exists() && (!path.isEmpty())) {
            QString scriptName = _selectedScript.getName();

            // set the script name from the file name if none was set yet
            if (scriptName.isEmpty() || (scriptName == _newScriptName)) {
                scriptName = QFileInfo(file).baseName();
                ui->scriptNameLineEdit->setText(scriptName);
                ui->scriptNameLabel->setText(scriptName);
                _selectedScript.setName(scriptName);

                const QSignalBlocker blocker(ui->scriptListWidget);
                Q_UNUSED(blocker)
                ui->scriptListWidget->currentItem()->setText(scriptName);
            }

            ui->scriptPathLineEdit->setText(path);
            _selectedScript.setScriptPath(path);
            _selectedScript.store();

            // validate the script
            validateCurrentScript();

            // reload the scripting engine
            ScriptingService::instance()->reloadEngine();

            // trigger the item change so that the page is reloaded for
            // script variables
            reloadCurrentScriptPage();
        }
    }
}

/**
 * Loads the current script in the UI when the current item changed
 */
void SettingsDialog::on_scriptListWidget_currentItemChanged(
    QListWidgetItem *current, QListWidgetItem *previous) {
    Q_UNUSED(current)
    Q_UNUSED(previous)

    reloadCurrentScriptPage();
}

/**
 * Loads the current script in the UI
 */
void SettingsDialog::reloadCurrentScriptPage() {
    QListWidgetItem *item = ui->scriptListWidget->currentItem();

    if (item == Q_NULLPTR) {
        return;
    }

    ui->scriptValidationLabel->clear();

    int scriptId = item->data(Qt::UserRole).toInt();
    _selectedScript = Script::fetch(scriptId);
    if (_selectedScript.isFetched()) {
        ui->scriptNameLabel->setText("<b>" + _selectedScript.getName() +
                                     "</b>");
        ui->scriptPathLineEdit->setText(_selectedScript.getScriptPath());
        ui->scriptEditFrame->setEnabled(true);

        bool isScriptFromRepository = _selectedScript.isScriptFromRepository();
        ui->scriptNameLineEdit->setReadOnly(isScriptFromRepository);
        ui->scriptPathButton->setDisabled(isScriptFromRepository);
        ui->scriptRepositoryItemFrame->setVisible(isScriptFromRepository);
        ui->localScriptItemFrame->setHidden(isScriptFromRepository);
        ui->scriptNameLineEdit->setHidden(isScriptFromRepository);
        ui->scriptNameLineEditLabel->setHidden(isScriptFromRepository);

        // add additional information if script was from the script repository
        if (isScriptFromRepository) {
            ScriptInfoJson infoJson = _selectedScript.getScriptInfoJson();

            ui->scriptVersionLabel->setText(infoJson.version);
            ui->scriptDescriptionLabel->setText(infoJson.description);
            ui->scriptAuthorsLabel->setText(infoJson.richAuthorText);
            ui->scriptRepositoryLinkLabel->setText(
                "<a href=\"https://github.com/pkbsuite/scripts/tree/"
                "master/" +
                infoJson.identifier + "\">" + tr("Open repository") + "</a>");
        } else {
            ui->scriptNameLineEdit->setText(_selectedScript.getName());
        }

        // get the registered script settings variables
        QList<QVariant> variables =
            ScriptingService::instance()->getSettingsVariables(
                _selectedScript.getId());

        bool hasScriptSettings = variables.count() > 0;
        ui->scriptSettingsFrame->setVisible(hasScriptSettings);

        if (hasScriptSettings) {
            // remove the current ScriptSettingWidget widgets in the
            // scriptSettingsFrame
            QList<ScriptSettingWidget *> widgets =
                ui->scriptSettingsFrame->findChildren<ScriptSettingWidget *>();
            Q_FOREACH (ScriptSettingWidget *widget, widgets) { delete widget; }

            foreach (QVariant variable, variables) {
                QMap<QString, QVariant> varMap = variable.toMap();

                // populate the variable UI
                ScriptSettingWidget *scriptSettingWidget =
                    new ScriptSettingWidget(this, _selectedScript, varMap);

                //                    QString name = varMap["name"].toString();

                ui->scriptSettingsFrame->layout()->addWidget(
                    scriptSettingWidget);
            }
        }

        // validate the script
        validateCurrentScript();
    } else {
        ui->scriptEditFrame->setEnabled(false);
        ui->scriptNameLineEdit->clear();
        ui->scriptPathLineEdit->clear();
    }
}

/**
 * Validates the current script
 */
void SettingsDialog::validateCurrentScript() {
    ui->scriptValidationLabel->clear();

    if (_selectedScript.isFetched()) {
        QString path = _selectedScript.getScriptPath();

        // check the script validity if the path is not empty
        if (!path.isEmpty()) {
            QString errorMessage;
            bool result =
                ScriptingService::validateScript(_selectedScript, errorMessage);
            QString validationText =
                result ? tr("Your script seems to be valid")
                       : tr("There were script errors:\n%1").arg(errorMessage);
            ui->scriptValidationLabel->setText(validationText);
            ui->scriptValidationLabel->setStyleSheet(
                QStringLiteral("color: %1;").arg(result ? "green" : "red"));
        }
    }
}

/**
 * Stores a script name after it was edited
 */
void SettingsDialog::on_scriptNameLineEdit_editingFinished() {
    QString text = ui->scriptNameLineEdit->text();
    _selectedScript.setName(text);
    _selectedScript.store();

    ui->scriptListWidget->currentItem()->setText(text);
}

/**
 * Stores the enabled states of the scripts
 */
void SettingsDialog::storeScriptListEnabledState() {
    for (int i = 0; i < ui->scriptListWidget->count(); i++) {
        QListWidgetItem *item = ui->scriptListWidget->item(i);
        bool enabled = item->checkState() == Qt::Checked;
        int scriptId = item->data(Qt::UserRole).toInt();

        Script script = Script::fetch(scriptId);
        if (script.isFetched()) {
            if (script.getEnabled() != enabled) {
                script.setEnabled(enabled);
                script.store();
            }
        }
    }

    // reload the scripting engine
    ScriptingService::instance()->reloadEngine();
}

/**
 * Validates the current script
 */
void SettingsDialog::on_scriptValidationButton_clicked() {
    // validate the script
    validateCurrentScript();
}

/**
 * Reloads the scripting engine
 */
void SettingsDialog::on_scriptReloadEngineButton_clicked() {
    // store the enabled states and reload the scripting engine
    storeScriptListEnabledState();

    // trigger the item change so that the page is reloaded for
    // script variables
    reloadCurrentScriptPage();
}

/**
 * Adds a custom file extension
 */
void SettingsDialog::on_addCustomNoteFileExtensionButton_clicked() {
    bool ok;
    QString fileExtension;
    fileExtension = QInputDialog::getText(
        this, tr("File extension"), tr("Enter your custom file extension:"),
        QLineEdit::Normal, fileExtension, &ok);

    if (!ok) {
        return;
    }

    // make sure the file extension doesn't start with a point
    fileExtension = Utils::Misc::removeIfStartsWith(std::move(fileExtension),
                                                    QStringLiteral("."));

    QListWidgetItem *item = addCustomNoteFileExtension(fileExtension);

    if (item != Q_NULLPTR) {
        ui->defaultNoteFileExtensionListWidget->setCurrentItem(item);
    }
}

/**
 * Adds a custom note file extension
 */
QListWidgetItem *SettingsDialog::addCustomNoteFileExtension(
    const QString &fileExtension) {
    if (listWidgetValueExists(ui->defaultNoteFileExtensionListWidget,
                              fileExtension)) {
        return Q_NULLPTR;
    }

    auto *item = new QListWidgetItem(fileExtension);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setWhatsThis(fileExtension);
    ui->defaultNoteFileExtensionListWidget->addItem(item);

    return item;
}

/**
 * Removes a custom file extension
 */
void SettingsDialog::on_removeCustomNoteFileExtensionButton_clicked() {
    delete (ui->defaultNoteFileExtensionListWidget->currentItem());
}

/**
 * Updates a custom file extension
 */
void SettingsDialog::on_defaultNoteFileExtensionListWidget_itemChanged(
    QListWidgetItem *item) {
    // make sure the file extension doesn't start with a point
    QString fileExtension =
        Utils::Misc::removeIfStartsWith(item->text(), QStringLiteral("."));

    if (fileExtension != item->text()) {
        item->setText(fileExtension);
    }

    item->setWhatsThis(fileExtension);
}

/**
 * Disables the remove custom file extension button for the first two rows
 */
void SettingsDialog::on_defaultNoteFileExtensionListWidget_currentRowChanged(
    int currentRow) {
    ui->removeCustomNoteFileExtensionButton->setEnabled(currentRow > 1);
}

void SettingsDialog::on_darkModeCheckBox_toggled() {
    handleDarkModeCheckBoxToggled(true, true);
}

/**
 * Toggles the dark mode colors check box with the dark mode checkbox
 */
void SettingsDialog::handleDarkModeCheckBoxToggled(bool updateCheckBoxes,
                                                   bool updateSchema) {
    bool checked = ui->darkModeCheckBox->isChecked();

    ui->darkModeColorsCheckBox->setEnabled(!checked);
    ui->darkModeInfoLabel->setVisible(checked);

    if (updateCheckBoxes && checked) {
        ui->darkModeColorsCheckBox->setChecked(true);
        ui->darkModeIconThemeCheckBox->setChecked(true);
    }

    if (updateSchema) {
        if (checked) {
            ui->editorFontColorWidget->selectFirstDarkSchema();
        } else {
            ui->editorFontColorWidget->selectFirstLightSchema();
        }
    }
}

void SettingsDialog::on_noteFolderShowSubfoldersCheckBox_toggled(bool checked) {
    _selectedNoteFolder.setShowSubfolders(checked);

    // reset the active note subfolder if showing subfolders was turned off
    if (!checked) {
        _selectedNoteFolder.resetActiveNoteSubFolder();
    }

    _selectedNoteFolder.store();
}

void SettingsDialog::on_allowDifferentNoteFileNameCheckBox_toggled(bool checked)
{
    _selectedNoteFolder.setSettingsValue(QStringLiteral("allowDifferentNoteFileName"),
                                         checked);
}

/**
 * Searches in the description and in the shortcut for a entered text
 *
 * @param arg1
 */
void SettingsDialog::on_shortcutSearchLineEdit_textChanged(
    const QString &arg1) {
    // get all items
    QList<QTreeWidgetItem *> allItems = ui->shortcutTreeWidget->findItems(
        QString(), Qt::MatchContains | Qt::MatchRecursive);

    // search text if at least one character was entered
    if (arg1.count() >= 1) {
        // search for items in the description
        QList<QTreeWidgetItem *> foundItems = ui->shortcutTreeWidget->findItems(
            arg1, Qt::MatchContains | Qt::MatchRecursive);

        // hide all not found items
        Q_FOREACH (QTreeWidgetItem *item, allItems) {
            bool foundKeySequence = false;

            auto *keyWidget = dynamic_cast<QKeySequenceWidget *>(
                ui->shortcutTreeWidget->itemWidget(item, 1));

            // search in the local shortcut text
            if (keyWidget != nullptr) {
                QKeySequence keySequence = keyWidget->keySequence();
                foundKeySequence =
                    keySequence.toString().contains(arg1, Qt::CaseInsensitive);
            }

            // search in the global shortcut text
            if (!foundKeySequence) {
                keyWidget = dynamic_cast<QKeySequenceWidget *>(
                    ui->shortcutTreeWidget->itemWidget(item, 2));

                if (keyWidget != nullptr) {
                    QKeySequence keySequence = keyWidget->keySequence();
                    foundKeySequence =
                        keySequence.toString().contains(arg1, Qt::CaseInsensitive);
                }
            }

            item->setHidden(!foundItems.contains(item) && !foundKeySequence);
        }

        // show items again that have visible children so that they are
        // really shown
        Q_FOREACH (QTreeWidgetItem *item, allItems) {
            if (Utils::Gui::isOneTreeWidgetItemChildVisible(item)) {
                item->setHidden(false);
                item->setExpanded(true);
            }
        }
    } else {
        // show all items otherwise
        Q_FOREACH (QTreeWidgetItem *item, allItems) { item->setHidden(false); }
    }
}

void SettingsDialog::on_settingsTreeWidget_currentItemChanged(
    QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    Q_UNUSED(previous)
    const int currentIndex = current->whatsThis(0).toInt();

    ui->settingsStackedWidget->setCurrentIndex(currentIndex);

    if (currentIndex == SettingsPages::LayoutPage) {
        ui->layoutWidget->resizeLayoutImage();
    }
}

void SettingsDialog::on_settingsStackedWidget_currentChanged(int index) {
    QTreeWidgetItem *item = findSettingsTreeWidgetItemByPage(index);
    if (item != Q_NULLPTR) {
        const QSignalBlocker blocker(ui->settingsTreeWidget);
        Q_UNUSED(blocker)

        ui->settingsTreeWidget->setCurrentItem(item);
        ui->headlineLabel->setText("<h3>" + item->text(0) + "</h3>");
    }
}

/**
 * Returns the settings tree widget item corresponding to a page
 *
 * @param page
 * @return
 */
QTreeWidgetItem *SettingsDialog::findSettingsTreeWidgetItemByPage(int page) {
    // search for items
    QList<QTreeWidgetItem *> allItems = ui->settingsTreeWidget->findItems(
        QLatin1String(""), Qt::MatchContains | Qt::MatchRecursive);

    // hide all not found items
    Q_FOREACH (QTreeWidgetItem *item, allItems) {
        int id = item->whatsThis(0).toInt();

        if (id == page) {
            return item;
        }
    }

    return Q_NULLPTR;
}

/**
 * Does the initialization for the main splitter
 */
void SettingsDialog::initMainSplitter() {
    _mainSplitter = new QSplitter(this);
    _mainSplitter->setOrientation(Qt::Horizontal);
    ui->leftSideFrame->setStyleSheet(
        QStringLiteral("#leftSideFrame {margin-right: 5px;}"));

    _mainSplitter->addWidget(ui->leftSideFrame);
    _mainSplitter->addWidget(ui->settingsFrame);

    ui->mainFrame->layout()->addWidget(_mainSplitter);

    // restore tag frame splitter state
    QSettings settings;
    QByteArray state =
        settings.value(QStringLiteral("SettingsDialog/mainSplitterState"))
            .toByteArray();
    _mainSplitter->restoreState(state);
}

void SettingsDialog::closeEvent(QCloseEvent *event) {
    Q_UNUSED(event)

    // make sure no settings get written after after we got the
    // clearAppDataAndExit call
    if (qApp->property("clearAppDataAndExit").toBool()) {
        return;
    }

    // store the splitter settings
    storeSplitterSettings();
}

/**
 * Stores some data for Utils::Misc::generateDebugInformation
 */
void SettingsDialog::storeOwncloudDebugData() const {
    QSettings settings;
    settings.setValue(QStringLiteral("ownCloudInfo/appIsValid"), appIsValid);
    settings.setValue(QStringLiteral("ownCloudInfo/notesPathExistsText"),
                      notesPathExistsText);
    settings.setValue(QStringLiteral("ownCloudInfo/serverVersion"),
                      serverVersion);
    settings.setValue(QStringLiteral("ownCloudInfo/connectionErrorMessage"),
                      connectionErrorMessage);
}

/**
 * Stores the splitter settings
 */
void SettingsDialog::storeSplitterSettings() {
    QSettings settings;
    settings.setValue(QStringLiteral("SettingsDialog/mainSplitterState"),
                      _mainSplitter->saveState());
}

/**
 * Resets the item height
 */
void SettingsDialog::on_itemHeightResetButton_clicked() {
    QTreeWidget treeWidget(this);
    auto *treeWidgetItem = new QTreeWidgetItem();
    treeWidget.addTopLevelItem(treeWidgetItem);
    int height = treeWidget.visualItemRect(treeWidgetItem).height();
    ui->itemHeightSpinBox->setValue(height);
}

/**
 * Resets the icon seize
 */
void SettingsDialog::on_toolbarIconSizeResetButton_clicked() {
    QToolBar toolbar(this);
    ui->toolbarIconSizeSpinBox->setValue(toolbar.iconSize().height());
}

void SettingsDialog::on_ignoreNonTodoCalendarsCheckBox_toggled(bool checked) {
    QSettings settings;
    settings.setValue(QStringLiteral("ownCloud/ignoreNonTodoCalendars"),
                      checked);
}

void SettingsDialog::on_applyToolbarButton_clicked() {
    ui->toolbarEditor->apply();

    MainWindow *mainWindow = MainWindow::instance();
    if (mainWindow == Q_NULLPTR) {
        return;
    }

    // get all available toolbar names from the toolbar editor
    QStringList toolbarObjectNames = ui->toolbarEditor->toolbarObjectNames();

    QList<ToolbarContainer> toolbarContainers;
    foreach (QToolBar *toolbar, mainWindow->findChildren<QToolBar *>()) {
        QString name = toolbar->objectName();

        // don't store the custom actions toolbar and toolbars that are
        // not in the toolbar edit widget any more (for some reason they
        // are still found by findChildren)
        if (name == QLatin1String("customActionsToolbar") ||
            !toolbarObjectNames.contains(name)) {
            continue;
        }

        toolbarContainers.append(toolbar);

        // update the icon size
        ToolbarContainer::updateIconSize(toolbar);
    }

    QSettings settings;

    // remove the current toolbars
    //    settings.beginGroup("toolbar");
    //    settings.remove("");
    //    settings.endGroup();

    settings.beginWriteArray(QStringLiteral("toolbar"),
                             toolbarContainers.size());

    for (int i = 0; i < toolbarContainers.size(); i++) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), toolbarContainers[i].name);
        settings.setValue(QStringLiteral("title"), toolbarContainers[i].title);
        settings.setValue(QStringLiteral("items"),
                          toolbarContainers[i].actions);
    }

    settings.endArray();
}

void SettingsDialog::on_resetToolbarPushButton_clicked() {
    if (QMessageBox::information(
            this, tr("Reset toolbars and exit"),
            tr("Do you really want to reset all toolbars? "
               "The application will be closed in the process, the "
               "default toolbars will be restored when you start it "
               "again."),
            tr("Reset and &exit"), tr("&Cancel"), QLatin1String(""), 1) == 0) {
        QSettings settings;

        // remove all settings in the group
        settings.beginGroup(QStringLiteral("toolbar"));
        settings.remove(QLatin1String(""));
        settings.endGroup();

        qApp->quit();
    }
}

/**
 * Toggles the visibility of the image scaling frame
 *
 * @param checked
 */
void SettingsDialog::on_imageScaleDownCheckBox_toggled(bool checked) {
    ui->imageScalingFrame->setVisible(checked);
}

/**
 * Searches for text in the whole settings dialog and filters the settings
 * tree widget
 *
 * @param arg1
 */
void SettingsDialog::on_searchLineEdit_textChanged(const QString &arg1) {
    QList<QTreeWidgetItem *> allItems = ui->settingsTreeWidget->findItems(
        QString(), Qt::MatchContains | Qt::MatchRecursive);

    // search text if at least one character was entered
    if (arg1.count() >= 1) {
        QList<int> pageIndexList;

        // search in the tree widget items themselves
        Q_FOREACH (QTreeWidgetItem *item, allItems) {
            if (item->text(0).contains(arg1, Qt::CaseInsensitive)) {
                int pageIndex = item->whatsThis(0).toInt();

                if (!pageIndexList.contains(pageIndex)) {
                    pageIndexList << pageIndex;
                }
            }
        }

        // search in all labels
        Q_FOREACH (QLabel *widget, findChildren<QLabel *>()) {
            if (widget->text().contains(arg1, Qt::CaseInsensitive)) {
                addToSearchIndexList(widget, pageIndexList);
            }
        }

        // search in all push buttons
        Q_FOREACH (QPushButton *widget, findChildren<QPushButton *>()) {
            if (widget->text().contains(arg1, Qt::CaseInsensitive)) {
                addToSearchIndexList(widget, pageIndexList);
            }
        }

        // search in all checkboxes
        Q_FOREACH (QCheckBox *widget, findChildren<QCheckBox *>()) {
            if (widget->text().contains(arg1, Qt::CaseInsensitive)) {
                addToSearchIndexList(widget, pageIndexList);
            }
        }

        // search in all radio buttons
        Q_FOREACH (QRadioButton *widget, findChildren<QRadioButton *>()) {
            if (widget->text().contains(arg1, Qt::CaseInsensitive)) {
                addToSearchIndexList(widget, pageIndexList);
            }
        }

        // search in all group boxes
        Q_FOREACH (QGroupBox *widget, findChildren<QGroupBox *>()) {
            if (widget->title().contains(arg1, Qt::CaseInsensitive)) {
                addToSearchIndexList(widget, pageIndexList);
            }
        }

        // show and hide items according of if index was found in pageIndexList
        Q_FOREACH (QTreeWidgetItem *item, allItems) {
            // get stored index of list widget item
            int pageIndex = item->whatsThis(0).toInt();
            item->setHidden(!pageIndexList.contains(pageIndex));
        }

        // show items again that have visible children so that they are
        // really shown
        Q_FOREACH (QTreeWidgetItem *item, allItems) {
            if (Utils::Gui::isOneTreeWidgetItemChildVisible(item)) {
                item->setHidden(false);
                item->setExpanded(true);
            }
        }
    } else {
        // show all items otherwise
        Q_FOREACH (QTreeWidgetItem *item, allItems) { item->setHidden(false); }
    }
}

/**
 * Adds the page index of a widget to the pageIndexList if not already added
 *
 * @param widget
 * @param pageIndexList
 */
void SettingsDialog::addToSearchIndexList(QWidget *widget,
                                          QList<int> &pageIndexList) {
    // get the page id of the widget
    int pageIndex = findSettingsPageIndexOfWidget(widget);

    // add page id if not already added
    if (!pageIndexList.contains(pageIndex)) {
        pageIndexList << pageIndex;
    }
}

/**
 * Finds the settings page index of a widget
 *
 * @param widget
 * @return
 */
int SettingsDialog::findSettingsPageIndexOfWidget(QWidget *widget) {
    QWidget *parent = qobject_cast<QWidget *>(widget->parent());

    if (parent == Q_NULLPTR) {
        return -1;
    }

    // check if the parent is our settings stacked widget
    if (parent->objectName() == QLatin1String("settingsStackedWidget")) {
        // get the index of the object in the settings stacked widget
        return ui->settingsStackedWidget->indexOf(widget);
    }

    // search for the page id in the parent
    return findSettingsPageIndexOfWidget(parent);
}

/**
 * Removes the log file
 */
void SettingsDialog::on_clearLogFileButton_clicked() {
    // remove the log file
    removeLogFile();

    Utils::Gui::information(this, tr("Log file cleared"),
                            tr("The log file <strong>%1</strong> was cleared"
                               ".")
                                .arg(Utils::Misc::logFilePath()),
                            QStringLiteral("log-file-cleared"));
}

/**
 * Declares that we need a restart
 */
void SettingsDialog::needRestart() { Utils::Misc::needRestart(); }

/**
 * Opens a dialog to search for scripts in the script repository
 */
void SettingsDialog::searchScriptInRepository(bool checkForUpdates) {
    auto *dialog = new ScriptRepositoryDialog(this, checkForUpdates);
    dialog->exec();
    Script lastInstalledScript = dialog->getLastInstalledScript();
    delete (dialog);

    // reload the script list
    reloadScriptList();

    // select the last installed script
    if (lastInstalledScript.isFetched()) {
        auto item = Utils::Gui::getListWidgetItemWithUserData(
            ui->scriptListWidget, lastInstalledScript.getId());
        ui->scriptListWidget->setCurrentItem(item);
    }

    // reload the scripting engine
    ScriptingService::instance()->reloadEngine();

    // reload page so the script settings will be viewed
    reloadCurrentScriptPage();
}

/**
 * Opens a dialog to check for script updates
 */
void SettingsDialog::checkForScriptUpdates() { searchScriptInRepository(true); }

/**
 * Saves the enabled state of all items and reload the current script page to
 * make the script settings available when a script was enabled or disabled
 *
 * @param item
 */
void SettingsDialog::on_scriptListWidget_itemChanged(QListWidgetItem *item) {
    Q_UNUSED(item)

    storeScriptListEnabledState();
    reloadCurrentScriptPage();
}

void SettingsDialog::on_interfaceStyleComboBox_currentTextChanged(
    const QString &arg1) {
    QApplication::setStyle(arg1);

    // if the interface style was set to automatic we need a restart
    if (ui->interfaceStyleComboBox->currentIndex() == 0) {
        needRestart();
    }
}

/**
 * Reset the cursor width spin box value
 */
void SettingsDialog::on_cursorWidthResetButton_clicked() {
    ui->cursorWidthSpinBox->setValue(1);
}

/**
 * Also enable the single instance feature if the system tray icon is turned on
 */
void SettingsDialog::on_showSystemTrayCheckBox_toggled(bool checked) {
    // we don't need to do that on macOS
#ifndef Q_OS_MAC
    if (checked) {
        ui->allowOnlyOneAppInstanceCheckBox->setChecked(true);
    }
#endif

    ui->startHiddenCheckBox->setEnabled(checked);

    if (!checked) {
        ui->startHiddenCheckBox->setChecked(false);
    }
}

/**
 * Resets the overrides for all message boxes
 */
void SettingsDialog::on_resetMessageBoxesButton_clicked() {
    if (QMessageBox::question(
            this, tr("Reset message boxes"),
            tr("Do you really want to reset the overrides of all message "
               "boxes?")) == QMessageBox::Yes) {
        QSettings settings;

        // remove all settings in the group
        settings.beginGroup(QStringLiteral("MessageBoxOverride"));
        settings.remove(QLatin1String(""));
        settings.endGroup();
    }
}

void SettingsDialog::on_markdownHighlightingCheckBox_toggled(bool checked) {
    ui->markdownHighlightingFrame->setEnabled(checked);
}

void SettingsDialog::on_localTrashEnabledCheckBox_toggled(bool checked) {
    ui->localTrashGroupBox->setEnabled(checked);
}

void SettingsDialog::on_localTrashClearCheckBox_toggled(bool checked) {
    ui->localTrashClearFrame->setEnabled(checked);
}

void SettingsDialog::on_ignoreNoteSubFoldersResetButton_clicked() {
    ui->ignoreNoteSubFoldersLineEdit->setText(IGNORED_NOTE_SUBFOLDERS_DEFAULT);
}

void SettingsDialog::on_interfaceFontSizeSpinBox_valueChanged(int arg1) {
    QSettings settings;
    settings.setValue(QStringLiteral("interfaceFontSize"), arg1);
    Utils::Gui::updateInterfaceFontSize(arg1);
}

void SettingsDialog::on_overrideInterfaceFontSizeGroupBox_toggled(bool arg1) {
    QSettings settings;
    settings.setValue(QStringLiteral("overrideInterfaceFontSize"), arg1);
    Utils::Gui::updateInterfaceFontSize();
}

void SettingsDialog::on_webSocketServerServicePortResetButton_clicked() {
    ui->webSocketServerServicePortSpinBox->setValue(
        WebSocketServerService::getDefaultPort());
}

void SettingsDialog::on_enableSocketServerCheckBox_toggled() {
    bool checked = ui->enableSocketServerCheckBox->isChecked();
    ui->browserExtensionFrame->setEnabled(checked);
}

void SettingsDialog::on_internalIconThemeCheckBox_toggled(bool checked) {
    if (checked) {
        const QSignalBlocker blocker(ui->systemIconThemeCheckBox);
        Q_UNUSED(blocker)
        ui->systemIconThemeCheckBox->setChecked(false);
    }

    ui->systemIconThemeCheckBox->setDisabled(checked);
}

void SettingsDialog::on_systemIconThemeCheckBox_toggled(bool checked) {
    if (checked) {
        const QSignalBlocker blocker(ui->internalIconThemeCheckBox);
        ui->internalIconThemeCheckBox->setChecked(false);
    }

    ui->internalIconThemeCheckBox->setDisabled(checked);
    ui->darkModeIconThemeCheckBox->setDisabled(checked);
}

void SettingsDialog::on_webSocketTokenButton_clicked() {
    auto webSocketTokenDialog = new WebSocketTokenDialog();
    webSocketTokenDialog->exec();
    delete (webSocketTokenDialog);
}

void SettingsDialog::on_languageSearchLineEdit_textChanged(const QString &arg1) {
    Utils::Gui::searchForTextInListWidget(ui->languageListWidget, arg1, true);
}
