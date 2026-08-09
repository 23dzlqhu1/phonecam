#include "gui/preview_widget.h"
#include <QPainter>
#include <cstring>
#include <cmath>

namespace phonecam {

// ── NV12 → RGB shaders (GLSL 330 core, explicit attribute locations) ──

static const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* kFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D yTex;
uniform sampler2D uvTex;

void main() {
    // Sample Y and UV planes (values in limited range: Y in [16,235], UV in [16,240])
    float y  = texture(yTex, vTexCoord).r;
    float u  = texture(uvTex, vTexCoord).r;
    float v  = texture(uvTex, vTexCoord).g;

    // Normalize: limited range → [0,1]
    y = (y - 16.0/255.0) / (219.0/255.0);
    u = (u - 16.0/255.0) / (224.0/255.0) - 0.5;
    v = (v - 16.0/255.0) / (224.0/255.0) - 0.5;

    // BT.601 → RGB
    float r = y + 1.402   * v;
    float g = y - 0.34414 * u - 0.71414 * v;
    float b = y + 1.772   * u;

    fragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
)";

// ── Full-screen quad vertices (NDC + texcoords) ──
// Interleaved: pos.x, pos.y, tex.u, tex.v  (used with setAttributeArray stride=4*sizeof(float))
static const float kQuadVertices[] = {
    // pos (x,y)      tex (u,v)
    -1.0f, -1.0f,     0.0f, 1.0f,  // bottom-left
     1.0f, -1.0f,     1.0f, 1.0f,  // bottom-right
    -1.0f,  1.0f,     0.0f, 0.0f,  // top-left
     1.0f,  1.0f,     1.0f, 0.0f,  // top-right
};

// ── NV12 → QImage fallback converter (BT.601 limited range) ──

QImage PreviewWidget::nv12ToQImage(const phonecam::Nv12Frame& frame) {
    const int w = frame.width;
    const int h = frame.height;
    QImage rgb(w, h, QImage::Format_RGB888);
    const uint8_t* yPlane  = reinterpret_cast<const uint8_t*>(frame.data.constData());
    const uint8_t* uvPlane = yPlane + (w * h);

    for (int row = 0; row < h; row++) {
        uint8_t* dst = rgb.scanLine(row);
        for (int col = 0; col < w; col++) {
            // Limited-range Y
            float yVal = static_cast<float>(yPlane[row * w + col]);
            yVal = (yVal - 16.0f) / 219.0f;

            // Chroma from 2×2 block
            const int uvRow = row / 2;
            const int uvCol = col / 2;
            const int uvIdx = uvRow * w + uvCol * 2;
            float uVal = static_cast<float>(uvPlane[uvIdx])     - 128.0f;
            float vVal = static_cast<float>(uvPlane[uvIdx + 1]) - 128.0f;

            // BT.601 → RGB
            float r = yVal + 1.402f   * vVal / 224.0f * 255.0f;
            float g = yVal - 0.34414f * uVal / 224.0f * 255.0f - 0.71414f * vVal / 224.0f * 255.0f;
            float b = yVal + 1.772f   * uVal / 224.0f * 255.0f;

            dst[col * 3]     = static_cast<uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
            dst[col * 3 + 1] = static_cast<uint8_t>(g < 0 ? 0 : (g > 255 ? 255 : g));
            dst[col * 3 + 2] = static_cast<uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b));
        }
    }
    return rgb;
}

// ── PreviewWidget ──

PreviewWidget::PreviewWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(320, 240);
}

PreviewWidget::~PreviewWidget() = default;

void PreviewWidget::updateNv12Frame(const phonecam::Nv12Frame& frame) {
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_nv12Frame = frame;
        m_useNv12 = true;
        m_needsUpdate = true;
    }
    update();
}

void PreviewWidget::updateFrame(const QImage& frame) {
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_currentFrame = frame;
        m_useNv12 = false;
        m_needsUpdate = true;
    }
    update();
}

void PreviewWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.13f, 0.13f, 0.14f, 1.0f);  // dark background for letterbox
    initShaders();
}

void PreviewWidget::initShaders() {
    m_nv12Program = std::make_unique<QOpenGLShaderProgram>();

    if (!m_nv12Program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) {
        qWarning() << "[PREVIEW] NV12 vertex shader FAILED:" << m_nv12Program->log();
        m_nv12Program.reset();
        return;
    }
    if (!m_nv12Program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)) {
        qWarning() << "[PREVIEW] NV12 fragment shader FAILED:" << m_nv12Program->log();
        m_nv12Program.reset();
        return;
    }

    // Bind attribute locations BEFORE linking (Qt6 core profile requirement)
    m_nv12Program->bindAttributeLocation("aPos", 0);
    m_nv12Program->bindAttributeLocation("aTexCoord", 1);

    if (!m_nv12Program->link()) {
        qWarning() << "[PREVIEW] NV12 shader link FAILED:" << m_nv12Program->log();
        m_nv12Program.reset();
        return;
    }

    m_shadersReady = true;
    qDebug() << "[PREVIEW] NV12 OpenGL shaders OK (will use GPU path)";
}

void PreviewWidget::resizeGL(int w, int h) {
    Q_UNUSED(w); Q_UNUSED(h);
    // Viewport set dynamically in renderNv12GL for 16:9 letterbox
}

void PreviewWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Snapshot current frame under lock
    phonecam::Nv12Frame nv12;
    QImage qimg;
    bool useNv12 = false;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        useNv12 = m_useNv12;
        if (useNv12) {
            nv12 = m_nv12Frame;
        } else {
            qimg = m_currentFrame;
        }
        m_needsUpdate = false;
    }

    if (useNv12 && !nv12.data.isEmpty()) {
        if (m_shadersReady) {
            renderNv12GL(nv12);
        } else {
            // Shader failed — fallback to CPU NV12→QImage→QPainter
            renderNv12Fallback(nv12);
        }
    } else if (!qimg.isNull()) {
        renderQImage(qimg);
    } else {
        // Draw placeholder text
        QPainter painter(this);
        painter.setPen(QColor(0x8b, 0x95, 0xa5));
        painter.setFont(QFont("Segoe UI", 13));
        painter.drawText(rect(), Qt::AlignCenter,
                         QString::fromUtf8("等待连接"));
    }
}

// ── GPU path: OpenGL NV12 shader with 16:9 letterbox ──

void PreviewWidget::renderNv12GL(const phonecam::Nv12Frame& frame) {
    const int fw = frame.width;   // 1280
    const int fh = frame.height;  // 720

    // QWidget::width()/height() 返回逻辑像素（device-independent），
    // 而 glViewport() 操作的是 OpenGL 物理 framebuffer。Windows 开启
    // DPI 缩放（如 150%）时，逻辑 800x450 对应物理 1200x675 framebuffer；
    // 若直接用逻辑像素设置 viewport，画面只会出现在 framebuffer 左下角。
    // 通过 devicePixelRatioF() 在每次渲染时读取 DPR 并转换为物理像素。
    const qreal dpr = devicePixelRatioF();
    const int widgetW = static_cast<int>(std::lround(width() * dpr));
    const int widgetH = static_cast<int>(std::lround(height() * dpr));

    if (widgetW <= 0 || widgetH <= 0 || fw <= 0 || fh <= 0) {
        return;
    }

    // Calculate 16:9 viewport centered in widget
    const double frameAspect = static_cast<double>(fw) / fh;  // 16/9 = 1.778
    const double widgetAspect = static_cast<double>(widgetW) / widgetH;

    int vpW, vpH, vpX, vpY;
    if (widgetAspect > frameAspect) {
        // Widget is wider — letterbox left/right (black bars on sides)
        // Actually for 16:9 source, if widget is wider than 16:9, we get pillarbox
        vpH = widgetH;
        vpW = static_cast<int>(std::lround(widgetH * frameAspect));
        vpX = (widgetW - vpW) / 2;
        vpY = 0;
    } else {
        // Widget is taller — letterbox top/bottom (black bars)
        vpW = widgetW;
        vpH = static_cast<int>(std::lround(widgetW / frameAspect));
        vpX = 0;
        vpY = (widgetH - vpH) / 2;
    }

    // Clear entire widget to dark (letterbox color)
    glClearColor(0.13f, 0.13f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vpX, vpY, vpW, vpH);

    // Create or recreate textures if dimensions changed
    if (!m_yTexture || m_yTexture->width() != fw || m_yTexture->height() != fh) {
        m_yTexture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        m_yTexture->setSize(fw, fh);
        m_yTexture->setFormat(QOpenGLTexture::R8_UNorm);
        m_yTexture->setMinificationFilter(QOpenGLTexture::Linear);
        m_yTexture->setMagnificationFilter(QOpenGLTexture::Linear);
        m_yTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_yTexture->allocateStorage();

        m_uvTexture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        m_uvTexture->setSize(fw / 2, fh / 2);
        m_uvTexture->setFormat(QOpenGLTexture::RG8_UNorm);
        m_uvTexture->setMinificationFilter(QOpenGLTexture::Linear);
        m_uvTexture->setMagnificationFilter(QOpenGLTexture::Linear);
        m_uvTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_uvTexture->allocateStorage();
    }

    // Upload Y and UV planes
    const uint8_t* data = reinterpret_cast<const uint8_t*>(frame.data.constData());
    m_yTexture->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8,
                        data, nullptr);
    m_uvTexture->setData(QOpenGLTexture::RG, QOpenGLTexture::UInt8,
                         data + (fw * fh), nullptr);

    // Bind program and textures
    m_nv12Program->bind();
    m_yTexture->bind(0);
    m_nv12Program->setUniformValue("yTex", 0);
    m_uvTexture->bind(1);
    m_nv12Program->setUniformValue("uvTex", 1);

    // Draw full-screen quad (fills the viewport which is already 16:9)
    m_nv12Program->enableAttributeArray(0);  // aPos at location 0
    m_nv12Program->enableAttributeArray(1);  // aTexCoord at location 1
    m_nv12Program->setAttributeArray(0, kQuadVertices, 2, 4 * sizeof(float));
    m_nv12Program->setAttributeArray(1, kQuadVertices + 2, 2, 4 * sizeof(float));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_nv12Program->disableAttributeArray(0);
    m_nv12Program->disableAttributeArray(1);
    m_nv12Program->release();
}

// ── CPU fallback: NV12 → QImage → QPainter (16:9 KeepAspectRatio) ──

void PreviewWidget::renderNv12Fallback(const phonecam::Nv12Frame& frame) {
    QImage rgb = nv12ToQImage(frame);
    renderQImage(rgb);
}

// ── Legacy QPainter path ──

void PreviewWidget::renderQImage(const QImage& frame) {
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
