// ui_dialogs.cpp - 对话框：新建会话 / 通用单行输入 / 目录选择

#include "sm.h"
HWND dcLabel(HWND p, const wchar_t* t, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT,
                             DS(x), DS(y), DS(w), DS(h), p, nullptr, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}

HWND dcEdit(HWND p, int id, const wchar_t* t, int x, int y, int w, DWORD ex) {
    HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ex,
                             DS(x), DS(y), DS(w), DS(24), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}

HWND dcRadio(HWND p, int id, const wchar_t* t, int x, int y, int w, bool first) {
    HWND c = CreateWindowExW(0, L"BUTTON", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                             (first ? WS_GROUP : 0),
                             DS(x), DS(y), DS(w), DS(22), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}

HWND dcCheck(HWND p, int id, const wchar_t* t, int x, int y, int w, bool first) {
    HWND c = CreateWindowExW(0, L"BUTTON", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX |
                             (first ? WS_GROUP : 0),
                             DS(x), DS(y), DS(w), DS(22), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}

HWND dcCombo(HWND p, int id, int x, int y, int w) {
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                             WS_VSCROLL | CBS_DROPDOWNLIST, DS(x), DS(y), DS(w), DS(240), p,
                             (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}

HWND dcGroup(HWND p, const wchar_t* t, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                             DS(x), DS(y), DS(w), DS(h), p, nullptr, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}

HWND dcBtn(HWND p, int id, const wchar_t* t, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                             DS(x), DS(y), DS(w), DS(h), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}

std::wstring editText(HWND h) {
    wchar_t b[512] = L"";
    if (h) GetWindowTextW(h, b, 512);
    return std::wstring(b);
}

std::wstring comboText(HWND h) {
    wchar_t b[256] = L"";
    int i = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (i >= 0) SendMessageW(h, CB_GETLBTEXT, i, (LPARAM)b);
    return std::wstring(b);
}

void dlgFillPorts() {
    if (!g_dcPort) return;
    SendMessageW(g_dcPort, CB_RESETCONTENT, 0, 0);
    auto ports = enumSerialPorts();
    int sel = 0;
    for (int i = 0; i < (int)ports.size(); i++) {
        SendMessageW(g_dcPort, CB_ADDSTRING, 0, (LPARAM)ports[i].c_str());
        if (ports[i] == g_portName) sel = i;
    }
    if (ports.empty()) { SendMessageW(g_dcPort, CB_ADDSTRING, 0, (LPARAM)L"COM3"); sel = 0; }
    SendMessageW(g_dcPort, CB_SETCURSEL, sel, 0);
}

void dlgFillBaud(HWND combo, int cur) {
    for (int i = 0; i < kBaudCount; i++) {
        wchar_t t[32];
        swprintf_s(t, L"%d", kBaits[i]);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)t);
    }
    int idx = baudIndexOf(cur);
    SendMessageW(combo, CB_SETCURSEL, idx < 0 ? 4 : idx, 0);
}

void dlgShowPage(ConnType t) {
    g_dlgType = t;
    for (int i = 0; i < 4; i++)
        for (HWND h : g_pg[i]) ShowWindow(h, (i == (int)t) ? SW_SHOW : SW_HIDE);
    if (g_dlgWnd) CheckRadioButton(g_dlgWnd, IDD_SER, IDD_REM, IDD_SER + (int)t);
}


// 通用单行输入对话框：标签 + 单行编辑框 + 确定/取消

void dlgBuildInput(HWND h) {
    g_dcIbLabel = dcLabel(h, g_ibPrompt.c_str(), 20, 18, 380);
    g_dcIbEdit  = dcEdit(h, IDD_IB_EDIT, g_ibText.c_str(), 20, 46, 380);
    dcBtn(h, IDD_OK,     L"确定", 226, 96, 82, 30);
    dcBtn(h, IDD_CANCEL, L"取消", 318, 96, 82, 30);
    ShowWindow(g_dcIbLabel, SW_SHOW);
    SetFocus(g_dcIbEdit);
    SendMessageW(g_dcIbEdit, EM_SETSEL, 0, -1);
}

void dlgBuild(HWND h) {
    for (int i = 0; i < 4; i++) g_pg[i].clear();
    if (g_ibMode) { dlgBuildInput(h); return; }   // 通用单行输入对话框

    // 连接类型
    dcGroup(h, L"连接类型", 12, 8, 446, 54);
    dcRadio(h, IDD_SER, L"串口",     30, 32, 72,  true);
    dcRadio(h, IDD_SSH, L"SSH",      112, 32, 66,  false);
    dcRadio(h, IDD_TEL, L"Telnet",   186, 32, 76,  false);
    dcRadio(h, IDD_REM, L"远程串口", 270, 32, 104, false);

    // ---- 页 0：串口 ----
    {
        auto& pg = g_pg[0];
        pg.push_back(dcLabel(h, L"串口:", 20, 80, 46));
        g_dcPort = dcCombo(h, IDD_PORT, 70, 76, 140);
        pg.push_back(g_dcPort);
        pg.push_back(dcBtn(h, IDD_PORTRF, L"刷新", 220, 76, 56, 26));
        pg.push_back(dcLabel(h, L"波特率:", 292, 80, 56));
        g_dcBaud = dcCombo(h, IDD_BAUD, 352, 76, 100);
        pg.push_back(g_dcBaud);

        pg.push_back(dcGroup(h, L"接管模式", 20, 116, 430, 62));
        g_dcMServe  = dcRadio(h, IDD_M_SERVE,  L"被接管（AI 可控制）", 36, 140, 178, true);
        g_dcMDirect = dcRadio(h, IDD_M_DIRECT, L"直连（独占串口）",    228, 140, 150, false);
        pg.push_back(g_dcMServe);
        pg.push_back(g_dcMDirect);

        g_dcDtr = dcCheck(h, IDD_DTR, L"DTR 有效", 36, 190, 100, true);
        g_dcRts = dcCheck(h, IDD_RTS, L"RTS 有效", 150, 190, 100, false);
        pg.push_back(g_dcDtr);
        pg.push_back(g_dcRts);

        pg.push_back(dcLabel(h,
            L"被接管：本程序在 127.0.0.1:7000 开放服务，AI 助手可直接下发命令；"
            L"直连：仅本窗口使用，不监听端口。", 20, 222, 430, 48));
    }

    // ---- 页 1：SSH ----
    {
        auto& pg = g_pg[1];
        pg.push_back(dcLabel(h, L"主机:", 20, 82, 46));
        g_dcSshHost = dcEdit(h, IDD_SSH_HOST, g_sshHost.c_str(), 74, 78, 190);
        pg.push_back(g_dcSshHost);
        pg.push_back(dcLabel(h, L"端口:", 280, 82, 46));
        g_dcSshPort = dcEdit(h, IDD_SSH_PORT, std::to_wstring(g_sshPort).c_str(), 330, 78, 70);
        pg.push_back(g_dcSshPort);

        pg.push_back(dcLabel(h, L"用户名:", 20, 118, 56));
        g_dcSshUser = dcEdit(h, IDD_SSH_USER, g_sshUser.c_str(), 84, 114, 178);
        pg.push_back(g_dcSshUser);
        pg.push_back(dcLabel(h, L"密码:", 280, 118, 46));
        g_dcSshPass = dcEdit(h, IDD_SSH_PASS, g_sshPass.c_str(), 322, 114, 122, ES_PASSWORD);
        pg.push_back(g_dcSshPass);

        pg.push_back(dcLabel(h, L"密码认证（自实现 SSH 协议，无需 PuTTY/plink 等外部程序）。", 20, 156, 430, 48));
    }

    // ---- 页 2：Telnet ----
    {
        auto& pg = g_pg[2];
        pg.push_back(dcLabel(h, L"主机:", 20, 82, 46));
        g_dcTelHost = dcEdit(h, IDD_TEL_HOST, g_telHost.c_str(), 74, 78, 190);
        pg.push_back(g_dcTelHost);
        pg.push_back(dcLabel(h, L"端口:", 280, 82, 46));
        g_dcTelPort = dcEdit(h, IDD_TEL_PORT, std::to_wstring(g_telPort).c_str(), 330, 78, 70);
        pg.push_back(g_dcTelPort);

        pg.push_back(dcLabel(h, L"Telnet：明文连接，支持 IAC 协商 / 窗口大小(NAWS) / 终端类型上报。\n适合 busybox telnetd、交换机、开发板。", 20, 120, 430, 48));
    }

    // ---- 页 3：远程串口 ----
    {
        auto& pg = g_pg[3];
        pg.push_back(dcLabel(h, L"远端 IP:", 20, 82, 66));
        g_dcRip = dcEdit(h, IDD_RIP, g_remIp.c_str(), 92, 78, 160);
        pg.push_back(g_dcRip);
        pg.push_back(dcLabel(h, L"远端 COM:", 272, 82, 72));
        g_dcRcom = dcEdit(h, IDD_RCOM, g_remCom.c_str(), 350, 78, 90);
        pg.push_back(g_dcRcom);

        pg.push_back(dcLabel(h, L"波特率:", 20, 118, 56));
        g_dcRbaud = dcCombo(h, IDD_RBAUD, 86, 114, 120);
        pg.push_back(g_dcRbaud);

        pg.push_back(dcLabel(h,
            L"远端 PC 上也运行本程序，勾选「启用本地接管服务」并打开对应串口（默认 7000 端口）。"
            L"连上后本窗口的键盘输入会经网络下发到那台机器的串口。", 20, 156, 430, 60));
    }

    // 底部
    dcBtn(h, IDD_OK,     L"确定", 272, 286, 82, 30);
    dcBtn(h, IDD_CANCEL, L"取消", 366, 286, 82, 30);
}

void dlgCollect() {
    if (g_dlgType == CONN_SERIAL) {
        std::wstring p = comboText(g_dcPort);
        if (!p.empty()) g_portName = p;
        int bi = (int)SendMessageW(g_dcBaud, CB_GETCURSEL, 0, 0);
        if (bi >= 0 && bi < kBaudCount) { g_baudIdx = bi; g_baud = kBaits[bi]; }
        g_serve = isChecked(g_dcMServe);
        g_dtr = isChecked(g_dcDtr);
        g_rts = isChecked(g_dcRts);
    } else if (g_dlgType == CONN_SSH) {
        g_sshHost = editText(g_dcSshHost);
        g_sshPort = (int)wcstol(editText(g_dcSshPort).c_str(), nullptr, 10);
        g_sshUser = editText(g_dcSshUser);
        g_sshPass = editText(g_dcSshPass);
        if (g_sshPort <= 0) g_sshPort = 22;
    } else if (g_dlgType == CONN_TELNET) {
        g_telHost = editText(g_dcTelHost);
        g_telPort = (int)wcstol(editText(g_dcTelPort).c_str(), nullptr, 10);
        if (g_telPort <= 0) g_telPort = 23;
    } else {
        g_remIp  = editText(g_dcRip);
        g_remCom = editText(g_dcRcom);
        int bi = (int)SendMessageW(g_dcRbaud, CB_GETCURSEL, 0, 0);
        if (bi >= 0 && bi < kBaudCount) g_remBaud = kBaits[bi];
    }
    g_connType = g_dlgType;
}

LRESULT CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, g_dlgFg);
            SetBkColor(dc, g_dlgBg);
            return (LRESULT)(g_brushDlg ? g_brushDlg : GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_CTLCOLORBTN:
            return (LRESULT)(g_brushDlg ? g_brushDlg : GetSysColorBrush(COLOR_BTNFACE));
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, g_dlgFg);
            SetBkColor(dc, RGB(0xFF, 0xFF, 0xFF));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect((HDC)wp, &rc, g_brushDlg ? g_brushDlg : GetSysColorBrush(COLOR_BTNFACE));
            return 1;
        }
        case WM_CREATE:
            dlgBuild(h);
            if (g_ibMode) return 0;
            dlgFillPorts();
            dlgFillBaud(g_dcBaud, g_baud);
            dlgFillBaud(g_dcRbaud, g_remBaud);
            SendMessageW(g_dcMServe,  BM_SETCHECK, g_serve ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_dcMDirect, BM_SETCHECK, g_serve ? BST_UNCHECKED : BST_CHECKED, 0);
            SendMessageW(g_dcDtr, BM_SETCHECK, g_dtr ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_dcRts, BM_SETCHECK, g_rts ? BST_CHECKED : BST_UNCHECKED, 0);
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id >= IDD_SER && id <= IDD_REM) {
                dlgShowPage((ConnType)(id - IDD_SER));
                return 0;
            }
            if (id == IDD_PORTRF) { dlgFillPorts(); return 0; }
            if (id == IDD_M_SERVE || id == IDD_M_DIRECT) {
                // 两个 radio 需要互斥（不同 WS_GROUP 组，这里手动同步）
                bool serve = (id == IDD_M_SERVE);
                SendMessageW(g_dcMServe,  BM_SETCHECK, serve ? BST_CHECKED : BST_UNCHECKED, 0);
                SendMessageW(g_dcMDirect, BM_SETCHECK, serve ? BST_UNCHECKED : BST_CHECKED, 0);
                return 0;
            }
            if (id == IDD_OK) {
                if (g_ibMode) {                       // 单行输入模式
                    g_ibText = editText(g_dcIbEdit);
                    g_dlgOK = true;
                    DestroyWindow(h);
                    return 0;
                }
                dlgCollect();
                g_dlgOK = true;
                DestroyWindow(h);
                return 0;
            }
            if (id == IDD_CANCEL) { DestroyWindow(h); return 0; }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(h);
            return 0;

        case WM_DESTROY:
            g_dlgDone = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int showSessionDialog() {
    g_dlgType = g_connType;
    g_dlgDone = false;
    g_dlgOK = false;

    RECT pr;
    GetWindowRect(g_hwnd, &pr);
    g_dlgScale = (double)kUiPts[g_dlgPtIdx] / 10.0;
    RECT wr{0, 0, DS(470), DS(328)};
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dw = wr.right - wr.left, dh = wr.bottom - wr.top;
    int x = pr.left + ((pr.right - pr.left) - dw) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - dh) / 2;

    EnableWindow(g_hwnd, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SerialMateDlg", L"新建会话",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               x, y, dw, dh, g_hwnd, nullptr, g_inst, nullptr);
    if (!dlg) { EnableWindow(g_hwnd, TRUE); return 0; }
    g_dlgWnd = dlg;
    dlgShowPage(g_dlgType);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    SetFocus(dlg);

    MSG msg;
    while (!g_dlgDone && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    g_dlgWnd = nullptr;
    EnableWindow(g_hwnd, TRUE);
    SetActiveWindow(g_hwnd);
    return g_dlgOK ? 1 : 0;
}


// 按当前 g_connType 建立连接
// 建连（对指定会话，默认当前会话）

void newSessionDialog() {
    if (!showSessionDialog()) return;
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
    Session* s = sp.get();
    g_sess.push_back(std::move(sp));
    switchSession(s);
    connectSession(s);
    saveSettings();
    syncMenuChecks();
    updateStatus();
    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
    if (s->term) s->term->focus();          // SSH 会话要喂终端控件，别把焦点塞给隐藏的 RichEdit
    else if (g_hView) SetFocus(g_hView);
}


// 关闭一条会话（断开并从列表移除）

bool showInputBox(const wchar_t* title, const wchar_t* prompt, std::wstring& val) {
    g_ibMode   = true;
    g_ibPrompt = prompt;
    g_ibText   = val;
    g_dlgDone  = false;
    g_dlgOK    = false;

    RECT pr;
    GetWindowRect(g_hwnd, &pr);
    g_dlgScale = (double)kUiPts[g_dlgPtIdx] / 10.0;
    RECT wr{0, 0, DS(420), DS(150)};
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dw = wr.right - wr.left, dh = wr.bottom - wr.top;
    int x = pr.left + ((pr.right - pr.left) - dw) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - dh) / 2;

    EnableWindow(g_hwnd, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SerialMateDlg", title,
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, dw, dh,
                               g_hwnd, nullptr, g_inst, nullptr);
    if (!dlg) {
        EnableWindow(g_hwnd, TRUE);
        g_ibMode = false;
        return false;
    }
    g_dlgWnd = dlg;
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    if (g_dcIbEdit) SetFocus(g_dcIbEdit); else SetFocus(dlg);

    MSG msg;
    while (!g_dlgDone && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    g_dlgWnd = nullptr;
    g_dcIbLabel = g_dcIbEdit = nullptr;
    EnableWindow(g_hwnd, TRUE);
    SetActiveWindow(g_hwnd);
    g_ibMode = false;
    if (g_dlgOK) val = g_ibText;
    return g_dlgOK;
}

bool pickLogDir(std::wstring& out) {
    BROWSEINFOW bi{};
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"选择串口日志保存目录（serial.log 写在这里）";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t buf[MAX_PATH] = L"";
    bool ok = SHGetPathFromIDListW(pidl, buf) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) out = buf;
    return ok;
}


// 在输出框上右键弹出：编辑命令 + 编码/时间戳/日志/信息栏 等常用设置
