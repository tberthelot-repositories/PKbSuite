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

kbGraphWidget::kbGraphWidget(QWidget *parent) : QGraphicsView(parent) {
}

void kbGraphWidget::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        _eventPos = event->position();

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

void kbGraphWidget::scalingTime(qreal x) {
    qreal factor = 1.0+ qreal(_numScheduledScalings) / 300.0;
    scale(factor, factor);
    centerOn(_eventPos);
}

void kbGraphWidget::animFinished() {
    if (_numScheduledScalings > 0)
        _numScheduledScalings--;
    else
        _numScheduledScalings++;
    sender()->~QObject();
}
