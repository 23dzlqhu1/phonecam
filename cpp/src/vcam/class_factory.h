#pragma once
#include <windows.h>
#include <unknwn.h>

// COM Class Factory for CVCam
class CClassFactory : public IClassFactory {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

    CClassFactory();

private:
    LONG m_ref_count;
};
