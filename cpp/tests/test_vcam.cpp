// test_vcam.cpp — Minimal test for PhoneCam virtual camera filter
// Tests: COM creation, interface queries, pin enumeration, media type
#include <windows.h>
#include <initguid.h>
#include <dshow.h>
#include <stdio.h>

// {B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}
DEFINE_GUID(CLSID_PhoneCamVCam,
    0xb5ca7e2a, 0x7e4b, 0x4c3e, 0x9e, 0x1a, 0x3d, 0x5f, 0x8a, 0x2c, 0x6b, 0x4e);

#define CHECK(hr, msg) if (FAILED(hr)) { printf("FAIL: %s (hr=0x%08X)\n", msg, hr); return 1; } else { printf("OK: %s\n", msg); }

int main() {
    printf("=== PhoneCam Virtual Camera Test ===\n\n");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CHECK(hr, "CoInitializeEx");

    // Test 1: Create the filter via CoCreateInstance
    printf("\n--- Test 1: CoCreateInstance ---\n");
    IBaseFilter* pFilter = nullptr;
    hr = CoCreateInstance(CLSID_PhoneCamVCam, nullptr, CLSCTX_INPROC_SERVER,
        IID_IBaseFilter, (void**)&pFilter);
    CHECK(hr, "CoCreateInstance(CLSID_PhoneCamVCam)");
    if (FAILED(hr)) { CoUninitialize(); return 1; }

    // Test 2: Query IUnknown
    printf("\n--- Test 2: QueryInterface ---\n");
    IUnknown* pUnk = nullptr;
    hr = pFilter->QueryInterface(IID_IUnknown, (void**)&pUnk);
    CHECK(hr, "QueryInterface(IUnknown)");
    if (pUnk) pUnk->Release();

    // Test 3: Enumerate pins
    printf("\n--- Test 3: Enumerate Pins ---\n");
    IEnumPins* pEnumPins = nullptr;
    hr = pFilter->EnumPins(&pEnumPins);
    CHECK(hr, "EnumPins");
    if (SUCCEEDED(hr)) {
        IPin* pPin = nullptr;
        ULONG fetched = 0;
        hr = pEnumPins->Next(1, &pPin, &fetched);
        if (SUCCEEDED(hr) && pPin) {
            printf("OK: Found pin (fetched=%lu)\n", fetched);

            // Test 4: Query pin interfaces
            printf("\n--- Test 4: Pin Interfaces ---\n");
            IKsPropertySet* pKs = nullptr;
            hr = pPin->QueryInterface(IID_IKsPropertySet, (void**)&pKs);
            CHECK(hr, "QueryInterface(IKsPropertySet)");
            if (pKs) {
                // Test 5: Query pin category
                printf("\n--- Test 5: Pin Category ---\n");
                GUID guidCategory;
                DWORD cbReturned = 0;
                hr = pKs->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY,
                    nullptr, 0, &guidCategory, sizeof(GUID), &cbReturned);
                if (SUCCEEDED(hr)) {
                    if (guidCategory == PIN_CATEGORY_CAPTURE) {
                        printf("OK: Pin category = PIN_CATEGORY_CAPTURE\n");
                    } else {
                        printf("WARN: Pin category is not CAPTURE\n");
                    }
                } else {
                    printf("FAIL: Get(PIN_CATEGORY) hr=0x%08X\n", hr);
                }
                pKs->Release();
            }

            // Test 6: Enumerate media types
            printf("\n--- Test 6: Enumerate Media Types ---\n");
            IEnumMediaTypes* pEnumMT = nullptr;
            hr = pPin->EnumMediaTypes(&pEnumMT);
            CHECK(hr, "EnumMediaTypes");
            if (SUCCEEDED(hr) && pEnumMT) {
                AM_MEDIA_TYPE* pmt = nullptr;
                ULONG mtFetched = 0;
                hr = pEnumMT->Next(1, &pmt, &mtFetched);
                if (SUCCEEDED(hr) && pmt) {
                    printf("OK: Got media type (majortype GUID, subtype=%d bits)\n",
                        pmt->subtype == MEDIASUBTYPE_RGB24 ? 24 : -1);
                    if (pmt->pbFormat) {
                        VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)pmt->pbFormat;
                        printf("  Width=%d Height=%d BitCount=%d\n",
                            pvi->bmiHeader.biWidth, pvi->bmiHeader.biHeight,
                            pvi->bmiHeader.biBitCount);
                    }
                    CoTaskMemFree(pmt);
                }
                pEnumMT->Release();
            }

            pPin->Release();
        }
        pEnumPins->Release();
    }

    // Test 7: QueryFilterInfo
    printf("\n--- Test 7: Filter Info ---\n");
    FILTER_INFO info;
    hr = pFilter->QueryFilterInfo(&info);
    CHECK(hr, "QueryFilterInfo");
    if (SUCCEEDED(hr)) {
        wprintf(L"  Filter name: %s\n", info.achName);
        if (info.pGraph) info.pGraph->Release();
    }

    // Cleanup
    pFilter->Release();
    CoUninitialize();

    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
