#include "gui/preview_widget.h"
#include <QPainter>

namespace phonecam {

PreviewWidget::PreviewWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(320, 240);
}

PreviewWidget::~PreviewWidget() = default;

void PreviewWidget::updateFrame(const QImage& frame) {
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_currentFrame = frame;
        m_needsUpdate = true;
    }
    // Schedule a repaint on the main thread
    update();
}

void PreviewWidget::setMirror(bool mirror) {
    m_mirror = mirror;
    update();
}

void PreviewWidget::setFlip(bool flip) {
    m_flip = flip;
    update();
}

void PreviewWidget::setRotation(int degrees) {
    m_rotation = degrees;
    update();
}

void PreviewWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.96f, 0.96f, 0.97f, 1.0f);  // #f5f6f8
}

void PreviewWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PreviewWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    QImage frame;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_currentFrame.isNull()) {
            // Draw placeholder text
            QPainter painter(this);
            painter.setPen(QColor(0x8b, 0x95, 0xa5));  // #8b95a5
            painter.setFont(QFont("Segoe UI", 13));
            painter.drawText(rect(), Qt::AlignCenter, "等待连接");
            return;
        }
        frame = m_currentFrame;
        m_needsUpdate = false;
    }

    // Apply transforms
    if (m_mirror) frame = frame.mirrored(true, false);
    if (m_flip) frame = frame.mirrored(false, true);
    if (m_rotation != 0) {
        QTransform t;
        t.rotate(m_rotation);
        frame = frame.transformed(t);
    }

    // Draw scaled image centered in widget
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QSize widgetSize = size();
    QSize imgSize = frame.size();
    QSize scaledSize = imgSize.scaled(widgetSize, Qt::KeepAspectRatio);

    int x = (widgetSize.width() - scaledSize.width()) / 2;
    int y = (widgetSize.height() - scaledSize.height()) / 2;

    painter.drawImage(QRect(x, y, scaledSize.width(), scaledSize.height()), frame);
}

void PreviewWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
        event->accept();
    } else {
        QOpenGLWidget::mouseDoubleClickEvent(event);
    }
}
} // namespace phonecam
