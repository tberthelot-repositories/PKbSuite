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

QVector<kbGraphNode*> kbGraphWidget::_noteNodes;

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

void kbGraphWidget::GenerateKBGraph() {
    QMap<Note*, QSet<QString>> noteMap = NoteMap::getInstance()->getNoteMap();

    // Insert all nodes
    QMapIterator<Note*, QSet<QString>> iterator(noteMap);

    while (iterator.hasNext()) {
        Note* note = iterator.next().key();

        kbGraphNode* node = new kbGraphNode(note->getName(), this);

        _noteNodes << node;
        scene()->addItem(node);

        // Insert links
        foreach (QString targetNoteName, noteMap.value(note)) {
            kbGraphNode* targetNode = nodeFromNote(targetNoteName);
            if (targetNode && (targetNode->name() != node->name())) {
                kbGraphLink* link = new kbGraphLink(node, targetNode);
                node->addLink(link);
                link->adjust();
                scene()->addItem(link);
            }
        }

        if (node->getNumberOfLinks() > _maxLinkNumber)
            _maxLinkNumber = node->getNumberOfLinks();
        else
            _maxLinkNumber = 1;
    }

    float nodesCount = _noteNodes.size();

    // Initialize nodes positions on a circle
    float a = 0.f;
    float da = 2.f * M_PI / nodesCount;
    foreach (kbGraphNode* node, _noteNodes) {
        node->setPos(nodesCount * cos(a) * node->getNumberOfLinks() / _maxLinkNumber, nodesCount * sin(a) * node->getNumberOfLinks() / _maxLinkNumber);
        a += da;
    }

    // foreach (kbGraphNode* node, _noteNodes) {
    //     node->setPos((qreal) (rand() %20 + 100 * (1 - node->getNumberOfLinks() / _maxLinkNumber)) * qCos((rand() %360) * 2 * M_PI / 360), (qreal) (rand() %20 + 100 * (1 - node->getNumberOfLinks() / _maxLinkNumber)) * qCos((rand() %360) * 2 * M_PI / 360));
    // }
}

// TODO Check if has to be modified to take note graph into acount
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
                NoteMap* noteMap = NoteMap::getInstance();
                Note note = noteMap->fetchNoteByName(_pointedNode->name());
                if (note.getId() > 0)
                    _mainWindow->setCurrentNote(std::move(note));
                centerOn(_pointedNode);
                _pointedNode = nullptr;
            }
        }
        else {
            if (_pointedNode) {
                _pointedNode->setPos(newPt);
                _pointedNode->fix();
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
        timerId = startTimer(1000 / 1000);
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
        if (node != _pointedNode)     // TODO Check if it is needed to fix a node manually positionned
            if (node->advancePosition())
                itemsMoved = true;
    }

    if (_pointedNode)
        _pointedNode = nullptr;

    if (!itemsMoved) {
        killTimer(timerId);
        timerId = 0;
    }
}

kbGraphNode* kbGraphWidget::nodeFromNote(QString noteName) {
    foreach (kbGraphNode* node, _noteNodes) {
        if (node->name() == noteName)
            return node;
    }

    return nullptr;
}

void kbGraphWidget::scalingTime(qreal x) {
    qreal factor = 1.0 + qreal(_numScheduledScalings) / 300.0;
    scale(factor, factor);
}

void kbGraphWidget::animFinished() {
    if (_numScheduledScalings > 0)
        _numScheduledScalings--;
    else
        _numScheduledScalings++;
    sender()->~QObject();
}
