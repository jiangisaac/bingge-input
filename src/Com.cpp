#include "TV1TSF.h"

#include <cwchar>
#include <iterator>
#include <new>

HMODULE g_module = nullptr;
LONG g_serverLocks = 0;

EXTERN_C const CLSID CLSID_TV1TextService =
    {0x7f3c21a4, 0x6d0b, 0x4a8e, {0x9c, 0x20, 0x11, 0xe2, 0xa7, 0xb3, 0xc5, 0x01}};
EXTERN_C const GUID GUID_TV1Profile =
    {0x4ba8ce42, 0x58c2, 0x43f6, {0xa2, 0x54, 0x91, 0x63, 0x27, 0xbd, 0x18, 0x72}};
EXTERN_C const GUID GUID_TV1PreserveKey =
    {0xb852be43, 0xc7cd, 0x41b6, {0xa6, 0x62, 0x29, 0x2f, 0x8b, 0x71, 0xca, 0x90}};
EXTERN_C const GUID GUID_TV1SchemeKey =
    {0x2f7a9c15, 0x84d3, 0x4e6b, {0x91, 0xa7, 0x35, 0xc2, 0x68, 0xd4, 0x50, 0xf6}};
EXTERN_C const GUID GUID_TV1PunctuationKey =
    {0x6c3a91e8, 0x27bf, 0x4d52, {0xa8, 0x13, 0x59, 0xe7, 0x24, 0xc6, 0x1b, 0x8d}};

namespace
{
class ClassFactory : public IClassFactory
{
public:
    ClassFactory()
    {
        InterlockedIncrement(&g_serverLocks);
    }

    virtual ~ClassFactory()
    {
        InterlockedDecrement(&g_serverLocks);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
        {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(reinterpret_cast<LONG*>(&refCount_));
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG count = InterlockedDecrement(reinterpret_cast<LONG*>(&refCount_));
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override
    {
        if (object == nullptr)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (outer != nullptr)
        {
            return CLASS_E_NOAGGREGATION;
        }

        TextService* service = new (std::nothrow) TextService();
        if (service == nullptr)
        {
            return E_OUTOFMEMORY;
        }
        const HRESULT hr = service->QueryInterface(riid, object);
        service->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock)
        {
            InterlockedIncrement(&g_serverLocks);
        }
        else
        {
            InterlockedDecrement(&g_serverLocks);
        }
        return S_OK;
    }

private:
    ULONG refCount_ = 1;
};

HRESULT GetClassIdString(wchar_t (&value)[64])
{
    const int length = StringFromGUID2(CLSID_TV1TextService, value, static_cast<int>(std::size(value)));
    return length > 0 ? S_OK : E_FAIL;
}

HRESULT SetRegistryValue(HKEY key, const wchar_t* name, const wchar_t* value)
{
    const DWORD bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(key,
                                          name,
                                          0,
                                          REG_SZ,
                                          reinterpret_cast<const BYTE*>(value),
                                          bytes);
    return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID classId, REFIID riid, LPVOID* object)
{
    if (object == nullptr)
    {
        return E_POINTER;
    }
    *object = nullptr;
    if (classId != CLSID_TV1TextService)
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    ClassFactory* factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = factory->QueryInterface(riid, object);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return g_serverLocks == 0 ? S_OK : S_FALSE;
}

HRESULT RegisterServer()
{
    wchar_t classId[64] = {};
    HRESULT hr = GetClassIdString(classId);
    if (FAILED(hr))
    {
        return hr;
    }

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(g_module, modulePath, MAX_PATH) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wchar_t classKeyPath[128] = {};
    swprintf_s(classKeyPath, L"Software\\Classes\\CLSID\\%s", classId);
    HKEY classKey = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                     classKeyPath,
                                     0,
                                     nullptr,
                                     0,
                                     KEY_WRITE,
                                     nullptr,
                                     &classKey,
                                     nullptr);
    if (status != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(status);
    }
    hr = SetRegistryValue(classKey, nullptr, L"TV1 五笔拼音 TSF 原型");
    if (FAILED(hr))
    {
        RegCloseKey(classKey);
        return hr;
    }

    HKEY serverKey = nullptr;
    status = RegCreateKeyExW(classKey,
                             L"InprocServer32",
                             0,
                             nullptr,
                             0,
                             KEY_WRITE,
                             nullptr,
                             &serverKey,
                             nullptr);
    if (status == ERROR_SUCCESS)
    {
        hr = SetRegistryValue(serverKey, nullptr, modulePath);
        if (SUCCEEDED(hr))
        {
            hr = SetRegistryValue(serverKey, L"ThreadingModel", L"Apartment");
        }
        RegCloseKey(serverKey);
    }
    RegCloseKey(classKey);
    if (status != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(status);
    }
    if (FAILED(hr))
    {
        return hr;
    }

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
    {
        return comHr;
    }

    ITfInputProcessorProfileMgr* profileMgr = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,
                          nullptr,
                          CLSCTX_INPROC_SERVER,
                          IID_ITfInputProcessorProfileMgr,
                          reinterpret_cast<void**>(&profileMgr));
    if (SUCCEEDED(hr))
    {
        const wchar_t description[] = L"炳哥输入";
        hr = profileMgr->RegisterProfile(CLSID_TV1TextService,
                                         MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),
                                         GUID_TV1Profile,
                                         description,
                                         static_cast<ULONG>(wcslen(description)),
                                         modulePath,
                                         static_cast<ULONG>(wcslen(modulePath)),
                                         0,
                                         nullptr,
                                         0,
                                         TRUE,
                                         0);
        profileMgr->Release();
    }

    if (SUCCEEDED(hr))
    {
        ITfCategoryMgr* categoryMgr = nullptr;
        hr = CoCreateInstance(CLSID_TF_CategoryMgr,
                              nullptr,
                              CLSCTX_INPROC_SERVER,
                              IID_ITfCategoryMgr,
                              reinterpret_cast<void**>(&categoryMgr));
        if (SUCCEEDED(hr))
        {
            hr = categoryMgr->RegisterCategory(CLSID_TV1TextService,
                                               GUID_TFCAT_TIP_KEYBOARD,
                                               CLSID_TV1TextService);
            categoryMgr->Release();
        }
    }

    if (SUCCEEDED(comHr))
    {
        CoUninitialize();
    }
    return hr;
}

HRESULT UnregisterServer()
{
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
    {
        return comHr;
    }

    ITfInputProcessorProfileMgr* profileMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfileMgr,
                                  reinterpret_cast<void**>(&profileMgr));
    if (SUCCEEDED(hr))
    {
        hr = profileMgr->UnregisterProfile(CLSID_TV1TextService,
                                           MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),
                                           GUID_TV1Profile,
                                           0);
        profileMgr->Release();
    }

    ITfCategoryMgr* categoryMgr = nullptr;
    const HRESULT categoryHr = CoCreateInstance(CLSID_TF_CategoryMgr,
                                                nullptr,
                                                CLSCTX_INPROC_SERVER,
                                                IID_ITfCategoryMgr,
                                                reinterpret_cast<void**>(&categoryMgr));
    if (SUCCEEDED(categoryHr))
    {
        categoryMgr->UnregisterCategory(CLSID_TV1TextService,
                                        GUID_TFCAT_TIP_KEYBOARD,
                                        CLSID_TV1TextService);
        categoryMgr->Release();
    }

    wchar_t classId[64] = {};
    if (SUCCEEDED(GetClassIdString(classId)))
    {
        wchar_t classKeyPath[128] = {};
        swprintf_s(classKeyPath, L"Software\\Classes\\CLSID\\%s", classId);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, classKeyPath);
    }

    if (SUCCEEDED(comHr))
    {
        CoUninitialize();
    }
    return hr;
}

STDAPI DllRegisterServer()
{
    return RegisterServer();
}

STDAPI DllUnregisterServer()
{
    return UnregisterServer();
}

void WriteDiagnostic(const wchar_t* eventName) noexcept
{
    if (eventName == nullptr)
    {
        return;
    }

    wchar_t localAppData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0)
    {
        return;
    }

    wchar_t directory[MAX_PATH] = {};
    swprintf_s(directory, L"%s\\TV1TSF", localAppData);
    CreateDirectoryW(directory, nullptr);

    wchar_t path[MAX_PATH] = {};
    swprintf_s(path, L"%s\\TV1TSF.log", directory);
    HANDLE file = CreateFileW(path,
                              FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t line[256] = {};
    swprintf_s(line,
               L"%04u-%02u-%02uT%02u:%02u:%02u.%03u pid=%lu event=%s\r\n",
               time.wYear,
               time.wMonth,
               time.wDay,
               time.wHour,
               time.wMinute,
               time.wSecond,
               time.wMilliseconds,
               GetCurrentProcessId(),
               eventName);

    char utf8[512] = {};
    const int bytes = WideCharToMultiByte(CP_UTF8,
                                          0,
                                          line,
                                          -1,
                                          utf8,
                                          static_cast<int>(sizeof(utf8)),
                                          nullptr,
                                          nullptr);
    if (bytes > 1)
    {
        DWORD written = 0;
        WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
    }
    CloseHandle(file);
}
