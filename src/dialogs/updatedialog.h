#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include "masterdialog.h"

namespace Ui {
class UpdateDialog;
}

class QAbstractButton;
class QNetworkReply;
class QNetworkAccessManager;

class UpdateDialog : public MasterDialog {
    Q_OBJECT

   public:
    explicit UpdateDialog(QWidget *parent = nullptr,
                          const QString &changesHtml = QString(),
                          const QString &releaseUrl = QString(),
                          const QString &releaseVersionString = QString(),
                          int releaseBuildNumber = 0);
    ~UpdateDialog();
    static bool isUpdateDialogOpen();
    int exec();

   public slots:
    void show();

   private slots:
    void dialogButtonClicked(QAbstractButton *button);
    void setIsUpdateDialogOpen(bool isOpen);
    void releaseDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void slotReplyFinished(QNetworkReply *reply);

   private:
    Ui::UpdateDialog *ui;
    QString releaseUrl;
    QString releaseVersionString;
    QNetworkAccessManager *_networkManager;
    QPushButton *_updateButton;

    enum ButtonRole {
        Unset,    // nothing was selected
        Update,
        Download,
        Skip,
        Disable,
        Cancel
    };

    void closeEvent(QCloseEvent *event);

    bool initializeUpdateProcess(const QString &filePath);

    bool initializeWindowsUpdateProcess(const QString &filePath);

    bool initializeMacOSUpdateProcess(const QString &releaseUrl);
};

#endif    // UPDATEDIALOG_H
