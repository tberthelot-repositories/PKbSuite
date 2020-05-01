#include "notefolder.h"

#include <utils/misc.h>

#include <QDebug>
#include <QDir>
#include <QJsonObject>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

#include "notesubfolder.h"

NoteFolder::NoteFolder() : name(QString()),
                           localPath(QString()) {
    id = 0;
//    name = "";
//    localPath = "";
    priority = 0;
    activeTagId = 0;
    showSubfolders = false;
}

int NoteFolder::getId() const { return this->id; }

QString NoteFolder::getLocalPath() const { return this->localPath; }

QString NoteFolder::getName() const { return this->name; }

int NoteFolder::getPriority() const { return this->priority; }

bool NoteFolder::isShowSubfolders() const { return showSubfolders; }

int NoteFolder::getActiveTagId() const { return this->activeTagId; }

NoteSubFolder NoteFolder::getActiveNoteSubFolder() const {
    return NoteSubFolder::fetchByPathData(this->activeNoteSubFolderData);
}

void NoteFolder::setName(const QString &text) { this->name = text; }

void NoteFolder::setLocalPath(const QString &text) { this->localPath = text; }

void NoteFolder::setPriority(int value) { this->priority = value; }

void NoteFolder::setShowSubfolders(bool value) { showSubfolders = value; }

void NoteFolder::setActiveTagId(int value) { this->activeTagId = value; }

void NoteFolder::setActiveNoteSubFolder(const NoteSubFolder &noteSubFolder) {
    this->activeNoteSubFolderData = noteSubFolder.pathData();
}

void NoteFolder::resetActiveNoteSubFolder() {
    this->activeNoteSubFolderData.clear();
}

bool NoteFolder::create(const QString& name, const QString& localPath) {
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("disk"));
    QSqlQuery query(db);

    query.prepare("INSERT INTO noteFolder ( name, local_path) "
                          "VALUES ( :name, :localPath )");
    query.bindValue(":name", name);
    query.bindValue(":localPath", localPath);
    return query.exec();
}

NoteFolder NoteFolder::fetch(int id) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("disk"));
    QSqlQuery query(db);

    query.prepare(QStringLiteral("SELECT * FROM noteFolder WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else if (query.first()) {
        NoteFolder noteFolder;
        noteFolder.fillFromQuery(query);
        return noteFolder;
    }

    return NoteFolder();
}

int NoteFolder::countAll() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("disk"));
    QSqlQuery query(db);

    query.prepare(QStringLiteral("SELECT COUNT(*) AS cnt FROM noteFolder"));

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else if (query.first()) {
        return query.value(QStringLiteral("cnt")).toInt();
    }

    return 0;
}

bool NoteFolder::localPathExists() const {
    const QDir folder(localPath);
    return folder.exists() && !localPath.isEmpty();
}

bool NoteFolder::remove() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("disk"));
    QSqlQuery query(db);

    query.prepare(QStringLiteral("DELETE FROM noteFolder WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), this->id);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
        return false;
    } else {
        // remove the settings of the note folder
        QSettings settings;
        settings.remove(QStringLiteral("NoteHistory-") +
                        QString::number(this->id));
        settings.remove(QStringLiteral("NoteHistoryCurrentIndex-") +
                        QString::number(this->id));
        settings.remove(QStringLiteral("NoteFolder-") +
                        QString::number(this->id));
        return true;
    }
}

NoteFolder NoteFolder::noteFolderFromQuery(const QSqlQuery &query) {
    NoteFolder noteFolder;
    noteFolder.fillFromQuery(query);
    return noteFolder;
}

bool NoteFolder::fillFromQuery(const QSqlQuery &query) {
    this->id = query.value(QStringLiteral("id")).toInt();
    this->name = query.value(QStringLiteral("name")).toString();
    this->priority = query.value(QStringLiteral("priority")).toInt();
    this->showSubfolders =
        query.value(QStringLiteral("show_subfolders")).toBool();
    this->activeTagId = query.value(QStringLiteral("active_tag_id")).toInt();
    this->activeNoteSubFolderData =
        query.value(QStringLiteral("active_note_sub_folder_data")).toString();

    // prepend the portable data path if we are in portable mode
    this->localPath =
        query.value(QStringLiteral("local_path")).toString();

    return true;
}

QList<NoteFolder> NoteFolder::fetchAll() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("disk"));
    QSqlQuery query(db);

    QList<NoteFolder> noteFolderList;

    query.prepare(QStringLiteral(
        "SELECT * FROM noteFolder ORDER BY priority ASC, id ASC"));
    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            noteFolderList.append(noteFolderFromQuery(query));
        }
    }

    return noteFolderList;
}

/**
 * Inserts or updates a NoteFolder object in the database
 */
bool NoteFolder::store() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("disk"));
    QSqlQuery query(db);

    if (this->id > 0) {
        query.prepare(QStringLiteral(
                "UPDATE noteFolder SET name = :name, local_path = :localPath, "
                        "priority = :priority, "
                        "active_tag_id = :activeTagId, show_subfolders = "
                        ":showSubfolders, active_note_sub_folder_data = "
                        ":activeNoteSubFolderData WHERE "
                        "id = :id"));
        query.bindValue(QStringLiteral(":id"), this->id);
    } else {
        query.prepare(QStringLiteral(
                "INSERT INTO noteFolder (name, local_path,"
                        "priority, active_tag_id, "
                        "show_subfolders, active_note_sub_folder_data)"
                        " VALUES (:name, :localPath, "
                        ":priority, :activeTagId, "
                        ":showSubfolders, :activeNoteSubFolderData)"));
    }

    query.bindValue(QStringLiteral(":name"), this->name);
    query.bindValue(QStringLiteral(":priority"), this->priority);
    query.bindValue(QStringLiteral(":activeTagId"), this->activeTagId);
    query.bindValue(QStringLiteral(":showSubfolders"), this->showSubfolders);
    query.bindValue(QStringLiteral(":activeNoteSubFolderData"), this->activeNoteSubFolderData);

    query.bindValue(QStringLiteral(":localPath"), this->localPath);

    if (!query.exec()) {
        // on error
        qWarning() << __func__ << ": " << query.lastError();
        return false;
    } else if (this->id == 0) {
        // on insert
        this->id = query.lastInsertId().toInt();
    }

    return true;
}

/**
 * Checks if the current noteFolder still exists in the database
 */
bool NoteFolder::exists() const { return NoteFolder::fetch(this->id).id > 0; }

bool NoteFolder::isFetched() const { return (this->id > 0); }

void NoteFolder::setAsCurrent() const {
    QSettings settings;
    settings.setValue(QStringLiteral("currentNoteFolderId"), id);

    // make the path relative to the portable data path if we are in
    // portable mode
    settings.setValue(QStringLiteral("notesPath"), localPath);
}

/**
 * Checks if this note folder is the current one
 */
bool NoteFolder::isCurrent() const { return currentNoteFolderId() == id; }

/**
 * Returns the id of the current note folder in the settings
 */
int NoteFolder::currentNoteFolderId() {
    const QSettings settings;
    return settings.value(QStringLiteral("currentNoteFolderId")).toInt();
}

/**
 * Returns the current note folder
 */
NoteFolder NoteFolder::currentNoteFolder() {
    return NoteFolder::fetch(currentNoteFolderId());
}

/**
 * Fetches the current local path
 */
QString NoteFolder::currentLocalPath() {
    QString path = QLatin1String("");
    const NoteFolder noteFolder = currentNoteFolder();

    if (noteFolder.isFetched()) {
        path = noteFolder.getLocalPath();
        const QFileInfo fileInfo(path);
        path = fileInfo.canonicalFilePath();
    }

    // load notesPath as fallback
    if (path.isEmpty()) {
        const QSettings settings;

        path = settings.value(QStringLiteral("notesPath")).toString();
    }

    path = Utils::Misc::removeIfEndsWith(std::move(path), QDir::separator());
    path = Utils::Misc::removeIfEndsWith(std::move(path),
                                         Utils::Misc::dirSeparator());

    return path;
}

/**
 * @brief getCurrentRootFolderName
 * @return Name of current root folder
 */
QString NoteFolder::currentRootFolderName(bool fullPath) {
    QString path = currentLocalPath();
    return fullPath ? path
                    : path.remove(
                          0, path.lastIndexOf(Utils::Misc::dirSeparator()) + 1);
}

/**
 * Fetches the current trash path
 */
QString NoteFolder::currentTrashPath() {
    return NoteFolder::currentLocalPath() + QDir::separator() +
           QStringLiteral("trash");
}

/**
 * Fetches the current attachments path
 */
QString NoteFolder::currentAttachmentsPath() {
    return NoteFolder::currentLocalPath() + QDir::separator() +
           QStringLiteral("attachments");
}

/**
 * Checks if there are note subfolders in the current note folder
 */
bool NoteFolder::isCurrentHasSubfolders() {
    const NoteFolder noteFolder = currentNoteFolder();
    return noteFolder.isFetched() ? noteFolder.isShowSubfolders() : false;
}

/**
 * Checks if the subfolders should be viewed for the current note folder
 */
bool NoteFolder::isCurrentShowSubfolders() {
    return !Utils::Misc::isEnableNoteTree() && isCurrentHasSubfolders();
}

/**
 * Checks if a note tree (not a note list) should be used for the current note
 * folder
 */
bool NoteFolder::isCurrentNoteTreeEnabled() {
    return Utils::Misc::isEnableNoteTree() && isCurrentHasSubfolders();
}

/**
 * Migrate the notesPath and the recentNoteFolders to NoteFolder objects
 */
bool NoteFolder::migrateToNoteFolders() {
    QSettings settings;

    // prepend the portable data path if we are in portable mode
    const QString notesPath = 
        settings.value(QStringLiteral("notesPath")).toString();

    if (countAll() > 0) {
        if (currentNoteFolder().getLocalPath() == notesPath) return false;
    }

    int priority = 0;

    // create notes path as NoteFolder
    if (!notesPath.isEmpty()) {
        NoteFolder noteFolder;
        noteFolder.setName(QObject::tr("default"));
        noteFolder.setLocalPath(notesPath);
        noteFolder.setPriority(priority++);
        noteFolder.setShowSubfolders(
            settings.value(QStringLiteral("showNoteSubFolders")).toBool());
        noteFolder.store();

        settings.remove(QStringLiteral("showNoteSubFolders"));

        if (noteFolder.isFetched()) {
            noteFolder.setAsCurrent();
        }
    }

    const QStringList recentNoteFolders =
        settings.value(QStringLiteral("recentNoteFolders")).toStringList();

    // create recent note folders as NoteFolder
    if (!recentNoteFolders.empty()) {
        for (const QString &recentNoteFolderPath : recentNoteFolders) {
            if (notesPath != recentNoteFolderPath) {
                NoteFolder noteFolder;
                noteFolder.setName(recentNoteFolderPath);
                noteFolder.setLocalPath(recentNoteFolderPath);
                noteFolder.setPriority(priority++);
                noteFolder.store();
            }
        }
    }

    return priority > 0;
}

QJsonObject NoteFolder::jsonObject() const {
    QJsonObject object;
    object.insert(QStringLiteral("text"), QJsonValue::fromVariant(name));
    object.insert(QStringLiteral("value"), QJsonValue::fromVariant(id));
    return object;
};

/**
 * Returns json text of the note folder list for the WebSocketServerService
 *
 * @return
 */
QString NoteFolder::noteFoldersWebServiceJsonText() {
    QJsonArray objectList;
    const QList<NoteFolder> noteFolders = NoteFolder::fetchAll();

    for (const NoteFolder &noteFolder : noteFolders) {
        objectList.push_back(noteFolder.jsonObject());
    }

    QJsonObject resultObject;
    resultObject.insert(QStringLiteral("type"),
                        QJsonValue::fromVariant(QStringLiteral("noteFolders")));
    resultObject.insert(QStringLiteral("data"), objectList);
    resultObject.insert(QStringLiteral("noteFolderName"),
                        NoteFolder::currentNoteFolder().getName());

    const QJsonDocument doc(resultObject);

    return doc.toJson(QJsonDocument::Compact);
}

/**
 * Checks if path is the localPath of a note folder
 *
 * @param path
 * @return
 */
bool NoteFolder::isPathNoteFolder(const QString &path) {
    const auto noteFolderList = fetchAll();
    for (const NoteFolder &noteFolder : noteFolderList) {
        if (path == noteFolder.getLocalPath()) {
            return true;
        }
    }

    return false;
}

void NoteFolder::setSettingsValue(const QString &key, const QVariant &value) {
    QSettings settings;
    settings.setValue(QString("NoteFolder-%1/%2").arg(QString::number(id), key), value);
}

QVariant NoteFolder::settingsValue(const QString &key,
                                   const QVariant &defaultValue) const {
    const QSettings settings;
    return settings.value(QString("NoteFolder-%1/%2").arg(QString::number(id), key), defaultValue);
}

QDebug operator<<(QDebug dbg, const NoteFolder &noteFolder) {
    dbg.nospace() << "NoteFolder: <id>" << noteFolder.id << " <name>" <<
            noteFolder.name << " <localPath>" << noteFolder.localPath <<
            " <priority>" << noteFolder.priority;
    return dbg.space();
}
