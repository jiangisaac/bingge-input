#include "CandidateListUIElement.h"

#include <algorithm>
#include <new>

namespace
{
constexpr DWORD kContentUpdatedFlags = TF_CLUIE_COUNT |
                                       TF_CLUIE_SELECTION |
                                       TF_CLUIE_STRING |
                                       TF_CLUIE_PAGEINDEX |
                                       TF_CLUIE_CURRENTPAGE;
}

CandidateListUIElement::CandidateListUIElement(ITfDocumentMgr* documentMgr)
    : documentMgr_(documentMgr)
{
    if (documentMgr_ != nullptr)
        documentMgr_->AddRef();
}

CandidateListUIElement::~CandidateListUIElement()
{
    if (documentMgr_ != nullptr)
        documentMgr_->Release();
}

HRESULT CandidateListUIElement::Create(ITfDocumentMgr* documentMgr,
                                       const std::vector<std::wstring>& candidates,
                                       size_t currentPage,
                                       CandidateListUIElement** element)
{
    if (element == nullptr)
        return E_POINTER;
    *element = nullptr;

    CandidateListUIElement* created = new (std::nothrow) CandidateListUIElement(documentMgr);
    if (created == nullptr)
        return E_OUTOFMEMORY;

    const HRESULT hr = created->SetContent(candidates, currentPage);
    if (FAILED(hr))
    {
        created->Release();
        return hr;
    }

    *element = created;
    return S_OK;
}

HRESULT CandidateListUIElement::SetContent(const std::vector<std::wstring>& candidates,
                                           size_t currentPage)
{
    try
    {
        std::vector<std::wstring> updated(candidates);
        candidates_.swap(updated);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    const size_t pageCount = (candidates_.size() + kCandidatePageSize - 1) / kCandidatePageSize;
    currentPage_ = pageCount == 0
        ? 0
        : static_cast<UINT>(std::min(currentPage, pageCount - 1));
    updatedFlags_ |= kContentUpdatedFlags;
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::QueryInterface(REFIID riid, void** object)
{
    if (object == nullptr)
        return E_POINTER;
    *object = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfCandidateListUIElement)
        *object = static_cast<ITfCandidateListUIElement*>(this);
    else if (riid == IID_ITfUIElement)
        *object = static_cast<ITfUIElement*>(this);
    else
        return E_NOINTERFACE;

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CandidateListUIElement::AddRef()
{
    return InterlockedIncrement(reinterpret_cast<LONG*>(&refCount_));
}

STDMETHODIMP_(ULONG) CandidateListUIElement::Release()
{
    const ULONG count = InterlockedDecrement(reinterpret_cast<LONG*>(&refCount_));
    if (count == 0)
        delete this;
    return count;
}

STDMETHODIMP CandidateListUIElement::GetDescription(BSTR* description)
{
    if (description == nullptr)
        return E_POINTER;
    *description = SysAllocString(L"TV1 Candidate List");
    return *description == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CandidateListUIElement::GetGUID(GUID* guid)
{
    if (guid == nullptr)
        return E_POINTER;
    *guid = CLSID_TV1TextService;
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::Show(BOOL show)
{
    shown_ = show;
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::IsShown(BOOL* show)
{
    if (show == nullptr)
        return E_POINTER;
    *show = shown_;
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::GetUpdatedFlags(DWORD* flags)
{
    if (flags == nullptr)
        return E_POINTER;
    *flags = updatedFlags_;
    updatedFlags_ = 0;
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::GetDocumentMgr(ITfDocumentMgr** documentMgr)
{
    if (documentMgr == nullptr)
        return E_POINTER;
    *documentMgr = documentMgr_;
    if (*documentMgr != nullptr)
        (*documentMgr)->AddRef();
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::GetCount(UINT* count)
{
    if (count == nullptr)
        return E_POINTER;
    *count = static_cast<UINT>(candidates_.size());
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::GetSelection(UINT* index)
{
    if (index == nullptr)
        return E_POINTER;
    const size_t selection = static_cast<size_t>(currentPage_) * kCandidatePageSize;
    *index = selection < candidates_.size() ? static_cast<UINT>(selection) : 0;
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::GetString(UINT index, BSTR* text)
{
    if (text == nullptr)
        return E_POINTER;
    *text = nullptr;
    if (index >= candidates_.size())
        return E_INVALIDARG;

    *text = SysAllocString(candidates_[index].c_str());
    return *text == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CandidateListUIElement::GetPageIndex(UINT* indexes, UINT size, UINT* pageCount)
{
    if (pageCount == nullptr)
        return E_POINTER;

    const UINT count = static_cast<UINT>((candidates_.size() + kCandidatePageSize - 1) /
                                         kCandidatePageSize);
    *pageCount = count;
    if (indexes == nullptr)
        return size == 0 ? S_OK : E_POINTER;
    if (size < count)
        return E_INVALIDARG;

    for (UINT page = 0; page < count; ++page)
        indexes[page] = page * kCandidatePageSize;
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::SetPageIndex(UINT* indexes, UINT pageCount)
{
    UINT currentCount = 0;
    const HRESULT hr = GetPageIndex(nullptr, 0, &currentCount);
    if (FAILED(hr))
        return hr;
    if (pageCount != currentCount)
        return E_INVALIDARG;
    if (pageCount != 0 && indexes == nullptr)
        return E_POINTER;

    for (UINT page = 0; page < pageCount; ++page)
    {
        if (indexes[page] != page * kCandidatePageSize)
            return E_INVALIDARG;
    }
    return S_OK;
}

STDMETHODIMP CandidateListUIElement::GetCurrentPage(UINT* page)
{
    if (page == nullptr)
        return E_POINTER;
    *page = currentPage_;
    return S_OK;
}
