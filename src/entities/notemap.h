#ifndef NOTEMAP_H
#define NOTEMAP_H

#pragma once

#include "note.h"

#include <QMap>
#include <QList>

class NoteMap {
public:
	NoteMap();
	
	void addNoteToMap(Note* note, QList<Note>* linkedNotes);
private:
	QMap<Note*, QList<Note>*> _noteMap;
};

#endif
