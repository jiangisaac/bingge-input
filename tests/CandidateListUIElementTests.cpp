#include "CandidateListUIElement.h"

#include <iostream>
#include <string>
#include <vector>

EXTERN_C const CLSID CLSID_TV1TextService =
    {0x7f3c21a4, 0x6d0b, 0x4a8e, {0x9c, 0x20, 0x11, 0xe2, 0xa7, 0xb3, 0xc5, 0x01}};

namespace
{
constexpr DWORD kExpectedContentFlags = TF_CLUIE_COUNT |
                                        TF_CLUIE_SELECTION |
                                        TF_CLUIE_STRING |
                                        TF_CLUIE_PAGEINDEX |
                                        TF_CLUIE_CURRENTPAGE;

bool Expect(bool condition, const wchar_t* message)
{
    if (!condition)
        std::wcerr << message << L'\n';
    return condition;
}

bool ExpectString(ITfCandidateListUIElement* element, UINT index, const wchar_t* expected)
{
    BSTR text = nullptr;
    const HRESULT hr = element->GetString(index, &text);
    bool passed = Expect(SUCCEEDED(hr) && text != nullptr, L"GetString failed");
    if (SUCCEEDED(hr) && text != nullptr)
        passed &= Expect(std::wstring(text) == expected, L"Unexpected candidate text");
    SysFreeString(text);
    return passed;
}
}

int main()
{
    bool passed = true;
    passed &= Expect(CandidateListUIElement::Create(nullptr, {}, 0, nullptr) == E_POINTER,
                     L"Null output pointer was accepted");

    std::vector<std::wstring> source;
    for (UINT index = 0; index < 12; ++index)
        source.push_back(L"candidate-" + std::to_wstring(index));

    CandidateListUIElement* element = nullptr;
    HRESULT hr = CandidateListUIElement::Create(nullptr, source, 1, &element);
    passed &= Expect(SUCCEEDED(hr) && element != nullptr, L"Candidate element creation failed");
    if (element == nullptr)
        return 1;
    source[0] = L"modified-source";

    IUnknown* unknown = nullptr;
    passed &= Expect(SUCCEEDED(element->QueryInterface(IID_IUnknown,
                                                       reinterpret_cast<void**>(&unknown))) &&
                         unknown != nullptr,
                     L"IUnknown query failed");
    if (unknown != nullptr)
        unknown->Release();

    ITfUIElement* base = nullptr;
    passed &= Expect(SUCCEEDED(element->QueryInterface(IID_ITfUIElement,
                                                       reinterpret_cast<void**>(&base))) &&
                         base != nullptr,
                     L"ITfUIElement query failed");
    if (base != nullptr)
    {
        BSTR description = nullptr;
        passed &= Expect(SUCCEEDED(base->GetDescription(&description)) && description != nullptr,
                         L"Description allocation failed");
        SysFreeString(description);

        GUID guid = GUID_NULL;
        passed &= Expect(SUCCEEDED(base->GetGUID(&guid)) && guid == CLSID_TV1TextService,
                         L"Unexpected UI element GUID");
        BOOL shown = FALSE;
        passed &= Expect(SUCCEEDED(base->IsShown(&shown)) && shown == TRUE,
                         L"Initial shown state is false");
        passed &= Expect(SUCCEEDED(base->Show(FALSE)) && SUCCEEDED(base->IsShown(&shown)) &&
                             shown == FALSE,
                         L"Hide state was not retained");
        passed &= Expect(SUCCEEDED(base->Show(TRUE)), L"Show failed");
        base->Release();
    }

    ITfCandidateListUIElement* candidateList = nullptr;
    passed &= Expect(SUCCEEDED(element->QueryInterface(IID_ITfCandidateListUIElement,
                                                       reinterpret_cast<void**>(&candidateList))) &&
                         candidateList != nullptr,
                     L"Candidate list interface query failed");
    passed &= Expect(element->QueryInterface(IID_ITfUIElementMgr,
                                             reinterpret_cast<void**>(&unknown)) == E_NOINTERFACE &&
                         unknown == nullptr,
                     L"Unsupported interface was returned");

    DWORD flags = 0;
    passed &= Expect(SUCCEEDED(element->GetUpdatedFlags(&flags)) &&
                         (flags & TF_CLUIE_DOCUMENTMGR) != 0 &&
                         (flags & TF_CLUIE_COUNT) != 0 &&
                         (flags & TF_CLUIE_SELECTION) != 0 &&
                         (flags & TF_CLUIE_STRING) != 0 &&
                         (flags & TF_CLUIE_PAGEINDEX) != 0 &&
                         (flags & TF_CLUIE_CURRENTPAGE) != 0,
                     L"Initial update flags are incomplete");
    passed &= Expect(SUCCEEDED(element->GetUpdatedFlags(&flags)) && flags == 0,
                     L"Update flags did not reset");

    UINT count = 0;
    passed &= Expect(SUCCEEDED(element->GetCount(&count)) && count == 12,
                     L"Unexpected candidate count");
    UINT selection = 0;
    passed &= Expect(SUCCEEDED(element->GetSelection(&selection)) && selection == 5,
                     L"Selection does not match page one first item");
    UINT currentPage = 0;
    passed &= Expect(SUCCEEDED(element->GetCurrentPage(&currentPage)) && currentPage == 1,
                     L"Unexpected current page");
    passed &= ExpectString(element, 0, L"candidate-0");
    passed &= ExpectString(element, 5, L"candidate-5");

    UINT pageCount = 0;
    passed &= Expect(SUCCEEDED(element->GetPageIndex(nullptr, 0, &pageCount)) && pageCount == 3,
                     L"Page count query failed");
    UINT shortIndexes[2] = {};
    passed &= Expect(element->GetPageIndex(shortIndexes, 2, &pageCount) == E_INVALIDARG &&
                         pageCount == 3,
                     L"Short page buffer was accepted");
    UINT indexes[3] = {};
    passed &= Expect(SUCCEEDED(element->GetPageIndex(indexes, 3, &pageCount)) &&
                         indexes[0] == 0 && indexes[1] == 5 && indexes[2] == 10,
                     L"Unexpected page indexes");
    passed &= Expect(SUCCEEDED(element->SetPageIndex(indexes, 3)),
                     L"Matching page indexes were rejected");
    UINT invalidIndexes[3] = {0, 4, 10};
    passed &= Expect(element->SetPageIndex(invalidIndexes, 3) == E_INVALIDARG,
                     L"Incompatible page indexes were accepted");

    BSTR retainedText = nullptr;
    passed &= Expect(SUCCEEDED(element->GetString(0, &retainedText)) && retainedText != nullptr,
                     L"Retained BSTR allocation failed");
    passed &= Expect(SUCCEEDED(element->SetContent({L"first", L"second"}, 99)),
                     L"Candidate update failed");
    passed &= Expect(retainedText != nullptr && std::wstring(retainedText) == L"candidate-0",
                     L"Returned BSTR changed after candidate update");
    SysFreeString(retainedText);
    passed &= Expect(SUCCEEDED(element->GetCount(&count)) && count == 2 &&
                         SUCCEEDED(element->GetCurrentPage(&currentPage)) && currentPage == 0 &&
                         SUCCEEDED(element->GetSelection(&selection)) && selection == 0,
                     L"Page was not clamped after update");
    passed &= ExpectString(element, 0, L"first");
    passed &= Expect(SUCCEEDED(element->GetUpdatedFlags(&flags)) &&
                         (flags & kExpectedContentFlags) == kExpectedContentFlags,
                     L"Content update flags are incomplete");

    BSTR invalidText = reinterpret_cast<BSTR>(static_cast<UINT_PTR>(1));
    passed &= Expect(element->GetString(2, &invalidText) == E_INVALIDARG && invalidText == nullptr,
                     L"Out-of-range candidate was accepted");
    passed &= Expect(SUCCEEDED(element->SetContent({}, 4)) &&
                         SUCCEEDED(element->GetCount(&count)) && count == 0 &&
                         SUCCEEDED(element->GetSelection(&selection)) && selection == 0 &&
                         SUCCEEDED(element->GetPageIndex(nullptr, 0, &pageCount)) && pageCount == 0 &&
                         SUCCEEDED(element->GetCurrentPage(&currentPage)) && currentPage == 0,
                     L"Empty candidate state is invalid");
    passed &= Expect(SUCCEEDED(element->SetPageIndex(nullptr, 0)),
                     L"Empty page index update failed");

    ITfDocumentMgr* documentMgr = reinterpret_cast<ITfDocumentMgr*>(static_cast<UINT_PTR>(1));
    passed &= Expect(SUCCEEDED(element->GetDocumentMgr(&documentMgr)) && documentMgr == nullptr,
                     L"Null document manager was not preserved");
    passed &= Expect(element->GetCount(nullptr) == E_POINTER &&
                         element->GetSelection(nullptr) == E_POINTER &&
                         element->GetCurrentPage(nullptr) == E_POINTER &&
                         element->GetPageIndex(nullptr, 0, nullptr) == E_POINTER,
                     L"Null argument validation failed");

    if (candidateList != nullptr)
        candidateList->Release();
    passed &= Expect(element->AddRef() == 2 && element->Release() == 1,
                     L"Explicit reference count mismatch");
    passed &= Expect(element->Release() == 0, L"Final reference count mismatch");

    if (passed)
        std::wcout << L"CandidateListUIElement tests passed\n";
    return passed ? 0 : 1;
}
