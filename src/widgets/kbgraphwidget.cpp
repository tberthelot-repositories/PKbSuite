/*
 * Copyright (c) 2021-... Thomas Berthelot -- <thomas.berthelot@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "kbgraphwidget.h"
#include <QWheelEvent>
#include <qtimeline.h>
#include <utils/kbgraph.h>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QGraphicsSceneMouseEvent>
#include <entities/note.h>
#include <mainwindow.h>
#include <QtMath>
#include <QScrollBar>

kbGraphWidget::kbGraphWidget(QWidget *parent) : QGraphicsView(parent) {
    QGraphicsScene *scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setScene(scene);
    setCacheMode(CacheBackground);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    scale(qreal(1.), qreal(1.));
    setMinimumSize(400, 400);
}

void kbGraphWidget::setMainWindowPtr(MainWindow* mainWindow) {
    _mainWindow = mainWindow;
}

void kbGraphWidget::updateLinks(kbGraphNode* node, QString textNote) {
    QRegularExpression re = QRegularExpression(R"(\[([A-Za-zÀ-ÖØ-öø-ÿ_\s]*)\]\([AA-Za-zÀ-ÖØ-öø-ÿ_\s\d?%]*\.md\))");
    QRegularExpressionMatchIterator reIterator = re.globalMatch(textNote);
    while (reIterator.hasNext()) {
        QRegularExpressionMatch reMatch = reIterator.next();
        QString targetNoteName = reMatch.captured(1);

        for (int i = 0; i < _noteNodes.size(); i++) {
            if (_noteNodes.at(i)->name() == targetNoteName) {
                if ((!node->linkToNodeExists(_noteNodes.at(i))) & (!node->reverseLinkExists(_noteNodes.at(i)))) {
                    kbGraphLink* link = new kbGraphLink(node, _noteNodes.at(i));
                    node->addLink(link);
                    link->adjust();
                    scene()->addItem(link);
                }

                break;
            }
        }
    }
}

kbGraphNode* kbGraphWidget::getNodeFromNote(Note* note) {
    foreach (kbGraphNode* node, _noteNodes) {
        if (node->name() == note->getName())
            return node;
    }

    return NULL;
}

void kbGraphWidget::GenerateKBGraph(const QString noteFolder) {
    QDir dir = QDir(noteFolder);

    QStringList noteFiles = dir.entryList(QStringList() << "*.md" << "*.Md" << "*.MD", QDir::Files);
    foreach (QString filename, noteFiles) {
        QString fullFileName = noteFolder + "/" + filename;
        QFile noteFile(fullFileName);
        if (!noteFile.open(QIODevice::ReadOnly)) // | QIODevice::Text))
            return;

        kbGraphNode* node = new kbGraphNode(filename.left(filename.length() - 3), this);
        _noteNodes << node;
        scene()->addItem(node);
        noteFile.close();
    }

    foreach (kbGraphNode* node, _noteNodes) {
        QString fullFileName = noteFolder + "/" + node->name() + ".md";
        QFile noteFile(fullFileName);
        if (!noteFile.open(QIODevice::ReadOnly))
            return;

        QTextStream flux(&noteFile);
        QString fluxText = flux.readAll();

        updateLinks(node, fluxText);

        if (node->getNumberOfLinks() > _maxLinkNumber)
            _maxLinkNumber = node->getNumberOfLinks();

        noteFile.close();
    }

    if (!_maxLinkNumber)
        _maxLinkNumber = 1;

    foreach (kbGraphNode* node, _noteNodes) {
        node->setPos((qreal) (rand() %20 + 100 * (1 - node->getNumberOfLinks() / _maxLinkNumber)) * qCos((rand() %360) * 2 * M_PI / 360), (qreal) (rand() %20 + 100 * (1 - node->getNumberOfLinks() / _maxLinkNumber)) * qSin((rand() %360) * 2 * M_PI / 360));
   }
}

void kbGraphWidget::addNoteToGraph(QString noteName) {
    kbGraphNode* node = new kbGraphNode(noteName, this);
    _noteNodes << node;
    scene()->addItem(node);

    itemMoved();
}

void kbGraphWidget::removeNode(QString noteName) {
    foreach (kbGraphNode* node, _noteNodes) {
        if (node->name() == noteName) {
            scene()->removeItem(node);
            _noteNodes.removeAt(_noteNodes.indexOf(node));
        }
    }
}


void kbGraphWidget::mousePressEvent(QMouseEvent *mouseEvent) {
    _pointedNode = nullptr;
    _initialPos = QPointF(-1, -1);
    if (mouseEvent->button() == Qt::LeftButton)
    {
        QGraphicsItem *item = scene()->itemAt(mapToScene(mouseEvent->pos()), QTransform());
        if (item) {
            _pointedNode = qgraphicsitem_cast<kbGraphNode*>(item);
            _initialPos = mapToScene(mouseEvent->pos());
        }
    }
    else if (mouseEvent->button() == Qt::MiddleButton)
    {
        _middleButtonPressed = true;
        _panStartX = mouseEvent->x();
        _panStartY = mouseEvent->y();
        setCursor(Qt::ClosedHandCursor);
        mouseEvent->accept();
    }

    update();
    QGraphicsView::mousePressEvent(mouseEvent);
}

void kbGraphWidget::mouseReleaseEvent(QMouseEvent *mouseEvent) {
    if (mouseEvent->button() == Qt::LeftButton)
    {
        QPointF newPt = mapToScene(mouseEvent->pos());
        if ((_pointedNode) && (_initialPos == newPt)) {
            if (!_pointedNode->name().isEmpty()) {
                Note note = Note::fetchByName(_pointedNode->name());
                _mainWindow->setCurrentNote(std::move(note));
                _pointedNode = nullptr;
            }
        }
    }
    else if (mouseEvent->button() == Qt::MiddleButton)
    {
        _middleButtonPressed = false;
        setCursor(Qt::ArrowCursor);
        mouseEvent->accept();
    }

    update();
    QGraphicsView::mouseReleaseEvent(mouseEvent);
}

void kbGraphWidget::mouseMoveEvent(QMouseEvent *event){
    if (_middleButtonPressed)
    {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (event->x() - _panStartX));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - (event->y() - _panStartY));
        _panStartX = event->x();
        _panStartY = event->y();
        event->accept();
        return;
    }

    update();
    QGraphicsView::mouseMoveEvent(event);
}

void kbGraphWidget::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        int numDegrees = event->angleDelta().y() / 8;
        int numSteps = numDegrees / 15; // see QWheelEvent documentation
        _numScheduledScalings += numSteps;
        if (_numScheduledScalings * numSteps < 0) // if user moved the wheel in another direction, we reset previously scheduled scalings
            _numScheduledScalings = numSteps;

        QTimeLine *anim = new QTimeLine(350, this);
        anim->setUpdateInterval(20);

        connect(anim, SIGNAL(valueChanged(qreal)), SLOT(scalingTime(qreal)));
        connect(anim, SIGNAL(finished()), SLOT(animFinished()));
        anim->start();
    }
    else
        QGraphicsView::wheelEvent(event);
}

void kbGraphWidget::centerOnNote(Note* note) {
    QList<QGraphicsItem*> sceneItems = scene()->items();
    foreach (QGraphicsItem* item, sceneItems) {
        kbGraphNode* itemNode = qgraphicsitem_cast<kbGraphNode*>(item);
        if (itemNode)
            if (itemNode->name() == note->getName()) {
                centerOn(item);
                exit;
            }
    }
}

void kbGraphWidget::itemMoved() {
    if (!timerId)
        timerId = startTimer(1000 / 500); // 25);
}

void kbGraphWidget::timerEvent(QTimerEvent *event) {
    Q_UNUSED(event);

    QList<kbGraphNode*> nodes;
    const QList<QGraphicsItem*> items = scene()->items();
    for (QGraphicsItem *item : items) {
        if (kbGraphNode* node = qgraphicsitem_cast<kbGraphNode*>(item))
            nodes << node;
    }

    for (kbGraphNode *node : qAsConst(nodes))
        node->calculateForces();

    bool itemsMoved = false;
    for (kbGraphNode *node : qAsConst(nodes)) {
        if (node->advancePosition())
            itemsMoved = true;
    }

    if (!itemsMoved) {
        killTimer(timerId);
        timerId = 0;
    }
}

void kbGraphWidget::scalingTime(qreal x) {
    qreal factor = 1.0+ qreal(_numScheduledScalings) / 300.0;
    scale(factor, factor);
}

void kbGraphWidget::animFinished() {
    if (_numScheduledScalings > 0)
        _numScheduledScalings--;
    else
        _numScheduledScalings++;
    sender()->~QObject();
}
