#include "adb_setup/setup_window.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QFileDialog>
#include <QDir>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QMessageBox>
#include <QDebug>

namespace phonecam {

SetupWindow::SetupWindow(QWidget* parent)
    : QWidget(parent)
    , m_network(new QNetworkAccessManager(this))
{
    setWindowTitle(QString::fromUtf8("PhoneCam ADB 安装向导"));
    setMinimumWidth(520);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(20, 20, 20, 20);

    // 说明文字
    auto* desc = new QLabel(QString::fromUtf8(
        "此向导将从清华大学 TUNA 镜像下载 Android Platform Tools（包含 ADB）。\n"
        "下载完成后，PhoneCam 将使用它来支持 USB 数据线连接手机。"));
    desc->setWordWrap(true);
    root->addWidget(desc);

    // 目录选择行
    auto* dirRow = new QHBoxLayout;
    dirRow->setSpacing(8);
    auto* dirLabel = new QLabel(QString::fromUtf8("安装目录："));
    m_dirEdit = new QLineEdit(defaultInstallDir());
    m_browseBtn = new QPushButton(QString::fromUtf8("浏览..."));
    dirRow->addWidget(dirLabel);
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(m_browseBtn);
    root->addLayout(dirRow);

    // 下载按钮
    m_downloadBtn = new QPushButton(QString::fromUtf8("开始下载并安装"));
    m_downloadBtn->setDefault(true);
    root->addWidget(m_downloadBtn);

    // 进度条
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    root->addWidget(m_progressBar);

    // 状态标签
    m_statusLabel = new QLabel(QString::fromUtf8("准备就绪"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    root->addStretch();

    connect(m_browseBtn, &QPushButton::clicked, this, &SetupWindow::onBrowseClicked);
    connect(m_downloadBtn, &QPushButton::clicked, this, &SetupWindow::onDownloadClicked);
}

QString SetupWindow::defaultInstallDir() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .absoluteFilePath("adb");
}

QString SetupWindow::adbExePath() const
{
    return QDir(m_dirEdit->text()).absoluteFilePath("platform-tools/adb.exe");
}

void SetupWindow::setUiEnabled(bool enabled)
{
    m_dirEdit->setEnabled(enabled);
    m_browseBtn->setEnabled(enabled);
    m_downloadBtn->setEnabled(enabled);
}

void SetupWindow::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
    qDebug() << "[ADB-SETUP]" << text;
}

void SetupWindow::onBrowseClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        QString::fromUtf8("选择 ADB 安装目录"),
        m_dirEdit->text());
    if (!dir.isEmpty()) {
        m_dirEdit->setText(dir);
    }
}

void SetupWindow::onDownloadClicked()
{
    QString destDir = m_dirEdit->text();
    if (destDir.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("目录为空"),
            QString::fromUtf8("请先选择安装目录。"));
        return;
    }

    QDir dir(destDir);
    if (!dir.exists() && !dir.mkpath(destDir)) {
        QMessageBox::critical(this, QString::fromUtf8("创建目录失败"),
            QString::fromUtf8("无法创建目录：%1").arg(destDir));
        return;
    }

    setUiEnabled(false);
    m_progressBar->setValue(0);
    setStatus(QString::fromUtf8("正在下载 platform-tools..."));

    QString tempPath = QDir(destDir).absoluteFilePath("platform-tools-download.zip");
    m_tempFile = new QFile(tempPath, this);
    if (!m_tempFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, QString::fromUtf8("文件错误"),
            QString::fromUtf8("无法创建临时文件：%1").arg(tempPath));
        setUiEnabled(true);
        return;
    }

    QUrl downloadUrl(kDownloadUrl);
    QNetworkRequest networkRequest(downloadUrl);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader,
        "PhoneCam-ADB-Setup/1.0 (Qt)");

    m_reply = m_network->get(networkRequest);
    connect(m_reply, &QNetworkReply::downloadProgress,
        this, &SetupWindow::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished,
        this, &SetupWindow::onDownloadFinished);
    connect(m_reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
        this, &SetupWindow::onDownloadError);
}

void SetupWindow::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0) {
        int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_progressBar->setValue(percent);
    } else {
        m_progressBar->setValue(0);
    }
}

void SetupWindow::onDownloadFinished()
{
    if (!m_reply || !m_tempFile) return;

    if (m_reply->error() != QNetworkReply::NoError) {
        m_tempFile->close();
        m_tempFile->remove();
        setStatus(QString::fromUtf8("下载失败：%1").arg(m_reply->errorString()));
        setUiEnabled(true);
        return;
    }

    m_tempFile->write(m_reply->readAll());
    m_tempFile->close();
    m_progressBar->setValue(100);

    QString zipPath = m_tempFile->fileName();
    QString destDir = m_dirEdit->text();

    setStatus(QString::fromUtf8("下载完成，正在解压到 %1...").arg(destDir));

    if (!extractZip(zipPath, destDir)) {
        QMessageBox::critical(this, QString::fromUtf8("解压失败"),
            QString::fromUtf8("无法解压下载的文件：%1").arg(zipPath));
        setStatus(QString::fromUtf8("解压失败"));
        setUiEnabled(true);
        return;
    }

    // 删除临时 zip
    QFile::remove(zipPath);

    QString adbPath = adbExePath();
    if (!QFileInfo::exists(adbPath)) {
        QMessageBox::critical(this, QString::fromUtf8("文件缺失"),
            QString::fromUtf8("解压后未找到 adb.exe：%1").arg(adbPath));
        setStatus(QString::fromUtf8("解压后未找到 adb.exe"));
        setUiEnabled(true);
        return;
    }

    if (!writeAdbPathToSettings(adbPath)) {
        QMessageBox::warning(this, QString::fromUtf8("配置警告"),
            QString::fromUtf8("ADB 已安装，但保存配置时出错。"));
        setStatus(QString::fromUtf8("ADB 已安装，但配置保存失败"));
        setUiEnabled(true);
        return;
    }

    setStatus(QString::fromUtf8("安装完成：%1").arg(adbPath));
    QMessageBox::information(this, QString::fromUtf8("安装完成"),
        QString::fromUtf8("ADB 已安装到：%1\n\nPhoneCam 将在下次启动时使用该路径。").arg(adbPath));

    setUiEnabled(true);
}

void SetupWindow::onDownloadError(QNetworkReply::NetworkError code)
{
    Q_UNUSED(code)
    if (m_reply) {
        setStatus(QString::fromUtf8("下载出错：%1").arg(m_reply->errorString()));
    }
}

bool SetupWindow::extractZip(const QString& zipPath, const QString& destDir)
{
    // Windows 10+ 自带 tar.exe，调用它解压 zip
    QProcess tar;
    tar.start("tar", QStringList() << "-xf" << zipPath << "-C" << destDir);
    if (!tar.waitForStarted(5000)) {
        qDebug() << "[ADB-SETUP] tar not available, trying powershell";
        // 备用方案：PowerShell Expand-Archive
        QProcess ps;
        ps.start("powershell",
            QStringList()
                << "-NoProfile"
                << "-ExecutionPolicy"
                << "Bypass"
                << "-Command"
                << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                       .arg(zipPath, destDir));
        if (!ps.waitForStarted(5000) || !ps.waitForFinished(60000)) {
            qDebug() << "[ADB-SETUP] powershell extract failed:" << ps.errorString();
            return false;
        }
        return ps.exitCode() == 0;
    }

    if (!tar.waitForFinished(120000)) {
        qDebug() << "[ADB-SETUP] tar timeout";
        return false;
    }
    return tar.exitCode() == 0;
}

bool SetupWindow::writeAdbPathToSettings(const QString& adbPath)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       "PhoneCam", "PhoneCam");
    settings.setValue("adb/path", adbPath);
    return settings.status() == QSettings::NoError;
}

} // namespace phonecam
