// ui_keys.cpp - 右侧快捷命令栏：按钮/文本两种模式、增删改、导入导出

#include "sm.h"
void saveShortcuts() {
    FILE* f = _wfopen((exeDir() + L"\\shortcuts.ini").c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    for (size_t i = 0; i < g_keys.size(); i++)
        fwprintf(f, L"F%d=%ls\n", (int)i + 1, g_keys[i].c_str());
    fclose(f);
}

void loadShortcuts() {
    const wchar_t* def[8] = {L"ls -l", L"dmesg | tail -20", L"ifconfig", L"ps",
                             L"free -m", L"df -h", L"ps | head", L"uname -a"};
    std::vector<std::pair<int, std::wstring>> got;
    FILE* f = _wfopen((exeDir() + L"\\shortcuts.ini").c_str(), L"r, ccs=UTF-8");
    bool fileExists = (f != nullptr);
    if (f) {
        wchar_t line[4096];
        while (fgetws(line, 4096, f)) {
            std::wstring s = trimW(line);
            size_t eq = s.find(L'=');
            if (eq == std::wstring::npos || eq < 2) continue;
            if (s[0] != L'F' && s[0] != L'f') continue;
            int n = _wtoi(s.c_str() + 1);
            // 编号只用于排序，不要设 99 这种人为上限：用户导入的文档常常几百行，
            // 截断后重启就悄悄丢了后半段（saveShortcuts 会按实际条数写 F1..FN）。
            if (n < 1) continue;
            got.push_back({n, s.substr(eq + 1)});
        }
        fclose(f);
    }
    std::sort(got.begin(), got.end(),
              [](const std::pair<int, std::wstring>& a, const std::pair<int, std::wstring>& b) {
                  return a.first < b.first;
              });
    g_keys.clear();
    if (!fileExists) {
        // 只有首次运行（配置文件还不存在）才给一套默认命令。
        // 用户清空列表后落盘的是"存在但没有有效条目"的文件，这时必须保持空，
        // 否则每次启动都会把默认命令重新塞回来，看起来像"删了又自己长出来"。
        for (int i = 0; i < 8; i++) g_keys.push_back(def[i]);
    } else {
        for (auto& kv : got) if (!kv.second.empty()) g_keys.push_back(kv.second);
    }
}

// 剥掉行内注释：`#` 或 `//` 之后的内容连同符号一起去掉。
// 引号里的不算（echo "#1"），`://` 里的 // 也不算 —— 否则 wget http://… 会被截成 "wget http:"。
std::wstring keyStripComment(const std::wstring& s) {
    wchar_t quote = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == L'"' || c == L'\'') { quote = c; continue; }
        if (c == L'#') return trimW(s.substr(0, i));
        if (c == L'/' && i + 1 < s.size() && s[i + 1] == L'/') {
            if (i > 0 && s[i - 1] == L':') continue;
            return trimW(s.substr(0, i));
        }
    }
    return trimW(s);
}


// 一行拆成多条命令：; 分隔，引号内的 ; 不拆（echo "a;b" 仍是一条）
void keySplitCmds(const std::wstring& s, std::vector<std::wstring>& out) {
    std::wstring cur;
    wchar_t quote = 0;
    for (wchar_t c : s) {
        if (quote) {
            cur += c;
            if (c == quote) quote = 0;
            continue;
        }
        if (c == L'"' || c == L'\'') { quote = c; cur += c; continue; }
        if (c == L';') {
            std::wstring t = trimW(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
            continue;
        }
        cur += c;
    }
    std::wstring t = trimW(cur);
    if (!t.empty()) out.push_back(t);
}


// 整段文本 → 逐行剥注释、丢空行
void keyParseLines(const std::wstring& text, std::vector<std::wstring>& out) {
    size_t p = 0;
    while (p <= text.size()) {
        size_t q = text.find(L'\n', p);
        std::wstring line = (q == std::wstring::npos) ? text.substr(p) : text.substr(p, q - p);
        p = (q == std::wstring::npos) ? text.size() + 1 : q + 1;
        line = keyStripComment(line);
        if (!line.empty()) out.push_back(line);
    }
}


// 待发送命令队列：一行写了 `cmd1; cmd2; cmd3` 时逐条发，前一条发出去后
// 用 IDT_CMD 隔开再发下一条，避免几条命令糊成一坨
static std::vector<std::wstring> g_cmdQ;
static size_t g_cmdQi = 0;

void keyRunCommand(const std::wstring& line) {
    std::wstring cmd = keyStripComment(line);
    std::vector<std::wstring> parts;
    keySplitCmds(cmd, parts);
    if (parts.empty()) return;                 // 整行都是注释，不发
    g_cmdQ.swap(parts);
    g_cmdQi = 0;
    keyCmdTick();                              // 先发第一条
}

void keyCmdTick() {
    if (g_cmdQi >= g_cmdQ.size()) {             // 队列跑完
        KillTimer(g_hwnd, IDT_CMD);
        g_cmdQ.clear();
        g_cmdQi = 0;
        return;
    }
    termSendBytes(txEncode(g_cmdQ[g_cmdQi]) + "\r", C_TX, nullptr);
    lineReset();
    ++g_cmdQi;
    if (g_cmdQi < g_cmdQ.size()) {
        SetTimer(g_hwnd, IDT_CMD, kCmdGapMs, nullptr);   // 隔一会儿再发下一条
    } else {
        KillTimer(g_hwnd, IDT_CMD);
        g_cmdQ.clear();
        g_cmdQi = 0;
    }
}


void keyBarRefresh() {
    if (g_hKeysBar) {
        int maxS = 0;
        RECT rc; GetClientRect(g_hKeysBar, &rc);
        int content = kKeyPad + (int)g_keys.size() * (kKeyRowH + kKeyGap) + kKeyFootH;
        if (content > rc.bottom) maxS = content - rc.bottom;
        if (g_keyScroll > maxS) g_keyScroll = maxS;
        if (g_keyScroll < 0) g_keyScroll = 0;
        InvalidateRect(g_hKeysBar, nullptr, TRUE);
    }
}


// 右侧栏折叠按钮文字：展开时 « 收起，收起时 » 展开

void updateKeysBtn() {
    if (g_hKeysBtn) SetWindowTextW(g_hKeysBtn, g_keysOn ? L"»" : L"«");
}


// 顶部切换条文字：按钮模式显示「文本模式」（点它去文本），反之亦然

void updateKeysModeBtn() {
    if (g_hKeyMode) SetWindowTextW(g_hKeyMode, g_keyTextMode ? L"按钮模式" : L"文本模式");
}


// 把编辑框内容按行拆成命令列表（剥注释、空行忽略），写回 g_keys 并落盘

void keyTextCollect() {
    if (!g_hKeyText) return;
    int n = (int)SendMessageW(g_hKeyText, WM_GETTEXTLENGTH, 0, 0);
    std::wstring all;
    all.resize((size_t)n + 1);
    int got = (int)SendMessageW(g_hKeyText, WM_GETTEXT, n + 1, (LPARAM)&all[0]);
    all.resize(got > 0 ? (size_t)got : 0);

    std::vector<std::wstring> out;
    keyParseLines(all, out);          // # 与 // 之后算注释，整行注释/空行不进列表
    g_keys.swap(out);
    saveShortcuts();
    keyBarRefresh();
}


// 命令列表 → 编辑框（一行一条）；toEnd=true 时光标停在末尾

void keyTextReload(bool toEnd) {
    if (!g_hKeyText) return;
    std::wstring all;
    for (size_t i = 0; i < g_keys.size(); i++) {
        if (i) all += L"\r\n";
        all += g_keys[i];
    }
    SetWindowTextW(g_hKeyText, all.c_str());
    SendMessageW(g_hKeyText, EM_SETSEL, toEnd ? (WPARAM)-1 : 0, toEnd ? (LPARAM)-1 : 0);
}


// 按钮模式 <-> 文本模式

void keyModeApply(bool textMode) {
    if (textMode == g_keyTextMode) return;
    if (textMode) {                              // 命令列表 → 一行一条写进编辑框
        g_keyTextMode = true;
        keyTextReload(false);
    } else {                                     // 编辑框 → 命令列表
        keyTextCollect();
        g_keyTextMode = false;
    }
    updateKeysModeBtn();
    if (g_hwnd) layout(g_hwnd);
    if (g_hKeysBar) InvalidateRect(g_hKeysBar, nullptr, TRUE);
    if (g_keyTextMode) { if (g_hKeyText) SetFocus(g_hKeyText); }
    else if (g_hView) SetFocus(g_hView);
}


// 执行文本模式中的某一行（双击触发）：按回车发送，等同手动敲入

void keyTextRunLine(int line) {
    if (!g_hKeyText || line < 0) return;
    wchar_t buf[2048];
    *((WORD*)buf) = (WORD)2046;                  // EM_GETLINE 要求首字为缓冲区字符数
    int n = (int)SendMessageW(g_hKeyText, EM_GETLINE, (WPARAM)line, (LPARAM)buf);
    if (n <= 0) return;
    buf[n] = 0;
    std::wstring cmd = trimW(buf);
    if (cmd.empty()) return;
    keyRunCommand(cmd);            // 剥注释；行内多条用 ; 分开逐条发
}


// 文本编辑框：双击某行 → 立即执行该行命令

LRESULT CALLBACK KeyTextProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_LBUTTONDBLCLK) {
        LRESULT r = DefSubclassProc(h, m, w, l);  // 先让编辑框自己定位光标
        POINT pt{(short)LOWORD(l), (short)HIWORD(l)};
        LRESULT ci = SendMessageW(h, EM_CHARFROMPOS, 0, (LPARAM)&pt);
        int line = (int)SendMessageW(h, EM_LINEFROMCHAR, (WPARAM)(int)ci, 0);
        keyTextRunLine(line);
        return r;
    }
    return DefSubclassProc(h, m, w, l);
}


// 通用单行输入框：返回 true 时 val 为新内容

void runShortcut(int i) {
    if (i < 0 || i >= (int)g_keys.size()) return;
    keyRunCommand(g_keys[i]);      // 剥注释；行内多条用 ; 分开逐条发
    // 焦点要还给它真正显示的那个控件：SSH 会话的显示区是 Terminal，
    // 原来无条件 SetFocus(g_hView)，会把手写焦点交给一个被隐藏的 RichEdit。
    if (g_cur && isTermSess(g_cur) && g_cur->term) g_cur->term->focus();
    else if (g_hView) SetFocus(g_hView);
}

void keyAdd() {
    std::wstring v;
    if (!showInputBox(L"新增快捷命令", L"命令内容（点击按钮即回车发送）：", v)) return;
    v = trimW(v);
    if (v.empty()) return;
    g_keys.push_back(v);
    saveShortcuts();
    keyBarRefresh();
    appendRun(L"— 已新增快捷命令：" + v + L" —\r\n", C_SYS);
}

void keyEdit(int i) {
    if (i < 0 || i >= (int)g_keys.size()) return;
    std::wstring v = g_keys[i];
    if (!showInputBox(L"修改快捷命令", L"命令内容（留空则删除该条）：", v)) return;
    v = trimW(v);
    if (v.empty()) {
        g_keys.erase(g_keys.begin() + i);
        appendRun(L"— 已删除该快捷命令 —\r\n", C_SYS);
    } else {
        g_keys[i] = v;
        appendRun(L"— 快捷命令已改为：" + v + L" —\r\n", C_SYS);
    }
    saveShortcuts();
    keyBarRefresh();
}

void keyDel(int i) {
    if (i < 0 || i >= (int)g_keys.size()) return;
    std::wstring old = g_keys[i];
    g_keys.erase(g_keys.begin() + i);
    saveShortcuts();
    keyBarRefresh();
    appendRun(L"— 已删除快捷命令：" + old + L" —\r\n", C_SYS);
}


// 读文本文件：优先 UTF-8，非法则按 ANSI(GBK)

std::wstring readTextAuto(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    DWORD sz = GetFileSize(f, nullptr);
    std::string buf;
    if (sz > 0 && sz < (16u << 20)) {
        buf.resize(sz);
        DWORD rd = 0;
        ReadFile(f, &buf[0], sz, &rd, nullptr);
        buf.resize(rd);
    }
    CloseHandle(f);
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        buf = buf.substr(3);
    if (buf.empty()) return L"";

    int wl = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf.data(), (int)buf.size(), nullptr, 0);
    if (wl > 0) {
        std::wstring w(wl, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), &w[0], wl);
        return w;
    }
    int al = MultiByteToWideChar(CP_ACP, 0, buf.data(), (int)buf.size(), nullptr, 0);
    if (al <= 0) return L"";
    std::wstring w(al, L'\0');
    MultiByteToWideChar(CP_ACP, 0, buf.data(), (int)buf.size(), &w[0], al);
    return w;
}

void keyExport() {
    if (g_keyTextMode) keyTextCollect();          // 文本模式下先收集改动再导出
    wchar_t path[MAX_PATH] = L"shortcuts.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"文本 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"导出快捷命令";
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    FILE* f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) { appendRun(L"— 无法写入该文件 —\r\n", C_ERR); return; }
    for (auto& s : g_keys) fwprintf(f, L"%ls\n", s.c_str());   // ccs=UTF-8 自带 BOM
    fclose(f);
    appendRun(L"— 已导出 " + std::to_wstring(g_keys.size()) + L" 条快捷命令 —\r\n", C_SYS);
}

void keyImport() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"文本 (*.txt;*.ini)\0*.txt;*.ini\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"导入快捷命令（每行一条，追加到当前列表；# 或 // 之后为注释；空文件会清空列表）";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    // 文本模式下编辑框才是唯一事实来源。用户刚把内容删空、或手工改过几行，
    // 这些改动平时只在切换模式/收起/退出时才写回 g_keys；导入必须先把它们收进来，
    // 否则 keyTextReload 会把内存里的旧列表连同导入内容一起倒回编辑框，
    // 看起来就是"清空了又自己长回来"。
    if (g_keyTextMode) keyTextCollect();

    // 空文件是合法输入（用来一键清空列表），只有真读不到文件才算错误
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        appendRun(L"— 无法读取该文件 —\r\n", C_ERR);
        return;
    }
    std::wstring txt = readTextAuto(path);

    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= txt.size()) {
        size_t nl = txt.find(L'\n', start);
        std::wstring line = (nl == std::wstring::npos) ? txt.substr(start)
                                                       : txt.substr(start, nl - start);
        start = (nl == std::wstring::npos) ? txt.size() + 1 : nl + 1;
        line = keyStripComment(line);        // # 与 // 之后的内容都算注释
        if (line.empty()) continue;
        // 兼容 "F1=xxx" 这种从 shortcuts.ini 直接拷过来的行。
        // 编号位数不设上限（F100= / F1000= 也要认），但必须是纯数字，
        // 免得把文档里 "File=xxx" 之类的正文误剥掉前缀。
        if (line.size() > 2 && (line[0] == L'F' || line[0] == L'f')) {
            size_t eq = line.find(L'=');
            bool numbered = eq != std::wstring::npos && eq > 1;
            for (size_t i = 1; numbered && i < eq; ++i)
                if (line[i] < L'0' || line[i] > L'9') numbered = false;
            if (numbered) {
                line = trimW(line.substr(eq + 1));
                if (line.empty()) continue;
            }
        }
        lines.push_back(line);
    }

    if (lines.empty()) {                     // 空文件 / 整份都是注释 → 清空列表
        g_keys.clear();
        saveShortcuts();
        keyBarRefresh();
        if (g_keyTextMode) keyTextReload(true);
        appendRun(L"— 文件为空，已清空快捷命令列表 —\r\n", C_SYS);
        return;
    }

    for (auto& s : lines) g_keys.push_back(s);
    saveShortcuts();
    keyBarRefresh();
    if (g_keyTextMode) keyTextReload(true);        // 文本模式：把导入结果同步到编辑框
    appendRun(L"— 已导入 " + std::to_wstring(lines.size()) + L" 条快捷命令 —\r\n", C_SYS);
}

void drawKeysBar(const DRAWITEMSTRUCT* ds) {
    RECT rc = ds->rcItem;
    int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
    HDC hdc = ds->hDC;
    if (bw < 30 || bh < 4) {                     // 太窄：只铺底色
        HBRUSH bg = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        return;
    }
    HDC dc  = hdc;
    HBITMAP bmp = nullptr;
    HGDIOBJ obmp = nullptr;
    {                                            // 双缓冲：整帧画完再一次性贴回，消除拖宽时的重叠残影
        dc = CreateCompatibleDC(hdc);
        bmp = CreateCompatibleBitmap(hdc, bw, bh);
        if (dc && bmp) {
            obmp = SelectObject(dc, bmp);
            SetViewportOrgEx(dc, -rc.left, -rc.top, nullptr);
        } else {
            if (bmp) DeleteObject(bmp);
            if (dc) DeleteDC(dc);
            dc = hdc; bmp = nullptr;
        }
    }
    HBRUSH bg = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xDC, 0xDC, 0xDC));
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(dc, g_fontUI);

    HPEN   pn   = CreatePen(PS_SOLID, 1, RGB(0xC6, 0xC6, 0xC6));
    HPEN   pnH  = CreatePen(PS_SOLID, 1, RGB(0x2E, 0x7C, 0xE0));
    HBRUSH brN  = CreateSolidBrush(RGB(0xF7, 0xF8, 0xFA));
    HBRUSH brH  = CreateSolidBrush(RGB(0xE8, 0xF1, 0xFE));
    HGDIOBJ onb = SelectObject(dc, GetStockObject(NULL_BRUSH));

    // 列表区域：底部留给「导入 / 导出」，左边缘留给折叠把手
    int fy    = rc.bottom - kKeyFootH;                    // 底部按钮区上边界
    if (g_keysOn) {                                       // 左边缘一条浅色轨道，折叠把手浮在上面
        RECT rail{rc.left + 1, rc.top + 1, rc.left + kHandleW, rc.bottom - 1};
        HBRUSH rb = CreateSolidBrush(RGB(0xF8, 0xF9, 0xFB));
        FillRect(dc, &rail, rb);
        DeleteObject(rb);
    }
    int lpad  = rc.left + kKeyPad + (g_keysOn ? kHandleW : 0);
    int lpadT = rc.left + kKeyPad;                        // 空提示用
    for (int i = 0; i < (int)g_keys.size() && !g_keyTextMode; i++) {
        int y = rc.top + kKeyPad + i * (kKeyRowH + kKeyGap) - g_keyScroll;
        RECT b{lpad, y, rc.right - kKeyPad, y + kKeyRowH};
        if (b.bottom <= rc.top + 1 || b.bottom > fy) continue;   // 超出可视区/压到底部按钮区的不画
        bool hv = (i == g_keyHover);
        FillRect(dc, &b, hv ? brH : brN);
        HGDIOBJ op2 = SelectObject(dc, hv ? pnH : pn);
        Rectangle(dc, b.left, b.top, b.right, b.bottom);
        SelectObject(dc, op2);
        SetTextColor(dc, hv ? RGB(0x1A, 0x4F, 0xA8) : RGB(0x28, 0x28, 0x28));
        RECT tr = b;
        tr.left += 6;
        tr.right -= 6;
        DrawTextW(dc, g_keys[i].c_str(), -1, &tr,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // ---- 底部：导入 / 导出 ----
    {
        HPEN sep = CreatePen(PS_SOLID, 1, RGB(0xDC, 0xDC, 0xDC));
        HGDIOBJ osp = SelectObject(dc, sep);
        MoveToEx(dc, rc.left + 1, fy, nullptr);
        LineTo(dc, rc.right - 1, fy);
        SelectObject(dc, osp);
        DeleteObject(sep);

        int fbh = 24;
        int fby = fy + (kKeyFootH - fbh) / 2;
        int mid = (rc.left + rc.right) / 2;
        RECT fb1{rc.left + kKeyPad, fby, mid - 3, fby + fbh};
        RECT fb2{mid + 3, fby, rc.right - kKeyPad, fby + fbh};
        for (int k = 0; k < 2; k++) {
            RECT b = k ? fb2 : fb1;
            bool hv = (g_keyFootHover == k + 1);
            FillRect(dc, &b, hv ? brH : brN);
            HGDIOBJ op3 = SelectObject(dc, hv ? pnH : pn);
            Rectangle(dc, b.left, b.top, b.right, b.bottom);
            SelectObject(dc, op3);
            SetTextColor(dc, hv ? RGB(0x1A, 0x4F, 0xA8) : RGB(0x28, 0x28, 0x28));
            DrawTextW(dc, k ? L"导出" : L"导入", -1, &b,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }

    SelectObject(dc, onb);
    DeleteObject(pn);
    DeleteObject(pnH);
    DeleteObject(brN);
    DeleteObject(brH);

    if (g_keys.empty() && !g_keyTextMode) {
        RECT tr{lpadT, rc.top + kKeyPad, rc.right - kKeyPad, rc.top + kKeyPad + 20};
        SetTextColor(dc, RGB(0x9A, 0x9A, 0x9A));
        DrawTextW(dc, L"右键新增", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (g_keyDrag) {   // 拖动中：左边缘高亮，提示当前正在调整宽度
        RECT bar{rc.left, rc.top + 1, rc.left + kResizeW, rc.bottom - 1};
        HBRUSH bb = CreateSolidBrush(RGB(0x2E, 0x7C, 0xE0));
        FillRect(dc, &bar, bb);
        DeleteObject(bb);
    }
    SelectObject(dc, of);

    if (bmp) {                                    // 整帧贴回屏幕
        SetViewportOrgEx(dc, 0, 0, nullptr);
        BitBlt(hdc, rc.left, rc.top, bw, bh, dc, rc.left, rc.top, SRCCOPY);
        SelectObject(dc, obmp);
        DeleteObject(bmp);
        DeleteDC(dc);
    }
}


// 命中测试：y 为客户区坐标，返回快捷命令下标（空白处 -1）

int keyHit(int y) {
    if (g_hKeysBar) {
        RECT rc;
        GetClientRect(g_hKeysBar, &rc);
        if (y >= rc.bottom - kKeyFootH) return -1;        // 底部按钮区不算命令
    }
    int rel = y - kKeyPad + g_keyScroll;
    if (rel < 0) return -1;
    int slot = rel / (kKeyRowH + kKeyGap);
    int in   = rel % (kKeyRowH + kKeyGap);
    if (in >= kKeyRowH) return -1;
    if (slot < 0 || slot >= (int)g_keys.size()) return -1;
    return slot;
}


// 底部按钮命中：0 无，1 导入，2 导出

int footHit(HWND h, int x, int y) {
    RECT rc;
    GetClientRect(h, &rc);
    if (y < rc.bottom - kKeyFootH) return 0;
    int mid = (rc.left + rc.right) / 2;
    return (x < mid) ? 1 : 2;
}

void showKeyMenu(POINT pt, int idx) {
    HMENU m = CreatePopupMenu();
    if (idx >= 0 && idx < (int)g_keys.size()) {
        std::wstring t = g_keys[idx];
        if (t.size() > 24) t = t.substr(0, 24) + L"…";
        mAddMenu(m, MF_STRING | MF_DISABLED, 0, (L"「" + t + L"」").c_str());
        mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        mAddMenu(m, MF_STRING, IDM_KEY_EXEC, L"执行此命令");
        mAddMenu(m, MF_STRING, IDM_KEY_EDIT, L"修改…");
        mAddMenu(m, MF_STRING, IDM_KEY_DEL,  L"删除");
        mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        mAddMenu(m, MF_STRING, IDM_KEY_ADD, L"新增快捷命令…");
    } else {
        mAddMenu(m, MF_STRING, IDM_KEY_ADD, L"新增快捷命令…");
    }
    mAddMenu(m, MF_SEPARATOR, 0, nullptr);
    mAddMenu(m, MF_STRING, IDM_KEY_EXPORT, L"导出快捷命令…");
    mAddMenu(m, MF_STRING, IDM_KEY_IMPORT, L"导入快捷命令…");

    SetForegroundWindow(g_hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                  pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)cmd, 0);
}

LRESULT CALLBACK KeyProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
        // 同侧栏：快捷栏底部有「导入/导出」贴底绘制，窗口放大时系统只重绘新露出
        // 的区域，旧位置会留残影。尺寸一变就整块重绘。
        //
        // UpdateWindow 不能省：栏是 SS_OWNERDRAW，实际绘制在父窗口的 WM_DRAWITEM 里，
        // 而 InvalidateRect 只是标脏、等消息队列空闲才画。拖动左边缘加宽时每帧都在
        // 改尺寸，绘制永远排在鼠标消息后面 → 新盖住终端的那一条来不及画，露出下层
        // 终端的深色背景（看到「黑框」）。这里强制同步走一次 WM_PAINT。
        case WM_SIZE:
            InvalidateRect(h, nullptr, TRUE);
            UpdateWindow(h);
            return 0;
        case WM_SETCURSOR: {                       // 左边缘显示左右箭头
            POINT pt; GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            if (pt.x >= 0 && pt.x <= kResizeW) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            break;
        }
        case WM_LBUTTONDOWN: {                     // 左边缘按下 → 开始拖宽度
            if ((short)LOWORD(l) <= kResizeW) {
                g_keyDrag = true;
                g_keyHover = -1;
                SetCapture(h);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_keyDrag) {
                g_keyDrag = false;
                ReleaseCapture();
                layout(g_hwnd);                        // 松手才真正重排终端，只有这一次重绘
                if (g_hView) InvalidateRect(g_hView, nullptr, TRUE);
                RedrawWindow(g_hwnd, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
                saveSettings();
                return 0;
            }
            int fx = footHit(h, (short)LOWORD(l), (short)HIWORD(l));
            if (fx == 1) { keyImport(); return 0; }
            if (fx == 2) { keyExport(); return 0; }
            int i = keyHit((short)HIWORD(l));
            if (i >= 0) runShortcut(i);
            return 0;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT pt;
            if (m == WM_CONTEXTMENU && l != (LPARAM)-1) {
                pt.x = (short)LOWORD(l);
                pt.y = (short)HIWORD(l);
            } else GetCursorPos(&pt);
            POINT cp = pt;
            ScreenToClient(h, &cp);
            g_ctxKey = keyHit(cp.y);
            showKeyMenu(pt, g_ctxKey);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(w);
            g_keyScroll -= (delta / WHEEL_DELTA) * (kKeyRowH + kKeyGap);
            keyBarRefresh();
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_keyDrag) {                       // 拖动：按鼠标在父窗口的 x 反算栏宽
                POINT pt{(short)LOWORD(l), (short)HIWORD(l)};
                ClientToScreen(h, &pt);
                ScreenToClient(g_hwnd, &pt);
                RECT wr;
                GetClientRect(g_hwnd, &wr);
                int sideW = g_sideOn ? kSideW : 24;
                int maxW  = wr.right - 16 - sideW - 6 - kMinViewW - 6;
                if (maxW > kKeyMaxW) maxW = kKeyMaxW;   // 绝对上限：原 460 + 一个左侧栏宽度
                int newW = wr.right - 8 - pt.x;
                if (newW < kKeyMinW) newW = kKeyMinW;
                if (maxW >= kKeyMinW && newW > maxW) newW = maxW;
                if (newW != g_keysW) {
                    g_keysW = newW;
                    layoutKeysOnly();                  // 拖动中终端保持原尺寸，栏直接盖上去，不重排不闪
                }
                return 0;
            }
            int fx = footHit(h, (short)LOWORD(l), (short)HIWORD(l));
            if (fx != g_keyFootHover) {
                g_keyFootHover = fx;
                InvalidateRect(h, nullptr, TRUE);
            }
            int i = keyHit((short)HIWORD(l));
            if (i != g_keyHover) {
                g_keyHover = i;
                InvalidateRect(h, nullptr, TRUE);
            }
            return 0;
        }
    }
    return DefSubclassProc(h, m, w, l);
}
