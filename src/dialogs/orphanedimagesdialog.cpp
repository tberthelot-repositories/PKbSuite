#include "orphanedimagesdialog.h"

#include <entities/note.h>
#include <entities/notefolder.h>
#include <mainwindow.h>
#include <utils/gui.h>

#include <QDebug>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QKeyEvent>
#include <QTreeWidgetItem>
#include <QtWidgets/QMessageBox>
#include <QDirIterator>

#include "ui_orphanedimagesdialog.h"
#include "widgets/pkbsuitemarkdowntextedit.h"

OrphanedImagesDialog::OrphanedImagesDialog(QWidget *parent)
    : MasterDialog(parent), ui(new Ui::OrphanedImagesDialog) {
    ui->setupUi(this);
    ui->fileTreeWidget->installEventFilter(this);

	QStringList orphanedFiles;

	QDirIterator iterator(NoteFolder::currentLocalPath(), QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		iterator.next();
		if (QFileInfo(iterator.filePath()).isFile())
			if ((QFileInfo(iterator.filePath()).suffix() == "jpg") | (QFileInfo(iterator.filePath()).suffix() == "Jpg") | (QFileInfo(iterator.filePath()).suffix() == "JPG") | (QFileInfo(iterator.filePath()).suffix() == "jpeg") | (QFileInfo(iterator.filePath()).suffix() == "Jpeg") | (QFileInfo(iterator.filePath()).suffix() == "JPEG") | (QFileInfo(iterator.filePath()).suffix() == "png") | (QFileInfo(iterator.filePath()).suffix() == "Png") | (QFileInfo(iterator.filePath()).suffix() == "PNG"))
				orphanedFiles << iterator.filePath();
	}	
    orphanedFiles.removeDuplicates();
	
    QVector<Note> noteList = Note::fetchAll();
    int noteListCount = noteList.count();

    ui->progressBar->setMaximum(noteListCount);
    ui->progressBar->show();

    Q_FOREACH (Note note, noteList) {
        QStringList embeddedFileList = note.getEmbedmentFileList();

        // remove all found images from the orphaned files list
        Q_FOREACH (QString fileName, embeddedFileList) {
            orphanedFiles.removeAll(fileName);
        }

        ui->progressBar->setValue(ui->progressBar->value() + 1);
    }

    ui->progressBar->hide();

    Q_FOREACH (QString fileName, orphanedFiles) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, fileName);
        item->setData(0, Qt::UserRole, fileName);

        QString filePath = getFilePath(item);
        QFileInfo info(filePath);
        item->setToolTip(
            0, tr("Last modified at %1").arg(info.lastModified().toString()));

        ui->fileTreeWidget->addTopLevelItem(item);
    }

    // jump to the first item
    if (orphanedFiles.count() > 0) {
        QKeyEvent *event =
            new QKeyEvent(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
        QApplication::postEvent(ui->fileTreeWidget, event);
    }
}

OrphanedImagesDialog::~OrphanedImagesDialog() { delete ui; }

/**
 * Shows the currently selected image
 *
 * @param current
 * @param previous
 */
void OrphanedImagesDialog::on_fileTreeWidget_currentItemChanged(
    QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    Q_UNUSED(previous);

    auto *scene = new QGraphicsScene(this);
    QString filePath = getFilePath(current);

    if (!filePath.isEmpty()) {
        scene->addPixmap(QPixmap(filePath));
    }

    ui->graphicsView->setScene(scene);
}

/**
 * Gets the file path of a tree widget item
 *
 * @param item
 * @return
 */
QString OrphanedImagesDialog::getFilePath(QTreeWidgetItem *item) {
    if (item == Q_NULLPTR) {
        return QString();
    }
/*    
	QStringList nameFilter(item->data(0, Qt::UserRole).toString());
	QStringList itemFile = QDir(NoteFolder::currentLocalPath()).entryList(nameFilter);

    QString fileName = itemFile.at(0);
    return fileName;
*/
	return item->data(0, Qt::UserRole).toString();
}

/**
 * Deletes selected images
 */
void OrphanedImagesDialog::on_deleteButton_clicked() {
    int selectedItemsCount = ui->fileTreeWidget->selectedItems().count();

    if (selectedItemsCount == 0) {
        return;
    }

    if (Utils::Gui::question(this, tr("Delete selected files"),
                             tr("Delete <strong>%n</strong> selected file(s)?",
                                "", selectedItemsCount),
                             QStringLiteral("delete-files")) !=
        QMessageBox::Yes) {
        return;
    }

    // delete all selected files
    Q_FOREACH (QTreeWidgetItem *item, ui->fileTreeWidget->selectedItems()) {
        QString filePath = getFilePath(item);
        bool removed = QFile::remove(filePath);

        if (removed) {
            delete item;
        }
    }
}

/**
 * Event filters
 *
 * @param obj
 * @param event
 * @return
 */
bool OrphanedImagesDialog::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (obj == ui->fileTreeWidget) {
            // delete the currently selected images
            if ((keyEvent->key() == Qt::Key_Delete) ||
                (keyEvent->key() == Qt::Key_Backspace)) {
                on_deleteButton_clicked();
                return true;
            }
            return false;
        }
    }

    return MasterDialog::eventFilter(obj, event);
}

void OrphanedImagesDialog::on_insertButton_clicked() {
#ifndef INTEGRATION_TESTS
    MainWindow *mainWindow = MainWindow::instance();
    if (mainWindow == Q_NULLPTR) {
        return;
    }
#else
    return;
#endif

    int selectedItemsCount = ui->fileTreeWidget->selectedItems().count();

    if (selectedItemsCount == 0) {
        return;
    }

    PKbSuiteMarkdownTextEdit *textEdit = mainWindow->activeNoteTextEdit();
    Note note = mainWindow->getCurrentNote();

    // insert all selected images
    Q_FOREACH (QTreeWidgetItem *item, ui->fileTreeWidget->selectedItems()) {
        QString filePath = getFilePath(item);
        QFileInfo fileInfo(filePath);
        QString embedmentUrlString =
            note.embedmentUrlStringForFileName(fileInfo.fileName());
        QString imageLink =
            "![" + fileInfo.baseName() + "](" + embedmentUrlString + ")\n";
        textEdit->insertPlainText(imageLink);
        delete item;
    }
}

QStringList listEmbeddedFiles(QDir folder) {
	QDirIterator iterator(folder.absolutePath(), QDirIterator::Subdirectories);
	QStringList listFiles;
	
	while (iterator.hasNext()) {
		listEmbeddedFiles(QDir(iterator.next()));
	}
	
	QStringList filters;
	filters << ".jpg" << "*.Jpg" << "*.JPG" << "*.jpeg" << "*.Jpeg" << "*.JPEG";
	listFiles += folder.entryList(filters);	

	return listFiles;
}
