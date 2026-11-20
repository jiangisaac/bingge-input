#pragma once

#include "TV1TSF.h"

class CandidateListUIElement final : public ITfCandidateListUIElement
{
public:
    static HRESULT Create(ITfDocumentMgr* documentMgr,
                          const std::vector<std::wstring>& candidates,
                          size_t currentPage,
                          CandidateListUIElement** element);

    CandidateListUIElement(const CandidateListUIElement&) = delete;
    CandidateListUIElement& operator=(const CandidateListUIElement&) = delete;

    HRESULT SetContent(const std::vector<std::wstring>& candidates, size_t currentPage);

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetDescription(BSTR* description) override;
    STDMETHODIMP GetGUID(GUID* guid) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP IsShown(BOOL* show) override;

    STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** documentMgr) override;
    STDMETHODIMP GetCount(UINT* count) override;
    STDMETHODIMP GetSelection(UINT* index) override;
    STDMETHODIMP GetString(UINT index, BSTR* text) override;
    STDMETHODIMP GetPageIndex(UINT* indexes, UINT size, UINT* pageCount) override;
    STDMETHODIMP SetPageIndex(UINT* indexes, UINT pageCount) override;
    STDMETHODIMP GetCurrentPage(UINT* page) override;

private:
    explicit CandidateListUIElement(ITfDocumentMgr* documentMgr);
    ~CandidateListUIElement();

    ULONG refCount_ = 1;
    ITfDocumentMgr* documentMgr_ = nullptr;
    std::vector<std::wstring> candidates_;
    UINT currentPage_ = 0;
    BOOL shown_ = TRUE;
    DWORD updatedFlags_ = TF_CLUIE_DOCUMENTMGR;
};
