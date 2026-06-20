#pragma once
#include <streams.h>
#include <memory>
#include "shared_memory.h"

// GUID declared extern — defined in dll_main.cpp via DEFINE_GUID + initguid.h
// DEFINE_GUID creates extern "C" linkage, so declaration must match
extern "C" { extern const CLSID CLSID_PhoneCamVCam; }

namespace phonecam {
namespace vcam {

// Output pixel format — NV12 preferred by video conferencing apps, RGB24 fallback
enum class PixelFormat { NV12, RGB24 };

class CVCam;

// ── CVCamStream: Output pin ──
class CVCamStream : public CSourceStream, public IKsPropertySet, public IAMStreamConfig {
public:
    DECLARE_IUNKNOWN

    CVCamStream(HRESULT* phr, CSource* pParent, LPCWSTR pPinName);
    ~CVCamStream() override;

    // CSourceStream overrides
    HRESULT FillBuffer(IMediaSample* pSample) override;
    HRESULT GetMediaType(CMediaType* pMediaType) override;
    HRESULT GetMediaType(int iPosition, CMediaType* pMediaType) override;
    HRESULT CheckMediaType(const CMediaType* pMediaType) override;
    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pRequest) override;
    HRESULT Active() override;
    HRESULT Inactive() override;

    // IAMStreamConfig
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE** ppmt) override;
    STDMETHODIMP GetNumberOfCapabilities(int* piCount, int* piSize) override;
    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) override;

    // IKsPropertySet
    STDMETHODIMP Set(REFGUID guidPropSet, DWORD dwPropID,
        LPVOID pInstanceData, DWORD cbInstanceData,
        LPVOID pPropData, DWORD cbPropData) override;
    STDMETHODIMP Get(REFGUID guidPropSet, DWORD dwPropID,
        LPVOID pInstanceData, DWORD cbInstanceData,
        LPVOID pPropData, DWORD cbPropData, DWORD* pcbReturned) override;
    STDMETHODIMP QuerySupported(REFGUID guidPropSet,
        DWORD dwPropID, DWORD* pTypeSupport) override;

    // Override NonDelegatingQueryInterface to expose IKsPropertySet
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

private:
    // Format helpers
    void fillMediaType_NV12(CMediaType* pMediaType);
    void fillMediaType_RGB24(CMediaType* pMediaType);
    long getFrameBufferSize();
    void convertBGR24ToNV12(uint8_t* dst, const uint8_t* src, int w, int h);
    void convertNV12ToBGR24(uint8_t* dst, int dstStride, const uint8_t* src, int w, int h);
    void fillPlaceholderFrame(uint8_t* pData, int width, int height, PixelFormat fmt);

    SharedMemoryReader m_reader;
    REFERENCE_TIME m_avgTimePerFrame;
    int m_width = 1280;
    int m_height = 720;
    int m_fps = 30;
    LONGLONG m_frame_count = 0;
    std::unique_ptr<uint8_t[]> m_frame_buffer;
    bool m_has_last_frame = false;
    PixelFormat m_outputFormat = PixelFormat::NV12;  // Prefer NV12

    // Cached last-frame metadata (valid only when m_has_last_frame=true)
    SharedPixelFormat m_lastShmFmt = SharedPixelFormat::BGR24;
    int m_lastWidth = 0;
    int m_lastHeight = 0;
    uint64_t m_lastSequence = 0;
};

// ── CVCam: Source filter ──
class CVCam : public CSource, public IAMFilterMiscFlags {
public:
    DECLARE_IUNKNOWN

    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE GetMiscFlags() override { return AM_FILTER_MISC_FLAGS_IS_SOURCE; }

    static CUnknown* WINAPI CreateInstance(LPUNKNOWN punk, HRESULT* phr);

private:
    CVCam(LPUNKNOWN punk, HRESULT* phr);
};

} // namespace vcam
} // namespace phonecam
