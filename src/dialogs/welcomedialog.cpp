#include "welcomedialog.h"

#include <utils/misc.h>

#include <QFileDialog>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QSettings>

#include "QDebug"
#include "settingsdialog.h"
#include "ui_welcomedialog.h"

WelcomeDialog::WelcomeDialog(QWidget *parent)
    : MasterDialog(parent), ui(new Ui::WelcomeDialog) {
    ui->setupUi(this);
    ui->layoutWidget->setManualSettingsStoring(false);

    // if note layout has already been set, we can finish settings in the first
    // step
    QSettings settings;
    _allowFinishButton =
        settings.contains(QStringLiteral("workspace-initial/windowState"));
    ui->finishButton->setEnabled(_allowFinishButton);

    ui->backButton->setEnabled(false);
    ui->errorMessageLabel->setVisible(false);

    _notesPath = Utils::Misc::defaultNotesPath();
    ui->noteFolderLineEdit->setText(_notesPath);

    ui->stackedWidget->setCurrentIndex(WelcomePages::NoteFolderPage);
}

WelcomeDialog::~WelcomeDialog() { delete ui; }

void WelcomeDialog::on_cancelButton_clicked() {
    done(QDialog::Rejected);
}

void WelcomeDialog::on_nextButton_clicked() {
    int index = ui->stackedWidget->currentIndex();
    int maxIndex = ui->stackedWidget->count() - 1;

    if (index == WelcomePages::NoteFolderPage) {
        // try to create the note folder, if it failed stay on page
        if (!handleNoteFolderSetup()) {
            return;
        }
    }

    if (index < maxIndex) {
        index++;
        ui->stackedWidget->setCurrentIndex(index);
    }

    if (index == WelcomePages::MetricsPage) {
        QSettings settings;
        settings.setValue(QStringLiteral("appMetrics/notificationShown"), true);
    }

    if (index == WelcomePages::LayoutPage) {
        ui->layoutWidget->resizeLayoutImage();
    }

    ui->finishButton->setEnabled(_allowFinishButton);
    ui->backButton->setEnabled(true);
    ui->nextButton->setEnabled(index < maxIndex);
}

/**
 * Tries to create the note folder and store it in the settings
 */
bool WelcomeDialog::handleNoteFolderSetup() {
    ui->errorMessageLabel->setVisible(false);
    _allowFinishButton = false;
    QDir dir(_notesPath);
    if (dir.exists()) {
        // everything is all right, the path already exists
        _allowFinishButton = true;

        Utils::Misc::printInfo(
            QStringLiteral("Note path '%1' exists.").arg(_notesPath));
    } else {
        if (ui->createNoteFolderCheckBox->isChecked()) {
            Utils::Misc::printInfo(
                QString("Note path '%1' doesn't exist yet and will "
                        "be created.")
                    .arg(_notesPath));

            // mkpath should only return true if the path was created, but we
            // want to double-check because there were some troubles on Windows
            // see: https://github.com/pbek/PKbSuite/issues/951
            if (dir.mkpath(_notesPath)) {
                if (dir.exists()) {
                    // everything is all right, the path was now created
                    _allowFinishButton = true;

                    Utils::Misc::printInfo(
                        QStringLiteral("Note path '%1' was now created.")
                            .arg(_notesPath));
                } else {
                    qWarning() << "Cannot create note path with mkpath!";
                    showNoteFolderErrorMessage(
                        tr("Cannot create note path! You have to create "
                           "the note folder manually!"));
                }
            } else {
                qWarning() << "Cannot create note path!";
                showNoteFolderErrorMessage(tr("Cannot create note path!"));
            }
        } else {
            showNoteFolderErrorMessage(tr("This note path doesn't exist!"));
        }
    }

    if (_allowFinishButton) {
        storeNoteFolderSettings();
    }

    return _allowFinishButton;
}

/**
 * Shows an error message on the note path page
 */
void WelcomeDialog::showNoteFolderErrorMessage(const QString &message) {
    ui->errorMessageLabel->setText(message);
    ui->errorMessageLabel->setVisible(true);
}

/**
 * Stores the note path in the settings
 */
void WelcomeDialog::storeNoteFolderSettings() {
    QSettings settings;

    // make the path relative to the portable data path if we are in
    // portable mode
    settings.setValue(
        QStringLiteral("notesPath"),_notesPath);
    settings.setValue(QStringLiteral("showNoteSubFolders"),
                      ui->showNoteSubFoldersCheckBox->isChecked());
}

void WelcomeDialog::on_backButton_clicked() {
    int index = ui->stackedWidget->currentIndex();

    if (index > 0) {
        index--;
        ui->stackedWidget->setCurrentIndex(index);
    }

    ui->nextButton->setEnabled(true);
    ui->backButton->setEnabled(index > 0);
}

void WelcomeDialog::on_finishButton_clicked() {
    if (ui->stackedWidget->currentIndex() == WelcomePages::NoteFolderPage) {
        if (!handleNoteFolderSetup()) return;
    } else {
        ui->layoutWidget->storeSettings();
    }
    storeNoteFolderSettings();
    done(QDialog::Accepted);
}

void WelcomeDialog::on_noteFolderButton_clicked() {
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Please select the folder where your notes will get stored to"),
        _notesPath, QFileDialog::ShowDirsOnly);

    QDir d = QDir(dir);

    if (d.exists() && (!dir.isEmpty())) {
#ifdef Q_OS_WIN32
        // check if user chose a different drive in portable mode
        if (Utils::Misc::isInPortableMode() &&
            Utils::Misc::portableDataPath().toLower().at(0) !=
                dir.toLower().at(0)) {
            QMessageBox::information(
                this, tr("Note folder"),
                tr("Keep in mind that the note folder will be "
                   "stored relative to the directory where PKbSuite "
                   "resides in portable mode! So you need to stay on the "
                   "same drive."));

            return;
        }
#endif

        _notesPath = dir;
        ui->noteFolderLineEdit->setText(dir);
    }
}

void WelcomeDialog::on_ownCloudSettingsButton_clicked() {
    auto *dialog = new SettingsDialog(SettingsDialog::OwnCloudPage, this);
    dialog->exec();
}

void WelcomeDialog::on_networkSettingsButton_clicked() {
    auto *dialog = new SettingsDialog(SettingsDialog::NetworkPage, this);
    dialog->exec();
}

void WelcomeDialog::closeEvent(QCloseEvent *event) {
    MasterDialog::closeEvent(event);
}
