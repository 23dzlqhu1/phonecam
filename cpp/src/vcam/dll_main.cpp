// dll_main.cpp — Full version with virtual_cam_filter.h
#include <initguid.h>
#include <streams.h>
#include "virtual_cam_filter.h"

// {B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}
DEFINE_GUID(CLSID_PhoneCamVCam,
    0xb5ca7e2a, 0x7e4b, 0x4c3e, 0x9e, 0x1a, 0x3d, 0x5f, 0x8a, 0x2c, 0x6b, 0x4e);

HMODULE g_hModule = nullptr;
// Reference count for DllCanUnloadNow
static LONG g_cLocks = 0;


class CVCamClassFactory : public CUnknown, public IClassFactory {
public:
    CVCamClassFactory(LPUNKNOWN pUnk, HRESULT* phr) : CUnknown(NAME("PhoneCam Factory"), pUnk) {
        // Ensure ref count starts at 1, not 0 (see DllGetClassObject comment)
        NonDelegatingAddRef();
    }
    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IClassFactory || riid == IID_IUnknown)
            return GetInterface(static_cast<IClassFactory*>(this), ppv);
        return CUnknown::NonDelegatingQueryInterface(riid, ppv);
    }
    STDMETHODIMP CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        HRESULT hr = S_OK;
        CUnknown* pObject = phonecam::vcam::CVCam::CreateInstance(nullptr, &hr);
        if (!pObject) return hr ? hr : E_OUTOFMEMORY;
        hr = pObject->NonDelegatingQueryInterface(riid, ppv);
        pObject->NonDelegatingRelease();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) InterlockedIncrement(&g_cLocks);
        else InterlockedDecrement(&g_cLocks);
        return S_OK;
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        OutputDebugStringA("[VCAM] DllMain: DLL_PROCESS_ATTACH\n");
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    char buf[256];
    snprintf(buf, sizeof(buf), "[VCAM] DllGetClassObject: rclsid={%08X-...} riid={%08X-...}\n",
        rclsid.Data1, riid.Data1);
    OutputDebugStringA(buf);
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_PhoneCamVCam) {
        OutputDebugStringA("[VCAM] DllGetClassObject: CLASS_E_CLASSNOTAVAILABLE\n");
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    HRESULT hr = S_OK;
    auto* pFactory = new CVCamClassFactory(nullptr, &hr);
    if (!pFactory) return E_OUTOFMEMORY;
    // Match OBS pattern: QI + Release with AddRef safety net.
    // CUnknown starts m_cRef=0. GetInterface (inside NonDelegatingQI) AddRefs
    // to 1. NonDelegatingRelease then decrements to 0 — object destroyed!
    // COM activation AddRefs after we return, but the window is fragile.
    // With AddRef() in constructor, m_cRef starts at 1, GetInterface -> 2, 
    // Release -> 1. Object safely alive when COM gets the pointer.
    hr = pFactory->NonDelegatingQueryInterface(riid, ppv);
    pFactory->NonDelegatingRelease();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_cLocks == 0) ? S_OK : S_FALSE;
}
