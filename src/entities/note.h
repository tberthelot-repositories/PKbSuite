#ifndef NOTE_H
#define NOTE_H

#include <utils/misc.h>

#include <QDateTime>

class Bookmark;
class NoteSubFolder;
class QRegularExpression;
class QFile;
class QUrl;
class QSqlQuery;

typedef enum mediaType {
	image,
	attachment,
	pdf
};
	
class Note {
   public:
    explicit Note();

    int getId() const;

    QString getName() const;

    QString getFileName() const;

    QString getNoteText() const;

    bool getHasDirtyData() const;

    void setHasDirtyData(const bool hasDirtyData);

    void setName(QString text);

    void setNoteText(QString text);

    static bool addNote(const QString &name, const QString &fileName,
                        const QString &text);

    static Note fetch(int id);

    static Note fetchByName(const QRegularExpression &regExp,
                            int noteSubFolderId = -1);

    static Note fetchByFileName(const QString &fileName,
                                int noteSubFolderId = -1);

    static Note fetchByFileName(const QString &fileName,
                                const QString &noteSubFolderPathData);

    static Note fetchByName(const QString &name, int noteSubFolderId = -1);

    static Note fetchByName(const QString &name,
        const QString &noteSubFolderPathData,
        const QString& pathDataSeparator = QStringLiteral("\n"));

    static QVector<Note> fetchAll(int limit = -1);

    static QVector<Note> fetchAllNotTagged(int activeNoteSubFolderId);

    static QVector<int> fetchAllNotTaggedIds();

    static int countAllNotTagged(int activeNoteSubFolderId = -1);

    static QVector<Note> search(const QString &text);

    static QVector<QString> searchAsNameListInCurrentNoteSubFolder(
        const QString &text, bool searchInNameOnly = false);

    static QVector<QString> searchAsNameList(const QString &text,
                                             bool searchInNameOnly = false);

    static QStringList fetchNoteNamesInCurrentNoteSubFolder();

    static QStringList fetchNoteNames();

    static QStringList fetchNoteFileNames();

    static Note noteFromQuery(const QSqlQuery &query);

    bool store();

    bool storeNewText(QString text);

    bool storeNoteTextFileToDisk();

    static QString defaultNoteFileExtension();

    static QStringList customNoteFileExtensionList(
        const QString &prefix = QString());

    static QString getFullFilePathForFile(const QString &fileName);

    QString getFilePathRelativeToNote(const Note &note) const;

    static int storeDirtyNotesToDisk(Note &currentNote,
                                     bool *currentNoteChanged = Q_NULLPTR,
                                     bool *noteWasRenamed = Q_NULLPTR);

    bool updateNoteTextFromDisk();

    friend QDebug operator<<(QDebug dbg, const Note &note);

    void createFromFile(QFile &file, int noteSubFolderId = 0);

    static bool deleteAll();

    bool fileExists() const;

    bool fileWriteable() const;

    bool exists() const;

    static bool noteIdExists(int id);

    bool refetch();

    Note fillFromQuery(const QSqlQuery &query);

    bool fillByFileName(const QString &fileName, int noteSubFolderId = -1);

    bool removeNoteFile();

    bool remove(bool withFile = false);

    QString toMarkdownHtml(const QString &notesPath, int maxImageWidth = 980,
                           bool forExport = false, bool base64Images = false);

    bool isFetched() const;

    bool copyToPath(const QString &destinationPath,
                    QString noteFolderPath = QString());

    bool moveToPath(const QString &destinationPath,
                    const QString &noteFolderPath = QString());

    static QString generateTextForLink(QString text);

    static qint64 qint64Hash(const QString &str);

    QUrl fullNoteFileUrl() const;

    QString fullNoteFilePath() const;

    QString fullNoteFileDirPath() const;

    static QString encodeCssFont(const QFont &refFont);

    QDateTime getFileLastModified() const;

    QDateTime getFileCreated() const;

    QDateTime getModified() const;

    static int countAll();

    static bool allowDifferentFileName();

    bool renameNoteFile(QString newName);

    QString fileNameSuffix() const;

    static QVector<int> searchInNotes(QString query,
                                      bool ignoreNoteSubFolder = false,
                                      int noteSubFolderId = -1);

    int countSearchTextInNote(const QString &search) const;

    static QStringList buildQueryStringList(
        QString searchString, bool escapeForRegularExpression = false);

    QString fileBaseName(bool withFullName = false);

    NoteSubFolder getNoteSubFolder() const;

    void setNoteSubFolder(const NoteSubFolder &noteSubFolder);

    void setNoteSubFolderId(int id);

    static QVector<Note> fetchAllByNoteSubFolderId(int noteSubFolderId);

    static QVector<int> noteIdListFromNoteList(const QVector<Note> &noteList);

    static int countByNoteSubFolderId(int noteSubFolderId = 0,
                                      bool recursive = false);

    int getNoteSubFolderId() const;

    bool isInCurrentNoteSubFolder() const;

    QString relativeNoteFilePath(QString separator = QString()) const;

    QString relativeNoteSubFolderPath() const;

    QString noteSubFolderPathData() const;

    bool isSameFile(const Note &note) const;

    QString relativeNoteFilePath(QString separator = "");

    qint64 getFileSize() const;

    static Note updateOrCreateFromFile(QFile &file,
                                       const NoteSubFolder &noteSubFolder);

    static QVector<int> fetchAllIds(int limit = -1, int offset = -1);

    QVector<int> findLinkedNoteIds() const;

    void handleNoteMoving(const Note &oldNote) const;

    static QString createNoteHeader(const QString &name);

	static QString createNoteFooter();
	
	QString currentEmbedmentFolder();

    QString getInsertEmbedmentMarkdown(QFile *file, mediaType type, bool copyFile, bool addNewLine = true,
                                   bool returnUrlOnly = false,
                                   QString title = QString());
	
    static bool scaleDownImageFileIfNeeded(QFile &file);

    QString downloadUrlToEmbedment(const QUrl &url, bool returnUrlOnly = false);

    QString importMediaFromBase64(
        QString &data, const QString &imageSuffix = QStringLiteral("dat"));

    bool canWriteToNoteFile();

    static QString generateNoteFileNameFromName(const QString &name);

    void generateFileNameFromName();

    QString textToMarkdownHtml(QString str, const QString &notesPath,
                               int maxImageWidth = 980, bool forExport = false,
                               bool base64Images = false);

    QStringList getEmbedmentFileList(bool onlyImages = false) const;

    static Note fetchByUrlString(const QString &urlString);

    static QVector<int> fetchAllIdsByNoteTextPart(const QString &textPart);

    QString getNotePreviewText(bool asHtml = false, int lines = 3) const;

    static QString generateMultipleNotesPreviewText(const QVector<Note> &notes);

    bool handleNoteTextFileName();

    QString getNoteIdURL() const;

    static QString cleanupFileName(QString name);

    static QString extendedCleanupFileName(QString name);

    QVector<Bookmark> getParsedBookmarks() const;

    QString getParsedBookmarksWebServiceJsonText() const;

    void resetNoteTextHtmlConversionHash();

    QString getFileURLFromFileName(QString fileName,
                                   bool urlDecodeFileName = false) const;

    static bool fileUrlIsNoteInCurrentNoteFolder(const QUrl &url);

    static QString fileUrlInCurrentNoteFolderToRelativePath(const QUrl &url);

    QString relativeFilePath(const QString &path) const;

    static Note fetchByFileUrl(const QUrl &url);

    static Note fetchByRelativeFilePath(const QString &relativePath);

    QString getNoteUrlForLinkingTo(const Note &note,
                                   bool forceLegacy = false) const;

    QString embedmentUrlStringForFileName(const QString &fileName) const;

    Note fetchByRelativeFileName(const QString &fileName) const;

    static Utils::Misc::ExternalImageHash *externalImageHash();

    static QString urlEncodeNoteUrl(const QString &url);

    static QString urlDecodeNoteUrl(QString url);

    QStringList getNoteTextLines() const;

    bool stripTrailingSpaces(int skipLine = -1);

    QString detectNewlineCharacters();
	
	void updateReferenceBySectionInLinkedNotes();

   protected:
    QString _name;
    QString _fileName;
    QString _noteTextHtml;
    QString _noteTextHtmlConversionHash;
    QString _noteText;
    bool _hasDirtyData;
    QDateTime _fileCreated;
    QDateTime _fileLastModified;
    QDateTime _created;
    QDateTime _modified;
    qint64 _fileSize;
    int _id;
    int _noteSubFolderId;

    static const QString getNoteURL(const QString &baseName);

    static const QString getNoteURLFromFileName(const QString &fileName);

    void restoreCreatedDate();
	
	void updateReferencedNote(QString linkedNotePath, QString currentNotePath);
};

Q_DECLARE_TYPEINFO(Note, Q_MOVABLE_TYPE);

#endif    // NOTE_H
