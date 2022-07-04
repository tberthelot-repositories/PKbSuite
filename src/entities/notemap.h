#ifndef NOTEMAP_H
#define NOTEMAP_H

#pragma once

#include <note.h>

#include <QMap>
#include <QList>

class NoteMap {
public:
	NoteMap();
private:
	QMap _noteMap<Note, QList<Note>>;
}

#endif
