#include "notedialog.h"

#include <entities/note.h>

#include <QDesktopServices>
#include <QSettings>
#include <QDebug>

#include "ui_notedialog.h"

NoteDialog::NoteDialog(QWidget *parent)
    : MasterDialog(parent), ui(new Ui::NoteDialog) {
    ui->setupUi(this);
    ui->textEdit->initSearchFrame(ui->searchFrame);
    ui->textEdit->setReadOnly(true);
    ui->tabWidget->setCurrentIndex(
        QSettings().value("NoteDialog/tabWidgetIndex").toInt());

    // set the note text view font
    QFont font;
    font.fromString(Utils::Misc::previewFontString());
    ui->noteTextView->setFont(font);
}

void NoteDialog::setNote(Note &note) {
    setWindowTitle(note.getName());

    ui->textEdit->setPlainText(note.getNoteText());
    ui->noteTextView->setHtml(note.toMarkdownHtml(getMainWindow()->getNotePath()));
}

NoteDialog::~NoteDialog() { delete ui; }

MainWindow* NoteDialog::getMainWindow() {
    const QWidgetList &list = QApplication::topLevelWidgets();

    for(QWidget * w : list){
        MainWindow *mainWindow = qobject_cast<MainWindow*>(w);
        if(mainWindow)
            return mainWindow;
    }

    return NULL;
}

void NoteDialog::on_noteTextView_anchorClicked(const QUrl &url) {
    qDebug() << __func__ << " - 'url': " << url;
    const QString scheme = url.scheme();

    if ((scheme == QStringLiteral("note") ||
         scheme == QStringLiteral("noteid") ||
         scheme == QStringLiteral("checkbox")) ||
        (scheme == QStringLiteral("file") &&
         MainWindow::fileUrlIsNoteInCurrentNoteFolder(url))) {
        return;
    }

    QDesktopServices::openUrl(url);
}

void NoteDialog::on_tabWidget_currentChanged(int index) {
    QSettings().setValue("NoteDialog/tabWidgetIndex", index);
}
