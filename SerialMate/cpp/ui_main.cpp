// ui_main.cpp - 主窗口：控件创建、布局、菜单、窗口过程

#include "sm.h"
void syncMenuChecks() {
    if (!g_menuTool) return;
    CheckMenuItem(g_menuTool, IDM_PORT_DTR, MF_BYCOMMAND | (g_dtr ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menuTool, IDM_PORT_RTS, MF_BYCOMMAND | (g_rts ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menuTool, IDM_PORT_SERVE, MF_BYCOMMAND | (g_serve ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_VIEW_TS, MF_BYCOMMAND | (g_showTs ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_VIEW_AGENT, MF_BYCOMMAND | (g_showAgent ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menuTool, IDM_TOOL_STTY, MF_BYCOMMAND | (g_sttyAdapt ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(g_menu, IDM_ENC_BASE, IDM_ENC_BASE + 2, IDM_ENC_BASE + g_rxEnc, MF_BYCOMMAND);
    CheckMenuItem(g_menu, IDM_LOG_SAVE, MF_BYCOMMAND | (g_logSave ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_SIDE_TOGGLE, MF_BYCOMMAND | (g_sideOn ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_KEYS_TOGGLE, MF_BYCOMMAND | (g_keysOn ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(g_menuSess, IDM_SESSION_RECONN, MF_BYCOMMAND | (curOn() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(g_menuSess, IDM_PORT_CLOSE, MF_BYCOMMAND | (curOn() ? MF_ENABLED : MF_GRAYED));
    if (g_menuSet) {
        CheckMenuRadioItem(g_menuSet, IDM_SET_MFONT_BASE, IDM_SET_MFONT_BASE + kUiPtCount - 1,
                           IDM_SET_MFONT_BASE + g_menuPtIdx, MF_BYCOMMAND);
        CheckMenuRadioItem(g_menuSet, IDM_SET_DFONT_BASE, IDM_SET_DFONT_BASE + kUiPtCount - 1,
                           IDM_SET_DFONT_BASE + g_dlgPtIdx, MF_BYCOMMAND);
    }
}


// ---------------- 自绘菜单 ----------------
// 菜单项数据：文本（可含 \t 加速键）+ 是否子菜单


void mAddMenu(HMENU m, UINT flags, UINT_PTR id, const wchar_t* text) {
    if (!text) { AppendMenuW(m, flags, id, nullptr); return; }
    MenuItemData* d = new MenuItemData;
    d->text = text;
    d->popup = (flags & MF_POPUP) != 0;
    d->top = (m == g_menu);
    g_menuItems.push_back(d);
    AppendMenuW(m, flags | MF_OWNERDRAW, id, (LPCWSTR)d);
}

UINT menuStateIn(HMENU m, UINT_PTR id) {
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        UINT st = GetMenuState(m, i, MF_BYPOSITION);
        if (st == (UINT)-1) continue;
        if (st & MF_POPUP) {
            HMENU sub = GetSubMenu(m, i);
            if (sub) { UINT r = menuStateIn(sub, id); if (r != (UINT)-1) return r; }
        } else if ((UINT_PTR)GetMenuItemID(m, i) == id) {
            return st;
        }
    }
    return (UINT)-1;
}

void makeMenuFont() {
    HDC dc = GetDC(g_hwnd);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    if (g_fontMenu) DeleteObject(g_fontMenu);
    g_fontMenu = CreateFontW(-MulDiv(kUiPts[g_menuPtIdx], dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g_hwnd, dc);
    g_menuRowH = tm.tmHeight + 8;
}

void applyMenuStyle() {
    if (!g_menu) return;
    makeMenuFont();
    if (g_brushMenu) DeleteObject(g_brushMenu);
    g_brushMenu = CreateSolidBrush(g_menuBg);
    MENUINFO mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = g_brushMenu;
    SetMenuInfo(g_menu, &mi);
    if (g_hwnd) DrawMenuBar(g_hwnd);
}

void drawMenuItem(const DRAWITEMSTRUCT* ds, const MenuItemData* d) {
    HDC dc = ds->hDC;
    RECT rc = ds->rcItem;
    bool sel = (ds->itemState & ODS_SELECTED) != 0;
    bool dis = (ds->itemState & ODS_GRAYED) != 0;

    HBRUSH bg = CreateSolidBrush(sel ? g_menuSel : g_menuBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(dc, g_fontMenu);
    SetTextColor(dc, dis ? RGB(0x9A, 0x9A, 0x9A) : (sel ? RGB(0xFF, 0xFF, 0xFF) : g_menuFg));

    // 勾选标记
    if (!d->popup && !d->top) {
        UINT st = g_menu ? menuStateIn(g_menu, ds->itemID) : (UINT)-1;
        if (st != (UINT)-1 && (st & MF_CHECKED)) {
            RECT rcc{rc.left + 7, rc.top, rc.left + 26, rc.bottom};
            DrawTextW(dc, L"\u2713", -1, &rcc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    // 文本 + 右侧加速键
    std::wstring main = d->text, acc;
    size_t tab = main.find(L'\t');
    if (tab != std::wstring::npos) { acc = main.substr(tab + 1); main = main.substr(0, tab); }
    int lx = d->top ? 10 : 27;
    RECT rt{rc.left + lx, rc.top, rc.right - (d->popup && !d->top ? 22 : 10), rc.bottom};
    DrawTextW(dc, main.c_str(), -1, &rt, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (!acc.empty()) {
        RECT ra{rc.left + lx, rc.top, rc.right - (d->popup && !d->top ? 22 : 12), rc.bottom};
        DrawTextW(dc, acc.c_str(), -1, &ra, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    // 子菜单箭头（顶层菜单栏项不画）
    if (d->popup && !d->top) {
        int cx = rc.right - 13, cy = (rc.top + rc.bottom) / 2;
        POINT p[3] = {{cx - 3, cy - 4}, {cx + 2, cy}, {cx - 3, cy + 4}};
        HBRUSH ab = CreateSolidBrush(dis ? RGB(0xAA, 0xAA, 0xAA)
                                         : (sel ? RGB(0xFF, 0xFF, 0xFF) : g_menuFg));
        HGDIOBJ ob = SelectObject(dc, ab);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, p, 3);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(ab);
    }
    SelectObject(dc, of);
}

void measureMenuItem(MEASUREITEMSTRUCT* ms) {
    MenuItemData* d = (MenuItemData*)ms->itemData;
    if (!d) { ms->itemHeight = g_menuRowH; ms->itemWidth = 120; return; }
    std::wstring main = d->text, acc;
    size_t tab = main.find(L'\t');
    if (tab != std::wstring::npos) { acc = main.substr(tab + 1); main = main.substr(0, tab); }
    main.erase(std::remove(main.begin(), main.end(), L'&'), main.end());

    HDC dc = GetDC(g_hwnd);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    SIZE s1{}, s2{};
    GetTextExtentPoint32W(dc, main.c_str(), (int)main.size(), &s1);
    if (!acc.empty()) GetTextExtentPoint32W(dc, acc.c_str(), (int)acc.size(), &s2);
    SelectObject(dc, of);
    ReleaseDC(g_hwnd, dc);

    ms->itemHeight = g_menuRowH;
    if (d->top) {
        ms->itemWidth = s1.cx + 26;
    } else {
        ms->itemWidth = 36 + s1.cx + (s2.cx ? s2.cx + 34 : 0) + (d->popup ? 18 : 0);
    }
}

void buildMenu() {
    g_menu = CreateMenu();

    // ---- 会话 ----
    g_menuSess = CreatePopupMenu();
    mAddMenu(g_menuSess, MF_STRING, IDM_PORT_OPEN,      L"新建会话…\tCtrl+N");
    mAddMenu(g_menuSess, MF_STRING, IDM_SESSION_RECONN, L"重新连接\tCtrl+R");
    mAddMenu(g_menuSess, MF_STRING, IDM_PORT_CLOSE,     L"断开连接\tCtrl+D");
    mAddMenu(g_menuSess, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuSess, MF_STRING, IDM_PORT_REFRESH,   L"刷新端口列表\tF5");
    mAddMenu(g_menuSess, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuSess, MF_STRING, IDM_SESSION_EXIT,   L"退出");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)g_menuSess, L"会话(&S)");

    // ---- 工具 ----
    g_menuTool = CreatePopupMenu();
    mAddMenu(g_menuTool, MF_STRING, IDM_PORT_SERVE, L"启用本地接管服务 (127.0.0.1:7000)");
    mAddMenu(g_menuTool, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuTool, MF_STRING, IDM_PORT_DTR, L"DTR 有效");
    mAddMenu(g_menuTool, MF_STRING, IDM_PORT_RTS, L"RTS 有效");
    mAddMenu(g_menuTool, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuTool, MF_STRING, IDM_TOOL_STTY, L"打开会话时适配终端宽度 (stty cols 400)");
    mAddMenu(g_menuTool, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuTool, MF_STRING, IDM_LOG_SAVE, L"保存串口日志");
    mAddMenu(g_menuTool, MF_STRING, IDM_LOG_DIR, L"设置日志目录…");
    mAddMenu(g_menuTool, MF_STRING, IDM_LOG_OPEN, L"打开日志目录");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)g_menuTool, L"工具(&T)");

    // ---- 视图 ----
    HMENU mView = CreatePopupMenu();
    mAddMenu(mView, MF_STRING, IDM_VIEW_BIGGER, L"字体放大\tCtrl+=");
    mAddMenu(mView, MF_STRING, IDM_VIEW_SMALLER, L"字体缩小\tCtrl+-");
    mAddMenu(mView, MF_STRING, IDM_VIEW_RESET, L"字体重置");
    mAddMenu(mView, MF_SEPARATOR, 0, nullptr);
    mAddMenu(mView, MF_STRING, IDM_VIEW_TS, L"显示时间戳");
    mAddMenu(mView, MF_STRING, IDM_VIEW_AGENT, L"显示接管命令");
    mAddMenu(mView, MF_STRING, IDM_SIDE_TOGGLE, L"左侧连接信息栏\tCtrl+B");
    mAddMenu(mView, MF_SEPARATOR, 0, nullptr);
    HMENU mEnc = CreatePopupMenu();
    mAddMenu(mEnc, MF_STRING, IDM_ENC_BASE + 0, L"自动（UTF-8 优先，失败退 GBK）");
    mAddMenu(mEnc, MF_STRING, IDM_ENC_BASE + 1, L"UTF-8");
    mAddMenu(mEnc, MF_STRING, IDM_ENC_BASE + 2, L"文本 ANSI (GBK)");
    mAddMenu(mView, MF_POPUP, (UINT_PTR)mEnc, L"串口编码(&E)");
    mAddMenu(mView, MF_SEPARATOR, 0, nullptr);
    mAddMenu(mView, MF_STRING, IDM_VIEW_CLEAR, L"清屏\tCtrl+L");
    mAddMenu(mView, MF_STRING, IDM_VIEW_SAVE, L"保存日志…");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)mView, L"视图(&V)");

    // ---- 设置 ----
    g_menuSet = CreatePopupMenu();
    HMENU mMF = CreatePopupMenu();
    for (int i = 0; i < kUiPtCount; i++) {
        wchar_t t[16];
        swprintf_s(t, L"%d pt", kUiPts[i]);
        mAddMenu(mMF, MF_STRING, IDM_SET_MFONT_BASE + i, t);
    }
    mAddMenu(g_menuSet, MF_POPUP, (UINT_PTR)mMF, L"菜单字体大小");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_MFG, L"菜单文字颜色…");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_MBG, L"菜单背景颜色…");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_MSEL, L"菜单选中颜色…");
    mAddMenu(g_menuSet, MF_SEPARATOR, 0, nullptr);
    HMENU mDF = CreatePopupMenu();
    for (int i = 0; i < kUiPtCount; i++) {
        wchar_t t[16];
        swprintf_s(t, L"%d pt", kUiPts[i]);
        mAddMenu(mDF, MF_STRING, IDM_SET_DFONT_BASE + i, t);
    }
    mAddMenu(g_menuSet, MF_POPUP, (UINT_PTR)mDF, L"对话框字体大小");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_DFG, L"对话框文字颜色…");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_DBG, L"对话框背景颜色…");
    mAddMenu(g_menuSet, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_RESET, L"恢复默认样式");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)g_menuSet, L"设置(&O)");

    // ---- 帮助 ----
    HMENU mHelp = CreatePopupMenu();
    mAddMenu(mHelp, MF_STRING, IDM_HELP_ABOUT, L"关于 SerialMate");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)mHelp, L"帮助(&A)");

    SetMenu(g_hwnd, g_menu);
    applyMenuStyle();
    syncMenuChecks();
}


// 切换串口显示/发送编码：菜单和底部下拉框共用这一个入口

void applyEnc(int e, bool announce) {
    if (e < 0 || e > 2) e = 0;
    g_rxEnc = e;
    saveSettings();
    syncMenuChecks();                     // 菜单勾选 + 下拉框选中项
    updateStatus();
    if (g_termReady) termSyncLine();      // 还没回车的那一行按新编码重画
    if (announce) {
        appendRun(L"— 串口编码切换为 " + std::wstring(encName()) + L" —\r\n", C_SYS);
        logLine(L"SYS", "enc=" + std::to_string(e));
    }
}


// ---------------- 布局 ----------------

void layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    int top     = 8;
    int sideW   = g_sideOn ? 196 : 24;
    int statusH = 22;
    int viewH   = H - top - statusH - 8;
    int keysW   = g_keysOn ? g_keysW : 24;            // 收起时只留一条按钮
    int maxKeys = W - 16 - sideW - 6 - 200;           // 终端至少留 200px
    if (keysW > maxKeys) keysW = maxKeys;
    if (keysW < 24) keysW = 24;
    int viewW = W - 16 - sideW - 6 - keysW - 6;
    if (viewW < 120) { keysW = 0; viewW = W - 16 - sideW - 6; }

    // 左侧栏占满整列，折叠把手贴在其右边缘、垂直居中
    MoveWindow(g_hSide, 8, top, sideW, viewH, TRUE);
    if (g_hSideBtn) {
        int bw2 = g_sideOn ? kHandleW : 22;
        int bh2 = g_sideOn ? kHandleH : 46;
        int bx  = 8 + sideW - bw2 - (g_sideOn ? 0 : 1);
        int by  = top + (viewH - bh2) / 2;
        if (by < top) by = top;
        SetWindowPos(g_hSideBtn, HWND_TOP, bx, by, bw2, bh2, SWP_NOACTIVATE);
    }
    MoveWindow(g_hView, 8 + sideW + 6, top, viewW, viewH, TRUE);

    // SSH 会话：终端控件顶替 RichEdit 占同一块区域
    bool sshMode = (g_cur && isTermSess(g_cur));
    for (auto& sp : g_sess)
        if (sp->term) sp->term->showWindow(false);
    if (sshMode && g_cur->term) {
#ifdef TERM_TRACE
        { FILE* f = fopen("term_size.log", "a");
          if (f) { fprintf(f, "layout W=%d H=%d side=%d keys=%d viewW=%d viewH=%d top=%d\n",
                           W, H, sideW, keysW, viewW, viewH, top); fclose(f); } }
#endif
        g_cur->term->setBounds(8 + sideW + 6, top, viewW, viewH);
        g_cur->term->showWindow(true);
        ShowWindow(g_hView, SW_HIDE);
    } else if (g_hView) {
        ShowWindow(g_hView, SW_SHOW);
    }

    int keysX = 8 + sideW + 6 + viewW + 6;
    if (g_hKeysBtn) {
        if (keysW) {
            // 折叠把手：始终贴在栏的左边缘、垂直居中
            int bw2 = g_keysOn ? kHandleW : 22;
            int bh2 = g_keysOn ? kHandleH : 46;
            int bx  = keysX + (g_keysOn ? 0 : 1);
            int by  = top + (viewH - bh2) / 2;
            if (by < top + kKeyTopH) by = top + kKeyTopH;
            SetWindowPos(g_hKeysBtn, HWND_TOP, bx, by, bw2, bh2, SWP_NOACTIVATE);
            ShowWindow(g_hKeysBtn, SW_SHOW);
        } else {
            ShowWindow(g_hKeysBtn, SW_HIDE);
        }
    }
    // 顶部：按钮/文本 模式切换条
    if (g_hKeyMode) {
        if (keysW && g_keysOn) {
            SetWindowPos(g_hKeyMode, HWND_TOP, keysX, top, keysW, kKeyTopH, SWP_NOACTIVATE);
            ShowWindow(g_hKeyMode, SW_SHOW);
        } else {
            ShowWindow(g_hKeyMode, SW_HIDE);
        }
    }
    if (g_hKeysBar) {
        if (keysW && g_keysOn) {
            int barY = top + kKeyTopH, barH = viewH - kKeyTopH;
            MoveWindow(g_hKeysBar, keysX, barY, keysW, barH, TRUE);   // 栏占满整列，折叠把手浮在其左边缘中部
            ShowWindow(g_hKeysBar, SW_SHOW);
        } else {
            ShowWindow(g_hKeysBar, SW_HIDE);
        }
    }
    // 文本模式：多行编辑框盖住命令按钮区（底部导入/导出仍可见）
    if (g_hKeyText) {
        if (keysW && g_keysOn && g_keyTextMode) {
            int ex = keysX + kHandleW + 6;
            int ey = top + kKeyTopH + 6;
            int ew = keysW - kHandleW - 6 - 8;
            int eh = viewH - kKeyTopH - kKeyFootH - 12;
            if (ew < 40) ew = 40;
            if (eh < 40) eh = 40;
            // HWND_TOP：编辑框必须压在自绘栏之上，否则栏会盖住它导致点不到
            SetWindowPos(g_hKeyText, HWND_TOP, ex, ey, ew, eh, SWP_NOACTIVATE);
            ShowWindow(g_hKeyText, SW_SHOW);
        } else {
            ShowWindow(g_hKeyText, SW_HIDE);
        }
    }
    MoveWindow(g_hStatus, 8, H - 30, W - 16, 20, TRUE);
}


// 拖动中专用：只挪右侧栏和折叠按钮，终端尺寸不动（避免 RichEdit 反复重排造成闪烁/残影）

void layoutKeysOnly() {
    if (!g_hwnd) return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int top = 8, statusH = 22;
    int sideW = g_sideOn ? 196 : 24;
    int viewH = H - top - statusH - 8;
    int keysW = g_keysW;
    int maxKeys = W - 16 - sideW - 6 - 200;
    if (keysW > maxKeys) keysW = maxKeys;
    if (keysW < 24) keysW = 24;
    int keysX = W - 8 - keysW;

    // 终端只在「栏变窄、会露出空档」时跟着变宽；拖宽栏（栏盖住终端）时终端不动
    // → 最常见的拖宽操作零重排、不闪；拖窄时也不会露出背景灰块
    if (g_hView) {
        RECT vr;
        GetWindowRect(g_hView, &vr);
        int curW  = vr.right - vr.left;
        int needW = keysX - 6 - (8 + sideW + 6);
        if (needW > curW + 1) {
            SetWindowPos(g_hView, nullptr, 8 + sideW + 6, top, needW, viewH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_cur && g_cur->term) {
#ifdef TERM_TRACE
                { FILE* f = fopen("term_size.log", "a");
                  if (f) { fprintf(f, "keysOnly W=%d H=%d keysW=%d keysX=%d needW=%d curW=%d viewH=%d\n",
                                   W, H, keysW, keysX, needW, curW, viewH); fclose(f); } }
#endif
                g_cur->term->setBounds(8 + sideW + 6, top, needW, viewH);
            }
        }
    }
    if (g_hKeysBtn) {
        int bw2 = g_keysOn ? kHandleW : 22;
        int bh2 = g_keysOn ? kHandleH : 46;
        int bx  = keysX + (g_keysOn ? 0 : 1);
        int by  = top + (viewH - bh2) / 2;
        if (by < top + kKeyTopH) by = top + kKeyTopH;
        SetWindowPos(g_hKeysBtn, HWND_TOP, bx, by, bw2, bh2, SWP_NOACTIVATE);
    }
    if (g_hKeyMode)
        SetWindowPos(g_hKeyMode, HWND_TOP, keysX, top, keysW, kKeyTopH, SWP_NOACTIVATE);
    if (g_hKeysBar && g_keysOn) {
        SetWindowPos(g_hKeysBar, nullptr, keysX, top + kKeyTopH, keysW, viewH - kKeyTopH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(g_hKeysBar, nullptr, TRUE);
    }
    if (g_hKeyText && g_keyTextMode) {
        SetWindowPos(g_hKeyText, HWND_TOP, keysX + kHandleW + 6, top + kKeyTopH + 6,
                     keysW - kHandleW - 6 - 8, viewH - kKeyTopH - kKeyFootH - 12,
                     SWP_NOACTIVATE);
    }
}


// ---------------- 控件 ----------------

HWND mkBtn(HWND p, int id, const wchar_t* t) {
    return CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           0, 0, 80, 24, p, (HMENU)(INT_PTR)id, g_inst, nullptr);
}

void createControls(HWND hwnd) {
    // 终端：优先 RichEdit50W（支持着色），退化到 RichEdit20W / EDIT
    g_hView = nullptr;
    if (LoadLibraryW(L"Msftedit.dll")) {
        g_hView = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_NOHIDESEL, 0, 0, 100, 100, hwnd,
                                  (HMENU)IDC_VIEW, g_inst, nullptr);
        g_termReady = (g_hView != nullptr);
    }
    if (!g_hView) {
        g_hView = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT20W", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_NOHIDESEL, 0, 0, 100, 100, hwnd,
                                  (HMENU)IDC_VIEW, g_inst, nullptr);
        g_termReady = (g_hView != nullptr);
    }
    if (!g_hView) {
        g_hView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_NOHIDESEL, 0, 0, 100, 100, hwnd,
                                  (HMENU)IDC_VIEW, g_inst, nullptr);
    }
    if (g_hView) {
        SendMessageW(g_hView, EM_EXLIMITTEXT, 0, 20 * 1024 * 1024);
        // 关掉软换行：终端里“一行”就应该是屏幕上的一行，否则 readline 用 \r 重绘时
        // 会以为在刷新屏幕行，而我们按逻辑行处理，长命令就会花屏
        SendMessageW(g_hView, EM_SETTARGETDEVICE, 0, 1);
        // 订阅文本变化：用来抓 TSF 输入法绕过窗口消息直接写进来的上屏内容
        SendMessageW(g_hView, EM_SETEVENTMASK, 0, ENM_CHANGE);
        SetWindowSubclass(g_hView, ViewProc, 1, 0);
    }

    g_hStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                0, 0, 100, 20, hwnd, (HMENU)IDC_STATUS, g_inst, nullptr);

    // 右侧：快捷命令栏（自绘，条目可增删改）+ 折叠按钮
    g_hKeysBtn = CreateWindowExW(0, L"BUTTON", L"»",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                 0, 0, 20, 22, hwnd, (HMENU)IDC_KEYBTN, g_inst, nullptr);
    g_hKeysBar = CreateWindowExW(0, L"STATIC", L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY,
                                 0, 0, 150, 100, hwnd, (HMENU)IDC_KEYS, g_inst, nullptr);
    SetWindowSubclass(g_hKeysBar, KeyProc, 3, 0);

    // 右侧栏顶部：按钮 / 文本 模式切换
    g_hKeyMode = CreateWindowExW(0, L"BUTTON", L"文本模式",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_OWNERDRAW,
                                 0, 0, 150, 24, hwnd, (HMENU)IDC_KEYMODE, g_inst, nullptr);
    // 文本模式编辑框：一行一条命令，双击某行执行
    g_hKeyText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL |
                                 ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL | ES_WANTRETURN,
                                 0, 0, 100, 100, hwnd, (HMENU)IDC_KEYTEXT, g_inst, nullptr);
    if (g_hKeyText) {
        SendMessageW(g_hKeyText, EM_SETLIMITTEXT, 0, 64 * 1024);
        SendMessageW(g_hKeyText, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SetWindowSubclass(g_hKeyText, KeyTextProc, 4, 0);
    }

    // 左侧：连接信息栏（串口名 + 状态），右边缘中部为折叠把手
    g_hSideBtn = CreateWindowExW(0, L"BUTTON", L"«",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                 0, 0, 14, 132, hwnd, (HMENU)IDC_SIDEBTN, g_inst, nullptr);
    g_hSide = CreateWindowExW(0, L"STATIC", L"",
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY,
                              0, 0, 150, 100, hwnd, (HMENU)IDC_SIDE, g_inst, nullptr);
    SetWindowSubclass(g_hSide, SideProc, 2, 0);   // 右键：波特率 / 断开串口

    for (HWND h : {g_hStatus, g_hSide, g_hSideBtn, g_hKeysBar, g_hKeysBtn, g_hKeyMode, g_hKeyText})
        SendMessageW(h, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    updateKeysBtn();
    updateKeysModeBtn();
    applyFont();
    updateSide();
}

void refreshPorts() {
    auto ports = enumSerialPorts();
    if (ports.empty()) return;
    bool found = false;
    for (auto& p : ports) if (p == g_portName) { found = true; break; }
    if (!found) g_portName = ports[0];
}


// ---------------- 外观：字体与配色 ----------------

HFONT makeUiFont(int idx) {
    HDC dc = GetDC(g_hwnd);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(g_hwnd, dc);
    return CreateFontW(-MulDiv(kUiPts[idx], dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}


// 对话框字体（同时套到主窗口的 UI 控件上）

void applyUiFont() {
    HFONT old = g_fontUI;
    g_fontUI = makeUiFont(g_dlgPtIdx);
    if (old) DeleteObject(old);
    for (HWND hw : {g_hStatus, g_hSide, g_hSideBtn, g_hKeysBar, g_hKeysBtn, g_hKeyMode, g_hKeyText})
        if (hw) SendMessageW(hw, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    if (g_hwnd) layout(g_hwnd);
    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
    if (g_hKeysBar) InvalidateRect(g_hKeysBar, nullptr, TRUE);
}

void applyDlgStyle() {
    if (g_brushDlg) DeleteObject(g_brushDlg);
    g_brushDlg = CreateSolidBrush(g_dlgBg);
    if (g_dlgWnd) InvalidateRect(g_dlgWnd, nullptr, TRUE);
}

bool pickColor(HWND owner, COLORREF& c) {
    static COLORREF cust[16] = {0};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.rgbResult = c;
    cc.lpCustColors = cust;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return false;
    c = cc.rgbResult;
    return true;
}


// ---------------- 会话对话框 ----------------


// 对话框按字号整体缩放（以 10pt 为基准），避免字变大后控件挤出窗口


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
#ifdef TERM_TRACE
        // 自测用：模拟输入法把「大字体」留在插入点格式上（不写任何文字）
        case WM_APP + 30: {
            CHARRANGE end{-1, -1};
            SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&end);
            CHARFORMAT2W cf{};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE;
            cf.yHeight = g_fontPt * 2 * 20;                  // 双倍字号
            lstrcpynW(cf.szFaceName, L"SimSun", LF_FACESIZE);
            SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            return 0;
        }
        // 自测用：把插入点当前的字符格式写进 fmt.txt
        case WM_APP + 31: {
            CHARFORMAT2W q{};
            q.cbSize = sizeof(q);
            SendMessageW(g_hView, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&q);
            FILE* f = fopen("fmt.txt", "a");
            if (f) { fprintf(f, "yH=%d face=%ls\n", (int)q.yHeight, q.szFaceName); fclose(f); }
            return 0;
        }
        // 自测用：验证快捷命令在 SSH 会话下能不能发出去。
        // +40 = 按钮/右键「执行此命令」那条路（runShortcut）
        // +41 = 文本模式双击那条路（keyTextRunLine）
        case WM_APP + 40: runShortcut(0); return 0;
        case WM_APP + 41:
            if (g_hKeyText && !g_keys.empty()) {
                SetWindowTextW(g_hKeyText, g_keys[0].c_str());
                keyTextRunLine(0);
            }
            return 0;
#endif
        case WM_CREATE:
            g_hwnd = hwnd;
            createControls(hwnd);
            buildMenu();
            applyDlgStyle();
            layout(hwnd);
            refreshPorts();
            applyFont();
            updateStatus();
            appendRun(L"SerialMate " SM_VERSION_W L" · 「会话 → 新建会话」(Ctrl+N) 选择 串口 / SSH / Telnet / 远程串口。\r\n", C_HINT);
            appendRun(L"— 输出区可直接键盘输入（Enter 执行，↑↓ 调历史）—\r\n\r\n", C_HINT);
            // 开箱即用：按上次的配置建成第一条会话并连上
            {
                auto sp = std::make_unique<Session>();
                sp->id      = g_nextSessId++;
                sp->type    = g_connType;
                sp->port    = g_portName;
                sp->baud    = g_baud;
                sp->remIp   = g_remIp;
                sp->remCom  = g_remCom;
                sp->remBaud = g_remBaud;
                sp->host    = (g_connType == CONN_SSH) ? g_sshHost : g_telHost;
                sp->hostPort= (g_connType == CONN_SSH) ? g_sshPort : g_telPort;
                if (g_connType == CONN_SSH) {
                    sp->sshUser = g_sshUser;
                    sp->sshPass = g_sshPass;
                    sp->sshInfo = g_sshUser + L"@" + g_sshHost + L":" + std::to_wstring(g_sshPort);
                }
                sessionNameOf(sp.get());
                Session* s0 = sp.get();
                g_sess.push_back(std::move(sp));
                g_cur = s0;
                connectSession(s0);
                updateStatus();
            }
            if (g_cur && g_cur->term) g_cur->term->focus();
            else SetFocus(g_hView);
            return 0;

        case WM_SIZE:
            layout(hwnd);
            return 0;

        // 焦点必须始终待在输入区，否则键盘敲下去没人接（点到按钮/状态栏后打字全丢）
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE && !g_dlgWnd) {
                if (g_keyTextMode && g_hKeyText) SetFocus(g_hKeyText);
                else if (g_hView) SetFocus(g_hView);
            }
            return 0;

        case WM_SETFOCUS:
            if (!g_dlgWnd) {
                if (g_keyTextMode && g_hKeyText) SetFocus(g_hKeyText);
                else if (g_hView) SetFocus(g_hView);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (!g_dlgWnd) {
                if (g_keyTextMode && g_hKeyText) SetFocus(g_hKeyText);
                else if (g_hView) SetFocus(g_hView);
            }
            return 0;

        case WM_CHAR:                                  // 兜底：字符落到主窗口也喂给终端
            if (!g_dlgWnd && g_hView && wp >= 32) { termTypeChar((wchar_t)wp); return 0; }
            break;

        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = 900;
            mmi->ptMinTrackSize.y = 560;
            return 0;
        }

        case WM_INITMENUPOPUP:
            syncMenuChecks();
            return 0;

        case WM_APP_DATA: {
            auto* d = (std::string*)lp;
            Session* ss = sessionById((int)wp);
            if (ss && ss == g_cur) {
                showRx(*d);
                updateStatus();
            } else if (ss) {                       // 后台会话：先攒着，切回来再放
                std::lock_guard<std::mutex> lk(ss->mtx);
                ss->pending += *d;
                if (ss->pending.size() > 256 * 1024)
                    ss->pending.erase(0, ss->pending.size() - 192 * 1024);
            }
            delete d;
            return 0;
        }

        case WM_APP_TX: {
            auto* pr = (std::pair<std::string, std::string>*)lp;
            if (g_showAgent && (int)wp != 0 && sessionById((int)wp) != g_cur) { delete pr; return 0; }
            if (g_showAgent) {
                std::wstring who = (pr->first == "agent") ? L"AGENT" : utf8ToWide(pr->first);
                for (auto& c : who) c = towupper(c);
                std::wstring body = bytesToWide((const unsigned char*)pr->second.data(), (int)pr->second.size());
                for (auto& ch : body) if (ch == L'\r' || ch == L'\n') ch = L' ';
                std::wstring t = (g_showTs ? timestamp() : L"") + std::wstring(L"◀ [") + who + L"] ";
                appendRun(t + body + L"\r\n", C_AGENT);
            }
            delete pr;
            return 0;
        }

        case WM_APP_META: {
            auto* s = (std::wstring*)lp;
            appendRun(L"— " + *s + L" —\r\n", C_SYS);
            delete s;
            return 0;
        }

        case WM_APP_SSH_DATA: {
            auto* d = (std::string*)lp;
            Session* ss = sessionById((int)wp);
            if (ss && ss->term) {
                ss->term->write(*d);          // 终端自己保存画面，后台会话也能收到
                ss->rx += (long long)d->size();
                if (ss == g_cur) updateStatus();
            }
            delete d;
            return 0;
        }

        case WM_APP_SSH_LOG: {
            auto* p = (SshLog*)lp;
            Session* ss = sessionById((int)wp);
            if (ss && ss->term) {
                if (p->level == 1) {
                    ss->term->writeText(L"— " + p->text + L" —\r\n");
                } else {
                    ss->term->writeText(L"· " + p->text + L"\r\n");
                }
                if (ss == g_cur) updateStatus();
            }
            delete p;
            return 0;
        }

        case WM_APP_SSH_END: {
            auto* why = (std::wstring*)lp;
            Session* ss = sessionById((int)wp);
            if (ss && ss->term && !why->empty())
                ss->term->writeText(L"— " + *why + L" —\r\n");
            if (ss) {
                if (ss->type == CONN_TELNET) stopTelnet(ss);
                else                         stopSsh(ss);
                updateStatus();
                if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
            }
            delete why;
            return 0;
        }

        case WM_APP_DEAD: {
            auto* s = (std::wstring*)lp;
            Session* ds = sessionById((int)wp);
            if (!ds) ds = g_cur;
            if (ds == g_cur) appendRun(L"— " + *s + L" —\r\n", C_ERR);
            delete s;
            closePort(ds);
            updateStatus();
            if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
            return 0;
        }

        case WM_APP_PEER:
            updateStatus();
            return 0;

        case WM_APP_INPUT_RESET:
            // AI 注入前已经给板子发了 Ctrl-U，本地那份「还没回车的输入」一起作废，
            // 否则退格会按旧账本多发字节、上翻历史也会拿到一条根本没执行过的命令
            lineReset();
            return 0;

        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* ms = (MEASUREITEMSTRUCT*)lp;
            if (ms && ms->CtlType == ODT_MENU) { measureMenuItem(ms); return TRUE; }
            break;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* ds = (DRAWITEMSTRUCT*)lp;
            if (ds && ds->CtlType == ODT_MENU) {
                if (ds->itemData) drawMenuItem(ds, (const MenuItemData*)ds->itemData);
                else {
                    HBRUSH b = CreateSolidBrush(g_menuBg);
                    FillRect(ds->hDC, &ds->rcItem, b);
                    DeleteObject(b);
                }
                return TRUE;
            }
            if (wp == IDC_SIDE && ds) { drawSideBar(ds); return TRUE; }
            if (wp == IDC_KEYS && ds) { drawKeysBar(ds); return TRUE; }
            if (wp == IDC_KEYBTN && ds) {                 // 右侧栏折叠把手
                drawHandleBtn(ds);
                return TRUE;
            }
            if (wp == IDC_SIDEBTN && ds) {                // 左侧栏折叠把手（样式与右侧一致）
                drawHandleBtn(ds);
                return TRUE;
            }
            if (wp == IDC_KEYMODE && ds) {                // 顶部：按钮 / 文本 模式切换条
                RECT r = ds->rcItem;
                bool down = (ds->itemState & ODS_SELECTED) != 0;
                HBRUSH pb = CreateSolidBrush(down ? RGB(0xDD, 0xE6, 0xF5) : RGB(0xF2, 0xF4, 0xF8));
                FillRect(ds->hDC, &r, pb);
                DeleteObject(pb);
                HPEN sp = CreatePen(PS_SOLID, 1, RGB(0xD4, 0xDA, 0xE4));
                HGDIOBJ osp = SelectObject(ds->hDC, sp);
                MoveToEx(ds->hDC, r.left, r.bottom - 1, nullptr);
                LineTo(ds->hDC, r.right, r.bottom - 1);
                SelectObject(ds->hDC, osp);
                DeleteObject(sp);
                SetBkMode(ds->hDC, TRANSPARENT);
                SetTextColor(ds->hDC, RGB(0x3A, 0x4A, 0x66));
                wchar_t t[32] = L"";
                GetWindowTextW((HWND)ds->hwndItem, t, 32);
                HFONT ofm = (HFONT)SelectObject(ds->hDC, g_fontUI);
                DrawTextW(ds->hDC, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(ds->hDC, ofm);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);

            if (id == IDC_VIEW && HIWORD(wp) == EN_CHANGE) {
                // 文本变了：我们自己的改动 → 刷新权威副本；外来改动（TSF 上屏等）
                // → 起防抖定时器，等拼写结束后再统一撤掉转发
                dbgLog(L"EN_CHANGE self=%d", g_selfEdit ? 1 : 0);
                if (g_selfEdit) g_expect = viewText();
                else SetTimer(hwnd, IDT_IME, 350, nullptr);
                return 0;
            }

            if (id >= IDC_KEYBASE && id < IDC_KEYBASE + 8) {   // 兼容旧按钮 ID（已改用自绘）
                int i = id - IDC_KEYBASE;
                if (i < (int)g_keys.size()) {
                    termSendBytes(txEncode(g_keys[i]) + "\r", C_TX, nullptr);
                    lineReset();
                }
                if (g_hView) SetFocus(g_hView);
                return 0;
            }
            if (id == IDC_KEYMODE) {                            // 顶部：按钮 <-> 文本
                keyModeApply(!g_keyTextMode);
                return 0;
            }
            if (id == IDM_KEYS_TOGGLE || id == IDC_KEYBTN) {   // 右侧快捷命令栏 折叠/展开
                if (!g_keysOn && g_keyTextMode) keyTextCollect();   // 收起前先把文本改动写回
                g_keysOn = !g_keysOn;
                updateKeysBtn();
                saveSettings();
                syncMenuChecks();
                layout(hwnd);
                return 0;
            }
            if (id == IDM_KEY_EXEC)   { runShortcut(g_ctxKey); return 0; }
            if (id == IDM_KEY_EDIT)   { keyEdit(g_ctxKey);     return 0; }
            if (id == IDM_KEY_DEL)    { keyDel(g_ctxKey);      return 0; }
            if (id == IDM_KEY_ADD)    { keyAdd();              return 0; }
            if (id == IDM_KEY_EXPORT) { keyExport();           return 0; }
            if (id == IDM_KEY_IMPORT) { keyImport();           return 0; }
            if (id >= IDM_ENC_BASE && id <= IDM_ENC_BASE + 2) {
                applyEnc(id - IDM_ENC_BASE, true);
                return 0;
            }
            if (id == IDC_SIDEBTN || id == IDM_SIDE_TOGGLE) {   // 左侧信息栏折叠/展开
                g_sideOn = !g_sideOn;
                saveSettings();
                syncMenuChecks();
                layout(hwnd);
                updateSide();
                if (g_hView) SetFocus(g_hView);
                return 0;
            }
            if (id >= IDM_BAUD_BASE && id < IDM_BAUD_BASE + kBaudCount) {
                g_baudIdx = id - IDM_BAUD_BASE;
                g_baud = kBaits[g_baudIdx];
                Session* t = g_ctxSess ? g_ctxSess : g_cur;
                if (t) {
                    bool wasOn = t->on && t->type == CONN_SERIAL;
                    t->baud = g_baud;
                    if (wasOn) { closePort(t); openPort(t); }   // 换速率重开该会话
                    if (t == g_cur) appendRun(L"— 波特率改为 " + std::to_wstring(g_baud) + L" —\r\n", C_SYS);
                }
                saveSettings();
                syncMenuChecks();
                updateStatus();
                if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
                return 0;
            }
            // ---- 设置：菜单/对话框外观 ----
            if (id >= IDM_SET_MFONT_BASE && id < IDM_SET_MFONT_BASE + kUiPtCount) {
                g_menuPtIdx = id - IDM_SET_MFONT_BASE;
                applyMenuStyle();
                saveSettings();
                syncMenuChecks();
                return 0;
            }
            if (id >= IDM_SET_DFONT_BASE && id < IDM_SET_DFONT_BASE + kUiPtCount) {
                g_dlgPtIdx = id - IDM_SET_DFONT_BASE;
                applyUiFont();
                saveSettings();
                syncMenuChecks();
                return 0;
            }
            if (id == IDM_SET_MFG || id == IDM_SET_MBG || id == IDM_SET_MSEL ||
                id == IDM_SET_DFG || id == IDM_SET_DBG) {
                bool changed = false;
                if (id == IDM_SET_MFG)       changed = pickColor(hwnd, g_menuFg);
                else if (id == IDM_SET_MBG)  changed = pickColor(hwnd, g_menuBg);
                else if (id == IDM_SET_MSEL) changed = pickColor(hwnd, g_menuSel);
                else if (id == IDM_SET_DFG)  changed = pickColor(hwnd, g_dlgFg);
                else                         changed = pickColor(hwnd, g_dlgBg);
                if (changed) {
                    if (id == IDM_SET_DFG || id == IDM_SET_DBG) applyDlgStyle();
                    else applyMenuStyle();
                    saveSettings();
                }
                return 0;
            }
            if (id == IDM_SET_RESET) {
                g_menuPtIdx = 1;
                g_dlgPtIdx  = 1;
                g_menuFg  = RGB(0x24, 0x24, 0x24);
                g_menuBg  = RGB(0xF6, 0xF6, 0xF6);
                g_menuSel = RGB(0x2E, 0x7C, 0xE0);
                g_dlgFg   = RGB(0x24, 0x24, 0x24);
                g_dlgBg   = RGB(0xF2, 0xF3, 0xF5);
                applyMenuStyle();
                applyUiFont();
                applyDlgStyle();
                saveSettings();
                syncMenuChecks();
                return 0;
            }
            switch (id) {
                case IDM_PORT_OPEN:                 // 新建会话（原会话保留）
                    newSessionDialog();
                    if (g_hView) SetFocus(g_hView);   // 对话框关掉后把焦点还给输入区
                    return 0;
                case IDM_SESSION_RECONN:
                    if (g_cur) { closePort(g_cur); connectSession(g_cur); }
                    updateStatus();
                    syncMenuChecks();
                    return 0;
                case IDM_PORT_CLOSE: {
                    Session* t = g_ctxSess ? g_ctxSess : g_cur;
                    closePort(t);
                    syncMenuChecks();
                    updateStatus();
                    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
                    return 0;
                }
                case IDM_SESS_CLOSE: {
                    Session* t = g_ctxSess ? g_ctxSess : g_cur;
                    closeSession(t);
                    return 0;
                }
                case IDM_SIDE_CONN: {   // 左侧栏右键：连接该会话
                    Session* t = g_ctxSess ? g_ctxSess : g_cur;
                    if (t && !t->on) {
                        if (t != g_cur) switchSession(t);
                        connectSession(t);
                        saveSettings();
                        syncMenuChecks();
                        updateStatus();
                    }
                    return 0;
                }
                case IDM_SESSION_EXIT: DestroyWindow(hwnd); return 0;
                case IDM_PORT_REFRESH:
                    refreshPorts();
                    appendRun(L"— 端口列表已刷新 —\r\n", C_SYS);
                    return 0;
                case IDM_TOOL_STTY:
                    g_sttyAdapt = !g_sttyAdapt;
                    saveSettings();
                    syncMenuChecks();
                    if (g_sttyAdapt && curOn() && !isTermSess(g_cur))
                        termSendBytes("\x15shopt -s checkwinsize 2>/dev/null; stty cols 400 rows 100 2>/dev/null; COLUMNS=400; LINES=100; export COLUMNS LINES\r", C_TX, nullptr);
                    return 0;
                case IDM_PORT_DTR:
                    g_dtr = !g_dtr;
                    if (curOn() && !g_cur->isRemote) g_cur->ser.setDtr(g_dtr);
                    saveSettings();
                    syncMenuChecks();
                    return 0;
                case IDM_PORT_RTS:
                    g_rts = !g_rts;
                    if (curOn() && !g_cur->isRemote) g_cur->ser.setRts(g_rts);
                    saveSettings();
                    syncMenuChecks();
                    return 0;
                case IDM_PORT_SERVE:
                    g_serve = !g_serve;
                    if (g_serve) { startServer(); appendRun(L"— 接管服务已启用 127.0.0.1:7000 —\r\n", C_SYS); }
                    else { stopServer(); appendRun(L"— 接管服务已停止 —\r\n", C_SYS); }
                    saveSettings();
                    syncMenuChecks();
                    updateStatus();
                    return 0;
                case IDM_VIEW_BIGGER:
                    if (g_fontPt < 32) { g_fontPt++; applyFont(); saveSettings(); }
                    return 0;
                case IDM_VIEW_SMALLER:
                    if (g_fontPt > 8) { g_fontPt--; applyFont(); saveSettings(); }
                    return 0;
                case IDM_VIEW_RESET:
                    g_fontPt = 12;
                    applyFont();
                    saveSettings();
                    return 0;
                case IDM_VIEW_TS:
                    g_showTs = !g_showTs;
                    saveSettings();
                    syncMenuChecks();
                    return 0;
                case IDM_VIEW_AGENT:
                    g_showAgent = !g_showAgent;
                    saveSettings();
                    syncMenuChecks();
                    return 0;
                case IDM_VIEW_CLEAR:
                    termClearAll();
                    return 0;
                case IDM_VIEW_SAVE:
                    saveLog(hwnd);
                    return 0;
                case IDM_LOCAL_ECHO:
                    g_localEcho = !g_localEcho;
                    saveSettings();
                    appendRun(g_localEcho ? L"— 按键即时显示：开（本地先画，回显到达时丢弃） —\r\n"
                                          : L"— 按键即时显示：关（完全靠板子回显） —\r\n", C_SYS);
                    if (g_hView) SetFocus(g_hView);
                    return 0;
                case IDM_LOG_SAVE:
                    g_logSave = !g_logSave;
                    if (!g_logSave) closeLog();
                    saveSettings();
                    syncMenuChecks();
                    updateSide();
                    appendRun(g_logSave ? L"— 串口日志保存：开 —\r\n" : L"— 串口日志保存：关 —\r\n", C_SYS);
                    return 0;
                case IDM_LOG_DIR: {
                    std::wstring p;
                    if (pickLogDir(p)) {
                        g_logDir = p;
                        closeLog();
                        saveSettings();
                        updateSide();
                        appendRun(L"— 日志目录改为 " + p + L" —\r\n", C_SYS);
                    }
                    if (g_hView) SetFocus(g_hView);
                    return 0;
                }
                case IDM_LOG_OPEN: {
                    std::wstring d = logDirPath();
                    CreateDirectoryW(d.c_str(), nullptr);
                    ShellExecuteW(nullptr, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    if (g_hView) SetFocus(g_hView);
                    return 0;
                }
                case IDM_HELP_ABOUT:
                    MessageBoxW(hwnd,
                                L"SerialMate " SM_VERSION_W L"\n"
                                L"编译于 " SM_BUILD_W L"\n\n"
                                L"· 独占串口，内置接管服务 127.0.0.1:7000\n"
                                L"· 在输出区直接输入；↑↓ 调历史，Ctrl+C 中断\n"
                                L"· 视图菜单可调字号；配置存 serialmate.ini\n\n"
                                L"协议：newline JSON + base64 (in / out / tx / meta)\n"
                                L"命令行：serial_gui.exe --version 查看版本",
                                L"关于 SerialMate " SM_VERSION_W, MB_OK | MB_ICONINFORMATION);
                    return 0;
            }
            break;
        }

        case WM_TIMER:
            if (wp == IDT_IME) {
                KillTimer(hwnd, IDT_IME);
                bool composing = imeComposing();
                dbgLog(L"IDT_IME composing=%d", composing ? 1 : 0);
                if (composing) SetTimer(hwnd, IDT_IME, 300, nullptr);        // 还在拼写，继续等
                else syncForeignText();
                return 0;
            }
            if (wp == IDT_STATUS) {
                KillTimer(hwnd, IDT_STATUS);
                g_stDirty = false;
                g_stTick  = 0;              // 让下面这次一定刷进去
                updateStatus();
                return 0;
            }
            if (wp == IDT_STTY) {
                KillTimer(hwnd, IDT_STTY);
                // 让板子的 readline 知道终端很宽：超宽命令不会自己折行，
                // 避免它按 80 列重绘而与我们不换行的显示错位
                if (g_sttyAdapt && curOn() && !isTermSess(g_cur))
                    termSendBytes("\x15shopt -s checkwinsize 2>/dev/null; stty cols 400 rows 100 2>/dev/null; COLUMNS=400; LINES=100; export COLUMNS LINES\r", C_TX, nullptr);
                return 0;
            }
            break;

        case WM_DESTROY:
            if (g_keyTextMode) keyTextCollect();     // 文本模式下的改动也落盘
            saveSettings();
            for (auto& sp : g_sess) closePort(sp.get());
            stopServer();
            if (g_log) { fclose(g_log); g_log = nullptr; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}


// --version / -v：打印版本后直接退出，不建窗口。
// 这是个 -mwindows 进程，自己没有控制台，要 AttachConsole 挂到调用它的 cmd/bash 才能
// 看到输出；挂不上（比如双击带参数的快捷方式）就退化成弹窗，别让人以为程序没反应。

int DS(int v) { return (int)(v * g_dlgScale + 0.5); }
