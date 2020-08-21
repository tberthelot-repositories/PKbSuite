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

#include "pdffile.h"
#include <QMessageBox>
#include "entities/notefolder.h"

#include <poppler/PDFDoc.h>
#include <poppler/Page.h>
#include <goo/GooString.h>
#include <QFileInfo>
#include <QDir>

PDFFile::PDFFile(QString fileToProcess) {
    _sFile = fileToProcess.simplified();
    _sFile.replace(" ", "_");
	_sDocumentFolder = "";
    _document = Poppler::Document::load(fileToProcess);
    if (!_document || _document->isLocked()) {
        delete _document;
    } else
        _nSheetDocument = _document->numPages();
}

PDFFile::~PDFFile() {
    if (_document)
        delete _document;
}

bool PDFFile::hasAnnotations()
{
    bool foundAnnotations = false;
    QSet<Poppler::Annotation::SubType> subtypes;  // Look for annotations
    subtypes.insert(Poppler::Annotation::AText);
    subtypes.insert(Poppler::Annotation::AHighlight);
    
    for (int iPage = 0; iPage < _nSheetDocument; iPage++) { 
        Poppler::Page* pageProcessed = _document->page(iPage);
        
        if (pageProcessed) {
            QList<Poppler::Annotation*> listPageAnnotations = pageProcessed->annotations(subtypes);
			QFileInfo fileInfo(_sFile);
			QString pageSummary = "";
			
            // TODO A finaliser
            for (int i = 0; i < listPageAnnotations.size(); i++) {
                if (listPageAnnotations.at(i)->subType() == Poppler::Annotation::AHighlight) { //  listPageAnnotations.at(i)->AHighlight) {
                    QSizeF sizePage = pageProcessed->pageSizeF();
                    
                    QColor annotColor = listPageAnnotations.at(i)->style().color();
                    
                    if ((annotColor.red() == 255) & (annotColor.green() == 255) & (annotColor.blue() == 0)) {		// Jaune : résumé
                        Poppler::HighlightAnnotation* highlightAnnotation = (Poppler::HighlightAnnotation*) listPageAnnotations.at(i);
                        for (int j = 0; j < highlightAnnotation->highlightQuads().size(); j++) {
                            Poppler::HighlightAnnotation::Quad quad = highlightAnnotation->highlightQuads().at(j);
                            
                            QRectF rectHighlight = QRectF(quad.points[0].x() * sizePage.width(), quad.points[0].y() * sizePage.height(), (quad.points[2].x() - quad.points[0].x())* sizePage.width(), (quad.points[2].y() - quad.points[0].y()) * sizePage.height());
							pageSummary += pageProcessed->text(rectHighlight) + " ";
                        }
                        while (pageSummary[pageSummary.size() -1] == ' ')
                            pageSummary.remove(pageSummary.size() - 1, 1);
                    }
                    else if ((annotColor.red() == 255) & (annotColor.green() == 0) & (annotColor.blue() == 0)) {        // Rouge : citation
                        Citation* citation = new Citation();
                        citation->page = iPage + 1;
                        citation->link = "#" + QString::number(iPage + 1);

						Poppler::HighlightAnnotation* highlightAnnotation = (Poppler::HighlightAnnotation*) listPageAnnotations.at(i);
                        for (int j = 0; j < highlightAnnotation->highlightQuads().size(); j++) {
                            Poppler::HighlightAnnotation::Quad quad = highlightAnnotation->highlightQuads().at(j);
                            
                            QRectF rectHighlight = QRectF(quad.points[0].x() * sizePage.width(), quad.points[0].y() * sizePage.height(), (quad.points[2].x() - quad.points[0].x())* sizePage.width(), (quad.points[2].y() - quad.points[0].y()) * sizePage.height());
                            citation->extract = citation->extract + pageProcessed->text(rectHighlight) + " ";
                        }
                        while (citation->extract[citation->extract.size() -1] == ' ')
                            citation->extract.remove(citation->extract.size() - 1, 1);
                        
                        _listCitations.append(citation);
                    }
                }
                else {
                    Comment* comment = new Comment();
                    comment->page = iPage + 1;
                    comment->link = "#" + QString::number(iPage + 1);
                    comment->text = listPageAnnotations.at(i)->contents() + "\n\n";
                    
                    _listComments.append(comment);
                }
                
                foundAnnotations = true;
            }
            
            if (pageSummary.length() > 0)
				_summary.text += "* " + pageSummary + " " + "[Page " + iPage + "]\n";
            delete pageProcessed;
        }
	}
    
    return foundAnnotations;
}

QString PDFFile::markdownSummary()
{
    QString sTmp = "";
    
    sTmp.append("## Résumé : \n");
    sTmp.append(_summary.text + "\n\n");
    sTmp.append("*--- Fin ---*\n\n");
    
    return sTmp;
}

QString PDFFile::markdownCitations(QString embedmentLink)
{
    QString sTmp = "## Citations :\n";
    
	embedmentLink.chop(1);
    for (int iCitation = 0; iCitation < _listCitations.size(); iCitation++) {
        sTmp.append("### Citation " + QString::number(iCitation + 1) + " \n");
        sTmp.append("Page : " + QString::number(_listCitations.at(iCitation)->page) + "   \n");
        sTmp.append(embedmentLink + _listCitations.at(iCitation)->link + ")\n\n");
        sTmp.append("Extrait : ");
        sTmp.append("*" + _listCitations.at(iCitation)->extract + "*\n\n");
        sTmp.append("*--- Fin ---*\n\n");
    }
    
    return sTmp;
}

QString PDFFile::markdownComments(QString embedmentLink)
{
    QString sTmp = "## Commentaires :\n";
    
	embedmentLink.chop(1);
    for (int iComment = 0; iComment < _listComments.size(); iComment++) {
        sTmp.append("### Commentaire " + QString::number(iComment + 1) + "   \n");
        sTmp.append("Page : " + QString::number(_listComments.at(iComment)->page) + "\n\n");
        sTmp.append(embedmentLink + _listComments.at(iComment)->link + ")\n\n");
        sTmp.append("Commentaire :\n\n");
        sTmp.append(_listComments.at(iComment)->text + "\n\n");
        sTmp.append("*--- Fin ---*\n\n");
    }
    
    return sTmp;
}

QString PDFFile::title() {
	return _document->info("Title");
}

QString PDFFile::author() {
	return _document->info("Author");
}

QString PDFFile::subject() {
	return _document->info("Subject");
}

QString PDFFile::keywords() {
	return _document->info("Keywords");
}
