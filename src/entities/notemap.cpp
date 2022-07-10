#include "entities/notemap.h"
#include <QDir>
#include <qtextstream.h>
#include <QRegularExpression>

NoteMap::NoteMap() {
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

    int i = _noteMap.size();
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

Note* NoteMap::findNoteFromName(QString name) {
    QMapIterator<Note*, QList<Note*>> iterator(_noteMap);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.key()->getName() == name)
            return iterator.key();
    }

    return NULL;
}


QList<Note*> NoteMap::getLinkedNotes(Note* note) {
	/*
	 * - Search in the note body to identify all links (RegExp)
	 * - Look for the related notes in the _noteMap
	 * - Add the note to a QList
	 */

    QList<Note*> linkedNotes;

    QRegularExpression re = QRegularExpression(R"(\[([A-Za-zÀ-ÖØ-öø-ÿ_\s]*)\]\([AA-Za-zÀ-ÖØ-öø-ÿ_\s\d?%]*\.md\))");
    QRegularExpressionMatchIterator reIterator = re.globalMatch(note->getNoteText());
    while (reIterator.hasNext()) {
        QRegularExpressionMatch reMatch = reIterator.next();
        QString targetNoteName = reMatch.captured(1);

        Note* linkedNote = findNoteFromName(targetNoteName);
        if (linkedNote)
            linkedNotes << linkedNote;
    }

    return linkedNotes;
}
