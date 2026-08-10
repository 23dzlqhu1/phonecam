// ═══════════════════════════════════════════════════════════════
// setup_window.cpp — PhoneCam USB 连接设置窗口实现
//
// 覆盖四条路径：
//   1. 启动自动检查本机已有 ADB（后台线程，不卡 GUI）→ [使用此 ADB]
//   2. [官方下载并安装]：Google 官方 platform-tools ZIP，流式写入、HTTP/TLS/超时分类
//   3. [选择已有 adb.exe]：validateAdb 通过后才保存 QSettings adb/path
//   4. [导入 Platform-Tools ZIP]：staging 解压 → 结构发现 → 验证 → 替换 → 保存
// 下载的 ZIP 与用户导入的 ZIP 共用 installPlatformToolsZip() 统一安装流程。
// ═══════════════════════════════════════════════════════════════

#include "adb_setup/setup_window.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QFileDialog>
#include <QDir>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFileInfo>
#include <QSettings>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <QCloseEvent>
#include <QUuid>
#include <QProcess>
#include <QSslError>

#include <memory>
#include <utility>

#include "core/adb_locator.h"

namespace phonecam {

SetupWindow::SetupWindow(QWidget* parent)
    : QWidget(parent)
    , m_network(new QNetworkAccessManager(this))
{
    setWindowTitle(QString::fromUtf8("PhoneCam USB 连接设置"));
    setMinimumWidth(580);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(20, 20, 20, 20);

    // 说明文字：用普通用户能理解的语言描述"USB 连接组件"
    auto* desc = new QLabel(QString::fromUtf8(
        "使用 USB 数据线连接手机前，需要先准备“USB 连接组件”（Android Debug Bridge）。\n"
        "本工具会优先复用本机已有的组件；如果没有，可以从 Android 官方下载，\n"
        "也可以选择已有的 adb.exe，或导入 Platform-Tools 压缩包。"));
    desc->setWordWrap(true);
    root->addWidget(desc);

    // 状态区（多行）：显示检查结果 / 进度 / 错误
    m_statusLabel = new QLabel(QString::fromUtf8("正在检查…"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumHeight(72);
    root->addWidget(m_statusLabel);

    // [使用此 ADB]：仅当本机已找到可用组件时显示
    m_useExistingBtn = new QPushButton(QString::fromUtf8("使用此 ADB"));
    m_useExistingBtn->setVisible(false);
    root->addWidget(m_useExistingBtn, 0, Qt::AlignLeft);

    // 进度条：仅下载期间显示
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    root->addWidget(m_progressBar);

    // 三种方法平级展示（不藏进高级设置）
    m_downloadBtn = new QPushButton(QString::fromUtf8("官方下载并安装"));
    m_downloadBtn->setDefault(true);
    root->addWidget(m_downloadBtn, 0, Qt::AlignLeft);

    m_selectExeBtn = new QPushButton(QString::fromUtf8("选择已有 adb.exe"));
    root->addWidget(m_selectExeBtn, 0, Qt::AlignLeft);

    m_importZipBtn = new QPushButton(QString::fromUtf8("导入 Platform-Tools ZIP"));
    root->addWidget(m_importZipBtn, 0, Qt::AlignLeft);

    root->addStretch();

    connect(m_useExistingBtn, &QPushButton::clicked,
            this, &SetupWindow::onUseExistingClicked);
    connect(m_downloadBtn, &QPushButton::clicked,
            this, &SetupWindow::onDownloadClicked);
    connect(m_selectExeBtn, &QPushButton::clicked,
            this, &SetupWindow::onSelectExeClicked);
    connect(m_importZipBtn, &QPushButton::clicked,
            this, &SetupWindow::onImportZipClicked);

    setState(SetupState::CheckingExisting);

    // 延迟到事件循环启动后再检查，避免在 show() 返回前阻塞
    QTimer::singleShot(0, this, &SetupWindow::startExistingCheck);
}

QString SetupWindow::defaultInstallDir() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .absoluteFilePath(QStringLiteral("adb"));
}

void SetupWindow::setState(SetupState state)
{
    m_state = state;
    const bool busy = (state == SetupState::Downloading
        || state == SetupState::Extracting
        || state == SetupState::Validating);
    const bool blocked = busy || state == SetupState::CheckingExisting;

    // 忙/检查期间禁用所有操作按钮，防止重复点击与流程互相覆盖
    m_useExistingBtn->setVisible(state == SetupState::ReadyExisting);
    m_useExistingBtn->setEnabled(state == SetupState::ReadyExisting);
    m_downloadBtn->setEnabled(!blocked);
    m_selectExeBtn->setEnabled(!blocked);
    m_importZipBtn->setEnabled(!blocked);
    m_progressBar->setVisible(state == SetupState::Downloading);
}

void SetupWindow::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
    qDebug().noquote() << "[ADB-SETUP]" << text;
}

bool SetupWindow::canStartOperation() const
{
    return m_state != SetupState::Downloading
        && m_state != SetupState::Extracting
        && m_state != SetupState::Validating;
}

void SetupWindow::closeEvent(QCloseEvent* event)
{
    clearDownload();  // 关闭窗口时中止下载、删除 partial zip，避免悬挂 reply
    QWidget::closeEvent(event);
}

void SetupWindow::clearDownload()
{
    if (m_reply) {
        m_reply->disconnect(this);  // 先断开，防止 abort() 触发 finished 再进入 onDownloadFinished
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_tempFile) {
        if (m_tempFile->isOpen()) m_tempFile->close();
        m_tempFile->remove();  // 删除未完成的 partial zip
        m_tempFile->deleteLater();
        m_tempFile = nullptr;
    }
}

// ── 启动自动检查（第 9 节）────────────────────────────────────────
void SetupWindow::startExistingCheck()
{
    setState(SetupState::CheckingExisting);
    setStatus(QString::fromUtf8("正在检查本机已有的 USB 连接组件…"));

    // AdbLocator::resolveAdb() 会同步执行 adb version（最长约 5 秒），放到后台线程避免卡住界面
    auto result = std::make_shared<std::pair<QString, QString>>();  // (path, versionText)
    QThread* thread = QThread::create([result]() {
        const QString path = AdbLocator::resolveAdb();
        if (path.isEmpty()) {
            *result = { QString(), QString() };
            return;
        }
        const AdbValidation validation = AdbLocator::validateAdb(path);  // 取版本行文本
        *result = { path, validation.versionText };
    });
    connect(thread, &QThread::finished, this, [this, result]() {
        onExistingCheckFinished(result->first, result->second);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SetupWindow::onExistingCheckFinished(const QString& path, const QString& versionText)
{
    m_existingPath = path;
    m_existingVersion = versionText;

    if (path.isEmpty()) {
        // 未找到：进入引导状态，三种方法全部可用
        setState(SetupState::Failed);
        setStatus(QString::fromUtf8(
            "未找到可用的 USB 连接组件。\n请选择以下任一种方式完成准备："));
        return;
    }

    // 已找到：显示 ✅ + 版本 + 路径，并提供 [使用此 ADB]
    setState(SetupState::ReadyExisting);
    setStatus(QString::fromUtf8("✅ 已找到 USB 连接组件\n%1\n路径：%2")
        .arg(versionText, path));
}

void SetupWindow::onUseExistingClicked()
{
    if (m_existingPath.isEmpty()) return;

    // 写入/确认 QSettings adb/path 并标记 Ready
    if (!writeAdbPathToSettings(m_existingPath)) {
        qWarning() << "[ADB-SETUP] 保存 QSettings adb/path 失败:" << m_existingPath;
    }
    AdbLocator::clearCache();
    setState(SetupState::Ready);
    setStatus(QString::fromUtf8("✅ USB 连接组件已配置\n%1\n路径：%2")
        .arg(m_existingVersion, m_existingPath));
}

// ── 官方下载（第 11-16 节）───────────────────────────────────────
void SetupWindow::onDownloadClicked()
{
    if (!canStartOperation()) return;
    startDownload();
}

void SetupWindow::startDownload()
{
    clearDownload();  // 清理上一次可能残留的下载

    const QString destDir = defaultInstallDir();
    QDir dir(destDir);
    if (!dir.exists() && !dir.mkpath(destDir)) {
        setState(SetupState::Failed);
        setStatus(QString::fromUtf8("无法创建安装目录：%1").arg(destDir));
        return;
    }

    // 受控临时 ZIP：放在安装目录下，随下载流程删除（失败/取消/成功后均删除）
    const QString tempPath = QDir(destDir).absoluteFilePath(
        QStringLiteral(".phonecam-adb-download-%1.zip")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
    m_tempFile = new QFile(tempPath, this);
    if (!m_tempFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_tempFile->deleteLater();
        m_tempFile = nullptr;
        setState(SetupState::Failed);
        setStatus(QString::fromUtf8("无法创建临时下载文件：%1").arg(tempPath));
        return;
    }

    QUrl url(QString::fromLatin1(kDownloadUrl));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    // 30 秒无数据传输即超时（Qt 5.15+ 标准 API）；不用整体 total timeout
    request.setTransferTimeout(30000);

    m_tlsErrors.clear();
    m_bytesReceived = 0;
    m_bytesExpected = -1;
    m_downloadTimer.start();

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress,
            this, &SetupWindow::onDownloadProgress);
    connect(m_reply, &QNetworkReply::readyRead,
            this, &SetupWindow::onReadyRead);
    connect(m_reply, &QNetworkReply::finished,
            this, &SetupWindow::onDownloadFinished);
    connect(m_reply, &QNetworkReply::sslErrors,
            this, &SetupWindow::onSslErrors);

    setState(SetupState::Downloading);
    m_progressBar->setValue(0);
    setStatus(QString::fromUtf8("正在从 Android 官方服务器下载 USB 连接组件…"));
}

void SetupWindow::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    m_bytesReceived = bytesReceived;
    m_bytesExpected = bytesTotal;
    if (bytesTotal > 0) {
        m_progressBar->setValue(static_cast<int>((bytesReceived * 100) / bytesTotal));
    } else {
        m_progressBar->setValue(0);
    }
}

void SetupWindow::onReadyRead()
{
    if (!m_tempFile || !m_reply) return;
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) return;

    // 流式写入，避免在 finished() 里 readAll() 一次性读入内存
    if (m_tempFile->write(chunk) != chunk.size()) {
        clearDownload();  // 中止并清理 partial zip
        setState(SetupState::Failed);
        showFailureDialog(QString::fromUtf8(
            "磁盘写入失败：无法保存下载的文件，请检查磁盘空间。"));
    }
}

void SetupWindow::onDownloadFinished()
{
    if (!m_reply || !m_tempFile) return;
    m_reply->disconnect(this);  // 防止 finished/sslErrors 重复进入

    const int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netError = m_reply->error();
    const bool tlsFailed = !m_tlsErrors.isEmpty();

    // 详细日志（第 16 节）：记录 source/host/HTTP status/error code/errorString/TLS/bytes/elapsed
    // 不记录代理密码、认证 token 或 URL 参数等敏感信息
    qDebug().noquote() << "[ADB-SETUP] source=google-official"
        << "host=" << m_reply->url().host()
        << "httpStatus=" << httpStatus
        << "errorCode=" << int(netError)
        << "errorString=" << m_reply->errorString()
        << "tlsErrors=" << m_tlsErrors
        << "bytesReceived=" << m_bytesReceived
        << "bytesExpected=" << m_bytesExpected
        << "elapsedMs=" << m_downloadTimer.elapsed();

    const QString zipPath = m_tempFile->fileName();

    // 用户取消（例如关闭窗口）：静默清理，不弹框
    if (netError == QNetworkReply::OperationCanceledError) {
        m_tempFile->close();
        clearDownload();
        return;
    }

    // 失败统一处理：关文件、删 partial zip、恢复按钮、弹"自救"对话框
    auto fail = [&](const QString& message) {
        m_tempFile->close();
        clearDownload();  // 删除 partial zip
        setState(SetupState::Failed);
        showFailureDialog(message);
    };

    if (httpStatus >= 200 && httpStatus < 300) {
        // 下载成功：保留 ZIP 文件，交给统一安装流程
        if (m_tempFile->isOpen()) m_tempFile->close();
        m_progressBar->setValue(100);
        if (m_reply) { m_reply->deleteLater(); m_reply = nullptr; }
        if (m_tempFile) { m_tempFile->deleteLater(); m_tempFile = nullptr; }

        installPlatformToolsZip(zipPath, true);
        QFile::remove(zipPath);  // 官方下载临时 ZIP：无论安装成败都删除
        return;
    }

    // 有明确 HTTP 状态码且非 2xx：优先显示具体 code（403/404/429/5xx 等）
    if (httpStatus > 0) {
        fail(QString::fromUtf8("官方下载失败：HTTP %1").arg(httpStatus));
        return;
    }

    // TLS 握手失败：提示"无法建立安全连接（TLS）"，绝不调用 ignoreSslErrors()
    if (tlsFailed) {
        fail(QString::fromUtf8("无法建立安全连接（TLS）"));
        return;
    }

    if (netError != QNetworkReply::NoError) {
        fail(mapNetworkError(netError));
        return;
    }

    fail(QString::fromUtf8("未知网络错误"));
}

void SetupWindow::onSslErrors(const QList<QSslError>& errors)
{
    m_tlsErrors.clear();
    for (const QSslError& error : errors) {
        m_tlsErrors << error.errorString();
    }
    qWarning().noquote() << "[ADB-SETUP] TLS 错误:" << m_tlsErrors;
}

QString SetupWindow::mapNetworkError(QNetworkReply::NetworkError code) const
{
    switch (code) {
    case QNetworkReply::TimeoutError:
        return QString::fromUtf8(
            "网络超时：无法连接 Android 官方下载服务器，请检查网络后重试，或使用“导入 Platform-Tools ZIP”。");
    case QNetworkReply::HostNotFoundError:
        return QString::fromUtf8("无法解析 Android 官方下载服务器，请检查网络连接。");
    case QNetworkReply::ConnectionRefusedError:
        return QString::fromUtf8("无法连接 Android 官方下载服务器，请检查网络后重试。");
    case QNetworkReply::RemoteHostClosedError:
        return QString::fromUtf8("连接被服务器关闭，请重试。");
    case QNetworkReply::TemporaryNetworkFailureError:
        return QString::fromUtf8("网络暂时不可用，请稍后重试。");
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ProxyAuthenticationRequiredError:
    case QNetworkReply::AuthenticationRequiredError:
        return QString::fromUtf8("代理或认证失败，请检查系统代理设置。");
    case QNetworkReply::SslHandshakeFailedError:
        return QString::fromUtf8("无法建立安全连接（TLS）。");
    default:
        return QString::fromUtf8("未知网络错误。");
    }
}

// 失败"自救"对话框（第 33 节）：提供重试 / 选择已有 adb.exe / 导入 ZIP，不让用户卡死
void SetupWindow::showFailureDialog(const QString& message)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QString::fromUtf8("USB 连接组件配置失败"));
    box.setText(message);
    box.setInformativeText(QString::fromUtf8(
        "你可以：\n- 重试下载\n- 选择已有的 adb.exe\n- 导入已经下载好的 Platform-Tools ZIP"));
    QPushButton* retryBtn = box.addButton(QString::fromUtf8("重试"), QMessageBox::AcceptRole);
    QPushButton* selectBtn = box.addButton(QString::fromUtf8("选择已有 adb.exe"), QMessageBox::ActionRole);
    QPushButton* importBtn = box.addButton(QString::fromUtf8("导入 Platform-Tools ZIP"), QMessageBox::ActionRole);
    box.addButton(QString::fromUtf8("关闭"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == retryBtn) {
        startDownload();
    } else if (box.clickedButton() == selectBtn) {
        onSelectExeClicked();
    } else if (box.clickedButton() == importBtn) {
        onImportZipClicked();
    }
}

// ── 选择已有 adb.exe（第 17-18 节）───────────────────────────────
void SetupWindow::onSelectExeClicked()
{
    if (!canStartOperation()) return;

    const QString selected = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择 adb.exe"),
        QString(),
        QString::fromUtf8("adb.exe (adb.exe)"));
    if (selected.isEmpty()) return;

    // 不能直接保存，必须先真实验证
    setState(SetupState::Validating);
    setStatus(QString::fromUtf8("正在验证所选文件…"));
    const AdbValidation validation = AdbLocator::validateAdb(selected);

    if (!validation.valid) {
        setState(SetupState::Failed);
        const QString message = QString::fromUtf8(
            "所选文件不是可用的 Android Debug Bridge。\n\n原因：%1").arg(validation.error);
        setStatus(message);
        QMessageBox::warning(this, QString::fromUtf8("验证失败"), message);
        return;
    }

    if (!writeAdbPathToSettings(selected)) {
        qWarning() << "[ADB-SETUP] 保存 QSettings adb/path 失败:" << selected;
    }
    AdbLocator::clearCache();
    setState(SetupState::Ready);
    setStatus(QString::fromUtf8("✅ USB 连接组件已配置\n%1\n路径：%2")
        .arg(validation.versionText, selected));
}

// ── 导入 Platform-Tools ZIP（第 19-23 节）────────────────────────
void SetupWindow::onImportZipClicked()
{
    if (!canStartOperation()) return;

    const QString zipPath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("导入 Platform-Tools ZIP"),
        QString(),
        QString::fromUtf8("ZIP 压缩包 (*.zip)"));
    if (zipPath.isEmpty()) return;

    // 用户导入的原文件：绝不删除
    installPlatformToolsZip(zipPath, false);
}

// ── 统一安装入口（第 22 节）：官方下载 ZIP 与本地 ZIP 共用 ────────
// 流程：extract staging → locate adb → validate → commit install → save settings
bool SetupWindow::installPlatformToolsZip(const QString& zipPath, bool isTemporaryZip)
{
    const QString destDir = defaultInstallDir();
    QDir dir(destDir);
    if (!dir.exists() && !dir.mkpath(destDir)) {
        setState(SetupState::Failed);
        setStatus(QString::fromUtf8("无法创建安装目录：%1").arg(destDir));
        return false;
    }

    // 失败统一处理：isTemporaryZip（官方下载）时弹三按钮自救框，否则弹普通警告
    auto fail = [&](const QString& message) {
        setState(SetupState::Failed);
        setStatus(message);
        if (isTemporaryZip) {
            showFailureDialog(message);
        } else {
            QMessageBox::warning(this, QString::fromUtf8("导入失败"), message);
        }
    };

    // 1) staging 解压（第 20 节）：先解压到独立 staging 目录，不直接覆盖现有 platform-tools
    setState(SetupState::Extracting);
    setStatus(QString::fromUtf8("正在解压组件…"));
    const QString stagingDir = QDir(destDir).absoluteFilePath(
        QStringLiteral(".phonecam-adb-staging-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
    if (!QDir().mkpath(stagingDir)) {
        fail(QString::fromUtf8("无法创建临时目录：%1").arg(stagingDir));
        return false;
    }
    QString errorOut;
    if (!extractZipWithCategory(zipPath, stagingDir, &errorOut)) {
        QDir(stagingDir).removeRecursively();
        fail(QString::fromUtf8("解压失败：%1").arg(errorOut));
        return false;
    }

    // 2) 有限结构发现（第 20 节）：只认 staging/platform-tools/adb.exe 或一层包装目录，
    //    绝不递归全盘寻找任意 adb.exe
    const QString stagingAdb = findAdbInStaging(stagingDir);
    if (stagingAdb.isEmpty()) {
        QDir(stagingDir).removeRecursively();
        fail(QString::fromUtf8(
            "压缩包中未找到 platform-tools/adb.exe，请确认是官方 Platform-Tools 压缩包。"));
        return false;
    }

    // 3) 先验证再替换（第 21 节）：验证失败则删除 staging，保留旧 ADB 不动
    setState(SetupState::Validating);
    setStatus(QString::fromUtf8("正在验证组件（adb version）…"));
    const AdbValidation validation = AdbLocator::validateAdb(stagingAdb);
    if (!validation.valid) {
        QDir(stagingDir).removeRecursively();
        fail(QString::fromUtf8("组件验证失败：%1\n原有配置保持不变。").arg(validation.error));
        return false;
    }

    // 4) commit：staging/platform-tools 替换为正式 destDir/platform-tools（先备份旧目录）
    const QString stagingPtDir = QFileInfo(stagingAdb).absolutePath();
    const QString finalPtDir = QDir(destDir).absoluteFilePath(QStringLiteral("platform-tools"));
    if (!commitInstall(stagingPtDir, finalPtDir, &errorOut)) {
        QDir(stagingDir).removeRecursively();
        fail(QString::fromUtf8("安装失败：%1").arg(errorOut));
        return false;
    }
    QDir(stagingDir).removeRecursively();  // 清理 staging 剩余部分

    // 5) 保存 QSettings adb/path 并清除 locator 缓存
    const QString finalAdbPath = QDir(finalPtDir).absoluteFilePath(QStringLiteral("adb.exe"));
    if (!writeAdbPathToSettings(finalAdbPath)) {
        qWarning() << "[ADB-SETUP] 保存 QSettings adb/path 失败:" << finalAdbPath;
    }
    AdbLocator::clearCache();

    // 6) 成功（第 24 节）：adb.exe 存在 + 进程启动 + adb version exit 0 + 输出有效
    setState(SetupState::Ready);
    setStatus(QString::fromUtf8("✅ USB 连接组件已就绪\n%1\n安装位置：%2\nPhoneCam 已保存该路径。")
        .arg(validation.versionText, finalAdbPath));
    QMessageBox::information(this, QString::fromUtf8("USB 连接设置完成"),
        QString::fromUtf8("✅ USB 连接组件已就绪\n\n%1\n\n安装位置：%2\n\nPhoneCam 已保存该路径。")
            .arg(validation.versionText, finalAdbPath));
    return true;
}

// 解压（第 23 节）：tar.exe → PowerShell Expand-Archive fallback，错误必须分类
bool SetupWindow::extractZipWithCategory(const QString& zipPath, const QString& destDir,
                                         QString* errorOut) const
{
    // 优先 Windows 10+ 自带 tar.exe
    QProcess tar;
    tar.start(QStringLiteral("tar"),
              { QStringLiteral("-xf"), zipPath, QStringLiteral("-C"), destDir });
    if (!tar.waitForStarted(5000)) {
        // tar 不可用 → PowerShell Expand-Archive 备用
        QProcess ps;
        ps.start(QStringLiteral("powershell"),
            { QStringLiteral("-NoProfile"),
              QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
              QStringLiteral("-Command"),
              QStringLiteral("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                  .arg(zipPath, destDir) });
        if (!ps.waitForStarted(5000)) {
            *errorOut = QString::fromUtf8("解压工具不可用（tar 与 PowerShell 均无法启动）");
            return false;
        }
        if (!ps.waitForFinished(120000)) {
            ps.kill();
            ps.waitForFinished(1000);
            *errorOut = QString::fromUtf8("解压超时（PowerShell Expand-Archive 超过 120 秒）");
            return false;
        }
        if (ps.exitCode() != 0) {
            *errorOut = QString::fromUtf8("PowerShell 解压失败（exit code=%1）").arg(ps.exitCode());
            return false;
        }
        return true;
    }

    if (!tar.waitForFinished(120000)) {
        tar.kill();
        tar.waitForFinished(1000);
        *errorOut = QString::fromUtf8("解压超时（tar 超过 120 秒）");
        return false;
    }
    if (tar.exitCode() != 0) {
        *errorOut = QString::fromUtf8("tar 解压失败（exit code=%1）").arg(tar.exitCode());
        return false;
    }
    return true;
}

// 有限结构发现：只接受官方 Platform-Tools ZIP 的两种结构
QString SetupWindow::findAdbInStaging(const QString& stagingDir) const
{
    // 结构 1：staging/platform-tools/adb.exe（官方 ZIP 标准结构）
    const QString direct = QDir(stagingDir).absoluteFilePath(
        QStringLiteral("platform-tools/adb.exe"));
    if (QFileInfo::exists(direct)) return direct;

    // 结构 2：staging/<单层包装目录>/platform-tools/adb.exe（ZIP 多包一层目录）
    const QDir top(stagingDir);
    const QStringList subDirs = top.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (subDirs.size() == 1) {
        const QString nested = QDir(top.absoluteFilePath(subDirs.first()))
            .absoluteFilePath(QStringLiteral("platform-tools/adb.exe"));
        if (QFileInfo::exists(nested)) return nested;
    }
    return {};
}

// staging 替换正式目录：先备份旧目录，再重命名（同卷，尽量原子），失败回滚
bool SetupWindow::commitInstall(const QString& stagingPtDir, const QString& finalPtDir,
                                QString* errorOut) const
{
    // 1) 旧目录先备份，而不是直接删除，保证替换失败还能回滚
    QString backupDir;
    if (QFileInfo::exists(finalPtDir)) {
        backupDir = finalPtDir + QStringLiteral(".old-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        if (!QDir(finalPtDir).rename(finalPtDir, backupDir)) {
            if (errorOut) *errorOut = QString::fromUtf8("无法备份原有组件目录：%1").arg(finalPtDir);
            return false;
        }
    }

    // 2) staging 的 platform-tools 重命名为正式目录
    if (!QDir(stagingPtDir).rename(stagingPtDir, finalPtDir)) {
        if (!backupDir.isEmpty()) QDir(backupDir).rename(backupDir, finalPtDir);  // 回滚备份
        if (errorOut) *errorOut = QString::fromUtf8("无法将新组件移入安装目录：%1").arg(finalPtDir);
        return false;
    }

    // 3) 删除旧备份
    if (!backupDir.isEmpty()) {
        QDir backup(backupDir);
        backup.removeRecursively();
    }
    return true;
}

bool SetupWindow::writeAdbPathToSettings(const QString& adbPath) const
{
    // 与 AdbLocator 读取时保持一致：IniFormat + UserScope + org/app = PhoneCam
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("PhoneCam"), QStringLiteral("PhoneCam"));
    settings.setValue(QStringLiteral("adb/path"), adbPath);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace phonecam
