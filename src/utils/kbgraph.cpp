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
 *
 */

#include "kbgraph.h"
#include <QDir>
#include <QTextStream>
#include <QPainter>
#include <QRegularExpression>
#include <QGraphicsSceneMouseEvent>
#include <entities/note.h>
#include <mainwindow.h>
#include <QGraphicsView>
#include "math.h"

/*
 * kbGraph : main class to manage the graph of notes
*/
kbGraph::kbGraph(MainWindow* wnd, QGraphicsView* kbGraphView) {
    QGraphicsScene();
    _mainWindow = wnd;
    _pointedNode = nullptr;
    _initialPos = QPointF(-1, -1);
    _kbGraphView = kbGraphView;
}

void kbGraph::GenerateKBGraph(const QString noteFolder) {
    QDir dir = QDir(noteFolder);

    QStringList noteFiles = dir.entryList(QStringList() << "*.md" << "*.Md" << "*.MD", QDir::Files);
    foreach(QString filename, noteFiles) {
        QString fullFileName = noteFolder + "/" + filename;
        QFile noteFile(fullFileName);
        if (!noteFile.open(QIODevice::ReadOnly)) // | QIODevice::Text))
            return;

        kbGraphNode* node = new kbGraphNode(filename.left(filename.length() - 3));
        _noteNodes << node;
        noteFile.close();
    }

    foreach(kbGraphNode* node, _noteNodes) {
        QString fullFileName = noteFolder + "/" + node->name() + ".md";
        QFile noteFile(fullFileName);
        if (!noteFile.open(QIODevice::ReadOnly))
            return;

        QTextStream flux(&noteFile);
        QString fluxText = flux.readAll();

        QRegularExpression re = QRegularExpression(R"(\[([A-Za-zÀ-ÖØ-öø-ÿ_\s]*)\]\([AA-Za-zÀ-ÖØ-öø-ÿ_\s\d?%]*\.md\))");
        QRegularExpressionMatchIterator reIterator = re.globalMatch(fluxText);
        while (reIterator.hasNext()) {
            QRegularExpressionMatch reMatch = reIterator.next();
            QString targetNoteName = reMatch.captured(1);

            for (int i = 0; i < _noteNodes.size(); i++) {
                if (_noteNodes.at(i)->name() == targetNoteName) {
                    kbGraphLink* link = new kbGraphLink(node, _noteNodes.at(i));
                    link->adjust();
                    node->addLink(link);
                    addItem(link);

                    break;
                }
            }
        }

        if (node->getNumberOfLinks() > _maxLinkNumber)
            _maxLinkNumber = node->getNumberOfLinks();

        noteFile.close();
    }

    // Sort the vector to have heavier nodes first
    for (int i=0; i < _noteNodes.size(); i++) {
        for (int j=i + 1; j < _noteNodes.size(); j++) {
            if (_noteNodes.at(i)->getNumberOfLinks() < _noteNodes.at(j)->getNumberOfLinks())
                _noteNodes.swapItemsAt(i, j);
        }
    }

    // Position the nodes
    foreach(kbGraphNode* node, _noteNodes) {
        qDebug() << "Note en cours : " << node->name();
        node->positionChildNodes();
        addItem(node);
    }
}

void kbGraph::mousePressEvent(QGraphicsSceneMouseEvent *mouseEvent) {
    _pointedNode = nullptr;
    _initialPos = QPointF(-1, -1);
    if (mouseEvent->button() == Qt::LeftButton)
    {
        QGraphicsItem *item = itemAt(mouseEvent->scenePos(), QTransform());
        if (item) {
            _pointedNode = qgraphicsitem_cast<kbGraphNode*>(item);
            _initialPos = mouseEvent->screenPos();
        }
    }

    QGraphicsScene::mousePressEvent(mouseEvent);
}

void kbGraph::mouseReleaseEvent(QGraphicsSceneMouseEvent *mouseEvent) {
    if (mouseEvent->button() == Qt::LeftButton)
    {
        QPointF newPt = mouseEvent->screenPos();
        if ((_pointedNode) && (_initialPos == newPt)) {
            if (!_pointedNode->name().isEmpty()) {
                Note note = Note::fetchByName(_pointedNode->name());
                _mainWindow->setCurrentNote(std::move(note));
                _kbGraphView->centerOn(_pointedNode);
                _pointedNode = nullptr;
            }
            else
                _pointedNode = nullptr;
        }
    }

    QGraphicsScene::mouseReleaseEvent(mouseEvent);
}

/*
 * kbGraphNode : class representing each note in the graph
*/
kbGraphNode::kbGraphNode(QString note) : _noteName(note) {
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setFlag(ItemIsFocusable);
    setCacheMode(DeviceCoordinateCache);
    setZValue(10);

    _position = QPointF(rand() % 1500, rand() % 1500);
    setPos(_position);
    _positionned = false;

    _noteLinkCount = 0;
}

void kbGraphNode::addLink(kbGraphLink* link) {
    _noteLinks << link;
    _noteLinkCount++;
    link->adjust();
}

QString kbGraphNode::name() {
    return _noteName;
}

int kbGraphNode::getNumberOfLinks() const {
    return _noteLinkCount;
}

QRectF kbGraphNode::boundingRect() const
{
    return QRectF(_rectText);
}

void kbGraphNode::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    QFont font = painter->font();
    font.setPointSize(font.pointSize() * (1 + getNumberOfLinks() / 40));
    painter->setFont(font);
    QFontMetrics fontMetrics (font);
    _rectText = fontMetrics.boundingRect(_noteName);
    _rectText.setWidth(fontMetrics.horizontalAdvance(_noteName));
    _rectText.setHeight(fontMetrics.height());

    painter->setPen(QPen(Qt::lightGray, 0.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::white);
    painter->drawRect(_rectText);

    if (_noteLinks.size() == 0)
        painter->setPen(QPen(Qt::red, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    else if (_noteLinks.size() == 1)
        painter->setPen(QPen(Qt::darkYellow, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    else {

        painter->setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }

    painter->drawText(0, 0, _noteName);
}

QVariant kbGraphNode::itemChange(GraphicsItemChange change, const QVariant &value)
{
    switch (change) {
    case ItemPositionHasChanged:
        for (kbGraphLink *link : qAsConst(_noteLinks))
            link->adjust();
        update();
        break;
    default:
        break;
    };

    return QGraphicsItem::itemChange(change, value);
}

void kbGraphNode::setPositionFlag() {
    _positionned = true;
}

bool kbGraphNode::isPositionned() {
    return _positionned;
}


void kbGraphNode::positionChildNodes() {
    for (int i = 0; i < _noteLinkCount; i++) {
        kbGraphNode* dest = _noteLinks.at(i)->dest();

        int factorLink = (getNumberOfLinks() + dest->getNumberOfLinks()) / 2;
        qreal angle = 180 / getNumberOfLinks();
        qreal distance = sqrt(pow(pos().x(), 2) + pow(pos().y(), 2));
        if (!isPositionned()) {
            // Calculate position of new node

            if (factorLink) {
                qreal destX = pos().x() + distance * sin(angle * i) / factorLink;
                qreal destY = pos().y() + distance * cos(angle * i) / factorLink;

                dest->setPos(QPointF(destX, destY));
                dest->setPositionFlag();
            }
        }
    }
}

void kbGraphNode::calculateForces() {
    if (!scene() || scene()->mouseGrabberItem() == this) {
        _position = pos();
        return;
    }

    // Sum up all forces pushing this item away
    qreal xvel = 0;
    qreal yvel = 0;
    const QList<QGraphicsItem *> items = scene()->items();
    for (QGraphicsItem *item : items) {
        kbGraphNode* node = qgraphicsitem_cast<kbGraphNode*>(item);
        if (!node)
            continue;

        QPointF vec = mapToItem(node, 0, 0);
        qreal dx = vec.x();
        qreal dy = vec.y();
        double l = 2.0 * (dx * dx + dy * dy);
        if (l > 0) {
            xvel += (dx * 150.0) / l;
            yvel += (dy * 150.0) / l;
        }
    }

    // Now subtract all forces pulling items together
    double weight = (_noteLinks.size() + 1) * 10;
    for (const kbGraphLink* edge : qAsConst(_noteLinks)) {
        QPointF vec;
        if (edge->source() == this)
            vec = mapToItem(edge->dest(), 0, 0);
        else
            vec = mapToItem(edge->source(), 0, 0);
        xvel -= vec.x() / weight;
        yvel -= vec.y() / weight;
    }

    if (qAbs(xvel) < 0.1 && qAbs(yvel) < 0.1)
        xvel = yvel = 0;

    QRectF sceneRect = scene()->sceneRect();
    _position = pos() + QPointF(xvel, yvel);
    _position.setX(qMin(qMax(_position.x(), sceneRect.left() + 10), sceneRect.right() - 10));
    _position.setY(qMin(qMax(_position.y(), sceneRect.top() + 10), sceneRect.bottom() - 10));

}

bool kbGraphNode::advancePosition() {
    if (_position == pos())
        return false;

    setPos(_position);
    return true;
}

/*
 * kbGraphLink : class representing each relation between notes in the graph
*/
kbGraphLink::kbGraphLink(kbGraphNode* source, kbGraphNode* dest) : _source(source), _dest(dest) {
    setAcceptedMouseButtons(Qt::NoButton);

    _source->addLink(this);
    _dest->addLink(this);
    adjust();
}

void kbGraphLink::adjust()
{
    if (!_source || !_dest)
        return;

    QRectF rectSource = _source->boundingRect();
    QRectF rectDest = _dest->boundingRect();
    QLineF line(mapFromItem(_source, (rectSource.left() + rectSource.width()) / 2, (rectSource.top() + rectSource.height()) / 2), mapFromItem(_dest, (rectDest.left() + rectDest.width()) / 2, (rectDest.top() + rectDest.height()) / 2));
    qreal length = line.length();

    prepareGeometryChange();

    if (length > qreal(20.)) {
        QPointF edgeOffset((line.dx() * 10) / length, (line.dy() * 10) / length);
        _sourcePoint = line.p1() + edgeOffset;
        _destPoint = line.p2() - edgeOffset;
    } else {
        _sourcePoint = _destPoint = line.p1();
    }
}

QRectF kbGraphLink::boundingRect() const
{
    if (!_source || !_dest)
        return QRectF();

    qreal penWidth = 1;
    qreal extra = (penWidth + _arrowSize) / 2.0;

    return QRectF(_sourcePoint, QSizeF(_destPoint.x() - _sourcePoint.x(),
                                      _destPoint.y() - _sourcePoint.y()))
        .normalized()
        .adjusted(-extra, -extra, extra, extra);
}

void kbGraphLink::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (!_source || !_dest)
        return;

    QLineF line(_sourcePoint, _destPoint);
    if (qFuzzyCompare(line.length(), qreal(0.)))
        return;

    // Draw the line itself
    painter->setPen(QPen(Qt::lightGray, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawLine(line);
}

kbGraphNode* kbGraphLink::source() const {
    return _source;
}

kbGraphNode* kbGraphLink::dest() const {
    return _dest;
}
