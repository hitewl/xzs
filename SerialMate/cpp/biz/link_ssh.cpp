// link_ssh.cpp - SSH 会话：包装 ssh.cpp 的 SshClient + 终端控件

#include "sm.h"
void startSsh(Session* s) {
    if (!s || s->type != CONN_SSH || !g_hwnd) return;
    stopSsh(s);
    {   // 串口路径里才会初始化 winsock，SSH 自己也要确保一次
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    if (!s->term) {
        s->term.reset(new Terminal(g_hwnd, [s](const std::string& b) {
            if (s->ssh) {
                s->ssh->write(b);
                s->tx += (long long)b.size();
            }
        }));
        s->term->setFontPt(g_fontPt);
    }
    s->term->onResize = [s](int c, int r) {          // 窗口变化时同步远端 pty 大小
        if (s->ssh) s->ssh->resize(c, r);
    };
    if (s->sshInfo.empty())
        s->sshInfo = s->sshUser + L"@" + s->host + L":" + std::to_wstring(s->hostPort);
    if (s == g_cur) {
        layout(g_hwnd);                 // 先把终端摆好，拿到真实行列数再协商 pty
        s->term->showWindow(true);
        ShowWindow(g_hView, SW_HIDE);
        s->term->focus();
    }
    int cols = s->term->cols(), rows = s->term->rows();
    if (cols < 20) cols = 80;
    if (rows < 5) rows = 24;

    s->ssh.reset(new SshClient());
    int id = s->id;
    s->ssh->onData = [id](const std::string& d) {
        PostMessageW(g_hwnd, WM_APP_SSH_DATA, (WPARAM)id, (LPARAM)new std::string(d));
    };
    s->ssh->onLog = [id](const std::wstring& t, int lv) {
        PostMessageW(g_hwnd, WM_APP_SSH_LOG, (WPARAM)id, (LPARAM)new SshLog{t, lv});
    };
    s->ssh->onClosed = [id](const std::wstring& why) {
        PostMessageW(g_hwnd, WM_APP_SSH_END, (WPARAM)id, (LPARAM)new std::wstring(why));
    };
    s->on = true;
    s->term->writeText(L"正在连接 " + s->sshInfo + L" …\r\n");
    s->ssh->start(wideToUtf8(s->host), s->hostPort, wideToUtf8(s->sshUser),
                  wideToUtf8(s->sshPass), cols, rows);
    updateStatus();
}

void stopSsh(Session* s) {
    if (!s) return;
    if (s->ssh) {
        s->ssh->stop();
        s->ssh.reset();
    }
    s->on = false;
}


// ---------------- 终端输入 ----------------
