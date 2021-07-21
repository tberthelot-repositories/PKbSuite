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

class kbGraphLink : public QGraphicsItem {
public:
    kbGraphLink(kbGraphNode* source, kbGraphNode* dest);

    void adjust();

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
    kbGraphNode(QString note);

    void addLink(kbGraphLink* link);
    QString name();
    int getNumberOfLinks();

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QString _noteName;
    QPointF _position;
    QVector<kbGraphLink*> _noteLinks;
    int _noteLinkCount;
    QRect _rectText;
};

class kbGraph : public QGraphicsScene {
public:
    kbGraph(MainWindow* wnd, QGraphicsView* kbGraphView);

    void GenerateKBGraph(const QString noteFolder);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *mouseEvent);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *mouseEvent);

private:
    QVector<kbGraphNode*> _noteNodes;
    int _maxLinkNumber;
    MainWindow* _mainWindow;
    QGraphicsView* _kbGraphView;

    kbGraphNode* _pointedNode;
    QPointF _initialPos;
};
