// ui_sidebar.cpp - 左侧会话栏：绘制、命中、右键菜单

#include "sm.h"
void drawHandleBtn(const DRAWITEMSTRUCT* ds) {
    RECT r = ds->rcItem;
    bool down = (ds->itemState & ODS_SELECTED) != 0;
    HBRUSH pb = CreateSolidBrush(down ? RGB(0xD2, 0xE0, 0xF8) : RGB(0xE3, 0xE7, 0xEE));
    FillRect(ds->hDC, &r, pb);
    DeleteObject(pb);
    HPEN pp = CreatePen(PS_SOLID, 1, RGB(0xB6, 0xBF, 0xCC));
    HGDIOBJ op1 = SelectObject(ds->hDC, pp);
    HGDIOBJ op2 = SelectObject(ds->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(ds->hDC, r.left, r.top, r.right, r.bottom);
    SelectObject(ds->hDC, op2);
    SelectObject(ds->hDC, op1);
    DeleteObject(pp);
    SetBkMode(ds->hDC, TRANSPARENT);
    SetTextColor(ds->hDC, RGB(0x4A, 0x5A, 0x74));
    wchar_t t[8] = L"";
    GetWindowTextW((HWND)ds->hwndItem, t, 8);
    HFONT of = (HFONT)SelectObject(ds->hDC, g_fontUI);
    DrawTextW(ds->hDC, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(ds->hDC, of);
}


// ---------------- 会话管理 ----------------


RECT sideRowRect(const RECT& rc, int i) {
    int y = rc.top + kSessPad + i * (kSessRowH + kSessGap) - g_sideScroll;
    return RECT{rc.left + 4, y, rc.right - kHandleW - 2, y + kSessRowH};
}

RECT sideNewRect(const RECT& rc) {
    return RECT{rc.left + 4, rc.bottom - kSessNewH - 4, rc.right - kHandleW - 2, rc.bottom - 4};
}

// 命中：>=0 会话下标；-2 新建会话；-1 空白

int sideHit(int y) {
    if (!g_hSide) return -1;
    RECT rc;
    GetClientRect(g_hSide, &rc);
    RECT nb = sideNewRect(rc);
    if (y >= nb.top && y < nb.bottom) return -2;
    for (int i = 0; i < (int)g_sess.size(); i++) {
        RECT r = sideRowRect(rc, i);
        if (y >= r.top && y < r.bottom) return i;
    }
    return -1;
}


// 侧栏整块重画不便宜（擦背景 + 逐个会话量文字画圆点），而 updateStatus 每收到
// 一段串口数据就会调它一次。会话列表、名字、连线状态都没变时，没必要重画。


void updateSide() {
    if (!g_hSide || !g_hSideBtn) return;
    SetWindowTextW(g_hSideBtn, g_sideOn ? L"«" : L"»");
    std::wstring sig;
    sig.reserve(g_sess.size() * 12);
    for (auto& s : g_sess) {
        sig += (s->on ? L'1' : L'0');
        sig += std::to_wstring(s->id);
        sig += s->name;
        sig += L',';
    }
    if (g_cur) { sig += L'|'; sig += std::to_wstring(g_cur->id); }
    if (sig == g_sideSig && g_sideSigOn == g_sideOn) return;
    g_sideSig = sig;
    g_sideSigOn = g_sideOn;
    InvalidateRect(g_hSide, nullptr, TRUE);   // 自绘，重画即可
}


// 左侧栏自绘：白底细框 + 会话列表 + 底部新建会话

void drawSideBar(const DRAWITEMSTRUCT* ds) {
    HDC dc = ds->hDC;
    RECT rc = ds->rcItem;
    HBRUSH bg = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    if (!g_sideOn || rc.right - rc.left < 40) return;

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xDC, 0xDC, 0xDC));
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);

    // 右边缘一条浅色轨道，折叠把手浮在上面（与右侧快捷栏对称）
    RECT rail{rc.right - kHandleW, rc.top + 1, rc.right - 1, rc.bottom - 1};
    HBRUSH rb = CreateSolidBrush(RGB(0xF8, 0xF9, 0xFB));
    FillRect(dc, &rail, rb);
    DeleteObject(rb);

    SetBkMode(dc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(dc, g_fontUI);

    RECT nb = sideNewRect(rc);
    int listBottom = nb.top - 4;

    for (int i = 0; i < (int)g_sess.size(); i++) {
        RECT r = sideRowRect(rc, i);
        if (r.bottom <= rc.top + 1 || r.top >= listBottom) continue;
        Session* ss = g_sess[i].get();
        bool act = (ss == g_cur);
        bool hv  = (i == g_sideHover);

        if (act || hv) {
            HBRUSH b = CreateSolidBrush(act ? RGB(0xE8, 0xF1, 0xFE) : RGB(0xF4, 0xF6, 0xF9));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        if (act) {
            RECT bar{r.left, r.top + 1, r.left + 3, r.bottom - 1};
            HBRUSH b = CreateSolidBrush(RGB(0x2E, 0x7C, 0xE0));
            FillRect(dc, &bar, b);
            DeleteObject(b);
        }

        // 状态圆点
        COLORREF c = ss->on ? RGB(0x2E, 0xA0, 0x43) : RGB(0xD8, 0x3B, 0x3B);
        int d = 8, cx = r.left + 8, cy = r.top + (r.bottom - r.top - d) / 2;
        HBRUSH db = CreateSolidBrush(c);
        HGDIOBJ ob2 = SelectObject(dc, db);
        HGDIOBJ op2 = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx, cy, cx + d, cy + d);
        SelectObject(dc, op2);
        SelectObject(dc, ob2);
        DeleteObject(db);

        // 名字 + 状态：名字量宽后紧接状态；放不下就把状态贴右边、名字省略
        int tx = cx + d + 6;
        int lim = r.right - 4;
        const wchar_t* stx = ss->on ? L"已连接" : L"未连接";
        SIZE sn{0, 0}, ss2{0, 0};
        GetTextExtentPoint32W(dc, ss->name.c_str(), (LONG)ss->name.size(), &sn);
        GetTextExtentPoint32W(dc, stx, (LONG)wcslen(stx), &ss2);
        if (tx + sn.cx + 5 + ss2.cx <= lim) {
            RECT r1{tx, r.top, lim, r.bottom};
            SetTextColor(dc, act ? RGB(0x14, 0x3A, 0x7A) : RGB(0x28, 0x28, 0x28));
            DrawTextW(dc, ss->name.c_str(), -1, &r1,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            RECT r2{tx + sn.cx + 5, r.top, lim, r.bottom};
            SetTextColor(dc, c);
            DrawTextW(dc, stx, -1, &r2,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        } else {
            RECT r2{lim - ss2.cx, r.top, lim, r.bottom};
            SetTextColor(dc, c);
            DrawTextW(dc, stx, -1, &r2,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            RECT r1{tx, r.top, r2.left - 4, r.bottom};
            SetTextColor(dc, act ? RGB(0x14, 0x3A, 0x7A) : RGB(0x28, 0x28, 0x28));
            DrawTextW(dc, ss->name.c_str(), -1, &r1,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }

    // 底部分隔线 + 新建会话
    {
        HPEN lp = CreatePen(PS_SOLID, 1, RGB(0xE6, 0xE6, 0xE6));
        HGDIOBJ o = SelectObject(dc, lp);
        MoveToEx(dc, rc.left + 4, nb.top - 5, nullptr);
        LineTo(dc, rc.right - kHandleW - 2, nb.top - 5);
        SelectObject(dc, o);
        DeleteObject(lp);

        if (g_sideHover == -2) {
            HBRUSH b = CreateSolidBrush(RGB(0xE8, 0xF1, 0xFE));
            FillRect(dc, &nb, b);
            DeleteObject(b);
        }
        SetTextColor(dc, RGB(0x2E, 0x7C, 0xE0));
        DrawTextW(dc, L"＋ 新建会话", -1, &nb,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    SelectObject(dc, of);
}


// 左侧栏右键菜单：按命中的会话给出操作

void showSideMenu(POINT pt) {
    if (!g_hSide || !g_sideOn) return;
    POINT cp = pt;
    ScreenToClient(g_hSide, &cp);
    int hit = sideHit(cp.y);
    g_ctxSess = (hit >= 0) ? g_sess[hit].get() : g_cur;
    Session* s = g_ctxSess;

    HMENU m = CreatePopupMenu();
    if (hit == -2 || hit < 0) {                       // 空白 / 新建按钮
        mAddMenu(m, MF_STRING, IDM_PORT_OPEN, L"新建会话…\tCtrl+N");
        if (!g_sess.empty()) {
            mAddMenu(m, MF_SEPARATOR, 0, nullptr);
            mAddMenu(m, MF_STRING | (s && s->on ? 0 : MF_GRAYED), IDM_SESS_CLOSE, L"关闭当前会话");
        }
    } else {
        HMENU mb = CreatePopupMenu();
        bool isSsh = s && s->type == CONN_SSH;
        if (!isSsh) {
            for (int i = 0; i < kBaudCount; i++) {
                wchar_t t[32];
                swprintf_s(t, L"%d", kBaits[i]);
                mAddMenu(mb, MF_STRING | (s && s->baud == kBaits[i] ? MF_CHECKED : 0),
                         IDM_BAUD_BASE + i, t);
            }
            mAddMenu(m, MF_POPUP, (UINT_PTR)mb, L"波特率");
            mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        } else {
            DestroyMenu(mb);
        }
        if (isSsh) {
            if (s->on) {
                mAddMenu(m, MF_STRING, IDM_PORT_CLOSE, L"断开 SSH");
            } else {
                std::wstring ct = L"重新连接 " + (s->sshInfo.empty() ? s->name : s->sshInfo);
                mAddMenu(m, MF_STRING, IDM_SIDE_CONN, ct.c_str());
            }
            mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        } else if (s && s->on) {
            mAddMenu(m, MF_STRING, IDM_PORT_CLOSE, L"断开串口");
        } else {
            wchar_t ct[160];
            swprintf_s(ct, L"连接串口 (%ls)", s ? s->name.c_str() : L"");
            mAddMenu(m, MF_STRING, IDM_SIDE_CONN, ct);
        }
        mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        mAddMenu(m, MF_STRING, IDM_PORT_OPEN, L"新建会话…\tCtrl+N");
        mAddMenu(m, MF_STRING, IDM_SESS_CLOSE, L"关闭此会话");
    }

    SetForegroundWindow(g_hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                  pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);                     // 子菜单会一并销毁
    if (cmd) SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)cmd, 0);
    g_ctxSess = nullptr;                // 只在本次菜单内有效，避免菜单栏命令误用
}


// 左侧栏子类化：左键切换会话 / 右键菜单

LRESULT CALLBACK SideProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
        // 侧栏的内容是「贴底」排的（新建会话贴窗口底部、列表按剩余高度裁剪）。
        // 窗口变大时系统只重绘新露出来的那一块，旧位置画过的「新建会话」会留在
        // 原地，放大窗口后就会出现一叠重复条目。这里强制整块重绘。
        case WM_SIZE:
            InvalidateRect(h, nullptr, TRUE);
            return 0;
        case WM_LBUTTONUP: {
            int hit = sideHit((short)HIWORD(l));
            if (hit >= 0) switchSession(g_sess[hit].get());
            else if (hit == -2) SendMessageW(g_hwnd, WM_COMMAND, IDM_PORT_OPEN, 0);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int hit = sideHit((short)HIWORD(l));
            if (hit != g_sideHover) { g_sideHover = hit; InvalidateRect(h, nullptr, TRUE); }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int row = (int)g_sess.size() * (kSessRowH + kSessGap);
            int max = row - (int)0;
            RECT rc; GetClientRect(h, &rc);
            max = row - (rc.bottom - 2 * kSessPad - kSessNewH - 8);
            if (max < 0) max = 0;
            int delta = GET_WHEEL_DELTA_WPARAM(w);
            g_sideScroll -= (delta / WHEEL_DELTA) * (kSessRowH + kSessGap);
            if (g_sideScroll > max) g_sideScroll = max;
            if (g_sideScroll < 0) g_sideScroll = 0;
            InvalidateRect(h, nullptr, TRUE);
            return 0;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT pt;
            if (m == WM_CONTEXTMENU && l != (LPARAM)-1) {
                pt.x = (short)LOWORD(l);
                pt.y = (short)HIWORD(l);
            } else GetCursorPos(&pt);
            showSideMenu(pt);
            return 0;
        }
    }
    return DefSubclassProc(h, m, w, l);
}


// 状态栏在「打字 + 回显 + 命令输出」时会被打到几十次每秒，每次都要
// SetWindowText + 重画侧栏，白白拖慢按键响应。这里 100ms 合并一次，
// 尾巴用一次性定时器补刷，所以数字最终仍然准确。

