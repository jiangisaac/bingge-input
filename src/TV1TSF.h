#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <msctf.h>
#include <functional>
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
constexpr wchar_t kDefaultCandidateFontFace[] = L"Microsoft YaHei UI";
constexpr int kDefaultCandidateFontSizeTenths = 135;
constexpr COLORREF kDefaultCandidateTextColor = RGB(245, 246, 250);
constexpr COLORREF kDefaultCandidateBackColor = RGB(28, 31, 38);

enum class InputScheme
{
    Pinyin,
    Wubi86,
};

enum class InputMode
{
    Chinese,
    English,
    EnglishSuggestion,
};

std::vector<std::wstring> LookupCandidates(InputScheme scheme,
                                           const std::wstring& code,
                                           HMODULE resourceModule);
std::vector<std::wstring> LookupEnglishCandidates(const std::wstring& code,
                                                  HMODULE resourceModule);
#ifdef TV1TSF_DICTIONARY_TESTING
std::vector<std::wstring> LookupCandidatesFromMemoryForTest(InputScheme scheme,
                                                            const std::wstring& code,
                                                            const void* pinyinData,
                                                            size_t pinyinSize,
                                                            const void* wubiData,
                                                            size_t wubiSize);
std::vector<std::wstring> LookupEnglishCandidatesFromMemoryForTest(const std::wstring& code,
                                                                   const void* englishData,
                                                                   size_t englishSize);
#endif
void WriteDiagnostic(const wchar_t* eventName) noexcept;

class CandidateWindow
{
public:
    CandidateWindow() = default;
    ~CandidateWindow();

    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;

    bool Create();
    void SetSettingsHandler(std::function<void()> handler);
    void Destroy();
    void SetContent(const std::wstring& code,
                    const std::vector<std::wstring>& pageCandidates,
                    size_t pageIndex,
                    size_t pageCount,
                    InputMode mode,
                    InputScheme scheme);
    void SetVertical(bool vertical);
    void SetFullWidthPunctuation(bool fullWidth);
    void SetFont(const std::wstring& family, int sizeTenths);
    void SetColors(COLORREF textColor, COLORREF backColor);
    bool IsVertical() const noexcept { return vertical_; }
    void ShowAt(const RECT& anchor);
    void Hide();
    bool IsVisible() const noexcept;

private:
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT smallFont_ = nullptr;
    std::function<void()> settingsHandler_;
    bool settingsButtonPressed_ = false;
    std::wstring code_;
    std::vector<std::wstring> candidates_;
    size_t pageIndex_ = 0;
    size_t pageCount_ = 0;
    InputMode inputMode_ = InputMode::Chinese;
    InputScheme scheme_ = InputScheme::Pinyin;
    bool vertical_ = false;
    bool fullWidthPunctuation_ = true;
    std::wstring fontFamily_ = kDefaultCandidateFontFace;
    int fontSizeTenths_ = kDefaultCandidateFontSizeTenths;
    COLORREF textColor_ = kDefaultCandidateTextColor;
    COLORREF backColor_ = kDefaultCandidateBackColor;
    int dpi_ = 96;

    int Scale(int value) const noexcept { return MulDiv(value, dpi_, 96); }
    void RecreateFonts();
    void Paint();
    bool IsSettingsButtonAt(POINT point);
    void SizeToContent(const RECT& anchor);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};

class CandidateListUIElement;

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
    ITfUIElementMgr* uiElementMgr_ = nullptr;
    CandidateListUIElement* candidateUIElement_ = nullptr;
    DWORD candidateUIElementId_ = TF_INVALID_UIELEMENTID;
    CandidateWindow candidateWindow_;
    std::wstring code_;
    std::vector<std::wstring> candidates_;
    size_t pageIndex_ = 0;
    RECT anchor_{};
    bool hasAnchor_ = false;
    RECT screenExtAnchor_{};
    bool hasScreenExtAnchor_ = false;
    InputMode inputMode_ = InputMode::Chinese;
    InputScheme scheme_ = InputScheme::Pinyin;
    bool vertical_ = false;
    bool fullWidthPunctuation_ = true;
    std::wstring candidateFontFamily_ = kDefaultCandidateFontFace;
    int candidateFontSizeTenths_ = kDefaultCandidateFontSizeTenths;
    COLORREF candidateTextColor_ = kDefaultCandidateTextColor;
    COLORREF candidateBackColor_ = kDefaultCandidateBackColor;
    bool capsLockEnglish_ = false;
    InputMode modeBeforeCapsLock_ = InputMode::Chinese;
    bool shiftPressed_ = false;
    bool shiftChordUsed_ = false;
    bool activated_ = false;
    bool keyEventSinkAdvised_ = false;
    HWND settingsDialog_ = nullptr;

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
    HRESULT ProcessEnglishSuggestionKey(TfEditCookie editCookie,
                                        ITfContext* context,
                                        WPARAM wParam,
                                        BOOL* eaten);
    void RefreshCandidates(TfEditCookie editCookie, ITfContext* context);
    void UpdateAnchor(TfEditCookie editCookie, ITfContext* context);
    void ShowCandidateWindow();
    void EndCandidateUIElement();
    void ClearCompositionState();
    void ToggleChineseMode();
    void ToggleScheme();
    void ToggleLayout();
    void TogglePunctuation();
    void ApplyScheme(InputScheme scheme);
    void ApplyVertical(bool vertical);
    void ApplyFullWidthPunctuation(bool fullWidth);
    void ApplyCandidateFont(const std::wstring& family, int sizeTenths);
    void ApplyCandidateColors(COLORREF textColor, COLORREF backColor);
    void OpenSettingsDialog();
    static INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
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
