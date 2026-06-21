#pragma once

#include <QWidget>
#include <QNetworkReply>
#include <QFile>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
class QNetworkAccessManager;
QT_END_NAMESPACE

namespace phonecam {

// PhoneCam ADB 安装向导主窗口
class SetupWindow : public QWidget {
    Q_OBJECT
public:
    explicit SetupWindow(QWidget* parent = nullptr);

private slots:
    void onBrowseClicked();
    void onDownloadClicked();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onDownloadError(QNetworkReply::NetworkError code);

private:
    QString defaultInstallDir() const;
    QString adbExePath() const;
    void setUiEnabled(bool enabled);
    void setStatus(const QString& text);
    bool extractZip(const QString& zipPath, const QString& destDir);
    bool writeAdbPathToSettings(const QString& adbPath);

    QLineEdit* m_dirEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;

    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    QFile* m_tempFile = nullptr;

    static constexpr const char* kDownloadUrl =
        "https://mirrors.tuna.tsinghua.edu.cn/android/repository/platform-tools-latest-windows.zip";
};

} // namespace phonecam
