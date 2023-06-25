#include "entities/tagmap.h"
#include <QRegularExpression>

TagMap* TagMap::_instance = NULL;
QMap<QString, QSet<Note*>> TagMap::_tagMap;

TagMap* TagMap::getInstance()
{
    if (!_instance) {
        _instance = new TagMap();
    }

    return _instance;
}

void TagMap::updateTagList(Note* note) {
	QRegularExpression re = QRegularExpression(R"([^A-Za-z]#([A-Za-zÀ-ÖØ-öø-ÿ_]|\d+[A-Za-zÀ-ÖØ-öø-ÿ_])[A-Za-zÀ-ÖØ-öø-ÿ0-9_]*)");       // Take care of accented characters
	QRegularExpressionMatchIterator reIterator = re.globalMatch(note->getNoteText());
	while (reIterator.hasNext()) {
		QRegularExpressionMatch reMatch = reIterator.next();
		QString tagName = reMatch.captured().right(reMatch.capturedLength() - 2);

		if (!_tagMap.contains(tagName)) {
			static QSet<Note*> noteList;
			noteList.insert(note);
			_tagMap.insert(tagName, noteList);
			note->addTag(tagName);
		}
		else {
			_tagMap[tagName].insert(note);

			if (!note->isTagged(tagName)) {
				note->addTag(tagName);
			}
		}
	}
}

bool TagMap::tagExists(QString tag) {
	return _tagMap.contains(tag);
}

void TagMap::createTag(QString tag) {
	_tagMap.insert(tag, QSet<Note*>());
}

void TagMap::renameTag(QString tag, QString newName) {
	_tagMap.insert(newName, _tagMap[tag]);
	_tagMap.remove(tag);
}

void TagMap::remove(QString tag) {
	_tagMap.remove(tag);
}


void TagMap::addTagToNote(QString tag, Note* note) {
	_tagMap[tag] << note;
}

void TagMap::removeTagFromNote(QString tag, Note* note) {
	_tagMap[tag].remove(note);
}

int TagMap::taggedNotesCount(QString tag) {
	return _tagMap[tag].size();
}

QVector<QString> TagMap::fetchAllTags() {
    QVector<QString> tagList;

    QList<QString> tagMapKeys = _tagMap.keys();

	QListIterator<QString> iterator(tagMapKeys);
    while (iterator.hasNext())
        tagList.append(iterator.next());

    return tagList;
}

QVector<int> TagMap::fetchAllLinkedNotes(QString tag) {
    QVector<int> idList;

    QSet<Note*> tagMapKeys = _tagMap[tag];
    QSetIterator<Note*> iterator(tagMapKeys);
    while (iterator.hasNext())
        idList.append(iterator.next()->getId());

    return idList;
}

QVector<QString> TagMap::fetchAllWithLinkToNoteNames(const QStringList& noteNameList) {
	QVector<QString> tagList;

    QList<QString> tagMapKeys = _tagMap.keys();

	QListIterator<QString> tagIterator(tagMapKeys);
    while (tagIterator.hasNext()) {
		QString tag = tagIterator.next();
		QSet<Note*> noteLinked = _tagMap[tag];
		// TODO Check if each note name of the Set is in noteNameList. If yes, add tag to tagList
		// Check if I should add a break to get tags inserted only once
		QSetIterator<Note*> noteIterator(noteLinked);
		while (noteIterator.hasNext()) {
			QString noteName = noteIterator.next()->getName();
			if (noteNameList.contains(noteName))
				tagList.append(tag);
		}
	}

	return tagList;
}

QString TagMap::fetchByName(QString name, const bool startsWith) {
	QList<QString> tagMapKeys = _tagMap.keys();

	const int searchedStringSize = name.length();

	QListIterator<QString> iterator(tagMapKeys);
    while (iterator.hasNext()) {
		QString key = iterator.next();
		if (!startsWith) {
			if (key == name)
				return key;
		} else {
			if (key.left(searchedStringSize) == name)
				return key;
		}
	}

	return "";
}

