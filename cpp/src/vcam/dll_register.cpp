// dll_register.cpp — DirectShow + COM registration (complete)
#include <windows.h>
#include <strmif.h>
#include <dshow.h>

// CLSID defined in dll_main.cpp via DEFINE_GUID + initguid.h
// DEFINE_GUID creates extern "C" linkage, so declaration must match
extern "C" { extern const CLSID CLSID_PhoneCamVCam; }

extern HMODULE g_hModule;

static const wchar_t CLSID_STR[] = L"{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}";

static const wchar_t FRIENDLY_NAME[] = L"PhoneCam Camera";

// REG_SZ 写入辅助：长度由 wcslen 计算，保证包含 terminating null
static LONG SetRegValueW(HKEY hKey, const wchar_t* name, const wchar_t* value) {
    return RegSetValueExW(hKey, name, 0, REG_SZ,
        (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}

static void GetDllPath(wchar_t* path, int maxLen) {
    if (g_hModule) GetModuleFileNameW(g_hModule, path, maxLen);
    else path[0] = 0;
}

STDAPI DllRegisterServer() {
    wchar_t dllPath[MAX_PATH];
    GetDllPath(dllPath, MAX_PATH);
    if (!dllPath[0]) return E_FAIL;  // 无法获取 DLL 自身路径 → 注册必定失败

    // ── 1. COM CLSID registration ──
    LONG lr;
    HKEY hKey = nullptr;
    wchar_t keyPath[256];

    wsprintfW(keyPath, L"CLSID\\%s", CLSID_STR);
    lr = RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);
    lr = SetRegValueW(hKey, nullptr, FRIENDLY_NAME);
    RegCloseKey(hKey);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);

    wsprintfW(keyPath, L"CLSID\\%s\\InprocServer32", CLSID_STR);
    lr = RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);
    lr = SetRegValueW(hKey, nullptr, dllPath);
    if (lr == ERROR_SUCCESS) {
        lr = SetRegValueW(hKey, L"ThreadingModel", L"Both");
    }
    RegCloseKey(hKey);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);

    // ── 2. IFilterMapper2 registration WITH pin media types ──
    // COM 初始化：只有本次调用真正成功初始化（S_OK/S_FALSE）时才需要 CoUninitialize。
    HRESULT hrCoInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE：线程已以其他模式初始化，COM 仍可用，且不配对 CoUninitialize。
    if (FAILED(hrCoInit) && hrCoInit != RPC_E_CHANGED_MODE) {
        return hrCoInit;  // COM 环境不可用 → 注册失败，不再 return S_OK
    }
    const bool comInitializedHere = (hrCoInit == S_OK || hrCoInit == S_FALSE);

    IFilterMapper2* pMapper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
        IID_IFilterMapper2, (void**)&pMapper);
    if (FAILED(hr)) {
        if (comInitializedHere) CoUninitialize();
        return hr;  // 修复假成功：FilterMapper2 创建失败必须让安装器知道
    }

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
        FRIENDLY_NAME,
        nullptr,
        &CLSID_VideoInputDeviceCategory,
        FRIENDLY_NAME,
        &filterInfo
    );

    pMapper->Release();
    if (comInitializedHere) CoUninitialize();
    return hr;  // 修复假成功：RegisterFilter 失败同样传播失败 HRESULT
}

STDAPI DllUnregisterServer() {
    // Best-effort / idempotent：注册项本来不存在不应视为严重失败。
    // Remove COM entries
    wchar_t keyPath[256];
    wsprintfW(keyPath, L"CLSID\\%s\\InprocServer32", CLSID_STR);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, keyPath);
    wsprintfW(keyPath, L"CLSID\\%s", CLSID_STR);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, keyPath);

    // Remove filter mapper entry
    HRESULT hrCoInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrCoInit) && hrCoInit != RPC_E_CHANGED_MODE) {
        return S_OK;  // 无法初始化 COM 时保持幂等返回成功
    }
    const bool comInitializedHere = (hrCoInit == S_OK || hrCoInit == S_FALSE);

    IFilterMapper2* pMapper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
        IID_IFilterMapper2, (void**)&pMapper);
    if (SUCCEEDED(hr)) {
        pMapper->UnregisterFilter(&CLSID_VideoInputDeviceCategory,
            FRIENDLY_NAME, CLSID_PhoneCamVCam);
        pMapper->Release();
    }
    if (comInitializedHere) CoUninitialize();
    return S_OK;
}
