#include "output/virtual_cam.h"
#include "vcam/shared_memory.h"
#include <QDebug>

namespace phonecam {

VirtualCam::VirtualCam(QObject* parent) : QObject(parent) {}

VirtualCam::~VirtualCam() {
    close();
}

bool VirtualCam::open(int width, int height, int fps) {
    Q_UNUSED(fps)
    if (m_isOpen) return true;

    m_writer = std::make_unique<vcam::SharedMemoryWriter>();
    if (!m_writer->open(width, height)) {
        qWarning() << "[VCAM] Failed to open shared memory" << width << "x" << height;
        m_writer.reset();
        return false;
    }

    m_isOpen = true;
    qDebug() << "[VCAM] Shared memory opened" << width << "x" << height;
    return true;
}

bool VirtualCam::send(const QImage& frame) {
    if (!m_isOpen || !m_writer) return false;

    // Convert QImage to BGR24 (OpenCV/SharedMemory format)
    QImage bgr = frame.convertToFormat(QImage::Format_RGB888).rgbSwapped();
    // rgbSwapped converts RGB888 → BGR888

    return m_writer->write(bgr.constBits(), bgr.width(), bgr.height());
}

void VirtualCam::close() {
    if (m_writer) {
        m_writer->close();
        m_writer.reset();
    }
    m_isOpen = false;
}

} // namespace phonecam
