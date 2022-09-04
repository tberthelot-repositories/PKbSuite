#ifndef NOTEMAP_H
#define NOTEMAP_H

#pragma once

#include <QMap>
#include <QList>

using namespace std;

class Note;
class NoteSubFolder;

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
	QVector<Note> fetchAllNotesByNoteSubFolderId(int noteSubFolderId);
	QVector<int> fetchAllIdsByNoteSubFolderId(int noteSubFolderId);
	static QVector<Note> search(const QString &text);
	static QVector<QString> searchAsNameListInCurrentNoteSubFolder(const QString &text, bool searchInNameOnly = false);
	static QVector<QString> searchAsNameList(const QString &text, bool searchInNameOnly = false);
    static QVector<int> searchInNotes(QString search, bool ignoreNoteSubFolder = false, int noteSubFolderId = -1);
    static QStringList buildQueryStringList(QString searchString, bool escapeForRegularExpression = false, bool removeSearchPrefix = false);
	static bool isNameSearch(const QString &searchTerm);
    static QString removeNameSearchPrefix(QString searchTerm);
    static QStringList fetchNoteNamesInCurrentNoteSubFolder();
    static QStringList fetchNoteNames();
    static QStringList fetchNoteFileNames();
    static QVector<int> fetchAllIdsByNoteTextPart(const QString &textPart);

	static bool deleteAll();
    static int countAll();

    static int countByNoteSubFolderId(int noteSubFolderId = 0,
                                      bool recursive = false);

	Note* fillByFileName(const QString &fileName);

	QMap<Note*, QList<Note*>> getNoteMap();

private:
	NoteMap() {};

	NoteMap(const NoteMap&);
	NoteMap& operator=(const NoteMap&);

    static NoteMap* _instance;
	static QMap<Note*, QList<Note*>> _noteMap;
	void getLinkedNotes(Note* note);

	int _lastID = 0;
};

#endif
