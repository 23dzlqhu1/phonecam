#include "virtual_cam_filter.h"
#include <initguid.h>
#include <Ks.h>
#include <Ksmedia.h>
#include <cstring>
#include <memory>
#include <Windows.h>
#include <cstdio>
#include <ctime>
#include <mutex>

// ── Phase 4.1: RGB24-only safe default ──
// Set to 1 to force DirectShow to only expose RGB24/BGR24 (safe, compatible default).
// Set to 0 to expose NV12 as preferred format (fast path, experimental).
#define VCAM_DEFAULT_RGB24_ONLY 1

// ── VCAM file logger ──
// Writes to logs/phonecam-vcam-YYYYMMDD-HHMMSS.log alongside OutputDebugStringA.
static FILE* s_vcamLogFile = nullptr;
static std::mutex s_vcamLogMutex;
static bool s_vcamLogInit = false;

static void vcam_file_log(const char* line) {
    std::lock_guard<std::mutex> lock(s_vcamLogMutex);
    if (!s_vcamLogInit) {
        // Try to create logs/ directory (best-effort)
        CreateDirectoryA("logs", nullptr);
        // Generate filename with timestamp
        time_t now = time(nullptr);
        struct tm tm_buf;
        localtime_s(&tm_buf, &now);
        char path[256];
        snprintf(path, sizeof(path), "logs/phonecam-vcam-%04d%02d%02d-%02d%02d%02d.log",
                 tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                 tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
        s_vcamLogFile = fopen(path, "w");
        s_vcamLogInit = true;
        if (s_vcamLogFile) {
            fprintf(s_vcamLogFile, "\xEF\xBB\xBF"); // UTF-8 BOM
            fflush(s_vcamLogFile);
        }
    }
    if (s_vcamLogFile) {
        // Timestamp prefix
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(s_vcamLogFile, "[%04d-%02d-%02dT%02d:%02d:%02d.%03d] %s\n",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);
        fflush(s_vcamLogFile);
    }
}

// Always-visible debug output (OutputDebugStringA is never filtered)
// Also writes to vcam log file for persistent capture.
static void VCAM_LOG(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA("[VCAM] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    // Also write to file (prepend [VCAM] tag)
    char fileBuf[540];
    snprintf(fileBuf, sizeof(fileBuf), "[VCAM] %s", buf);
    vcam_file_log(fileBuf);
}

namespace phonecam {
namespace vcam {

// ── Simple 5x7 bitmap font for "Naoko" (no GDI dependency) ──
static const uint8_t FONT_N[] = {0x63,0x77,0x7F,0x7B,0x73,0x63,0x63}; // N
static const uint8_t FONT_A[] = {0x18,0x3C,0x66,0x7E,0x66,0x66,0x66}; // A
static const uint8_t FONT_O[] = {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C}; // O
static const uint8_t FONT_K[] = {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66}; // K

static void drawChar5x7(BYTE* bgr, int imgW, int imgH, int ox, int oy,
                         const uint8_t* glyph, BYTE r, BYTE g, BYTE b) {
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 6; col++) {  // 6 pixels wide
            if (bits & (1 << (5 - col))) {
                // Draw a 4x4 block per "pixel" for visibility
                for (int dy = 0; dy < 4; dy++) {
                    for (int dx = 0; dx < 4; dx++) {
                        int px = ox + col * 4 + dx;
                        int py = oy + row * 4 + dy;
                        if (px >= 0 && px < imgW && py >= 0 && py < imgH) {
                            int off = (py * imgW + px) * 3;
                            bgr[off]     = b;
                            bgr[off + 1] = g;
                            bgr[off + 2] = r;
                        }
                    }
                }
            }
        }
    }
}

// ── Helper: fill "Naoko" placeholder on BGR24 buffer (no GDI) ──
static void fillNaokoBGR24(BYTE* pData, int width, int height) {
    int pixelCount = width * height;
    // Dark blue-gray background (#1a1a2e) in BGR
    std::unique_ptr<BYTE[]> bgrBuf(new BYTE[pixelCount * 3]);
    for (int i = 0; i < pixelCount * 3; i += 3) {
        bgrBuf[i]     = 0x2e;  // B
        bgrBuf[i + 1] = 0x1a;  // G
        bgrBuf[i + 2] = 0x1a;  // R
    }

    // Draw "Naoko" centered (each char = 6*4=24px wide, 7*4=28px tall, 4px spacing)
    int charW = 24, charH = 28, gap = 8;
    const uint8_t* glyphs[] = {FONT_N, FONT_A, FONT_O, FONT_K, FONT_O};
    int totalW = 5 * charW + 4 * gap;  // 5 chars + 4 gaps
    int ox = (width - totalW) / 2;
    int oy = (height - charH) / 2;

    for (int c = 0; c < 5; c++) {
        drawChar5x7(bgrBuf.get(), width, height,
                    ox + c * (charW + gap), oy, glyphs[c],
                    0xe0, 0xd0, 0xb0);  // warm beige in RGB
    }

    // Copy BGR with stride alignment
    LONG stride = (width * 3 + 3) & ~3;
    for (int y = 0; y < height; y++) {
        std::memcpy(pData + y * stride, bgrBuf.get() + y * width * 3, width * 3);
    }
}

// ── BGR24 → NV12 conversion (no SIMD, pure C++) ──
// NV12 layout: [Y plane: w*h bytes] [UV plane: w*h/2 bytes (interleaved U,V)]
void CVCamStream::convertBGR24ToNV12(uint8_t* dst, const uint8_t* src, int w, int h) {
    uint8_t* yPlane = dst;
    uint8_t* uvPlane = dst + w * h;

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int srcIdx = (row * w + col) * 3;
            uint8_t b = src[srcIdx];
            uint8_t g = src[srcIdx + 1];
            uint8_t r = src[srcIdx + 2];

            // BT.601 full range
            int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            yPlane[row * w + col] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));

            // Subsample chroma 4:2:0 — one UV pair per 2x2 block
            if ((row & 1) == 0 && (col & 1) == 0) {
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                int uvIdx = (row / 2) * w + col;  // UV plane is w * h/2
                uvPlane[uvIdx]     = (uint8_t)(u < 0 ? 0 : (u > 255 ? 255 : u));
                uvPlane[uvIdx + 1] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
        }
    }
}

// ── NV12 → BGR24 conversion (BT.601 limited range) ──
// Used when consumer wants RGB24 but shared memory has NV12 (fallback path).
// dstStride = row stride in bytes (may be > width*3 due to alignment).
void CVCamStream::convertNV12ToBGR24(uint8_t* dst, int dstStride,
                                      const uint8_t* src, int w, int h) {
    const uint8_t* yPlane  = src;
    const uint8_t* uvPlane = src + (w * h);

    for (int row = 0; row < h; row++) {
        uint8_t* dstRow = dst + row * dstStride;
        for (int col = 0; col < w; col++) {
            float yVal = static_cast<float>(yPlane[row * w + col]);
            yVal = (yVal - 16.0f) / 219.0f;

            const int uvIdx = (row / 2) * w + (col / 2) * 2;
            float uVal = static_cast<float>(uvPlane[uvIdx])     - 128.0f;
            float vVal = static_cast<float>(uvPlane[uvIdx + 1]) - 128.0f;

            float r = yVal + 1.402f   * vVal / 224.0f;
            float g = yVal - 0.34414f * uVal / 224.0f - 0.71414f * vVal / 224.0f;
            float b = yVal + 1.772f   * uVal / 224.0f;

            dstRow[col * 3]     = static_cast<uint8_t>(b < 0 ? 0 : (b > 1.0f ? 255 : (int)(b * 255)));
            dstRow[col * 3 + 1] = static_cast<uint8_t>(g < 0 ? 0 : (g > 1.0f ? 255 : (int)(g * 255)));
            dstRow[col * 3 + 2] = static_cast<uint8_t>(r < 0 ? 0 : (r > 1.0f ? 255 : (int)(r * 255)));
        }
    }
}

// ── Unified placeholder frame generator ──
void CVCamStream::fillPlaceholderFrame(uint8_t* pData, int width, int height, PixelFormat fmt) {
    if (fmt == PixelFormat::NV12) {
        // Generate BGR24 placeholder first, then convert to NV12
        std::unique_ptr<BYTE[]> bgrBuf(new BYTE[width * height * 3]);
        fillNaokoBGR24(bgrBuf.get(), width, height);
        convertBGR24ToNV12(pData, bgrBuf.get(), width, height);
    } else {
        fillNaokoBGR24(pData, width, height);
    }
}

// ── Buffer size helper ──
long CVCamStream::getFrameBufferSize() {
    if (m_outputFormat == PixelFormat::NV12) {
        return m_width * m_height * 3 / 2;  // Y plane + UV plane
    } else {
        LONG stride = (m_width * 3 + 3) & ~3;
        return stride * m_height;
    }
}

// ── CVCamStream ──

CVCamStream::CVCamStream(HRESULT* phr, CSource* pParent, LPCWSTR pPinName)
    : CSourceStream(NAME("PhoneCam Camera"), phr, pParent, pPinName)
    , m_avgTimePerFrame(10000000LL / 30)  // 30fps default
    , m_frame_buffer(new uint8_t[MAX_FRAME_SIZE])
{
    VCAM_LOG("CVCamStream created: pin=%ls %dx%d@%dfps", pPinName, m_width, m_height, m_fps);
}

CVCamStream::~CVCamStream() = default;

STDMETHODIMP CVCamStream::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IKsPropertySet) {
        return GetInterface(static_cast<IKsPropertySet*>(this), ppv);
    }
    if (riid == IID_IAMStreamConfig) {
        return GetInterface(static_cast<IAMStreamConfig*>(this), ppv);
    }
    return CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CVCamStream::Active() {
    VCAM_LOG("Active() called");
    HRESULT hr = CSourceStream::Active();
    if (SUCCEEDED(hr)) {
        if (!m_reader.open()) {
            VCAM_LOG("Active: shared memory not available (will retry)");
        } else {
            VCAM_LOG("Active: shared memory opened OK");
        }
    } else {
        VCAM_LOG("Active: FAILED hr=0x%08X", hr);
    }
    return hr;
}

HRESULT CVCamStream::Inactive() {
    VCAM_LOG("Inactive() called");
    m_reader.close();
    return CSourceStream::Inactive();
}

// ── Media type helpers ──

void CVCamStream::fillMediaType_NV12(CMediaType* pMediaType) {
    VIDEOINFOHEADER* pvi = reinterpret_cast<VIDEOINFOHEADER*>(
        pMediaType->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
    ZeroMemory(pvi, sizeof(VIDEOINFOHEADER));
    pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pvi->bmiHeader.biWidth = m_width;
    pvi->bmiHeader.biHeight = m_height;  // NV12 uses positive (bottom-up)
    pvi->bmiHeader.biPlanes = 1;
    pvi->bmiHeader.biBitCount = 12;  // NV12 = 12 bpp
    pvi->bmiHeader.biCompression = MAKEFOURCC('N','V','1','2');
    pvi->bmiHeader.biSizeImage = m_width * m_height * 3 / 2;
    pvi->AvgTimePerFrame = m_avgTimePerFrame;

    pMediaType->SetType(&MEDIATYPE_Video);
    pMediaType->SetSubtype(&MEDIASUBTYPE_NV12);
    pMediaType->SetFormatType(&FORMAT_VideoInfo);
    pMediaType->SetTemporalCompression(FALSE);
    pMediaType->SetSampleSize(m_width * m_height * 3 / 2);
}

void CVCamStream::fillMediaType_RGB24(CMediaType* pMediaType) {
    VIDEOINFOHEADER* pvi = reinterpret_cast<VIDEOINFOHEADER*>(
        pMediaType->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
    ZeroMemory(pvi, sizeof(VIDEOINFOHEADER));
    const LONG stride = (m_width * 3 + 3) & ~3;
    const LONG sampleSize = stride * m_height;
    pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pvi->bmiHeader.biWidth = m_width;
    pvi->bmiHeader.biHeight = -m_height;  // Negative = top-down
    pvi->bmiHeader.biPlanes = 1;
    pvi->bmiHeader.biBitCount = 24;
    pvi->bmiHeader.biCompression = BI_RGB;
    pvi->bmiHeader.biSizeImage = sampleSize;
    pvi->AvgTimePerFrame = m_avgTimePerFrame;

    pMediaType->SetType(&MEDIATYPE_Video);
    pMediaType->SetSubtype(&MEDIASUBTYPE_RGB24);
    pMediaType->SetFormatType(&FORMAT_VideoInfo);
    pMediaType->SetTemporalCompression(FALSE);
    pMediaType->SetSampleSize(sampleSize);
    VCAM_LOG("fillMediaType_RGB24: %dx%d stride=%d sampleSize=%d biHeight=%d orientation=top-down",
             m_width, m_height, (int)stride, (int)sampleSize, -m_height);
}

HRESULT CVCamStream::GetMediaType(CMediaType* pMediaType) {
#if VCAM_DEFAULT_RGB24_ONLY
    VCAM_LOG("GetMediaType(default) -> RGB24 %dx%d (RGB24-only mode)", m_width, m_height);
    fillMediaType_RGB24(pMediaType);
#else
    VCAM_LOG("GetMediaType(default) -> NV12 %dx%d", m_width, m_height);
    fillMediaType_NV12(pMediaType);
#endif
    return S_OK;
}

HRESULT CVCamStream::GetMediaType(int iPosition, CMediaType* pMediaType) {
    if (iPosition < 0) return E_INVALIDARG;
#if VCAM_DEFAULT_RGB24_ONLY
    // Only expose RGB24
    if (iPosition > 0) return VFW_S_NO_MORE_ITEMS;
    VCAM_LOG("GetMediaType(0) -> RGB24 %dx%d (RGB24-only)", m_width, m_height);
    fillMediaType_RGB24(pMediaType);
#else
    if (iPosition > 1) return VFW_S_NO_MORE_ITEMS;
    if (iPosition == 0) {
        VCAM_LOG("GetMediaType(0) -> NV12 %dx%d biComp=NV12 biBit=12 biSizeImage=%d sampleSize=%d",
                 m_width, m_height, m_width * m_height * 3 / 2, m_width * m_height * 3 / 2);
        fillMediaType_NV12(pMediaType);
    } else {
        VCAM_LOG("GetMediaType(1) -> RGB24 %dx%d biComp=BI_RGB biBit=24", m_width, m_height);
        fillMediaType_RGB24(pMediaType);
    }
#endif
    return S_OK;
}

HRESULT CVCamStream::CheckMediaType(const CMediaType* pMediaType) {
    if (!pMediaType) return E_POINTER;
    const GUID* pMajor = pMediaType->Type();
    if (*pMajor != MEDIATYPE_Video) {
        VCAM_LOG("CheckMediaType: REJECTED (not MEDIATYPE_Video)");
        return E_FAIL;
    }
    const GUID* pSub = pMediaType->Subtype();
#if VCAM_DEFAULT_RGB24_ONLY
    bool ok = (*pSub == MEDIASUBTYPE_RGB24);
#else
    bool ok = (*pSub == MEDIASUBTYPE_NV12 || *pSub == MEDIASUBTYPE_RGB24);
#endif
    VCAM_LOG("CheckMediaType: %s (sub=%s mode=%s)",
        ok ? "OK" : "REJECTED",
        (*pSub == MEDIASUBTYPE_NV12) ? "NV12" :
        (*pSub == MEDIASUBTYPE_RGB24) ? "RGB24" : "OTHER",
        VCAM_DEFAULT_RGB24_ONLY ? "RGB24-only" : "NV12+RGB24");
    if (!ok) return E_FAIL;
    const GUID* pFmt = pMediaType->FormatType();
    if (*pFmt == FORMAT_VideoInfo) return S_OK;
    VCAM_LOG("CheckMediaType: REJECTED (not FORMAT_VideoInfo)");
    return E_FAIL;
}

HRESULT CVCamStream::DecideBufferSize(IMemAllocator* pAlloc,
                                        ALLOCATOR_PROPERTIES* pRequest) {
    pRequest->cBuffers = 2;

    // Use negotiated media type, not m_outputFormat
    PixelFormat actualFormat = PixelFormat::NV12;
    const GUID* pSubtype = m_mt.Subtype();
    if (pSubtype && *pSubtype == MEDIASUBTYPE_RGB24) {
        actualFormat = PixelFormat::RGB24;
    }

    if (actualFormat == PixelFormat::NV12) {
        pRequest->cbBuffer = m_width * m_height * 3 / 2;
    } else {
        LONG stride = (m_width * 3 + 3) & ~3;
        pRequest->cbBuffer = stride * m_height;
    }

    ALLOCATOR_PROPERTIES actual;
    HRESULT hr = pAlloc->SetProperties(pRequest, &actual);
    if (FAILED(hr)) return hr;

    if (actual.cbBuffer < pRequest->cbBuffer) return E_FAIL;
    return S_OK;
}

HRESULT CVCamStream::FillBuffer(IMediaSample* pSample) {
    BYTE* pData = nullptr;
    HRESULT hrPtr = pSample->GetPointer(&pData);
    if (FAILED(hrPtr) || !pData) {
        VCAM_LOG("FATAL: GetPointer failed hr=0x%08X pData=%p", hrPtr, pData);
        return E_POINTER;
    }
    long buf_size = pSample->GetSize();
    int width = 0, height = 0;
    uint64_t sequence = 0;

    // Determine actual output format from negotiated media type (m_mt)
    PixelFormat actualFormat = PixelFormat::NV12;
    const GUID* pSubtype = m_mt.Subtype();
    if (pSubtype && *pSubtype == MEDIASUBTYPE_RGB24) {
        actualFormat = PixelFormat::RGB24;
    }

    // Pre-calculate buffer sizes
    long nv12_size = m_width * m_height * 3 / 2;
    long rgb_stride = (m_width * 3 + 3) & ~3;
    long rgb_size = rgb_stride * m_height;
    long data_size = (actualFormat == PixelFormat::NV12) ? nv12_size : rgb_size;

    // Read shared memory (10ms timeout)
    SharedPixelFormat shmFmt = SharedPixelFormat::BGR24;  // only valid if got_frame=true
    bool got_frame = false;
    __try {
        got_frame = m_reader.read(m_frame_buffer.get(), MAX_FRAME_SIZE,
                                        width, height, sequence, shmFmt, 10);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        VCAM_LOG("FATAL: read() crashed! Exception code: 0x%08X", GetExceptionCode());
        fillPlaceholderFrame(pData, m_width, m_height, actualFormat);
        pSample->SetActualDataLength(data_size);
        return S_OK;
    }

    // Sanity check: output data must fit in sample buffer
    if (data_size > buf_size) {
        VCAM_LOG("FATAL: data_size=%ld > buf_size=%ld! format=%d", data_size, buf_size, (int)actualFormat);
        fillPlaceholderFrame(pData, m_width, m_height, actualFormat);
        pSample->SetActualDataLength(min(data_size, buf_size));
        return S_OK;
    }

    // ── Determine effective format: prefer fresh read, fall back to cached ──
    // Initialize with safe defaults so placeholder/no-frame log paths never
    // read uninitialized variables (fixes KI-007 Debug Runtime Check #3).
    SharedPixelFormat effectiveShmFmt = SharedPixelFormat::BGR24;
    int effectiveWidth = 0, effectiveHeight = 0;
    uint64_t effectiveSeq = 0;
    bool hasFrame = false;

    if (got_frame) {
        // Fresh frame — update cache
        bool wasWaiting = !m_has_last_frame;  // P1-5: track first-frame transition
        m_has_last_frame = true;
        m_lastShmFmt   = shmFmt;
        m_lastWidth    = width;
        m_lastHeight   = height;
        m_lastSequence = sequence;

        effectiveShmFmt = shmFmt;
        effectiveWidth  = width;
        effectiveHeight = height;
        effectiveSeq    = sequence;
        hasFrame = true;

        if (wasWaiting) {
            VCAM_LOG("FIRST-FRAME: transition from placeholder to live stream %dx%d fmt=%d seq=%llu",
                     width, height, (int)shmFmt, (unsigned long long)sequence);
        }

        if (width != m_width || height != m_height) {
            VCAM_LOG("Resolution mismatch! DShow wants %dx%d, received %dx%d", m_width, m_height, width, height);
        }
    } else if (m_has_last_frame) {
        // No new frame — reuse cached last frame with its actual format
        effectiveShmFmt = m_lastShmFmt;
        effectiveWidth  = m_lastWidth;
        effectiveHeight = m_lastHeight;
        effectiveSeq    = m_lastSequence;
        hasFrame = true;
    } else {
        hasFrame = false;
    }

    if (hasFrame) {
        if (actualFormat == PixelFormat::NV12 && effectiveShmFmt == SharedPixelFormat::NV12) {
            int copySize = m_width * m_height * 3 / 2;
            if (copySize <= MAX_FRAME_SIZE) {
                std::memcpy(pData, m_frame_buffer.get(), copySize);
            }
        } else if (actualFormat == PixelFormat::NV12 && effectiveShmFmt == SharedPixelFormat::BGR24) {
            int frame_size = m_width * m_height * 3;
            if (frame_size <= MAX_FRAME_SIZE) {
                convertBGR24ToNV12(pData, m_frame_buffer.get(), m_width, m_height);
            }
        } else if (actualFormat == PixelFormat::RGB24) {
            const LONG stride = rgb_stride;
            std::memset(pData, 0, data_size);
            if (effectiveShmFmt == SharedPixelFormat::NV12) {
                convertNV12ToBGR24(pData, stride, m_frame_buffer.get(), m_width, m_height);
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

    // Set timestamps
    REFERENCE_TIME rtStart = m_frame_count * m_avgTimePerFrame;
    REFERENCE_TIME rtEnd = rtStart + m_avgTimePerFrame;
    pSample->SetTime(&rtStart, &rtEnd);
    pSample->SetSyncPoint(TRUE);
    m_frame_count++;

    if (m_frame_count <= 3 || m_frame_count % 60 == 0) {
        const char* pathStr = "placeholder";
        const char* stateStr = "waiting";  // P1-5: explicit startup state
        // Use safe sentinel values for placeholder/no-frame log to avoid
        // suggesting a real pixel format when no frame exists.
        int logEffFmt = -1;
        int logEffW = 0, logEffH = 0;
        if (hasFrame) {
            logEffFmt = (int)effectiveShmFmt;
            logEffW = effectiveWidth;
            logEffH = effectiveHeight;
            if (got_frame) {
                stateStr = "fresh";  // P1-5: first or new frame from shared memory
                if (actualFormat == PixelFormat::NV12 && effectiveShmFmt == SharedPixelFormat::NV12)
                    pathStr = "NV12->NV12";
                else if (actualFormat == PixelFormat::NV12 && effectiveShmFmt == SharedPixelFormat::BGR24)
                    pathStr = "BGR24->NV12";
                else if (actualFormat == PixelFormat::RGB24 && effectiveShmFmt == SharedPixelFormat::NV12)
                    pathStr = "NV12->BGR24";
                else if (actualFormat == PixelFormat::RGB24)
                    pathStr = "BGR24->RGB24";
            } else {
                stateStr = "cached";  // P1-5: reusing last frame
                pathStr = "cached-reuse";
            }
        }
        VCAM_LOG("FillBuffer #%lld: outFmt=%s effFmt=%d shmSize=%dx%d sample=%ld path=%s state=%s fresh=%d reuse=%d",
            (long long)m_frame_count,
            (actualFormat == PixelFormat::NV12) ? "NV12" : "RGB24",
            logEffFmt,
            logEffW, logEffH,
            (long)data_size,
            pathStr,
            stateStr,
            got_frame ? 1 : 0,
            (hasFrame && !got_frame) ? 1 : 0);
    }

    return S_OK;
}

// IKsPropertySet implementation — CRITICAL for camera enumeration
// Without this, apps won't recognize this filter as a capture device
HRESULT CVCamStream::Set(REFGUID guidPropSet, DWORD dwPropID,
    LPVOID pInstanceData, DWORD cbInstanceData,
    LPVOID pPropData, DWORD cbPropData)
{
    return E_NOTIMPL;
}

HRESULT CVCamStream::Get(REFGUID guidPropSet, DWORD dwPropID,
    LPVOID pInstanceData, DWORD cbInstanceData,
    LPVOID pPropData, DWORD cbPropData, DWORD* pcbReturned)
{
    if (guidPropSet == AMPROPSETID_Pin) {
        if (dwPropID == AMPROPERTY_PIN_CATEGORY) {
            if (pPropData && cbPropData >= sizeof(GUID)) {
                *(GUID*)pPropData = PIN_CATEGORY_CAPTURE;
                *pcbReturned = sizeof(GUID);
                return S_OK;
            }
            if (pcbReturned) {
                *pcbReturned = sizeof(GUID);
                return S_OK;
            }
            return E_UNEXPECTED;
        }
    }
    return E_NOTIMPL;
}

HRESULT CVCamStream::QuerySupported(REFGUID guidPropSet, DWORD dwPropID,
    DWORD* pTypeSupport)
{
    if (guidPropSet == AMPROPSETID_Pin) {
        if (dwPropID == AMPROPERTY_PIN_CATEGORY) {
            if (pTypeSupport) {
                *pTypeSupport = KSPROPERTY_SUPPORT_GET;
            }
            return S_OK;
        }
    }
    return E_NOTIMPL;
}

// ── IAMStreamConfig implementation ──

HRESULT CVCamStream::SetFormat(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (pmt->formattype == FORMAT_VideoInfo && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        VIDEOINFOHEADER* pvi = reinterpret_cast<VIDEOINFOHEADER*>(pmt->pbFormat);
        m_width = pvi->bmiHeader.biWidth;
        m_height = abs(pvi->bmiHeader.biHeight);
        m_avgTimePerFrame = pvi->AvgTimePerFrame ? pvi->AvgTimePerFrame : (10000000LL / 30);
        m_fps = static_cast<int>(10000000LL / m_avgTimePerFrame);

        // Detect format from subtype
        const char* subName = "UNKNOWN";
        if (pmt->subtype == MEDIASUBTYPE_NV12) {
            m_outputFormat = PixelFormat::NV12;
            subName = "NV12";
        } else if (pmt->subtype == MEDIASUBTYPE_RGB24) {
            m_outputFormat = PixelFormat::RGB24;
            subName = "RGB24";
        }
        VCAM_LOG("SetFormat: %s %dx%d@%dfps biComp=0x%08X biBit=%d biHeight=%d biSizeImage=%u",
                 subName, m_width, m_height, m_fps,
                 (unsigned)pvi->bmiHeader.biCompression,
                 (int)pvi->bmiHeader.biBitCount,
                 (int)pvi->bmiHeader.biHeight,
                 (unsigned)pvi->bmiHeader.biSizeImage);
    } else {
        VCAM_LOG("SetFormat: UNSUPPORTED formattype");
    }
    return S_OK;
}

HRESULT CVCamStream::GetFormat(AM_MEDIA_TYPE** ppmt) {
    if (!ppmt) return E_POINTER;
    CMediaType mt;
    HRESULT hr = GetMediaType(&mt);
    if (FAILED(hr)) return hr;
    *ppmt = CreateMediaType(&mt);
    return *ppmt ? S_OK : E_OUTOFMEMORY;
}

HRESULT CVCamStream::GetNumberOfCapabilities(int* piCount, int* piSize) {
    if (!piCount || !piSize) return E_POINTER;
#if VCAM_DEFAULT_RGB24_ONLY
    *piCount = 1;  // RGB24 only
#else
    *piCount = 2;  // NV12 + RGB24
#endif
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

HRESULT CVCamStream::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
#if VCAM_DEFAULT_RGB24_ONLY
    if (iIndex < 0 || iIndex > 0) return VFW_S_NO_MORE_ITEMS;
#else
    if (iIndex < 0 || iIndex > 1) return E_INVALIDARG;
#endif
    if (!ppmt || !pSCC) return E_POINTER;

    CMediaType mt;
#if VCAM_DEFAULT_RGB24_ONLY
    fillMediaType_RGB24(&mt);
#else
    if (iIndex == 0) {
        fillMediaType_NV12(&mt);
    } else {
        fillMediaType_RGB24(&mt);
    }
#endif

    *ppmt = CreateMediaType(&mt);
    if (!*ppmt) return E_OUTOFMEMORY;

    // Fill VIDEO_STREAM_CONFIG_CAPS
    VIDEO_STREAM_CONFIG_CAPS* pCaps = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);
    ZeroMemory(pCaps, sizeof(VIDEO_STREAM_CONFIG_CAPS));
    pCaps->guid = FORMAT_VideoInfo;
    pCaps->VideoStandard = AnalogVideo_None;
    pCaps->InputSize.cx = m_width;
    pCaps->InputSize.cy = m_height;
    pCaps->MinCroppingSize.cx = 320;
    pCaps->MinCroppingSize.cy = 240;
    pCaps->MaxCroppingSize.cx = 1920;
    pCaps->MaxCroppingSize.cy = 1080;
    pCaps->CropGranularityX = 2;
    pCaps->CropGranularityY = 2;
    pCaps->MinOutputSize.cx = 320;
    pCaps->MinOutputSize.cy = 240;
    pCaps->MaxOutputSize.cx = 1920;
    pCaps->MaxOutputSize.cy = 1080;
    pCaps->OutputGranularityX = 2;
    pCaps->OutputGranularityY = 2;
    pCaps->StretchTapsX = 0;
    pCaps->StretchTapsY = 0;
    pCaps->ShrinkTapsX = 0;
    pCaps->ShrinkTapsY = 0;
    pCaps->MinFrameInterval = 333333;    // 30fps
    pCaps->MaxFrameInterval = 10000000;  // 1fps
    pCaps->MinBitsPerSecond = 320 * 240 * 12 * 1;
    pCaps->MaxBitsPerSecond = 1920 * 1080 * 24 * 30;

    return S_OK;
}

// ── CVCam (the filter) ──

CVCam::CVCam(LPUNKNOWN punk, HRESULT* phr)
    : CSource(NAME("PhoneCam Camera"), punk, CLSID_PhoneCamVCam, phr)
{
    VCAM_LOG("CVCam constructor: creating pin...");
    // Create one output pin
    CAutoLock cAutoLock(&m_cStateLock);
    auto* pStream = new CVCamStream(phr, this, L"Out");
    if (FAILED(*phr)) {
        VCAM_LOG("CVCam: pin creation FAILED hr=0x%08X", *phr);
        delete pStream;
        return;
    }
    VCAM_LOG("CVCam: pin created OK");
    AddRef();
}

STDMETHODIMP CVCam::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IAMFilterMiscFlags) {
        return GetInterface(static_cast<IAMFilterMiscFlags*>(this), ppv);
    }
    return CSource::NonDelegatingQueryInterface(riid, ppv);
}

CUnknown* WINAPI CVCam::CreateInstance(LPUNKNOWN punk, HRESULT* phr) {
    VCAM_LOG("CreateInstance called");
    auto* pNewObject = new CVCam(punk, phr);
    if (pNewObject == nullptr) {
        VCAM_LOG("CreateInstance: FAILED (out of memory)");
        *phr = E_OUTOFMEMORY;
    } else {
        VCAM_LOG("CreateInstance: OK");
    }
    return pNewObject;
}

} // namespace vcam
} // namespace phonecam

// Factory function for DLL entry point (avoids streams.h in dll_main.cpp)
extern "C" CUnknown* CreatePhoneCamFilter(LPUNKNOWN punk, HRESULT* phr) {
    return phonecam::vcam::CVCam::CreateInstance(punk, phr);
}
