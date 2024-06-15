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

#pragma once

#include <QGraphicsView>
#include <utils/kbgraph.h>

class MainWindow;
class Note;

class kbGraphWidget : public QGraphicsView {
    Q_OBJECT

public:
    explicit kbGraphWidget(QWidget *parent = nullptr);

    void GenerateKBGraph();
    void addNoteToGraph(QString noteName);
    void removeNode(QString noteName);
    void itemMoved();
    void setMainWindowPtr(MainWindow* mainWindow);
    void centerOnNote(Note* note);
    kbGraphNode* getNodeFromNote(Note* note);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void mousePressEvent(QMouseEvent *mouseEvent) override;
    void mouseReleaseEvent(QMouseEvent *mouseEvent) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    static QVector<kbGraphNode*> _noteNodes;
    int _numScheduledScalings = 0;
    int timerId = 0;
    int _maxLinkNumber = 0;
    kbGraphNode* _pointedNode;
    QPointF _initialPos;
    MainWindow* _mainWindow = nullptr;
    bool _middleButtonPressed = false;
    int _panStartX;
    int _panStartY;

    kbGraphNode* nodeFromNote(QString noteName);

protected slots:
    void scalingTime(qreal x);
    void animFinished();
};

