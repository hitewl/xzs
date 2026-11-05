// link.cpp - 连接调度：按会话类型分发读/写/断开

#include "sm.h"
void readerProc(Session* s) {
    unsigned char buf[8192];

    // ---- 远端串口：走 TCP，把对端的 out 帧当成本机 RX ----
    if (s->isRemote) {
        std::string acc;
        char tmp[4096];
        while (!s->stopRead) {
            int n = ::recv(s->sock, tmp, sizeof(tmp), 0);
            if (n <= 0) {
                if (s->stopRead) break;
                PostMessageW(g_hwnd, WM_APP_DEAD, (WPARAM)s->id, (LPARAM)new std::wstring(L"远程串口连接已断开"));
                break;
            }
            acc.append(tmp, (size_t)n);
            size_t pos;
            while ((pos = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);
                if (line.size() < 5) continue;
                std::string t   = jsonGetStr(line, "t");
                std::string b64 = jsonGetStr(line, "b");
                if (t == "out" && !b64.empty()) {
                    auto vb = b64decode(b64);
                    postSessionData(s, std::string(vb.begin(), vb.end()));
                } else if (t == "tx" && !b64.empty()) {
                    std::string src = jsonGetStr(line, "src");
                    if (src != "gui") {
                        auto vb = b64decode(b64);
                        auto* pr = new std::pair<std::string, std::string>(
                            src, std::string(vb.begin(), vb.end()));
                        PostMessageW(g_hwnd, WM_APP_TX, (WPARAM)s->id, (LPARAM)pr);
                    }
                }
            }
        }
        return;
    }

    // ---- 本地串口 ----
    while (!s->stopRead) {
        int n = s->ser.read(buf, sizeof(buf));
        if (n < 0) {
            if (s->stopRead) break;
            if (!s->ser.isOpen()) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
            PostMessageW(g_hwnd, WM_APP_DEAD, (WPARAM)s->id, (LPARAM)new std::wstring(L"串口读错误，连接已断开"));
            break;
        }
        if (n > 0) {
            postSessionData(s, std::string((const char*)buf, (size_t)n));
            broadcastLine("{\"t\":\"out\",\"b\":\"" +
                              b64encode(buf, n) + "\",\"ts\":0}", nullptr);
        }
    }
}

void closePort(Session* s) {
    if (!s) s = g_cur;
    if (!s || !s->on) return;
    if (s->type == CONN_SSH || s->type == CONN_TELNET) {
        if (s->type == CONN_SSH) stopSsh(s);
        else                     stopTelnet(s);
        if (s == g_cur) {
            appendRun(s->type == CONN_SSH ? L"— SSH 连接已断开 —\r\n"
                                          : L"— Telnet 连接已断开 —\r\n", C_SYS);
            updateStatus();
        }
        return;
    }
    s->stopRead = true;
    if (s->isRemote && s->sock != INVALID_SOCKET) {
        ::shutdown(s->sock, SD_BOTH);
        ::closesocket(s->sock);
        s->sock = INVALID_SOCKET;
    }
    if (s->reader.joinable()) s->reader.join();
    if (!s->isRemote) s->ser.close();
    s->isRemote = false;
    s->on = false;
    if (g_hubSess == s) {               // 提供串口的会话断了，接管服务暂时没有可写的口
        g_hubSess = nullptr;
        for (auto& sp : g_sess) {
            Session* q = sp.get();
            if (q != s && q->on && !q->isRemote && q->type == CONN_SERIAL && q->ser.isOpen()) {
                g_hubSess = q;
                break;
            }
        }
    }
    if (s == g_cur) {
        appendRun(L"— 连接已断开 —\r\n", C_SYS);
        updateStatus();
    }
}


// ---------------- SSH 会话 ----------------


void termSendBytes(const std::string& bytes, COLORREF color, const wchar_t* tag) {
    if (bytes.empty()) return;
    // SSH 会话必须单独走 ssh 通道。
    // 原来这里只有「串口/远程串口」一条路（serialWrite），SSH 会话下 serialWrite 看到
    // isRemote=false、串口没打开就直接 false，于是快捷命令（按钮、右键执行、
    // 文本模式双击）全部报「— 发送失败 —」。键盘打字能用的原因是它走的是
    // Terminal 的 onInput 回调，压根没经过这个函数。
    if (g_cur && g_cur->type == CONN_SSH) {
        if (!g_cur->ssh || !g_cur->ssh->alive()) {
            appendRun(L"— SSH 会话未连接 —\r\n", C_ERR);
            return;
        }
        g_cur->ssh->write(bytes);
        g_cur->tx += (long long)bytes.size();
        logLine(L"TX", bytes);
        if (tag) appendRun(tag + rxDecode(bytes) + L"\r\n", color);
        updateStatus();
        return;
    }
    if (g_cur && g_cur->type == CONN_TELNET) {
        if (!g_cur->on || g_cur->sock == INVALID_SOCKET) {
            appendRun(L"— Telnet 会话未连接 —\r\n", C_ERR);
            return;
        }
        if (!telnetWrite(g_cur, bytes)) { appendRun(L"— 发送失败 —\r\n", C_ERR); return; }
        g_cur->tx += (long long)bytes.size();
        logLine(L"TX", bytes);
        if (tag) appendRun(tag + rxDecode(bytes) + L"\r\n", color);
        updateStatus();
        return;
    }
    if (!curOn()) {
        appendRun(L"— 未连接，请用 会话 → 新建会话(Ctrl+N) 建立连接 —\r\n", C_ERR);
        return;
    }
    if (!serialWrite(bytes)) { appendRun(L"— 发送失败 —\r\n", C_ERR); return; }
    if (g_cur) g_cur->tx += (long long)bytes.size();
    logLine(L"TX", bytes);
    if (tag) appendRun(tag + rxDecode(bytes) + L"\r\n", color);
    broadcastLine("{\"t\":\"tx\",\"b\":\"" + b64encode((const unsigned char*)bytes.data(), (int)bytes.size()) +
                      "\",\"src\":\"gui\"}", nullptr);
    updateStatus();
}


// ---------------- 输入行记账 ----------------
// 行编辑器（板子上的 readline/sh）是按**字节**退格的：一个汉字 UTF-8 是 3 字节、GBK 是 2 字节，
// 少发一个就只是把字删掉一半（屏幕上看着像“退格删不动/删出乱码”）。
// 所以这里给 g_line 每个字符记下它真正发出去的字节数，退格时按记录精确回退。



// 弹掉最后一个字符，返回它发出去时的字节数（0 表示本地没有记录）
