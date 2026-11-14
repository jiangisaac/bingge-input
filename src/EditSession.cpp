#include "TV1TSF.h"

EditSession::EditSession(TextService* service,
                         ITfContext* context,
                         WPARAM wParam,
                         LPARAM lParam,
                         int action,
                         BOOL* eaten)
    : service_(service), context_(context), wParam_(wParam), lParam_(lParam), action_(action), eaten_(eaten)
{
    service_->AddRef();
    context_->AddRef();
}

EditSession::~EditSession()
{
    context_->Release();
    service_->Release();
}

STDMETHODIMP EditSession::QueryInterface(REFIID riid, void** object)
{
    if (object == nullptr)
    {
        return E_POINTER;
    }
    *object = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfEditSession)
    {
        *object = static_cast<ITfEditSession*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EditSession::AddRef()
{
    return InterlockedIncrement(reinterpret_cast<LONG*>(&refCount_));
}

STDMETHODIMP_(ULONG) EditSession::Release()
{
    const ULONG count = InterlockedDecrement(reinterpret_cast<LONG*>(&refCount_));
    if (count == 0)
    {
        delete this;
    }
    return count;
}

STDMETHODIMP EditSession::DoEditSession(TfEditCookie editCookie)
{
    return service_->ExecuteEditSession(editCookie, context_, wParam_, lParam_, action_, eaten_);
}
