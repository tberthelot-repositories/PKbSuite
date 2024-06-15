#include "entities/note.h"

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
#include <QTemporaryFile>
#include <QUrl>
#include <utility>

#include "entities/bookmark.h"
#include "helpers/codetohtmlconverter.h"

#include "libraries/md4c/md2html/render_html.h"
#include "libraries/md4c/md4c/md4c.h"

#include "notemap.h"
#include "tagmap.h"

Note::Note()
      : _id{0},
      _hasDirtyData{false},
      _fileSize{0} {}

int Note::getId() const { return this->_id; }

void Note::setId(const int id) {_id = id;};

QString Note::getName() const { return this->_name; }

QDateTime Note::getFileLastModified() const { return this->_fileLastModified; }

void Note::setFileLastModified(QDateTime dateLastModified) { _fileLastModified = dateLastModified; }

QDateTime Note::getFileCreated() const { return this->_fileCreated; }

void Note::setFileCreated(QDateTime dateCreated) { _fileCreated = dateCreated; }

QDateTime Note::getModified() const { return this->_modified; }

void Note::setModified(QDateTime dateModified) { _modified = dateModified; }

int Note::getFileSize() const { return this->_fileSize; }

void Note::setFileSize(int fileSize) { _fileSize = fileSize; }

QString Note::getFileName() const { return this->_fileName; }

void Note::setFileName(QString fileName) { this->_fileName = fileName; }

QString Note::getNoteText() const { return this->_noteText; }

void Note::setHasDirtyData(const bool _hasDirtyData) {
    this->_hasDirtyData = _hasDirtyData;
}

bool Note::getHasDirtyData() const { return this->_hasDirtyData; }

void Note::setName(QString text) { this->_name = std::move(text); }

void Note::setNoteText(QString text) { this->_noteText = std::move(text); }

bool Note::hasAttachments() {
    return !getEmbedmentFileList().empty();
}

bool Note::updateRelativeAttachmentFileLinks() {
    static const QRegularExpression re(
        QStringLiteral(R"((\[.*?\])\((.*attachments/(.+?))\))"));
    QRegularExpressionMatchIterator i = re.globalMatch(_noteText);
    bool textWasUpdated = false;
    QString newText = getNoteText();

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString filePath = match.captured(2);

        if (filePath.startsWith(QLatin1String("file://"))) {
            continue;
        }

        const QString wholeLinkText = match.captured(0);
        const QString titlePart = match.captured(1);
        const QString fileName = match.captured(3);

        filePath = embedmentUrlStringForFileName(fileName);
        newText.replace(wholeLinkText,
                        titlePart + QChar('(') + filePath + QChar(')'));
        textWasUpdated = true;
    }

    if (textWasUpdated) {
        storeNewText(std::move(newText));
    }

    return textWasUpdated;
}

/**
 * Fetches a note by note name with a regular expression
 *
 * @param regExp
 * @param noteSubFolderId if not set all notes will be searched
 * @return
 */
Note Note::fetchByName(const QRegularExpression &regExp) {
    NoteMap* noteMap = NoteMap::getInstance();
    const QVector<Note> noteList = noteMap->fetchAllNotes();

    // since there is no regular expression search in Qt's sqlite
    // implementation we have to iterate
    for (const Note &note : noteList) {
        const bool match = regExp.match(note.getName()).hasMatch();
        if (match) {
            return note;
        }
    }

    return Note();
}

bool Note::remove(bool withFile) {
    NoteMap::getInstance()->removeNote(this);

    if (withFile) {
        this->removeNoteFile();

        // remove all links to tags
        this->removeAllTags();
    }

    return true;
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
            destinationPath + QDir::separator() + this->_fileName;

        if (d.exists(destinationFileName)) {
            qDebug() << destinationFileName << "already exists!";

            // find a new filename for the note
            const QDateTime currentDateTime = QDateTime::currentDateTime();
            destinationFileName = destinationPath + QDir::separator() +
                                  this->_name + QChar(' ') +
                                  currentDateTime.toString(Qt::ISODate)
                                      .replace(QChar(':'), QChar('_')) +
                                  QChar('.') + defaultNoteFileExtension();
        }

        // copy the note file to the destination
        const bool isFileCopied = file.copy(destinationFileName);
        if (!isFileCopied) {
            return false;
        }

        if (isFileCopied) {
            const QStringList embedmentFileList = getEmbedmentFileList();
			
			if (embedmentFileList.count() > 0) {

				if (currentEmbedmentFolder() != "") {	// The note has an embedment folder to copy
					if (noteFolderPath.isEmpty()) {
						noteFolderPath = destinationPath;
					}

					const QDir noteEmbedmentDir(noteFolderPath + QDir::separator() + getName().replace(" ", "_"));

					// created the note embedment folder if it doesn't exist
					if (!noteEmbedmentDir.exists()) {
						noteEmbedmentDir.mkpath(noteEmbedmentDir.path());
					}
			
					if (noteEmbedmentDir.exists()) {
						// copy all files to the note embedment folder inside
						// destinationPath
						bool successCopy = true;
						for (const QString &fileName : embedmentFileList) {
							QString tmp = this->fullNoteFilePath() + "/" + getName().replace(" ", "_") +
											QDir::separator() + fileName;
							QFile embeddedFile(fileName);
							QFileInfo fileInfo(fileName);

							if (embeddedFile.exists()) {
								successCopy &= embeddedFile.copy(noteEmbedmentDir.path() +
											QDir::separator() + fileInfo.fileName());
							}
						}
						
						return successCopy;
					}
                }
            }
        }
    }

    return false;
}

/**
 * Exports a note to destinationPath as Markdown file
 *
 * @param destinationPath of the note file (with note name and extension)
 * @param withAttachedFiles if true media files and attachments will be exported too
 * @return
 */
bool Note::exportToPath(const QString &destinationPath, bool withAttachedFiles) {
    auto noteText = getNoteText();
    QFile file(destinationPath);
    QFileInfo fileInfo(destinationPath);
    auto absolutePath = fileInfo.absolutePath();

    if (withAttachedFiles) {
        // check if there are media files in the note
        const QStringList mediaFileList = getEmbedmentFileList();

        if (!mediaFileList.empty()) {
            qDebug() << __func__ << " - 'mediaFileList': " << mediaFileList;

            // copy all images to the destination folder
            for (const QString &fileName : mediaFileList) {
                QFile mediaFile(currentEmbedmentFolder() + QDir::separator() +
                                fileName);

                if (mediaFile.exists()) {
                    mediaFile.copy(absolutePath + QDir::separator() + fileName);
                }
            }

            static const QRegularExpression re(QStringLiteral(R"((!\[.*?\])\(.*media/(.+?)\))"));
            QRegularExpressionMatchIterator i = re.globalMatch(noteText);

            // remove the "media/" part from the file names in the note text
            while (i.hasNext()) {
                QRegularExpressionMatch match = i.next();
                const QString wholeLinkText = match.captured(0);
                const QString titlePart = match.captured(1);
                const QString fileName = match.captured(2);

                noteText.replace(wholeLinkText,
                                titlePart + QStringLiteral("(./") + fileName + QChar(')'));
            }
        }

        // check if there are attachment files in the note
        const QStringList attachmentFileList = getEmbedmentFileList();

        if (!attachmentFileList.empty()) {
            qDebug() << __func__ << " - 'attachmentFileList': " << attachmentFileList;

            // copy all attachment to the destination folder
            for (const QString &fileName : attachmentFileList) {
                QFile attachmentFile(getName().replace(" ", "_") + QDir::separator() +
                                fileName);

                if (attachmentFile.exists()) {
                    attachmentFile.copy(absolutePath + QDir::separator() + fileName);
                }
            }

            static const QRegularExpression re(QStringLiteral(R"((\[.*?\])\(.*attachments/(.+?)\))"));
            QRegularExpressionMatchIterator i = re.globalMatch(noteText);

            // remove the "attachments/" part from the file names in the note text
            while (i.hasNext()) {
                QRegularExpressionMatch match = i.next();
                const QString wholeLinkText = match.captured(0);
                const QString titlePart = match.captured(1);
                const QString fileName = match.captured(2);

                noteText.replace(wholeLinkText,
                                titlePart + QStringLiteral("(./") + fileName + QChar(')'));
            }
        }
    }

    qDebug() << "exporting note file: " << destinationPath;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << file.errorString();

        return false;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << noteText;

    file.flush();
    file.close();
    Utils::Misc::openFolderSelect(destinationPath);

    return true;
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
 * Returns a list of all linked attachments of the current note
 * @return
 */
QStringList Note::getEmbedmentFileList() const
{
    const QString text = getNoteText();
    QStringList fileList;

    // match attachment links like [956321614](file://attachments/956321614.pdf)
    // or [956321614](attachments/956321614.pdf)
    static const QRegularExpression re(
        QStringLiteral(R"(\[.*?\]\(.*attachments/(.+?)\))"));
    QRegularExpressionMatchIterator i = re.globalMatch(text);

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        const QString fileName = match.captured(1);
        fileList << fileName;
    }

    return fileList;
}

/**
 * Gets a list of note ids from a note list
 */
QVector<int> Note::noteIdListFromNoteList(const QVector<Note> &noteList) {
    QVector<int> _idList;
    _idList.reserve(noteList.size());

    QVector<Note>::const_iterator i;
    for (i = noteList.constBegin(); i != noteList.constEnd(); ++i) {
        _idList.append((*i).getId());
    }
    return _idList;
}

/**
 * Returns all notes names that are not tagged
 */
QVector<int> Note::fetchAllNotTaggedIds() {
    NoteMap* noteMap = NoteMap::getInstance();
    QVector<Note> noteList = noteMap->fetchAllNotes();
    QVector<int> untaggedNoteIdList;
    untaggedNoteIdList.reserve(noteList.size());

    QVector<Note>::iterator it = noteList.begin();
    for (; it != noteList.end(); ++it) {
        if (!it->hasTags()) {
            untaggedNoteIdList << it->getId();
        }
    }

    return untaggedNoteIdList;
}

void Note::addTag(QString tag) {
    _tagSet.insert(tag);
    TagMap* tagMap = TagMap::getInstance();
    tagMap->addTagToNote(tag, this);
}

bool Note::isTagged(QString tag) {
    if (_tagSet.contains(tag))
        return true;

    return false;
}

bool Note::hasTags() {
    return (_tagSet.size() > 0);
}

void Note::removeTag(QString tag) {
    QString currentNoteText = getNoteText();
    currentNoteText.replace("#" + tag, "");         // TODO Take care of commas before or after #tags
    setNoteText(currentNoteText);

    _tagSet.remove(tag);
    TagMap* tagMap = TagMap::getInstance();
    tagMap->removeTagFromNote(tag, this);
}

void Note::removeAllTags() {
    foreach (QString tag, _tagSet) {
        removeTag(tag);
    }
}

const QSet<QString> Note::getTags() {
    return _tagSet;
}

int Note::countSearchTextInNote(const QString &search) const {
    return _noteText.count(search, Qt::CaseInsensitive);
}

bool Note::storeNewText(QString text) {
    if (!this->fileWriteable()) {
        return false;
    }

    this->_noteText = std::move(text);
    this->_hasDirtyData = true;

    return this->store();
}

/**
 * Returns the default note file extension (`md`, `txt` or custom extensions)
 */
QString Note::defaultNoteFileExtension() {
    return QSettings()
        .value(QStringLiteral("defaultNoteFileExtension"), QStringLiteral("md"))
        .toString();
}

/**
 * Checks if it is allowed to have a different note file name than the headline
 */
bool Note::allowDifferentFileName() {
    const QSettings settings;
    return settings.value(QStringLiteral("allowDifferentNoteFileName"))
        .toBool();
}

//
// inserts or updates a note object in the NoteMap
//
bool Note::store() {
    if (_fileName.isEmpty()) {
        // don't store notes with empty filename and empty name
        if (_name.isEmpty()) {
            return false;
        }

        generateFileNameFromName();
    }

    setModified(QDateTime::currentDateTime());

    // get the size of the note text
    const QByteArray bytes = _noteText.toUtf8();
    _fileSize = bytes.size();

    NoteMap* noteMap = NoteMap::getInstance();
    noteMap->addNoteToMap(this);

    return true;
}

/**
 * Stores a note text file to disk
 * The file name will be changed if needed
 *
 * @return true if note was stored
 */
bool Note::storeNoteTextFileToDisk() {
    bool currentNoteTextChanged = false;

    // We couldn't set a default parameter for currentNoteTextChanged, because that
    // would only work with a pointer and then the app would crash when
    // currentNoteTextChanged would get assigned a value
    return storeNoteTextFileToDisk(currentNoteTextChanged);
}

/**
 * Stores a note text file to disk
 * The file name will be changed if needed
 *
 * @param currentNoteTextChanged true if the note text was changed during a rename
 * @return true if note was stored
 */
bool Note::storeNoteTextFileToDisk(bool &currentNoteTextChanged) {
    const Note oldNote = *this;
    const QString oldName = _name;
    const QString oldNoteFilePath = fullNoteFilePath();

    handleNoteTextFileName();

    const QString newName = _name;
    bool noteFileWasRenamed = false;

    // rename the current note file if the file name changed
    if (oldName != newName) {
        QFile oldFile(oldNoteFilePath);

        // rename the note file
        if (oldFile.exists()) {
            noteFileWasRenamed = oldFile.rename(fullNoteFilePath());
            qDebug() << __func__
                     << " - 'noteFileWasRenamed': " << noteFileWasRenamed;

            // Restore the created date of the current note under Windows,
            // because it gets set to the current date when note is renamed
            restoreCreatedDate();
        }
    }

    QFile file(fullNoteFilePath());
    QFile::OpenMode flags = QIODevice::WriteOnly;
    const QSettings settings;
    const bool useUNIXNewline =
        settings.value(QStringLiteral("useUNIXNewline")).toBool();

    if (!useUNIXNewline) {
        flags |= QIODevice::Text;
    }

    qDebug() << "storing note file: " << this->_fileName;

    if (!file.open(flags)) {
        qCritical() << QObject::tr(
                           "Could not store note file: %1 - Error "
                           "message: %2")
                           .arg(file.fileName(), file.errorString());
        return false;
    }

    const bool fileExists = this->fileExists();

    // assign the tags to the new name if the name has changed
    if (oldName != newName) {
        // handle the replacing of all note urls if a note was renamed
        // (we couldn't make currentNoteTextChanged a pointer or the app would crash)
        currentNoteTextChanged = handleNoteMoving(oldNote);
    }

    const QString text = Utils::Misc::transformLineFeeds(this->_noteText);

    //    diff_match_patch *diff = new diff_match_patch();
    //    QList<Diff> diffList = diff->diff_main( this->_noteText, text );

    //    QString html = diff->diff_prettyHtml( diffList );
    //    diff->diff_cleanupSemantic( diffList );
    //    qDebug() << __func__ << " - 'diffList': " << diffList[0].toString();
    //    qDebug() << __func__ << " - 'html': " << html;

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << text;
    file.flush();
    file.close();

    this->_hasDirtyData = false;
    this->_fileLastModified = QDateTime::currentDateTime();

    if (!fileExists || !this->_fileCreated.isValid()) {
        this->_fileCreated = this->_fileLastModified;
    }

    const bool noteStored = this->store();

    // if note was stored but the note file wasn't renamed do some more checks
    // whether we need to remove the old note file
    if (noteStored && !noteFileWasRenamed) {
        QFile oldFile(oldNoteFilePath);
        const QFileInfo oldFileInfo(oldFile);
        const QFile newFile(fullNoteFilePath());
        const QFileInfo newFileInfo(newFile);

        // in the end we want to remove the old note file if note was stored and
        // filename has changed
        // #1190: we also need to check if the files are the same even if the
        // name is not the same for NTFS
        if ((fullNoteFilePath() != oldNoteFilePath) &&
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
                                  .arg(oldFile.fileName(),
                                       oldFile.errorString());
            }
        }
    }

    return noteStored;
}

/**
 * Restores the created date of the current note under Windows,
 * because it gets set to the current date when note is renamed
 *
 * See: https://github.com/pbek/QOwnNotes/issues/1743
 */
void Note::restoreCreatedDate() {
#ifdef Q_OS_WIN32
    // QDateTime to FILETIME conversion
    // see: https://stackoverflow.com/questions/19704817/qdatetime-to-filetime
    QDateTime origin(QDate(1601, 1, 1), QTime(0, 0, 0, 0), Qt::UTC);
    const qint64 _100nanosecs = 10000 * origin.msecsTo(_fileCreated);
    FILETIME fileTime;
    fileTime.dwLowDateTime = _100nanosecs;
    fileTime.dwHighDateTime = (_100nanosecs >> 32);

    LPCWSTR filePath =
        (const wchar_t *)QDir::toNativeSeparators(fullNoteFilePath()).utf16();
    HANDLE fileHandle = CreateFile(
        filePath, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    // set the created date to the old created date before the rename
    // see:
    // https://stackoverflow.com/questions/10041651/changing-the-file-creation-date-in-c-using-windows-h-in-windows-7
    SetFileTime(fileHandle, &fileTime, (LPFILETIME) nullptr,
                (LPFILETIME) nullptr);
    CloseHandle(fileHandle);
#endif
}

/**
 * Does a file name cleanup
 */
QString Note::cleanupFileName(QString name) {
    // remove characters from the name that are problematic
    static const QRegularExpression re(QStringLiteral(R"([\/\\:])"));
    name.remove(re);

    // remove multiple whitespaces from the name
    static const QRegularExpression re1(QStringLiteral("\\s+"));
    name.replace(re1, QStringLiteral(" "));

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
    static const QRegularExpression re(QStringLiteral(R"([\/\\:<>\"\|\?\*])"));
    name.replace(re, QStringLiteral(" "));

    return name;
}

/**
 * Checks if the filename has to be changed
 * Generates a new name and filename and removes the old file
 * (the new file is not stored to a note text file!)
 *
 * @return (bool) true if filename was changed
 */
bool Note::handleNoteTextFileName() {
    QString noteText = _noteText;

    // remove frontmatter from start of markdown text
    if (noteText.startsWith(QLatin1String("---"))) {
        static const QRegularExpression re(QStringLiteral(R"(^---\n.+?\n---\n)"), QRegularExpression::DotMatchesEverythingOption);
        noteText.remove(re);
    }

    // remove html comment from start of markdown text
    if (noteText.startsWith(QLatin1String("<!--"))) {
        static const QRegularExpression re(QStringLiteral(R"(^<!--.+?-->\n)"), QRegularExpression::DotMatchesEverythingOption);
        noteText.remove(re);
    }

    // split the text into a string list
    static const QRegularExpression re(QStringLiteral(R"((\r\n)|(\n\r)|\r|\n)"));
    const QStringList noteTextLines = noteText.trimmed().split(re);
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

    // remove a leading "# " for markdown headlines
    static const QRegularExpression headlinesRE(QStringLiteral("^#\\s"));
    name.remove(headlinesRE);

    // cleanup additional characters
    name = cleanupFileName(name);

    if (name.isEmpty()) {
        name = QObject::tr("Note");
    }

    // check if name has changed
    if (name != this->_name) {
        qDebug() << __func__ << " - 'name' was changed: " << name;
        QString fileName = generateNoteFileNameFromName(name);

        // only try to find a new name if the filename in lowercase has changed
        // to prevent troubles on case-insensitive filesystems like NTFS
        if (fileName.toLower() != this->_fileName.toLower()) {
            int nameCount = 0;
            const QString nameBase = name;

            // check if note with this filename already exists
            while (NoteMap::getInstance()->fetchNoteByFileName(fileName).isFetched()) {
                // find new filename for the note
                name = nameBase + QStringLiteral(" ") +
                       QString::number(++nameCount);
                fileName = generateNoteFileNameFromName(name);
                qDebug() << __func__ << " - 'override fileName': " << fileName;

                if (nameCount > 1000) {
                    break;
                }
            }
        }

        // set the new name and filename
        this->_name = name;
        generateFileNameFromName();

        // let's check if we would be able to write to the file
        if (!canWriteToNoteFile()) {
            qDebug() << __func__ << " - cannot write to file "
                     << this->_fileName << " - we will try another filename";

            // we try to replace some more characters (mostly for Windows
            // filesystems)
            name = extendedCleanupFileName(name);

            this->_name = name;
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
    _fileName = generateNoteFileNameFromName(_name);
}

/**
 * Checks if we can write to the note file
 *
 * @return
 */
bool Note::canWriteToNoteFile() {
    QFile file(fullNoteFilePath());
    const bool canWrite = file.open(QIODevice::WriteOnly);
    const bool fileExists = file.exists();

    if (file.isOpen()) {
        file.close();

        if (!fileExists) {
            file.remove();
        }
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

    // Update the last modified date
    QFileInfo fileInfo;
    fileInfo.setFile(file);
    this->_fileLastModified = fileInfo.lastModified();

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    this->_noteText = in.readAll();
    file.close();

    // strangely it sometimes gets null
    if (this->_noteText.isNull()) this->_noteText = QLatin1String("");

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

    // we need that for links to notes in subfolders in portable mode if
    // note folder lies outs_ide of the application directory
    // Warning: This also resolves symbolic links! If a folder that lies outs_ide
    //          of the note folder is linked into it as subfolder
    //          "canonicalFilePath" will show the original file path!
#ifdef Q_OS_WIN32
    // Let's stay with "canonicalFilePath" on Windows in case there is any issue
    // in portable mode
    const QString canonicalFilePath = fileInfo.canonicalFilePath();
#else
    // Don't resolve symbolic links
    const QString canonicalFilePath = fileInfo.absoluteFilePath();
#endif

    return canonicalFilePath;
}

QString Note::getFilePathRelativeToNote(const Note &note) const {
    const QDir dir(fullNoteFilePath());

    // for some reason there is a leading "../" too much
    static const QRegularExpression re(QStringLiteral(R"(^\.\.\/)"));
    QString path = dir.relativeFilePath(note.fullNoteFilePath()).remove(re);

    // if "note" is the current note we want to use the real filename
    if (path == QChar('.')) {
        path = note.getFileName();
    }

    return path;
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
        static const QRegularExpression re(QRegularExpression(R"([<>()])"));
        if (noteUrl.contains(re)) {
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
 * the note notemap if url came from a note link
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
    return _fileName;
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
 * @param currentNoteTextChanged true if the current note text was changed during a rename
 * @return amount of notes that were saved
 */
int Note::storeDirtyNotesToDisk(Note &currentNote, bool *currentNoteChanged,
                                bool *noteWasRenamed,
                                bool *currentNoteTextChanged) {

    QList<Note*> noteMapKeys = NoteMap::getInstance()->getNoteMap().keys();

    QListIterator<Note*> iterator(noteMapKeys);
    int count = 0;
    while (iterator.hasNext()) {
        Note* nextNote = iterator.peekNext();
        if (nextNote->getHasDirtyData()) {
            Note* note = iterator.next();
            const QString &oldName = note->getName();
            const bool noteWasStored = note->storeNoteTextFileToDisk(
            *currentNoteTextChanged);

            // continue if note couldn't be stored
            if (!noteWasStored) {
                continue;
            }

            const QString &newName = note->getName();

            // check if the file name has changed
            if (oldName != newName) {
                *noteWasRenamed = true;

                // overr_ide the current note because the file name has changed
                if (note->isSameFile(currentNote)) {
                    currentNote = *note;
                }

                // handle the replacing of all note urls if a note was renamed
                // we don't need to do that here, it would be called two
                // times this way
                //                Note::handleNoteRenaming(oldName, newName);
            }

            // reassign currentNote if filename of currentNote has changed
            if (note->isSameFile(currentNote)) {
                *currentNoteChanged = true;
            }

            qDebug() << "stored note: " << *note;
            count++;
        } else
            iterator.next();
    }

    return count;
}

/**
 * Strips trailing whitespaces off the note text
 *
 * @param skipLine skip that line (e.g. the current line)
 * @return
 */
bool Note::stripTrailingSpaces(int skipLine) {
    QStringList _noteTextLines = getNoteTextLines();
    const int lineCount = _noteTextLines.count();
    bool wasStripped = false;

    for (int l = 0; l < lineCount; l++) {
        if (l == skipLine) {
            continue;
        }

        const auto lineText = _noteTextLines.at(l);
        if (lineText.endsWith(QChar(' '))) {
            _noteTextLines[l] = Utils::Misc::rstrip(
                Utils::Misc::rstrip(lineText));
            wasStripped = true;
        }
    }

    if (wasStripped) {
        _noteText = _noteTextLines.join(detectNewlineCharacters());
        store();
    }

    return wasStripped;
}

QString Note::detectNewlineCharacters() {
    if (_noteText.contains(QStringLiteral("\r\n"))) {
        return QStringLiteral("\r\n");
    } else if (_noteText.contains(QStringLiteral("\n\r"))) {
        return QStringLiteral("\n\r");
    } else if (_noteText.contains(QStringLiteral("\r"))) {
        return QStringLiteral("\r");
    }

    return QStringLiteral("\n");
}

void Note::createFromFile(QFile &file, bool withNoteNameHook) {
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        in.setCodec("UTF-8");
#endif

        // qDebug() << file.size() << in.readAll();
        const QString _noteText = in.readAll();
        file.close();

        QFileInfo fileInfo;
        fileInfo.setFile(file);

        // create a nicer name by removing the extension
        QString name = fileInfo.fileName();

        const int lastPoint = name.lastIndexOf(QLatin1Char('.'));
        name = name.left(lastPoint);

        this->_name = std::move(name);
        this->_fileName = fileInfo.fileName();
        this->_noteText = _noteText;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
        this->_fileCreated = fileInfo.created();
#else
        this->_fileCreated = fileInfo.birthTime();
#endif

        this->_fileLastModified = fileInfo.lastModified();
        this->store();
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
                                  bool withNoteNameHook) {
    const QFileInfo fileInfo(file);
    Note note = NoteMap::getInstance()->fetchNoteByFileName(fileInfo.fileName());

    // regardless if the file was found or not, if the size differs or the
    // file was modified after the internal note was modified we want to load
    // the note content again
    if ((fileInfo.size() != note.getFileSize()) ||
        (fileInfo.lastModified() > note.getModified())) {
        // load file data and store note
        note.createFromFile(file);

        //        qDebug() << __func__ << " - 'file modified': " <<
        //        file.fileName();
    }

    return note;
}

/**
 * Checks if file of note exists in the filesystem and is readable
 *
 * @return bool
 */
bool Note::fileExists() const {
    return Utils::Misc::fileExists(fullNoteFilePath());
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
// checks if the current note still exists in the notemap
//
bool Note::exists() const { return noteIdExists(this->_id); }

bool Note::noteIdExists(int _id) { return NoteMap::getInstance()->fetchNoteById(_id)._id > 0; }

//
// reloads the current Note (by fileName)
//
bool Note::refetch() {
    return NoteMap::getInstance()->fillByFileName(_fileName);
}

/**
 * Returns the suffix of the note file name
 */
QString Note::fileNameSuffix() const {
    QFileInfo fileInfo;
    fileInfo.setFile(_fileName);
    return fileInfo.suffix();
}

/**
 * Returns the base name of the note file name
 */
QString Note::fileBaseName(bool withFullName) {
    if (withFullName) {
        QStringList parts = _fileName.split(QChar('.'));
        parts.removeLast();
        return parts.join(QChar('.'));
    }
    QFileInfo fileInfo;
    fileInfo.setFile(_fileName);
    return fileInfo.baseName();
}

/**
 * Renames a note file
 *
 * @param newName new file name (without file-extension)
 * @return
 */
bool Note::renameNoteFile(QString newName) {
    // cleanup not allowed characters
    newName = cleanupFileName(std::move(newName));

    // add the old file suffix to the name
    const QString newFileName = newName + QChar('.') + fileNameSuffix();

    // check if name has really changed
    if (_name == newName) {
        return false;
    }

    // check if name already exists
    NoteMap* noteMap = NoteMap::getInstance();
    const Note existingNote = noteMap->fetchNoteByName(newName);
    if (existingNote.isFetched() && (existingNote.getId() != _id)) {
        return false;
    }

    // Rename the embedment folder if exists
    QDir embedmendFolder(currentEmbedmentFolder());
	if (embedmendFolder.exists())
		embedmendFolder.rename(currentEmbedmentFolder(), fullNoteFileDirPath() + QDir::separator() + newName.replace(" ", "_"));

    // get the note file to rename it
    QFile file(fullNoteFilePath());

    // store the new note file name
    _fileName = std::move(newFileName);
    _name = std::move(newName);
    store();

    // rename the note file name
    return file.rename(fullNoteFilePath());
}

/**
 * Removes the file of the note + the embedment folder if it exists
 *
 * @return
 */
bool Note::removeNoteFile() {
    if (this->fileExists()) {
		QDir embedmentFolder(currentEmbedmentFolder());
		if (embedmentFolder.exists())
			embedmentFolder.removeRecursively();

        QFile file(fullNoteFilePath());
        qDebug() << __func__ << " - 'this->fileName': " << this->_fileName;
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
                           bool forExport, bool decrypt,
                           bool base64Images) {
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

static void captureHtmlFragment(const MD_CHAR *data, MD_SIZE data_size,
                                void *userData) {
    QByteArray *array = static_cast<QByteArray *>(userData);

    if (data_size > 0) {
        array->append(data, int(data_size));
    }
}

/**
 * @brief Converts code blocks to highlighted code
 */
static void highlightCode(QString &str, const QString &type, int cbCount) {
    if (cbCount >= 1) {
        const int firstBlock = str.indexOf(type, 0);
        int currentCbPos = firstBlock;
        for (int i = 0; i < cbCount; ++i) {
            // find endline
            const int endline = str.indexOf(QChar('\n'), currentCbPos);
            // something invalid? => just skip it
            if (endline == -1) {
                break;
            }
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
            if (next == -1) {
                break;
            }
            // extract the codeBlock
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 2)
            const QStringRef codeBlock =
                str.midRef(currentCbPos, next - currentCbPos);
#else
            QStringView str_view = str;
            QStringView codeBlock = str_view.mid(currentCbPos, next - currentCbPos);
#endif

            QString highlightedCodeBlock;
            if (!(codeBlock.isEmpty() && lang.isEmpty())) {
                const CodeToHtmlConverter c(lang);
                highlightedCodeBlock = c.process(codeBlock);
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

static inline int nonOverlapCount(const QString &str, const QChar c = '`') {
    const auto len = str.length();
    int count = 0;
    for (int i = 0; i < len; ++i) {
        if (str[i] == c && i + 2 < len && str[i + 1] == c && str[i + 2] == c) {
            ++count;
            i += 2;
        }
    }
    return count;
}

struct ImageSize {
    QString fileName;
    int size;
};
static std::vector<ImageSize> *getImageSizeCache()
{
    static std::vector<ImageSize> _imageSizesCache;
    if (_imageSizesCache.size() > 100)
        _imageSizesCache.erase(_imageSizesCache.begin());
    return &_imageSizesCache;
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
                     MD_FLAG_LATEXMATHSPANS | MD_FLAG_UNDERLINE;
    // we parse the task lists ourselves
    flags &= ~MD_FLAG_TASKLISTS;

    const QSettings settings;
    if (!settings
             .value(QStringLiteral("MainWindow/_noteTextView.underline"), true)
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
        static const QRegularExpression re(QStringLiteral(R"(^---\n.+?\n---\n)"), QRegularExpression::DotMatchesEverythingOption);
        str.remove(re);
    }

    /*CODE HIGHLIGHTING*/
    int cbCount = nonOverlapCount(str, '`');
    if (cbCount % 2 != 0) --cbCount;

    int cbTildeCount = nonOverlapCount(str, '~');
    if (cbTildeCount % 2 != 0) --cbTildeCount;

    // divide by two to get actual number of code blocks
    cbCount /= 2;
    cbTildeCount /= 2;

    highlightCode(str, QStringLiteral("```"), cbCount);
    highlightCode(str, QStringLiteral("~~~"), cbTildeCount);

    // parse for relative file urls and make them absolute
    // (for example to show images under the note path)
    static const QRegularExpression re(QStringLiteral(R"(([\(<])file:\/\/([^\/].+?)([\)>]))"));
    str.replace(re,
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
    static const QRegularExpression linkRE(QStringLiteral("<(((?!\\w+:\\/\\/)[^\\*<>\n])+\\.[\\w\\d]+)>"));
    i = linkRE.globalMatch(str);

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        const QString fileLink = match.captured(1);
        const QString url = Note::getFileURLFromFileName(fileLink, true, true);

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

        const QString url = Note::getFileURLFromFileName(fileLink, true, true);

        str.replace(match.captured(0), QStringLiteral("[") + fileText +
                                           QStringLiteral("](") + url +
                                           QStringLiteral(")"));
    }

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

    const QString fontString = Utils::Misc::previewCodeFontString();

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
                           "MainWindow/_noteTextView.ignoreCodeFontSize"),
                       true)
                .toBool()) {
            codeStyleSheet.remove(
                QRegularExpression(QStringLiteral(R"(font-size: \d+\w+;)")));
        }
    }

    const bool darkModeColors =
        !forExport && settings.value(QStringLiteral("darkModeColors")).toBool();

    const QString codeForegroundColor =
        darkModeColors ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    const QString codeBackgroundColor =
        darkModeColors ? QStringLiteral("#444444") : QStringLiteral("#f1f1f1");

    // do some more code formatting
    // the "pre" styles are for the full-width code block background color
    codeStyleSheet += QString(
                          "pre { display: block; background-color: %1;"
                          " white-space: pre-wrap } "
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
        settings.value(QStringLiteral("MainWindow/_noteTextView.rtl")).toBool();
    const QString rtlStyle =
        rtl ? QStringLiteral("body {text-align: right; direction: rtl;}")
            : QLatin1String("");

    if (forExport) {
        // get defined body font from settings
        const QString bodyFontString = Utils::Misc::previewFontString();

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
        const QString schemaStyles = Utils::Misc::isPreviewUseEditorStyles()
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

    // check if width of embedded local images is too high
    static const QRegularExpression imgRE(QStringLiteral("<img src=\"(file:\\/\\/[^\"]+)\""));
    i = imgRE.globalMatch(result);

    auto getImageSizeFromCache = [](const QString& image) {
        auto cache = getImageSizeCache();
        auto it = std::find_if(cache->begin(), cache->end(), [image](const ImageSize& i){
            return i.fileName == image;
        });
        return it == cache->end() ? -1 : it->size;
    };

    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        const QString fileUrl = match.captured(1);
        const QString fileName = QUrl(fileUrl).toLocalFile();

        int imageWidth = 0;

        // If file is greater than 1MB just limit its width already
        constexpr int OneMB = 1000 * 1000;
        if (QFileInfo(fileName).size() > OneMB) {
            imageWidth = maxImageWidth;
        }

        // try the cache
        if (imageWidth == 0) {
            imageWidth = getImageSizeFromCache(fileName);
        }

        // get image size using QImage and cache it
        if (imageWidth == -1) {
            const QImage image(fileName);
            imageWidth = image.width();
            getImageSizeCache()->push_back({fileName, imageWidth});
        }


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
            const int originalWidth = imageWidth;
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

bool Note::isFetched() const { return (this->_id > 0); }

/**
 * @brief Generates a text that can be used in a link
 * @param text
 * @return
 */
QString Note::generateTextForLink(QString text) {
    // replace everything but characters and numbers with "_"
    // we want to treat unicode characters as normal characters
    // to support links to notes with unicode characters in their names
    static const QRegularExpression re(QStringLiteral("[^\\d\\w]"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    text.replace(re, QStringLiteral("_"));

    // if there are only numbers we also want the "@" added, because
    // otherwise the text will get interpreted as ip address
    static const QRegularExpression re1 = QRegularExpression(QStringLiteral(R"(^(\d+)$)"));
    const QRegularExpressionMatch match = re1.match(text);
    bool addAtSign = match.hasMatch();

    if (!addAtSign) {
        // add a "@" if the text contains numbers and utf8 characters
        // because the url will be invalid then
        static const QRegularExpression re(QStringLiteral("\\d"));
        addAtSign = text.contains(re) &&
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

/*
 * Splits the text into a string list
 *
 * @return
 */
QStringList Note::getNoteTextLines() const {
    return _noteText.split(QRegExp(QStringLiteral(R"((\r\n)|(\n\r)|\r|\n)")));
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
 * Checks if the notes are the same (by file)
 *
 * @param note
 * @return
 */
bool Note::isSameFile(const Note &note) const {
    return (_fileName == note.getFileName());
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
    const QString linkText = getNoteURL(_name);
    NoteMap* noteMap = NoteMap::getInstance();
    noteIdList << noteMap->searchInNotes(QChar('<') + linkText + QChar('>'));
    noteIdList << noteMap->searchInNotes(
        QStringLiteral("](") + linkText + QStringLiteral(")"));

    // search vor legacy links ending with "@"
    const QString altLinkText =
        Utils::Misc::appendIfDoesNotEndWith(linkText, QStringLiteral("@"));
    if (altLinkText != linkText) {
        noteIdList << noteMap->searchInNotes(QChar('<') + altLinkText + QChar('>'));
        noteIdList << noteMap->searchInNotes(
            QStringLiteral("](") + altLinkText + QChar(')'));
    }

    const auto noteList = noteMap->fetchAllNotes();
    noteIdList.reserve(noteList.size());
    // search for links to the relative file path in all notes
    for (const Note &note : noteList) {
        const int noteId = note.getId();

        // we also want to search in the current note, but we want to skip the
        // search if we already have found it
        if (noteIdList.contains(noteId)) {
            continue;
        }

        const QString relativeFilePath =
            Note::urlEncodeNoteUrl(note.getFilePathRelativeToNote(*this));
        const QString _noteText = note.getNoteText();

        // search for links to the relative file path in note
        // the "#" is for notes with a fragment (link to heading in note)
        if (_noteText.contains(QStringLiteral("<") + relativeFilePath +
                              QStringLiteral(">")) ||
            _noteText.contains(QStringLiteral("](") + relativeFilePath +
                              QStringLiteral(")")) ||
            _noteText.contains(QStringLiteral("](") + relativeFilePath +
                              QStringLiteral("#"))) {
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
                                     bool urlDecodeFileName,
                                     bool withFragment) const {
    // Remove the url fragment from the filename
    const auto splitList = fileName.split(QChar('#'));
    fileName = splitList.at(0);
    const QString fragment = splitList.count() > 1 ? splitList.at(1) : "";

    if (urlDecodeFileName) {
        fileName = urlDecodeNoteUrl(fileName);
    }

    const QString path = this->getFullFilePathForFile(fileName);
    QString url = QUrl::fromLocalFile(path).toEncoded();

    if (withFragment && !fragment.isEmpty()) {
        url += QStringLiteral("#") + fragment;
    }

    return url;
}

QString Note::getURLFragmentFromFileName(const QString& fileName) {
    const auto splitList = fileName.split(QChar('#'));
    const QString fragment = splitList.count() > 1 ? splitList.at(1) : "";

    return QUrl::fromPercentEncoding(fragment.toLocal8Bit());
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
    static const QRegularExpression re(QStringLiteral(R"(^\.\.\/)"));
    return dir.relativeFilePath(path).remove(re);
}

/**
 * Handles the replacing of all note urls if the note was renamed or moved
 * (subfolder)
 *
 * @param oldNote
 * @return true if we had to change the current note
 */
bool Note::handleNoteMoving(const Note &oldNote) {
    const QVector<int> noteIdList = oldNote.findLinkedNoteIds();
    const int noteCount = noteIdList.count();

    if (noteCount == 0) {
        return false;
    }

    const QString oldUrl = getNoteURL(oldNote.getName());
    const QString newUrl = getNoteURL(_name);

    if (Utils::Gui::question(
            Q_NULLPTR, QObject::tr("Note file path changed"),
            QObject::tr("A change of the note path was detected. Would you "
                        "like to replace all occurrences of "
                        "<strong>%1</strong> links with "
                        "<strong>%2</strong> and links with filename "
                        "<strong>%3</strong> with <strong>%4</strong>"
                        " in <strong>%n</strong> note file(s)?",
                        "", noteCount)
                .arg(oldUrl, newUrl, oldNote.getFileName(), _fileName),
            QStringLiteral("note-replace-links")) == QMessageBox::Yes) {
        // replace the urls in all found notes
        for (const int noteId : noteIdList) {
            Note note = NoteMap::getInstance()->fetchNoteById(noteId);

            if (!note.isFetched()) {
                continue;
            }

            QString text = note.getNoteText();

            // replace legacy links with note://
            text.replace(QStringLiteral("<") + oldUrl + QStringLiteral(">"),
                         QStringLiteral("<") + newUrl + QStringLiteral(">"));
            text.replace(QStringLiteral("](") + oldUrl + QStringLiteral(")"),
                         QStringLiteral("](") + newUrl + QStringLiteral(")"));

            //
            // replace legacy links with note:// and ending @
            //
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

            //
            // replace non-urlencoded relative file links to the note
            //
            text.replace(
                QStringLiteral("<") + oldNoteRelativeFilePath +
                    QStringLiteral(">"),
                QStringLiteral("<") + relativeFilePath + QStringLiteral(">"));
            text.replace(
                QStringLiteral("](") + oldNoteRelativeFilePath +
                    QStringLiteral(")"),
                QStringLiteral("](") + relativeFilePath + QStringLiteral(")"));
            text.replace(
                QStringLiteral("](") + oldNoteRelativeFilePath +
                    QStringLiteral("#"),
                QStringLiteral("](") + relativeFilePath + QStringLiteral("#"));

            //
            // replace url encoded relative file links to the note
            //
            oldNoteRelativeFilePath = urlEncodeNoteUrl(oldNoteRelativeFilePath);
            text.replace(
                QStringLiteral("<") + oldNoteRelativeFilePath +
                    QStringLiteral(">"),
                QStringLiteral("<") + relativeFilePath + QStringLiteral(">"));
            text.replace(
                QStringLiteral("](") + oldNoteRelativeFilePath +
                    QStringLiteral(")"),
                QStringLiteral("](") + relativeFilePath + QStringLiteral(")"));
            text.replace(
                QStringLiteral("](") + oldNoteRelativeFilePath +
                    QStringLiteral("#"),
                QStringLiteral("](") + relativeFilePath + QStringLiteral("#"));

            // if the current note was changed we need to make sure the
            // _noteText is updated
            if (note.getId() == _id) {
                _noteText = text;
            }

            note.storeNewText(std::move(text));
        }
    }

    // return true if we had to change the current note
    return noteIdList.contains(_id);
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
 * Creates a note footer with the "Referenced by" section
 * 
**/
QString Note::createNoteFooter() {
	QString footer = QStringLiteral("\n## *Tags:*\n#LITERATURE, #PERMANENT, #INDEX, #TODO\n\n") + QStringLiteral("---\n\n*Body of the Zettel*\n\n---\n") + QStringLiteral("\n\n---\n## *Referenced by:*\n\n");
	return footer;
}

/**
 * Return the path of note's embedded item folder
 */
QString Note::currentEmbedmentFolder() {
	return fullNoteFileDirPath() + QDir::separator() + getName().replace(" ", "_");
}

/**
 * Returns the markdown of the inserted object/file into a note
 */
QString Note::getInsertEmbedmentMarkdown(QFile *file, mediaType type, bool copyFile, bool addNewLine,
                                     bool returnUrlOnly, QString title) {
    // file->exists() is false on Arch Linux for QTemporaryFile!
    if (file->size() > 0) {
		QString strTmp = file->fileName();
        const QFileInfo fileInfo(file->fileName());
		
		QString embedmentUrlString = "";
		QString newFilePath = "";
		if (copyFile) {
			// Test if note's embedment folder exists
			const QDir dir(currentEmbedmentFolder());
			// created the embedment folder if it doesn't exist
			if (!dir.exists()) {
				dir.mkpath(dir.path());
			}

			newFilePath = dir.path() + QDir::separator() + fileInfo.fileName().replace(" ", "_");

			// copy the file to the embedment folder
			file->copy(newFilePath);

			embedmentUrlString = embedmentUrlStringForFileName(fileInfo.fileName());

			// check if we only want to return the embedment url string
			if (returnUrlOnly) {
				return embedmentUrlString;
			}
		}
		else
			embedmentUrlString = fileInfo.absoluteFilePath().replace(" ", "%20");

        if (title.isEmpty()) {
            title = fileInfo.baseName();
        }

        // Fix for closig parenthesis
        embedmentUrlString.replace(")", "%29");
        
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
				strEmbedmentCode = QStringLiteral("[") + title + QStringLiteral("](file://") + embedmentUrlString + QStringLiteral(")");
			}
			break;
			case mediaType::pdf: {
				strEmbedmentCode = QStringLiteral("[") + title + QStringLiteral("](file://") + embedmentUrlString + QStringLiteral(")") + (addNewLine ? "\n" : "");
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
#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
                         .split(QStringLiteral("."), QString::SkipEmptyParts)
#else
                         .split(QStringLiteral("."), Qt::SkipEmptyParts)
#endif
                         .last();

    if (suffix.isEmpty()) {
        suffix = QStringLiteral("image");
    }

    // remove strings like "?b=16068071000" and non-characters from the suffix
    static const QRegularExpression re(QStringLiteral("\\?.+$"));
    static const QRegularExpression re1(QStringLiteral("[^a-zA-Z0-9]"));
    suffix.remove(re).remove(re1);

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
    auto *tempFile =
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
    const QString markdownCode = getInsertEmbedmentMarkdown(tempFile, mediaType::image, true);

    delete tempFile;

    return markdownCode;
}

/**
 * Tries to import a media file into the note and returns the code for the
 * markdown image tag
 */
QString Note::importMediaFromDataUrl(const QString &dataUrl) {
    if (dataUrl.startsWith(QLatin1String("data:image/"),
                           Qt::CaseInsensitive)) {
        QStringList parts = dataUrl.split(QStringLiteral(";base64,"));
        if (parts.count() == 2) {
            QString fileExtension = Utils::Misc::fileExtensionForMimeType(
                parts[0].mid(5));
            return importMediaFromBase64(parts[1], fileExtension);
        }
    }

    return "";
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

    QMimeDatabase db;
    QMimeType type = db.mimeTypeForFile(file);

    // we don't want to resize SVGs because Qt can't store them
    if (type.name().contains("image/svg")) {
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

    // don't scale if image is already small enough
    if (image.width() <= width && image.height() <= height) {
        return true;
    }


    const QPixmap &pixmap = QPixmap::fromImage(image.scaled(
        width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation));

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
    const QUrl url{urlString};

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

    Note note;

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
    static const QRegularExpression leRE(QStringLiteral("\r\n"));
    noteText.replace(leRE, QStringLiteral("\n"));

    if (!allowDifferentFileName()) {
        // remove headlines
        static const QRegularExpression re1(QStringLiteral("^.+\n=+\n+"));
        static const QRegularExpression re2(QStringLiteral("^# .+\n+"));
        noteText.remove(re1);
        noteText.remove(re2);
    }

    // remove multiple line breaks
    static const QRegularExpression re(QStringLiteral("\n\n+"));
    noteText.replace(re, QStringLiteral("\n"));

    const QStringList &lineList = noteText.split(QStringLiteral("\n"));

    if (lineList.isEmpty()) {
        return QLatin1String("");
    }

    noteText = QLatin1String("");

    // first line
    QString line = lineList.at(0).trimmed();
    line.truncate(80);
    noteText += line;

    const auto min = std::min(lines, lineList.count());
    for (int i = 1; i < min; i++) {
        noteText += QStringLiteral("\n");

        QString line = lineList.at(i).trimmed();
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
        const Note &note = notes.at(i);
        const QString oddStyle =
            isOdd ? QStringLiteral(" class='odd'") : QLatin1String("");
        const QDateTime modified = note.getFileLastModified();
        const QString _noteText = note.getNotePreviewText(true, 5);
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
                       _noteText +
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
 * Returns the parsed bookmarks of the note
 *
 * @return
 */
QVector<Bookmark> Note::getParsedBookmarks() const {
    return Bookmark::parseBookmarks(_noteText);
}

void Note::resetNoteTextHtmlConversionHash() {
    _noteTextHtmlConversionHash = QLatin1String("");
}

/**
 * Get a list of all headings in a note starting with ##
 *
 * @return
 */
QStringList Note::getHeadingList() {
    QStringList headingList;

    static const QRegularExpression re(QStringLiteral(R"(^##+ (.+)$)"),
                                       QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator i = re.globalMatch(_noteText);

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        headingList << match.captured(1);
    }

    return headingList;
}

/**
 * Fetches all tags of the note
 */
// QList<Tag> Note::tags() {
//    return Tag::fetchAllOfNote(this);
//}

QDebug operator<<(QDebug dbg, const Note &note) {
    dbg.nospace() << "Note: <id>" << note._id
                  << " <name>" << note._name
                  << " <fileName>" << note._fileName
                  << " <_hasDirtyData>" << note._hasDirtyData;
    return dbg.space();
}

void Note::updateReferencedBySectionInLinkedNotes() {
	QRegularExpression re = QRegularExpression(R"(([A-Za-zÀ-ÖØ-öø-ÿ0-9\%\s\*\_\-\.]*.md))");
	QRegularExpressionMatchIterator reIterator = re.globalMatch(_noteText);
	while (reIterator.hasNext()) {
		QRegularExpressionMatch reMatch = reIterator.next();
		QString linkedNote = reMatch.captured();

		updateReferencedNote(linkedNote.replace("%20", " "), _fileName);
	}	
}

void Note::updateReferencedNote(QString linkedNotePath, QString currentNotePath) {
	Note linkedNote = NoteMap::getInstance()->fetchNoteByFileName(linkedNotePath);
	QString text = linkedNote.getNoteText();

	if (text.length() != 0) {
		// First, look for the "Referenced by" section
		QRegularExpressionMatch match = QRegularExpression(R"(\n\n---\n## \*Referenced by:\*\n\n)").match(text);
		bool textModified = false;

        QString strReferences = "";
		// No "Referenced by" section yet. Let's create it
		if (!match.hasMatch()) {
			text.append(QStringLiteral("\n\n---\n## \*Referenced by:\*\n\n"));
                        textModified = true;
		}
		else {
            strReferences = text.right(match.capturedStart(text.length() - match.capturedStart()));
        }
		// Next, check if links are available and create/update them
		QString path = relativeFilePath(currentNotePath);

// TODO Remove when tested		match = QRegularExpression(R"(\*\s\[[A-Za-zÀ-ÖØ-öø-ÿ0-9\%\s\*\_\-\.]*\]\()" + path.replace(" ","%20") + R"(\))").match(strReferences);
		match = QRegularExpression(R"(\()" + path.replace(" ","%20") + R"(\))").match(strReferences);

		// No link to current note in "Referenced by" section yet, add it
		if (!match.hasMatch()) {
			text.append("* [" + _name + "](" + path.replace(" ", "%20") +")\n");
                        textModified = true;
		} 
		// TODO temporarily disable link update because it should be managed by the note move function or equivalent
/*		else { 	// Link is present, update it with current note path
			text.replace(QRegularExpression(R"(\*\s\[\([)" + _name + R"(\)\]\([A-Za-zÀ-ÖØ-öø-ÿ0-9\%\s]*.md))"), "* [\\1](" + path.replace(" ", "%20") + ")");
		}
*/
        if (textModified) {
            linkedNote.setNoteText(text);
            linkedNote.setHasDirtyData(true);
            linkedNote.store();
        }

	}
}

bool Note::operator==(const Note &note) const {
    return _id == note.getId() && _fileName == note.getFileName();
}
