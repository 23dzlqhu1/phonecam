#pragma once
// virtual_cam_mf.h — Windows 11 Virtual Camera using MFCreateVirtualCamera API
//
// Architecture:
//   MFCreateVirtualCamera() registers a virtual camera with a sourceId (CLSID).
//   When an app opens the camera, Windows calls IMFActivate::ActivateObject()
//   on our COM class, which creates a PhoneCamMFMediaSource (IMFMediaSource).
//   The media source provides a PhoneCamMFMediaStream that delivers NV12 frames
//   read from SharedMemoryReader (BGR24 -> NV12 conversion).
//
// Requirements:
//   - Windows 11 Build 22000+ (mfvirtualcamera.h / mfsensorgroup.lib)
//   - The DLL must be registered as a COM InprocServer32 for the sourceId CLSID
//   - Admin rights needed for MFVirtualCameraAccess_AllUsers

#include <windows.h>
#include <mfidl.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <mfvirtualcamera.h>
#include <mferror.h>
#include <ks.h>
#include <ksmedia.h>
#include <initguid.h>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <memory>

#include "shared_memory.h"

namespace phonecam {
namespace vcam {

// =========================================================================
//  GUIDs — defined in virtual_cam_mf.cpp
// =========================================================================

// CLSID for our IMFActivate / IMFMediaSource COM class
// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
extern const CLSID CLSID_PhoneCamMFSource;

// =========================================================================
//  Constants
// =========================================================================

constexpr int VCAM_DEFAULT_WIDTH  = 1280;
constexpr int VCAM_DEFAULT_HEIGHT = 720;
constexpr int VCAM_FPS            = 30;
constexpr int VCAM_NUM_STREAMS    = 1;

// =========================================================================
//  BGR24 -> NV12 color conversion utility
// =========================================================================

void ConvertBGR24ToNV12(const uint8_t* bgr, int width, int height,
                         uint8_t* nv12_y, uint8_t* nv12_uv);

// =========================================================================
//  PhoneCamMFMediaStream — implements IMFMediaStream2
// =========================================================================

class PhoneCamMFMediaStream : public IMFMediaStream2 {
public:
    PhoneCamMFMediaStream(DWORD streamId, IMFMediaSource* pParent);
    ~PhoneCamMFMediaStream();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMFMediaEventGenerator
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    STDMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    STDMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType,
                            HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaStream
    STDMETHODIMP GetMediaSource(IMFMediaSource** ppMediaSource) override;
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor) override;
    STDMETHODIMP RequestSample(IUnknown* pToken) override;

    // IMFMediaStream2
    STDMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    STDMETHODIMP GetStreamState(MF_STREAM_STATE* pState) override;

    // Internal methods
    HRESULT Initialize(int width, int height, int fps);
    HRESULT Start(IMFMediaType* pMediaType);
    HRESULT Stop(bool sendEvent);
    HRESULT Shutdown();

    DWORD Id() const { return m_streamId; }

private:
    std::atomic<ULONG>          m_refCount{1};
    CRITICAL_SECTION            m_lock;
    bool                        m_isShutdown = false;
    bool                        m_isSelected = false;
    MF_STREAM_STATE             m_streamState = MF_STREAM_STATE_STOPPED;
    DWORD                       m_streamId;

    IMFMediaSource*             m_parent;          // weak ref (parent owns us)
    IMFMediaEventQueue*         m_eventQueue = nullptr;
    IMFStreamDescriptor*        m_streamDesc = nullptr;
    IMFMediaType*               m_mediaType = nullptr;

    SharedMemoryReader          m_reader;
    std::unique_ptr<uint8_t[]>  m_bgrBuffer;
    std::unique_ptr<uint8_t[]>  m_nv12Buffer;

    int m_width  = VCAM_DEFAULT_WIDTH;
    int m_height = VCAM_DEFAULT_HEIGHT;
    int m_fps    = VCAM_FPS;
    REFERENCE_TIME m_avgTimePerFrame;
    LONGLONG    m_frameCount = 0;

    HRESULT _CheckShutdown();
    HRESULT _CreateFrame(IMFSample** ppSample);
    HRESULT _ConvertAndFillBuffer(IMFMediaBuffer* pBuffer, int width, int height);
};

// =========================================================================
//  PhoneCamMFMediaSource — implements IMFMediaSourceEx, IMFGetService,
//                          IKsControl
// =========================================================================

class PhoneCamMFMediaSource : public IMFMediaSourceEx,
                               public IMFGetService,
                               public IKsControl {
public:
    PhoneCamMFMediaSource();
    ~PhoneCamMFMediaSource();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMFMediaEventGenerator
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    STDMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    STDMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType,
                            HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaSource
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** ppPD) override;
    STDMETHODIMP GetCharacteristics(DWORD* pdwCharacteristics) override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Shutdown() override;
    STDMETHODIMP Start(IMFPresentationDescriptor* pPD, const GUID* pguidTimeFormat,
                       const PROPVARIANT* pvarStartPosition) override;
    STDMETHODIMP Stop() override;

    // IMFMediaSourceEx
    STDMETHODIMP GetSourceAttributes(IMFAttributes** ppAttributes) override;
    STDMETHODIMP GetStreamAttributes(DWORD dwStreamIdentifier, IMFAttributes** ppAttributes) override;
    STDMETHODIMP SetD3DManager(IUnknown* pManager) override;

    // IMFGetService
    STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject) override;

    // IKsControl
    STDMETHODIMP KsProperty(PKSPROPERTY pProperty, ULONG ulPropertyLength,
                             LPVOID pPropertyData, ULONG ulDataLength,
                             ULONG* pBytesReturned) override;
    STDMETHODIMP KsMethod(PKSMETHOD pMethod, ULONG ulMethodLength,
                           LPVOID pMethodData, ULONG ulDataLength,
                           ULONG* pBytesReturned) override;
    STDMETHODIMP KsEvent(PKSEVENT pEvent, ULONG ulEventLength,
                          LPVOID pEventData, ULONG ulDataLength,
                          ULONG* pBytesReturned) override;

    // Internal
    HRESULT Initialize();

private:
    enum class SourceState { Invalid, Stopped, Started, Shutdown };

    std::atomic<ULONG>      m_refCount{1};
    CRITICAL_SECTION        m_lock;
    SourceState             m_state = SourceState::Invalid;

    IMFMediaEventQueue*         m_eventQueue = nullptr;
    IMFPresentationDescriptor*  m_presDesc = nullptr;
    IMFAttributes*              m_sourceAttrs = nullptr;

    PhoneCamMFMediaStream*      m_streams[VCAM_NUM_STREAMS] = {};

    bool m_initialized = false;

    HRESULT _CheckShutdown();
    HRESULT _ValidatePresentationDescriptor(IMFPresentationDescriptor* pPD);
};

// =========================================================================
//  PhoneCamMFActivate — implements IMFActivate (COM class factory)
//   This is the class registered with CLSID and passed as sourceId to
//   MFCreateVirtualCamera. When Windows activates the virtual camera,
//   ActivateObject() creates and returns our PhoneCamMFMediaSource.
// =========================================================================

class PhoneCamMFActivate : public IMFActivate {
public:
    PhoneCamMFActivate();
    ~PhoneCamMFActivate();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMFActivate
    STDMETHODIMP ActivateObject(REFIID riid, void** ppv) override;
    STDMETHODIMP ShutdownObject() override;
    STDMETHODIMP DetachObject() override;

    // IMFAttributes (delegated to internal attributes object)
    STDMETHODIMP GetItem(REFGUID guidKey, PROPVARIANT* pValue) override;
    STDMETHODIMP GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) override;
    STDMETHODIMP CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) override;
    STDMETHODIMP Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) override;
    STDMETHODIMP GetUINT32(REFGUID guidKey, UINT32* punValue) override;
    STDMETHODIMP GetUINT64(REFGUID guidKey, UINT64* punValue) override;
    STDMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override;
    STDMETHODIMP GetGUID(REFGUID guidKey, GUID* pguidValue) override;
    STDMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength) override;
    STDMETHODIMP GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) override;
    STDMETHODIMP GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) override;
    STDMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) override;
    STDMETHODIMP GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) override;
    STDMETHODIMP GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) override;
    STDMETHODIMP GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) override;
    STDMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value) override;
    STDMETHODIMP DeleteItem(REFGUID guidKey) override;
    STDMETHODIMP DeleteAllItems() override;
    STDMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue) override;
    STDMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue) override;
    STDMETHODIMP SetDouble(REFGUID guidKey, double fValue) override;
    STDMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue) override;
    STDMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue) override;
    STDMETHODIMP SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) override;
    STDMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown) override;
    STDMETHODIMP LockStore() override;
    STDMETHODIMP UnlockStore() override;
    STDMETHODIMP GetCount(UINT32* pcItems) override;
    STDMETHODIMP GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) override;
    STDMETHODIMP CopyAllItems(IMFAttributes* pDest) override;

private:
    std::atomic<ULONG>      m_refCount{1};
    IMFAttributes*          m_attrs = nullptr;
    PhoneCamMFMediaSource*  m_source = nullptr;
};

// =========================================================================
//  Public API — called from the main phonecam.exe process
// =========================================================================

// Create and start a virtual camera named "PhoneCam Camera".
// Returns true on success. The virtual camera persists until
// VirtualCamMF_Stop() is called or the process exits.
bool VirtualCamMF_Start();

// Stop and remove the virtual camera.
void VirtualCamMF_Stop();

} // namespace vcam
} // namespace phonecam
