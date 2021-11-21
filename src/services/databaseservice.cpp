#include "services/databaseservice.h"

#include <entities/notefolder.h>
#include <entities/tag.h>
#include <utils/misc.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QMessageBox>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include "mainwindow.h"

DatabaseService::DatabaseService() = default;

/**
 * Returns the path to the database (on disk)
 *
 * @return
 */
QString DatabaseService::getDiskDatabasePath() {
    QString databaseFileName = Utils::Misc::appDataPath() +
                               Utils::Misc::dirSeparator() +
                               QStringLiteral("PKbSuite.sqlite");
    qDebug() << __func__ << " - 'databaseFileName': " << databaseFileName;

    return databaseFileName;
}

/**
 * @brief Returns the path to the note folder database
 * @return string
 */
QString DatabaseService::getNoteFolderDatabasePath() {
    QString pathNotes = NoteFolder::currentLocalPath();
    QString nameDatabase = pathNotes.remove(0, pathNotes.lastIndexOf(Utils::Misc::dirSeparator())) + QStringLiteral(".sqlite");
    return Utils::Misc::appDataPath() + nameDatabase;
}

bool DatabaseService::removeDiskDatabase() {
    QFile file(getDiskDatabasePath());

    if (file.exists()) {
        // the database file will not get deleted under Windows if the
        // database isn't closed
        QSqlDatabase dbDisk = QSqlDatabase::database(QStringLiteral("disk"));
        dbDisk.close();

        // remove the file
        bool result = file.remove();

        QString text = result ? QStringLiteral("Removed")
                              : QStringLiteral("Could not remove");
        qWarning() << text + " database file: " << file.fileName();
        return result;
    }

    return false;
}

bool DatabaseService::createConnection() {
    return createMemoryConnection() && createDiskConnection();
}

bool DatabaseService::reinitializeDiskDatabase() {
    return removeDiskDatabase() && createDiskConnection() && setupTables();
}

bool DatabaseService::checkDiskDatabaseIntegrity() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("disk"));
    QSqlQuery query(db);

    if (!query.exec(QStringLiteral("PRAGMA integrity_check"))) {
        qWarning() << __func__ << ": " << query.lastError();

        return false;
    } else if (query.first()) {
        const auto result = query.value(0).toString();

        if (result == QStringLiteral("ok")) {
            return true;
        }

        qWarning() << __func__ << ": " << result;

        return false;
    }

    return false;
}

bool DatabaseService::createMemoryConnection() {
    QSqlDatabase dbMemory = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                      QStringLiteral("memory"));
    dbMemory.setDatabaseName(QStringLiteral(":memory:"));

    if (!dbMemory.open()) {
        QMessageBox::critical(
            nullptr, QWidget::tr("Cannot open memory database"),
            QWidget::tr("Unable to establish a memory database connection."),
            QMessageBox::Ok);
        return false;
    }

    return true;
}

bool DatabaseService::createDiskConnection() {
    QSqlDatabase dbDisk = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("disk"));
    QString path = getDiskDatabasePath();
    dbDisk.setDatabaseName(path);

    if (!dbDisk.open()) {
        QMessageBox::critical(
            nullptr, QWidget::tr("Cannot open disk database"),
            QWidget::tr("Unable to establish a database connection with "
                        "file '%1'.\nAre the folder and the file "
                        "writeable?")
                .arg(path),
            QMessageBox::Ok);
        return false;
    }

    return true;
}

bool DatabaseService::createNoteFolderConnection() {
    QSqlDatabase dbDisk =
        QSqlDatabase::contains(QStringLiteral("note_folder"))
            ? QSqlDatabase::database(QStringLiteral("note_folder"))
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                        QStringLiteral("note_folder"));

    QString path = getNoteFolderDatabasePath();
    dbDisk.setDatabaseName(path);

    if (!dbDisk.open()) {
        QMessageBox::critical(
            nullptr, QWidget::tr("Cannot open note folder database"),
            QWidget::tr("Unable to establish a database connection with "
                        "file '%1'.\nAre the folder and the file "
                        "writeable?")
                .arg(path),
            QMessageBox::Ok);
        return false;
    }

    return true;
}

/**
 * Creates or updates the note folder tables
 */
bool DatabaseService::setupNoteFolderTables() {
    QSqlDatabase dbDisk = getNoteFolderDatabase();
    QSqlQuery queryDisk(dbDisk);

    queryDisk.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS appData ("
                       "name VARCHAR(255) PRIMARY KEY, "
                       "value VARCHAR(255))"));
    int version = getAppData(QStringLiteral("database_version"),
                             QStringLiteral("note_folder"))
                      .toInt();
    int oldVersion = version;
    qDebug() << __func__ << " - 'database version': " << version;

    if (version < 1) {
        queryDisk.exec(
            QStringLiteral("CREATE TABLE IF NOT EXISTS tag ("
                           "id INTEGER PRIMARY KEY,"
                           "name VARCHAR(255),"
                           "priority INTEGER DEFAULT 0,"
                           "created DATETIME DEFAULT current_timestamp)"));

        queryDisk.exec(
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idxUniqueTag ON "
                           "tag (name)"));

        queryDisk.exec(
            QStringLiteral("CREATE TABLE IF NOT EXISTS noteTagLink ("
                           "id INTEGER PRIMARY KEY,"
                           "tag_id INTEGER,"
                           "note_file_name VARCHAR(255),"
                           "created DATETIME DEFAULT current_timestamp)"));

        queryDisk.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idxUniqueTagNoteLink"
            " ON noteTagLink (tag_id, note_file_name)"));

        version = 1;
    }

    if (version < 2) {
        queryDisk.exec(
            QStringLiteral("ALTER TABLE tag ADD parent_id INTEGER DEFAULT 0"));
        queryDisk.exec(
            QStringLiteral("CREATE INDEX IF NOT EXISTS idxTagParent "
                           "ON tag( parent_id )"));
        version = 2;
    }

    if (version < 3) {
        queryDisk.exec(QStringLiteral("DROP INDEX IF EXISTS idxUniqueTag"));
        queryDisk.exec(
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idxUniqueTag ON "
                           "tag (name, parent_id)"));
        version = 3;
    }

    if (version < 4) {
        queryDisk.exec(QStringLiteral(
            "ALTER TABLE noteTagLink ADD note_sub_folder_path TEXT"));
        version = 4;
    }

    if (version < 5) {
        queryDisk.exec(
            QStringLiteral("DROP INDEX IF EXISTS idxUniqueTagNoteLink"));
        queryDisk.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idxUniqueTagNoteLink "
            "ON noteTagLink (tag_id, note_file_name, "
            "note_sub_folder_path)"));
        version = 5;
    }

    if (version < 6) {
        // we need to add a `DEFAULT ''` to column note_sub_folder_path
        queryDisk.exec(
            QStringLiteral("ALTER TABLE noteTagLink RENAME TO _noteTagLink"));
        queryDisk.exec(
            QStringLiteral("CREATE TABLE IF NOT EXISTS noteTagLink ("
                           "id INTEGER PRIMARY KEY,"
                           "tag_id INTEGER,"
                           "note_file_name VARCHAR(255) DEFAULT '',"
                           "note_sub_folder_path TEXT DEFAULT '',"
                           "created DATETIME DEFAULT current_timestamp)"));
        queryDisk.exec(
            QStringLiteral("INSERT INTO noteTagLink (tag_id, note_file_name, "
                           "note_sub_folder_path, created) "
                           "SELECT tag_id, note_file_name, "
                           "note_sub_folder_path, created "
                           "FROM _noteTagLink ORDER BY id"));
        queryDisk.exec(
            QStringLiteral("DROP INDEX IF EXISTS idxUniqueTagNoteLink"));
        queryDisk.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idxUniqueTagNoteLink "
            "ON noteTagLink (tag_id, note_file_name, "
            "note_sub_folder_path)"));
        queryDisk.exec(QStringLiteral("DROP TABLE _noteTagLink"));
        queryDisk.exec(
            QStringLiteral("UPDATE noteTagLink SET note_sub_folder_path = '' "
                           "WHERE note_sub_folder_path IS NULL"));
        version = 6;
    }

    if (version < 7) {
        // convert backslashes to slashes in the noteTagLink table to fix
        // problems with Windows
        Tag::convertDirSeparator();
        version = 7;
    }

    if (version < 8) {
        queryDisk.exec(QStringLiteral("ALTER TABLE tag ADD color VARCHAR(20)"));
        version = 8;
    }

    if (version < 9) {
        queryDisk.exec(
            QStringLiteral("ALTER TABLE tag ADD dark_color VARCHAR(20)"));
        version = 9;
    }

    if (version < 10) {
        // set the non-darkMode colors as darkMode colors for all tags
        Tag::migrateDarkColors();
        version = 10;
    }

    if (version < 11) {
        // create a case insensitive index
        queryDisk.exec(QStringLiteral("DROP INDEX IF EXISTS idxUniqueTag"));
        queryDisk.exec(
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idxUniqueTag ON "
                           "tag (name COLLATE NOCASE, parent_id)"));
        version = 11;
    }

    if (version < 12) {
        // create new tag table, because
        //     ALTER TABLE tag ADD updated DEFAULT CURRENT_TIMESTAMP
        // is not supported by sqlite -- you can't add a column with
        // a non-constant default value. And if collate ... is used
        // on a column, it's also defaulted to indices on that column.
        queryDisk.exec(QStringLiteral("ALTER TABLE tag RENAME TO _tag"));
        queryDisk.exec(
            QStringLiteral("CREATE TABLE IF NOT EXISTS tag ("
                           "id INTEGER PRIMARY KEY,"
                           "name VARCHAR(255) COLLATE NOCASE,"
                           "priority INTEGER DEFAULT 0,"
                           "created DATETIME DEFAULT current_timestamp,"
                           "parent_id INTEGER DEFAULT 0,"
                           "color VARCHAR(20),"
                           "dark_color VARCHAR(20),"
                           "updated DATETIME DEFAULT current_timestamp)"));

        // recreate the indices
        queryDisk.exec(QStringLiteral("DROP INDEX IF EXISTS idxUniqueTag"));
        queryDisk.exec(
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idxUniqueTag ON "
                           "tag (name, parent_id)"));
        queryDisk.exec(QStringLiteral("DROP INDEX IF EXISTS idxTagParent"));
        queryDisk.exec(
            QStringLiteral("CREATE INDEX IF NOT EXISTS idxTagParent "
                           "ON tag( parent_id )"));

        // convert old values to new table
        queryDisk.exec(
            QStringLiteral("INSERT INTO tag ( "
                           "id, name, priority, created, parent_id, "
                           "color, dark_color, updated "
                           ") SELECT "
                           "id, name, priority, created, parent_id, "
                           "color, dark_color, created "
                           "FROM _tag ORDER BY id"));

        queryDisk.exec(QStringLiteral("DROP TABLE _tag"));
        version = 12;
    }

    if (version < 13) {
        queryDisk.exec(
            QStringLiteral("CREATE TABLE IF NOT EXISTS trashItem ("
                           "id INTEGER PRIMARY KEY,"
                           "file_name VARCHAR(255),"
                           "file_size INTEGER,"
                           "note_sub_folder_path_data TEXT,"
                           "created DATETIME DEFAULT current_timestamp)"));

        version = 13;
    }

    if (version < 14) {
        // removing broken tag assignments from
        // https://github.com/pbek/PKbSuite/issues/1510
        queryDisk.exec(QStringLiteral(
            "DELETE FROM noteTagLink WHERE note_sub_folder_path IS NULL"));

        version = 14;
    }

    if (version < 15) {
        // https://github.com/pbek/QOwnNotes/issues/2292
        queryDisk.exec(QStringLiteral(
            "ALTER TABLE noteTagLink ADD stale_date DATETIME DEFAULT NULL"));

        version = 15;
    }

    if (version != oldVersion) {
        setAppData(QStringLiteral("database_version"), QString::number(version),
                   QStringLiteral("note_folder"));
    }

    closeDatabaseConnection(dbDisk, queryDisk);

    return true;
}

QSqlDatabase DatabaseService::getNoteFolderDatabase() {
#ifdef Q_OS_WIN32
    if (Utils::Misc::doAutomaticNoteFolderDatabaseClosing()) {
        // open database if it was closed in closeDatabaseConnection
        createNoteFolderConnection();
    }
#endif

    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("note_folder"));
    //    db.transaction();
    return db;
}

/**
 * Closes a database connection if it was open
 *
 * @param db
 */
void DatabaseService::closeDatabaseConnection(QSqlDatabase& db,
                                              QSqlQuery& query) {
    query.finish();
    query.clear();

//    db.commit();
#ifdef Q_OS_WIN32
    if (Utils::Misc::doAutomaticNoteFolderDatabaseClosing()) {
        if (db.isOpen()) {
            db.close();
        }
    }
#else
    Q_UNUSED(db)
#endif
}

bool DatabaseService::setupTables() {
    QSqlDatabase dbDisk = QSqlDatabase::database(QStringLiteral("disk"));
    QSqlQuery queryDisk(dbDisk);
    QSettings settings;

    queryDisk.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS appData ("
                       "name VARCHAR(255) PRIMARY KEY, "
                       "value VARCHAR(255))"));
    int version = getAppData(QStringLiteral("database_version")).toInt();
    int oldVersion = version;
    qDebug() << __func__ << " - 'database_version': " << version;

    QSqlDatabase dbMemory = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery queryMemory(dbMemory);
    queryMemory.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS note ("
                       "id INTEGER PRIMARY KEY,"
                       "name VARCHAR(255) COLLATE NOCASE,"
                       "file_name VARCHAR(255) COLLATE NOCASE,"
                       "file_size INT64 DEFAULT 0,"
                       "note_sub_folder_id int,"
                       "note_text TEXT,"
                       "has_dirty_data INTEGER DEFAULT 0,"
                       "file_last_modified DATETIME,"
                       "file_created DATETIME,"
                       "created DATETIME default current_timestamp,"
                       "modified DATETIME default current_timestamp)"));
    queryMemory.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS noteSubFolder ("
                       "id INTEGER PRIMARY KEY,"
                       "name VARCHAR(255),"
                       "parent_id int,"
                       "file_last_modified DATETIME,"
                       "created DATETIME default current_timestamp,"
                       "modified DATETIME default current_timestamp)"));

    if (version < 3) {
        queryDisk.exec(
            QStringLiteral("CREATE TABLE IF NOT EXISTS noteFolder ("
                           "id INTEGER PRIMARY KEY,"
                           "name VARCHAR(255),"
                           "local_path VARCHAR(255),"
                           "remote_path VARCHAR(255),"
                           "priority INTEGER DEFAULT 0 )"));
        version = 3;
    }

    // we need to remove the main splitter sizes for version 4 and 5
    if (version < 5) {
        // remove the main splitter sizes for the tags pane
        settings.remove(QStringLiteral("mainSplitterSizes"));

        version = 5;
    }

    if (version < 6) {
        // remove the obsolete activeTagId setting
        settings.remove(QStringLiteral("activeTagId"));

        version = 6;
    }

    if (version < 7) {
        queryDisk.exec(
            QStringLiteral("ALTER TABLE noteFolder ADD active_tag_id INTEGER"));
        version = 7;
    }

    if (version < 8) {
        queryDisk.exec(
            QStringLiteral("CREATE TABLE IF NOT EXISTS script ("
                           "id INTEGER PRIMARY KEY,"
                           "name VARCHAR(255),"
                           "script_path TEXT,"
                           "enabled BOOLEAN DEFAULT 1,"
                           "priority INTEGER DEFAULT 0 )"));
        version = 8;
    }

    if (version < 9) {
        queryDisk.exec(
            QStringLiteral("ALTER TABLE noteFolder ADD show_subfolders BOOLEAN "
                           "DEFAULT 0"));
        version = 9;
    }

    if (version < 10) {
        queryDisk.exec(
            QStringLiteral("ALTER TABLE noteFolder "
                           "ADD active_note_sub_folder_data TEXT"));
        version = 10;
    }

    if (version < 11) {
        // remove the oneColumnModeEnabled setting, that wrongly
        // was turned on by default
        settings.remove(QStringLiteral("oneColumnModeEnabled"));

        version = 11;
    }

    if (version < 12) {
        bool darkModeColors =
            settings.value(QStringLiteral("darkModeColors")).toBool();

        // set an initial schema key
        QString schemaKey =
            darkModeColors
                ? QStringLiteral(
                      "EditorColorSchema-cdbf28fc-1ddc-4d13-bb21-6a4043316a2f")
                : QStringLiteral(
                      "EditorColorSchema-6033d61b-cb96-46d5-a3a8-20d5172017eb");
        settings.setValue(QStringLiteral("Editor/CurrentSchemaKey"), schemaKey);

        version = 12;
    }

    if (version < 15) {
        // turn off the DFM initially after the dock widget update
        settings.remove(QStringLiteral("DistractionFreeMode/isEnabled"));

        // remove some deprecated settings
        settings.remove(QStringLiteral("MainWindow/windowState"));
        settings.remove(QStringLiteral("windowState"));
        settings.remove(QStringLiteral("markdownViewEnabled"));
        settings.remove(QStringLiteral("tagsEnabled"));
        settings.remove(QStringLiteral("noteEditPaneEnabled"));
        settings.remove(QStringLiteral("dockWindowState"));
        settings.remove(QStringLiteral("verticalPreviewModeEnabled"));
        settings.remove(QStringLiteral("mainSplitterSizes"));
        settings.remove(
            QStringLiteral("DistractionFreeMode/mainSplitterSizes"));
        settings.remove(QStringLiteral("mainSplitterState-0-0-0-0"));
        settings.remove(QStringLiteral("mainSplitterState-0-0-0-1"));
        settings.remove(QStringLiteral("mainSplitterState-0-0-1-0"));
        settings.remove(QStringLiteral("mainSplitterState-0-0-1-1"));
        settings.remove(QStringLiteral("mainSplitterState-0-1-0-0"));
        settings.remove(QStringLiteral("mainSplitterState-0-1-0-1"));
        settings.remove(QStringLiteral("mainSplitterState-0-1-1-0"));
        settings.remove(QStringLiteral("mainSplitterState-0-1-1-1"));
        settings.remove(QStringLiteral("mainSplitterState-1-0-0-0"));
        settings.remove(QStringLiteral("mainSplitterState-1-0-0-1"));
        settings.remove(QStringLiteral("mainSplitterState-1-0-1-0"));
        settings.remove(QStringLiteral("mainSplitterState-1-0-1-1"));
        settings.remove(QStringLiteral("mainSplitterState-1-1-0-0"));
        settings.remove(QStringLiteral("mainSplitterState-1-1-0-1"));
        settings.remove(QStringLiteral("mainSplitterState-1-1-1-0"));
        settings.remove(QStringLiteral("mainSplitterState-1-1-1-1"));
        settings.remove(QStringLiteral("noteListSplitterState"));
        settings.remove(QStringLiteral("tagFrameSplitterState"));
        settings.remove(QStringLiteral("verticalNoteFrameSplitterState"));

        version = 15;
    }

    if (version < 16) {
        // remove some deprecated settings
        settings.remove(QStringLiteral("dockWindowGeometry"));
        settings.remove(
            QStringLiteral("MainWindow/showRecentNoteFolderInMainArea"));

        version = 16;
    }

    if (version < 17) {
        // remove some deprecated settings
        settings.beginGroup(QStringLiteral("LogDialog"));
        settings.remove(QString());
        settings.endGroup();

        settings.remove(QStringLiteral("LogWidget/geometry"));
        settings.remove(QStringLiteral("LogWidget/showAtStartup"));

        version = 17;
    }

    if (version < 18) {
        // set a new markdownHighlightingEnabled setting
        settings.setValue(
            QStringLiteral("markdownHighlightingEnabled"),
            settings.value(QStringLiteral("markdownHighlightingInterval"), 200)
                    .toInt() > 0);

        // remove the deprecated markdownHighlightingInterval setting
        settings.remove(QStringLiteral("markdownHighlightingInterval"));

        version = 18;
    }

    if (version < 20) {
#ifdef Q_OS_MAC
        // disable restoreCursorPosition for macOS by default, because there
        // are users where it causes troubles
        settings.setValue(QStringLiteral("restoreCursorPosition"), false);
#endif

        version = 20;
    }

    if (version < 21) {
        // migrate to the new Portuguese translation
        QString locale =
            settings.value(QStringLiteral("interfaceLanguage")).toString();
        if (locale == QStringLiteral("pt")) {
            settings.setValue(QStringLiteral("interfaceLanguage"),
                              QStringLiteral("pt_BR"));
        }

        version = 21;
    }

    if (version < 23) {
        queryDisk.exec(
            QStringLiteral("ALTER TABLE script ADD identifier VARCHAR(255)"));
        queryDisk.exec(QStringLiteral("ALTER TABLE script ADD info_json TEXT"));
        version = 23;
    }

    if (version < 24) {
        queryDisk.exec(QStringLiteral(
            "ALTER TABLE script ADD settings_variables_json TEXT"));
        version = 24;
    }

    if (version < 25) {
        // migrate old sort and order settings + set defaults if unset
        // if settings.s;
        if (settings.contains(QStringLiteral("SortingModeAlphabetically"))) {
            bool sort =
                settings.value(QStringLiteral("SortingModeAlphabetically"))
                    .toBool();    // read old setting
            settings.setValue(QStringLiteral("notesPanelSort"),
                              sort ? SORT_ALPHABETICAL : SORT_BY_LAST_CHANGE);
            settings.remove(QStringLiteral("SortingModeAlphabetically"));
        }

        if (settings.contains(QStringLiteral("NoteSortOrder"))) {
            int order = static_cast<Qt::SortOrder>(
                settings.value(QStringLiteral("NoteSortOrder")).toInt());
            settings.setValue(QStringLiteral("notesPanelOrder"),
                              order);    // see defines in MainWindow.h
            settings.remove(QStringLiteral("NoteSortOrder"));
        }

        // set defaults for now settings if not set already
        if (!settings.contains(QStringLiteral("notesPanelSort"))) {
            settings.value(QStringLiteral("notesPanelSort"),
                           SORT_BY_LAST_CHANGE);
        }
        if (!settings.contains(QStringLiteral("notesPanelOrder"))) {
            settings.value(QStringLiteral("notesPanelOrder"), ORDER_DESCENDING);
        }

        if (!settings.contains(QStringLiteral("noteSubfoldersPanelSort"))) {
            settings.value(QStringLiteral("noteSubfoldersPanelSort"),
                           SORT_BY_LAST_CHANGE);
        }
        if (!settings.contains(QStringLiteral("noteSubfoldersOrder"))) {
            settings.value(QStringLiteral("noteSubfoldersOrder"),
                           ORDER_ASCENDING);
        }

        if (!settings.contains(QStringLiteral("tagsPanelSort"))) {
            settings.value(QStringLiteral("tagsPanelSort"), SORT_ALPHABETICAL);
        }
        if (!settings.contains(QStringLiteral("tagsPanelOrder"))) {
            settings.value(QStringLiteral("tagsPanelOrder"), ORDER_ASCENDING);
        }
        version = 25;
    }

    if (version < 26) {
        // remove setting with wrong default value
        settings.remove(QStringLiteral("localTrash/autoCleanupDays"));

        version = 26;
    }

    if (version < 27) {
        // if the application was not started for the first time we want to
        // disable that the note edit is the central widget
        if (oldVersion != 0) {
            settings.setValue(QStringLiteral("noteEditIsCentralWidget"), false);
        }

        version = 27;
    }

    if (version < 28) {
#ifndef Q_OS_MAC
        // we only want one app instance on Windows and Linux by default
        settings.setValue(QStringLiteral("allowOnlyOneAppInstance"), true);
#endif

        version = 28;
    }

    if (version < 33) {
        foreach(NoteFolder noteFolder, NoteFolder::fetchAll()) {
            noteFolder.setSettingsValue(QStringLiteral("allowDifferentNoteFileName"),
                                        settings.value(QStringLiteral("allowDifferentNoteFileName")));
        }

        settings.remove(QStringLiteral("allowDifferentNoteFileName"));

        version = 33;
    }


    if (version < 35) {
        // migrate setting with typo
        settings.setValue(
            QStringLiteral("Editor/removeTrailingSpaces"),
            settings.value(QStringLiteral("Editor/removeTrainingSpaces")).toBool());
        settings.remove(QStringLiteral("Editor/removeTrainingSpaces"));

        version = 35;
    }

    if (version < 36) {
        // remove possibly corrupted printer dialog settings from
        // https://github.com/pbek/QOwnNotes/commit/ef0475692a4baf6f0b30bb200c0ee10157e7c2a6
        // so they can be generated new
        settings.remove(QStringLiteral("Printer/NotePrinting"));
        settings.remove(QStringLiteral("Printer/NotePDFExport"));

        version = 36;
    }

    if (version < 37) {
        // add "txt" and "md" to the note file extensions, so they can also be removed
        auto extensions = settings.value(
              QStringLiteral("customNoteFileExtensionList")).toStringList();
        extensions << "md" << "txt";

        settings.setValue(
            QStringLiteral("customNoteFileExtensionList"),
            extensions);

        version = 37;
    }

    if (version != oldVersion) {
        setAppData(QStringLiteral("database_version"),
                   QString::number(version));
    }

    return true;
}

bool DatabaseService::setAppData(const QString& name, const QString& value,
                                 const QString& connectionName) {
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery query(db);

    query.prepare(
        QStringLiteral("REPLACE INTO appData ( name, value ) "
                       "VALUES ( :name, :value )"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":value"), value);
    return query.exec();
}

QString DatabaseService::getAppData(const QString& name,
                                    const QString& connectionName) {
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery query(db);

    query.prepare(
        QStringLiteral("SELECT value FROM appData WHERE name = :name"));
    query.bindValue(QStringLiteral(":name"), name);

    if (!query.exec()) {
        qCritical() << __func__ << ": " << query.lastError();
    } else if (query.first()) {
        return query.value(QStringLiteral("value")).toString();
    }

    return QString();
}

/**
 * WIP
 *
 * @param path
 * @return
 */
bool DatabaseService::mergeNoteFolderDatabase(const QString& path) {
    QSqlDatabase mergeDB = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("note_folder_merge"));
    mergeDB.setDatabaseName(path);

    if (!mergeDB.open()) {
        QMessageBox::critical(
            nullptr, QWidget::tr("Cannot open database"),
            QWidget::tr("Unable to establish a database connection with "
                        "note folder database to merge '%1'.\nAre the folder "
                        "and the file writeable?")
                .arg(path),
            QMessageBox::Ok);

        return false;
    }

    const bool isTagsMerged = Tag::mergeFromDatabase(mergeDB);
    mergeDB.close();

    // We can ignore the appData table, because data there will get updated by
    // PKbSuite itself
    // We can ignore the trashItem table, because PKbSuite will manage the
    // trashed notes itself
    return isTagsMerged;
}

/**
 * Generates a SHA1 signature for the content of a database table
 *
 * @return
 */
QByteArray DatabaseService::generateDatabaseTableSha1Signature(
    QSqlDatabase& db, const QString& table) {
    QCryptographicHash hash(QCryptographicHash::Sha1);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT * FROM ") + table);

    if (!query.exec()) {
        qCritical() << __func__ << ": " << query.lastError();

        return QByteArray();
    }

    // loop through all table rows
    for (int r = 0; query.next(); r++) {
        int i = 0;
        QVariant value = query.value(i);

        // add data from all query columns
        while (value.isValid() && i < 1000) {
            hash.addData(value.toByteArray());
            value = query.value(i);
            i++;
        }
    }

    const QByteArray& result = hash.result();
    qDebug() << __func__ << " - 'hash': " << result;

    // retrieve the SHA1 signature from the hash
    return result;
}
