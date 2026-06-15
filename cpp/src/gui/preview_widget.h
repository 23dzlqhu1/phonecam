#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QImage>
#include <mutex>

namespace phonecam {

// QOpenGLWidget-based video preview for 60fps texture upload.
// Receives QImage frames and renders them as OpenGL textures.
class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    // Update the displayed frame. Must be called from the GUI thread (use queued connection or timer).
    void updateFrame(const QImage& frame);

    void setMirror(bool mirror);
    void setFlip(bool flip);
    void setRotation(int degrees);  // 0/90/180/270

signals:
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;


protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QImage m_currentFrame;
    std::mutex m_frameMutex;
    bool m_mirror = false;
    bool m_flip = false;
    int m_rotation = 0;
    bool m_needsUpdate = false;
};

} // namespace phonecam
