#include "entities/note.h"

#include <services/scriptingservice.h>
#include <utils/gui.h>
#include <utils/misc.h>
#include <utils/schema.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QSettings>
#include <QSqlError>
#include <QSqlRecord>
#include <QTemporaryFile>
#include <QUrl>
#include <utility>

#include "api/noteapi.h"
#include "entities/bookmark.h"
#include "helpers/codetohtmlconverter.h"
#include "libraries/botan/botan.h"
#include "libraries/botan/botanwrapper.h"
#include "libraries/md4c/md2html/render_html.h"
#include "libraries/md4c/md4c/md4c.h"
#include "notefolder.h"
#include "notesubfolder.h"
#include "tag.h"
#include "trashitem.h"

Note::Note() {
    this->id = 0;
    this->noteSubFolderId = 0;
    this->hasDirtyData = false;
    this->fileSize = 0;
}

int Note::getId() const { return this->id; }

QString Note::getName() const { return this->name; }

QDateTime Note::getFileLastModified() const { return this->fileLastModified; }

QDateTime Note::getFileCreated() const { return this->fileCreated; }

QDateTime Note::getModified() const { return this->modified; }

/**
 * Check 2nd bit for edit permissions
 *
 * @return
 */
qint64 Note::getFileSize() const { return this->fileSize; }

QString Note::getFileName() const { return this->fileName; }

NoteSubFolder Note::getNoteSubFolder() const {
    return NoteSubFolder::fetch(this->noteSubFolderId);
}

int Note::getNoteSubFolderId() const { return this->noteSubFolderId; }

bool Note::isInCurrentNoteSubFolder() const {
    const int currentNoteSubFolderId = NoteSubFolder::activeNoteSubFolderId();

    // beware: the special "All notes" note sub-folder also uses the id 0
    if (currentNoteSubFolderId < 0) {
        return true;
    }

    return this->noteSubFolderId == currentNoteSubFolderId;
}

void Note::setNoteSubFolder(const NoteSubFolder &noteSubFolder) {
    setNoteSubFolderId(noteSubFolder.getId());
}

void Note::setNoteSubFolderId(int id) { this->noteSubFolderId = id; }

QString Note::getNoteText() const { return this->noteText; }

void Note::setHasDirtyData(const bool hasDirtyData) {
    this->hasDirtyData = hasDirtyData;
}

bool Note::getHasDirtyData() const { return this->hasDirtyData; }

void Note::setName(QString text) { this->name = std::move(text); }

void Note::setNoteText(QString text) { this->noteText = std::move(text); }

bool Note::addNote(const QString &name, const QString &fileName,
                   const QString &text) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    query.prepare(
        QStringLiteral("INSERT INTO note ( name, file_name, note_text ) "
                       "VALUES ( :name, :file_name, :note_text )"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":file_name"), fileName);
    query.bindValue(QStringLiteral(":note_text"), text);
    return query.exec();
}

Note Note::fetch(int id) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    Note note;

    query.prepare(QStringLiteral("SELECT * FROM note WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        if (query.first()) {
            note = noteFromQuery(query);
        }
    }

    return note;
}

/**
 * Fetches a note by note name with a regular expression
 *
 * @param regExp
 * @param noteSubFolderId if not set all notes will be searched
 * @return
 */
Note Note::fetchByName(const QRegularExpression &regExp, int noteSubFolderId) {
    const QVector<Note> noteList =
        noteSubFolderId == -1 ? fetchAll()
                              : fetchAllByNoteSubFolderId(noteSubFolderId);

    // since there is no regular expression search in Qt's sqlite
    // implementation we have to iterate
    for (const Note &note : noteList) {
        QRegularExpressionMatch match = regExp.match(note.getName());
        if (match.hasMatch()) {
            return note;
        }
    }

    return Note();
}

Note Note::fetchByFileName(const QString &fileName, int noteSubFolderId) {
    Note note;

    // get the active note subfolder id if none was set
    if (noteSubFolderId == -1) {
        noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();
    }

    note.fillByFileName(fileName, noteSubFolderId);
    return note;
}

Note Note::fetchByFileName(const QString &fileName,
    const QString &noteSubFolderPathData) {
    auto noteSubFolder = NoteSubFolder::fetchByPathData(noteSubFolderPathData);

    return fetchByFileName(fileName, noteSubFolder.getId());
}

bool Note::fillByFileName(const QString &fileName, int noteSubFolderId) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    // get the active note subfolder id if none was set
    if (noteSubFolderId == -1) {
        noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();
    }

    query.prepare(
        QStringLiteral("SELECT * FROM note WHERE file_name = :file_name AND "
                       "note_sub_folder_id = :note_sub_folder_id"));
    query.bindValue(QStringLiteral(":file_name"), fileName);
    query.bindValue(QStringLiteral(":note_sub_folder_id"), noteSubFolderId);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        if (query.first()) {
            this->fillFromQuery(query);
            return true;
        }
    }

    return false;
}

/**
 * Fetches a note by its file path relative to the note folder it is in
 *
 * @brief Note::fetchByRelativeFilePath
 * @param relativePath
 * @return
 */
Note Note::fetchByRelativeFilePath(const QString &relativePath) {
    const QFileInfo fileInfo(relativePath);

    // load note sub-folder and note from the relative path
    // be aware that there must not be a ".." in the path, a canonical path must
    // be presented!
    const auto noteSubFolder =
        NoteSubFolder::fetchByPathData(fileInfo.path(), QStringLiteral("/"));
    const Note note =
        Note::fetchByFileName(fileInfo.fileName(), noteSubFolder.getId());

    return note;
}

/**
 * Fetches a note by its full file url
 *
 * @brief Note::fetchByFileUrl
 * @param url
 * @return
 */
Note Note::fetchByFileUrl(const QUrl &url) {
    const QString relativePath =
        Note::fileUrlInCurrentNoteFolderToRelativePath(url);
    const Note note = Note::fetchByRelativeFilePath(relativePath);

    return note;
}

bool Note::remove(bool withFile) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    query.prepare(QStringLiteral("DELETE FROM note WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), this->id);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
        return false;
    } else {
        if (withFile) {
            this->removeNoteFile();

            // remove all links to tags
            Tag::removeAllLinksToNote(*this);
        }

        return true;
    }
}

/**
 * @brief Copies a note to another path
 *
 * @param destinationPath
 * @return bool
 */
bool Note::copyToPath(const QString &destinationPath, QString noteFolderPath) {
    QDir d;
    if (this->fileExists() && (d.exists(destinationPath))) {
        QFile file(fullNoteFilePath());
        QString destinationFileName =
            destinationPath + QDir::separator() + this->fileName;

        if (d.exists(destinationFileName)) {
            qDebug() << destinationFileName << "already exists!";

            // find a new filename for the note
            const QDateTime currentDateTime = QDateTime::currentDateTime();
            destinationFileName = destinationPath + QDir::separator() +
                                  this->name + QChar(' ') +
                                  currentDateTime.toString(Qt::ISODate)
                                      .replace(QChar(':'), QChar('_')) +
                                  QChar('.') + defaultNoteFileExtension();
        }

        // copy the note file to the destination
        const bool isFileCopied = file.copy(destinationFileName);

        if (isFileCopied) {
            const QStringList embedmentFileList = getEmbedmentFileList();

            if (embedmentFileList.count() > 0) {
                if (noteFolderPath.isEmpty()) {
                    noteFolderPath = destinationPath;
                }

                if (NoteFolder::isPathNoteFolder(noteFolderPath)) {
                    const QDir noteEmbedmentDir(noteFolderPath + QDir::separator() +
                                        getName());

                    // created the note embedment folder if it doesn't exist
                    if (!noteEmbedmentDir.exists()) {
                        noteEmbedmentDir.mkpath(noteEmbedmentDir.path());
                    }

                    if (noteEmbedmentDir.exists()) {
                        // copy all images to the note embedment folder inside
                        // destinationPath
                        for (const QString &fileName : embedmentFileList) {
                            QFile embeddedFile(this->fullNoteFilePath() + "/" + getName() +
                                            QDir::separator() + fileName);

                            if (embeddedFile.exists()) {
                                embeddedFile.copy(noteEmbedmentDir.path() +
                                               QDir::separator() + fileName);
                            }
                        }
                    }
                }
            }
        }

        return isFileCopied;
    }

    return false;
}

/**
 * @brief Moves a note to another path
 *
 * @param destinationPath
 * @return bool
 */
bool Note::moveToPath(const QString &destinationPath,
                      const QString &noteFolderPath) {
    const bool result = copyToPath(destinationPath, noteFolderPath);

    if (result) {
        return remove(true);
    }

    return false;
}

/**
 * Returns a list of all linked image files of the note folder of the current
 * note
 * @return
 */
QStringList Note::getEmbedmentFileList() {
    QStringList fileList;

    // match image links in note's embedment folders
	QString noteName = getName().replace(" ", "_");

    QRegularExpression re(QStringLiteral(R"(!\[.*?\]\(.*)") + noteName + QStringLiteral(R"(/(.+?)\))"));
    QRegularExpressionMatchIterator i = re.globalMatch(noteText);

    // remove all found images from the orphaned files list
	const QString noteEmbedmentDir = getNoteSubFolder().fullPath() + QDir::separator() + getName().replace(" ", "_") + QDir::separator();
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        const QString fileName = match.captured(1);
        fileList << noteEmbedmentDir + fileName;
    }

    return fileList;
}

/**
 * Returns a list of all linked attachments of the current note
 * @return
 */
QStringList Note::getAttachmentsFileList() const {
    const QString text = getNoteText();
    QStringList fileList;

    // match attachment links like [956321614](file://attachments/956321614.pdf)
    // or [956321614](attachments/956321614.pdf)
    const QRegularExpression re(
        QStringLiteral(R"(\[.*?\]\(.*attachments/(.+?)\))"));
    QRegularExpressionMatchIterator i = re.globalMatch(text);

    // remove all found attachments from the orphaned files list
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        const QString fileName = match.captured(1);
        fileList << fileName;
    }

    return fileList;
}

Note Note::fetchByName(const QString &name,
                       const QString &noteSubFolderPathData,
                       const QString& pathDataSeparator) {
    auto noteSubFolder = NoteSubFolder::fetchByPathData(noteSubFolderPathData,
                                                        pathDataSeparator);

    return fetchByName(name, noteSubFolder.getId());
}

Note Note::fetchByName(const QString &name, int noteSubFolderId) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    // get the active note subfolder id if none was set
    if (noteSubFolderId == -1) {
        noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();
    }

    query.prepare(
        QStringLiteral("SELECT * FROM note WHERE name = :name AND "
                       "note_sub_folder_id = :note_sub_folder_id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":note_sub_folder_id"), noteSubFolderId);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        if (query.first()) {
            return noteFromQuery(query);
        }
    }
    return Note();
}

Note Note::noteFromQuery(const QSqlQuery &query) {
    Note note;
    note.fillFromQuery(query);
    return note;
}

void Note::fillFromQuery(const QSqlQuery &query) {
    id = query.value(QStringLiteral("id")).toInt();
    name = query.value(QStringLiteral("name")).toString();
    fileName = query.value(QStringLiteral("file_name")).toString();
    noteSubFolderId = query.value(QStringLiteral("note_sub_folder_id")).toInt();
    noteText = query.value(QStringLiteral("note_text")).toString();
    fileSize = query.value(QStringLiteral("file_size")).toLongLong();
    hasDirtyData = query.value(QStringLiteral("has_dirty_data")).toInt() == 1;
    fileCreated = query.value(QStringLiteral("file_created")).toDateTime();
    fileLastModified =
        query.value(QStringLiteral("file_last_modified")).toDateTime();
    created = query.value(QStringLiteral("created")).toDateTime();
    modified = query.value(QStringLiteral("modified")).toDateTime();
}

QVector<Note> Note::fetchAll(int limit) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QVector<Note> noteList;

    const QString sql =
        limit >= 0 ? QStringLiteral(
                         "SELECT * FROM note ORDER BY file_last_modified DESC "
                         "LIMIT :limit")
                   : QStringLiteral(
                         "SELECT * FROM note ORDER BY file_last_modified DESC");

    query.prepare(sql);

    if (limit >= 0) {
        noteList.reserve(limit);
        query.bindValue(QStringLiteral(":limit"), limit);
    }

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            noteList.append(noteFromQuery(query));
        }
    }

    return noteList;
}

QVector<int> Note::fetchAllIds(int limit, int offset) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QVector<int> noteIdList;
    QString sql = QStringLiteral("SELECT * FROM note ORDER BY id");

    if (limit >= 0) {
        sql += QStringLiteral(" LIMIT :limit");
    }

    if (offset >= 0) {
        sql += QStringLiteral(" OFFSET :offset");
    }

    query.prepare(sql);

    if (limit >= 0) {
        noteIdList.reserve(limit);
        query.bindValue(QStringLiteral(":limit"), limit);
    }

    if (offset >= 0) {
        query.bindValue(QStringLiteral(":offset"), offset);
    }

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            noteIdList.append(noteFromQuery(query).getId());
        }
    }

    return noteIdList;
}

QVector<Note> Note::fetchAllByNoteSubFolderId(int noteSubFolderId) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QVector<Note> noteList;
    const QString sql = QStringLiteral(
        "SELECT * FROM note WHERE note_sub_folder_id = "
        ":note_sub_folder_id ORDER BY file_last_modified DESC");

    query.prepare(sql);
    query.bindValue(QStringLiteral(":note_sub_folder_id"), noteSubFolderId);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            noteList.append(noteFromQuery(query));
        }
    }

    return noteList;
}

/**
 * Gets a list of note ids from a note list
 */
QVector<int> Note::noteIdListFromNoteList(const QVector<Note> &noteList) {
    QVector<int> idList;
    idList.reserve(noteList.size());

    QVector<Note>::const_iterator i;
    for (i = noteList.constBegin(); i != noteList.constEnd(); ++i) {
        idList.append((*i).getId());
    }
    return idList;
}

/**
 * Returns all notes that are not tagged
 */
QVector<Note> Note::fetchAllNotTagged(int activeNoteSubFolderId) {
    QVector<Note> noteList;
    if (activeNoteSubFolderId < 0) {
        noteList = Note::fetchAll();
    } else {
        noteList = Note::fetchAllByNoteSubFolderId(activeNoteSubFolderId);
    }
    QVector<Note> untaggedNoteList;
    untaggedNoteList.reserve(noteList.size());

    QVector<Note>::const_iterator i;
    for (i = noteList.constBegin(); i != noteList.constEnd(); ++i) {
        const int tagCount = Tag::countAllOfNote(*i);
        if (tagCount == 0) untaggedNoteList.append(*i);
    }
    return untaggedNoteList;
}

/**
 * Returns all notes names that are not tagged
 */
QVector<int> Note::fetchAllNotTaggedIds() {
    QVector<Note> noteList = Note::fetchAll();
    QVector<int> untaggedNoteIdList;
    untaggedNoteIdList.reserve(noteList.size());

    QVectorIterator<Note> itr(noteList);
    QVector<Note>::const_iterator it = noteList.constBegin();
    for (; it != noteList.constEnd(); ++it) {
        const int tagCount = Tag::countAllOfNote(*it);
        if (tagCount == 0) untaggedNoteIdList << it->getId();
    }

    return untaggedNoteIdList;
}

/**
 * Counts all notes that are not tagged
 */
int Note::countAllNotTagged(int activeNoteSubFolderId) {
    QVector<Note> noteList = Note::fetchAllNotTagged(activeNoteSubFolderId);
    return noteList.count();
}

QVector<Note> Note::search(const QString &text) {
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QVector<Note> noteList;

    query.prepare(
        QStringLiteral("SELECT * FROM note WHERE note_text LIKE :text "
                       "ORDER BY file_last_modified DESC"));
    query.bindValue(QStringLiteral(":text"),
                    QStringLiteral("%") + text + QStringLiteral("%"));

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            Note note = noteFromQuery(query);
            noteList.append(note);
        }
    }

    return noteList;
}

QVector<QString> Note::searchAsNameListInCurrentNoteSubFolder(
    const QString &text, bool searchInNameOnly) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QVector<QString> nameList;
    const QString textSearchSql =
        !searchInNameOnly ? QStringLiteral("OR note_text LIKE :text ")
                          : QLatin1String("");
    const int noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();

    query.prepare(
        QStringLiteral("SELECT name FROM note WHERE (name LIKE :text ") +
        textSearchSql +
        QStringLiteral(") AND note_sub_folder_id = :note_sub_folder_id "
                       "ORDER BY file_last_modified DESC"));
    query.bindValue(QStringLiteral(":text"),
                    QStringLiteral("%") + text + QStringLiteral("%"));
    query.bindValue(QStringLiteral(":note_sub_folder_id"), noteSubFolderId);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            nameList.append(query.value(QStringLiteral("name")).toString());
        }
    }

    return nameList;
}

QVector<QString> Note::searchAsNameList(const QString &text,
                                        bool searchInNameOnly) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QVector<QString> nameList;
    const QString textSearchSql =
        !searchInNameOnly ? QStringLiteral("OR note_text LIKE :text ")
                          : QLatin1String("");

    query.prepare(
        QStringLiteral("SELECT name FROM note WHERE (name LIKE :text ") +
        textSearchSql + QStringLiteral(") ORDER BY file_last_modified DESC"));
    query.bindValue(QStringLiteral(":text"),
                    QStringLiteral("%") + text + QStringLiteral("%"));

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            nameList.append(query.value(QStringLiteral("name")).toString());
        }
    }

    return nameList;
}

/**
 * Searches for text in notes and returns the note ids
 *
 * By default notes that contain every single word will be found, `word1
 * word2` will find all notes that are containing `word1` and `word2`
 *
 * You can search for longer texts by using quotes, `"this word1" word2`
 * will find all notes that are containing `this word1` and `word2`
 */
QVector<int> Note::searchInNotes(QString search, bool ignoreNoteSubFolder,
                                 int noteSubFolderId) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);
    auto noteIdList = QVector<int>();
    QStringList sqlList;

    // get the active note subfolder id if none was set
    if ((noteSubFolderId == -1) && !ignoreNoteSubFolder) {
        noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();
    }

    // build the string list of the search string
    const QStringList queryStrings = buildQueryStringList(std::move(search));

    sqlList.reserve(queryStrings.count());

    // we want to search for the text in the note text and the filename
    for (int i = 0; i < queryStrings.count(); i++) {
        sqlList.append(
            QStringLiteral("(note_text LIKE ? OR file_name LIKE ?)"));
    }

    QString sql;

    // build the query
    if (ignoreNoteSubFolder) {
        sql = QStringLiteral("SELECT id FROM note WHERE ") +
              sqlList.join(QStringLiteral(" AND "));
        query.prepare(sql);
    } else {
        sql = QStringLiteral(
                  "SELECT id FROM note WHERE note_sub_folder_id = "
                  ":note_sub_folder_id AND ") +
              sqlList.join(QStringLiteral(" AND "));
        query.prepare(sql);
        query.bindValue(0, noteSubFolderId);
    }

    // add the values to the query
    for (int i = 0; i < queryStrings.count(); i++) {
        int pos = i * 2;
        pos = ignoreNoteSubFolder ? pos : pos + 1;

        // bind the values for the note text and the filename
        query.bindValue(
            pos, QStringLiteral("%") + queryStrings[i] + QStringLiteral("%"));
        query.bindValue(pos + 1, QStringLiteral("%") + queryStrings[i] +
                                     QStringLiteral("%"));
    }

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            noteIdList.append(query.value(QStringLiteral("id")).toInt());
        }
    }

    return noteIdList;
}

int Note::countSearchTextInNote(const QString &search) const {
    return noteText.count(search, Qt::CaseInsensitive);
}

/**
 * Builds a string list of a search string
 */
QStringList Note::buildQueryStringList(QString searchString,
                                       bool escapeForRegularExpression) {
    auto queryStrings = QStringList();

    // check for strings in ""
    const QRegularExpression re(QStringLiteral("\"([^\"]+)\""));
    QRegularExpressionMatchIterator i = re.globalMatch(searchString);
    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        QString text = match.captured(1);

        if (escapeForRegularExpression) {
            text = QRegularExpression::escape(text);
        }

        queryStrings.append(text);
        searchString.remove(match.captured(0));
    }

    // remove a possible remaining "
    searchString.remove(QChar('\"'));
    // remove multiple spaces and spaces in front and at the end
    searchString = searchString.simplified();

    const QStringList searchStringList = searchString.split(QChar(' '));
    queryStrings.reserve(searchStringList.size());
    // add the remaining strings
    for (const QString &text : searchStringList) {
        // escape the text so strings like `^ ` don't cause an
        // infinite loop
        queryStrings.append(escapeForRegularExpression
                                ? QRegularExpression::escape(text)
                                : text);
    }

    // remove empty items, so the search will not run amok
    queryStrings.removeAll(QLatin1String(""));

    // remove duplicate query items
    queryStrings.removeDuplicates();

    return queryStrings;
}

QStringList Note::fetchNoteNamesInCurrentNoteSubFolder() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QStringList list;
    const int noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();

    query.prepare(
        QStringLiteral("SELECT DISTINCT(name) FROM note WHERE "
                       "note_sub_folder_id = :note_sub_folder_id "
                       "ORDER BY file_last_modified DESC"));
    query.bindValue(QStringLiteral(":note_sub_folder_id"), noteSubFolderId);

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            QString name = query.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                list.append(name);
            }
        }
    }

    return list;
}

QStringList Note::fetchNoteNames() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QStringList list;

    query.prepare(
        QStringLiteral("SELECT DISTINCT(name) FROM note "
                       "ORDER BY file_last_modified DESC"));

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            const QString name = query.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                list.append(name);
            }
        }
    }

    return list;
}

QStringList Note::fetchNoteFileNames() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QStringList list;

    query.prepare(QStringLiteral(
        "SELECT file_name FROM note ORDER BY file_last_modified DESC"));
    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            list.append(query.value(QStringLiteral("file_name")).toString());
        }
    }

    return list;
}

QVector<int> Note::fetchAllIdsByNoteTextPart(const QString &textPart) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    QVector<int> list;

    query.prepare(
        QStringLiteral("SELECT id FROM note WHERE note_text LIKE :text "
                       "ORDER BY file_last_modified DESC"));
    query.bindValue(QStringLiteral(":text"),
                    QStringLiteral("%") + textPart + QStringLiteral("%"));

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else {
        for (int r = 0; query.next(); r++) {
            list.append(query.value(QStringLiteral("id")).toInt());
        }
    }

    return list;
}

bool Note::storeNewText(QString text) {
    if (!this->fileWriteable()) {
        return false;
    }

    this->noteText = std::move(text);
    this->hasDirtyData = true;

    return this->store();
}

/**
 * Returns the default note file extension (`md`, `txt` or custom extensions)
 */
QString Note::defaultNoteFileExtension() {
    const QSettings settings;
    const QString extension =
        settings
            .value(QStringLiteral("defaultNoteFileExtension"),
                   QStringLiteral("md"))
            .toString();
    return extension;
}

/**
 * Returns the a list of the custom note file extensions
 */
QStringList Note::customNoteFileExtensionList(const QString &prefix) {
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

/**
 * Checks if it is allowed to have a different note file name than the headline
 */
bool Note::allowDifferentFileName() {
    return NoteFolder::currentNoteFolder().settingsValue(
                QStringLiteral("allowDifferentNoteFileName")).toBool();
}

//
// inserts or updates a note object in the database
//
bool Note::store() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    if (fileName.isEmpty()) {
        // don't store notes with empty filename and empty name
        if (name.isEmpty()) {
            return false;
        }

        generateFileNameFromName();
    }

    if (id > 0) {
        query.prepare(QStringLiteral("UPDATE note SET "
                              "name = :name,"
                              "file_name = :file_name,"
                              "file_size = :file_size,"
                              "note_sub_folder_id = :note_sub_folder_id,"
                              "note_text = :note_text,"
                              "has_dirty_data = :has_dirty_data, "
                              "file_last_modified = :file_last_modified,"
                              "file_created = :file_created,"
                              "modified = :modified "
                              "WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), id);
    } else {
        query.prepare(QStringLiteral("INSERT INTO note"
                              "(name, file_name, "
                              "file_size, note_text, has_dirty_data, "
                              "file_last_modified, file_created,"
                              "modified, note_sub_folder_id) "
                              "VALUES (:name, "
                              ":file_name, :file_size, :note_text,"
                              ":has_dirty_data, :file_last_modified,"
                              ":file_created, :modified,"
                              ":note_sub_folder_id)"));
    }

    const QDateTime modified = QDateTime::currentDateTime();

    // get the size of the note text
    const QByteArray bytes = noteText.toUtf8();
    fileSize = bytes.size();

    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":file_name"), fileName);
    query.bindValue(QStringLiteral(":file_size"), fileSize);
    query.bindValue(QStringLiteral(":note_sub_folder_id"), noteSubFolderId);
    query.bindValue(QStringLiteral(":note_text"), noteText);
    query.bindValue(QStringLiteral(":has_dirty_data"), hasDirtyData ? 1 : 0);
    query.bindValue(QStringLiteral(":file_created"), fileCreated);
    query.bindValue(QStringLiteral(":file_last_modified"), fileLastModified);
    query.bindValue(QStringLiteral(":modified"), modified);

    // on error
    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
        return false;
    } else if (id == 0) {    // on insert
        id = query.lastInsertId().toInt();
    }

    this->modified = modified;
    return true;
}

/**
 * Stores a note text file to disk
 * The file name will be changed if needed
 */
bool Note::storeNoteTextFileToDisk() {
    const Note oldNote = *this;
    const QString oldName = name;
    const QString oldNoteFilePath = fullNoteFilePath();
    TrashItem trashItem = TrashItem::prepare(this);

    if (allowDifferentFileName()) {
        // check if a QML function wants to set another note file name and
        // modify it accordingly
        modifyNoteTextFileNameFromQMLHook();
    } else {
        // checks if filename has to be changed (and change it if needed)
        handleNoteTextFileName();
    }

    QFile file(fullNoteFilePath());
    QFile::OpenMode flags = QIODevice::WriteOnly;
    const QSettings settings;
    const bool useUNIXNewline =
        settings.value(QStringLiteral("useUNIXNewline")).toBool();

    if (!useUNIXNewline) {
        flags |= QIODevice::Text;
    }

    qDebug() << "storing note file: " << this->fileName;

    if (!file.open(flags)) {
        qCritical() << QObject::tr(
                           "Could not store note file: %1 - Error "
                           "message: %2")
                           .arg(file.fileName(), file.errorString());
        return false;
    }

    const bool fileExists = this->fileExists();
    const QString newName = name;

    // assign the tags to the new name if the name has changed
    if (oldName != newName) {
        if (TrashItem::isLocalTrashEnabled()) {
            qDebug() << __func__ << " - 'trashItem': " << trashItem;

            // trash the old note
            trashItem.doTrashing();
        }

        // rename the note file names of note tag links
        Tag::renameNoteFileNamesOfLinks(oldName, newName,
                                        this->getNoteSubFolder());

        // handle the replacing of all note urls if a note was renamed
        handleNoteMoving(oldNote);
    }

    // transform all types of newline to \n
    // (maybe the ownCloud-sync works better then)
    const QString text = Utils::Misc::transformLineFeeds(this->noteText);

    //    diff_match_patch *diff = new diff_match_patch();
    //    QList<Diff> diffList = diff->diff_main( this->noteText, text );

    //    QString html = diff->diff_prettyHtml( diffList );
    //    diff->diff_cleanupSemantic( diffList );
    //    qDebug() << __func__ << " - 'diffList': " << diffList[0].toString();
    //    qDebug() << __func__ << " - 'html': " << html;

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << text;
    file.flush();
    file.close();

    this->hasDirtyData = false;
    this->fileLastModified = QDateTime::currentDateTime();

    if (!fileExists) {
        this->fileCreated = this->fileLastModified;
    }

    const bool noteStored = this->store();
    QFile oldFile(oldNoteFilePath);
    const QFileInfo oldFileInfo(oldFile);
    const QFile newFile(fullNoteFilePath());
    const QFileInfo newFileInfo(newFile);

    // in the end we want to remove the old note file if note was stored and
    // filename has changed
    // #1190: we also need to check if the files are the same even if the name
    // is not the same for NTFS
    if (noteStored && (fullNoteFilePath() != oldNoteFilePath) &&
        (oldFileInfo != newFileInfo)) {
        // remove the old note file
        if (oldFile.exists() && oldFileInfo.isFile() &&
            oldFileInfo.isReadable() && oldFile.remove()) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 5, 0))
            qInfo() << QObject::tr("Renamed note-file was removed: %1")
                           .arg(oldFile.fileName());
#else
            qDebug() << __func__ << " - 'renamed note-file was removed': "
                     << oldFile.fileName();
#endif

        } else {
            qWarning() << QObject::tr(
                              "Could not remove renamed note-file: %1"
                              " - Error message: %2")
                              .arg(oldFile.fileName(), oldFile.errorString());
        }
    }

    return noteStored;
}

/**
 * Does a file name cleanup
 */
QString Note::cleanupFileName(QString name) {
    // remove characters from the name that are problematic
    name.remove(QRegularExpression(QStringLiteral(R"([\/\\:])")));

    // remove multiple whitespaces from the name
    name.replace(QRegularExpression(QStringLiteral("\\s+")),
                 QStringLiteral(" "));

    return name;
}

/**
 * Does the extended filename cleanup
 * Will be triggered mainly on FAT and NTFS filesystems
 *
 * @param name
 * @return
 */
QString Note::extendedCleanupFileName(QString name) {
    // replace characters that cause problems on certain filesystems when
    // present in filenames with underscores
    name.replace(QRegularExpression(QStringLiteral(R"([\/\\:<>\"\|\?\*])")),
                 QStringLiteral(" "));

    return name;
}

/**
 * Checks if a QML function wants to set another note file name and
 * modifies it accordingly
 */
bool Note::modifyNoteTextFileNameFromQMLHook() {
    // check if a QML function wants to set another note name
    const QString newName =
        ScriptingService::instance()->callHandleNoteTextFileNameHook(this);

    // set the file name from the QML hook
    if (!newName.isEmpty() && (newName != name)) {
        qDebug() << __func__ << " - 'newName': " << newName;

        // store new name and filename
        name = newName;
        fileName = newName + QStringLiteral(".") + fileNameSuffix();
        return store();
    }

    return false;
}

/**
 * Checks if the filename has to be changed
 * Generates a new name and filename and removes the old file
 * (the new file is not stored to a note text file!)
 *
 * @return (bool) true if filename was changed
 */
bool Note::handleNoteTextFileName() {
    // split the text into a string list
    const QStringList noteTextLines =
        this->noteText.split(QRegExp(QStringLiteral(R"((\r\n)|(\n\r)|\r|\n)")));
    const int noteTextLinesCount = noteTextLines.count();

    // do nothing if there is no text
    if (noteTextLinesCount == 0) {
        return false;
    }

    QString name = noteTextLines.at(0).trimmed();
    // do nothing if the first line is empty
    if (name.isEmpty()) {
        return false;
    }

    // check if we have a frontmatter
    if (name == QStringLiteral("---") && noteTextLinesCount > 1) {
        bool foundEnd = false;

        for (int i = 1; i < noteTextLinesCount; i++) {
            const QString &line = noteTextLines.at(i).trimmed();

            if (foundEnd) {
                if (!line.isEmpty()) {
                    // set the name to the first non-empty line after the
                    // frontmatter
                    name = line;
                    break;
                }
            } else if (line == QStringLiteral("---")) {
                // we found the end of the frontmatter
                foundEnd = true;
            }
        }
    }

    // remove a leading "# " for markdown headlines
    name.remove(QRegularExpression(QStringLiteral("^#\\s")));

    // cleanup additional characters
    name = cleanupFileName(name);

    // check if name has changed
    if (name != this->name) {
        qDebug() << __func__ << " - 'name' was changed: " << name;
        QString fileName = generateNoteFileNameFromName(name);

        int nameCount = 0;
        const QString nameBase = name;

        // check if note with this filename already exists
        while (Note::fetchByFileName(fileName).exists()) {
            // find new filename for the note
            name =
                nameBase + QStringLiteral(" ") + QString::number(++nameCount);
            fileName = generateNoteFileNameFromName(name);
            qDebug() << __func__ << " - 'override fileName': " << fileName;

            if (nameCount > 1000) {
                break;
            }
        }

        // update the first line of the note text
        // TODO(pbek): UI has to be updated too then!
        // update: we now try not to change the first line of the note,
        //         this doesn't seem to trouble ownCloud / Nextcloud notes
        //         a lot, but it renames the notes to its own liking
        //        noteTextLines[0] = name;
        //        this->noteText = noteTextLines.join("\n");

        // set the new name and filename
        this->name = name;
        generateFileNameFromName();

        // let's check if we would be able to write to the file
        if (!canWriteToNoteFile()) {
            qDebug() << __func__ << " - cannot write to file " << this->fileName
                     << " - we will try another filename";

            // we try to replace some more characters (mostly for Windows
            // filesystems)
            name = extendedCleanupFileName(name);

            this->name = name;
            generateFileNameFromName();
        }

        return this->store();
    }

    return false;
}

/**
 * Generates a note filename from a name
 *
 * @param name
 * @return
 */
QString Note::generateNoteFileNameFromName(const QString &name) {
    return name + QStringLiteral(".") + defaultNoteFileExtension();
}

/**
 * Generates filename of the note from it's name
 */
void Note::generateFileNameFromName() {
    fileName = generateNoteFileNameFromName(name);
}

/**
 * Checks if we can write to the note file
 *
 * @return
 */
bool Note::canWriteToNoteFile() {
    QFile file(fullNoteFilePath());
    const bool canWrite = file.open(QIODevice::WriteOnly);

    if (file.isOpen()) {
        file.close();
    }

    return canWrite;
}

bool Note::updateNoteTextFromDisk() {
    if (!isFetched()) {
        return false;
    }

    QFile file(fullNoteFilePath());

    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << __func__ << " - 'file': " << file.fileName();
        qDebug() << __func__ << " - " << file.errorString();
        return false;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");
    this->noteText = in.readAll();
    file.close();

    // strangely it sometimes gets null
    if (this->noteText.isNull()) this->noteText = QLatin1String("");

    return true;
}

QString Note::getFullFilePathForFile(const QString &fileName) {
    const QSettings settings;

    // prepend the portable data path if we are in portable mode
    const QString notesPath =
        settings.value(QStringLiteral("notesPath")).toString();

    const QString path = Utils::Misc::removeIfEndsWith(std::move(notesPath),
                                         QStringLiteral("/")) +
           Utils::Misc::dirSeparator() + fileName;
    const QFileInfo fileInfo(path);

    // we can't get a canonical path if the path doesn't exist
    if (!fileInfo.exists()) {
        return path;
    }

    // we need that for links to notes in sub-folders in portable mode if
    // note folder lies outside of the application directory
    const QString canonicalFilePath = fileInfo.canonicalFilePath();

    return canonicalFilePath;
}

QString Note::getFilePathRelativeToNote(const Note &note) const {
    const QDir dir(fullNoteFilePath());

    // for some reason there is a leading "../" too much
    return dir.relativeFilePath(note.fullNoteFilePath())
        .remove(QRegularExpression(QStringLiteral(R"(^\.\.\/)")));
}

QString Note::getNoteUrlForLinkingTo(const Note &note, bool forceLegacy) const {
    const QSettings settings;
    QString noteUrl;

    if (forceLegacy ||
        settings.value(QStringLiteral("legacyLinking")).toBool()) {
        const QString noteNameForLink =
            Note::generateTextForLink(note.getName());
        noteUrl = QStringLiteral("note://") + noteNameForLink;
    } else {
        noteUrl = urlEncodeNoteUrl(getFilePathRelativeToNote(note));

        // if one of the link characters `<>()` were found in the note url use
        // the legacy way of linking because otherwise the "url" would break the
        // markdown link
        if (noteUrl.contains(QRegularExpression(R"([<>()])"))) {
            noteUrl = getNoteUrlForLinkingTo(note, true);
        }
    }

    return noteUrl;
}

/**
 * Returns an url string that is fit to be placed in a note link
 *
 * Example:
 * "Note with one bracket].md" will get "Note%20with%20one%20bracket%5D.md"
 */
QString Note::urlEncodeNoteUrl(const QString &url) {
    return QUrl::toPercentEncoding(url);
}

/**
 * Returns the url decoded representation of a string to e.g. fetch a note from
 * the note database if url came from a note link
 *
 * Example:
 * "Note%20with%20one%20bracket%5D.md" will get "Note with one bracket].md"
 * "Note%20with&#32;one bracket].md" will also get "Note with one bracket].md"
 */
QString Note::urlDecodeNoteUrl(QString url) {
    return QUrl::fromPercentEncoding(
        url.replace(QStringLiteral("&#32;"), QStringLiteral(" ")).toUtf8());
}

/**
 * Returns the full path of the note file
 */
QString Note::fullNoteFilePath() const {
    return getFullFilePathForFile(relativeNoteFilePath());
}

/**
 * Returns the full path of directory of the note file
 */
QString Note::fullNoteFileDirPath() const {
    QFileInfo fileInfo;
    fileInfo.setFile(fullNoteFilePath());
    return fileInfo.dir().path();
}

/**
 * Returns the relative path of the note file
 */
QString Note::relativeNoteFilePath(QString separator) const {
    QString fullFileName = fileName;

    if (separator.isEmpty()) {
        separator = Utils::Misc::dirSeparator();
    }

    if (noteSubFolderId > 0) {
        const NoteSubFolder noteSubFolder = getNoteSubFolder();
        if (noteSubFolder.isFetched()) {
            fullFileName.prepend(noteSubFolder.relativePath() + separator);
        }
    }

    return fullFileName;
}

/**
 * Returns the relative path of the note subfolder
 */
QString Note::relativeNoteSubFolderPath() const {
    QString path = QLatin1String("");

    if (noteSubFolderId > 0) {
        const NoteSubFolder noteSubFolder = getNoteSubFolder();
        if (noteSubFolder.isFetched()) {
            path = noteSubFolder.relativePath();
        }
    }

    return path;
}

/**
 * Returns the path-data of the note subfolder file
 */
QString Note::noteSubFolderPathData() const {
    QString path = QLatin1String("");

    if (noteSubFolderId > 0) {
        const NoteSubFolder noteSubFolder = getNoteSubFolder();
        if (noteSubFolder.isFetched()) {
            path = noteSubFolder.pathData();
        }
    }

    return path;
}

/**
 * Returns the full url of the note file
 */
QUrl Note::fullNoteFileUrl() const {
    QString windowsSlash = QLatin1String("");

#ifdef Q_OS_WIN32
    // we need another slash for Windows
    windowsSlash = QStringLiteral("/");
#endif

    return QUrl(
        QStringLiteral("file://") + windowsSlash +
        QUrl::toPercentEncoding(fullNoteFilePath(), QByteArrayLiteral(":/")));
}

/**
 * Stores all notes that were changed to disk
 *
 * @param currentNote will be set by this method if the filename of the current
 * note has changed
 * @param currentNoteChanged true if current note was changed
 * @param noteWasRenamed true if a note was renamed
 * @return amount of notes that were saved
 */
int Note::storeDirtyNotesToDisk(Note &currentNote, bool *currentNoteChanged,
                                bool *noteWasRenamed) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);
    ScriptingService *scriptingService = ScriptingService::instance();
    Note note;
    //    qDebug() << "storeDirtyNotesToDisk";

    query.prepare(
        QStringLiteral("SELECT * FROM note WHERE has_dirty_data = 1"));
    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
        return 0;
    } else {
        int count = 0;
        for (int r = 0; query.next(); r++) {
            note = noteFromQuery(query);
            const QString oldName = note.getName();
            const bool noteWasStored = note.storeNoteTextFileToDisk();

            // continue if note couldn't be stored
            if (!noteWasStored) {
                continue;
            }

            const QString newName = note.getName();

            // check if the file name has changed
            if (oldName != newName) {
                // rename the note file names of note tag links
                Tag::renameNoteFileNamesOfLinks(oldName, newName,
                                                note.getNoteSubFolder());
                *noteWasRenamed = true;

                // override the current note because the file name has changed
                if (note.isSameFile(currentNote)) {
                    currentNote = note;
                }

                // handle the replacing of all note urls if a note was renamed
                // we don't need to do that here, it would be called two
                // times this way
                //                Note::handleNoteRenaming(oldName, newName);
            }

            // emit the signal for the QML that the note was stored
            emit scriptingService->noteStored(QVariant::fromValue(
                static_cast<QObject *>(NoteApi::fromNote(note))));

            // reassign currentNote if filename of currentNote has changed
            if (note.isSameFile(currentNote)) {
                *currentNoteChanged = true;
            }

            qDebug() << "stored note: " << note;
            count++;
        }

        return count;
    }
}

void Note::createFromFile(QFile &file, int noteSubFolderId,
                          bool withNoteNameHook) {
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
        in.setCodec("UTF-8");

        // qDebug() << file.size() << in.readAll();
        const QString noteText = in.readAll();
        file.close();

        QFileInfo fileInfo;
        fileInfo.setFile(file);

        // create a nicer name by removing the extension
        QString name = fileInfo.fileName();

        const int lastPoint = name.lastIndexOf(QLatin1Char('.'));
        name = name.left(lastPoint);

        this->name = std::move(name);
        this->fileName = fileInfo.fileName();
        this->noteSubFolderId = noteSubFolderId;
        this->noteText = noteText;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
        this->fileCreated = fileInfo.created();
#else
        this->fileCreated = fileInfo.birthTime();
#endif

        this->fileLastModified = fileInfo.lastModified();
        this->store();

        if (withNoteNameHook) {
            // check if a name was set in a script
            const QString hookName =
                ScriptingService::instance()->callHandleNoteNameHook(this);

            if (!hookName.isEmpty()) {
                this->name = std::move(hookName);
                this->store();
            }
        }
    }
}

/**
 * Updates or creates a note from a file
 *
 * @param file
 * @param noteSubFolder
 * @return
 */
Note Note::updateOrCreateFromFile(QFile &file,
                                  const NoteSubFolder &noteSubFolder,
                                  bool withNoteNameHook) {
    const QFileInfo fileInfo(file);
    Note note = fetchByFileName(fileInfo.fileName(), noteSubFolder.getId());

    // regardless if the file was found or not, if the size differs or the
    // file was modified after the internal note was modified we want to load
    // the note content again
    if ((fileInfo.size() != note.getFileSize()) ||
        (fileInfo.lastModified() > note.getModified())) {
        // load file data and store note
        note.createFromFile(file, noteSubFolder.getId(), withNoteNameHook);

        //        qDebug() << __func__ << " - 'file modified': " <<
        //        file.fileName();
    }

    return note;
}

//
// deletes all notes in the database
//
bool Note::deleteAll() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    // no truncate in sqlite
    query.prepare(QStringLiteral("DELETE FROM note"));
    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
        return false;
    } else {
        return true;
    }
}

/**
 * Checks if file of note exists in the filesystem and is readable
 *
 * @return bool
 */
bool Note::fileExists() const {
    const QFile file(fullNoteFilePath());
    const QFileInfo fileInfo(file);
    return file.exists() && fileInfo.isFile() && fileInfo.isReadable();
}

/**
 * Checks if file of note exists in the filesystem and is writeable
 *
 * @return bool
 */
bool Note::fileWriteable() const {
    const QFile file(fullNoteFilePath());
    const QFileInfo fileInfo(file);
    return file.exists() && fileInfo.isFile() && fileInfo.isWritable();
}

//
// checks if the current note still exists in the database
//
bool Note::exists() const { return noteIdExists(this->id); }

bool Note::noteIdExists(int id) { return fetch(id).id > 0; }

//
// reloads the current Note (by fileName)
//
bool Note::refetch() { return this->fillByFileName(fileName, noteSubFolderId); }

/**
 * Returns the suffix of the note file name
 */
QString Note::fileNameSuffix() const {
    QFileInfo fileInfo;
    fileInfo.setFile(fileName);
    return fileInfo.suffix();
}

/**
 * Returns the base name of the note file name
 */
QString Note::fileBaseName(bool withFullName) {
    if (withFullName) {
        QStringList parts = fileName.split(QChar('.'));
        parts.removeLast();
        return parts.join(QChar('.'));
    } else {
        QFileInfo fileInfo;
        fileInfo.setFile(fileName);
        return fileInfo.baseName();
    }
}

/**
 * Renames a note file
 *
 * @param newName new file name (without file-extension)
 * @return
 */
bool Note::renameNoteFile(QString newName) {
    // cleanup not allowed characters characters
    newName = cleanupFileName(std::move(newName));

    // add the old file suffix to the name
    const QString newFileName = newName + QChar('.') + fileNameSuffix();

    // check if name has really changed
    if (name == newName) {
        return false;
    }

    // check if name already exists
    const Note existingNote = Note::fetchByName(newName);
    if (existingNote.isFetched() && (existingNote.getId() != id)) {
        return false;
    }

    if (TrashItem::isLocalTrashEnabled()) {
        // add note to trash
        bool trashResult = TrashItem::add(this);
        qDebug() << __func__ << " - 'trashResult': " << trashResult;
    }

    // get the note file to rename it
    QFile file(fullNoteFilePath());

    // store the new note file name
    fileName = std::move(newFileName);
    name = std::move(newName);
    store();

    // rename the note file name
    return file.rename(fullNoteFilePath());
}

/**
 * Removes the file of the note
 *
 * @return
 */
bool Note::removeNoteFile() {
    if (this->fileExists()) {
        if (TrashItem::isLocalTrashEnabled()) {
            // add note to trash
            bool trashResult = TrashItem::add(this);
            qDebug() << __func__ << " - 'trashResult': " << trashResult;
        }

        QFile file(fullNoteFilePath());
        qDebug() << __func__ << " - 'this->fileName': " << this->fileName;
        qDebug() << __func__ << " - 'file': " << file.fileName();
        return file.remove();
    }

    return false;
}

/**
 * @brief Returns html rendered markdown of the note text
 * @param notesPath for transforming relative local urls to absolute ones
 * @param maxImageWidth defined maximum image width (ignored if forExport is
 * true)
 * @param forExport defines whether the export or preview stylesheet
 * @return
 */
QString Note::toMarkdownHtml(const QString &notesPath, int maxImageWidth,
                             bool forExport, bool base64Images) {
    const QString str = getNoteText();

    // create a hash of the note text and the parameters
    const QString toHash = str + QString::number(maxImageWidth) +
                           (forExport ? QChar('1') : QChar('0')) +
                           (base64Images ? QChar('1') : QChar('0'));
    const QString hash = QString(
        QCryptographicHash::hash(toHash.toLocal8Bit(), QCryptographicHash::Sha1)
            .toHex());

    // check if the hash changed, if not return the old note text html
    if (hash == _noteTextHtmlConversionHash) {
        return _noteTextHtml;
    }

    const QString result = textToMarkdownHtml(
        std::move(str), notesPath, maxImageWidth, forExport, base64Images);

    // cache the html output and conversion hash
    _noteTextHtmlConversionHash = std::move(hash);
    _noteTextHtml = std::move(result);

    return _noteTextHtml;
}

void captureHtmlFragment(const MD_CHAR *data, MD_SIZE data_size,
                         void *userData) {
    QByteArray *array = static_cast<QByteArray *>(userData);

    if (data_size > 0) {
        array->append(data, int(data_size));
    }
}

/**
 * @brief Converts code blocks to highlighted code
 */
void highlightCode(QString &str, const QString &type, int cbCount) {
    if (cbCount >= 1) {
        const int firstBlock = str.indexOf(type, 0);
        int currentCbPos = firstBlock;
        for (int i = 0; i < cbCount; ++i) {
            // find endline
            const int endline = str.indexOf(QChar('\n'), currentCbPos);
            const QString lang =
                str.mid(currentCbPos + 3, endline - (currentCbPos + 3));
            // we skip it because it is inline code and not codeBlock
            if (lang.contains(type)) {
                int nextEnd = str.indexOf(type, currentCbPos + 3);
                nextEnd += 3;
                currentCbPos = str.indexOf(type, nextEnd);
                continue;
            }
            // move start pos to after the endline

            currentCbPos = endline + 1;
            // find the codeBlock end
            int next = str.indexOf(type, currentCbPos);
            // extract the codeBlock
            const QStringRef codeBlock =
                str.midRef(currentCbPos, next - currentCbPos);

            QString highlightedCodeBlock;
            if (!(codeBlock.isEmpty() && lang.isEmpty())) {
                const CodeToHtmlConverter c(codeBlock, lang);
                highlightedCodeBlock = c.process();
                // take care of the null char
                highlightedCodeBlock.replace(QChar('\u0000'),
                                             QLatin1String(""));
                str.replace(currentCbPos, next - currentCbPos,
                            highlightedCodeBlock);
                // recalculate next because string has now changed
                next = str.indexOf(type, currentCbPos);
            }
            // move next pos to after the backticks
            next += 3;
            // find the start of the next code block
            currentCbPos = str.indexOf(type, next);
        }
    }
}

/**
 * Converts a markdown string for a note to html
 *
 * @param str
 * @param notesPath
 * @param maxImageWidth
 * @param forExport
 * @param base64Images
 * @return
 */
QString Note::textToMarkdownHtml(QString str, const QString &notesPath,
                                 int maxImageWidth, bool forExport,
                                 bool base64Images) {
    // MD4C flags
    unsigned flags = MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS |
                     MD_FLAG_LATEXMATHSPANS | MD_FLAG_PERMISSIVEATXHEADERS |
                     MD_FLAG_UNDERLINE;
    // we parse the task lists ourselves
    flags &= ~MD_FLAG_TASKLISTS;

    const QSettings settings;
    if (!settings
             .value(QStringLiteral("MainWindow/noteTextView.underline"), true)
             .toBool()) {
        flags &= ~MD_FLAG_UNDERLINE;
    }

    QString windowsSlash = QLatin1String("");

#ifdef Q_OS_WIN32
    // we need another slash for Windows
    windowsSlash = "/";
#endif

    // remove frontmatter from markdown text
    if (str.startsWith(QLatin1String("---"))) {
        str.remove(
            QRegularExpression(QStringLiteral(R"(^---\n.+?\n---\n)"),
                               QRegularExpression::DotMatchesEverythingOption));
    }

    // parse for relative file urls and make them absolute
    // (for example to show images under the note path)
    str.replace(QRegularExpression(
                    QStringLiteral(R"(([\(<])file:\/\/([^\/].+?)([\)>]))")),
                QStringLiteral("\\1file://") + windowsSlash +
                    QRegularExpression::escape(notesPath) +
                    QStringLiteral("/\\2\\3"));

    // transform images without "file://" urls to file-urls (but we better do
    // that in the html, not the markdown!)
    //    str.replace(
    //            QRegularExpression(R"((\!\[.*\]\()((?!file:\/\/).+)(\)))"),
    //            "\\1file://" + windowsSlash +
    //            QRegularExpression::escape(notesPath)
    //            + "/\\2\\3");

    QRegularExpressionMatchIterator i;

    // Try to replace links like <my-note.md> or <file.pdf> with proper file
    // links We need to do that in the markdown because Hoedown would not create
    // a link tag This is a "has not '\w+:\/\/' in it" regular expression see:
    // http://stackoverflow.com/questions/406230/regular-expression-to-match-line-that-doesnt-contain-a-word
    // TODO: maybe we could do that per QTextBlock to check if it's done in
    // comment block? Important: The `\n` is needed to not crash under Windows
    // if there is just
    //            an opening `<` and a lot of other text after it
    i = QRegularExpression(
            QStringLiteral("<(((?!\\w+:\\/\\/)[^\\*<>\n])+\\.[\\w\\d]+)>"))
            .globalMatch(str);

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        const QString fileLink = match.captured(1);
        const QString url = Note::getFileURLFromFileName(fileLink, true);

        str.replace(match.captured(0), QStringLiteral("[") + fileLink +
                                           QStringLiteral("](") + url +
                                           QStringLiteral(")"));
    }

    // Try to replace links like [my note](my-note.md) or [file](file.md) with
    // proper file links This is currently also is handling relative image and
    // attachment links! We are using `{1,500}` instead of `+` because there
    // were crashes with regular expressions running wild
    // TODO: In theory we could convert relative note links in the html (and not
    // in the markdown) to prevent troubles with code blocks
    i = QRegularExpression(
            QStringLiteral(R"(\[(.+?)\]\((((?!\w+:\/\/)[^<>]){1,500}?)\))"))
            .globalMatch(str);

    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        const QString fileText = match.captured(1);
        const QString fileLink = match.captured(2);

        // ignore links to remote Nextcloud images
        if (fileLink.startsWith(QLatin1String("/core/preview"))) {
            continue;
        }

        const QString url = Note::getFileURLFromFileName(fileLink, true);

        str.replace(match.captured(0), QStringLiteral("[") + fileText +
                                           QStringLiteral("](") + url +
                                           QStringLiteral(")"));
    }

    // check if there is a script that wants to modify the markdown
    const QString preScriptResult =
        ScriptingService::instance()->callPreNoteToMarkdownHtmlHook(this, str);

    if (!preScriptResult.isEmpty()) {
        str = std::move(preScriptResult);
    }

    /*CODE HIGHLIGHTING*/
    int cbCount = str.count(QStringLiteral("```"));
    if (cbCount % 2 != 0) --cbCount;

    int cbTildeCount = str.count(QStringLiteral("~~~"));
    if (cbTildeCount % 2 != 0) --cbTildeCount;

    // divide by two to get actual number of code blocks
    cbCount /= 2;
    cbTildeCount /= 2;

    highlightCode(str, QStringLiteral("```"), cbCount);
    highlightCode(str, QStringLiteral("~~~"), cbTildeCount);

    const auto data = str.toUtf8();
    if (data.size() == 0) {
        return QLatin1String("");
    }

    QByteArray array;
    const int renderResult =
        md_render_html(data.data(), MD_SIZE(data.size()), &captureHtmlFragment,
                       &array, flags, 0);

    QString result;
    if (renderResult == 0) {
        result = QString::fromUtf8(array);
    } else {
        qWarning() << "MD4C Failure!";
        return QString();
    }

    // transform remote preview image tags
    Utils::Misc::transformRemotePreviewImages(result, maxImageWidth,
                                              externalImageHash());

    // transform images without "file://" urls to file-urls
    // Note: this is currently handled above in markdown
    //       if we want to activate this code again we need to take care of
    //       remote http(s) links to images! see:
    //       https://github.com/pbek/PKbSuite/issues/1286
    /*
        const QString subFolderPath = getNoteSubFolder().relativePath("/");
        const QString notePath = notesPath + (subFolderPath.isEmpty() ? "" : "/"
       + subFolderPath); result.replace( QRegularExpression(R"((<img
       src=\")((?!file:\/\/).+)\")"),
                "\\1file://" + windowsSlash + notePath + "/\\2\"");
    */

    const QString fontString =
        settings.value(QStringLiteral("MainWindow/noteTextView.code.font"))
            .toString();

    // set the stylesheet for the <code> blocks
    QString codeStyleSheet = QLatin1String("");
    if (!fontString.isEmpty()) {
        // set the note text view font
        QFont font;
        font.fromString(fontString);

        // add the font for the code block
        codeStyleSheet = QStringLiteral("pre, code { %1; }")
                             .arg(Utils::Schema::encodeCssFont(font));

        // ignore code font size to allow zooming (#1202)
        if (settings
                .value(QStringLiteral(
                           "MainWindow/noteTextView.ignoreCodeFontSize"),
                       true)
                .toBool()) {
            codeStyleSheet.remove(
                QRegularExpression(QStringLiteral(R"(font-size: \d+\w+;)")));
        }
    }

    const bool darkModeColors =
        !forExport ? settings.value(QStringLiteral("darkModeColors")).toBool()
                   : false;

    const QString codeForegroundColor =
        darkModeColors ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    const QString codeBackgroundColor =
        darkModeColors ? QStringLiteral("#444444") : QStringLiteral("#f1f1f1");

    // do some more code formatting
    // the "pre" styles are for the full-width code block background color
    codeStyleSheet += QString(
                          "pre { display: block; background-color: %1 } "
                          "code { padding: 3px; overflow: auto;"
                          " line-height: 1.45em; background-color: %1;"
                          " border-radius: 5px; color: %2; }")
                          .arg(codeBackgroundColor, codeForegroundColor);

    // TODO: We should probably make a stylesheet for this
    codeStyleSheet +=
        QStringLiteral(" .code-comment { color: #75715E; font-style: italic;}");
    codeStyleSheet += QStringLiteral(" .code-string { color: #E6DB74;}");
    codeStyleSheet += QStringLiteral(" .code-literal { color: #AE81FF;}");
    codeStyleSheet += QStringLiteral(" .code-type { color: #66D9EF;}");
    codeStyleSheet += QStringLiteral(" .code-builtin { color: #A6E22E;}");
    codeStyleSheet += QStringLiteral(" .code-keyword { color: #F92672;}");
    codeStyleSheet += QStringLiteral(" .code-other { color: #F92672;}");

    // correct the strikeout tag
    result.replace(QRegularExpression(QStringLiteral("<del>([^<]+)<\\/del>")),
                   QStringLiteral("<s>\\1</s>"));
    const bool rtl =
        settings.value(QStringLiteral("MainWindow/noteTextView.rtl")).toBool();
    const QString rtlStyle =
        rtl ? QStringLiteral("body {text-align: right; direction: rtl;}")
            : QLatin1String("");

    if (forExport) {
        // get defined body font from settings
        const QString bodyFontString =
            settings.value(QStringLiteral("MainWindow/noteTextView.font"))
                .toString();

        // create export stylesheet
        QString exportStyleSheet = QLatin1String("");
        if (!bodyFontString.isEmpty()) {
            QFont bodyFont;
            bodyFont.fromString(bodyFontString);

            exportStyleSheet = QStringLiteral("body { %1; }")
                                   .arg(Utils::Schema::encodeCssFont(bodyFont));
        }

        result = QString(
                     "<html><head><meta charset=\"utf-8\"/><style>"
                     "h1 { margin: 5px 0 20px 0; }"
                     "h2, h3 { margin: 10px 0 15px 0; }"
                     "img { max-width: 100%; }"
                     "pre { background-color: %5; border-radius: 5px; padding: "
                     "10px; }"
                     "pre > code { padding: 0; }"
                     "table {border-spacing: 0; border-style: solid; "
                     "border-width: 1px; "
                     "border-collapse: collapse; margin-top: 0.5em;}"
                     "th, td {padding: 2px 5px;}"
                     "a { color: #FF9137; text-decoration: none; } %1 %2 %4"
                     "</style></head><body class=\"export\">%3</body></html>")
                     .arg(codeStyleSheet, exportStyleSheet, result, rtlStyle,
                          codeBackgroundColor);

        // remove trailing newline in code blocks
        result.replace(QStringLiteral("\n</code>"), QStringLiteral("</code>"));
    } else {
        const QString schemaStyles =
            settings.value(QStringLiteral(
                               "MainWindow/noteTextView.useEditorStyles"),
                           true)
                    .toBool()
                ? Utils::Schema::getSchemaStyles()
                : QLatin1String("");

        // for preview
        result =
            QStringLiteral(
                "<html><head><style>"
                "h1 { margin: 5px 0 20px 0; }"
                "h2, h3 { margin: 10px 0 15px 0; }"
                "table {border-spacing: 0; border-style: solid; border-width: "
                "1px; border-collapse: collapse; margin-top: 0.5em;}"
                "th, td {padding: 2px 5px;}"
                "a { color: #FF9137; text-decoration: none; } %1 %3 %4"
                "</style></head><body class=\"preview\">%2</body></html>")
                .arg(codeStyleSheet, result, rtlStyle, schemaStyles);
        // remove trailing newline in code blocks
        result.replace(QStringLiteral("\n</code>"), QStringLiteral("</code>"));
    }

    // check if there is a script that wants to modify the content
    const QString scriptResult =
        ScriptingService::instance()->callNoteToMarkdownHtmlHook(this, result);

    if (!scriptResult.isEmpty()) {
        result = scriptResult;
    }

    // check if width of embedded local images is too high
    const QRegularExpression re(
        QStringLiteral("<img src=\"(file:\\/\\/[^\"]+)\""));
    i = re.globalMatch(result);

    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        const QString fileUrl = match.captured(1);
        const QString fileName = QUrl(fileUrl).toLocalFile();
        const QImage image(fileName);

        if (forExport) {
            result.replace(
                QRegularExpression(
                    QStringLiteral(R"(<img src="file:\/\/)") +
                    QRegularExpression::escape(windowsSlash + fileName) +
                    QStringLiteral("\"")),
                QStringLiteral("<img src=\"file://%2\"")
                    .arg(windowsSlash + fileName));
        } else {
            // for preview
            // cap the image width at maxImageWidth (note text view width)
            const int originalWidth = image.width();
            const int displayWidth =
                (originalWidth > maxImageWidth) ? maxImageWidth : originalWidth;

            result.replace(
                QRegularExpression(
                    QStringLiteral(R"(<img src="file:\/\/)") +
                    QRegularExpression::escape(windowsSlash + fileName) +
                    QChar('"')),
                QStringLiteral(R"(<img width="%1" src="file://%2")")
                    .arg(QString::number(displayWidth),
                         windowsSlash + fileName));
        }

        // encode the image base64
        if (base64Images) {
            QFile file(fileName);

            if (!file.open(QIODevice::ReadOnly)) {
                qWarning() << QObject::tr("Could not read image file: %1")
                                  .arg(fileName);

                continue;
            }

            QMimeDatabase db;
            const QMimeType type = db.mimeTypeForFile(file.fileName());
            const QByteArray ba = file.readAll();

            result.replace(
                QRegularExpression(QStringLiteral("<img(.+?)src=\"") +
                                   QRegularExpression::escape(fileUrl) +
                                   QChar('"')),
                QStringLiteral(R"(<img\1src="data:%1;base64,%2")")
                    .arg(type.name(), QString(ba.toBase64())));
        }
    }

    //    qDebug() << __func__ << " - 'result': " << result;
    return result;
}

/**
 * Returns the global external image hash instance
 */
Utils::Misc::ExternalImageHash *Note::externalImageHash() {
    auto *instance = qApp->property("externalImageHash")
                         .value<Utils::Misc::ExternalImageHash *>();

    if (instance == nullptr) {
        instance = new Utils::Misc::ExternalImageHash;

        qApp->setProperty(
            "externalImageHash",
            QVariant::fromValue<Utils::Misc::ExternalImageHash *>(instance));
    }

    return instance;
}

bool Note::isFetched() const { return (this->id > 0); }

/**
 * @brief Generates a text that can be used in a link
 * @param text
 * @return
 */
QString Note::generateTextForLink(QString text) {
    // replace everything but characters and numbers with "_"
    // we want to treat unicode characters as normal characters
    // to support links to notes with unicode characters in their names
    const QRegularExpression re(
        QStringLiteral("[^\\d\\w]"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::UseUnicodePropertiesOption);
    text.replace(re, QStringLiteral("_"));

    // if there are only numbers we also want the "@" added, because
    // otherwise the text will get interpreted as ip address
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral(R"(^(\d+)$)")).match(text);
    bool addAtSign = match.hasMatch();

    if (!addAtSign) {
        // add a "@" if the text contains numbers and utf8 characters
        // because the url will be invalid then
        addAtSign = text.contains(QRegularExpression(QStringLiteral("\\d"))) &&
                    text.toLocal8Bit().size() != text.length();
    }

    // if the hostname of the url will get to long QUrl will not
    // recognize it because of STD 3 rules, so we will use the
    // username for the note name instead of the hostname
    // the limit is 63 characters, but special characters use up
    // far more space
    if (addAtSign || text.length() > 46) {
        text += QChar('@');
    }

    return text;
}

/**
 * Generates a qint64 hash from a QString
 */
qint64 Note::qint64Hash(const QString &str) {
    const QByteArray hash =
        QCryptographicHash::hash(str.toUtf8(), QCryptographicHash::Md5);
    Q_ASSERT(hash.size() == 16);
    QDataStream stream(hash);
    qint64 a, b;
    stream >> a >> b;
    return a ^ b;
}

/**
 * Counts all notes
 */
int Note::countAll() {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);

    query.prepare(QStringLiteral("SELECT COUNT(*) AS cnt FROM note"));

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else if (query.first()) {
        return query.value(QStringLiteral("cnt")).toInt();
    }

    return 0;
}

/**
 * Counts all notes by note sub folder id
 */
int Note::countByNoteSubFolderId(int noteSubFolderId, bool recursive) {
    const QSqlDatabase db = QSqlDatabase::database(QStringLiteral("memory"));
    QSqlQuery query(db);
    QVector<int> noteSubFolderIdList;

    if (recursive) {
        noteSubFolderIdList =
            NoteSubFolder::fetchIdsRecursivelyByParentId(noteSubFolderId);
    } else {
        noteSubFolderIdList << noteSubFolderId;
    }

    QStringList idStringList;
    idStringList.reserve(noteSubFolderIdList.size());
    for (const int id : Utils::asConst(noteSubFolderIdList)) {
        idStringList << QString::number(id);
    }

    query.prepare(
        QStringLiteral(
            "SELECT COUNT(*) AS cnt FROM note WHERE note_sub_folder_id "
            "IN (") +
        idStringList.join(QChar(',')) + QChar(')'));

    if (!query.exec()) {
        qWarning() << __func__ << ": " << query.lastError();
    } else if (query.first()) {
        return query.value(QStringLiteral("cnt")).toInt();
    }

    return 0;
}

/**
 * Checks if the notes are the same (by file)
 *
 * @param note
 * @return
 */
bool Note::isSameFile(const Note &note) const {
    return (id == note.getId()) &&
           (noteSubFolderId == note.getNoteSubFolderId());
}

/**
 * Finds notes that link to a note with fileName via legacy note:// links or the
 * relative file links
 *
 * @param fileName
 * @return list of note ids
 */
QVector<int> Note::findLinkedNoteIds() const {
    QVector<int> noteIdList;

    // search for legacy links
    const QString linkText = getNoteURL(name);
    noteIdList << searchInNotes(QChar('<') + linkText + QChar('>'), true);
    noteIdList << searchInNotes(
        QStringLiteral("](") + linkText + QStringLiteral(")"), true);

    // search vor legacy links ending with "@"
    const QString altLinkText =
        Utils::Misc::appendIfDoesNotEndWith(linkText, QStringLiteral("@"));
    if (altLinkText != linkText) {
        noteIdList << searchInNotes(QChar('<') + altLinkText + QChar('>'),
                                    true);
        noteIdList << searchInNotes(
            QStringLiteral("](") + altLinkText + QChar(')'), true);
    }

    const auto noteList = Note::fetchAll();
    noteIdList.reserve(noteList.size());
    // search for links to the relative file path in all note
    for (const Note &note : noteList) {
        const int noteId = note.getId();
        if (noteId == getId() || noteIdList.contains(noteId)) {
            continue;
        }

        const QString relativeFilePath =
            Note::urlEncodeNoteUrl(note.getFilePathRelativeToNote(*this));
        const QString noteText = note.getNoteText();

        // search for links to the relative file path in note
        if (noteText.contains(QStringLiteral("<") + relativeFilePath +
                              QStringLiteral(">")) ||
            noteText.contains(QStringLiteral("](") + relativeFilePath +
                              QStringLiteral(")"))) {
            noteIdList.append(note.getId());
        }
    }

    // remove duplicates and return list
    // return noteIdList.toSet().toList();
    // QSet<int>(noteIdList.constBegin(), noteIdList.constEnd());
    std::sort(noteIdList.begin(), noteIdList.end());
    noteIdList.erase(std::unique(noteIdList.begin(), noteIdList.end()),
                     noteIdList.end());
    return noteIdList;
}

/**
 * Returns a (legacy) url to a note
 *
 * @param baseName
 * @return
 */
const QString Note::getNoteURL(const QString &baseName) {
    return QStringLiteral("note://") + generateTextForLink(std::move(baseName));
}

/**
 * Returns the note-id url to a note
 *
 * @return
 */
QString Note::getNoteIdURL() const {
    return QStringLiteral("noteid://note-") + QString::number(getId());
}

/**
 * Returns the url to a note from a file name
 *
 * @param fileName
 * @return
 */
const QString Note::getNoteURLFromFileName(const QString &fileName) {
    const QFileInfo info(fileName);
    // TODO: baseName() will cut names like "Note 2018-07-26T18.24.22.md" down
    // to "Note 2018-07-26T18"!
    return Note::getNoteURL(info.baseName());
}

/**
 * Returns the absolute file url from a relative file name
 *
 * @param fileName
 * @return
 */
QString Note::getFileURLFromFileName(QString fileName,
                                     bool urlDecodeFileName) const {
    if (urlDecodeFileName) {
        fileName = urlDecodeNoteUrl(fileName);
    }

    if (noteSubFolderId > 0) {
        const NoteSubFolder noteSubFolder = getNoteSubFolder();
        if (noteSubFolder.isFetched()) {
            fileName.prepend(noteSubFolder.relativePath() +
                             QStringLiteral("/"));
        }
    }

    const QString path = this->getFullFilePathForFile(fileName);

    return QString(QUrl::fromLocalFile(path).toEncoded());
}

/**
 * @brief Note::fetchByRelativeFileName fetches a note by a relative filename by
 * current note
 * @param fileName
 * @return
 */
Note Note::fetchByRelativeFileName(const QString &fileName) const {
    const QString url = getFileURLFromFileName(fileName);
    return fetchByFileUrl(QUrl(url));
}

bool Note::fileUrlIsNoteInCurrentNoteFolder(const QUrl &url) {
    if (url.scheme() != QStringLiteral("file")) {
        return false;
    }

    const QString path = url.toLocalFile();
    if (!QFile(path).exists()) {
        return false;
    }

    return path.startsWith(NoteFolder::currentLocalPath()) &&
           path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive);
}

QString Note::fileUrlInCurrentNoteFolderToRelativePath(const QUrl &url) {
    QString path = url.toLocalFile();
    qDebug() << __func__ << " - 'path': " << path;

    // translates the "a path/../an other path" to "an other path"
    // needed for Note::fetchByRelativeFilePath!
    const QFileInfo fileInfo(path);
    path = fileInfo.canonicalFilePath();

    qDebug() << __func__ << " - 'canonicalFilePath': " << path;

    return path.remove(Utils::Misc::appendIfDoesNotEndWith(
        NoteFolder::currentLocalPath(), QStringLiteral("/")));
}

/**
 * @brief Note::relativeFilePath returns the relative path of "path" in regard
 * to the the path of the note
 * @param path
 * @return
 */
QString Note::relativeFilePath(const QString &path) const {
    const QDir dir(fullNoteFilePath());
    // for some reason there is a leading "../" too much
    return dir.relativeFilePath(path).remove(
        QRegularExpression(QStringLiteral(R"(^\.\.\/)")));
}

/**
 * Handles the replacing of all note urls if the note was renamed or moved
 * (sub-folder)
 *
 * @param oldNote
 */
void Note::handleNoteMoving(const Note &oldNote) const {
    const QVector<int> noteIdList = oldNote.findLinkedNoteIds();
    const int noteCount = noteIdList.count();

    if (noteCount == 0) {
        return;
    }

    const QString oldUrl = getNoteURL(oldNote.getName());
    const QString newUrl = getNoteURL(name);

    if (Utils::Gui::question(
            Q_NULLPTR, QObject::tr("Note file path changed"),
            QObject::tr("A change of the note path was detected. Would you "
                        "like to replace all occurrences of "
                        "<strong>%1</strong> links with "
                        "<strong>%2</strong> and links with filename "
                        "<strong>%3</strong> with <strong>%4</strong>"
                        " in <strong>%n</strong> note file(s)?",
                        "", noteCount)
                .arg(oldUrl, newUrl, oldNote.getFileName(), fileName),
            QStringLiteral("note-replace-links")) == QMessageBox::Yes) {
        // replace the urls in all found notes
        for (const int noteId : noteIdList) {
            Note note = Note::fetch(noteId);
            if (!note.isFetched()) {
                continue;
            }

            QString text = note.getNoteText();

            // replace legacy links with note://
            text.replace(QStringLiteral("<") + oldUrl + QStringLiteral(">"),
                         QStringLiteral("<") + newUrl + QStringLiteral(">"));
            text.replace(QStringLiteral("](") + oldUrl + QStringLiteral(")"),
                         QStringLiteral("](") + newUrl + QStringLiteral(")"));

            // replace legacy links with note:// and ending @
            if (!oldUrl.contains(QLatin1String("@"))) {
                text.replace(
                    QStringLiteral("<") + oldUrl + QStringLiteral("@>"),
                    QStringLiteral("<") + newUrl + QStringLiteral(">"));
                text.replace(
                    QStringLiteral("](") + oldUrl + QStringLiteral("@)"),
                    QStringLiteral("](") + newUrl + QStringLiteral(")"));
            }

            QString oldNoteRelativeFilePath =
                note.getFilePathRelativeToNote(oldNote);
            const QString relativeFilePath =
                urlEncodeNoteUrl(note.getFilePathRelativeToNote(*this));

            // replace non-urlencoded relative file links to the note
            text.replace(
                QStringLiteral("<") + oldNoteRelativeFilePath +
                    QStringLiteral(">"),
                QStringLiteral("<") + relativeFilePath + QStringLiteral(">"));
            text.replace(
                QStringLiteral("](") + oldNoteRelativeFilePath +
                    QStringLiteral(")"),
                QStringLiteral("](") + relativeFilePath + QStringLiteral(")"));

            // replace url encoded relative file links to the note
            oldNoteRelativeFilePath = urlEncodeNoteUrl(oldNoteRelativeFilePath);
            text.replace(
                QStringLiteral("<") + oldNoteRelativeFilePath +
                    QStringLiteral(">"),
                QStringLiteral("<") + relativeFilePath + QStringLiteral(">"));
            text.replace(
                QStringLiteral("](") + oldNoteRelativeFilePath +
                    QStringLiteral(")"),
                QStringLiteral("](") + relativeFilePath + QStringLiteral(")"));

            note.storeNewText(text);
        }
    }
}

/**
 * Creates a note headline from a name
 *
 * @param name
 * @return
 */
QString Note::createNoteHeader(const QString &name) {
    QString header = name.trimmed() + QStringLiteral("\n");
    const auto len = std::min(name.length(), 40);
    header.reserve(len);
    header.append(QString(QChar('=')).repeated(len));
    header.append(QStringLiteral("\n\n"));
    return header;
}

/**
 * Return the path of note's embedded item folder
 */
QString Note::currentEmbedmentFolder() {
	return fullNoteFileDirPath() + "/" + getName().replace(" ", "_");
}

/**
 * Returns the markdown of the inserted object/file into a note
 */
QString Note::getInsertEmbedmentMarkdown(QFile *file, mediaType type, bool addNewLine,
                                     bool returnUrlOnly, QString title) {
    // file->exists() is false on Arch Linux for QTemporaryFile!
    if (file->size() > 0) {
        // Test if note's embedment folder exists
        const QDir dir(currentEmbedmentFolder());

        // created the embedment folder if it doesn't exist
        if (!dir.exists()) {
            dir.mkpath(dir.path());
        }

        const QFileInfo fileInfo(file->fileName());

        QString suffix = fileInfo.suffix();
        QMimeDatabase db;
        const QMimeType mimetype = db.mimeTypeForFile(file->fileName());

        // try to detect the mime type of the file and use a proper file suffix
        if (mimetype.isValid()) {
            const QStringList suffixes = mimetype.suffixes();
            if (suffixes.count() > 0) {
                suffix = suffixes.at(0);
            }
        }

        // find a random name for the new file
        const QString newFileName = Utils::Misc::makeFileNameRandom(
            file->fileName(), suffix);

        const QString newFilePath =
            dir.path() + QDir::separator() + fileInfo.fileName().replace(" ", "_");

        // copy the file to the embedment folder
        file->copy(newFilePath);

        QString embedmentUrlString = embedmentUrlStringForFileName(fileInfo.fileName());

        // check if we only want to return the embedment url string
        if (returnUrlOnly) {
            return embedmentUrlString;
        }

        if (title.isEmpty()) {
            title = fileInfo.baseName();
        }

        QString strEmbedmentCode = QLatin1String("");
        switch (type) {
			case mediaType::image: {
				QFile newFile(newFilePath);
				scaleDownImageFileIfNeeded(newFile);
				
				// return the image link
				// we add a "\n" in the end so that hoedown recognizes multiple images
				strEmbedmentCode =  QStringLiteral("![") + title + QStringLiteral("](") + embedmentUrlString + QStringLiteral(")") + (addNewLine ? QStringLiteral("\n") : QLatin1String(""));
			}
			break;
			case mediaType::attachment: {
				strEmbedmentCode = QStringLiteral("[") + title + QStringLiteral("](") + embedmentUrlString + QStringLiteral(")");
			}
			break;
			case mediaType::pdf: {
				strEmbedmentCode = QStringLiteral("[") + title + QStringLiteral("](file://") + dir.path() + QDir::separator() + embedmentUrlString + QStringLiteral(")") + (addNewLine ? "\n" : "");
			}
		}
		
		return strEmbedmentCode;
    }

    return QLatin1String("");
}

QString Note::embedmentUrlStringForFileName(const QString &fileName) const {
	QString tmpUrlString = getName() + QDir::separator() + fileName;
	
    return tmpUrlString.replace(" ", "_");
}

/**
 * Downloads an url to the note's embedment folder and returns the markdown code or the
 * url for it relative to the note
 *
 * @param url
 * @param returnUrlOnly
 * @return
 */
QString Note::downloadUrlToEmbedment(const QUrl &url, bool returnUrlOnly) {
    // try to get the suffix from the url
    QString suffix = url.toString()
                         .split(QStringLiteral("."), QString::SkipEmptyParts)
                         .last();

    if (suffix.isEmpty()) {
        suffix = QStringLiteral("image");
    }

    // remove strings like "?b=16068071000" and non-characters from the suffix
    suffix.remove(QRegularExpression(QStringLiteral("\\?.+$")))
        .remove(QRegularExpression(QStringLiteral("[^a-zA-Z0-9]")));

    QString text;
    QTemporaryFile *tempFile =
        new QTemporaryFile(QDir::tempPath() + QDir::separator() +
                           QStringLiteral("media-XXXXXX.") + suffix);

    if (tempFile->open()) {
        // download the image to the temporary file
        if (Utils::Misc::downloadUrlToFile(url, tempFile)) {
            // copy image to embedment folder and generate markdown code for
            // the image
            text = getInsertEmbedmentMarkdown(tempFile, mediaType::image, true, returnUrlOnly);
        }
    }

    delete tempFile;

    return text;
}

/**
 * Imports an image from a base64 string and returns the markdown code
 *
 * @param data
 * @param imageSuffix
 * @return
 */
QString Note::importMediaFromBase64(QString &data, const QString &imageSuffix) {
    // if data still starts with base64 prefix remove it
    if (data.startsWith(QLatin1String("base64,"), Qt::CaseInsensitive)) {
        data = data.mid(6);
    }

    // create a temporary file for the image
    QTemporaryFile *tempFile =
        new QTemporaryFile(QDir::tempPath() + QDir::separator() +
                           QStringLiteral("media-XXXXXX.") + imageSuffix);

    if (!tempFile->open()) {
        delete tempFile;
        return QLatin1String("");
    }

    // write image to the temporary file
    tempFile->write(QByteArray::fromBase64(data.toLatin1()));

    // store the temporary image in the embedment folder and return the markdown
    // code
    const QString markdownCode = getInsertEmbedmentMarkdown(tempFile, mediaType::image);

    delete tempFile;

    return markdownCode;
}

/**
 * Scales down an image file if needed
 * The image file will be overwritten in the process
 *
 * @param file
 * @return
 */
bool Note::scaleDownImageFileIfNeeded(QFile &file) {
    const QSettings settings;

    // load image scaling settings
    const bool scaleImageDown =
        settings.value(QStringLiteral("imageScaleDown"), false).toBool();

    if (!scaleImageDown) {
        return true;
    }

    QImage image;

    if (!image.load(file.fileName())) {
        return false;
    }

    const int width =
        settings.value(QStringLiteral("imageScaleDownMaximumWidth"), 1024)
            .toInt();
    const int height =
        settings.value(QStringLiteral("imageScaleDownMaximumHeight"), 1024)
            .toInt();

    QPixmap pixmap;
    pixmap = pixmap.fromImage(image.scaled(width, height, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));

    file.open(QIODevice::WriteOnly);
    pixmap.save(&file);
    file.close();

    return true;
}

/**
 * Tries to fetch a note from an url string
 *
 * @param urlString
 * @return
 */
Note Note::fetchByUrlString(const QString &urlString) {
    const QUrl url = QUrl(urlString);

    // if the name of the linked note only consists of numbers we cannot use
    // host() to get the filename, it would get converted to an ip-address
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral(R"(^\w+:\/\/(\d+)$)"))
            .match(urlString);
    QString fileName = match.hasMatch() ? match.captured(1) : url.host();

    // we are using the user name as fallback if the hostname was too long
    if (!url.userName().isEmpty()) {
        fileName = url.userName();
    } else {
        if (fileName.isEmpty()) {
            return Note();
        }

        // add a ".com" to the filename to simulate a valid domain
        fileName += QStringLiteral(".com");

        // convert the ACE to IDN (internationalized domain names) to support
        // links to notes with unicode characters in their names
        // then remove the ".com" again
        fileName = Utils::Misc::removeIfEndsWith(
            QUrl::fromAce(fileName.toLatin1()), QStringLiteral(".com"));

        // if it seem we have unicode characters in our filename let us use
        // wildcards for each number, because full width numbers get somehow
        // translated to normal numbers by the QTextEdit
        if (fileName != url.host()) {
            fileName.replace(QLatin1Char('1'), QStringLiteral("[1１]"))
                .replace(QLatin1Char('2'), QStringLiteral("[2２]"))
                .replace(QLatin1Char('3'), QStringLiteral("[3３]"))
                .replace(QLatin1Char('4'), QStringLiteral("[4４]"))
                .replace(QLatin1Char('5'), QStringLiteral("[5５]"))
                .replace(QLatin1Char('6'), QStringLiteral("[6６]"))
                .replace(QLatin1Char('7'), QStringLiteral("[7７]"))
                .replace(QLatin1Char('8'), QStringLiteral("[8８]"))
                .replace(QLatin1Char('9'), QStringLiteral("[9９]"))
                .replace(QLatin1Char('0'), QStringLiteral("[0０]"));
        }
    }

    // this makes it possible to search for file names containing spaces
    // instead of spaces a "-" has to be used in the note link
    // example: note://my-note-with-spaces-in-the-name
    fileName.replace(QChar('-'), QChar('?')).replace(QChar('_'), QChar('?'));

    // create a regular expression to search in sqlite note table
    QString escapedFileName = QRegularExpression::escape(fileName);
    escapedFileName.replace(QStringLiteral("\\?"), QStringLiteral("."));
    const QRegularExpression regExp = QRegularExpression(
        QLatin1Char('^') + escapedFileName + QLatin1Char('$'),
        QRegularExpression::CaseInsensitiveOption);

    qDebug() << __func__ << " - 'regExp': " << regExp;

    const int noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();
    Note note;

    qDebug() << __func__ << " - 'noteSubFolderId': " << noteSubFolderId;

    if (noteSubFolderId > 0) {
        note = Note::fetchByName(regExp, noteSubFolderId);
        qDebug() << __func__ << " - 'note in sub folder': " << note;
    }

    // if we haven't found a note we try searching in all note subfolders
    if (!note.isFetched()) {
        note = Note::fetchByName(regExp);
        qDebug() << __func__ << " - 'note in all sub folders': " << note;
    }

    return note;
}

/**
 * Generates the preview text of the note
 *
 * @return
 */
QString Note::getNotePreviewText(bool asHtml, int lines) const {
    QString noteText = getNoteText();

    // remove Windows line breaks
    noteText.replace(QRegularExpression(QStringLiteral("\r\n")),
                     QStringLiteral("\n"));

    // remove headlines
    noteText.remove(QRegularExpression(QStringLiteral("^.+\n=+\n+")));
    noteText.remove(QRegularExpression(QStringLiteral("^# .+\n+")));

    // remove multiple line breaks
    noteText.replace(QRegularExpression(QStringLiteral("\n\n+")),
                     QStringLiteral("\n"));

    const QStringList &lineList = noteText.split(QStringLiteral("\n"));

    if (lineList.isEmpty()) {
        return QLatin1String("");
    }

    noteText = QLatin1String("");
    for (int i = 0; i < std::min(lines, lineList.count()); i++) {
        if (i > 0) {
            noteText += QStringLiteral("\n");
        }

        QString line = lineList.at(i);
        line = line.trimmed();
        line.truncate(80);

        noteText += line;
    }

    if (asHtml) {
        noteText = Utils::Misc::htmlspecialchars(std::move(noteText));
        noteText.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    }

    return noteText;
}

/**
 * Generate the preview text if multiple notes are selected
 *
 * @param notes
 * @return
 */
QString Note::generateMultipleNotesPreviewText(const QVector<Note> &notes) {
    const QSettings settings;
    const bool darkModeColors =
        settings.value(QStringLiteral("darkModeColors")).toBool();
    const QString oddBackgroundColor =
        darkModeColors ? QStringLiteral("#444444") : QStringLiteral("#f1f1f1");
    const QString linkColor =
        darkModeColors ? QStringLiteral("#eeeeee") : QStringLiteral("#222222");

    QString previewHtml = QStringLiteral(
                              "<html><head><style>"
                              "table, body {width: 100%;}"
                              "table td {padding: 10px}"
                              "table td.odd {background-color: ") +
                          oddBackgroundColor +
                          QStringLiteral(
                              ";}"
                              "p {margin: 0.5em 0 0 0;}"
                              "small {font-size: 0.8em;}"
                              "h2 {margin-bottom: 0.5em;}"
                              "h2 a {text-decoration: none; color: ") +
                          linkColor +
                          QStringLiteral(
                              "}"
                              "</style></head>"
                              "<body>"
                              "   <table>");

    const int notesCount = notes.count();
    const int displayedNotesCount = notesCount > 40 ? 40 : notesCount;

    bool isOdd = false;
    for (int i = 0; i < displayedNotesCount; i++) {
        const Note note = notes[i];
        const QString oddStyle =
            isOdd ? QStringLiteral(" class='odd'") : QLatin1String("");
        const QDateTime modified = note.getFileLastModified();
        const QString noteText = note.getNotePreviewText(true, 5);
        const QString noteLink = note.getNoteIdURL();

        previewHtml += QStringLiteral("<tr><td") + oddStyle +
                       QStringLiteral(
                           ">"
                           "<h2><a href='") +
                       noteLink + QStringLiteral("'>") + note.getName() +
                       QStringLiteral(
                           "</a></h2>"
                           "<small>") +
                       modified.toString() +
                       QStringLiteral(
                           "</small>"
                           "<p>") +
                       noteText +
                       QStringLiteral(
                           "</p>"
                           "</td></tr>");
        isOdd = !isOdd;
    }

    if (displayedNotesCount < notesCount) {
        previewHtml += QStringLiteral("<tr><td>") +
                       QObject::tr("…and %n more note(s)", "",
                                   notesCount - displayedNotesCount) +
                       QStringLiteral("</td></tr>");
    }

    previewHtml += QStringLiteral(
        "   </table>"
        "</body>"
        "</html>");

    return previewHtml;
}

/**
 * Returns the parsed bookmarks of the note for the WebSocketServerService
 *
 * @return
 */
QString Note::getParsedBookmarksWebServiceJsonText() const {
    return Bookmark::bookmarksWebServiceJsonText(getParsedBookmarks());
}

/**
 * Returns the parsed bookmarks of the note
 *
 * @return
 */
QVector<Bookmark> Note::getParsedBookmarks() const {
    return Bookmark::parseBookmarks(noteText);
}

void Note::resetNoteTextHtmlConversionHash() {
    _noteTextHtmlConversionHash = QLatin1String("");
}

/**
 * Fetches all tags of the note
 */
// QList<Tag> Note::tags() {
//    return Tag::fetchAllOfNote(this);
//}

QDebug operator<<(QDebug dbg, const Note &note) {
    const NoteSubFolder noteSubFolder =
        NoteSubFolder::fetch(note.noteSubFolderId);
    dbg.nospace() << "Note: <id>" << note.id << " <name>" << note.name
                  << " <fileName>" << note.fileName << " <noteSubFolderId>"
                  << note.noteSubFolderId << " <relativePath>"
                  << noteSubFolder.relativePath() << " <hasDirtyData>"
                  << note.hasDirtyData;
    return dbg.space();
}
