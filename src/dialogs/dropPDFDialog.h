#ifndef DROPPDFDIALOG_H
#define DROPPDFDIALOG_H

#include "masterdialog.h"

#include <QDialog>

namespace Ui {
class DropPDFDialog;
}

class DropPDFDialog : public MasterDialog {
    Q_OBJECT

public:
    static const int idLink = 2;
    static const int idCreateNote = 3;
    
    explicit DropPDFDialog(QWidget *parent = 0);
    ~DropPDFDialog();
	
	bool copyFileToKb();

private:
    Ui::DropPDFDialog *ui;
	bool copyFile = true;

private slots:
    void on_linkButton_clicked();
    void on_createNoteButton_clicked();
};

#endif // DROPPDFDIALOG_H
