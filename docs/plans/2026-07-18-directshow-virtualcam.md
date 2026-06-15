# PhoneCam DirectShow Virtual Camera Filter - Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Create a standalone DirectShow virtual camera COM DLL that registers as a system camera device ("PhoneCam Camera"), allowing Zoom/Teams/OBS/腾讯会议 to select it as a video input source.

**Architecture:** A COM DLL (phonecam-virtualcam.dll) that implements DirectShow video capture source filter. Frames are pushed from the main phonecam.exe via shared memory (memory-mapped file). The DLL is self-registering (regsvr32 or DllRegisterServer with UAC).

**Why DirectShow (not OBS VirtualCam, not Win11 API):**
- DirectShow: Win7/8/10/11 all compatible ← 竞品都用这个
- OBS VirtualCam: requires OBS dependency, COM registration fragility
- Win11 Virtual Camera API: only Win11 22H2+, excludes Win10 users

**Tech Stack:** C++ (no Qt dependency for the DLL), DirectShow SDK (baseclasses), CMake, MSVC 2022

---

## How DirectShow Virtual Cameras Work

```
phonecam.exe (主进程)          phonecam-virtualcam.dll (COM DLL)
┌─────────────────┐           ┌──────────────────────────────┐
│ H264 decode     │           │ DirectShow Source Filter      │
│ → RGB frame     │           │                              │
│ → SharedMemory  │──────────→│ SharedMemory reader          │
│   push          │           │ → IVideoSource::FillBuffer() │
└─────────────────┘           │ → Zoom/Teams/OBS reads frame  │
                              └──────────────────────────────┘

Registration (once, with admin):
  regsvr32 phonecam-virtualcam.dll
  → Writes CLSID to HKLM\SOFTWARE\Classes\CLSID\{...}
  → Writes device to HKLM\SYSTEM\CurrentControlSet\Control\DeviceClasses\

After registration, ALL camera apps see "PhoneCam Camera" in their device list.
```

---

## Module Mapping

```
Component                  →  Files
──────────────────────────────────────────────────
SharedMemory frame bridge  →  src/vcam/shared_memory.h/cpp
DirectShow source filter   →  src/vcam/virtual_cam_filter.h/cpp
COM class factory          →  src/vcam/class_factory.h/cpp
DLL entry + registration   →  src/vcam/dll_main.cpp
Registration helper        →  src/vcam/dll_register.h/cpp
DirectShow baseclasses     →  src/vcam/ds_baseclasses/ (static lib)
CMake build                →  src/vcam/CMakeLists.txt
Test app                   →  tests/test_vcam.cpp
```

---

## Implementation Tasks

### Task 0: Vendor DirectShow Baseclasses

**Objective:** Copy the Windows SDK DirectShow baseclasses sample source into the project as a static library (same approach as OBS virtualcam). CSource/CSourceStream are NOT in strmiids.lib — they come from the SDK sample code.

**Files:**
- Create: `src/vcam/ds_baseclasses/` directory
- Copy from Windows SDK (typically `Samples/multimedia/directshow/baseclasses/`):

**Required source files:**
- `amfilter.cpp`, `source.cpp`, `cprop.cpp`, `renbase.cpp`, `wxdebug.cpp`, `wxlist.cpp`, `combase.cpp`, `dllsetup.cpp`, `fourcc.cpp`, `mtype.cpp`, `amvideo.cpp`, `winutil.cpp`, `strmaloc.cpp`, `pullpin.cpp`
- Headers: `amfilter.h`, `source.h`, `cprop.h`, `streams.h`, `wxdebug.h`, `wxlist.h`, `combase.h`, `dllsetup.h`, `fourcc.h`, `mtype.h`, `amvideo.h`, `winutil.h`, `strmaloc.h`, `pullpin.h`, `refclock.h`, `schedule.h`, `evcode.h`, `dvdmedia.h`, `vfwmsgs.h`

**CMake integration:**
```cmake
add_library(ds_baseclasses STATIC
    ds_baseclasses/amfilter.cpp
    ds_baseclasses/source.cpp
    ds_baseclasses/cprop.cpp
    ds_baseclasses/renbase.cpp
    ds_baseclasses/wxdebug.cpp
    ds_baseclasses/wxlist.cpp
    ds_baseclasses/combase.cpp
    ds_baseclasses/dllsetup.cpp
    ds_baseclasses/fourcc.cpp
    ds_baseclasses/mtype.cpp
    ds_baseclasses/amvideo.cpp
    ds_baseclasses/winutil.cpp
    ds_baseclasses/strmaloc.cpp
    ds_baseclasses/pullpin.cpp
)
target_include_directories(ds_baseclasses PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ds_baseclasses)
```

**Verification:** ds_baseclasses.lib builds without errors; virtual_cam_filter.cpp can #include <streams.h> and use CSource/CSourceStream.

---

### Task 1: Shared Memory Frame Bridge

**Objective:** Create a shared memory (memory-mapped file) IPC mechanism for frame delivery from phonecam.exe to the virtual camera DLL.

**Files:**
- Create: `src/vcam/shared_memory.h`
- Create: `src/vcam/shared_memory.cpp`

**Design:**
```cpp
// Maximum supported resolution
#define VCAM_MAX_WIDTH  1920
#define VCAM_MAX_HEIGHT 1080
#define VCAM_FRAME_SIZE (VCAM_MAX_WIDTH * VCAM_MAX_HEIGHT * 3)  // RGB24 = BGR24

// Double-buffered shared memory layout
struct SharedFrameHeader {
    uint32_t magic;           // 0x5043414D "PCAM"
    uint32_t width;           // actual frame width (<= VCAM_MAX_WIDTH)
    uint32_t height;          // actual frame height (<= VCAM_MAX_HEIGHT)
    uint32_t format;          // 0=BGR24 (DirectShow RGB24), 1=RGB32, 2=YUV420
    uint32_t frame_size;      // actual bytes per frame (width*height*3)
    uint64_t sequence;        // monotonically increasing
    double   timestamp;       // seconds since epoch
    volatile LONG active_buffer; // 0 or 1 — index of the current read buffer
    CRITICAL_SECTION cs;      // writer-reader synchronization
    uint8_t  frame_data[2 * VCAM_FRAME_SIZE]; // double-buffer: two full-size slots
};

// Producer (phonecam.exe side)
class SharedMemoryWriter {
public:
    bool open(const char* name = "PhoneCamSharedFrame");
    bool write(const uint8_t* bgr_data, int width, int height);  // BGR byte order!
    void close();
private:
    HANDLE m_mapping = nullptr;
    void* m_view = nullptr;
};

// Consumer (virtualcam DLL side)
class SharedMemoryReader {
public:
    bool open(const char* name = "PhoneCamSharedFrame");
    bool read(uint8_t* out_buffer, int& width, int& height, uint64_t& sequence);
    bool is_available() const;
    void close();
private:
    HANDLE m_mapping = nullptr;
    void* m_view = nullptr;
};
```

**Synchronization strategy:**
- Writer acquires CRITICAL_SECTION, writes to back buffer (index = 1 - active_buffer), then atomically swaps active_buffer via InterlockedExchange, releases CS.
- Reader acquires CRITICAL_SECTION, reads from active_buffer, releases CS.
- This prevents tearing: reader always gets a complete frame.

**Note on pixel format:** DirectShow's MEDIASUBTYPE_RGB24 uses BGR byte order (Blue, Green, Red). The writer must convert from whatever source format to BGR before writing.

**Verification:** Write data from one process, read from another. Verify frame content matches. Test concurrent read/write for no tearing.

---

### Task 2: DirectShow Source Filter

**Objective:** Implement the core DirectShow video capture source filter.

**Files:**
- Create: `src/vcam/virtual_cam_filter.h`
- Create: `src/vcam/virtual_cam_filter.cpp`

**Key COM Interfaces to Implement:**
- `IBaseFilter` — Core filter interface
- `IAMFilterMiscFlags` — Identifies as source filter
- `IKsPropertySet` — Pin category (must return KSCATEGORY_CAPTURE)
- Output pin: `CVCamStream` inherits from `CSourceStream`

**Architecture (follows OBS virtualcam pattern):**
```cpp
#include <streams.h>       // from our vendored baseclasses
#include <Ks.h>
#include <Ksproxy.h>

// CVCam — the filter object
class CVCam : public CSource, public IAMFilterMiscFlags {
public:
    // IUnknown
    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

    // IAMFilterMiscFlags
    ULONG STDMETHODCALLTYPE GetMiscFlags() { return AM_FILTER_MISC_FLAGS_IS_SOURCE; }

    // Create via class factory
    static CUnknown* WINAPI CreateInstance(LPUNKNOWN punk, HRESULT* phr);

private:
    CVCam(LPUNKNOWN punk, HRESULT* phr);
};

// CVCamStream — the output pin (provides frames)
class CVCamStream : public CSourceStream, public IKsPropertySet {
public:
    HRESULT FillBuffer(IMediaSample* pSample) override;
    HRESULT GetMediaType(CMediaType* pMediaType) override;
    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pRequest) override;
    HRESULT Active() override;   // Start receiving frames
    HRESULT Inactive() override; // Stop

    // IKsPropertySet — required for pin category
    STDMETHODIMP Set(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData,
                     DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData);
    STDMETHODIMP Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData,
                     DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData,
                     DWORD* pcbReturned);
    STDMETHODIMP QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport);

private:
    SharedMemoryReader m_reader;
    uint64_t m_last_sequence = 0;
    uint64_t m_frame_count = 0;
    REFERENCE_TIME m_avgTimePerFrame;
    uint8_t* m_lastFrame = nullptr;  // cached last frame for timeout fallback
};
```

**IKsPropertySet implementation (critical for camera detection):**
```cpp
HRESULT CVCamStream::Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData,
                         DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData,
                         DWORD* pcbReturned) {
    if (guidPropSet == AMPROPSETID_Pin) {
        if (dwPropID == AMPROPERTY_PIN_CATEGORY) {
            *pcbReturned = sizeof(GUID);
            if (pPropData == NULL) return S_OK;
            if (cbPropData < sizeof(GUID)) return E_UNEXPECTED;
            *(GUID*)pPropData = PIN_CATEGORY_CAPTURE;  // KSCATEGORY_CAPTURE
            return S_OK;
        }
    }
    return E_NOTIMPL;
}

HRESULT CVCamStream::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport) {
    if (guidPropSet == AMPROPSETID_Pin && dwPropID == AMPROPERTY_PIN_CATEGORY) {
        *pTypeSupport = KSPROPERTY_SUPPORT_GET;
        return S_OK;
    }
    return E_NOTIMPL;
}
```

**FillBuffer implementation (with timeout, black frame fallback, buffer size check):**
```cpp
HRESULT CVCamStream::FillBuffer(IMediaSample* pSample) {
    uint8_t* pData;
    pSample->GetPointer(&pData);
    long bufSize = pSample->GetSize();

    // Verify buffer is large enough for max resolution
    if (bufSize < VCAM_FRAME_SIZE) {
        return E_FAIL;  // buffer too small
    }

    int w, h;
    uint64_t seq;

    // Check if shared memory is available
    if (!m_reader.is_available()) {
        // Return black frame
        memset(pData, 0, VCAM_FRAME_SIZE);
        pSample->SetActualDataLength(VCAM_FRAME_SIZE);
    } else if (m_reader.read(pData, w, h, seq)) {
        // Success — got a new frame
        int frameBytes = w * h * 3;
        pSample->SetActualDataLength(frameBytes);
        // Cache last frame for timeout fallback
        memcpy(m_lastFrame, pData, frameBytes);
        m_last_sequence = seq;
    } else {
        // Timeout (100ms) — return last frame if available, else black
        if (m_lastFrame) {
            memcpy(pData, m_lastFrame, VCAM_FRAME_SIZE);
        } else {
            memset(pData, 0, VCAM_FRAME_SIZE);
        }
        pSample->SetActualDataLength(VCAM_FRAME_SIZE);
    }

    // Set timestamps
    REFERENCE_TIME rtStart = m_frame_count * m_avgTimePerFrame;
    REFERENCE_TIME rtEnd = rtStart + m_avgTimePerFrame;
    pSample->SetTime(&rtStart, &rtEnd);
    m_frame_count++;

    return S_OK;
}
```

**Media type — top-down BGR24 (negative biHeight):**
```cpp
HRESULT CVCamStream::GetMediaType(CMediaType* pMediaType) {
    VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pMediaType->AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
    ZeroMemory(vih, sizeof(VIDEOINFOHEADER));
    vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih->bmiHeader.biWidth = VCAM_MAX_WIDTH;
    vih->bmiHeader.biHeight = -(LONG)VCAM_MAX_HEIGHT;  // NEGATIVE = top-down
    vih->bmiHeader.biPlanes = 1;
    vih->bmiHeader.biBitCount = 24;
    vih->bmiHeader.biCompression = BI_RGB;
    vih->bmiHeader.biSizeImage = VCAM_FRAME_SIZE;
    vih->AvgTimePerFrame = m_avgTimePerFrame;  // e.g. 333333 for 30fps

    pMediaType->SetType(&MEDIATYPE_Video);
    pMediaType->SetSubtype(&MEDIASUBTYPE_RGB24);  // BGR byte order!
    pMediaType->SetFormatType(&FORMAT_VideoInfo);
    return S_OK;
}
```

**Verification:** Build DLL, register, check if "PhoneCam Camera" appears in Zoom's camera list. Verify pin category returns PIN_CATEGORY_CAPTURE via GraphStudio.

---

### Task 3: COM Class Factory + DLL Entry Point

**Objective:** Implement COM class factory for the filter and standard DLL entry points.

**Files:**
- Create: `src/vcam/class_factory.h`
- Create: `src/vcam/class_factory.cpp`
- Create: `src/vcam/dll_main.cpp`

**Standard DirectShow DLL exports:**
```cpp
// dll_main.cpp
STDAPI DllRegisterServer();
STDAPI DllUnregisterServer();
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv);
STDAPI DllCanUnloadNow();

// GUID for PhoneCam virtual camera
// {GENERATED-GUID-HERE}
static const GUID CLSID_PhoneCamVCam = { ... };
```

**Verification:** `regsvr32 phonecam-virtualcam.dll` succeeds.

---

### Task 4: DLL Registration (Device Registration)

**Objective:** Register the virtual camera so it appears in all camera applications.

**Files:**
- Create: `src/vcam/dll_register.h`
- Create: `src/vcam/dll_register.cpp`

**Registration approach — use IFilterMapper2::RegisterFilter():**
This is the recommended approach (used by OBS) as it handles DeviceClasses registry entries automatically.

```cpp
#include <dshow.h>

STDAPI DllRegisterServer() {
    // Standard COM registration
    // ... CLSID, InprocServer32 ...

    // Register with IFilterMapper2 — handles DeviceClasses automatically
    IFilterMapper2* pFM = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IFilterMapper2, (void**)&pFM);
    if (SUCCEEDED(hr)) {
        REGFILTER2 rf2 = {0};
        rf2.dwVersion = 1;
        rf2.dwMerit = MERIT_DO_NOT_USE + 1;  // slightly above do-not-use
        rf2.cPins = 1;
        REGFILTERPINS rfp = {0};
        rfp.strName = L"Output";
        fMediaTypes = 1;
        const GUID* types[] = { &MEDIATYPE_Video };
        rfp.lpMediaType = types;
        rf2.rgPins = &rfp;

        hr = pFM->RegisterFilter(CLSID_PhoneCamVCam, L"PhoneCam Camera", NULL,
                                  &CLSID_VideoInputDeviceCategory, NULL, &rf2);
        pFM->Release();
    }
    return hr;
}
```

**Both category registrations:**
- `CLSID_VideoInputDeviceCategory` (KSCATEGORY_CAPTURE)
- Register under `CLSID_VideoInputDeviceCategory` which IFilterMapper2 handles — this covers both KSCATEGORY_CAPTURE and KSCATEGORY_VIDEO_CAMERA automatically via DeviceClasses.

**Self-registration with UAC elevation:**
```cpp
STDAPI DllUnregisterServer() {
    IFilterMapper2* pFM = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IFilterMapper2, (void**)&pFM);
    if (SUCCEEDED(hr)) {
        pFM->UnregisterFilter(&CLSID_VideoInputDeviceCategory, NULL, CLSID_PhoneCamVCam);
        pFM->Release();
    }
    // Also remove COM registration...
    return hr;
}
```

**Verification:** After registration, `ffmpeg -list_devices true -f dshow -i dummy` shows "PhoneCam Camera" under both video capture and video camera categories.

---

### Task 5: Integration with Main Application

**Objective:** Wire the SharedMemoryWriter into the phonecam.exe decode pipeline.

**Files:**
- Modify: `src/gui/main_window.h` — Add SharedMemoryWriter member
- Modify: `src/gui/main_window.cpp` — Write decoded frames to shared memory
- Modify: `CMakeLists.txt` — Add virtualcam DLL as separate target

**Connection in MainWindow:**
```cpp
void MainWindow::onFrameDecoded(const QImage& image) {
    m_preview->updateFrame(image);
    m_frameCount++;
    // Convert QImage to BGR format for DirectShow RGB24
    QImage bgrImage = image.convertToFormat(QImage::Format_RGB888).rgbSwapped();
    m_sharedWriter.write(bgrImage.bits(), bgrImage.width(), bgrImage.height());
}
```

**CMakeLists.txt addition:**
```cmake
# Virtual Camera DLL (separate target, no Qt dependency)
add_library(phonecam-virtualcam SHARED
    src/vcam/dll_main.cpp
    src/vcam/virtual_cam_filter.cpp
    src/vcam/class_factory.cpp
    src/vcam/shared_memory.cpp
    src/vcam/dll_register.cpp
    $<TARGET_OBJECTS:ds_baseclasses>
)
target_link_libraries(phonecam-virtualcam PRIVATE
    strmiids  # DirectShow GUIDs (CSource/CSourceStream NOT included here — comes from baseclasses)
    ole32
    uuid
    ksuser
)
target_include_directories(phonecam-virtualcam PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vcam/ds_baseclasses
)
set_target_properties(phonecam-virtualcam PROPERTIES
    OUTPUT_NAME "phonecam-virtualcam"
)
```

**Verification:** Launch phonecam.exe → start streaming → open Zoom → select "PhoneCam Camera" → preview shows phone camera.

---

### Task 6: Auto-Registration on First Launch

**Objective:** Automatically register the virtual camera DLL on first launch (with UAC prompt).

**Design:** When phonecam.exe starts, check if the virtual camera is registered. If not, prompt for admin elevation and register.

**Files:**
- Modify: `src/gui/main_window.cpp` — Add registration check on startup

**Verification:** Fresh install → launch → UAC prompt → "PhoneCam Camera" appears in camera list.

---

## Execution Order

```
Task 0: Vendor DirectShow baseclasses (no dependencies)
Task 1: SharedMemory bridge (no dependencies)
Task 2: DirectShow source filter (depends on Task 0, Task 1)
Task 3: COM class factory + DLL entry (depends on Task 2)
Task 4: DLL registration (depends on Task 3)
Task 5: Integration with main app (depends on Task 1)
Task 6: Auto-registration (depends on Task 4, 5)
```

Tasks 0 and 1 can be parallelized. Task 5 can start after Task 1 completes.

---

## Risk Mitigation

| Risk                              | Mitigation                                        |
|-----------------------------------|---------------------------------------------------|
| DirectShow API complexity         | Follow OBS virtualcam pattern closely              |
| Admin required for registration   | One-time UAC prompt, fallback to manual regsvr32   |
| Frame format mismatch (RGB vs YUV)| Support BGR24 (default), negotiate via GetMediaType |
| Shared memory contention          | Double-buffer with CRITICAL_SECTION                |
| DLL loaded by multiple apps       | Shared memory is system-wide, handle gracefully    |
| Win10/Win11 API differences       | DirectShow is stable across versions, no issue     |
| FillBuffer timeout                | Return last cached frame or black frame             |
| Resolution changes at runtime     | Allocate for max 1920x1080, use header w/h fields  |

---

## Success Criteria

- [ ] `regsvr32 phonecam-virtualcam.dll` registers without error
- [ ] "PhoneCam Camera" appears in `ffmpeg -list_devices true -f dshow -i dummy`
- [ ] Zoom/腾讯会议 can select "PhoneCam Camera" as video input
- [ ] Live camera preview visible in Zoom/腾讯会议
- [ ] Frame rate ≥ 25fps in virtual camera output
- [ ] No visible latency between phone camera and virtual camera
- [ ] DLL size < 1MB
- [ ] Pin category correctly reports PIN_CATEGORY_CAPTURE
- [ ] No frame tearing under concurrent read/write

---

## Post-Review Fixes

Applied 2026-07-18 based on code review feedback:

### Critical Fixes

1. **Baseclasses source dependency** — CSource/CSourceStream are NOT in strmiids.lib. Added Task 0 to vendor the Windows SDK baseclasses sample as a static library (ds_baseclasses). Listed all required .cpp and .h files. Updated CMake to build and link against ds_baseclasses.

2. **Shared memory synchronization** — Added CRITICAL_SECTION in SharedFrameHeader for writer-reader sync. Implemented double-buffer strategy: two frame slots, writer writes to back buffer then swaps active_buffer index atomically via InterlockedExchange.

3. **IKsPropertySet pin category** — Added explicit Get/Set/QuerySupported implementation in CVCamStream returning KSCATEGORY_CAPTURE (PIN_CATEGORY_CAPTURE) for AMPROPERTY_PIN_CATEGORY. This is required for camera apps to detect the filter as a capture device.

### Important Fixes

4. **BGR byte order** — Documented that DirectShow MEDIASUBTYPE_RGB24 uses BGR byte order. Updated SharedMemoryWriter.write() signature comment and MainWindow integration to use rgbSwapped() conversion.

5. **Top-down bitmap** — Set negative biHeight (-VCAM_MAX_HEIGHT) in VIDEOINFOHEADER to indicate top-down scan order. Without this, frames would appear upside-down.

6. **FillBuffer timeout** — Added 100ms timeout handling: returns last cached frame if available, otherwise returns a black (zeroed) frame. Prevents blocking the graph indefinitely.

7. **Dual category registration** — Using IFilterMapper2::RegisterFilter() with CLSID_VideoInputDeviceCategory handles both KSCATEGORY_CAPTURE and KSCATEGORY_VIDEO_CAMERA registration automatically (via DeviceClasses).

8. **Resolution change handling** — Allocated shared memory for maximum 1920x1080 resolution. Actual resolution is communicated via the header width/height fields. Frame data region is VCAM_FRAME_SIZE (1920*1080*3 bytes).

9. **Uninitialized shared memory** — FillBuffer now checks m_reader.is_available() before attempting to read. If shared memory is not yet open, returns a black frame instead of failing.

10. **IMediaSample buffer size verification** — FillBuffer now calls pSample->GetSize() and verifies the buffer is at least VCAM_FRAME_SIZE before writing. Returns E_FAIL if buffer is too small.
