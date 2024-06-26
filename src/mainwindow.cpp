/*
 * Copyright (c) 2014-2021 Patrizio Bekerle -- <patrizio@bekerle.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 */

#include "mainwindow.h"

#include <dialogs/attachmentdialog.h>
#include <dialogs/dictionarymanagerdialog.h>
#include <dialogs/filedialog.h>
#include <dialogs/imagedialog.h>
#include <dialogs/notedialog.h>
#include <dialogs/tabledialog.h>
#include <dialogs/tagadddialog.h>
#include <dialogs/dropPDFDialog.h>
#include <diff_match_patch.h>
#include <helpers/toolbarcontainer.h>
#include <helpers/flowlayout.h>
#include <utils/gui.h>
#include <utils/misc.h>
#include <utils/schema.h>
#include <widgets/notetreewidgetitem.h>

#include <QAbstractEventDispatcher>
#include <QActionGroup>
#include <QClipboard>
#include <QColorDialog>
#include <QCompleter>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QInputDialog>
#include <QJSEngine>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMimeData>
#include <QPageSetupDialog>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QProgressDialog>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSystemTrayIcon>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextLength>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QWidgetAction>
#include <libraries/qttoolbareditor/src/toolbar_editor.hpp>
#include <utility>

#include "build_number.h"
#include "dialogs/aboutdialog.h"
#include "dialogs/commandbar.h"
#include "dialogs/linkdialog.h"
#include "dialogs/notediffdialog.h"
#include "dialogs/settingsdialog.h"
#include "helpers/pkbsuitemarkdownhighlighter.h"
#include "libraries/sonnet/src/core/speller.h"
#include "release.h"
#include "ui_mainwindow.h"
#include "version.h"
#include "widgets/pkbsuitemarkdowntextedit.h"
#include "utils/pdffile.h"
#include <QGraphicsDropShadowEffect>

#include "entities/tagmap.h"

QString MainWindow::notesPath = "";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {

    QSettings settings;
    _noteEditIsCentralWidget =
        settings.value(QStringLiteral("noteEditIsCentralWidget"), true)
            .toBool();

    ui->setupUi(this);

#if QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)
    ui->noteEditTabWidget->setTabBarAutoHide(true);
#endif
    ui->noteEditTabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->noteEditTabWidget->tabBar(), &QWidget::customContextMenuRequested,
            this, &MainWindow::showNoteEditTabWidgetContextMenu);

	setWindowIcon(getSystemTrayIcon());

    // initialize the workspace combo box
    initWorkspaceComboBox();

    _noteViewIsRegenerated = false;
    _searchLineEditFromCompleter = false;
    _isNotesDirectoryWasModifiedDisabled = false;
    _isNotesWereModifiedDisabled = false;
    _isDefaultShortcutInitialized = false;
    _noteExternallyRemovedCheckEnabled = true;
    _settingsDialog = Q_NULLPTR;
    _lastNoteSelectionWasMultiple = false;
    _closeEventWasFired = false;
    _leaveFullScreenModeButton = nullptr;
    _noteViewNeedsUpdate = false;

    this->setWindowTitle(QStringLiteral("PKbSuite - version ") +
                         QStringLiteral(VERSION) + QStringLiteral(" - build ") +
                         QString::number(BUILD));

    qApp->setProperty("mainWindow", QVariant::fromValue<MainWindow *>(this));

    auto *sorting = new QActionGroup(this);
    sorting->addAction(ui->actionAlphabetical);
    sorting->addAction(ui->actionBy_date);

    auto *sortingOrder = new QActionGroup(this);
    sortingOrder->addAction(ui->actionAscending);
    sortingOrder->addAction(ui->actionDescending);
    sortingOrder->setExclusive(true);

    // set the search frames for the note text edits
    bool darkMode = settings.value(QStringLiteral("darkMode")).toBool();
    ui->noteTextEdit->initSearchFrame(ui->noteTextEditSearchFrame, darkMode);
    ui->noteTextView->initSearchFrame(ui->noteTextViewSearchFrame, darkMode);

    // set the main window for accessing it's public methods
    ui->noteTextEdit->setMainWindow(this);

    // initialize the tag button scroll area
    initTagButtonScrollArea();

    noteHistory = NoteHistory();

    // initialize the toolbars
    initToolbars();

    // adding some alternate shortcuts for changing the current note
    auto *shortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+PgDown")), this);
    connect(shortcut, &QShortcut::activated, this,
            &MainWindow::on_actionNext_note_triggered);

    shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+PgUp")), this);
    connect(shortcut, &QShortcut::activated, this,
            &MainWindow::on_actionPrevious_Note_triggered);

    // read the settings (shortcuts have to be defined before that)
    readSettings();

    // do a bit more styling
    initStyling();

    // initialize the note graph
    initKbNoteMap();

    // initialize the dock widgets
    initDockWidgets();

    // restore toolbars
    // initDockWidgets() has to be called first so panel checkboxes can be
    // used in toolbars
    restoreToolbars();

    // update the workspace menu and combobox entries again after
    // restoreToolbars() to fill the workspace combo box again
    updateWorkspaceLists();

    // check if we want to start the application hidden
    initShowHidden();

    createSystemTrayIcon();

    buildNotesIndexAndLoadNoteDirectoryList();

    // setup the update available button
    setupStatusBarWidgets();

    this->noteDiffDialog = new NoteDiffDialog();

    // look if we need to save something every 10 sec (default)
    this->noteSaveTimer = new QTimer(this);
    connect(this->noteSaveTimer, &QTimer::timeout, this,
            &MainWindow::storeUpdatedNotesToDisk);

    this->noteSaveTimer->start(this->noteSaveIntervalTime * 1000);

    // look if we need update the note view every two seconds
    _noteViewUpdateTimer = new QTimer(this);
    _noteViewUpdateTimer->setSingleShot(true);
    connect(_noteViewUpdateTimer, &QTimer::timeout, this,
            &MainWindow::noteViewUpdateTimerSlot);

    _noteViewUpdateTimer->start(2000);

    QObject::connect(&this->noteDirectoryWatcher,
                     SIGNAL(directoryChanged(QString)), this,
                     SLOT(notesDirectoryWasModified(QString)));
    QObject::connect(&this->noteDirectoryWatcher, SIGNAL(fileChanged(QString)),
                     this, SLOT(notesWereModified(QString)));

    ui->searchLineEdit->installEventFilter(this);
    ui->noteTreeWidget->installEventFilter(this);
    ui->noteTextView->installEventFilter(this);
    ui->noteTextView->viewport()->installEventFilter(this);
    ui->noteTextEdit->installEventFilter(this);
    ui->noteTextEdit->viewport()->installEventFilter(this);
    ui->tagTreeWidget->installEventFilter(this);
    ui->newNoteTagLineEdit->installEventFilter(this);
    ui->selectedTagsToolButton->installEventFilter(this);

    // init the saved searches completer
    initSavedSearchesCompleter();

    // ignores note clicks in QMarkdownTextEdit in the note text edit
    ui->noteTextEdit->setIgnoredClickUrlSchemata(QStringList({"note", "task"}));

    // handle note url externally in the note text edit
    connect(ui->noteTextEdit, &PKbSuiteMarkdownTextEdit::urlClicked, this,
            &MainWindow::openLocalUrl);

    // handle note edit zooming
    connect(ui->noteTextEdit, &PKbSuiteMarkdownTextEdit::zoomIn, this,
            &MainWindow::on_action_Increase_note_text_size_triggered);
    connect(ui->noteTextEdit, &PKbSuiteMarkdownTextEdit::zoomOut, this,
            &MainWindow::on_action_Decrease_note_text_size_triggered);

    // handle note text edit resize events
    connect(ui->noteTextEdit, &PKbSuiteMarkdownTextEdit::resize, this,
            &MainWindow::noteTextEditResize);

    // set the tab stop to the width of 4 spaces in the editor
    const int tabStop = 4;
    QFont font = ui->noteTextEdit->font();
    QFontMetrics metrics(font);

#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
    int width = tabStop * metrics.width(' ');
    ui->noteTextEdit->setTabStopWidth(width);
#else
    int width = tabStop * metrics.horizontalAdvance(' ');
    ui->noteTextEdit->setTabStopDistance(width);
#endif

    // called now in readSettingsFromSettingsDialog() line 494
    // set the edit mode for the note text edit
    // this->setNoteTextEditMode(true);

    // update panels sort and order
    updatePanelsSortOrder();

    // we need to restore the current workspace a little later when
    // application window is maximized or in full-screen mode
    if (isMaximized() || isFullScreen()) {
        // if it is in distraction mode we restore it immediately
        // otherwise it can result in mixed state
        if (isInDistractionFreeMode()) {
            restoreCurrentWorkspace();
        } else {
            QTimer::singleShot(500, this, SLOT(restoreCurrentWorkspace()));
        }
    }

    // setup the shortcuts for the note bookmarks
    setupNoteBookmarkShortcuts();

    // restore the distraction free mode
    restoreDistractionFreeMode();

    // add action tracking
    connect(ui->menuBar, &QMenuBar::triggered, this, &MainWindow::trackAction);

    // set "show toolbar" menu item checked/unchecked
    const QSignalBlocker blocker(ui->actionShow_toolbar);
    {
        Q_UNUSED(blocker)
        ui->actionShow_toolbar->setChecked(isToolbarVisible());
    }

    const auto toolbars = findChildren<QToolBar *>();
    for (QToolBar *toolbar : toolbars) {
        connect(toolbar, &QToolBar::visibilityChanged, this,
                &MainWindow::toolbarVisibilityChanged);
    }

    // set the action group for the width selector of the distraction free mode
    auto *dfmEditorWidthActionGroup = new QActionGroup(this);
    dfmEditorWidthActionGroup->addAction(ui->actionEditorWidthNarrow);
    dfmEditorWidthActionGroup->addAction(ui->actionEditorWidthMedium);
    dfmEditorWidthActionGroup->addAction(ui->actionEditorWidthWide);
    dfmEditorWidthActionGroup->addAction(ui->actionEditorWidthFull);
    dfmEditorWidthActionGroup->addAction(ui->actionEditorWidthCustom);
    dfmEditorWidthActionGroup->setExclusive(true);

    connect(dfmEditorWidthActionGroup, &QActionGroup::triggered, this,
            &MainWindow::dfmEditorWidthActionTriggered);

    setAcceptDrops(true);

    // act on position clicks in the navigation widget
    connect(ui->navigationWidget, &NavigationWidget::positionClicked, this,
            &MainWindow::onNavigationWidgetPositionClicked);

    // do the navigation parsing after the highlighter was finished
    connect(ui->noteTextEdit->highlighter(),
            &PKbSuiteMarkdownHighlighter::highlightingFinished, this,
            &MainWindow::startNavigationParser);

    ui->navigationFrame->setParent(ui->noteTextEdit);
    QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect();
    effect->setBlurRadius(5);
    effect->setOffset(2);
    ui->navigationFrame->setGraphicsEffect(effect);

    // act on note preview resize
    connect(ui->noteTextView, &NotePreviewWidget::resize, this,
            &MainWindow::onNoteTextViewResize);

    // setup the soft-wrap checkbox
    const QSignalBlocker blocker2(ui->actionUse_softwrap_in_note_editor);
    Q_UNUSED(blocker2)
    ui->actionUse_softwrap_in_note_editor->setChecked(
        settings.value(QStringLiteral("useSoftWrapInNoteEditor"), true)
            .toBool());
	
    // initialize the editor soft wrapping
    initEditorSoftWrap();

    _storedAttachmentsDialog = Q_NULLPTR;

    // track cursor position changes for the line number label
    connect(ui->noteTextEdit, &PKbSuiteMarkdownTextEdit::cursorPositionChanged,
            this, &MainWindow::noteEditCursorPositionChanged);

    // restore the note tabs
    Utils::Gui::restoreNoteTabs(ui->noteEditTabWidget,
                                ui->noteEditTabWidgetLayout);

    if (isInDistractionFreeMode()) {
        ui->noteEditTabWidget->tabBar()->hide();
    }

    // restore the note history
    noteHistory.restore();
	
	// initialize the note preview button. State is updated afterwards.
	ui->actionShow_Preview_Panel->setChecked(_notePreviewDockWidget->isVisible());

    // initialize the note graph button. State is updated afterwards.
	ui->actionShow_Note_Graph->setChecked(_graphDockWidget->isVisible());

    if (settings.value(QStringLiteral("restoreLastNoteAtStartup"), true)
            .toBool()) {
        // try to restore the last note before the app was quit
        // if that fails jump to the first note
        // we do that with a timer, because otherwise the scrollbar will not be
        // restored correctly, because the maximum position of the scrollbar is
        // 0
        QTimer::singleShot(250, this, SLOT(restoreActiveNoteHistoryItem()));
    }

    // wait some time for the tagTree to get visible, if selected, and apply
    // last selected tag search
    QTimer::singleShot(250, this, SLOT(filterNotesByTag()));

    // attempt to quit the application when a logout is initiated
    connect(qApp, &QApplication::commitDataRequest, this,
            &MainWindow::on_action_Quit_triggered);
}

QString MainWindow::getNotePath() {return notesPath;}

/**
 * Initializes the global shortcuts
 */
void MainWindow::initGlobalKeyboardShortcuts() {
    // deleting old global shortcut assignments
    foreach(QHotkey *hotKey, _globalShortcuts) {
        delete hotKey;
    }

    _globalShortcuts.clear();
    QSettings settings;
    settings.beginGroup(QStringLiteral("GlobalShortcuts"));

    foreach(const QString &key, settings.allKeys()) {
        if (!key.contains(QStringLiteral("MainWindow"))) {
            continue;
        }

        QString actionName = key;
        actionName.remove(QStringLiteral("MainWindow-"));
        QAction *action = findAction(actionName);
        QString shortcut = settings.value(key).toString();

        auto hotKey = new QHotkey(QKeySequence(shortcut), true, this);
        _globalShortcuts.append(hotKey);
        connect(hotKey, &QHotkey::activated, this, [this, action]() {
            qDebug() << "Global shortcut action triggered: " << action->objectName();

            // Don't call showWindow() for the "Show/Hide application" action
            // because it will call it itself
            if (action->objectName() != "actionShow_Hide_application") {
                // bring application window to the front
                showWindow();
            }

            action->trigger();
        });
    }
}

/**
 * Restores the active note history item
 */
bool MainWindow::restoreActiveNoteHistoryItem() {
    QSettings settings;
    QVariant var = settings.value(QStringLiteral("ActiveNoteHistoryItem"));
    //    qDebug() << __func__ << " - 'var': " << var;

    // check if the NoteHistoryItem could be de-serialized
    if (var.isValid()) {
        NoteHistoryItem noteHistoryItem = var.value<NoteHistoryItem>();
        //        qDebug() << __func__ << " - 'noteHistoryItem': " <<
        //        noteHistoryItem;

        if (jumpToNoteHistoryItem(noteHistoryItem)) {
            noteHistoryItem.restoreTextEditPosition(ui->noteTextEdit);
            reloadCurrentNoteTags();
            return true;
        }
    }

    // if restoring the last note failed jump to the first note
    resetCurrentNote();

    reloadCurrentNoteTags();

    return false;
}

MainWindow::~MainWindow() {
    disableFullScreenMode();

    const bool forceQuit = qApp->property("clearAppDataAndExit").toBool();

    // make sure no settings get written after we got the
    // clearAppDataAndExit call
    if (!forceQuit) {
        storeSettings();
    }

    if (!isInDistractionFreeMode() && !forceQuit && !_closeEventWasFired) {
        storeCurrentWorkspace();
    }

    storeUpdatedNotesToDisk();

    delete ui;
}

/*!
 * Methods
 */

/**
 * Initializes the workspace combo box
 */
void MainWindow::initWorkspaceComboBox() {
    _workspaceComboBox = new QComboBox(this);
    connect(
        _workspaceComboBox,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, &MainWindow::onWorkspaceComboBoxCurrentIndexChanged);
    _workspaceComboBox->setToolTip(tr("Workspaces"));
    _workspaceComboBox->setObjectName(QStringLiteral("workspaceComboBox"));
}

/**
 * Initializes the dock widgets
 */
void MainWindow::initDockWidgets() {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    setDockOptions(dockOptions() | GroupedDragging);
#endif
    QSizePolicy sizePolicy;

    _taggingDockWidget = new QDockWidget(tr("Tags"), this);
    _taggingDockWidget->setObjectName(QStringLiteral("taggingDockWidget"));
    _taggingDockWidget->setWidget(ui->tagFrame);
    _taggingDockTitleBarWidget = _taggingDockWidget->titleBarWidget();
    sizePolicy = _taggingDockWidget->sizePolicy();
    sizePolicy.setHorizontalStretch(2);
    _taggingDockWidget->setSizePolicy(sizePolicy);
    addDockWidget(Qt::LeftDockWidgetArea, _taggingDockWidget, Qt::Vertical);

    _noteSearchDockWidget = new QDockWidget(tr("Note search"), this);
    _noteSearchDockWidget->setObjectName(
        QStringLiteral("noteSearchDockWidget"));
    _noteSearchDockWidget->setWidget(ui->searchLineEdit);
    _noteSearchDockTitleBarWidget = _noteSearchDockWidget->titleBarWidget();
    sizePolicy = _noteSearchDockWidget->sizePolicy();
    sizePolicy.setHorizontalStretch(2);
    _noteSearchDockWidget->setSizePolicy(sizePolicy);
    addDockWidget(Qt::LeftDockWidgetArea, _noteSearchDockWidget, Qt::Vertical);

    _noteListDockWidget = new QDockWidget(tr("Note list"), this);
    _noteListDockWidget->setObjectName(QStringLiteral("noteListDockWidget"));
    _noteListDockWidget->setWidget(ui->notesListFrame);
    _noteListDockTitleBarWidget = _noteListDockWidget->titleBarWidget();
    sizePolicy = _noteListDockWidget->sizePolicy();
    sizePolicy.setHorizontalStretch(2);
    _noteListDockWidget->setSizePolicy(sizePolicy);
    addDockWidget(Qt::LeftDockWidgetArea, _noteListDockWidget, Qt::Vertical);

    if (!_noteEditIsCentralWidget) {
        _noteEditDockWidget = new QDockWidget(tr("Note edit"), this);
        _noteEditDockWidget->setObjectName(
            QStringLiteral("noteEditDockWidget"));
        _noteEditDockWidget->setWidget(ui->noteEditTabWidget);
        _noteEditDockTitleBarWidget = _noteEditDockWidget->titleBarWidget();
        sizePolicy = _noteEditDockWidget->sizePolicy();
        sizePolicy.setHorizontalStretch(5);
        _noteEditDockWidget->setSizePolicy(sizePolicy);
        addDockWidget(Qt::RightDockWidgetArea, _noteEditDockWidget,
                      Qt::Horizontal);
    }

    _noteTagDockWidget = new QDockWidget(tr("Note tags"), this);
    _noteTagDockWidget->setObjectName(QStringLiteral("noteTagDockWidget"));
    _noteTagDockWidget->setWidget(ui->noteTagFrame);
    _noteTagDockTitleBarWidget = _noteTagDockWidget->titleBarWidget();
    sizePolicy = _noteTagDockWidget->sizePolicy();
    sizePolicy.setHorizontalStretch(5);
    _noteTagDockWidget->setSizePolicy(sizePolicy);
    addDockWidget(_noteEditIsCentralWidget ? Qt::LeftDockWidgetArea
                                           : Qt::RightDockWidgetArea,
                  _noteTagDockWidget, Qt::Vertical);

    _notePreviewDockWidget = new QDockWidget(tr("Note preview"), this);
    _notePreviewDockWidget->setObjectName(
        QStringLiteral("notePreviewDockWidget"));
    _notePreviewDockWidget->setWidget(ui->noteViewFrame);
    _notePreviewDockTitleBarWidget = _notePreviewDockWidget->titleBarWidget();
    addDockWidget(Qt::RightDockWidgetArea, _notePreviewDockWidget,
                  Qt::Horizontal);

    _graphDockWidget = new QDockWidget(tr("Note graph"), this);
    _graphDockWidget->setObjectName(QStringLiteral("graphDockWidget"));
    _graphDockWidget->setWidget(ui->kbGraphFrame);
    _graphDockTitleBarWidget = _graphDockWidget->titleBarWidget();
    addDockWidget(Qt::RightDockWidgetArea, _graphDockWidget,
                  Qt::Horizontal);

    QSettings settings;

    // forcing some dock widget sizes on the first application start
    if (!settings.value(QStringLiteral("dockWasInitializedOnce")).toBool()) {
        // setting a small height for the note tag panel
        _noteTagDockWidget->setMaximumHeight(40);

        // giving the left panels with the note list a fifth of the screen
        _noteListDockWidget->setMaximumWidth(width() / 5);

        // giving the preview pane a third of the screen, the rest goes to the
        // note edit pane
        _notePreviewDockWidget->setMaximumWidth(width() / 3);
        _graphDockWidget->setMaximumWidth(width() / 3);

        settings.setValue(QStringLiteral("dockWasInitializedOnce"), true);

        // releasing the forced maximum sizes
        QTimer::singleShot(250, this, SLOT(releaseDockWidgetSizes()));
    }

    //    ui->noteEditTabWidget->setStyleSheet("* { border: none; }");
    //    ui->noteTextEdit->setStyleSheet("* { border: none; }");
    //    ui->noteEditTabWidget->layout()->setContentsMargins(0, 0, 0, 0);

    setDockNestingEnabled(true);
    setCentralWidget(_noteEditIsCentralWidget ? ui->noteEditTabWidget : Q_NULLPTR);

    // macOS and Windows will look better without this
#ifdef Q_OS_LINUX
    if (_noteEditIsCentralWidget) {
        ui->noteTextEdit->setFrameShape(QFrame::StyledPanel);
    }
#endif

    // restore the current workspace
    restoreCurrentWorkspace();

    // lock the dock widgets
    on_actionUnlock_panels_toggled(false);

    // update the workspace menu and combobox entries
    updateWorkspaceLists();

    // initialize the panel menu
    initPanelMenu();

    // initialize the KB graph
    ui->kbGraphView->setMainWindowPtr(this);
    ui->kbGraphView->GenerateKBGraph();
}

/**
 * Releasing the forced maximum sizes on some dock widgets
 */
void MainWindow::releaseDockWidgetSizes() {
    _noteListDockWidget->setMaximumWidth(10000);
    _notePreviewDockWidget->setMaximumWidth(10000);
    _graphDockWidget->setMaximumWidth(10000);
    _noteTagDockWidget->setMaximumHeight(10000);
}

/**
 * Initializes if we want to start the application hidden
 */
void MainWindow::initShowHidden() {
    QSettings settings;
    const bool startHidden =
        settings.value(QStringLiteral("StartHidden"), false).toBool();
    if (startHidden) {
        QTimer::singleShot(250, this, SLOT(hide()));
    }
}

/**
 * Initializes the tag button scroll area
 *
 * If there are more tags assigned to a note than the width of the edit
 * pane allows there now will be used a scrollbar to scroll through the
 * tags, so that the width of the edit pane can still be small
 */
void MainWindow::initTagButtonScrollArea() {
    _noteTagButtonScrollArea = new QScrollArea(this);
    _noteTagButtonScrollArea->setWidgetResizable(true);
    _noteTagButtonScrollArea->setSizePolicy(QSizePolicy::MinimumExpanding,
                                            QSizePolicy::Ignored);
    _noteTagButtonScrollArea->setAlignment(Qt::AlignLeft);
    _noteTagButtonScrollArea->setWidget(ui->noteTagButtonFrame);

    ui->noteTagButtonFrame->layout()->setContentsMargins(0, 0, 0, 0);
    _noteTagButtonScrollArea->setContentsMargins(0, 0, 0, 0);

#ifdef Q_OS_MAC
    // we need to set a minimum height under OS X or else the scroll area
    // will be far to high
    _noteTagButtonScrollArea->setMinimumHeight(36);
#endif
#ifdef Q_OS_WIN32
    // we need to set a minimum height under Windows or else the scroll area
    // will be far to high
    _noteTagButtonScrollArea->setMinimumHeight(40);
#endif

    ui->noteTagFrame->layout()->addWidget(_noteTagButtonScrollArea);
    ui->noteTagFrame->layout()->addWidget(ui->newNoteTagButton);
    ui->noteTagFrame->layout()->addWidget(ui->newNoteTagLineEdit);
    ui->selectedTagsToolButton->setVisible(false);
}

/**
 * Returns all menus from the menu bar
 */
QList<QMenu *> MainWindow::menuList() {
    return ui->menuBar->findChildren<QMenu *>();
}

/**
 * Finds an action in all menus of the menu bar
 */
QAction *MainWindow::findAction(const QString &objectName) {
    const QList<QMenu *> menus = menuList();

    // loop through all menus because we were not able to find the action with
    // ui->menuBar->findChild<QAction *>(objectName);
    for (QMenu *menu : menus) {
        // loop through all actions of the menu
        const auto menuActions = menu->actions();
        for (QAction *action : menuActions) {
            if (action->objectName() == objectName) {
                return action;
            }
        }
    }

    return Q_NULLPTR;
}

/**
 * Initialize the memory QMap to manage the graph of Notes
 */
void MainWindow::initKbNoteMap() {
    _kbNoteMap = NoteMap::getInstance();

    QString notePath = Utils::Misc::removeIfEndsWith(this->notesPath, QDir::separator());
    _kbNoteMap->createNoteList(notePath);
}

NoteMap* MainWindow::getNoteMap() {
    return _kbNoteMap;
}

/**
 * Builds the note index and loads the note directory list
 *
 * @param forceBuild
 * @param forceLoad
 */
void MainWindow::buildNotesIndexAndLoadNoteDirectoryList(bool forceBuild,
                                                         bool forceLoad) {
    const bool wasBuilt = buildNotesIndex(0, forceBuild);

    if (wasBuilt || forceLoad) {
        loadNoteDirectoryList();
    }
}

/**
 * Returns the global main window instance
 */
MainWindow *MainWindow::instance() {
    return qApp ? qApp->property("mainWindow").value<MainWindow *>() : nullptr;
}

/**
 * Initializes the editor soft wrapping
 */
void MainWindow::initEditorSoftWrap() {
    QSettings settings;
    const bool useSoftWrapInNoteEditor =
        settings.value(QStringLiteral("useSoftWrapInNoteEditor"), true)
            .toBool();

    QTextEdit::LineWrapMode mode =
        useSoftWrapInNoteEditor ? QTextEdit::WidgetWidth : QTextEdit::NoWrap;
    QPlainTextEdit::LineWrapMode pMode = useSoftWrapInNoteEditor
                                             ? QPlainTextEdit::WidgetWidth
                                             : QPlainTextEdit::NoWrap;

    ui->noteTextEdit->setLineWrapMode(pMode);
    ui->noteTextView->setLineWrapMode(mode);
}

/**
 * Initializes the toolbars
 */
void MainWindow::initToolbars() {
    _formattingToolbar = new QToolBar(tr("formatting toolbar"), this);
    _formattingToolbar->addAction(ui->actionFormat_text_bold);
    _formattingToolbar->addAction(ui->actionFormat_text_italic);
    _formattingToolbar->addAction(ui->actionStrike_out_text);
    _formattingToolbar->addAction(ui->actionInsert_code_block);
    _formattingToolbar->addAction(ui->actionInsert_block_quote);
    _formattingToolbar->setObjectName(QStringLiteral("formattingToolbar"));
    addToolBar(_formattingToolbar);

    _insertingToolbar = new QToolBar(tr("inserting toolbar"), this);
    _insertingToolbar->addAction(ui->actionInsert_text_link);
    _insertingToolbar->addAction(ui->actionInsert_image);
    _insertingToolbar->addAction(ui->actionInsert_current_time);
    _insertingToolbar->setObjectName(QStringLiteral("insertingToolbar"));
    addToolBar(_insertingToolbar);

    _windowToolbar = new QToolBar(tr("window toolbar"), this);
    updateWindowToolbar();
    _windowToolbar->setObjectName(QStringLiteral("windowToolbar"));
    addToolBar(_windowToolbar);
}

/**
 * Populates the window toolbar
 */
void MainWindow::updateWindowToolbar() {
    _windowToolbar->clear();

    auto *widgetAction = new QWidgetAction(this);
    widgetAction->setDefaultWidget(_workspaceComboBox);
    widgetAction->setObjectName(QStringLiteral("actionWorkspaceComboBox"));
    widgetAction->setText(tr("Workspace selector"));
    _windowToolbar->addAction(widgetAction);
    _windowToolbar->addAction(ui->actionStore_as_new_workspace);
    _windowToolbar->addAction(ui->actionRemove_current_workspace);
    _windowToolbar->addAction(ui->actionRename_current_workspace);
    _windowToolbar->addAction(ui->actionSwitch_to_previous_workspace);
    _windowToolbar->addAction(ui->actionUnlock_panels);

    _windowToolbar->addSeparator();
    _windowToolbar->addAction(ui->actionToggle_distraction_free_mode);
    _windowToolbar->addAction(ui->action_Increase_note_text_size);
    _windowToolbar->addAction(ui->action_Decrease_note_text_size);
    _windowToolbar->addAction(ui->action_Reset_note_text_size);
}

/**
 * Updates the workspace menu and combobox entries
 */
void MainWindow::updateWorkspaceLists(bool rebuild) {
    QSettings settings;
    const QStringList workspaces = getWorkspaceUuidList();
    const QString currentUuid = currentWorkspaceUuid();

    if (rebuild) {
        // we need to create a new combo box so the width gets updated in the
        // window toolbar
        initWorkspaceComboBox();

        ui->menuWorkspaces->clear();

        _workspaceNameUuidMap.clear();
    }

    const QSignalBlocker blocker(_workspaceComboBox);
    Q_UNUSED(blocker)

    int currentIndex = 0;

    for (int i = 0; i < workspaces.count(); i++) {
        const QString &uuid = workspaces.at(i);

        if (uuid == currentUuid) {
            currentIndex = i;
        }

        // check if we want to skip the rebuilding part
        if (!rebuild) {
            continue;
        }

        const QString name = settings
                                 .value(QStringLiteral("workspace-") + uuid +
                                        QStringLiteral("/name"))
                                 .toString();
        const QString objectName = QStringLiteral("restoreWorkspace-") + uuid;

        _workspaceNameUuidMap.insert(name, uuid);

        _workspaceComboBox->addItem(name, uuid);

        auto *action = new QAction(name, ui->menuWorkspaces);
        connect(action, &QAction::triggered, this,
                [this, uuid]() { setCurrentWorkspace(uuid); });

        // set an object name for creating shortcuts
        action->setObjectName(objectName);

        // try to load a key sequence from the settings
        QKeySequence shortcut = QKeySequence(
            settings.value(QStringLiteral("Shortcuts/MainWindow-") + objectName)
                .toString());
        action->setShortcut(shortcut);

        //        if (uuid == currentUuid) {
        //            QFont font = action->font();
        //            font.setBold(true);
        //            action->setFont(font);
        //        }

        ui->menuWorkspaces->addAction(action);
    }

    _workspaceComboBox->setCurrentIndex(currentIndex);

    if (rebuild) {
        // we need to adapt the width of the workspaces combo box
        updateWindowToolbar();
    }

    // enable the remove button if there are at least two workspaces
    ui->actionRemove_current_workspace->setEnabled(workspaces.count() > 1);
}

/**
 * Initializes the panel menu
 */
void MainWindow::initPanelMenu() {
    // update the panel menu if the visibility of a panel was changed
    const auto dockWidgets = findChildren<QDockWidget *>();
    for (QDockWidget *dockWidget : dockWidgets) {
        // seems to crash the application on exit
        //        connect(dockWidget, &QDockWidget::visibilityChanged, this,
        //        [this](){
        //            updatePanelMenu();
        //        });

        // this connect works without crash, it doesn't seem to trigger on exit
        QObject::connect(dockWidget, SIGNAL(visibilityChanged(bool)), this,
                         SLOT(updatePanelMenu()));

        dockWidget->setContextMenuPolicy(Qt::PreventContextMenu);
    }
}

/**
 * Initializes the toolbar menu
 */
void MainWindow::initToolbarMenu() {
    // update the toolbar menu if the visibility of a toolbar was changed
    const auto toolbars = findChildren<QToolBar *>();
    for (QToolBar *toolbar : toolbars) {
        // in case the connection was already established
        QObject::disconnect(toolbar, &QToolBar::visibilityChanged, this,
                            &MainWindow::updateToolbarMenu);
        QObject::connect(toolbar, &QToolBar::visibilityChanged, this,
                         &MainWindow::updateToolbarMenu);

        toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    }
}

/**
 * Updates the panel menu entries
 */
void MainWindow::updatePanelMenu() {
    qDebug() << __func__ << " - 'updatePanelMenu'";

    ui->menuPanels->clear();
    QSettings settings;

    const auto dockWidgets = findChildren<QDockWidget *>();
    for (QDockWidget *dockWidget : dockWidgets) {
        auto *action = new QAction(this);
        const QString objectName =
            QStringLiteral("togglePanel-") + dockWidget->objectName();

        action->setText(tr("Show %1 panel").arg(dockWidget->windowTitle()));
        action->setObjectName(objectName);
        action->setCheckable(true);
        action->setChecked(!dockWidget->isHidden());

        // try to load a key sequence from the settings
        QKeySequence shortcut = QKeySequence(
            settings.value(QStringLiteral("Shortcuts/MainWindow-") + objectName)
                .toString());
        action->setShortcut(shortcut);

        // toggle the panel if the checkbox was triggered
        connect(action, &QAction::triggered, this, [this, dockWidget]() {
            togglePanelVisibility(dockWidget->objectName());
            updateJumpToActionsAvailability();
        });

        ui->menuPanels->addAction(action);
    }

    updateJumpToActionsAvailability();

    // update the preview in case it was disable previously
    if (_notePreviewDockWidget->isVisible()) {
        setNoteTextFromNote(&_currentNote, true);
		ui->actionShow_Preview_Panel->setChecked(true);
    }
}

/**
 * Updates the toolbar menu entries
 */
void MainWindow::updateToolbarMenu() {
    ui->menuToolbars->clear();

    const auto toolbars = findChildren<QToolBar*>();
    for (QToolBar *toolbar : toolbars) {
        auto *action = new QAction(this);
        action->setText(tr("Show %1").arg(toolbar->windowTitle()));
        action->setObjectName(QStringLiteral("toggleToolBar-") +
                              toolbar->objectName());
        action->setCheckable(true);
        action->setChecked(!toolbar->isHidden());

        // toggle the panel if the checkbox was triggered
        connect(action, &QAction::triggered, this, [this, toolbar]() {
            toggleToolbarVisibility(toolbar->objectName());
        });

        ui->menuToolbars->addAction(action);
    }
}

/**
 * Toggles the visibility of a panel by object name
 *
 * @param objectName
 */
void MainWindow::togglePanelVisibility(const QString &objectName) {
    auto *dockWidget = findChild<QDockWidget *>(objectName);

    if (dockWidget == Q_NULLPTR) {
        return;
    }

    // to prevent crashes if updatePanelMenu removes all actions
    const QSignalBlocker blocker(dockWidget);
    Q_UNUSED(blocker)

    bool newVisibility = dockWidget->isHidden();

    dockWidget->setVisible(newVisibility);

    // filter notes again according to new widget state
    filterNotes();
}

/**
 * Toggles the visibility of a toolbar by object name
 *
 * @param objectName
 */
void MainWindow::toggleToolbarVisibility(const QString &objectName) {
    auto *toolbar = findChild<QToolBar *>(objectName);

    if (toolbar == Q_NULLPTR) {
        return;
    }

    // to prevent crashes if updateToolbarMenu removes all actions
    const QSignalBlocker blocker(toolbar);
    Q_UNUSED(blocker)

    const bool newVisibility = toolbar->isHidden();
    toolbar->setVisible(newVisibility);
}

/**
 * Restores the distraction free mode
 */
void MainWindow::restoreDistractionFreeMode() {
    if (isInDistractionFreeMode()) {
        setDistractionFreeMode(true);
    }
}

/**
 * Checks if we are in distraction free mode
 */
bool MainWindow::isInDistractionFreeMode() {
    QSettings settings;
    return settings.value(QStringLiteral("DistractionFreeMode/isEnabled"))
        .toBool();
}

/**
 * Toggles the distraction free mode
 */
void MainWindow::toggleDistractionFreeMode() {
    QSettings settings;
    bool isInDistractionFreeMode = this->isInDistractionFreeMode();

    qDebug() << __func__
             << " - 'isInDistractionFreeMode': " << isInDistractionFreeMode;

    // store the window settings before we go into distraction free mode
    if (!isInDistractionFreeMode) {
        storeSettings();
    }

    isInDistractionFreeMode = !isInDistractionFreeMode;

    // remember that we were using the distraction free mode
    settings.setValue(QStringLiteral("DistractionFreeMode/isEnabled"),
                      isInDistractionFreeMode);

    setDistractionFreeMode(isInDistractionFreeMode);
}

/**
 * Does some basic styling
 */
void MainWindow::initStyling() {
    QSettings settings;
    const bool darkMode = settings.value(QStringLiteral("darkMode")).toBool();
    QString appStyleSheet;
    QString noteTagFrameColorName;

    // turn on the dark mode if enabled
    if (darkMode) {
        QFile f(QStringLiteral(":qdarkstyle/style.qss"));
        if (!f.exists()) {
            qWarning("Unable to set stylesheet, file not found!");
        } else {
            f.open(QFile::ReadOnly | QFile::Text);
            QTextStream ts(&f);
            appStyleSheet = ts.readAll();
        }

        // QTextEdit background color of qdarkstyle
        noteTagFrameColorName = QStringLiteral("#201F1F");
    } else {
        QPalette palette;
        const QColor &color = palette.color(QPalette::Base);
        noteTagFrameColorName = color.name();
    }

    // get the color name of the background color of the default text
    // highlighting item
    const QString fgColorName =
        Utils::Schema::schemaSettings
            ->getForegroundColor(MarkdownHighlighter::HighlighterState::NoState)
            .name();
    const QString bgColorName =
        Utils::Schema::schemaSettings
            ->getBackgroundColor(MarkdownHighlighter::HighlighterState::NoState)
            .name();

    // set the foreground and background color for the note text edits
    appStyleSheet +=
        QStringLiteral("QMarkdownTextEdit{color:%1;background-color:%2;}")
            .arg(fgColorName, bgColorName);

    // set the background color for the note tag frame and its children QFrames
    appStyleSheet += QStringLiteral(
                         "QFrame#noteTagFrame, QFrame#noteTagFrame QFrame "
                         "{background-color: %1;}")
                         .arg(noteTagFrameColorName);

    qApp->setStyleSheet(appStyleSheet);
    Utils::Gui::updateInterfaceFontSize();

    if (!isInDistractionFreeMode()) {
        ui->noteTextEdit->setPaperMargins(0);
    }

    // move the note view scrollbar when the note edit scrollbar was moved
    connect(ui->noteTextEdit->verticalScrollBar(), SIGNAL(valueChanged(int)),
            this, SLOT(noteTextSliderValueChanged(int)));

    // move the note edit scrollbar when the note view scrollbar was moved
    connect(ui->noteTextView->verticalScrollBar(), SIGNAL(valueChanged(int)),
            this, SLOT(noteViewSliderValueChanged(int)));
}

/**
 * Moves the note view scrollbar when the note edit scrollbar was moved
 */
void MainWindow::noteTextSliderValueChanged(int value, bool force) {
    // don't react if note text edit doesn't have the focus
    if (!activeNoteTextEdit()->hasFocus() && !force) {
        return;
    }

    QScrollBar *editScrollBar = activeNoteTextEdit()->verticalScrollBar();
    QScrollBar *viewScrollBar = ui->noteTextView->verticalScrollBar();

    const float editScrollFactor =
        static_cast<float>(value) / editScrollBar->maximum();
    const int viewPosition =
        static_cast<int>(viewScrollBar->maximum() * editScrollFactor);

    // set the scroll position in the note text view
    viewScrollBar->setSliderPosition(viewPosition);
}

/**
 * Moves the note edit scrollbar when the note view scrollbar was moved
 */
void MainWindow::noteViewSliderValueChanged(int value, bool force) {
    // don't react if note text view doesn't have the focus
    if (!ui->noteTextView->hasFocus() && !force) {
        return;
    }

    QScrollBar *editScrollBar = activeNoteTextEdit()->verticalScrollBar();
    QScrollBar *viewScrollBar = ui->noteTextView->verticalScrollBar();

    const float editScrollFactor =
        static_cast<float>(value) / viewScrollBar->maximum();

    const int editPosition =
        static_cast<int>(editScrollBar->maximum() * editScrollFactor);

    // for some reason we get some int-min value here sometimes
    if (editPosition < 0) {
        return;
    }

    // set the scroll position in the note text edit
    editScrollBar->setSliderPosition(editPosition);
}

/**
 * Enables or disables the distraction free mode
 */
void MainWindow::setDistractionFreeMode(const bool enabled) {
    QSettings settings;

    if (enabled) {
        //
        // enter the distraction free mode
        //

        // turn off line numbers because they would look broken in dfm
        ui->noteTextEdit->setLineNumberEnabled(false);

        // store the current workspace in case we changed something
        storeCurrentWorkspace();

        const bool menuBarWasVisible =
            settings
                .value(QStringLiteral("showMenuBar"), !ui->menuBar->isHidden())
                .toBool();

        // set the menu bar visible so we get the correct height
        if (!menuBarWasVisible) {
            ui->menuBar->setVisible(true);
        }

        // remember states, geometry and sizes
        settings.setValue(QStringLiteral("DistractionFreeMode/windowState"),
                          saveState());
        settings.setValue(QStringLiteral("DistractionFreeMode/menuBarGeometry"),
                          ui->menuBar->saveGeometry());
        settings.setValue(QStringLiteral("DistractionFreeMode/menuBarHeight"),
                          ui->menuBar->height());
        settings.setValue(QStringLiteral("DistractionFreeMode/menuBarVisible"),
                          menuBarWasVisible);

        // we must not hide the menu bar or else the shortcuts
        // will not work any more
        ui->menuBar->setFixedHeight(0);

        // hide the toolbars
        const QList<QToolBar *> toolbars = findChildren<QToolBar *>();
        for (QToolBar *toolbar : toolbars) {
            toolbar->hide();
        }

        if (!_noteEditIsCentralWidget) {
            // show the note edit dock widget
            _noteEditDockWidget->show();
        }

        // hide all dock widgets but the note edit dock widget
        const QList<QDockWidget *> dockWidgets = findChildren<QDockWidget *>();
        for (QDockWidget *dockWidget : dockWidgets) {
            if (dockWidget->objectName() ==
                QStringLiteral("noteEditDockWidget")) {
                continue;
            }
            dockWidget->hide();
        }

        // hide the status bar
        //        ui->statusBar->hide();

        _leaveDistractionFreeModeButton = new QPushButton(tr("leave"));
        _leaveDistractionFreeModeButton->setFlat(true);
        _leaveDistractionFreeModeButton->setToolTip(
            tr("Leave distraction free mode"));
        _leaveDistractionFreeModeButton->setStyleSheet(
            QStringLiteral("QPushButton {padding: 0 5px}"));

        _leaveDistractionFreeModeButton->setIcon(QIcon::fromTheme(
            QStringLiteral("zoom-original"),
            QIcon(QStringLiteral(
                ":icons/breeze-pkbsuite/16x16/zoom-original.svg"))));

        connect(_leaveDistractionFreeModeButton, &QPushButton::clicked, this,
                &MainWindow::toggleDistractionFreeMode);

        statusBar()->addPermanentWidget(_leaveDistractionFreeModeButton);

        ui->noteEditTabWidget->tabBar()->hide();
    } else {
        //
        // leave the distraction free mode
        //

        statusBar()->removeWidget(_leaveDistractionFreeModeButton);
        disconnect(_leaveDistractionFreeModeButton, Q_NULLPTR, Q_NULLPTR,
                   Q_NULLPTR);

        // restore states and sizes
        restoreState(
            settings.value(QStringLiteral("DistractionFreeMode/windowState"))
                .toByteArray());
        ui->menuBar->setVisible(
            settings.value(QStringLiteral("DistractionFreeMode/menuBarVisible"))
                .toBool());
        ui->menuBar->restoreGeometry(
            settings
                .value(QStringLiteral("DistractionFreeMode/menuBarGeometry"))
                .toByteArray());
        ui->menuBar->setFixedHeight(
            settings.value(QStringLiteral("DistractionFreeMode/menuBarHeight"))
                .toInt());

        if (ui->noteEditTabWidget->count() > 1) {
            ui->noteEditTabWidget->tabBar()->show();
        }

        bool showLineNumbersInEditor = settings.value(QStringLiteral("Editor/showLineNumbers")).toBool();

        // turn line numbers on again if they were enabled
        if (showLineNumbersInEditor) {
            ui->noteTextEdit->setLineNumberEnabled(true);
        }
    }

    ui->noteTextEdit->setPaperMargins();
    activeNoteTextEdit()->setFocus();
}

/**
 * Sets the distraction free mode if it is currently other than we want it to be
 */
void MainWindow::changeDistractionFreeMode(const bool enabled) {
    if (isInDistractionFreeMode() != enabled) {
        setDistractionFreeMode(enabled);
    }
}

/**
 * Shows a status bar message if not in distraction free mode
 */
void MainWindow::showStatusBarMessage(const QString &message,
                                      const int timeout) {
    if (!isInDistractionFreeMode()) {
        ui->statusBar->showMessage(message, timeout);
    }
}

/**
 * Sets the shortcuts for the note bookmarks up
 */
void MainWindow::setupNoteBookmarkShortcuts() {
    for (int number = 1; number <= 9; number++) {
        // setup the store shortcut
        auto *storeShortcut =
            new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+") +
                                       QString::number(number)),
                          this);

        connect(storeShortcut, &QShortcut::activated, this,
                [this, number]() { storeNoteBookmark(number); });

        // setup the goto shortcut
        auto *gotoShortcut = new QShortcut(
            QKeySequence(QStringLiteral("Ctrl+") + QString::number(number)),
            this);

        connect(gotoShortcut, &QShortcut::activated, this,
                [this, number]() { gotoNoteBookmark(number); });
    }
}

int MainWindow::openNoteDiffDialog(Note changedNote) {
    if (this->noteDiffDialog->isVisible()) {
        this->noteDiffDialog->close();
    }

    qDebug() << __func__ << " - 'changedNote': " << changedNote;

    QSettings settings;

    // check if we should ignore all changes
    if (settings.value(QStringLiteral("ignoreAllExternalModifications"))
            .toBool()) {
        return NoteDiffDialog::Ignore;
    }

    // check if we should accept all changes
    if (settings.value(QStringLiteral("acceptAllExternalModifications"))
            .toBool()) {
        return NoteDiffDialog::Reload;
    }

    const QString text1 = this->ui->noteTextEdit->toPlainText();

    changedNote.updateNoteTextFromDisk();
    const QString text2 = changedNote.getNoteText();

    //    qDebug() << __func__ << " - 'text1': " << text1;
    //    qDebug() << __func__ << " - 'text2': " << text2;

    diff_match_patch *diff = new diff_match_patch();
    const QList<Diff> diffList = diff->diff_main(text1, text2);

    const QString html = diff->diff_prettyHtml(diffList);
    //    qDebug() << __func__ << " - 'html': " << html;

    this->noteDiffDialog = new NoteDiffDialog(this, html);
    this->noteDiffDialog->exec();

    int result = this->noteDiffDialog->resultActionRole();
    return result;
}

void MainWindow::createSystemTrayIcon() {
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(getSystemTrayIcon());

    connect(trayIcon, &QSystemTrayIcon::activated, this,
            &MainWindow::systemTrayIconClicked);

    if (showSystemTray) {
        trayIcon->show();
    }
}

/**
 * Returns a proper system tray icon
 *
 * @return
 */
QIcon MainWindow::getSystemTrayIcon() {
    const QSettings settings;
    const bool darkModeIcon =
        settings.value(QStringLiteral("darkModeTrayIcon"), false).toBool();
    const QString file = darkModeIcon ? QStringLiteral(":/images/icon-dark.png")
                                      : QStringLiteral(":/images/icon.png");
    return QIcon(file);
}

/**
 * Creates the items in the note tree widget from the notes
 */
void MainWindow::loadNoteDirectoryList() {
    qDebug() << __func__;

    const QSignalBlocker blocker(ui->noteTextEdit);
    Q_UNUSED(blocker)

    const QSignalBlocker blocker2(ui->noteTreeWidget);
    Q_UNUSED(blocker2)

    ui->noteTreeWidget->clear();
    //    ui->noteTreeWidget->setRootIsDecorated(isCurrentNoteTreeEnabled);

    // load all notes and add them to the note list widget
    NoteMap* noteMap = NoteMap::getInstance();
    QVector<Note> noteList = noteMap->fetchAllNotes();
    for (Note &note : noteList) {
        addNoteToNoteTreeWidget(note);
    }

    // sort alphabetically again if necessary
    QSettings settings;
    if (settings.value(QStringLiteral("notesPanelSort"), SORT_BY_LAST_CHANGE)
            .toInt() == SORT_ALPHABETICAL) {
        ui->noteTreeWidget->sortItems(
            0, toQtOrder(
                   settings.value(QStringLiteral("notesPanelOrder")).toInt()));
    }

    // setup tagging
    setupTags();

    // generate the tray context menu
    generateSystemTrayContextMenu();

    // clear the text edits if there is no visible note
    if (firstVisibleNoteTreeWidgetItem() == Q_NULLPTR) {
        unsetCurrentNote();
    } else {
        const auto item = findNoteInNoteTreeWidget(_currentNote);

        // in the end we need to set the current item again if we can find it
        if (item != Q_NULLPTR) {
            ui->noteTreeWidget->setCurrentItem(item);
        }
    }
}

/**
 * Adds a note to the note tree widget
 */
bool MainWindow::addNoteToNoteTreeWidget(Note &note,
                                         QTreeWidgetItem *parent) {
    const QString name = note.getName();

    // skip notes without name
    if (name.isEmpty()) {
        return false;
    }

    const bool isNoteListPreview = Utils::Misc::isNoteListPreview();

    // add a note item to the tree
    auto *noteItem = new QTreeWidgetItem();
    Utils::Gui::setTreeWidgetItemToolTipForNote(noteItem, note);
    noteItem->setText(0, name);
    noteItem->setData(0, Qt::UserRole, note.getId());
    noteItem->setData(0, Qt::UserRole + 1, NoteType);
    noteItem->setIcon(0, _noteIcon);

    if (note.isTagged("TODO"))
        handleTreeWidgetItemTagColor(noteItem, "TODO");
    else if (note.isTagged("REFERENCE_NOTE"))
                handleTreeWidgetItemTagColor(noteItem, "REFERENCE_NOTE");

    const bool isEditable = Note::allowDifferentFileName();
    if (isEditable) {
        noteItem->setFlags(noteItem->flags() | Qt::ItemIsEditable);
    }

    const QSignalBlocker blocker(ui->noteTreeWidget);
    Q_UNUSED(blocker)

    if (parent == nullptr) {
        // strange things happen if we insert with insertTopLevelItem
        ui->noteTreeWidget->addTopLevelItem(noteItem);
    } else {
        parent->addChild(noteItem);
    }

    if (isNoteListPreview) {
        updateNoteTreeWidgetItem(note, noteItem);
    }

    //    QSettings settings;
    //    if (settings.value("notesPanelSort", SORT_BY_LAST_CHANGE).toInt() ==
    //    SORT_ALPHABETICAL) {
    //        ui->noteTreeWidget->addTopLevelItem(noteItem);
    //    } else {
    //        ui->noteTreeWidget->insertTopLevelItem(0, noteItem);
    //    }

    return true;
}

void MainWindow::updateNoteTreeWidgetItem(const Note &note,
                                          QTreeWidgetItem *noteItem) {
    if (noteItem == nullptr) {
        noteItem = findNoteInNoteTreeWidget(note);
    }

    QWidget *widget = ui->noteTreeWidget->itemWidget(noteItem, 0);
    auto *noteTreeWidgetItem = dynamic_cast<NoteTreeWidgetItem *>(widget);

    // check if we already set a NoteTreeWidgetItem in the past
    if (noteTreeWidgetItem != nullptr) {
        noteTreeWidgetItem->updateUserInterface(note);
    } else {
        noteTreeWidgetItem = new NoteTreeWidgetItem(note, ui->noteTreeWidget);
    }

    // TODO: set background color
    //    noteTreeWidgetItem->setBackground(noteItem->background(0).color());
    // TODO: handle note renaming
    // TODO: handle updating when note gets changed
    // TODO: handle updating in handleTreeWidgetItemTagColor

    // this takes too long, it takes ages to do this on 1000 notes
    ui->noteTreeWidget->setItemWidget(noteItem, 0, noteTreeWidgetItem);
}

/**
 * @brief makes the current note the first item in the note list without
 * reloading the whole list
 */
void MainWindow::makeCurrentNoteFirstInNoteList() {
    QTreeWidgetItem *item = findNoteInNoteTreeWidget(_currentNote);

    if (item != Q_NULLPTR) {
        const QSignalBlocker blocker(ui->noteTreeWidget);
        Q_UNUSED(blocker)

        ui->noteTreeWidget->takeTopLevelItem(
            ui->noteTreeWidget->indexOfTopLevelItem(item));
        ui->noteTreeWidget->insertTopLevelItem(0, item);

        // set the item as current item if it is visible
        if (!item->isHidden()) {
            ui->noteTreeWidget->setCurrentItem(item);

            if (Utils::Misc::isNoteListPreview()) {
                // ui->noteTreeWidget->setCurrentItem seems to destroy the
                // NoteTreeWidgetItem
                // TODO: the list symbol is still gone
                updateNoteTreeWidgetItem(_currentNote, item);
            }
        }
    }
}

/**
 * Finds a note in the note tree widget and returns its item
 *
 * @param note
 * @return
 */
QTreeWidgetItem *MainWindow::findNoteInNoteTreeWidget(const Note &note) {
    const int noteId = note.getId();
    const int count = ui->noteTreeWidget->topLevelItemCount();

    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = ui->noteTreeWidget->topLevelItem(i);

        if (item->data(0, Qt::UserRole + 1) == NoteType &&
            item->data(0, Qt::UserRole).toInt() == noteId) {
            return item;
        }
    }

    return Q_NULLPTR;
}

void MainWindow::readSettings() {
    QSettings settings;

    notesPath =
        settings.value(QStringLiteral("notesPath")).toString();
    showSystemTray =
        settings.value(QStringLiteral("ShowSystemTray"), false).toBool();
    restoreGeometry(
        settings.value(QStringLiteral("MainWindow/geometry")).toByteArray());
    ui->menuBar->restoreGeometry(
        settings.value(QStringLiteral("MainWindow/menuBarGeometry"))
            .toByteArray());

    // read all relevant settings, that can be set in the settings dialog
    readSettingsFromSettingsDialog(true);

    // get the notes path
    this->notesPath = 
        settings.value(QStringLiteral("notesPath")).toString();

    // migration: remove GAnalytics-cid
    if (!settings.value(QStringLiteral("GAnalytics-cid"))
             .toString()
             .isEmpty()) {
        settings.remove(QStringLiteral("GAnalytics-cid"));
    }

    // let us select a folder if we haven't find one in the settings
    if (this->notesPath.isEmpty()) {
        selectPKbSuiteNotesFolder();
    }

    // migration: remove notes path from recent note folders
    if (!this->notesPath.isEmpty()) {
        QStringList recentNoteFolders =
            settings.value(QStringLiteral("recentNoteFolders")).toStringList();
        if (recentNoteFolders.contains(this->notesPath)) {
            recentNoteFolders.removeAll(this->notesPath);
            settings.setValue(QStringLiteral("recentNoteFolders"),
                              recentNoteFolders);
        }
    }

    // set the editor width selector for the distraction free mode
    const int editorWidthMode =
        settings.value(QStringLiteral("DistractionFreeMode/editorWidthMode"))
            .toInt();

    switch (editorWidthMode) {
        case PKbSuiteMarkdownTextEdit::Medium:
            ui->actionEditorWidthMedium->setChecked(true);
            break;
        case PKbSuiteMarkdownTextEdit::Wide:
            ui->actionEditorWidthWide->setChecked(true);
            break;
        case PKbSuiteMarkdownTextEdit::Full:
            ui->actionEditorWidthFull->setChecked(true);
            break;
        case PKbSuiteMarkdownTextEdit::Custom:
            ui->actionEditorWidthCustom->setChecked(true);
            break;
        default:
        case PKbSuiteMarkdownTextEdit::Narrow:
            ui->actionEditorWidthNarrow->setChecked(true);
            break;
    }

    // toggle the show status bar checkbox
    const bool showStatusBar =
        settings.value(QStringLiteral("showStatusBar"), true).toBool();
    on_actionShow_status_bar_triggered(showStatusBar);

    // toggle the show menu bar checkbox
    // use the current menu bar visibility as default (so it will not be
    // shown by default on Unity desktop)
    const bool showMenuBar =
        settings.value(QStringLiteral("showMenuBar"), !ui->menuBar->isHidden())
            .toBool();
    on_actionShow_menu_bar_triggered(showMenuBar);

    // we want to trigger the event afterwards so the settings of the note edits
    // are updated
    const bool centerCursor =
        settings.value(QStringLiteral("Editor/centerCursor")).toBool();
    ui->actionTypewriter_mode->setChecked(centerCursor);

    // restore old spell check settings
    ui->actionCheck_spelling->setChecked(
        settings.value(QStringLiteral("checkSpelling"), true).toBool());

    // load backends
#ifdef ASPELL_ENABLED
    _spellBackendGroup = new QActionGroup(ui->menuSpelling_backend);
    loadSpellingBackends();
#else
    ui->menuSpelling_backend->menuAction()->setVisible(false);
#endif

    // load language dicts names into menu
    _languageGroup = new QActionGroup(ui->menuLanguages);
    loadDictionaryNames();
}

/**
 * Restores the toolbars
 */
void MainWindow::restoreToolbars() {
    QSettings settings;
    QList<ToolbarContainer> toolbarContainers;
    const int toolbarCount = settings.beginReadArray(QStringLiteral("toolbar"));

    for (int i = 0; i < toolbarCount; i++) {
        settings.setArrayIndex(i);

        ToolbarContainer toolbarContainer;

        toolbarContainer.name =
            settings.value(QStringLiteral("name")).toString();
        if (toolbarContainer.name.isEmpty()) {
            qWarning() << tr("Toolbar could not be loaded without name");
            continue;
        }

        toolbarContainer.title =
            settings.value(QStringLiteral("title")).toString();
        toolbarContainer.actions =
            settings.value(QStringLiteral("items")).toStringList();

        toolbarContainers.push_back(toolbarContainer);
    }

    settings.endArray();

    if (!toolbarContainers.empty()) {
        // delete the custom toolbars
        const auto toolbars = findChildren<QToolBar *>();
        for (QToolBar *toolbar : toolbars) {
            if (!toolbar->objectName().startsWith(
                    Toolbar_Editor::customToolbarNamePrefix)) {
                continue;
            }

            delete toolbar;
        }

        for (ToolbarContainer toolbarContainer :
             Utils::asConst(toolbarContainers)) {
            if (toolbarContainer.toolbarFound()) {
                toolbarContainer.updateToolbar();
            } else {
                toolbarContainer.create(this);
            }
        }
    }

    // initialize the toolbar menu
    initToolbarMenu();

    // update the toolbar menu
    updateToolbarMenu();
}

/**
 * @brief Reads all relevant settings, that can be set in the settings dialog
 */
void MainWindow::readSettingsFromSettingsDialog(const bool isAppLaunch) {
    QSettings settings;

    this->notifyAllExternalModifications =
        settings.value(QStringLiteral("notifyAllExternalModifications"))
            .toBool();
    this->noteSaveIntervalTime =
        settings.value(QStringLiteral("noteSaveIntervalTime"), 10).toInt();

    // default value is 10 seconds
    if (this->noteSaveIntervalTime == 0) {
        this->noteSaveIntervalTime = 10;
        settings.setValue(QStringLiteral("noteSaveIntervalTime"),
                          this->noteSaveIntervalTime);
    }

    // load note text view font
    QString fontString = Utils::Misc::previewFontString();

    // store the current font if there isn't any set yet
    if (fontString.isEmpty()) {
        fontString = ui->noteTextView->font().toString();
        settings.setValue(QStringLiteral("MainWindow/noteTextView.font"),
                          fontString);
    }

    // set the note text view font
    QFont font;
    font.fromString(fontString);
    ui->noteTextView->setFont(font);

    // set the main toolbar icon size
    int toolBarIconSize =
        settings.value(QStringLiteral("MainWindow/mainToolBar.iconSize"))
            .toInt();
    if (toolBarIconSize == 0) {
        toolBarIconSize = ui->mainToolBar->iconSize().height();
        settings.setValue(QStringLiteral("MainWindow/mainToolBar.iconSize"),
                          toolBarIconSize);
    } else {
        QSize size(toolBarIconSize, toolBarIconSize);
        ui->mainToolBar->setIconSize(size);
        _formattingToolbar->setIconSize(size);
        _insertingToolbar->setIconSize(size);
        _windowToolbar->setIconSize(size);
    }

    // change the search notes symbol between dark and light mode
    QString fileName = settings.value(QStringLiteral("darkModeColors")).toBool()
                           ? QStringLiteral("search-notes-dark.svg")
                           : QStringLiteral("search-notes.svg");
    QString styleSheet = ui->searchLineEdit->styleSheet();
    static const QRegularExpression re(QStringLiteral("background-image: url\\(:.+\\);"));
    styleSheet.replace(re, QStringLiteral("background-image: url(:/images/%1);").arg(fileName));
    ui->searchLineEdit->setStyleSheet(styleSheet);

    // initialize the shortcuts for the actions
    initShortcuts();

    // initialize the item height of the tree widgets
    initTreeWidgetItemHeight();

    // we need to initialize the toolbar menu again in case there are new
    // toolbars
    initToolbarMenu();

    // update the toolbar menu
    updateToolbarMenu();

    // init the saved searches completer
    initSavedSearchesCompleter();

    // update the settings of all markdown edits
    const auto textEdits = findChildren<PKbSuiteMarkdownTextEdit *>();
    for (PKbSuiteMarkdownTextEdit *textEdit : textEdits) {
        textEdit->updateSettings();
    }

    ui->tagLineEdit->setHidden(
        settings.value(QStringLiteral("tagsPanelHideSearch")).toBool());
    ui->navigationLineEdit->setHidden(
        settings.value(QStringLiteral("navigationPanelHideSearch")).toBool());

    // set the cursor width of the note text-edits
    int cursorWidth = settings.value(QStringLiteral("cursorWidth"), 1).toInt();
    ui->noteTextEdit->setCursorWidth(cursorWidth);

    // turn line numbers on if enabled
    bool showLineNumbersInEditor = settings.value(
        QStringLiteral("Editor/showLineNumbers")).toBool();
    ui->noteTextEdit->setLineNumberEnabled(showLineNumbersInEditor);

    if (showLineNumbersInEditor) {
        bool darkMode = settings.value(QStringLiteral("darkMode")).toBool();
        ui->noteTextEdit->setLineNumbersCurrentLineColor(QColor(darkMode ?
            QStringLiteral("#eef067") : QStringLiteral("##141414")));
    }

    ui->noteTextEdit->setPaperMargins();

    if (settings.value(QStringLiteral("Editor/disableCursorBlinking"))
            .toBool()) {
        qApp->setCursorFlashTime(0);
    }

    initGlobalKeyboardShortcuts();
}

/**
 * Initializes the item height of the tree widgets
 */
void MainWindow::initTreeWidgetItemHeight() {
    QSettings settings;
    int height = settings.value(QStringLiteral("itemHeight")).toInt();

    // if the height was 0 set it the the current height of a tree widget item
    if (height == 0) {
        QTreeWidget treeWidget(this);
        auto *treeWidgetItem = new QTreeWidgetItem();
        treeWidget.addTopLevelItem(treeWidgetItem);
        height = treeWidget.visualItemRect(treeWidgetItem).height();
        settings.setValue(QStringLiteral("itemHeight"), height);
    }

    updateTreeWidgetItemHeight(ui->tagTreeWidget, height);
    updateTreeWidgetItemHeight(ui->noteTreeWidget, height);
    updateTreeWidgetItemHeight(ui->navigationWidget, height);
}

/**
 * Sets height of the items of a tree widget
 *
 * @param treeWidget
 * @param height
 */
void MainWindow::updateTreeWidgetItemHeight(QTreeWidget *treeWidget,
                                            int height) {
    QString styleText = treeWidget->styleSheet();

    // remove the old height stylesheet
    static const QRegularExpression re(
        QStringLiteral("\nQTreeWidget::item \\{height: \\d+px\\}"),
                                    QRegularExpression::CaseInsensitiveOption);
    styleText.remove(re);

    // add the new height stylesheet
    styleText += QStringLiteral("\nQTreeWidget::item {height: %1px}")
                     .arg(QString::number(height));

    treeWidget->setStyleSheet(styleText);
}

void MainWindow::updateNoteTextFromDisk(Note note) {
    note.updateNoteTextFromDisk();

    // Check if the note has @Tags not yet linked
	QRegularExpression re = QRegularExpression(R"([^A-Za-z]#([A-Za-zÀ-ÖØ-öø-ÿ_]|\d+[A-Za-zÀ-ÖØ-öø-ÿ_])[A-Za-zÀ-ÖØ-öø-ÿ0-9_]*)");       // Take care of accented characters
	QRegularExpressionMatchIterator reIterator = re.globalMatch(_currentNote.getNoteText());
	while (reIterator.hasNext()) {
		QRegularExpressionMatch reMatch = reIterator.next();
		QString tag = reMatch.captured().right(reMatch.capturedLength() - 2);

        const QSignalBlocker blocker(noteDirectoryWatcher);
		Q_UNUSED(blocker);
		linkTagNameToCurrentNote(tag);
	}

    note.store();

    const QSignalBlocker blocker(this->ui->noteTextEdit);
	Q_UNUSED(blocker)
	this->setNoteTextFromNote(&note);
}

void MainWindow::notesWereModified(const QString &str) {
    // workaround when signal block doesn't work correctly
    if (_isNotesWereModifiedDisabled) {
        return;
    }

    // if we should ignore all changes return here
    if (QSettings().value(QStringLiteral("ignoreAllExternalNoteFolderChanges"))
        .toBool()) {
        return;
    }

    qDebug() << "notesWereModified: " << str;

    QFileInfo fi(str);
    Note note = NoteMap::getInstance()->fetchNoteByFileName(fi.fileName());

    // load note from disk if current note was changed
    if (note.getFileName() == this->_currentNote.getFileName()) {
        if (note.fileExists()) {
            // If the modified date of the file is the same as the one
            // from the current note it was a false alarm
            if (fi.lastModified() == this->_currentNote.getFileLastModified()) {
                return;
            }

            const QString oldNoteText = note.getNoteText();

            // fetch text of note from disk
            note.updateNoteTextFromDisk();
            const QString noteTextOnDisk =
                Utils::Misc::transformLineFeeds(note.getNoteText());
            const bool isCurrentNoteNotEditedForAWhile =
                this->_currentNoteLastEdited.addSecs(60) <
                QDateTime::currentDateTime();
            // If the current note wasn't edited for a while, we want that it is possible
            // to get updated even with small changes, so we are setting a threshold of 0
            const int threshold = isCurrentNoteNotEditedForAWhile ? 0 : 8;

            // Check if the old note text is the same or similar as the one on disk
            if (Utils::Misc::isSimilar(oldNoteText, noteTextOnDisk, threshold)) {
                return;
            }

            const QString noteTextOnDiskHash =
                QString(QCryptographicHash::hash(noteTextOnDisk.toLocal8Bit(),
                                                 QCryptographicHash::Sha1)
                            .toHex());

            // skip dialog if text of note file on disk and current note are
            // equal
            if (noteTextOnDiskHash == _currentNoteTextHash) {
                return;
            }

            // fetch current text
            const QString noteTextEditText =
                this->ui->noteTextEdit->toPlainText();

            // skip dialog if text of note file on disk text from note text
            // edit are equal or similar
            if (Utils::Misc::isSimilar(noteTextEditText, noteTextOnDisk, threshold)) {
                return;
            }

            showStatusBarMessage(tr("Current note was modified externally"),
                                 5000);

            // if we don't want to get notifications at all
            // external modifications check if we really need one
            if (!this->notifyAllExternalModifications) {
                // reloading the current note text straight away
                // if we didn't change it for a minute
                if (!this->_currentNote.getHasDirtyData() &&
                    isCurrentNoteNotEditedForAWhile) {
                    updateNoteTextFromDisk(std::move(note));
                    return;
                }
            }

            const int result = openNoteDiffDialog(note);
            switch (result) {
                // overwrite file with local changes
                case NoteDiffDialog::Overwrite: {
                    const QSignalBlocker blocker(this->noteDirectoryWatcher);
                    Q_UNUSED(blocker)

                    showStatusBarMessage(
                        tr("Overwriting external changes of: %1")
                            .arg(_currentNote.getFileName()),
                        3000);

                    // the note text has to be stored newly because the
                    // external change is already in the note table entry
                    _currentNote.storeNewText(
                            ui->noteTextEdit->toPlainText());
                    _currentNote.storeNoteTextFileToDisk();

                    // just to make sure everything is up-to-date
                    //                        this->_currentNote = note;
                    //                        this->setNoteTextFromNote( &note,
                    //                        true );

                    // wait 100ms before the block on this->noteDirectoryWatcher
                    // is opened, otherwise we get the event
                    Utils::Misc::waitMsecs(100);
                } break;

                // reload note file from disk
                case NoteDiffDialog::Reload:
                    showStatusBarMessage(tr("Loading external changes from: %1")
                                             .arg(_currentNote.getFileName()),
                                         3000);
                    updateNoteTextFromDisk(note);
                    break;

                    //                case NoteDiffDialog::Cancel:
                    //                case NoteDiffDialog::Ignore:
                default:
                    // do nothing
                    break;
            }
        } else if (_noteExternallyRemovedCheckEnabled) {
            // only allow the check if current note was removed externally in
            // the root note folder, because it gets triggered every time
            // a note gets renamed in subfolders

            qDebug() << "Current note was removed externally!";

            if (Utils::Gui::question(
                    this, tr("Note was removed externally!"),
                    tr("Current note was removed outside of this application!\n"
                       "Restore current note?"),
                    QStringLiteral("restore-note")) == QMessageBox::Yes) {
                const QSignalBlocker blocker(this->noteDirectoryWatcher);
                Q_UNUSED(blocker)

                QString text = this->ui->noteTextEdit->toPlainText();
                note.storeNewText(std::move(text));

                // store note to disk again
                const bool noteWasStored = note.storeNoteTextFileToDisk();
                showStatusBarMessage(
                    noteWasStored
                        ? tr("Stored current note to disk")
                        : tr("Current note could not be stored to disk"),
                    3000);

                // rebuild and reload the notes directory list
                buildNotesIndexAndLoadNoteDirectoryList();

                // fetch note new (because all the IDs have changed
                // after the buildNotesIndex()
                note.refetch();

                // restore old selected row (but don't update the note text)
                setCurrentNote(note, false);
            }
        }
    } else {
        qDebug() << "other note was changed: " << str;

        showStatusBarMessage(tr("Note was modified externally: %1").arg(str),
                             5000);

        // rebuild and reload the notes directory list
        buildNotesIndexAndLoadNoteDirectoryList();
        setCurrentNote(std::move(this->_currentNote), false);
    }
}

void MainWindow::notesDirectoryWasModified(const QString &str) {
    // workaround when signal block doesn't work correctly
    if (_isNotesDirectoryWasModifiedDisabled) {
        return;
    }

    // if we should ignore all changes return here
    if (QSettings().value(QStringLiteral("ignoreAllExternalNoteFolderChanges"))
            .toBool()) {
        return;
    }

    qDebug() << "notesDirectoryWasModified: " << str;
    showStatusBarMessage(tr("Notes directory was modified externally"), 5000);

    // rebuild and reload the notes directory list
    buildNotesIndexAndLoadNoteDirectoryList();

    // check if the current note was modified
    // this fixes not detected external note changes of the current note if the
    // event for the change in the current note comes after the event that the
    // note folder was modified
    QString noteFileName = _currentNote.getFileName();
    if (!noteFileName.isEmpty()) {
        notesWereModified(_currentNote.getFileName());
    }

    // also update the text of the text edit if current note has changed
    bool updateNoteText = !this->_currentNote.exists();
    qDebug() << "updateNoteText: " << updateNoteText;

    // restore old selected row (but don't update the note text)
    setCurrentNote(this->_currentNote, updateNoteText);
}

/**
 * Checks if the note view needs an update because the text has changed
 */
void MainWindow::noteViewUpdateTimerSlot() {
    if (_noteViewNeedsUpdate) {
        if (isMarkdownViewEnabled()) {
            setNoteTextFromNote(&_currentNote, true);
        }
        _noteViewNeedsUpdate = false;
    }
    _noteViewUpdateTimer->start(2000);
}

void MainWindow::storeUpdatedNotesToDisk() {
    const QSignalBlocker blocker(noteDirectoryWatcher);
    Q_UNUSED(blocker)

    QString oldNoteName = _currentNote.getName();

    // For some reason this->noteDirectoryWatcher gets an event from this.
    // I didn't find another solution than to wait yet.
    // All flushing and syncing didn't help.
    bool currentNoteChanged = false;
    bool noteWasRenamed = false;
    bool currentNoteTextChanged = false;
         
    QString currentNoteText = _currentNote.getNoteText();
    
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor cursor = textEdit->textCursor();
    // Check if the note has #Tags not yet linked
    QRegularExpression re = QRegularExpression(R"([^A-Za-z]#([A-Za-zÀ-ÖØ-öø-ÿ_]|\d+[A-Za-zÀ-ÖØ-öø-ÿ_])[A-Za-zÀ-ÖØ-öø-ÿ0-9_]*)");       // Take care of accented characters
    QRegularExpressionMatchIterator reIterator = re.globalMatch(currentNoteText);
    while (reIterator.hasNext()) {
        QRegularExpressionMatch reMatch = reIterator.next();
        QString tagName = reMatch.captured().right(reMatch.capturedLength() - 2);
        int tagNameStart = reMatch.capturedStart(reMatch.lastCapturedIndex());
        int tagNameEnd = reMatch.capturedEnd(reMatch.lastCapturedIndex());

		const int cursorPos = cursor.position();
        if ((cursorPos < tagNameStart) || (cursorPos > tagNameEnd)) {
            const QSignalBlocker blocker(noteDirectoryWatcher);
            Q_UNUSED(blocker);

            linkTagNameToCurrentNote(tagName);
        }
    }

    // Check if some [[Links]] needs to be expanded but ONLY if the target note exists to prevent inserting empty note names
    re = QRegularExpression(R"(\[\[([A-Za-z\s\_\-]*)\]\])");	// TODO Take figures into account if I want to implement filenames based on note's IDs
    QRegularExpressionMatch reMatch = re.match(currentNoteText);

    if (reMatch.hasMatch()){
        QString candidateNoteName = Utils::Misc::toStartCase(reMatch.captured(reMatch.lastCapturedIndex()));
        int candidateNoteNameStart = reMatch.capturedStart(reMatch.lastCapturedIndex());
        int candidateNoteNameEnd = reMatch.capturedEnd(reMatch.lastCapturedIndex());
        
		int cursorPos = cursor.position();
        if ((cursorPos < candidateNoteNameStart) || (cursorPos > candidateNoteNameEnd)) {
            cursor.setPosition(candidateNoteNameStart - 2, QTextCursor::MoveAnchor);
            cursor.setPosition(candidateNoteNameEnd + 2, QTextCursor::KeepAnchor);            

            QString tstStr = cursor.selectedText();
            cursor.removeSelectedText();
            const QString strLink = "[" + candidateNoteName + "](" + candidateNoteName.replace(" ", "%20") + ".md)";
            cursor.insertText(strLink);

            if (cursorPos > candidateNoteNameEnd)
                cursor.setPosition(cursorPos + strLink.length() - (candidateNoteNameEnd - candidateNoteNameStart) - 4);
            else
                cursor.setPosition(cursorPos);
            textEdit->setTextCursor(cursor);
        }
    }

    // Check if some links are not existing in the graph
    ui->kbGraphView->updateLinks(ui->kbGraphView->getNodeFromNote(&_currentNote), currentNoteText);

    // _currentNote will be set by this method if the filename has changed
    const int count = Note::storeDirtyNotesToDisk(
        _currentNote, &currentNoteChanged, &noteWasRenamed, &currentNoteTextChanged);

    if (count > 0) {
        _noteViewNeedsUpdate = true;

        qDebug() << __func__ << " - 'count': " << count;

        showStatusBarMessage(tr("Stored %n note(s) to disk", "", count), 3000);

        // wait 100ms before the block on this->noteDirectoryWatcher
        // is opened, otherwise we get the event
        Utils::Misc::waitMsecs(100);

        if (currentNoteChanged) {
            // strip trailing spaces of the current note (if enabled)
            if (QSettings().value(QStringLiteral("Editor/removeTrailingSpaces"))
                    .toBool()) {
                const bool wasStripped = _currentNote.stripTrailingSpaces(
                    activeNoteTextEdit()->textCursor().position());

                if (wasStripped) {
                    qDebug() << __func__ << " - 'wasStripped'";

                    // updating the current note text is disabled because it
                    // moves the cursor to the top
//                    const QSignalBlocker blocker2(activeNoteTextEdit());
//                    Q_UNUSED(blocker2)
//                    setNoteTextFromNote(&_currentNote);
                }
            }

            if (currentNoteTextChanged) {
                // reload the current note if we had to change it during a note rename
                reloadCurrentNoteByNoteId(true);
            }

            // just to make sure everything is up-to-date
            _currentNote.refetch();

            // create a hash of the text of the current note to be able if it
            // was modified outside of PKbSuite
            updateCurrentNoteTextHash();

            if (oldNoteName != _currentNote.getName()) {
                // just to make sure the window title is set correctly
                updateWindowTitle();

                // update current tab name
                updateCurrentTabData(_currentNote);
            }
        }

        if (noteWasRenamed) {
            // reload the directory list if note name has changed
            loadNoteDirectoryList();
        }
    }
}

/**
 * Does the setup the status bar widgets
 */
void MainWindow::setupStatusBarWidgets() {
    /*
     * setup of line number label
     */
    _noteEditLineNumberLabel = new QLabel(this);
    _noteEditLineNumberLabel->setText(QStringLiteral("0:0"));
    _noteEditLineNumberLabel->setToolTip(tr("Line numbers"));

    ui->statusBar->addPermanentWidget(_noteEditLineNumberLabel);
}

/**
 * Builds the index of notes and note sub folders
 */
bool MainWindow::buildNotesIndex(int noteSubFolderId, bool forceRebuild) {
    QString notePath =
        Utils::Misc::removeIfEndsWith(this->notesPath, QDir::separator());
    bool wasModified = false;

    // make sure we destroy nothing
    storeUpdatedNotesToDisk();

    // init the lists to check for removed items
    NoteMap* noteMap = NoteMap::getInstance();
    _buildNotesIndexBeforeNoteIdList = noteMap->fetchAllIds();
    _buildNotesIndexAfterNoteIdList.clear();


    //    qDebug() << __func__ << " - 'notePath': " << notePath;

    QDir notesDir(notePath);

    // only show certain files
    auto filters = customNoteFileExtensionList(QStringLiteral("*."));

    // show the newest entry first
    QStringList files = notesDir.entryList(filters, QDir::Files, QDir::Time);
    //    qDebug() << __func__ << " - 'files': " << files;

    bool createDemoNotes = (files.count() == 0);

    if (createDemoNotes) {
        QSettings settings;
        // check if we already have created the demo notes once
        createDemoNotes =
            !settings.value(QStringLiteral("demoNotesCreated")).toBool();

        if (createDemoNotes) {
            // we don't want to create the demo notes again
            settings.setValue(QStringLiteral("demoNotesCreated"), true);
        }
    }

    // add some notes if there aren't any and
    // we haven't already created them once
    if (createDemoNotes) {
        qDebug() << "No notes! We will add some...";
        const QStringList filenames =
            QStringList({"Markdown Showcase.md", "Markdown Cheatsheet.md",
                         "Welcome to PKbSuite.md"});

        // copy note files to the notes path
        for (int i = 0; i < filenames.size(); ++i) {
            const QString &filename = filenames.at(i);
            const QString destinationFile =
                this->notesPath + QDir::separator() + filename;
            QFile sourceFile(QStringLiteral(":/demonotes/") + filename);
            sourceFile.copy(destinationFile);
            // set read/write permissions for the owner and user
            QFile::setPermissions(destinationFile,
                                  QFile::ReadOwner | QFile::WriteOwner |
                                      QFile::ReadUser | QFile::WriteUser);
        }

        // copy the shortcuts file and handle its file permissions
        //        destinationFile = this->notesPath + QDir::separator() +
        //              "Important Shortcuts.txt";
        //        QFile::copy( ":/shortcuts", destinationFile );
        //        QFile::setPermissions( destinationFile, QFile::ReadOwner |
        //                  QFile::WriteOwner | QFile::ReadUser |
        //                  QFile::WriteUser );

        // fetch all files again
        files = notesDir.entryList(filters, QDir::Files, QDir::Time);

        // jump to the welcome note in the note selector in 500ms
        QTimer::singleShot(500, this, SLOT(jumpToWelcomeNote()));
    }

    if (forceRebuild) {
        // first delete all notes and note sub folders in the note map if a
        // rebuild was forced
        NoteMap::getInstance()->deleteAll();
    }

    const int numFiles = files.count();
    QProgressDialog progress(tr("Loading notes…"), tr("Abort"), 0, numFiles,
                             this);
    progress.setWindowModality(Qt::WindowModal);
    int currentCount = 0;

    _buildNotesIndexAfterNoteIdList.reserve(files.size());
    // create all notes from the files              // TODO: To be removed
    for (QString fileName : Utils::asConst(files)) {
        if (progress.wasCanceled()) {
            break;
        }

        // fetching the content of the file
        QFile file(Note::getFullFilePathForFile(fileName));

        // update or create a note from the file
        const Note note =
            Note::updateOrCreateFromFile(file);

        // add the note id to in the end check if notes need to be removed
        _buildNotesIndexAfterNoteIdList << note.getId();

        if (!_buildNotesIndexBeforeNoteIdList.contains(note.getId())) {
            wasModified = true;
        }

        // this still causes double entries on OS X and maybe Windows
#ifdef Q_OS_LINUX
        QCoreApplication::sendPostedEvents();
#endif
        progress.setValue(++currentCount);
    }

    progress.setValue(numFiles);

    // update the UI and get user input after all the notes were loaded
    // this still can cause duplicate note subfolders to be viewed
    //    QCoreApplication::processEvents();

    // re-fetch current note (because all the IDs have changed after the
    // buildNotesIndex()
    _currentNote.refetch();

    // check for removed notes
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    const QList<int> removedNoteIdList =
        QSet<int>(_buildNotesIndexBeforeNoteIdList.begin(),
                    _buildNotesIndexBeforeNoteIdList.end())
            .subtract(QSet<int>(_buildNotesIndexAfterNoteIdList.begin(),
                                _buildNotesIndexAfterNoteIdList.end()))
            .values();
#else
    const QList<int> removedNoteIdList =
        _buildNotesIndexBeforeNoteIdList.toList()
            .toSet()
            .subtract(_buildNotesIndexAfterNoteIdList.toSet())
            .toList();
#endif

    // remove all missing notes
    for (const int noteId : removedNoteIdList) {
        Note note = NoteMap::getInstance()->fetchNoteById(noteId);
        if (note.isFetched()) {
            note.remove();
            wasModified = true;
        }
    }

    // update the note directory watcher
    updateNoteDirectoryWatcher();

    return wasModified;
}

/**
 * Updates the note directory watcher
 */
void MainWindow::updateNoteDirectoryWatcher() {
    // clear all paths from the directory watcher
    clearNoteDirectoryWatcher();

    const QString notePath =
        Utils::Misc::removeIfEndsWith(this->notesPath, QDir::separator());

    const QDir notesDir(notePath);

    if (notesDir.exists()) {
        // watch the notes directory for changes
        noteDirectoryWatcher.addPath(notePath);
    }

    int count = 0;
    NoteMap* noteMap = NoteMap::getInstance();
    const QVector<Note> noteList = noteMap->fetchAllNotes();
    for (const Note &note : noteList) {
#ifdef Q_OS_LINUX
        // only add the last first 200 notes to the file watcher to
        // prevent that nothing is watched at all because of too many
        // open files
        if (count > 200) {
            break;
        }
#endif
        const QString path = note.fullNoteFilePath();
        const QFile file(path);

        if (file.exists()) {
            // watch the note for changes
            noteDirectoryWatcher.addPath(path);

            ++count;
        }
    }

    //    qDebug() << __func__ << " - 'noteDirectoryWatcher.files()': " <<
    //    noteDirectoryWatcher.files();
    //
    //    qDebug() << __func__ << " - 'noteDirectoryWatcher.directories()': " <<
    //    noteDirectoryWatcher.directories();
}

/**
 * Clears all paths from the directory watcher
 */
void MainWindow::clearNoteDirectoryWatcher() {
    const QStringList fileList =
        noteDirectoryWatcher.directories() + noteDirectoryWatcher.files();
    if (fileList.count() > 0) {
        noteDirectoryWatcher.removePaths(fileList);
    }
}

/**
 * Jumps to the welcome note in the note selector
 */
void MainWindow::jumpToWelcomeNote() {
    jumpToNoteName(QStringLiteral("Welcome to PKbSuite"));
}

/**
 * Jumps to a note in the note selector
 */
bool MainWindow::jumpToNoteName(const QString &name) {
    // search for the note
    QList<QTreeWidgetItem *> items = ui->noteTreeWidget->findItems(
        name, Qt::MatchExactly | Qt::MatchRecursive, 0);

    if (items.count() > 0) {
        ui->noteTreeWidget->setCurrentItem(items.at(0));
        return true;
    }

    return false;
}

/**
 * Jumps to a note in the note selector by NoteHistoryItem
 */
bool MainWindow::jumpToNoteHistoryItem(const NoteHistoryItem &historyItem) {
    // search for the note
    const QList<QTreeWidgetItem *> items = ui->noteTreeWidget->findItems(
        historyItem.getNoteName(), Qt::MatchExactly | Qt::MatchRecursive, 0);

    for (QTreeWidgetItem *item : items) {
        ui->noteTreeWidget->setCurrentItem(item);
        return true;
    }

    return false;
}

QString MainWindow::selectPKbSuiteNotesFolder() {
    QString path = this->notesPath;

    if (path.isEmpty()) {
        path = Utils::Misc::defaultNotesPath();
    }

    // TODO(pbek): We sometimes seem to get a "QCoreApplication::postEvent:
    // Unexpected null receiver" here.
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Please select the folder where your notes will get stored"),
        path, QFileDialog::ShowDirsOnly);

    QDir d = QDir(dir);

    if (d.exists() && (!dir.isEmpty())) {
        // let's remove trailing slashes
        dir = d.path();

        this->notesPath = dir;
        QSettings settings;
        // make the path relative to the portable data path if we are in
        // portable mode
        settings.setValue(
            QStringLiteral("notesPath"),
                std::move(dir));

    } else {
        if (this->notesPath.isEmpty()) {
            switch (QMessageBox::information(
                this, tr("No folder was selected"),
                    tr("You have to select your folder to make this software work!"),
                tr("&Retry"), tr("&Exit"), QString(), 0, 1)) {
                case 0:
                    selectPKbSuiteNotesFolder();
                    break;
                case 1:
                default:
                    // No other way to quit the application worked
                    // in the constructor
                    // Waqar144: this doesn't seem very wise...
                    QTimer::singleShot(0, this, SLOT(quitApp()));
                    QTimer::singleShot(100, this, SLOT(quitApp()));
                    break;
            }
        }
    }

    return this->notesPath;
}

/**
 * Sets the current note from a note id
 */
void MainWindow::setCurrentNoteFromNoteId(const int noteId) {
    // make sure the main window is visible
    show();

    Note note = NoteMap::getInstance()->fetchNoteById(noteId);
    if (note.isFetched()) {
        setCurrentNote(std::move(note));
    }
}

/**
 * Reloads the current note by id
 * This is useful when the path or filename of the current note changed
 */
void MainWindow::reloadCurrentNoteByNoteId(bool updateNoteText) {
    // get current cursor position
    auto cursor = activeNoteTextEdit()->textCursor();
    const int pos = cursor.position();

    // update the current note
    _currentNote = NoteMap::getInstance()->fetchNoteById(_currentNote.getId());
    setCurrentNote(std::move(_currentNote), updateNoteText);

    // restore old cursor position
    cursor.setPosition(pos);
    activeNoteTextEdit()->setTextCursor(cursor);
}

void MainWindow::setCurrentNote(Note note, bool updateNoteText,
                                bool updateSelectedNote,
                                bool addNoteToHistory) {
    qDebug() << __func__ << " - 'note': " << note
             << " - 'updateNoteText': " << updateNoteText
             << " - 'updateSelectedNote': " << updateSelectedNote;

    // update cursor position of previous note
    const int noteId = note.getId();
    if (_currentNote.exists() && (_currentNote.getId() != note.getId())) {
        this->noteHistory.updateCursorPositionOfNote(this->_currentNote,
                                                     ui->noteTextEdit);
    }

    this->_lastNoteId = this->_currentNote.getId();
    this->_currentNote = note;

    // for places we can't get the current note id, like the markdown
    // highlighter
    qApp->setProperty("_currentNoteId", noteId);

    const QString name = note.getName();
    updateWindowTitle();

    // update current tab
    if (!jumpToTab(note) && Utils::Gui::isTabWidgetTabSticky(
           ui->noteEditTabWidget, ui->noteEditTabWidget->currentIndex())) {
        openCurrentNoteInTab();
    }

    updateCurrentTabData(note);

    // find and set the current item
    if (updateSelectedNote) {
        QList<QTreeWidgetItem *> items =
            ui->noteTreeWidget->findItems(name, Qt::MatchExactly);
        if (items.count() > 0) {
            const QSignalBlocker blocker(ui->noteTreeWidget);
            Q_UNUSED(blocker)

            // to avoid that multiple notes will be selected
            ui->noteTreeWidget->clearSelection();

            ui->noteTreeWidget->setCurrentItem(items[0]);
        }
    }

    // update the text of the text edit
    if (updateNoteText) {
        const QSignalBlocker blocker(ui->noteTextEdit);
        Q_UNUSED(blocker)

        this->setNoteTextFromNote(&note, false, false, true);

        ui->noteTextEdit->show();
    }

    // we also need to do this in on_noteTreeWidget_itemSelectionChanged
    // because of different timings
    reloadCurrentNoteTags();

    // Update graph
    ui->kbGraphView->centerOnNote(&note);

    QSettings settings;
    const bool restoreCursorPositionDefault = true;

    const bool restoreCursorPosition =
        settings
            .value(QStringLiteral("restoreCursorPosition"),
                   restoreCursorPositionDefault)
            .toBool();

    // restore the last position in the note text edit
    if (restoreCursorPosition) {
        noteHistory.getLastItemOfNote(note).restoreTextEditPosition(
            ui->noteTextEdit);
    }

    // add new note to history
    if (addNoteToHistory && note.exists()) {
        this->noteHistory.add(note, ui->noteTextEdit);
    }

    noteEditCursorPositionChanged();

    // create a hash of the text of the current note to be able if it was
    // modified outside of PKbSuite
    updateCurrentNoteTextHash();

    // clear external image cache
    Note::externalImageHash()->clear();

    ui->actionToggle_distraction_free_mode->setEnabled(true);
}

void MainWindow::updateCurrentTabData(const Note &note) const {
    Utils::Gui::updateTabWidgetTabData(ui->noteEditTabWidget,
                                       ui->noteEditTabWidget->currentIndex(),
                                       note);
}

void MainWindow::closeOrphanedTabs() const {
    const int maxIndex = ui->noteEditTabWidget->count() - 1;

    for (int i = maxIndex; i >= 0; i--) {
        const int noteId = Utils::Gui::getTabWidgetNoteId(
            ui->noteEditTabWidget, i);

        if (!Note::noteIdExists(noteId)) {
            removeNoteTab(i);
        }
    }
}

bool MainWindow::jumpToTab(const Note &note) const {
    const int noteId = note.getId();
    const int tabIndexOfNote = Utils::Gui::getTabWidgetIndexByProperty(
        ui->noteEditTabWidget, QStringLiteral("note-id"), noteId);

    if (tabIndexOfNote == -1) {
        return false;
    }

    ui->noteEditTabWidget->setCurrentIndex(tabIndexOfNote);
    QWidget *widget = ui->noteEditTabWidget->currentWidget();

    if (widget->layout() == nullptr) {
        widget->setLayout(ui->noteEditTabWidgetLayout);
        closeOrphanedTabs();
    }

    return true;
}

/**
 * Creates a hash of the text of the current note to be able to tell if it was
 * modified outside of PKbSuite
 */
void MainWindow::updateCurrentNoteTextHash() {
    _currentNoteTextHash = QString(
        QCryptographicHash::hash(_currentNote.getNoteText().toLocal8Bit(),
                                 QCryptographicHash::Sha1)
            .toHex());
}

/**
 * Updates the windows title for the current note
 */
void MainWindow::updateWindowTitle() {
    const QString &session = qApp->property("session").toString();
    QString title = _currentNote.exists() ?
        _currentNote.getName() : QStringLiteral("#");

    if (!session.isEmpty()) {
        title += QStringLiteral(" - %1").arg(session);
    }

    title += QStringLiteral(" - PKbSuite - %3").arg(QStringLiteral(VERSION));
    setWindowTitle(title);
}

/**
 * Focuses the note text edit and sets the cursor
 */
void MainWindow::focusNoteTextEdit() {
    QTextCursor tmpCursor = ui->noteTextEdit->textCursor();

    // move the cursor to the 4th line if the cursor was at the beginning
    if (tmpCursor.position() == 0) {
        tmpCursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
        tmpCursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor);
        tmpCursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor);
        tmpCursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor);
        ui->noteTextEdit->setTextCursor(tmpCursor);
    }

    // focus note text edit
    ui->noteTextEdit->setFocus();
}

/**
 * Removes the current note
 */
void MainWindow::removeCurrentNote() {
    // store updated notes to disk
    storeUpdatedNotesToDisk();

    if (Utils::Gui::question(this, tr("Remove current note"),
                             tr("Remove current note: <strong>%1</strong>?")
                                 .arg(this->_currentNote.getName()),
                             QStringLiteral("remove-note")) ==
        QMessageBox::Yes) {
        const QSignalBlocker blocker2(ui->noteTextEdit);
        Q_UNUSED(blocker2)

        const QSignalBlocker blocker3(ui->noteTextView);
        Q_UNUSED(blocker3)

        const QSignalBlocker blocker5(noteDirectoryWatcher);
        Q_UNUSED(blocker5)

        // we try to fix problems with note subfolders
        directoryWatcherWorkaround(true);

        {
            const QSignalBlocker blocker1(ui->noteTreeWidget);
            Q_UNUSED(blocker1)

            // search and remove note from the note tree widget
            removeNoteFromNoteTreeWidget(_currentNote);

            // delete note on file system
            _currentNote.remove(true);

            unsetCurrentNote();
        }

        // set a new current note
        resetCurrentNote(false);

        // we try to fix problems with note subfolders
        // we need to wait some time to turn the watcher on again because
        // something is happening after this method that reloads the
        // note folder
        directoryWatcherWorkaround(false);
    }
}

/**
 * Searches and removes note from the note tree widget
 */
void MainWindow::removeNoteFromNoteTreeWidget(Note &note) const {
    auto *item = Utils::Gui::getTreeWidgetItemWithUserData(ui->noteTreeWidget,
                                                           note.getId());

    if (item != nullptr) {
        delete(item);
    }
}

/**
 * Resets the current note to the first note
 */
void MainWindow::resetCurrentNote(bool goToTop) {
    auto *event =
        new QKeyEvent(QEvent::KeyPress, goToTop ? Qt::Key_Home : Qt::Key_Down,
            Qt::NoModifier);
    QApplication::postEvent(ui->noteTreeWidget, event);
}

/**
 * Stores the settings
 */
void MainWindow::storeSettings() {
    QSettings settings;

    // don't store the window settings in distraction free mode
    if (!isInDistractionFreeMode()) {
        settings.setValue(QStringLiteral("MainWindow/geometry"),
                          saveGeometry());
        settings.setValue(QStringLiteral("MainWindow/menuBarGeometry"),
                          ui->menuBar->saveGeometry());
    }

    // store a NoteHistoryItem to open the note again after the app started
    const NoteHistoryItem noteHistoryItem(&_currentNote, ui->noteTextEdit);
    qDebug() << __func__ << " - 'noteHistoryItem': " << noteHistoryItem;
    settings.setValue(QStringLiteral("ActiveNoteHistoryItem"),
                      QVariant::fromValue(noteHistoryItem));

    // store the note history of the current note folder
    noteHistory.store();

    Utils::Gui::storeNoteTabs(ui->noteEditTabWidget);
}

/*!
 * Internal events
 */

void MainWindow::closeEvent(QCloseEvent *event) {
    _closeEventWasFired = true;
    const bool forceQuit = qApp->property("clearAppDataAndExit").toBool();
    const bool isJustHide = showSystemTray;

#ifdef Q_OS_MAC
    // #1113, unfortunately the closeEvent is also fired when the application
    // will be quit in the dock menu
//    isJustHide = true;
#endif

    // #1496, don't ignore close event when the app is hidden to tray
    // this can occur when the OS issues close events on shutdown
    if (isJustHide && !forceQuit && !isHidden()) {
#ifdef Q_OS_MAC
        showMinimized();
#else
        hide();
#endif
        event->ignore();
    } else {
        // we need to do this in the close event (and _not_ in the destructor),
        // because in the destructor the layout will be destroyed in dark mode
        // when the window was closed
        // https://github.com/pbek/PKbSuite/issues/1015
        if (!isInDistractionFreeMode()) {
            storeCurrentWorkspace();
        }

        QMainWindow::closeEvent(event);
    }
}

//
// Event filters on the MainWindow
//
bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (obj == ui->searchLineEdit->completer()->popup()) {
            if (keyEvent->key() == Qt::Key_Return) {
                // set a variable to ignore that first "Return" in the
                // return-handler
                _searchLineEditFromCompleter = true;
                return false;
            }
        } else if ((obj == ui->newNoteTagLineEdit) ||
                   ((ui->newNoteTagLineEdit->completer() != nullptr) &&
                    (obj == ui->newNoteTagLineEdit->completer()->popup()))) {
            // if tab is pressed while adding a tag the tag that starts with
            // the current text will be added
            if (keyEvent->key() == Qt::Key_Tab) {
                // fetch the tag that is starting with the current text
                TagMap* tagMap = TagMap::getInstance();
                QString tag = tagMap->fetchByName(ui->newNoteTagLineEdit->text(), true);

                if (tag != "") {
                    linkTagNameToCurrentNote(tag, true);
    
					QTextCursor tc = ui->noteTextEdit->textCursor();
					tc.insertText("#" + tag);
				}

                return false;
            }
        } else if (obj == ui->searchLineEdit) {
            bool downSelectNote = false;

            // fallback to the default completion
            ui->searchLineEdit->completer()->setCompletionMode(
                QCompleter::PopupCompletion);

            if (keyEvent->key() == Qt::Key_Down) {
                if (ui->searchLineEdit->completer()->completionCount() > 0) {
                    // the search text is empty we want to show all saved
                    // searches if "Down" was pressed
                    if (ui->searchLineEdit->text().isEmpty()) {
                        ui->searchLineEdit->completer()->setCompletionMode(
                            QCompleter::UnfilteredPopupCompletion);
                    }

                    // open the completer
                    ui->searchLineEdit->completer()->complete();
                    return false;
                } else {
                    // if nothing was found in the completer we want to jump
                    // to the note list
                    downSelectNote = true;
                }
            }

            // set focus to the notes list if Key_Right or Key_Tab were
            // pressed in the search line edit
            if ((keyEvent->key() == Qt::Key_Right) ||
                (keyEvent->key() == Qt::Key_Tab) || downSelectNote) {
                // add the current search text to the saved searches
                storeSavedSearch();

                // choose another selected item if current item is invisible
                QTreeWidgetItem *item = ui->noteTreeWidget->currentItem();
                if ((item != Q_NULLPTR) && item->isHidden()) {
                    QTreeWidgetItem *firstVisibleItem =
                        firstVisibleNoteTreeWidgetItem();
                    if (firstVisibleItem != Q_NULLPTR) {
                        ui->noteTreeWidget->setCurrentItem(firstVisibleItem);
                    }
                }

                // give the keyboard focus to the note tree widget
                ui->noteTreeWidget->setFocus();
                return true;
            }
            return false;
        } else if (obj == activeNoteTextEdit()) {
            // check if we want to leave the distraction free mode and the
            // search widget is not visible (because we want to close that
            // first)
            if ((keyEvent->key() == Qt::Key_Escape) &&
                isInDistractionFreeMode() &&
                !activeNoteTextEdit()->searchWidget()->isVisible()) {
                toggleDistractionFreeMode();

                return true;
            }

            return false;
        } else if (obj == ui->noteTreeWidget) {
            // set focus to the note text edit if Key_Return or Key_Tab were
            // pressed in the notes list
            if ((keyEvent->key() == Qt::Key_Return) ||
                (keyEvent->key() == Qt::Key_Tab)) {
                // focusNoteTextEdit() might cause a crash in
                // on_noteTreeWidget_itemChanged if Note::allowDifferentFileName()
                // is true when Note::handleNoteRenaming is called, the
                // QTimer::singleShot helps with that
                QTimer::singleShot(150, this,
                                   SLOT(focusNoteTextEdit()));

                return true;
            } else if ((keyEvent->key() == Qt::Key_Delete) ||
                       (keyEvent->key() == Qt::Key_Backspace)) {
                removeSelectedNotes();
                return true;
            }
            return false;
        } else if (obj == ui->tagTreeWidget) {
            if ((keyEvent->key() == Qt::Key_Delete) ||
                (keyEvent->key() == Qt::Key_Backspace)) {
                removeSelectedTags();
                return true;
            }
            return false;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);

        if ((mouseEvent->button() == Qt::BackButton)) {
            // move back in the note history
            on_action_Back_in_note_history_triggered();
        } else if ((mouseEvent->button() == Qt::ForwardButton)) {
            // move forward in the note history
            on_action_Forward_in_note_history_triggered();
        }
    } else if (event->type() == QEvent::MouseButtonPress &&
               obj == ui->selectedTagsToolButton) {
        // we don't want to make the button clickable
        return true;
    }

    return QMainWindow::eventFilter(obj, event);
}

/**
 * Finds the first visible tree widget item
 */
QTreeWidgetItem *MainWindow::firstVisibleNoteTreeWidgetItem() {
    QTreeWidgetItemIterator it(ui->noteTreeWidget,
                               QTreeWidgetItemIterator::NotHidden);

    return *it;
}

/**
 * Highlights all occurrences of str in the note text edit and does a "in note
 * search"
 */
void MainWindow::searchInNoteTextEdit(QString str) {
    QList<QTextEdit::ExtraSelection> extraSelections;
    QList<QTextEdit::ExtraSelection> extraSelections2;

    if (str.count() >= 2) {
        // do an in-note search
        doSearchInNote(str);
        ui->noteTextEdit->moveCursor(QTextCursor::Start);
        ui->noteTextView->moveCursor(QTextCursor::Start);
        const QColor color = QColor(0, 180, 0, 100);

        // build the string list of the search string
        const QString queryStr =
            str.replace(QLatin1String("|"), QLatin1String("\\|"));
        const QStringList queryStrings =
            NoteMap::buildQueryStringList(queryStr, true);

        if (queryStrings.count() > 0) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 13, 0))
            const QRegularExpression regExp(
                QLatin1Char('(') + queryStrings.join(QLatin1String("|")) +
                    QLatin1Char(')'),
                QRegularExpression::CaseInsensitiveOption);
#else
            const QRegExp regExp(QLatin1String("(") +
                                     queryStrings.join(QLatin1String("|")) +
                                     QLatin1String(")"),
                                 Qt::CaseInsensitive);
#endif
            while (ui->noteTextEdit->find(regExp)) {
                QTextEdit::ExtraSelection extra = QTextEdit::ExtraSelection();
                extra.format.setBackground(color);

                extra.cursor = ui->noteTextEdit->textCursor();
                extraSelections.append(extra);
            }

            while (ui->noteTextView->find(regExp)) {
                QTextEdit::ExtraSelection extra = QTextEdit::ExtraSelection();
                extra.format.setBackground(color);

                extra.cursor = ui->noteTextView->textCursor();
                extraSelections2.append(extra);
            }
        }
    }

    ui->noteTextEdit->setExtraSelections(extraSelections);
    ui->noteTextView->setExtraSelections(extraSelections2);
}

/**
 * highlights all occurrences of the search line text in the note text edit
 */
void MainWindow::searchForSearchLineTextInNoteTextEdit() {
    QString searchString = ui->searchLineEdit->text();
    searchInNoteTextEdit(std::move(searchString));
}

/**
 * Gets the maximum image width
 */
int MainWindow::getMaxImageWidth() {
    const QMargins margins = ui->noteTextView->contentsMargins();
    int maxImageWidth = ui->noteTextView->viewport()->width() - margins.left() -
                        margins.right() - 15;

    if (maxImageWidth < 0) {
        maxImageWidth = 16;
    }

    return maxImageWidth;
}

/**
 * Sets the note text according to a note
 */
void MainWindow::setNoteTextFromNote(Note *note, bool updateNoteTextViewOnly,
                                     bool ignorePreviewVisibility, bool allowRestoreCursorPosition) {
    if (note == nullptr) {
        return;
    }

    auto historyItem = noteHistory.getLastItemOfNote(_currentNote);

    if (!updateNoteTextViewOnly) {
        qobject_cast<PKbSuiteMarkdownHighlighter *>(
            ui->noteTextEdit->highlighter())
            ->updateCurrentNote(note);
        ui->noteTextEdit->setText(note->getNoteText());
    }

    if (allowRestoreCursorPosition && Utils::Misc::isRestoreCursorPosition()) {
        historyItem.restoreTextEditPosition(ui->noteTextEdit);
        ui->noteTextEdit->highlightCurrentLine();
    }

    // update the preview text edit if the dock widget is visible
    if (_notePreviewDockWidget->isVisible() || ignorePreviewVisibility) {

        const QString html = note->toMarkdownHtml(
            notesPath, getMaxImageWidth(), false);

        // create a hash of the html (because
        const QString hash =
            QString(QCryptographicHash::hash(html.toLocal8Bit(),
                                             QCryptographicHash::Sha1)
                        .toHex());

        // update the note preview if the text has changed
        // we use our hash because ui->noteTextView->toHtml() may return
        // a different text than before
        if (_notePreviewHash != hash) {
            ui->noteTextView->setHtml(html);
            _notePreviewHash = hash;
        }
    }

    // update the slider when editing notes
    noteTextSliderValueChanged(
        activeNoteTextEdit()->verticalScrollBar()->value(), true);
}

/**
 * Starts the parsing for the navigation widget
 */
void MainWindow::startNavigationParser() {
    if (ui->navigationWidget->isVisible())
        ui->navigationWidget->parse(activeNoteTextEdit()->document());
}

/**
 * Sets the text of the current note.
 * This is a public callback function for the version dialog.
 *
 * @brief MainWindow::setCurrentNoteText
 * @param text
 */
void MainWindow::setCurrentNoteText(QString text) {
    _currentNote.setNoteText(std::move(text));
    setNoteTextFromNote(&_currentNote, false);
}

/**
 * Creates a new note (to restore a trashed note)
 * This is a public callback function for the trash dialog.
 *
 * @brief MainWindow::createNewNote
 * @param name
 * @param text
 * @param cursorAtEnd
 */
void MainWindow::createNewNote(QString name, QString text,
                               CreateNewNoteOptions options) {
    const QString extension = Note::defaultNoteFileExtension();
    auto *f = new QFile(this->notesPath + QDir::separator() + name +
                        QStringLiteral(".") + extension);
    const bool useNameAsHeadline =
        options.testFlag(CreateNewNoteOption::UseNameAsHeadline);

    // change the name and headline if note exists
    if (f->exists()) {
        QDateTime currentDate = QDateTime::currentDateTime();
        name.append(QStringLiteral(" ") +
                    currentDate.toString("yyyyMMddhhmmss")
                        .replace(QStringLiteral(":"), QStringLiteral(".")));

        if (!useNameAsHeadline) {
            QString preText = Note::createNoteHeader(name);
            text.prepend(preText);
        }
    }

    // create a new note
    ui->searchLineEdit->setText(name);

    jumpToNoteOrCreateNew(
        options.testFlag(CreateNewNoteOption::DisableLoadNoteDirectoryList));

    // check if to append the text or replace the text of the note
    if (useNameAsHeadline) {
        QTextCursor c = ui->noteTextEdit->textCursor();
        // make sure the cursor is really at the end to be able to
        // insert the text on the correct position
        c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
        c.insertText(QStringLiteral("\n\n") + text);
        ui->noteTextEdit->setTextCursor(c);
    } else {
        ui->noteTextEdit->setText(text);
    }

    // move the cursor to the end of the note
    if (options.testFlag(CreateNewNoteOption::CursorAtEnd)) {
        QTextCursor c = ui->noteTextEdit->textCursor();
        c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
        ui->noteTextEdit->setTextCursor(c);
    }
}

/**
 * @brief Removes selected notes after a confirmation
 */
void MainWindow::removeSelectedNotes() {
    // store updated notes to disk
    storeUpdatedNotesToDisk();

    const int selectedItemsCount = getSelectedNotesCount();

    if (selectedItemsCount == 0) {
        return;
    }

    if (Utils::Gui::question(
            this, tr("Remove selected notes"),
                tr("Remove <strong>%n</strong> selected note(s)?\n",
                   "", selectedItemsCount),
            QStringLiteral("remove-notes")) == QMessageBox::Yes) {
        const QSignalBlocker blocker(this->noteDirectoryWatcher);
        Q_UNUSED(blocker)

        const QSignalBlocker blocker2(activeNoteTextEdit());
        Q_UNUSED(blocker2)

        const QSignalBlocker blocker3(ui->noteTextView);
        Q_UNUSED(blocker3)

        // we try to fix problems with note subfolders
        directoryWatcherWorkaround(true);

        {
            const QSignalBlocker blocker1(ui->noteTreeWidget);
            Q_UNUSED(blocker1)

            const auto selItems = ui->noteTreeWidget->selectedItems();
            for (QTreeWidgetItem *item : selItems) {
                if (item->data(0, Qt::UserRole + 1) != NoteType) {
                    continue;
                }

                const int id = item->data(0, Qt::UserRole).toInt();
                Note note = NoteMap::getInstance()->fetchNoteById(id);

                // search and remove note from the note tree widget
                removeNoteFromNoteTreeWidget(note);

                note.remove(true);
                qDebug() << "Removed note " << note.getName();
            }

            // clear the text edit so it stays clear after removing the
            // last note
            activeNoteTextEdit()->clear();
        }

        // set a new current note
        resetCurrentNote(false);

        // we try to fix problems with note subfolders
        // we need to wait some time to turn the watcher on again because
        // something is happening after this method that reloads the note folder
        directoryWatcherWorkaround(false);
    }

    loadNoteDirectoryList();
}

/**
 * Removes selected tags after a confirmation
 */
void MainWindow::removeSelectedTags() {
    const int selectedItemsCount = ui->tagTreeWidget->selectedItems().size();

    if (selectedItemsCount == 0) {
        return;
    }

    if (Utils::Gui::question(
            this, tr("Remove selected tags"),
            tr("Remove <strong>%n</strong> selected tag(s)? No notes will "
               "be removed in this process.",
               "", selectedItemsCount),
            QStringLiteral("remove-tags")) == QMessageBox::Yes) {
        const QSignalBlocker blocker(this->noteDirectoryWatcher);
        Q_UNUSED(blocker)

        const QSignalBlocker blocker1(ui->tagTreeWidget);
        Q_UNUSED(blocker1)

        // workaround when signal blocking doesn't work correctly
        directoryWatcherWorkaround(true, true);

        const auto selItems = ui->tagTreeWidget->selectedItems();
        for (QTreeWidgetItem *item : selItems) {
            TagMap* tagMap = TagMap::getInstance();
            const QString tag = item->data(0, Qt::UserRole).toString();

            QVector<int> idsTaggedNotes = tagMap->fetchAllLinkedNotes(tag);
            
            int idNote = 0;
            while (idNote < idsTaggedNotes.size()) {
                Note note = NoteMap::getInstance()->fetchNoteById(idsTaggedNotes.at(idNote));
                QString noteText = note.getNoteText();
                   
                QRegularExpression re = QRegularExpression(R"((, )?@)" + tag);
                QRegularExpressionMatchIterator reIterator = re.globalMatch(noteText);

                while (reIterator.hasNext()) {
                    QRegularExpressionMatch reMatch = reIterator.next();
                    int lTag = reMatch.capturedLength() + 1;
                    
                    noteText.replace(reMatch.capturedStart(), lTag, "");
                }
                                
                const QSignalBlocker blocker(this->noteDirectoryWatcher);
                Q_UNUSED(blocker);

                note.setNoteText(noteText);
                
                if (note.getId() == ui->noteTreeWidget->currentItem()->data(0, Qt::UserRole).toInt())
                    ui->noteTextEdit->setPlainText(noteText);
                idNote++;
			}
			
            tagMap->remove(tag);
			qDebug() << "Removed tag " << tag;
		}

        storeUpdatedNotesToDisk();

        // disable workaround
        directoryWatcherWorkaround(false, true);

        reloadCurrentNoteTags();
        reloadTagTree();
    }
}

/**
 * @brief Select all notes
 */
void MainWindow::selectAllNotes() { ui->noteTreeWidget->selectAll(); }

/**
 * @brief Moves selected notes after a confirmation
 * @param destinationFolder
 */
void MainWindow::moveSelectedNotesToFolder(const QString &destinationFolder) {
    // store updated notes to disk
    storeUpdatedNotesToDisk();

    const int selectedItemsCount = ui->noteTreeWidget->selectedItems().size();

    if (Utils::Gui::question(
            this, tr("Move selected notes"),
            tr("Move %n selected note(s) to <strong>%2</strong>?", "",
               selectedItemsCount)
                .arg(destinationFolder),
            QStringLiteral("move-notes")) == QMessageBox::Yes) {
        const QSignalBlocker blocker(this->noteDirectoryWatcher);
        Q_UNUSED(blocker)

        const auto selectedItems = ui->noteTreeWidget->selectedItems();
        for (QTreeWidgetItem *item : selectedItems) {
            if (item->data(0, Qt::UserRole + 1) != NoteType) {
                continue;
            }

            const int noteId = item->data(0, Qt::UserRole).toInt();
            Note note = NoteMap::getInstance()->fetchNoteById(noteId);

            if (!note.isFetched()) {
                continue;
            }

            // remove note path form directory watcher
            this->noteDirectoryWatcher.removePath(note.fullNoteFilePath());

            if (note.getId() == _currentNote.getId()) {
                // unset the current note
                unsetCurrentNote();
            }

            // move note
            const bool result = note.moveToPath(destinationFolder);
            if (result) {
                qDebug() << "Note was moved:" << note.getName();
            } else {
                qWarning() << "Could not move note:" << note.getName();
            }
        }

        loadNoteDirectoryList();
    }
}

/**
 * Returns a list of all selected notes
 *
 * @return
 */
QVector<Note> MainWindow::selectedNotes() {
    QVector<Note> selectedNotes;

    const auto selectedItems = ui->noteTreeWidget->selectedItems();
    for (QTreeWidgetItem *item : selectedItems) {
        if (item->data(0, Qt::UserRole + 1) != NoteType) {
            continue;
        }

        const int noteId = item->data(0, Qt::UserRole).toInt();
        const Note note = NoteMap::getInstance()->fetchNoteById(noteId);

        if (note.isFetched()) {
            selectedNotes << note;
        }
    }

    return selectedNotes;
}

/**
 * Un-sets the current note
 */
void MainWindow::unsetCurrentNote() {
    // reset the current note
    _currentNote = Note();

    // clear the note preview
    const QSignalBlocker blocker(ui->noteTextView);
    Q_UNUSED(blocker)
    ui->noteTextView->clear();

    // clear the note text edit
    const QSignalBlocker blocker2(ui->noteTextEdit);
    Q_UNUSED(blocker2)
    ui->noteTextEdit->clear();
    ui->noteTextEdit->show();
}

/**
 * @brief Copies selected notes after a confirmation
 * @param destinationFolder
 */
void MainWindow::copySelectedNotesToFolder(const QString &destinationFolder,
                                           const QString &noteFolderPath) {
    int selectedItemsCount = ui->noteTreeWidget->selectedItems().size();

    if (Utils::Gui::question(
            this, tr("Copy selected notes"),
            tr("Copy %n selected note(s) to <strong>%2</strong>?", "",
               selectedItemsCount)
                .arg(destinationFolder),
            QStringLiteral("copy-notes")) == QMessageBox::Yes) {
        int copyCount = 0;
        const auto selectedItems = ui->noteTreeWidget->selectedItems();
        for (QTreeWidgetItem *item : selectedItems) {
            if (item->data(0, Qt::UserRole + 1) != NoteType) {
                continue;
            }

            const int noteId = item->data(0, Qt::UserRole).toInt();
            Note note = NoteMap::getInstance()->fetchNoteById(noteId);

            if (!note.isFetched()) {
                continue;
            }

            // copy note
            const bool result =
                note.copyToPath(destinationFolder, noteFolderPath);
            if (result) {
                copyCount++;
                qDebug() << "Note was copied:" << note.getName();
            } else {
                qWarning() << "Could not copy note:" << note.getName();
            }
        }

        Utils::Gui::information(
            this, tr("Done"),
            tr("%n note(s) were copied to <strong>%2</strong>.", "", copyCount)
                .arg(destinationFolder),
            QStringLiteral("notes-copied"));
    }
}

/**
 * Tags selected notes
 */
void MainWindow::tagSelectedNotes(const QString &tag) {
    const int selectedItemsCount = ui->noteTreeWidget->selectedItems().size();

    if (Utils::Gui::question(
            this, tr("Tag selected notes"),
            tr("Tag %n selected note(s) with <strong>%2</strong>?", "",
               selectedItemsCount)
                .arg(tag),
            QStringLiteral("tag-notes")) == QMessageBox::Yes) {
        int tagCount = 0;

        // workaround when signal block doesn't work correctly
        directoryWatcherWorkaround(true, true);

        const auto selectedItems = ui->noteTreeWidget->selectedItems();
        for (QTreeWidgetItem *item : selectedItems) {
            if (item->data(0, Qt::UserRole + 1) != NoteType) {
                continue;
            }

            const int noteId = item->data(0, Qt::UserRole).toInt();
            Note note = NoteMap::getInstance()->fetchNoteById(noteId);

            if (!note.isFetched()) {
                continue;
            }

            const QSignalBlocker blocker(noteDirectoryWatcher);
            Q_UNUSED(blocker)

            // tag note
            note.addTag(tag);;
			QString strNote = note.getNoteText();
			QRegularExpressionMatch reMatch;
			if (strNote.contains(QRegularExpression(R"(===\n(@[a-zA-Z]*,?\s?)*)"), &reMatch)) {
				QString strUpdatedTag = reMatch.captured(0);
				strUpdatedTag.chop(1);
				note.setNoteText(strNote.replace(QRegularExpression(R"(===\n(@[a-zA-Z]*,?\s?)*)"), strUpdatedTag + ", @" + tag + "\n"));
				note.storeNoteTextFileToDisk();
			}

            tagCount++;
            qDebug() << "Note was tagged:" << note.getName();

            // handle the coloring of the note in the note tree widget
            handleNoteTreeTagColoringForNote(note);
        }

        reloadCurrentNoteTags();
        reloadTagTree();
		updateNoteTextFromDisk(_currentNote);
		
        showStatusBarMessage(
            tr("%n note(s) were tagged with \"%2\"", "", tagCount)
                .arg(tag),
            5000);

        // turn off the workaround again
        directoryWatcherWorkaround(false, true);
    }
}

/**
 * Removes a tag from the selected notes
 */
void MainWindow::removeTagFromSelectedNotes(const QString tag) {
    const int selectedItemsCount = ui->noteTreeWidget->selectedItems().size();

    if (Utils::Gui::question(
            this, tr("Remove tag from selected notes"),
            tr("Remove tag <strong>%1</strong> from %n selected note(s)?", "",
               selectedItemsCount)
                .arg(tag),
            QStringLiteral("remove-tag-from-notes")) == QMessageBox::Yes) {
        int tagCount = 0;

        // workaround when signal blocking doesn't work correctly
        directoryWatcherWorkaround(true, true);

        const auto selectedItems = ui->noteTreeWidget->selectedItems();
        for (auto *item : selectedItems) {
            if (item->data(0, Qt::UserRole + 1) != NoteType) {
                continue;
            }

            const int noteId = item->data(0, Qt::UserRole).toInt();
            Note note = NoteMap::getInstance()->fetchNoteById(noteId);

            if (!note.isFetched()) {
                continue;
            }

            const QSignalBlocker blocker(noteDirectoryWatcher);
            Q_UNUSED(blocker)

            // tag note
            note.removeTag(tag);
            tagCount++;
            qDebug() << "Tag was removed from note:" << note.getName();

            // handle the coloring of the note in the note tree widget
            handleNoteTreeTagColoringForNote(note);
        }

        reloadCurrentNoteTags();
        reloadTagTree();
        filterNotesByTag();

        Utils::Gui::information(
            this, tr("Done"),
            tr("Tag <strong>%1</strong> was removed from %n note(s)", "",
               tagCount)
                .arg(tag),
            QStringLiteral("tag-removed-from-notes"));

        // turn off the workaround again
        directoryWatcherWorkaround(false, true);
    }
}

/**
 * Activates or deactivates a workaround for the ill behaving directory watcher
 *
 * @param isNotesDirectoryWasModifiedDisabled
 * @param alsoHandleNotesWereModified
 */
void MainWindow::directoryWatcherWorkaround(
    bool isNotesDirectoryWasModifiedDisabled,
    bool alsoHandleNotesWereModified) {
    if (!isNotesDirectoryWasModifiedDisabled) {
        Utils::Misc::waitMsecs(200);
    }

    _isNotesDirectoryWasModifiedDisabled = isNotesDirectoryWasModifiedDisabled;

    if (alsoHandleNotesWereModified) {
        _isNotesWereModifiedDisabled = isNotesDirectoryWasModifiedDisabled;
    }
}

/**
 * Handle the coloring of the note in the note tree widget
 *
 * @param note
 */
void MainWindow::handleNoteTreeTagColoringForNote(Note &note) {
    QTreeWidgetItem *noteItem = findNoteInNoteTreeWidget(note);

    if (note.isTagged("TODO")) {
        handleTreeWidgetItemTagColor(noteItem, "TODO");
        return;
    }

    if (note.isTagged("REFERENCE_NOTE")) {
        handleTreeWidgetItemTagColor(noteItem, "REFERENCE_NOTE");
        return;
    }
}

/**
 * Opens the settings dialog
 */
void MainWindow::openSettingsDialog(int page, bool openScriptRepository) {
    if (_settingsDialog == Q_NULLPTR) {
        _settingsDialog = new SettingsDialog(page, this);
    } else {
        _settingsDialog->readSettings();
        _settingsDialog->setCurrentPage(page);
    }

    if (openScriptRepository) {
        QTimer::singleShot(150, _settingsDialog,
                           SLOT(searchScriptInRepository()));
    }

    // open the settings dialog
    _settingsDialog->exec();

    // seems to safe a little leaking memory
    // we must not null the dialog, this will crash if the ownCloud check
    // tries to write to the labels and the dialog went away
    //    delete(_settingsDialog);
    //    _settingsDialog = Q_NULLPTR;

    // shows a restart application notification if needed
    if (showRestartNotificationIfNeeded()) {
        return;
    }

    // make sure no settings get written after after we got the
    // clearAppDataAndExit call
    if (qApp->property("clearAppDataAndExit").toBool()) {
        return;
    }

    // read all relevant settings, that can be set in the settings dialog,
    // even if the dialog was canceled
    readSettingsFromSettingsDialog();

    // update the panels sort and order
    updatePanelsSortOrder();

    // reset the note save timer
    this->noteSaveTimer->stop();
    this->noteSaveTimer->start(this->noteSaveIntervalTime * 1000);

    // load the note list again in case the setting on the note name has changed
    loadNoteDirectoryList();

    // force that the preview is regenerated
    forceRegenerateNotePreview();
}

void MainWindow::forceRegenerateNotePreview() {
    _notePreviewHash.clear();
    _currentNote.resetNoteTextHtmlConversionHash();
    regenerateNotePreview();
}

/**
 * Shows a restart application notification if needed
 *
 * @return true if the applications is restarting
 */
bool MainWindow::showRestartNotificationIfNeeded(bool force) {
    const bool needsRestart = qApp->property("needsRestart").toBool() || force;

    if (!needsRestart) {
        return false;
    }

    qApp->setProperty("needsRestart", false);

    if (QMessageBox::information(
            this, tr("Restart application"),
            tr("You may need to restart the application to let the "
                   "changes take effect.") +
                Utils::Misc::appendSingleAppInstanceTextIfNeeded(),
            tr("Restart"), tr("Cancel"), QString(), 0, 1) == 0) {
        storeSettings();
        Utils::Misc::restartApplication();

        return true;
    }

    return false;
}

/**
 * @brief Returns the active note text edit
 */
PKbSuiteMarkdownTextEdit *MainWindow::activeNoteTextEdit() {
    return ui->noteTextEdit;
}

/**
 * @brief Handles the linking of text
 */
void MainWindow::handleTextNoteLinking(int page) {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    auto *dialog = new LinkDialog(page, QString(), this);

    QString selectedText = textEdit->textCursor().selectedText();
    if (!selectedText.isEmpty()) {
        dialog->setLinkName(selectedText);
    }

    dialog->exec();

    if (dialog->result() == QDialog::Accepted) {
        const QString url = dialog->getURL();
        const QString linkName = dialog->getLinkName();
        const QString linkDescription = dialog->getLinkDescription();
        // remove ] characters, because they will break markdown links
        QString noteName = dialog->getSelectedNoteName().remove("]");

        if ((!noteName.isEmpty()) || (!url.isEmpty())) {
            QString newText;
            QString chosenLinkName = linkName.isEmpty()
                                         ? textEdit->textCursor().selectedText()
                                         : linkName;
            // remove ] characters, because they will break markdown links
            chosenLinkName.remove("]");

            // if user has entered an url
            if (!url.isEmpty()) {
                newText = !chosenLinkName.isEmpty()
                              ? QStringLiteral("[") + chosenLinkName +
                                    QStringLiteral("](") + url +
                                    QStringLiteral(")")
                              : QStringLiteral("<") + url + QStringLiteral(">");
            } else {
                const QString noteUrl = _currentNote.getNoteUrlForLinkingTo(
                    dialog->getSelectedNote());

                const QString heading = dialog->getSelectedHeading();
                const QString headingText = heading.isEmpty() ?
                              QStringLiteral() : QStringLiteral("#") +
                                 QUrl::toPercentEncoding(heading);

                // if user has selected a note
                if (!chosenLinkName.isEmpty()) {
                    noteName = chosenLinkName;
                } else if (!heading.isEmpty()) {
                    // if a note and a heading were selected add heading text to link title
                    noteName += QStringLiteral(" - ") + heading;
                }

                newText = QStringLiteral("[") + noteName +
                          QStringLiteral("](") + noteUrl + headingText +
                          QStringLiteral(")");
            }

            if (!linkDescription.isEmpty()) {
                newText += QStringLiteral(" ") + linkDescription;
            }

            textEdit->textCursor().insertText(newText);
        }
    }

    delete (dialog);
}

/**
 * @brief Sets the current note from a CurrentNoteHistoryItem
 * @param item
 */
void MainWindow::setCurrentNoteFromHistoryItem(const NoteHistoryItem &item) {
    qDebug() << item;
    qDebug() << item.getNote();

    setCurrentNote(item.getNote(), true, true, false);
    item.restoreTextEditPosition(ui->noteTextEdit);
}

/**
 * @brief Prepares the printer to print the content of a text edit widget
 * @param textEdit
 */
bool MainWindow::preparePrintNotePrinter(QPrinter *printer) {
    Utils::Misc::loadPrinterSettings(printer,
                                     QStringLiteral("Printer/NotePrinting"));

    QPrintDialog dialog(printer, this);
    dialog.setWindowTitle(tr("Print note"));
    const int ret = dialog.exec();

    if (ret != QDialog::Accepted) {
        return false;
    }

    Utils::Misc::storePrinterSettings(printer,
                                      QStringLiteral("Printer/NotePrinting"));
    return true;
}

/**
 * @brief Prints the content of a plain text edit widget
 * @param textEdit
 */
void MainWindow::printNote(QPlainTextEdit *textEdit) {
    printTextDocument(textEdit->document());
}

/**
 * @brief Prints the content of a text edit widget
 * @param textEdit
 */
void MainWindow::printNote(QTextEdit *textEdit) {
    printTextDocument(textEdit->document());
}

/**
 * @brief Prints the content of a text document
 * @param textEdit
 */
void MainWindow::printTextDocument(QTextDocument *textDocument) {
    auto *printer = new QPrinter();

    if (preparePrintNotePrinter(printer)) {
        textDocument->print(printer);
    }

    delete printer;
}

/**
 * @brief Prepares the printer dialog to exports the content of a text edit
 *        widget as PDF
 * @param printer
 */
bool MainWindow::prepareExportNoteAsPDFPrinter(QPrinter *printer) {
#ifdef Q_OS_LINUX
    Utils::Misc::loadPrinterSettings(printer,
                                     QStringLiteral("Printer/NotePDFExport"));

    // under Linux we use the QPageSetupDialog to change layout
    // settings of the PDF export
    QPageSetupDialog pageSetupDialog(printer, this);

    if (pageSetupDialog.exec() != QDialog::Accepted) {
        return false;
    }

    Utils::Misc::storePrinterSettings(printer,
                                      QStringLiteral("Printer/NotePDFExport"));
#else
    // under OS X and Windows the QPageSetupDialog dialog doesn't work,
    // we will use a workaround to select page sizes and the orientation

    QSettings settings;

    // select the page size
    QStringList pageSizeStrings;
    pageSizeStrings << QStringLiteral("A0") << QStringLiteral("A1")
                    << QStringLiteral("A2") << QStringLiteral("A3")
                    << QStringLiteral("A4") << QStringLiteral("A5")
                    << QStringLiteral("A6") << QStringLiteral("A7")
                    << QStringLiteral("A8") << QStringLiteral("A9")
                    << tr("Letter");
    QList<QPageSize::PageSizeId> pageSizes;
    pageSizes << QPageSize::A0 << QPageSize::A1 << QPageSize::A2
              << QPageSize::A3 << QPageSize::A4 << QPageSize::A5
              << QPageSize::A6 << QPageSize::A7 << QPageSize::A8
              << QPageSize::A9 << QPageSize::Letter;

    bool ok;
    QString pageSizeString = QInputDialog::getItem(
        this, tr("Page size"), tr("Page size:"), pageSizeStrings,
        settings.value(QStringLiteral("Printer/NotePDFExportPageSize"), 4)
            .toInt(),
        false, &ok);

    if (!ok || pageSizeString.isEmpty()) {
        return false;
    }

    int pageSizeIndex = pageSizeStrings.indexOf(pageSizeString);
    if (pageSizeIndex == -1) {
        return false;
    }

    QPageSize pageSize(pageSizes.at(pageSizeIndex));
    settings.setValue(QStringLiteral("Printer/NotePDFExportPageSize"),
                      pageSizeIndex);
    printer->setPageSize(pageSize);

    // select the orientation
    QStringList orientationStrings;
    orientationStrings << tr("Portrait") << tr("Landscape");
    QList<QPrinter::Orientation> orientations;
    orientations << QPrinter::Portrait << QPrinter::Landscape;

    QString orientationString = QInputDialog::getItem(
        this, tr("Orientation"), tr("Orientation:"), orientationStrings,
        settings.value(QStringLiteral("Printer/NotePDFExportOrientation"), 0)
            .toInt(),
        false, &ok);

    if (!ok || orientationString.isEmpty()) {
        return false;
    }

    int orientationIndex = orientationStrings.indexOf(orientationString);
    if (orientationIndex == -1) {
        return false;
    }

    printer->setOrientation(orientations.at(orientationIndex));
    settings.setValue(QStringLiteral("Printer/NotePDFExportOrientation"),
                      orientationIndex);
#endif

    FileDialog dialog(QStringLiteral("NotePDFExport"));
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(tr("PDF files") + QStringLiteral(" (*.pdf)"));
    dialog.setWindowTitle(tr("Export current note as PDF"));
    dialog.selectFile(_currentNote.getName() + QStringLiteral(".pdf"));
    int ret = dialog.exec();

    if (ret != QDialog::Accepted) {
        return false;
    }

    QString fileName = dialog.selectedFile();

    if (fileName.isEmpty()) {
        return false;
    }

    if (QFileInfo(fileName).suffix().isEmpty()) {
        fileName.append(QLatin1String(".pdf"));
    }

    printer->setOutputFormat(QPrinter::PdfFormat);
    printer->setOutputFileName(fileName);
    return true;
}

/**
 * @brief Exports the content of a plain text edit widget as PDF
 * @param textEdit
 */
void MainWindow::exportNoteAsPDF(QPlainTextEdit *textEdit) {
    exportNoteAsPDF(textEdit->document());
}

/**
 * @brief Exports the content of a text edit widget as PDF
 * @param textEdit
 */
void MainWindow::exportNoteAsPDF(QTextEdit *textEdit) {
    exportNoteAsPDF(textEdit->document());
}

/**
 * @brief Exports the document as PDF
 * @param doc
 */
void MainWindow::exportNoteAsPDF(QTextDocument *doc) {
    auto *printer = new QPrinter(QPrinter::HighResolution);

    if (prepareExportNoteAsPDFPrinter(printer)) {
        doc->print(printer);
        Utils::Misc::openFolderSelect(printer->outputFileName());
    }

    delete printer;
}

// *****************************************************************************
// *
// *
// * Slot implementations
// *
// *
// *****************************************************************************

/**
 * Triggers if the text in the note text edit was modified
 */
void MainWindow::on_noteTextEdit_textChanged() {
    // this also triggers when formatting is applied / syntax highlighting
    // changes!
    //    noteTextEditTextWasUpdated();
}

void MainWindow::noteTextEditTextWasUpdated() {
    Note note = this->_currentNote;
    note.updateNoteTextFromDisk();

    // we are transforming line feeds, because in some instances Windows
    // managed to sneak some "special" line feeds in
    const QString noteTextFromDisk =
        Utils::Misc::transformLineFeeds(note.getNoteText());
    QString text =
        Utils::Misc::transformLineFeeds(ui->noteTextEdit->toPlainText());

    // store the note if the note text differs from the one
    // on the disk or the note was already modified but not stored to disk
    if (text != noteTextFromDisk || _currentNote.getHasDirtyData()) {
        this->_currentNote.storeNewText(std::move(text));
        this->_currentNote.refetch();
        this->_currentNoteLastEdited = QDateTime::currentDateTime();
        _noteViewNeedsUpdate = true;

        handleNoteTextChanged();
    }
}

void MainWindow::handleNoteTextChanged() {
    QSettings settings;
    if (settings.value(QStringLiteral("notesPanelSort"), SORT_BY_LAST_CHANGE)
            .toInt() == SORT_BY_LAST_CHANGE) {
        makeCurrentNoteFirstInNoteList();
    } else if (Utils::Misc::isNoteListPreview()) {
        updateNoteTreeWidgetItem(_currentNote);
    }

    const QSignalBlocker blocker(ui->noteTreeWidget);
    Q_UNUSED(blocker)

    // update the note list tooltip of the note
    Utils::Gui::setTreeWidgetItemToolTipForNote(
        ui->noteTreeWidget->currentItem(), _currentNote, &_currentNoteLastEdited);
}

void MainWindow::on_action_Quit_triggered() {
    storeSettings();
    QApplication::quit();
}

void MainWindow::quitApp() {
    QApplication::quit();
}

void MainWindow::on_searchLineEdit_textChanged(const QString &arg1) {
    Q_UNUSED(arg1)
    filterNotes();
}

/**
 * Does the note filtering
 */
void MainWindow::filterNotes(bool searchForText) {
    ui->noteTreeWidget->scrollToTop();

    // filter the notes by text in the search line edit
    filterNotesBySearchLineEditText();

    // moved condition whether to filter notes by tag at all into
    // filterNotesByTag() -- it can now be used as a slot at startup
    filterNotesByTag();

    if (searchForText) {
        // let's highlight the text from the search line edit
        searchForSearchLineTextInNoteTextEdit();

        // prevent that the last occurrence of the search term is found
        // first, instead the first occurrence should be found first
        ui->noteTextEdit->searchWidget()->doSearchDown();
    }
}

/**
 * Checks if tagging is enabled
 */
bool MainWindow::isTagsEnabled() { return _taggingDockWidget->isVisible(); }

/**
 * Checks if the markdown view is enabled
 */
bool MainWindow::isMarkdownViewEnabled() {
    QSettings settings;
    return settings.value(QStringLiteral("markdownViewEnabled"), true).toBool();
}

/**
 * Checks if the note edit pane is enabled
 */
bool MainWindow::isNoteEditPaneEnabled() {
    return _noteEditIsCentralWidget ? true : _noteEditDockWidget->isVisible();
}

/**
 * Does the note filtering by text in the search line edit
 */
void MainWindow::filterNotesBySearchLineEditText() {
    const QString searchText = ui->searchLineEdit->text();

    QTreeWidgetItemIterator it(ui->noteTreeWidget);
    ui->noteTreeWidget->setColumnCount(1);

    // search notes when at least 2 characters were entered
    if (searchText.count() >= 2) {
        // open search dialog
        doSearchInNote(searchText);

        NoteMap* noteMap = NoteMap::getInstance();
        QVector<int> noteIdList = noteMap->searchInNotes(
            searchText);

        int columnWidth = ui->noteTreeWidget->columnWidth(0);
        ui->noteTreeWidget->setColumnCount(2);
        int maxWidth = 0;
        const QStringList searchTextTerms =
            NoteMap::buildQueryStringList(searchText);
        const QSettings settings;
        const bool showMatches =
            settings.value(QStringLiteral("showMatches"), true).toBool();

        while (*it) {
            QTreeWidgetItem *item = *it;

            // skip note folders (if they are also shown in the note list)
            if (item->data(0, Qt::UserRole + 1) != NoteType) {
                ++it;
                continue;
            }

            const int noteId = item->data(0, Qt::UserRole).toInt();
            bool isHidden = noteIdList.indexOf(noteId) < 0;

            // hide all filtered notes
            item->setHidden(isHidden);

            // count occurrences of search terms in notes
            if (!isHidden && showMatches) {
                const Note note = NoteMap::getInstance()->fetchNoteById(noteId);
                item->setForeground(1, QColor(Qt::gray));
                int count = 0;

                for (QString word : searchTextTerms) {
                    if (NoteMap::isNameSearch(word)) {
                        word = NoteMap::removeNameSearchPrefix(word);
                    }

                    count += note.countSearchTextInNote(word);
                }

                const QString text = QString::number(count);
                item->setText(1, text);

                const QString &toolTipText =
                    searchTextTerms.count() == 1
                        ? tr("Found <strong>%n</strong> occurrence(s) of "
                             "<strong>%1</strong>",
                             "", count)
                              .arg(searchText)
                        : tr("Found <strong>%n</strong> occurrence(s) of any "
                             "term of <strong>%1</strong>",
                             "", count)
                              .arg(searchText);
                item->setToolTip(1, toolTipText);

                // calculate the size of the search count column
                QFontMetrics fm(item->font(1));

#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
                maxWidth = std::max(maxWidth, fm.width(text));
#else
                maxWidth = std::max(maxWidth, fm.horizontalAdvance(text));
#endif
            }

            ++it;
        }

        // resize the column 0 so we can see the search counts
        columnWidth = std::max(10, columnWidth - maxWidth - 5);
        //        ui->noteTreeWidget->resizeColumnToContents(1);
        ui->noteTreeWidget->setColumnWidth(0, columnWidth);
        ui->noteTreeWidget->setColumnWidth(1, maxWidth);
    } else {
        // otherwise show all items
        while (*it) {
            (*it)->setHidden(false);
            ++it;
        }
    }
}

/**
 * Opens the search widget in the the current note and searches for all
 * occurrences of the words in the search text
 *
 * @param searchText
 */
void MainWindow::doSearchInNote(QString searchText) {
    const QStringList searchTextTerms =
        NoteMap::buildQueryStringList(searchText, true, true);

    if (searchTextTerms.count() > 1) {
        QString localSearchTerm = QStringLiteral("(") +
                                  searchTextTerms.join(QStringLiteral("|")) +
                                  QStringLiteral(")");
        activeNoteTextEdit()->doSearch(
            localSearchTerm, QPlainTextEditSearchWidget::RegularExpressionMode);
    } else {
        if (NoteMap::isNameSearch(searchText)) {
            searchText = NoteMap::removeNameSearchPrefix(searchText);
        }

        activeNoteTextEdit()->doSearch(searchText.remove(QStringLiteral("\"")));
    }
}

/**
 * Does the note filtering by tags
 */
void MainWindow::filterNotesByTag() {
    if (!isTagsEnabled()) {
        return;    // do nothing
    }

    TagMap* tagMap = TagMap::getInstance();
    QString activeTag = tagMap->getActiveTag();

    QVector<int> noteIdList;

    if (activeTag == "AllNotes") {                   // TODO Fix to manage translations
            // don't do any additional filtering here
            return;
    }
    else if (activeTag == "UntaggedNotes") {                     // TODO Fix the bug that prevent the Untagged Notes item to appear in the tag tree (see vs QOWnnotes
            // get all note names that are not tagged
            noteIdList = Note::fetchAllNotTaggedIds();
    }
    else {
        // check for multiple active;
        const auto selectedItems = ui->tagTreeWidget->selectedItems();
        QVector<QString> tags;

        if (selectedItems.count() > 1) {
            for (auto *i : selectedItems) {
                const QString tag = i->data(0, Qt::UserRole).toString();
                tags << tag;
            }
        } else {
            // check if there is an active tag
            if (activeTag == "") {
                return;
            }
            tags << activeTag;
        }

        qDebug() << __func__ << " - 'tags': " << tags;

        for (const QString tag : tags) {
            noteIdList << tagMap->fetchAllLinkedNotes(tag);
        }
    }

    qDebug() << __func__ << " - 'noteIdList': " << noteIdList;

    // omit the already hidden notes
    QTreeWidgetItemIterator it(ui->noteTreeWidget,
                               QTreeWidgetItemIterator::NotHidden);

    // loop through all visible notes
    while (*it) {
        if ((*it)->data(0, Qt::UserRole + 1) != NoteType) {
            ++it;
            continue;
        }

        if (!noteIdList.contains((*it)->data(0, Qt::UserRole).toInt())) {
            (*it)->setHidden(true);
        }

        ++it;
    }
}

//
// set focus on search line edit if Ctrl + Shift + F was pressed
//
void MainWindow::on_action_Find_note_triggered() {
    changeDistractionFreeMode(false);
    this->ui->searchLineEdit->setFocus();
    this->ui->searchLineEdit->selectAll();
}

//
// jump to found note or create a new one if not found
//
void MainWindow::on_searchLineEdit_returnPressed() { jumpToNoteOrCreateNew(); }

/**
 * Jumps to found note or create a new one if not found
 */
void MainWindow::jumpToNoteOrCreateNew(bool disableLoadNoteDirectoryList) {
    // ignore if `return` was pressed in the completer
    if (_searchLineEditFromCompleter) {
        _searchLineEditFromCompleter = false;
        return;
    }

    const QString text = ui->searchLineEdit->text().trimmed();

    // prevent creation of broken note text files
    if (text.isEmpty()) {
        return;
    }

    // this doesn't seem to work with note sub folders
    const QSignalBlocker blocker(noteDirectoryWatcher);
    Q_UNUSED(blocker)

    // add the current search text to the saved searches
    storeSavedSearch();

    // clear search line edit so all notes will be viewed again and to prevent
    // a brief appearing of the note search widget when creating a new note
    // with action_New_note
    ui->searchLineEdit->clear();

    // first let us search for the entered text
    NoteMap* noteMap = NoteMap::getInstance();
    Note note = noteMap->fetchNoteByName(text);

    // if we can't find a note we create a new one
    if (note.getId() == 0) {
        // check if a hook wants to set the text
        QString noteText = Note::createNoteHeader(text);
		noteText.append(Note::createNoteFooter());

        note = Note();
        note.setName(text);
        note.setNoteText(noteText);
        note.store();

        // workaround when signal block doesn't work correctly
        directoryWatcherWorkaround(true);

        // we even need a 2nd workaround because something triggers that the
        // note folder was modified
        noteDirectoryWatcher.removePath(notesPath);
        noteDirectoryWatcher.removePath(notesPath);

        // store the note to disk
        // if a tag is selected add the tag to the just created note
        TagMap* tagMap = TagMap::getInstance();
        QString activeTag = tagMap->getActiveTag();
        if (activeTag != "") {
            note.addTag(activeTag);
        }

        const bool noteWasStored = note.storeNoteTextFileToDisk();
        showStatusBarMessage(
            noteWasStored ? tr("Stored current note to disk")
                          : tr("Current note could not be stored to disk"),
            3000);

        const QSignalBlocker blocker2(ui->noteTreeWidget);
        Q_UNUSED(blocker2)

        // adds the note to the note tree widget
        addNoteToNoteTreeWidget(note);

        ui->kbGraphView->addNoteToGraph(text);

        //        buildNotesIndex();
        if (!disableLoadNoteDirectoryList) {
            loadNoteDirectoryList();
        }

        // fetch note new (because all the IDs have changed after
        // the buildNotesIndex()
        //        note.refetch();

        // add the file to the note directory watcher
        noteDirectoryWatcher.addPath(note.fullNoteFilePath());

        // add the paths from the workaround
        noteDirectoryWatcher.addPath(notesPath);
        noteDirectoryWatcher.addPath(notesPath);

        // turn on the method again
        directoryWatcherWorkaround(false);
    }

    // jump to the found or created note
    setCurrentNote(std::move(note));

    // hide the search widget after creating a new note
    activeNoteTextEdit()->hideSearchWidget(true);

    // focus the note text edit and set the cursor correctly
    focusNoteTextEdit();
}

void MainWindow::on_action_Remove_note_triggered() { removeCurrentNote(); }

void MainWindow::on_actionAbout_PKbSuite_triggered() {
    auto *dialog = new AboutDialog(this);
    dialog->exec();
    delete (dialog);
}

/**
 * Triggered by the shortcut to create a new note with date in the headline
 */
void MainWindow::on_action_New_note_triggered() {
    QSettings settings;
    const bool newNoteAskHeadline =
        settings.value(QStringLiteral("newNoteAskHeadline")).toBool();

    // check if we want to ask for a headline
    if (newNoteAskHeadline) {
        bool ok;
        QString headline =
            QInputDialog::getText(this, tr("New note"), tr("Note headline"),
                                  QLineEdit::Normal, QString(), &ok);

        if (!ok) {
            return;
        }

        if (!headline.isEmpty()) {
            createNewNote(headline, false);
            return;
        }
    }

    // create a new note
    createNewNote();
}

/**
 * Creates a new note
 *
 * @param noteName
 */
void MainWindow::createNewNote(QString noteName, bool withNameAppend) {
    // show the window in case we are using the system tray
    show();

    if (noteName.isEmpty()) {
        noteName = QStringLiteral("Note");
    }

    if (withNameAppend) {
        QDateTime currentDate = QDateTime::currentDateTime();

        // replacing ":" with "_" for Windows systems
        noteName = noteName + QStringLiteral(" ") +
                   currentDate.toString("yyyyMMddhhmmss")
                       .replace(QStringLiteral(":"), QStringLiteral("."));
    }

    const QSignalBlocker blocker(ui->searchLineEdit);
    Q_UNUSED(blocker)

    ui->searchLineEdit->setText(noteName);

    // create a new note or jump to the existing
    jumpToNoteOrCreateNew();
}

void MainWindow::on_actionNew_note_from_selected_text_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QString selectedText = textEdit->textCursor().selectedText();
    if (!selectedText.isEmpty()) {
        textEdit->textCursor().removeSelectedText();
        const QString strLink = "[[" + selectedText + "]]";
        textEdit->textCursor().insertText(strLink);

        createNewNote(selectedText, false);
    }

    openCurrentNoteInTab();
}

/*
 * Handles urls in the noteTextView
 *
 * examples:
 * - <note://MyNote> opens the note "MyNote"
 * - <note://my-note-with-spaces-in-the-name> opens the note "My Note with
 * spaces in the name"
 * - <https://www.pkbsuite.org> opens the web page
 * - <file:///path/to/my/note/folder/sub-folder/My%20note.pdf> opens the note
 * "My note" in the sub-folder "sub-folder"
 * - <file:///path/to/my/file/PKbSuite.pdf> opens the file
 * "/path/to/my/file/PKbSuite.pdf" if the operating system supports that
 * handler
 */
void MainWindow::on_noteTextView_anchorClicked(const QUrl &url) {
    qDebug() << __func__ << " - 'url': " << url;
    const QString scheme = url.scheme();

    if ((scheme == QStringLiteral("note") ||
         scheme == QStringLiteral("noteid") ||
         scheme == QStringLiteral("checkbox")) ||
        (scheme == QStringLiteral("file") &&
         fileUrlIsNoteInCurrentNoteFolder(url))) {
        openLocalUrl(url.toString());
    } else {
        ui->noteTextEdit->openUrl(url.toString());
    }
}


/**
 * Returns the a list of the custom note file extensions
 */
QStringList MainWindow::customNoteFileExtensionList(const QString &prefix) {
    const QSettings settings;
    QStringList list =
        settings.value(QStringLiteral("customNoteFileExtensionList"))
            .toStringList();
    list.removeDuplicates();

    if (!prefix.isEmpty()) {
        list.replaceInStrings(QRegularExpression(QStringLiteral("^")), prefix);
    }

    return list;
}

bool MainWindow::fileUrlIsNoteInCurrentNoteFolder(const QUrl &url) {
    if (url.scheme() != QStringLiteral("file")) {
        return false;
    }

    const QString path = url.toLocalFile();

    if (!path.startsWith(notesPath)) {
        return false;
    }

    QListIterator<QString> itr(customNoteFileExtensionList(
        QStringLiteral(".")));

    while (itr.hasNext()) {
        const auto fileExtension = itr.next();

        if (path.endsWith(fileExtension, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

bool MainWindow::fileUrlIsExistingNoteInCurrentNoteFolder(const QUrl &url) {
    if (url.scheme() != QStringLiteral("file")) {
        return false;
    }

    const QString path = url.toLocalFile();
    if (!QFile(path).exists()) {
        return false;
    }

    return path.startsWith(notesPath) &&
           path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive);
}

/*
 * Handles note urls
 *
 * examples:
 * - <note://MyNote> opens the note "MyNote"
 * - <note://my-note-with-spaces-in-the-name> opens the note "My Note with
 * spaces in the name"
 */
void MainWindow::openLocalUrl(QString urlString) {
    if (urlString.isEmpty()) {
        return;
    }

    bool urlWasNotValid = false;
    QString fragment;

    // if urlString is no valid url we will try to convert it into a note file
    // url
    if (!PKbSuiteMarkdownTextEdit::isValidUrl(urlString)) {
        fragment = Note::getURLFragmentFromFileName(urlString);
        urlString = _currentNote.getFileURLFromFileName(urlString, true);
        urlWasNotValid = true;
    }
    
    QUrl url = QUrl(urlString);

    // If url was valid we want to get the fragment directly from the url
    if (!urlWasNotValid) {
        fragment = url.fragment();
    }

    const bool isExistingNoteFileUrl = fileUrlIsExistingNoteInCurrentNoteFolder(url);
    const bool isNoteFileUrl = fileUrlIsNoteInCurrentNoteFolder(url);

    // convert relative file urls to absolute urls and open them
    if (urlString.startsWith(QStringLiteral("file://..")) && !isExistingNoteFileUrl) {
        QString windowsSlash = QString();

        urlString.replace(QLatin1String("file://.."),
                          QStringLiteral("file://") + windowsSlash +
                              notesPath +
                              QStringLiteral("/.."));

        QDesktopServices::openUrl(QUrl(urlString));
        return;
    }

    const QString scheme = url.scheme();

    if (scheme == QStringLiteral("noteid")) {    // jump to a note by note id
        static const QRegularExpression re(QStringLiteral(R"(^noteid:\/\/note-(\d+)$)"));
        QRegularExpressionMatch match = re.match(urlString);

        if (match.hasMatch()) {
            int noteId = match.captured(1).toInt();
            Note note = NoteMap::getInstance()->fetchNoteById(noteId);
            if (note.isFetched()) {
                // set current note
                setCurrentNote(std::move(note));
            }
        } else {
            qDebug() << "malformed url: " << urlString;
        }
    } else if (scheme == QStringLiteral("checkbox")) {
        const auto text = ui->noteTextEdit->toPlainText();

        int index = url.host().midRef(1).toInt();
#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
        QRegExp re(R"((^|\n)\s*[-*+]\s\[([xX ]?)\])", Qt::CaseInsensitive);
#else
        static const QRegularExpression re(R"((^|\n)\s*[-*+]\s\[([xX ]?)\])", QRegularExpression::CaseInsensitiveOption);
#endif
        int pos = 0;
        while (true) {

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
            pos = re.indexIn(text, pos);
#else
            QRegularExpressionMatch match;
            pos = text.indexOf(re, pos, &match);
#endif
            if (pos == -1)    // not found
                return;
            auto cursor = ui->noteTextEdit->textCursor();

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
            int matchedLength = re.matchedLength();
            cursor.setPosition(pos + re.matchedLength() - 1);
#else
            int matchedLength = match.capturedLength();
            qDebug() << __func__ << "match.capturedLength(): " << match.capturedLength();
            cursor.setPosition(pos + match.capturedLength() - 1);
#endif
            if (cursor.block().userState() ==
                MarkdownHighlighter::HighlighterState::List) {
                if (index == 0) {

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
                    auto ch = re.cap(2);
#else
                    auto ch = match.captured(2);
#endif
                    if (ch.isEmpty())
                        cursor.insertText(QStringLiteral("x"));
                    else {
                        cursor.movePosition(QTextCursor::PreviousCharacter,
                                            QTextCursor::KeepAnchor);
                        cursor.insertText(ch == QStringLiteral(" ")
                                              ? QStringLiteral("x")
                                              : QStringLiteral(" "));
                    }

                    // refresh instantly
                    _noteViewUpdateTimer->start(1);
                    break;
                }
                --index;
            }
            pos += matchedLength;
        }
    } else if (scheme == QStringLiteral("file") && urlWasNotValid) {
		// First, handle the case of a note file and create it if it doesn't exists
        Note note;

        if (isNoteFileUrl) {
            note = NoteMap::getInstance()->fetchByFileUrl(url);
        } else {
            // try to fetch a note from the url string
            note = Note::fetchByUrlString(urlString);
        }

        // does this note really exist?
        if (note.isFetched()) {
            // set current note
            setCurrentNote(std::move(note));
			return;
        } else {
            // if the name of the linked note only consists of numbers we cannot
            // use host() to get the filename, it would get converted to an
            // ip-address
            QRegularExpressionMatch match =			// TODO Check management of links to URL without 3 "/"
                QRegularExpression(QStringLiteral(R"(^\w+:\/\/(\d+)$)"))
                    .match(urlString);
            QString fileName =
                match.hasMatch() ? match.captured(1) : QFileInfo(url.path()).baseName();

            // try to generate a useful title for the note
            fileName = Utils::Misc::toStartCase(
                fileName.replace(QStringLiteral("_"), QStringLiteral(" ")));

            // ask if we want to create a new note if note wasn't found
            if (Utils::Gui::question(this, tr("Note was not found"),
                                     tr("Note was not found, create new note "
                                        "<strong>%1</strong>?")
                                         .arg(fileName),
                                     QStringLiteral("open-url-create-note")) ==
                QMessageBox::Yes) {
                return createNewNote(fileName, false);
            }
        }	
        
        // open urls that previously were not valid
        QDesktopServices::openUrl(QUrl(urlString));
    }
}

void MainWindow::on_actionAlphabetical_triggered(bool checked) {
    if (checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("notesPanelSort"), SORT_ALPHABETICAL);
        loadNoteDirectoryList();
    }

    // update the visibility of the note sort order selector
    updateNoteSortOrderSelectorVisibility(checked);
}

void MainWindow::on_actionBy_date_triggered(bool checked) {
    if (checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("notesPanelSort"),
                          SORT_BY_LAST_CHANGE);
        loadNoteDirectoryList();
    }

    // update the visibility of the note sort order selector
    updateNoteSortOrderSelectorVisibility(!checked);
}

void MainWindow::systemTrayIconClicked(
    QSystemTrayIcon::ActivationReason reason) {
    // don't show or hide the app on OS X with a simple click because also the
    // context menu will be triggered
#ifndef Q_OS_MAC
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible() && !isMinimized()) {
            this->hide();
        } else {
            showWindow();
        }
    }
#else
    Q_UNUSED(reason);
#endif
}

/**
 * Shows the window (also brings it to the front and un-minimizes it)
 */
void MainWindow::showWindow() {
    // show the window in case we are using the system tray
    show();

    // bring application window to the front
    activateWindow();    // for Windows
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();             // for MacOS

    // parse the current note for the navigation panel in case it wasn't parsed
    // while the mainwindow was hidden (https://github.com/pbek/QOwnNotes/issues/2110)
    startNavigationParser();
}

/**
 * Generates the system tray context menu
 */
void MainWindow::generateSystemTrayContextMenu() {
    // trying to destroy the old context menu as fix for Ubuntu 14.04
    // just clearing an existing menu resulted in empty sub-menus
    //    QMenu *menu = trayIcon->contextMenu();
    //    delete(menu);

    // QMenu(this) is not allowed here or it will not be recognized as child of
    // the tray icon later (see: https://github.com/pbek/PKbSuite/issues/1239)
    auto *menu = new QMenu();
    menu->setTitle(QStringLiteral("PKbSuite"));

    // add menu entry to open the app
    QAction *openAction = menu->addAction(tr("Open PKbSuite"));
    openAction->setIcon(getSystemTrayIcon());

    connect(openAction, &QAction::triggered, this, &MainWindow::showWindow);

    // add menu entry to create a new note
    QAction *createNoteAction = menu->addAction(tr("New note"));
    createNoteAction->setIcon(QIcon::fromTheme(
        QStringLiteral("document-new"),
        QIcon(
            QStringLiteral(":icons/breeze-pkbsuite/16x16/document-new.svg"))));

    connect(createNoteAction, &QAction::triggered, this,
            &MainWindow::on_action_New_note_triggered);

    int maxNotes = NoteMap::getInstance()->countAll();

    if (maxNotes > 0) {
        if (maxNotes > 9) {
            maxNotes = 9;
        }

        // add a menu for recent notes
        QMenu *noteMenu = menu->addMenu(tr("Recent notes"));

        NoteMap* noteMap = NoteMap::getInstance();
        const auto noteList = noteMap->fetchAllNotes(maxNotes);

        for (const Note &note : noteList) {
            QAction *action = noteMenu->addAction(note.getName());
            action->setIcon(_noteIcon);
            int noteId = note.getId();
            connect(action, &QAction::triggered, this,
                    [this, noteId]() { setCurrentNoteFromNoteId(noteId); });
        }
    }

	menu->addSeparator();

    // add menu entry to quit the app
    QAction *quitAction = menu->addAction(tr("Quit"));
    quitAction->setIcon(QIcon::fromTheme(
        QStringLiteral("application-exit"),
        QIcon(QStringLiteral(
            ":icons/breeze-pkbsuite/16x16/application-exit.svg"))));
    connect(quitAction, &QAction::triggered, this,
            &MainWindow::on_action_Quit_triggered);

    trayIcon->setContextMenu(menu);
}

void MainWindow::on_action_Settings_triggered() {
    // open the settings dialog
    openSettingsDialog();
}

void MainWindow::on_actionSelect_all_notes_triggered() { selectAllNotes(); }

/**
 * Creates the additional menu entries for the note text edit field
 *
 * @param pos
 */
void MainWindow::on_noteTextEdit_customContextMenuRequested(const QPoint pos) {
    noteTextEditCustomContextMenuRequested(ui->noteTextEdit, pos);
}

/**
 * Creates the additional menu entries for a note text edit field
 *
 * @param noteTextEdit
 * @param pos
 */
void MainWindow::noteTextEditCustomContextMenuRequested(
    PKbSuiteMarkdownTextEdit *noteTextEdit, const QPoint pos) {
    const QPoint globalPos = noteTextEdit->mapToGlobal(pos);
    QMenu *menu = noteTextEdit->createStandardContextMenu();
    const bool isAllowNoteEditing = Utils::Misc::isNoteEditingAllowed();
    const bool isTextSelected = isNoteTextSelected();

    const QString createNoteFromSelectedText =
        isTextSelected ? tr("New note from selected text"):tr("");
    QAction *createNoteFromSelectedTextAction = menu->addAction(createNoteFromSelectedText);
    createNoteFromSelectedTextAction->setShortcut(ui->actionNew_note_from_selected_text->shortcut());
    createNoteFromSelectedTextAction->setEnabled(isAllowNoteEditing);

    const QString linkTextActionName =
        isTextSelected ? tr("&Link selected text") : tr("Insert &link");
    QAction *linkTextAction = menu->addAction(linkTextActionName);
    linkTextAction->setShortcut(ui->actionInsert_text_link->shortcut());
    linkTextAction->setEnabled(isAllowNoteEditing);

    QString blockQuoteTextActionName =
        isTextSelected ? tr("Block &quote selected text",
                            "Action to apply a block quote formatting to the "
                            "selected text")
                       : tr("Insert block &quote");
    QAction *blockQuoteTextAction = menu->addAction(blockQuoteTextActionName);
    blockQuoteTextAction->setShortcut(ui->actionInsert_block_quote->shortcut());
    blockQuoteTextAction->setEnabled(isAllowNoteEditing);

    QAction *searchAction =
        menu->addAction(ui->actionSearch_text_on_the_web->text());
    searchAction->setShortcut(ui->actionSearch_text_on_the_web->shortcut());
    searchAction->setEnabled(isTextSelected);

    QAction *copyCodeBlockAction = menu->addAction(tr("Copy code block"));
    copyCodeBlockAction->setIcon(QIcon::fromTheme(
        QStringLiteral("edit-copy"),
        QIcon(QStringLiteral(":icons/breeze-pkbsuite/16x16/edit-copy.svg"))));
    const QTextBlock &currentTextBlock =
        noteTextEdit->cursorForPosition(pos).block();
    const int userState = currentTextBlock.userState();
    const bool isCodeSpan = ui->noteTextEdit->highlighter()->isPosInACodeSpan(currentTextBlock.blockNumber(),
                                                                              noteTextEdit->cursorForPosition(pos).positionInBlock());

    copyCodeBlockAction->setEnabled(
        MarkdownHighlighter::isCodeBlock(userState) || isCodeSpan);

    menu->addSeparator();

    // add the print menu
    QMenu *printMenu = menu->addMenu(tr("Print"));
    QIcon printIcon = QIcon::fromTheme(
        QStringLiteral("document-print"),
        QIcon(QStringLiteral(
            ":icons/breeze-pkbsuite/16x16/document-print.svg")));
    printMenu->setIcon(printIcon);

    // add the print selected text action
    QAction *printTextAction = printMenu->addAction(tr("Print selected text"));
    printTextAction->setEnabled(isTextSelected);
    printTextAction->setIcon(printIcon);

    // add the print selected text (preview) action
    QAction *printHTMLAction =
        printMenu->addAction(tr("Print selected text (preview)"));
    printHTMLAction->setEnabled(isTextSelected);
    printHTMLAction->setIcon(printIcon);

    // add the export menu
    QMenu *exportMenu = menu->addMenu(tr("Export"));
    exportMenu->setIcon(QIcon::fromTheme(
        QStringLiteral("document-export"),
        QIcon(QStringLiteral(
            ":icons/breeze-pkbsuite/16x16/document-export.svg"))));

    QIcon pdfIcon = QIcon::fromTheme(
        QStringLiteral("application-pdf"),
        QIcon(QStringLiteral(
            ":icons/breeze-pkbsuite/16x16/application-pdf.svg")));

    // add the export selected text action
    QAction *exportTextAction =
        exportMenu->addAction(tr("Export selected text as PDF"));
    exportTextAction->setEnabled(isTextSelected);
    exportTextAction->setIcon(pdfIcon);

    // add the export selected text (preview) action
    QAction *exportHTMLAction =
        exportMenu->addAction(tr("Export selected text as PDF (preview)"));
    exportHTMLAction->setEnabled(isTextSelected);
    exportHTMLAction->setIcon(pdfIcon);

    menu->addSeparator();

    // add some other existing menu entries
    menu->addAction(ui->actionPaste_image);
    menu->addAction(ui->actionAutocomplete);
    menu->addAction(ui->actionSplit_note_at_cursor_position);

    // add the custom actions to the context menu
    if (!_noteTextEditContextMenuActions.isEmpty()) {
        menu->addSeparator();

        for (QAction *action :
             Utils::asConst(_noteTextEditContextMenuActions)) {
            menu->addAction(action);
        }
    }

    QAction *selectedItem = menu->exec(globalPos);
    if (selectedItem) {
        if (selectedItem == linkTextAction) {
            // handle the linking of text with a note
            handleTextNoteLinking();
        }
        else if (selectedItem == createNoteFromSelectedTextAction) {
            // handle the block quoting of text
            on_actionNew_note_from_selected_text_triggered();
        } else if (selectedItem == blockQuoteTextAction) {
            // handle the block quoting of text
            on_actionInsert_block_quote_triggered();
        } else if (selectedItem == searchAction) {
            // search for the selected text on the web
            on_actionSearch_text_on_the_web_triggered();
        } else if (selectedItem == printTextAction) {
            // print the selected text
            auto *textEdit = new PKbSuiteMarkdownTextEdit(this);
            textEdit->setPlainText(selectedNoteTextEditText());
            printNote(textEdit);
        } else if (selectedItem == printHTMLAction) {
            // print the selected text (preview)
            QString html = _currentNote.textToMarkdownHtml(
                selectedNoteTextEditText(), notesPath,
                getMaxImageWidth(),
                Utils::Misc::useInternalExportStylingForPreview());
            auto *textEdit = new QTextEdit(this);
            textEdit->setHtml(html);
            printNote(textEdit);
        } else if (selectedItem == exportTextAction) {
            // export the selected text as PDF
            auto *textEdit = new PKbSuiteMarkdownTextEdit(this);
            textEdit->setPlainText(selectedNoteTextEditText());
            exportNoteAsPDF(textEdit);
        } else if (selectedItem == exportHTMLAction) {
            // export the selected text (preview) as PDF
            QString html = _currentNote.textToMarkdownHtml(
                selectedNoteTextEditText(), notesPath,
                getMaxImageWidth(),
                Utils::Misc::useInternalExportStylingForPreview());
            html = Utils::Misc::parseTaskList(html, false);
            auto *textEdit = new QTextEdit(this);
            textEdit->setHtml(html);
            exportNoteAsPDF(textEdit);
        } else if (selectedItem == copyCodeBlockAction) {
            // copy the text from a copy block around currentTextBlock to the
            // clipboard
            if (isCodeSpan) {
                const auto codeSpanRange = ui->noteTextEdit->highlighter()->getSpanRange(MarkdownHighlighter::RangeType::CodeSpan,
                                                                                         currentTextBlock.blockNumber(),
                                                                                         noteTextEdit->cursorForPosition(pos).positionInBlock());
                QApplication::clipboard()->setText(currentTextBlock.text().mid(codeSpanRange.first + 1,
                                                                               codeSpanRange.second - codeSpanRange.first - 1));
            } else {
                Utils::Gui::copyCodeBlockText(currentTextBlock);
            }
        }
    }
}

/**
 * Checks if text in a note is selected
 *
 * @return
 */
bool MainWindow::isNoteTextSelected() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    const QString selectedText =
        textEdit->textCursor().selectedText().trimmed();
    return !selectedText.isEmpty();
}

void MainWindow::on_actionInsert_text_link_triggered() {
    // handle the linking of text
    handleTextNoteLinking(LinkDialog::TextLinkPage);
}

void MainWindow::on_actionInsert_note_link_triggered() {
    // handle the linking of a note
    handleTextNoteLinking(LinkDialog::NoteLinkPage);
}

void MainWindow::on_action_DuplicateText_triggered() {
    activeNoteTextEdit()->duplicateText();
}

void MainWindow::on_action_Back_in_note_history_triggered() {
    if (this->noteHistory.back()) {
        ui->searchLineEdit->clear();
        setCurrentNoteFromHistoryItem(
            this->noteHistory.getCurrentHistoryItem());
    }
}

void MainWindow::on_action_Forward_in_note_history_triggered() {
    if (this->noteHistory.forward()) {
        ui->searchLineEdit->clear();
        setCurrentNoteFromHistoryItem(
            this->noteHistory.getCurrentHistoryItem());
    }
}

void MainWindow::on_action_Shortcuts_triggered() {
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://www.pkbsuite.org/shortcuts/PKbSuite")));
}

void MainWindow::on_action_Knowledge_base_triggered() {
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://www.pkbsuite.org/Knowledge-base")));
}

/**
 * Inserts the current date
 */
void MainWindow::on_actionInsert_current_time_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();
    const QDateTime dateTime = QDateTime::currentDateTime();
    QSettings settings;
    const QString format =
        settings.value(QStringLiteral("insertTimeFormat")).toString();
    const QString text = format.isEmpty()
                             ? dateTime.toString(Qt::SystemLocaleShortDate)
                             : dateTime.toString(format);

    // insert the current date
    c.insertText(text);
}

/**
 * @brief Exports the current note as PDF (markdown)
 */
void MainWindow::on_action_Export_note_as_PDF_markdown_triggered() {
    auto doc = getDocumentForPreviewExport();
    exportNoteAsPDF(doc);
    doc->deleteLater();
}

/**
 * @brief Exports the current note as PDF (text)
 */
void MainWindow::on_action_Export_note_as_PDF_text_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    exportNoteAsPDF(textEdit);
}

QTextDocument *MainWindow::getDocumentForPreviewExport() {
    QString html = _currentNote.toMarkdownHtml(
        notesPath, getMaxImageWidth(),
        Utils::Misc::useInternalExportStylingForPreview());
    html = Utils::Misc::parseTaskList(html, false);

    // Windows 10 has troubles with the QTextDocument from the QTextBrowser
    // see: https://github.com/pbek/QOwnNotes/issues/2015
//    auto doc = ui->noteTextView->document()->clone();
    auto doc = new QTextDocument(this);
    doc->setHtml(html);

    return doc;
}

/**
 * @brief Prints the current note (markdown)
 */
void MainWindow::on_action_Print_note_markdown_triggered() {
    auto doc = getDocumentForPreviewExport();
    printTextDocument(doc);
    doc->deleteLater();
}

/**
 * @brief Prints the current note (text)
 */
void MainWindow::on_action_Print_note_text_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    printNote(textEdit);
}

/**
 * @brief Inserts a chosen image at the current cursor position in the note text
 * edit
 */
void MainWindow::on_actionInsert_image_triggered() {
    auto *dialog = new ImageDialog(this);
    const int ret = dialog->exec();

    if (ret == QDialog::Accepted) {
        QString title = dialog->getImageTitle();

        if (dialog->isDisableCopying()) {
            QString pathOrUrl = dialog->getFilePathOrUrl();
            auto url = QUrl(pathOrUrl);

            if (!url.isValid()) {
                return;
            }

            if (url.scheme() == QStringLiteral("file")) {
                pathOrUrl = url.toLocalFile();
            }

            if (!url.scheme().startsWith(QStringLiteral("http"))) {
                pathOrUrl = _currentNote.relativeFilePath(pathOrUrl);
            }

#ifdef Q_OS_WIN32
            // make sure a local path on a different drive really works
            if (Utils::Misc::fileExists(pathOrUrl)) {
                pathOrUrl = QUrl::toPercentEncoding(pathOrUrl).prepend("file:///");
            }
#endif

            // title must not be empty
            if (title.isEmpty()) {
                title = QStringLiteral("img");
            }

            insertNoteText(QStringLiteral("![") + title + QStringLiteral("](") +
                           pathOrUrl + QStringLiteral(")"));
        } else {
            QFile *file = dialog->getImageFile();

            if (file->size() > 0) {
                insertImage(file, title);
            }
        }
    }

    delete (dialog);
}

/**
 * Inserts an image file into the current note
 */
bool MainWindow::insertImage(QFile *file, QString title) {
    QString text =
        _currentNote.getInsertEmbedmentMarkdown(file, mediaType::image, true, true, false, std::move(title));

    if (!text.isEmpty()) {
        qDebug() << __func__ << " - 'text': " << text;

        insertNoteText(text);

        return true;
    }

    return false;
}

/**
 * Inserts a PDF file into a note
 * - Extracts the highlighted notes and comments
 * - Create BIBTex references
 */

bool MainWindow::insertPDF(QFile *file) {
    PDFFile pdfFile(file->fileName());
    
    if (pdfFile.hasAnnotations()) {
        DropPDFDialog* dialog = new DropPDFDialog(this);
        
        int iResult = dialog->exec();
        if (iResult == DropPDFDialog::idLink) {
            // If the annotations have not been processed, we just add a link to the PDF file
            QString text = _currentNote.getInsertEmbedmentMarkdown(file, mediaType::pdf, false);
            if (!text.isEmpty()) {
                qDebug() << __func__ << " - 'text': " << text;

                PKbSuiteMarkdownTextEdit* textEdit = activeNoteTextEdit();
                QTextCursor c = textEdit->textCursor();

                // if we try to insert PDF in the first line of the note (aka.
                // note name) move the cursor to the last line
                if (currentNoteLineNumber() == 1) {
                    c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
                    textEdit->setTextCursor(c);
                }

                // insert the PDF link
                c.insertText(text);
            }
        } else if (iResult == DropPDFDialog::idCreateNote) {
            QFileInfo pdfFileInfo(file->fileName());
			
            QFileInfo noteFileInfo(notesPath + "/" + pdfFileInfo.baseName() + ".md");
            if (noteFileInfo.exists()) {
                QMessageBox::warning(this, QStringLiteral("Erreur"), QStringLiteral("Attention : la note existe déjà dans la KB. Supprimez la note existante ou renommez l'une des deux."));
            }
            else {
				// La note n'existe pas, on la crée
                Note note = Note();
                note.setName(pdfFileInfo.baseName());

                QString noteText = pdfFileInfo.baseName();

                // check if a hook changed the text
                if (noteText.isEmpty()) {
                    // fallback to the old text if no hook changed the text
                    noteText = Note::createNoteHeader(pdfFileInfo.baseName());
                } else {
                    noteText.append("\n=====\n");
                }
				const QString embedmentLink = note.getInsertEmbedmentMarkdown(file, mediaType::pdf, dialog->copyFileToKb(), false);

				noteText.append("\n-----\n");
                noteText.append("**Title:** " + pdfFile.title() + "  \n");
                noteText.append("**Creation date:** " + QDateTime::currentDateTime().toString("yyyy-MM-ddThh:mm:sst") + "  \n");
                noteText.append("**File:** " + embedmentLink + "  \n");
                noteText.append("**Subject:** " + pdfFile.subject() + "  \n");
                noteText.append("**Keywords:** " + pdfFile.keywords() + "  \n");
                noteText.append("**Tags:** #LECTURE, #TODO\n");
                noteText.append("**Author:** " + pdfFile.author() + "  \n");
				noteText.append("\n-----\n");

				pdfFile.setDocumentFolder(notesPath);
				
                noteText.append(pdfFile.markdownSummary());
                noteText.append(pdfFile.markdownCitations(embedmentLink));
                noteText.append(pdfFile.markdownComments(embedmentLink));
				
                note.setNoteText(noteText);

                note.store();
                ui->kbGraphView->addNoteToGraph(pdfFileInfo.baseName());

                // workaround when signal block doesn't work correctly
                _isNotesDirectoryWasModifiedDisabled = true;

                // we even need a 2nd workaround because something triggers that the
                // note folder was modified
                noteDirectoryWatcher.removePath(notesPath);
                
                // store the note to disk
                // if a tag is selected add the tag to the just created note
                TagMap* tagMap = TagMap::getInstance();
                QString activeTag = tagMap->getActiveTag();
                if (activeTag != "") {
                    note.addTag(activeTag);
                }

                note.storeNoteTextFileToDisk();
                showStatusBarMessage(
                        tr("stored current note to disk"), 3000);

                {
                    const QSignalBlocker blocker2(ui->noteTreeWidget);
                    Q_UNUSED(blocker2);

                    // adds the note to the note tree widget
                    MainWindow::addNoteToNoteTreeWidget(note);
                }

                loadNoteDirectoryList();

                // fetch note new (because all the IDs have changed after
                // the buildNotesIndex()
        //        note.refetch();

                // add the file to the note directory watcher
                noteDirectoryWatcher.addPath(note.fullNoteFilePath());

                // add the paths from the workaround
                noteDirectoryWatcher.addPath(notesPath);

                // turn on the method again
                _isNotesDirectoryWasModifiedDisabled = false;
                
                // jump to the found or created note
                setCurrentNote(note);

                // focus the note text edit and set the cursor correctly
                focusNoteTextEdit();
            }
        } else
            return false;
    } else {
        // If the annotations have not been processed, we just add a link to the PDF file
        QString text = _currentNote.getInsertEmbedmentMarkdown(file, mediaType::pdf, false);
        if (!text.isEmpty()) {
            qDebug() << __func__ << " - 'text': " << text;

            QMarkdownTextEdit* textEdit = activeNoteTextEdit();
            QTextCursor c = textEdit->textCursor();

            // if we try to insert PDF in the first line of the note (aka.
            // note name) move the cursor to the last line
            if (currentNoteLineNumber() == 1) {
                c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
                textEdit->setTextCursor(c);
            }

            // insert the PDF link
            c.insertText(text);
        }
    }
    
    return true;
}

void MainWindow::insertNoteText(const QString &text) {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();

    // if we try to insert text in the first line of the note (aka.
    // note name) move the cursor to the last line
    if (currentNoteLineNumber() == 1) {
        c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
        textEdit->setTextCursor(c);
    }

    // insert the image link
    c.insertText(text);
}

/**
 * Inserts a file attachment into the current note
 */
bool MainWindow::insertAttachment(QFile *file, const QString &title) {
    QString text = _currentNote.getInsertEmbedmentMarkdown(file, mediaType::attachment, true, false, false, title);

    if (!text.isEmpty()) {
        PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
        QTextCursor c = textEdit->textCursor();

        // if we try to insert the attachment in the first line of the note
        // (aka. note name) move the cursor to the last line
        if (currentNoteLineNumber() == 1) {
            c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
            textEdit->setTextCursor(c);
        }

        // add a space if we are not at the start of a line or if there is no
        // space in front of the current cursor position
        c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
        if (!c.atBlockStart() && c.selectedText() != QStringLiteral(" ")) {
            text = QStringLiteral(" ") + text;
        }

        // insert the attachment link
        c = textEdit->textCursor();
        c.insertText(text);

        return true;
    }

    return false;
}

/**
 * Returns the cursor's line number in the current note
 */
int MainWindow::currentNoteLineNumber() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    const QTextCursor cursor = textEdit->textCursor();

    QTextDocument *doc = textEdit->document();
    QTextBlock blk = doc->findBlock(cursor.position());
    QTextBlock blk2 = doc->begin();

    int i = 1;
    while (blk != blk2) {
        blk2 = blk2.next();
        ++i;
    }

    return i;
}

/**
 * @brief Opens a browser with the changelog page
 */
void MainWindow::on_actionShow_changelog_triggered() {
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://www.pkbsuite.org/changelog/PKbSuite")));
}

void MainWindow::on_action_Find_text_in_note_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    textEdit->searchWidget()->activate();
}

/**
 * Opens the current note in an external editor
 */
void MainWindow::on_action_Open_note_in_external_editor_triggered() {
    QSettings settings;
    const QString externalEditorPath =
        settings.value(QStringLiteral("externalEditorPath")).toString();

    // use the default editor if no other editor was set
    if (externalEditorPath.isEmpty()) {
        const QUrl url = _currentNote.fullNoteFileUrl();
        qDebug() << __func__ << " - 'url': " << url;

        // open note file in default application for the type of file
        QDesktopServices::openUrl(url);
    } else {
        const QString path = _currentNote.fullNoteFilePath();

        qDebug() << __func__
                 << " - 'externalEditorPath': " << externalEditorPath;
        qDebug() << __func__ << " - 'path': " << path;

        // open note file in external editor
        Utils::Misc::openFilesWithApplication(externalEditorPath,
                                              QStringList({path}));
    }
}

/**
 * Exports the current note as markdown file
 */
void MainWindow::on_action_Export_note_as_markdown_triggered() {
    FileDialog dialog(QStringLiteral("NoteMarkdownExport"));
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(tr("Markdown files") + " (*.md)");
    dialog.setWindowTitle(tr("Export current note as Markdown file"));
    dialog.selectFile(_currentNote.getName() + QStringLiteral(".md"));
    const int ret = dialog.exec();

    if (ret == QDialog::Accepted) {
        QString fileName = dialog.selectedFile();

        if (!fileName.isEmpty()) {
            if (QFileInfo(fileName).suffix().isEmpty()) {
                fileName.append(QStringLiteral(".md"));
            }

            bool withAttachedFiles = (_currentNote.hasAttachments()) &&
                 Utils::Gui::question(this, tr("Export attached files"),
                tr("Do you also want to export media files and attachments of "
                   "the note? Files may be overwritten in the destination folder!"),
                                 QStringLiteral("note-export-attachments")) ==
                QMessageBox::Yes;

            _currentNote.exportToPath(fileName, withAttachedFiles);
        }
    }
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
}

/**
 * Sets a note bookmark on bookmark slot 0..9
 */
void MainWindow::storeNoteBookmark(int slot) {
    // return if note text edit doesn't have the focus
    if (!ui->noteTextEdit->hasFocus()) {
        return;
    }

    NoteHistoryItem item = NoteHistoryItem(&_currentNote, ui->noteTextEdit);
    noteBookmarks[slot] = item;

    showStatusBarMessage(
        tr("Bookmarked note position at slot %1").arg(QString::number(slot)),
        3000);
}

/**
 * Loads and jumps to a note bookmark from bookmark slot 0..9
 */
void MainWindow::gotoNoteBookmark(int slot) {
    NoteHistoryItem item = noteBookmarks[slot];

    // check if the note (still) exists
    if (item.getNote().isFetched()) {
        ui->noteTextEdit->setFocus();
        setCurrentNoteFromHistoryItem(item);

        showStatusBarMessage(tr("Jumped to bookmark position at slot %1")
                                 .arg(QString::number(slot)),
                             3000);
    }
}

/**
 * Inserts a code block at the current cursor position
 */
void MainWindow::on_actionInsert_code_block_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();
    QString selectedText = c.selection().toPlainText();

    if (selectedText.isEmpty()) {
        // insert multi-line code block if cursor is in an empty line
        if (c.atBlockStart() && c.atBlockEnd()) {
            c.insertText(QStringLiteral("```\n\n```"));
            c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 3);
        } else {
            c.insertText(QStringLiteral("``"));
        }

        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor);
        textEdit->setTextCursor(c);
    } else {
        bool addNewline = false;

        // if the selected text has multiple lines add a multi-line code block
        if (selectedText.contains(QStringLiteral("\n"))) {
            // add another newline if there is no newline at the end of the
            // selected text
            const QString endNewline =
                selectedText.endsWith(QLatin1String("\n"))
                    ? QString()
                    : QStringLiteral("\n");

            selectedText = QStringLiteral("``\n") + selectedText + endNewline +
                           QStringLiteral("``");
            addNewline = true;
        }

        c.insertText(QStringLiteral("`") + selectedText + QStringLiteral("`"));

        if (addNewline) {
            c.insertText(QStringLiteral("\n"));
        }
    }
}

void MainWindow::on_actionNext_note_triggered() { gotoNextNote(); }

/**
 * Jumps to the next visible note
 */
void MainWindow::gotoNextNote() {
    auto *event = new QKeyEvent(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::postEvent(ui->noteTreeWidget, event);
}

/**
 * Activate the context menu in the currently focused widget
 */
void MainWindow::activateContextMenu() {
    auto *event = new QContextMenuEvent(QContextMenuEvent::Keyboard, QPoint());
    QApplication::postEvent(focusWidget(), event);
}

void MainWindow::on_actionPrevious_Note_triggered() { gotoPreviousNote(); }

/**
 * Jumps to the previous visible note
 */
void MainWindow::gotoPreviousNote() {
    auto *event = new QKeyEvent(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::postEvent(ui->noteTreeWidget, event);
}

void MainWindow::on_actionToggle_distraction_free_mode_triggered() {
    toggleDistractionFreeMode();
}

/**
 * Tracks an action
 */
void MainWindow::trackAction(QAction *action) {
    if (action == Q_NULLPTR) {
        return;
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    ui->tagTreeWidget->resizeColumnToContents(0);
    ui->tagTreeWidget->resizeColumnToContents(1);
    QMainWindow::resizeEvent(event);
}

/**
 * Toggles the visibility of the toolbars
 */
void MainWindow::on_actionShow_toolbar_triggered(bool checked) {
    const auto toolbars = findChildren<QToolBar *>();
    for (QToolBar *toolbar : toolbars) {
        toolbar->setVisible(checked);
    }
}

/**
 * Toggles the checked state of the "show toolbar" checkbox in the main menu
 */
void MainWindow::toolbarVisibilityChanged(bool visible) {
    Q_UNUSED(visible)

    const QSignalBlocker blocker(ui->actionShow_toolbar);
    {
        Q_UNUSED(blocker)
        ui->actionShow_toolbar->setChecked(isToolbarVisible());
    }
}

/**
 * Checks if at least one toolbar is visible
 */
bool MainWindow::isToolbarVisible() {
    const auto toolbars = findChildren<QToolBar *>();
    for (QToolBar *toolbar : toolbars) {
        if (toolbar->isVisible()) {
            return true;
        }
    }

    return false;
}

void MainWindow::dfmEditorWidthActionTriggered(QAction *action) {
    QSettings settings;
    settings.setValue(QStringLiteral("DistractionFreeMode/editorWidthMode"),
                      action->whatsThis().toInt());

    ui->noteTextEdit->setPaperMargins();
}

/**
 * Allows files to be dropped to PKbSuite
 */
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    }
}

/**
 * Handles the copying of notes to the current notes folder
 */
void MainWindow::dropEvent(QDropEvent *e) {
    handleInsertingFromMimeData(e->mimeData());
}

/**
 * Handles the inserting of media files and notes from a mime data, for example
 * produced by a drop event or a paste action
 */
void MainWindow::handleInsertingFromMimeData(const QMimeData *mimeData) {
    if (mimeData->hasHtml()) {
        insertHtml(mimeData->html());
    } else if (mimeData->hasUrls()) {
        int successCount = 0;
        int failureCount = 0;
        int skipCount = 0;

        const auto urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString path(url.toLocalFile());
            const QFileInfo fileInfo(path);
            qDebug() << __func__ << " - 'path': " << path;

            if (fileInfo.isReadable()) {
                auto *file = new QFile(path);

                // only allow markdown and text files to be copied as note
                if (isValidNoteFile(file)) {
                    // copy file to notes path
                    const bool success =
                        file->copy(notesPath + QDir::separator() +
                                   fileInfo.fileName());

                    if (success) {
                        successCount++;
                    } else {
                        failureCount++;
                    }
                    // only allow image files to be inserted as image
                } else if (isValidImageFile(file)) {
                    showStatusBarMessage(tr("Inserting image"));

                    // insert the image
                    insertImage(file);

                        showStatusBarMessage(tr("Done inserting image"), 3000);
                    } else if (isValidPDFFile(file)) {
                        showStatusBarMessage(tr("Inserting PDF file"));

                        // insert the PDF File
                        insertPDF(file); // TODO

                        showStatusBarMessage(tr("Done inserting attachment"),
                                             3000);
                    } else {
                        showStatusBarMessage(tr("Inserting attachment"));

                        // inserting the attachment
                        insertAttachment(file);

                        showStatusBarMessage(tr("Done inserting attachment"),
                                             3000);
                    }

                    delete file;
                } else {
                    skipCount++;
                }
            }

        QString message;
        if (successCount > 0) {
            message +=
                tr("Copied %n note(s) to %1", "", successCount).arg(notesPath);
            reloadNoteFolder();
        }

        if (failureCount > 0) {
            if (!message.isEmpty()) {
                message += QStringLiteral(", ");
            }

            message +=
                tr("Failed to copy %n note(s) (most likely already existing)",
                   "", failureCount);
        }

        if (skipCount > 0) {
            if (!message.isEmpty()) {
                message += QStringLiteral(", ");
            }

            message +=
                tr("Skipped copying of %n note(s) "
                   "(no markdown or text file or not readable)",
                   "", skipCount);
        }

        if (!message.isEmpty()) {
            showStatusBarMessage(message, 5000);
        }
    } else if (mimeData->hasImage()) {
        // get the image from mime data
        QImage image = mimeData->imageData().value<QImage>();

        if (!image.isNull()) {
            showStatusBarMessage(tr("Saving temporary image"), 0);

            QTemporaryFile tempFile(
                QDir::tempPath() + QDir::separator() +
                QStringLiteral("pkbsuite-media-XXXXXX.png"));

            if (tempFile.open()) {
                // save temporary png image
                image.save(tempFile.fileName(), "PNG");

                // insert media into note
                auto *file = new QFile(tempFile.fileName());

                showStatusBarMessage(tr("Inserting image"));
                insertImage(file);

                delete file;

                showStatusBarMessage(tr("Done inserting image"), 3000);
            } else {
                showStatusBarMessage(tr("Temporary file can't be opened"),
                                     3000);
            }
        }
    }
}

/**
 * Inserts html as markdown in the current note
 * Images are also downloaded
 */
void MainWindow::insertHtml(QString html) {
    // convert html tags to markdown
    html = Utils::Misc::htmlToMarkdown(std::move(html));

    // match image tags
    static const QRegularExpression re(QStringLiteral("<img.+?src=[\"'](.+?)[\"'].*?>"),
                          QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator i = re.globalMatch(html);

    // find, download locally and replace all images
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        const QString imageTag = match.captured(0);
        const QString imageUrlText = match.captured(1).trimmed();
        QString markdownCode;

        // check if image is an inline image
        if (imageUrlText.startsWith(QLatin1String("data:image/"),
                                    Qt::CaseInsensitive)) {
            QStringList parts = imageUrlText.split(QStringLiteral(";base64,"));
            if (parts.count() == 2) {
                markdownCode = _currentNote.importMediaFromBase64(parts[1]);
            }
        } else {
            const QUrl imageUrl = QUrl(imageUrlText);

            qDebug() << __func__ << " - 'imageUrl': " << imageUrl;

            if (!imageUrl.isValid()) {
                continue;
            }

            showStatusBarMessage(tr("Downloading %1").arg(imageUrl.toString()), 0);

            // download the image and get the media markdown code for it
            markdownCode = _currentNote.downloadUrlToEmbedment(imageUrl);
        }

        if (!markdownCode.isEmpty()) {
            // replace the image tag with markdown code
            html.replace(imageTag, markdownCode);
        }
    }

    showStatusBarMessage(tr("Downloading images finished"), 3000);

    // remove all html tags
    static const QRegularExpression tagRE(QStringLiteral("<.+?>"));
    html.remove(tagRE);

    // unescape some html special characters
    html = Utils::Misc::unescapeHtml(std::move(html)).trimmed();

    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();

    c.insertText(html);
}

QString MainWindow::getWorkspaceUuid(const QString &workspaceName)
{
    return _workspaceNameUuidMap.value(workspaceName, "");
}

/**
 * Evaluates if file is an image file
 */
bool MainWindow::isValidImageFile(QFile *file) {
    QStringList imageExtensions = QStringList() << "jpg" << "png" << "gif";
    QFileInfo fileInfo(file->fileName());
    QString extension = fileInfo.suffix();
    return imageExtensions.contains(extension, Qt::CaseInsensitive);
}

/**
 * Evaluates if file is a PDF file
 */
bool MainWindow::isValidPDFFile(QFile *file) {
    QStringList pdfExtension = QStringList() << "pdf";

    // append the custom extensions
    pdfExtension.append(customNoteFileExtensionList());

    QFileInfo fileInfo(file->fileName());
    QString extension = fileInfo.suffix();
    return pdfExtension.contains(extension, Qt::CaseInsensitive);
}

/**
 * Evaluates if file is a note file
 */
bool MainWindow::isValidNoteFile(QFile *file) {
    auto mediaExtensions = customNoteFileExtensionList();
    const QFileInfo fileInfo(file->fileName());
    const QString extension = fileInfo.suffix();
    return mediaExtensions.contains(extension, Qt::CaseInsensitive);
}

void MainWindow::on_actionPaste_image_triggered() { pasteMediaIntoNote(); }

/**
 * Handles the pasting of media into notes
 */
void MainWindow::pasteMediaIntoNote() {
    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData(QClipboard::Clipboard);
    handleInsertingFromMimeData(mimeData);
}

void MainWindow::on_actionShow_note_in_file_manager_triggered() {
    Utils::Misc::openFolderSelect(_currentNote.fullNoteFilePath());
}

/**
 * Attempts to undo the formatting on a selected string
 *
 * @param formatter
 * @return
 */
bool MainWindow::undoFormatting(const QString &formatter) {
    auto *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();
    const QString selectedText = c.selectedText();
    const int formatterLength = formatter.length();
    const int selectionStart = c.selectionStart();
    const int selectionEnd = c.selectionEnd();

    c.setPosition(selectionStart - formatterLength);
    c.setPosition(selectionEnd + formatterLength, QTextCursor::KeepAnchor);
    const QString selectedTextWithFormatter = c.selectedText();

    // if the formatter characters were found we remove them
    if (selectedTextWithFormatter.startsWith(formatter) &&
        selectedTextWithFormatter.endsWith(formatter)) {
        c.insertText(selectedText);
        return true;
    }

    return false;
}

/**
 * Applies a formatter to a selected string
 *
 * @param formatter
 */
void MainWindow::applyFormatter(const QString &formatter) {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();
    const QString selectedText = c.selectedText();

    // first try to undo an existing formatting
    if (undoFormatting(formatter)) {
        return;
    }

    if (selectedText.isEmpty()) {
        c.insertText(formatter.repeated(2));
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor,
                       formatter.length());
        textEdit->setTextCursor(c);
    } else {
        QRegularExpressionMatch match =
            QRegularExpression(QStringLiteral(R"(^(\s*)(.+?)(\s*)$)"))
                .match(selectedText);
        if (match.hasMatch()) {
            c.insertText(match.captured(1) + formatter + match.captured(2) +
                         formatter + match.captured(3));
        }
    }
}

/**
 * Inserts a bold block at the current cursor position
 */
void MainWindow::on_actionFormat_text_bold_triggered() {
    applyFormatter(QStringLiteral("**"));
}

/**
 * Inserts a underline block at the current cursor position
 */
void MainWindow::on_actionFormat_text_underline_triggered() {
    applyFormatter(QStringLiteral("__"));
}

/**
 * Inserts an italic block at the current cursor position
 */
void MainWindow::on_actionFormat_text_italic_triggered() {
    applyFormatter(QStringLiteral("*"));
}

/**
 * Increases the note text font size by one
 */
void MainWindow::on_action_Increase_note_text_size_triggered() {
    const int fontSize =
        ui->noteTextEdit->modifyFontSize(PKbSuiteMarkdownTextEdit::Increase);

    if (isInDistractionFreeMode()) {
        ui->noteTextEdit->setPaperMargins();
    }

    showStatusBarMessage(tr("Increased font size to %1 pt").arg(fontSize),
                         3000);
}

/**
 * Decreases the note text font size by one
 */
void MainWindow::on_action_Decrease_note_text_size_triggered() {
    const int fontSize =
        ui->noteTextEdit->modifyFontSize(PKbSuiteMarkdownTextEdit::Decrease);

    if (isInDistractionFreeMode()) {
        ui->noteTextEdit->setPaperMargins();
    }

    showStatusBarMessage(tr("Decreased font size to %1 pt").arg(fontSize),
                         3000);
}

/**
 * Resets the note text font size
 */
void MainWindow::on_action_Reset_note_text_size_triggered() {
    const int fontSize =
        ui->noteTextEdit->modifyFontSize(PKbSuiteMarkdownTextEdit::Reset);
    showStatusBarMessage(tr("Reset font size to %1 pt",
                            "Will be shown after "
                            "the font size is reset by 'Reset note text size'")
                             .arg(fontSize),
                         3000);
}

/**
 * Reloads the tag tree
 */
void MainWindow::reloadTagTree() {
    qDebug() << __func__;

    QSettings settings;

    ui->tagTreeWidget->clear();

    int untaggedNoteCount = NoteMap::countAllNotTagged();

    // create an item to view all notes
    int linkCount = NoteMap::getInstance()->countAll();
    QString toolTip = tr("show all notes (%1)").arg(QString::number(linkCount));

    auto *allItem = new QTreeWidgetItem();
    allItem->setText(0, tr("All notes"));
    allItem->setForeground(1, QColor(Qt::gray));
    allItem->setText(1, QString::number(linkCount));
    allItem->setToolTip(0, toolTip);
    allItem->setToolTip(1, toolTip);
    allItem->setData(0, Qt::UserRole, "AllNotes");
    allItem->setFlags(allItem->flags() & ~Qt::ItemIsSelectable);
    allItem->setIcon(
        0,
        QIcon::fromTheme(QStringLiteral("edit-copy"),
                         QIcon(QStringLiteral(
                             ":icons/breeze-pkbsuite/16x16/edit-copy.svg"))));
    // this time, the tags come first
    buildTagTree();
    // and get sorted
    if (settings.value(QStringLiteral("tagsPanelSort")).toInt() ==
        SORT_ALPHABETICAL) {
        ui->tagTreeWidget->sortItems(
            0, toQtOrder(
                   settings.value(QStringLiteral("tagsPanelOrder")).toInt()));
    }
    // now add 'All notes' to the top
    ui->tagTreeWidget->insertTopLevelItem(0, allItem);

    // add an item to view untagged notes if there are any
    linkCount = NoteMap::countAllNotTagged();

    if (linkCount > 0) {
        toolTip =
            tr("show all untagged notes (%1)").arg(QString::number(linkCount));
        auto *untaggedItem = new QTreeWidgetItem();
        untaggedItem->setText(0, tr("Untagged notes"));
        untaggedItem->setForeground(1, QColor(Qt::gray));
        untaggedItem->setText(1, QString::number(linkCount));
        untaggedItem->setToolTip(0, toolTip);
        untaggedItem->setToolTip(1, toolTip);
        untaggedItem->setData(0, Qt::UserRole,"UntaggedNotes");
        untaggedItem->setFlags(untaggedItem->flags() & ~Qt::ItemIsSelectable);
        untaggedItem->setIcon(
            0, QIcon::fromTheme(
                   QStringLiteral("edit-copy"),
                   QIcon(QStringLiteral(
                       ":icons/breeze-pkbsuite/16x16/edit-copy.svg"))));
        ui->tagTreeWidget->addTopLevelItem(untaggedItem);
    }

    ui->tagTreeWidget->resizeColumnToContents(0);
    ui->tagTreeWidget->resizeColumnToContents(1);

    highlightCurrentNoteTagsInTagTree();
}

/**
 * Populates the tag tree with its tags
 */
void MainWindow::buildTagTree(QTreeWidgetItem *parent,
                                           bool topLevel) {
    TagMap* tagMap = TagMap::getInstance();
    const QString activeTag = tagMap->getActiveTag();

    QSettings settings;
    const int tagPanelSort = settings.value(QStringLiteral("tagsPanelSort")).toInt();
    const int tagPanelOrder = settings.value(QStringLiteral("tagsPanelOrder")).toInt();
    const QVector<QString> tagList = tagMap->fetchAllTags();
    for (const QString &tag : tagList) {
        QTreeWidgetItem *item = addTagToTagTreeWidget(tag);

        // set the active item
        if (activeTag == tag) {
            const QSignalBlocker blocker(ui->tagTreeWidget);
            Q_UNUSED(blocker)

            ui->tagTreeWidget->setCurrentItem(item);
        }

        if (tagPanelSort == SORT_ALPHABETICAL) {
            item->sortChildren(0, toQtOrder(tagPanelOrder));
        }
    }

    // update the UI
    // this will crash the app sporadically
    // QCoreApplication::processEvents();
}

/**
 * Ads a tag to the tag tree widget
 */
QTreeWidgetItem* MainWindow::addTagToTagTreeWidget(const QString tag) {
    auto hideCount = QSettings().value("tagsPanelHideNoteCount", false).toBool();

    int count = 0;
    if (!hideCount) {
        TagMap* tagMap = TagMap::getInstance();
        count = tagMap->taggedNotesCount(tag);
    }

    const int linkCount = count;
    const QString toolTip = tr("show all notes tagged with '%1' (%2)")
                                .arg(tag, QString::number(linkCount));
    auto *item = new QTreeWidgetItem();
    item->setData(0, Qt::UserRole, tag);
    item->setText(0, tag);
    item->setText(1, linkCount > 0 ? QString::number(linkCount) : QString());
    item->setForeground(1, QColor(Qt::gray));
    item->setIcon(0, _tagIcon);
    item->setToolTip(0, toolTip);
    item->setToolTip(1, toolTip);
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    // set the color of the tag tree widget item
    handleTreeWidgetItemTagColor(item, tag);

    // add the item at top level
    ui->tagTreeWidget->addTopLevelItem(item);

    return item;
}

/**
 * Reads the color from a tag and sets the background color of a tree widget
 * item
 *
 * @param item
 * @param tag
 */
void MainWindow::handleTreeWidgetItemTagColor(QTreeWidgetItem *item,
                                              const QString &tag) {
    if (item == Q_NULLPTR) {
        return;
    }

    const int columnCount = item->columnCount();

    if (columnCount == 0) {
        return;
    }

    // get the color from the tag
    QColor color = QColorConstants::Black;      //TODO Implement the possibility to define specific colors for some Tags (ex.: #TODO)
    if (tag == "TODO")
        color = QColorConstants::Red;
    else if (tag == "REFERENCE_NOTE")
        color = QColorConstants::DarkBlue;

    // if no color was set reset it by using a transparent white
    if (!color.isValid()) {
        color = Qt::transparent;
    }

    QBrush brush = QBrush(color);

    // the tree widget events have to be blocked because when called in
    // assignColorToTagItem() the 2nd setBackground() crashes the app,
    // because it seems the tag tree will be reloaded
    const QSignalBlocker blocker(item->treeWidget());
    Q_UNUSED(blocker)

    // set the color for all columns
    for (int column = 0; column < columnCount; ++column) {
        item->setForeground(column, brush);
    }
}

/**
 * Creates a new tag
 */
void MainWindow::on_tagLineEdit_returnPressed() {
    const QString name = ui->tagLineEdit->text();
    if (name.isEmpty()) {
        return;
    }

    const QSignalBlocker blocker(this->noteDirectoryWatcher);
    Q_UNUSED(blocker)

    TagMap* tagMap = TagMap::getInstance();
    tagMap->createTag(name);

    const QSignalBlocker blocker2(ui->tagLineEdit);
    Q_UNUSED(blocker2)

    // clear the line edit if the tag was stored
    ui->tagLineEdit->clear();

    reloadTagTree();
}

/**
 * Filters tags in the tag tree widget
 */
void MainWindow::on_tagLineEdit_textChanged(const QString &arg1) {
    Utils::Gui::searchForTextInTreeWidget(
        ui->tagTreeWidget, arg1, Utils::Gui::TreeWidgetSearchFlag::IntCheck);
}

/**
 * Shows or hides everything for the note tags
 */
void MainWindow::setupTags() {
    ui->newNoteTagLineEdit->setVisible(false);
    ui->newNoteTagButton->setVisible(true);

    // we want the tag frame as small as possible
    ui->noteTagFrame->layout()->setContentsMargins(8, 0, 8, 0);

    reloadTagTree();
    reloadCurrentNoteTags();
    // filter the notes again
    filterNotes(false);
}

/**
 * Hides the note tag add button and shows the text edit
 */
void MainWindow::on_newNoteTagButton_clicked() {
    _noteTagDockWidget->setVisible(true);
    ui->newNoteTagLineEdit->setVisible(true);
    ui->newNoteTagLineEdit->setFocus();
    ui->newNoteTagLineEdit->selectAll();
    ui->newNoteTagButton->setVisible(false);

    QSettings settings;
    // enable the tagging dock widget the first time tagging was used
    if (!settings.value(QStringLiteral("tagWasAddedToNote")).toBool()) {
        _taggingDockWidget->setVisible(true);
        settings.setValue(QStringLiteral("tagWasAddedToNote"), true);
    }

    // add tag name auto-completion
    TagMap* tagMap = TagMap::getInstance();
    QStringList wordList = tagMap->fetchAllTags().toList();
    auto *completer = new QCompleter(wordList, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->newNoteTagLineEdit->setCompleter(completer);
    completer->popup()->installEventFilter(this);
}

/**
 * Links a note to the tag entered after pressing return
 * in the note tag line edit
 */
void MainWindow::on_newNoteTagLineEdit_returnPressed() {
    QString text = ui->newNoteTagLineEdit->text();
    linkTagNameToCurrentNote(text);
    
    QTextCursor tc = ui->noteTextEdit->textCursor();
    tc.insertText("#" + text);
}

/**
 * Links a tag to the current note (or all selected notes)
 *
 * @param tagName
 */
void MainWindow::linkTagNameToCurrentNote(const QString &tagName,
                                          bool linkToSelectedNotes) {
    if (tagName.isEmpty()) {
        return;
    }

    // workaround when signal block doesn't work correctly
    directoryWatcherWorkaround(true, true);

    TagMap* tagMap = TagMap::getInstance();

    // create a new tag if it doesn't exist
    if (!tagMap->tagExists(tagName)) {
        const QSignalBlocker blocker(noteDirectoryWatcher);
        Q_UNUSED(blocker)

        tagMap->createTag(tagName);
    }

    // link the current note to the tag
    if (tagMap->tagExists(tagName)) {
        const QSignalBlocker blocker(noteDirectoryWatcher);
        Q_UNUSED(blocker)

        const int selectedNotesCount = getSelectedNotesCount();

        if (linkToSelectedNotes && selectedNotesCount > 1) {
            auto noteList = selectedNotes();
            for (Note &note : noteList) {
                if (note.isTagged(tagName)) {
                    continue;
                }

                note.addTag(tagName);
            }
        } else {
            _currentNote.addTag(tagName);
        }

        reloadCurrentNoteTags();
        reloadTagTree();
        filterNotes();

        // handle the coloring of the note in the note tree widget
        handleNoteTreeTagColoringForNote(_currentNote);
    }

    // turn off the workaround again
    directoryWatcherWorkaround(false, true);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        QString windowStateString;
        switch(windowState()) {
        case Qt::WindowMinimized:
            windowStateString = "minimized";
            break;
        case Qt::WindowMaximized:
            windowStateString = "maximized";
            break;
        case Qt::WindowFullScreen:
            windowStateString = "fullscreen";
            break;
        case Qt::WindowActive:
            windowStateString = "active";
            break;
        default:
            windowStateString = "nostate";
            break;
        }
    }

    QMainWindow::changeEvent(event);
}

/**
 * Hides the note tag line edit after editing
 */
void MainWindow::on_newNoteTagLineEdit_editingFinished() {
    ui->newNoteTagLineEdit->setVisible(false);
    ui->newNoteTagButton->setVisible(true);
}

/**
 * Reloads the note tag buttons for the current note (or the selected notes)
 */
void MainWindow::reloadCurrentNoteTags() {
    // remove all remove-tag buttons
    QLayoutItem *child;
    while ((child = ui->noteTagButtonFrame->layout()->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    int selectedNotesCount = getSelectedNotesCount();
    bool _currentNoteOnly = selectedNotesCount <= 1;
    ui->selectedTagsToolButton->setVisible(!_currentNoteOnly);
    ui->newNoteTagButton->setToolTip(
        _currentNoteOnly ? tr("Add a tag to the current note")
                        : tr("Add a tag to the selected notes"));
    QSet<QString> noteTags;

    ui->multiSelectActionFrame->setVisible(!_currentNoteOnly);
    ui->noteEditorFrame->setVisible(_currentNoteOnly);

    if (_currentNoteOnly) {
        noteTags = _currentNote.getTags();

        // only refresh the preview if we previously selected multiple notes
        // because we used it for showing note information
        if (_lastNoteSelectionWasMultiple) {
            _notePreviewHash.clear();
            regenerateNotePreview();
        }
    } else {
        const QVector<Note> notes = selectedNotes();
        foreach (Note note, notes) {
            noteTags += note.getTags();
        }

        const QString notesSelectedText =
            tr("%n notes selected", "", selectedNotesCount);

        ui->selectedTagsToolButton->setText(
            QString::number(selectedNotesCount));
        ui->selectedTagsToolButton->setToolTip(notesSelectedText);

        ui->notesSelectedLabel->setText(notesSelectedText);

        // overwrite the note preview with a preview of the selected notes
        const QString previewHtml =
            Note::generateMultipleNotesPreviewText(notes);
        ui->noteTextView->setText(previewHtml);
    }

    _lastNoteSelectionWasMultiple = !_currentNoteOnly;

    // add all new remove-tag buttons
    foreach (const QString tag, noteTags) {
        QPushButton *button = new QPushButton(
        Utils::Misc::shorten(tag, 25), ui->noteTagButtonFrame);
        button->setIcon(QIcon::fromTheme(
            QStringLiteral("tag-delete"),
            QIcon(QStringLiteral(
                ":icons/breeze-pkbsuite/16x16/xml-attribute-delete.svg"))));
        button->setToolTip(
            _currentNoteOnly
                ? tr("Remove tag '%1' from the current note").arg(tag)
                : tr("Remove tag '%1' from the selected notes")
                      .arg(tag));
        button->setObjectName(QStringLiteral("removeNoteTag") +
                              tag);

        QObject::connect(button, &QPushButton::clicked, this,
                         &MainWindow::removeNoteTagClicked);

        ui->noteTagButtonFrame->layout()->addWidget(button);
    }

    //    // find tags not in common of selected notes
    //    if (selectedNotesCount > 1) {
    //        QLabel *noteTagButtonFrame = new QLabel("+3 tags");
    //        ui->noteTagButtonFrame->layout()->addWidget(noteTagButtonFrame);
    //    }

    // add a spacer to prevent the button items to take the full width
    auto *spacer = new QSpacerItem(0, 20, QSizePolicy::MinimumExpanding,
                                   QSizePolicy::Ignored);
    ui->noteTagButtonFrame->layout()->addItem(spacer);

    highlightCurrentNoteTagsInTagTree();
}

/**
 * Highlights the tags of the current note in the tag tree
 */
void MainWindow::highlightCurrentNoteTagsInTagTree() {
    const int selectedNotesCount = getSelectedNotesCount();
    const bool _currentNoteOnly = selectedNotesCount <= 1;
    QSet<QString> tagList;

    if (_currentNoteOnly) {
        tagList = _currentNote.getTags();
    } else {
        const QVector<Note> &notes = selectedNotes();

        for (Note note : notes) {
            tagList.unite(note.getTags());
        }
    }

    const QSignalBlocker blocker1(ui->tagTreeWidget);
    Q_UNUSED(blocker1)

    Utils::Gui::resetBoldStateOfAllTreeWidgetItems(ui->tagTreeWidget);

    for (const QString &tag : Utils::asConst(tagList)) {
        QTreeWidgetItem *item = Utils::Gui::getTreeWidgetItemWithUserData(
            ui->tagTreeWidget, tag);

        if (item != nullptr) {
            // set tag item in tag tree widget to bold if note has tag
            auto font = item->font(0);
            if (!font.bold()) {
                font.setBold(true);
                item->setFont(0, font);
            }
        }
    }
}

/**
 * Removes a note tag link
 */
void MainWindow::removeNoteTagClicked() {
    QString objectName = sender()->objectName();
    if (objectName.startsWith(QLatin1String("removeNoteTag"))) {
        const QString tag =
            objectName.remove(QLatin1String("removeNoteTag"));
        TagMap* tagMap = TagMap::getInstance();
        if (!tagMap->tagExists(tag)) {
            return;
        }

        // workaround when signal blocking doesn't work correctly
        directoryWatcherWorkaround(true, true);

        const int selectedNotesCount = getSelectedNotesCount();

        if (selectedNotesCount <= 1) {
			QRegularExpression re = QRegularExpression(R"((, )?#)" + tag);
			QRegularExpressionMatchIterator reIterator = re.globalMatch(ui->noteTextEdit->toPlainText());
			
			int lTag = 0;

			QTextCursor tc = ui->noteTextEdit->textCursor();
			while (reIterator.hasNext()) {
				QRegularExpressionMatch reMatch = reIterator.next();
				
				tc.setPosition(reMatch.capturedStart() - lTag);
				tc.setPosition(reMatch.capturedEnd() - lTag, QTextCursor::KeepAnchor);
				
				lTag = reMatch.capturedLength() + 1;  // lTag is the offset to apply for new pos calculations as text has been removed
				
				tc.removeSelectedText();  
			}
			
			_currentNote.removeTag(tag);
        } else {
            auto selectedNotesList = selectedNotes();
            for (Note &note : selectedNotesList) {
                if (!note.isTagged(tag)) {
                    continue;
                }

                note.removeTag(tag);
            }
        }

        reloadCurrentNoteTags();
        reloadTagTree();
        filterNotesByTag();

        // handle the coloring of the note in the note tree widget
        handleNoteTreeTagColoringForNote(_currentNote);

        // disable workaround
        directoryWatcherWorkaround(false, true);
    }
}

int MainWindow::getSelectedNotesCount() const {
    return ui->noteTreeWidget->selectedItems().count();
}

/**
 * Allows the user to add a tag to the current note
 */
void MainWindow::on_action_new_tag_triggered() {
    on_newNoteTagButton_clicked();
}

/**
 * Reloads the current note folder
 */
void MainWindow::reloadNoteFolder() {
    // force build and load
    buildNotesIndexAndLoadNoteDirectoryList(true, true);
    _currentNote.refetch();
    setNoteTextFromNote(&_currentNote, false, false, true);
}

/**
 * Stores the tag after it was edited
 */
void MainWindow::on_tagTreeWidget_itemChanged(QTreeWidgetItem *item,
                                              int column) {
    Q_UNUSED(column)

    QString tagName = item->data(0, Qt::UserRole).toString();
    const QString newTagName = item->text(0);
    TagMap* tagMap = TagMap::getInstance();
    if (tagMap->tagExists(tagName)) {
        // workaround when signal block doesn't work correctly
        directoryWatcherWorkaround(true, true);

        if (!newTagName.isEmpty()) {
            const QSignalBlocker blocker(this->noteDirectoryWatcher);
            Q_UNUSED(blocker)

            QVector<int> idsTaggedNotes = tagMap->fetchAllLinkedNotes(tagName);
            
            int idNote = 0;
            while (idNote < idsTaggedNotes.size()) {
                Note note = _kbNoteMap->fetchNoteById(idNote);
                QString noteText = note.getNoteText();
                
                QRegularExpression re = QRegularExpression("#" + tagName);
                QRegularExpressionMatchIterator reIterator = re.globalMatch(noteText);

                while (reIterator.hasNext()) {
                    QRegularExpressionMatch reMatch = reIterator.next();
                    
                    noteText.replace("#" + tagName, "#" + newTagName);
                }
                                
                note.setNoteText(noteText);

                if (idNote == ui->noteTreeWidget->currentItem()->data(0, Qt::UserRole).toInt())
                    ui->noteTextEdit->setPlainText(noteText);
            
                note.store();
                idNote++;
            }

            tagMap->renameTag(tagName, newTagName);
        }

        // we also have to reload the tag tree if we don't change the tag
        // name to get the old name back
        reloadTagTree();
        reloadCurrentNoteTags();

        // turn off the workaround again
        directoryWatcherWorkaround(false, true);
    }
}

/**
 * Sets a new active tag
 */
void MainWindow::on_tagTreeWidget_currentItemChanged(
    QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    Q_UNUSED(previous)

    if (current == nullptr) {
        return;
    }

    // set the tag id as active
    const QString tag = current->data(0, Qt::UserRole).toString();
    TagMap* tagMap = TagMap::getInstance();

    tagMap->setActiveTag(tag);

    const int count = ui->tagTreeWidget->selectedItems().count();
    if (count > 1) return;

    const QSignalBlocker blocker(ui->searchLineEdit);
    Q_UNUSED(blocker)

    ui->searchLineEdit->clear();
    filterNotes();
}

/**
 * Triggers filtering when multiple tags are selected
 */
void MainWindow::on_tagTreeWidget_itemSelectionChanged() {
    const int count = ui->tagTreeWidget->selectedItems().count();

    if (count <= 1) {
        if (count == 1) {
            //           on_tagTreeWidget_currentItemChanged(ui->tagTreeWidget->selectedItems().first(),
            //                                                Q_NULLPTR);
        }
        return;
    }

    const QSignalBlocker blocker(ui->searchLineEdit);
    Q_UNUSED(blocker)

    ui->searchLineEdit->clear();
    filterNotes();
}

/**
 * Creates a context menu for the tag tree widget
 */
void MainWindow::on_tagTreeWidget_customContextMenuRequested(const QPoint pos) {
    // don't open the most of the context menu if no tags are selected
    const bool hasSelected = ui->tagTreeWidget->selectedItems().count() > 0;

    const QPoint globalPos = ui->tagTreeWidget->mapToGlobal(pos);
    QMenu menu;

    QAction *addAction = menu.addAction(tr("&Add tag"));

    // allow these actions only if tags are selected
    QAction *renameAction = nullptr;
    QAction *removeAction = nullptr;
    if (hasSelected) {
        renameAction = menu.addAction(tr("Rename tag"));
        removeAction = menu.addAction(tr("&Remove tags"));
    }

    QAction *selectedItem = menu.exec(globalPos);

    if (selectedItem == nullptr) {
        return;
    }

    QTreeWidgetItem *item = ui->tagTreeWidget->currentItem();

    if (selectedItem == addAction) {
        // open the "add new tag" dialog
        auto *dialog = new TagAddDialog(this);
        const int dialogResult = dialog->exec();

        // if user pressed ok take the name
        if (dialogResult == QDialog::Accepted) {
            const QString name = dialog->name();
            if (!name.isEmpty()) {
                // create a new tag with the name
                TagMap* tagMap = TagMap::getInstance();
                tagMap->createTag(name);

                reloadTagTree();
            }
        }

        delete (dialog);
        return;
    }

    // don't allow clicking on non-tag items for removing, editing and colors
    if (item->data(0, Qt::UserRole).toInt() <= 0) {         // TODO: check if I should test non-tag items based on their names instead of ID
        return;
    }

    if (selectedItem == removeAction) {
        // remove selected tag
        removeSelectedTags();
    } else if (selectedItem == renameAction) {
        ui->tagTreeWidget->editItem(item);
    }
}

/**
 * Populates a tag menu tree for bulk note tagging
 */
void MainWindow::buildBulkNoteTagMenuTree(QMenu *parentMenu) {
    TagMap* tagMap = TagMap::getInstance();
    const auto tagList = tagMap->fetchAllTags();

    for (const QString &tag : tagList) {
        QAction *action = parentMenu->addAction(tag);

        connect(action, &QAction::triggered, this,
                [this, tag]() { tagSelectedNotes(tag); });
    }
}

/**
 * Enables the note externally removed check
 */
void MainWindow::enableNoteExternallyRemovedCheck() {
    _noteExternallyRemovedCheckEnabled = true;
}

/**
 * Opens the widget to replace text in the current note
 */
void MainWindow::on_actionReplace_in_current_note_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    textEdit->searchWidget()->activateReplace();
}

/**
 * Jumps to the position that was clicked in the navigation widget
 */
void MainWindow::onNavigationWidgetPositionClicked(int position) {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();

    // set the focus first so the preview also scrolls to the headline
    textEdit->setFocus();

    QTextCursor c = textEdit->textCursor();

    // if the current position of the cursor is smaller than the position
    // where we want to jump to set the cursor to the end of the note to make
    // sure it scrolls up, not down
    // everything is visible that way
    if (c.position() < position) {
        c.movePosition(QTextCursor::End);
        textEdit->setTextCursor(c);
    }

    c.setPosition(position);

    // select the text of the headline
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

    textEdit->setTextCursor(c);

    // update the preview-slider
    noteTextSliderValueChanged(textEdit->verticalScrollBar()->value(), true);

    // set focus back to the navigation widget so you can use the
    // keyboard to navigate
    ui->navigationWidget->setFocus();
}

/**
 * Starts a note preview regeneration to resize too large images
 */
void MainWindow::onNoteTextViewResize(QSize size, QSize oldSize) {
    Q_UNUSED(size)
    Q_UNUSED(oldSize)

    // just regenerate the note once a second for performance reasons
    if (!_noteViewIsRegenerated) {
        _noteViewIsRegenerated = true;
        QTimer::singleShot(1000, this, SLOT(regenerateNotePreview()));
    }
}

/**
 * Regenerates the note preview by converting the markdown to html again
 */
void MainWindow::regenerateNotePreview() {
    setNoteTextFromNote(&_currentNote, true);
    _noteViewIsRegenerated = false;
}

/**
 * Tries to open a link at the current cursor position or solve an equation
 */
void MainWindow::on_actionAutocomplete_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();

    // attempt to toggle a checkbox at the cursor position
    if (Utils::Gui::toggleCheckBoxAtCursor(textEdit)) {
        return;
    }

    // try to open a link at the cursor position
    if (textEdit->openLinkAtCursorPosition()) {
        showStatusBarMessage(
            tr("An url was opened at the current cursor position"), 5000);
        return;
    }

    // attempt a markdown table auto-format
    if (Utils::Gui::autoFormatTableAtCursor(textEdit)) {
        return;
    }

    QMenu menu;

    double resultValue;
    if (solveEquationInNoteTextEdit(resultValue)) {
        const QString text = QString::number(resultValue);
        auto *action = menu.addAction(QStringLiteral("= ") + text);
        action->setData(text);
        action->setWhatsThis(QStringLiteral("equation"));
    }

    QStringList resultList;
    if (noteTextEditAutoComplete(resultList)) {
        for (const QString &text : Utils::asConst(resultList)) {
            auto *action = menu.addAction(text);
            action->setData(text);
            action->setWhatsThis(QStringLiteral("autocomplete"));
        }
    }

    QPoint globalPos =
        textEdit->mapToGlobal(textEdit->cursorRect().bottomRight());

    // compensate viewport margins
    globalPos.setY(globalPos.y() + textEdit->viewportMargins().top());
    globalPos.setX(globalPos.x() + textEdit->viewportMargins().left());

    if (menu.actions().count() > 0) {
        QAction *selectedItem = menu.exec(globalPos);
        if (selectedItem) {
            const QString text = selectedItem->data().toString();
            const QString type = selectedItem->whatsThis();

            if (text.isEmpty()) {
                return;
            }

            if (type == QStringLiteral("autocomplete")) {
                // overwrite the currently written word
                QTextCursor c = textEdit->textCursor();
                c.movePosition(QTextCursor::StartOfWord,
                               QTextCursor::KeepAnchor);
                c.insertText(text + QStringLiteral(" "));
            } else {
                textEdit->insertPlainText(text);
            }
        }
    }
}

/**
 * Tries to find an equation in the current line and solves it
 *
 * @param returnValue
 * @return
 */
bool MainWindow::solveEquationInNoteTextEdit(double &returnValue) {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();

    // get the text from the current cursor to the start of the line
    c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    QString text = c.selectedText();
    qDebug() << __func__ << " - 'text': " << text;

    QString equation = text;

    // replace "," with "." to allow "," as coma
    equation.replace(QLatin1Char(','), QLatin1Char('.'));

    // remove leading list characters
    equation.remove(QRegularExpression(QStringLiteral(R"(^\s*[\-*+] )")));

    // match all numbers and basic operations like +, -, * and /
    QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral(R"(([\d\.,+\-*\/\(\)\s]+)\s*=)"))
            .match(equation);

    if (!match.hasMatch()) {
        if (equation.trimmed().endsWith(QChar('='))) {
            showStatusBarMessage(
                tr("No equation was found in front of the cursor"), 5000);
        }

        return false;
    }

    equation = match.captured(1);
    qDebug() << __func__ << " - 'equation': " << equation;

    QJSEngine engine;
    // evaluate our equation
    QJSValue result = engine.evaluate(equation);
    double resultValue = result.toNumber();
    qDebug() << __func__ << " - 'resultValue': " << resultValue;

    // compensate for subtraction errors with 0
    if ((resultValue < 0.0001) && (resultValue > 0)) {
        resultValue = 0;
    }

    showStatusBarMessage(tr("Result for equation: %1 = %2")
                             .arg(equation, QString::number(resultValue)),
                         10000);

    // check if cursor is after the "="
    match = QRegularExpression(QStringLiteral("=\\s*$")).match(text);
    if (!match.hasMatch()) {
        return false;
    }

    returnValue = resultValue;
    return true;
}

/**
 * Returns the text from the current cursor to the start of the word in the
 * note text edit
 *
 * @param withPreviousCharacters also get more characters at the beginning
 *                               to get characters like "@" that are not
 *                               word-characters
 * @return
 */
QString MainWindow::noteTextEditCurrentWord(bool withPreviousCharacters) {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();

    // get the text from the current word
    c.movePosition(QTextCursor::EndOfWord);
    c.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);

    QString text = c.selectedText();

    if (withPreviousCharacters) {
        static const QRegularExpression re(QStringLiteral("^[\\s\\n][^\\s]*"));
        do {
            c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
            text = c.selectedText();
        } while (!(re.match(text).hasMatch() || c.atBlockStart()));
    }

    return text.trimmed();
}

/**
 * Tries to find words that start with the current word in the note text edit
 *
 * @param resultList
 * @return
 */
bool MainWindow::noteTextEditAutoComplete(QStringList &resultList) {
    // get the text from the current cursor to the start of the word
    const QString text = noteTextEditCurrentWord();
    qDebug() << __func__ << " - 'text': " << text;

    if (text.isEmpty()) {
        return false;
    }

    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    const QString noteText = textEdit->toPlainText();

    // find all items that match our current word
    resultList = noteText
                     .split(QRegularExpression(
                                QStringLiteral("[^\\w\\d]"),
                                QRegularExpression::UseUnicodePropertiesOption),
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                            QString::SkipEmptyParts)
#else
                            Qt::SkipEmptyParts)
#endif
                     .filter(QRegularExpression(
                         QStringLiteral("^") + QRegularExpression::escape(text),
                         QRegularExpression::CaseInsensitiveOption));

    // we only want each word once
    resultList.removeDuplicates();

    // remove the text we already entered
    resultList.removeOne(text);

    if (resultList.count() == 0) {
        return false;
    }

    qDebug() << __func__ << " - 'resultList': " << resultList;

    return true;
}

/**
 * Exports the note preview as HTML
 */
void MainWindow::on_actionExport_preview_HTML_triggered() {
    FileDialog dialog(QStringLiteral("NoteHTMLExport"));
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(tr("HTML files") + " (*.html)");
    dialog.setWindowTitle(tr("Export current note as HTML file"));
    dialog.selectFile(_currentNote.getName() + QStringLiteral(".html"));
    const int ret = dialog.exec();

    if (ret == QDialog::Accepted) {
        QString fileName = dialog.selectedFile();

        if (!fileName.isEmpty()) {
            if (QFileInfo(fileName).suffix().isEmpty()) {
                fileName.append(QStringLiteral(".html"));
            }

            QFile file(fileName);

            qDebug() << "exporting html file: " << fileName;

            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qCritical() << file.errorString();
                return;
            }
            QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            out.setCodec("UTF-8");
#endif
            out << _currentNote.toMarkdownHtml(notesPath,
                                              getMaxImageWidth(), true, true,
                                              true);
            file.flush();
            file.close();
            Utils::Misc::openFolderSelect(fileName);
        }
    }
}

/**
 * Adds the current search text to the saved searches
 */
void MainWindow::storeSavedSearch() {
    QSettings settings;

    if (settings.value(QStringLiteral("disableSavedSearchesAutoCompletion"))
            .toBool()) {
        return;
    }

    const QString text = ui->searchLineEdit->text();
    if (!text.isEmpty()) {
        QString settingsKey = QStringLiteral("savedSearches");
        QStringList savedSearches = settings.value(settingsKey).toStringList();

        // add the text to the saved searches
        savedSearches.prepend(text);

        // remove duplicate entries, `text` will remain at the top
        savedSearches.removeDuplicates();

        // only keep 100 searches
        while (savedSearches.count() > 100) {
            savedSearches.removeLast();
        }

        settings.setValue(settingsKey, savedSearches);

        // init the saved searches completer
        initSavedSearchesCompleter();
    }
}

/**
 * Initializes the saved searches completer
 */
void MainWindow::initSavedSearchesCompleter() {
    QStringList savedSearches;
    QSettings settings;

    if (!settings.value(QStringLiteral("disableSavedSearchesAutoCompletion"))
             .toBool()) {
        QString settingsKey = QStringLiteral("savedSearches/noteFolder");
        savedSearches = settings.value(settingsKey).toStringList();
    }

    // release the old completer
    auto *completer = ui->searchLineEdit->completer();
    delete completer;

    // add the completer
    completer = new QCompleter(savedSearches, ui->searchLineEdit);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->searchLineEdit->setCompleter(completer);

    // install event filter for the popup
    completer->popup()->installEventFilter(this);
}

/**
 * Inserts the note file name as headline
 */
void MainWindow::on_actionInsert_headline_from_note_filename_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();
    c.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);

    const QString fileName = _currentNote.fileBaseName(true);
    const QString text = Note::createNoteHeader(fileName);
    c.insertText(text);
}

/**
 * Toggles the editor soft wrapping
 */
void MainWindow::on_actionUse_softwrap_in_note_editor_toggled(bool arg1) {
    QSettings settings;
    settings.setValue(QStringLiteral("useSoftWrapInNoteEditor"), arg1);

    // initialize the editor soft wrapping
    initEditorSoftWrap();
}

void MainWindow::on_actionShow_status_bar_triggered(bool checked) {
    ui->statusBar->setVisible(checked);

    const QSignalBlocker blocker(ui->actionShow_status_bar);
    {
        Q_UNUSED(blocker)
        ui->actionShow_status_bar->setChecked(checked);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("showStatusBar"), checked);
}

void MainWindow::on_noteTreeWidget_currentItemChanged(
    QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    // in case all notes were removed
    if (current == nullptr) {
        return;
    }

    int noteId = current->data(0, Qt::UserRole).toInt();
    Note note = _kbNoteMap->fetchNoteById(noteId);
    qDebug() << __func__;

    setCurrentNote(std::move(note), true, false);

	// Check if the note has @Tags not yet linked
	QRegularExpression re = QRegularExpression(R"([^A-Za-z]#([A-Za-zÀ-ÖØ-öø-ÿ_]|\d+[A-Za-zÀ-ÖØ-öø-ÿ_])[A-Za-zÀ-ÖØ-öø-ÿ0-9_]*)");       // Take care of accented characters
	QRegularExpressionMatchIterator reIterator = re.globalMatch(_currentNote.getNoteText());
	while (reIterator.hasNext()) {
		QRegularExpressionMatch reMatch = reIterator.next();
		QString tag = reMatch.captured().right(reMatch.capturedLength() - 2);

		const QSignalBlocker blocker(noteDirectoryWatcher);
		Q_UNUSED(blocker);

        if (!_currentNote.isTagged(tag))
            linkTagNameToCurrentNote(tag);
	}

    // let's highlight the text from the search line edit and do a "in note
    // search"
    searchForSearchLineTextInNoteTextEdit();
}

void MainWindow::openCurrentNoteInTab() {
    // simulate a newly opened tab by updating the current tab with the last note
    if (_lastNoteId > 0) {
        auto previousNote = _kbNoteMap->fetchNoteById(_lastNoteId);
        if (previousNote.isFetched()) {
            updateCurrentTabData(previousNote);
        }
    }

    const QString &noteName = _currentNote.getName();
    const int noteId = _currentNote.getId();
    int tabIndex = Utils::Gui::getTabWidgetIndexByProperty(
        ui->noteEditTabWidget, QStringLiteral("note-id"), noteId);

    if (tabIndex == -1) {
        auto *widgetPage = new QWidget();
        widgetPage->setLayout(ui->noteEditTabWidgetLayout);
        widgetPage->setProperty("note-id", noteId);
        tabIndex = ui->noteEditTabWidget->addTab(widgetPage, noteName);
    } else {
        ui->noteEditTabWidget->setTabText(tabIndex, noteName);
    }

    Utils::Gui::updateTabWidgetTabData(ui->noteEditTabWidget,
                                       tabIndex, _currentNote);

    ui->noteEditTabWidget->setCurrentIndex(tabIndex);

    // remove the tab initially created by the ui file
    if (ui->noteEditTabWidget->widget(0)->property("note-id").isNull()) {
        ui->noteEditTabWidget->removeTab(0);
    }
}

void MainWindow::on_noteTreeWidget_customContextMenuRequested(
    const QPoint pos) {
    auto *item = ui->noteTreeWidget->itemAt(pos);

    // if the user clicks at empty space, this is null and if it isn't handled
    // QON crashes
    if (item == nullptr) {
        return;
    }

    const QPoint globalPos = ui->noteTreeWidget->mapToGlobal(pos);
    const int type = item->data(0, Qt::UserRole + 1).toInt();

    if (type == NoteType) {
        openNotesContextMenu(globalPos);
    }
}

void MainWindow::openNotesContextMenu(const QPoint globalPos,
                                      bool multiNoteMenuEntriesOnly) {
    QMenu noteMenu;
    QAction *renameAction = nullptr;

    if (!multiNoteMenuEntriesOnly) {
        auto *createNoteAction = noteMenu.addAction(tr("New note"));
        connect(createNoteAction, &QAction::triggered, this,
                &MainWindow::on_action_New_note_triggered);

        renameAction = noteMenu.addAction(tr("Rename note"));
        renameAction->setToolTip(
            tr("Allows you to rename the filename of "
               "the note"));
    }

    auto *removeAction = noteMenu.addAction(tr("&Remove notes"));
    noteMenu.addSeparator();

    TagMap* tagMap = TagMap::getInstance();
    int tagCount = tagMap->tagCount();

    // show the tagging menu if at least one tag is present
    if (tagCount) {
        auto *tagMenu = noteMenu.addMenu(tr("&Tag selected notes with…"));
        buildBulkNoteTagMenuTree(tagMenu);
    }

    QStringList noteNameList;
    const auto items = ui->noteTreeWidget->selectedItems();
    for (QTreeWidgetItem *item : items) {
        // the note names are not unique any more but the note subfolder
        // path will be taken into account in
        // Tag::fetchAllWithLinkToNoteNames
        const QString name = item->text(0);
        NoteMap* noteMap = NoteMap::getInstance();
        const Note note = noteMap->fetchNoteByName(name);
        if (note.isFetched()) {
            noteNameList << note.getName();
        }
    }

    const QVector<QString> tagRemoveList = tagMap->fetchAllWithLinkToNoteNames(noteNameList);

    // show the remove tags menu if at least one tag is present
    QMenu *tagRemoveMenu = nullptr;
    if (tagRemoveList.count() > 0) {
        tagRemoveMenu =
            noteMenu.addMenu(tr("&Remove tag from selected notes…"));

        for (const QString &tag : tagRemoveList) {
            auto *action = tagRemoveMenu->addAction(tag);
            action->setToolTip(tag);
            action->setStatusTip(tag);
        }
    }

    QAction *openInExternalEditorAction = nullptr;
    QAction *openNoteWindowAction = nullptr;
    QAction *showInFileManagerAction = nullptr;

    if (!multiNoteMenuEntriesOnly) {
        noteMenu.addSeparator();
        auto *openNoteInTabAction = noteMenu.addAction(tr("Open note in tab"));
        connect(openNoteInTabAction, &QAction::triggered, this,
                &MainWindow::openCurrentNoteInTab);

        openInExternalEditorAction =
            noteMenu.addAction(tr("Open note in external editor"));
        openNoteWindowAction =
            noteMenu.addAction(tr("Open note in different window"));
        showInFileManagerAction =
            noteMenu.addAction(tr("Show note in file manager"));
    }

    // add the custom actions to the context menu
    if (!_noteListContextMenuActions.isEmpty()) {
        noteMenu.addSeparator();

        for (QAction *action : Utils::asConst(_noteListContextMenuActions)) {
            noteMenu.addAction(action);
        }
    }

    QAction *selectAllAction = nullptr;
    if (!multiNoteMenuEntriesOnly) {
        noteMenu.addSeparator();
        selectAllAction = noteMenu.addAction(tr("Select &all notes"));
    }

    QAction *selectedItem = noteMenu.exec(globalPos);
    if (selectedItem) {
        if (selectedItem->parent() == tagRemoveMenu) {
            // remove tag from notes
            const QString tag = selectedItem->data().toString();

            removeTagFromSelectedNotes(tag);
        } else if (selectedItem == removeAction) {
            // remove notes
            removeSelectedNotes();
        } else if (selectedItem == selectAllAction) {
            // select all notes
            selectAllNotes();
        } else if (selectedItem == openInExternalEditorAction) {
            // open the current note in an external editor
            on_action_Open_note_in_external_editor_triggered();
        } else if (selectedItem == openNoteWindowAction) {
            // open the current note in a dialog
            on_actionView_note_in_new_window_triggered();
        } else if (selectedItem == showInFileManagerAction) {
            // show the current note in the file manager
            on_actionShow_note_in_file_manager_triggered();
       } else if (selectedItem == renameAction) {
            QTreeWidgetItem *item = ui->noteTreeWidget->currentItem();

            if (Note::allowDifferentFileName()) {
                if (Utils::Misc::isNoteListPreview()) {
                    bool ok{};
                    const QString name = QInputDialog::getText(
                        this, tr("Rename note"), tr("Name:"), QLineEdit::Normal,
                        _currentNote.getName(), &ok);

                    if (ok && !name.isEmpty()) {
                        item->setText(0, name);
                        on_noteTreeWidget_itemChanged(item, 0);
                    }
                } else {
                    ui->noteTreeWidget->editItem(item);
                }
            } else {
                if (QMessageBox::warning(
                        this, tr("Note renaming not enabled!"),
                        tr("If you want to rename your note you have to enable "
                           "the option to allow the note filename to be "
                           "different from the headline."),
                        tr("Open &settings"), tr("&Cancel"), QString(), 0,
                        1) == 0) {
                    openSettingsDialog(SettingsDialog::NoteFolderPage);
                }
            }
        }
    }
}

/**
 * Renames a note file if the note was renamed in the note tree widget
 */
void MainWindow::on_noteTreeWidget_itemChanged(QTreeWidgetItem *item,
                                               int column) {
    if (item == nullptr) {
        return;
    }
    
    if (!Note::allowDifferentFileName()) {
        return;
    }

    const int noteId = item->data(0, Qt::UserRole).toInt();
    Note note = _kbNoteMap->fetchNoteById(noteId);
    if (note.isFetched()) {
        qDebug() << __func__ << " - 'note': " << note;

        const QSignalBlocker blocker(this->noteDirectoryWatcher);
        Q_UNUSED(blocker)

        const Note oldNote = note;
        const QString oldNoteName = note.getName();

        if (note.renameNoteFile(item->text(0))) {
            QString newNoteName = note.getName();

            if (oldNoteName != newNoteName) {
                note.refetch();
                setCurrentNote(note);

                // handle the replacing of all note urls if a note was renamed
                if (note.handleNoteMoving(oldNote)) {
                    // reload the current note if we had to change it
                    reloadCurrentNoteByNoteId(true);
                }

                // reload the directory list if note name has changed
                //                loadNoteDirectoryList();

                // sort notes if note name has changed
                QSettings settings;
                if (settings
                        .value(QStringLiteral("notesPanelSort"),
                               SORT_BY_LAST_CHANGE)
                        .toInt() == SORT_ALPHABETICAL) {
                    ui->noteTreeWidget->sortItems(
                        0, toQtOrder(
                               settings.value(QStringLiteral("notesPanelOrder"))
                                   .toInt()));
                    ui->noteTreeWidget->scrollToItem(item);
                }
            }
        }

        const QSignalBlocker blocker2(ui->noteTreeWidget);
        Q_UNUSED(blocker2)

        // set old name back in case the renaming failed or the file name got
        // altered in the renaming process
        item->setText(0, note.getName());

        if (Utils::Misc::isNoteListPreview()) {
            updateNoteTreeWidgetItem(note, item);
        }
    }
}

void MainWindow::clearTagFilteringColumn() {
    QTreeWidgetItemIterator it(ui->noteTreeWidget);
    while (*it) {
        // if the item wasn't filtered by the searchLineEdit
        if ((*it)->data(4, Qt::UserRole).toBool()) {
            (*it)->setData(4, Qt::UserRole, false);
        }
        // reset the value for searchLineEdit
        ++it;
    }
}

/**
 * Toggles the case of the word under the Cursor or the selected text
 */
void MainWindow::on_actionToggle_text_case_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();
    // Save positions to restore everything at the end
    const int selectionStart = c.selectionStart();
    const int selectionEnd = c.selectionEnd();
    const int cPos = c.position();

    QString selectedText = c.selectedText();
    const bool textWasSelected = !selectedText.isEmpty();

    // if no text is selected: automatically select the Word under the Cursor
    if (selectedText.isEmpty()) {
        c.select(QTextCursor::WordUnderCursor);
        selectedText = c.selectedText();
    }

    // cycle text through lowercase, uppercase, start case, and sentence case
    c.insertText(Utils::Misc::cycleTextCase(selectedText));

    if (textWasSelected) {
        // select the text again to maybe do another operation on it
        // keep the original cursor position
        if (cPos == selectionStart) {
            c.setPosition(selectionEnd, QTextCursor::MoveAnchor);
            c.setPosition(selectionStart, QTextCursor::KeepAnchor);
        } else {
            c.setPosition(selectionStart, QTextCursor::MoveAnchor);
            c.setPosition(selectionEnd, QTextCursor::KeepAnchor);
        }
    } else {
        // Just restore the Cursor Position if no text was selected
        c.setPosition(cPos, QTextCursor::MoveAnchor);
    }
    // Restore the visible cursor
    textEdit->setTextCursor(c);
}

/**
 * Opens the Markdown Cheatsheet webpage
 */
void MainWindow::on_actionMarkdown_cheatsheet_triggered() {
    QDesktopServices::openUrl(
        QUrl("https://github.com/pbek/PKbSuite/blob/develop/src/demonotes"
             "/Markdown%20Cheatsheet.md"));
}

/**
 * Strikes out the selected text
 */
void MainWindow::on_actionStrike_out_text_triggered() {
    applyFormatter(QStringLiteral("~~"));
}

/**
 * Initializes the shortcuts for the actions
 *
 * @param setDefaultShortcut
 */
void MainWindow::initShortcuts() {
    const QList<QMenu *> menus = menuList();
    QSettings settings;

    // we also have to clear the shortcuts directly, just removing the
    // objects didn't remove the shortcut
    for (QShortcut *menuShortcut : Utils::asConst(_menuShortcuts)) {
        menuShortcut->setKey(QKeySequence());
    }

    // remove all menu shortcuts to create new ones
    _menuShortcuts.clear();

#ifndef Q_OS_MAC
    const bool menuBarIsVisible = !ui->menuBar->isHidden();
    qDebug() << __func__ << " - 'menuBarIsVisible': " << menuBarIsVisible;
#endif

    // loop through all menus
    for (QMenu *menu : menus) {
        // loop through all actions of the menu
        const auto actions = menu->actions();
        for (QAction *action : actions) {
            // we don't need empty objects
            if (action->objectName().isEmpty()) {
                continue;
            }

            QString oldShortcut = action->shortcut().toString();

#ifdef Q_OS_MAC
            // #1222, replace Option key by Ctrl key on macOS to prevent
            // blocking of accent characters when writing text
            oldShortcut.replace(QStringLiteral("Alt+"),
                                QStringLiteral("Meta+"));
#endif

            const QString &key =
                QStringLiteral("Shortcuts/MainWindow-") + action->objectName();
            const bool settingFound = settings.contains(key);
			
            // try to load a key sequence from the settings
            auto shortcut = QKeySequence(settingFound ?
                settings.value(key).toString() : "");

            // do we can this method the first time?
            if (!_isDefaultShortcutInitialized) {
                // set the default shortcut
                action->setData(oldShortcut);

                // if there was a shortcut set use the new shortcut
                if (!shortcut.isEmpty()) {
                    action->setShortcut(shortcut);
                }
            } else {
                // set to the default shortcut if no shortcut was found,
                // otherwise store the new shortcut
                action->setShortcut(
                    shortcut.isEmpty() ? action->data().toString() : shortcut);
            }

#ifndef Q_OS_MAC
            // if the menu bar is not visible (like for the Unity
            // desktop) create a workaround with a QShortcut so the
            // shortcuts are still working
            // we don't do that under OS X, it causes all shortcuts
            // to not be viewed
            if (!menuBarIsVisible) {
                shortcut = action->shortcut();
                action->setShortcut(QKeySequence());

                auto *shortcutItem = new QShortcut(shortcut, this);
                connect(shortcutItem, &QShortcut::activated, action,
                        &QAction::trigger);
                _menuShortcuts.append(shortcutItem);
            }
#endif
        }
    }

    if (!_isDefaultShortcutInitialized) {
        _isDefaultShortcutInitialized = true;
    }
}

/**
 * Shows or hides the main menu bar
 *
 * @param checked
 */
void MainWindow::on_actionShow_menu_bar_triggered(bool checked) {
    ui->menuBar->setVisible(checked);

    const QSignalBlocker blocker(ui->actionShow_menu_bar);
    {
        Q_UNUSED(blocker)
        ui->actionShow_menu_bar->setChecked(checked);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("showMenuBar"), checked);

    // show the action in the toolbar if the main menu isn't shown
    if (checked) {
        _windowToolbar->removeAction(ui->actionShow_menu_bar);
    } else {
        _windowToolbar->addAction(ui->actionShow_menu_bar);
    }

    // init the shortcuts again to create or remove the menu bar shortcut
    // workaround
    initShortcuts();
}

/**
 * Splits the current note into two notes at the current cursor position
 */
void MainWindow::on_actionSplit_note_at_cursor_position_triggered() {
    QString name = _currentNote.getName();
    const QSet<QString> tags = _currentNote.getTags();

    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();

    // select the text to get into a new note
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    const QString selectedText = c.selectedText();

    // remove the selected text
    c.removeSelectedText();
    textEdit->setTextCursor(c);

    Note previousNote = _currentNote;

    // create a new note
    createNewNote(std::move(name));

    // adding a link to new note into the old note
    previousNote.refetch();
    const QString noteLink = previousNote.getNoteUrlForLinkingTo(_currentNote);
    QString previousNoteText = previousNote.getNoteText();
    previousNoteText.reserve(3 + noteLink.size() + 1);
    previousNoteText +=
        QStringLiteral("\n\n<") + noteLink + QStringLiteral(">");
    previousNote.storeNewText(std::move(previousNoteText));

    // add the previously removed text
    textEdit = activeNoteTextEdit();
    textEdit->insertPlainText(selectedText);

    // link the tags of the old note to the new note
    for (const QString &tag : tags) {
        _currentNote.addTag(tag);
    }
}

/**
 * Sends an event to jump to "All notes" in the tag tree widget
 */
void MainWindow::selectAllNotesInTagTreeWidget() const {
    QKeyEvent *event =
        new QKeyEvent(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
    QCoreApplication::postEvent(ui->tagTreeWidget, event);
}

/**
 * Writes text to the note text edit (for ScriptingService)
 *
 * @param text
 */
void MainWindow::writeToNoteTextEdit(const QString &text) {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    textEdit->insertPlainText(text);
}

/**
 * Returns the text that is selected in the note text edit
 *
 * @return
 */
QString MainWindow::selectedNoteTextEditText() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QString selectedText = textEdit->textCursor().selectedText();

    // transform Unicode line endings
    // this newline character seems to be used in multi-line selections
    const QString newLine = QString::fromUtf8(QByteArray::fromHex("e280a9"));
    selectedText.replace(newLine, QStringLiteral("\n"));

    return selectedText;
}

/**
 * Locks and unlocks the dock widgets
 *
 * @param arg1
 */
void MainWindow::on_actionUnlock_panels_toggled(bool arg1) {
    const QSignalBlocker blocker(ui->actionUnlock_panels);
    {
        Q_UNUSED(blocker)
        ui->actionUnlock_panels->setChecked(arg1);
    }

    const QList<QDockWidget *> dockWidgets = findChildren<QDockWidget *>();

    if (!arg1) {
        // remove the title bar widgets of all dock widgets
        for (QDockWidget *dockWidget : dockWidgets) {
            // we don't want to lock floating dock widgets
            if (dockWidget->isFloating()) {
                continue;
            }

            // remove the title bar widget
            dockWidget->setTitleBarWidget(new QWidget());

#ifndef Q_OS_MAC
            // set 3px top margin for the enclosed widget
            dockWidget->widget()->setContentsMargins(0, 3, 0, 0);
#endif
        }
    } else {
        // add the old title bar widgets to all dock widgets
        _taggingDockWidget->setTitleBarWidget(_taggingDockTitleBarWidget);
        _noteSearchDockWidget->setTitleBarWidget(_noteSearchDockTitleBarWidget);
        _noteListDockWidget->setTitleBarWidget(_noteListDockTitleBarWidget);

        if (!_noteEditIsCentralWidget) {
            _noteEditDockWidget->setTitleBarWidget(_noteEditDockTitleBarWidget);
        }

        _noteTagDockWidget->setTitleBarWidget(_noteTagDockTitleBarWidget);
        _notePreviewDockWidget->setTitleBarWidget(
            _notePreviewDockTitleBarWidget);
        _graphDockWidget->setTitleBarWidget(
            _graphDockTitleBarWidget);

        for (QDockWidget *dockWidget : dockWidgets) {
            // reset the top margin of the enclosed widget
            dockWidget->widget()->setContentsMargins(0, 0, 0, 0);
        }
    }
}

/**
 * Creates a new workspace with asking for its name
 */
void MainWindow::on_actionStore_as_new_workspace_triggered() {
    const QString name = QInputDialog::getText(this, tr("Create new workspace"),
                                               tr("Workspace name:"))
                             .trimmed();

    if (name.isEmpty()) {
        return;
    }

    // store the current workspace
    storeCurrentWorkspace();

    // create the new workspace
    createNewWorkspace(name);
}

/**
 * Creates a new workspace with name
 *
 * @param name
 * @return
 */
bool MainWindow::createNewWorkspace(QString name) {
    name = name.trimmed();

    if (name.isEmpty()) {
        return false;
    }

    QSettings settings;
    const QString currentUuid = currentWorkspaceUuid();
    settings.setValue(QStringLiteral("previousWorkspace"), currentUuid);

    const QString uuid = Utils::Misc::createUuidString();
    QStringList workspaces = getWorkspaceUuidList();
    workspaces.append(uuid);

    settings.setValue(QStringLiteral("workspaces"), workspaces);
    settings.setValue(QStringLiteral("currentWorkspace"), uuid);
    settings.setValue(
        QStringLiteral("workspace-") + uuid + QStringLiteral("/name"), name);

    // store the new current workspace
    storeCurrentWorkspace();

    // update the menu and combo box
    updateWorkspaceLists();

    return true;
}

/**
 * Returns the uuid of the current workspace
 *
 * @return
 */
QString MainWindow::currentWorkspaceUuid() {
    QSettings settings;
    return settings.value(QStringLiteral("currentWorkspace")).toString();
}

/**
 * Sets the new current workspace when the workspace combo box index has changed
 */
void MainWindow::onWorkspaceComboBoxCurrentIndexChanged(int index) {
    Q_UNUSED(index)

    const QString uuid = _workspaceComboBox->currentData().toString();

    // set the new workspace
    setCurrentWorkspace(uuid);
}

/**
 * Sets a new current workspace
 */
void MainWindow::setCurrentWorkspace(const QString &uuid) {
    // store the current workspace
    storeCurrentWorkspace();

    QSettings settings;
    QString currentUuid = currentWorkspaceUuid();
    settings.setValue(QStringLiteral("previousWorkspace"), currentUuid);
    settings.setValue(QStringLiteral("currentWorkspace"), uuid);

    // restore the new workspace
    QTimer::singleShot(0, this, SLOT(restoreCurrentWorkspace()));

    // update the menu and combo box (but don't rebuild it)
    updateWorkspaceLists(false);

    // update the preview in case it was disable previously
    setNoteTextFromNote(&_currentNote, true);
}

/**
 * Stores the current workspace
 */
void MainWindow::storeCurrentWorkspace() {
    QSettings settings;
    QString uuid = currentWorkspaceUuid();

    settings.setValue(
        QStringLiteral("workspace-") + uuid + QStringLiteral("/windowState"),
        saveState());
}

/**
 * Restores the current workspace
 */
void MainWindow::restoreCurrentWorkspace() {
    QSettings settings;
    QStringList workspaces = getWorkspaceUuidList();
    QWidget *focusWidget = qApp->focusWidget();

    // create a default workspace if there is none yet
    if (workspaces.count() == 0) {
        createNewWorkspace(tr("full", "full workspace"));

        _taggingDockWidget->setVisible(false);
        _noteTagDockWidget->setVisible(false);
        _notePreviewDockWidget->setVisible(false);
        createNewWorkspace(tr("minimal", "minimal workspace"));

        // TODO: maybe still create those workspaces initially?
    }

    QString uuid = currentWorkspaceUuid();

    // set the first workspace as current workspace if there is none set
    if (uuid.isEmpty()) {
        workspaces = getWorkspaceUuidList();

        if (workspaces.count() == 0) {
            return;
        }

        uuid = workspaces.at(0);
        settings.setValue(QStringLiteral("currentWorkspace"), uuid);

        // update the menu and combo box
        updateWorkspaceLists();
    }

    restoreState(settings
                     .value(QStringLiteral("workspace-") + uuid +
                            QStringLiteral("/windowState"))
                     .toByteArray());

    // update the panel lists
    updatePanelMenu();

    // if app was newly installed we want to center and resize the window
    if (settings.value("initialWorkspace").toBool()) {
        settings.remove("initialWorkspace");
        centerAndResize();
    }

    if (focusWidget != Q_NULLPTR) {
        // set the focus to the widget that had the focus before
        // the workspace was restored
        focusWidget->setFocus();
    }
}

/**
 * Returns the list of workspace uuids
 * @return
 */
QStringList MainWindow::getWorkspaceUuidList() {
    QSettings settings;
    return settings.value(QStringLiteral("workspaces")).toStringList();
}

/**
 * Removes the current workspace
 */
void MainWindow::on_actionRemove_current_workspace_triggered() {
    QStringList workspaces = getWorkspaceUuidList();

    // there have to be at least one workspace
    if (workspaces.count() < 2) {
        return;
    }

    QString uuid = currentWorkspaceUuid();

    // if no workspace is set we can't remove it
    if (uuid.isEmpty()) {
        return;
    }

    // ask for permission
    if (Utils::Gui::question(this, tr("Remove current workspace"),
                             tr("Remove the current workspace?"),
                             QStringLiteral("remove-workspace")) !=
        QMessageBox::Yes) {
        return;
    }

    // reset current workspace
    workspaces.removeAll(uuid);
    const QString newUuid = workspaces.at(0);

    // set the new workspace
    setCurrentWorkspace(newUuid);

    QSettings settings;
    settings.setValue(QStringLiteral("workspaces"), workspaces);

    // remove all settings in the group
    settings.beginGroup(QStringLiteral("workspace-") + uuid);
    settings.remove(QLatin1String(""));
    settings.endGroup();

    // update the menu and combo box
    updateWorkspaceLists();
}

void MainWindow::on_actionRename_current_workspace_triggered() {
    const QString uuid = currentWorkspaceUuid();

    // if no workspace is set we can't rename it
    if (uuid.isEmpty()) {
        return;
    }

    QSettings settings;
    QString name = settings
                       .value(QStringLiteral("workspace-") + uuid +
                              QStringLiteral("/name"))
                       .toString();

    // ask for the new name
    name = QInputDialog::getText(this, tr("Rename workspace"),
                                 tr("Workspace name:"), QLineEdit::Normal, name)
               .trimmed();

    if (name.isEmpty()) {
        return;
    }

    // rename the workspace
    settings.setValue(
        QStringLiteral("workspace-") + uuid + QStringLiteral("/name"), name);

    // update the menu and combo box
    updateWorkspaceLists();
}

/**
 * Switch to the previous workspace
 */
void MainWindow::on_actionSwitch_to_previous_workspace_triggered() {
    QSettings settings;
    QString uuid =
        settings.value(QStringLiteral("previousWorkspace")).toString();

    if (!uuid.isEmpty()) {
        setCurrentWorkspace(uuid);
    }
}

/**
 * Shows all dock widgets
 */
void MainWindow::on_actionShow_all_panels_triggered() {
    const QList<QDockWidget *> dockWidgets = findChildren<QDockWidget *>();

    for (QDockWidget *dockWidget : dockWidgets) {
        dockWidget->setVisible(true);
    }

    // update the preview in case it was disable previously
    setNoteTextFromNote(&_currentNote, true);

    // filter notes according to selections
    filterNotes();
}

static void loadAllActions(QMenu* menu, QVector<QPair<QString, QAction*>>& outActions) {
    if (!menu) {
        return;
    }

    const auto menuActions = menu->actions();
    QVector<QPair<QString, QAction*>> actions;
    actions.reserve(menuActions.size());

    for (auto action : menuActions) {
        if (auto submenu = action->menu()) {
            loadAllActions(submenu, outActions);
        } else {
            if (!action->text().isEmpty() && !action->objectName().isEmpty() &&
                action->isVisible()) {
                outActions.append({menu->title(), action});
            }
        }
    }
}

/**
 * Opens the table dialog
 */
void MainWindow::on_actionInsert_table_triggered() {
    auto *dialog = new TableDialog(this);
    dialog->exec();
    delete (dialog);
}

/**
 * Inserts a block quote character or formats the selected text as block quote
 */
void MainWindow::on_actionInsert_block_quote_triggered() {
    auto *textEdit = activeNoteTextEdit();
    QTextCursor c = textEdit->textCursor();
    QString selectedText = c.selectedText();

    if (selectedText.isEmpty()) {
        c.insertText(QStringLiteral("> "));
        textEdit->setTextCursor(c);
    } else {
        // this only applies to the start of the selected block
        selectedText.replace(QRegularExpression(QStringLiteral("^")),
                             QStringLiteral("> "));

        // transform Unicode line endings
        // this newline character seems to be used in multi-line selections
        const QString newLine =
            QString::fromUtf8(QByteArray::fromHex("e280a9"));
        selectedText.replace(newLine, QStringLiteral("\n> "));

        // remove the block quote if it was placed at the end of the text
        selectedText.remove(QRegularExpression(QStringLiteral("> $")));

        c.insertText(selectedText);
    }
}

Note MainWindow::currentNote() const {
    return _currentNote;
}

/**
 * Searches for the selected text on the web
 */
void MainWindow::on_actionSearch_text_on_the_web_triggered() {
    auto *textEdit = activeNoteTextEdit();
    QString selectedText = textEdit->textCursor().selectedText().trimmed();

    if (selectedText.isEmpty()) {
        return;
    }

    // handling the case in which the saved engine id
    // has been removed

    QSettings settings;
    typedef Utils::Misc::SearchEngine SearchEngine;
    int selectedSearchEngineId =
        settings
            .value(QStringLiteral("SearchEngineId"),
                   Utils::Misc::getDefaultSearchEngineId())
            .toInt();
    QHash<int, SearchEngine> SearchEngines =
        Utils::Misc::getSearchEnginesHashMap();
    SearchEngine selectedEngine = SearchEngines.value(selectedSearchEngineId);
    QString searchEngineUrl = selectedEngine.searchUrl;
    QUrl url(searchEngineUrl + QUrl::toPercentEncoding(selectedText));
    QDesktopServices::openUrl(url);
}

/**
 * Updates the line number label
 */
void MainWindow::noteEditCursorPositionChanged() {
    if (!_noteEditLineNumberLabel->isVisible()) return;
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    QTextCursor cursor = textEdit->textCursor();
    QString selectedText = cursor.selectedText();
    QString text;

    this->noteHistory.updateCursorPositionOfNote(_currentNote, textEdit);

    if (!selectedText.isEmpty()) {
        text = tr("%n chars", "characters", selectedText.count()) + "  ";
    }

    text += QString::number(cursor.block().blockNumber() + 1) +
            QStringLiteral(":") + QString::number(cursor.positionInBlock() + 1);

    _noteEditLineNumberLabel->setText(text);
}

/**
 * Deletes the current line in the active note text edit
 */
void MainWindow::on_actionDelete_line_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();

    // if the note text edit doesn't have the focus delegate the default
    // shortcut to the widget with the focus
    if (!textEdit->hasFocus()) {
        QKeyEvent *event =
            new QKeyEvent(QEvent::KeyPress, Qt::Key_Backspace, Qt::AltModifier);

        // we need a special fallback for QLineEdit because it seems to ignore
        // our event
        if (dynamic_cast<QLineEdit *>(QApplication::focusWidget()) != nullptr) {
            auto *lineEdit =
                dynamic_cast<QLineEdit *>(QApplication::focusWidget());
            lineEdit->clear();
        } else {
            QApplication::postEvent(QApplication::focusWidget(), event);
        }
	
		delete event;
		
        return;
    }

    QTextCursor cursor = textEdit->textCursor();
    cursor.select(QTextCursor::BlockUnderCursor);
    QString selectedText = cursor.selectedText();

    if (selectedText.isEmpty()) {
        cursor.deletePreviousChar();
    } else {
        // remove the text in the current line
        cursor.removeSelectedText();
    }

    cursor.movePosition(QTextCursor::NextBlock);
    textEdit->setTextCursor(cursor);
}

/**
 * Deletes the current word in the active note text edit
 */
void MainWindow::on_actionDelete_word_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();

    // if the note text edit doesn't have the focus delegate the default
    // shortcut to the widget with the focus
    if (!textEdit->hasFocus()) {
        QKeyEvent *event = new QKeyEvent(QEvent::KeyPress, Qt::Key_Backspace,
                                         Qt::ControlModifier);

        // we need a special fallback for QLineEdit because it seems to ignore
        // our event
        if (dynamic_cast<QLineEdit *>(QApplication::focusWidget()) != nullptr) {
            auto *lineEdit =
                dynamic_cast<QLineEdit *>(QApplication::focusWidget());
            lineEdit->cursorWordBackward(true);
            lineEdit->del();
        } else {
            QApplication::postEvent(QApplication::focusWidget(), event);
        }

		delete event;
			
        return;
    }

    QTextCursor cursor = textEdit->textCursor();

    if (cursor.selectedText().isEmpty()) {
        cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
    }

    cursor.removeSelectedText();
}

/**
 * Opens the current note in a dialog
 */
void MainWindow::on_actionView_note_in_new_window_triggered() {
    auto *dialog = new NoteDialog(this);
    dialog->setNote(_currentNote);
    dialog->show();
}

/**
 * Manually stores updated notes to disk
 */
void MainWindow::on_actionSave_modified_notes_triggered() {
    // store updated notes to disk
    storeUpdatedNotesToDisk();
}

/**
 * Sets ascending note sort order
 */
void MainWindow::on_actionAscending_triggered() {
    QSettings settings;
    settings.setValue(QStringLiteral("notesPanelOrder"), ORDER_ASCENDING);
    ui->noteTreeWidget->sortItems(0, toQtOrder(ORDER_ASCENDING));
}

/**
 * Sets descending note sort order
 */
void MainWindow::on_actionDescending_triggered() {
    QSettings settings;
    settings.setValue(QStringLiteral("notesPanelOrder"), ORDER_DESCENDING);
    ui->noteTreeWidget->sortItems(0, toQtOrder(ORDER_DESCENDING));
}

/**
 * Updates the visibility of the note sort order selector
 */
void MainWindow::updateNoteSortOrderSelectorVisibility(bool visible) {
    ui->actionAscending->setVisible(visible);
    ui->actionDescending->setVisible(visible);
//    ui->sortOrderSeparator->setVisible(visible);
}

Qt::SortOrder MainWindow::toQtOrder(int order) {
    return order == ORDER_ASCENDING ? Qt::AscendingOrder : Qt::DescendingOrder;
}

void MainWindow::updatePanelsSortOrder() {
    updateNotesPanelSortOrder();
    // do not reload it again, it has already been reloaded when
    // updateNotesPanelSortOrder() was called
    //reloadTagTree();
}

void MainWindow::updateNotesPanelSortOrder() {
    QSettings settings;
    int sort =
        settings.value(QStringLiteral("notesPanelSort"), SORT_BY_LAST_CHANGE)
            .toInt();
    ui->actionAlphabetical->setChecked(sort == SORT_ALPHABETICAL);
    ui->actionBy_date->setChecked(sort == SORT_BY_LAST_CHANGE);

    updateNoteSortOrderSelectorVisibility(sort == SORT_ALPHABETICAL);

    int order = settings.value(QStringLiteral("notesPanelOrder")).toInt();
    ui->actionAscending->setChecked(order == ORDER_ASCENDING);
    ui->actionDescending->setChecked(order == ORDER_DESCENDING);

    loadNoteDirectoryList();
}

/**
 * Inserts a file as attachment
 */
void MainWindow::on_actionInsert_attachment_triggered() {
    auto *dialog = new AttachmentDialog(this);
    dialog->exec();

    if (dialog->result() == QDialog::Accepted) {
        insertAttachment(dialog->getFile(), dialog->getTitle());
    }

    delete (dialog);
}

/**
 * Enables or disables a menu and all its actions
 *
 * @param menu
 * @param enabled
 */
void MainWindow::setMenuEnabled(QMenu *menu, bool enabled) {
    menu->setEnabled(enabled);

    // loop through all actions of the menu
    const auto actions = menu->actions();
    for (QAction *action : actions) {
        action->setEnabled(enabled);
    }
}


void MainWindow::updateJumpToActionsAvailability()
{
    ui->actionJump_to_note_list_panel->setEnabled(ui->notesListFrame->isVisible());
    ui->actionJump_to_tags_panel->setEnabled(ui->tagFrame->isVisible());
}

void MainWindow::noteTextEditResize(QResizeEvent *event) {
    Q_UNUSED(event)
    ui->noteTextEdit->setPaperMargins();

    // Set the navigation pane inside the editing area.
    ui->navigationFrame->move(ui->noteTextEdit->width() - ui->navigationFrame->width() - 25, 10);
    Qt::WindowFlags flags = ui->navigationFrame->windowFlags();
    ui->navigationFrame->setWindowFlags(flags |= Qt::WindowStaysOnTopHint);
    ui->navigationFrame->adjustSize();	
}

void MainWindow::on_actionJump_to_note_text_edit_triggered() {
    if (!_noteEditIsCentralWidget) {
        _noteEditDockWidget->show();
    }

    activeNoteTextEdit()->setFocus();
}

/**
 * Double-clicking a tag assigns the tag to the current note
 */
void MainWindow::on_tagTreeWidget_itemDoubleClicked(QTreeWidgetItem *item,
                                                    int column) {
    Q_UNUSED(column)
    QString tag = item->data(0, Qt::DisplayRole).toString();

    TagMap* tagMap = TagMap::getInstance();

    if (tagMap->tagExists(tag)) {
        // workaround when signal block doesn't work correctly
        directoryWatcherWorkaround(true, true);

        const QSignalBlocker blocker(noteDirectoryWatcher);
        Q_UNUSED(blocker)

        if (_currentNote.isTagged(tag)) {
            _currentNote.removeTag(tag);
        } else {
            _currentNote.addTag(tag);
        }

        filterNotes();

        reloadCurrentNoteTags();
        reloadTagTree();

        // turn off the workaround again
        directoryWatcherWorkaround(false, true);
    }
}

/**
 * Double-clicking a note calls a hook
 */
void MainWindow::on_noteTreeWidget_itemDoubleClicked(QTreeWidgetItem *item,
                                                     int column) {
    Q_UNUSED(item)
    Q_UNUSED(column)

    openCurrentNoteInTab();
}

/**
 * Reloads the current note (and selected notes) tags if there were selected
 * multiple notes
 */
void MainWindow::on_noteTreeWidget_itemSelectionChanged() {
    qDebug() << __func__;
    if (ui->noteTreeWidget->selectedItems().size() == 1) {
        int noteId = ui->noteTreeWidget->selectedItems()[0]->data(0, Qt::UserRole).toInt();
        Note note = _kbNoteMap->fetchNoteById(noteId);
        bool currentNoteChanged = _currentNote.getId() != noteId;

        _currentNote.updateReferencedBySectionInLinkedNotes();

        setCurrentNote(std::move(note), true, false);

        // Let's highlight the text from the search line edit and do an "in-note
        // search" if the current note has changed and there is a search term
        // in the search line edit
        if (currentNoteChanged && !ui->searchLineEdit->text().isEmpty()) {
            searchForSearchLineTextInNoteTextEdit();

            // prevent that the last occurrence of the search term is found
            // first, instead the first occurrence should be found first
            ui->noteTextEdit->searchWidget()->doSearchDown();
            ui->noteTextEdit->searchWidget()->updateSearchExtraSelections();
        }
    }

    // we also need to do this in setCurrentNote because of different timings
    reloadCurrentNoteTags();
}

void MainWindow::on_noteOperationsButton_clicked() {
    QPoint globalPos = ui->noteOperationsButton->mapToGlobal(
            QPoint(0, ui->noteOperationsButton->height()));
    openNotesContextMenu(globalPos, true);
}

void MainWindow::on_actionImport_notes_from_text_files_triggered() {
    FileDialog dialog(QStringLiteral("ImportTextFiles"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setWindowTitle(tr("Select text files to import"));
    int ret = dialog.exec();

    if (ret != QDialog::Accepted) {
        return;
    }

    QStringList fileNames = dialog.selectedFiles();
    const int fileCount = fileNames.size();

    if (fileCount == 0) {
        return;
    }

    QProgressDialog progressDialog(QString(), tr("Cancel"), 0, fileCount, this);
    progressDialog.setWindowModality(Qt::WindowModal);

    const QSignalBlocker blocker(noteDirectoryWatcher);
    Q_UNUSED(blocker)

    for (int i = 0; i < fileCount; i++) {
        if (progressDialog.wasCanceled()) {
            break;
        }

        const QString &fileName = fileNames.at(i);

        QFile file(fileName);
        QFileInfo fileInfo(file);
        progressDialog.setLabelText(
            tr("Importing: %1").arg(fileInfo.fileName()));

        file.open(QFile::ReadOnly | QFile::Text);
        QTextStream ts(&file);
        QString text = ts.readAll().trimmed();

        QRegularExpressionMatch match =
            QRegularExpression(QStringLiteral(R"(^.+\n=+)"),
                               QRegularExpression::MultilineOption)
                .match(text);

        CreateNewNoteOptions options = CreateNewNoteOption::None;

        // add a headline if none was found
        if (!match.hasMatch()) {
            options = CreateNewNoteOption::UseNameAsHeadline;
        }

        options |= CreateNewNoteOption::DisableLoadNoteDirectoryList;

        createNewNote(fileInfo.baseName(), text, options);
        progressDialog.setValue(i);
    }

    progressDialog.setValue(fileCount);
    loadNoteDirectoryList();
}

/**
 * Copies the headline of the current note
 */
void MainWindow::on_actionCopy_headline_triggered() {
    QString noteText = _currentNote.getNoteText();

    // try regular headlines
    QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral(R"(^(.+)\n=+)"),
                           QRegularExpression::MultilineOption)
            .match(noteText);

    QString headline;
    if (match.hasMatch()) {
        headline = match.captured(1);
    } else {
        // try alternative headlines
        match = QRegularExpression(QStringLiteral(R"(^#+ (.+)$)"),
                                   QRegularExpression::MultilineOption)
                    .match(noteText);

        if (match.hasMatch()) {
            headline = match.captured(1);
        }
    }

    if (!headline.isEmpty()) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(headline);
        showStatusBarMessage(
            tr("Note headline '%1' was copied to the clipboard").arg(headline),
            3000);
    }
}

void MainWindow::on_action_FormatTable_triggered() {
    PKbSuiteMarkdownTextEdit *textEdit = activeNoteTextEdit();
    Utils::Gui::autoFormatTableAtCursor(textEdit);
}

/**
 * Centers and resized the main window
 */
void MainWindow::centerAndResize() {
    // get the dimension available on this screen
    QSize availableSize =
        QGuiApplication::primaryScreen()->availableGeometry().size();
    int width = availableSize.width();
    int height = availableSize.height();
    qDebug() << "Available dimensions " << width << "x" << height;
    width *= 0.9;     // 90% of the screen size
    height *= 0.9;    // 90% of the screen size
    qDebug() << "Computed dimensions " << width << "x" << height;
    QSize newSize(width, height);

    setGeometry(QStyle::alignedRect(
        Qt::LeftToRight, Qt::AlignCenter, newSize,
        QGuiApplication::primaryScreen()->availableGeometry()));
}

/**
 * Filters navigation entries in the navigation tree widget
 */
void MainWindow::on_navigationLineEdit_textChanged(const QString &arg1) {
    Utils::Gui::searchForTextInTreeWidget(
        ui->navigationWidget, arg1, Utils::Gui::TreeWidgetSearchFlag::IntCheck);
}

Note MainWindow::getCurrentNote() { return _currentNote; }

void MainWindow::on_actionJump_to_note_list_panel_triggered() {
}

void MainWindow::on_actionJump_to_tags_panel_triggered() {
    ui->tagTreeWidget->setFocus();
}

void MainWindow::on_actionActivate_context_menu_triggered() {
    activateContextMenu();
}

void MainWindow::on_actionToggle_fullscreen_triggered() {
    // #1302: we need to init the button in any case if the app was already in
    //        fullscreen mode or "disconnect" will crash the app
    if (_leaveFullScreenModeButton == nullptr) {
        _leaveFullScreenModeButton = new QPushButton(tr("leave"));
    }

    if (isFullScreen()) {
        showNormal();

        // we need a showNormal() first to exist full-screen mode
        if (_isMaximizedBeforeFullScreen) {
            showMaximized();
        } else if (_isMinimizedBeforeFullScreen) {
            showMinimized();
        }

        statusBar()->removeWidget(_leaveFullScreenModeButton);
        disconnect(_leaveFullScreenModeButton, Q_NULLPTR, Q_NULLPTR, Q_NULLPTR);
        delete _leaveFullScreenModeButton;
        _leaveFullScreenModeButton = nullptr;
    } else {
        _isMaximizedBeforeFullScreen = isMaximized();
        _isMinimizedBeforeFullScreen = isMinimized();
        showFullScreen();

        _leaveFullScreenModeButton->setFlat(true);
        _leaveFullScreenModeButton->setToolTip(tr("Leave full-screen mode"));
        _leaveFullScreenModeButton->setStyleSheet(
            QStringLiteral("QPushButton {padding: 0 5px}"));

        _leaveFullScreenModeButton->setIcon(QIcon::fromTheme(
            QStringLiteral("zoom-original"),
            QIcon(QStringLiteral(
                ":icons/breeze-pkbsuite/16x16/zoom-original.svg"))));

        connect(_leaveFullScreenModeButton, &QPushButton::clicked, this,
                &MainWindow::on_actionToggle_fullscreen_triggered);

        statusBar()->addPermanentWidget(_leaveFullScreenModeButton);
    }
}

void MainWindow::disableFullScreenMode() {
    if (isFullScreen()) {
        on_actionToggle_fullscreen_triggered();
    }
}

void MainWindow::on_actionTypewriter_mode_toggled(bool arg1) {
    QSettings settings;
    settings.setValue(QStringLiteral("Editor/centerCursor"), arg1);
    ui->noteTextEdit->updateSettings();

    if (arg1) {
        // center the cursor immediately if typewriter mode is turned on
        activeNoteTextEdit()->centerTheCursor();
    }
}

void MainWindow::on_actionCheck_spelling_toggled(bool checked) {
    QSettings settings;
    settings.setValue(QStringLiteral("checkSpelling"), checked);
    ui->noteTextEdit->updateSettings();
}

void MainWindow::loadDictionaryNames() {
    QSettings settings;

    QStringList languages = Sonnet::Speller::availableLanguages();
    QStringList langNames = Sonnet::Speller::availableLanguageNames();

    // if there are no dictionaries installed, disable the spellchecker
    if (languages.isEmpty()) {
        settings.setValue(QStringLiteral("checkSpelling"), false);
        ui->actionCheck_spelling->setEnabled(false);
        ui->menuLanguages->setTitle(QStringLiteral("No dictionaries found"));
        ui->menuLanguages->setEnabled(false);
        ui->noteTextEdit->updateSettings();
        return;
    }

    _languageGroup->setExclusive(true);
    connect(_languageGroup, &QActionGroup::triggered, this,
            &MainWindow::onLanguageChanged);

    // first add autoDetect
    QAction *autoDetect =
        ui->menuLanguages->addAction(tr("Automatically detect"));
    autoDetect->setCheckable(true);
    autoDetect->setData(QStringLiteral("auto"));
    autoDetect->setActionGroup(_languageGroup);
    QString prevLang =
        settings
            .value(QStringLiteral("spellCheckLanguage"), QStringLiteral("auto"))
            .toString();
    // if only one dictionary found, disable auto detect
    if (languages.length() > 1) {
        if (prevLang == QStringLiteral("auto")) {
            autoDetect->setChecked(true);
            autoDetect->trigger();
        }
    } else {
        autoDetect->setChecked(false);
        autoDetect->setEnabled(false);
    }

    // not really possible but just in case
    if (langNames.length() != languages.length()) {
        qWarning() << "Error: langNames.length != languages.length()";
        return;
    }

    QStringList::const_iterator it = langNames.constBegin();
    QStringList::const_iterator itt = languages.constBegin();
    for (; it != langNames.constEnd(); ++it, ++itt) {
        QAction *action = ui->menuLanguages->addAction(*it);
        action->setCheckable(true);
        action->setActionGroup(_languageGroup);
        action->setData(*itt);

        if (*itt == prevLang || languages.length() == 1) {
            action->setChecked(true);
            action->trigger();
        }
    }
}

void MainWindow::onLanguageChanged(QAction *action) {
    QString lang = action->data().toString();
    QSettings settings;
    settings.setValue(QStringLiteral("spellCheckLanguage"), lang);
    ui->noteTextEdit->updateSettings();
}

void MainWindow::loadSpellingBackends() {
    QSettings settings;
    QString prevBackend = settings
                              .value(QStringLiteral("spellCheckBackend"),
                                     QStringLiteral("Hunspell"))
                              .toString();

    _spellBackendGroup->setExclusive(true);
    connect(_spellBackendGroup, &QActionGroup::triggered, this,
            &MainWindow::onBackendChanged);

    QAction *hs =
        ui->menuSpelling_backend->addAction(QStringLiteral("Hunspell"));
    hs->setCheckable(true);
    hs->setData("Hunspell");
    hs->setActionGroup(_spellBackendGroup);
    QAction *as = ui->menuSpelling_backend->addAction(QStringLiteral("Aspell"));
    as->setCheckable(true);
    as->setActionGroup(_spellBackendGroup);
    as->setData("Aspell");

    if (prevBackend == hs->data()) {
        hs->setChecked(true);
    } else {
        as->setChecked(true);
    }
}

void MainWindow::onBackendChanged(QAction *action) {
    QString backend = action->data().toString();
    QSettings settings;
    settings.setValue(QStringLiteral("spellCheckBackend"), backend);
    showRestartNotificationIfNeeded(true);
}

void MainWindow::on_actionManage_dictionaries_triggered() {
    auto *dialog = new DictionaryManagerDialog(this);
    dialog->exec();
    delete (dialog);

    // shows a restart application notification
    showRestartNotificationIfNeeded();
}

void MainWindow::on_noteTextEdit_modificationChanged(bool arg1) {
    if (!arg1) {
        return;
    }

    ui->noteTextEdit->document()->setModified(false);
    noteTextEditTextWasUpdated();
}

void MainWindow::on_actionEditorWidthCustom_triggered() {
    QSettings settings;
    bool ok;
    int characters = QInputDialog::getInt(
        this, tr("Custom editor width"), tr("Characters:"),
        settings
            .value(QStringLiteral("DistractionFreeMode/editorWidthCustom"), 80)
            .toInt(),
        20, 10000, 1, &ok);

    if (ok) {
        settings.setValue(
            QStringLiteral("DistractionFreeMode/editorWidthCustom"),
            characters);
    }
}

void MainWindow::on_actionShow_Hide_application_triggered() {
    // isVisible() or isHidden() didn't work properly
    if (isActiveWindow()) {
        hide();
    } else {
        showWindow();
    }
}

void MainWindow::on_noteEditTabWidget_currentChanged(int index) {
    QWidget *widget = ui->noteEditTabWidget->currentWidget();
    const int noteId = widget->property("note-id").toInt();

    // close the tab if note doesn't exist any more
    if (!Note::noteIdExists(noteId)) {
        removeNoteTab(index);
        return;
    }

    setCurrentNoteFromNoteId(noteId);
    widget->setLayout(ui->noteEditTabWidgetLayout);

    closeOrphanedTabs();
}

void MainWindow::on_noteEditTabWidget_tabCloseRequested(int index) {
    removeNoteTab(index);
}

void MainWindow::on_actionPrevious_note_tab_triggered() {
    int index = ui->noteEditTabWidget->currentIndex() - 1;

    if (index < 0) {
        index = ui->noteEditTabWidget->count() - 1;
    }

    ui->noteEditTabWidget->setCurrentIndex(index);
    focusNoteTextEdit();
}

void MainWindow::on_actionNext_note_tab_triggered() {
    int index = ui->noteEditTabWidget->currentIndex() + 1;

    if (index >= ui->noteEditTabWidget->count()) {
        index = 0;
    }

    ui->noteEditTabWidget->setCurrentIndex(index);
    focusNoteTextEdit();
}

void MainWindow::on_actionClose_current_note_tab_triggered() {
    removeNoteTab(ui->noteEditTabWidget->currentIndex());
}

void MainWindow::on_actionNew_note_in_new_tab_triggered() {
    on_action_New_note_triggered();
    openCurrentNoteInTab();
}

void MainWindow::removeNoteTab(int index) const {
    if (ui->noteEditTabWidget->count() > 1) {
        ui->noteEditTabWidget->removeTab(index);
    }
}

void MainWindow::on_noteEditTabWidget_tabBarDoubleClicked(int index) {
    Utils::Gui::setTabWidgetTabSticky(ui->noteEditTabWidget, index,
          !Utils::Gui::isTabWidgetTabSticky(ui->noteEditTabWidget, index));
}

void MainWindow::on_actionToggle_note_stickiness_of_current_tab_triggered() {
    on_noteEditTabWidget_tabBarDoubleClicked(ui->noteEditTabWidget->currentIndex());
}

void MainWindow::on_actionShow_Preview_Panel_triggered(bool checked) {
    // update the preview in case it was disable previously
	
	if (checked)
		_notePreviewDockWidget->show();
	else
		_notePreviewDockWidget->hide();
}

void MainWindow::on_actionShow_Note_Graph_triggered(bool checked) {
    // update the preview in case it was disable previously

	if (checked)
		_graphDockWidget->show();
	else
		_graphDockWidget->hide();
}

/**
 * Note tab context menu
 */
void MainWindow::showNoteEditTabWidgetContextMenu(const QPoint &point) {
    if (point.isNull()) {
        return;
    }

    int tabIndex = ui->noteEditTabWidget->tabBar()->tabAt(point);
    auto *menu = new QMenu();

    // Toggle note stickiness
    auto *stickAction = menu->addAction(tr("Toggle note stickiness"));
    connect(stickAction, &QAction::triggered, this, [this, tabIndex]() {
        on_noteEditTabWidget_tabBarDoubleClicked(tabIndex);
    });

    // Close other note tabs
    auto *closeAction = menu->addAction(tr("Close other note tabs"));
    connect(closeAction, &QAction::triggered, this, [this, tabIndex]() {
        const int maxIndex = ui->noteEditTabWidget->count() - 1;
        const int keepNoteId = Utils::Gui::getTabWidgetNoteId(
            ui->noteEditTabWidget, tabIndex);

        for (int i = maxIndex; i >= 0; i--) {
            const int noteId = Utils::Gui::getTabWidgetNoteId(
                ui->noteEditTabWidget, i);

            if (noteId != keepNoteId) {
                removeNoteTab(i);
            }
        }
    });

    menu->exec(ui->noteEditTabWidget->tabBar()->mapToGlobal(point));
}

void MainWindow::on_actionJump_to_navigation_panel_triggered() {
    if (ui->navigationLineEdit->isVisible()) {
        ui->navigationLineEdit->setFocus();
    } else {
        ui->navigationWidget->setFocus();
    }
}

/**
 * Imports a DataUrl as file into QOwnNotes and inserts it into the current note
 * This currently only supports images
 * @param dataUrl
 * @return
 */
bool MainWindow::insertDataUrlAsFileIntoCurrentNote(const QString &dataUrl) {
    QString markdownCode = _currentNote.importMediaFromDataUrl(dataUrl);

    if (markdownCode.isEmpty()) {
        return false;
    }

    insertNoteText(markdownCode);

    return true;
}
