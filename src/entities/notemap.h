#ifndef NOTEMAP_H
#define NOTEMAP_H

#pragma once

#include <QMap>
#include <QList>

using namespace std;

class Note;

class NoteMap {
public:
	static NoteMap* getInstance();
	
	void createNoteList(const QString noteFolder);
	void updateNoteLinks(Note* note = NULL);
	void addNoteToMap(Note* note);
	void removeNote(Note* note);

	Note fetchNoteByName(QString name);
	Note fetchNoteById(int _id);
	Note fetchNoteByFileName(const QString &fileName);
	int fetchNoteIdByName(const QString &name);

	bool fillByFileName(const QString &fileName, Note* note);

	QMap<Note*, QList<Note*>> getNoteMap();

private:
	NoteMap() {};

	NoteMap(const NoteMap&);
	NoteMap& operator=(const NoteMap&);

    static NoteMap* _instance;
	static QMap<Note*, QList<Note*>> _noteMap;
	QList<Note*> getLinkedNotes(Note* note);
};

#endif
