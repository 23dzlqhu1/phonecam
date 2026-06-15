// test_dll_load.cpp — Minimal: just load DLL and call DllRegisterServer
#include <windows.h>
#include <stdio.h>

int main() {
    printf("Loading DLL...\n");
    HMODULE hDll = LoadLibraryA("D:\\PhoneCam\\cpp\\build\\src\\vcam\\phonecam-virtualcam.dll");
    if (!hDll) {
        printf("FAIL: LoadLibrary error %lu\n", GetLastError());
        return 1;
    }
    printf("OK: DLL loaded at %p\n", hDll);

    typedef HRESULT (WINAPI *DllRegisterServerFunc)();
    auto pDllRegisterServer = (DllRegisterServerFunc)GetProcAddress(hDll, "DllRegisterServer");
    if (!pDllRegisterServer) {
        printf("FAIL: DllRegisterServer not found\n");
        FreeLibrary(hDll);
        return 1;
    }
    printf("OK: DllRegisterServer found at %p\n", pDllRegisterServer);

    printf("Calling DllGetClassObject...\n");
    typedef HRESULT (WINAPI *DllGetClassObjectFunc)(REFCLSID, REFIID, void**);
    auto pDllGetClassObject = (DllGetClassObjectFunc)GetProcAddress(hDll, "DllGetClassObject");
    if (!pDllGetClassObject) {
        printf("FAIL: DllGetClassObject not found\n");
        FreeLibrary(hDll);
        return 1;
    }
    printf("OK: DllGetClassObject found\n");

    // Try to create class factory
    CLSID clsid = {0xb5ca7e2a, 0x7e4b, 0x4c3e, {0x9e, 0x1a, 0x3d, 0x5f, 0x8a, 0x2c, 0x6b, 0x4e}};
    IClassFactory* pFactory = nullptr;
    HRESULT hr = pDllGetClassObject(clsid, IID_IClassFactory, (void**)&pFactory);
    printf("DllGetClassObject: hr=0x%08X\n", hr);
    if (SUCCEEDED(hr) && pFactory) {
        printf("OK: Class factory created\n");

        // Try to create instance
        IUnknown* pUnk = nullptr;
        hr = pFactory->CreateInstance(nullptr, IID_IUnknown, (void**)&pUnk);
        printf("CreateInstance: hr=0x%08X\n", hr);
        if (SUCCEEDED(hr) && pUnk) {
            printf("OK: Object created!\n");
            pUnk->Release();
        }
        pFactory->Release();
    }

    FreeLibrary(hDll);
    printf("Done.\n");
    return 0;
}
