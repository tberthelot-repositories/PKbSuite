#ifndef SHAREDIALOG_H
#define SHAREDIALOG_H

#include <entities/note.h>

#include "masterdialog.h"

namespace Ui {
class ShareDialog;
}

class ShareDialog : public MasterDialog {
    Q_OBJECT

   public:
    explicit ShareDialog(const Note &note, QWidget *parent = 0);
    ~ShareDialog();

    void updateDialog();

   private slots:
    void on_linkCheckBox_toggled(bool checked);

    void on_editCheckBox_toggled(bool checked);

   private:
    Note note;
    Ui::ShareDialog *ui;
};

#endif    // SHAREDIALOG_H
