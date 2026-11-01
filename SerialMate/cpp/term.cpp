// term.cpp —— VT100/xterm 终端控件实现
#include "term.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

static const wchar_t* kTermClass = L"SerialMateTerm";

// ---------- 颜色 ----------
// 与主界面 RichEdit 终端一致的深色配色
static const uint32_t kDefFg = 0xD8DEE9;
static const uint32_t kDefBg = 0x1B1E24;
static const uint32_t kSelBg = 0x2E5C8A;

static const uint32_t kPal16[16] = {
    0x3B4048, 0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xABB2BF,
    0x5C6370, 0xFF7B72, 0xB5E890, 0xFFD68A, 0x7CC4FF, 0xD9A0FF, 0x6FD3DE, 0xF2F4F8,
};
static uint32_t palette(int i) {
    if (i < 16) return kPal16[i & 15];
    if (i < 232) {                     // 6x6x6 色立方
        int n = i - 16;
        static const int lv[6] = {0, 95, 135, 175, 215, 255};
        return ((uint32_t)lv[n / 36] << 16) | ((uint32_t)lv[(n / 6) % 6] << 8) | (uint32_t)lv[n % 6];
    }
    int g = 8 + (i - 232) * 10;
    if (g > 255) g = 255;
    return ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g;
}
static inline uint32_t rgb(COLORREF c) { return ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | GetBValue(c); }
static inline COLORREF cr(uint32_t c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

// CJK 等双宽字符
static bool isWide(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F9FF) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);
}

void registerTermClass(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = Terminal::WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_IBEAM);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kTermClass;
    RegisterClassExW(&wc);
}

// ================= 构造 / 销毁 =================
Terminal::Terminal(HWND parent, InputFn onInput) : parent_(parent), onInput_(std::move(onInput)) {
    fontPt_ = 11;
    makeFont();
    grid_.assign(rows_, Row(cols_, Cell()));
    h_ = CreateWindowExW(WS_EX_CLIENTEDGE, kTermClass, L"", WS_CHILD | WS_CLIPCHILDREN,
                         0, 0, 100, 100, parent, nullptr, (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE),
                         this);
    layoutMetrics();
    resetScreen(false);
}

Terminal::~Terminal() {
    if (h_) {
        KillTimer(h_, 1);
        SetWindowLongPtrW(h_, GWLP_USERDATA, 0);
        DestroyWindow(h_);
        h_ = nullptr;
    }
    if (font_) DeleteObject(font_);
    if (bgBrush_) DeleteObject(bgBrush_);
}

// ================= 字体与尺寸 =================
void Terminal::makeFont() {
    HDC dc = GetDC(h_ ? h_ : parent_);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(h_ ? h_ : parent_, dc);
    fontPx_ = MulDiv(fontPt_, dpi, 72);
    LOGFONTW lf{};
    lf.lfHeight = -fontPx_;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    HFONT nf = CreateFontIndirectW(&lf);
    if (font_) DeleteObject(font_);
    font_ = nf ? nf : (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
}

void Terminal::setFontPt(int pt) {
    if (pt < 6) pt = 6;
    if (pt > 28) pt = 28;
    if (pt == fontPt_) return;
    fontPt_ = pt;
    makeFont();
    if (h_) {
        RECT rc;
        GetClientRect(h_, &rc);
        layoutMetrics();
        resizeGrid((rc.right - 2 * padX_) / cellW_, (rc.bottom - 2 * padY_) / cellH_);
    }
    invalidate();
}

void Terminal::layoutMetrics() {
    HDC dc = GetDC(h_);
    HFONT of = (HFONT)SelectObject(dc, font_);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SIZE sz{};
    GetTextExtentPoint32W(dc, L"M", 1, &sz);
    SelectObject(dc, of);
    ReleaseDC(h_, dc);
    cellW_ = sz.cx > 0 ? sz.cx : 8;
    cellH_ = tm.tmHeight > 0 ? tm.tmHeight : 16;
    padX_ = 2;
    padY_ = 2;
}

void Terminal::setBounds(int x, int y, int w, int h) {
    if (!h_) return;
    if (w < 40) w = 40;
    if (h < 40) h = 40;
    MoveWindow(h_, x, y, w, h, TRUE);
    RECT rc{0, 0, w, h};
    int nc = (rc.right - 2 * padX_) / cellW_;
    int nr = (rc.bottom - 2 * padY_) / cellH_;
    if (nc < 20) nc = 20;
    if (nr < 5) nr = 5;
    if (nc != cols_ || nr != rows_) resizeGrid(nc, nr);
}

void Terminal::showWindow(bool s) { if (h_) ShowWindow(h_, s ? SW_SHOW : SW_HIDE); }
void Terminal::focus() { if (h_) SetFocus(h_); }
void Terminal::invalidate() { if (h_) InvalidateRect(h_, nullptr, FALSE); }

// ================= 屏幕状态 =================
Terminal::Cell Terminal::blank() const {
    Cell c;
    c.ch = ' ';
    c.fg = fg_;
    c.bg = bg_;
    c.attr = 0;
    return c;
}

void Terminal::resetScreen(bool keepHist) {
    grid_.assign(rows_, Row(cols_, blank()));
    if (!keepHist) hist_.clear();
    cx_ = cy_ = 0;
    top_ = 0;
    bot_ = rows_ - 1;
    sbOffset_ = 0;
    alt_ = false;
    savedGrid_.clear();
    fg_ = kDefFg;
    bg_ = kDefBg;
    attr_ = 0;
    wrapPending_ = false;
    curStyle_ = 0;
    invalidate();
}

void Terminal::resizeGrid(int cols, int rows) {
    if (cols == cols_ && rows == rows_) return;
    if (cols < 1) cols = 80;
    if (rows < 1) rows = 24;

    if (alt_ && !savedGrid_.empty()) {          // 全屏程序（vim 等）直接重建
        altGrid_.assign(rows, Row(cols, blank()));
        altCx_ = 0;
        altCy_ = 0;
        cursorOn_ = true;
        cols_ = cols;
        rows_ = rows;
        top_ = 0;
        bot_ = rows_ - 1;
        invalidate();
        return;
    }

    std::vector<Row> ng(rows, Row(cols, blank()));
    // 行：从底部对齐保留内容，多出来的顶部行进回滚缓冲
    int keep = std::min(rows, rows_);
    int drop = rows_ - keep;
    for (int i = 0; i < drop; i++) hist_.push_back(grid_[i]);
    for (int i = 0; i < keep; i++) {
        Row& src = grid_[drop + i];
        for (int c = 0; c < cols && c < (int)src.size(); c++) ng[rows - keep + i][c] = src[c];
    }
    grid_.swap(ng);
    cy_ = std::min(cy_, rows - 1);
    cx_ = std::min(cx_, cols - 1);
    cols_ = cols;
    rows_ = rows;
    top_ = 0;
    bot_ = rows_ - 1;
    sbOffset_ = 0;
    invalidate();
    if (onResize) onResize(cols_, rows_);
}

Terminal::Row& Terminal::lineAt(int abs) { return grid_[abs - histCount()]; }
const Terminal::Row* Terminal::lineAtC(int abs) const {
    if (abs < (int)hist_.size()) return &hist_[abs];
    int i = abs - (int)hist_.size();
    if (i < 0 || i >= (int)grid_.size()) return nullptr;
    return &grid_[i];
}

void Terminal::clearCells(Row& r, int from, int to) {
    if (from < 0) from = 0;
    if (to > (int)r.size()) to = (int)r.size();
    Cell b = blank();
    for (int i = from; i < to; i++) r[i] = b;
}

void Terminal::scrollUp(int n) {
    for (int k = 0; k < n; k++) {
        if (top_ == 0 && !alt_) {                 // 顶行进回滚
            hist_.push_back(grid_[top_]);
            if (hist_.size() > 4000) hist_.pop_front();
        }
        for (int r = top_; r < bot_; r++) grid_[r] = grid_[r + 1];
        grid_[bot_].assign(cols_, blank());
    }
    invalidate();
}

void Terminal::scrollDown(int n) {
    for (int k = 0; k < n; k++) {
        for (int r = bot_; r > top_; r--) grid_[r] = grid_[r - 1];
        grid_[top_].assign(cols_, blank());
    }
    invalidate();
}

void Terminal::newLine() {
    wrapPending_ = false;
    if (cy_ == bot_) scrollUp(1);
    else if (cy_ < rows_ - 1) cy_++;
    invalidate();
}

void Terminal::carriageReturn() { cx_ = 0; wrapPending_ = false; }

// ================= 输出 =================
void Terminal::write(const std::string& bytes) {
    for (unsigned char c : bytes) feed((char)c);
    invalidate();
}

void Terminal::writeText(const std::wstring& text) {
    std::string u;
    for (wchar_t ch : text) {
        uint32_t cp = ch;
        if (cp < 0x80) u.push_back((char)cp);
        else if (cp < 0x800) { u.push_back((char)(0xC0 | (cp >> 6))); u.push_back((char)(0x80 | (cp & 0x3F))); }
        else { u.push_back((char)(0xE0 | (cp >> 12))); u.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
               u.push_back((char)(0x80 | (cp & 0x3F))); }
    }
    write(u);
}

void Terminal::clear() {
    hist_.clear();
    sbOffset_ = 0;
    resetScreen(false);
    invalidate();
}

void Terminal::feedUtf8(unsigned char c) {
    if (utf8Need_ == 0) {
        if (c < 0x80) { putChar(c); return; }
        if ((c & 0xE0) == 0xC0) { utf8Cp_ = c & 0x1F; utf8Need_ = 1; }
        else if ((c & 0xF0) == 0xE0) { utf8Cp_ = c & 0x0F; utf8Need_ = 2; }
        else if ((c & 0xF8) == 0xF0) { utf8Cp_ = c & 0x07; utf8Need_ = 3; }
        return;
    }
    if ((c & 0xC0) != 0x80) { utf8Need_ = 0; return; }   // 非法续字节，丢弃
    utf8Cp_ = (utf8Cp_ << 6) | (c & 0x3F);
    if (--utf8Need_ == 0) putChar(utf8Cp_);
}

void Terminal::feed(char cc) {
    unsigned char c = (unsigned char)cc;
    switch (st_) {
        case 0:
            if (c == 0x1B) { st_ = 1; return; }
            if (c == '\r') { carriageReturn(); return; }
            if (c == '\n') { newLine(); cx_ = cx_; return; }
            if (c == 0x08) { if (cx_ > 0) { cx_--; wrapPending_ = false; } return; }
            if (c == '\t') {
                int n = 8 - (cx_ % 8);
                while (n-- > 0) putChar(' ');
                return;
            }
            if (c == 0x07) return;
            if (c < 0x20) return;
            if (c == 0x7F) return;
            feedUtf8(c);
            return;
        case 1:                     // ESC
            if (c == '[') { st_ = 2; csi_.clear(); return; }
            if (c == ']') { st_ = 3; csi_.clear(); return; }
            if (c == '(' || c == ')' || c == '#' || c == '%') { st_ = 4; return; }
            if (c == '7') { savedX_ = cx_; savedY_ = cy_; st_ = 0; return; }
            if (c == '8') { cx_ = savedX_; cy_ = savedY_; st_ = 0; return; }
            if (c == 'D') { newLine(); st_ = 0; return; }
            if (c == 'M') { if (cy_ == top_) scrollDown(1); else if (cy_ > 0) cy_--; st_ = 0; return; }
            if (c == 'E') { newLine(); carriageReturn(); st_ = 0; return; }
            if (c == 'c') { clear(); st_ = 0; return; }
            if (c == '=' || c == '>') { st_ = 0; return; }
            st_ = 0;
            return;
        case 2:                     // CSI
            if ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == '>' || c == '<' || c == ':' ||
                c == ' ' || c == '!') {
                if (csi_.size() < 64) csi_.push_back((char)c);
                return;
            }
            if (c >= 0x40 && c <= 0x7E) {
                csiDispatch((char)c);
                st_ = 0;
                return;
            }
            st_ = 0;
            return;
        case 3:                     // OSC：吃到 BEL 或 ST
            if (c == 0x07) st_ = 0;
            else if (c == 0x1B) st_ = 5;
            return;
        case 4: st_ = 0; return;    // ESC (x 之类，忽略一个字符
        case 5: st_ = (c == '\\') ? 0 : 1; return;
    }
}

void Terminal::csiDispatch(char final) {
    // 解析参数
    nparam_ = 0;
    private_ = false;
    {
        int v = -1;
        for (size_t i = 0; i < csi_.size(); i++) {
            char c = csi_[i];
            if (c == '?') { private_ = true; continue; }
            if (c >= '0' && c <= '9') { v = (v < 0 ? 0 : v) * 10 + (c - '0'); continue; }
            if (c == ';') {
                if (nparam_ < 16) params_[nparam_++] = v < 0 ? 0 : v;
                v = -1;
                continue;
            }
        }
        if (nparam_ < 16) params_[nparam_++] = v < 0 ? 0 : v;
    }
    auto P = [&](int i, int def) { return (i < nparam_ && params_[i] > 0) ? params_[i] : def; };
    auto P0 = [&](int i, int def) { return (i < nparam_ && params_[i] >= 0) ? params_[i] : def; };

    switch (final) {
        case 'A': { int n = P(0, 1); cy_ = std::max(top_, cy_ - n); wrapPending_ = false; break; }
        case 'B': { int n = P(0, 1); cy_ = std::min(bot_, cy_ + n); wrapPending_ = false; break; }
        case 'C': { int n = P(0, 1); cx_ = std::min(cols_ - 1, cx_ + n); wrapPending_ = false; break; }
        case 'D': { int n = P(0, 1); cx_ = std::max(0, cx_ - n); wrapPending_ = false; break; }
        case 'E': { int n = P(0, 1); cy_ = std::min(bot_, cy_ + n); cx_ = 0; break; }
        case 'F': { int n = P(0, 1); cy_ = std::max(top_, cy_ - n); cx_ = 0; break; }
        case 'G': case '`': { cx_ = std::min(cols_ - 1, P(0, 1) - 1); break; }
        case 'd': { cy_ = std::min(rows_ - 1, P(0, 1) - 1); break; }
        case 'H': case 'f': {
            cy_ = std::min(rows_ - 1, P0(0, 0) - 1 < 0 ? 0 : P(0, 1) - 1);
            cx_ = std::min(cols_ - 1, P(1, 1) - 1 < 0 ? 0 : P(1, 1) - 1);
            wrapPending_ = false;
            break;
        }
        case 'J': {
            int n = P0(0, 0);
            if (n == 0) {
                clearCells(grid_[cy_], cx_, cols_);
                for (int r = cy_ + 1; r < rows_; r++) clearCells(grid_[r], 0, cols_);
            } else if (n == 1) {
                for (int r = 0; r < cy_; r++) clearCells(grid_[r], 0, cols_);
                clearCells(grid_[cy_], 0, cx_ + 1);
            } else {
                for (int r = 0; r < rows_; r++) clearCells(grid_[r], 0, cols_);
            }
            break;
        }
        case 'K': {
            int n = P0(0, 0);
            if (n == 0) clearCells(grid_[cy_], cx_, cols_);
            else if (n == 1) clearCells(grid_[cy_], 0, cx_ + 1);
            else clearCells(grid_[cy_], 0, cols_);
            break;
        }
        case 'L': { int n = P(0, 1); for (int k = 0; k < n; k++) { for (int r = bot_; r > cy_; r--) grid_[r] = grid_[r - 1]; grid_[cy_].assign(cols_, blank()); } break; }
        case 'M': { int n = P(0, 1); for (int k = 0; k < n; k++) { for (int r = cy_; r < bot_; r++) grid_[r] = grid_[r + 1]; grid_[bot_].assign(cols_, blank()); } break; }
        case 'P': { int n = P(0, 1); Row& r = grid_[cy_]; if (n > cols_ - cx_) n = cols_ - cx_; for (int i = cx_; i + n < cols_; i++) r[i] = r[i + n]; for (int i = cols_ - n; i < cols_; i++) r[i] = blank(); break; }
        case '@': { int n = P(0, 1); if (n > cols_ - cx_) n = cols_ - cx_; Row& r = grid_[cy_]; for (int i = cols_ - 1; i - n >= cx_; i--) r[i] = r[i - n]; for (int i = cx_; i < cx_ + n; i++) r[i] = blank(); break; }
        case 'X': { int n = P(0, 1); clearCells(grid_[cy_], cx_, cx_ + n); break; }
        case 'S': scrollUp(P(0, 1)); break;
        case 'T': scrollDown(P(0, 1)); break;
        case 'r': {
            int t = P(0, 1) - 1, b = P(1, rows_) - 1;
            if (t >= 0 && b < rows_ && t < b) { top_ = t; bot_ = b; cx_ = 0; cy_ = t; }
            break;
        }
        case 'm': sgr(); break;
        case 'h': case 'l': {
            bool on = (final == 'h');
            for (int i = 0; i < nparam_; i++) {
                int v = params_[i];
                if (private_) {
                    if (v == 25) { cursorOn_ = on; }
                    else if (v == 1) { appCursor_ = on; }
                    else if (v == 7) { /* 自动换行，忽略 */ }
                    else if (v == 1049 || v == 47 || v == 1047) {
                        if (on && !alt_) {
                            alt_ = true;
                            savedGrid_ = grid_;
                            altCx_ = cx_; altCy_ = cy_;
                            grid_.assign(rows_, Row(cols_, blank()));
                            cx_ = cy_ = 0;
                            sbOffset_ = 0;
                        } else if (!on && alt_) {
                            alt_ = false;
                            grid_ = savedGrid_;
                            savedGrid_.clear();
                            cx_ = altCx_; cy_ = altCy_;
                        }
                    } else if (v == 2004) bracketedPaste_ = on;
                } else {
                    if (v == 4) { /* 插入模式，忽略 */ }
                }
            }
            break;
        }
        case 'n': {
            int v = P0(0, 0);
            if (v == 6 && onInput_) {
                char buf[32];
                snprintf(buf, sizeof(buf), "\x1b[%d;%dR", cy_ + 1, cx_ + 1);
                onInput_(std::string(buf));
            } else if (v == 5 && onInput_) {
                onInput_(std::string("\x1b[0n"));
            }
            break;
        }
        case 'c': if (onInput_) onInput_(std::string("\x1b[?6c")); break;
        case 's': savedX_ = cx_; savedY_ = cy_; break;
        case 'u': cx_ = savedX_; cy_ = savedY_; break;
        default: break;
    }
    invalidate();
}

void Terminal::sgr() {
    if (nparam_ == 0) { fg_ = kDefFg; bg_ = kDefBg; attr_ = 0; return; }
    for (int i = 0; i < nparam_; i++) {
        int v = params_[i];
        switch (v) {
            case 0:  fg_ = kDefFg; bg_ = kDefBg; attr_ = 0; break;
            case 1:  attr_ |= 1; break;
            case 4:  attr_ |= 2; break;
            case 7:  attr_ |= 8; break;
            case 22: attr_ &= ~1; break;
            case 24: attr_ &= ~2; break;
            case 27: attr_ &= ~8; break;
            case 39: fg_ = kDefFg; break;
            case 49: bg_ = kDefBg; break;
            case 38:
            case 48: {
                uint32_t col = 0;
                if (i + 1 < nparam_ && params_[i + 1] == 5 && i + 2 < nparam_) {
                    col = palette(params_[i + 2]);
                    i += 2;
                } else if (i + 1 < nparam_ && params_[i + 1] == 2 && i + 4 < nparam_) {
                    col = ((uint32_t)params_[i + 2] << 16) | ((uint32_t)params_[i + 3] << 8) |
                          (uint32_t)params_[i + 4];
                    i += 4;
                }
                if (v == 38) fg_ = col; else bg_ = col;
                break;
            }
            default:
                if (v >= 30 && v <= 37) fg_ = palette(v - 30 + ((attr_ & 1) ? 8 : 0));
                else if (v >= 40 && v <= 47) bg_ = palette(v - 40);
                else if (v >= 90 && v <= 97) fg_ = palette(v - 90 + 8);
                else if (v >= 100 && v <= 107) bg_ = palette(v - 100 + 8);
                break;
        }
    }
}

void Terminal::putChar(uint32_t cp) {
    int w = isWide(cp) ? 2 : 1;
    if (wrapPending_) { carriageReturn(); newLine(); }
    if (cx_ + w > cols_) { carriageReturn(); newLine(); }
    if (cy_ >= rows_) cy_ = rows_ - 1;
    Row& r = grid_[cy_];
    Cell c;
    c.ch = cp;
    c.fg = fg_;
    c.bg = bg_;
    c.attr = attr_;
    if (cp == ' ') c.ch = ' ';
    r[cx_] = c;
    if (w == 2) {
        if (cx_ + 1 < cols_) {
            Cell c2 = c;
            c2.ch = 0;
            c2.attr |= 4;
            r[cx_ + 1] = c2;
        }
    }
    cx_ += w;
    if (cx_ >= cols_) { cx_ = cols_ - 1; wrapPending_ = true; }
}

// ================= 绘制 =================
void Terminal::paint(HDC dc, const RECT& rc) {
    // 双缓冲
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    paintRows(mem, 0, 0);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

void Terminal::paintRows(HDC dc, int x0, int y0) {
    RECT rc;
    GetClientRect(h_, &rc);
    HBRUSH bgB = CreateSolidBrush(cr(kDefBg));
    FillRect(dc, &rc, bgB);
    DeleteObject(bgB);

    SetBkMode(dc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(dc, font_);

    auto norm = [&](int v) { return std::max(0, std::min(totalLines() - 1, v)); };
    int baseV = histCount() - sbOffset_;

    // 选区排序
    int sv0 = selV0_, sc0 = selC0_, sv1 = selV1_, sc1 = selC1_;
    if (sv0 > sv1 || (sv0 == sv1 && sc0 > sc1)) {
        std::swap(sv0, sv1);
        std::swap(sc0, sc1);
    }

    for (int r = 0; r < rows_; r++) {
        int vline = baseV + r;
        const Row* row = lineAtC(vline);
        int y = y0 + padY_ + r * cellH_;
        if (!row) continue;
        int n = (int)row->size();

        // 背景（含选区）
        int c = 0;
        while (c < cols_) {
            uint32_t bgc = (c < n) ? row->at(c).bg : kDefBg;
            int start = c;
            while (c < cols_) {
                bool inSel = (vline > sv0 || (vline == sv0 && c >= sc0)) &&
                             (vline < sv1 || (vline == sv1 && c <= sc1)) && (selV0_ != selV1_ || selC0_ != selC1_);
                uint32_t b2 = inSel ? kSelBg : ((c < n) ? row->at(c).bg : kDefBg);
                if (b2 != bgc) break;
                c++;
            }
            bool selRun = (vline > sv0 || (vline == sv0 && start >= sc0)) && (vline < sv1 || (vline == sv1 && start <= sc1));
            (void)selRun;
            uint32_t useb = bgc;
            bool inSelRun = (vline > sv0 || (vline == sv0 && start >= sc0)) &&
                            (vline < sv1 || (vline == sv1 && start <= sc1)) && (selV0_ != selV1_ || selC0_ != selC1_);
            if (inSelRun) useb = kSelBg;
            RECT br{x0 + padX_ + start * cellW_, y, x0 + padX_ + c * cellW_, y + cellH_};
            if (useb != kDefBg) {
                HBRUSH hb = CreateSolidBrush(cr(useb));
                FillRect(dc, &br, hb);
                DeleteObject(hb);
            }
        }

        // 文字
        int i = 0;
        while (i < cols_ && i < n) {
            const Cell& cl = row->at(i);
            if (cl.ch == 0) { i++; continue; }
            uint32_t fgc = cl.fg;
            uint8_t at = cl.attr;
            bool inSel = (vline > sv0 || (vline == sv0 && i >= sc0)) &&
                         (vline < sv1 || (vline == sv1 && i <= sc1)) && (selV0_ != selV1_ || selC0_ != selC1_);
            uint32_t back = inSel ? kSelBg : cl.bg;
            if (at & 8) { uint32_t t = fgc; fgc = back; back = t; }
            if (inSel && !(at & 8)) fgc = kDefBg;
            int j = i;
            std::wstring run;
            while (j < cols_ && j < n) {
                const Cell& cj = row->at(j);
                bool sj = (vline > sv0 || (vline == sv0 && j >= sc0)) &&
                          (vline < sv1 || (vline == sv1 && j <= sc1)) && (selV0_ != selV1_ || selC0_ != selC1_);
                uint32_t bj = sj ? kSelBg : cj.bg;
                uint32_t fj = cj.fg;
                if (cj.attr & 8) { uint32_t t = fj; fj = bj; bj = t; }
                if (sj && !(cj.attr & 8)) fj = kDefBg;
                if (cj.ch == 0 && !(cj.attr & 4)) { j++; continue; }
                if (cj.ch == 0) break;
                if (fj != fgc || bj != back) break;
                uint32_t cp = cj.ch;
                if (cp <= 0xFFFF) run.push_back((wchar_t)cp);
                else {
                    cp -= 0x10000;
                    run.push_back((wchar_t)(0xD800 + (cp >> 10)));
                    run.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
                }
                j++;
                if ((cj.attr & 4) == 0 && isWide(cj.ch)) {
                    // 双宽字符占两格，跳过后续半格
                    j++;
                }
            }
            if (run.empty()) { i = j + 1; continue; }
            SetTextColor(dc, cr(fgc));
            if (at & 2) {
                // 下划线用矩形近似
                RECT ur{x0 + padX_ + i * cellW_, y + cellH_ - 2, x0 + padX_ + j * cellW_, y + cellH_ - 1};
                HBRUSH hb = CreateSolidBrush(cr(fgc));
                FillRect(dc, &ur, hb);
                DeleteObject(hb);
            }
            TextOutW(dc, x0 + padX_ + i * cellW_, y, run.c_str(), (int)run.size());
            i = j;
        }
    }

    // 光标
    if (cursorOn_ && blinkOn_ && sbOffset_ == 0 && !alt_ ? true : (cursorOn_ && blinkOn_ && sbOffset_ == 0)) {
        int y = y0 + padY_ + cy_ * cellH_;
        int x = x0 + padX_ + cx_ * cellW_;
        uint32_t cc = kDefFg;
        const Row* cr2 = lineAtC(histCount() + cy_);
        if (cr2 && cx_ < (int)cr2->size() && cr2->at(cx_).ch) cc = cr2->at(cx_).fg;
        RECT cb{x, y, x + cellW_, y + cellH_};
        HBRUSH hb = CreateSolidBrush(cr(cc));
        FillRect(dc, &cb, hb);
        DeleteObject(hb);
        const Row* row = lineAtC(histCount() + cy_);
        if (row && cx_ < (int)row->size() && row->at(cx_).ch > ' ') {
            wchar_t wc[2] = {(wchar_t)row->at(cx_).ch, 0};
            SetTextColor(dc, cr(kDefBg));
            TextOutW(dc, x, y, wc, 1);
        }
    }

    SelectObject(dc, of);
    (void)norm;
}

// ================= 选择 =================
void Terminal::posToCell(int px, int py, int* vline, int* col) {
    int c = (px - padX_) / cellW_;
    int r = (py - padY_) / cellH_;
    if (c < 0) c = 0;
    if (r < 0) r = 0;
    if (c > cols_ - 1) c = cols_ - 1;
    if (r > rows_ - 1) r = rows_ - 1;
    *vline = histCount() - sbOffset_ + r;
    *col = c;
}

std::wstring Terminal::selectionText() {
    int v0 = selV0_, c0 = selC0_, v1 = selV1_, c1 = selC1_;
    if (v0 > v1 || (v0 == v1 && c0 > c1)) { std::swap(v0, v1); std::swap(c0, c1); }
    std::wstring out;
    for (int v = v0; v <= v1; v++) {
        const Row* row = lineAtC(v);
        if (!row) continue;
        int a = (v == v0) ? c0 : 0;
        int b = (v == v1) ? c1 : cols_ - 1;
        std::wstring line;
        for (int c = a; c <= b && c < (int)row->size(); c++) {
            uint32_t ch = row->at(c).ch;
            if (ch == 0) continue;
            if (ch <= 0xFFFF) line.push_back((wchar_t)ch);
        }
        while (!line.empty() && line.back() == L' ') line.pop_back();
        out += line;
        if (v != v1) out += L"\r\n";
    }
    return out;
}

void Terminal::copySelection() {
    std::wstring t = selectionText();
    if (t.empty()) return;
    if (!OpenClipboard(h_)) return;
    EmptyClipboard();
    size_t bytes = (t.size() + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
        void* p = GlobalLock(hg);
        if (p) {
            memcpy(p, t.c_str(), bytes);
            GlobalUnlock(hg);
            SetClipboardData(CF_UNICODETEXT, hg);
        }
    }
    CloseClipboard();
}

void Terminal::pasteClipboard() {
    if (!OpenClipboard(h_)) return;
    HANDLE hd = GetClipboardData(CF_UNICODETEXT);
    if (hd) {
        auto* p = (const wchar_t*)GlobalLock(hd);
        if (p) {
            std::wstring t(p);
            GlobalUnlock(hd);
            std::string u;
            for (wchar_t wc : t) {
                uint32_t cp = wc;
                if (cp < 0x80) u.push_back((char)cp);
                else if (cp < 0x800) { u.push_back((char)(0xC0 | (cp >> 6))); u.push_back((char)(0x80 | (cp & 0x3F))); }
                else { u.push_back((char)(0xE0 | (cp >> 12))); u.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                       u.push_back((char)(0x80 | (cp & 0x3F))); }
            }
            if (bracketedPaste_) u = "\x1b[200~" + u + "\x1b[201~";
            if (onInput_) onInput_(u);
        }
    }
    CloseClipboard();
}

void Terminal::sendKey(const char* s) { if (onInput_) onInput_(std::string(s)); }

void Terminal::sendChar(uint32_t cp) {
    if (bracketedPaste_) { /* 普通输入不走粘贴包装 */ }
    std::string u;
    if (cp < 0x80) u.push_back((char)cp);
    else if (cp < 0x800) { u.push_back((char)(0xC0 | (cp >> 6))); u.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { u.push_back((char)(0xE0 | (cp >> 12))); u.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                             u.push_back((char)(0x80 | (cp & 0x3F))); }
    else { u.push_back((char)(0xF0 | (cp >> 18))); u.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
           u.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); u.push_back((char)(0x80 | (cp & 0x3F))); }
    if (onInput_) onInput_(u);
}

// ================= 消息 =================
LRESULT CALLBACK Terminal::WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Terminal* self = (Terminal*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (Terminal*)cs->lpCreateParams;
        self->h_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (!self) return DefWindowProcW(h, m, wp, lp);
    return self->handle(m, wp, lp);
}

LRESULT Terminal::handle(UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_CREATE:
            timer_ = SetTimer(h_, 1, 530, nullptr);
            return 0;
        case WM_DESTROY:
            if (timer_) KillTimer(h_, 1);
            return 0;
        case WM_TIMER:
            blinkOn_ = !blinkOn_;
            invalidate();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h_, &ps);
            RECT rc;
            GetClientRect(h_, &rc);
            int nc = (rc.right - 2 * padX_) / cellW_;
            int nr = (rc.bottom - 2 * padY_) / cellH_;
            if (nc >= 20 && nr >= 5 && (nc != cols_ || nr != rows_)) {
                resizeGrid(nc, nr);          // 布局顺序错乱时自愈，保证 pty 与实际窗口一致
                GetClientRect(h_, &rc);
            }
            if (rc.right > 0 && rc.bottom > 0) paint(dc, rc);
            EndPaint(h_, &ps);
            return 0;
        }
        case WM_SIZE: {
            RECT rc;
            GetClientRect(h_, &rc);
            int nc = (rc.right - 2 * padX_) / cellW_;
            int nr = (rc.bottom - 2 * padY_) / cellH_;
            if (nc < 20) nc = 20;
            if (nr < 5) nr = 5;
            if (nc != cols_ || nr != rows_) resizeGrid(nc, nr);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(h_);
            int v, c;
            posToCell(LOWORD(lp), HIWORD(lp), &v, &c);
            selV0_ = selV1_ = v;
            selC0_ = selC1_ = c;
            seling_ = true;
            SetCapture(h_);
            invalidate();
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!seling_) return 0;
            int v, c;
            posToCell(LOWORD(lp), HIWORD(lp), &v, &c);
            selV1_ = v;
            selC1_ = c;
            invalidate();
            return 0;
        }
        case WM_LBUTTONUP: {
            seling_ = false;
            ReleaseCapture();
            invalidate();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            int lines = -delta / WHEEL_DELTA * 3;
            int maxSb = std::min((int)hist_.size(), 3000);
            sbOffset_ = std::max(0, std::min(maxSb, sbOffset_ + lines));
            invalidate();
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT pt{LOWORD(lp), HIWORD(lp)};
            HMENU pm = CreatePopupMenu();
            AppendMenuW(pm, MF_STRING, 1, L"复制(&C)");
            AppendMenuW(pm, MF_STRING, 2, L"粘贴(&P)");
            AppendMenuW(pm, MF_STRING, 3, L"全选(&A)");
            AppendMenuW(pm, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(pm, MF_STRING, 4, L"清屏(&L)");
            ClientToScreen(h_, &pt);
            int cmd = TrackPopupMenu(pm, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h_, nullptr);
            DestroyMenu(pm);
            if (cmd == 1) copySelection();
            else if (cmd == 2) pasteClipboard();
            else if (cmd == 3) {
                selV0_ = histCount() - sbOffset_;
                selC0_ = 0;
                selV1_ = histCount() - sbOffset_ + rows_ - 1;
                selC1_ = cols_ - 1;
                copySelection();
                invalidate();
            } else if (cmd == 4) clear();
            SetFocus(h_);
            return 0;
        }
        case WM_CHAR: {
            uint32_t ch = (uint32_t)wp;
            if (ch == 0) return 0;
            // 回车/退格/制表走常见控制码
            if (ch == L'\r') { sendChar('\r'); return 0; }
            if (ch == 0x08) { sendChar(0x7F); return 0; }
            if (ch == 0x09) { sendChar('\t'); return 0; }
            if (ch == 0x1B) { sendChar(0x1B); return 0; }
            if (ch < 0x20) { sendChar(ch); return 0; }        // Ctrl+A 等
            sendChar(ch);
            if (sbOffset_) { sbOffset_ = 0; invalidate(); }
            return 0;
        }
        case WM_KEYDOWN: {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            int mod = 1 + (shift ? 1 : 0) + (alt ? 2 : 0);
            char buf[16];
            if (ctrl && shift && (wp == 'C')) { copySelection(); return 0; }
            if (ctrl && shift && (wp == 'V')) { pasteClipboard(); return 0; }
            if (ctrl && (wp == VK_INSERT)) { copySelection(); return 0; }
            if (shift && (wp == VK_INSERT)) { pasteClipboard(); return 0; }
            switch (wp) {
                case VK_UP:    snprintf(buf, sizeof(buf), appCursor_ ? "\x1bOA" : "\x1b[A"); sendKey(buf); return 0;
                case VK_DOWN:  snprintf(buf, sizeof(buf), appCursor_ ? "\x1bOB" : "\x1b[B"); sendKey(buf); return 0;
                case VK_RIGHT: snprintf(buf, sizeof(buf), appCursor_ ? "\x1bOC" : "\x1b[C"); sendKey(buf); return 0;
                case VK_LEFT:  snprintf(buf, sizeof(buf), appCursor_ ? "\x1bOD" : "\x1b[D"); sendKey(buf); return 0;
                case VK_HOME:  sendKey(shift ? "\x1b[1;2H" : "\x1b[H"); return 0;
                case VK_END:   sendKey(shift ? "\x1b[1;2F" : "\x1b[F"); return 0;
                case VK_INSERT: sendKey("\x1b[2~"); return 0;
                case VK_DELETE: sendKey("\x1b[3~"); return 0;
                case VK_PRIOR: {
                    if (shift) { sbOffset_ = std::max(0, std::min((int)hist_.size(), sbOffset_ + rows_)); invalidate(); }
                    else sendKey("\x1b[5~");
                    return 0;
                }
                case VK_NEXT: {
                    if (shift) { sbOffset_ = std::max(0, sbOffset_ - rows_); invalidate(); }
                    else sendKey("\x1b[6~");
                    return 0;
                }
                case VK_F1: sendKey("\x1bOP"); return 0;
                case VK_F2: sendKey("\x1bOQ"); return 0;
                case VK_F3: sendKey("\x1bOR"); return 0;
                case VK_F4: sendKey("\x1bOS"); return 0;
                case VK_F5: sendKey("\x1b[15~"); return 0;
                case VK_F6: sendKey("\x1b[17~"); return 0;
                case VK_F7: sendKey("\x1b[18~"); return 0;
                case VK_F8: sendKey("\x1b[19~"); return 0;
                case VK_F9: sendKey("\x1b[20~"); return 0;
                case VK_F10: sendKey("\x1b[21~"); return 0;
                case VK_F11: sendKey("\x1b[23~"); return 0;
                case VK_F12: sendKey("\x1b[24~"); return 0;
                default: break;
            }
            (void)mod;
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            blinkOn_ = true;
            invalidate();
            return 0;
        case WM_LBUTTONDBLCLK: {
            // 双击选中整行
            int v, c;
            posToCell(LOWORD(lp), HIWORD(lp), &v, &c);
            selV0_ = selV1_ = v;
            selC0_ = 0;
            selC1_ = cols_ - 1;
            copySelection();
            invalidate();
            return 0;
        }
        default: break;
    }
    return DefWindowProcW(h_, m, wp, lp);
}
