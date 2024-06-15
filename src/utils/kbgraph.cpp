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

#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <entities/note.h>
#include <mainwindow.h>
#include <QGraphicsView>
#include "math.h"
#include <widgets/kbgraphwidget.h>
#include <QtMath>

/*
 * kbGraphNode : class representing each note in the graph
*/
kbGraphNode::kbGraphNode(QString note, kbGraphWidget* graph) : _noteName(note) {
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setFlag(ItemIsFocusable);
    setCacheMode(DeviceCoordinateCache);
    setZValue(10);

    _graph = graph;

    _noteLinkCount = 0;
}

void kbGraphNode::addLink(kbGraphLink* link) {
    _noteLinks << link;
    _noteLinkCount++;
    link->adjust();
}

QVector<kbGraphLink *> kbGraphNode::linkList() {
    return _noteLinks;
}

bool kbGraphNode::linkToNodeExists(kbGraphNode* toNode) {
    foreach(kbGraphLink* link, linkList()) {
        if (link->dest() == toNode)
            return true;
    }

    return false;
}

bool kbGraphNode::reverseLinkExists(kbGraphNode* fromNode) {
    foreach(kbGraphLink* link, fromNode->linkList()) {
        if (link->dest() == this)
            return true;
    }

    return false;
}

QString kbGraphNode::name() {
    return _noteName;
}

int kbGraphNode::getNumberOfLinks() const {
    return (_noteLinkCount>0?_noteLinkCount:1);
}

float kbGraphNode::getCircleSize() const {
    return 8 * (1 + getNumberOfLinks());
}

QRectF kbGraphNode::boundingRect() const
{
    return QRectF(qMin(_rectText.x(), (_rectText.x() + _rectText.width()) / 2 - (int(getCircleSize()) / 2)), _rectText.y() - int(getCircleSize()) - 2.5, qMax(_rectText.width(), int(getCircleSize())), _rectText.height() + int(getCircleSize()) + 5);
}

void kbGraphNode::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    QFont font = painter->font();
    font.setPixelSize(24);
    painter->setFont(font);    QFontMetrics fontMetrics (font);
    _rectText = fontMetrics.boundingRect(_noteName);
    _rectText.setWidth(fontMetrics.horizontalAdvance(_noteName));
    _rectText.setHeight(fontMetrics.height());

    painter->setPen(QPen(Qt::lightGray, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::lightGray);
    painter->drawEllipse(QRectF(_rectText.x() + _rectText.width() / 2 - (getCircleSize() / 2), _rectText.y() - getCircleSize(), getCircleSize(), getCircleSize()));
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
        _graph->itemMoved();
     //   update();
        break;
    default:
        break;
    };

    return QGraphicsItem::itemChange(change, value);
}

void kbGraphNode::calculateForces() {
    // if (!scene() || scene()->mouseGrabberItem() == this) {
    //     _position = pos();
    //     return;
    // }
    //
    // // Sum up all forces pushing this item away
    // qreal xvel = 0;
    // qreal yvel = 0;
    // const QList<QGraphicsItem *> items = scene()->items();
    // for (QGraphicsItem *item : items) {
    //     kbGraphNode* node = qgraphicsitem_cast<kbGraphNode*>(item);
    //     if (node && node != this) {
    //         QPointF vec = mapToItem(node, 0, 0);
    //         qreal dx = vec.x();
    //         qreal dy = vec.y();
    //         double l2 = dx * dx + dy * dy;
    //         xvel += (dx * 15) / l2;
    //         yvel += (dy * 15) / l2;
    //     }
    // }
    //
    // // Now subtract all forces caused by links
    // for (const kbGraphLink* edge : qAsConst(_noteLinks)) {
    //     QPointF vec;
    //     if (edge->source() == this)
    //         vec = mapToItem(edge->dest(), 0, 0);
    //     else
    //         vec = mapToItem(edge->source(), 0, 0);
    //     xvel -= vec.x() / (edge->weight() * 100);
    //     yvel -= vec.y() / (edge->weight() * 100);
    // }
    //
    // if (qAbs(xvel) < 1 && qAbs(yvel) < 1)
    //     xvel = yvel = 0;
    //
    // _position = pos() + QPointF(xvel, yvel);
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
        double l2 = 10 * (dx * dx + dy * dy);
        if (!linkToNodeExists(node))
            l2 /= 1.5;
        if (l2 > 0) {
            xvel += (dx * 150) / l2;
            yvel += (dy * 150) / l2;
        }
    }

    // Now subtract all forces pulling items together
    double weight2 = (_noteLinks.size() + 1) * 100;
    for (const kbGraphLink* edge : qAsConst(_noteLinks)) {
        QPointF vec;
        if (edge->source() == this)
            vec = mapToItem(edge->dest(), 0, 0);
        else
            vec = mapToItem(edge->source(), 0, 0);
        xvel -= vec.x() / weight2;
        yvel -= vec.y() / weight2;
    }

    if (qAbs(xvel) < 1 && qAbs(yvel) < 1)
        xvel = yvel = 0;

    _position = pos() + QPointF(xvel, yvel);
}

bool kbGraphNode::advancePosition() {
    if (_position == pos())
        return false;
//     if (QLineF(_position, pos()).length() < 5)
//         return false;

    if (!_fixedPos)
        setPos(_position);

    return true;
}

void kbGraphNode::fix() {
    _fixedPos = true;
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
    QLineF line(mapFromItem(_source, rectSource.center().x(), rectSource.top() + _source->getCircleSize() / 2), mapFromItem(_dest, rectDest.center().x(), rectDest.top() + _dest->getCircleSize() / 2));
    qreal length = line.length();

    prepareGeometryChange();

    _sourcePoint = line.p1();
    _destPoint = line.p2();
}

int kbGraphLink::weight() const {
    return _source->getNumberOfLinks() + _dest->getNumberOfLinks();
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

kbGraphNode* kbGraphLink::source() const
{
    return _source;
}

kbGraphNode* kbGraphLink::dest() const
{
    return _dest;
}
