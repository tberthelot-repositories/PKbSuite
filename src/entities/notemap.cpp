#include "entities/notemap.h"
#include <QDir>
#include <qtextstream.h>
#include <QRegularExpression>
#include "note.h"

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
        note->setName(filename.left(lastPoint));
        note->setFileName(filename);
        note->setFileSize(noteFile.size());
        note->setNoteText(flux.readAll());
        note->setHasDirtyData(false);
        note->setFileCreated(fileInfo.birthTime());
        note->setFileLastModified(fileInfo.lastModified());
        note->setModified(QDateTime::currentDateTime());
        note->setId(_noteMap.size() + 1);

        QList<Note*> listInit;
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
        _noteMap.insert(note, getLinkedNotes(note));
    }
    else {
        QMapIterator<Note*, QList<Note*>> iterator(_noteMap);
        while (iterator.hasNext()) {
            iterator.next();
            note = iterator.key();

            _noteMap.insert(note, getLinkedNotes(note));
        }
    }
}

void NoteMap::addNoteToMap(Note* note) {
	_noteMap.insert(note, getLinkedNotes(note));
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
    Note note;

    fillByFileName(fileName, &note);

    return note;
}

int NoteMap::fetchNoteIdByName(const QString& name) {
    return fetchNoteByFileName(name).getId();
}


bool NoteMap::fillByFileName(const QString& fileName, Note* note) {
    QMapIterator<Note*, QList<Note*>> iterator(_noteMap);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.key()->getFileName() == fileName) {
            note = iterator.key();
            note->setName(iterator.key()->getName());
            note->setFileName(iterator.key()->getFileName());
            note->setFileSize(iterator.key()->getFileSize());
            note->setNoteText(iterator.key()->getNoteText());
            note->setHasDirtyData(iterator.key()->getHasDirtyData());
            note->setFileCreated(iterator.key()->getFileCreated());
            note->setFileLastModified(iterator.key()->getFileLastModified());
            note->setModified(QDateTime::currentDateTime());
            note->setId(_noteMap.size() + 1);
        
            return true;
        }
    }

    return false;
}


QMap<Note*, QList<Note*>> NoteMap::getNoteMap() {
    return _noteMap;
}

QList<Note*> NoteMap::getLinkedNotes(Note* note) {
	/*
	 * - Search in the note body to identify all links (RegExp)
	 * - Look for the related notes in the _noteMap
	 * - Add the note to a QList
	 */

    QList<Note*> linkedNotes;

    if (_noteMap[note][0] == note)
        _noteMap[note].removeFirst();

    QRegularExpression re = QRegularExpression(R"(\[([A-Za-zÀ-ÖØ-öø-ÿ_\s]*)\]\([AA-Za-zÀ-ÖØ-öø-ÿ_\s\d?%]*\.md\))");
    QRegularExpressionMatchIterator reIterator = re.globalMatch(note->getNoteText());
    while (reIterator.hasNext()) {
        QRegularExpressionMatch reMatch = reIterator.next();
        QString targetNoteName = reMatch.captured(1);

        Note linkedNote = fetchNoteByName(targetNoteName);
        if (linkedNote.getId() > 0)
            linkedNotes << &linkedNote;
    }

    return linkedNotes;
}
