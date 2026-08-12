#include "core/adb_locator.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTimer>

using namespace phonecam;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList backends = QSslSocket::availableBackends();
    if (backends.contains(QStringLiteral("schannel"))) {
        QSslSocket::setActiveBackend(QStringLiteral("schannel"));
    }
    if (!QSslSocket::supportsSsl() || QSslSocket::activeBackend() != QStringLiteral("schannel")) {
        qCritical().noquote() << "Schannel unavailable. supportsSsl=" << QSslSocket::supportsSsl()
                              << "available=" << backends
                              << "active=" << QSslSocket::activeBackend();
        return 1;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QStringLiteral(
        "https://dl.google.com/android/repository/platform-tools-latest-windows.zip")));
    request.setTransferTimeout(60000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("PhoneCam-ADB-Setup-Integration-Test/1.0"));
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(120000);
    loop.exec();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray zipData = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || status != 200 || zipData.isEmpty()) {
        qCritical().noquote() << "Qt official download failed. http=" << status
                              << "error=" << int(reply->error()) << reply->errorString();
        reply->deleteLater();
        return 2;
    }
    reply->deleteLater();

    QTemporaryDir temp;
    if (!temp.isValid()) return 3;
    const QString zipPath = temp.filePath(QStringLiteral("platform-tools.zip"));
    QFile zip(zipPath);
    if (!zip.open(QIODevice::WriteOnly) || zip.write(zipData) != zipData.size()) return 4;
    zip.close();

    QProcess tar;
    tar.start(QStringLiteral("tar"),
        { QStringLiteral("-xf"), zipPath, QStringLiteral("-C"), temp.path() });
    if (!tar.waitForStarted(5000) || !tar.waitForFinished(120000) || tar.exitCode() != 0) {
        qCritical().noquote() << "Downloaded ZIP extraction failed:" << tar.readAllStandardError();
        return 5;
    }

    const QString adbPath = QDir(temp.path()).absoluteFilePath(
        QStringLiteral("platform-tools/adb.exe"));
    const AdbValidation validation = AdbLocator::validateAdb(adbPath);
    if (!validation.valid) {
        qCritical().noquote() << "Downloaded adb version failed:" << validation.error;
        return 6;
    }
    qInfo().noquote() << "Qt Schannel download and adb version passed:" << validation.versionText;
    return 0;
}
