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

    // Build transform state
    FrameTransform transform;
    transform.mirror = m_mirror;
    transform.flip = m_flip;
    transform.manualRotation = m_manualRotation;
    transform.androidRotation = frame.rotation;

    if (m_useLegacyCompose || transform.needsFallback()) {
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
{
    setupUi();

    // Wire up connection manager
    connect(m_connManager, &ConnectionManager::stateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(m_connManager, &ConnectionManager::diagnosticsChanged,
            this, &MainWindow::onDiagnosticsChanged);
    connect(m_connManager, &ConnectionManager::candidatesChanged,
            this, [this](const QVector<DeviceCandidate>& candidates) {
        // P1-1: Update device combo box
        m_deviceCombo->blockSignals(true);
        int prevIndex = m_deviceCombo->currentIndex();
        QString prevData = m_deviceCombo->currentData().toString();
        m_deviceCombo->clear();
        m_deviceCombo->addItem(QString::fromUtf8("自动选择"), "");
        int restoreIndex = 0;
        for (int i = 0; i < candidates.size(); i++) {
            const auto& c = candidates[i];
            QString icon = (c.transport == "usb") ? "🔌" :
                           (c.transport == "wifi") ? "📶" : "🔗";
            QString statusIcon = (c.status == "Connected") ? "✅" :
                                 (c.status == "Connecting") ? "⏳" :
                                 (c.status == "Failed") ? "❌" : "⬜";
            QString label = QString("%1 %2 %3 [%4]")
                .arg(icon, c.displayName, statusIcon, c.status);
            if (!c.lastError.isEmpty() && c.status == "Failed") {
                label += QString(" — %1").arg(c.lastError);
            }
            m_deviceCombo->addItem(label, c.id);
            if (c.id == prevData) restoreIndex = i + 1;
        }
        m_deviceCombo->setCurrentIndex(restoreIndex);
        m_deviceCombo->blockSignals(false);
    });

    // Wire up PcpReceiver
    connect(m_receiver, &PcpReceiver::connectionEstablished, this, [this]() {
        m_statusDot->setStyleSheet("background-color: #10b981; border-radius: 6px;");
        auto info = m_connManager->info();
        QString label = (info.connectionType == "hotspot") ?
            QString::fromUtf8("热点连接") : info.connectionType;
        m_statusTitle->setText(label);
        m_streamEstablished = true;  // P0-2: track for preflight
        m_receiver->sendCommand(QByteArrayLiteral("PLI\n"));
        if (m_decodeWorker) {
            QMetaObject::invokeMethod(m_decodeWorker, "requestFlush", Qt::QueuedConnection);
        }
    });
    connect(m_receiver, &PcpReceiver::connectionLost, this, [this]() {
        m_statusDot->setStyleSheet("background-color: #f59e0b; border-radius: 6px;");
        m_statusTitle->setText(QString::fromUtf8("等待手机推流..."));
        m_streamEstablished = false;  // P0-2: reset for preflight
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
    QLabel* pfTitle = new QLabel(QString::fromUtf8("会议准备"));
    pfTitle->setStyleSheet("font: 600 13px 'Segoe UI'; color: #2c3e50; background: transparent;");
    pfLayout->addWidget(pfTitle);
    pfLayout->addWidget(new QLabel("|"));
    const char* pfNames[] = {"手机推流", "PC 接收", "虚拟摄像头", "腾讯会议可见"};
    for (int i = 0; i < 4; i++) {
        m_preflightLabels[i] = new QLabel(QString::fromUtf8("\u2B1C %1").arg(pfNames[i]));
        m_preflightLabels[i]->setStyleSheet("font: 12px 'Segoe UI'; color: #8b95a5; background: transparent;");
        pfLayout->addWidget(m_preflightLabels[i]);
    }
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
    QLabel* adbLabel = new QLabel("ADB");
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
    QLabel* streamLabel;
    QVBoxLayout* streamCol = makeStatusCol("推流", streamLabel);
    streamLabel->setText("待机");
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
    QLabel* previewNote = new QLabel(QString::fromUtf8("预览 = 腾讯会议输出"));
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
        m_statusTitle->setText("ADB 就绪");
        m_statusDetail->setText("等待推流");
        updateDiagnosticsBar();
        break;
    case ConnectionState::Connected:
        m_statusDot->setStyleSheet("background-color: #27ae60; border-radius: 3px;");
        m_statusTitle->setText("已连接");
        m_statusDetail->setText(info.connectionType);
        m_diagLabel->setVisible(false);  // Hide diagnostics when connected
        break;
    case ConnectionState::Disconnected:
        m_statusDot->setStyleSheet("background-color: #c53030; border-radius: 3px;");
        m_statusTitle->setText("断开");
        m_statusDetail->setText("重连中");
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
    // Keep tooltip for detailed probe info
    QStringList lines;
    lines << QString::fromUtf8("ADB: %1").arg(diag.adbStatus);
    for (const auto& p : diag.probeResults) {
        QString icon = (p.result == ProbeResult::Success) ? QString::fromUtf8("\u2705") :
                       (p.result == ProbeResult::Timeout) ? QString::fromUtf8("\u23F0") :
                       (p.result == ProbeResult::ConnectionRefused) ? QString::fromUtf8("\u274C") :
                       QString::fromUtf8("\u26A0");
        lines << QString("  %1 %2:%3 — %4").arg(icon, p.host).arg(p.port).arg(p.errorDetail);
    }
    m_statusDetail->setToolTip(lines.join("\n"));
    updateDiagnosticsBar();
}

void MainWindow::updateDiagnosticsBar() {
    if (!m_diagLabel) return;
    if (m_connManager->info().state == ConnectionState::Connected) {
        m_diagLabel->setVisible(false);
        return;
    }
    QStringList msgs;
    const auto& diag = m_lastDiag;
    // ADB diagnostics
    if (diag.adbStatus == "not found") {
        msgs << QString::fromUtf8("⚠ adb 未安装 — 请安装 Android Platform Tools 或使用 WiFi/热点连接");
    } else if (diag.adbStatus == "no devices") {
        msgs << QString::fromUtf8("⚠ 未检测到 USB 设备 — 请连接手机并开启 USB 调试");
    } else {
        for (const auto& line : diag.adbDevices) {
            if (line.contains("unauthorized")) {
                msgs << QString::fromUtf8("❌ ADB 设备未授权 — 请在手机上点击「允许 USB 调试」");
            } else if (line.contains("offline")) {
                msgs << QString::fromUtf8("❌ ADB 设备离线 — 请拔插 USB 线或重启 USB 调试");
            }
        }
    }
    // USB forward diagnostics
    for (const auto& c : m_connManager->candidates()) {
        if (c.transport == "usb" && c.status == "Failed") {
            msgs << QString::fromUtf8("❌ USB 转发失败: %1").arg(c.lastError);
        }
    }
    // WiFi/Hotspot diagnostics
    if (diag.gatewayIp.isEmpty() && diag.localNics.isEmpty()) {
        msgs << QString::fromUtf8("⚠ 未发现网络 — 请确认已连接手机热点或同一 WiFi");
    } else {
        for (const auto& p : diag.probeResults) {
            if (p.result == ProbeResult::Timeout) {
                msgs << QString::fromUtf8("⏰ %1:%2 超时 — 手机未响应，请确认手机端已开始推流")
                    .arg(p.host).arg(p.port);
            } else if (p.result == ProbeResult::ConnectionRefused) {
                msgs << QString::fromUtf8("❌ %1:%2 拒绝连接 — 手机端未开始推流，请打开 PhoneCam App 并点击开始")
                    .arg(p.host).arg(p.port);
            } else if (p.result == ProbeResult::Unreachable) {
                msgs << QString::fromUtf8("❌ %1 不可达 — 请确认已连接手机热点或同一 WiFi")
                    .arg(p.host);
            }
        }
    }
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
            // ADB version
            QProcess adbProc;
            adbProc.start("adb", QStringList() << "version");
            adbProc.waitForFinished(3000);
            ts << "ADB: " << adbProc.readAllStandardOutput().trimmed() << "\n";

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
            ts << "Gateway IP: " << m_lastDiag.gatewayIp << "\n";
            ts << "Local NICs: " << m_lastDiag.localNics.join(", ") << "\n";
            ts << "ADB devices:\n";
            for (const auto& d : m_lastDiag.adbDevices) {
                ts << "  " << d << "\n";
            }
            ts << "Probe results:\n";
            for (const auto& p : m_lastDiag.probeResults) {
                QString resStr = (p.result == ProbeResult::Success) ? "OK" :
                                 (p.result == ProbeResult::Timeout) ? "Timeout" :
                                 (p.result == ProbeResult::ConnectionRefused) ? "Refused" :
                                 (p.result == ProbeResult::Unreachable) ? "Unreachable" : "Unknown";
                ts << "  " << p.host << ":" << p.port << " -> " << resStr;
                if (!p.errorDetail.isEmpty()) ts << " (" << p.errorDetail << ")";
                ts << "\n";
            }
            ts << "\nCandidates:\n";
            for (const auto& c : m_connManager->candidates()) {
                ts << "  " << c.transport << " - " << c.displayName
                   << " [" << c.status << "]";
                if (!c.lastError.isEmpty()) ts << " error: " << c.lastError;
                ts << "\n";
            }
            ts << "\nStream established: " << (m_streamEstablished ? "yes" : "no") << "\n";

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

void MainWindow::updatePreflightStatus() {
    if (!m_preflightPanel) return;
    // Item 1: 手机已推流
    bool phoneStreaming = m_streamEstablished;
    m_preflightLabels[0]->setText(phoneStreaming ?
        QString::fromUtf8("✅ 手机推流") : QString::fromUtf8("❌ 手机推流"));
    m_preflightLabels[0]->setStyleSheet(phoneStreaming ?
        "font: 12px 'Segoe UI'; color: #27ae60; background: transparent;" :
        "font: 12px 'Segoe UI'; color: #c53030; background: transparent;");
    // Item 2: PC 已接收帧
    bool pcReceiving = m_frameCount > 0;
    m_preflightLabels[1]->setText(pcReceiving ?
        QString::fromUtf8("✅ PC 接收") : QString::fromUtf8("❌ PC 接收"));
    m_preflightLabels[1]->setStyleSheet(pcReceiving ?
        "font: 12px 'Segoe UI'; color: #27ae60; background: transparent;" :
        "font: 12px 'Segoe UI'; color: #c53030; background: transparent;");
    // Item 3: 虚拟摄像头已注册 (Windows registry check)
    bool vcamRegistered = false;
#ifdef Q_OS_WIN
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Classes\\CLSID\\{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}\\InprocServer32",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        vcamRegistered = true;
        RegCloseKey(hKey);
    }
#endif
    m_preflightLabels[2]->setText(vcamRegistered ?
        QString::fromUtf8("✅ 虚拟摄像头") : QString::fromUtf8("❌ 虚拟摄像头"));
    m_preflightLabels[2]->setStyleSheet(vcamRegistered ?
        "font: 12px 'Segoe UI'; color: #27ae60; background: transparent;" :
        "font: 12px 'Segoe UI'; color: #c53030; background: transparent;");
    // Item 4: 腾讯会议可见 (DirectShow enumeration via registry)
    bool dshowVisible = false;
#ifdef Q_OS_WIN
    HKEY hInstance;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Classes\\CLSID\\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\\Instance",
            0, KEY_READ, &hInstance) == ERROR_SUCCESS) {
        wchar_t subKeyName[256];
        DWORD index = 0;
        DWORD nameLen = 256;
        while (RegEnumKeyExW(hInstance, index++, subKeyName, &nameLen,
                             nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            HKEY hSub;
            if (RegOpenKeyExW(hInstance, subKeyName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                wchar_t friendlyName[512];
                DWORD size = sizeof(friendlyName);
                if (RegQueryValueExW(hSub, L"FriendlyName", nullptr, nullptr,
                                     (LPBYTE)friendlyName, &size) == ERROR_SUCCESS) {
                    if (wcsstr(friendlyName, L"PhoneCam Camera") != nullptr) {
                        dshowVisible = true;
                    }
                }
                RegCloseKey(hSub);
            }
            nameLen = 256;
            if (dshowVisible) break;
        }
        RegCloseKey(hInstance);
    }
#endif
    m_preflightLabels[3]->setText(dshowVisible ?
        QString::fromUtf8("✅ 腾讯会议可见") : QString::fromUtf8("❌ 腾讯会议可见"));
    m_preflightLabels[3]->setStyleSheet(dshowVisible ?
        "font: 12px 'Segoe UI'; color: #27ae60; background: transparent;" :
        "font: 12px 'Segoe UI'; color: #c53030; background: transparent;");
    // Overall status
    bool allGreen = phoneStreaming && pcReceiving && vcamRegistered && dshowVisible;
    m_preflightPanel->setStyleSheet(allGreen ?
        "background: #e8f5e9; border-bottom: 1px solid #c8e6c9;" :
        "background: #f8f9fa; border-bottom: 1px solid #e0e3e8;");
}

} // namespace phonecam
