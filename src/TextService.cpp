#include "TV1TSF.h"

#include <algorithm>
#include <new>

namespace
{
constexpr int kActionKey = 0;
constexpr int kActionCancel = 1;

void CollapseSelectionToEnd(TfEditCookie editCookie, ITfContext* context, ITfRange* range)
{
    TF_SELECTION selection = {};
    selection.range = range;
    selection.style.ase = TF_AE_END;
    selection.style.fInterimChar = FALSE;
    context->SetSelection(editCookie, 1, &selection);
}

wchar_t MapFullWidthPunctuation(WPARAM wParam)
{
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shift)
    {
        switch (wParam)
        {
        case '0': return L'）';
        case '1': return L'！';
        case '2': return L'＠';
        case '3': return L'＃';
        case '4': return L'￥';
        case '5': return L'％';
        case '6': return L'…';
        case '7': return L'＆';
        case '8': return L'＊';
        case '9': return L'（';
        case VK_OEM_1: return L'：';
        case VK_OEM_2: return L'？';
        case VK_OEM_3: return L'～';
        case VK_OEM_4: return L'｛';
        case VK_OEM_5: return L'｜';
        case VK_OEM_6: return L'｝';
        case VK_OEM_7: return L'＂';
        case VK_OEM_COMMA: return L'＜';
        case VK_OEM_PERIOD: return L'＞';
        case VK_OEM_MINUS: return L'＿';
        case VK_OEM_PLUS: return L'＋';
        default: return L'\0';
        }
    }

    switch (wParam)
    {
    case VK_OEM_1: return L'；';
    case VK_OEM_2: return L'／';
    case VK_OEM_3: return L'｀';
    case VK_OEM_4: return L'［';
    case VK_OEM_5: return L'＼';
    case VK_OEM_6: return L'］';
    case VK_OEM_7: return L'＇';
    case VK_OEM_COMMA: return L'，';
    case VK_OEM_PERIOD: return L'。';
    case VK_OEM_MINUS: return L'－';
    case VK_OEM_PLUS: return L'＝';
    default: return L'\0';
    }
}

wchar_t MapHalfWidthPunctuation(WPARAM wParam)
{
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shift)
    {
        switch (wParam)
        {
        case '0': return L')';
        case '1': return L'!';
        case '2': return L'@';
        case '3': return L'#';
        case '4': return L'$';
        case '5': return L'%';
        case '6': return L'^';
        case '7': return L'&';
        case '8': return L'*';
        case '9': return L'(';
        case VK_OEM_1: return L':';
        case VK_OEM_2: return L'?';
        case VK_OEM_3: return L'~';
        case VK_OEM_4: return L'{';
        case VK_OEM_5: return L'|';
        case VK_OEM_6: return L'}';
        case VK_OEM_7: return L'"';
        case VK_OEM_COMMA: return L'<';
        case VK_OEM_PERIOD: return L'>';
        case VK_OEM_MINUS: return L'_';
        case VK_OEM_PLUS: return L'+';
        default: return L'\0';
        }
    }

    switch (wParam)
    {
    case VK_OEM_1: return L';';
    case VK_OEM_2: return L'/';
    case VK_OEM_3: return L'`';
    case VK_OEM_4: return L'[';
    case VK_OEM_5: return L'\\';
    case VK_OEM_6: return L']';
    case VK_OEM_7: return L'\'';
    case VK_OEM_COMMA: return L',';
    case VK_OEM_PERIOD: return L'.';
    case VK_OEM_MINUS: return L'-';
    case VK_OEM_PLUS: return L'=';
    default: return L'\0';
    }
}
}

TextService::TextService()
{
    InterlockedIncrement(&g_serverLocks);
}

TextService::~TextService()
{
    DetachDocument();
    if (composition_ != nullptr)
    {
        composition_->Release();
    }
    if (compositionContext_ != nullptr)
    {
        compositionContext_->Release();
    }
    if (threadMgr_ != nullptr)
    {
        threadMgr_->Release();
    }
    InterlockedDecrement(&g_serverLocks);
}

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** object)
{
    if (object == nullptr)
    {
        return E_POINTER;
    }
    *object = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessorEx)
    {
        *object = static_cast<ITfTextInputProcessorEx*>(this);
    }
    else if (riid == IID_ITfTextInputProcessor)
    {
        *object = static_cast<ITfTextInputProcessor*>(this);
    }
    else if (riid == IID_ITfKeyEventSink)
    {
        *object = static_cast<ITfKeyEventSink*>(this);
    }
    else if (riid == IID_ITfThreadMgrEventSink)
    {
        *object = static_cast<ITfThreadMgrEventSink*>(this);
    }
    else if (riid == IID_ITfCompositionSink)
    {
        *object = static_cast<ITfCompositionSink*>(this);
    }
    else
    {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef()
{
    return InterlockedIncrement(reinterpret_cast<LONG*>(&refCount_));
}

STDMETHODIMP_(ULONG) TextService::Release()
{
    const ULONG count = InterlockedDecrement(reinterpret_cast<LONG*>(&refCount_));
    if (count == 0)
    {
        delete this;
    }
    return count;
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* threadMgr, TfClientId clientId)
{
    return ActivateInternal(threadMgr, clientId);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD)
{
    return ActivateInternal(threadMgr, clientId);
}

HRESULT TextService::ActivateInternal(ITfThreadMgr* threadMgr, TfClientId clientId)
{
    if (threadMgr == nullptr || activated_)
    {
        return E_INVALIDARG;
    }

    threadMgr_ = threadMgr;
    threadMgr_->AddRef();
    clientId_ = clientId;
    activated_ = true;

    ITfSource* source = nullptr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source))))
    {
        source->AdviseSink(IID_ITfThreadMgrEventSink,
                           static_cast<ITfThreadMgrEventSink*>(this),
                           &threadSinkCookie_);
        source->Release();
    }

    ITfKeystrokeMgr* keystrokeMgr = nullptr;
    HRESULT hr = threadMgr_->QueryInterface(IID_ITfKeystrokeMgr,
                                            reinterpret_cast<void**>(&keystrokeMgr));
    if (FAILED(hr) || keystrokeMgr == nullptr)
    {
        WriteDiagnostic(L"keystroke-mgr-unavailable");
        Deactivate();
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    hr = keystrokeMgr->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE);
    if (FAILED(hr))
    {
        keystrokeMgr->Release();
        WriteDiagnostic(L"key-sink-advise-failed");
        Deactivate();
        return hr;
    }
    keyEventSinkAdvised_ = true;

    TF_PRESERVEDKEY preservedKey = {};
    preservedKey.uVKey = VK_SPACE;
    preservedKey.uModifiers = TF_MOD_CONTROL;
    keystrokeMgr->PreserveKey(clientId_,
                              GUID_TV1PreserveKey,
                              &preservedKey,
                              L"Ctrl+Space 切换中英文",
                              static_cast<ULONG>(wcslen(L"Ctrl+Space 切换中英文")));

    TF_PRESERVEDKEY schemeKey = {};
    schemeKey.uVKey = VK_F8;
    schemeKey.uModifiers = 0;
    hr = keystrokeMgr->PreserveKey(clientId_,
                                   GUID_TV1SchemeKey,
                                   &schemeKey,
                                   L"F8 切换拼音/五笔86",
                                   static_cast<ULONG>(wcslen(L"F8 切换拼音/五笔86")));
    WriteDiagnostic(FAILED(hr) ? L"scheme-key-preserve-failed" : L"scheme-key-ready");

    TF_PRESERVEDKEY punctuationKey = {};
    punctuationKey.uVKey = VK_OEM_PERIOD;
    punctuationKey.uModifiers = TF_MOD_CONTROL;
    hr = keystrokeMgr->PreserveKey(clientId_,
                                   GUID_TV1PunctuationKey,
                                   &punctuationKey,
                                   L"Ctrl+. 切换全角/半角标点",
                                   static_cast<ULONG>(wcslen(L"Ctrl+. 切换全角/半角标点")));
    WriteDiagnostic(FAILED(hr) ? L"punctuation-key-preserve-failed" : L"punctuation-key-ready");
    keystrokeMgr->Release();
    WriteDiagnostic(L"key-sink-ready");

    candidateWindow_.Create();
    DWORD scheme = 0;
    DWORD schemeSize = sizeof(scheme);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\TV1TSF",
                     L"Scheme",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &scheme,
                     &schemeSize) == ERROR_SUCCESS &&
        scheme <= static_cast<DWORD>(InputScheme::Wubi86))
    {
        scheme_ = static_cast<InputScheme>(scheme);
    }

    DWORD vertical = 0;
    DWORD size = sizeof(vertical);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\TV1TSF",
                     L"Vertical",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &vertical,
                     &size) == ERROR_SUCCESS)
    {
        vertical_ = vertical != 0;
    }
    candidateWindow_.SetVertical(vertical_);

    DWORD fullWidth = 1;
    size = sizeof(fullWidth);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\TV1TSF",
                     L"FullWidthPunctuation",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &fullWidth,
                     &size) == ERROR_SUCCESS)
    {
        fullWidthPunctuation_ = fullWidth != 0;
    }
    candidateWindow_.SetFullWidthPunctuation(fullWidthPunctuation_);

    capsLockEnglish_ = (GetKeyState(VK_CAPITAL) & 1) != 0;
    if (capsLockEnglish_)
    {
        chineseBeforeCapsLock_ = chinese_;
        chinese_ = false;
    }

    ITfDocumentMgr* focus = nullptr;
    if (SUCCEEDED(threadMgr_->GetFocus(&focus)) && focus != nullptr)
    {
        AttachDocument(focus);
        focus->Release();
    }

    SetKeyboardOpenState();
    WriteDiagnostic(L"activate");
    return S_OK;
}

STDMETHODIMP TextService::Deactivate()
{
    if (!activated_)
    {
        return S_OK;
    }

    CancelComposition(true);
    candidateWindow_.Destroy();

    if (threadMgr_ != nullptr)
    {
        ITfKeystrokeMgr* keystrokeMgr = nullptr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keystrokeMgr))))
        {
            if (keyEventSinkAdvised_)
            {
                keystrokeMgr->UnadviseKeyEventSink(clientId_);
            }

            TF_PRESERVEDKEY preservedKey = {};
            preservedKey.uVKey = VK_SPACE;
            preservedKey.uModifiers = TF_MOD_CONTROL;
            keystrokeMgr->UnpreserveKey(GUID_TV1PreserveKey, &preservedKey);

            TF_PRESERVEDKEY schemeKey = {};
            schemeKey.uVKey = VK_F8;
            schemeKey.uModifiers = 0;
            keystrokeMgr->UnpreserveKey(GUID_TV1SchemeKey, &schemeKey);

            TF_PRESERVEDKEY punctuationKey = {};
            punctuationKey.uVKey = VK_OEM_PERIOD;
            punctuationKey.uModifiers = TF_MOD_CONTROL;
            keystrokeMgr->UnpreserveKey(GUID_TV1PunctuationKey, &punctuationKey);
            keystrokeMgr->Release();
        }
        keyEventSinkAdvised_ = false;

        ITfSource* source = nullptr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source))))
        {
            if (threadSinkCookie_ != TF_INVALID_COOKIE)
            {
                source->UnadviseSink(threadSinkCookie_);
                threadSinkCookie_ = TF_INVALID_COOKIE;
            }
            source->Release();
        }
    }

    DetachDocument();
    ClearCompositionState();
    if (threadMgr_ != nullptr)
    {
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }
    clientId_ = TF_CLIENTID_NULL;
    activated_ = false;
    WriteDiagnostic(L"deactivate");
    return S_OK;
}

void TextService::AttachDocument(ITfDocumentMgr* documentMgr)
{
    DetachDocument();
    if (documentMgr == nullptr)
    {
        return;
    }

    documentMgr_ = documentMgr;
    documentMgr_->AddRef();
    if (FAILED(documentMgr_->GetBase(&context_)) || context_ == nullptr)
    {
        return;
    }

}

void TextService::DetachDocument()
{
    if (context_ != nullptr)
    {
        context_->Release();
        context_ = nullptr;
    }
    if (documentMgr_ != nullptr)
    {
        documentMgr_->Release();
        documentMgr_ = nullptr;
    }
}

STDMETHODIMP TextService::OnSetFocus(BOOL foreground)
{
    if (!foreground)
    {
        candidateWindow_.Hide();
        CancelComposition(false);
    }
    return S_OK;
}

bool TextService::ShouldHandleKey(WPARAM wParam, LPARAM) const
{
    if (wParam == VK_CAPITAL)
        return true;
    if (capsLockEnglish_)
        return false;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 || (GetKeyState(VK_MENU) & 0x8000) != 0)
        return false;
    if (wParam == VK_SHIFT)
        return true;
    if (!chinese_)
        return false;
    if ((wParam >= 'A' && wParam <= 'Z') || wParam == VK_F8)
        return true;
    if (MapFullWidthPunctuation(wParam) != L'\0')
        return true;
    if (composition_ == nullptr)
        return false;
    if ((wParam >= '0' && wParam <= '9') ||
        (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9))
        return true;

    switch (wParam)
    {
    case VK_BACK:
    case VK_ESCAPE:
    case VK_RETURN:
    case VK_SPACE:
    case VK_F6:
        return true;
    default:
        return false;
    }
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext*, WPARAM wParam, LPARAM lParam, BOOL* eaten)
{
    if (eaten == nullptr)
    {
        return E_POINTER;
    }
    *eaten = ShouldHandleKey(wParam, lParam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten)
{
    if (context == nullptr || eaten == nullptr)
        return E_POINTER;
    *eaten = FALSE;
    if (shiftPressed_ && wParam != VK_SHIFT)
        shiftChordUsed_ = true;
    if (!ShouldHandleKey(wParam, lParam))
        return S_OK;

    if (wParam == VK_CAPITAL)
    {
        shiftChordUsed_ = shiftChordUsed_ || shiftPressed_;
        if ((lParam & 0x40000000) == 0)
        {
            if (!capsLockEnglish_)
            {
                capsLockEnglish_ = true;
                chineseBeforeCapsLock_ = chinese_;
                if (chinese_)
                    CancelComposition(true);
                chinese_ = false;
                WriteDiagnostic(L"capslock-english");
            }
            else
            {
                capsLockEnglish_ = false;
                chinese_ = chineseBeforeCapsLock_;
                WriteDiagnostic(chinese_ ? L"capslock-restore-chinese" : L"capslock-remain-english");
            }
            SetKeyboardOpenState();
        }
        return S_OK;
    }

    if (wParam == VK_SHIFT)
    {
        if ((lParam & 0x40000000) == 0)
        {
            shiftPressed_ = true;
            shiftChordUsed_ = false;
        }
        *eaten = TRUE;
        return S_OK;
    }

    if (shiftPressed_)
        shiftChordUsed_ = true;
    if (wParam >= 'A' && wParam <= 'Z' && (GetKeyState(VK_SHIFT) & 0x8000) != 0)
        return RequestKeyEdit(context, wParam, lParam, eaten);

    if (wParam == VK_F8)
    {
        ToggleScheme();
        *eaten = TRUE;
        return S_OK;
    }

    return RequestKeyEdit(context, wParam, lParam, eaten);
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* eaten)
{
    if (eaten == nullptr)
    {
        return E_POINTER;
    }
    *eaten = wParam == VK_SHIFT && shiftPressed_ ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* eaten)
{
    if (eaten == nullptr)
    {
        return E_POINTER;
    }
    *eaten = FALSE;
    if (wParam != VK_SHIFT || !shiftPressed_)
        return S_OK;

    if (!shiftChordUsed_ && !capsLockEnglish_)
        ToggleChineseMode();
    shiftPressed_ = false;
    shiftChordUsed_ = false;
    *eaten = TRUE;
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID guid, BOOL* eaten)
{
    if (eaten == nullptr)
    {
        return E_POINTER;
    }
    *eaten = FALSE;
    if (guid == GUID_TV1PreserveKey)
    {
        if (!capsLockEnglish_)
        {
            ToggleChineseMode();
        }
        *eaten = TRUE;
    }
    else if (guid == GUID_TV1SchemeKey)
    {
        if (chinese_)
        {
            ToggleScheme();
        }
        *eaten = TRUE;
    }
    else if (guid == GUID_TV1PunctuationKey)
    {
        if (chinese_)
        {
            TogglePunctuation();
        }
        *eaten = TRUE;
    }
    return S_OK;
}

HRESULT TextService::RequestKeyEdit(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten)
{
    EditSession* session = new (std::nothrow) EditSession(this, context, wParam, lParam, kActionKey, eaten);
    if (session == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT sessionResult = S_OK;
    const HRESULT hr = context->RequestEditSession(clientId_,
                                                    session,
                                                    TF_ES_SYNC | TF_ES_READWRITE,
                                                    &sessionResult);
    session->Release();
    if (FAILED(hr) || FAILED(sessionResult))
    {
        *eaten = FALSE;
        WriteDiagnostic(L"key-edit-failed");
        return FAILED(hr) ? hr : sessionResult;
    }
    return S_OK;
}

void TextService::CancelComposition(bool synchronous)
{
    if (composition_ == nullptr || compositionContext_ == nullptr)
    {
        candidateWindow_.Hide();
        return;
    }

    candidateWindow_.Hide();
    EditSession* session = new (std::nothrow) EditSession(this,
                                                           compositionContext_,
                                                           0,
                                                           0,
                                                           kActionCancel,
                                                           nullptr);
    if (session == nullptr)
    {
        return;
    }

    HRESULT sessionResult = S_OK;
    const DWORD flags = TF_ES_READWRITE | (synchronous ? TF_ES_SYNC : TF_ES_ASYNC);
    const HRESULT hr = compositionContext_->RequestEditSession(clientId_, session, flags, &sessionResult);
    session->Release();
    if (FAILED(hr) || FAILED(sessionResult))
    {
        WriteDiagnostic(L"cancel-edit-failed");
        if (synchronous)
        {
            CancelComposition(false);
        }
    }
}

HRESULT TextService::ExecuteEditSession(TfEditCookie editCookie,
                                        ITfContext* context,
                                        WPARAM wParam,
                                        LPARAM,
                                        int action,
                                        BOOL* eaten)
{
    if (action == kActionCancel)
    {
        return FinishComposition(editCookie, context, nullptr, true);
    }
    return ProcessKey(editCookie, context, wParam, eaten);
}

HRESULT TextService::ProcessKey(TfEditCookie editCookie, ITfContext* context, WPARAM wParam, BOOL* eaten)
{
    if (eaten != nullptr)
    {
        *eaten = TRUE;
    }

    if (wParam >= 'A' && wParam <= 'Z')
    {
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        {
            if (composition_ != nullptr)
            {
                const HRESULT hr = FinishComposition(editCookie, context, nullptr, true);
                if (FAILED(hr))
                    return hr;
            }
            const wchar_t text[] = {static_cast<wchar_t>(wParam), L'\0'};
            return InsertTextAtSelection(editCookie, context, text);
        }

        if (code_.size() >= 32)
        {
            return S_OK;
        }
        const wchar_t letter = static_cast<wchar_t>(wParam - 'A' + L'a');
        if (composition_ == nullptr)
        {
            return StartComposition(editCookie, context, letter);
        }
        code_.push_back(letter);
        const HRESULT hr = UpdateComposition(editCookie, context);
        if (SUCCEEDED(hr))
        {
            RefreshCandidates(editCookie, context);
        }
        return hr;
    }

    const wchar_t fullWidthPunctuation = MapFullWidthPunctuation(wParam);
    if (fullWidthPunctuation != L'\0')
    {
        const bool shifted = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool candidatePageKey = composition_ != nullptr && !shifted &&
                                      (wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD);
        if (candidatePageKey)
        {
            if (wParam == VK_OEM_COMMA)
            {
                if (pageIndex_ > 0)
                {
                    --pageIndex_;
                    RefreshCandidates(editCookie, context);
                }
            }
            else if ((pageIndex_ + 1) * kCandidatePageSize < candidates_.size())
            {
                ++pageIndex_;
                RefreshCandidates(editCookie, context);
            }
            return S_OK;
        }

        if (composition_ != nullptr)
        {
            const HRESULT hr = FinishComposition(editCookie, context, nullptr, true);
            if (FAILED(hr))
                return hr;
        }

        const wchar_t punctuation = fullWidthPunctuation_
            ? fullWidthPunctuation
            : MapHalfWidthPunctuation(wParam);
        const wchar_t text[] = {punctuation, L'\0'};
        return InsertTextAtSelection(editCookie, context, text);
    }

    if (composition_ == nullptr)
    {
        return S_OK;
    }

    if ((wParam >= '0' && wParam <= '9') || (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9))
    {
        UINT digit = 0;
        if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
        {
            digit = static_cast<UINT>(wParam - VK_NUMPAD0);
        }
        else
        {
            digit = static_cast<UINT>(wParam - '0');
        }
        const size_t indexOnPage = digit == 0 ? 9 : static_cast<size_t>(digit - 1);
        const size_t index = pageIndex_ * kCandidatePageSize + indexOnPage;
        if (indexOnPage < kCandidatePageSize && index < candidates_.size())
        {
            return FinishComposition(editCookie, context, candidates_[index].c_str(), false);
        }
        return S_OK;
    }

    switch (wParam)
    {
    case VK_BACK:
        if (!code_.empty())
        {
            code_.pop_back();
        }
        if (code_.empty())
        {
            return FinishComposition(editCookie, context, nullptr, true);
        }
        {
            const HRESULT hr = UpdateComposition(editCookie, context);
            if (SUCCEEDED(hr))
            {
                RefreshCandidates(editCookie, context);
            }
            return hr;
        }

    case VK_ESCAPE:
        WriteDiagnostic(L"composition-cancel");
        return FinishComposition(editCookie, context, nullptr, true);

    case VK_RETURN:
        WriteDiagnostic(L"composition-commit-raw");
        return FinishComposition(editCookie, context, nullptr, false);

    case VK_SPACE:
        if (!candidates_.empty())
        {
            const size_t index = std::min(pageIndex_ * kCandidatePageSize, candidates_.size() - 1);
            WriteDiagnostic(L"composition-commit-candidate");
            return FinishComposition(editCookie, context, candidates_[index].c_str(), false);
        }
        WriteDiagnostic(L"composition-commit-raw");
        return FinishComposition(editCookie, context, nullptr, false);

    case VK_F6:
        ToggleLayout();
        ShowCandidateWindow();
        return S_OK;

    default:
        return S_OK;
    }
}

HRESULT TextService::InsertTextAtSelection(TfEditCookie editCookie,
                                            ITfContext* context,
                                            const wchar_t* text)
{
    if (context == nullptr || text == nullptr)
        return E_POINTER;

    ITfInsertAtSelection* insertAtSelection = nullptr;
    HRESULT hr = context->QueryInterface(IID_ITfInsertAtSelection,
                                         reinterpret_cast<void**>(&insertAtSelection));
    if (FAILED(hr) || insertAtSelection == nullptr)
        return FAILED(hr) ? hr : E_NOINTERFACE;

    ITfRange* range = nullptr;
    hr = insertAtSelection->InsertTextAtSelection(editCookie,
                                                  0,
                                                  text,
                                                  static_cast<LONG>(wcslen(text)),
                                                  &range);
    insertAtSelection->Release();
    if (FAILED(hr) || range == nullptr)
        return FAILED(hr) ? hr : E_FAIL;

    CollapseSelectionToEnd(editCookie, context, range);
    range->Release();
    return S_OK;
}

HRESULT TextService::StartComposition(TfEditCookie editCookie, ITfContext* context, wchar_t letter)
{
    if (composition_ != nullptr)
    {
        return E_FAIL;
    }

    ITfInsertAtSelection* insertAtSelection = nullptr;
    HRESULT hr = context->QueryInterface(IID_ITfInsertAtSelection,
                                         reinterpret_cast<void**>(&insertAtSelection));
    if (FAILED(hr))
    {
        return hr;
    }

    ITfRange* range = nullptr;
    hr = insertAtSelection->InsertTextAtSelection(editCookie, 0, &letter, 1, &range);
    insertAtSelection->Release();
    if (FAILED(hr) || range == nullptr)
    {
        return FAILED(hr) ? hr : E_FAIL;
    }

    ITfContextComposition* contextComposition = nullptr;
    hr = context->QueryInterface(IID_ITfContextComposition,
                                 reinterpret_cast<void**>(&contextComposition));
    if (FAILED(hr) || contextComposition == nullptr)
    {
        range->SetText(editCookie, 0, L"", 0);
        range->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    ITfComposition* composition = nullptr;
    hr = contextComposition->StartComposition(editCookie,
                                               range,
                                               static_cast<ITfCompositionSink*>(this),
                                               &composition);
    contextComposition->Release();
    if (FAILED(hr) || composition == nullptr)
    {
        range->SetText(editCookie, 0, L"", 0);
        range->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    composition_ = composition;
    compositionContext_ = context;
    compositionContext_->AddRef();
    code_.assign(1, letter);
    pageIndex_ = 0;
    CollapseSelectionToEnd(editCookie, context, range);
    range->Release();
    RefreshCandidates(editCookie, context);
    WriteDiagnostic(L"composition-start");
    return S_OK;
}

HRESULT TextService::UpdateComposition(TfEditCookie editCookie, ITfContext* context)
{
    if (composition_ == nullptr)
    {
        return E_FAIL;
    }

    ITfRange* range = nullptr;
    HRESULT hr = composition_->GetRange(&range);
    if (FAILED(hr) || range == nullptr)
    {
        return FAILED(hr) ? hr : E_FAIL;
    }
    hr = range->SetText(editCookie, 0, code_.data(), static_cast<LONG>(code_.size()));
    if (SUCCEEDED(hr))
    {
        CollapseSelectionToEnd(editCookie, context, range);
    }
    range->Release();
    return hr;
}

HRESULT TextService::FinishComposition(TfEditCookie editCookie,
                                       ITfContext* context,
                                       const wchar_t* replacement,
                                       bool cancel)
{
    if (composition_ == nullptr)
    {
        ClearCompositionState();
        return S_OK;
    }

    ITfComposition* composition = composition_;
    composition->AddRef();
    ITfRange* range = nullptr;
    HRESULT hr = composition->GetRange(&range);
    if (FAILED(hr) || range == nullptr)
    {
        composition->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    if (cancel)
    {
        hr = range->SetText(editCookie, 0, L"", 0);
    }
    else if (replacement != nullptr)
    {
        hr = range->SetText(editCookie, 0, replacement, static_cast<LONG>(wcslen(replacement)));
    }
    if (SUCCEEDED(hr))
    {
        CollapseSelectionToEnd(editCookie, context, range);
        hr = composition->EndComposition(editCookie);
    }
    range->Release();

    if (SUCCEEDED(hr) && composition_ == composition)
    {
        ClearCompositionState();
    }
    composition->Release();
    return hr;
}

void TextService::RefreshCandidates(TfEditCookie editCookie, ITfContext* context)
{
    candidates_ = LookupCandidates(scheme_, code_, g_module);
    const size_t pageCount = (candidates_.size() + kCandidatePageSize - 1) / kCandidatePageSize;
    if (pageCount == 0)
    {
        pageIndex_ = 0;
    }
    else
    {
        pageIndex_ = std::min(pageIndex_, pageCount - 1);
    }
    UpdateAnchor(editCookie, context);
    ShowCandidateWindow();
}

void TextService::UpdateAnchor(TfEditCookie editCookie, ITfContext* context)
{
    hasAnchor_ = false;
    if (context == nullptr || composition_ == nullptr)
    {
        return;
    }

    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || view == nullptr)
    {
        return;
    }

    ITfRange* range = nullptr;
    if (SUCCEEDED(composition_->GetRange(&range)) && range != nullptr)
    {
        RECT rect = {};
        BOOL clipped = FALSE;
        if (SUCCEEDED(view->GetTextExt(editCookie, range, &rect, &clipped)))
        {
            anchor_ = rect;
            hasAnchor_ = true;
        }
        range->Release();
    }
    view->Release();
}

void TextService::ShowCandidateWindow()
{
    if (composition_ == nullptr)
    {
        candidateWindow_.Hide();
        return;
    }

    if (!hasAnchor_)
    {
        GUITHREADINFO threadInfo = {};
        threadInfo.cbSize = sizeof(threadInfo);
        if (GetGUIThreadInfo(GetCurrentThreadId(), &threadInfo) && threadInfo.hwndCaret != nullptr)
        {
            anchor_ = threadInfo.rcCaret;
            ClientToScreen(threadInfo.hwndCaret, reinterpret_cast<POINT*>(&anchor_.left));
            ClientToScreen(threadInfo.hwndCaret, reinterpret_cast<POINT*>(&anchor_.right));
            hasAnchor_ = true;
        }
    }

    if (!hasAnchor_)
    {
        HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(monitor, &monitorInfo))
        {
            anchor_.left = monitorInfo.rcWork.left + 24;
            anchor_.top = monitorInfo.rcWork.bottom - 160;
            anchor_.right = anchor_.left;
            anchor_.bottom = anchor_.top;
            hasAnchor_ = true;
        }
    }

    const size_t begin = std::min(pageIndex_ * kCandidatePageSize, candidates_.size());
    const size_t end = std::min(begin + kCandidatePageSize, candidates_.size());
    const std::vector<std::wstring> page(candidates_.begin() + begin, candidates_.begin() + end);
    const size_t pageCount = (candidates_.size() + kCandidatePageSize - 1) / kCandidatePageSize;
    candidateWindow_.SetContent(code_, page, pageIndex_, pageCount, chinese_, scheme_);
    candidateWindow_.ShowAt(anchor_);
}

void TextService::ClearCompositionState()
{
    candidateWindow_.Hide();
    code_.clear();
    candidates_.clear();
    pageIndex_ = 0;
    hasAnchor_ = false;
    if (composition_ != nullptr)
    {
        composition_->Release();
        composition_ = nullptr;
    }
    if (compositionContext_ != nullptr)
    {
        compositionContext_->Release();
        compositionContext_ = nullptr;
    }
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie, ITfComposition* composition)
{
    if (composition == composition_)
    {
        ClearCompositionState();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*)
{
    return S_OK;
}

STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr*)
{
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*)
{
    if (composition_ != nullptr)
    {
        WriteDiagnostic(L"focus-cancel");
        CancelComposition(false);
    }
    AttachDocument(focus);
    return S_OK;
}

STDMETHODIMP TextService::OnPushContext(ITfContext*)
{
    return S_OK;
}

STDMETHODIMP TextService::OnPopContext(ITfContext* context)
{
    if (context != nullptr && context == compositionContext_)
    {
        CancelComposition(false);
    }
    return S_OK;
}

void TextService::ToggleChineseMode()
{
    CancelComposition(true);
    chinese_ = !chinese_;
    SetKeyboardOpenState();
    WriteDiagnostic(chinese_ ? L"mode-chinese" : L"mode-english");
}

void TextService::ToggleScheme()
{
    CancelComposition(true);
    scheme_ = scheme_ == InputScheme::Pinyin ? InputScheme::Wubi86 : InputScheme::Pinyin;

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\TV1TSF",
                        0,
                        nullptr,
                        0,
                        KEY_SET_VALUE,
                        nullptr,
                        &key,
                        nullptr) == ERROR_SUCCESS)
    {
        const DWORD value = static_cast<DWORD>(scheme_);
        RegSetValueExW(key, L"Scheme", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
    WriteDiagnostic(scheme_ == InputScheme::Pinyin ? L"scheme-pinyin" : L"scheme-wubi86");
    candidateWindow_.SetContent({}, {}, 0, 0, chinese_, scheme_);
    candidateWindow_.ShowAt(anchor_);
}

void TextService::ToggleLayout()
{
    vertical_ = !vertical_;
    candidateWindow_.SetVertical(vertical_);

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\TV1TSF",
                        0,
                        nullptr,
                        0,
                        KEY_SET_VALUE,
                        nullptr,
                        &key,
                        nullptr) == ERROR_SUCCESS)
    {
        const DWORD value = vertical_ ? 1 : 0;
        RegSetValueExW(key, L"Vertical", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
    WriteDiagnostic(vertical_ ? L"layout-vertical" : L"layout-horizontal");
}

void TextService::TogglePunctuation()
{
    fullWidthPunctuation_ = !fullWidthPunctuation_;
    candidateWindow_.SetFullWidthPunctuation(fullWidthPunctuation_);

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\TV1TSF",
                        0,
                        nullptr,
                        0,
                        KEY_SET_VALUE,
                        nullptr,
                        &key,
                        nullptr) == ERROR_SUCCESS)
    {
        const DWORD value = fullWidthPunctuation_ ? 1 : 0;
        RegSetValueExW(key, L"FullWidthPunctuation", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
    WriteDiagnostic(fullWidthPunctuation_ ? L"punctuation-fullwidth" : L"punctuation-halfwidth");
    if (composition_ == nullptr)
    {
        candidateWindow_.SetContent({}, {}, 0, 0, chinese_, scheme_);
        candidateWindow_.ShowAt(anchor_);
    }
}

void TextService::SetKeyboardOpenState()
{
    if (threadMgr_ == nullptr)
    {
        return;
    }

    ITfCompartmentMgr* compartmentMgr = nullptr;
    if (FAILED(threadMgr_->QueryInterface(IID_ITfCompartmentMgr,
                                          reinterpret_cast<void**>(&compartmentMgr))))
    {
        return;
    }

    ITfCompartment* compartment = nullptr;
    if (SUCCEEDED(compartmentMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment)) &&
        compartment != nullptr)
    {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_BOOL;
        value.boolVal = chinese_ ? VARIANT_TRUE : VARIANT_FALSE;
        compartment->SetValue(clientId_, &value);
        compartment->Release();
    }
    compartmentMgr->Release();
}
