#pragma once

#include "masterdialog.h"
class QTreeWidgetItem;
class QEvent;

namespace Ui {
class StoredAttachmentsDialog;
}

class StoredAttachmentsDialog : public MasterDialog {
    Q_OBJECT

   public:
    explicit StoredAttachmentsDialog(QWidget *parent = 0);
    ~StoredAttachmentsDialog();

   protected:
    bool eventFilter(QObject *obj, QEvent *event);

   private slots:
    void on_fileTreeWidget_currentItemChanged(QTreeWidgetItem *current,
                                              QTreeWidgetItem *previous);

    void on_deleteButton_clicked();

    void on_insertButton_clicked();

    void on_openFileButton_clicked();

    void on_openFolderButton_clicked();

   private:
    Ui::StoredAttachmentsDialog *ui;

    static QString getFilePath(QTreeWidgetItem *item);
};