/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2016  Thomas Berthelot <thomas.berthelot@gmail.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifndef PDFFILE_H
#define PDFFILE_H

#include <qt5/poppler-qt5.h>
#include <QString>

class Citation {
public:
    int page;
    QString link;
    QString extract;
};

class Comment {
public:
    int page;
    QString link;
    QString text;
};

class Summary {
public:
    QString text;
};

class PDFFile
{
public:
    PDFFile(QString fileToProcess);
    ~PDFFile();

    bool hasAnnotations();
	
	void setDocumentFolder(const QString strFolder) {_sDocumentFolder = strFolder;}
    
    QString markdownSummary();
    QString markdownCitations(QString strEmbedmentFolder);
    QString markdownComments(QString strEmbedmentFolder);

    QString title();
    QString author();
    QString subject();
    QString keywords();
    

private:
    QString _sFile;
	QString _sDocumentFolder;
    Poppler::Document* _document;
    int _nSheetDocument;
    
    QList<Comment*> _listComments;
    QList<Citation*> _listCitations;
    Summary _summary;
};

#endif // PDFFILE_H
