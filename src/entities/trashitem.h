#pragma once

#include <QDateTime>
#include <QFile>
#include <QSqlQuery>
#include <QUrl>

class Note;
class NoteSubFolder;

class TrashItem {
   public:
    explicit TrashItem();

    int getId();

    QString getFileName();

    static TrashItem fetch(int id);

    static QList<TrashItem> fetchAll(int limit = -1);

    bool store();

    friend QDebug operator<<(QDebug dbg, const TrashItem &trashItem);

    bool fileExists();

    bool exists();

    bool refetch();

    bool fillFromQuery(const QSqlQuery &query);

    bool removeFile();

    bool remove(bool withFile = false);

    bool isFetched();

    QDateTime getCreated();

    static int countAll();

    QString fileBaseName(bool withFullName = false);

    NoteSubFolder getNoteSubFolder();

    void setNoteSubFolder(const NoteSubFolder &noteSubFolder);

    QString relativeNoteFilePath(QString separator = QString());

    QString getNoteSubFolderPathData();

    qint64 getFileSize();

    static TrashItem trashItemFromQuery(const QSqlQuery &query);

    static bool deleteAll();

    bool fillFromId(int id);

    static bool add(Note note);

    static bool add(Note *note);

    void setNote(Note note);

    void setNote(Note *note);

    static TrashItem prepare(Note *note);

    bool doTrashing();

    QString fullFilePath();

    QString loadFileFromDisk();

    bool restoreFile();

    QString restorationFilePath();

    static bool isLocalTrashEnabled();

    static bool expireItems();

    static QList<TrashItem> fetchAllExpired();

   protected:
    int id;
    QString fileName;
    qint64 fileSize;
    QString noteSubFolderPathData;
    int noteSubFolderId;
    QDateTime created;
    QString _fullNoteFilePath;
};
