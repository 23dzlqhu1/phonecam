import sys

path = "/mnt/d/PhoneCam/cpp/src/vcam/virtual_cam_filter.cpp"
with open(path, "r", encoding="utf-8") as f:
    text = f.read()

old_logic = """    if (got_frame) {
        m_has_last_frame = true;
    }

    if (m_has_last_frame) {
        // We output the frame_buffer which contains either the newly read frame,
        // or the last cached frame if a new one wasn't available yet.
        int frame_size = m_width * m_height * 3; // The buffer is always updated to m_width/height when got_frame is true?
        
        // Wait, what if got_frame was true, but resolution mismatch? The buffer is still updated by read().
        // Actually, if we just assume m_width and m_height for the memcpy, it matches because read() populates based on shared memory width.
        // It's safer to always use m_width and m_height for the conversion output.
        if (frame_size <= MAX_FRAME_SIZE) {
            if (actualFormat == PixelFormat::NV12) {
                convertBGR24ToNV12(pData, m_frame_buffer.get(), m_width, m_height);
            } else {
                LONG stride = (m_width * 3 + 3) & ~3;
                if (stride == m_width * 3) {
                    std::memcpy(pData, m_frame_buffer.get(), frame_size);
                } else {
                    for (int y = 0; y < m_height; y++) {
                        std::memcpy(pData + y * stride,
                                   m_frame_buffer.get() + y * m_width * 3,
                                   m_width * 3);
                    }
                }
            }
            pSample->SetActualDataLength(data_size);
        } else {
            fillPlaceholderFrame(pData, m_width, m_height, actualFormat);
            pSample->SetActualDataLength(data_size);
        }
    } else {
        // If we've never received any frame yet, output the test pattern or placeholder instead of random memory block
        fillPlaceholderFrame(pData, m_width, m_height, actualFormat);
        pSample->SetActualDataLength(data_size);
    }"""

new_logic = """    if (got_frame) {
        if (width == m_width && height == m_height) {
            m_has_last_frame = true;
        } else {
            VCAM_LOG("Resolution changed %dx%d -> %dx%d, ignoring frame to prevent memory corruption", m_width, m_height, width, height);
            m_has_last_frame = false;
        }
    }

    if (m_has_last_frame) {
        int frame_size = m_width * m_height * 3;
        
        if (frame_size <= MAX_FRAME_SIZE) {
            if (actualFormat == PixelFormat::NV12) {
                convertBGR24ToNV12(pData, m_frame_buffer.get(), m_width, m_height);
            } else {
                LONG stride = (m_width * 3 + 3) & ~3;
                if (stride == m_width * 3) {
                    std::memcpy(pData, m_frame_buffer.get(), frame_size);
                } else {
                    for (int y = 0; y < m_height; y++) {
                        std::memcpy(pData + y * stride,
                                   m_frame_buffer.get() + y * m_width * 3,
                                   m_width * 3);
                    }
                }
            }
            pSample->SetActualDataLength(data_size);
        } else {
            VCAM_LOG("Frame too large %d, placeholder", frame_size);
            fillPlaceholderFrame(pData, m_width, m_height, actualFormat);
            pSample->SetActualDataLength(data_size);
        }
    } else {
        fillPlaceholderFrame(pData, m_width, m_height, actualFormat);
        pSample->SetActualDataLength(data_size);
    }"""

if old_logic in text:
    print("Found old logic, replacing...")
    text = text.replace(old_logic, new_logic)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
else:
    print("Old logic not found!")

