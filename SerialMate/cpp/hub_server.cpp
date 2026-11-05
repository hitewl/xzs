// hub_server.cpp - 内置接管服务(:7000)：accept 线程 + 每客户端一个线程

#include "sm.h"
Session* hubPort() {
    if (g_hubSess && g_hubSess->on && !g_hubSess->isRemote &&
        g_hubSess->type == CONN_SERIAL && g_hubSess->ser.isOpen())
        return g_hubSess;
    for (auto& sp : g_sess) {
        Session* q = sp.get();
        if (q->on && !q->isRemote && q->type == CONN_SERIAL && q->ser.isOpen()) return q;
    }
    return nullptr;
}

void broadcastLine(const std::string& line, Peer* skip) {
    std::vector<std::shared_ptr<Peer>> copy;
    {
        std::lock_guard<std::mutex> lk(g_peersMtx);
        copy = g_peers;
    }
    for (auto& p : copy) {
        if (p.get() == skip) continue;
        p->sendLine(line);
    }
    {
        std::lock_guard<std::mutex> lk(g_peersMtx);
        g_peers.erase(std::remove_if(g_peers.begin(), g_peers.end(),
                                     [](const std::shared_ptr<Peer>& p) { return !p->alive; }),
                      g_peers.end());
        g_peerCount = (int)g_peers.size();
    }
}

void postToGui(UINT msg, void* payload) {
    if (g_hwnd) PostMessageW(g_hwnd, msg, 0, (LPARAM)payload);
}

void postToGuiId(UINT msg, WPARAM id, void* payload) {
    if (g_hwnd) PostMessageW(g_hwnd, msg, id, (LPARAM)payload);
}

void postText(UINT msg, const std::wstring& s) {
    postToGui(msg, new std::wstring(s));
}

void peerThread(std::shared_ptr<Peer> p) {
    Session* hp0 = hubPort();
    p->sendLine("{\"t\":\"meta\",\"msg\":\"已连接到枢纽\",\"port\":\"" +
                wideToUtf8(hp0 ? hp0->name : g_portName) +
                "\",\"baud\":" + std::to_string(hp0 ? hp0->baud : g_baud) + "}");
    std::string acc;
    char tmp[4096];
    while (g_srvRun) {
        int n = recv(p->s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        acc.append(tmp, n);
        size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);
            if (line.empty()) continue;
            std::string t = jsonGetStr(line, "t");
            if (t == "in") {
                auto vb = b64decode(jsonGetStr(line, "b"));
                std::string bytes((const char*)vb.data(), vb.size());
                std::string src = jsonGetStr(line, "src");
                if (src.empty()) src = "agent";
                // 写「持有串口的那条会话」，不是 g_cur（本机自环时 g_cur 就是客户端自己）
                Session* hp = hubPort();
                // 串口只有一根线，本地键盘和 AI 共用同一个写通道。用户可能正打到一半
                // 没回车，此时 AI 的 "\r" 会把那半截命令**提前提交**：
                // 实测本地敲 "echo ABC" 未回车，AI 写 "pwd\r" → 板子执行 "echo ABCpwd"，
                // 用户的输入被吞、命令也变了。若半截是 rm/cp 之类，等于被 AI 按了回车。
                // 处理：先发 Ctrl-U 把板子上那行清掉，再让界面丢掉本地账本，然后才注入。
                // src=="gui" 是另一台 GUI 的键盘转发，属于正常打字，不该被清掉。
                if (hp && src != "gui" && g_pendingKeys.load() > 0) {
                    {
                        std::lock_guard<std::mutex> lk(g_wrMtx);
                        unsigned char cu = 0x15;              // Ctrl-U：kill line
                        hp->ser.write(&cu, 1);
                    }
                    g_pendingKeys = 0;
                    postText(WM_APP_META,
                             L"检测到未提交的输入，已按 Ctrl-U 清行，避免和接管命令拼成一条");
                    postToGui(WM_APP_INPUT_RESET, nullptr);
                    Sleep(40);                                // 给板子处理 Ctrl-U 的时间
                }
                bool ok = false;
                if (hp) {
                    std::lock_guard<std::mutex> lk(g_wrMtx);
                    ok = hp->ser.write((const unsigned char*)bytes.data(), (int)bytes.size());
                }
                if (!ok) {
                    p->sendLine("{\"t\":\"meta\",\"msg\":\"串口未打开，数据被丢弃\"}");
                } else {
                    hp->tx += (long long)bytes.size();
                    if (!bytes.empty())
                        p->openLine = (bytes.back() != '\r' && bytes.back() != '\n');
                    logLine(L"TX", bytes);
                    // 「显示接管命令」只给 agent / CLI 注入用。远端 GUI 敲键是逐字节流，
                    // 一个字符打一条带时间戳的标签行会把正常回显冲成"一字一行"，
                    // 所以 gui 来源只往其它对端广播，不进本地画面（画面里本来就有板子回显）。
                    if (src != "gui") {
                        auto* pr = new std::pair<std::string, std::string>(src, bytes);
                        postToGuiId(WM_APP_TX, (WPARAM)(hp ? hp->id : 0), pr);
                    }
                    broadcastLine("{\"t\":\"tx\",\"b\":\"" +
                                      b64encode((const unsigned char*)bytes.data(), (int)bytes.size()) +
                                      "\",\"src\":\"" + jsonEscape(src) + "\"}",
                                  p.get());
                }
            } else if (t == "ping") {
                p->sendLine("{\"t\":\"meta\",\"msg\":\"pong\"}");
            }
        }
    }
    p->alive = false;
    closesocket(p->s);
    // 接管端走了，但它可能把板子留在半条命令上（最后写的字节没带回车）。
    // 这时用户接着敲，字会粘在那半截后面，看起来就是"没反应/命令不对"。
    // 替它补一个 Ctrl-U 清行，把提示符还给用户。
    if (p->openLine.load()) {
        Session* hp = hubPort();
        if (hp && hp->ser.isOpen()) {
            bool userTyping = (g_pendingKeys.load() > 0);
            {
                std::lock_guard<std::mutex> lk(g_wrMtx);
                unsigned char cu = 0x15;
                hp->ser.write(&cu, 1);
            }
            postText(WM_APP_META, userTyping
                     ? L"接管端已断开：你的未提交输入和它留下的半截命令混在一起，已整行清掉"
                     : L"接管端已断开，已自动清掉它留下的半截命令行");
            if (userTyping) { g_pendingKeys = 0; postToGui(WM_APP_INPUT_RESET, nullptr); }
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_peersMtx);
        g_peers.erase(std::remove_if(g_peers.begin(), g_peers.end(),
                                     [&](const std::shared_ptr<Peer>& q) { return q.get() == p.get(); }),
                      g_peers.end());
        g_peerCount = (int)g_peers.size();
    }
    postToGui(WM_APP_PEER, nullptr);
}

void serverLoop() {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    // 绑 0.0.0.0：本机 127.0.0.1 / 本机网卡 IP / 局域网另一台 PC 都能连。
    // 只绑 127.0.0.1 的话，连 192.168.x.x:7000 会直接被拒（之前就是这个坑）。
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(7000);
    if (bind(g_srv, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) {
        a.sin_addr.s_addr = inet_addr("127.0.0.1");     // 退一步只给本机用
        if (bind(g_srv, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) {
            postText(WM_APP_META, L"接管服务端口 7000 被占用，本机 CLI 可能连不上");
            return;
        }
        postText(WM_APP_META, L"接管服务只监听 127.0.0.1:7000（0.0.0.0 绑定失败）");
    }
    listen(g_srv, 8);
    while (g_srvRun) {
        SOCKET c = accept(g_srv, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        auto p = std::make_shared<Peer>(c);
        {
            std::lock_guard<std::mutex> lk(g_peersMtx);
            g_peers.push_back(p);
            g_peerCount = (int)g_peers.size();
        }
        postToGui(WM_APP_PEER, nullptr);
        std::thread(peerThread, p).detach();
    }
}

void startServer() {
    if (g_srvRun) return;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_srv == INVALID_SOCKET) return;
    g_srvRun = true;
    g_srvThread = std::thread(serverLoop);
}

void stopServer() {
    if (!g_srvRun) return;
    g_srvRun = false;
    if (g_srv != INVALID_SOCKET) { closesocket(g_srv); g_srv = INVALID_SOCKET; }
    if (g_srvThread.joinable()) g_srvThread.join();
    std::lock_guard<std::mutex> lk(g_peersMtx);
    for (auto& p : g_peers) { p->alive = false; closesocket(p->s); }
    g_peers.clear();
    g_peerCount = 0;
}


// ---------------- 串口收发 ----------------

void postSessionData(Session* s, const std::string& bytes) {
    if (!g_hwnd) return;
    auto* heap = new std::string(bytes);
    PostMessageW(g_hwnd, WM_APP_DATA, (WPARAM)s->id, (LPARAM)heap);
}
