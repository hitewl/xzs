// link_telnet.cpp - Telnet(RFC 854) 客户端
//
// 和 SSH 的差别：Telnet 是明文的，全部工作就是
//   1) TCP 连上去；
//   2) 把 IAC 协商序列吃掉（否则 255 开头的字节会当成乱码打到屏幕上）；
//   3) 剩下的字节原样交给终端控件，和 SSH 共用一套 VT100 渲染；
//   4) 本地窗口大小变化时用 NAWS 告诉服务端，否则 vim/top 会按 80x24 排版。
//
// 线程模型：每条 Telnet 会话一个读线程（recv 阻塞），数据经 WM_APP_SSH_DATA
// 回到 GUI 线程渲染 —— 和 SSH 同一条通道，界面代码不需要区分两者。

#include "sm.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// ---- Telnet 命令字（RFC 854 / 855 / 1073 / 1091） ----
enum {
    TN_SE   = 240,
    TN_SB   = 250,
    TN_WILL = 251,
    TN_WONT = 252,
    TN_DO   = 253,
    TN_DONT = 254,
    TN_IAC  = 255,
};
enum {
    TN_OPT_BINARY = 0,
    TN_OPT_ECHO   = 1,
    TN_OPT_SGA    = 3,      // suppress go-ahead
    TN_OPT_TTYPE  = 24,     // terminal type
    TN_OPT_NAWS   = 31,     // window size
};

static bool tnAcceptedWill(int opt) {
    return opt == TN_OPT_ECHO || opt == TN_OPT_SGA || opt == TN_OPT_TTYPE || opt == TN_OPT_BINARY;
}
static bool tnAcceptedDo(int opt) {
    return opt == TN_OPT_SGA || opt == TN_OPT_TTYPE || opt == TN_OPT_NAWS || opt == TN_OPT_BINARY;
}

// 把一串原始字节发出去（0xFF 必须转义成 0xFF 0xFF，否则对端当成 IAC）
bool telnetWrite(Session* s, const std::string& b) {
    if (!s || s->sock == INVALID_SOCKET) return false;
    std::string d;
    d.reserve(b.size() + 8);
    for (unsigned char c : b) {
        d.push_back((char)c);
        if (c == TN_IAC) d.push_back((char)TN_IAC);
    }
    std::lock_guard<std::mutex> lk(g_wrMtx);
    int off = 0;
    while (off < (int)d.size()) {
        int n = ::send(s->sock, d.c_str() + off, (int)d.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += n;
    }
    return true;
}

// NAWS：把当前行列数报给对端
static void tnSendNaws(Session* s, int cols, int rows) {
    if (!s || s->sock == INVALID_SOCKET) return;
    if (cols <= 0 || rows <= 0) return;
    if (cols > 254) cols = 254;          // 单字节会被当成 IAC 转义，限一下
    if (rows > 254) rows = 254;
    std::string b;
    b += (char)TN_IAC; b += (char)TN_SB; b += (char)TN_OPT_NAWS;
    b += (char)(cols >> 8); b += (char)(cols & 0xFF);
    b += (char)(rows >> 8); b += (char)(rows & 0xFF);
    b += (char)TN_IAC; b += (char)TN_SE;
    std::lock_guard<std::mutex> lk(g_wrMtx);
    ::send(s->sock, b.c_str(), (int)b.size(), MSG_NOSIGNAL);
}

// 解析收到的字节流：IAC 序列消化掉并生成应答，其余原样返回
// 说明：控制序列都很短，遇到半包（缓冲区被截断）就直接丢弃，下一次读会重新同步，
// 这在真实链路上不会造成可见影响。
static std::string tnFilter(Session* s, const std::string& in, std::string& reply) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = (unsigned char)in[i];
        if (c != TN_IAC) { out.push_back((char)c); ++i; continue; }
        if (i + 1 >= in.size()) break;
        unsigned char cmd = (unsigned char)in[i + 1];

        if (cmd == TN_IAC) { out.push_back((char)TN_IAC); i += 2; continue; }   // 转义的 0xFF

        if (cmd == TN_WILL || cmd == TN_WONT || cmd == TN_DO || cmd == TN_DONT) {
            if (i + 2 >= in.size()) break;
            unsigned char opt = (unsigned char)in[i + 2];
            if (cmd == TN_WILL)      reply += std::string{ (char)TN_IAC, (char)(tnAcceptedWill(opt) ? TN_DO : TN_DONT), (char)opt };
            else if (cmd == TN_DO) {
                reply += std::string{ (char)TN_IAC, (char)(tnAcceptedDo(opt) ? TN_WILL : TN_WONT), (char)opt };
                if (opt == TN_OPT_NAWS && s->term)
                    tnSendNaws(s, s->term->cols(), s->term->rows());
            }
            // WONT / DONT 不需要应答
            i += 3;
            continue;
        }

        if (cmd == TN_SB) {                              // 子协商，找到 IAC SE 为止
            size_t j = i + 2;
            while (j + 1 < in.size() &&
                   !((unsigned char)in[j] == TN_IAC && (unsigned char)in[j + 1] == TN_SE)) ++j;
            if (j + 1 >= in.size()) break;               // 子协商还没收完
            if (i + 2 < in.size() && (unsigned char)in[i + 2] == TN_OPT_TTYPE) {
                // 对端在问终端类型：回 XTERM-256COLOR，颜色/清屏就都对了
                static const char kTT[] = "XTERM-256COLOR";
                reply += std::string{ (char)TN_IAC, (char)TN_SB, (char)TN_OPT_TTYPE, (char)1 };   // 1 = IS
                reply.append(kTT, sizeof(kTT) - 1);
                reply += std::string{ (char)TN_IAC, (char)TN_SE };
            }
            i = j + 2;
            continue;
        }

        i += 2;                                          // 其他两字节命令（NOP/GA…）忽略
    }
    return out;
}

static void telnetThread(Session* s) {
    char buf[4096];
    // 连上后先声明能力：SGA 不等待、NAWS 报尺寸、TTYPE 报类型
    std::string hello;
    hello += std::string{ (char)TN_IAC, (char)TN_WILL, (char)TN_OPT_SGA };
    hello += std::string{ (char)TN_IAC, (char)TN_WILL, (char)TN_OPT_NAWS };
    hello += std::string{ (char)TN_IAC, (char)TN_WILL, (char)TN_OPT_TTYPE };
    hello += std::string{ (char)TN_IAC, (char)TN_DO,   (char)TN_OPT_SGA };
    {
        std::lock_guard<std::mutex> lk(g_wrMtx);
        ::send(s->sock, hello.c_str(), (int)hello.size(), MSG_NOSIGNAL);
    }
    if (s->term) tnSendNaws(s, s->term->cols(), s->term->rows());

    while (!s->stopRead) {
        int n = ::recv(s->sock, buf, sizeof(buf), 0);
        if (n <= 0) {
            if (s->stopRead) break;
            PostMessageW(g_hwnd, WM_APP_SSH_END, (WPARAM)s->id,
                         (LPARAM)new std::wstring(L"Telnet 连接已断开"));
            break;
        }
        std::string reply;
        std::string data = tnFilter(s, std::string(buf, (size_t)n), reply);
        if (!reply.empty()) {
            std::lock_guard<std::mutex> lk(g_wrMtx);
            ::send(s->sock, reply.c_str(), (int)reply.size(), MSG_NOSIGNAL);
        }
        if (!data.empty())
            PostMessageW(g_hwnd, WM_APP_SSH_DATA, (WPARAM)s->id, (LPARAM)new std::string(data));
    }
}

void startTelnet(Session* s) {
    if (!s || s->type != CONN_TELNET || !g_hwnd) return;
    stopTelnet(s);
    { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); }

    SOCKET sk = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sk == INVALID_SOCKET) { appendRun(L"— 创建套接字失败 —\r\n", C_ERR); return; }

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons((u_short)s->hostPort);
    std::string ip = wideToUtf8(s->host);
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) {
        ::closesocket(sk);
        appendRun(L"— Telnet 主机地址无效：" + s->host + L" —\r\n", C_ERR);
        return;
    }

    // 3 秒连接超时，别把界面卡住
    u_long nb = 1;
    ::ioctlsocket(sk, FIONBIO, &nb);
    ::connect(sk, (sockaddr*)&a, sizeof(a));
    fd_set wf, ef;
    FD_ZERO(&wf); FD_SET(sk, &wf);
    FD_ZERO(&ef); FD_SET(sk, &ef);
    timeval tv{3, 0};
    int rc = ::select(0, nullptr, &wf, &ef, &tv);
    bool ok = (rc > 0) && FD_ISSET(sk, &wf);
    if (ok) {
        int soerr = 0, len = sizeof(soerr);
        ::getsockopt(sk, SOL_SOCKET, SO_ERROR, (char*)&soerr, &len);
        if (soerr != 0) ok = false;
    }
    nb = 0;
    ::ioctlsocket(sk, FIONBIO, &nb);
    if (!ok) {
        ::closesocket(sk);
        appendRun(L"— 连不上 " + s->host + L":" + std::to_wstring(s->hostPort) +
                  L"（Telnet 服务是否开着？）—\r\n", C_ERR);
        updateStatus();
        return;
    }
    int one = 1;
    ::setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));

    s->sock = sk;
    s->on = true;
    s->stopRead = false;
    if (s->sshInfo.empty())
        s->sshInfo = s->host + L":" + std::to_wstring(s->hostPort);

    if (!s->term) {
        s->term.reset(new Terminal(g_hwnd, [s](const std::string& b) {
            if (telnetWrite(s, b)) s->tx += (long long)b.size();
        }));
        s->term->setFontPt(g_fontPt);
    }
    s->term->onResize = [s](int c, int r) { tnSendNaws(s, c, r); };

    if (s == g_cur) {
        layout(g_hwnd);
        s->term->showWindow(true);
        ShowWindow(g_hView, SW_HIDE);
        s->term->focus();
    }
    s->term->writeText(L"正在连接 " + s->sshInfo + L" …\r\n");
    s->reader = std::thread(telnetThread, s);
    updateStatus();
}

void stopTelnet(Session* s) {
    if (!s) return;
    s->stopRead = true;
    if (s->sock != INVALID_SOCKET) {
        ::shutdown(s->sock, SD_BOTH);
        ::closesocket(s->sock);
        s->sock = INVALID_SOCKET;
    }
    if (s->reader.joinable()) s->reader.join();
    s->on = false;
}
