// test_icf.cpp — Test IClassFactory inheritance with streams.h
#include <streams.h>
#include <stdio.h>

class TestFactory : public CUnknown, public IClassFactory {
public:
    TestFactory(LPUNKNOWN pUnk, HRESULT* phr)
        : CUnknown(NAME("Test"), pUnk) {}

    DECLARE_IUNKNOWN

    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IClassFactory) {
            return GetInterface(static_cast<IClassFactory*>(this), ppv);
        }
        return CUnknown::NonDelegatingQueryInterface(riid, ppv);
    }

    STDMETHODIMP CreateInstance(LPUNKNOWN p, REFIID r, void** v) override {
        if (v) *v = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP LockServer(BOOL f) override { return S_OK; }
};

int main() {
    printf("sizeof(CUnknown)=%zu\n", sizeof(CUnknown));
    printf("sizeof(IClassFactory)=%zu\n", sizeof(IClassFactory));
    printf("sizeof(TestFactory)=%zu\n", sizeof(TestFactory));
    printf("OK: IClassFactory inheritance works\n");
    return 0;
}
