// common.h - SerialMate C++ 公共部分：串口封装、base64、简易 JSON 取值
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cwchar>

#pragma comment(lib, "ws2_32.lib")

// ---------------- 字符串转换 ----------------
inline std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// 串口数据可能是 GBK/UTF-8 混杂，先按 UTF-8 解，失败再按 GBK
inline std::wstring bytesToWide(const unsigned char* p, int n) {
    if (n <= 0) return L"";
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)p, n, nullptr, 0);
    UINT cp = CP_UTF8;
    if (len <= 0) { cp = 936; len = MultiByteToWideChar(936, 0, (const char*)p, n, nullptr, 0); }
    if (len <= 0) {
        std::wstring w; w.reserve(n);
        for (int i = 0; i < n; i++) w.push_back((wchar_t)(p[i] < 0x80 ? p[i] : L'.'));
        return w;
    }
    std::wstring w(len, L'\0');
    MultiByteToWideChar(cp, 0, (const char*)p, n, &w[0], len);
    return w;
}

// 严格 UTF-8 解码：非法/不完整的序列不做 GBK 猜测，直接丢弃尾部残片
// （终端按字节流处理，半个汉字必须显示为“什么都没有”，而不是猜成别的汉字）
inline std::wstring utf8ToWideStrict(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        int need = 0;
        unsigned int cp = 0;
        if (c < 0x80) { cp = c; need = 0; }
        else if ((c >> 5) == 0x6)  { cp = c & 0x1F; need = 1; }
        else if ((c >> 4) == 0xE)  { cp = c & 0x0F; need = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; need = 3; }
        else { w.push_back(0xFFFD); i++; continue; }
        size_t j = i + 1;
        int k = 0;
        for (; k < need && j < n; k++, j++) {
            unsigned char cc = (unsigned char)s[j];
            if ((cc & 0xC0) != 0x80) break;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (k != need) {
            if (j >= n) break;                 // 尾部不完整（字节被切开）→ 不显示残片
            w.push_back(0xFFFD);
            i = j;
            continue;
        }
        i = j;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            w.push_back((wchar_t)(0xD800 + (cp >> 10)));
            w.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
        } else {
            w.push_back((wchar_t)cp);
        }
    }
    return w;
}

// ---------------- base64 ----------------
inline std::string b64encode(const unsigned char* data, int len) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (int i = 0; i < len; i += 3) {
        int b0 = data[i];
        int b1 = (i + 1 < len) ? data[i + 1] : 0;
        int b2 = (i + 2 < len) ? data[i + 2] : 0;
        out.push_back(T[b0 >> 2]);
        out.push_back(T[((b0 & 3) << 4) | (b1 >> 4)]);
        out.push_back((i + 1 < len) ? T[((b1 & 0xF) << 2) | (b2 >> 6)] : '=');
        out.push_back((i + 2 < len) ? T[b2 & 0x3F] : '=');
    }
    return out;
}

inline std::vector<unsigned char> b64decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<unsigned char> out;
    int buf = 0, bits = 0;
    for (char c : in) {
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((unsigned char)((buf >> bits) & 0xFF)); }
    }
    return out;
}

// ---------------- 简易 JSON 取值（仅用于本工具自有的扁平协议） ----------------
inline std::string jsonGetStr(const std::string& s, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return "";
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    p++;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
    if (p >= s.size()) return "";
    if (s[p] == '"') {
        p++;
        std::string v;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                char e = s[p + 1];
                if (e == 'n') v.push_back('\n');
                else if (e == 'r') v.push_back('\r');
                else if (e == 't') v.push_back('\t');
                else v.push_back(e);
                p += 2;
            } else v.push_back(s[p++]);
        }
        return v;
    }
    size_t e = s.find_first_of(",}", p);
    return s.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

// ---------------- 串口 ----------------
inline std::vector<std::wstring> enumSerialPorts() {
    std::vector<std::wstring> out;
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return out;
    wchar_t name[256], val[256];
    for (DWORD i = 0;; i++) {
        DWORD nl = 255, vl = sizeof(val), type = 0;
        LONG r = RegEnumValueW(k, i, name, &nl, nullptr, &type, (LPBYTE)val, &vl);
        if (r != ERROR_SUCCESS) break;
        if (type == REG_SZ) out.push_back(std::wstring(val, vl / sizeof(wchar_t) - 1));
    }
    RegCloseKey(k);
    return out;
}

class SerialPort {
public:
    ~SerialPort() { close(); }

    bool open(const std::wstring& port, int baud, std::wstring& err) {
        close();
        std::wstring path = L"\\\\.\\" + port;
        h_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            err = L"打开 " + port + L" 失败 (错误码 " + std::to_wstring(e) + L")";
            if (e == ERROR_ACCESS_DENIED) err += L"，可能被其他程序占用";
            h_ = nullptr;
            return false;
        }
        DCB dcb{};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(h_, &dcb)) { err = L"GetCommState 失败"; close(); return false; }
        dcb.BaudRate = baud;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_DISABLE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        if (!SetCommState(h_, &dcb)) { err = L"SetCommState 失败"; close(); return false; }
        COMMTIMEOUTS to{};
        // 读超时必须给得**小**，否则打字会一顿一顿的。
        //
        // ReadIntervalTimeout 的语义是「两个字节之间最多再等多久」：收到第 1 个
        // 字节后驱动还要盯着线路等这么久才把数据交上来。原来是 50ms，加上 USB
        // 转串口芯片自己的 latency（CH340/FT232 默认 16ms），一次「敲键→板子回显」
        // 的往返被硬生生拉长到 ~80ms，表现就是「键盘已经敲下去了，屏幕慢半拍」，
        // 退格尤其明显（要先等回显字节上来才能重画）。
        // 改成 2ms 后回显几乎立刻可见；连续大块输出时因为字节间隔远小于 2ms，
        // 仍然会攒满缓冲一次返回，不会降低吞吐。
        // 仍不用 MAXDWORD——部分 CH340 驱动会忽略总超时一直阻塞。
        to.ReadIntervalTimeout = 2;
        to.ReadTotalTimeoutMultiplier = 0;
        to.ReadTotalTimeoutConstant = 20;
        to.WriteTotalTimeoutMultiplier = 0;
        to.WriteTotalTimeoutConstant = 500;
        SetCommTimeouts(h_, &to);
        SetupComm(h_, 64 * 1024, 64 * 1024);
        PurgeComm(h_, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }

    void close() {
        if (h_) { CloseHandle(h_); h_ = nullptr; }
    }

    bool isOpen() const { return h_ != nullptr; }

    int read(unsigned char* buf, int cap) {
        if (!h_) return -1;
        DWORD n = 0;
        if (!ReadFile(h_, buf, (DWORD)cap, &n, nullptr)) return -1;
        return (int)n;
    }

    bool write(const unsigned char* data, int len) {
        if (!h_) return false;
        DWORD n = 0;
        return WriteFile(h_, data, (DWORD)len, &n, nullptr) && (int)n == len;
    }

    void setDtr(bool on) {
        if (!h_) return;
        EscapeCommFunction(h_, on ? SETDTR : CLRDTR);
    }

    void setRts(bool on) {
        if (!h_) return;
        EscapeCommFunction(h_, on ? SETRTS : CLRRTS);
    }

private:
    HANDLE h_ = nullptr;
};

// 去掉 ANSI 转义序列
inline std::wstring stripAnsi(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == 0x1B && i + 1 < in.size() && in[i + 1] == L'[') {
            i += 2;
            while (i < in.size() && !((in[i] >= L'@' && in[i] <= L'~'))) i++;
        } else out.push_back(in[i]);
    }
    return out;
}

// \r\n 与单独 \r 统一成 \n（串口控制台常见）
inline std::wstring normalizeCrLf(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == L'\r') {
            if (i + 1 < in.size() && in[i + 1] == L'\n') { out.push_back(L'\n'); i++; }
            else out.push_back(L'\n');
        } else out.push_back(in[i]);
    }
    return out;
}

inline std::wstring hexDump(const unsigned char* p, int n) {
    static const wchar_t* H = L"0123456789abcdef";
    std::wstring o;
    o.reserve(n * 3);
    for (int i = 0; i < n; i++) {
        o.push_back(H[p[i] >> 4]);
        o.push_back(H[p[i] & 0xF]);
        o.push_back(L' ');
    }
    return o;
}
