// test_com.cpp — Test COM virtual camera filter in isolation
// Build: cl /EHsc test_com.cpp /link ole32.lib uuid.lib
// Usage: test_com.exe [dll_path]

#include <windows.h>
#include <dshow.h>
#include <initguid.h>
#include <stdio.h>

// Our CLSID
DEFINE_GUID(CLSID_PhoneCamVCam,
    0xb5ca7e2a, 0x7e4b, 0x4c3e, 0x9e, 0x1a, 0x3d, 0x5f, 0x8a, 0x2c, 0x6b, 0x4e);

// Null renderer CLSID (from uuid.lib)
DEFINE_GUID(CLSID_NullRenderer_Test,
    0xc1f400a4, 0x3f08, 0x11d3, 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37);

#define CHECK_HR(msg, hr) do { \
    if (FAILED(hr)) { printf("FAIL: %s (hr=0x%08X)\n", msg, hr); return 1; } \
    else { printf("OK: %s\n", msg); } \
} while(0)

int main(int argc, char** argv) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CHECK_HR("CoInitializeEx", hr);

    // Step 1: Load DLL and get class object
    printf("\n=== Step 1: DllGetClassObject ===\n");
    HMODULE hDll = nullptr;
    if (argc > 1) {
        hDll = LoadLibraryA(argv[1]);
        if (!hDll) { printf("FAIL: LoadLibrary(%s) error %d\n", argv[1], GetLastError()); return 1; }
        printf("OK: Loaded %s\n", argv[1]);
    }

    typedef HRESULT (STDAPICALLTYPE *pfnDllGetClassObject)(REFCLSID, REFIID, void**);
    pfnDllGetClassObject fnGetClassObject = nullptr;
    
    if (hDll) {
        fnGetClassObject = (pfnDllGetClassObject)GetProcAddress(hDll, "DllGetClassObject");
        if (!fnGetClassObject) { printf("FAIL: DllGetClassObject not found\n"); return 1; }
    }

    IClassFactory* pFactory = nullptr;
    if (fnGetClassObject) {
        hr = fnGetClassObject(CLSID_PhoneCamVCam, IID_IClassFactory, (void**)&pFactory);
    } else {
        hr = CoGetClassObject(CLSID_PhoneCamVCam, CLSCTX_INPROC_SERVER, nullptr, IID_IClassFactory, (void**)&pFactory);
    }
    CHECK_HR("Get IClassFactory", hr);

    // Step 2: Create filter instance
    printf("\n=== Step 2: CreateInstance ===\n");
    IBaseFilter* pFilter = nullptr;
    hr = pFactory->CreateInstance(nullptr, IID_IBaseFilter, (void**)&pFilter);
    CHECK_HR("CreateInstance IID_IBaseFilter", hr);
    printf("  pFilter = %p\n", pFilter);

    // Step 3: Query interfaces
    printf("\n=== Step 3: QueryInterface ===\n");
    
    IUnknown* pUnk = nullptr;
    hr = pFilter->QueryInterface(IID_IUnknown, (void**)&pUnk);
    CHECK_HR("QI IUnknown", hr);
    if (pUnk) { pUnk->Release(); pUnk = nullptr; }

    IAMFilterMiscFlags* pFlags = nullptr;
    hr = pFilter->QueryInterface(IID_IAMFilterMiscFlags, (void**)&pFlags);
    CHECK_HR("QI IAMFilterMiscFlags", hr);
    if (pFlags) {
        ULONG flags = pFlags->GetMiscFlags();
        printf("  GetMiscFlags = 0x%X (expect IS_SOURCE=0x1)\n", flags);
        pFlags->Release();
    }

    // Step 4: Enumerate pins
    printf("\n=== Step 4: Enumerate pins ===\n");
    IEnumPins* pEnumPins = nullptr;
    hr = pFilter->EnumPins(&pEnumPins);
    CHECK_HR("EnumPins", hr);
    
    IPin* pPin = nullptr;
    ULONG fetched = 0;
    hr = pEnumPins->Next(1, &pPin, &fetched);
    CHECK_HR("Get first pin", hr);
    printf("  pPin = %p, fetched = %lu\n", pPin, fetched);

    // Step 5: Query pin interfaces
    printf("\n=== Step 5: Pin QI ===\n");
    
    IKsPropertySet* pKsProp = nullptr;
    hr = pPin->QueryInterface(IID_IKsPropertySet, (void**)&pKsProp);
    CHECK_HR("QI IKsPropertySet on pin", hr);
    if (pKsProp) { pKsProp->Release(); pKsProp = nullptr; }

    IAMStreamConfig* pStreamCfg = nullptr;
    hr = pPin->QueryInterface(IID_IAMStreamConfig, (void**)&pStreamCfg);
    CHECK_HR("QI IAMStreamConfig on pin", hr);
    if (pStreamCfg) {
        int count = 0, size = 0;
        hr = pStreamCfg->GetNumberOfCapabilities(&count, &size);
        CHECK_HR("GetNumberOfCapabilities", hr);
        printf("  Capabilities: count=%d, size=%d\n", count, size);

        AM_MEDIA_TYPE* pmt = nullptr;
        hr = pStreamCfg->GetFormat(&pmt);
        CHECK_HR("GetFormat", hr);
        if (pmt && pmt->formattype == FORMAT_VideoInfo) {
            VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
            printf("  Format: %dx%d, %d bpp\n", 
                vih->bmiHeader.biWidth, abs(vih->bmiHeader.biHeight), 
                vih->bmiHeader.biBitCount);
        }
        if (pmt) CoTaskMemFree(pmt);
        pStreamCfg->Release();
    }

    // Step 6: Enumerate media types
    printf("\n=== Step 6: EnumMediaTypes ===\n");
    IEnumMediaTypes* pEnumMT = nullptr;
    hr = pPin->EnumMediaTypes(&pEnumMT);
    CHECK_HR("EnumMediaTypes", hr);

    AM_MEDIA_TYPE* pmt = nullptr;
    hr = pEnumMT->Next(1, &pmt, &fetched);
    CHECK_HR("Next media type", hr);
    if (pmt) {
        printf("  Media type: major=");
        if (pmt->majortype == MEDIATYPE_Video) printf("Video");
        else printf("?");
        printf(", subtype=");
        if (pmt->subtype == MEDIASUBTYPE_RGB24) printf("RGB24");
        else printf("?");
        printf("\n");
        CoTaskMemFree(pmt);
    }

    // Step 7: Build filter graph
    printf("\n=== Step 7: Build filter graph ===\n");
    IGraphBuilder* pGraph = nullptr;
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, 
                          IID_IGraphBuilder, (void**)&pGraph);
    CHECK_HR("Create FilterGraph", hr);

    hr = pGraph->AddFilter(pFilter, L"PhoneCam Camera");
    CHECK_HR("AddFilter", hr);

    // Step 8: Try to connect to a null renderer
    printf("\n=== Step 8: Connect pin ===\n");
    IBaseFilter* pNullRenderer = nullptr;
    hr = CoCreateInstance(CLSID_NullRenderer_Test, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IBaseFilter, (void**)&pNullRenderer);
    CHECK_HR("Create NullRenderer", hr);

    hr = pGraph->AddFilter(pNullRenderer, L"Null Renderer");
    CHECK_HR("Add NullRenderer to graph", hr);

    // Get null renderer's input pin
    IEnumPins* pEnumNullPins = nullptr;
    pNullRenderer->EnumPins(&pEnumNullPins);
    IPin* pNullPin = nullptr;
    pEnumNullPins->Next(1, &pNullPin, &fetched);
    pEnumNullPins->Release();

    printf("  Connecting PhoneCam output pin to NullRenderer input pin...\n");
    hr = pGraph->Connect(pPin, pNullPin);
    if (FAILED(hr)) {
        printf("WARN: Connect failed (hr=0x%08X) — trying Render...\n", hr);
        hr = pGraph->Render(pPin);
        CHECK_HR("Render pin", hr);
    } else {
        printf("OK: Connected!\n");
    }

    // Step 9: Run the graph
    printf("\n=== Step 9: Run filter graph ===\n");
    IMediaControl* pControl = nullptr;
    hr = pGraph->QueryInterface(IID_IMediaControl, (void**)&pControl);
    CHECK_HR("QI IMediaControl", hr);

    hr = pControl->Run();
    CHECK_HR("Run graph", hr);
    printf("  Graph is running! Waiting 3 seconds...\n");
    Sleep(3000);

    hr = pControl->Stop();
    CHECK_HR("Stop graph", hr);

    // Cleanup
    printf("\n=== Cleanup ===\n");
    pControl->Release();
    if (pNullPin) pNullPin->Release();
    if (pNullRenderer) { pGraph->RemoveFilter(pNullRenderer); pNullRenderer->Release(); }
    pGraph->RemoveFilter(pFilter);
    pGraph->Release();
    pPin->Release();
    pEnumPins->Release();
    pFilter->Release();
    pFactory->Release();
    
    if (hDll) FreeLibrary(hDll);
    CoUninitialize();
    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
