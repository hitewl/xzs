// ui_console.cpp - 终端画面：RichEdit 渲染、VT100 解析、按键输入、输入法同步

#include "sm.h"
void viewReplace(const wchar_t* t) {
    g_selfEdit = true;
    SendMessageW(g_hView, EM_REPLACESEL, FALSE, (LPARAM)t);
    g_selfEdit = false;
}



// 把插入点挪到文档真正的末尾再读回来。
// 坑：RichEdit 的 WM_GETTEXTLENGTH 把段落符按 \r\n 两个字符计，而 CHARRANGE/EM_EXSETSEL
// 的下标是按 \r 一个字符算的——直接拿长度当插入位置，每多一行就偏 1，
// 攒够以后 EM_REPLACESEL 会落在**错误的位置**上，把提示符重复写进画面
// （表现为 root@root@ATK-IMX6U:~#，切会话后必现）。

long caretToEnd() {
    CHARRANGE cr{ 0x7FFFFFFF, 0x7FFFFFFF };
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARRANGE out{};
    SendMessageW(g_hView, EM_EXGETSEL, 0, (LPARAM)&out);
    return out.cpMax;
}


// 全文：用 GT_DEFAULT，行尾是单个 \r，与 CHARRANGE 下标口径一致

std::wstring viewText() {
    if (!g_hView) return L"";
    if (!g_termReady) {                              // 普通 EDIT 没有 EM_GETTEXTEX
        long n = (long)SendMessageW(g_hView, WM_GETTEXTLENGTH, 0, 0);
        std::wstring s((size_t)n + 1, L'\0');
        SendMessageW(g_hView, WM_GETTEXT, (WPARAM)(n + 1), (LPARAM)s.data());
        s.resize(wcslen(s.c_str()));
        return s;
    }
    GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_DEFAULT, 1200};
    long n = (long)SendMessageW(g_hView, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    if (n <= 0) return L"";
    std::wstring s((size_t)n + 16, L'\0');
    GETTEXTEX gt{sizeof(gt), GT_DEFAULT, 1200, nullptr};
    DWORD got = (DWORD)SendMessageW(g_hView, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s.data());
    s.resize(got < s.size() ? (size_t)got : s.size());
    return s;
}

std::wstring viewRange(HWND h, long s, long e) {
    std::wstring t;
    if (e <= s) return t;
    t.assign((size_t)(e - s), L'\0');
    TEXTRANGEW tr{{s, e}, t.data()};
    if (SendMessageW(h, EM_GETTEXTRANGE, 0, (LPARAM)&tr) <= 0) t.clear();
    return t;
}

void viewDeleteRange(HWND h, long s, long e) {
    CHARRANGE del{s, e};
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&del);
    g_selfEdit = true;                      // 删除是我们自己的动作，不触发外来文本逻辑
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)L"");
    g_selfEdit = false;
}

void cacheText(const std::wstring& t) {
    std::lock_guard<std::mutex> lk(g_textMtx);
    g_allText += t;
    if (g_allText.size() > 4 * 1024 * 1024) g_allText.erase(0, g_allText.size() - 3 * 1024 * 1024);
}


// 上一次成功设到选区的颜色（相同就不要重复设，见 termSyncLine）
// EM_SETCHARFORMAT(SCF_SELECTION) 会让 RichEdit 重排并重绘这段文本，
// 每敲一个字符都设一次，整行就一直在闪。

// 把插入点设成终端基准格式：字体 + 字号 + 字符集 + 当前颜色。
//
// 只设颜色是不够的。输入法上屏时 RichEdit/TSF 会往文档里塞自己的字体
// （宋体等的 yHeight 比 Consolas 大），我们随后虽然把那几个字删掉了，
// **插入点格式却留在了那个更大的字体上**，于是之后板子的所有输出都继承它——
// 表现为「输入汉字后行距变大、后面输出的字也变大」，而且一直不恢复。
// 所以每次写文本之前把字体/字号一起压实，颜色只是其中一项。

void termApplyColor() {
    if (!g_termReady) return;
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
#ifdef TERM_FMT_OLD
    cf.dwMask = CFM_COLOR;                    // 对照测试：只看颜色的老行为
#else
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR;
#endif
    cf.yHeight = g_fontPt * 20;
    cf.bCharSet = DEFAULT_CHARSET;
    lstrcpynW(cf.szFaceName, L"Consolas", LF_FACESIZE);
    cf.crTextColor = g_tcolor;
    SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    g_appliedColor = g_tcolor;
}


// 文档字符数：GTL_NUMCHARS 按 \r 一个字符算，和 CHARRANGE 下标同一口径

long docChars() {
    GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_DEFAULT, 1200};
    return (long)SendMessageW(g_hView, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
}


// 插入点必须停在「当前行末尾」。终端里光标跑到别处是没有意义的，
// 但 RichEdit 是可编辑控件，会在这些情况下把插入点挪走：
//   · 在输出区点一下（想选文字/顺手点窗口）→ 插入点落到点击位置
//   · 输入法上屏后 syncForeignText() 删掉本地插入的字 → 插入点塌在删除处
//   · Ctrl+A 全选之后又点了别处
// 表现为「光标跑到屏幕中间、不在最后一行」。这里把它拉回行尾；
// 用户正在框选（cpMin != cpMax）时不动，那是有意为了复制。

void termEnsureCaret() {
    if (!g_hView || !g_termReady) return;
    CHARRANGE cr{};
    SendMessageW(g_hView, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMax != cr.cpMin) return;                       // 有选区：用户框选，别打断
    long want = g_lcp + (long)g_lshow.size();
    if (cr.cpMax == want) return;
    dbgLog(L"caret fix: was=%ld want=%ld", (long)cr.cpMax, want);
    CHARRANGE to{want, want};
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&to);
    SendMessageW(g_hView, EM_SCROLLCARET, 0, 0);
}


// 当前行起点 + 已渲染长度 应当正好等于文档末尾（每次写完都维持这个不变量）。
// 对不上说明文档被别的路径改过（切会话、外来文本、被裁剪），
// 这时按旧锚点写会把字插到屏幕中间，所以只在「我们的行已经不在文末」这种
// 安全情况下把锚点重新落到文末，并清掉已渲染缓存让它整行重画。

void termAnchorCheck() {
    long end = docChars();
    long want = g_lcp + (long)g_lshow.size();
    if (end == want) return;
    dbgLog(L"anchor drift: end=%ld want=%ld", end, want);
    if (end < want) {                        // 我们的行被删掉了：重新落到文末重画
        g_lcp = end;
        g_lshow.clear();
    }
}


// 把当前行按字节缓冲重画一遍
//
// 只替换「公共前缀之后的那一段」。
// 早先的实现每次都把整行（含提示符 root@ATK-IMX6U:~#）选中后 EM_REPLACESEL，
// 等于每敲一个字符就把整行删掉重写，屏幕上提示符和前文会一下一下地闪；
// 现在按前缀求差集：在行尾追加时公共前缀就是整行，选区退化成插入点，
// 只有真正改动的那几个字被重画，提示符不会再动。
//
// **g_lshow 存的是渲染串，不是逻辑文本**：多一个时间戳、几个硬换行。
// 因为时间戳要出现在**每一条视觉行**的行首，而 RichEdit 的软换行是渲染层的，
// 文档里没有换行符，续行的行首根本不存在，没法挂时间戳——只能自己算列宽、
// 自己插 "\r" + 时间戳。所以这里先把逻辑行交给 termBuildVis 变成渲染串，再做 diff。

void termCalibrate();   // 定义在下面 termColsUpdate 旁边

// 把渲染串里「每行行首的时间戳」染成系统色（正文色由 termApplyColor 决定）。
// 只处理与本次新写入区间 [from, vis.size()) 有交集的时间戳——前面的早就染过了，
// 重复设格式会让 RichEdit 反复重排，整行发闪。

void termTintTs(size_t from, const std::wstring& vis) {
    if (!g_showTs || g_curTs.empty()) return;
    size_t tsLen = g_curTs.size();
    size_t p = from < vis.size() ? from : vis.size();
    while (p > 0 && vis[p - 1] != L'\r') --p;               // 回退到所在视觉行行首
    bool tinted = false;
    for (; p + tsLen <= vis.size(); ) {
        if ((p == 0 || vis[p - 1] == L'\r') && vis.compare(p, tsLen, g_curTs) == 0) {
            if (p + tsLen > from) {
                CHARRANGE cs{g_lcp + (long)p, g_lcp + (long)p + (long)tsLen};
                SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&cs);
                CHARFORMAT2W cf{};
                cf.cbSize = sizeof(cf);
                cf.dwMask = CFM_COLOR;
                cf.crTextColor = C_SYS;
                SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
                tinted = true;
            }
            p += tsLen;
        } else ++p;
    }
    if (tinted) {
        // 选区刚停在时间戳上，插入点格式会被带成灰色，压回文末并重设正文色
        CHARRANGE ce{g_lcp + (long)vis.size(), g_lcp + (long)vis.size()};
        SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&ce);
        termApplyColor();
    }
}

void termSyncLine() {
    if (!g_hView || !g_termReady) return;
    termAnchorCheck();
    // 行首时间戳在第一次渲染这一行时生成并缓存
    if (g_showTs) {
        if (g_tsNeed) { g_curTs = timestamp(); g_tsNeed = false; }
    } else {
        g_curTs.clear();
        g_tsNeed = false;
    }
    std::wstring r = rxDecode(g_lb);
    if (g_termCols <= 0) termColsUpdate();             // 首次渲染前一定先把列宽量出来
    std::wstring vis = termBuildVis(r);
#ifdef SM_WRAP_TRACE
    dbgLog(L"sync show=%d need=%d cur=%d cols=%d lb=%d r=%d vis=%d lshow=%d lcp=%ld",
           (int)g_showTs, (int)g_tsNeed, (int)g_curTs.size(), g_termCols,
           (int)g_lb.size(), (int)r.size(), (int)vis.size(), (int)g_lshow.size(), g_lcp);
    {
        POINTL pe{};
        SendMessageW(g_hView, EM_POSFROMCHAR, (WPARAM)&pe, g_lcp + (long)vis.size() - 1);
        RECT cr3{}; GetClientRect(g_hView, &cr3);
        dbgLog(L"  line: lastx=%ld clientW=%ld", (long)pe.x, (long)(cr3.right - cr3.left));
    }
#endif
    if (vis == g_lshow) return;
    size_t n = vis.size() < g_lshow.size() ? vis.size() : g_lshow.size();
    size_t cp = 0;
    while (cp < n && vis[cp] == g_lshow[cp]) ++cp;
    long s = g_lcp + (long)cp;
    CHARRANGE cr{s, g_lcp + (long)g_lshow.size()};
#ifdef TERM_TRACE
    { CHARFORMAT2W q{}; q.cbSize = sizeof(q);
      SendMessageW(g_hView, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&q);
      FILE* f = fopen("term_trace.txt", "a");
      if (f) { fprintf(f, "cp=%zu old=%zu new=%zu erase=%zu yH=%d mask=%08lx\n", cp, g_lshow.size(),
                       vis.size(), g_lshow.size() - cp, (int)q.yHeight, (unsigned long)q.dwMask); fclose(f); } }
#endif
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&cr);
    // 插入点紧接在同一颜色的文本后面时，格式天然就是对的，不必再设一次
    if (g_tcolor != g_appliedColor || s != g_colorEnd) termApplyColor();
    viewReplace(vis.c_str() + cp);
    g_lshow = vis;
    g_colorEnd = s + (long)(vis.size() - cp);
    termTintTs(cp, vis);          // 新写进去的时间戳染灰（正文保持 g_tcolor）
    termCalibrate();              // 顺带校正字宽（只影响下一行排版，不动这一行）
    SendMessageW(g_hView, EM_SCROLLCARET, 0, 0);
}


// 一个字符占几列：CJK/全角算 2 列，其余 1 列（和终端习惯一致）

int termCellW(wchar_t c) {
    if (c < 0x1100) return 1;
    if (c <= 0x115F) return 2;                        // 谚文字母
    if (c >= 0x2E80 && c <= 0xA4CF) return 2;         // CJK 部首 / 汉字 / 假名
    if (c >= 0xAC00 && c <= 0xD7A3) return 2;         // 谚文音节
    if (c >= 0xF900 && c <= 0xFAFF) return 2;         // 兼容汉字
    if (c >= 0xFE30 && c <= 0xFE6F) return 2;         // 兼容形式
    if (c >= 0xFF00 && c <= 0xFF60) return 2;         // 全角
    if (c >= 0xFFE0 && c <= 0xFFE6) return 2;
    return 1;
}


// 字宽（px）。GDI 测出来的和 RichEdit 实际排版能差 20% 以上——RichEdit 的坐标是
// 它自己的排版单位，不是 GetTextExtentPoint32 那套 em 估算。所以先用 GDI 顶一下，
// 等 termCalibrate 从控件里量到真值就固定用它（只跟字号/DPI 有关，与窗口宽度无关，
// 窗口缩放时可以一直沿用，见 termColsUpdate 里的复用）。
static int s_cwReal = 0;

// 用控件自己的排版位置量真字宽：时间戳里第 1 和第 11 个字符必在同一视觉行，
// 它们的 x 差 ÷ 10 就是 RichEdit 眼里的一个字符宽。EM_POSFROMCHAR 传的是指针，
// 只有同进程调用才有效（外部脚本用 SendMessage 量出来永远是 0）。
void termCalibrate() {
    if (!g_hView || !g_termReady) return;
    if (g_lshow.size() < 12 || g_lcp < 0) return;
    POINTL a{}, b{};
    SendMessageW(g_hView, EM_POSFROMCHAR, (WPARAM)&a, g_lcp + 1);
    SendMessageW(g_hView, EM_POSFROMCHAR, (WPARAM)&b, g_lcp + 11);
    int d = (int)(b.x - a.x);
    if (d <= 10) return;                       // 跨行了或没量到：这次不算
    int cw = (d + 5) / 10;
    if (cw <= 0) return;
    RECT cr2{};
    GetClientRect(g_hView, &cr2);
    int w = cr2.right - cr2.left;
    if (w < 80) return;
    int cols = w / cw;
    if (cols > 12 && cols != g_termCols) {
        s_cwReal = cw;
        g_termCols = cols;
#ifdef SM_WRAP_TRACE
        dbgLog(L"calibrate: cw=%d w=%d -> cols=%d", cw, w, cols);
#endif
    }
}


// 实测终端可用列数：按当前字号造一个 Consolas 量字宽，再用客户区算列数。

void termColsUpdate() {
    // 控件还没建好就别覆盖：清成 0 会让 termBuildVis 走「不折行也不加时间戳」的兜底，
    // 首屏就出现一整条没有时间戳的长行（启动时 layout 早于控件创建，真机上见过）。
    if (!g_hView || !g_termReady) return;
    static int s_lastPt = 0;
    static int s_chReal = 0;                                          // 行高（px），量一次就缓存
    if (s_lastPt != g_fontPt) { s_cwReal = 0; s_chReal = 0; s_lastPt = g_fontPt; }   // 换字号：实测值作废
    int cw = s_cwReal;
    if (cw <= 0 || s_chReal <= 0) {
        HDC hdc = GetDC(g_hView);
        if (!hdc) return;
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        HFONT hf = CreateFontW(-MulDiv(g_fontPt, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        HGDIOBJ oldF = SelectObject(hdc, hf);
        SIZE sz{};
        GetTextExtentPoint32W(hdc, L"0123456789", 10, &sz);
        cw = sz.cx > 0 ? (sz.cx + 9) / 10 : 8;
        TEXTMETRICW tm{};
        GetTextMetricsW(hdc, &tm);
        s_chReal = tm.tmHeight > 0 ? (int)tm.tmHeight : 0;
        SelectObject(hdc, oldF);
        DeleteObject(hf);
        ReleaseDC(g_hView, hdc);
    }

    // 用客户区，别用 EM_GETRECT：RichEdit 的「格式化矩形」可能是上一次排版留下的
    // 旧宽度（实测 441px vs 真实客户区 354px，偏大 25%），按它算会多放十几列，
    // 结果每行都超宽、RichEdit 再软折一次，就冒出一条没有时间戳的尾巴。
    RECT cr2{};
    GetClientRect(g_hView, &cr2);
    int w = cr2.right - cr2.left;
    DWORD mg = (DWORD)SendMessageW(g_hView, EM_GETMARGINS, 0, 0);
    w -= (int)LOWORD(mg) + (int)HIWORD(mg);
    g_termCols = cw > 0 ? w / cw : 0;
    if (s_chReal > 0) {
        int rows = (cr2.bottom - cr2.top) / s_chReal;
        if (rows > 5) g_termRows = rows;
    }
#ifdef SM_WRAP_TRACE
    dbgLog(L"cols: pt=%d w=%d cw=%d ch=%d -> cols=%d rows=%d",
           g_fontPt, w, cw, s_chReal, g_termCols, g_termRows);
#endif
}


// 把终端真实尺寸同步给板子。
//
// 早先这里报的是死值 `stty cols 400`，意图是"让板子别自己折行，交给本工具折"。
// 但 ls / ps 是按 COLUMNS 排版的：报 400 列，它们就输出 400 宽的行，我们再折一次，
// 列宽全乱、长短不齐，看着就是一坨。
//
// 正确做法是报「屏幕上一行真正能放几个字符的正文」——也就是总列数减掉行首时间戳
// 占掉的 11 列（"[HH:MM:SS] "），再留 1 列余量。这样板子排出来的行正好塞满，
// 既不提前折、也不需要我们再折，多列输出天然对齐。

void syncTermSize() {
    if (!g_termReady || !g_hView) return;
    int cols = g_termCols > 0 ? g_termCols : 80;
    int reserve = g_showTs ? 12 : 0;
    int c = cols - reserve;
    if (c < 20) c = 80;
    int r = g_termRows > 5 ? g_termRows : 24;
    // 命令必须短到一行放得下：stty 生效前屏幕还是旧宽度，长命令会被折成两三行，
    // 折行点又会被下一条命令的 Ctrl-U 打断，屏幕上就是一串错位残渣。
    // 不用 export COLUMNS/LINES：ls / ps / df 都是直接 ioctl(TIOCGWINSZ) 读，
    // stty 改的就是那个 winsize 结构，环境变量只是给少数读 COLUMNS 的程序兜底。
    char buf[80];
    snprintf(buf, sizeof(buf), "\x15stty cols %d rows %d\r", c, r);
    termSendBytes(buf, C_TX, nullptr);
    g_sttyCols = g_termCols;
}


// 逻辑行 -> 渲染串：每条视觉行「时间戳 + 正文」，正文列宽 = 总列数 - 时间戳宽。
// 最后再留 1 列余量：中文字体链接的宽度不严格等于 2×ASCII，差一点点就会被
// RichEdit 二次软折行，出现一条没有时间戳的尾巴。
//
// 折行按「词」而不是按字符：ls 出来的 i2c-dev / ttyS0 这类名字被从中间切开后
// 很难读，看起来像两条不同的东西。所以以空白为断点整词换行，词尾的空格跟着
// 词走（续行不会以空格开头）；只有单个词本身就宽过一整行时，才退化成按字符硬切。

std::wstring termBuildVis(const std::wstring& r) {
    if (!g_showTs || g_curTs.empty()) return r;        // 不显示时间戳：交给 RichEdit 软折行
    int cols = g_termCols;
    if (cols < 24) return g_curTs + r;                 // 还不知道列宽/窗口太窄：不折，但时间戳必须有
    int limit = cols - (int)g_curTs.size() - 1;
    if (limit < 12) limit = 12;

    std::wstring out;
    out.reserve(g_curTs.size() + r.size() + (r.size() / (size_t)limit + 1) * (g_curTs.size() + 1));
    out += g_curTs;
    int col = 0;

    const size_t n = r.size();
    size_t i = 0;
    while (i < n) {
        // 切一段：非空白词 + 尾部空白（连续空白自成一段）
        size_t j = i;
        bool isSpace = (r[j] == L' ' || r[j] == L'\t');
        if (isSpace) {
            while (j < n && (r[j] == L' ' || r[j] == L'\t')) ++j;
        } else {
            while (j < n && r[j] != L' ' && r[j] != L'\t') ++j;
            while (j < n && (r[j] == L' ' || r[j] == L'\t')) ++j;
        }
        int wseg = 0;
        for (size_t k = i; k < j; ++k) wseg += termCellW(r[k]);

        if (isSpace && col == 0) { i = j; continue; }        // 续行不靠空白起头
        if (col > 0 && col + wseg > limit) {                 // 整段放不下：换行再放
            out += L'\r'; out += g_curTs; col = 0;
        }
        for (size_t k = i; k < j; ++k) {                     // 单段超宽时在这里硬切
            int cw = termCellW(r[k]);
            if (col + cw > limit) { out += L'\r'; out += g_curTs; col = 0; }
            out += r[k];
            col += cw;
        }
        i = j;
    }
    return out;
}

void termInsertBytes(const std::string& s) {
    if (s.empty()) return;
    size_t avail = g_lb.size() > g_lc ? g_lb.size() - g_lc : 0;
    size_t ov = s.size() < avail ? s.size() : avail;
    if (ov) g_lb.replace(g_lc, ov, s.c_str(), ov);
    if (s.size() > ov) g_lb.insert(g_lc + ov, s.c_str() + ov, s.size() - ov);
    g_lc += s.size();
    termSyncLine();
}


// 本地回显：把刚发出去的字节立刻画到屏幕上，不等板子返回。
// 记下 g_echoQ，板子的回显字节随后到达时按内容比对丢弃（见 termFeed）。
//
// 例外：密码/口令提示行不回显。板子在这类提示下会关掉 echo，我们若抢先画出来，
// 密码就明文留在屏幕上了（真正靠回显时它本来就是不显示的）。

bool echoSensitive() {
    if (g_lshow.size() < 6) return false;
    size_t from = g_lshow.size() > 24 ? g_lshow.size() - 24 : 0;
    std::wstring tail = g_lshow.substr(from);
    for (auto& c : tail) c = towlower(c);
    return tail.find(L"assword") != std::wstring::npos ||
           tail.find(L"密码")     != std::wstring::npos ||
           tail.find(L"口令")     != std::wstring::npos;
}

void termLocalEcho(const std::string& b) {
    if (!g_localEcho || b.empty() || echoSensitive()) return;
    g_echoQ += b;
    g_echoStamp = GetTickCount();
    termInsertBytes(b);
}


// 换行：把当前行定格

void termCommitLine() {
    long end = g_lcp + (long)g_lshow.size();
    CHARRANGE cr{end, end};
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&cr);
    termApplyColor();
    viewReplace(L"\r");
    g_lcp = end + 1;
    g_colorEnd = g_lcp;
    g_lb.clear();
    g_lc = 0;
    g_bsPend = 0;
    g_bsSkip = 0;
    g_echoQ.clear();
    g_tsNeed = true;                    // 新行开始，下一次渲染补时间戳
    g_curTs.clear();                    // 时间戳必须换新的，否则下行还用上一行的
    g_lshow.clear();
    termColorReset();                   // 新行的插入点格式要重新设一次
    SendMessageW(g_hView, EM_SCROLLCARET, 0, 0);
}


// 板子回显的退格：一次删一个**字节**（中文要三次才删完，由自适应补发处理）

void termEchoBackspace() {
    if (g_lc == 0 || g_lb.empty()) return;
    g_lb.erase(g_lc - 1, 1);
    g_lc--;
    termSyncLine();
}

void termEraseInLine(int mode) {
    if (mode == 2) { g_lb.clear(); g_lc = 0; }
    else if (mode == 1) { g_lb.erase(0, g_lc < g_lb.size() ? g_lc : g_lb.size()); g_lc = 0; }
    else if (g_lc < g_lb.size()) g_lb.resize(g_lc);
    termSyncLine();
}

void termClearAll() {
    if (!g_hView) return;
    g_selfEdit = true;
    SetWindowTextW(g_hView, L"");
    g_selfEdit = false;
    g_lb.clear();
    g_lc = 0;
    g_bsPend = 0;
    g_bsSkip = 0;
    g_echoQ.clear();
    g_lshow.clear();
    g_lcp = 0;
    g_tsNeed = true;
    g_curTs.clear();
    termColorReset();
    { std::lock_guard<std::mutex> lk(g_textMtx); g_allText.clear(); }
}

void termCsi(const std::wstring& raw, wchar_t fin) {
    std::wstring p = raw;
    if (!p.empty() && (p[0] == L'?' || p[0] == L'>' || p[0] == L'<' || p[0] == L'=')) p.erase(0, 1);
    int a = 0;
    if (!p.empty()) {
        size_t sc = p.find(L';');
        a = _wtoi(sc == std::wstring::npos ? p.c_str() : p.substr(0, sc).c_str());
    }
    switch (fin) {
        case L'K': termEraseInLine(a); break;
        case L'J':
            if (a == 2 || a == 3) termClearAll();
            else if (a == 0) { if (g_lc < g_lb.size()) g_lb.resize(g_lc); termSyncLine(); }
            break;
        case L'P': {                                    // 删掉光标处 a 个字节（readline 重绘常用）
            if (g_lc < g_lb.size()) {
                size_t n = (a > 0) ? (size_t)a : 1;
                if (n > g_lb.size() - g_lc) n = g_lb.size() - g_lc;
                g_lb.erase(g_lc, n);
                termSyncLine();
            }
            break;
        }
        case L'@': {                                    // 插入 a 个空格
            size_t n = (a > 0) ? (size_t)a : 1;
            g_lb.insert(g_lc, std::string(n, ' '));
            termSyncLine();
            break;
        }
        case L'C': g_lc += (a > 0 ? (size_t)a : 1); if (g_lc > g_lb.size()) g_lc = g_lb.size(); break;
        case L'D': { size_t n = (a > 0 ? (size_t)a : 1); g_lc = (g_lc > n) ? g_lc - n : 0; break; }
        case L'G': g_lc = (a > 1) ? (size_t)(a - 1) : 0; if (g_lc > g_lb.size()) g_lc = g_lb.size(); break;
        default: break;                                 // m(颜色) / h / l / H 等忽略
    }
}


// 行末字符占几个字节（1..4）

int trailingCharBytes() {
    if (g_lb.empty()) return 1;
    size_t i = g_lb.size();
    size_t cont = 0;
    while (i > 0 && ((unsigned char)g_lb[i - 1] & 0xC0) == 0x80 && cont < 3) { i--; cont++; }
    if (i == 0) return (int)(cont + 1 > 4 ? 4 : cont + 1);
    unsigned char lead = (unsigned char)g_lb[i - 1];
    int need = 1;
    if ((lead >> 5) == 0x6) need = 2;
    else if ((lead >> 4) == 0xE) need = 3;
    else if ((lead >> 3) == 0x1E) need = 4;
    return need;
}

void termFeed(const std::string& bytes) {
    if (!g_hView || bytes.empty()) return;
    if (!g_termReady) {                                 // 退化控件：过滤后直接追加
        std::wstring t;
        for (size_t i = 0; i < bytes.size(); i++) {
            unsigned char c = (unsigned char)bytes[i];
            if (c == 0x08) { if (!t.empty() && t.back() != L'\n') t.pop_back(); }
            else if (c == '\r') {}
            else if (c == '\n') t += L"\r\n";
            else if (c >= 0x20 && c != 0x7F && c < 0x80) t.push_back((wchar_t)c);
        }
        appendRun(t, C_RX);
        return;
    }
    std::string run;
    auto flush = [&]() {
        if (run.empty()) return;
        // 时间戳不再单独插：termSyncLine 渲染整行时按视觉行补（见 termBuildVis）
        termInsertBytes(run);
        run.clear();
    };
    for (size_t i = 0; i < bytes.size(); i++) {
        unsigned char c = (unsigned char)bytes[i];

        // 退格回显的两种形态（实测）：
        //   删 ASCII  -> 08 20 08
        //   删汉字    -> 08 ESC[K   （一个字节一组，三组删掉一个字）
        // 本地已经删过内容，这里把配套的 0x08 / 空格吞掉，避免二次删除
        if (g_bsSkip == 1) {
            if (c == 0x20) { g_bsSkip = 2; continue; }
            g_bsSkip = 0;
        } else if (g_bsSkip == 2) {
            g_bsSkip = 0;
            if (c == 0x08) continue;
        }

        if (g_esc == 1) {
            if (c == '[') { g_esc = 2; g_csi.clear(); }
            else if (c == ']') g_esc = 3;
            else if (c == '(' || c == ')' || c == '#' || c == '%') g_esc = 4;
            else g_esc = 0;
            continue;
        }
        if (g_esc == 2) {
            if (c >= 0x40 && c <= 0x7E) { flush(); termCsi(g_csi, (wchar_t)c); g_esc = 0; }
            else if (g_csi.size() < 32) g_csi.push_back((wchar_t)c);
            continue;
        }
        if (g_esc == 3) { if (c == 0x07) g_esc = 0; else if (c == 0x1B) g_esc = 1; continue; }
        if (g_esc == 4) { g_esc = 0; continue; }

        if (c == 0x1B)        { flush(); g_esc = 1; g_echoQ.clear(); }
        else if (c == '\r')   { flush(); g_lc = 0; g_echoQ.clear(); }
        else if (c == '\n')   { flush(); termCommitLine(); g_echoQ.clear(); }
        else if (c == 0x08)   { flush(); if (g_bsPend > 0) { g_bsPend--; g_bsSkip = 1; } else termEchoBackspace(); }
        else if (c == '\t')   { flush(); termInsertBytes(std::string(8 - (g_lc % 8), ' ')); }
        else if (c < 0x20 || c == 0x7F) { /* 其余控制字符丢弃，不再显示成方块 */ }
        else {
            // 这些字节如果是我们自己刚敲下去、已经在屏幕上画过的，就丢掉。
            // 按内容比对：对不上说明不是回显（板子关了 echo，或者这边已经开始
            // 吐命令结果），立刻停手，否则后面的输出会被吃掉开头几个字节。
            if (!g_echoQ.empty()) {
                if (g_echoQ[0] == (char)c && GetTickCount() - g_echoStamp <= 400) {
                    g_echoQ.erase(0, 1);       // 本地已显示，回显丢弃
                    continue;
                }
                g_echoQ.clear();
            }
            run.push_back((char)c);
        }
    }
    flush();
}

void appendRun(const std::wstring& text, COLORREF color) {
    if (!g_hView || text.empty()) return;
    // SSH 会话的「画面」在终端控件里，本地提示也写进去
    if (g_cur && isTermSess(g_cur)) {
        cacheText(text);
        if (g_cur->term) g_cur->term->writeText(text);
        return;
    }
    // 当前行还没收尾（板子刚打出的提示符就停在行尾）时先把它定格，
    // 否则 "◀ [AGENT] xxx" 会粘在你输入的内容后面，看起来像命令被改写了
    if (!g_lshow.empty()) { termAnchorCheck(); termCommitLine(); cacheText(L"\r"); }
    cacheText(text);
    CHARRANGE crEnd{-1, -1};                            // -1 = 文末
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&crEnd);
    COLORREF oldTc = g_tcolor;
    g_tcolor = color;
    termApplyColor();
    viewReplace(text.c_str());
    g_tcolor = oldTc;                                   // 恢复：板子后续输出还得用正常色
    CHARRANGE crCur{};
    SendMessageW(g_hView, EM_EXGETSEL, 0, (LPARAM)&crCur);
    g_lcp = crCur.cpMax;                                // 新行起点
    g_lb.clear();
    g_lc = 0;
    g_echoQ.clear();                                     // 本地提示行插在中间，回显账本作废
    g_lshow.clear();
    SendMessageW(g_hView, EM_SCROLLCARET, 0, 0);
}


// 串口收到的字节 -> 终端

void showRx(const std::string& bytes) {
    if (g_cur) g_cur->rx += (long long)bytes.size();

    // 探测期间/判定无响应之后，只要真收到字节就是「设备在线」——这是唯一可信的证据，
    // 比打开串口成功有意义得多。从 SILENT 翻回来时只刷状态栏，终端不打印提示。
    if (g_cur && !isTermSess(g_cur) && g_devState != DEV_ALIVE) {
        bool wasSilent = (g_devState == DEV_SILENT);
        g_devState = DEV_ALIVE;
        if (wasSilent) {
            if (g_hwnd) KillTimer(g_hwnd, IDT_PROBE);
            updateStatus();
        }
    }

    termFeed(bytes);                    // 时间戳由 termFeed 在每行行首插入

    // 存档用的纯文本
    std::string keep;
    keep.reserve(bytes.size());
    for (size_t i = 0; i < bytes.size(); i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == 0x08) { if (!keep.empty() && keep.back() != '\n') keep.pop_back(); }
        else if (c == '\r') {}
        else if (c == '\n') keep.push_back('\n');
        else if (c >= 0x20 && c != 0x7F) keep.push_back((char)c);
    }
    cacheText(rxDecode(keep));
    logLine(L"RX", bytes);
}


// 折叠把手通用绘制：浅灰竖条 + 边框 + 居中箭头（左右两侧栏共用）

DWORD CALLBACK rtfOutCb(DWORD_PTR cookie, LPBYTE pb, LONG cb, LONG* pcb) {
    ((std::string*)cookie)->append((const char*)pb, (size_t)cb);
    *pcb = cb;
    return 0;
}

DWORD CALLBACK rtfInCb(DWORD_PTR cookie, LPBYTE pb, LONG cb, LONG* pcb) {
    std::string* s = (std::string*)cookie;
    size_t n = s->size() < (size_t)cb ? s->size() : (size_t)cb;
    if (n) memcpy(pb, s->data(), n);
    s->erase(0, n);
    *pcb = (LONG)n;
    return 0;
}

void viewSaveRtf(std::string& out) {
    out.clear();
    if (!g_hView) return;
    if (!g_termReady) {                       // 普通 EDIT：退化成纯文本
        out = wideToUtf8(viewText());
        return;
    }
    EDITSTREAM es{};
    es.dwCookie = (DWORD_PTR)&out;
    es.pfnCallback = rtfOutCb;
    SendMessageW(g_hView, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
}

void viewLoadRtf(const std::string& in) {
    if (!g_hView || in.empty()) return;
    g_selfEdit = true;
    if (!g_termReady) {
        SetWindowTextW(g_hView, utf8ToWide(in).c_str());
    } else {
        std::string buf = in;
        EDITSTREAM es{};
        es.dwCookie = (DWORD_PTR)&buf;
        es.pfnCallback = rtfInCb;
        SendMessageW(g_hView, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    }
    g_selfEdit = false;
}


// 把某个会话的画面装进视图（含未提交行状态）

int linePop() {
    if (g_line.empty()) return 0;
    int pend = g_pendingKeys.load();
    if (pend > 0) g_pendingKeys = pend - 1;
    while (!g_lineLen.empty()) {
        unsigned char n = g_lineLen.back();
        g_lineLen.pop_back();
        g_line.pop_back();
        if (n) return (int)n;
    }
    return 0;
}


// 敲一个字符（不本地回显，靠板子回显）

void termTypeChar(wchar_t ch) {
    if (ch == L'\r') {
        termSendBytes("\r", C_TX, nullptr);
        if (g_hiSur) { lineRecord(std::wstring(1, g_hiSur)); g_hiSur = 0; }   // 落单的代理对
        if (!g_line.empty()) {
            if (g_hist.empty() || g_hist.back() != g_line) g_hist.push_back(g_line);
            if (g_hist.size() > 200) g_hist.erase(g_hist.begin());
        }
        lineReset();
        g_histPos = -1;
        return;
    }
    if (ch == 8) {                       // Backspace：按本地记录精确算字节数，一次发够
        int n = linePop();               // 汉字按实际编码长度回退（UTF-8 3 字节 / GBK 2 字节）
        bool local = (n > 0);            // 这个字是我们自己敲进来的（本地有记录）
        if (!local) n = trailingCharBytes();   // AI 下发的内容本地没记录，按回显行算
        if (n < 1) n = 1;
        termSendBytes(std::string((size_t)n, '\x7f'), C_TX, nullptr);
        // 本地立刻删掉，别等板子回显——否则连按退格时看着像"没反应"
        if (local && g_lc >= (size_t)n && g_lb.size() >= (size_t)n) {
            g_lb.erase(g_lc - n, n);
            g_lc -= n;
            g_bsPend += n;                 // 板子随后会回 n 组 "\b \b"，吞掉其中 n 个 0x08
            termSyncLine();
        }
        return;
    }
    if (ch == 3) {                       // Ctrl+C
        termSendBytes("\x03", C_TX, nullptr);
        lineReset();
        return;
    }
    if (ch == 9) {                       // Tab
        termSendBytes("\t", C_TX, nullptr);
        g_line.push_back(L'\t');
        g_lineLen.push_back(1);
        g_pendingKeys++;
        return;
    }
    if (ch < 32) return;
    // 代理对（emoji 等）要凑齐两半再编码，分开编码会变成两个替换符
    if ((unsigned)ch >= 0xD800 && (unsigned)ch <= 0xDBFF) { g_hiSur = ch; return; }
    std::wstring unit;
    if (g_hiSur && (unsigned)ch >= 0xDC00 && (unsigned)ch <= 0xDFFF) {
        unit.push_back(g_hiSur);
        g_hiSur = 0;
    }
    unit.push_back(ch);
    std::string b = txEncode(unit);
    if (b.empty()) return;
    termLocalEcho(b);                    // 先画到屏幕上，键盘手感不再受链路延迟影响
    termSendBytes(b, C_TX, nullptr);
    lineRecord(unit);
}


// ↑ / ↓ 调历史：先用退格抹掉当前行，再把历史命令发出去（板子会回显）

void termHistory(int dir) {
    if (g_hist.empty()) return;
    if (g_histPos == -1) {
        g_histTmp = g_line;
        g_histPos = (int)g_hist.size();
    }
    int np = g_histPos + dir;
    if (np < 0) np = 0;
    if (np > (int)g_hist.size()) np = (int)g_hist.size();
    g_histPos = np;

    std::wstring target = (np >= (int)g_hist.size()) ? g_histTmp : g_hist[np];
    // 按**字节**数发退格：中文 UTF-8 一个字 3 字节 / GBK 2 字节，行编辑按字节删
    if (!g_line.empty()) {
        std::string cur = txEncode(g_line);
        termSendBytes(std::string(cur.size(), '\x7f'), C_TX, nullptr);
    }
    if (!target.empty()) termSendBytes(txEncode(target), C_TX, nullptr);
    lineReset();
    lineRecord(target);
}


// 取输入法组合/上屏字符串（拼音, GCS_RESULTSTR = 最终上屏的汉字）

std::wstring immStr(HWND h, DWORD flag) {
    std::wstring s;
    HIMC himc = ImmGetContext(h);
    if (!himc) return s;
    LONG n = ImmGetCompositionStringW(himc, flag, nullptr, 0);
    if (n > 0) {
        std::wstring buf((size_t)(n / 2) + 2, L'\0');
        LONG got = ImmGetCompositionStringW(himc, flag, buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)));
        if (got > 0) s.assign(buf.data(), (size_t)(got / 2));
    }
    ImmReleaseContext(h, himc);
    return s;
}


// 输出框子类化：直接把击键转成串口字节
// 注意：Enter/Backspace/Tab 会由 TranslateMessage 另外生成一个 WM_CHAR，
// 若不吞掉就会双发（表现为一次回车跑两条命令）。用 g_swallow 精确吞掉那一个。

LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
        case WM_CHAR:
        case WM_IME_CHAR: {   // 输入法（即使处于英文/半角模式）送字走 WM_IME_CHAR，
                              // 不拦的话字符会被 RichEdit 插进显示区：屏上有字、串口没发出
            wchar_t ch = (wchar_t)w;
            if (ch >= 0x80) dbgLog(L"WM_%sCHAR ch=%04X echo=%d", m == WM_IME_CHAR ? L"IME_" : L"", ch, (int)!g_imeEcho.empty());
            if (!g_imeEcho.empty()) {                   // 上屏内容已转发，这里是重复投递
                if (ch == g_imeEcho.front()) { g_imeEcho.erase(0, 1); return 0; }
                g_imeEcho.clear();                      // 内容对不上，说明是新输入
            }
            if (g_swallow && ch == g_swallow) { g_swallow = 0; return 0; }
            termTypeChar(ch);
            return 0;
        }

        case WM_IME_STARTCOMPOSITION:
            // 拼音过程中 RichEdit 会把拼写串内联插进来（el. "nihao"）。
            // 上屏时这块会被替换成汉字，必须整段撤掉，否则屏上留下幻影字：
            // 看着有字、串口没发过，退格自然删不掉。这里先记住起点。
            g_imeBase = viewTextLen(h);
            dbgLog(L"IME_STARTCOMPOSITION base=%ld", g_imeBase);
            break;

        case WM_IME_COMPOSITION:
            dbgLog(L"IME_COMPOSITION l=%08X result=%d", (unsigned)l, (l & GCS_RESULTSTR) ? 1 : 0);
            if (l & GCS_RESULTSTR) {
                // 上屏结果以输入法给的为准（不看控件相差了几个字：
                // 组合串"nihao"→结果"你好"，控件长度是变短的，按增量算会漏）
                std::wstring res = immStr(h, GCS_RESULTSTR);
                g_selfEdit = true;                          // 组合串/结果的内联改动算我们自己的
                LRESULT r = DefSubclassProc(h, m, w, l);   // 让 RichEdit 完成 组合串→结果 的替换
                g_selfEdit = false;

                long after = viewTextLen(h);
                CHARRANGE cr{};
                SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
                long end = (cr.cpMax > 0 && cr.cpMax <= after) ? cr.cpMax : after;

                // 结果串的落点：优先光标前 res 长度的位置，其次整个组合区
                long cand[2] = { end - (long)res.size(), g_imeBase >= 0 ? g_imeBase : -1 };
                long start = -1;
                if (!res.empty()) {
                    for (long c : cand) {
                        if (c >= 0 && c + (long)res.size() <= after && viewRange(h, c, c + (long)res.size()) == res) {
                            start = c;
                            break;
                        }
                    }
                    if (start < 0 && cand[0] >= 0 && cand[0] + (long)res.size() <= after) start = cand[0];
                }
                if (start >= 0) {
                    viewDeleteRange(h, start, start + (long)res.size());   // 撤掉本地插入
                    for (wchar_t ch : res) termTypeChar(ch);               // 真正发给串口
                    g_imeEcho = res;                                       // 吞掉可能的重复投递
                }
                dbgLog(L"IME_RESULT res=[%ls] start=%ld", res.c_str(), start);
                g_imeBase = -1;
                termColorReset();      // 输入法写进文档的字体不能被后面的输出继承
                return r;
            }
            break;

        case WM_IME_ENDCOMPOSITION:
            dbgLog(L"IME_ENDCOMPOSITION");
            g_imeEcho.clear();
            g_imeBase = -1;
            break;

        case WM_KEYDOWN:
            switch (w) {
                case VK_RETURN: g_swallow = L'\r'; termTypeChar(L'\r'); return 0;
                case VK_BACK:   g_swallow = (wchar_t)8; termTypeChar((wchar_t)8); return 0;
                case VK_TAB:    g_swallow = (wchar_t)9; termTypeChar((wchar_t)9); return 0;
                case VK_UP:     termHistory(-1); return 0;
                case VK_DOWN:   termHistory(1); return 0;
                case VK_ESCAPE: return 0;
                case VK_DELETE: return 0;
                case 'C':
                    if (GetKeyState(VK_CONTROL) & 0x8000) {
                        g_swallow = (wchar_t)3;
                        if (GetKeyState(VK_SHIFT) & 0x8000) {
                            CHARRANGE cr{};
                            SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
                            if (cr.cpMax > cr.cpMin) SendMessageW(h, WM_COPY, 0, 0);
                            g_swallow = 0;
                        } else {
                            termTypeChar((wchar_t)3);   // 如同真终端：Ctrl+C 中断
                        }
                        return 0;
                    }
                    break;
                case 'V':
                    if (GetKeyState(VK_CONTROL) & 0x8000) {
                        if (OpenClipboard(h)) {
                            HANDLE hd = GetClipboardData(CF_UNICODETEXT);
                            if (hd) {
                                auto* txt = (wchar_t*)GlobalLock(hd);
                                if (txt) {
                                    std::wstring s(txt);
                                    GlobalUnlock(hd);
                                    for (auto& ch : s) if (ch == L'\r' || ch == L'\n') ch = L' ';
                                    termSendBytes(txEncode(s), C_TX, nullptr);
                                    lineRecord(s);
                                }
                            }
                            CloseClipboard();
                        }
                        g_swallow = (wchar_t)22;
                        return 0;
                    }
                    break;
                case 'A':
                    if (GetKeyState(VK_CONTROL) & 0x8000) {
                        SendMessageW(h, EM_SETSEL, 0, -1);
                        return 0;
                    }
                    break;
            }
            // 其余 Ctrl+字母 原样透传给板子（Ctrl+U 清行、Ctrl+W 删词、Ctrl+L 清屏、
            // Ctrl+R 搜索历史等），此前这些键全被丢掉，像真终端那样发控制码
            if (w >= 'A' && w <= 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                char code = (char)(w - 'A' + 1);
                g_swallow = (wchar_t)(unsigned char)code;
                if (code == 21) lineReset();         // Ctrl+U：板子行已清空
                termSendBytes(std::string(1, code), C_TX, nullptr);
                return 0;
            }
            break;

        case WM_CONTEXTMENU:
            showViewMenu(h);          // 右键：复制/粘贴/全选/清屏 + 编码、时间戳、日志等功能设置
            return 0;

        case WM_SETFOCUS: {
            // 切回来时插入点可能还停在别处，先让 RichEdit 处理完再归位
            LRESULT r = DefSubclassProc(h, m, w, l);
            termEnsureCaret();
            return r;
        }

        case WM_LBUTTONUP: {
            LRESULT r = DefSubclassProc(h, m, w, l);
            // 单击（没有拖出选区）只会在 RichEdit 里挪走插入点，终端里没意义，退回行尾；
            // 拖选/双击选词保持不动，那是为了复制
            termEnsureCaret();
            return r;
        }

        case WM_PASTE: case WM_CUT: case WM_CLEAR: case WM_UNDO:
            return 0;
    }
    g_selfEdit = true;      // 默认处理里可能发生的文本改动（如传统 IMM 组合串）不算外来
    LRESULT r = DefSubclassProc(h, m, w, l);
    g_selfEdit = false;
    return r;
}


// 输入法是否正处于拼写组合中（TSF 搜狗/微软拼音 + 传统 IMM 都查）

bool imeComposing() {
    if (g_hView) {
        HIMC himc = ImmGetContext(g_hView);
        if (himc) {
            LONG n = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
            ImmReleaseContext(g_hView, himc);
            if (n > 0) return true;
        }
    }
    static ITfThreadMgr* s_tsf = nullptr;
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&s_tsf));
    }
    if (!s_tsf) return false;
    ITfDocumentMgr* dm = nullptr;
    if (FAILED(s_tsf->GetFocus(&dm)) || !dm) return false;
    ITfContext* ctx = nullptr;
    bool composing = false;
    if (SUCCEEDED(dm->GetTop(&ctx)) && ctx) {
        ITfContextComposition* cc = nullptr;
        if (SUCCEEDED(ctx->QueryInterface(IID_ITfContextComposition, (void**)&cc)) && cc) {
            IEnumITfCompositionView* en = nullptr;
            if (SUCCEEDED(cc->EnumCompositions(&en)) && en) {
                ITfCompositionView* v = nullptr;
                ULONG n = 0;
                if (en->Next(1, &v, &n) == S_OK && n > 0) { v->Release(); composing = true; }
                en->Release();
            }
            cc->Release();
        }
        ctx->Release();
    }
    dm->Release();
    return composing;
}


// EN_CHANGE 防抖到期：把控件里多出来的外来文本（TSF 上屏的汉字等）撤掉并转发给串口

void syncForeignText() {
    if (!g_hView || g_selfEdit) return;
    if (GetTickCount() < g_foreignGuard) {     // 刚切换过会话：整屏变化不算输入
        g_expect = viewText();
        return;
    }
    std::wstring cur = viewText();
    if (cur == g_expect) return;
    size_t p = 0, n1 = cur.size(), n2 = g_expect.size();
    while (p < n1 && p < n2 && cur[p] == g_expect[p]) p++;
    size_t s = 0;
    while (s < n1 - p && s < n2 - p && cur[n1 - 1 - s] == g_expect[n2 - 1 - s]) s++;
    std::wstring ins = cur.substr(p, n1 - p - s);          // 外来插入的文本
    dbgLog(L"syncForeign p=%zu ins=[%ls]", p, ins.c_str());
    if (ins.size() > 4) {                                  // 整段变化是我们自己刷屏/补放，不是输入
        g_expect = cur;
        return;
    }
    if (n1 - s > p) viewDeleteRange(g_hView, (long)p, (long)(n1 - s));
    g_expect = viewText();
    if (!ins.empty()) {
        for (wchar_t ch : ins) termTypeChar(ch);           // 真正发给串口
        g_imeEcho = ins;                                   // 吞掉可能的重复投递
    }
    // 上面删字会把插入点塌在删除处（屏幕中间），这里退回行尾
    termEnsureCaret();
    // 被删掉的那段是输入法塞进来的，它的字体比 Consolas 大；
    // 删完插入点格式可能还留着那份大字体，强制下一笔重新压实格式
    termColorReset();
}


// ---------------- 字体 ----------------

void applyFont() {
    if (!g_hView) return;
    for (auto& sp : g_sess)
        if (sp->term) sp->term->setFontPt(g_fontPt);
    if (g_termReady) {
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR;
        cf.yHeight = g_fontPt * 20;
        cf.crTextColor = C_RX;
        cf.bCharSet = DEFAULT_CHARSET;
        lstrcpynW(cf.szFaceName, L"Consolas", LF_FACESIZE);
        SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
        SendMessageW(g_hView, EM_SETBKGNDCOLOR, 0, (LPARAM)C_BG);
    } else {
        HDC dc = GetDC(g_hView);
        int hgt = -MulDiv(g_fontPt, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(g_hView, dc);
        HFONT f = CreateFontW(hgt, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH, L"Consolas");
        SendMessageW(g_hView, WM_SETFONT, (WPARAM)f, TRUE);
    }
}


// ---------------- 菜单 ----------------

void saveLog(HWND hwnd) {
    wchar_t path[MAX_PATH] = L"serial-log.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Log (*.log;*.txt)\0*.log;*.txt\0All (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    FILE* f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) return;
    {
        std::lock_guard<std::mutex> lk(g_textMtx);
        fputws(g_allText.c_str(), f);
    }
    fclose(f);
    appendRun(L"— 日志已保存 —\r\n", C_SYS);
}


// ---------------- 输出框右键菜单 ----------------

void viewCopySel() {
    if (!g_hView) return;
    CHARRANGE cr{};
    SendMessageW(g_hView, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMax <= cr.cpMin) return;
    std::wstring s = viewRange(g_hView, cr.cpMin, cr.cpMax);
    if (s.empty() || !OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(g)) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(g);
            SetClipboardData(CF_UNICODETEXT, g);
        }
    }
    CloseClipboard();
}


// 剪贴板内容当命令发下去（换行压成空格），和 Ctrl+V 一致

void viewPasteToPort() {
    if (!OpenClipboard(g_hwnd)) return;
    HANDLE hd = GetClipboardData(CF_UNICODETEXT);
    if (hd) {
        if (auto* txt = (wchar_t*)GlobalLock(hd)) {
            std::wstring s(txt);
            GlobalUnlock(hd);
            for (auto& ch : s) if (ch == L'\r' || ch == L'\n') ch = L' ';
            if (!s.empty()) { termSendBytes(txEncode(s), C_TX, nullptr); lineRecord(s); }
        }
    }
    CloseClipboard();
}

void showViewMenu(HWND h) {
    HMENU m = CreatePopupMenu();
    HMENU mEnc = CreatePopupMenu();
    mAddMenu(mEnc, MF_STRING | (g_rxEnc == 0 ? MF_CHECKED : 0), IDM_ENC_BASE + 0, L"自动（UTF-8 优先，失败退 GBK）");
    mAddMenu(mEnc, MF_STRING | (g_rxEnc == 1 ? MF_CHECKED : 0), IDM_ENC_BASE + 1, L"UTF-8");
    mAddMenu(mEnc, MF_STRING | (g_rxEnc == 2 ? MF_CHECKED : 0), IDM_ENC_BASE + 2, L"文本 ANSI (GBK)");

    CHARRANGE cr{};
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
    bool hasSel = cr.cpMax > cr.cpMin;

    mAddMenu(m, MF_STRING | (hasSel ? 0 : MF_GRAYED), IDM_CTX_COPY, L"复制\tCtrl+C");
    mAddMenu(m, MF_STRING, IDM_CTX_PASTE, L"粘贴到串口\tCtrl+V");
    mAddMenu(m, MF_STRING, IDM_CTX_ALL, L"全选\tCtrl+A");
    mAddMenu(m, MF_STRING, IDM_VIEW_CLEAR, L"清屏\tCtrl+L");
    mAddMenu(m, MF_SEPARATOR, 0, nullptr);
    mAddMenu(m, MF_POPUP, (UINT_PTR)mEnc, L"串口编码");
    mAddMenu(m, MF_STRING | (g_showTs ? MF_CHECKED : 0), IDM_VIEW_TS, L"显示时间戳");
    mAddMenu(m, MF_STRING | (g_showAgent ? MF_CHECKED : 0), IDM_VIEW_AGENT, L"显示接管命令");
    mAddMenu(m, MF_STRING | (g_localEcho ? MF_CHECKED : 0), IDM_LOCAL_ECHO, L"按键即时显示（本地回显）");
    mAddMenu(m, MF_STRING | (g_sideOn ? MF_CHECKED : 0), IDM_SIDE_TOGGLE, L"显示会话列表");
    mAddMenu(m, MF_STRING | (g_keysOn ? MF_CHECKED : 0), IDM_KEYS_TOGGLE, L"显示快捷命令栏");
    mAddMenu(m, MF_SEPARATOR, 0, nullptr);
    mAddMenu(m, MF_STRING | (g_logSave ? MF_CHECKED : 0), IDM_LOG_SAVE, L"保存串口日志");
    mAddMenu(m, MF_STRING, IDM_LOG_DIR, L"设置日志目录…");
    mAddMenu(m, MF_STRING, IDM_LOG_OPEN, L"打开日志目录");

    POINT pt;
    GetCursorPos(&pt);
    int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN,
                                 pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    if (!id) return;
    if (id == IDM_CTX_COPY)  { viewCopySel(); return; }
    if (id == IDM_CTX_PASTE) { viewPasteToPort(); return; }
    if (id == IDM_CTX_ALL)   { SendMessageW(h, EM_SETSEL, 0, -1); return; }
    SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)id, 0);   // 其余走主窗口统一处理
}


// ---------------- 主窗口 ----------------

long viewTextLen(HWND h) { return (long)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0); }

void         termColorReset() { g_appliedColor = 0xFFFFFFFF; g_colorEnd = -1; }

void lineReset() { g_line.clear(); g_lineLen.clear(); g_hiSur = 0; g_pendingKeys = 0; }

void lineRecord(const std::wstring& s) {   // 记录一串已发出的字符（含代理对合并）
    size_t i = 0;
    while (i < s.size()) {
        std::wstring unit(1, s[i]);
        if ((unsigned)s[i] >= 0xD800 && (unsigned)s[i] <= 0xDBFF && i + 1 < s.size() &&
            (unsigned)s[i + 1] >= 0xDC00 && (unsigned)s[i + 1] <= 0xDFFF) {
            unit.push_back(s[i + 1]);
            i++;
        }
        std::string b = txEncode(unit);
        if (b.empty()) { i++; continue; }
        g_line += unit;
        g_lineLen.push_back((unsigned char)b.size());
        if (unit.size() == 2) g_lineLen.push_back((unsigned char)0);   // 低半代理不占字节
        g_pendingKeys++;                                               // 未提交的字符 +1
        i++;
    }
}
