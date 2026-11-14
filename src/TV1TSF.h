#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <msctf.h>
#include <string>
#include <vector>

extern HMODULE g_module;
extern LONG g_serverLocks;

EXTERN_C const CLSID CLSID_TV1TextService;
EXTERN_C const GUID GUID_TV1Profile;
EXTERN_C const GUID GUID_TV1PreserveKey;
EXTERN_C const GUID GUID_TV1SchemeKey;
EXTERN_C const GUID GUID_TV1PunctuationKey;

constexpr UINT kCandidatePageSize = 5;

enum class InputScheme
{
    Pinyin,
    Wubi86,
};

std::vector<std::wstring> LookupCandidates(InputScheme scheme,
                                           const std::wstring& code,
                                           HMODULE resourceModule);
void WriteDiagnostic(const wchar_t* eventName) noexcept;

class CandidateWindow
{
public:
    CandidateWindow() = default;
    ~CandidateWindow();

    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;

    bool Create();
    void Destroy();
    void SetContent(const std::wstring& code,
                    const std::vector<std::wstring>& pageCandidates,
                    size_t pageIndex,
                    size_t pageCount,
                    bool chinese,
                    InputScheme scheme);
    void SetVertical(bool vertical);
    void SetFullWidthPunctuation(bool fullWidth);
    bool IsVertical() const noexcept { return vertical_; }
    void ShowAt(const RECT& anchor);
    void Hide();
    bool IsVisible() const noexcept;

private:
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT smallFont_ = nullptr;
    std::wstring code_;
    std::vector<std::wstring> candidates_;
    size_t pageIndex_ = 0;
    size_t pageCount_ = 0;
    bool chinese_ = true;
    InputScheme scheme_ = InputScheme::Pinyin;
    bool vertical_ = false;
    bool fullWidthPunctuation_ = true;
    int dpi_ = 96;

    int Scale(int value) const noexcept { return MulDiv(value, dpi_, 96); }
    void RecreateFonts();
    void Paint();
    void SizeToContent(const RECT& anchor);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};

class TextService : public ITfTextInputProcessorEx,
                    public ITfKeyEventSink,
                    public ITfThreadMgrEventSink,
                    public ITfCompositionSink
{
public:
    TextService();
    virtual ~TextService();

    TextService(const TextService&) = delete;
    TextService& operator=(const TextService&) = delete;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* threadMgr, TfClientId clientId) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL foreground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid, BOOL* eaten) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* documentMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* documentMgr) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* previousFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* context) override;
    STDMETHODIMP OnPopContext(ITfContext* context) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie editCookie, ITfComposition* composition) override;

    TfClientId ClientId() const noexcept { return clientId_; }
    HRESULT ExecuteEditSession(TfEditCookie editCookie,
                               ITfContext* context,
                               WPARAM wParam,
                               LPARAM lParam,
                               int action,
                               BOOL* eaten);
    void CancelComposition(bool synchronous);

private:
    ULONG refCount_ = 1;
    ITfThreadMgr* threadMgr_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    ITfDocumentMgr* documentMgr_ = nullptr;
    ITfContext* context_ = nullptr;
    ITfContext* compositionContext_ = nullptr;
    DWORD threadSinkCookie_ = TF_INVALID_COOKIE;
    ITfComposition* composition_ = nullptr;
    CandidateWindow candidateWindow_;
    std::wstring code_;
    std::vector<std::wstring> candidates_;
    size_t pageIndex_ = 0;
    RECT anchor_{};
    bool hasAnchor_ = false;
    bool chinese_ = true;
    InputScheme scheme_ = InputScheme::Pinyin;
    bool vertical_ = false;
    bool fullWidthPunctuation_ = true;
    bool capsLockEnglish_ = false;
    bool chineseBeforeCapsLock_ = true;
    bool shiftPressed_ = false;
    bool shiftChordUsed_ = false;
    bool activated_ = false;
    bool keyEventSinkAdvised_ = false;

    HRESULT ActivateInternal(ITfThreadMgr* threadMgr, TfClientId clientId);
    void AttachDocument(ITfDocumentMgr* documentMgr);
    void DetachDocument();
    bool ShouldHandleKey(WPARAM wParam, LPARAM lParam) const;
    HRESULT RequestKeyEdit(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten);
    HRESULT StartComposition(TfEditCookie editCookie, ITfContext* context, wchar_t letter);
    HRESULT UpdateComposition(TfEditCookie editCookie, ITfContext* context);
    HRESULT FinishComposition(TfEditCookie editCookie,
                              ITfContext* context,
                              const wchar_t* replacement,
                              bool cancel);
    HRESULT InsertTextAtSelection(TfEditCookie editCookie, ITfContext* context, const wchar_t* text);
    HRESULT ProcessKey(TfEditCookie editCookie, ITfContext* context, WPARAM wParam, BOOL* eaten);
    void RefreshCandidates(TfEditCookie editCookie, ITfContext* context);
    void UpdateAnchor(TfEditCookie editCookie, ITfContext* context);
    void ShowCandidateWindow();
    void ClearCompositionState();
    void ToggleChineseMode();
    void ToggleScheme();
    void ToggleLayout();
    void TogglePunctuation();
    void SetKeyboardOpenState();
};

class EditSession : public ITfEditSession
{
public:
    EditSession(TextService* service, ITfContext* context, WPARAM wParam, LPARAM lParam, int action, BOOL* eaten);

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP DoEditSession(TfEditCookie editCookie) override;

private:
    virtual ~EditSession();
    ULONG refCount_ = 1;
    TextService* service_;
    ITfContext* context_;
    WPARAM wParam_;
    LPARAM lParam_;
    int action_;
    BOOL* eaten_;
};

HRESULT RegisterServer();
HRESULT UnregisterServer();
