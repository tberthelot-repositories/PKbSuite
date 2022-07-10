#ifndef NOTEMAP_H
#define NOTEMAP_H

#pragma once

#include "note.h"

#include <QMap>
#include <QList>

using namespace std;

class NoteMap {
public:
	NoteMap();
	
	void createNoteList(const QString noteFolder);
	void updateNoteLinks(Note* note = NULL);
	void addNoteToMap(Note* note);

	Note* findNoteFromName(QString name);

private:
	QMap<Note*, QList<Note*>> _noteMap;


	QList<Note*> getLinkedNotes(Note* note);
};

#endif
