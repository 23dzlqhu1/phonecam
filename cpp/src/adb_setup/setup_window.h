#pragma once

#include <QWidget>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QSslError>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QProgressBar;
class QCloseEvent;
class QNetworkAccessManager;
class QFile;
QT_END_NAMESPACE

namespace phonecam {

// USB 连接设置窗口的当前状态（简单枚举，防止重复点击与 UI 文案互相覆盖）
enum class SetupState {
    CheckingExisting,  // 启动后自动检查本机已有的 USB 连接组件
    ReadyExisting,     // 已找到本机可用 ADB，等待用户确认使用
    Downloading,       // 官方下载中
    Extracting,        // 解压中
    Validating,        // 验证 adb version 中
    Ready,             // 配置完成
    Failed             // 失败或未找到，等待用户选择其他方式
};

// PhoneCam USB 连接设置窗口
// 职责：检查/复用本机 ADB、从 Google 官方下载、选择已有 adb.exe、导入 Platform-Tools ZIP，
//       所有来源都经过 AdbLocator::validateAdb()（真实执行 adb version）验证后才算安装成功。
class SetupWindow : public QWidget {
    Q_OBJECT
public:
    explicit SetupWindow(const QString& androidApkPath = {},
                         bool installApkRequested = false,
                         QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;  // 关闭窗口时中止下载并清理临时文件

private slots:
    void startExistingCheck();                 // 后台线程检查本机已有 ADB
    void onExistingCheckFinished(const QString& path, const QString& versionText);
    void onUseExistingClicked();               // [使用此 ADB]
    void onDownloadClicked();                  // [官方下载并安装]
    void onSelectExeClicked();                 // [选择已有 adb.exe]
    void onImportZipClicked();                 // [导入 Platform-Tools ZIP]
    void onInstallApkClicked();                // [安装/修复手机端]
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onReadyRead();                        // 流式写入临时 ZIP，避免一次性读入内存
    void onDownloadFinished();
    void onSslErrors(const QList<QSslError>& errors);

private:
    QString defaultInstallDir() const;         // 固定安装目录：<AppLocalData>/adb
    void setState(SetupState state);           // 更新状态并联动按钮可用性/可见性
    void setStatus(const QString& text);       // 更新状态区文案并写日志
    bool canStartOperation() const;            // 下载/解压/验证期间禁止再次触发安装类操作
    void clearDownload();                      // 中止下载并清理 reply 与临时 ZIP
    void startDownload();                      // 从 Google 官方源下载
    bool installPlatformToolsZip(const QString& zipPath, bool isTemporaryZip); // 统一安装入口
    bool extractZipWithCategory(const QString& zipPath, const QString& destDir,
                                QString* errorOut) const;  // 解压（tar → PowerShell fallback）
    QString findAdbInStaging(const QString& stagingDir) const;  // 有限结构发现
    bool commitInstall(const QString& stagingPtDir, const QString& finalPtDir,
                       QString* errorOut) const;  // staging 替换正式目录（先备份旧目录）
    bool writeAdbPathToSettings(const QString& adbPath) const;
    QString mapNetworkError(QNetworkReply::NetworkError code) const;  // 网络错误→中文提示
    void showFailureDialog(const QString& message);  // 失败自救：重试 / 选 exe / 导入 ZIP
    bool prepareTlsBackend();                  // HTTPS 预检，并优先启用 Schannel

    // 控件
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_useExistingBtn = nullptr;   // [使用此 ADB]（仅 ReadyExisting 状态可见）
    QPushButton* m_downloadBtn = nullptr;      // [官方下载并安装]
    QPushButton* m_selectExeBtn = nullptr;     // [选择已有 adb.exe]
    QPushButton* m_importZipBtn = nullptr;     // [导入 Platform-Tools ZIP]
    QPushButton* m_installApkBtn = nullptr;    // [安装/修复手机端]
    QProgressBar* m_progressBar = nullptr;

    // 下载状态
    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    QFile* m_tempFile = nullptr;
    QElapsedTimer m_downloadTimer;             // 记录下载耗时（日志用）
    qint64 m_bytesReceived = 0;
    qint64 m_bytesExpected = -1;
    QStringList m_tlsErrors;                   // 保存真实 TLS 错误详情，仅用于日志与分类
    QStringList m_availableTlsBackends;
    QString m_activeTlsBackend;
    bool m_tlsPreflightOk = false;

    // 启动检查结果缓存
    QString m_existingPath;                    // 检查找到的本机 ADB 路径（空 = 未找到）
    QString m_existingVersion;                 // 对应的版本行文本
    QString m_androidApkPath;                  // 仅接受命令行/安装器明确传入的 APK 绝对路径
    bool m_installApkRequested = false;         // 等 ADB启动检查结束后再执行 CLI安装

    SetupState m_state = SetupState::CheckingExisting;

    // Google 官方 Android Platform-Tools 固定链接（长期有效，始终指向最新版）
    static constexpr const char* kDownloadUrl =
        "https://dl.google.com/android/repository/platform-tools-latest-windows.zip";
    // 下载 User-Agent（明确标识自身，不伪装浏览器）
    static constexpr const char* kUserAgent = "PhoneCam-ADB-Setup/1.0";
};

} // namespace phonecam
