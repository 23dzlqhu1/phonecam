#include "virtual_cam_filter.h"
#include <initguid.h>
#include <Ks.h>
#include <Ksmedia.h>
#include <cstring>
#include <memory>
#include <Windows.h>
#include <cstdio>

// Always-visible debug output (OutputDebugStringA is never filtered)
static void VCAM_LOG(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA("[VCAM] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
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
    pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pvi->bmiHeader.biWidth = m_width;
    pvi->bmiHeader.biHeight = -m_height;  // Negative = top-down
    pvi->bmiHeader.biPlanes = 1;
    pvi->bmiHeader.biBitCount = 24;
    pvi->bmiHeader.biCompression = BI_RGB;
    pvi->bmiHeader.biSizeImage = DIBSIZE(pvi->bmiHeader);
    pvi->AvgTimePerFrame = m_avgTimePerFrame;

    pMediaType->SetType(&MEDIATYPE_Video);
    pMediaType->SetSubtype(&MEDIASUBTYPE_RGB24);
    pMediaType->SetFormatType(&FORMAT_VideoInfo);
    pMediaType->SetTemporalCompression(FALSE);
}

HRESULT CVCamStream::GetMediaType(CMediaType* pMediaType) {
    // Default to NV12 (preferred by video conferencing apps)
    VCAM_LOG("GetMediaType(default) -> NV12 %dx%d", m_width, m_height);
    fillMediaType_NV12(pMediaType);
    return S_OK;
}

HRESULT CVCamStream::GetMediaType(int iPosition, CMediaType* pMediaType) {
    if (iPosition < 0) return E_INVALIDARG;
    if (iPosition > 1) return VFW_S_NO_MORE_ITEMS;

    if (iPosition == 0) {
        VCAM_LOG("GetMediaType(0) -> NV12 %dx%d", m_width, m_height);
        fillMediaType_NV12(pMediaType);   // Index 0: NV12 (preferred)
    } else {
        VCAM_LOG("GetMediaType(1) -> RGB24 %dx%d", m_width, m_height);
        fillMediaType_RGB24(pMediaType);  // Index 1: RGB24 (fallback)
    }
    return S_OK;
}

HRESULT CVCamStream::CheckMediaType(const CMediaType* pMediaType) {
    if (!pMediaType) return E_POINTER;
    const GUID* pMajor = pMediaType->Type();
    if (*pMajor != MEDIATYPE_Video) return E_FAIL;
    const GUID* pSub = pMediaType->Subtype();
    bool ok = (*pSub == MEDIASUBTYPE_NV12 || *pSub == MEDIASUBTYPE_RGB24);
    VCAM_LOG("CheckMediaType: %s (sub=%s)",
        ok ? "OK" : "REJECTED",
        (*pSub == MEDIASUBTYPE_NV12) ? "NV12" :
        (*pSub == MEDIASUBTYPE_RGB24) ? "RGB24" : "OTHER");
    if (!ok) return E_FAIL;
    const GUID* pFmt = pMediaType->FormatType();
    if (*pFmt == FORMAT_VideoInfo) return S_OK;
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
    int width, height;
    uint64_t sequence;

    // Determine actual output format from negotiated media type (m_mt)
    PixelFormat actualFormat = PixelFormat::NV12;
    const GUID* pSubtype = m_mt.Subtype();
    if (pSubtype && *pSubtype == MEDIASUBTYPE_RGB24) {
        actualFormat = PixelFormat::RGB24;
    }

    VCAM_LOG("FillBuffer: buf_size=%ld format=%d subtype=%s",
        buf_size, (int)actualFormat,
        (pSubtype && *pSubtype == MEDIASUBTYPE_NV12) ? "NV12" :
        (pSubtype && *pSubtype == MEDIASUBTYPE_RGB24) ? "RGB24" : "UNKNOWN");

    // Pre-calculate buffer size (needed in exception handler too)
    long data_size;
    if (actualFormat == PixelFormat::NV12) {
        data_size = m_width * m_height * 3 / 2;
    } else {
        LONG stride = (m_width * 3 + 3) & ~3;
        data_size = stride * m_height;
    }

    // Short timeout — 10ms instead of 100ms for responsive stop/reconfigure
    VCAM_LOG("FillBuffer: calling m_reader.read()...");
    bool got_frame = false;
    __try {
        got_frame = m_reader.read(m_frame_buffer.get(), MAX_FRAME_SIZE,
                                        width, height, sequence, 10);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        VCAM_LOG("FATAL: read() crashed! Exception code: 0x%08X", GetExceptionCode());
        fillPlaceholderFrame(pData, m_width, m_height, actualFormat);
        pSample->SetActualDataLength(data_size);
        return S_OK;
    }
    VCAM_LOG("FillBuffer: read() returned %d", got_frame ? 1 : 0);

    // Sanity check: output data must fit in sample buffer
    if (data_size > buf_size) {
        VCAM_LOG("FATAL: data_size=%ld > buf_size=%ld! format=%d", data_size, buf_size, (int)actualFormat);
        fillPlaceholderFrame(pData, m_width, m_height, actualFormat);
        pSample->SetActualDataLength(min(data_size, buf_size));
        return S_OK;
    }

    if (got_frame) {
        m_has_last_frame = true;
        
        if (width != m_width || height != m_height) {
            VCAM_LOG("Resolution mismatch! DShow wants %dx%d, but we received %dx%d. Will process safely.", m_width, m_height, width, height);
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
    }

    // Set timestamps
    REFERENCE_TIME rtStart = m_frame_count * m_avgTimePerFrame;
    REFERENCE_TIME rtEnd = rtStart + m_avgTimePerFrame;
    pSample->SetTime(&rtStart, &rtEnd);
    pSample->SetSyncPoint(TRUE);
    m_frame_count++;

    if (m_frame_count <= 3 || m_frame_count % 300 == 0) {
        VCAM_LOG("FillBuffer #%lld OK, data_size=%ld, got_frame=%d",
            (long long)m_frame_count, data_size, got_frame ? 1 : 0);
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
        if (pmt->subtype == MEDIASUBTYPE_NV12) {
            m_outputFormat = PixelFormat::NV12;
            VCAM_LOG("SetFormat: NV12 %dx%d@%dfps", m_width, m_height, m_fps);
        } else if (pmt->subtype == MEDIASUBTYPE_RGB24) {
            m_outputFormat = PixelFormat::RGB24;
            VCAM_LOG("SetFormat: RGB24 %dx%d@%dfps", m_width, m_height, m_fps);
        }
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
    *piCount = 2;  // NV12 + RGB24
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

HRESULT CVCamStream::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
    if (iIndex < 0 || iIndex > 1) return E_INVALIDARG;
    if (!ppmt || !pSCC) return E_POINTER;

    CMediaType mt;
    if (iIndex == 0) {
        fillMediaType_NV12(&mt);
    } else {
        fillMediaType_RGB24(&mt);
    }

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
