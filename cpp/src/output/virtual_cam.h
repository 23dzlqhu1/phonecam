#pragma once
#include <QObject>
#include <QImage>
#include <memory>

// Forward declare to avoid including shared_memory.h in header
namespace phonecam { namespace vcam { class SharedMemoryWriter; } }

namespace phonecam {

class VirtualCam : public QObject {
    Q_OBJECT
public:
    explicit VirtualCam(QObject* parent = nullptr);
    ~VirtualCam() override;

    // Open shared memory for writing frames
    bool open(int width, int height, int fps);

    // Send a frame (QImage in any format — will be converted to BGR24)
    bool send(const QImage& frame);

    void close();
    bool isOpen() const { return m_isOpen; }

signals:
    void error(const QString& message);

private:
    std::unique_ptr<vcam::SharedMemoryWriter> m_writer;
    bool m_isOpen = false;
};

} // namespace phonecam
