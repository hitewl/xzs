// link_serial.cpp - 本地串口：打开、关闭、写、读线程

#include "sm.h"
bool serialWrite(const std::string& bytes) {
    Session* s = g_cur;
    if (!s || !s->on) return false;
    if (s->isRemote) {
        if (s->sock == INVALID_SOCKET) return false;
        std::lock_guard<std::mutex> lk(g_wrMtx);
        std::string line = "{\"t\":\"in\",\"b\":\"" +
                           b64encode((const unsigned char*)bytes.data(), (int)bytes.size()) +
                           "\",\"src\":\"gui\"}\n";
        int off = 0;
        while (off < (int)line.size()) {
            int n = ::send(s->sock, line.c_str() + off, (int)line.size() - off, 0);
            if (n <= 0) return false;
            off += n;
        }
        return true;
    }
    if (!s->ser.isOpen()) return false;
    std::lock_guard<std::mutex> lk(g_wrMtx);
    return s->ser.write((const unsigned char*)bytes.data(), (int)bytes.size());
}


// ---------------- 内置接管服务 ----------------


// 接管服务真正能写的串口：优先 g_hubSess，退化到第一个本地串口会话。
// 千万不能用 g_cur —— 本机自环测试时当前会话就是「远程串口」客户端，
// 写 g_cur 等于把数据发回它自己的 socket，会自己跟自己打乒乓。

void openPort(Session* s) {
    if (!s || s->on) return;
    std::wstring port = s->port;
    if (port.empty()) { appendRun(L"— 没有可用串口 —\r\n", C_ERR); return; }

    std::wstring err;
    if (!s->ser.open(port, s->baud, err)) {
        appendRun(L"— " + err + L" —\r\n", C_ERR);
        updateStatus();
        return;
    }
    s->port = port;
    s->on = true;
    s->isRemote = false;
    g_hubSess = s;                       // 接管服务对外提供的就是这个串口
    if (g_dtr) s->ser.setDtr(true);
    if (g_rts) s->ser.setRts(true);
    s->stopRead = false;
    s->reader = std::thread(readerProc, s);
    appendRun(L"— 已打开 " + port + L" @" + std::to_wstring(s->baud) + L"-8N1 —\r\n", C_SYS);
    if (g_sttyAdapt && s == g_cur) SetTimer(g_hwnd, IDT_STTY, 700, nullptr);
    saveSettings();
    updateStatus();
}


// ---- 远端串口：连另一台 PC 上本程序的接管服务 ----
