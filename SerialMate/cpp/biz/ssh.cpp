// ssh.cpp —— SSH-2 客户端实现（curve25519-sha256 + aes128-ctr + hmac-sha2-256）
// 密码学：SHA-256 / HMAC 自己实现，AES-CTR 走 Windows CNG(bcrypt)，X25519 用 TweetNaCl 的阶梯算法
#include "ssh.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <cstring>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

// ======================= SHA-256 =======================
namespace {

inline uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256 {
    uint32_t h[8];
    uint64_t total;
    unsigned char buf[64];
    size_t bl;

    void init() {
        static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        memcpy(h, iv, sizeof(h));
        total = 0;
        bl = 0;
    }
    static void block(uint32_t* st, const unsigned char* p) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
                   ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], hh = st[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        st[0] += a; st[1] += b; st[2] += c; st[3] += d;
        st[4] += e; st[5] += f; st[6] += g; st[7] += hh;
    }
    void update(const void* data, size_t n) {
        const unsigned char* p = (const unsigned char*)data;
        total += n;
        while (n) {
            size_t take = 64 - bl;
            if (take > n) take = n;
            memcpy(buf + bl, p, take);
            bl += take; p += take; n -= take;
            if (bl == 64) { block(h, buf); bl = 0; }
        }
    }
    void final(unsigned char out[32]) {
        uint64_t bits = total * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char z = 0;
        while (bl != 56) update(&z, 1);
        unsigned char lb[8];
        for (int i = 0; i < 8; i++) lb[i] = (unsigned char)(bits >> (56 - 8 * i));
        update(lb, 8);
        for (int i = 0; i < 8; i++) {
            out[i * 4]     = (unsigned char)(h[i] >> 24);
            out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
            out[i * 4 + 2] = (unsigned char)(h[i] >> 8);
            out[i * 4 + 3] = (unsigned char)(h[i]);
        }
    }
};

std::string sha256(const std::string& d) {
    Sha256 s; s.init(); s.update(d.data(), d.size());
    std::string o(32, 0);
    s.final((unsigned char*)&o[0]);
    return o;
}

std::string hmacSha256(const std::string& key, const std::string& msg) {
    unsigned char k[64] = {0};
    if (key.size() > 64) {
        std::string h = sha256(key);
        memcpy(k, h.data(), 32);
    } else if (!key.empty()) {
        memcpy(k, key.data(), key.size());
    }
    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha256 s; s.init();
    s.update(ipad, 64); s.update(msg.data(), msg.size());
    unsigned char ih[32];
    s.final(ih);
    Sha256 s2; s2.init();
    s2.update(opad, 64); s2.update(ih, 32);
    std::string o(32, 0);
    s2.final((unsigned char*)&o[0]);
    return o;
}

// ======================= X25519（TweetNaCl 阶梯） =======================
typedef int64_t i64;
typedef i64 gf[16];
static const gf g_121665 = {0xDB41, 1};

void car25519(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}
void sel25519(gf p, gf q, int b) {
    i64 c = ~(i64)(b - 1);
    for (int i = 0; i < 16; i++) {
        i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t; q[i] ^= t;
    }
}
void pack25519(unsigned char* o, const gf n) {
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i]     = (unsigned char)(t[i] & 0xff);
        o[2 * i + 1] = (unsigned char)(t[i] >> 8);
    }
}
void unpack25519(gf o, const unsigned char* n) {
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}
void A_(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
void Z_(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
void M_(gf o, const gf a, const gf b) {
    i64 t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}
void S_(gf o, const gf a) { M_(o, a, a); }
void inv25519(gf o, const gf i) {
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        S_(c, c);
        if (a != 2 && a != 4) M_(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

void x25519(unsigned char* q, const unsigned char* n, const unsigned char* p) {
    unsigned char z[32];
    i64 x[80];
    gf a, b, c, d, e, f;
    for (int i = 0; i < 31; i++) z[i] = n[i];
    z[31] = (unsigned char)((n[31] & 127) | 64);
    z[0] &= 248;
    unpack25519(x, p);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r);
        sel25519(c, d, r);
        A_(e, a, c);
        Z_(a, a, c);
        A_(c, b, d);
        Z_(b, b, d);
        S_(d, e);
        S_(f, a);
        M_(a, c, a);
        M_(c, b, e);
        A_(e, a, c);
        Z_(a, a, c);
        S_(b, a);
        Z_(c, d, f);
        M_(a, c, g_121665);
        A_(a, a, d);
        M_(c, c, a);
        M_(a, d, f);
        M_(d, b, x);
        S_(b, e);
        sel25519(a, b, r);
        sel25519(c, d, r);
    }
    for (int i = 0; i < 16; i++) {
        x[i + 16] = a[i];
        x[i + 32] = c[i];
        x[i + 48] = b[i];
        x[i + 64] = d[i];
    }
    inv25519(x + 32, x + 32);
    M_(x + 16, x + 16, x + 32);
    pack25519(q, x + 16);
}

// ======================= AES-128 CTR（CNG） =======================
struct AesCtr {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    unsigned char ctr[16] = {0};

    bool open() {
        if (alg) return true;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return false;
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                              (ULONG)(sizeof(BCRYPT_CHAIN_MODE_ECB)), 0) != 0) return false;
        return true;
    }
    bool setKey(const std::string& k, const std::string& iv) {
        if (!open()) return false;
        if (key) { BCryptDestroyKey(key); key = nullptr; }
        memcpy(ctr, iv.data(), 16);
        return BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, (PUCHAR)k.data(), (ULONG)k.size(), 0) == 0;
    }
    void inc() {
        for (int i = 15; i >= 0; i--) { if (++ctr[i]) break; }
    }
    void crypt(unsigned char* p, size_t n) {
        if (!key || !n) return;
        size_t blocks = (n + 15) / 16;
        std::vector<unsigned char> ks(blocks * 16);
        std::vector<unsigned char> in(blocks * 16);
        for (size_t b = 0; b < blocks; b++) {
            memcpy(&in[b * 16], ctr, 16);
            inc();
        }
        ULONG out = 0;
        if (BCryptEncrypt(key, &in[0], (ULONG)in.size(), nullptr, nullptr, 0, &ks[0], (ULONG)ks.size(),
                          &out, 0) != 0)
            return;
        for (size_t i = 0; i < n; i++) p[i] ^= ks[i];
    }
    void reset() {
        if (key) { BCryptDestroyKey(key); key = nullptr; }
    }
};

// ======================= 报文缓冲 =======================
struct Buf {
    std::string d;
    void u8(int v) { d.push_back((char)(v & 0xff)); }
    void u32(uint32_t v) {
        d.push_back((char)(v >> 24)); d.push_back((char)(v >> 16));
        d.push_back((char)(v >> 8));  d.push_back((char)v);
    }
    void str(const std::string& s) { u32((uint32_t)s.size()); d += s; }
    void cstr(const char* s) { str(std::string(s)); }
    // mpint：大端，去掉前导 0，最高位为 1 时补 0x00
    void mpint(const std::string& be) {
        size_t i = 0;
        while (i < be.size() && be[i] == 0) i++;
        std::string v = be.substr(i);
        if (!v.empty() && (unsigned char)v[0] & 0x80) v.insert(v.begin(), '\0');
        str(v);
    }
};

struct Rd {
    const unsigned char* p;
    size_t n, i;
    bool bad = false;
    Rd(const std::string& s) : p((const unsigned char*)s.data()), n(s.size()), i(0) {}
    int u8() { if (i + 1 > n) { bad = true; return 0; } return p[i++]; }
    uint32_t u32() {
        if (i + 4 > n) { bad = true; return 0; }
        uint32_t v = ((uint32_t)p[i] << 24) | ((uint32_t)p[i + 1] << 16) |
                     ((uint32_t)p[i + 2] << 8) | (uint32_t)p[i + 3];
        i += 4;
        return v;
    }
    std::string str() {
        uint32_t len = u32();
        if (bad || i + len > n) { bad = true; return std::string(); }
        std::string s((const char*)p + i, len);
        i += len;
        return s;
    }
    std::string rest() { std::string s((const char*)p + i, n - i); i = n; return s; }
};

bool nameListContains(const std::string& list, const std::string& want) {
    std::string cur;
    for (size_t i = 0; i <= list.size(); i++) {
        if (i == list.size() || list[i] == ',') {
            if (cur == want) return true;
            cur.clear();
        } else cur.push_back(list[i]);
    }
    return false;
}

std::wstring w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring o((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &o[0], n);
    return o;
}

}  // namespace

// ======================= 客户端 =======================
struct SshClient::Impl {
    SOCKET s = INVALID_SOCKET;
    std::thread th;
    std::atomic<bool> run{false};
    std::atomic<bool> ready{false};
    std::mutex wmtx;
    std::string outQ;                 // 待发送的用户输入（受 wmtx 保护）
    int cols = 80, rows = 24;
    std::atomic<bool> sizeDirty{false};
    std::mutex callMtx;

    std::string verC, verS;
    std::string sessId;               // 第一次 KEX 的 H
    uint32_t seqOut = 0, seqIn = 0;
    AesCtr enc, dec;
    std::string mkOut, mkIn;
    bool encOn = false, etm = false;

    uint32_t srvChan = 0, ourChan = 0;
    uint32_t srvWin = 0, srvMaxPkt = 32768;
    uint32_t ourWin = 1024 * 1024;
    uint32_t consumed = 0;

    SshClient* owner = nullptr;

    void log(const std::wstring& t, int lv) {
        if (owner && owner->onLog) owner->onLog(t, lv);
    }
    void data(const std::string& d) {
        if (owner && owner->onData) owner->onData(d);
    }
    void closed(const std::wstring& why) {
        if (owner && owner->onClosed) owner->onClosed(why);
    }

    // ---------- socket ----------
    bool connectTo(const std::string& host, int port, std::wstring& err) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", port);
        if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
            err = L"域名/地址解析失败：" + w(host);
            return false;
        }
        bool ok = false;
        for (addrinfo* a = res; a && !ok; a = a->ai_next) {
            SOCKET sk = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (sk == INVALID_SOCKET) continue;
            u_long nb = 1;
            ioctlsocket(sk, FIONBIO, &nb);
            int rc = ::connect(sk, a->ai_addr, (int)a->ai_addrlen);
            if (rc == SOCKET_ERROR) {
                if (WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(sk); continue; }
                fd_set ws, es;
                FD_ZERO(&ws); FD_ZERO(&es);
                FD_SET(sk, &ws); FD_SET(sk, &es);
                timeval tv{8, 0};
                if (select(0, nullptr, &ws, &es, &tv) <= 0) { closesocket(sk); continue; }
                if (!FD_ISSET(sk, &ws)) { closesocket(sk); continue; }
                int soerr = 0; int len = sizeof(soerr);
                getsockopt(sk, SOL_SOCKET, SO_ERROR, (char*)&soerr, &len);
                if (soerr != 0) { closesocket(sk); continue; }
            }
            nb = 0;
            ioctlsocket(sk, FIONBIO, &nb);
            int one = 1;
            setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
            s = sk;
            ok = true;
        }
        freeaddrinfo(res);
        if (!ok) err = L"连不上 " + w(host) + L":" + std::to_wstring(port) + L"（端口未开或被防火墙拦）";
        return ok;
    }

    bool readN(void* buf, int n, int timeoutMs = 15000) {
        char* p = (char*)buf;
        int got = 0;
        DWORD t0 = GetTickCount();
        while (got < n) {
            if (!run && !ready) return false;
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(s, &rs);
            timeval tv{0, 100 * 1000};
            int r = select(0, &rs, nullptr, nullptr, &tv);
            if (r < 0) return false;
            if (r == 0) {
                if (GetTickCount() - t0 > (DWORD)timeoutMs) return false;
                continue;
            }
            int k = ::recv(s, p + got, n - got, 0);
            if (k <= 0) return false;
            got += k;
        }
        return true;
    }
    bool writeAll(const std::string& d) {
        int off = 0;
        while (off < (int)d.size()) {
            int k = ::send(s, d.data() + off, (int)d.size() - off, 0);
            if (k <= 0) return false;
            off += k;
        }
        return true;
    }

    // ---------- 二进制报文 ----------
    bool sendPacket(const std::string& payload) {
        size_t bs = encOn ? 16 : 8;
        // ETM：length 字段是明文、不参与加密，只有「padding_len+payload+padding」这 len 字节被加密，
        // 所以要求 len % bs == 0；非 ETM：整包（含 4 字节长度）都要对齐。
        size_t base = (encOn && etm) ? 1 : 5;
        size_t pad = bs - ((base + payload.size()) % bs);
        if (pad < 4) pad += bs;
        std::string plain;
        uint32_t plen = (uint32_t)(1 + payload.size() + pad);
        plain.push_back((char)(plen >> 24)); plain.push_back((char)(plen >> 16));
        plain.push_back((char)(plen >> 8));  plain.push_back((char)plen);
        plain.push_back((char)pad);
        plain += payload;
        unsigned char rnd[64];
        if (pad) {
            BCryptGenRandom(nullptr, rnd, (ULONG)pad, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            plain.append((const char*)rnd, pad);
        }
        std::string wire;
        if (!encOn) {
            wire = plain;                                  // 未加密：无 MAC
        } else {
            std::string macIn;
            macIn.push_back((char)(seqOut >> 24)); macIn.push_back((char)(seqOut >> 16));
            macIn.push_back((char)(seqOut >> 8));  macIn.push_back((char)seqOut);
            if (etm) {
                std::string ct = plain.substr(4);
                enc.crypt((unsigned char*)&ct[0], ct.size());
                wire = plain.substr(0, 4) + ct;
                macIn += wire;
                wire += hmacSha256(mkOut, macIn).substr(0, 32);
            } else {
                std::string ct = plain;
                enc.crypt((unsigned char*)&ct[0], ct.size());
                macIn += plain;
                wire = ct + hmacSha256(mkOut, macIn).substr(0, 32);
            }
        }
        seqOut++;
        return writeAll(wire);
    }

    bool readPacket(std::string& payload) {
        if (!encOn) {
            unsigned char hdr[4];
            if (!readN(hdr, 4)) return false;
            uint32_t plen = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                            ((uint32_t)hdr[2] << 8) | hdr[3];
            if (plen < 5 || plen > 256 * 1024) return false;
            std::string body(plen, 0);
            if (!readN(&body[0], (int)plen)) return false;
            size_t pad = (unsigned char)body[0];
            if (pad + 1 > body.size()) return false;
            payload = body.substr(1, body.size() - 1 - pad);
            seqIn++;
            return true;
        }
        if (etm) {
            unsigned char hdr[4];
            if (!readN(hdr, 4)) return false;
            uint32_t plen = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                            ((uint32_t)hdr[2] << 8) | hdr[3];
            if (plen < 5 || plen > 256 * 1024) return false;
            std::string ct(plen, 0);
            if (!readN(&ct[0], (int)plen)) return false;
            unsigned char mac[32];
            if (!readN(mac, 32)) return false;
            std::string macIn;
            macIn.push_back((char)(seqIn >> 24)); macIn.push_back((char)(seqIn >> 16));
            macIn.push_back((char)(seqIn >> 8));  macIn.push_back((char)seqIn);
            macIn.append((const char*)hdr, 4);
            macIn += ct;
            std::string want = hmacSha256(mkIn, macIn).substr(0, 32);
            if (memcmp(want.data(), mac, 32) != 0) { log(L"报文校验失败（MAC 不匹配）", 1); return false; }
            dec.crypt((unsigned char*)&ct[0], ct.size());
            size_t pad = (unsigned char)ct[0];
            if (pad + 1 > ct.size()) return false;
            payload = ct.substr(1, ct.size() - 1 - pad);
            seqIn++;
            return true;
        }
        // 非 ETM：先解密首块拿到长度
        std::string first(16, 0);
        if (!readN(&first[0], 16)) return false;
        dec.crypt((unsigned char*)&first[0], 16);
        uint32_t plen = ((uint32_t)(unsigned char)first[0] << 24) |
                        ((uint32_t)(unsigned char)first[1] << 16) |
                        ((uint32_t)(unsigned char)first[2] << 8) | (uint32_t)(unsigned char)first[3];
        if (plen < 5 || plen > 256 * 1024) return false;
        std::string body = first;
        size_t total = 4 + plen;
        if (total > 16) {
            std::string more(total - 16, 0);
            if (!readN(&more[0], (int)more.size())) return false;
            dec.crypt((unsigned char*)&more[0], more.size());
            body += more;
        }
        unsigned char mac[32];
        if (!readN(mac, 32)) return false;
        std::string macIn;
        macIn.push_back((char)(seqIn >> 24)); macIn.push_back((char)(seqIn >> 16));
        macIn.push_back((char)(seqIn >> 8));  macIn.push_back((char)seqIn);
        macIn += body;
        std::string want = hmacSha256(mkIn, macIn).substr(0, 32);
        if (memcmp(want.data(), mac, 32) != 0) { log(L"报文校验失败（MAC 不匹配）", 1); return false; }
        size_t pad = (unsigned char)body[4];
        payload = body.substr(5, body.size() - 5 - pad);
        seqIn++;
        return true;
    }

    // ---------- 版本交换 ----------
    bool versionExchange(std::wstring& err) {
        verC = "SSH-2.0-SerialMate_1.0";
        if (!writeAll(verC + "\r\n")) { err = L"发送版本号失败"; return false; }
        std::string acc;
        DWORD t0 = GetTickCount();
        while (true) {
            size_t pos = acc.find('\n');
            if (pos != std::string::npos) {
                std::string line = acc.substr(0, pos + 1);
                acc.erase(0, pos + 1);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                if (line.rfind("SSH-", 0) == 0) {
                    verS = line;
                    break;
                }
                continue;                     // 老服务器会先发几行注释
            }
            if (GetTickCount() - t0 > 10000) { err = L"等待服务器版本号超时"; return false; }
            char buf[512];
            fd_set rs; FD_ZERO(&rs); FD_SET(s, &rs);
            timeval tv{0, 100 * 1000};
            if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
            int k = ::recv(s, buf, sizeof(buf), 0);
            if (k <= 0) { err = L"连接被对方关闭"; return false; }
            acc.append(buf, (size_t)k);
        }
        return true;
    }

    // ---------- 密钥交换 ----------
    bool doKex(bool initial, std::wstring& err) {
        static const char* kexList  = "curve25519-sha256,curve25519-sha256@libssh.org";
        static const char* hostList = "ssh-ed25519,rsa-sha2-512,rsa-sha2-256,ecdsa-sha2-nistp256,ssh-rsa";
        static const char* encList  = "aes128-ctr";
        static const char* macList  = "hmac-sha2-256-etm@openssh.com,hmac-sha2-256";
        static const char* cmpList  = "none";

        Buf ic;
        ic.u8(20);
        unsigned char cookie[16];
        BCryptGenRandom(nullptr, cookie, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        ic.d.append((const char*)cookie, 16);
        ic.cstr(kexList); ic.cstr(hostList);
        ic.cstr(encList); ic.cstr(encList);
        ic.cstr(macList); ic.cstr(macList);
        ic.cstr(cmpList); ic.cstr(cmpList);
        ic.cstr(""); ic.cstr("");
        ic.u8(0);
        ic.u32(0);
        if (!sendPacket(ic.d)) { err = L"发送 KEXINIT 失败"; return false; }

        std::string is;
        bool gotIs = false;
        while (!gotIs) {
            std::string pl;
            if (!readPacket(pl)) { err = L"读取服务器 KEXINIT 失败"; return false; }
            int t = (unsigned char)pl[0];
            if (t == 20) { is = pl; gotIs = true; }
            else if (t == 2 || t == 4) continue;
            else if (t == 3) { /* unimplemented */ }
            else if (t == 1 || t == 0) { err = L"服务器断开（版本或算法不兼容）"; return false; }
        }
        // 解析服务器 KEXINIT 的算法列表
        std::string lists[10];
        {
            Rd r(is);
            r.u8();
            r.i += 16;                       // cookie
            for (int k = 0; k < 10 && !r.bad; k++) lists[k] = r.str();
            if (r.bad) { err = L"服务器 KEXINIT 解析失败"; return false; }
        }
        // lists: 0 kex / 1 主机密钥 / 2 加密c2s / 3 加密s2c / 4 MAC c2s / 5 MAC s2c / 6,7 压缩 / 8,9 语言
        // 协商规则：按客户端偏好顺序，取第一个服务器也支持的
        auto pick = [](const char* ours, const std::string& theirs) -> std::string {
            std::string cur, s(ours);
            for (size_t i = 0; i <= s.size(); i++) {
                if (i == s.size() || s[i] == ',') {
                    if (!cur.empty() && nameListContains(theirs, cur)) return cur;
                    cur.clear();
                } else cur.push_back(s[i]);
            }
            return std::string();
        };
        auto firstField = [](const std::string& s) {
            size_t p = s.find(',');
            return p == std::string::npos ? s : s.substr(0, p);
        };
        std::string kexAlg  = pick(kexList,  lists[0]);
        std::string hostAlg = pick(hostList, lists[1]);
        std::string encUse  = pick(encList,  lists[2]);
        std::string macUse  = pick(macList,  lists[4]);
        std::string cmpUse  = pick(cmpList,  lists[6]);
        if (kexAlg.empty()) {
            err = L"没有共同的主密钥算法（对端支持 " + w(firstField(lists[0])) + L" 等）";
            return false;
        }
        if (hostAlg.empty()) {
            err = L"没有共同的主机密钥类型（对端支持 " + w(firstField(lists[1])) + L" 等）";
            return false;
        }
        if (encUse.empty()) {
            err = L"没有共同的加密算法（对端支持 " + w(firstField(lists[2])) + L" 等）";
            return false;
        }
        if (macUse.empty()) {
            err = L"没有共同的校验算法（对端支持 " + w(firstField(lists[4])) + L" 等）";
            return false;
        }
        if (cmpUse != "none") { err = L"对端要求压缩，本程序不支持"; return false; }
        bool useEtm = (macUse == "hmac-sha2-256-etm@openssh.com");
        log(L"算法协商：" + w(kexAlg) + L" / " + w(encUse) + L" / " + w(macUse) + L" / " + w(hostAlg), 0);

        // curve25519
        unsigned char secret[32], base[32] = {9}, qc[32];
        BCryptGenRandom(nullptr, secret, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        x25519(qc, secret, base);

        Buf init;
        init.u8(30);
        init.str(std::string((const char*)qc, 32));
        if (!sendPacket(init.d)) { err = L"发送密钥交换包失败"; return false; }

        std::string ks, qs, sig;
        bool gotReply = false;
        while (!gotReply) {
            std::string pl;
            if (!readPacket(pl)) { err = L"读取密钥交换应答失败"; return false; }
            int t = (unsigned char)pl[0];
            if (t == 31) {
                Rd rr(pl);
                rr.u8();
                ks = rr.str();
                qs = rr.str();
                sig = rr.str();
                if (rr.bad || qs.size() != 32) { err = L"密钥交换应答格式错误"; return false; }
                gotReply = true;
            } else if (t == 2 || t == 4 || t == 7) continue;
            else if (t == 1) { err = L"服务器拒绝密钥交换"; return false; }
        }

        unsigned char shared[32];
        x25519(shared, secret, (const unsigned char*)qs.data());
        bool allZero = true;
        for (int i = 0; i < 32; i++) if (shared[i]) { allZero = false; break; }
        if (allZero) { err = L"密钥交换得到全零共享密钥"; return false; }

        // 共享密钥编码成 mpint（= 4 字节长度 + 大端内容），H 和密钥派生里都要用「带长度」的整串
        std::string kMp;
        {
            std::string raw((const char*)shared, 32);
            size_t i = 0;
            while (i < raw.size() && raw[i] == 0) i++;
            std::string v = raw.substr(i);
            if (!v.empty() && ((unsigned char)v[0] & 0x80)) v.insert(v.begin(), '\0');
            Buf kb;
            kb.str(v);
            kMp = kb.d;                       // 长度前缀 + 内容，后面派生密钥直接用这个
        }
        std::string H;
        {
            Buf hb;
            hb.str(verC); hb.str(verS);
            hb.str(ic.d); hb.str(is);
            hb.str(ks);
            hb.str(std::string((const char*)qc, 32));
            hb.str(qs);
            hb.d += kMp;
            H = sha256(hb.d);
        }
        if (sessId.empty()) sessId = H;

        auto derive = [&](char x, size_t n) {
            std::string m = kMp + H;
            m.push_back(x);
            m += sessId;
            std::string h = sha256(m);
            while (h.size() < n) h += sha256(m + h);
            return h.substr(0, n);
        };
        std::string ivC = derive('A', 16), ivS = derive('B', 16);
        std::string keyC = derive('C', 16), keyS = derive('D', 16);
        std::string macKC = derive('E', 32), macKS = derive('F', 32);

        Buf nk;
        nk.u8(21);
        if (!sendPacket(nk.d)) { err = L"发送 NEWKEYS 失败"; return false; }
        enc.setKey(keyC, ivC); mkOut = macKC;   // 注意：序号在 NEWKEYS 后不重置（RFC 4253 §6.4）
        // 等对方的 NEWKEYS
        while (true) {
            std::string pl;
            if (!readPacket(pl)) { err = L"等待 NEWKEYS 失败"; return false; }
            int t = (unsigned char)pl[0];
            if (t == 21) break;
            if (t == 2 || t == 4 || t == 7) continue;
            if (t == 1) { err = L"服务器断开（NEWKEYS 阶段）"; return false; }
        }
        dec.setKey(keyS, ivS); mkIn = macKS;
        etm = useEtm;
        encOn = true;
        if (initial) log(L"密钥交换完成 · " + w(kexAlg) + L" · " + w(hostAlg), 0);
        return true;
    }

    // ---------- 认证 ----------
    bool sendService() {
        Buf b; b.u8(5); b.cstr("ssh-userauth");
        return sendPacket(b.d);
    }

    // 读一个认证相关回包（跳过无关消息）
    bool waitAuth(std::string& pl) {
        while (true) {
            if (!readPacket(pl)) return false;
            int t = (unsigned char)pl[0];
            if (t == 2 || t == 4 || t == 7) continue;
            return true;
        }
    }

    bool authPassword(const std::string& user, const std::string& pass, std::wstring& err) {
        Buf b;
        b.u8(50);
        b.str(user);
        b.cstr("ssh-connection");
        b.cstr("password");
        b.u8(0);
        b.str(pass);
        if (!sendPacket(b.d)) { err = L"发送口令认证失败"; return false; }

        std::string pl;
        if (!waitAuth(pl)) { err = L"认证过程中连接断开"; return false; }
        int t = (unsigned char)pl[0];
        if (t == 52) return true;
        if (t == 51) {
            Rd r(pl);
            r.u8();
            std::string methods = r.str();
            log(L"口令认证未通过，服务器允许的方式：" + w(methods), 1);
            if (nameListContains(methods, "keyboard-interactive")) {
                return authKbdInt(user, pass, err);
            }
            err = L"口令认证失败（用户名或密码不对，或服务器只允许密钥登录）";
            return false;
        }
        if (t == 53) { err = L"认证失败次数过多，服务器暂时拒绝"; return false; }
        err = L"认证返回未知结果（type " + std::to_wstring(t) + L"）";
        return false;
    }

    bool authKbdInt(const std::string& user, const std::string& pass, std::wstring& err) {
        log(L"改用 keyboard-interactive 认证", 0);
        Buf b;
        b.u8(50);
        b.str(user);
        b.cstr("ssh-connection");
        b.cstr("keyboard-interactive");
        b.str("");            // language tag
        b.str("");            // submethods
        if (!sendPacket(b.d)) { err = L"发送认证失败"; return false; }
        for (int round = 0; round < 8; round++) {
            std::string pl;
            if (!waitAuth(pl)) { err = L"认证过程中连接断开"; return false; }
            int t = (unsigned char)pl[0];
            if (t == 52) return true;
            if (t == 53) { err = L"认证失败次数过多"; return false; }
            if (t == 60) {                      // INFO_REQUEST
                Rd r(pl);
                r.u8();
                std::string name = r.str();
                r.str();
                r.str();
                uint32_t n = r.u32();
                if (n > 32) n = 32;
                Buf a;
                a.u8(61);
                a.u32(n);
                for (uint32_t i = 0; i < n; i++) {
                    std::string prompt = r.str();
                    r.u8();                     // echo
                    if (!name.empty() && i == 0) log(L"服务器提示：" + w(name), 0);
                    (void)prompt;
                    a.str(pass);
                }
                if (!sendPacket(a.d)) { err = L"发送认证应答失败"; return false; }
                continue;
            }
            if (t == 51) {
                Rd r(pl); r.u8();
                std::string methods = r.str();
                err = L"认证失败（允许方式：" + w(methods) + L"）";
                return false;
            }
        }
        err = L"认证流程异常";
        return false;
    }

    // ---------- 通道 ----------
    bool openShell(int cols_, int rows_, std::wstring& err) {
        ourChan = 0;
        Buf b;
        b.u8(90);
        b.cstr("session");
        b.u32(ourChan);
        b.u32(1024 * 1024);
        b.u32(32768);
        if (!sendPacket(b.d)) { err = L"打开会话通道失败"; return false; }
        while (true) {
            std::string pl;
            if (!readPacket(pl)) { err = L"会话通道无应答"; return false; }
            int t = (unsigned char)pl[0];
            if (t == 91) {
                Rd r(pl);
                r.u8();
                uint32_t recipient = r.u32();
                srvChan = r.u32();
                srvWin = r.u32();
                srvMaxPkt = r.u32();
                (void)recipient;
                break;
            }
            if (t == 92) { err = L"服务器拒绝打开会话通道"; return false; }
            if (t == 2 || t == 4 || t == 7) continue;
        }
        // pty-req
        Buf p;
        p.u8(98);
        p.u32(srvChan);
        p.cstr("pty-req");
        p.u8(1);
        p.cstr("xterm-256color");
        p.u32((uint32_t)cols_);
        p.u32((uint32_t)rows_);
        p.u32(0);
        p.u32(0);
        p.str(std::string(1, '\0'));
        if (!sendPacket(p.d)) { err = L"申请伪终端失败"; return false; }
        // shell
        Buf sh;
        sh.u8(98);
        sh.u32(srvChan);
        sh.cstr("shell");
        sh.u8(1);
        if (!sendPacket(sh.d)) { err = L"申请 shell 失败"; return false; }
        return true;
    }

    void sendWindowChange() {
        Buf b;
        b.u8(98);
        b.u32(srvChan);
        b.cstr("window-change");
        b.u8(0);
        b.u32((uint32_t)cols);
        b.u32((uint32_t)rows);
        b.u32(0);
        b.u32(0);
        sendPacket(b.d);
    }

    bool sendChannelData(const std::string& d) {
        size_t off = 0;
        while (off < d.size()) {
            if (srvWin == 0) return false;             // 远端窗口满，等调整
            size_t n = d.size() - off;
            if (n > srvMaxPkt) n = srvMaxPkt;
            if (n > srvWin) n = srvWin;
            Buf b;
            b.u8(94);
            b.u32(srvChan);
            b.str(d.substr(off, n));
            if (!sendPacket(b.d)) return false;
            srvWin -= (uint32_t)n;
            off += n;
        }
        return true;
    }

    void sendWindowAdjust(uint32_t n) {
        if (!n) return;
        Buf b;
        b.u8(93);
        b.u32(srvChan);
        b.u32(n);
        sendPacket(b.d);
    }

    void flushOut() {
        std::string q;
        {
            std::lock_guard<std::mutex> lk(wmtx);
            q.swap(outQ);
        }
        if (q.empty()) return;
        if (!sendChannelData(q)) {
            std::lock_guard<std::mutex> lk(wmtx);
            outQ.insert(0, q);        // 窗口不够，放回去下次再发
        }
    }

    // ---------- 主循环 ----------
    void loop() {
        while (run) {
            flushOut();
            if (sizeDirty.exchange(false) && srvChan) sendWindowChange();

            fd_set rs;
            FD_ZERO(&rs); FD_SET(s, &rs);
            timeval tv{0, 40 * 1000};
            int r = select(0, &rs, nullptr, nullptr, &tv);
            if (r < 0) break;
            if (r == 0) continue;

            std::string pl;
            if (!readPacket(pl)) { closed(L"连接已断开"); break; }
            int t = (unsigned char)pl[0];
            if (t == 94 || t == 95) {                     // CHANNEL_DATA / EXTENDED_DATA
                Rd rr(pl);
                rr.u8();
                rr.u32();
                if (t == 95) rr.u32();
                std::string d = rr.str();
                if (!d.empty()) data(d);
                consumed += (uint32_t)d.size();
                if (consumed > ourWin / 2) { sendWindowAdjust(consumed); consumed = 0; }
            } else if (t == 93) {                          // WINDOW_ADJUST
                Rd rr(pl);
                rr.u8();
                rr.u32();
                srvWin += rr.u32();
            } else if (t == 98) {                          // CHANNEL_REQUEST
                Rd rr(pl);
                rr.u8();
                uint32_t ch = rr.u32();
                std::string what = rr.str();
                int wantReply = rr.u8();
                if (what == "exit-status") {
                    uint32_t code = rr.u32();
                    log(L"远端 shell 退出，状态码 " + std::to_wstring(code), 0);
                }
                if (wantReply) {
                    Buf a;
                    a.u8(99);                              // CHANNEL_SUCCESS
                    a.u32(ch);
                    sendPacket(a.d);
                }
            } else if (t == 96) {                          // EOF
                closed(L"远端已关闭会话");
                break;
            } else if (t == 97) {                          // CLOSE
                Buf a; a.u8(97); a.u32(srvChan);
                sendPacket(a.d);
                closed(L"连接已关闭");
                break;
            } else if (t == 80) {                          // GLOBAL_REQUEST
                Rd rr(pl);
                rr.u8();
                rr.str();
                int wantReply = rr.u8();
                if (wantReply) { Buf a; a.u8(82); sendPacket(a.d); }
            } else if (t == 20) {                          // 服务器发起重新协商
                sizeDirty = false;
                sizeDirty = true;
                std::wstring err2;
                log(L"服务器要求重新协商密钥…", 0);
                if (!doKex(false, err2)) { closed(L"密钥重协商失败：" + err2); break; }
            } else if (t == 1) {
                closed(L"服务器报告协议错误"); break;
            }
        }
    }

    void threadRun(std::string host, int port, std::string user, std::string pass, int c, int r) {
        cols = c; rows = r;
        std::wstring err;
        if (!connectTo(host, port, err)) { closed(err); return; }
        log(L"已建立 TCP 连接 " + w(host) + L":" + std::to_wstring(port), 0);
        if (!versionExchange(err))   { closed(err); return; }
        if (!doKex(true, err))       { closed(err); return; }
        if (!sendService())          { closed(L"发送服务请求失败"); return; }
        {
            std::string pl;
            if (!waitAuth(pl)) { closed(L"服务请求无应答"); return; }
            if ((unsigned char)pl[0] != 6) { closed(L"服务器不接受 ssh-userauth 服务"); return; }
        }
        if (!authPassword(user, pass, err)) { closed(err); return; }
        log(L"认证通过，正在打开 shell…", 0);
        if (!openShell(cols, rows, err)) { closed(err); return; }
        ready = true;
        loop();
        run = false;
        ready = false;
        if (s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; }
    }
};

// ======================= 对外接口 =======================
SshClient::SshClient() : p(new Impl()) { p->owner = this; }

SshClient::~SshClient() {
    stop();
    delete p;
}

void SshClient::start(const std::string& host, int port, const std::string& user,
                      const std::string& pass, int cols, int rows) {
    if (p->th.joinable()) return;
    p->run = true;
    p->th = std::thread([this, host, port, user, pass, cols, rows] {
        p->threadRun(host, port, user, pass, cols, rows);
    });
}

void SshClient::write(const std::string& bytes) {
    if (!p->ready) return;
    std::lock_guard<std::mutex> lk(p->wmtx);
    p->outQ += bytes;
}

void SshClient::resize(int cols, int rows) {
    // 即使 shell 还没开好也要记下来：界面刚起来时窗口尺寸往往还没定，
    // 这时算出的行列数（常见是下限 20x5）如果直接丢掉，pty 就会一直停在
    // 那个尺寸上，表现是终端里文字行距被拉得很大、命令回显乱折行。
    // 这里存下最新值，openShell 之后 loop 会补发一次 window-change。
    p->cols = cols;
    p->rows = rows;
    p->sizeDirty = true;
}

void SshClient::stop() {
    if (!p->th.joinable()) return;
    p->run = false;
    if (p->s != INVALID_SOCKET) {
        shutdown(p->s, SD_BOTH);
    }
    p->th.join();
    p->ready = false;
}

bool SshClient::alive() const { return p->ready.load(); }
