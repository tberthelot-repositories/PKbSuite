#ifndef TAGLIST_H
#define TAGLIST_H

#pragma once

#include <QMap>
#include <QSet>
#include "entities/note.h"

using namespace std;

class Tag;

class TagMap {
public:
    static TagMap* getInstance();
    QString getActiveTag() {return _activeTag;}
    void setActiveTag(QString tag) {_activeTag = tag;}

    bool tagExists(QString tag);
    void createTag(QString tag);
    void renameTag(QString tag, QString newName);
    void remove(QString tag);

	void updateTagList(Note* note);
    void addTagToNote(QString tag, Note* note);
    void removeTagFromNote(QString tag, Note* note);

    int tagCount() {return _tagMap.size(); }
    int taggedNotesCount(QString tag);

    QVector<QString> fetchAllTags();
    QVector<int> fetchAllLinkedNotes(QString tag);
    QVector<QString> fetchAllWithLinkToNoteNames(const QStringList &noteNameList);
    QString fetchByName(QString name, const bool startsWith);

private:
    TagMap() {};

    static TagMap* _instance;
    static QMap<QString, QSet<Note*>> _tagMap;

    QString _activeTag = "";
};

#endif
