#include "TV1TSF.h"

#include <algorithm>

namespace
{
constexpr wchar_t kCandidateWindowClass[] = L"TV1TSF.CandidateWindow";
constexpr wchar_t kOperationHint[] = L"1-5 选词  , . 翻页  F8 方案  Ctrl+. 标点  F6 横/竖排";
constexpr int kAnchorGap = 4;

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
                       bool chinese,
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
    layout.statusText = chinese
        ? (scheme == InputScheme::Pinyin ? L"当前：拼音" : L"当前：五笔86＋拼音")
        : L"英文";
    if (chinese)
        layout.statusText += fullWidthPunctuation ? L"·全角" : L"·半角";
    const size_t displayedPage = pageCount == 0 ? 0 : pageIndex + 1;
    layout.pageText = std::to_wstring(displayedPage) + L"/" + std::to_wstring(pageCount);
    layout.statusWidth = TextWidth(dc, layout.statusText);
    layout.pageWidth = TextWidth(dc, layout.pageText);
    layout.hintWidth = TextWidth(dc, kOperationHint);
    layout.footerHeight = (std::max)(ScaleDpi(24, dpi), static_cast<int>(smallMetrics.tmHeight) + ScaleDpi(8, dpi));
    SelectObject(dc, oldFont);

    const int headerWidth = layout.codeWidth + layout.statusWidth + layout.pageWidth + layout.gap * 2;
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
                                 bool chinese,
                                 InputScheme scheme)
{
    code_ = code;
    candidates_ = pageCandidates;
    if (candidates_.size() > kCandidatePageSize)
        candidates_.resize(kCandidatePageSize);
    pageIndex_ = pageIndex;
    pageCount_ = pageCount;
    chinese_ = chinese;
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

void CandidateWindow::ShowAt(const RECT& anchor)
{
    if (!Create())
        return;

    SizeToContent(anchor);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
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

void CandidateWindow::RecreateFonts()
{
    HFONT newFont = CreateFontW(-Scale(18),
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
                                L"Microsoft YaHei UI");
    HFONT newSmallFont = CreateFontW(-Scale(12),
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
                                     L"Microsoft YaHei UI");

    if (newFont)
    {
        if (font_)
            DeleteObject(font_);
        font_ = newFont;
    }
    if (newSmallFont)
    {
        if (smallFont_)
            DeleteObject(smallFont_);
        smallFont_ = newSmallFont;
    }
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
                                          chinese_,
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
                                          chinese_,
                                          scheme_,
                                          vertical_,
                                          fullWidthPunctuation_,
                                          dpi_,
                                          font_,
                                          smallFont_);

    FillSolidRect(dc, client, RGB(28, 31, 38));
    SetBkMode(dc, TRANSPARENT);

    HGDIOBJ oldFont = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    int y = layout.margin;
    RECT codeRect{layout.margin,
                  y,
                  layout.margin + layout.codeWidth,
                  y + layout.headerHeight};
    SetTextColor(dc, RGB(235, 238, 245));
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
            SetTextColor(dc, RGB(245, 246, 250));
            DrawSingleLine(dc, layout.items[i], itemRect);
            itemTop += layout.itemHeight;
            if (i + 1 < layout.items.size())
            {
                RECT separator{layout.margin, itemTop - layout.separator, contentRight, itemTop};
                FillSolidRect(dc, separator, RGB(62, 67, 79));
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
                FillSolidRect(dc, separator, RGB(74, 80, 94));
                x += layout.gap;
            }
            RECT itemRect{x + Scale(5),
                          candidatesTop,
                          x + layout.itemWidths[i] - Scale(5),
                          candidatesBottom};
            SetTextColor(dc, RGB(245, 246, 250));
            DrawSingleLine(dc, layout.items[i], itemRect);
            x += layout.itemWidths[i];
        }
    }

    SelectObject(dc, smallFont_ ? smallFont_ : GetStockObject(DEFAULT_GUI_FONT));
    RECT statusRect{codeRect.right + layout.gap,
                    y,
                    codeRect.right + layout.gap + layout.statusWidth,
                    y + layout.headerHeight};
    SetTextColor(dc, chinese_ ? RGB(105, 211, 164) : RGB(176, 181, 194));
    DrawSingleLine(dc, layout.statusText, statusRect);

    RECT pageRect{client.right - layout.margin - layout.pageWidth,
                  y,
                  client.right - layout.margin,
                  y + layout.headerHeight};
    SetTextColor(dc, RGB(173, 181, 197));
    DrawSingleLine(dc, layout.pageText, pageRect, DT_RIGHT);

    RECT headerSeparator{layout.margin,
                         y + layout.headerHeight,
                         contentRight,
                         y + layout.headerHeight + layout.separator};
    FillSolidRect(dc, headerSeparator, RGB(74, 80, 94));

    RECT footerSeparator{layout.margin,
                         candidatesBottom,
                         contentRight,
                         candidatesBottom + layout.separator};
    FillSolidRect(dc, footerSeparator, RGB(74, 80, 94));
    RECT hintRect{layout.margin,
                  candidatesBottom + layout.separator,
                  contentRight,
                  candidatesBottom + layout.separator + layout.footerHeight};
    SetTextColor(dc, RGB(174, 181, 194));
    DrawSingleLine(dc, kOperationHint, hintRect);
    SelectObject(dc, oldFont);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(91, 99, 116));
    if (borderPen)
    {
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 0, 0, client.right, client.bottom);
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
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self->hwnd_ == hwnd)
            self->hwnd_ = nullptr;
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
