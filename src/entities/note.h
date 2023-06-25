#ifndef NOTE_H
#define NOTE_H

#include <utils/misc.h>

#include <QDateTime>
#include <QSet>

class Bookmark;
class QRegularExpression;
class QFile;
class QUrl;
class QSqlQuery;

typedef enum mediaType {
	image,
	attachment,
	pdf
} mediaType;
	
class Note {
   public:
    Note();

    int getId() const;

    void setId(const int id);

    QString getName() const;

    QString getFileName() const;
    void setFileName(QString filename);

    QString getNoteText() const;

    bool getHasDirtyData() const;

    void setHasDirtyData(const bool hasDirtyData);

    void setName(QString text);

    void setNoteText(QString text);

    static Note fetchByName(const QRegularExpression &regExp);

    static QVector<int> fetchAllNotTaggedIds();

    void addTag(QString tag);

    bool isTagged(QString tag);

    bool hasTags();

    void removeTag(QString tag);

    void removeAllTags();

    const QSet<QString> getTags();

    bool store();

    bool storeNewText(QString text);

    bool storeNoteTextFileToDisk(bool &currentNoteTextChanged);
    bool storeNoteTextFileToDisk();

    static QString defaultNoteFileExtension();

    static QString getFullFilePathForFile(const QString &fileName);

    QString getFilePathRelativeToNote(const Note &note) const;

    static int storeDirtyNotesToDisk(Note &currentNote,
                                     bool *currentNoteChanged = Q_NULLPTR,
                                     bool *noteWasRenamed = Q_NULLPTR,
                                     bool *currentNoteTextChanged = Q_NULLPTR);

    bool updateNoteTextFromDisk();

    friend QDebug operator<<(QDebug dbg, const Note &note);

    bool operator==(const Note &note) const;

    void createFromFile(QFile &file,
                        bool withNoteNameHook = false);

    bool fileExists() const;

    bool fileWriteable() const;

    bool exists() const;

    static bool noteIdExists(int id);

    bool refetch();

    bool removeNoteFile();

    bool remove(bool withFile = false);

    QString toMarkdownHtml(const QString &notesPath, int maxImageWidth = 980,
                           bool forExport = false, bool decrypt = true,
                           bool base64Images = false);

    bool isFetched() const;

    bool copyToPath(const QString &destinationPath,
                    QString noteFolderPath = QString());

    bool exportToPath(const QString &destinationPath, bool withAttachedFiles = false);

    bool moveToPath(const QString &destinationPath,
                    const QString &noteFolderPath = QString());

    static QString generateTextForLink(QString text);

    static qint64 qint64Hash(const QString &str);

    QUrl fullNoteFileUrl() const;

    QString fullNoteFilePath() const;

    QString fullNoteFileDirPath() const;

    static QString encodeCssFont(const QFont &refFont);

    QDateTime getFileLastModified() const;

    void setFileLastModified(QDateTime dateLastModified);

    QDateTime getFileCreated() const;

    void setFileCreated(QDateTime dateCreated);

    QDateTime getModified() const;

    void setModified(QDateTime dateModified);

    static bool allowDifferentFileName();

    bool renameNoteFile(QString newName);

    QString fileNameSuffix() const;

    int countSearchTextInNote(const QString &search) const;

    QString fileBaseName(bool withFullName = false);

    static QVector<int> noteIdListFromNoteList(const QVector<Note> &noteList);

    bool isSameFile(const Note &note) const;

    QString relativeNoteFilePath(QString separator = "");

    int getFileSize() const;

    void setFileSize(int fileSize);

    static Note updateOrCreateFromFile(QFile &file,
                                       bool withNoteNameHook = false);

    QVector<int> findLinkedNoteIds() const;

    bool handleNoteMoving(const Note &oldNote);

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

    QString importMediaFromDataUrl(const QString &dataUrl);

    bool canWriteToNoteFile();

    static QString generateNoteFileNameFromName(const QString &name);

    void generateFileNameFromName();

    QString textToMarkdownHtml(QString str, const QString &notesPath,
                               int maxImageWidth = 980, bool forExport = false,
                               bool base64Images = false);

    QStringList getEmbedmentFileList() const;

    bool hasMediaFiles();

    static Note fetchByUrlString(const QString &urlString);

    bool hasAttachments();

    QString getNotePreviewText(bool asHtml = false, int lines = 3) const;

    static QString generateMultipleNotesPreviewText(const QVector<Note> &notes);

    bool handleNoteTextFileName();

    QString getNoteIdURL() const;

    static QString cleanupFileName(QString name);

    static QString extendedCleanupFileName(QString name);

    QVector<Bookmark> getParsedBookmarks() const;

    void resetNoteTextHtmlConversionHash();

    QString getFileURLFromFileName(QString fileName,
                                   bool urlDecodeFileName = false,
                                   bool withFragment = false) const;

    static QString getURLFragmentFromFileName(const QString& fileName);

    QString relativeFilePath(const QString &path) const;

    QString getNoteUrlForLinkingTo(const Note &note,
                                   bool forceLegacy = false) const;

    QString embedmentUrlStringForFileName(const QString &fileName) const;

    bool updateRelativeAttachmentFileLinks();

    static Utils::Misc::ExternalImageHash *externalImageHash();

    static QString urlEncodeNoteUrl(const QString &url);

    static QString urlDecodeNoteUrl(QString url);

    QStringList getNoteTextLines() const;

    bool stripTrailingSpaces(int skipLine = -1);

    QString detectNewlineCharacters();
	
	void updateReferenceBySectionInLinkedNotes();

    QStringList getHeadingList();

   protected:
    int _id;
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
    int _fileSize;
    QSet<QString> _tagSet;

    static const QString getNoteURL(const QString &baseName);

    static const QString getNoteURLFromFileName(const QString &fileName);

    void restoreCreatedDate();
	
	void updateReferencedNote(QString linkedNotePath, QString currentNotePath);
};

Q_DECLARE_TYPEINFO(Note, Q_MOVABLE_TYPE);

#endif    // NOTE_H
