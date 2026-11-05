// link_remote.cpp - 远程串口：连对端接管服务(:7000)，解析 JSON 帧

#include "sm.h"
bool openRemote(Session* s) {
    if (!s) return false;
    if (s->on) return true;

    std::string ip = wideToUtf8(s->remIp);
    for (auto& c : ip) if (c == ' ') c = '\0';
    ip = ip.c_str();

    {   // 远程会话可能比任何串口会话都先建，winsock 得自己确保初始化
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    SOCKET sk = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sk == INVALID_SOCKET) { appendRun(L"— 创建套接字失败 —\r\n", C_ERR); return false; }

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(7000);
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) {
        ::closesocket(sk);
        appendRun(L"— 远端 IP 无效：" + s->remIp + L" —\r\n", C_ERR);
        return false;
    }

    // 连接超时 3 秒，避免界面卡死
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
        appendRun(L"— 连不上远端 " + s->remIp + L":7000（请确认对端已启用接管服务）—\r\n", C_ERR);
        updateStatus();
        return false;
    }
    int one = 1;
    ::setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));

    s->sock = sk;
    s->isRemote = true;
    s->on = true;
    s->stopRead = false;
    s->reader = std::thread(readerProc, s);
    appendRun(L"— 已连上远端串口 " + s->remIp + L":" + s->remCom +
              L" @" + std::to_wstring(s->remBaud) + L" —\r\n", C_SYS);
    saveSettings();
    updateStatus();
    return true;
}
