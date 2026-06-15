#include "gui/main_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QApplication>
#include <QDebug>
#include <QThread>

namespace phonecam {

// ── DecodeWorker ──

DecodeWorker::DecodeWorker(HwDecoder* decoder, BoundedQueue<QImage>* queue, QObject* parent)
    : QObject(parent), m_decoder(decoder), m_displayQueue(queue) {}

void DecodeWorker::decodeFrame(const VideoFrame& frame) {
    if (!m_decoder || !m_decoder->isInitialized()) return;

    QImage img;
    {
        std::lock_guard<std::mutex> lock(m_decoder->mutex());
        img = m_decoder->decode(frame.data.data(), static_cast<int>(frame.data.size()));
    }

    if (img.isNull()) return;

    // Apply rotation
    if (frame.rotation == 90) {
        img = img.transformed(QTransform().rotate(90));
    } else if (frame.rotation == 180) {
        img = img.transformed(QTransform().rotate(180));
    } else if (frame.rotation == 270) {
        img = img.transformed(QTransform().rotate(-90));
    }

    // Push to bounded queue (drop oldest if full)
    m_displayQueue->push(img);
    emit frameDecoded(img);
}

// ── MainWindow ──

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_receiver(new PcpReceiver(this))
    , m_decoder(new HwDecoder(this))
    , m_connManager(new ConnectionManager(this))
    , m_virtualCam(new VirtualCam(this))
    , m_displayQueue(new BoundedQueue<QImage>(3))
    , m_fpsTimer(new QTimer(this))
{
    setupUi();

    // Wire up connection manager
    connect(m_connManager, &ConnectionManager::stateChanged,
            this, &MainWindow::onConnectionStateChanged);

    // Wire up PcpReceiver
    connect(m_receiver, &PcpReceiver::connectionEstablished, this, [this]() {
        m_statusDot->setStyleSheet("background-color: #10b981; border-radius: 6px;");
        m_statusTitle->setText("已连接 (USB)");
    });
    connect(m_receiver, &PcpReceiver::connectionLost, this, [this]() {
        m_statusDot->setStyleSheet("background-color: #f59e0b; border-radius: 6px;");
        m_statusTitle->setText("等待手机推流...");
    });
    connect(m_receiver, &PcpReceiver::portInUse, this, [this](quint16 port) {
        m_statusTitle->setText(QString("端口 %1 被占用").arg(port));
        m_statusDetail->setText("请关闭其他 PhoneCam 实例后重试");
    });

    // Setup decode thread
    startPipeline();

    // Frame display timer (poll BoundedQueue at 60fps)
    QTimer* displayTimer = new QTimer(this);
    connect(displayTimer, &QTimer::timeout, this, [this]() {
        auto frame = m_displayQueue->tryPop();
        if (frame.has_value()) {
            m_preview->updateFrame(frame.value());
            m_frameCount++;
        }
    });
    displayTimer->start(16);  // ~60fps

    // FPS counter timer
    connect(m_fpsTimer, &QTimer::timeout, this, &MainWindow::onFpsTimer);
    m_fpsTimer->start(1000);

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
    if (m_fpsTimer) {
        m_fpsTimer->stop();
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
    m_displayQueue->shutdown();
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

    m_mirrorBtn = makeBtn("镜像", btnStyle);
    connect(m_mirrorBtn, &QPushButton::clicked, this, &MainWindow::onMirrorToggled);
    toolLayout->addWidget(m_mirrorBtn);

    m_flipBtn = makeBtn("翻转", btnStyle);
    connect(m_flipBtn, &QPushButton::clicked, this, &MainWindow::onFlipToggled);
    toolLayout->addWidget(m_flipBtn);

    m_rotateBtn = makeBtn("旋转 0°", btnStyle);
    connect(m_rotateBtn, &QPushButton::clicked, this, &MainWindow::onRotationToggled);
    toolLayout->addWidget(m_rotateBtn);

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
    m_decodeWorker = new DecodeWorker(m_decoder, m_displayQueue);
    m_decodeWorker->moveToThread(m_decodeThread);
    connect(m_decodeThread, &QThread::finished, m_decodeWorker, &QObject::deleteLater);

    // Wire: PcpReceiver → DecodeWorker
    connect(m_receiver, &PcpReceiver::frameReceived,
            m_decodeWorker, &DecodeWorker::decodeFrame, Qt::QueuedConnection);

    // Wire: DecodeWorker → onFrameDecoded (for shared memory writer)
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
        break;
    case ConnectionState::WaitingForPhone:
        m_statusDot->setStyleSheet("background-color: #e67e22; border-radius: 3px;");
        m_statusTitle->setText("ADB 就绪");
        m_statusDetail->setText("等待推流");
        break;
    case ConnectionState::Connected:
        m_statusDot->setStyleSheet("background-color: #27ae60; border-radius: 3px;");
        m_statusTitle->setText("已连接");
        m_statusDetail->setText(info.connectionType);
        break;
    case ConnectionState::Disconnected:
        m_statusDot->setStyleSheet("background-color: #c53030; border-radius: 3px;");
        m_statusTitle->setText("断开");
        m_statusDetail->setText("重连中");
        break;
    default:
        break;
    }
}

void MainWindow::onFrameDecoded(const QImage& image) {
    // HIGH-1 fix: removed m_preview->updateFrame() — displayTimer already handles rendering.
    // This method only handles shared memory writing for virtual camera.
    m_frameCount++;

    // Write BGR frame to shared memory for virtual camera DLL
    // Re-open if resolution changes (e.g. phone rotation)
    if (!m_sharedWriter.is_open() || m_last_width != image.width() || m_last_height != image.height()) {
        if (m_sharedWriter.is_open()) {
            m_sharedWriter.close();
            qDebug() << "[MAIN] SharedMemoryWriter closed due to resolution change";
        }
        m_sharedWriter.open(image.width(), image.height());
        m_last_width = image.width();
        m_last_height = image.height();
        qDebug() << "[MAIN] SharedMemoryWriter opened with new resolution:" << image.width() << "x" << image.height();
    }
    if (m_sharedWriter.is_open()) {
        // QImage is RGB32, need to convert to BGR24 for DirectShow
        QImage bgr = image.convertToFormat(QImage::Format_RGB888).rgbSwapped();
        m_sharedWriter.write(bgr.bits(), bgr.width(), bgr.height());
    }
}

void MainWindow::onMirrorToggled() {
    m_mirror = !m_mirror;
    m_preview->setMirror(m_mirror);
    m_mirrorBtn->setStyleSheet(m_mirror ?
        "QPushButton { background: #e8f0fe; color: #2b6cb0; border: 1px solid #90b4e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #d4e4fc; }" :
        "QPushButton { background: #ffffff; color: #4a5568; border: 1px solid #d5d9e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #f0f2f5; border-color: #b0b8c4; }");
}

void MainWindow::onFlipToggled() {
    m_flip = !m_flip;
    m_preview->setFlip(m_flip);
    m_flipBtn->setStyleSheet(m_flip ?
        "QPushButton { background: #e8f0fe; color: #2b6cb0; border: 1px solid #90b4e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #d4e4fc; }" :
        "QPushButton { background: #ffffff; color: #4a5568; border: 1px solid #d5d9e0; padding: 5px 16px; border-radius: 3px; font: 12px 'Segoe UI'; }"
        "QPushButton:hover { background: #f0f2f5; border-color: #b0b8c4; }");
}

void MainWindow::onRotationToggled() {
    m_rotation = (m_rotation + 90) % 360;
    m_preview->setRotation(m_rotation);
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

void MainWindow::onFpsTimer() {
    int fps = m_frameCount;
    m_frameCount = 0;

    QStringList parts;
    parts << QString("接收: %1fps").arg(fps);
    parts << QString("显示: %1fps").arg(fps);
    m_infoLabel->setText(parts.join(" | "));
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
} // namespace phonecam
