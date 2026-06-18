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
#include "core/nv12_frame.h"
#include "core/final_frame_composer.h"
#include "gui/preview_widget.h"
#include "output/virtual_cam.h"
#include "vcam/shared_memory.h"

namespace phonecam {

// Decode worker runs on a dedicated thread.
// Primary path: decodeFrame() → FinalFrameComposer → Nv12Frame
// Legacy fallback: decode() → QImage (when mirror/flip/manualRotation active)
class DecodeWorker : public QObject {
    Q_OBJECT
public:
    explicit DecodeWorker(HwDecoder* decoder, FinalFrameComposer* composer,
                          BoundedQueue<QImage>* legacyQueue, QObject* parent = nullptr);

    void setUseLegacyCompose(bool legacy) { m_useLegacyCompose = legacy; }
    void setTransformState(bool mirror, bool flip, int manualRotation);

public slots:
    void decodeFrame(const phonecam::VideoFrame& frame);
    void requestFlush();

signals:
    void finalFrameReady(const phonecam::Nv12Frame& frame);
    void frameDecoded(const QImage& image);

private:
    HwDecoder* m_decoder;
    FinalFrameComposer* m_composer;
    BoundedQueue<QImage>* m_legacyDisplayQueue;
    uint32_t m_sequence = 0;
    bool m_useLegacyCompose = false;
    bool m_mirror = false;
    bool m_flip = false;
    int m_manualRotation = 0;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void setForceSoftwareDecode(bool force);
    void setUseLegacyCompose(bool legacy);
    void enableH264Dump(const QString& path);
    void enableCanonicalDump(const QString& path);

private slots:
    void onConnectionStateChanged(const phonecam::ConnectionInfo& info);
    void onDiagnosticsChanged(const phonecam::ConnectionDiagnostics& diag);
    void onFinalFrameReady(const phonecam::Nv12Frame& frame);
    void onFrameDecoded(const QImage& image);  // legacy fallback
    void onMirrorToggled();
    void onFlipToggled();
    void onRotationToggled();
    void onResolutionChanged(const QString& text);
    void onStatsTimer();
    void toggleFullScreen();
    void onExportLogs();
    void onDeviceSelected(int index);
    void onRefreshDevices();
    void onManualConnect();


protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void setupUi();
    void startPipeline();
    void updateDiagnosticsBar();
    void updatePreflightStatus();

    // Core components
    PcpReceiver* m_receiver;
    HwDecoder* m_decoder;
    FinalFrameComposer* m_composer;
    ConnectionManager* m_connManager;
    VirtualCam* m_virtualCam;
    vcam::SharedMemoryWriter m_sharedWriter;  // For virtual camera DLL

    // Decode thread
    QThread* m_decodeThread = nullptr;
    DecodeWorker* m_decodeWorker = nullptr;
    BoundedQueue<QImage>* m_legacyDisplayQueue = nullptr;  // legacy fallback only

    // UI elements
    QLabel* m_statusDot;
    QLabel* m_statusTitle;
    QLabel* m_statusDetail;
    QLabel* m_diagLabel;         // P0-3: visible connection diagnostics
    QWidget* m_preflightPanel;   // P0-2: pre-flight check panel
    QLabel* m_preflightLabels[4]; // P0-2: 4 check item labels
    QComboBox* m_resolutionCombo;
    QComboBox* m_deviceCombo;       // P1-1: device selector
    QPushButton* m_refreshBtn;      // P1-1: refresh devices
    PreviewWidget* m_preview;
    QLabel* m_infoLabel;
    QPushButton* m_mirrorBtn;
    QPushButton* m_flipBtn;
    QPushButton* m_rotateBtn;
    QPushButton* m_exportLogBtn;
    QPushButton* m_quitBtn;

    // State
    bool m_mirror = false;
    bool m_flip = false;
    int m_rotation = 0;
    int m_frameCount = 0;
    int m_nv12FrameCount = 0;  // NV12 path counter
    int m_legacyFrameCount = 0;  // legacy QImage path counter
    int m_last_width = 0;
    int m_last_height = 0;
    QTimer* m_statsTimer;
    QTimer* m_preflightTimer;
    bool m_streamEstablished = false;
    ConnectionDiagnostics m_lastDiag;
    bool m_useLegacyCompose = false;
    QString m_canonicalDumpPath;

    // Window drag
    bool m_dragging = false;
    QPoint m_dragPos;

    bool m_isFullscreen = false;
    QWidget* m_topPanel = nullptr;
    QWidget* m_sidebarPanel = nullptr;
};

} // namespace phonecam
