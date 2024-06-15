#ifndef NOTEMAP_H
#define NOTEMAP_H

#pragma once

#include <QMap>
#include <QList>
#include <QUrl>

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
	QVector<Note> fetchAllNotes(int limit = -1);
	QVector<int> fetchAllIds(int limit = -1, int offset = -1);
	static QVector<Note> search(const QString &text);
	static QVector<QString> searchAsNameList(const QString &text, bool searchInNameOnly = false);
    static QVector<int> searchInNotes(QString search);
    static QStringList buildQueryStringList(QString searchString, bool escapeForRegularExpression = false, bool removeSearchPrefix = false);
	static bool isNameSearch(const QString &searchTerm);
    static QString removeNameSearchPrefix(QString searchTerm);
    static QStringList fetchNoteNames();
    static QStringList fetchNoteFileNames();
    static QVector<int> fetchAllIdsByNoteTextPart(const QString &textPart);
	static Note fetchByFileUrl(const QUrl &url);
    Note fetchByRelativeFileName(const QString &fileName) const;

	static bool deleteAll();
    static int countAll();
    static int countAllNotTagged();

	Note* fillByFileName(const QString &fileName);

	static QMap<Note*, QSet<QString>> getNoteMap();

private:
	NoteMap() {};

	NoteMap(const NoteMap&);
	NoteMap& operator=(const NoteMap&);

    static NoteMap* _instance;
	static QMap<Note*, QSet<QString>> _noteMap;
	void getLinkedNotes(Note* note);

	int _lastID = 0;
};

#endif
