// serial_hub.cpp - 串口枢纽（C++ 版，协议与 Python 版完全一致）
// 编译：x86_64-w64-mingw32-g++ -std=c++17 -O2 -o serial_hub.exe serial_hub.cpp -lws2_32 -static
// 用法：serial_hub.exe --port COM3 --baud 115200 --tcp 7000 [--log logs\serial.log]
#include "common.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <chrono>
#include <algorithm>

using namespace std::chrono;

struct Client {
    SOCKET s;
    std::mutex mtx;
    std::atomic<bool> alive{true};
    Client(SOCKET so) : s(so) {}

    void sendLine(const std::string& line) {
        if (!alive) return;
        std::lock_guard<std::mutex> lk(mtx);
        std::string data = line + "\n";
        int off = 0;
        while (off < (int)data.size()) {
            int n = ::send(s, data.c_str() + off, (int)data.size() - off, 0);
            if (n <= 0) { alive = false; return; }
            off += n;
        }
    }
};

static std::string g_port = "COM3";
static int g_baud = 115200;
static int g_tcpPort = 7000;
static SerialPort g_ser;
static std::mutex g_serMtx;
static std::vector<std::shared_ptr<Client>> g_clients;
static std::mutex g_clientsMtx;
static FILE* g_log = nullptr;
static std::mutex g_logMtx;
static std::atomic<bool> g_running{true};

static void logLine(const char* dir, const unsigned char* p, int n) {
    std::lock_guard<std::mutex> lk(g_logMtx);
    if (!g_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring w = normalizeCrLf(bytesToWide(p, n));
    std::string u = wideToUtf8(w);
    size_t start = 0;
    while (start <= u.size()) {
        size_t e = u.find('\n', start);
        std::string line = (e == std::string::npos) ? u.substr(start) : u.substr(start, e - start);
        if (!line.empty())
            fprintf(g_log, "[%02d:%02d:%02d] %-3s %s\n", st.wHour, st.wMinute, st.wSecond, dir, line.c_str());
        if (e == std::string::npos) break;
        start = e + 1;
    }
    fflush(g_log);
}

static void broadcast(const std::string& line, Client* skip = nullptr) {
    std::vector<std::shared_ptr<Client>> copy;
    {
        std::lock_guard<std::mutex> lk(g_clientsMtx);
        copy = g_clients;
    }
    for (auto& c : copy) {
        if (c.get() == skip) continue;
        c->sendLine(line);
    }
    std::lock_guard<std::mutex> lk(g_clientsMtx);
    g_clients.erase(std::remove_if(g_clients.begin(), g_clients.end(),
                                   [](const std::shared_ptr<Client>& c) { return !c->alive; }),
                    g_clients.end());
}

static void meta(const std::string& msg) {
    std::string line = "{\"t\":\"meta\",\"msg\":\"" + jsonEscape(msg) +
                       "\",\"port\":\"" + jsonEscape(g_port) + "\",\"baud\":" + std::to_string(g_baud) + "}";
    broadcast(line);
    printf("[hub] %s\n", msg.c_str());
    fflush(stdout);
}

static void serialOpener() {
    while (g_running) {
        {
            std::lock_guard<std::mutex> lk(g_serMtx);
            if (g_ser.isOpen()) break;
        }
        std::wstring err;
        std::wstring wp = utf8ToWide(g_port);
        bool ok = g_ser.open(wp, g_baud, err);
        if (ok) {
            meta("已打开 " + g_port + " @ " + std::to_string(g_baud) + "-8N1");
            break;
        } else {
            meta("打开 " + g_port + " 失败：" + wideToUtf8(err) + "（5 秒后重试）");
            for (int i = 0; i < 50 && g_running; i++) std::this_thread::sleep_for(milliseconds(100));
        }
    }
}

static void serialReader() {
    unsigned char buf[8192];
    while (g_running) {
        // 读操作刻意不持 g_serMtx：Win32 串口句柄支持读写并发，
        // 一旦读的时候抓着锁，客户端发来的写请求会被阻塞式 ReadFile 饿死。
        int n = g_ser.read(buf, sizeof(buf));
        if (n < 0) {
            // 句柄还没打开：交给 serialOpener 去重试，这里只让出 CPU
            if (!g_ser.isOpen()) {
                std::this_thread::sleep_for(milliseconds(100));
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(g_serMtx);
                if (g_ser.isOpen()) { g_ser.close(); meta("串口断开，准备重连"); }
            }
            std::this_thread::sleep_for(milliseconds(500));
            serialOpener();
            continue;
        }
        if (n > 0) {
            logLine("RX", buf, n);
            std::string line = "{\"t\":\"out\",\"b\":\"" + b64encode(buf, n) + "\",\"ts\":0}";
            broadcast(line);
        }
    }
}

static void clientThread(std::shared_ptr<Client> cli) {
    printf("[hub] 客户端接入\n");
    fflush(stdout);
    cli->sendLine("{\"t\":\"meta\",\"msg\":\"已连接到枢纽\",\"port\":\"" + jsonEscape(g_port) +
                  "\",\"baud\":" + std::to_string(g_baud) + "}");

    std::string acc;
    char tmp[4096];
    while (g_running && cli->alive) {
        int n = recv(cli->s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        acc.append(tmp, n);
        size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);
            if (line.empty()) continue;
            std::string t = jsonGetStr(line, "t");
            if (t == "in") {
                auto bytes = b64decode(jsonGetStr(line, "b"));
                std::string src = jsonGetStr(line, "src");
                if (src.empty()) src = "?";
                bool ok = false;
                {
                    std::lock_guard<std::mutex> lk(g_serMtx);
                    if (g_ser.isOpen()) ok = g_ser.write(bytes.data(), (int)bytes.size());
                }
                if (!ok) {
                    cli->sendLine("{\"t\":\"meta\",\"msg\":\"串口未就绪，数据被丢弃\"}");
                } else {
                    logLine("TX", bytes.data(), (int)bytes.size());
                    broadcast("{\"t\":\"tx\",\"b\":\"" + b64encode(bytes.data(), (int)bytes.size()) +
                                  "\",\"src\":\"" + jsonEscape(src) + "\"}", cli.get());
                }
            } else if (t == "ctl") {
                std::lock_guard<std::mutex> lk(g_serMtx);
                std::string d = jsonGetStr(line, "dtr");
                std::string r = jsonGetStr(line, "rts");
                if (!d.empty()) g_ser.setDtr(d == "true");
                if (!r.empty()) g_ser.setRts(r == "true");
            } else if (t == "ping") {
                cli->sendLine("{\"t\":\"meta\",\"msg\":\"pong\"}");
            }
        }
    }
    cli->alive = false;
    closesocket(cli->s);
    printf("[hub] 客户端断开\n");
    fflush(stdout);
}

int main(int argc, char** argv) {
    std::string logPath;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--port") g_port = next();
        else if (a == "--baud") g_baud = atoi(next().c_str());
        else if (a == "--tcp") g_tcpPort = atoi(next().c_str());
        else if (a == "--log") logPath = next();
    }
    SetConsoleOutputCP(65001);
    if (!logPath.empty()) {
        g_log = fopen(logPath.c_str(), "a");
    }
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    printf("SerialMate Hub (C++) : %s @ %d  ->  tcp 127.0.0.1:%d\n", g_port.c_str(), g_baud, g_tcpPort);
    fflush(stdout);

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons((u_short)g_tcpPort);
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("[hub] TCP 端口 %d 被占用，可能已有一个枢纽在运行\n", g_tcpPort);
        return 1;
    }
    listen(srv, 8);

    std::thread(serialOpener).detach();
    std::this_thread::sleep_for(milliseconds(300));
    std::thread(serialReader).detach();
    printf("[hub] TCP 监听 127.0.0.1:%d\n", g_tcpPort);
    fflush(stdout);

    while (g_running) {
        SOCKET c = accept(srv, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        auto cli = std::make_shared<Client>(c);
        {
            std::lock_guard<std::mutex> lk(g_clientsMtx);
            g_clients.push_back(cli);
        }
        std::thread(clientThread, cli).detach();
    }
    g_running = false;
    {
        std::lock_guard<std::mutex> lk(g_serMtx);
        g_ser.close();
    }
    if (g_log) fclose(g_log);
    WSACleanup();
    return 0;
}
