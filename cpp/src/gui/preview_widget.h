#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QImage>
#include <mutex>
#include <memory>

#include "core/nv12_frame.h"

namespace phonecam {

// QOpenGLWidget-based video preview supporting two paths:
//   Primary:  NV12 frame → OpenGL shader (BT.601 limited-range YUV→RGB)
//            with 16:9 letterbox (centered, black bars)
//   Fallback: NV12 → QImage → QPainter (if OpenGL shader fails)
//
// No longer applies mirror/flip/rotation — transforms happen upstream
// in FinalFrameComposer, guaranteeing preview ↔ virtual cam consistency.
class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    // Primary path: display a pre-composed NV12 frame (no further transforms)
    void updateNv12Frame(const phonecam::Nv12Frame& frame);

    // Legacy path: display a QImage (kept for backward compatibility)
    void updateFrame(const QImage& frame);

    // Kept for API compatibility but are no-ops — transforms are now in FinalFrameComposer.
    void setMirror(bool mirror)   { Q_UNUSED(mirror); }
    void setFlip(bool flip)       { Q_UNUSED(flip); }
    void setRotation(int degrees) { Q_UNUSED(degrees); }

signals:
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void initShaders();
    void renderNv12GL(const phonecam::Nv12Frame& frame);
    void renderNv12Fallback(const phonecam::Nv12Frame& frame);
    void renderQImage(const QImage& frame);
    static QImage nv12ToQImage(const phonecam::Nv12Frame& frame);

    // NV12 OpenGL resources
    std::unique_ptr<QOpenGLShaderProgram> m_nv12Program;
    std::unique_ptr<QOpenGLTexture> m_yTexture;
    std::unique_ptr<QOpenGLTexture> m_uvTexture;
    bool m_shadersReady = false;

    // Frame storage (mutex-protected for cross-thread access)
    phonecam::Nv12Frame m_nv12Frame;
    QImage m_currentFrame;
    std::mutex m_frameMutex;
    bool m_useNv12 = false;
    bool m_needsUpdate = false;
};

} // namespace phonecam
