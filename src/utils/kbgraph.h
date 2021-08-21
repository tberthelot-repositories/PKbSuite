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

#pragma once

#include <QObject>
#include <QGraphicsScene>
#include <QGraphicsItem>

class MainWindow;

class kbGraphNode;
class kbGraphWidget;

class kbGraphLink : public QGraphicsItem {
public:
    kbGraphLink(kbGraphNode* source, kbGraphNode* dest);

    void adjust();

    kbGraphNode* source() const;
    kbGraphNode* dest() const;

protected:
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    kbGraphNode* _source;
    kbGraphNode* _dest;
    QPointF _sourcePoint;
    QPointF _destPoint;
    qreal _arrowSize = 10;
};

class kbGraphNode : public QGraphicsItem {
public:
    kbGraphNode(QString note, kbGraphWidget* graph);

    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    void addLink(kbGraphLink* link);
    bool linkToNodeExists(kbGraphNode* toNode);
    bool reverseLinkExists(kbGraphNode* fromNode);
    QString name();
    int getNumberOfLinks() const;
    float getCircleSize() const;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void calculateForces();
    bool advancePosition();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QString _noteName;
    QPointF _position;
    QVector<kbGraphLink*> _noteLinks;
    int _noteLinkCount;
    QRect _rectText;
    kbGraphWidget* _graph;
};
