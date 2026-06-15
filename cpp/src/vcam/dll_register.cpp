// dll_register.cpp — DirectShow + COM registration (complete)
#include <windows.h>
#include <strmif.h>
#include <dshow.h>

// CLSID defined in dll_main.cpp via DEFINE_GUID + initguid.h
// DEFINE_GUID creates extern "C" linkage, so declaration must match
extern "C" { extern const CLSID CLSID_PhoneCamVCam; }

extern HMODULE g_hModule;

static const wchar_t CLSID_STR[] = L"{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}";

static void GetDllPath(wchar_t* path, int maxLen) {
    if (g_hModule) GetModuleFileNameW(g_hModule, path, maxLen);
    else path[0] = 0;
}

STDAPI DllRegisterServer() {
    wchar_t dllPath[MAX_PATH];
    GetDllPath(dllPath, MAX_PATH);
    if (!dllPath[0]) return E_FAIL;

    // 1. COM CLSID registration
    wchar_t keyPath[256];
    HKEY hKey;

    wsprintfW(keyPath, L"CLSID\\%s", CLSID_STR);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ,
            (const BYTE*)L"PhoneCam Camera", 30);
        RegCloseKey(hKey);
    }

    wsprintfW(keyPath, L"CLSID\\%s\\InprocServer32", CLSID_STR);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ,
            (const BYTE*)dllPath, (DWORD)((wcslen(dllPath) + 1) * sizeof(wchar_t)));
        RegSetValueExA(hKey, "ThreadingModel", 0, REG_SZ,
            (const BYTE*)"Both", 5);
        RegCloseKey(hKey);
    }

    // 2. IFilterMapper2 registration WITH pin media types
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IFilterMapper2* pMapper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
        IID_IFilterMapper2, (void**)&pMapper);
    if (FAILED(hr)) { CoUninitialize(); return S_OK; }

    // Define supported media types (NV12 preferred + RGB24 fallback)
    REGPINTYPES pinMediaTypes[] = {
        { (CLSID*)&MEDIATYPE_Video, (CLSID*)&MEDIASUBTYPE_NV12 },
        { (CLSID*)&MEDIATYPE_Video, (CLSID*)&MEDIASUBTYPE_RGB24 },
    };

    // Define output pin
    REGFILTERPINS pinInfo;
    ZeroMemory(&pinInfo, sizeof(pinInfo));
    pinInfo.strName = L"Output";
    pinInfo.bRendered = FALSE;
    pinInfo.bOutput = TRUE;
    pinInfo.bZero = FALSE;
    pinInfo.bMany = FALSE;
    pinInfo.clsConnectsToFilter = nullptr;
    pinInfo.strConnectsToPin = nullptr;
    pinInfo.nMediaTypes = 2;
    pinInfo.lpMediaType = pinMediaTypes;

    // Define filter with 1 pin
    REGFILTER2 filterInfo;
    ZeroMemory(&filterInfo, sizeof(filterInfo));
    filterInfo.dwVersion = 1;
    filterInfo.dwMerit = MERIT_NORMAL;
    filterInfo.cPins = 1;
    filterInfo.rgPins = &pinInfo;

    hr = pMapper->RegisterFilter(
        CLSID_PhoneCamVCam,
        L"PhoneCam Camera",
        nullptr,
        &CLSID_VideoInputDeviceCategory,
        L"PhoneCam Camera",
        &filterInfo
    );

    pMapper->Release();
    CoUninitialize();
    return S_OK;
}

STDAPI DllUnregisterServer() {
    // Remove COM entries
    wchar_t keyPath[256];
    wsprintfW(keyPath, L"CLSID\\%s\\InprocServer32", CLSID_STR);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, keyPath);
    wsprintfW(keyPath, L"CLSID\\%s", CLSID_STR);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, keyPath);

    // Remove filter mapper entry
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IFilterMapper2* pMapper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
        IID_IFilterMapper2, (void**)&pMapper);
    if (SUCCEEDED(hr)) {
        pMapper->UnregisterFilter(&CLSID_VideoInputDeviceCategory,
            L"PhoneCam Camera", CLSID_PhoneCamVCam);
        pMapper->Release();
    }
    CoUninitialize();
    return S_OK;
}
