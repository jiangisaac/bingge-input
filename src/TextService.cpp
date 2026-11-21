#include "TV1TSF.h"
#include "CandidateListUIElement.h"

#include <algorithm>
#include <cwchar>
#include <new>

namespace
{
constexpr int kActionKey = 0;
constexpr int kActionCancel = 1;

wchar_t MapFullWidthPunctuation(WPARAM wParam);

#ifdef TV1TSF_SEARCH_DIAGNOSTICS
const wchar_t* SearchHostName() noexcept
{
    static wchar_t processPath[MAX_PATH] = {};
    static bool initialized = false;
    if (!initialized)
    {
        GetModuleFileNameW(nullptr, processPath, MAX_PATH);
        initialized = true;
    }

    const wchar_t* baseName = wcsrchr(processPath, L'\\');
    baseName = baseName == nullptr ? processPath : baseName + 1;
    if (_wcsicmp(baseName, L"SearchHost.exe") == 0 ||
        _wcsicmp(baseName, L"SearchUI.exe") == 0 ||
        _wcsicmp(baseName, L"SearchApp.exe") == 0)
    {
        return baseName;
    }
    return L"other";
}

const wchar_t* SearchKeyClass(WPARAM wParam) noexcept
{
    if (wParam >= 'A' && wParam <= 'Z') return L"letter";
    if ((wParam >= '0' && wParam <= '9') ||
        (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)) return L"digit";
    if (wParam == VK_SHIFT) return L"shift";
    if (wParam == VK_CAPITAL) return L"capslock";
    if (wParam == VK_BACK || wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE)
        return L"editing";
    if (wParam == VK_F6 || wParam == VK_F8) return L"function";
    if (MapFullWidthPunctuation(wParam) != L'\0') return L"punctuation";
    return L"other";
}

void SearchDiag(const wchar_t* event, HRESULT hr = S_OK) noexcept
{
    wchar_t message[256] = {};
    swprintf_s(message,
               L"search host=%s event=%s hr=0x%08lX",
               SearchHostName(),
               event,
               static_cast<unsigned long>(hr));
    WriteDiagnostic(message);
}

void SearchEditDiag(const wchar_t* event, HRESULT hr, HRESULT sessionResult) noexcept
{
    wchar_t message[256] = {};
    swprintf_s(message,
               L"search host=%s event=%s hr=0x%08lX session=0x%08lX",
               SearchHostName(),
               event,
               static_cast<unsigned long>(hr),
               static_cast<unsigned long>(sessionResult));
    WriteDiagnostic(message);
}

void SearchKeyDiag(const wchar_t* event,
                   WPARAM wParam,
                   int eaten,
                   bool chinese,
                   bool capsLock,
                   bool composing) noexcept
{
    wchar_t message[256] = {};
    swprintf_s(message,
               L"search host=%s event=%s key=%s eaten=%d chinese=%d caps=%d composing=%d",
               SearchHostName(),
               event,
               SearchKeyClass(wParam),
               eaten,
               chinese ? 1 : 0,
               capsLock ? 1 : 0,
               composing ? 1 : 0);
    WriteDiagnostic(message);
}

void SearchCandidateDiag(const wchar_t* event, const RECT& anchor, bool visible) noexcept
{
    wchar_t message[256] = {};
    swprintf_s(message,
               L"search host=%s event=%s rect=%ld,%ld,%ld,%ld visible=%d",
               SearchHostName(),
               event,
               anchor.left,
               anchor.top,
               anchor.right,
               anchor.bottom,
               visible ? 1 : 0);
    WriteDiagnostic(message);
}
#define TV1_SEARCH_DIAG(...) SearchDiag(__VA_ARGS__)
#define TV1_SEARCH_CANDIDATE_DIAG(...) SearchCandidateDiag(__VA_ARGS__)
#define TV1_SEARCH_EDIT_DIAG(...) SearchEditDiag(__VA_ARGS__)
#define TV1_SEARCH_KEY_DIAG(...) SearchKeyDiag(__VA_ARGS__)
#else
#define TV1_SEARCH_DIAG(...) ((void)0)
#define TV1_SEARCH_CANDIDATE_DIAG(...) ((void)0)
#define TV1_SEARCH_EDIT_DIAG(...) ((void)0)
#define TV1_SEARCH_KEY_DIAG(...) ((void)0)
#endif

void CandidateUIDiag(const wchar_t* stage, HRESULT hr, int displayState) noexcept
{
    wchar_t message[192] = {};
    swprintf_s(message,
               L"candidate-ui stage=%s hr=0x%08lX display=%d",
               stage,
               static_cast<unsigned long>(hr),
               displayState);
    WriteDiagnostic(message);
}

void WriteDwordSetting(const wchar_t* name, DWORD value) noexcept
{
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
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
}

void WriteStringSetting(const wchar_t* name, const std::wstring& value) noexcept
{
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
        RegSetValueExW(key,
                       name,
                       0,
                       REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

bool IsValidCandidateFont(const std::wstring& family, int sizeTenths) noexcept
{
    return !family.empty() && family.size() < LF_FACESIZE &&
           sizeTenths >= 10 && sizeTenths <= 7200;
}

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
    // 中文输入法惯例：全角标点模式下反斜杠输出顿号。
    case VK_OEM_5: return L'、';
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
    TV1_SEARCH_DIAG(L"service-construct");
}

TextService::~TextService()
{
    EndCandidateUIElement();
    if (uiElementMgr_ != nullptr)
    {
        uiElementMgr_->Release();
        uiElementMgr_ = nullptr;
    }
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
    TV1_SEARCH_DIAG(L"service-query-interface");
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
    TV1_SEARCH_DIAG(L"activate-begin");

    HRESULT hr = threadMgr_->QueryInterface(IID_ITfUIElementMgr,
                                            reinterpret_cast<void**>(&uiElementMgr_));
    CandidateUIDiag(L"manager-query", hr, uiElementMgr_ == nullptr ? -1 : 0);

    ITfSource* source = nullptr;
    hr = threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source));
    TV1_SEARCH_DIAG(L"thread-source-query", hr);
    if (SUCCEEDED(hr) && source != nullptr)
    {
        hr = source->AdviseSink(IID_ITfThreadMgrEventSink,
                                static_cast<ITfThreadMgrEventSink*>(this),
                                &threadSinkCookie_);
        TV1_SEARCH_DIAG(L"thread-sink-advise", hr);
        source->Release();
    }

    ITfKeystrokeMgr* keystrokeMgr = nullptr;
    hr = threadMgr_->QueryInterface(IID_ITfKeystrokeMgr,
                                    reinterpret_cast<void**>(&keystrokeMgr));
    TV1_SEARCH_DIAG(L"keystroke-query", hr);
    if (FAILED(hr) || keystrokeMgr == nullptr)
    {
        WriteDiagnostic(L"keystroke-mgr-unavailable");
        Deactivate();
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    hr = keystrokeMgr->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE);
    TV1_SEARCH_DIAG(L"key-sink-advise", hr);
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
    hr = keystrokeMgr->PreserveKey(clientId_,
                                   GUID_TV1PreserveKey,
                                   &preservedKey,
                                   L"Ctrl+Space 切换中英文",
                                   static_cast<ULONG>(wcslen(L"Ctrl+Space 切换中英文")));
    TV1_SEARCH_DIAG(L"preserve-mode-key", hr);

    TF_PRESERVEDKEY schemeKey = {};
    schemeKey.uVKey = VK_F8;
    schemeKey.uModifiers = 0;
    hr = keystrokeMgr->PreserveKey(clientId_,
                                   GUID_TV1SchemeKey,
                                   &schemeKey,
                                   L"F8 切换拼音/五笔86",
                                   static_cast<ULONG>(wcslen(L"F8 切换拼音/五笔86")));
    TV1_SEARCH_DIAG(L"preserve-scheme-key", hr);
    WriteDiagnostic(FAILED(hr) ? L"scheme-key-preserve-failed" : L"scheme-key-ready");

    TF_PRESERVEDKEY punctuationKey = {};
    punctuationKey.uVKey = VK_OEM_PERIOD;
    punctuationKey.uModifiers = TF_MOD_CONTROL;
    hr = keystrokeMgr->PreserveKey(clientId_,
                                   GUID_TV1PunctuationKey,
                                   &punctuationKey,
                                   L"Ctrl+. 切换全角/半角标点",
                                   static_cast<ULONG>(wcslen(L"Ctrl+. 切换全角/半角标点")));
    TV1_SEARCH_DIAG(L"preserve-punctuation-key", hr);
    WriteDiagnostic(FAILED(hr) ? L"punctuation-key-preserve-failed" : L"punctuation-key-ready");
    keystrokeMgr->Release();
    WriteDiagnostic(L"key-sink-ready");

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

    candidateFontFamily_ = kDefaultCandidateFontFace;
    candidateFontSizeTenths_ = kDefaultCandidateFontSizeTenths;
    wchar_t fontFace[LF_FACESIZE] = {};
    DWORD fontFaceSize = sizeof(fontFace);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\TV1TSF",
                     L"CandidateFontFace",
                     RRF_RT_REG_SZ,
                     nullptr,
                     fontFace,
                     &fontFaceSize) == ERROR_SUCCESS)
    {
        fontFace[LF_FACESIZE - 1] = L'\0';
        const std::wstring family(fontFace);
        if (IsValidCandidateFont(family, kDefaultCandidateFontSizeTenths))
            candidateFontFamily_ = family;
    }

    DWORD fontSizeTenths = kDefaultCandidateFontSizeTenths;
    size = sizeof(fontSizeTenths);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\TV1TSF",
                     L"CandidateFontSizeTenths",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &fontSizeTenths,
                     &size) == ERROR_SUCCESS &&
        fontSizeTenths >= 10 && fontSizeTenths <= 7200)
    {
        candidateFontSizeTenths_ = static_cast<int>(fontSizeTenths);
    }

    candidateTextColor_ = kDefaultCandidateTextColor;
    DWORD textColor = kDefaultCandidateTextColor;
    size = sizeof(textColor);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\TV1TSF",
                     L"CandidateTextColor",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &textColor,
                     &size) == ERROR_SUCCESS)
    {
        candidateTextColor_ = static_cast<COLORREF>(textColor);
    }

    candidateBackColor_ = kDefaultCandidateBackColor;
    DWORD backColor = kDefaultCandidateBackColor;
    size = sizeof(backColor);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\TV1TSF",
                     L"CandidateBackColor",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &backColor,
                     &size) == ERROR_SUCCESS)
    {
        candidateBackColor_ = static_cast<COLORREF>(backColor);
    }

    candidateWindow_.SetFont(candidateFontFamily_, candidateFontSizeTenths_);
    candidateWindow_.SetColors(candidateTextColor_, candidateBackColor_);
    candidateWindow_.SetSettingsHandler([this]() { OpenSettingsDialog(); });
    const bool candidateCreated = candidateWindow_.Create();
    (void)candidateCreated;
    TV1_SEARCH_DIAG(L"candidate-create-result",
                    candidateCreated ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

    capsLockEnglish_ = (GetKeyState(VK_CAPITAL) & 1) != 0;
    if (capsLockEnglish_)
    {
        modeBeforeCapsLock_ = inputMode_;
        inputMode_ = InputMode::English;
    }

    ITfDocumentMgr* focus = nullptr;
    hr = threadMgr_->GetFocus(&focus);
    TV1_SEARCH_DIAG(L"initial-focus-query", hr);
    if (SUCCEEDED(hr) && focus != nullptr)
    {
        TV1_SEARCH_DIAG(L"initial-focus-present");
        AttachDocument(focus);
        focus->Release();
    }
    else
    {
        TV1_SEARCH_DIAG(L"initial-focus-none");
    }

    SetKeyboardOpenState();
    WriteDiagnostic(L"activate");
    return S_OK;
}

STDMETHODIMP TextService::Deactivate()
{
    TV1_SEARCH_DIAG(L"deactivate");
    if (!activated_)
    {
        return S_OK;
    }

    CancelComposition(true);
    EndCandidateUIElement();
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
    if (uiElementMgr_ != nullptr)
    {
        uiElementMgr_->Release();
        uiElementMgr_ = nullptr;
    }
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
        TV1_SEARCH_DIAG(L"document-detached");
        return;
    }

    documentMgr_ = documentMgr;
    documentMgr_->AddRef();
    const HRESULT hr = documentMgr_->GetBase(&context_);
    TV1_SEARCH_DIAG(L"document-get-base", hr);
    if (FAILED(hr) || context_ == nullptr)
    {
        TV1_SEARCH_DIAG(L"document-context-unavailable", FAILED(hr) ? hr : E_NOINTERFACE);
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
    TV1_SEARCH_DIAG(foreground ? L"thread-focus-gained" : L"thread-focus-lost");
    if (!foreground)
    {
        EndCandidateUIElement();
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
    if (inputMode_ == InputMode::English)
        return false;
    if (wParam >= 'A' && wParam <= 'Z')
        return true;
    if (inputMode_ == InputMode::Chinese)
    {
        if (wParam == VK_F8 || MapFullWidthPunctuation(wParam) != L'\0')
            return true;
    }
    else if (MapHalfWidthPunctuation(wParam) != L'\0')
    {
        return true;
    }
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
    TV1_SEARCH_KEY_DIAG(L"test-key-down", wParam, *eaten, inputMode_ == InputMode::Chinese, capsLockEnglish_, composition_ != nullptr);
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten)
{
    if (context == nullptr || eaten == nullptr)
        return E_POINTER;
    *eaten = FALSE;
    TV1_SEARCH_KEY_DIAG(L"key-down", wParam, -1, inputMode_ == InputMode::Chinese, capsLockEnglish_, composition_ != nullptr);
    if (shiftPressed_ && wParam != VK_SHIFT)
        shiftChordUsed_ = true;
    if (!ShouldHandleKey(wParam, lParam))
    {
        TV1_SEARCH_KEY_DIAG(L"key-down-pass", wParam, *eaten, inputMode_ == InputMode::Chinese, capsLockEnglish_, composition_ != nullptr);
        return S_OK;
    }

    if (wParam == VK_CAPITAL)
    {
        shiftChordUsed_ = shiftChordUsed_ || shiftPressed_;
        if ((lParam & 0x40000000) == 0)
        {
            if (!capsLockEnglish_)
            {
                capsLockEnglish_ = true;
                // 英文联想是单次组合态：若编码中途按 CapsLock，恢复目标应为中文，
                // 否则关闭 CapsLock 后会留下"无组合串的英文联想"异常状态。
                modeBeforeCapsLock_ = inputMode_ == InputMode::EnglishSuggestion
                    ? InputMode::Chinese
                    : inputMode_;
                if (composition_ != nullptr)
                    CancelComposition(true);
                inputMode_ = InputMode::English;
                WriteDiagnostic(L"capslock-english");
            }
            else
            {
                capsLockEnglish_ = false;
                inputMode_ = modeBeforeCapsLock_;
                WriteDiagnostic(inputMode_ == InputMode::Chinese
                                    ? L"capslock-restore-chinese"
                                    : L"capslock-remain-english");
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
    TV1_SEARCH_KEY_DIAG(L"test-key-up", wParam, *eaten, inputMode_ == InputMode::Chinese, capsLockEnglish_, composition_ != nullptr);
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* eaten)
{
    if (eaten == nullptr)
    {
        return E_POINTER;
    }
    *eaten = FALSE;
    TV1_SEARCH_KEY_DIAG(L"key-up", wParam, -1, inputMode_ == InputMode::Chinese, capsLockEnglish_, composition_ != nullptr);
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
        if (inputMode_ == InputMode::Chinese)
        {
            ToggleScheme();
        }
        *eaten = TRUE;
    }
    else if (guid == GUID_TV1PunctuationKey)
    {
        if (inputMode_ == InputMode::Chinese)
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
    TV1_SEARCH_EDIT_DIAG(L"key-edit-request", hr, sessionResult);
    TV1_SEARCH_KEY_DIAG(L"key-edit-result", wParam, *eaten, inputMode_ == InputMode::Chinese, capsLockEnglish_, composition_ != nullptr);
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
    EndCandidateUIElement();
    candidateWindow_.Hide();
    if (composition_ == nullptr || compositionContext_ == nullptr)
    {
        return;
    }

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
    TV1_SEARCH_EDIT_DIAG(L"cancel-edit-request", hr, sessionResult);
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
    TV1_SEARCH_DIAG(action == kActionKey ? L"edit-session-key" : L"edit-session-cancel");
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

    const bool letterKey = wParam >= 'A' && wParam <= 'Z';
    const bool shiftedLetter = letterKey && (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (inputMode_ == InputMode::Chinese && shiftedLetter && composition_ == nullptr)
    {
        inputMode_ = InputMode::EnglishSuggestion;
        SetKeyboardOpenState();
        const HRESULT hr = StartComposition(editCookie, context, static_cast<wchar_t>(wParam));
        if (FAILED(hr))
        {
            inputMode_ = InputMode::Chinese;
            SetKeyboardOpenState();
        }
        return hr;
    }

    if (inputMode_ == InputMode::EnglishSuggestion)
        return ProcessEnglishSuggestionKey(editCookie, context, wParam, eaten);

    if (letterKey)
    {
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

HRESULT TextService::ProcessEnglishSuggestionKey(TfEditCookie editCookie,
                                                 ITfContext* context,
                                                 WPARAM wParam,
                                                 BOOL* eaten)
{
    if (eaten != nullptr)
        *eaten = TRUE;

    if (wParam >= 'A' && wParam <= 'Z')
    {
        if (code_.size() >= 128)
            return S_OK;
        const bool shifted = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const wchar_t letter = shifted
            ? static_cast<wchar_t>(wParam)
            : static_cast<wchar_t>(wParam - 'A' + L'a');
        if (composition_ == nullptr)
            return StartComposition(editCookie, context, letter);
        code_.push_back(letter);
        const HRESULT hr = UpdateComposition(editCookie, context);
        if (SUCCEEDED(hr))
            RefreshCandidates(editCookie, context);
        return hr;
    }

    const wchar_t punctuation = MapHalfWidthPunctuation(wParam);
    if (punctuation != L'\0')
    {
        const bool shifted = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (composition_ != nullptr && !shifted)
        {
            if (wParam == VK_OEM_COMMA && pageIndex_ > 0)
            {
                --pageIndex_;
                RefreshCandidates(editCookie, context);
                return S_OK;
            }
            if (wParam == VK_OEM_PERIOD &&
                (pageIndex_ + 1) * kCandidatePageSize < candidates_.size())
            {
                ++pageIndex_;
                RefreshCandidates(editCookie, context);
                return S_OK;
            }
        }

        if (composition_ != nullptr)
        {
            const std::wstring committed = code_ + punctuation;
            return FinishComposition(editCookie, context, committed.c_str(), false);
        }
        const wchar_t text[] = {punctuation, L'\0'};
        return InsertTextAtSelection(editCookie, context, text);
    }

    if (composition_ == nullptr)
        return S_OK;

    if ((wParam >= '0' && wParam <= '9') ||
        (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9))
    {
        const UINT digit = wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9
            ? static_cast<UINT>(wParam - VK_NUMPAD0)
            : static_cast<UINT>(wParam - '0');
        const size_t indexOnPage = digit == 0 ? 9 : static_cast<size_t>(digit - 1);
        const size_t index = pageIndex_ * kCandidatePageSize + indexOnPage;
        if (indexOnPage < kCandidatePageSize && index < candidates_.size())
            return FinishComposition(editCookie, context, candidates_[index].c_str(), false);

        const std::wstring committed =
            code_ + static_cast<wchar_t>(L'0' + digit);
        return FinishComposition(editCookie, context, committed.c_str(), false);
    }

    switch (wParam)
    {
    case VK_BACK:
        if (!code_.empty())
            code_.pop_back();
        if (code_.empty())
            return FinishComposition(editCookie, context, nullptr, true);
        {
            const HRESULT hr = UpdateComposition(editCookie, context);
            if (SUCCEEDED(hr))
                RefreshCandidates(editCookie, context);
            return hr;
        }

    case VK_ESCAPE:
        return FinishComposition(editCookie, context, nullptr, true);

    case VK_RETURN:
        return FinishComposition(editCookie, context, code_.c_str(), false);

    case VK_SPACE:
        if (!candidates_.empty())
        {
            const size_t index = std::min(pageIndex_ * kCandidatePageSize, candidates_.size() - 1);
            return FinishComposition(editCookie, context, candidates_[index].c_str(), false);
        }
        return FinishComposition(editCookie, context, code_.c_str(), false);

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
    TV1_SEARCH_DIAG(L"direct-insert-query", hr);
    if (FAILED(hr) || insertAtSelection == nullptr)
        return FAILED(hr) ? hr : E_NOINTERFACE;

    ITfRange* range = nullptr;
    hr = insertAtSelection->InsertTextAtSelection(editCookie,
                                                  0,
                                                  text,
                                                  static_cast<LONG>(wcslen(text)),
                                                  &range);
    insertAtSelection->Release();
    TV1_SEARCH_DIAG(L"direct-insert-result", FAILED(hr) ? hr : (range == nullptr ? E_FAIL : S_OK));
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
    TV1_SEARCH_DIAG(L"composition-insert-query", hr);
    if (FAILED(hr))
    {
        return hr;
    }

    ITfRange* range = nullptr;
    hr = insertAtSelection->InsertTextAtSelection(editCookie, 0, &letter, 1, &range);
    insertAtSelection->Release();
    TV1_SEARCH_DIAG(L"composition-insert-result", FAILED(hr) ? hr : (range == nullptr ? E_FAIL : S_OK));
    if (FAILED(hr) || range == nullptr)
    {
        return FAILED(hr) ? hr : E_FAIL;
    }

    ITfContextComposition* contextComposition = nullptr;
    hr = context->QueryInterface(IID_ITfContextComposition,
                                 reinterpret_cast<void**>(&contextComposition));
    TV1_SEARCH_DIAG(L"composition-interface-query", FAILED(hr) ? hr : (contextComposition == nullptr ? E_NOINTERFACE : S_OK));
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
    TV1_SEARCH_DIAG(L"composition-start-result", FAILED(hr) ? hr : (composition == nullptr ? E_FAIL : S_OK));
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
        hr = composition->EndComposition(editCookie);
        if (SUCCEEDED(hr))
            CollapseSelectionToEnd(editCookie, context, range);
    }
    range->Release();

    if (SUCCEEDED(hr))
    {
        if (composition_ == composition)
            ClearCompositionState();
        if (inputMode_ == InputMode::EnglishSuggestion)
        {
            inputMode_ = InputMode::Chinese;
            SetKeyboardOpenState();
        }
    }
    composition->Release();
    return hr;
}

void TextService::RefreshCandidates(TfEditCookie editCookie, ITfContext* context)
{
    candidates_ = inputMode_ == InputMode::EnglishSuggestion
        ? LookupEnglishCandidates(code_, g_module)
        : LookupCandidates(scheme_, code_, g_module);
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
    hasScreenExtAnchor_ = false;
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
        const HRESULT textExtHr = view->GetTextExt(editCookie, range, &rect, &clipped);
        TV1_SEARCH_DIAG(L"anchor-textext", textExtHr);
        if (SUCCEEDED(textExtHr))
        {
            anchor_ = rect;
            hasAnchor_ = true;
        }
        range->Release();
    }

    // GetTextExt 失败时（部分浏览器地址栏不提供组合文本坐标），
    // 记录 GetScreenExt 的编辑区矩形作为次级锚点候选，
    // 供 ShowCandidateWindow 在插入符位置也不可用时使用。
    RECT screenRect = {};
    const HRESULT screenExtHr = view->GetScreenExt(&screenRect);
    TV1_SEARCH_DIAG(L"anchor-screenext", screenExtHr);
    if (SUCCEEDED(screenExtHr) &&
        screenRect.right > screenRect.left && screenRect.bottom > screenRect.top)
    {
        screenExtAnchor_.left = screenRect.left;
        screenExtAnchor_.top = screenRect.bottom;
        screenExtAnchor_.right = screenRect.left;
        screenExtAnchor_.bottom = screenRect.bottom;
        hasScreenExtAnchor_ = true;
    }
    view->Release();
}

void TextService::ShowCandidateWindow()
{
    if (composition_ == nullptr)
    {
        EndCandidateUIElement();
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

    if (!hasAnchor_ && hasScreenExtAnchor_)
    {
        anchor_ = screenExtAnchor_;
        hasAnchor_ = true;
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

    if (candidates_.empty())
    {
        EndCandidateUIElement();
    }
    else if (candidateUIElementId_ != TF_INVALID_UIELEMENTID)
    {
        HRESULT hr = candidateUIElement_->SetContent(candidates_, pageIndex_);
        if (SUCCEEDED(hr))
            hr = uiElementMgr_->UpdateUIElement(candidateUIElementId_);
        CandidateUIDiag(L"update", hr, SUCCEEDED(hr) ? 1 : 0);
        if (SUCCEEDED(hr))
        {
            candidateWindow_.Hide();
            return;
        }
        EndCandidateUIElement();
    }
    else if (uiElementMgr_ != nullptr)
    {
        CandidateListUIElement* element = nullptr;
        HRESULT hr = CandidateListUIElement::Create(documentMgr_,
                                                    candidates_,
                                                    pageIndex_,
                                                    &element);
        BOOL showOwnUI = TRUE;
        DWORD elementId = TF_INVALID_UIELEMENTID;
        if (SUCCEEDED(hr))
            hr = uiElementMgr_->BeginUIElement(element, &showOwnUI, &elementId);

        const bool hostDisplays = SUCCEEDED(hr) && !showOwnUI &&
                                  elementId != TF_INVALID_UIELEMENTID;
        CandidateUIDiag(L"begin", hr, hostDisplays ? 1 : 0);
        if (hostDisplays)
        {
            candidateUIElement_ = element;
            candidateUIElementId_ = elementId;
            candidateWindow_.Hide();
            return;
        }

        if (elementId != TF_INVALID_UIELEMENTID)
        {
            const HRESULT endHr = uiElementMgr_->EndUIElement(elementId);
            CandidateUIDiag(L"begin-cleanup", endHr, 0);
        }
        if (element != nullptr)
            element->Release();
    }

    const size_t begin = std::min(pageIndex_ * kCandidatePageSize, candidates_.size());
    const size_t end = std::min(begin + kCandidatePageSize, candidates_.size());
    const std::vector<std::wstring> page(candidates_.begin() + begin, candidates_.begin() + end);
    const size_t pageCount = (candidates_.size() + kCandidatePageSize - 1) / kCandidatePageSize;
    candidateWindow_.SetContent(code_, page, pageIndex_, pageCount, inputMode_, scheme_);
    candidateWindow_.ShowAt(anchor_);
    TV1_SEARCH_CANDIDATE_DIAG(L"candidate-show", anchor_, candidateWindow_.IsVisible());
}

void TextService::EndCandidateUIElement()
{
    CandidateListUIElement* element = candidateUIElement_;
    const DWORD elementId = candidateUIElementId_;
    candidateUIElement_ = nullptr;
    candidateUIElementId_ = TF_INVALID_UIELEMENTID;

    if (element != nullptr)
        element->Show(FALSE);
    if (uiElementMgr_ != nullptr && elementId != TF_INVALID_UIELEMENTID)
    {
        const HRESULT hr = uiElementMgr_->EndUIElement(elementId);
        CandidateUIDiag(L"end", hr, 0);
    }
    if (element != nullptr)
        element->Release();
}

void TextService::ClearCompositionState()
{
    EndCandidateUIElement();
    candidateWindow_.Hide();
    code_.clear();
    candidates_.clear();
    pageIndex_ = 0;
    hasAnchor_ = false;
    hasScreenExtAnchor_ = false;
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
    TV1_SEARCH_DIAG(composition == composition_ ? L"composition-terminated-current" : L"composition-terminated-other");
    if (composition == composition_)
    {
        ClearCompositionState();
        if (inputMode_ == InputMode::EnglishSuggestion)
        {
            inputMode_ = InputMode::Chinese;
            SetKeyboardOpenState();
        }
    }
    return S_OK;
}

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*)
{
    TV1_SEARCH_DIAG(L"document-init");
    return S_OK;
}

STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr*)
{
    TV1_SEARCH_DIAG(L"document-uninit");
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*)
{
    TV1_SEARCH_DIAG(focus == nullptr ? L"document-focus-none" : L"document-focus-present");
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
    TV1_SEARCH_DIAG(L"context-push");
    return S_OK;
}

STDMETHODIMP TextService::OnPopContext(ITfContext* context)
{
    TV1_SEARCH_DIAG(L"context-pop");
    if (context != nullptr && context == compositionContext_)
    {
        TV1_SEARCH_DIAG(L"context-pop-current-composition");
        CancelComposition(false);
    }
    return S_OK;
}

void TextService::ToggleChineseMode()
{
    const bool switchToChinese = inputMode_ != InputMode::Chinese;
    CancelComposition(true);
    inputMode_ = switchToChinese ? InputMode::Chinese : InputMode::English;
    SetKeyboardOpenState();
    WriteDiagnostic(inputMode_ == InputMode::Chinese ? L"mode-chinese" : L"mode-english");
}

void TextService::ToggleScheme()
{
    CancelComposition(true);
    ApplyScheme(scheme_ == InputScheme::Pinyin ? InputScheme::Wubi86 : InputScheme::Pinyin);
    candidateWindow_.SetContent({}, {}, 0, 0, inputMode_, scheme_);
    candidateWindow_.ShowAt(anchor_);
}

void TextService::ToggleLayout()
{
    ApplyVertical(!vertical_);
}

void TextService::TogglePunctuation()
{
    ApplyFullWidthPunctuation(!fullWidthPunctuation_);
    if (composition_ == nullptr)
    {
        candidateWindow_.SetContent({}, {}, 0, 0, inputMode_, scheme_);
        candidateWindow_.ShowAt(anchor_);
    }
}

void TextService::ApplyScheme(InputScheme scheme)
{
    if (scheme_ == scheme)
        return;

    scheme_ = scheme;
    WriteDwordSetting(L"Scheme", static_cast<DWORD>(scheme_));
    WriteDiagnostic(scheme_ == InputScheme::Pinyin ? L"scheme-pinyin" : L"scheme-wubi86");
}

void TextService::ApplyVertical(bool vertical)
{
    if (vertical_ == vertical)
        return;

    vertical_ = vertical;
    candidateWindow_.SetVertical(vertical_);
    WriteDwordSetting(L"Vertical", vertical_ ? 1 : 0);
    WriteDiagnostic(vertical_ ? L"layout-vertical" : L"layout-horizontal");
}

void TextService::ApplyFullWidthPunctuation(bool fullWidth)
{
    if (fullWidthPunctuation_ == fullWidth)
        return;

    fullWidthPunctuation_ = fullWidth;
    candidateWindow_.SetFullWidthPunctuation(fullWidthPunctuation_);
    WriteDwordSetting(L"FullWidthPunctuation", fullWidthPunctuation_ ? 1 : 0);
    WriteDiagnostic(fullWidthPunctuation_ ? L"punctuation-fullwidth" : L"punctuation-halfwidth");
}

void TextService::ApplyCandidateFont(const std::wstring& family, int sizeTenths)
{
    if (!IsValidCandidateFont(family, sizeTenths) ||
        (candidateFontFamily_ == family && candidateFontSizeTenths_ == sizeTenths))
    {
        return;
    }

    candidateFontFamily_ = family;
    candidateFontSizeTenths_ = sizeTenths;
    candidateWindow_.SetFont(candidateFontFamily_, candidateFontSizeTenths_);
    WriteStringSetting(L"CandidateFontFace", candidateFontFamily_);
    WriteDwordSetting(L"CandidateFontSizeTenths", static_cast<DWORD>(candidateFontSizeTenths_));
    WriteDiagnostic(L"candidate-font-updated");
}

void TextService::ApplyCandidateColors(COLORREF textColor, COLORREF backColor)
{
    if (candidateTextColor_ == textColor && candidateBackColor_ == backColor)
        return;

    candidateTextColor_ = textColor;
    candidateBackColor_ = backColor;
    candidateWindow_.SetColors(textColor, backColor);
    WriteDwordSetting(L"CandidateTextColor", static_cast<DWORD>(textColor));
    WriteDwordSetting(L"CandidateBackColor", static_cast<DWORD>(backColor));
    WriteDiagnostic(L"candidate-color-updated");
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
        value.boolVal = inputMode_ == InputMode::Chinese ? VARIANT_TRUE : VARIANT_FALSE;
        compartment->SetValue(clientId_, &value);
        compartment->Release();
    }
    compartmentMgr->Release();
}
