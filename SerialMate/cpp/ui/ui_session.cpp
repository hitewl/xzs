// ui_session.cpp - 会话切换逻辑：切画面、选会话、状态栏刷新

#include "sm.h"
void applySessionView(Session* t) {
    if (t && isTermSess(t)) {
        // SSH 会话：画面在独立的终端控件里，切过来只要把它显示出来
        { std::lock_guard<std::mutex> lk(g_textMtx); g_allText = t->allText; }
        if (g_hwnd) layout(g_hwnd);
        return;
    }
    termClearAll();
    viewLoadRtf(t->rtf);
    { std::lock_guard<std::mutex> lk(g_textMtx); g_allText = t->allText; }
    // 光标落到画面末尾：后台补放的字节接在后面写，不去覆盖已画好的行
    // （跨会话接着改同一行的字节账本对不上，硬接会把提示符重画成 root@root@…）
    g_lb.clear();
    g_lc = 0;
    g_lshow.clear();
    g_lcp = caretToEnd();
    g_curTs.clear();        // 渲染串里的时间戳不能跨会话复用
    g_tsNeed = (t->lineShow.empty() && t->lineBuf.empty());   // 上一行已收尾才补时间戳
    lineReset();
    // 整屏换掉不是用户输入：刷新「外来文本」基准，并在一小段时间内直接忽略外来文本判定，
    // 否则整屏恢复会被当成 IME 上屏，整段转发到串口
    KillTimer(g_hwnd, IDT_IME);
    g_selfEdit = true;
    g_expect = viewText();
    g_selfEdit = false;
    g_foreignGuard = GetTickCount() + 800;
    // 从 SSH 终端切回串口会话时，必须重排一次：把别的会话的终端控件收起来、
    // 让 RichEdit 重新显示，否则 SSH 终端窗口一直盖在上面，看起来像「没切换」
    if (g_hwnd) layout(g_hwnd);
    termEnsureCaret();                     // 切过去后插入点落在最后一行行尾
}


// 切换会话：先把当前画面存进旧会话，再把目标会话的画面装载进来

void switchSession(Session* t) {
    if (!t || t == g_cur || !g_hView) return;
    if (g_cur && !isTermSess(g_cur)) {   // SSH 画面在终端控件里，别拿 RichEdit 覆盖它
        viewSaveRtf(g_cur->rtf);
        { std::lock_guard<std::mutex> lk(g_textMtx); g_cur->allText = g_allText; }
        g_cur->lineBuf  = g_lb;
        g_cur->lineCur  = g_lc;
        g_cur->lineShow = g_lshow;
        g_cur->tsNeed   = g_tsNeed;
    }
    g_cur = t;
    applySessionView(t);

    // 后台期间收到的数据，切回来时按原样补上
    std::string pend;
    { std::lock_guard<std::mutex> lk(t->mtx); pend.swap(t->pending); }
    if (!pend.empty()) showRx(pend);

    updateStatus();
    syncMenuChecks();
    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
    // 焦点跟着画面走：SSH 喂终端控件，串口喂 RichEdit，否则切过去敲键盘没人接
    if (isTermSess(t) && t->term) t->term->focus();
    else if (g_hView) SetFocus(g_hView);
}


// 左侧会话列表：每行一个会话（圆点 + 名字 + 状态），底部一行「新建会话」


void updateStatus() {
    DWORD now = GetTickCount();
    if (now - g_stTick < 100) {
        if (!g_stDirty) { g_stDirty = true; SetTimer(g_hwnd, IDT_STATUS, 120, nullptr); }
        return;
    }
    g_stTick  = now;
    g_stDirty = false;
    wchar_t buf[512];
    long long rx = g_cur ? g_cur->rx : 0, tx = g_cur ? g_cur->tx : 0;
    if (g_cur && isTermSess(g_cur)) {
        bool live = (g_cur->ssh && g_cur->ssh->alive());
        swprintf_s(buf, L"%ls %ls | 会话 %d | RX %lld B  TX %lld B | 端口 %d x %d",
                   live ? L"已连接" : L"未连接",
                   g_cur->sshInfo.empty() ? g_cur->host.c_str() : g_cur->sshInfo.c_str(),
                   (int)g_sess.size(), rx, tx,
                   g_cur->term ? g_cur->term->cols() : 0, g_cur->term ? g_cur->term->rows() : 0);
        SetWindowTextW(g_hStatus, buf);
        updateSide();
        return;
    }
    if (curOn()) {
        // 「已连接」只在真的收到过字节之后才敢说。串口打开成功和板子在不在线是两件事：
        // 断电、线没接、波特率不对，句柄照样打得开。
        const wchar_t* lead = L"已连接";
        if (g_devState == DEV_PROBING)     lead = L"串口已打开(探测设备中)";
        else if (g_devState == DEV_SILENT) lead = L"串口已打开(设备无响应)";
        // 有 AI 连着时把话说白：串口是独占的，这时本地敲键盘会被接管命令打断
        if ((int)g_peerCount > 0)
            swprintf_s(buf, L"%ls %ls @%d | %ls | 会话 %d | AI 接管中(%d)，本地输入仍可用但会被注入打断 | RX %lld B  TX %lld B",
                       lead, g_cur->name.c_str(), g_cur->baud, encName(), (int)g_sess.size(),
                       (int)g_peerCount, rx, tx);
        else
            swprintf_s(buf, L"%ls %ls @%d | %ls | 会话 %d | 接管 :7000(0) | RX %lld B  TX %lld B",
                       lead, g_cur->name.c_str(), g_cur->baud, encName(), (int)g_sess.size(), rx, tx);
    } else {
        swprintf_s(buf, L"未连接 %ls | 会话 %d | RX %lld B  TX %lld B | 会话菜单 → 新建会话 (Ctrl+N)",
                   g_cur ? g_cur->name.c_str() : L"", (int)g_sess.size(), rx, tx);
    }
    SetWindowTextW(g_hStatus, buf);
    updateSide();
}


// ---------------- 写字工具 ----------------

void connectSession(Session* s) {
    if (!s) s = g_cur;
    if (!s) return;
    if (s->type == CONN_REMOTE) {
        // 注意：**不能** stopServer()。本机自环测试就是「COM3 会话提供接管服务 +
        // 远程串口会话连自己 127.0.0.1:7000」，停掉服务等于把自己掐死。
        openRemote(s);
    } else if (s->type == CONN_SSH) {
        startSsh(s);
    } else if (s->type == CONN_TELNET) {
        startTelnet(s);
    } else if (s->type == CONN_SERIAL) {
        if (g_serve) startServer(); else stopServer();
        openPort(s);
    }
}


// 新建会话：把对话框里的参数做成一条新会话，接进列表并切过去（原会话不动）

void closeSession(Session* s) {
    if (!s) return;
    closePort(s);
    auto it = std::find_if(g_sess.begin(), g_sess.end(),
                           [&](const std::unique_ptr<Session>& p) { return p.get() == s; });
    if (it == g_sess.end()) return;
    if (g_ctxSess == s) g_ctxSess = nullptr;
    bool wasCur = (s == g_cur);
    int idx = (int)(it - g_sess.begin());
    g_sess.erase(it);
    if (wasCur) {
        g_cur = nullptr;
        if (!g_sess.empty()) {
            int ni = idx < (int)g_sess.size() ? idx : (int)g_sess.size() - 1;
            Session* t = g_sess[ni].get();
            g_cur = t;
            applySessionView(t);
            std::string pend;
            { std::lock_guard<std::mutex> lk(t->mtx); pend.swap(t->pending); }
            if (!pend.empty()) showRx(pend);
        }
    }
    updateStatus();
    syncMenuChecks();
    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
}


// ================= 右侧快捷命令栏 =================

