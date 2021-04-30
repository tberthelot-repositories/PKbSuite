/*
 * Copyright (c) 2014-2021 Patrizio Bekerle -- <patrizio@bekerle.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 */

#include "misc.h"

#include <entities/note.h>
#include <entities/notefolder.h>
#include <entities/notesubfolder.h>
#include <services/databaseservice.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSettings>
#include <QStringBuilder>
#include <QTemporaryFile>
#include <QTextDocument>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QtGui/QIcon>
#include <utility>

#include "build_number.h"
#include "libraries/sonnet/src/core/speller.h"
#include "release.h"
#include "version.h"

#ifndef INTEGRATION_TESTS
#include <QScreen>
#if (QT_VERSION < QT_VERSION_CHECK(5, 6, 0))
#include <QGuiApplication>
#endif
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
#include <QRandomGenerator>
#include <QStandardPaths>
#endif

enum SearchEngines {
    Google = 0,
    Bing = 1,
    DuckDuckGo = 2,
    Yahoo = 3,
    GoogleScholar = 4,
    Yandex = 5,
    AskDotCom = 6,
    Qwant = 7,
    Startpage = 8
};

/**
 * Open the given path with an appropriate application
 * (thank you to qBittorrent for the inspiration)
 */
void Utils::Misc::openPath(const QString &absolutePath) {
    const QString path = QDir::fromNativeSeparators(absolutePath);
    // Hack to access samba shares with QDesktopServices::openUrl
    if (path.startsWith(QStringLiteral("//")))
        QDesktopServices::openUrl(
            QDir::toNativeSeparators(QStringLiteral("file:") % path));
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

/**
 * Opens the parent directory of the given path with a file manager and select
 * (if possible) the item at the given path
 * (thank you to qBittorrent for the inspiration)
 */
void Utils::Misc::openFolderSelect(const QString &absolutePath) {
    const QString path = QDir::fromNativeSeparators(absolutePath);
#ifdef Q_OS_WIN
    if (QFileInfo(path).exists()) {
        // Syntax is: explorer /select, "C:\Folder1\Folder2\file_to_select"
        // Dir separators MUST be win-style slashes

        // QProcess::startDetached() has an obscure bug. If the path has
        // no spaces and a comma(and maybe other special characters) it doesn't
        // get wrapped in quotes. So explorer.exe can't find the correct path
        // anddisplays the default one. If we wrap the path in quotes and pass
        // it to QProcess::startDetached() explorer.exe still shows the default
        // path. In this case QProcess::startDetached() probably puts its
        // own quotes around ours.

        STARTUPINFO startupInfo;
        ::ZeroMemory(&startupInfo, sizeof(startupInfo));
        startupInfo.cb = sizeof(startupInfo);

        PROCESS_INFORMATION processInfo;
        ::ZeroMemory(&processInfo, sizeof(processInfo));

        QString cmd = QStringLiteral("explorer.exe /select,\"%1\"")
                          .arg(QDir::toNativeSeparators(absolutePath));
        LPWSTR lpCmd = new WCHAR[cmd.size() + 1];
        cmd.toWCharArray(lpCmd);
        lpCmd[cmd.size()] = 0;

        bool ret = ::CreateProcessW(NULL, lpCmd, NULL, NULL, FALSE, 0, NULL,
                                    NULL, &startupInfo, &processInfo);
        delete[] lpCmd;

        if (ret) {
            ::CloseHandle(processInfo.hProcess);
            ::CloseHandle(processInfo.hThread);
        }
    } else {
        // If the item to select doesn't exist, try to open its parent
        openPath(path.left(path.lastIndexOf("/")));
    }
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    if (QFileInfo().exists(path)) {
        QProcess proc;
        QString output;
        proc.start(QStringLiteral("xdg-mime"),
                   QStringList{"query", "default", "inode/directory"});
        proc.waitForFinished();
        output = proc.readLine().simplified();
        if (output == QStringLiteral("dolphin.desktop") ||
            output == QStringLiteral("org.kde.dolphin.desktop")) {
            proc.startDetached(
                QStringLiteral("dolphin"),
                QStringList{"--select", QDir::toNativeSeparators(path)});
        } else if (output == QStringLiteral("nautilus.desktop") ||
                   output == QStringLiteral("org.gnome.Nautilus.desktop") ||
                   output ==
                       QStringLiteral("nautilus-folder-handler.desktop")) {
            proc.startDetached(
                QStringLiteral("nautilus"),
                QStringList{"--no-desktop", QDir::toNativeSeparators(path)});
        } else if (output == QStringLiteral("caja-folder-handler.desktop")) {
            const QDir dir = QFileInfo(path).absoluteDir();
            const QString absDir = dir.absolutePath();
            proc.startDetached(
                QStringLiteral("caja"),
                QStringList{"--no-desktop", QDir::toNativeSeparators(absDir)});
        } else if (output == QStringLiteral("nemo.desktop")) {
            proc.startDetached(
                QStringLiteral("nemo"),
                QStringList{"--no-desktop", QDir::toNativeSeparators(path)});
        } else if (output == QStringLiteral("konqueror.desktop") ||
                   output == QStringLiteral("kfmclient_dir.desktop")) {
            proc.startDetached(
                QStringLiteral("konqueror"),
                QStringList{"--select", QDir::toNativeSeparators(path)});
        } else {
            openPath(path.left(path.lastIndexOf(QLatin1Char('/'))));
        }
    } else {
        // if the item to select doesn't exist, try to open its parent
        openPath(path.left(path.lastIndexOf(QLatin1Char('/'))));
    }
#else
    openPath(path.left(path.lastIndexOf(QLatin1Char('/'))));
#endif
}

/**
 * Removes a string from the start if it starts with it
 */
QString Utils::Misc::removeIfStartsWith(QString text,
                                        const QString &removeString) {
    if (text.startsWith(removeString)) {
        text.remove(QRegularExpression(
            QStringLiteral("^") + QRegularExpression::escape(removeString)));
    }

    return text;
}

/**
 * Removes a string from the end if it ends with it
 */
QString Utils::Misc::removeIfEndsWith(QString text,
                                      const QString &removeString) {
    if (text.endsWith(removeString)) {
        text.remove(QRegularExpression(
            QRegularExpression::escape(removeString) + QStringLiteral("$")));
    }

    return text;
}

/**
 * Adds a string to the beginning of a string if it doesn't start with it
 */
QString Utils::Misc::prependIfDoesNotStartWith(QString text,
                                               const QString &startString) {
    if (!text.startsWith(startString)) {
        text.prepend(startString);
    }

    return text;
}

/**
 * Adds a string to the end of a string if it doesn't end with it
 */
QString Utils::Misc::appendIfDoesNotEndWith(QString text,
                                            const QString &endString) {
    if (!text.endsWith(endString)) {
        text.append(endString);
    }

    return text;
}

/**
 * Shortens text and adds a sequence string if text is too long
 */
QString Utils::Misc::shorten(QString text, int length,
                             const QString &sequence) {
    if (text.length() > length) {
        int newLength = length - sequence.length();

        if (newLength < 0) {
            newLength = 0;
        }

        return (text.left(newLength) + sequence).left(length);
    }

    return text;
}

/**
 * Cycles text through lowercase, uppercase, start case, and sentence case
 */
QString Utils::Misc::cycleTextCase(const QString &text) {
    if (text.isEmpty()) return text;

    const QString asLower = text.toLower();
    const QString asUpper = text.toUpper();

    // OK no matter what
    if (text == asLower) {
        return asUpper;
    }

    const QString asStart = toStartCase(text);
    const QString asSentence = toSentenceCase(text);

    if (text == asUpper) {
        if (asUpper == asStart) {
            // text == asUpper == asStart == asSentence && text != asLower
            if (asUpper == asSentence) {
                return asLower;
            }
            // text == asUpper == asStart && text != asSentence
            else {
                return asSentence;
            }
        }
        // text == asUpper && text != asStart
        else {
            return asStart;
        }
    }

    if (text == asStart) {
        // text == asStart == asSentence && asSentence != asLower
        if (asStart == asSentence) {
            return asLower;
        }
        // text == asStart && text != asSentence
        else {
            return asSentence;
        }
    }

    return asLower;
}

/**
 * Converts text to sentence case
 */
QString Utils::Misc::toSentenceCase(const QString &text) {
    // A sentence is a string of characters immediately preceded by:
    // (beginning of string followed by any amount of horizontal or vertical
    //     whitespace) or
    // (any of [.?!] followed by at least one horizontal or vertical
    //     whitespace)
    QRegularExpression sentenceSplitter(
        QStringLiteral(R"((^[\s\v]*|[.?!][\s\v]+)\K)"));

    QStringList sentences = text.toLower().split(sentenceSplitter);

    for (QString &sentence : sentences) {
        if (sentence.length() > 0) {
            sentence = sentence.at(0).toUpper() +
                       sentence.right(sentence.length() - 1);
        }
    }

    return sentences.join(QString());
}

/**
 * Converts text to start case
 */
QString Utils::Misc::toStartCase(const QString &text) {
    // A word is a string of characters immediately preceded by horizontal or
    // vertical whitespace
    QRegularExpression wordSplitter(QStringLiteral("(?<=[\\s\\v])"));

    QStringList words = text.toLower().split(wordSplitter);

    for (QString &word : words) {
        if (word.length() > 0) {
            word = word.at(0).toUpper() % word.right(word.length() - 1);
        }
    }

    return words.join(QString());
}

/**
 * Starts an executable detached with parameters
 *
 * @param executablePath the path of the executable
 * @param parameters a list of parameter strings
 * @param workingDirectory the directory to run the executable from
 * @return true on success, false otherwise
 */
bool Utils::Misc::startDetachedProcess(const QString &executablePath,
                                       const QStringList &parameters,
                                       QString workingDirectory) {
    QProcess process;

    if (workingDirectory.isEmpty()) {
        // set the directory to run the executable from
        // process.setWorkingDirectory() doesn't seem
        // to do anything under Windows
        workingDirectory = QCoreApplication::applicationDirPath();
    }

    // start executablePath detached with parameters
#ifdef Q_OS_MAC
    return process.startDetached(
        QStringLiteral("open"),
        QStringList() << executablePath << "--args" << parameters,
        workingDirectory);
#else
    return process.startDetached(executablePath, parameters, workingDirectory);
#endif
}

/**
 * Opens files via an executable
 *
 * @param executablePath the path of the executable
 * @param files a list of file strings
 * @param workingDirectory the directory to run the executable from
 * @return true on success, false otherwise
 */
bool Utils::Misc::openFilesWithApplication(const QString &executablePath,
                                           const QStringList &files,
                                           QString workingDirectory) {
#ifdef Q_OS_MAC
    QProcess process;

    if (workingDirectory.isEmpty()) {
        workingDirectory = QCoreApplication::applicationDirPath();
    }

    // start executablePath detached with parameters
    return process.startDetached(
        QStringLiteral("open"),
        QStringList() << "-a" << executablePath << files, workingDirectory);
#else
    return startDetachedProcess(executablePath, files, workingDirectory);
#endif
}

/**
 * Starts an executable synchronous with parameters
 *
 * @param executablePath the path of the executable
 * @param parameters a list of parameter strings
 * @param data the data that will be written to the process
 * @return the text that was returned by the process
 */
QByteArray Utils::Misc::startSynchronousProcess(const QString &executablePath,
                                                const QStringList &parameters,
                                                const QByteArray &data) {
    QProcess process;

    // start executablePath synchronous with parameters
#ifdef Q_OS_MAC
    process.start("open", QStringList()
                              << executablePath << "--args" << parameters);
#else
    process.start(executablePath, parameters);
#endif

    if (!process.waitForStarted()) {
        qWarning() << __func__ << " - 'process.waitForStarted' returned false";
        return QByteArray();
    }

    process.write(data);
    process.closeWriteChannel();

    if (!process.waitForFinished()) {
        qWarning() << __func__ << " - 'process.waitForFinished' returned false";
        return QByteArray();
    }

    const QByteArray result = process.readAll();
    return result;
}

/**
 * Starts a synchronous process and waits for its result
 *
 * @param cmd the terminal command containing all input and output information
 * @return true on success, false otherwise
 */
bool Utils::Misc::startSynchronousResultProcess(TerminalCmd &cmd) {
    QProcess process;

    // start executablePath synchronous with parameters
    process.start(cmd.executablePath, cmd.parameters);

    if (!process.waitForStarted()) {
        qWarning() << __func__ << " - 'process.waitForStarted' returned false";
        return false;
    }

    process.write(cmd.data);
    process.closeWriteChannel();

    if (!process.waitForFinished()) {
        qWarning() << __func__ << " - 'process.waitForFinished' returned false";
        return false;
    }

    cmd.resultSet = process.readAll();
    cmd.exitCode = process.exitCode();
    return process.exitStatus() == QProcess::NormalExit;
}

/**
 * Returns the default notes path we are suggesting
 *
 * @return
 */
QString Utils::Misc::defaultNotesPath() {
    // it seems QDir::separator() is not needed,
    // because Windows also uses "/" in QDir::homePath()
    QString path = QDir::homePath() % Utils::Misc::dirSeparator() %
                             QStringLiteral("PKbSuite");

    // add "Notes" to the base path
    path += Utils::Misc::dirSeparator() % QStringLiteral("Notes");

    // remove the snap path for Snapcraft builds
    path.remove(
        QRegularExpression(QStringLiteral(R"(snap\/pkbsuite\/\w\d+\/)")));

    return path;
}

/**
 * Returns the directory separator
 * Replaces QDir::separator() because it seems methods like
 * QDir::homePath() are using "/" under Windows too
 *
 * @return
 */
char Utils::Misc::dirSeparator() { return '/'; }

void Utils::Misc::waitMsecs(int msecs) {
    QTime dieTime = QTime::currentTime().addMSecs(msecs);
    while (QTime::currentTime() < dieTime)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

/**
 * Converts html tags to markdown
 *
 * @param text
 * @return markdown text
 */
QString Utils::Misc::htmlToMarkdown(QString text) {
    // replace Windows line breaks
    text.replace(QRegularExpression(QStringLiteral("\r\n")),
                 QStringLiteral("\n"));

    // remove all null characters
    // we can get those from Google Chrome via the clipboard
    text.remove(QChar(0));

    // remove some blocks
    text.remove(
        QRegularExpression(QStringLiteral("<head.*?>(.+?)<\\/head>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption));

    text.remove(
        QRegularExpression(QStringLiteral("<script.*?>(.+?)<\\/script>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption));

    text.remove(
        QRegularExpression(QStringLiteral("<style.*?>(.+?)<\\/style>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption));

    // replace some html tags with markdown
    text.replace(
        QRegularExpression(QStringLiteral("<strong.*?>(.+?)<\\/strong>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("**\\1**"));
    text.replace(
        QRegularExpression(QStringLiteral("<b.*?>(.+?)<\\/b>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("**\\1**"));
    text.replace(
        QRegularExpression(QStringLiteral("<em.*?>(.+?)<\\/em>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("*\\1*"));
    text.replace(
        QRegularExpression(QStringLiteral("<i.*?>(.+?)<\\/i>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("*\\1*"));
    text.replace(
        QRegularExpression(QStringLiteral("<pre.*?>(.+?)<\\/pre>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n```\n\\1\n```\n"));
    text.replace(
        QRegularExpression(QStringLiteral("<code.*?>(.+?)<\\/code>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n```\n\\1\n```\n"));
    text.replace(
        QRegularExpression(QStringLiteral("<h1.*?>(.+?)<\\/h1>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n# \\1\n"));
    text.replace(
        QRegularExpression(QStringLiteral("<h2.*?>(.+?)<\\/h2>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n## \\1\n"));
    text.replace(
        QRegularExpression(QStringLiteral("<h3.*?>(.+?)<\\/h3>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n### \\1\n"));
    text.replace(
        QRegularExpression(QStringLiteral("<h4.*?>(.+?)<\\/h4>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n#### \\1\n"));
    text.replace(
        QRegularExpression(QStringLiteral("<h5.*?>(.+?)<\\/h5>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n##### \\1\n"));
    text.replace(
        QRegularExpression(QStringLiteral("<h6.*?>(.+?)<\\/h6>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n###### \\1\n"));
    text.replace(
        QRegularExpression(QStringLiteral("<li.*?>(.+?)<\\/li>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("- \\1\n"));
    text.replace(QRegularExpression(QStringLiteral("<br.*?>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    text.replace(QRegularExpression(
                     QStringLiteral("<a[^>]+href=\"(.+?)\".*?>(.+?)<\\/a>"),
                     QRegularExpression::CaseInsensitiveOption |
                         QRegularExpression::DotMatchesEverythingOption),
                 QStringLiteral("[\\2](\\1)"));
    text.replace(
        QRegularExpression(QStringLiteral("<p.*?>(.+?)</p>"),
                           QRegularExpression::CaseInsensitiveOption |
                               QRegularExpression::DotMatchesEverythingOption),
        QStringLiteral("\n\n\\1\n\n"));

    // replace multiple line breaks
    text.replace(QRegularExpression(QStringLiteral("\n\n+")),
                 QStringLiteral("\n\n"));

    return text;
}

/**
 * Returns new html with task lists by checkboxes
 *
 * @param html
 * @param clickable
 * @return
 */
QString Utils::Misc::parseTaskList(const QString &html, bool clickable) {
    auto text = html;

    // set a list item style for todo items
    // using a css class didn't work because the styling seems to affects the sub-items too
    const auto listTag = QStringLiteral("<li style=\"list-style-type:square\">");

    if (!clickable) {
        text.replace(
            QRegularExpression(QStringLiteral(R"(<li>(\s*(<p>)*\s*)\[ ?\])"),
                               QRegularExpression::CaseInsensitiveOption),
            listTag % QStringLiteral("\\1&#9744;"));
        text.replace(
            QRegularExpression(QStringLiteral(R"(<li>(\s*(<p>)*\s*)\[[xX]\])"),
                               QRegularExpression::CaseInsensitiveOption),
            listTag % QStringLiteral("\\1&#9745;"));
        return text;
    }

    // TODO
    // to ensure the clicking behavior of checkboxes,
    // line numbers of checkboxes in the original markdown text
    // should be provided by the markdown parser

    const QString checkboxStart = QStringLiteral(
        R"(<a class="task-list-item-checkbox" href="checkbox://_)");
    text.replace(
        QRegularExpression(QStringLiteral(R"(<li>(\s*(<p>)*\s*)\[ ?\])"),
                           QRegularExpression::CaseInsensitiveOption),
        listTag % QStringLiteral("\\1") % checkboxStart %
            QStringLiteral("\">&#9744;</a>"));
    text.replace(
        QRegularExpression(QStringLiteral(R"(<li>(\s*(<p>)*\s*)\[[xX]\])"),
                           QRegularExpression::CaseInsensitiveOption),
        listTag % QStringLiteral("\\1") % checkboxStart %
            QStringLiteral("\">&#9745;</a>"));

    int count = 0;
    int pos = 0;
    while (true) {
        pos = text.indexOf(checkboxStart % QStringLiteral("\""), pos);
        if (pos == -1) break;

        pos += checkboxStart.length();
        text.insert(pos, QString::number(count++));
    }

    return text;
}

/**
 * Returns a list of all parents until the top
 *
 * @param object
 * @return
 */
QList<QObject *> Utils::Misc::getParents(QObject *object) {
    QList<QObject *> list;
    QObject *parent = object->parent();

    if (parent != Q_NULLPTR) {
        list = getParents(parent);
        list << parent;
    }

    return list;
}

/**
 * Returns the application data path
 *
 * @return
 */
QString Utils::Misc::appDataPath() {
    QString path = QString();

    QStandardPaths::StandardLocation location;

#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0))
	location = QStandardPaths::AppDataLocation;
#else
	location = QStandardPaths::GenericDataLocation;
#endif

	// get the path to store the database
	path = QStandardPaths::writableLocation(location);

	QDir dir;

	// create path if it doesn't exist yet
	dir.mkpath(path);

    return path;
}

/**
 * Returns the log file path
 *
 * @return
 */
QString Utils::Misc::logFilePath() {
    return appDataPath() % QStringLiteral("/") %
               qAppName().replace(QStringLiteral(" "), QStringLiteral("-")) +
           QStringLiteral(".log");
}

/**
 * Transforms all line feeds to \n
 *
 * @param text
 * @return
 */
QString Utils::Misc::transformLineFeeds(QString text) {
    return text.replace(
        QRegularExpression(QStringLiteral(R"((\r\n)|(\n\r)|\r|\n)")),
        QStringLiteral("\n"));
}

/**
 * Declares that we need a restart
 */
void Utils::Misc::needRestart() { qApp->setProperty("needsRestart", true); }

/**
 * Restarts the application
 */
void Utils::Misc::restartApplication() {
    QStringList parameters = QApplication::arguments();
    const QString appPath = parameters.takeFirst();

    // we don't want to have our settings cleared again after a restart
    parameters.removeOne(QStringLiteral("--clear-settings"));

    startDetachedProcess(appPath, parameters);
    QApplication::quit();
}

/**
 * Downloads an url and returns the data
 *
 * @param url
 * @return {QByteArray} the content of the downloaded url
 */
QByteArray Utils::Misc::downloadUrl(const QUrl &url) {
    auto *manager = new QNetworkAccessManager();
    QEventLoop loop;
    QTimer timer;

    timer.setSingleShot(true);

    QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(manager, SIGNAL(finished(QNetworkReply*)), &loop,
                     SLOT(quit()));

    // 10 sec timeout for the request
    timer.start(10000);

    QNetworkRequest networkRequest = QNetworkRequest(url);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)) && (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    networkRequest.setAttribute(QNetworkRequest::FollowRedirectsAttribute,
                                true);
#endif

    QByteArray data;
    QNetworkReply *reply = manager->get(networkRequest);
    loop.exec();

    // if we didn't get a timeout let us return the content
    if (timer.isActive()) {
        int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // only get the data if the status code was "success"
        // see: https://en.wikipedia.org/wiki/List_of_HTTP_status_codes
        if (statusCode >= 200 && statusCode < 300) {
            // get the data from the network reply
            data = reply->readAll();
        }
    }

    reply->deleteLater();
    delete (manager);

    return data;
}

/**
 * Downloads an url and stores it to a file
 */
bool Utils::Misc::downloadUrlToFile(const QUrl &url, QFile *file) {
    if (!file->open(QIODevice::WriteOnly)) {
        return false;
    }

    if (!file->isWritable()) {
        return false;
    }

    QByteArray data = downloadUrl(url);
    if (data.size() > 0) {
        file->write(data);
        return true;
    }

    return false;
}

/**
 * Returns generic CSS styles for displaying HTML in QTextBrowser, QLabel or
 * similar
 *
 * @return
 */
QString Utils::Misc::genericCSS() {
    QSettings settings;
    const bool darkModeColors =
        settings.value(QStringLiteral("darkModeColors")).toBool();
    QString color =
        darkModeColors ? QStringLiteral("#ffd694") : QStringLiteral("#fc7600");
    QString cssStyles =
        QStringLiteral("a {color: ") % color % QStringLiteral("}");

    color =
        darkModeColors ? QStringLiteral("#5b5b5b") : QStringLiteral("#e8e8e8");
    cssStyles +=
        QStringLiteral("kbd {background-color: ") % color % QStringLiteral("}");

    color =
        darkModeColors ? QStringLiteral("#336924") : QStringLiteral("#d6ffc7");
    cssStyles +=
        QStringLiteral("ins {background-color: ") % color % QStringLiteral("}");

    color =
        darkModeColors ? QStringLiteral("#802c2c") : QStringLiteral("#ffd7d7");
    cssStyles +=
        QStringLiteral("del {background-color: ") % color % QStringLiteral("}");
    return cssStyles;
}

/**
 * A vector of pairs, the first item in the pair is the search
 * engine name and the second item is the search url of the engine.
 */
QHash<int, Utils::Misc::SearchEngine> Utils::Misc::getSearchEnginesHashMap() {
    QHash<int, Utils::Misc::SearchEngine> searchEngines;
    searchEngines.insert(SearchEngines::Google,
                         {QStringLiteral("Google"),
                          QStringLiteral("https://www.google.com/search?q="),
                          SearchEngines::Google});
    searchEngines.insert(SearchEngines::Bing,
                         {QStringLiteral("Bing"),
                          QStringLiteral("https://www.bing.com/search?q="),
                          SearchEngines::Bing});
    searchEngines.insert(
        SearchEngines::DuckDuckGo,
        {QStringLiteral("DuckDuckGo"),
         QStringLiteral("https://duckduckgo.com/?t=pkbsuite&q="),
         SearchEngines::DuckDuckGo});
    searchEngines.insert(SearchEngines::Yahoo,
                         {QStringLiteral("Yahoo"),
                          QStringLiteral("https://search.yahoo.com/search?p="),
                          SearchEngines::Yahoo});
    searchEngines.insert(
        SearchEngines::GoogleScholar,
        {QStringLiteral("Google Scholar"),
         QStringLiteral("https://scholar.google.co.il/scholar?q="),
         SearchEngines::GoogleScholar});
    searchEngines.insert(
        SearchEngines::Yandex,
        {QStringLiteral("Yandex"),
         QStringLiteral("https://www.yandex.com/search/?text="),
         SearchEngines::Yandex});
    searchEngines.insert(SearchEngines::AskDotCom,
                         {QStringLiteral("Ask.com"),
                          QStringLiteral("https://www.ask.com/web?q="),
                          SearchEngines::AskDotCom});
    searchEngines.insert(
        SearchEngines::Qwant,
        {QStringLiteral("Qwant"), QStringLiteral("https://www.qwant.com/?q="),
         SearchEngines::Qwant});
    searchEngines.insert(
        SearchEngines::Startpage,
        {QStringLiteral("Startpage"),
         QStringLiteral("https://www.startpage.com/do/dsearch?query="),
         SearchEngines::Startpage});
    return searchEngines;
}

/**
 * Returns the default search engine id
 *
 * @return
 */
int Utils::Misc::getDefaultSearchEngineId() {
    return SearchEngines::DuckDuckGo;
}

/**
 * Returns a list of search engines in the order it should be displayed
 *
 * @return
 */
QList<int> Utils::Misc::getSearchEnginesIds() {
    return QList<int>{SearchEngines::DuckDuckGo,    SearchEngines::Google,
                      SearchEngines::Bing,          SearchEngines::Yahoo,
                      SearchEngines::GoogleScholar, SearchEngines::Yandex,
                      SearchEngines::AskDotCom,     SearchEngines::Qwant,
                      SearchEngines::Startpage};
}

/**
 * Disables the automatic update dialog per default for repositories and
 * self-builds if nothing is already set
 */
void Utils::Misc::presetDisableAutomaticUpdateDialog() {
    QSettings settings;

    // disable the automatic update dialog per default for repositories and
    // self-builds
    if (settings.value(QStringLiteral("disableAutomaticUpdateDialog"))
            .toString()
            .isEmpty()) {
        QString release = qApp->property("release").toString();
        bool enabled = release.contains(QStringLiteral("Travis")) ||
                       release.contains(QStringLiteral("AppVeyor")) ||
                       release.contains(QStringLiteral("AppImage"));
        settings.setValue(QStringLiteral("disableAutomaticUpdateDialog"),
                          !enabled);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Write all available Attributes from QPrinter into stream
///////////////////////////////////////////////////////////////////////////////

template <typename T>
void writeStreamElement(QDataStream &os, const T param) {
    const int i = static_cast<int>(param);
    os << i;
}

template <>
void writeStreamElement<QString>(QDataStream &os, const QString s) {
    os << s;
}

/**
 * Save printer settings to a stream
 */
QDataStream &Utils::Misc::dataStreamWrite(QDataStream &os,
                                          const QPrinter &printer) {
    writeStreamElement(os, printer.printerName());
    writeStreamElement(os, printer.pageLayout().pageSize().id());
    writeStreamElement(os, printer.collateCopies());
    writeStreamElement(os, printer.colorMode());
    writeStreamElement(os, printer.copyCount());
    writeStreamElement(os, printer.creator());
    writeStreamElement(os, printer.docName());
    writeStreamElement(os, printer.duplex());
    writeStreamElement(os, printer.fontEmbeddingEnabled());
    writeStreamElement(os, printer.fullPage());
    writeStreamElement(os, printer.pageLayout().orientation());
    writeStreamElement(os, printer.outputFileName());
    writeStreamElement(os, printer.outputFormat());
    writeStreamElement(os, printer.pageOrder());
    writeStreamElement(os, printer.paperSource());
    writeStreamElement(os, printer.printProgram());
    writeStreamElement(os, printer.printRange());
    writeStreamElement(os, printer.printerName());
    writeStreamElement(os, printer.resolution());

    auto margins = printer.pageLayout().margins(QPageLayout::Unit::Millimeter);
    os << margins.left() << margins.top() << margins.right() << margins.bottom();

    Q_ASSERT_X(
        os.status() == QDataStream::Ok, __FUNCTION__,
        QString("Stream status = %1").arg(os.status()).toStdString().c_str());
    return os;
}

///////////////////////////////////////////////////////////////////////////////
// Read all available Attributes from stream into QPrinter
///////////////////////////////////////////////////////////////////////////////

template <typename T>
T readStreamElement(QDataStream &is) {
    int i;
    is >> i;
    return static_cast<T>(i);
}

template <>
QString readStreamElement<QString>(QDataStream &is) {
    QString s;
    is >> s;
    return s;
}

/**
 * Load printer settings from a stream
 */
QDataStream &Utils::Misc::dataStreamRead(QDataStream &is, QPrinter &printer) {
    printer.setPrinterName(readStreamElement<QString>(is));

    QPageSize pageSize(readStreamElement<QPageSize::PageSizeId>(is));
    auto pageLayout = printer.pageLayout();
    pageLayout.setPageSize(pageSize);
    printer.setPageLayout(pageLayout);

    printer.setCollateCopies(readStreamElement<bool>(is));
    printer.setColorMode(readStreamElement<QPrinter::ColorMode>(is));
    printer.setCopyCount(readStreamElement<int>(is));
    printer.setCreator(readStreamElement<QString>(is));
    printer.setDocName(readStreamElement<QString>(is));
    printer.setDuplex(readStreamElement<QPrinter::DuplexMode>(is));
    printer.setFontEmbeddingEnabled(readStreamElement<bool>(is));
    printer.setFullPage(readStreamElement<bool>(is));
    printer.setPageOrientation(readStreamElement<QPageLayout::Orientation>(is));
    printer.setOutputFileName(readStreamElement<QString>(is));
    printer.setOutputFormat(readStreamElement<QPrinter::OutputFormat>(is));
    printer.setPageOrder(readStreamElement<QPrinter::PageOrder>(is));
    printer.setPaperSource(readStreamElement<QPrinter::PaperSource>(is));
    printer.setPrintProgram(readStreamElement<QString>(is));
    printer.setPrintRange(readStreamElement<QPrinter::PrintRange>(is));
    printer.setPrinterName(readStreamElement<QString>(is));
    printer.setResolution(readStreamElement<int>(is));

    qreal left, top, right, bottom;
    is >> left >> top >> right >> bottom;

    QMarginsF margins(left, top, right, bottom);
    printer.setPageMargins(margins, QPageLayout::Unit::Millimeter);

    Q_ASSERT_X(
        is.status() == QDataStream::Ok, __FUNCTION__,
        QString("Stream status = %1").arg(is.status()).toStdString().c_str());

    return is;
}

/**
 * Stores the properties of a QPrinter to the settings
 *
 * @param printer
 * @param settingsKey
 */
void Utils::Misc::storePrinterSettings(QPrinter *printer,
                                       const QString &settingsKey) {
    QByteArray byteArr;
    QDataStream os(&byteArr, QIODevice::WriteOnly);
    dataStreamWrite(os, *printer);
    QSettings().setValue(settingsKey, byteArr.toHex());
}

/**
 * Loads the properties of a QPrinter from the settings
 *
 * @param printer
 * @param settingsKey
 */
void Utils::Misc::loadPrinterSettings(QPrinter *printer,
                                      const QString &settingsKey) {
    QSettings settings;

    if (!settings.value(settingsKey).isValid()) {
        return;
    }

    QByteArray printSetup = settings.value(settingsKey).toByteArray();
    printSetup = QByteArray::fromHex(printSetup);
    QDataStream is(&printSetup, QIODevice::ReadOnly);
    dataStreamRead(is, *printer);
}

/**
 * Returns if "allowNoteEditing" is turned on
 *
 * @return
 */
bool Utils::Misc::isNoteEditingAllowed() {
    return QSettings().value(QStringLiteral("allowNoteEditing"), true).toBool();
}

/**
 * Returns if "useInternalExportStyling" is turned on
 *
 * @return
 */
bool Utils::Misc::useInternalExportStylingForPreview() {
    return QSettings().value(
            QStringLiteral("MainWindow/noteTextView.useInternalExportStyling"),
            true)
        .toBool();
}

/**
 * Returns the preview font string
 *
 * @return
 */
QString Utils::Misc::previewFontString() {
    return QSettings().value(isPreviewUseEditorStyles() ?
        QStringLiteral("MainWindow/noteTextEdit.font") :
        QStringLiteral("MainWindow/noteTextView.font")).toString();
}

/**
 * Returns the preview code font string
 *
 * @return
 */
QString Utils::Misc::previewCodeFontString() {
    return QSettings().value(isPreviewUseEditorStyles() ?
        QStringLiteral("MainWindow/noteTextEdit.code.font") :
        QStringLiteral("MainWindow/noteTextView.code.font")).toString();
}

/**
 * Returns if "MainWindow/noteTextView.useEditorStyles" is turned on
 *
 * @return
 */
bool Utils::Misc::isPreviewUseEditorStyles() {
    return QSettings().value(
        QStringLiteral("MainWindow/noteTextView.useEditorStyles"),
                       true).toBool();
}

/**
 * Returns if "enableSocketServer" is turned on
 *
 * @return
 */
bool Utils::Misc::isSocketServerEnabled() {
    return QSettings().value(QStringLiteral("enableSocketServer"), true).toBool();
}

/**
 * Returns if "enableWebAppSupport" is turned on
 *
 * @return
 */
bool Utils::Misc::isWebAppSupportEnabled() {
    return QSettings().value(QStringLiteral("enableWebAppSupport"), false).toBool();
}

/**
 * Returns if "darkModeIconTheme" is turned on
 *
 * @return
 */
bool Utils::Misc::isDarkModeIconTheme() {
    const QSettings settings;
    const bool darkMode = settings.value(QStringLiteral("darkMode")).toBool();
    return settings.value(QStringLiteral("darkModeIconTheme"), darkMode)
        .toBool();
}

/**
 * Returns if "automaticNoteFolderDatabaseClosing" is turned on
 *
 * @return
 */
bool Utils::Misc::doAutomaticNoteFolderDatabaseClosing() {
    return QSettings().value(QStringLiteral("automaticNoteFolderDatabaseClosing"))
        .toBool();
}

/**
 * Returns if "noteListPreview" is turned on
 *
 * @return
 */
bool Utils::Misc::isNoteListPreview() {
    return QSettings().value(QStringLiteral("noteListPreview")).toBool();
}

/**
 * Returns if "enableNoteTree" is turned on
 *
 * @return
 */
bool Utils::Misc::isEnableNoteTree() {
    return QSettings().value(QStringLiteral("enableNoteTree")).toBool();
}

/**
 * Returns the characters to use to indent
 *
 * @return
 */
QString Utils::Misc::indentCharacters() {
    return QSettings().value("Editor/useTabIndent").toBool()
               ? QStringLiteral("\t")
               : QStringLiteral(" ").repeated(indentSize());
}

/**
 * Returns the indent size
 *
 * @return
 */
int Utils::Misc::indentSize() {
    return QSettings().value("Editor/indentSize", 4).toInt();
}

/**
 * Unescapes html special characters
 *
 * @param html
 * @return
 */
QString Utils::Misc::unescapeHtml(QString html) {
    //    html.replace("&lt;","<");
    //    html.replace("&gt;",">");
    //    html.replace("&amp;","&");
    //    return html;

    // text.toPlainText() will remove all line breaks, we don't want that
    html.replace(QStringLiteral("\n"), QStringLiteral("<br>"));

    QTextDocument text;
    text.setHtml(html);
    return text.toPlainText();
}

/**
 * Unescapes some html special characters
 *
 * @param text
 * @return
 */
QString Utils::Misc::htmlspecialchars(QString text) {
    text.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    text.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
    text.replace(QStringLiteral("'"), QStringLiteral("&apos;"));
    text.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    text.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    return text;
}

/**
 * Outputs an info text
 *
 * @param text
 */
void Utils::Misc::printInfo(const QString &text) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 5, 0))
    qInfo() << text;
#else
    printf("%s\n", text.toLatin1().data());
#endif
}

/**
 * Converts a byte size to a human readable size
 *
 * @param size
 * @return
 */
QString Utils::Misc::toHumanReadableByteSize(qint64 size) {
    double num = size;
    const QStringList list = {"KB", "MB", "GB", "TB"};

    QStringListIterator i(list);
    QString unit(QStringLiteral("bytes"));

    while (num >= 1024.0 && i.hasNext()) {
        unit = i.next();
        num /= 1024.0;
    }

    return QString().setNum(num, 'f', 2) + " " + unit;
}

/**
 * Checks if text matches a regular expression in regExpList
 *
 * @param text
 * @param regExpList
 * @return
 */
bool Utils::Misc::regExpInListMatches(const QString &text,
                                      const QStringList &regExpList) {
    for (const QString &regExp : regExpList) {
        const QString &trimmed = regExp.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        if (QRegularExpression(trimmed).match(text).hasMatch()) {
            return true;
        }
    }

    return false;
}

/**
 * Transforms remote preview image tags
 *
 * @param html
 */
void Utils::Misc::transformRemotePreviewImages(
    QString &html, int maxImageWidth, ExternalImageHash *externalImageHash) {
    QRegularExpression re(
        QStringLiteral(R"(<img src=\"(https?:\/\/.+)\".*\/?>)"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::MultilineOption |
            QRegularExpression::InvertedGreedinessOption);
    QRegularExpressionMatchIterator i = re.globalMatch(html);

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString imageTag = match.captured(0);
        QString inlineImageTag;
        int imageWidth;
        ExternalImageHashItem hashItem;

        if (externalImageHash->contains(imageTag)) {
            hashItem = externalImageHash->value(imageTag);
            inlineImageTag = hashItem.imageTag;
            imageWidth = hashItem.imageWidth;
        } else {
            inlineImageTag =
                remotePreviewImageTagToInlineImageTag(imageTag, imageWidth);
            hashItem.imageTag = inlineImageTag;
            hashItem.imageWidth = imageWidth;
            externalImageHash->insert(imageTag, hashItem);
        }

        imageWidth = std::min(maxImageWidth, imageWidth);
        inlineImageTag.replace(
            ">", QString("width=\"%1\">").arg(QString::number(imageWidth)));
        html.replace(imageTag, inlineImageTag);
    }
}

/**
 * Transforms a remote preview image tag to an inline image tag
 *
 * @param data
 * @param imageSuffix
 * @return
 */
QString Utils::Misc::remotePreviewImageTagToInlineImageTag(QString imageTag,
                                                           int &imageWidth) {
    imageTag.replace(QStringLiteral("&amp;"), QStringLiteral("&"));

    QRegularExpression re(QStringLiteral(R"(<img src=\"(https?:\/\/.+)\")"),
                          QRegularExpression::CaseInsensitiveOption |
                              QRegularExpression::InvertedGreedinessOption);

    QRegularExpressionMatch match = re.match(imageTag);
    if (!match.hasMatch()) {
        return imageTag;
    }

    const QString url = match.captured(1);
    const QByteArray data = downloadUrl(url);
    auto image = QImage::fromData(data);
    imageWidth = image.width();
    const QMimeDatabase db;
    const auto type = db.mimeTypeForData(data);

    // for now we do no caching, because we don't know when to invalidate the
    // cache
    const QString replace = QStringLiteral("data:") % type.name() %
                            QStringLiteral(";base64,") % data.toBase64();
    return imageTag.replace(url, replace);
}

QString Utils::Misc::createUuidString() {
    const QUuid uuid = QUuid::createUuid();
    QString uuidString = uuid.toString();
    uuidString.replace(QStringLiteral("{"), QString())
        .replace(QStringLiteral("}"), QString());

    return uuidString;
}

/**
 * Returns the path where the local dictionaries will be stored
 *
 * @return
 */
QString Utils::Misc::localDictionariesPath() {
    QString path = Utils::Misc::appDataPath() % QStringLiteral("/dicts");
    QDir dir;

    // create path if it doesn't exist yet
    dir.mkpath(path);
    return path;
}

/**
 * Generates a SHA1 signature for a file
 *
 * @return
 */
QByteArray Utils::Misc::generateFileSha1Signature(const QString &path) {
    QCryptographicHash hash(QCryptographicHash::Sha1);
    QFile file(path);

    if (file.open(QIODevice::ReadOnly)) {
        hash.addData(file.readAll());
    } else {
        return QByteArray();
    }

    // retrieve the SHA1 signature of the file
    return hash.result();
}

/**
 * Checks if two files are the same
 *
 * @return
 */
bool Utils::Misc::isSameFile(const QString &path1, const QString &path2) {
    return generateFileSha1Signature(path1) == generateFileSha1Signature(path2);
}

QString Utils::Misc::generateRandomString(int length) {
    const QString possibleCharacters(QStringLiteral(
                 "ABCDEFGHKLMNPQRSTUVWXYZabcdefghkmnpqrstuvwxyz23456789"));

    QString randomString;
    for (int i = 0; i < length; ++i) {
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
        const int index = qrand() % possibleCharacters.length();
#else
        const quint32 index = QRandomGenerator::global()->generate() %
                        possibleCharacters.length();
#endif
        QChar nextChar = possibleCharacters.at(index);
        randomString.append(nextChar);
    }

    return randomString;
}

QString Utils::Misc::makeFileNameRandom(const QString &fileName,
                                        const QString &overrideSuffix) {
    const QFileInfo fileInfo(fileName);

    QString baseName = fileInfo.baseName().remove(
        QRegularExpression(QStringLiteral(R"([^\w\d\-_ ])"))).replace(
                               QChar(' '), QChar('-'));
    baseName.truncate(32);

    // find a more random name for the file
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    const quint32 number = qrand();
#else
    const quint32 number = QRandomGenerator::global()->generate();
#endif

    return baseName + QChar('-') + QString::number(number) + QChar('.') +
           (overrideSuffix.isEmpty() ? fileInfo.suffix() : overrideSuffix);
}

/**
 * Returns a non-existing filename (like "my-image.jpg") in directoryPath
 * If the filename is already taken a number will be appended (like "my-image-1.jpg")
 */
QString Utils::Misc::findAvailableFileName(const QString &fileName,
                                           const QString &directoryPath,
                                           const QString &overrideSuffix) {
    const QFileInfo fileInfo(fileName);
    QString baseName = fileInfo.baseName();
    baseName.truncate(200);
    const QString newSuffix = fileInfo.suffix();
    QString newBaseName = baseName;
    QString newFileName = newBaseName + QStringLiteral(".") + newSuffix;
    QString newFilePath = directoryPath + QDir::separator() + newFileName;
    QFile file(newFilePath);
    int nameCount = 0;

    // check if file with this filename already exists
    while (file.exists()) {
        // find new filename for the file
        newBaseName = baseName + QStringLiteral("-") +
               QString::number(++nameCount);
        newFileName = newBaseName + QStringLiteral(".") + newSuffix;
        newFilePath = directoryPath + QDir::separator() + newFileName;
        file.setFileName(newFilePath);
        qDebug() << __func__ << " - 'override fileName': " << newFileName;

        if (nameCount > 1000) {
            newFileName = makeFileNameRandom(fileName, overrideSuffix);
            break;
        }
    }

    return newFileName;
}

/**
 * Strips all trailing spaces from str and returns the stripped text
 *
 * @param str
 * @return
 */
QString Utils::Misc::rstrip(const QString& str) {
    int n = str.size() - 1;

    for (; n >= 0; --n) {
        if (!str.at(n).isSpace()) {
            return str.left(n + 1);
        }
    }

    return "";
}

/**
 * Checks if file exists in the filesystem and is readable
 *
 * @return bool
 */
bool Utils::Misc::fileExists(const QString& path) {
    const QFile file(path);
    const QFileInfo fileInfo(file);
    return file.exists() && fileInfo.isFile() && fileInfo.isReadable();
}

static QString removeReducedCJKAccMark(const QString &label, int pos)
{
    if (pos > 0 && pos + 1 < label.length()
            && label[pos - 1] == QLatin1Char('(') && label[pos + 1] == QLatin1Char(')')
            && label[pos].isLetterOrNumber()) {
        // Check if at start or end, ignoring non-alphanumerics.
        int len = label.length();
        int p1 = pos - 2;
        while (p1 >= 0 && !label[p1].isLetterOrNumber()) {
            --p1;
        }
        ++p1;
        int p2 = pos + 2;
        while (p2 < len && !label[p2].isLetterOrNumber()) {
            ++p2;
        }
        --p2;

        if (p1 == 0) {
            return label.left(pos - 1) + label.mid(p2 + 1);
        } else if (p2 + 1 == len) {
            return label.left(p1) + label.mid(pos + 2);
        }
    }
    return label;
}

/**
 * @brief Removes accelarator markers from a label
 *
 * Taken from KDE's Ki18n library
 */
QString Utils::Misc::removeAcceleratorMarker(const QString &label_)
{
    QString label = label_;

    int p = 0;
    bool accmarkRemoved = false;
    while (true) {
        p = label.indexOf(QLatin1Char('&'), p);
        if (p < 0 || p + 1 == label.length()) {
            break;
        }

        if (label[p + 1].isLetterOrNumber()) {
            // Valid accelerator.
            label = QString(label.left(p) + label.mid(p + 1));

            // May have been an accelerator in CJK-style "(&X)"
            // at the start or end of text.
            label = removeReducedCJKAccMark(label, p);

            accmarkRemoved = true;
        } else if (label[p + 1] == QLatin1Char('&')) {
            // Escaped accelerator marker.
            label = QString(label.left(p) + label.mid(p + 1));
        }

        ++p;
    }

    // If no marker was removed, and there are CJK characters in the label,
    // also try to remove reduced CJK marker -- something may have removed
    // ampersand beforehand.
    if (!accmarkRemoved) {
        bool hasCJK = false;
        for (const QChar c : asConst(label)) {
            if (c.unicode() >= 0x2e00) { // rough, but should be sufficient
                hasCJK = true;
                break;
            }
        }
        if (hasCJK) {
            p = 0;
            while (true) {
                p = label.indexOf(QLatin1Char('('), p);
                if (p < 0) {
                    break;
                }
                label = removeReducedCJKAccMark(label, p + 1);
                ++p;
            }
        }
    }

    return label;
}

/**
 * Tries to find a suiting file extension for a mime type
 *
 * @param mimeType
 * @return
 */
QString Utils::Misc::fileExtensionForMimeType(const QString &mimeType) {
    if (mimeType == "image/jpg" || mimeType == "image/jpeg") {
        return "jpg";
    } else if (mimeType == "image/png") {
        return "png";
    } else if (mimeType == "image/gif") {
        return "gif";
    } else if (mimeType == "application/pdf") {
        return "pdf";
    }

    return "";
}
