#include "class_factory.h"
#include "virtual_cam_filter.h"
#include <new>

extern volatile LONG g_cLocks;

CClassFactory::CClassFactory() : m_ref_count(1) {}

STDMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CClassFactory::AddRef() { return InterlockedIncrement(&m_ref_count); }
STDMETHODIMP_(ULONG) CClassFactory::Release() {
    LONG count = InterlockedDecrement(&m_ref_count);
    if (count == 0) delete this;
    return count;
}

STDMETHODIMP CClassFactory::CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    HRESULT hr = S_OK;
    CUnknown* pObject = phonecam::vcam::CVCam::CreateInstance(nullptr, &hr);
    if (!pObject) return hr;
    hr = pObject->NonDelegatingQueryInterface(riid, ppv);
    pObject->NonDelegatingRelease();
    return hr;
}

STDMETHODIMP CClassFactory::LockServer(BOOL fLock) {
    if (fLock) InterlockedIncrement(&g_cLocks);
    else InterlockedDecrement(&g_cLocks);
    return S_OK;
}
