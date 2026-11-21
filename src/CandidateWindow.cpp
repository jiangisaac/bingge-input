#include "TV1TSF.h"

#include <algorithm>
#include <utility>
#include <windowsx.h>

namespace
{
constexpr wchar_t kCandidateWindowClass[] = L"TV1TSF.CandidateWindow";
constexpr wchar_t kOperationHint[] = L"1-5 选词  , . 翻页  F8 方案  Ctrl+. 标点  F6 横/竖排";
constexpr int kAnchorGap = 4;
constexpr UINT kShowSettingsMessage = WM_APP + 1;

struct LayoutInfo
{
    int margin = 0;
    int gap = 0;
    int separator = 1;
    int headerHeight = 0;
    int itemHeight = 0;
    int footerHeight = 0;
    int candidatesWidth = 0;
    int candidatesHeight = 0;
    int width = 0;
    int height = 0;
    std::wstring codeText;
    std::wstring statusText;
    std::wstring pageText;
    std::vector<std::wstring> items;
    std::vector<int> itemWidths;
    int codeWidth = 0;
    int statusWidth = 0;
    int pageWidth = 0;
    int settingsWidth = 0;
    int settingsInset = 0;
    int hintWidth = 0;
};

int ScaleDpi(int value, int dpi) noexcept
{
    return MulDiv(value, dpi, 96);
}

int TextWidth(HDC dc, const std::wstring& text) noexcept
{
    if (text.empty())
        return 0;

    SIZE size{};
    if (!GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size))
        return 0;
    return size.cx;
}

LayoutInfo BuildLayout(HDC dc,
                       const std::wstring& code,
                       const std::vector<std::wstring>& candidates,
                       size_t pageIndex,
                       size_t pageCount,
                       InputMode mode,
                       InputScheme scheme,
                       bool vertical,
                       bool fullWidthPunctuation,
                       int dpi,
                       HFONT font,
                       HFONT smallFont)
{
    LayoutInfo layout;
    layout.margin = ScaleDpi(10, dpi);
    layout.gap = ScaleDpi(8, dpi);
    layout.separator = (std::max)(1, ScaleDpi(1, dpi));

    HGDIOBJ oldFont = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW mainMetrics{};
    GetTextMetricsW(dc, &mainMetrics);

    layout.codeText = code.empty() ? L"编码" : L"编码 " + code;
    layout.codeWidth = TextWidth(dc, layout.codeText);
    layout.headerHeight = (std::max)(ScaleDpi(28, dpi), static_cast<int>(mainMetrics.tmHeight) + ScaleDpi(8, dpi));
    layout.itemHeight = (std::max)(ScaleDpi(28, dpi), static_cast<int>(mainMetrics.tmHeight) + ScaleDpi(8, dpi));

    const size_t count = (std::min)(candidates.size(), static_cast<size_t>(kCandidatePageSize));
    layout.items.reserve(count);
    layout.itemWidths.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        layout.items.push_back(std::to_wstring(i + 1) + L". " + candidates[i]);
        const int itemWidth = TextWidth(dc, layout.items.back()) + ScaleDpi(10, dpi);
        layout.itemWidths.push_back(itemWidth);
        if (vertical)
        {
            layout.candidatesWidth = (std::max)(layout.candidatesWidth, itemWidth);
        }
        else
        {
            if (i != 0)
                layout.candidatesWidth += layout.gap;
            layout.candidatesWidth += itemWidth;
        }
    }

    if (vertical)
        layout.candidatesHeight = static_cast<int>(count) * layout.itemHeight;
    else if (count != 0)
        layout.candidatesHeight = layout.itemHeight;

    SelectObject(dc, smallFont ? smallFont : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW smallMetrics{};
    GetTextMetricsW(dc, &smallMetrics);
    layout.statusText = mode == InputMode::Chinese
        ? (scheme == InputScheme::Pinyin ? L"当前：拼音" : L"当前：五笔86＋拼音")
        : (mode == InputMode::EnglishSuggestion ? L"英文联想" : L"英文");
    if (mode == InputMode::Chinese)
        layout.statusText += fullWidthPunctuation ? L"·全角" : L"·半角";
    const size_t displayedPage = pageCount == 0 ? 0 : pageIndex + 1;
    layout.pageText = std::to_wstring(displayedPage) + L"/" + std::to_wstring(pageCount);
    layout.statusWidth = TextWidth(dc, layout.statusText);
    layout.pageWidth = TextWidth(dc, layout.pageText);
    layout.settingsWidth = TextWidth(dc, L"设置") + ScaleDpi(16, dpi);
    layout.settingsInset = ScaleDpi(2, dpi);
    layout.hintWidth = TextWidth(dc, kOperationHint);
    layout.footerHeight = (std::max)(ScaleDpi(24, dpi), static_cast<int>(smallMetrics.tmHeight) + ScaleDpi(8, dpi));
    SelectObject(dc, oldFont);

    const int headerWidth = layout.codeWidth + layout.statusWidth + layout.pageWidth +
                            layout.settingsWidth + layout.gap * 3;
    int contentWidth = (std::max)(headerWidth, layout.candidatesWidth);
    contentWidth = (std::max)(contentWidth, layout.hintWidth);
    layout.width = contentWidth + layout.margin * 2 + 2;
    layout.height = layout.margin * 2 + layout.headerHeight + layout.candidatesHeight +
                    layout.footerHeight + layout.separator * 2 + 2;
    return layout;
}

void FillSolidRect(HDC dc, RECT rect, COLORREF color) noexcept
{
    HBRUSH brush = CreateSolidBrush(color);
    if (!brush)
        return;
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawSingleLine(HDC dc, const std::wstring& text, RECT rect, UINT extraFlags = 0) noexcept
{
    DrawTextW(dc,
              text.c_str(),
              static_cast<int>(text.size()),
              &rect,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | extraFlags);
}

// 按背景亮度自动适配次要元素（次要文字、分隔线、边框、设置按钮）的深浅，
// 保证用户自定义任何背景色时窗口内容都可读。
struct ColorScheme
{
    COLORREF text;
    COLORREF secondaryText;
    COLORREF statusText;
    COLORREF separator;
    COLORREF settingsFill;
    COLORREF settingsPressed;
    COLORREF settingsText;
    COLORREF border;
};

ColorScheme MakeColorScheme(COLORREF textColor, COLORREF backColor) noexcept
{
    const int r = GetRValue(backColor);
    const int g = GetGValue(backColor);
    const int b = GetBValue(backColor);
    const bool dark = (r * 30 + g * 59 + b * 11) < 12800;
    ColorScheme scheme;
    scheme.text = textColor;
    scheme.secondaryText = dark ? RGB(173, 181, 194) : RGB(96, 102, 116);
    scheme.statusText = dark ? RGB(105, 211, 164) : RGB(0, 138, 92);
    scheme.separator = dark ? RGB(74, 80, 94) : RGB(206, 210, 220);
    scheme.settingsFill = dark ? RGB(48, 53, 64) : RGB(224, 228, 236);
    scheme.settingsPressed = dark ? RGB(76, 85, 102) : RGB(190, 196, 208);
    scheme.settingsText = dark ? RGB(225, 231, 241) : RGB(40, 44, 56);
    scheme.border = dark ? RGB(91, 99, 116) : RGB(150, 156, 170);
    return scheme;
}

RECT GetSettingsButtonRect(const RECT& client, const LayoutInfo& layout) noexcept
{
    const int right = (std::max)(layout.margin, static_cast<int>(client.right) - layout.margin);
    const int left = (std::max)(layout.margin, right - layout.settingsWidth);
    RECT button{left,
                layout.margin + layout.settingsInset,
                right,
                layout.margin + layout.headerHeight - layout.settingsInset};
    if (button.bottom < button.top)
        button.bottom = button.top;
    return button;
}
}

CandidateWindow::~CandidateWindow()
{
    Destroy();
}

bool CandidateWindow::Create()
{
    if (hwnd_ && IsWindow(hwnd_))
        return true;
    hwnd_ = nullptr;

    HINSTANCE instance = g_module ? g_module : GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kCandidateWindowClass;

    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            kCandidateWindowClass,
                            L"",
                            WS_POPUP,
                            0,
                            0,
                            1,
                            1,
                            nullptr,
                            nullptr,
                            instance,
                            this);
    if (!hwnd_)
        return false;

    const UINT windowDpi = GetDpiForWindow(hwnd_);
    dpi_ = windowDpi == 0 ? 96 : static_cast<int>(windowDpi);
    RecreateFonts();
    return true;
}

void CandidateWindow::SetSettingsHandler(std::function<void()> handler)
{
    settingsHandler_ = std::move(handler);
}

void CandidateWindow::Destroy()
{
    if (hwnd_ && IsWindow(hwnd_))
        DestroyWindow(hwnd_);
    hwnd_ = nullptr;

    if (font_)
    {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (smallFont_)
    {
        DeleteObject(smallFont_);
        smallFont_ = nullptr;
    }
}

void CandidateWindow::SetContent(const std::wstring& code,
                                 const std::vector<std::wstring>& pageCandidates,
                                 size_t pageIndex,
                                 size_t pageCount,
                                 InputMode mode,
                                 InputScheme scheme)
{
    code_ = code;
    candidates_ = pageCandidates;
    if (candidates_.size() > kCandidatePageSize)
        candidates_.resize(kCandidatePageSize);
    pageIndex_ = pageIndex;
    pageCount_ = pageCount;
    inputMode_ = mode;
    scheme_ = scheme;

    if (IsVisible())
    {
        RECT current{};
        GetWindowRect(hwnd_, &current);
        const int gap = Scale(kAnchorGap);
        RECT anchor{current.left, current.top - gap, current.left, current.top - gap};
        SizeToContent(anchor);
    }
    if (hwnd_ && IsWindow(hwnd_))
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void CandidateWindow::SetVertical(bool vertical)
{
    if (vertical_ == vertical)
        return;
    vertical_ = vertical;

    if (IsVisible())
    {
        RECT current{};
        GetWindowRect(hwnd_, &current);
        const int gap = Scale(kAnchorGap);
        RECT anchor{current.left, current.top - gap, current.left, current.top - gap};
        SizeToContent(anchor);
    }
    if (hwnd_ && IsWindow(hwnd_))
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void CandidateWindow::SetFullWidthPunctuation(bool fullWidth)
{
    if (fullWidthPunctuation_ == fullWidth)
        return;
    fullWidthPunctuation_ = fullWidth;

    if (IsVisible())
    {
        RECT current{};
        GetWindowRect(hwnd_, &current);
        const int gap = Scale(kAnchorGap);
        RECT anchor{current.left, current.top - gap, current.left, current.top - gap};
        SizeToContent(anchor);
    }
    if (hwnd_ && IsWindow(hwnd_))
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void CandidateWindow::SetFont(const std::wstring& family, int sizeTenths)
{
    if (fontFamily_ == family && fontSizeTenths_ == sizeTenths)
        return;
    fontFamily_ = family;
    fontSizeTenths_ = sizeTenths;

    if (hwnd_ != nullptr && IsWindow(hwnd_))
    {
        RecreateFonts();
        if (IsVisible())
        {
            RECT current{};
            GetWindowRect(hwnd_, &current);
            const int gap = Scale(kAnchorGap);
            RECT anchor{current.left, current.top - gap, current.left, current.top - gap};
            SizeToContent(anchor);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void CandidateWindow::SetColors(COLORREF textColor, COLORREF backColor)
{
    if (textColor_ == textColor && backColor_ == backColor)
        return;
    textColor_ = textColor;
    backColor_ = backColor;
    if (hwnd_ != nullptr && IsWindow(hwnd_))
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void CandidateWindow::ShowAt(const RECT& anchor)
{
    if (!Create())
        return;

    SizeToContent(anchor);    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
}

void CandidateWindow::Hide()
{
    if (hwnd_ && IsWindow(hwnd_))
        ShowWindow(hwnd_, SW_HIDE);
}

bool CandidateWindow::IsVisible() const noexcept
{
    return hwnd_ && IsWindow(hwnd_) && ::IsWindowVisible(hwnd_);
}

bool CandidateWindow::IsSettingsButtonAt(POINT point)
{
    if (!hwnd_ || !IsWindow(hwnd_))
        return false;

    HDC dc = GetDC(hwnd_);
    if (!dc)
        return false;
    const LayoutInfo layout = BuildLayout(dc,
                                          code_,
                                          candidates_,
                                          pageIndex_,
                                          pageCount_,
                                          inputMode_,
                                          scheme_,
                                          vertical_,
                                          fullWidthPunctuation_,
                                          dpi_,
                                          font_,
                                          smallFont_);
    ReleaseDC(hwnd_, dc);

    RECT client{};
    GetClientRect(hwnd_, &client);
    const RECT button = GetSettingsButtonRect(client, layout);
    return PtInRect(&button, point) != FALSE;
}

void CandidateWindow::RecreateFonts()
{
    const int mainHeight = (std::max)(1, MulDiv(fontSizeTenths_, dpi_, 720));
    const int smallHeight = (std::max)(1, MulDiv(mainHeight, 2, 3));
    auto createPair = [mainHeight, smallHeight](const wchar_t* family,
                                                HFONT& mainFont,
                                                HFONT& smallFont) {
        auto create = [family](int height) {
            return CreateFontW(-height,
                               0,
                               0,
                               0,
                               FW_NORMAL,
                               FALSE,
                               FALSE,
                               FALSE,
                               DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE,
                               family);
        };
        mainFont = create(mainHeight);
        smallFont = create(smallHeight);
        if (mainFont != nullptr && smallFont != nullptr)
            return true;
        if (mainFont != nullptr)
            DeleteObject(mainFont);
        if (smallFont != nullptr)
            DeleteObject(smallFont);
        mainFont = nullptr;
        smallFont = nullptr;
        return false;
    };

    HFONT newFont = nullptr;
    HFONT newSmallFont = nullptr;
    if (!createPair(fontFamily_.c_str(), newFont, newSmallFont) &&
        fontFamily_ != kDefaultCandidateFontFace)
    {
        createPair(kDefaultCandidateFontFace, newFont, newSmallFont);
    }
    if (newFont == nullptr || newSmallFont == nullptr)
        return;

    if (font_ != nullptr)
        DeleteObject(font_);
    if (smallFont_ != nullptr)
        DeleteObject(smallFont_);
    font_ = newFont;
    smallFont_ = newSmallFont;
}

void CandidateWindow::SizeToContent(const RECT& anchor)
{
    if (!hwnd_ || !IsWindow(hwnd_))
        return;

    HDC dc = GetDC(hwnd_);
    if (!dc)
        return;
    const LayoutInfo layout = BuildLayout(dc,
                                          code_,
                                          candidates_,
                                          pageIndex_,
                                          pageCount_,
                                          inputMode_,
                                          scheme_,
                                          vertical_,
                                          fullWidthPunctuation_,
                                          dpi_,
                                          font_,
                                          smallFont_);
    ReleaseDC(hwnd_, dc);

    HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    RECT workArea{};
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo))
    {
        workArea = monitorInfo.rcWork;
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    }

    const int workWidth = (std::max)(1, static_cast<int>(workArea.right - workArea.left));
    const int workHeight = (std::max)(1, static_cast<int>(workArea.bottom - workArea.top));
    const int width = (std::min)(layout.width, workWidth);
    const int height = (std::min)(layout.height, workHeight);
    const int gap = Scale(kAnchorGap);

    int x = anchor.left;
    int y = anchor.bottom + gap;
    if (x + width > workArea.right)
        x = workArea.right - width;
    if (x < workArea.left)
        x = workArea.left;

    if (y + height > workArea.bottom)
        y = anchor.top - height - gap;
    if (y < workArea.top)
        y = workArea.top;
    if (y + height > workArea.bottom)
        y = workArea.bottom - height;

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
}

void CandidateWindow::Paint()
{
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd_, &paint);
    if (!dc)
        return;

    RECT client{};
    GetClientRect(hwnd_, &client);
    const LayoutInfo layout = BuildLayout(dc,
                                          code_,
                                          candidates_,
                                          pageIndex_,
                                          pageCount_,
                                          inputMode_,
                                          scheme_,
                                          vertical_,
                                          fullWidthPunctuation_,
                                          dpi_,
                                          font_,
                                          smallFont_);

    const ColorScheme colors = MakeColorScheme(textColor_, backColor_);
    FillSolidRect(dc, client, backColor_);
    SetBkMode(dc, TRANSPARENT);

    HGDIOBJ oldFont = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    int y = layout.margin;
    RECT codeRect{layout.margin,
                  y,
                  layout.margin + layout.codeWidth,
                  y + layout.headerHeight};
    SetTextColor(dc, colors.text);
    DrawSingleLine(dc, layout.codeText, codeRect);

    const int candidatesTop = y + layout.headerHeight + layout.separator;
    const int candidatesBottom = candidatesTop + layout.candidatesHeight;
    const int contentRight = (std::max)(layout.margin, static_cast<int>(client.right) - layout.margin);
    if (vertical_)
    {
        int itemTop = candidatesTop;
        for (size_t i = 0; i < layout.items.size(); ++i)
        {
            RECT itemRect{layout.margin + Scale(5),
                          itemTop,
                          contentRight,
                          itemTop + layout.itemHeight};
            SetTextColor(dc, colors.text);
            DrawSingleLine(dc, layout.items[i], itemRect);
            itemTop += layout.itemHeight;
            if (i + 1 < layout.items.size())
            {
                RECT separator{layout.margin, itemTop - layout.separator, contentRight, itemTop};
                FillSolidRect(dc, separator, colors.separator);
            }
        }
    }
    else
    {
        int x = layout.margin;
        for (size_t i = 0; i < layout.items.size(); ++i)
        {
            if (i != 0)
            {
                RECT separator{x + layout.gap / 2 - layout.separator,
                                candidatesTop + Scale(4),
                                x + layout.gap / 2,
                                candidatesBottom - Scale(4)};
                FillSolidRect(dc, separator, colors.separator);
                x += layout.gap;
            }
            RECT itemRect{x + Scale(5),
                          candidatesTop,
                          x + layout.itemWidths[i] - Scale(5),
                          candidatesBottom};
            SetTextColor(dc, colors.text);
            DrawSingleLine(dc, layout.items[i], itemRect);
            x += layout.itemWidths[i];
        }
    }

    SelectObject(dc, smallFont_ ? smallFont_ : GetStockObject(DEFAULT_GUI_FONT));
    RECT statusRect{codeRect.right + layout.gap,
                    y,
                    codeRect.right + layout.gap + layout.statusWidth,
                    y + layout.headerHeight};
    SetTextColor(dc, inputMode_ == InputMode::Chinese ? colors.statusText : colors.secondaryText);
    DrawSingleLine(dc, layout.statusText, statusRect);

    const RECT settingsRect = GetSettingsButtonRect(client, layout);
    RECT pageRect{settingsRect.left - layout.gap - layout.pageWidth,
                  y,
                  settingsRect.left - layout.gap,
                  y + layout.headerHeight};
    SetTextColor(dc, colors.secondaryText);
    DrawSingleLine(dc, layout.pageText, pageRect, DT_RIGHT);

    FillSolidRect(dc,
                  settingsRect,
                  settingsButtonPressed_ ? colors.settingsPressed : colors.settingsFill);
    SetTextColor(dc, colors.settingsText);
    DrawSingleLine(dc, L"设置", settingsRect, DT_CENTER);

    RECT headerSeparator{layout.margin,
                         y + layout.headerHeight,
                         contentRight,
                         y + layout.headerHeight + layout.separator};
    FillSolidRect(dc, headerSeparator, colors.separator);

    RECT footerSeparator{layout.margin,
                         candidatesBottom,
                         contentRight,
                         candidatesBottom + layout.separator};
    FillSolidRect(dc, footerSeparator, colors.separator);
    RECT hintRect{layout.margin,
                  candidatesBottom + layout.separator,
                  contentRight,
                  candidatesBottom + layout.separator + layout.footerHeight};
    SetTextColor(dc, colors.secondaryText);
    DrawSingleLine(dc, kOperationHint, hintRect);
    SelectObject(dc, oldFont);

    HPEN borderPen = CreatePen(PS_SOLID, 1, colors.border);
    if (borderPen)
    {
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 0, 0, client.right, client.bottom);
        Rectangle(dc, settingsRect.left, settingsRect.top, settingsRect.right, settingsRect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);
    }

    EndPaint(hwnd_, &paint);
}

LRESULT CALLBACK CandidateWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self)
        return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_LBUTTONDOWN:
    {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (self->IsSettingsButtonAt(point))
        {
            self->settingsButtonPressed_ = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP:
        if (self->settingsButtonPressed_)
        {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const bool activate = self->IsSettingsButtonAt(point);
            self->settingsButtonPressed_ = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            if (activate)
                PostMessageW(hwnd, kShowSettingsMessage, 0, 0);
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (self->settingsButtonPressed_)
        {
            self->settingsButtonPressed_ = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case kShowSettingsMessage:
        if (self->settingsHandler_)
            self->settingsHandler_();
        return 0;

    case WM_PAINT:
        self->Paint();
        return 0;

    case WM_DPICHANGED:
    {
        const UINT newDpi = LOWORD(wParam);
        if (newDpi != 0)
            self->dpi_ = static_cast<int>(newDpi);
        self->RecreateFonts();

        RECT suggested{};
        if (lParam)
            suggested = *reinterpret_cast<const RECT*>(lParam);
        else
            GetWindowRect(hwnd, &suggested);
        const int gap = self->Scale(kAnchorGap);
        RECT anchor{suggested.left, suggested.top - gap, suggested.left, suggested.top - gap};
        self->SizeToContent(anchor);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_NCDESTROY:
        self->settingsButtonPressed_ = false;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self->hwnd_ == hwnd)
            self->hwnd_ = nullptr;
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
