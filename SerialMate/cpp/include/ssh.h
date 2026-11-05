// ssh.h —— 自带实现的 SSH-2 客户端（无第三方库）
// 支持：curve25519-sha256 密钥交换 / aes128-ctr / hmac-sha2-256(-etm) /
//       口令认证（含 keyboard-interactive 兜底）/ 单 session 通道 + pty + shell
#pragma once
#include <string>
#include <functional>

class SshClient {
public:
    // 终端输出（服务器发来的原始字节）
    std::function<void(const std::string&)> onData;
    // 进度/错误提示：level 0=普通 1=错误
    std::function<void(const std::wstring&, int)> onLog;
    // 连接彻底结束（参数为原因）
    std::function<void(const std::wstring&)> onClosed;

    SshClient();
    ~SshClient();

    // 异步建立连接：立刻返回，连接过程在内部线程里做
    void start(const std::string& host, int port, const std::string& user, const std::string& pass,
               int cols, int rows);
    void write(const std::string& bytes);   // 键盘输入（UTF-8 字节）
    void resize(int cols, int rows);
    void stop();
    bool alive() const;

private:
    struct Impl;
    Impl* p;
};
