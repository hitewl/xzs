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
    // 只要求"是本地串口"，别贪心写成 `s == g_cur`：新建会话时 openPort 先跑、
    // 切 g_cur 在后，用 g_cur 判断会导致定时器永远不设，stty 一次都发不出去。
    // 真正发给哪个会话由 tick 里的 syncTermSize 决定（走当前会话）。
    if (g_sttyAdapt) SetTimer(g_hwnd, IDT_STTY, 700, nullptr);
    // 打开成功只代表拿到本机句柄：串口没有握手，板子断电、线没接也都"打开成功"。
    // 主动发一个回车要提示符，kProbeMs 内一个字节都没回来就判定设备无响应，
    // 免得用户对着「已连接」猜板子到底通没通电。
    if (s == g_cur) {
        devProbeStart(s);
        {
            std::lock_guard<std::mutex> lk(g_wrMtx);
            unsigned char cr = '\r';
            s->ser.write(&cr, 1);
        }
    }
    saveSettings();
    updateStatus();
}


// ---------------- 设备在线探测 ----------------
// 串口没有 TCP 那样的握手机制：CreateFile 成功只说明设备节点和句柄都拿到了，
// 板子断电、TX/RX 没接、波特率完全不对，PC 侧一样"打开成功"。唯一可信的证据是
// **有没有字节回来**。所以打开后发一个回车，超时没有回显就标明"设备无响应"。

void devProbeStart(Session* s) {
    if (!s) return;
    g_devState = DEV_PROBING;
    g_probeRx  = s->rx;
    if (g_hwnd) SetTimer(g_hwnd, IDT_PROBE, kProbeMs, nullptr);
}

void devProbeTick() {
    if (g_hwnd) KillTimer(g_hwnd, IDT_PROBE);
    if (g_devState != DEV_PROBING) return;      // 期间已经收到数据（showRx 里翻成 ALIVE）
    long long rx = g_cur ? g_cur->rx : 0;
    if (rx > g_probeRx) {
        g_devState = DEV_ALIVE;
    } else {
        g_devState = DEV_SILENT;    // 只在状态栏标注「设备无响应」，终端不打印提示
    }
    updateStatus();
}


// ---- 远端串口：连另一台 PC 上本程序的接管服务 ----