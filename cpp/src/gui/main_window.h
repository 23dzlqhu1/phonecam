#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QMouseEvent>
#include <memory>

#include "core/pcp_receiver.h"
#include "core/hw_decoder.h"
#include "core/connection_manager.h"
#include "core/video_frame.h"
#include "core/bounded_queue.h"
#include "gui/preview_widget.h"
#include "output/virtual_cam.h"
#include "vcam/shared_memory.h"

namespace phonecam {

// Decode worker runs on a dedicated thread
class DecodeWorker : public QObject {
    Q_OBJECT
public:
    explicit DecodeWorker(HwDecoder* decoder, BoundedQueue<QImage>* queue, QObject* parent = nullptr);

public slots:
    void decodeFrame(const phonecam::VideoFrame& frame);

signals:
    void frameDecoded(const QImage& image);

private:
    HwDecoder* m_decoder;
    BoundedQueue<QImage>* m_displayQueue;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectionStateChanged(const phonecam::ConnectionInfo& info);
    void onFrameDecoded(const QImage& image);
    void onMirrorToggled();
    void onFlipToggled();
    void onRotationToggled();
    void onResolutionChanged(const QString& text);
    void onFpsTimer();
    void toggleFullScreen();


protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void setupUi();
    void startPipeline();

    // Core components
    PcpReceiver* m_receiver;
    HwDecoder* m_decoder;
    ConnectionManager* m_connManager;
    VirtualCam* m_virtualCam;
    vcam::SharedMemoryWriter m_sharedWriter;  // For virtual camera DLL

    // Decode thread
    QThread* m_decodeThread = nullptr;
    DecodeWorker* m_decodeWorker = nullptr;
    BoundedQueue<QImage>* m_displayQueue = nullptr;

    // UI elements
    QLabel* m_statusDot;
    QLabel* m_statusTitle;
    QLabel* m_statusDetail;
    QComboBox* m_resolutionCombo;
    PreviewWidget* m_preview;
    QLabel* m_infoLabel;
    QPushButton* m_mirrorBtn;
    QPushButton* m_flipBtn;
    QPushButton* m_rotateBtn;
    QPushButton* m_quitBtn;

    // State
    bool m_mirror = false;
    bool m_flip = false;
    int m_rotation = 0;
    int m_frameCount = 0;
    int m_last_width = 0;
    int m_last_height = 0;
    QTimer* m_fpsTimer;

    // Window drag
    bool m_dragging = false;
    QPoint m_dragPos;

    bool m_isFullscreen = false;
    QWidget* m_topPanel = nullptr;
    QWidget* m_sidebarPanel = nullptr;
};

} // namespace phonecam
