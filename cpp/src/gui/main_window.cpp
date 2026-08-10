#include "gui/main_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QApplication>
#include <QDebug>
#include <QThread>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QMessageBox>
#include <QFileInfo>
#include <QInputDialog>

namespace phonecam {

// ── DecodeWorker ──

DecodeWorker::DecodeWorker(HwDecoder* decoder, FinalFrameComposer* composer,
                           BoundedQueue<QImage>* legacyQueue, QObject* parent)
    : QObject(parent), m_decoder(decoder), m_composer(composer), m_legacyDisplayQueue(legacyQueue) {}

void DecodeWorker::setTransformState(bool mirror, bool flip, int manualRotation) {
    m_mirror = mirror;
    m_flip = flip;
    m_manualRotation = manualRotation;
}

void DecodeWorker::decodeFrame(const VideoFrame& frame) {
    if (!m_decoder || !m_decoder->isInitialized()) return;

    // ── Frame age measurement (ms since PC received the packet) ──
    const double nowMs = QDateTime::currentMSecsSinceEpoch() * 1.0;
    const double frameAgeMs = nowMs - frame.receive_time;

    // ── Hot fix: drop stale frames to chase real-time ──
    // If the decode/compose pipeline can't keep up, old frames accumulate in
    // the Qt::QueuedConnection queue. Dropping them prevents 20s+ latency.
    static constexpr double kMaxFrameAgeMs = 500.0;
    if (frameAgeMs > kMaxFrameAgeMs) {
        static int dropCount = 0;
        dropCount++;
        if (dropCount <= 5 || dropCount % 30 == 0) {
            qDebug() << "[LATENCY] DROP stale frame#" << frame.sequence
                     << "age=" << frameAgeMs << "ms"
                     << "dropped=" << dropCount;
        }
        // Flush decoder to discard stale reference frames
        m_decoder->flush();
        return;
    }

    // Build transform state
    FrameTransform transform;
    transform.mirror = m_mirror;
    transform.flip = m_flip;
    transform.manualRotation = m_manualRotation;
    transform.androidRotation = frame.rotation;

    // BUG-013: Only use legacy QImage path if explicitly requested (--legacy-qimage-compose).
    // mirror/flip/manualRotation are now handled natively in NV12 fast path.
    if (m_useLegacyCompose) {
        // Legacy QImage path: decode → QImage → apply transforms
        QImage img;
        {
            std::lock_guard<std::mutex> lock(m_decoder->mutex());
            img = m_decoder->decode(frame.data.data(), static_cast<int>(frame.data.size()));
        }
        if (img.isNull()) return;

        // Apply rotation via QImage transform (legacy)
        if (frame.rotation == 90) {
            img = img.transformed(QTransform().rotate(90));
        } else if (frame.rotation == 180) {
            img = img.transformed(QTransform().rotate(180));
        } else if (frame.rotation == 270) {
            img = img.transformed(QTransform().rotate(-90));
        }

        if (m_legacyDisplayQueue) m_legacyDisplayQueue->push(img);
        emit frameDecoded(img);
    } else {
        // Primary NV12 path: decodeFrame → composeFromDecodedFrame → Nv12Frame
        DecodedFrame decoded;
        {
            std::lock_guard<std::mutex> lock(m_decoder->mutex());
            decoded = m_decoder->decodeFrame(frame.data.data(), static_cast<int>(frame.data.size()));
        }
        if (!decoded.valid()) return;

        Nv12Frame nv12 = m_composer->composeFromDecodedFrame(
            decoded, transform, m_sequence++, frame.pts_ns, frame.receive_time);

        // Log frame age at output (every 30 frames)
        const double composeEndMs = QDateTime::currentMSecsSinceEpoch() * 1.0;
        const double totalAgeMs = composeEndMs - frame.receive_time;
        if (m_sequence % 30 == 0) {
            qDebug() << "[LATENCY] frame#" << m_sequence
                     << "age=" << totalAgeMs << "ms"
                     << "decode+compose=" << (composeEndMs - nowMs) << "ms";
        }

        emit finalFrameReady(nv12);
    }
}

void DecodeWorker::requestFlush() {
    if (m_decoder) m_decoder->flush();
}

// ── MainWindow ──

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_receiver(new PcpReceiver(this))
    , m_decoder(new HwDecoder(this))
    , m_composer(new FinalFrameComposer())
    , m_connManager(new ConnectionManager(this))
    , m_virtualCam(new VirtualCam(this))
    , m_legacyDisplayQueue(new BoundedQueue<QImage>(3))
    , m_statsTimer(new QTimer(this))
    , m_frameIdleTimer(new QTimer(this))
{
    setupUi();

    // Wire up connection manager
    connect(m_connManager, &ConnectionManager::stateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(m_connManager, &ConnectionManager::diagnosticsChanged,
            this, &MainWindow::onDiagnosticsChanged);
    // P2-1 Loop 4: Wire connectionReady to restart receiver for device switching
    connect(m_connManager, &ConnectionManager::connectionReady, this, [this](const QString& url) {
        // Parse host:port from URL
        QStringList parts = url.split(':');
        QString host = parts.value(0, "127.0.0.1");
        quint16 port = parts.value(1, "9999").toUShort();

        // Stop current receiver, clear queues, flush decoder
        m_receiver->stop();
        m_legacyDisplayQueue->clear();
        if (m_decodeWorker) {
            QMetaObject::invokeMethod(m_decodeWorker, "requestFlush", Qt::QueuedConnection);
        }

        // Restart receiver with new endpoint
        m_receiver->start(host, port);
        qDebug() << "[MAIN] Switching receiver to" << host << ":" << port;
    });
    connect(m_connManager, &ConnectionManager::candidatesChanged,
            this, [this](const QVector<DeviceCandidate>& candidates) {
        // P1-1: Update device combo box
        m_deviceCombo->blockSignals(true);
        int prevIndex = m_deviceCombo->currentIndex();
        QString prevData = m_deviceCombo->currentData().toString();
        m_deviceCombo->clear();
        m_deviceCombo->addItem(QString::fromUtf8("自动选择"), "");
        int restoreIndex = 0;
        QString activeId = m_connManager->activeDeviceId();
        for (int i = 0; i < candidates.size(); i++) {
            const auto& c = candidates[i];
            QString icon = (c.transport == "usb") ? "🔌" :
                           (c.transport == "wifi") ? "📶" : "🔗";
            QString statusIcon = (c.status == "Connected") ? "✅" :
                                 (c.status == "Connecting") ? "⏳" :
                                 (c.status == "Incompatible") ? "⚠" :   // 8月9日修复 A: 版本不兼容
                                 (c.status == "Failed") ? "❌" : "⬜";
            // P2-1 Loop 4: Mark active device with ▶
            QString activePrefix = (c.id == activeId) ? "▶ " : "";
            // 8月9日修复 A: Incompatible 用"版本不兼容"而非英文状态
            QString statusText = (c.status == "Incompatible")
                ? QString::fromUtf8("版本不兼容")
                : c.status;
            QString label = QString("%1%2 %3 %4 [%5]")
                .arg(activePrefix, icon, c.displayName, statusIcon, statusText);
            if (!c.lastError.isEmpty() && c.status == "Failed") {
                label += QString(" — %1").arg(c.lastError);
            }
            m_deviceCombo->addItem(label, c.id);
            // BUG-012: Sync UI with ConnectionManager state
            // If device is active (auto-selected or manual), restore to it
            if (c.id == activeId || c.id == prevData) {
                restoreIndex = i + 1;
            }
        }
        m_deviceCombo->setCurrentIndex(restoreIndex);
        m_deviceCombo->blockSignals(false);

        // BUG-007: Refresh device name in top status when already streaming
        if (m_streamEstablished) {
            m_statusDetail->setText(activeDeviceDisplayName());
        }
    });

    // Wire up PcpReceiver
    connect(m_receiver, &PcpReceiver::connectionEstablished, this, [this]() {
        m_statusDot->setStyleSheet("background-color: #10b981; border-radius: 6px;");
        auto info = m_connManager->info();
        QString label = (info.connectionType == "hotspot") ?
            QString::fromUtf8("热点连接") : info.connectionType;
        m_statusTitle->setText(label);
        // BUG-007: Do NOT set m_streamEstablished here — TCP socket ≠ streaming.
        // Only enterStreamingState() on first decoded frame sets it.
        m_statusDetail->setText(QString::fromUtf8("等待首帧..."));
        m_receiver->sendCommand(QByteArrayLiteral("PLI\n"));
        if (m_decodeWorker) {
            QMetaObject::invokeMethod(m_decodeWorker, "requestFlush", Qt::QueuedConnection);
        }
    });
    connect(m_receiver, &PcpReceiver::connectionLost, this, [this](const QString& reason) {
        qDebug() << "[MAIN] Connection lost, reason:" << reason;
        m_statusDot->setStyleSheet("background-color: #f59e0b; border-radius: 6px;");
        m_statusTitle->setText(QString::fromUtf8("等待手机推流..."));
        exitStreamingState();  // BUG-007: reset streaming UI state
        // BUG-012: Notify ConnectionManager so it re-probes WiFi gateways
        m_connManager->markStreamLost();
        // Stop PcpReceiver retry loop — let ConnectionManager pick a new endpoint
        m_receiver->stop();
    });

    // Setup decode thread
    startPipeline();

    // Legacy frame display timer (poll BoundedQueue<QImage> at 60fps — only for legacy fallback path)
    QTimer* legacyDisplayTimer = new QTimer(this);
    connect(legacyDisplayTimer, &QTimer::timeout, this, [this]() {
        auto frame = m_legacyDisplayQueue->tryPop();
        if (frame.has_value()) {
            m_preview->updateFrame(frame.value());
            m_frameCount++;
            m_legacyFrameCount++;
        }
    });
    legacyDisplayTimer->start(16);  // ~60fps

    // Pipeline stats timer (1 Hz — logs atomic counters + queue sizes)
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::onStatsTimer);
    m_statsTimer->start(1000);

    // Frame idle timer for camera switch detection (500ms interval)
    connect(m_frameIdleTimer, &QTimer::timeout, this, [this]() {
        if (m_lastFrameTimeMs == 0 || !m_streamEstablished) return;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 gap = now - m_lastFrameTimeMs;

        if (gap > kStreamPausedThresholdMs) {
            // >10s: stream paused or camera switch taking too long
            if (!m_cameraSwitchingDetected) {
                m_cameraSwitchingDetected = true;
                m_statusTitle->setText(QString::fromUtf8("WiFi 已连接"));
                m_statusDetail->setText(QString::fromUtf8("手机端暂停推流或正在切换"));
                qDebug() << "[IDLE] Frame idle >10s, gap:" << gap << "ms";
            }
        } else if (gap > kCameraSwitchThresholdMs) {
            // 1.5-10s: likely camera switch
            if (!m_cameraSwitchingDetected) {
                m_cameraSwitchingDetected = true;
                m_statusTitle->setText(QString::fromUtf8("摄像头切换中"));
                m_statusDetail->setText(QString::fromUtf8("等待新画面..."));
                qDebug() << "[IDLE] Frame idle >1.5s, gap:" << gap << "ms";
            }
        }
    });
    m_frameIdleTimer->start(500);

    // P0-2: Pre-flight check timer (every 3 seconds)
    m_preflightTimer = new QTimer(this);
    connect(m_preflightTimer, &QTimer::timeout, this, &MainWindow::updatePreflightStatus);
    m_preflightTimer->start(3000);
    updatePreflightStatus();  // Initial check

    // Start connection manager and receiver
    m_connManager->start(9999);
    m_receiver->start(9999);

    // m_virtualCam->ensureRegistered();

    // MEDIUM-4 fix: removed hardcoded m_sharedWriter.open(1280, 720)
    // Now lazy-opened in onFrameDecoded with actual frame resolution

    // Window setup
    setWindowTitle("PhoneCam");
    resize(900, 640);
    setMinimumSize(700, 500);
}

MainWindow::~MainWindow() {
    // 1. Stop all timers first (prevent callbacks during destruction)
    if (m_statsTimer) {
        m_statsTimer->stop();
    }

    // 2. Disconnect all signals from decode worker to prevent queued calls to dead object
    if (m_decodeWorker) {
        disconnect(m_decodeWorker, nullptr, this, nullptr);
    }
    disconnect(m_receiver, nullptr, this, nullptr);
    disconnect(m_connManager, nullptr, this, nullptr);

    // 3. Stop receiver
    m_receiver->stop();

    // 4. Shutdown queue and stop decode thread
    m_legacyDisplayQueue->shutdown();
    if (m_decodeThread) {
        m_decodeThread->quit();
        m_decodeThread->wait(3000);
    }

    // 5. Close shared memory
    m_sharedWriter.close();

    // 6. 若修复进程仍在运行（例如窗口被关闭），终止它
    if (m_repairProc) {
        disconnect(m_repairProc, nullptr, this, nullptr);
        if (m_repairProc->state() != QProcess::NotRunning) {
            m_repairProc->kill();
            m_repairProc->waitForFinished(2000);
        }
        m_repairProc->deleteLater();
        m_repairProc = nullptr;
    }
}

void MainWindow::setupUi() {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);

    QWidget* central = new QWidget;
    central->setStyleSheet("background-color: #f5f6f8;");
    setCentralWidget(central);

    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── P0-2: Pre-flight check panel ──
    m_preflightPanel = new QWidget;
    QHBoxLayout* pfLayout = new QHBoxLayout(m_preflightPanel);
    pfLayout->setContentsMargins(12, 8, 12, 8);
    pfLayout->setSpacing(18);
    QLabel* pfTitle = new QLabel(QString::fromUtf8("状态检查"));
    pfTitle->setStyleSheet("font: 600 13px 'Segoe UI'; color: #2c3e50; background: transparent;");
    pfLayout->addWidget(pfTitle);
    pfLayout->addWidget(new QLabel("|"));
    const char* pfNames[] = {"手机推流", "PC 接收", "虚拟摄像头", "系统摄像头"};
    for (int i = 0; i < 4; i++) {
        m_preflightLabels[i] = new QLabel(QString::fromUtf8("\u2B1C %1").arg(pfNames[i]));
        m_preflightLabels[i]->setStyleSheet("font: 12px 'Segoe UI'; color: #8b95a5; background: transparent;");
        pfLayout->addWidget(m_preflightLabels[i]);
    }
    // 虚拟摄像头一键修复按钮：仅在注册异常时显示
    m_repairVcamBtn = new QPushButton(QString::fromUtf8("修复虚拟摄像头"));
    m_repairVcamBtn->setStyleSheet(
        "QPushButton { font: 11px 'Segoe UI'; color: #2b6cb0; background: #e8f0fe; "
        "border: 1px solid #90b4e0; border-radius: 3px; padding: 2px 10px; }"
        "QPushButton:hover { background: #d4e4fc; }"
        "QPushButton:disabled { color: #a0aec0; background: #f0f2f5; border-color: #d5d9e0; }");
    m_repairVcamBtn->setCursor(Qt::PointingHandCursor);
    m_repairVcamBtn->setVisible(false);
    connect(m_repairVcamBtn, &QPushButton::clicked, this, &MainWindow::onRepairVirtualCamera);
    pfLayout->addWidget(m_repairVcamBtn);
    pfLayout->addStretch();
    m_preflightPanel->setStyleSheet("background: #f8f9fa; border-bottom: 1px solid #e0e3e8;");
    mainLayout->addWidget(m_preflightPanel);

    // ── P0-3: Connection diagnostics bar ──
    m_diagLabel = new QLabel;
    m_diagLabel->setWordWrap(true);
    m_diagLabel->setStyleSheet(
        "QLabel { background: #fff8e1; border: 1px solid #ffecb3; border-radius: 3px; "
        "padding: 6px 12px; font: 12px 'Segoe UI'; color: #5d4037; }");
    m_diagLabel->setVisible(false);
    mainLayout->addWidget(m_diagLabel);

    // ── 顶部仪器面板 ──
    m_topPanel = new QFrame;
    QFrame* topPanel = (QFrame*)m_topPanel;
    topPanel->setFixedHeight(64);
    topPanel->setStyleSheet(
        "QFrame { background-color: #ffffff; border-bottom: 1px solid #e0e3e8; }");
    QHBoxLayout* topLayout = new QHBoxLayout(topPanel);
    topLayout->setContentsMargins(20, 0, 20, 0);
    topLayout->setSpacing(0);

    // 四列状态面板（像仪器读数）
    auto makeStatusCol = [&](const char* label, QLabel*& valueLabel) -> QVBoxLayout* {
        QVBoxLayout* col = new QVBoxLayout;
        col->setSpacing(2);
        QLabel* lbl = new QLabel(label);
        lbl->setStyleSheet("font: 10px 'Segoe UI'; color: #8b95a5; background: transparent;");
        col->addWidget(lbl);
        valueLabel = new QLabel("—");
        valueLabel->setStyleSheet("font: 600 13px 'Consolas'; color: #2c3e50; background: transparent;");
        col->addWidget(valueLabel);
        return col;
    };

    // ADB 状态
    m_statusDot = new QLabel;
    m_statusDot->setFixedSize(6, 6);
    m_statusDot->setStyleSheet("background-color: #bdc3c7; border-radius: 3px;");
    QVBoxLayout* adbCol = new QVBoxLayout;
    adbCol->setSpacing(2);
    QLabel* adbLabel = new QLabel(QString::fromUtf8("连接"));
    adbLabel->setStyleSheet("font: 10px 'Segoe UI'; color: #8b95a5; background: transparent;");
    adbCol->addWidget(adbLabel);
    QHBoxLayout* adbValueLayout = new QHBoxLayout;
    adbValueLayout->setSpacing(5);
    adbValueLayout->addWidget(m_statusDot);
    m_statusTitle = new QLabel("未连接");
    m_statusTitle->setStyleSheet("font: 600 13px 'Consolas'; color: #2c3e50; background: transparent;");
    adbValueLayout->addWidget(m_statusTitle);
    adbValueLayout->addStretch();
    adbCol->addLayout(adbValueLayout);
    topLayout->addLayout(adbCol);

    // 分隔线
    auto makeSep = []() -> QFrame* {
        QFrame* sep = new QFrame;
        sep->setFixedWidth(1);
        sep->setFixedHeight(32);
        sep->setStyleSheet("background-color: #e0e3e8;");
        return sep;
    };

    topLayout->addSpacing(24);
    topLayout->addWidget(makeSep());
    topLayout->addSpacing(24);

    // 设备状态
    QVBoxLayout* devCol = makeStatusCol("设备", m_statusDetail);
    topLayout->addLayout(devCol);

    topLayout->addSpacing(24);
    topLayout->addWidget(makeSep());
    topLayout->addSpacing(24);

    // 推流状态
    QVBoxLayout* streamCol = makeStatusCol("推流", m_streamLabel);
    m_streamLabel->setText("待机");
    topLayout->addLayout(streamCol);

    topLayout->addSpacing(24);
    topLayout->addWidget(makeSep());
    topLayout->addSpacing(24);

    // FPS
    QVBoxLayout* fpsCol = makeStatusCol("FPS", m_infoLabel);
    topLayout->addLayout(fpsCol);

    topLayout->addSpacing(24);
    topLayout->addWidget(makeSep());
    topLayout->addSpacing(24);

    // P1-1: 设备选择
    QVBoxLayout* devSelCol = new QVBoxLayout;
    devSelCol->setSpacing(2);
    QLabel* devSelLabel = new QLabel(QString::fromUtf8("设备"));
    devSelLabel->setStyleSheet("font: 10px 'Segoe UI'; color: #8b95a5; background: transparent;");
    devSelCol->addWidget(devSelLabel);
    QHBoxLayout* devSelRow = new QHBoxLayout;
    devSelRow->setSpacing(4);
    m_deviceCombo = new QComboBox;
    m_deviceCombo->setMinimumWidth(180);
    m_deviceCombo->setStyleSheet(
        "QComboBox { font: 11px 'Segoe UI'; color: #2c3e50; background: #f8f9fa; "
        "border: 1px solid #d5d9e0; border-radius: 3px; padding: 2px 8px; }"
        "QComboBox:hover { border-color: #b0b8c4; }");
    m_deviceCombo->addItem(QString::fromUtf8("自动选择"));
    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDeviceSelected);
    devSelRow->addWidget(m_deviceCombo);
    m_refreshBtn = new QPushButton(QString::fromUtf8("刷新"));
    m_refreshBtn->setStyleSheet(
        "QPushButton { font: 10px 'Segoe UI'; color: #4a5568; background: #f0f2f5; "
        "border: 1px solid #d5d9e0; border-radius: 3px; padding: 2px 8px; }"
        "QPushButton:hover { background: #e8eaef; }");
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshDevices);
    devSelRow->addWidget(m_refreshBtn);
    // P2-1 Loop 1: Manual IP connect button
    QPushButton* manualBtn = new QPushButton(QString::fromUtf8("手动连接"));
    manualBtn->setStyleSheet(
        "QPushButton { font: 10px 'Segoe UI'; color: #4a5568; background: #f0f2f5; "
        "border: 1px solid #d5d9e0; border-radius: 3px; padding: 2px 8px; }"
        "QPushButton:hover { background: #e8eaef; }");
    manualBtn->setCursor(Qt::PointingHandCursor);
    connect(manualBtn, &QPushButton::clicked, this, &MainWindow::onManualConnect);
    devSelRow->addWidget(manualBtn);

    // ADB 安装向导按钮（当 ADB 未安装时可手动补票）
    QPushButton* adbSetupBtn = new QPushButton(QString::fromUtf8("安装 ADB"));
    adbSetupBtn->setStyleSheet(
        "QPushButton { font: 10px 'Segoe UI'; color: #2b6cb0; background: #e8f0fe; "
        "border: 1px solid #90b4e0; border-radius: 3px; padding: 2px 8px; }"
        "QPushButton:hover { background: #d6e6fa; }");
    adbSetupBtn->setCursor(Qt::PointingHandCursor);
    connect(adbSetupBtn, &QPushButton::clicked, this, &MainWindow::onInstallAdb);
    devSelRow->addWidget(adbSetupBtn);

    devSelCol->addLayout(devSelRow);
    topLayout->addLayout(devSelCol);

    topLayout->addStretch();

    mainLayout->addWidget(topPanel);

    // ── 预览区域（白框包围，边界清楚） ──
    QFrame* previewFrame = new QFrame;
    previewFrame->setStyleSheet(
        "QFrame { background-color: #ffffff; margin: 12px; border: 1px solid #d5d9e0; border-radius: 4px; }");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewFrame);
    previewLayout->setContentsMargins(1, 1, 1, 1);
    previewLayout->setSpacing(0);
        m_preview = new PreviewWidget;
    connect(m_preview, &PreviewWidget::doubleClicked, this, &MainWindow::toggleFullScreen);
    previewLayout->addWidget(m_preview);
    mainLayout->addWidget(previewFrame, 1);

    // ── 底部工具栏 ──
    m_sidebarPanel = new QFrame;
    QFrame* toolbar = (QFrame*)m_sidebarPanel;
    toolbar->setFixedHeight(48);
    toolbar->setStyleSheet(
        "QFrame { background-color: #ffffff; border-top: 1px solid #e0e3e8; }");
    QHBoxLayout* toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(16, 0, 16, 0);
    toolLayout->setSpacing(8);

    // 统一按钮样式（科研仪器风：白底、细边框、深灰文字）
    static const char* btnStyle =
        "QPushButton {"
        "  background: #ffffff; color: #4a5568; border: 1px solid #d5d9e0;"
        "  padding: 5px 16px; border-radius: 3px;"
        "  font: 12px 'Segoe UI';"
        "}"
        "QPushButton:hover { background: #f0f2f5; border-color: #b0b8c4; }"
        "QPushButton:pressed { background: #e8eaef; }";

    // 激活态：低饱和蓝
    static const char* btnActiveStyle =
        "QPushButton {"
        "  background: #e8f0fe; color: #2b6cb0; border: 1px solid #90b4e0;"
        "  padding: 5px 16px; border-radius: 3px;"
        "  font: 12px 'Segoe UI';"
        "}"
        "QPushButton:hover { background: #d4e4fc; }";

    // 退出：红色描边（仅危险操作用红色）
    static const char* btnDangerStyle =
        "QPushButton {"
        "  background: #ffffff; color: #718096; border: 1px solid #d5d9e0;"
        "  padding: 5px 16px; border-radius: 3px;"
        "  font: 12px 'Segoe UI';"
        "}"
        "QPushButton:hover { background: #fff5f5; color: #c53030; border-color: #e8a0a0; }";

    toolLayout->addStretch();

    auto makeBtn = [&](const QString& text, const char* style) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(style);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(30);
        return btn;
    };

    // P1-2: 画面设置分组标签
    QLabel* imgSettingsLabel = new QLabel(QString::fromUtf8("画面设置:"));
    imgSettingsLabel->setStyleSheet("font: 11px 'Segoe UI'; color: #8b95a5; background: transparent;");
    toolLayout->addWidget(imgSettingsLabel);

    m_mirrorBtn = makeBtn("镜像", btnStyle);
    connect(m_mirrorBtn, &QPushButton::clicked, this, &MainWindow::onMirrorToggled);
    toolLayout->addWidget(m_mirrorBtn);

    m_flipBtn = makeBtn("翻转", btnStyle);
    connect(m_flipBtn, &QPushButton::clicked, this, &MainWindow::onFlipToggled);
    toolLayout->addWidget(m_flipBtn);

    m_rotateBtn = makeBtn("旋转 0°", btnStyle);
    connect(m_rotateBtn, &QPushButton::clicked, this, &MainWindow::onRotationToggled);
    toolLayout->addWidget(m_rotateBtn);

    // P1-2: 预览输出说明
    QLabel* previewNote = new QLabel(QString::fromUtf8("预览 = 虚拟摄像头输出"));
    previewNote->setStyleSheet("font: 10px 'Segoe UI'; color: #a0aec0; background: transparent;");
    toolLayout->addWidget(previewNote);

    toolLayout->addStretch();

    m_exportLogBtn = makeBtn(QString::fromUtf8("导出日志"), btnStyle);
    connect(m_exportLogBtn, &QPushButton::clicked, this, &MainWindow::onExportLogs);
    toolLayout->addWidget(m_exportLogBtn);

    m_quitBtn = makeBtn("退出", btnDangerStyle);
    connect(m_quitBtn, &QPushButton::clicked, qApp, &QApplication::quit);
    toolLayout->addWidget(m_quitBtn);

    toolLayout->addStretch();

    mainLayout->addWidget(toolbar);
}

void MainWindow::startPipeline() {
    // Initialize decoder
    if (!m_decoder->init()) {
        qWarning() << "[MAIN] Failed to initialize H.264 decoder";
    }

    // Create decode thread
    m_decodeThread = new QThread(this);
    m_decodeWorker = new DecodeWorker(m_decoder, m_composer, m_legacyDisplayQueue);
    m_decodeWorker->setUseLegacyCompose(m_useLegacyCompose);
    m_decodeWorker->moveToThread(m_decodeThread);
    connect(m_decodeThread, &QThread::finished, m_decodeWorker, &QObject::deleteLater);

    // Wire: PcpReceiver → DecodeWorker
    connect(m_receiver, &PcpReceiver::frameReceived,
            m_decodeWorker, &DecodeWorker::decodeFrame, Qt::QueuedConnection);

    // Wire: DecodeWorker → MainWindow (NV12 primary path)
    connect(m_decodeWorker, &DecodeWorker::finalFrameReady,
            this, &MainWindow::onFinalFrameReady, Qt::QueuedConnection);

    // Wire: DecodeWorker → MainWindow (legacy QImage fallback path)
    connect(m_decodeWorker, &DecodeWorker::frameDecoded,
            this, &MainWindow::onFrameDecoded, Qt::QueuedConnection);

    m_decodeThread->start();
}

void MainWindow::onConnectionStateChanged(const ConnectionInfo& info) {
    switch (info.state) {
    case ConnectionState::Searching:
        m_statusDot->setStyleSheet("background-color: #bdc3c7; border-radius: 3px;");
        m_statusTitle->setText("未连接");
        m_statusDetail->setText("搜索中");
        updateDiagnosticsBar();
        break;
    case ConnectionState::WaitingForPhone:
        m_statusDot->setStyleSheet("background-color: #e67e22; border-radius: 3px;");
        m_statusTitle->setText(QString::fromUtf8("已发现设备"));
        m_statusDetail->setText(QString::fromUtf8("等待推流"));
        updateDiagnosticsBar();
        break;
    case ConnectionState::Connected:
        m_statusDot->setStyleSheet("background-color: #27ae60; border-radius: 3px;");
        m_statusTitle->setText("已连接");
        // BUG-007: Show active candidate displayName, not just connectionType
        m_statusDetail->setText(activeDeviceDisplayName());
        m_diagLabel->setVisible(false);  // Hide diagnostics when connected
        break;
    case ConnectionState::Disconnected:
        m_statusDot->setStyleSheet("background-color: #c53030; border-radius: 3px;");
        m_statusTitle->setText("断开");
        // 8月9日修复 A: 选择不兼容设备时显示版本不兼容原因 (而非"重连中")
        m_statusDetail->setText(info.error.isEmpty()
            ? QString::fromUtf8("重连中")
            : info.error);
        updateDiagnosticsBar();
        break;
    default:
        break;
    }
}

void MainWindow::onFinalFrameReady(const Nv12Frame& frame) {
    // Primary NV12 path: update preview + write to shared memory
    m_frameCount++;
    m_nv12FrameCount++;

    // Frame age measurement (ms since PC received the packet)
    const double nowMs = QDateTime::currentMSecsSinceEpoch() * 1.0;
    const double frameAgeMs = nowMs - frame.receive_ms;

    // Frame idle detection: update last frame time and reset camera switching flag
    m_lastFrameTimeMs = QDateTime::currentMSecsSinceEpoch();
    if (m_cameraSwitchingDetected) {
        m_cameraSwitchingDetected = false;
        qDebug() << "[IDLE] Frame received, camera switch complete";
    }

    // BUG-007: On first frame, transition UI to streaming state
    if (!m_streamEstablished) {
        enterStreamingState();
    }

    // Update NV12 OpenGL preview
    m_preview->updateNv12Frame(frame);

    // Write NV12 to shared memory for virtual camera DLL
    if (!m_sharedWriter.is_open() || m_last_width != frame.width || m_last_height != frame.height) {
        if (m_sharedWriter.is_open()) {
            m_sharedWriter.close();
            qDebug() << "[MAIN] SharedMemoryWriter closed due to resolution change";
        }
        m_sharedWriter.open(frame.width, frame.height);
        m_last_width = frame.width;
        m_last_height = frame.height;
        qDebug() << "[MAIN] SharedMemoryWriter opened:" << frame.width << "x" << frame.height;
    }
    if (m_sharedWriter.is_open()) {
        m_sharedWriter.writeNv12(
            reinterpret_cast<const uint8_t*>(frame.data.constData()),
            frame.width, frame.height);
    }
}

void MainWindow::onFrameDecoded(const QImage& image) {
    // Legacy fallback path: QImage → BGR24 → shared memory
    // Only used when mirror/flip/manualRotation active or --legacy-qimage-compose
    m_frameCount++;
    m_legacyFrameCount++;

    // BUG-007: On first frame, transition UI to streaming state
    if (!m_streamEstablished) {
        enterStreamingState();
    }

    // Write BGR frame to shared memory for virtual camera DLL
    if (!m_sharedWriter.is_open() || m_last_width != image.width() || m_last_height != image.height()) {
        if (m_sharedWriter.is_open()) {
            m_sharedWriter.close();
            qDebug() << "[MAIN] SharedMemoryWriter closed due to resolution change";
        }
        m_sharedWriter.open(image.width(), image.height());
        m_last_width = image.width();
        m_last_height = image.height();
        qDebug() << "[MAIN] SharedMemoryWriter opened (legacy):" << image.width() << "x" << image.height();
    }
    if (m_sharedWriter.is_open()) {
        QImage bgr = image.convertToFormat(QImage::Format_RGB888).rgbSwapped();
        m_sharedWriter.write(bgr.bits(), bgr.width(), bgr.height());
    }
}

void MainWindow::onMirrorToggled() {
    m_mirror = !m_mirror;
    if (m_decodeWorker) m_decodeWorker->setTransformState(m_mirror, m_flip, m_rotation);
    m_mirrorBtn->setStyleSheet(m_mirror ?
        "QPushButton { background: #e8f0fe; color: #2b6cb0; border: 1px solid #90b4e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #d4e4fc; }" :
        "QPushButton { background: #ffffff; color: #4a5568; border: 1px solid #d5d9e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #f0f2f5; border-color: #b0b8c4; }");
}

void MainWindow::onFlipToggled() {
    m_flip = !m_flip;
    if (m_decodeWorker) m_decodeWorker->setTransformState(m_mirror, m_flip, m_rotation);
    m_flipBtn->setStyleSheet(m_flip ?
        "QPushButton { background: #e8f0fe; color: #2b6cb0; border: 1px solid #90b4e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #d4e4fc; }" :
        "QPushButton { background: #ffffff; color: #4a5568; border: 1px solid #d5d9e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #f0f2f5; border-color: #b0b8c4; }");
}

void MainWindow::onRotationToggled() {
    m_rotation = (m_rotation + 90) % 360;
    if (m_decodeWorker) m_decodeWorker->setTransformState(m_mirror, m_flip, m_rotation);
    m_rotateBtn->setText(QString("旋转 %1°").arg(m_rotation));
    m_rotateBtn->setStyleSheet(m_rotation != 0 ?
        "QPushButton { background: #e8f0fe; color: #2b6cb0; border: 1px solid #90b4e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #d4e4fc; }" :
        "QPushButton { background: #ffffff; color: #4a5568; border: 1px solid #d5d9e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #f0f2f5; border-color: #b0b8c4; }");
}

void MainWindow::onResolutionChanged(const QString& text) {
    Q_UNUSED(text)
    // TODO: notify camera to change resolution
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void MainWindow::onStatsTimer() {
    int fps = m_frameCount;
    m_lastFps = fps;  // BUG-007: save before reset for preflight check
    m_frameCount = 0;

    QStringList parts;
    parts << QString("接收: %1fps").arg(fps);
    parts << QString("NV12: %1").arg(m_nv12FrameCount);
    if (m_legacyFrameCount > 0) {
        parts << QString("Legacy: %1").arg(m_legacyFrameCount);
    }
    m_infoLabel->setText(parts.join(" | "));
}

void MainWindow::setForceSoftwareDecode(bool force) {
    // TODO: wire to HwDecoder
    Q_UNUSED(force)
}

void MainWindow::setUseLegacyCompose(bool legacy) {
    m_useLegacyCompose = legacy;
}

void MainWindow::enableH264Dump(const QString& path) {
    // TODO: wire to PcpReceiver dump
    Q_UNUSED(path)
}

void MainWindow::enableCanonicalDump(const QString& path) {
    m_canonicalDumpPath = path;
}

void MainWindow::toggleFullScreen() {
    m_isFullscreen = !m_isFullscreen;
    if (m_isFullscreen) {
        if (m_topPanel) m_topPanel->hide();
        if (m_sidebarPanel) m_sidebarPanel->hide();
        showFullScreen();
    } else {
        if (m_topPanel) m_topPanel->show();
        if (m_sidebarPanel) m_sidebarPanel->show();
        showNormal();
    }
}

void MainWindow::onDiagnosticsChanged(const ConnectionDiagnostics& diag) {
    m_lastDiag = diag;
    // Connection status tooltip
    QStringList lines;
    lines << QString::fromUtf8("ADB: %1").arg(diag.adbStatus);
    if (diag.discoveryStatus == "no-interfaces" || diag.discoveryStatus == "send-failed") {
        lines << QString::fromUtf8("UDP discovery: 未发现可用于 PhoneCam 自动发现的局域网接口");
    } else {
        lines << QString::fromUtf8("UDP discovery: %1 (%2 个接口)")
            .arg(diag.discoveryStatus, QString::number(diag.discoveryInterfaces.size()));
        for (const auto& i : diag.discoveryInterfaces) {
            lines << QString("  %1").arg(i);
        }
    }
    m_statusDetail->setToolTip(lines.join("\n"));
    updateDiagnosticsBar();
}

void MainWindow::updateDiagnosticsBar() {
    if (!m_diagLabel) return;
    // BUG-007 safety: if streaming is established, always hide diagnostics
    if (m_streamEstablished) {
        m_diagLabel->setVisible(false);
        return;
    }
    if (m_connManager->info().state == ConnectionState::Connected) {
        m_diagLabel->setVisible(false);
        return;
    }
    QStringList msgs;
    const auto& diag = m_lastDiag;
    // USB/ADB diagnostics
    if (diag.adbStatus == "not found") {
        msgs << QString::fromUtf8("⚠ adb 未安装 — 请安装 Android Platform Tools 或使用 WiFi/热点连接");
    } else if (diag.adbStatus == "no devices") {
        msgs << QString::fromUtf8("⚠ 未检测到 USB 设备 — 请连接手机并开启 USB 调试");
    } else {
        for (const auto& line : diag.adbDevices) {
            if (line.contains("unauthorized")) {
                msgs << QString::fromUtf8("❌ USB 设备未授权 — 请在手机上点击「允许 USB 调试」");
            } else if (line.contains("offline")) {
                msgs << QString::fromUtf8("❌ USB 设备离线 — 请拔插 USB 线或重启 USB 调试");
            }
        }
    }
    // USB forward diagnostics
    for (const auto& c : m_connManager->candidates()) {
        if (c.transport == "usb" && c.status == "Failed") {
            msgs << QString::fromUtf8("❌ USB 转发失败: %1").arg(c.lastError);
        }
    }
    // WiFi/Hotspot diagnostics (UDP PhoneCam Discovery V1)
    if (diag.discoveryStatus == "no-interfaces" || diag.discoveryStatus == "send-failed") {
        msgs << QString::fromUtf8("⚠ 未发现可用于 PhoneCam 自动发现的局域网接口");
    } else if (diag.discoveryStatus == "ok-no-devices") {
        msgs << QString::fromUtf8("⚠ 未发现 PhoneCam — 请确认手机端已点击开始推流，并确保电脑已连接手机热点或与手机处于同一局域网；仍无法发现时可使用 USB 或手动连接");
    }
    // "ok-found": 下拉框中已有真实 PhoneCam, 无需额外提示
    // P2-1 Loop 3: Manual device failure diagnostics
    for (const auto& c : m_connManager->candidates()) {
        if (c.transport == "manual" && c.status == "Failed") {
            msgs << QString::fromUtf8("❌ 手动连接 %1 失败: %2 — 请确认手机端已开始推流且 IP 正确")
                .arg(c.displayName, c.lastError);
        }
    }
    if (msgs.isEmpty()) {
        m_diagLabel->setVisible(false);
    } else {
        msgs.removeDuplicates();
        m_diagLabel->setText(msgs.join("  |  "));
        m_diagLabel->setVisible(true);
    }
}

void MainWindow::onDeviceSelected(int index) {
    if (index <= 0) {
        // "自动选择" selected
        m_connManager->selectDevice("");
    } else {
        QString deviceId = m_deviceCombo->itemData(index).toString();
        m_connManager->selectDevice(deviceId);
    }
}

void MainWindow::onRefreshDevices() {
    m_connManager->refreshDevices();
}

void MainWindow::onManualConnect() {
    bool ok = false;
    QString text = QInputDialog::getText(this,
        QString::fromUtf8("手动连接"),
        QString::fromUtf8("输入手机 IP 地址和端口\n格式: 192.168.x.x:9999"),
        QLineEdit::Normal,
        QString::fromUtf8("192.168.1.100:9999"),
        &ok);
    if (!ok || text.trimmed().isEmpty()) return;

    QString input = text.trimmed();

    // Parse host:port
    QString host;
    quint16 port = 9999;
    if (input.contains(':')) {
        QStringList parts = input.split(':');
        host = parts[0].trimmed();
        bool portOk = false;
        int p = parts[1].trimmed().toInt(&portOk);
        if (!portOk || p < 1 || p > 65535) {
            QMessageBox::warning(this, QString::fromUtf8("输入错误"),
                QString::fromUtf8("端口必须是 1-65535 之间的数字"));
            return;
        }
        port = static_cast<quint16>(p);
    } else {
        host = input;
    }

    // Validate host
    if (host.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("输入错误"),
            QString::fromUtf8("IP 地址不能为空"));
        return;
    }

    // Add manual device
    m_connManager->addManualDevice(host, port);
    qDebug() << "[CONN] Manual device added:" << host << ":" << port;

    // Auto-select the manual device
    QString manualId = QString("manual:%1:%2").arg(host).arg(port);
    m_connManager->selectDevice(manualId);
}

void MainWindow::onInstallAdb() {
    // 启动与 phonecam.exe 同目录下的 ADB 安装向导
    QString appDir = QCoreApplication::applicationDirPath();
    QString setupPath = QDir(appDir).absoluteFilePath("phonecam-adb-setup.exe");

    if (!QFileInfo::exists(setupPath)) {
        QMessageBox::warning(this, QString::fromUtf8("未找到安装向导"),
            QString::fromUtf8("找不到 ADB 安装向导：%1\n请重新安装 PhoneCam。").arg(setupPath));
        return;
    }

    bool started = QProcess::startDetached(setupPath, QStringList(), appDir);
    if (!started) {
        QMessageBox::critical(this, QString::fromUtf8("启动失败"),
            QString::fromUtf8("无法启动 ADB 安装向导：%1").arg(setupPath));
    }
}

void MainWindow::onExportLogs() {
    m_exportLogBtn->setEnabled(false);
    m_exportLogBtn->setText(QString::fromUtf8("导出中..."));

    // 1. Build timestamp and paths
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString zipPath = desktop + "/phonecam-log-" + ts + ".zip";
    QString tmpDir = desktop + "/phonecam-log-tmp-" + ts;

    QDir dir;
    if (!dir.mkpath(tmpDir)) {
        QMessageBox::warning(this, QString::fromUtf8("导出失败"),
            QString::fromUtf8("无法创建临时目录: %1").arg(tmpDir));
        m_exportLogBtn->setEnabled(true);
        m_exportLogBtn->setText(QString::fromUtf8("导出日志"));
        return;
    }

    // 2. Collect PC logs
    QString pcLogDir = QDir::currentPath() + "/logs";
    int logCount = 0;
    {
        QDir logDir(pcLogDir);
        if (logDir.exists()) {
            QStringList pcLogs = logDir.entryList(QStringList() << "phonecam-pc-*.log", QDir::Files);
            for (const auto& f : pcLogs) {
                QFile::copy(pcLogDir + "/" + f, tmpDir + "/" + f);
                logCount++;
            }
        }
    }

    // 3. Collect VCAM logs
    {
        QDir logDir(pcLogDir);
        if (logDir.exists()) {
            QStringList vcamLogs = logDir.entryList(QStringList() << "phonecam-vcam-*.log", QDir::Files);
            for (const auto& f : vcamLogs) {
                QFile::copy(pcLogDir + "/" + f, tmpDir + "/" + f);
                logCount++;
            }
        }
    }

    // 4. Collect install/uninstall logs
    {
        QString releaseLogDir = QDir::currentPath() + "/../release/PhoneCam/logs";
        QDir rDir(releaseLogDir);
        if (rDir.exists()) {
            QStringList releaseLogs = rDir.entryList(QStringList() << "*.log", QDir::Files);
            for (const auto& f : releaseLogs) {
                QFile::copy(releaseLogDir + "/" + f, tmpDir + "/install-" + f);
                logCount++;
            }
        }
    }

    // 5. Write system-info.txt
    {
        QFile f(tmpDir + "/system-info.txt");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
#ifdef Q_OS_WIN
            // Windows version from registry
            QProcess verProc;
            verProc.start("cmd", QStringList() << "/c" << "ver");
            verProc.waitForFinished(3000);
            ts << "Windows: " << verProc.readAllStandardOutput().trimmed() << "\n";
#endif
            // ADB version（使用 ConnectionManager 找到的 ADB 路径）
            QString adbPath = m_connManager->adbPath();
            if (!adbPath.isEmpty()) {
                QProcess adbProc;
                adbProc.start(adbPath, QStringList() << "version");
                adbProc.waitForFinished(3000);
                ts << "ADB: " << adbProc.readAllStandardOutput().trimmed() << "\n";
            } else {
                ts << "ADB: not found\n";
            }

            ts << "Exe path: " << QCoreApplication::applicationFilePath() << "\n";
            ts << "Working dir: " << QDir::currentPath() << "\n";
            ts << "Qt version: " << qVersion() << "\n";
            ts << "Export time: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
            f.close();
        }
    }

    // 6. Write diagnostics.txt (connection state snapshot)
    {
        QFile f(tmpDir + "/diagnostics.txt");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << "=== Connection Diagnostics ===\n";
            ts << "ADB status: " << m_lastDiag.adbStatus << "\n";
            ts << "Local NICs: " << m_lastDiag.localNics.join(", ") << "\n";
            ts << "Discovery status: " << m_lastDiag.discoveryStatus << "\n";
            ts << "Discovery interfaces:\n";
            for (const auto& i : m_lastDiag.discoveryInterfaces) {
                ts << "  " << i << "\n";
            }
            ts << "ADB devices:\n";
            for (const auto& d : m_lastDiag.adbDevices) {
                ts << "  " << d << "\n";
            }
            ts << "\nCandidates:\n";
            for (const auto& c : m_connManager->candidates()) {
                ts << "  " << c.transport << " - " << c.displayName
                   << " [" << c.status << "]";
                if (!c.lastError.isEmpty()) ts << " error: " << c.lastError;
                ts << "\n";
            }
            ts << "\nStream established: " << (m_streamEstablished ? "yes" : "no") << "\n";

            // Virtual Camera 健康诊断（不写“腾讯会议可见”这类无法证明的承诺）
            ts << "\n=== Virtual Camera ===\n";
            const VirtualCameraHealth vcam = checkVirtualCameraHealth();
            QString vcamHealthText;
            switch (vcam.status()) {
            case VirtualCameraHealth::Status::Healthy: vcamHealthText = QStringLiteral("Healthy"); break;
            case VirtualCameraHealth::Status::MissingRegistration: vcamHealthText = QStringLiteral("MissingRegistration"); break;
            case VirtualCameraHealth::Status::MissingDll: vcamHealthText = QStringLiteral("MissingDll"); break;
            case VirtualCameraHealth::Status::WrongPath: vcamHealthText = QStringLiteral("WrongPath"); break;
            case VirtualCameraHealth::Status::DirectShowMissing: vcamHealthText = QStringLiteral("DirectShowMissing"); break;
            }
            ts << "Health: " << vcamHealthText << "\n";
            ts << "Expected DLL: " << vcam.expectedPath << "\n";
            ts << "Registered DLL: " << vcam.registeredPath << "\n";
            ts << "DLL exists: " << (vcam.dllExists ? "yes" : "no") << "\n";
            ts << "Path matches: " << (vcam.pathMatchesCurrentInstall ? "yes" : "no") << "\n";
            ts << "DirectShow registered: " << (vcam.directShowRegistered ? "yes" : "no") << "\n";

            // Preflight status
            ts << "\n=== Preflight ===\n";
            for (int i = 0; i < 4; i++) {
                ts << m_preflightLabels[i]->text() << "\n";
            }
            f.close();
        }
    }

    // 7. If no log files found, write a note
    if (logCount == 0) {
        QFile f(tmpDir + "/no-logs-found.txt");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << "未找到 PhoneCam 日志文件。\n";
            ts << "已搜索目录: " << pcLogDir << "\n";
            ts << "日志文件在首次运行 PhoneCam 后自动生成。\n";
            f.close();
        }
    }

    // 8. Create zip using PowerShell Compress-Archive
    QProcess zipProc;
    QString psCmd = QString(
        "Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force"
    ).arg(tmpDir, zipPath);
    zipProc.start("powershell.exe", QStringList()
        << "-NoProfile" << "-Command" << psCmd);
    zipProc.waitForFinished(15000);
    int zipExit = zipProc.exitCode();

    // 9. Cleanup temp dir
    QDir(tmpDir).removeRecursively();

    // 10. Report result
    m_exportLogBtn->setEnabled(true);
    m_exportLogBtn->setText(QString::fromUtf8("导出日志"));

    if (zipExit == 0 && QFileInfo::exists(zipPath)) {
        QMessageBox::information(this, QString::fromUtf8("导出成功"),
            QString::fromUtf8("日志已导出到:\n%1\n\n包含 %2 个日志文件").arg(zipPath).arg(logCount));
    } else {
        QString errMsg = zipProc.readAllStandardError();
        QMessageBox::warning(this, QString::fromUtf8("导出失败"),
            QString::fromUtf8("压缩失败 (exit=%1):\n%2").arg(zipExit).arg(errMsg));
    }
}

// ── BUG-007: GUI state sync helpers ──

QString MainWindow::activeDeviceDisplayName() const {
    QString activeId = m_connManager->activeDeviceId();
    if (!activeId.isEmpty()) {
        for (const auto& c : m_connManager->candidates()) {
            if (c.id == activeId) return c.displayName;
        }
    }
    // Fallback: use connection type or generic label
    auto info = m_connManager->info();
    if (!info.connectionType.isEmpty()) return info.connectionType;
    return QString::fromUtf8("已接收帧");
}

void MainWindow::enterStreamingState() {
    if (m_streamEstablished) return;  // already in streaming state
    m_streamEstablished = true;
    m_connManager->confirmStreamActive();
    m_diagLabel->setVisible(false);
    m_statusTitle->setText(QString::fromUtf8("已连接"));
    m_statusDetail->setText(activeDeviceDisplayName());
    if (m_streamLabel) m_streamLabel->setText(QString::fromUtf8("推流中"));
}

void MainWindow::exitStreamingState() {
    m_streamEstablished = false;
    if (m_streamLabel) m_streamLabel->setText(QString::fromUtf8("待机"));
}

void MainWindow::updatePreflightStatus() {
    if (!m_preflightPanel) return;
    // Item 1: 手机已推流
    bool phoneStreaming = m_streamEstablished;
    m_preflightLabels[0]->setText(phoneStreaming ?
        QString::fromUtf8("✅ 手机推流") : QString::fromUtf8("❌ 手机推流"));
    m_preflightLabels[0]->setStyleSheet(phoneStreaming ?
        "font: 12px 'Segoe UI'; color: #27ae60; background: transparent;" :
        "font: 12px 'Segoe UI'; color: #c53030; background: transparent;");
    // Item 2: PC 已接收帧 (BUG-007: use m_lastFps to avoid reset-window false negative)
    bool pcReceiving = m_lastFps > 0 || m_frameCount > 0;
    m_preflightLabels[1]->setText(pcReceiving ?
        QString::fromUtf8("✅ PC 接收") : QString::fromUtf8("❌ PC 接收"));
    m_preflightLabels[1]->setStyleSheet(pcReceiving ?
        "font: 12px 'Segoe UI'; color: #27ae60; background: transparent;" :
        "font: 12px 'Segoe UI'; color: #c53030; background: transparent;");

    // Item 3/4: 虚拟摄像头健康检查（只读，64-bit registry view）
    VirtualCameraHealth health = checkVirtualCameraHealth();
    bool vcamComOk = health.comRegistered && health.dllExists && health.pathMatchesCurrentInstall;
    m_preflightLabels[2]->setText(vcamComOk ?
        QString::fromUtf8("✅ 虚拟摄像头") : QString::fromUtf8("❌ 虚拟摄像头"));
    m_preflightLabels[2]->setStyleSheet(vcamComOk ?
        "font: 12px 'Segoe UI'; color: #27ae60; background: transparent;" :
        "font: 12px 'Segoe UI'; color: #c53030; background: transparent;");
    // Item 4: PhoneCam Camera 已进入 Windows DirectShow VideoInputDeviceCategory
    m_preflightLabels[3]->setText(health.directShowRegistered ?
        QString::fromUtf8("✅ 系统摄像头") : QString::fromUtf8("❌ 系统摄像头"));
    m_preflightLabels[3]->setStyleSheet(health.directShowRegistered ?
        "font: 12px 'Segoe UI'; color: #27ae60; background: transparent;" :
        "font: 12px 'Segoe UI'; color: #c53030; background: transparent;");

    // 修复按钮：修复进行中 → 禁用并显示进度；健康 → 隐藏；异常 → 显示
    if (m_repairVcamBtn) {
        bool repairing = m_repairProc && m_repairProc->state() != QProcess::NotRunning;
        if (repairing) {
            m_repairVcamBtn->setVisible(true);
            m_repairVcamBtn->setEnabled(false);
            m_repairVcamBtn->setText(QString::fromUtf8("修复中..."));
        } else if (health.healthy()) {
            m_repairVcamBtn->setVisible(false);
        } else {
            m_repairVcamBtn->setVisible(true);
            m_repairVcamBtn->setEnabled(true);
            m_repairVcamBtn->setText(QString::fromUtf8("修复虚拟摄像头"));
            // 已注册但会议软件没刷新时，通过 tooltip 说明 PhoneCam 只能保证系统层面注册
            m_repairVcamBtn->setToolTip(
                QString::fromUtf8("如果会议软件在安装/修复前已打开，请完全退出并重新打开会议软件后重新选择摄像头。"));
        }
    }

    // Overall status
    bool allGreen = phoneStreaming && pcReceiving && vcamComOk && health.directShowRegistered;
    m_preflightPanel->setStyleSheet(allGreen ?
        "background: #e8f5e9; border-bottom: 1px solid #c8e6c9;" :
        "background: #f8f9fa; border-bottom: 1px solid #e0e3e8;");
}

// ── 虚拟摄像头健康检查（只读，不修改任何注册项） ──

VirtualCameraHealth MainWindow::checkVirtualCameraHealth() const {
    VirtualCameraHealth health;
    health.expectedPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("phonecam-virtualcam.dll"));

#ifdef Q_OS_WIN
    // PhoneCam PC 与虚拟摄像头 DLL 均为 x64：显式读取 64-bit registry view，
    // 避免 WOW64 下误判。
    const REGSAM viewFlag = KEY_WOW64_64KEY;

    // 1. CLSID/InprocServer32 是否存在，并读取默认值（注册的 DLL path）
    wchar_t valueBuf[1024] = {0};
    DWORD valueSize = sizeof(valueBuf);
    HKEY hKey = nullptr;
    LONG lr = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}\\InprocServer32",
        0, KEY_READ | viewFlag, &hKey);
    if (lr == ERROR_SUCCESS) {
        health.comRegistered = true;
        DWORD type = 0;
        valueSize = sizeof(valueBuf);
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type,
                             (LPBYTE)valueBuf, &valueSize) == ERROR_SUCCESS &&
            type == REG_SZ && valueBuf[0] != L'\0') {
            health.registeredPath = QString::fromWCharArray(valueBuf);
        }
        RegCloseKey(hKey);
    }

    // 2. 注册路径指向的文件是否实际存在
    if (!health.registeredPath.isEmpty()) {
        health.dllExists = QFileInfo::exists(health.registeredPath);
    }

    // 3. 注册路径是否指向当前安装目录的 DLL（大小写不敏感，统一斜杠）
    if (!health.registeredPath.isEmpty()) {
        const QString regPath = QDir::cleanPath(QDir::fromNativeSeparators(health.registeredPath));
        const QString expPath = QDir::cleanPath(QDir::fromNativeSeparators(health.expectedPath));
        health.pathMatchesCurrentInstall = regPath.compare(expPath, Qt::CaseInsensitive) == 0;
    }

    // 4. DirectShow VideoInputDeviceCategory 中是否存在 PhoneCam Camera
    HKEY hInstance = nullptr;
    lr = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\\Instance",
        0, KEY_READ | viewFlag, &hInstance);
    if (lr == ERROR_SUCCESS) {
        wchar_t subKeyName[256];
        DWORD index = 0;
        for (;;) {
            DWORD nameLen = 256;
            LONG subLr = RegEnumKeyExW(hInstance, index++, subKeyName, &nameLen,
                                       nullptr, nullptr, nullptr, nullptr);
            if (subLr == ERROR_NO_MORE_ITEMS) break;
            if (subLr != ERROR_SUCCESS) continue;
            HKEY hSub = nullptr;
            if (RegOpenKeyExW(hInstance, subKeyName, 0, KEY_READ | viewFlag, &hSub) == ERROR_SUCCESS) {
                wchar_t friendlyName[512] = {0};
                DWORD size = sizeof(friendlyName);
                DWORD type = 0;
                if (RegQueryValueExW(hSub, L"FriendlyName", nullptr, &type,
                                     (LPBYTE)friendlyName, &size) == ERROR_SUCCESS &&
                    type == REG_SZ &&
                    wcsstr(friendlyName, L"PhoneCam Camera") != nullptr) {
                    health.directShowRegistered = true;
                }
                RegCloseKey(hSub);
            }
            if (health.directShowRegistered) break;
        }
        RegCloseKey(hInstance);
    }
#endif
    return health;
}

// ── 虚拟摄像头一键修复（异步 + UAC） ──

void MainWindow::onRepairVirtualCamera() {
    if (m_repairProc && m_repairProc->state() != QProcess::NotRunning) return;

    // 修复永远针对当前程序目录的 DLL，不使用旧注册路径
    const QString dllPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("phonecam-virtualcam.dll"));
    if (!QFileInfo::exists(dllPath)) {
        QMessageBox::warning(this, QString::fromUtf8("无法修复"),
            QString::fromUtf8("未找到 phonecam-virtualcam.dll，请重新安装 PhoneCam。"));
        return;
    }

    m_repairVcamBtn->setEnabled(false);
    m_repairVcamBtn->setText(QString::fromUtf8("修复中..."));

    // 通过 PowerShell Start-Process -Verb RunAs 触发 UAC，异步等待 regsvr32 完成。
    // regsvr32 从 system32 启动 → 64-bit 版本，与 DLL bitness 匹配。
    // 用户取消 UAC 时 PowerShell 抛错、无 "EXIT=" 输出 → 可区分"取消"与"注册失败"。
    QString psCmd = QStringLiteral(
        "$dll='%1'; "
        "$p=Start-Process -FilePath regsvr32.exe -ArgumentList ('/s \"'+$dll+'\"') "
        "-Verb RunAs -Wait -PassThru -ErrorAction Stop; "
        "Write-Output ('EXIT='+$p.ExitCode); exit 0").arg(dllPath);

    m_repairProc = new QProcess(this);
    connect(m_repairProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onRepairFinished);
    m_repairProc->start(QStringLiteral("powershell.exe"), QStringList()
        << QStringLiteral("-NoProfile") << QStringLiteral("-NonInteractive")
        << QStringLiteral("-Command") << psCmd);
    if (m_repairProc->state() == QProcess::NotRunning) {
        // 启动失败（极端情况）
        m_repairProc->deleteLater();
        m_repairProc = nullptr;
        m_repairVcamBtn->setEnabled(true);
        m_repairVcamBtn->setText(QString::fromUtf8("修复虚拟摄像头"));
        QMessageBox::warning(this, QString::fromUtf8("修复失败"),
            QString::fromUtf8("无法启动修复流程，请关闭正在使用摄像头的会议软件/浏览器后重试，或重新安装 PhoneCam。"));
    }
}

void MainWindow::onRepairFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitCode)
    QProcess* proc = qobject_cast<QProcess*>(sender());
    QString output;
    if (proc) {
        output = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
        proc->deleteLater();
    }
    m_repairProc = nullptr;

    // 重新做完整健康检查（不能只相信 regsvr32 的 exit code）
    const VirtualCameraHealth health = checkVirtualCameraHealth();
    updatePreflightStatus();

    if (output.contains("EXIT=")) {
        // UAC 已允许，regsvr32 已真正运行
        const int regExit = output.section("EXIT=", 1, 1).toInt();
        if (regExit == 0 && health.healthy()) {
            QMessageBox::information(this, QString::fromUtf8("修复完成"),
                QString::fromUtf8("PhoneCam Camera 已成功注册。\n\n"
                    "如果会议软件已在安装/修复前打开，请完全退出并重新打开会议软件后重新选择摄像头。"));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("修复未完成"),
                QString::fromUtf8("修复未完成，请关闭正在使用摄像头的会议软件/浏览器后重试，或重新安装 PhoneCam。"));
        }
    } else {
        // 无 "EXIT=" 输出 → PowerShell 异常退出，通常是用户取消了 UAC
        QMessageBox::information(this, QString::fromUtf8("已取消"),
            QString::fromUtf8("修复需要管理员权限，操作已取消。"));
    }
}

} // namespace phonecam
