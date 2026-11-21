#include "TV1TSF.h"
#include "resource.h"

#include <commdlg.h>
#include <cwchar>
#include <string>
#include <vector>

namespace
{
std::wstring GetCurrentModulePath()
{
    std::vector<wchar_t> buffer(260);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(g_module,
                                                buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size() - 1)
            return std::wstring(buffer.data(), length);
        if (buffer.size() >= 32768)
            return {};
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ReadFileVersion(const std::wstring& path)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0)
        return {};

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return {};

    wchar_t* value = nullptr;
    UINT valueSize = 0;
    if (VerQueryValueW(data.data(),
                       L"\\StringFileInfo\\080404b0\\FileVersion",
                       reinterpret_cast<void**>(&value),
                       &valueSize) &&
        value != nullptr && valueSize > 0)
    {
        return value;
    }

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &valueSize) &&
        fixedInfo != nullptr && valueSize >= sizeof(VS_FIXEDFILEINFO))
    {
        return std::to_wstring((fixedInfo->dwFileVersionMS >> 16) & 0xffff) + L"." +
               std::to_wstring(fixedInfo->dwFileVersionMS & 0xffff) + L"." +
               std::to_wstring((fixedInfo->dwFileVersionLS >> 16) & 0xffff) + L"." +
               std::to_wstring(fixedInfo->dwFileVersionLS & 0xffff);
    }
    return {};
}

void UpdateCandidateFontLabel(HWND dialog, const std::wstring& family, int sizeTenths)
{
    std::wstring sizeText = std::to_wstring(sizeTenths / 10);
    if (sizeTenths % 10 != 0)
        sizeText += L"." + std::to_wstring(sizeTenths % 10);
    const std::wstring label = family + L"\r\n" + sizeText + L" 磅";
    SetDlgItemTextW(dialog, IDC_CANDIDATE_FONT_VALUE, label.c_str());
}

// ChooseColor 对话框的自定义颜色板；进程内静态保存，重开设置窗口时保留最近选择。
COLORREF g_customColors[16] = {
    RGB(255, 255, 255), RGB(0, 0, 0),         RGB(28, 31, 38),   RGB(245, 246, 250),
    RGB(217, 83, 79),  RGB(235, 152, 78),     RGB(254, 195, 46), RGB(163, 196, 79),
    RGB(75, 152, 207), RGB(145, 145, 232),    RGB(240, 98, 146), RGB(105, 211, 164),
    RGB(240, 240, 240), RGB(200, 200, 204),  RGB(128, 128, 132), RGB(64, 64, 68),
};

bool ChooseCandidateColor(HWND dialog, COLORREF initial, COLORREF* chosen)
{
    CHOOSECOLORW chooseColor{};
    chooseColor.lStructSize = sizeof(chooseColor);
    chooseColor.hwndOwner = dialog;
    chooseColor.rgbResult = initial;
    chooseColor.lpCustColors = g_customColors;
    chooseColor.Flags = CC_FULLOPEN | CC_RGBINIT | CC_SOLIDCOLOR;
    if (!ChooseColorW(&chooseColor))
        return false;
    if (chosen != nullptr)
        *chosen = chooseColor.rgbResult;
    return true;
}

void DrawColorSwatch(const DRAWITEMSTRUCT* item, COLORREF color, const wchar_t* label)
{
    if (item == nullptr || item->hDC == nullptr)
        return;
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr)
    {
        FillRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
    }
    const int red = GetRValue(color);
    const int green = GetGValue(color);
    const int blue = GetBValue(color);
    const bool dark = (red * 30 + green * 59 + blue * 11) < 12800;
    SetTextColor(item->hDC, dark ? RGB(255, 255, 255) : RGB(20, 22, 28));
    SetBkMode(item->hDC, TRANSPARENT);
    wchar_t text[48] = {};
    swprintf_s(text, L"%s #%02X%02X%02X", label, red, green, blue);
    RECT rect = item->rcItem;
    DrawTextW(item->hDC, text, -1, &rect,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void SetAboutInfo(HWND dialog)
{
    const std::wstring path = GetCurrentModulePath();
    const size_t separator = path.find_last_of(L"\\/");
    const std::wstring fileName = path.empty()
        ? L"无法读取"
        : (separator == std::wstring::npos ? path : path.substr(separator + 1));
    const std::wstring version = path.empty() ? std::wstring{} : ReadFileVersion(path);

    std::wstring versionText = L"无法读取";
    if (!version.empty())
    {
        const size_t dot = version.find(L'.');
        const std::wstring major = dot == std::wstring::npos ? version : version.substr(0, dot);
        versionText = L"v" + major + L"（" + version + L"）";
    }
    SetDlgItemTextW(dialog, IDC_ABOUT_VERSION, (L"版本：" + versionText).c_str());
    SetDlgItemTextW(dialog, IDC_ABOUT_DLL, (L"当前 DLL：" + fileName).c_str());
}
}

void TextService::OpenSettingsDialog()
{
    if (settingsDialog_ && IsWindow(settingsDialog_))
    {
        SetForegroundWindow(settingsDialog_);
        return;
    }
    settingsDialog_ = nullptr;

    AddRef();
    HWND owner = GetActiveWindow();
    CancelComposition(true);
    candidateWindow_.Hide();

    DialogBoxParamW(g_module,
                    MAKEINTRESOURCEW(IDD_SETTINGS),
                    owner,
                    SettingsDialogProc,
                    reinterpret_cast<LPARAM>(this));
    settingsDialog_ = nullptr;
    Release();
}

INT_PTR CALLBACK TextService::SettingsDialogProc(HWND dialog,
                                                 UINT message,
                                                 WPARAM wParam,
                                                 LPARAM lParam)
{
    auto* self = reinterpret_cast<TextService*>(GetWindowLongPtrW(dialog, DWLP_USER));
    switch (message)
    {
    case WM_INITDIALOG:
        self = reinterpret_cast<TextService*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
        self->settingsDialog_ = dialog;
        CheckRadioButton(dialog,
                         IDC_SCHEME_PINYIN,
                         IDC_SCHEME_WUBI86,
                         self->scheme_ == InputScheme::Pinyin ? IDC_SCHEME_PINYIN : IDC_SCHEME_WUBI86);
        CheckRadioButton(dialog,
                         IDC_PUNCTUATION_FULLWIDTH,
                         IDC_PUNCTUATION_HALFWIDTH,
                         self->fullWidthPunctuation_ ? IDC_PUNCTUATION_FULLWIDTH : IDC_PUNCTUATION_HALFWIDTH);
        CheckRadioButton(dialog,
                         IDC_LAYOUT_HORIZONTAL,
                         IDC_LAYOUT_VERTICAL,
                         self->vertical_ ? IDC_LAYOUT_VERTICAL : IDC_LAYOUT_HORIZONTAL);
        UpdateCandidateFontLabel(dialog, self->candidateFontFamily_, self->candidateFontSizeTenths_);
        SetAboutInfo(dialog);
        return TRUE;

    case WM_COMMAND:
        if (self == nullptr)
            return FALSE;
        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case IDC_SCHEME_PINYIN:
                self->ApplyScheme(InputScheme::Pinyin);
                return TRUE;
            case IDC_SCHEME_WUBI86:
                self->ApplyScheme(InputScheme::Wubi86);
                return TRUE;
            case IDC_PUNCTUATION_FULLWIDTH:
                self->ApplyFullWidthPunctuation(true);
                return TRUE;
            case IDC_PUNCTUATION_HALFWIDTH:
                self->ApplyFullWidthPunctuation(false);
                return TRUE;
            case IDC_LAYOUT_HORIZONTAL:
                self->ApplyVertical(false);
                return TRUE;
            case IDC_LAYOUT_VERTICAL:
                self->ApplyVertical(true);
                return TRUE;
            case IDC_CHOOSE_CANDIDATE_FONT:
            {
                const UINT windowDpi = GetDpiForWindow(dialog);
                const int dpi = windowDpi == 0 ? 96 : static_cast<int>(windowDpi);
                LOGFONTW logFont{};
                logFont.lfHeight = -MulDiv(self->candidateFontSizeTenths_, dpi, 720);
                logFont.lfWeight = FW_NORMAL;
                logFont.lfCharSet = DEFAULT_CHARSET;
                logFont.lfQuality = CLEARTYPE_QUALITY;
                logFont.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
                wcsncpy_s(logFont.lfFaceName,
                          LF_FACESIZE,
                          self->candidateFontFamily_.c_str(),
                          _TRUNCATE);

                CHOOSEFONTW chooseFont{};
                chooseFont.lStructSize = sizeof(chooseFont);
                chooseFont.hwndOwner = dialog;
                chooseFont.lpLogFont = &logFont;
                chooseFont.iPointSize = self->candidateFontSizeTenths_;
                chooseFont.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT |
                                   CF_FORCEFONTEXIST | CF_NOSTYLESEL;
                if (ChooseFontW(&chooseFont))
                {
                    self->ApplyCandidateFont(logFont.lfFaceName, chooseFont.iPointSize);
                    UpdateCandidateFontLabel(dialog,
                                             self->candidateFontFamily_,
                                             self->candidateFontSizeTenths_);
                }
                return TRUE;
            }
            case IDC_CANDIDATE_TEXT_COLOR:
            {
                COLORREF chosen = 0;
                if (ChooseCandidateColor(dialog, self->candidateTextColor_, &chosen))
                {
                    self->ApplyCandidateColors(chosen, self->candidateBackColor_);
                    InvalidateRect(dialog, nullptr, TRUE);
                }
                return TRUE;
            }
            case IDC_CANDIDATE_BACK_COLOR:
            {
                COLORREF chosen = 0;
                if (ChooseCandidateColor(dialog, self->candidateBackColor_, &chosen))
                {
                    self->ApplyCandidateColors(self->candidateTextColor_, chosen);
                    InvalidateRect(dialog, nullptr, TRUE);
                }
                return TRUE;
            }
            default:
                break;
            }
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        break;

    case WM_DRAWITEM:
        if (self != nullptr && wParam == IDC_CANDIDATE_TEXT_COLOR_VALUE)
        {
            DrawColorSwatch(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam),
                            self->candidateTextColor_,
                            L"文字");
            return TRUE;
        }
        if (self != nullptr && wParam == IDC_CANDIDATE_BACK_COLOR_VALUE)
        {
            DrawColorSwatch(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam),
                            self->candidateBackColor_,
                            L"背景");
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    case WM_DESTROY:
        if (self != nullptr && self->settingsDialog_ == dialog)
            self->settingsDialog_ = nullptr;
        return TRUE;
    }
    return FALSE;
}
