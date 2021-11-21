cmake_minimum_required(VERSION 3.2.2)
project(PKbSuite)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
#set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

#include sonnet core directory, required for building plugins
include_directories(libraries/sonnet/src/core)
#some hunspell settings for windows
include(libraries/sonnet/src/plugins/hunspell/hunspell/CMakeLists.txt)


find_package( Qt5LinguistTools )
find_package( Qt5Core 5.3 REQUIRED )
find_package( Qt5Widgets REQUIRED )
find_package( Qt5Gui REQUIRED )
find_package( Qt5Sql REQUIRED )
find_package( Qt5Svg REQUIRED )
find_package( Qt5Network REQUIRED )
find_package( Qt5Xml REQUIRED )
find_package( Qt5XmlPatterns REQUIRED )
find_package( Qt5PrintSupport REQUIRED )
find_package( Qt5WebSockets REQUIRED )
find_package( Qt5Qml REQUIRED )
find_package( Qt5X11Extras REQUIRED )
find_package( X11 REQUIRED )
find_program(CCACHE_FOUND ccache)

if(CCACHE_FOUND)
    message(STATUS "ccache found")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif(CCACHE_FOUND)

qt5_wrap_ui(dialogs/ui_settingsdialog.h settingsdialog.ui)
qt5_wrap_ui(dialogs/ui_aboutdialog.h aboutdialog.ui)
qt5_wrap_ui(dialogs/ui_localtrashdialog.h localtrashdialog.ui)
qt5_wrap_ui(dialogs/ui_linkdialog.h linkdialog.ui)
qt5_wrap_ui(dialogs/ui_welcomedialog.h welcomedialog.ui)
qt5_wrap_ui(dialogs/ui_tagadddialog.h tagadddialog.ui)
qt5_wrap_ui(widgets/ui_fontcolorwidget.h fontcolorwidget.ui)
qt5_wrap_ui(widgets/ui_layoutwidget.ui ui_layoutwidget.ui)
qt5_wrap_ui(dialogs/ui_orphanedimagesdialog.h orphanedimagesdialog.ui)
qt5_wrap_ui(dialogs/ui_orphanedattachmentsdialog.h orphanedattachmentsdialog.ui)
qt5_wrap_ui(dialogs/ui_tabledialog.h tabledialog.ui)
qt5_wrap_ui(dialogs/ui_notedialog.h notedialog.ui)
qt5_wrap_ui(dialogs/ui_websockettokendialog.h websockettokendialog.ui)
qt5_wrap_ui(dialogs/ui_imagedialog.h imagedialog.ui)
qt5_wrap_ui(libraries/qmarkdowntextedit/ui_qplaintexteditsearchwidget.h
        libraries/qmarkdowntextedit/qplaintexteditsearchwidget.ui)
qt5_wrap_ui(libraries/qttoolbareditor/src/toolbar_editor.hpp
        libraries/qttoolbareditor/src/toolbar_editor.ui)

set(RESOURCE_FILES
        breeze-pkbsuite.qrc
        breeze-dark-pkbsuite.qrc
        pkbsuite.qrc
        demonotes.qrc
        images.qrc
        texts.qrc
        configurations.qrc
        libraries/qmarkdowntextedit/media.qrc
        libraries/qdarkstyle/style.qrc
        libraries/qkeysequencewidget/qkeysequencewidget/qkeysequencewidget.qrc
        )

qt5_add_resources(RESOURCE_ADDED ${RESOURCE_FILES})

set(SOURCE_FILES
    dialogs/welcomedialog.cpp
    dialogs/welcomedialog.h
    dialogs/welcomedialog.ui
    dialogs/aboutdialog.cpp
    dialogs/aboutdialog.h
    dialogs/aboutdialog.ui
    dialogs/linkdialog.cpp
    dialogs/linkdialog.h
    dialogs/linkdialog.ui
    dialogs/notediffdialog.cpp
    dialogs/notediffdialog.h
    dialogs/notediffdialog.ui
    dialogs/settingsdialog.cpp
    dialogs/settingsdialog.h
    dialogs/settingsdialog.ui
    dialogs/tododialog.cpp
    dialogs/tododialog.h
    dialogs/tododialog.ui
    dialogs/localtrashdialog.cpp
    dialogs/localtrashdialog.h
    dialogs/localtrashdialog.ui
    dialogs/masterdialog.cpp
    dialogs/masterdialog.h
    dialogs/tagadddialog.cpp
    dialogs/tagadddialog.h
    dialogs/tagadddialog.ui
    dialogs/storedimagesdialog.cpp
    dialogs/storedimagesdialog.h
    dialogs/storedimagesdialog.ui
    dialogs/storedattachmentsdialog.cpp
    dialogs/storedattachmentsdialog.h
    dialogs/storedattachmentsdialog.ui
    dialogs/scriptrepositorydialog.cpp
    dialogs/scriptrepositorydialog.h
    dialogs/scriptrepositorydialog.ui
    dialogs/dictionarymanagerdialog.cpp
    dialogs/dictionarymanagerdialog.h
    dialogs/dictionarymanagerdialog.ui
    dialogs/tabledialog.cpp
    dialogs/tabledialog.h
    dialogs/tabledialog.ui
    dialogs/notedialog.cpp
    dialogs/notedialog.h
    dialogs/notedialog.ui
    dialogs/websockettokendialog.cpp
    dialogs/websockettokendialog.h
    dialogs/websockettokendialog.ui
    dialogs/imagedialog.cpp
    dialogs/imagedialog.h
    dialogs/imagedialog.ui
    dialogs/commandbar.cpp
    dialogs/commandbar.h
    dialogs/attachmentdialog.cpp
    dialogs/attachmentdialog.h
    dialogs/attachmentdialog.ui
    dialogs/filedialog.cpp
    dialogs/filedialog.h
    models/commandmodel.cpp
    models/commandmodel.h
    entities/note.cpp
    entities/note.h
    entities/trashitem.cpp
    entities/trashitem.h
    entities/notesubfolder.cpp
    entities/notesubfolder.h
    entities/notehistory.cpp
    entities/notehistory.h
    entities/notefolder.cpp
    entities/notefolder.h
    entities/tag.cpp
    entities/tag.h
    entities/script.cpp
    entities/script.h
    helpers/flowlayout.cpp
    helpers/flowlayout.h
    helpers/htmlentities.cpp
    helpers/htmlentities.h
    helpers/toolbarcontainer.cpp
    helpers/toolbarcontainer.h
    helpers/pkbsuitemarkdownhighlighter.cpp
    helpers/pkbsuitemarkdownhighlighter.h
    helpers/LanguageCache.h
    helpers/qownspellchecker.cpp
    helpers/qownspellchecker.h
    helpers/codetohtmlconverter.cpp
    helpers/codetohtmlconverter.h
    api/noteapi.cpp
    api/noteapi.h
    api/tagapi.cpp
    api/tagapi.h
    api/scriptapi.cpp
    api/scriptapi.h
    libraries/diff_match_patch/diff_match_patch.cpp
    libraries/diff_match_patch/diff_match_patch.h
    libraries/md4c/md4c/md4c.c
    libraries/md4c/md2html/render_html.c
    libraries/md4c/md2html/entity.c
    libraries/qmarkdowntextedit/linenumberarea.h
    libraries/qmarkdowntextedit/qownlanguagedata.cpp
    libraries/qmarkdowntextedit/qownlanguagedata.h
    libraries/qmarkdowntextedit/markdownhighlighter.cpp
    libraries/qmarkdowntextedit/markdownhighlighter.h
    libraries/qmarkdowntextedit/qmarkdowntextedit.cpp
    libraries/qmarkdowntextedit/qmarkdowntextedit.h
    libraries/qmarkdowntextedit/qplaintexteditsearchwidget.cpp
    libraries/qmarkdowntextedit/qplaintexteditsearchwidget.h
    libraries/qmarkdowntextedit/qplaintexteditsearchwidget.ui
    libraries/versionnumber/versionnumber.cpp
    libraries/versionnumber/versionnumber.h
    libraries/singleapplication/singleapplication.h
    libraries/singleapplication/singleapplication.cpp
    libraries/singleapplication/singleapplication_p.h
    libraries/singleapplication/singleapplication_p.cpp
    libraries/qttoolbareditor/src/toolbar_editor.cpp
    libraries/qttoolbareditor/src/toolbar_editor.hpp
    libraries/qttoolbareditor/src/toolbar_editor.ui
    libraries/qtcsv/src/include/qtcsv_global.h
    libraries/qtcsv/src/include/abstractdata.h
    libraries/qtcsv/src/include/reader.h
    libraries/qtcsv/src/sources/reader.cpp
    libraries/qtcsv/src/sources/filechecker.h
    libraries/qtcsv/src/sources/symbols.h
    services/databaseservice.cpp
    services/databaseservice.h
    services/scriptingservice.cpp
    services/scriptingservice.h
    libraries/qkeysequencewidget/qkeysequencewidget/src/qkeysequencewidget_p.h
    libraries/qkeysequencewidget/qkeysequencewidget/src/qkeysequencewidget.h
    libraries/qkeysequencewidget/qkeysequencewidget/src/qkeysequencewidget.cpp
    libraries/sonnet/src/core/backgroundchecker.cpp
    libraries/sonnet/src/core/backgroundchecker.h
    libraries/sonnet/src/core/backgroundchecker_p.h
    libraries/sonnet/src/core/client.cpp
    libraries/sonnet/src/core/client_p.h
    libraries/sonnet/src/core/core_debug.cpp
    libraries/sonnet/src/core/core_debug.h
    libraries/sonnet/src/core/guesslanguage.cpp
    libraries/sonnet/src/core/guesslanguage.h
    libraries/sonnet/src/core/languagefilter.cpp
    libraries/sonnet/src/core/languagefilter_p.h
    libraries/sonnet/src/core/loader.cpp
    libraries/sonnet/src/core/loader_p.h
    libraries/sonnet/src/core/settings.cpp
    libraries/sonnet/src/core/settings_p.h
    libraries/sonnet/src/core/sonnetcore_export.h
    libraries/sonnet/src/core/speller.cpp
    libraries/sonnet/src/core/speller.h
    libraries/sonnet/src/core/spellerplugin.cpp
    libraries/sonnet/src/core/spellerplugin_p.h
    libraries/sonnet/src/core/textbreaks.cpp
    libraries/sonnet/src/core/textbreaks_p.h
    libraries/sonnet/src/core/tokenizer.cpp
    libraries/sonnet/src/core/tokenizer_p.h
    libraries/fuzzy/kfuzzymatcher.cpp
    widgets/pkbsuitemarkdowntextedit.cpp
    widgets/pkbsuitemarkdowntextedit.h
    widgets/navigationwidget.cpp
    widgets/navigationwidget.h
    widgets/notepreviewwidget.cpp
    widgets/notepreviewwidget.h
    widgets/combobox.cpp
    widgets/combobox.h
    widgets/graphicsview.cpp
    widgets/graphicsview.h
    widgets/scriptlistwidget.cpp
    widgets/scriptlistwidget.h
    widgets/notefolderlistwidget.cpp
    widgets/notefolderlistwidget.h
    widgets/fontcolorwidget.ui
    widgets/fontcolorwidget.cpp
    widgets/fontcolorwidget.h
    widgets/label.cpp
    widgets/label.h
    widgets/qrcodewidget.cpp
    widgets/qrcodewidget.h
    widgets/lineedit.cpp
    widgets/lineedit.h
    widgets/scriptsettingwidget.cpp
    widgets/scriptsettingwidget.h
    widgets/scriptsettingwidget.ui
    widgets/qtexteditsearchwidget.cpp
    widgets/qtexteditsearchwidget.h
    widgets/qtexteditsearchwidget.ui
    widgets/notetreewidgetitem.cpp
    widgets/notetreewidgetitem.h
    widgets/notetreewidgetitem.ui
    widgets/layoutwidget.cpp
    widgets/layoutwidget.h
    widgets/layoutwidget.ui
    widgets/todoitemtreewidget.cpp
    widgets/todoitemtreewidget.h
    utils/misc.cpp
    utils/misc.h
    utils/gui.cpp
    utils/gui.h
    utils/schema.cpp
    utils/schema.h
    build_number.h
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    release.h
    version.h
    services/websocketserverservice.cpp
    services/websocketserverservice.h
    services/webappclientservice.cpp
    services/webappclientservice.h
    entities/bookmark.h entities/bookmark.cpp)

if(NOT APPLE)
list(APPEND SOURCE_FILES
    libraries/sonnet/src/plugins/hunspell/hunspellclient.cpp
    libraries/sonnet/src/plugins/hunspell/hunspellclient.h
    libraries/sonnet/src/plugins/hunspell/hunspelldebug.cpp
    libraries/sonnet/src/plugins/hunspell/hunspelldebug.h
    libraries/sonnet/src/plugins/hunspell/hunspelldict.cpp
    libraries/sonnet/src/plugins/hunspell/hunspelldict.h
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/affentry.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/affentry.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/affixmgr.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/affixmgr.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/atypes.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/baseaffix.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/csutil.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/csutil.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/filemgr.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/filemgr.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hashmgr.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hashmgr.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/htypes.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hunspell.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hunspell.h
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hunspell.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hunvisapi.h
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hunvisapi.h.in
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hunzip.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/hunzip.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/langnum.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/phonet.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/phonet.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/replist.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/replist.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/suggestmgr.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/suggestmgr.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/utf_info.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/hunspell/w_char.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/firstparser.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/firstparser.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/htmlparser.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/htmlparser.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/latexparser.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/latexparser.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/manparser.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/manparser.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/odfparser.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/odfparser.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/textparser.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/textparser.hxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/xmlparser.cxx
    libraries/sonnet/src/plugins/hunspell/hunspell/src/parsers/xmlparser.hxx
)
endif()

if (APPLE)
   list(APPEND SOURCE_FILES
    libraries/sonnet/src/plugins/nsspellchecker/nsspellcheckerclient.h
    libraries/sonnet/src/plugins/nsspellchecker/nsspellcheckerclient.mm
    libraries/sonnet/src/plugins/nsspellchecker/nsspellcheckerdebug.cpp
    libraries/sonnet/src/plugins/nsspellchecker/nsspellcheckerdebug.h
    libraries/sonnet/src/plugins/nsspellchecker/nsspellcheckerdict.h
    libraries/sonnet/src/plugins/nsspellchecker/nsspellcheckerdict.mm)
endif()

# Translation files
SET(QON_TS_FILES
    languages/PKbSuite_en.ts
    languages/PKbSuite_fr.ts
)

qt5_add_translation(QON_QM_FILES ${QON_TS_FILES})
add_custom_target(translations DEPENDS ${QON_QM_FILES})

if(NOT QT_TRANSLATIONS_DIR)
    # If this directory is missing, we are in a Qt5 environment.
    # Extract the qmake executable location
    get_target_property(QT5_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
    # Ask Qt5 where to put the translations
    execute_process( COMMAND ${QT5_QMAKE_EXECUTABLE} -query QT_INSTALL_TRANSLATIONS
            OUTPUT_VARIABLE qt_translations_dir OUTPUT_STRIP_TRAILING_WHITESPACE )
    # make sure we have / and not \ as qmake gives on windows
    file( TO_CMAKE_PATH "${qt_translations_dir}" qt_translations_dir)
    set( QT_TRANSLATIONS_DIR ${qt_translations_dir} CACHE PATH
            "The location of the Qt translations" FORCE)
endif()

install(FILES ${QON_QM_FILES}
        DESTINATION ${QT_TRANSLATIONS_DIR})

add_executable(PKbSuite ${SOURCE_FILES} ${RESOURCE_ADDED} ${QON_QM_FILES})


# The Qt5Widgets_INCLUDES also includes the include directories for
# dependencies QtCore and QtGui
include_directories(${Qt5Widgets_INCLUDES} ${Qt5Sql_INCLUDES}
        ${Qt5Svg_INCLUDES} ${Qt5Network_INCLUDES}
        ${Qt5Xml_INCLUDES} ${Qt5XmlPatterns_INCLUDES}
        ${Qt5PrintSupport_INCLUDES} ${Qt5WebSockets_INCLUDES} ${Qt5Qml_INCLUDES}
        ${Qt5X11Extras_INCLUDES})

# We need add -DQT_WIDGETS_LIB when using QtWidgets in Qt 5.
add_definitions(${Qt5Widgets_DEFINITIONS} ${Qt5Sql_DEFINITIONS}
        ${Qt5Svg_DEFINITIONS} ${Qt5Network_DEFINITIONS}
        ${Qt5Xml_DEFINITIONS} ${Qt5XmlPatterns_DEFINITIONS}
        ${Qt5PrintSupport_DEFINITIONS} ${Qt5WebSockets_DEFINITIONS}
        ${Qt5Qml_DEFINITIONS} ${Qt5X11Extras_DEFINITIONS})

# Executables fail to build with Qt 5 in the default configuration
# without -fPIE. We add that here.
set(CMAKE_CXX_FLAGS "${Qt5Widgets_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5Sql_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5Svg_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5Network_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5Xml_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5XmlPatterns_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5PrintSupport_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5WebSockets_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5Qml_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${Qt5X11Extras_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "-std=c++0x")

# The Qt5Widgets_LIBRARIES variable also includes QtGui and QtCore
target_link_libraries(PKbSuite ${Qt5Widgets_LIBRARIES} ${Qt5Sql_LIBRARIES}
        ${Qt5Svg_LIBRARIES} ${Qt5Network_LIBRARIES}
        ${Qt5Xml_LIBRARIES} ${Qt5XmlPatterns_LIBRARIES}
        ${Qt5PrintSupport_LIBRARIES} ${Qt5WebSockets_LIBRARIES}
        ${Qt5Qml_LIBRARIES} ${Qt5X11Extras_LIBRARIES} ${X11_LIBRARIES})

#qt5_use_modules(PKbSuite Widgets Core Gui Sql Svg Network Script Xml PrintSupport)

# Sonnet support if we can get it to run in the future
#find_package(KF5Sonnet)
#target_link_libraries(KF5::SonnetCore KF5::SonnetUi)

add_compile_definitions(QAPPLICATION_CLASS=QApplication)
