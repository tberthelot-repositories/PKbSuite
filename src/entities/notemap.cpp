#include "entities/notemap.h"

NoteMap::NoteMap() {
}

void NoteMap::addNoteToMap(Note note, QList<Note> linkedNotes) {
	_noteMap.insert(note, linkedNotes);
}
