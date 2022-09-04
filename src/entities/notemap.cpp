#include "entities/notemap.h"
#include <QDir>
#include <qtextstream.h>
#include <QRegularExpression>
#include "note.h"
#include "notesubfolder.h"
#include <QMapIterator>

NoteMap* NoteMap::_instance = NULL;
QMap<Note*, QList<Note*>> NoteMap::_noteMap;

NoteMap* NoteMap::getInstance()
{
    if (!_instance) {
        _instance = new NoteMap();
    }

    return _instance;
}

void NoteMap::createNoteList(const QString noteFolder) {
    QDir dir = QDir(noteFolder);

    QStringList noteFiles = dir.entryList(QStringList() << "*.md" << "*.Md" << "*.MD", QDir::Files);
    foreach (QString filename, noteFiles) {
        QString fullFileName = noteFolder + "/" + filename;
        QFile noteFile(fullFileName);
        if (!noteFile.open(QIODevice::ReadOnly)) // | QIODevice::Text))
            return;

        QFileInfo fileInfo(noteFile);
        QTextStream flux(&noteFile);
        Note* note = new Note();

        const int lastPoint = filename.lastIndexOf(QLatin1Char('.'));
        note->setFileName(filename);
        note->setFileSize(noteFile.size());
        note->setNoteText(flux.readAll());
        note->setHasDirtyData(false);
        note->setFileCreated(fileInfo.birthTime());
        note->setFileLastModified(fileInfo.lastModified());
        note->setModified(QDateTime::currentDateTime());
        note->setId(++_lastID);
        note->setName(filename.left(lastPoint));

        static QList<Note*> listInit;

        listInit << note;
        _noteMap.insert(note, listInit);

// TODO     Check how to manage insertion in graph
//
//         kbGraphNode* node = new kbGraphNode(filename.left(filename.length() - 3), this);
//         _noteNodes << node;
//         scene()->addItem(node);
        noteFile.close();
    }
}

void NoteMap::updateNoteLinks(Note* note) {
    if (note) {
        getLinkedNotes(note);
        _noteMap.insert(note, _noteMap.value(note));
    }
    else {
        QMapIterator<Note*, QList<Note*>> iterator(_noteMap);
        while (iterator.hasNext()) {
            iterator.next();
            note = iterator.key();

            getLinkedNotes(note);
            _noteMap.insert(note, _noteMap.value(note));
        }
    }
}

void NoteMap::addNoteToMap(Note* note) {
    getLinkedNotes(note);
	_noteMap.insert(note, _noteMap.value(note));
}

void NoteMap::removeNote(Note* note) {
    QMapIterator<Note*, QList<Note*>> iterator(_noteMap);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.key()->getId() == note->getId())
            _noteMap.remove(iterator.key());
    }
}

Note NoteMap::fetchNoteByName(QString name) {
    QMapIterator<Note*, QList<Note*>> iterator(_noteMap);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.key()->getName() == name)
            return *iterator.key();
    }

    return Note();
}

Note NoteMap::fetchNoteById(int _id) {
    QMapIterator<Note*, QList<Note*>> iterator(_noteMap);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.key()->getId() == _id)
            return *iterator.key();
    }

    return Note();
}

Note NoteMap::fetchNoteByFileName(const QString &fileName) {
    return *fillByFileName(fileName);
}

int NoteMap::fetchNoteIdByName(const QString& name) {
    return fetchNoteByFileName(name).getId();
}

QVector<Note> NoteMap::fetchAllNotes(int limit) {
    QVector<Note> noteList;

    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });

    if (limit > 0)
        noteMapKeys = noteMapKeys.mid(0, limit);

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext())
        noteList.append(*iterator.next());

    return noteList;
}

QVector<int> NoteMap::fetchAllIds(int limit, int offset) {
    QVector<int> idList;

    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getId() < y->getId(); });

    noteMapKeys = noteMapKeys.mid((offset>0?offset:0), limit);

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext())
        idList.append(iterator.next()->getId());

    return idList;
}

QVector<Note> NoteMap::fetchAllNotesByNoteSubFolderId(int noteSubFolderId) {
    QVector<Note> noteList;

    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext())
        if (iterator.peekNext()->getNoteSubFolderId() == noteSubFolderId)
            noteList.append(*iterator.next());
        else
            iterator.next();

    return noteList;
}

QVector<int> NoteMap::fetchAllIdsByNoteSubFolderId(int noteSubFolderId) {
    QVector<int> idList;

    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext())
        if (iterator.peekNext()->getNoteSubFolderId() == noteSubFolderId)
            idList.append(iterator.next()->getId());
        else
            iterator.next();

    return idList;
}

QVector<Note> NoteMap::search(const QString& text) {
    QVector<Note> noteList;

    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext())
        if (iterator.peekNext()->getNoteText().contains(text))
            noteList.append(*iterator.next());
        else
            iterator.next();

    return noteList;
}

QVector<QString> NoteMap::searchAsNameListInCurrentNoteSubFolder(const QString& text, bool searchInNameOnly) {
    QVector<QString> nameList;

    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });

    const int noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext()) {
        Note* nextNote = iterator.peekNext();
        if ((nextNote->getNoteSubFolderId() == noteSubFolderId) && ((nextNote->getName().contains(text)) || (nextNote->getNoteText().contains(text))))
            nameList.append(iterator.next()->getName());
        else
            iterator.next();
    }

    return nameList;
}

QVector<QString> NoteMap::searchAsNameList(const QString& text, bool searchInNameOnly)
{
    QVector<QString> nameList;

    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });


    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext()) {
        Note* nextNote = iterator.peekNext();
        if ((nextNote->getName().contains(text)) || (nextNote->getNoteText().contains(text)))
            nameList.append(iterator.next()->getName());
        else
            iterator.next();
    }

    return nameList;
}

bool NoteMap::isNameSearch(const QString &searchTerm) {
    return searchTerm.startsWith(QStringLiteral("name:")) ||
           searchTerm.startsWith(QStringLiteral("n:"));
}

QString NoteMap::removeNameSearchPrefix(QString searchTerm) {
    static const QRegularExpression re("^(name:|n:)");
    return searchTerm.remove(re);
}

QVector<int> NoteMap::searchInNotes(QString search, bool ignoreNoteSubFolder, int noteSubFolderId) {
    auto noteIdList = QVector<int>();

    // get the active note subfolder _id if none was set
    if ((noteSubFolderId == -1) && !ignoreNoteSubFolder) {
        noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();
    }

    // build the string list of the search string
    const QStringList queryStrings = buildQueryStringList(std::move(search));

    QList<Note*> noteMapKeys = _noteMap.keys();

    QListIterator<Note*> iterator(noteMapKeys);
    if (!ignoreNoteSubFolder) {
        for (int i = 0; i < queryStrings.count(); i++) {
            QString queryString = queryStrings[i];
            while (iterator.hasNext()) {
                Note* nextNote = iterator.peekNext();
                if (isNameSearch(queryString)?((nextNote->getName().contains(removeNameSearchPrefix(queryString))) || (nextNote->getFileName().contains(removeNameSearchPrefix(queryString)))):(nextNote->getNoteText().contains(queryString)))
                    noteIdList.append(iterator.next()->getId());
                else
                    iterator.next();
            }
        }
    }
    else {
        for (int i = 0; i < queryStrings.count(); i++) {
            QString queryString = queryStrings[i];
            while (iterator.hasNext()) {
                Note* nextNote = iterator.peekNext();
                if ((nextNote->getNoteSubFolderId() == noteSubFolderId) && (isNameSearch(queryString)?((nextNote->getName().contains(removeNameSearchPrefix(queryString))) || (nextNote->getFileName().contains(removeNameSearchPrefix(queryString)))):(nextNote->getNoteText().contains(queryString))))
                    noteIdList.append(iterator.next()->getId());
                else
                    iterator.next();
            }
        }
    }

    return noteIdList;
}

QStringList NoteMap::fetchNoteNamesInCurrentNoteSubFolder() {
    const int noteSubFolderId = NoteSubFolder::activeNoteSubFolderId();

    QStringList nameList;
    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext()) {
        Note* nextNote = iterator.peekNext();
        if ((nextNote->getNoteSubFolderId() == noteSubFolderId) && (!nextNote->getName().isEmpty()))
            nameList.append(iterator.next()->getName());
        else
            iterator.next();
    }

    return nameList;
}

QStringList NoteMap::fetchNoteNames() {
    QStringList nameList;
    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext()) {
        Note* nextNote = iterator.peekNext();
        if (!nextNote->getName().isEmpty())
            nameList.append(iterator.next()->getName());
        else
            iterator.next();
    }

    return nameList;
}

QStringList NoteMap::fetchNoteFileNames() {
    QStringList filenameList;
    QList<Note*> noteMapKeys = _noteMap.keys();
    std::sort(noteMapKeys.begin(), noteMapKeys.end(), [](Note* const x,Note* const y){ return x->getFileLastModified() < y->getFileLastModified(); });

    QListIterator<Note*> iterator(noteMapKeys);
    while (iterator.hasNext()) {
        Note* nextNote = iterator.peekNext();
        if (!nextNote->getFileName().isEmpty())
            filenameList.append(iterator.next()->getFileName());
        else
            iterator.next();
    }

    return filenameList;
}

QVector<int> NoteMap::fetchAllIdsByNoteTextPart(const QString& textPart) {
    auto noteIdList = QVector<int>();


    QList<Note*> noteMapKeys = _noteMap.keys();

    QListIterator<Note*> iterator(noteMapKeys);

    while (iterator.hasNext()) {
        Note* nextNote = iterator.peekNext();
        if (nextNote->getNoteText().contains(textPart))
            noteIdList.append(iterator.next()->getId());
        else
            iterator.next();
    }

    return noteIdList;
}

bool NoteMap::deleteAll() {
    _noteMap.clear();

    return true;
}

int NoteMap::countAll() {
    return _noteMap.size();
}

int NoteMap::countByNoteSubFolderId(int noteSubFolderId, bool recursive) {
    QVector<int> noteSubFolderIdList;
    int countNotesByNoteSubFolderId = 0;

    if (recursive) {
        noteSubFolderIdList =
            NoteSubFolder::fetchIdsRecursivelyByParentId(noteSubFolderId);
    } else {
        noteSubFolderIdList << noteSubFolderId;
    }

    QList<Note*> noteMapKeys = _noteMap.keys();

    QListIterator<Note*> iterator(noteMapKeys);

    while (iterator.hasNext()) {
        Note* nextNote = iterator.next();
        if (noteSubFolderIdList.indexOf(nextNote->getNoteSubFolderId() != -1))
            countNotesByNoteSubFolderId++;
    }

    return countNotesByNoteSubFolderId;
}

QStringList NoteMap::buildQueryStringList(QString searchString, bool escapeForRegularExpression, bool removeSearchPrefix) {
    auto queryStrings = QStringList();

    // check for strings in ""
    static const QRegularExpression re(QStringLiteral("\"([^\"]+)\""));
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
    for (QString text : searchStringList) {
        if (removeSearchPrefix) {
            if (isNameSearch(text)) {
                text = removeNameSearchPrefix(text);
            }
        }

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

Note* NoteMap::fillByFileName(const QString& fileName) {
    QMapIterator<Note*, QList<Note*>> iterator(_noteMap);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.key()->getFileName() == fileName)
            return iterator.key();
    }

    return (Note*) false;
}

QMap<Note*, QList<Note*>> NoteMap::getNoteMap() {
    return _noteMap;
}

void NoteMap::getLinkedNotes(Note* note) {
	/*
	 * - Search in the note body to identify all links (RegExp)
	 * - Look for the related notes in the _noteMap
	 * - Add the note to a QList
	 */

    if (_noteMap[note][0] == note)
        _noteMap[note].removeFirst();

    QRegularExpression re = QRegularExpression(R"(\[([A-Za-zÀ-ÖØ-öø-ÿ_\s]*)\]\([AA-Za-zÀ-ÖØ-öø-ÿ_\s\d?%]*\.md\))");
    QRegularExpressionMatchIterator reIterator = re.globalMatch(note->getNoteText());
    while (reIterator.hasNext()) {
        QRegularExpressionMatch reMatch = reIterator.next();
        QString targetNoteName = reMatch.captured(1);

        Note linkedNote = fetchNoteByName(targetNoteName);
        if (linkedNote.getId() > 0)
            _noteMap[note] << &linkedNote;
    }
}
