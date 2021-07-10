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

class kbGraphWidget : public QGraphicsView {
    Q_OBJECT

   public:
    explicit kbGraphWidget(QWidget *parent = nullptr);

   protected:
//    void resizeEvent(QResizeEvent *event) override;
//    bool eventFilter(QObject *obj, QEvent *event) override;

//    void contextMenuEvent(QContextMenuEvent *event) override;

   public slots:
//    void hide();

   signals:
//    void resize(QSize size, QSize oldSize);
};

