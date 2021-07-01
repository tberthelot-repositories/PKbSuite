#-------------------------------------------------
#
# Project created by QtCreator 2014-11-29T08:31:41
#
#-------------------------------------------------

QT       += core gui widgets sql svg network xml printsupport qml websockets concurrent quickcontrols2

lessThan(QT_MAJOR_VERSION, 6) {
    QT += xmlpatterns
}

CONFIG += with_aspell debug

TARGET = PKbSuite
TEMPLATE = app
ICON = PKbSuite.ico
RC_FILE = PKbSuite.rc
TRANSLATIONS = $$PWD/languages/PKbSuite_en.ts \
    $$PWD/languages/PKbSuite_fr.ts

CODECFORTR = UTF-8
CONFIG += c++11 with_aspell

INCLUDEPATH += $$PWD/libraries $$PWD/libraries/diff_match_patch /usr/include/poppler

LIBS += -lpoppler-qt5 -lpoppler

SOURCES += main.cpp\
    dialogs/attachmentdialog.cpp \
    helpers/codetohtmlconverter.cpp \
    helpers/qownspellchecker.cpp \
	mainwindow.cpp \
    libraries/diff_match_patch/diff_match_patch.cpp \
    libraries/versionnumber/versionnumber.cpp \
    libraries/md4c/md4c/md4c.c \
    libraries/md4c/md2html/render_html.c \
    libraries/md4c/md2html/entity.c \
    dialogs/aboutdialog.cpp \
    dialogs/linkdialog.cpp \
    dialogs/notediffdialog.cpp \
    dialogs/settingsdialog.cpp \
    dialogs/localtrashdialog.cpp \
    entities/note.cpp \
    entities/trashitem.cpp \
    entities/notesubfolder.cpp \
    entities/notehistory.cpp \
    entities/notefolder.cpp \
    entities/tag.cpp \
    entities/bookmark.cpp \
    helpers/htmlentities.cpp \
    helpers/toolbarcontainer.cpp \
    helpers/pkbsuitemarkdownhighlighter.cpp \
    helpers/flowlayout.cpp \
    services/databaseservice.cpp \
    widgets/graphicsview.cpp \
    widgets/pkbsuitemarkdowntextedit.cpp \
    dialogs/masterdialog.cpp \
    utils/misc.cpp \
    utils/gui.cpp \
    utils/schema.cpp \
    dialogs/welcomedialog.cpp \
    dialogs/tagadddialog.cpp \
    widgets/navigationwidget.cpp \
    widgets/notepreviewwidget.cpp \
    api/noteapi.cpp \
    api/notesubfolderapi.cpp \
    api/tagapi.cpp \
    widgets/combobox.cpp \
    widgets/fontcolorwidget.cpp \
    dialogs/tabledialog.cpp \
    libraries/qtcsv/src/sources/reader.cpp \
    dialogs/dropPDFDialog.cpp \
    dialogs/notedialog.cpp \
    dialogs/filedialog.cpp \
    dialogs/dictionarymanagerdialog.cpp \
    widgets/label.cpp \
    widgets/lineedit.cpp \
    widgets/qtexteditsearchwidget.cpp \
    widgets/notefolderlistwidget.cpp \
    widgets/notetreewidgetitem.cpp \
    pdffile.cpp \
    widgets/layoutwidget.cpp \
    dialogs/imagedialog.cpp \
    dialogs/commandbar.cpp \
    models/commandmodel.cpp \
    libraries/fuzzy/kfuzzymatcher.cpp

HEADERS  += mainwindow.h \
    build_number.h \
    dialogs/attachmentdialog.h \
    helpers/LanguageCache.h \
    helpers/codetohtmlconverter.h \
    helpers/qownspellchecker.h \
    version.h \
    libraries/diff_match_patch/diff_match_patch.h \
    libraries/versionnumber/versionnumber.h \
    libraries/md4c/md4c/md4c.h \
    libraries/md4c/md2html/render_html.h \
    libraries/md4c/md2html/entity.h \
    entities/notehistory.h \
    entities/note.h \
    entities/trashitem.h \
    entities/notesubfolder.h \
    entities/notefolder.h \
    entities/tag.h \
    entities/bookmark.h \
    dialogs/aboutdialog.h \
    dialogs/linkdialog.h \
    dialogs/notediffdialog.h \
    dialogs/settingsdialog.h \
    dialogs/localtrashdialog.h \
    helpers/htmlentities.h \
    helpers/toolbarcontainer.h \
    helpers/pkbsuitemarkdownhighlighter.h \
    helpers/flowlayout.h \
    services/databaseservice.h \
    release.h \
    widgets/graphicsview.h \
    widgets/pkbsuitemarkdowntextedit.h \
    dialogs/masterdialog.h \
    utils/misc.h \
    utils/gui.h \
    utils/schema.h \
    dialogs/welcomedialog.h \
    dialogs/tagadddialog.h \
    widgets/navigationwidget.h \
    widgets/notepreviewwidget.h \
    api/noteapi.h \
    api/notesubfolderapi.h \
    api/tagapi.h \
    widgets/combobox.h \
    widgets/fontcolorwidget.h \
    dialogs/tabledialog.h \
    libraries/qtcsv/src/include/qtcsv_global.h \
    libraries/qtcsv/src/include/abstractdata.h \
    libraries/qtcsv/src/include/reader.h \
    libraries/qtcsv/src/sources/filechecker.h \
    libraries/qtcsv/src/sources/symbols.h \
    dialogs/dropPDFDialog.h \
    dialogs/notedialog.h \
    dialogs/filedialog.h \
    dialogs/dictionarymanagerdialog.h \
    widgets/label.h \
    widgets/lineedit.h \
    widgets/qtexteditsearchwidget.h \
    widgets/notefolderlistwidget.h \
    widgets/notetreewidgetitem.h \
    pdffile.h \
    widgets/layoutwidget.h \
    dialogs/imagedialog.h \
    dialogs/commandbar.h \
    models/commandmodel.h \
    libraries/fuzzy/kfuzzymatcher.h

FORMS    += mainwindow.ui \
    dialogs/attachmentdialog.ui \
    dialogs/imagedialog.ui \
    dialogs/notediffdialog.ui \
    dialogs/aboutdialog.ui \
    dialogs/settingsdialog.ui \
    dialogs/localtrashdialog.ui \
    dialogs/linkdialog.ui \
    dialogs/welcomedialog.ui \
    dialogs/tagadddialog.ui \
    widgets/fontcolorwidget.ui \
    dialogs/tabledialog.ui \
    dialogs/notedialog.ui \
    dialogs/dictionarymanagerdialog.ui \
    widgets/qtexteditsearchwidget.ui \
    widgets/notetreewidgetitem.ui \
    dialogs/dropPDFDialog.ui \
    widgets/layoutwidget.ui \

RESOURCES += \
    images.qrc \
    texts.qrc \
    breeze-pkbsuite.qrc \
    breeze-dark-pkbsuite.qrc \
    pkbsuite.qrc \
    demonotes.qrc \
    libraries/qdarkstyle/style.qrc \
    configurations.qrc

include(libraries/qmarkdowntextedit/qmarkdowntextedit.pri)
include(libraries/qkeysequencewidget/qkeysequencewidget/qkeysequencewidget.pri)
include(libraries/qttoolbareditor/toolbar_editor.pri)
include(libraries/singleapplication/singleapplication.pri)
include(libraries/sonnet/src/core/sonnet-core.pri)
include(libraries/qhotkey/qhotkey.pri)

unix {
  isEmpty(PREFIX) {
    PREFIX = /usr
  }

  isEmpty(BINDIR) {
    BINDIR = $$PREFIX/bin
  }

  isEmpty(DATADIR) {
    DATADIR = $$PREFIX/share
  }

  INSTALLS += target desktop i18n icons

  target.path = $$INSTROOT$$BINDIR
#  target.files += PKbSuite

  desktop.path = $$DATADIR/applications
  desktop.files += PKbSuite.desktop

  i18n.path = $$DATADIR/PKbSuite/languages
  i18n.files += languages/*.qm

  icons.path = $$DATADIR/icons/hicolor
  icons.files += images/icons/*
}

CONFIG(debug, debug|release) {
#    QMAKE_CXXFLAGS_DEBUG += -g3 -O0
    message("Currently in DEBUG mode.")
} else {
    DEFINES += QT_NO_DEBUG
    DEFINES += QT_NO_DEBUG_OUTPUT
    message("Currently in RELEASE mode.")
}

DEFINES += QAPPLICATION_CLASS=QApplication
