#include "dropPDFDialog.h"
#include "ui_dropPDFDialog.h"
#include "build_number.h"
#include "version.h"
#include "release.h"
#include <QFile>
#include <QDate>
#include <QTextStream>

DropPDFDialog::DropPDFDialog(QWidget *parent) :
    MasterDialog(parent),
    ui(new Ui::DropPDFDialog) {

    ui->setupUi(this);
}

DropPDFDialog::~DropPDFDialog() {
    delete ui;
}

void DropPDFDialog::on_linkButton_clicked() {
    done(idLink);
}

void DropPDFDialog::on_createNoteButton_clicked() {
	copyFile = ui->checkBoxCopy->isChecked();
    done(idCreateNote);
}

bool DropPDFDialog::copyFileToKb() {
	return copyFile;
}
