// serial_gui.cpp - SerialMate 串口接管助手（C++ / Win32，无第三方依赖）
// 单进程：自带串口收发 + 内置接管服务（127.0.0.1:7000），双击即用，无需另开 hub。
// 编译：x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -static -static-libgcc -static-libstdc++ \
//        -municode -mwindows -o serial_gui.exe serial_gui.cpp -lws2_32 -lcomctl32 -lcomdlg32 -lgdi32
#include "common.h"
#include "version.h"
#include "ssh.h"
#include "term.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <richedit.h>
#include <imm.h>
#include <msctf.h>
#include <cstdarg>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

// ---------------- 控件 ID ----------------
#define IDC_VIEW      1014
#define IDC_STATUS    1017
#define IDC_SIDE      1018   // 左侧连接信息栏
#define IDC_SIDEBTN   1019   // 左侧信息栏折叠/展开按钮
#define IDC_KEYS      1020   // 右侧快捷命令栏（自绘）
#define IDC_KEYBTN    1021   // 右侧快捷命令栏 折叠/展开按钮
#define IDC_KEYMODE   1022   // 右侧栏顶部 按钮/文本 模式切换
#define IDC_KEYTEXT   1023   // 右侧栏 文本模式编辑框（一行一条命令）
#define IDC_KEYBASE   1100

// ---------------- 会话对话框控件 ID ----------------
#define IDD_SER       1201   // 连接类型：串口
#define IDD_SSH       1202   // 连接类型：SSH
#define IDD_TEL       1203   // 连接类型：Telnet
#define IDD_REM       1204   // 连接类型：远程串口
#define IDD_PORT      1210
#define IDD_PORTRF    1211
#define IDD_BAUD      1212
#define IDD_M_SERVE   1213   // 模式：被接管
#define IDD_M_DIRECT  1214   // 模式：直连
#define IDD_DTR       1215
#define IDD_RTS       1216
#define IDD_SSH_HOST  1220
#define IDD_SSH_PORT  1221
#define IDD_SSH_USER  1222
#define IDD_SSH_PASS  1223
#define IDD_TEL_HOST  1230
#define IDD_TEL_PORT  1231
#define IDD_RIP       1240
#define IDD_RCOM      1241
#define IDD_RBAUD     1242
#define IDD_OK        1250
#define IDD_CANCEL    1251
#define IDD_NOTE      1260
#define IDD_IB_EDIT   1270   // 通用单行输入对话框的编辑框

// ---------------- 菜单 ID ----------------
#define IDM_PORT_OPEN        2001   // 新建会话…
#define IDM_PORT_CLOSE       2002   // 断开
#define IDM_PORT_REFRESH     2003   // 刷新端口列表
#define IDM_PORT_DTR         2004
#define IDM_PORT_RTS         2005
#define IDM_PORT_SERVE       2006
#define IDM_SESSION_RECONN   2007   // 重新连接
#define IDM_SESSION_EXIT     2008
#define IDM_BAUD_BASE        2100   // 2100 + 索引

#define IDM_TOOL_KEYS        2200   // 编辑常用命令
#define IDM_TOOL_STTY        2201   // 打开时适配终端宽度
#define IDT_STTY             1      // 一次性定时器
#define IDT_IME              2      // 输入法外来文本防抖同步
#define IDT_STATUS           3      // 状态栏节流后的补刷

#define IDM_VIEW_BIGGER      4001
#define IDM_VIEW_SMALLER     4002
#define IDM_VIEW_RESET       4003
#define IDM_VIEW_TS          4004
#define IDM_VIEW_AGENT       4005
#define IDM_VIEW_CLEAR       4006
#define IDM_VIEW_SAVE        4007
#define IDM_ENC_BASE         4010   // 接收/发送显示编码：4010 自动 4011 UTF-8 4012 GBK(文本)
#define IDM_CTX_COPY         4020   // 右键菜单：复制
#define IDM_CTX_PASTE        4021   // 右键菜单：粘贴到串口
#define IDM_CTX_ALL          4022   // 右键菜单：全选
#define IDM_LOG_SAVE         4030   // 串口日志开关
#define IDM_LOG_DIR          4031   // 设置日志目录
#define IDM_LOG_OPEN         4032   // 打开日志目录
#define IDM_LOCAL_ECHO       4033   // 本地回显开关（按键即时显示）
#define IDM_SIDE_TOGGLE      4040   // 左侧会话栏 显示/收起
#define IDM_SIDE_CONN        4041   // 左侧栏右键：连接该会话
#define IDM_SESS_CLOSE       4042   // 左侧栏右键：关闭该会话
#define IDM_KEYS_TOGGLE      4050   // 右侧快捷命令栏 显示/收起
#define IDM_KEY_EXEC         4051   // 快捷命令右键：执行
#define IDM_KEY_EDIT         4052   // 快捷命令右键：修改
#define IDM_KEY_DEL          4053   // 快捷命令右键：删除
#define IDM_KEY_ADD          4054   // 快捷命令右键：新增
#define IDM_KEY_EXPORT       4055   // 快捷命令：导出
#define IDM_KEY_IMPORT       4056   // 快捷命令：导入

// 设置菜单：菜单/对话框样式
#define IDM_SET_MFONT_BASE   4100   // 4100 + 字号索引：菜单字体大小
#define IDM_SET_MFG          4110   // 菜单文字颜色
#define IDM_SET_MBG          4111   // 菜单背景颜色
#define IDM_SET_MSEL         4112   // 菜单选中背景颜色
#define IDM_SET_DFONT_BASE   4120   // 4120 + 字号索引：对话框字体大小
#define IDM_SET_DFG          4130   // 对话框文字颜色
#define IDM_SET_DBG          4131   // 对话框背景颜色
#define IDM_SET_RESET        4140   // 恢复默认样式

#define IDM_HELP_ABOUT       5001

#define WM_APP_DATA (WM_APP + 1)   // 串口收到的数据（std::string*）
#define WM_APP_TX   (WM_APP + 2)   // 其他客户端发来的命令（pair<src,bytes>*）
#define WM_APP_META (WM_APP + 3)   // 提示信息（std::wstring*）
#define WM_APP_DEAD (WM_APP + 4)   // 串口断开（std::wstring*）
#define WM_APP_PEER (WM_APP + 5)   // 客户端数量变化（int）
#define WM_APP_SSH_DATA (WM_APP + 6)   // SSH 收到的数据（std::string*）
#define WM_APP_SSH_LOG  (WM_APP + 7)   // SSH 进度/错误（SshLog*）
#define WM_APP_SSH_END  (WM_APP + 8)   // SSH 连接结束（std::wstring*）
#define WM_APP_INPUT_RESET (WM_APP + 9) // 放弃本地未提交输入（AI 注入前清账，无参数）

// 深色终端配色
static const COLORREF C_BG     = RGB(0x1B, 0x1E, 0x24);
static const COLORREF C_RX     = RGB(0xD8, 0xDE, 0xE9);
static const COLORREF C_TX     = RGB(0x56, 0xD4, 0xDD);
static const COLORREF C_AGENT  = RGB(0xF0, 0xB4, 0x29);
static const COLORREF C_SYS    = RGB(0x7A, 0xA2, 0xF7);
static const COLORREF C_ERR    = RGB(0xEF, 0x6B, 0x73);
static const COLORREF C_HINT   = RGB(0x6B, 0x74, 0x86);

static const int kBaits[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
static const int kBaudCount = (int)(sizeof(kBaits) / sizeof(kBaits[0]));

// ---------------- 全局状态 ----------------
static HINSTANCE g_inst;
static HWND g_hwnd, g_hView, g_hStatus;
static HWND g_hSide = nullptr, g_hSideBtn = nullptr;   // 左侧连接信息栏 + 折叠按钮
static bool g_sideOn = true;
static HWND g_hKeysBar = nullptr;                      // 右侧快捷命令栏（自绘）
static HWND g_hKeysBtn = nullptr;                      // 右侧栏折叠/展开按钮
static HWND g_hKeyMode = nullptr;                      // 右侧栏顶部：按钮/文本 切换
static HWND g_hKeyText = nullptr;                      // 文本模式：多行编辑框
static bool g_keyTextMode = false;                     // true=文本模式，false=按钮模式
static bool g_keysOn = true;
static int  g_keysW  = 150;                            // 右侧栏宽度（可拖动调整）
static bool g_keyDrag = false;                         // 正在拖动改变宽度
static int  g_keyScroll = 0;                           // 快捷命令栏滚动偏移（像素）
static int  g_keyHover  = -1;
static int  g_keyFootHover = 0;                        // 底部按钮悬停：0 无 1 导入 2 导出
static int  g_ctxKey    = -1;                          // 右键命中的快捷命令索引
static const int kHandleW = 14;   // 折叠把手宽（贴在栏左边缘中间）
static const int kHandleH = 132;  // 折叠把手高（展开态）
static const int kKeyFootH = 42;  // 右侧栏底部「导入 / 导出」区高
static const int kKeyTopH  = 24;  // 右侧栏顶部「按钮/文本」切换条高
static LRESULT CALLBACK KeyProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK KeyTextProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static void drawKeysBar(const DRAWITEMSTRUCT* ds);
static void updateKeysBtn();
static void updateKeysModeBtn();
static void keyTextCollect();
static void keyTextReload(bool toEnd = false);

// 通用单行输入对话框
static bool         g_ibMode = false;
static std::wstring g_ibPrompt, g_ibText;
static HWND         g_dcIbLabel = nullptr, g_dcIbEdit = nullptr;
static void         dlgBuildInput(HWND h);
static HFONT g_fontUI;
static HMENU g_menu, g_menuSess, g_menuTool, g_menuSet;
static HWND  g_dlgWnd = nullptr;
static void  mAddMenu(HMENU m, UINT flags, UINT_PTR id, const wchar_t* text);
static bool g_termReady = false;      // 是否是 RichEdit（支持着色）

// 连接类型
enum ConnType { CONN_SERIAL = 0, CONN_SSH, CONN_TELNET, CONN_REMOTE };
static ConnType g_connType = CONN_SERIAL;

// 远端串口（连另一台 PC 上的本程序接管服务）—— 新建会话对话框的当前值
static std::wstring g_remIp  = L"192.168.0.110";
static std::wstring g_remCom = L"COM3";
static int          g_remBaud = 115200;

// SSH / Telnet 参数（本轮先存着，界面可填）
static std::wstring g_sshHost = L"192.168.0.110";
static int          g_sshPort = 22;
static std::wstring g_sshUser = L"xzs";
static std::wstring g_sshPass;
static std::wstring g_telHost = L"192.168.0.110";
static int          g_telPort = 23;

// ---------------- 会话：一条独立连接 + 一份终端画面 ----------------
// 多个会话可同时存在：新建会话不会打断原来的会话，侧栏里点一下切换
struct Session {
    int          id   = 0;
    ConnType     type = CONN_SERIAL;
    std::wstring name;                 // 侧栏显示名（COM3 / 192.168.0.110:COM3）
    std::wstring port;                 // 串口名
    int          baud = 115200;
    std::wstring remIp, remCom;        // 远程串口的对端
    int          remBaud = 115200;
    std::wstring host;                 // SSH / Telnet 主机
    int          hostPort = 0;

    SerialPort   ser;
    SOCKET       sock = INVALID_SOCKET;
    std::thread  reader;
    std::atomic<bool> stopRead{false};
    std::atomic<bool> on{false};
    std::atomic<bool> isRemote{false};
    long long    rx = 0, tx = 0;

    std::string  rtf;                  // 切走时保存的终端画面（RTF，带颜色）
    std::wstring allText;              // 纯文本存档（导出/接管用）
    std::string  lineBuf;              // 未提交行的字节账本（连同画面一起存，切回来接着写）
    size_t       lineCur = 0;
    std::wstring lineShow;             // 该行已显示出来的文字
    bool         tsNeed = true;
    std::string  pending;              // 后台期间收到的原始字节，切回来时补放
    std::mutex   mtx;

    // SSH 会话：独立的终端画面 + 连接对象（串口会话这两项为空）
    std::unique_ptr<Terminal> term;
    std::unique_ptr<SshClient> ssh;
    std::wstring sshUser;
    std::wstring sshPass;
    std::wstring sshInfo;              // 状态栏用：xzs@host:22
};

static std::vector<std::unique_ptr<Session>> g_sess;
static Session* g_cur = nullptr;       // 当前显示中的会话
static Session* g_hubSess = nullptr;   // 提供接管服务的本地串口会话（接管服务写数据要写它，
                                       // 不能写 g_cur——否则远程会话当道时会把数据回灌自己）
static int      g_nextSessId = 1;
static std::mutex g_wrMtx;             // 串口写锁（读不持锁）

static bool curOn() { return g_cur && g_cur->on; }

// 内置接管服务
static SOCKET g_srv = INVALID_SOCKET;
static std::atomic<bool> g_srvRun{false};
static std::thread g_srvThread;
static std::atomic<int> g_peerCount{0};

// 终端输入
static std::wstring g_line;                 // 当前输入行
// 本地还有多少字符「已敲出去但没回车」。接管服务写串口前必须知道这件事：
// 串口就一根线，AI 的 "\r" 会把用户打了一半的命令提前提交掉
// （实测 "echo ABC" 打到一半被 AI 写 "\r" → 板子执行了 "echo ABCpwd"）。
// 跨线程只能读原子量，g_line 本身留在 GUI 线程里。
static std::atomic<int> g_pendingKeys{0};
static std::vector<unsigned char> g_lineLen;// g_line 每个字符实际发下去的字节数（汉字 3/2 字节，
                                            // 代理对记在高的那一半上，低半部记 0）
static wchar_t g_hiSur = 0;                 // 攒着的高半代理，等低半部到齐再一起下发
static std::vector<std::wstring> g_hist;    // 历史命令
static int g_histPos = -1;
static std::wstring g_histTmp;
static wchar_t g_swallow = 0;               // 已由 KEYDOWN 处理，吞掉随之而来的 WM_CHAR
static std::wstring g_imeEcho;              // 已转发出去的上屏内容，用来吞掉随后重复的 WM_IME_CHAR
static long g_imeBase = -1;                 // 输入法开始组合时的文本长度（组合串会被内联插在它之后）

// 配置
static int g_baud = 115200;
static int g_baudIdx = 4;
static std::wstring g_portName = L"COM3";
static int g_fontPt = 12;
static bool g_showTs = true;
static bool g_tsNeed = true;      // 下一段输出落在新行行首，需要补时间戳

// 外观：菜单与对话框的字体大小 / 颜色
static const int kUiPts[] = {9, 10, 11, 12, 14, 16};
static const int kUiPtCount = (int)(sizeof(kUiPts) / sizeof(kUiPts[0]));
static int      g_menuPtIdx = 1;   // 菜单字体：kUiPts 下标
static int      g_dlgPtIdx  = 1;   // 对话框字体：kUiPts 下标
static COLORREF g_menuFg   = RGB(0x24, 0x24, 0x24);
static COLORREF g_menuBg   = RGB(0xF6, 0xF6, 0xF6);
static COLORREF g_menuSel  = RGB(0x2E, 0x7C, 0xE0);
static COLORREF g_dlgFg    = RGB(0x24, 0x24, 0x24);
static COLORREF g_dlgBg    = RGB(0xF2, 0xF3, 0xF5);
static HFONT    g_fontMenu = nullptr;
static int      g_menuRowH = 26;
static HBRUSH   g_brushMenu = nullptr, g_brushDlg = nullptr;
static bool g_showAgent = true;
static bool g_sttyAdapt = true;   // 打开会话后发 stty 让板子知道终端很宽（长命令不错位）
static bool g_serve = true;
static bool g_localEcho = true;   // 按键本地先显示，不等板子回显（关掉则完全靠回显）
static bool g_dtr = false, g_rts = false;

// 串口显示/发送编码：0=自动（UTF-8 优先，失败回退 GBK） 1=UTF-8  2=GBK(文本/ANSI)
static int g_rxEnc = 0;

static std::wstring g_allText;
static std::mutex g_textMtx;
static FILE* g_log = nullptr;
static bool  g_logSave = true;              // 是否落盘保存串口日志
static std::wstring g_logDir;               // 空 = exe 目录下 logs

static std::vector<std::wstring> g_keys;    // 快捷命令（可增删改、导入导出）

// ---------------- 小工具 ----------------
static std::wstring exeDir() {
    wchar_t p[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    size_t k = s.find_last_of(L'\\');
    return (k == std::wstring::npos) ? L"." : s.substr(0, k);
}

static std::wstring timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[40];
    swprintf_s(b, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    return b;
}

static bool isChecked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static int  comboIndex(HWND h) { return (int)SendMessageW(h, CB_GETCURSEL, 0, 0); }

static int baudIndexOf(int baud) {
    for (int i = 0; i < kBaudCount; i++) if (kBaits[i] == baud) return i;
    return 4;
}

// ---------------- 显示编码 ----------------
// 0=自动（UTF-8 优先，不合法就退到 GBK）  1=UTF-8  2=GBK(文本/ANSI)
// 说明：切换后 RX 显示、日志存档、以及中文/其它非 ASCII 字符的**发送**都走同一套码位，
// 这样“屏幕上看到的”和“板子实际收到的”永远一致（设备是 GBK 就该发 GBK，反之 UTF-8）。

// GBK 解码：末尾落单的首字节（半个汉字）先不显示，等下一个字节补齐
static std::wstring gbkToWide(const std::string& s) {
    std::wstring out;
    size_t i = 0, n = s.size();
    out.reserve(n);
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out.push_back((wchar_t)c); i++; continue; }
        if (i + 1 >= n) break;                                   // 只有高位字节 → 半个汉字，留着
        char mb[3] = {(char)c, (char)(unsigned char)s[i + 1], 0};
        wchar_t wbuf[2] = {0, 0};
        if (MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, mb, 2, wbuf, 2) > 0) { out += wbuf; i += 2; }
        else { out.push_back(0xFFFD); i++; }
    }
    return out;
}

// 串口字节 -> 显示宽串（按当前选择的码位）
static std::wstring rxDecode(const std::string& s) {
    if (s.empty()) return L"";
    if (g_rxEnc == 1) return utf8ToWideStrict(s);                // 严格 UTF-8：非法/残片不猜字
    if (g_rxEnc == 2) return gbkToWide(s);
    // 自动：UTF-8 合法就用 UTF-8；非法字节多了就改判 GBK
    std::wstring u = utf8ToWideStrict(s);
    size_t bad = 0;
    for (wchar_t c : u) if (c == 0xFFFD) bad++;
    if (bad == 0) return u;
    std::wstring g = gbkToWide(s);
    size_t gbad = 0;
    for (wchar_t c : g) if (c == 0xFFFD) gbad++;
    return (gbad < bad) ? g : u;
}

// 本地字符 -> 要发到串口的字节（同上，跟着当前码位走）
static std::string txEncode(const std::wstring& w) {
    if (w.empty()) return "";
    // SSH 走的是标准终端协议，编码固定 UTF-8，不吃串口那边的 GBK 设置
    if (g_cur && g_cur->type == CONN_SSH) return wideToUtf8(w);
    if (g_rxEnc == 2) {
        int n = WideCharToMultiByte(936, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return "";
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(936, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }
    return wideToUtf8(w);
}

static const wchar_t* encName() {
    return (g_rxEnc == 1) ? L"UTF-8" : (g_rxEnc == 2) ? L"文本 ANSI/GBK" : L"自动";
}

// ---------------- 配置读写 ----------------
static std::wstring iniPath() { return exeDir() + L"\\serialmate.ini"; }

static void loadSettings() {
    FILE* f = _wfopen(iniPath().c_str(), L"r, ccs=UTF-8");
    if (!f) return;
    wchar_t line[512];
    while (fgetws(line, 512, f)) {
        std::wstring s(line);
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == L"port") g_portName = v;
        else if (k == L"baud") g_baud = _wtoi(v.c_str());
        else if (k == L"font") g_fontPt = _wtoi(v.c_str());
        else if (k == L"ts") g_showTs = (v == L"1");
        else if (k == L"agent") g_showAgent = (v == L"1");
        else if (k == L"serve") g_serve = (v == L"1");
        else if (k == L"stty") g_sttyAdapt = (v == L"1");
        else if (k == L"conn") g_connType = (ConnType)_wtoi(v.c_str());
        else if (k == L"remip") g_remIp = v;
        else if (k == L"remcom") g_remCom = v;
        else if (k == L"rembaud") g_remBaud = _wtoi(v.c_str());
        else if (k == L"sshhost") g_sshHost = v;
        else if (k == L"sshport") g_sshPort = _wtoi(v.c_str());
        else if (k == L"sshuser") g_sshUser = v;
        else if (k == L"sshpass") g_sshPass = v;
        else if (k == L"conntype") g_connType = (ConnType)_wtoi(v.c_str());
        else if (k == L"telhost") g_telHost = v;
        else if (k == L"telport") g_telPort = _wtoi(v.c_str());
        else if (k == L"enc") g_rxEnc = _wtoi(v.c_str());
        else if (k == L"logsave") g_logSave = (v == L"1");
        else if (k == L"logdir") g_logDir = v;
        else if (k == L"lcho") g_localEcho = (v == L"1");
        else if (k == L"side") g_sideOn = (v == L"1");
        else if (k == L"keys") g_keysOn = (v == L"1");
        else if (k == L"keyw") g_keysW = _wtoi(v.c_str());
        else if (k == L"mpt") g_menuPtIdx = _wtoi(v.c_str());
        else if (k == L"dpt") g_dlgPtIdx = _wtoi(v.c_str());
        else if (k == L"mfg") g_menuFg = (COLORREF)_wtoi(v.c_str());
        else if (k == L"mbg") g_menuBg = (COLORREF)_wtoi(v.c_str());
        else if (k == L"msel") g_menuSel = (COLORREF)_wtoi(v.c_str());
        else if (k == L"dfg") g_dlgFg = (COLORREF)_wtoi(v.c_str());
        else if (k == L"dbg") g_dlgBg = (COLORREF)_wtoi(v.c_str());
    }
    fclose(f);
    if (g_fontPt < 8 || g_fontPt > 32) g_fontPt = 12;
    if (g_menuPtIdx < 0 || g_menuPtIdx >= kUiPtCount) g_menuPtIdx = 1;
    if (g_dlgPtIdx  < 0 || g_dlgPtIdx  >= kUiPtCount) g_dlgPtIdx  = 1;
    g_baudIdx = baudIndexOf(g_baud);
    g_baud = kBaits[g_baudIdx];
    if ((int)g_connType < 0 || (int)g_connType > 3) g_connType = CONN_SERIAL;
    if (g_rxEnc < 0 || g_rxEnc > 2) g_rxEnc = 0;
    if (g_keysW < 90 || g_keysW > 460) g_keysW = 150;
}

static void saveSettings() {
    FILE* f = _wfopen(iniPath().c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"port=%ls\nbaud=%d\nfont=%d\nts=%d\nagent=%d\nserve=%d\nstty=%d\n",
             g_portName.c_str(), g_baud, g_fontPt, g_showTs ? 1 : 0, g_showAgent ? 1 : 0,
             g_serve ? 1 : 0, g_sttyAdapt ? 1 : 0);
    fwprintf(f, L"conn=%d\nremip=%ls\nremcom=%ls\nrembaud=%d\n",
             (int)g_connType, g_remIp.c_str(), g_remCom.c_str(), g_remBaud);
    fwprintf(f, L"sshhost=%ls\nsshport=%d\nsshuser=%ls\nsshpass=%ls\nconntype=%d\n",
             g_sshHost.c_str(), g_sshPort, g_sshUser.c_str(), g_sshPass.c_str(), (int)g_connType);
    fwprintf(f, L"telhost=%ls\ntelport=%d\nenc=%d\n",
             g_telHost.c_str(), g_telPort, g_rxEnc);
    fwprintf(f, L"logsave=%d\nlogdir=%ls\nlcho=%d\nside=%d\nkeys=%d\nkeyw=%d\n",
             g_logSave ? 1 : 0, g_logDir.c_str(), g_localEcho ? 1 : 0, g_sideOn ? 1 : 0,
             g_keysOn ? 1 : 0, g_keysW);
    fwprintf(f, L"mpt=%d\ndpt=%d\nmfg=%u\nmbg=%u\nmsel=%u\ndfg=%u\ndbg=%u\n",
             g_menuPtIdx, g_dlgPtIdx, (unsigned)g_menuFg, (unsigned)g_menuBg,
             (unsigned)g_menuSel, (unsigned)g_dlgFg, (unsigned)g_dlgBg);
    fclose(f);
}

// ---------------- 日志 ----------------
// —— 输入法路径临时诊断日志（排查中文上屏/删除用，稳定后删掉）——
static FILE* g_dbg = nullptr;
static void dbgLog(const wchar_t* fmt, ...) {
    if (!g_dbg) {
        std::wstring dir = exeDir() + L"\\logs";
        CreateDirectoryW(dir.c_str(), nullptr);
        g_dbg = _wfopen((dir + L"\\ime_debug.log").c_str(), L"a, ccs=UTF-8");
        if (!g_dbg) return;
    }
    std::wstring ts = timestamp();
    fwprintf(g_dbg, L"%ls", ts.c_str());
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(g_dbg, fmt, ap);
    va_end(ap);
    fwprintf(g_dbg, L"\n");
    fflush(g_dbg);
}

static std::wstring logDirPath() {
    return g_logDir.empty() ? (exeDir() + L"\\logs") : g_logDir;
}

static std::wstring g_logDirOpen;            // 当前已打开的日志目录

static void closeLog() {
    if (g_log) { fclose(g_log); g_log = nullptr; }
    g_logDirOpen.clear();
}

static void ensureLog() {
    std::wstring dir = logDirPath();
    if (g_log && dir == g_logDirOpen) return;
    closeLog();
    CreateDirectoryW(dir.c_str(), nullptr);
    g_log = _wfopen((dir + L"\\serial.log").c_str(), L"a, ccs=UTF-8");
    if (g_log) g_logDirOpen = dir;
}

static void logLine(const wchar_t* dir, const std::string& bytes) {
    if (!g_logSave) return;                  // 右键/菜单里关掉了日志保存
    ensureLog();
    if (!g_log) return;
    std::wstring w = normalizeCrLf(rxDecode(bytes));
    std::wstring ts = timestamp();
    bool any = false;
    size_t s = 0;
    while (s <= w.size()) {
        size_t e = w.find(L'\n', s);
        std::wstring ln = (e == std::wstring::npos) ? w.substr(s) : w.substr(s, e - s);
        while (!ln.empty() && ln.back() == L'\r') ln.pop_back();
        if (!ln.empty()) { fwprintf(g_log, L"%ls%-3ls %ls\n", ts.c_str(), dir, ln.c_str()); any = true; }
        if (e == std::wstring::npos) break;
        s = e + 1;
    }
    if (!any && !bytes.empty())
        fwprintf(g_log, L"%ls%-3ls ⏎\n", ts.c_str(), dir);   // 纯回车（Enter）
    fflush(g_log);
}

// ---------------- 终端显示（按字节的行缓冲终端） ----------------
// 关键：终端里一行的真相是「字节流」，不是宽字符。
// readline 这类行编辑器按**字节**算列、按字节退格；我们若按字符算，一遇到中文
// （3 字节/字）两边就会错位，表现为「退格没反应、删不干净」。
// 所以这里维护当前行的原始字节缓冲 g_lb + 字节光标 g_lc，
// 每次变化后把整行重新解码渲染（一行很短，重画代价可以忽略）。
static std::string  g_lb;                 // 当前行原始字节
static size_t       g_lc      = 0;        // 光标在本行中的字节偏移
static long         g_lcp     = 0;        // 当前行文本在控件中的起始字符位置
static std::wstring g_lshow;              // 当前行已渲染出来的宽字符（用于整行替换）
static int          g_esc     = 0;        // 0=普通 1=ESC 2=CSI 3=OSC 4=两字节转义
static std::wstring g_csi;                // CSI 参数缓存
static COLORREF     g_tcolor  = C_RX;     // 当前输出颜色
static int          g_bsPend  = 0;        // 本地已删、等待板子回显的 0x08 个数（吞掉避免重复删）
static int          g_bsSkip  = 0;        // 0=正常 1=刚吞掉 0x08（可能是 "\b \b" 前半） 2=已吞空格

// —— 本地回显 ——
// 串口链路是「我们发→板子收→板子回显→我们收」，一来一回几十毫秒（USB 转串口
// 芯片的 latency timer 占大头）。完全靠板子回显，敲键就会慢半拍，退格更明显。
// 所以按键先在本地画出来（零延迟），板子随后把同样的字节回显回来时再丢掉。
// g_echoQ 存「本地已画、还没被回显确认」的原始字节，按**内容**逐字节比对：
// 只认一模一样的字节，一旦对不上（板子关着 echo、或者已经开始打命令输出），
// 立刻清空队列按正常输出处理——早先用计数的方式在 `stty -echo` 下会把
// `/home/root` 的前几个字节当成回显吃掉，屏幕上就成 "me/root"。
static std::string  g_echoQ;
static DWORD        g_echoStamp = 0;


static void termSendBytes(const std::string& bytes, COLORREF color, const wchar_t* tag);
static void appendRun(const std::wstring& text, COLORREF color);
static void termTypeChar(wchar_t ch);
static void showViewMenu(HWND h);      // 输出框右键菜单

// —— 终端自绘内容的权威副本 ——
// 新版输入法（搜狗/微软拼音走 TSF）会把上屏结果**直接**写进 RichEdit 文档，
// 不经过 WM_IME_CHAR / WM_IME_COMPOSITION：字出现在屏上、串口却一无所知，
// 退格自然也删不掉（这就是"中文删不掉"的另一条路径）。
// 办法：g_expect 记录"我们认为控件里应该有的文本"；外来改动（EN_CHANGE）做
// 前后缀 diff，把外来内容撤掉、转发给串口。TSF 拼写过程中的组合串不算外来，
// 用防抖定时器 + TSF 组合状态查询区分（见 syncForeignText）。
static std::wstring g_expect;               // 控件中应有的文本（我们自己维护）
static bool g_selfEdit = false;             // 我们自己正在改控件文本
static DWORD g_foreignGuard = 0;            // 换会话后的一小段时间内不把整屏变化当外来输入

// 我们自己的文本改动都要包一层（EN_CHANGE 里据此区分内外）
static void viewReplace(const wchar_t* t) {
    g_selfEdit = true;
    SendMessageW(g_hView, EM_REPLACESEL, FALSE, (LPARAM)t);
    g_selfEdit = false;
}

static long viewTextLen(HWND h) { return (long)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0); }

// 把插入点挪到文档真正的末尾再读回来。
// 坑：RichEdit 的 WM_GETTEXTLENGTH 把段落符按 \r\n 两个字符计，而 CHARRANGE/EM_EXSETSEL
// 的下标是按 \r 一个字符算的——直接拿长度当插入位置，每多一行就偏 1，
// 攒够以后 EM_REPLACESEL 会落在**错误的位置**上，把提示符重复写进画面
// （表现为 root@root@ATK-IMX6U:~#，切会话后必现）。
static long caretToEnd() {
    CHARRANGE cr{ 0x7FFFFFFF, 0x7FFFFFFF };
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARRANGE out{};
    SendMessageW(g_hView, EM_EXGETSEL, 0, (LPARAM)&out);
    return out.cpMax;
}

// 全文：用 GT_DEFAULT，行尾是单个 \r，与 CHARRANGE 下标口径一致
static std::wstring viewText() {
    if (!g_hView) return L"";
    if (!g_termReady) {                              // 普通 EDIT 没有 EM_GETTEXTEX
        long n = (long)SendMessageW(g_hView, WM_GETTEXTLENGTH, 0, 0);
        std::wstring s((size_t)n + 1, L'\0');
        SendMessageW(g_hView, WM_GETTEXT, (WPARAM)(n + 1), (LPARAM)s.data());
        s.resize(wcslen(s.c_str()));
        return s;
    }
    GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_DEFAULT, 1200};
    long n = (long)SendMessageW(g_hView, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    if (n <= 0) return L"";
    std::wstring s((size_t)n + 16, L'\0');
    GETTEXTEX gt{sizeof(gt), GT_DEFAULT, 1200, nullptr};
    DWORD got = (DWORD)SendMessageW(g_hView, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s.data());
    s.resize(got < s.size() ? (size_t)got : s.size());
    return s;
}

static std::wstring viewRange(HWND h, long s, long e) {
    std::wstring t;
    if (e <= s) return t;
    t.assign((size_t)(e - s), L'\0');
    TEXTRANGEW tr{{s, e}, t.data()};
    if (SendMessageW(h, EM_GETTEXTRANGE, 0, (LPARAM)&tr) <= 0) t.clear();
    return t;
}

static void viewDeleteRange(HWND h, long s, long e) {
    CHARRANGE del{s, e};
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&del);
    g_selfEdit = true;                      // 删除是我们自己的动作，不触发外来文本逻辑
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)L"");
    g_selfEdit = false;
}

static void cacheText(const std::wstring& t) {
    std::lock_guard<std::mutex> lk(g_textMtx);
    g_allText += t;
    if (g_allText.size() > 4 * 1024 * 1024) g_allText.erase(0, g_allText.size() - 3 * 1024 * 1024);
}

// 上一次成功设到选区的颜色（相同就不要重复设，见 termSyncLine）
// EM_SETCHARFORMAT(SCF_SELECTION) 会让 RichEdit 重排并重绘这段文本，
// 每敲一个字符都设一次，整行就一直在闪。
static COLORREF     g_appliedColor = 0xFFFFFFFF;  // 插入点当前生效的颜色
static long         g_colorEnd     = -1;          // 该颜色文本的末尾位置（插入点在此可复用，不必重设）
static void         termColorReset() { g_appliedColor = 0xFFFFFFFF; g_colorEnd = -1; }

// 把插入点设成终端基准格式：字体 + 字号 + 字符集 + 当前颜色。
//
// 只设颜色是不够的。输入法上屏时 RichEdit/TSF 会往文档里塞自己的字体
// （宋体等的 yHeight 比 Consolas 大），我们随后虽然把那几个字删掉了，
// **插入点格式却留在了那个更大的字体上**，于是之后板子的所有输出都继承它——
// 表现为「输入汉字后行距变大、后面输出的字也变大」，而且一直不恢复。
// 所以每次写文本之前把字体/字号一起压实，颜色只是其中一项。
static void termApplyColor() {
    if (!g_termReady) return;
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
#ifdef TERM_FMT_OLD
    cf.dwMask = CFM_COLOR;                    // 对照测试：只看颜色的老行为
#else
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR;
#endif
    cf.yHeight = g_fontPt * 20;
    cf.bCharSet = DEFAULT_CHARSET;
    lstrcpynW(cf.szFaceName, L"Consolas", LF_FACESIZE);
    cf.crTextColor = g_tcolor;
    SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    g_appliedColor = g_tcolor;
}

// 文档字符数：GTL_NUMCHARS 按 \r 一个字符算，和 CHARRANGE 下标同一口径
static long docChars() {
    GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_DEFAULT, 1200};
    return (long)SendMessageW(g_hView, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
}

// 插入点必须停在「当前行末尾」。终端里光标跑到别处是没有意义的，
// 但 RichEdit 是可编辑控件，会在这些情况下把插入点挪走：
//   · 在输出区点一下（想选文字/顺手点窗口）→ 插入点落到点击位置
//   · 输入法上屏后 syncForeignText() 删掉本地插入的字 → 插入点塌在删除处
//   · Ctrl+A 全选之后又点了别处
// 表现为「光标跑到屏幕中间、不在最后一行」。这里把它拉回行尾；
// 用户正在框选（cpMin != cpMax）时不动，那是有意为了复制。
static void termEnsureCaret() {
    if (!g_hView || !g_termReady) return;
    CHARRANGE cr{};
    SendMessageW(g_hView, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMax != cr.cpMin) return;                       // 有选区：用户框选，别打断
    long want = g_lcp + (long)g_lshow.size();
    if (cr.cpMax == want) return;
    dbgLog(L"caret fix: was=%ld want=%ld", (long)cr.cpMax, want);
    CHARRANGE to{want, want};
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&to);
    SendMessageW(g_hView, EM_SCROLLCARET, 0, 0);
}

// 当前行起点 + 已渲染长度 应当正好等于文档末尾（每次写完都维持这个不变量）。
// 对不上说明文档被别的路径改过（切会话、外来文本、被裁剪），
// 这时按旧锚点写会把字插到屏幕中间，所以只在「我们的行已经不在文末」这种
// 安全情况下把锚点重新落到文末，并清掉已渲染缓存让它整行重画。
static void termAnchorCheck() {
    long end = docChars();
    long want = g_lcp + (long)g_lshow.size();
    if (end == want) return;
    dbgLog(L"anchor drift: end=%ld want=%ld", end, want);
    if (end < want) {                        // 我们的行被删掉了：重新落到文末重画
        g_lcp = end;
        g_lshow.clear();
    }
}

// 把当前行按字节缓冲重画一遍
//
// 只替换「公共前缀之后的那一段」。
// 早先的实现每次都把整行（含提示符 root@ATK-IMX6U:~#）选中后 EM_REPLACESEL，
// 等于每敲一个字符就把整行删掉重写，屏幕上提示符和前文会一下一下地闪；
// 现在按前缀求差集：在行尾追加时公共前缀就是整行，选区退化成插入点，
// 只有真正改动的那几个字被重画，提示符不会再动。
static void termSyncLine() {
    if (!g_hView || !g_termReady) return;
    termAnchorCheck();
    std::wstring r = rxDecode(g_lb);
    if (r == g_lshow) return;
    size_t n = r.size() < g_lshow.size() ? r.size() : g_lshow.size();
    size_t cp = 0;
    while (cp < n && r[cp] == g_lshow[cp]) ++cp;
    long s = g_lcp + (long)cp;
    CHARRANGE cr{s, g_lcp + (long)g_lshow.size()};
#ifdef TERM_TRACE
    { CHARFORMAT2W q{}; q.cbSize = sizeof(q);
      SendMessageW(g_hView, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&q);
      FILE* f = fopen("term_trace.txt", "a");
      if (f) { fprintf(f, "cp=%zu old=%zu new=%zu erase=%zu yH=%d mask=%08lx\n", cp, g_lshow.size(),
                       r.size(), g_lshow.size() - cp, (int)q.yHeight, (unsigned long)q.dwMask); fclose(f); } }
#endif
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&cr);
    // 插入点紧接在同一颜色的文本后面时，格式天然就是对的，不必再设一次
    if (g_tcolor != g_appliedColor || s != g_colorEnd) termApplyColor();
    viewReplace(r.c_str() + cp);
    g_lshow = r;
    g_colorEnd = s + (long)(r.size() - cp);
    SendMessageW(g_hView, EM_SCROLLCARET, 0, 0);
}

// 光标处写入字节：右侧已有内容按“覆盖”处理，超出部分插入（和真终端一致）
// 行首时间戳：插在视图当前行起点，不进入 g_lb（字节账本不受影响）
static void termInsertTs() {
    if (!g_hView || !g_termReady) return;
    std::wstring ts = timestamp();
    CHARRANGE cr{g_lcp, g_lcp};
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&cr);
    COLORREF old = g_tcolor;
    g_tcolor = C_SYS;
    termApplyColor();
    g_tcolor = old;
    viewReplace(ts.c_str());
    g_lcp += (long)ts.size();
    g_lshow.clear();
    g_colorEnd = g_lcp;
}

static void termInsertBytes(const std::string& s) {
    if (s.empty()) return;
    size_t avail = g_lb.size() > g_lc ? g_lb.size() - g_lc : 0;
    size_t ov = s.size() < avail ? s.size() : avail;
    if (ov) g_lb.replace(g_lc, ov, s.c_str(), ov);
    if (s.size() > ov) g_lb.insert(g_lc + ov, s.c_str() + ov, s.size() - ov);
    g_lc += s.size();
    termSyncLine();
}

// 本地回显：把刚发出去的字节立刻画到屏幕上，不等板子返回。
// 记下 g_echoQ，板子的回显字节随后到达时按内容比对丢弃（见 termFeed）。
//
// 例外：密码/口令提示行不回显。板子在这类提示下会关掉 echo，我们若抢先画出来，
// 密码就明文留在屏幕上了（真正靠回显时它本来就是不显示的）。
static bool echoSensitive() {
    if (g_lshow.size() < 6) return false;
    size_t from = g_lshow.size() > 24 ? g_lshow.size() - 24 : 0;
    std::wstring tail = g_lshow.substr(from);
    for (auto& c : tail) c = towlower(c);
    return tail.find(L"assword") != std::wstring::npos ||
           tail.find(L"密码")     != std::wstring::npos ||
           tail.find(L"口令")     != std::wstring::npos;
}

static void termLocalEcho(const std::string& b) {
    if (!g_localEcho || b.empty() || echoSensitive()) return;
    g_echoQ += b;
    g_echoStamp = GetTickCount();
    termInsertBytes(b);
}

// 换行：把当前行定格
static void termCommitLine() {
    long end = g_lcp + (long)g_lshow.size();
    CHARRANGE cr{end, end};
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&cr);
    termApplyColor();
    viewReplace(L"\r");
    g_lcp = end + 1;
    g_colorEnd = g_lcp;
    g_lb.clear();
    g_lc = 0;
    g_bsPend = 0;
    g_bsSkip = 0;
    g_echoQ.clear();
    g_tsNeed = true;                    // 新行开始，下一次输出补时间戳
    g_lshow.clear();
    termColorReset();                   // 新行的插入点格式要重新设一次
    SendMessageW(g_hView, EM_SCROLLCARET, 0, 0);
}

// 板子回显的退格：一次删一个**字节**（中文要三次才删完，由自适应补发处理）
static void termEchoBackspace() {
    if (g_lc == 0 || g_lb.empty()) return;
    g_lb.erase(g_lc - 1, 1);
    g_lc--;
    termSyncLine();
}

static void termEraseInLine(int mode) {
    if (mode == 2) { g_lb.clear(); g_lc = 0; }
    else if (mode == 1) { g_lb.erase(0, g_lc < g_lb.size() ? g_lc : g_lb.size()); g_lc = 0; }
    else if (g_lc < g_lb.size()) g_lb.resize(g_lc);
    termSyncLine();
}

static void termClearAll() {
    if (!g_hView) return;
    g_selfEdit = true;
    SetWindowTextW(g_hView, L"");
    g_selfEdit = false;
    g_lb.clear();
    g_lc = 0;
    g_bsPend = 0;
    g_bsSkip = 0;
    g_echoQ.clear();
    g_lshow.clear();
    g_lcp = 0;
    g_tsNeed = true;
    termColorReset();
    { std::lock_guard<std::mutex> lk(g_textMtx); g_allText.clear(); }
}

static void termCsi(const std::wstring& raw, wchar_t fin) {
    std::wstring p = raw;
    if (!p.empty() && (p[0] == L'?' || p[0] == L'>' || p[0] == L'<' || p[0] == L'=')) p.erase(0, 1);
    int a = 0;
    if (!p.empty()) {
        size_t sc = p.find(L';');
        a = _wtoi(sc == std::wstring::npos ? p.c_str() : p.substr(0, sc).c_str());
    }
    switch (fin) {
        case L'K': termEraseInLine(a); break;
        case L'J':
            if (a == 2 || a == 3) termClearAll();
            else if (a == 0) { if (g_lc < g_lb.size()) g_lb.resize(g_lc); termSyncLine(); }
            break;
        case L'P': {                                    // 删掉光标处 a 个字节（readline 重绘常用）
            if (g_lc < g_lb.size()) {
                size_t n = (a > 0) ? (size_t)a : 1;
                if (n > g_lb.size() - g_lc) n = g_lb.size() - g_lc;
                g_lb.erase(g_lc, n);
                termSyncLine();
            }
            break;
        }
        case L'@': {                                    // 插入 a 个空格
            size_t n = (a > 0) ? (size_t)a : 1;
            g_lb.insert(g_lc, std::string(n, ' '));
            termSyncLine();
            break;
        }
        case L'C': g_lc += (a > 0 ? (size_t)a : 1); if (g_lc > g_lb.size()) g_lc = g_lb.size(); break;
        case L'D': { size_t n = (a > 0 ? (size_t)a : 1); g_lc = (g_lc > n) ? g_lc - n : 0; break; }
        case L'G': g_lc = (a > 1) ? (size_t)(a - 1) : 0; if (g_lc > g_lb.size()) g_lc = g_lb.size(); break;
        default: break;                                 // m(颜色) / h / l / H 等忽略
    }
}

// 行末字符占几个字节（1..4）
static int trailingCharBytes() {
    if (g_lb.empty()) return 1;
    size_t i = g_lb.size();
    size_t cont = 0;
    while (i > 0 && ((unsigned char)g_lb[i - 1] & 0xC0) == 0x80 && cont < 3) { i--; cont++; }
    if (i == 0) return (int)(cont + 1 > 4 ? 4 : cont + 1);
    unsigned char lead = (unsigned char)g_lb[i - 1];
    int need = 1;
    if ((lead >> 5) == 0x6) need = 2;
    else if ((lead >> 4) == 0xE) need = 3;
    else if ((lead >> 3) == 0x1E) need = 4;
    return need;
}

static void termFeed(const std::string& bytes) {
    if (!g_hView || bytes.empty()) return;
    if (!g_termReady) {                                 // 退化控件：过滤后直接追加
        std::wstring t;
        for (size_t i = 0; i < bytes.size(); i++) {
            unsigned char c = (unsigned char)bytes[i];
            if (c == 0x08) { if (!t.empty() && t.back() != L'\n') t.pop_back(); }
            else if (c == '\r') {}
            else if (c == '\n') t += L"\r\n";
            else if (c >= 0x20 && c != 0x7F && c < 0x80) t.push_back((wchar_t)c);
        }
        appendRun(t, C_RX);
        return;
    }
    std::string run;
    auto flush = [&]() {
        if (run.empty()) return;
        if (g_showTs && g_tsNeed) { g_tsNeed = false; termInsertTs(); }   // 每行行首补一次
        termInsertBytes(run);
        run.clear();
    };
    for (size_t i = 0; i < bytes.size(); i++) {
        unsigned char c = (unsigned char)bytes[i];

        // 退格回显的两种形态（实测）：
        //   删 ASCII  -> 08 20 08
        //   删汉字    -> 08 ESC[K   （一个字节一组，三组删掉一个字）
        // 本地已经删过内容，这里把配套的 0x08 / 空格吞掉，避免二次删除
        if (g_bsSkip == 1) {
            if (c == 0x20) { g_bsSkip = 2; continue; }
            g_bsSkip = 0;
        } else if (g_bsSkip == 2) {
            g_bsSkip = 0;
            if (c == 0x08) continue;
        }

        if (g_esc == 1) {
            if (c == '[') { g_esc = 2; g_csi.clear(); }
            else if (c == ']') g_esc = 3;
            else if (c == '(' || c == ')' || c == '#' || c == '%') g_esc = 4;
            else g_esc = 0;
            continue;
        }
        if (g_esc == 2) {
            if (c >= 0x40 && c <= 0x7E) { flush(); termCsi(g_csi, (wchar_t)c); g_esc = 0; }
            else if (g_csi.size() < 32) g_csi.push_back((wchar_t)c);
            continue;
        }
        if (g_esc == 3) { if (c == 0x07) g_esc = 0; else if (c == 0x1B) g_esc = 1; continue; }
        if (g_esc == 4) { g_esc = 0; continue; }

        if (c == 0x1B)        { flush(); g_esc = 1; g_echoQ.clear(); }
        else if (c == '\r')   { flush(); g_lc = 0; g_echoQ.clear(); }
        else if (c == '\n')   { flush(); termCommitLine(); g_echoQ.clear(); }
        else if (c == 0x08)   { flush(); if (g_bsPend > 0) { g_bsPend--; g_bsSkip = 1; } else termEchoBackspace(); }
        else if (c == '\t')   { flush(); termInsertBytes(std::string(8 - (g_lc % 8), ' ')); }
        else if (c < 0x20 || c == 0x7F) { /* 其余控制字符丢弃，不再显示成方块 */ }
        else {
            // 这些字节如果是我们自己刚敲下去、已经在屏幕上画过的，就丢掉。
            // 按内容比对：对不上说明不是回显（板子关了 echo，或者这边已经开始
            // 吐命令结果），立刻停手，否则后面的输出会被吃掉开头几个字节。
            if (!g_echoQ.empty()) {
                if (g_echoQ[0] == (char)c && GetTickCount() - g_echoStamp <= 400) {
                    g_echoQ.erase(0, 1);       // 本地已显示，回显丢弃
                    continue;
                }
                g_echoQ.clear();
            }
            run.push_back((char)c);
        }
    }
    flush();
}

static void appendRun(const std::wstring& text, COLORREF color) {
    if (!g_hView || text.empty()) return;
    // SSH 会话的「画面」在终端控件里，本地提示也写进去
    if (g_cur && g_cur->type == CONN_SSH) {
        cacheText(text);
        if (g_cur->term) g_cur->term->writeText(text);
        return;
    }
    // 当前行还没收尾（板子刚打出的提示符就停在行尾）时先把它定格，
    // 否则 "◀ [AGENT] xxx" 会粘在你输入的内容后面，看起来像命令被改写了
    if (!g_lshow.empty()) { termAnchorCheck(); termCommitLine(); cacheText(L"\r"); }
    cacheText(text);
    CHARRANGE crEnd{-1, -1};                            // -1 = 文末
    SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&crEnd);
    COLORREF oldTc = g_tcolor;
    g_tcolor = color;
    termApplyColor();
    viewReplace(text.c_str());
    g_tcolor = oldTc;                                   // 恢复：板子后续输出还得用正常色
    CHARRANGE crCur{};
    SendMessageW(g_hView, EM_EXGETSEL, 0, (LPARAM)&crCur);
    g_lcp = crCur.cpMax;                                // 新行起点
    g_lb.clear();
    g_lc = 0;
    g_echoQ.clear();                                     // 本地提示行插在中间，回显账本作废
    g_lshow.clear();
    SendMessageW(g_hView, EM_SCROLLCARET, 0, 0);
}

// 串口收到的字节 -> 终端
static void showRx(const std::string& bytes) {
    if (g_cur) g_cur->rx += (long long)bytes.size();
    termFeed(bytes);                    // 时间戳由 termFeed 在每行行首插入

    // 存档用的纯文本
    std::string keep;
    keep.reserve(bytes.size());
    for (size_t i = 0; i < bytes.size(); i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == 0x08) { if (!keep.empty() && keep.back() != '\n') keep.pop_back(); }
        else if (c == '\r') {}
        else if (c == '\n') keep.push_back('\n');
        else if (c >= 0x20 && c != 0x7F) keep.push_back((char)c);
    }
    cacheText(rxDecode(keep));
    logLine(L"RX", bytes);
}

// 折叠把手通用绘制：浅灰竖条 + 边框 + 居中箭头（左右两侧栏共用）
static void drawHandleBtn(const DRAWITEMSTRUCT* ds) {
    RECT r = ds->rcItem;
    bool down = (ds->itemState & ODS_SELECTED) != 0;
    HBRUSH pb = CreateSolidBrush(down ? RGB(0xD2, 0xE0, 0xF8) : RGB(0xE3, 0xE7, 0xEE));
    FillRect(ds->hDC, &r, pb);
    DeleteObject(pb);
    HPEN pp = CreatePen(PS_SOLID, 1, RGB(0xB6, 0xBF, 0xCC));
    HGDIOBJ op1 = SelectObject(ds->hDC, pp);
    HGDIOBJ op2 = SelectObject(ds->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(ds->hDC, r.left, r.top, r.right, r.bottom);
    SelectObject(ds->hDC, op2);
    SelectObject(ds->hDC, op1);
    DeleteObject(pp);
    SetBkMode(ds->hDC, TRANSPARENT);
    SetTextColor(ds->hDC, RGB(0x4A, 0x5A, 0x74));
    wchar_t t[8] = L"";
    GetWindowTextW((HWND)ds->hwndItem, t, 8);
    HFONT of = (HFONT)SelectObject(ds->hDC, g_fontUI);
    DrawTextW(ds->hDC, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(ds->hDC, of);
}

// ---------------- 会话管理 ----------------
static void switchSession(Session* t);
static void connectSession(Session* s);
static void newSessionDialog();
static void syncMenuChecks();
static void sessionNameOf(Session* s);
static void lineReset();
static void updateStatus();
static void viewSaveRtf(std::string& out);
static void viewLoadRtf(const std::string& in);
static void layout(HWND hwnd);
static void startSsh(Session* s);
static void stopSsh(Session* s);

static Session* sessionById(int id) {
    for (auto& s : g_sess) if (s->id == id) return s.get();
    return nullptr;
}

// 侧栏显示名：串口名 / 远端 IP:串口 / 主机
static void sessionNameOf(Session* s) {
    if (!s) return;
    if (s->type == CONN_REMOTE)       s->name = s->remIp + L":" + s->remCom;
    else if (s->type == CONN_SSH)     s->name = s->host.empty() ? s->remIp : s->host;
    else if (s->type == CONN_TELNET)  s->name = s->host.empty() ? s->remIp : s->host;
    else                              s->name = s->port;
}

// ---- 终端画面存取（RTF，保留颜色）----
static DWORD CALLBACK rtfOutCb(DWORD_PTR cookie, LPBYTE pb, LONG cb, LONG* pcb) {
    ((std::string*)cookie)->append((const char*)pb, (size_t)cb);
    *pcb = cb;
    return 0;
}
static DWORD CALLBACK rtfInCb(DWORD_PTR cookie, LPBYTE pb, LONG cb, LONG* pcb) {
    std::string* s = (std::string*)cookie;
    size_t n = s->size() < (size_t)cb ? s->size() : (size_t)cb;
    if (n) memcpy(pb, s->data(), n);
    s->erase(0, n);
    *pcb = (LONG)n;
    return 0;
}
static void viewSaveRtf(std::string& out) {
    out.clear();
    if (!g_hView) return;
    if (!g_termReady) {                       // 普通 EDIT：退化成纯文本
        out = wideToUtf8(viewText());
        return;
    }
    EDITSTREAM es{};
    es.dwCookie = (DWORD_PTR)&out;
    es.pfnCallback = rtfOutCb;
    SendMessageW(g_hView, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
}
static void viewLoadRtf(const std::string& in) {
    if (!g_hView || in.empty()) return;
    g_selfEdit = true;
    if (!g_termReady) {
        SetWindowTextW(g_hView, utf8ToWide(in).c_str());
    } else {
        std::string buf = in;
        EDITSTREAM es{};
        es.dwCookie = (DWORD_PTR)&buf;
        es.pfnCallback = rtfInCb;
        SendMessageW(g_hView, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    }
    g_selfEdit = false;
}

// 把某个会话的画面装进视图（含未提交行状态）
static void applySessionView(Session* t) {
    if (t && t->type == CONN_SSH) {
        // SSH 会话：画面在独立的终端控件里，切过来只要把它显示出来
        { std::lock_guard<std::mutex> lk(g_textMtx); g_allText = t->allText; }
        if (g_hwnd) layout(g_hwnd);
        return;
    }
    termClearAll();
    viewLoadRtf(t->rtf);
    { std::lock_guard<std::mutex> lk(g_textMtx); g_allText = t->allText; }
    // 光标落到画面末尾：后台补放的字节接在后面写，不去覆盖已画好的行
    // （跨会话接着改同一行的字节账本对不上，硬接会把提示符重画成 root@root@…）
    g_lb.clear();
    g_lc = 0;
    g_lshow.clear();
    g_lcp = caretToEnd();
    g_tsNeed = (t->lineShow.empty() && t->lineBuf.empty());   // 上一行已收尾才补时间戳
    lineReset();
    // 整屏换掉不是用户输入：刷新「外来文本」基准，并在一小段时间内直接忽略外来文本判定，
    // 否则整屏恢复会被当成 IME 上屏，整段转发到串口
    KillTimer(g_hwnd, IDT_IME);
    g_selfEdit = true;
    g_expect = viewText();
    g_selfEdit = false;
    g_foreignGuard = GetTickCount() + 800;
    // 从 SSH 终端切回串口会话时，必须重排一次：把别的会话的终端控件收起来、
    // 让 RichEdit 重新显示，否则 SSH 终端窗口一直盖在上面，看起来像「没切换」
    if (g_hwnd) layout(g_hwnd);
    termEnsureCaret();                     // 切过去后插入点落在最后一行行尾
}

// 切换会话：先把当前画面存进旧会话，再把目标会话的画面装载进来
static void switchSession(Session* t) {
    if (!t || t == g_cur || !g_hView) return;
    if (g_cur && g_cur->type != CONN_SSH) {   // SSH 画面在终端控件里，别拿 RichEdit 覆盖它
        viewSaveRtf(g_cur->rtf);
        { std::lock_guard<std::mutex> lk(g_textMtx); g_cur->allText = g_allText; }
        g_cur->lineBuf  = g_lb;
        g_cur->lineCur  = g_lc;
        g_cur->lineShow = g_lshow;
        g_cur->tsNeed   = g_tsNeed;
    }
    g_cur = t;
    applySessionView(t);

    // 后台期间收到的数据，切回来时按原样补上
    std::string pend;
    { std::lock_guard<std::mutex> lk(t->mtx); pend.swap(t->pending); }
    if (!pend.empty()) showRx(pend);

    updateStatus();
    syncMenuChecks();
    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
    // 焦点跟着画面走：SSH 喂终端控件，串口喂 RichEdit，否则切过去敲键盘没人接
    if (t->type == CONN_SSH && t->term) t->term->focus();
    else if (g_hView) SetFocus(g_hView);
}

// 左侧会话列表：每行一个会话（圆点 + 名字 + 状态），底部一行「新建会话」
static const int kSessRowH = 26;
static const int kSessGap  = 2;
static const int kSessPad  = 6;
static const int kSessNewH = 26;
static int g_sideHover  = -1;
static int g_sideScroll = 0;
static Session* g_ctxSess = nullptr;      // 右键菜单作用的会话

static RECT sideRowRect(const RECT& rc, int i) {
    int y = rc.top + kSessPad + i * (kSessRowH + kSessGap) - g_sideScroll;
    return RECT{rc.left + 4, y, rc.right - kHandleW - 2, y + kSessRowH};
}
static RECT sideNewRect(const RECT& rc) {
    return RECT{rc.left + 4, rc.bottom - kSessNewH - 4, rc.right - kHandleW - 2, rc.bottom - 4};
}
// 命中：>=0 会话下标；-2 新建会话；-1 空白
static int sideHit(int y) {
    if (!g_hSide) return -1;
    RECT rc;
    GetClientRect(g_hSide, &rc);
    RECT nb = sideNewRect(rc);
    if (y >= nb.top && y < nb.bottom) return -2;
    for (int i = 0; i < (int)g_sess.size(); i++) {
        RECT r = sideRowRect(rc, i);
        if (y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

// 侧栏整块重画不便宜（擦背景 + 逐个会话量文字画圆点），而 updateStatus 每收到
// 一段串口数据就会调它一次。会话列表、名字、连线状态都没变时，没必要重画。
static std::wstring g_sideSig;
static bool         g_sideSigOn = false;

static void updateSide() {
    if (!g_hSide || !g_hSideBtn) return;
    SetWindowTextW(g_hSideBtn, g_sideOn ? L"«" : L"»");
    std::wstring sig;
    sig.reserve(g_sess.size() * 12);
    for (auto& s : g_sess) {
        sig += (s->on ? L'1' : L'0');
        sig += std::to_wstring(s->id);
        sig += s->name;
        sig += L',';
    }
    if (g_cur) { sig += L'|'; sig += std::to_wstring(g_cur->id); }
    if (sig == g_sideSig && g_sideSigOn == g_sideOn) return;
    g_sideSig = sig;
    g_sideSigOn = g_sideOn;
    InvalidateRect(g_hSide, nullptr, TRUE);   // 自绘，重画即可
}

// 左侧栏自绘：白底细框 + 会话列表 + 底部新建会话
static void drawSideBar(const DRAWITEMSTRUCT* ds) {
    HDC dc = ds->hDC;
    RECT rc = ds->rcItem;
    HBRUSH bg = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    if (!g_sideOn || rc.right - rc.left < 40) return;

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xDC, 0xDC, 0xDC));
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);

    // 右边缘一条浅色轨道，折叠把手浮在上面（与右侧快捷栏对称）
    RECT rail{rc.right - kHandleW, rc.top + 1, rc.right - 1, rc.bottom - 1};
    HBRUSH rb = CreateSolidBrush(RGB(0xF8, 0xF9, 0xFB));
    FillRect(dc, &rail, rb);
    DeleteObject(rb);

    SetBkMode(dc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(dc, g_fontUI);

    RECT nb = sideNewRect(rc);
    int listBottom = nb.top - 4;

    for (int i = 0; i < (int)g_sess.size(); i++) {
        RECT r = sideRowRect(rc, i);
        if (r.bottom <= rc.top + 1 || r.top >= listBottom) continue;
        Session* ss = g_sess[i].get();
        bool act = (ss == g_cur);
        bool hv  = (i == g_sideHover);

        if (act || hv) {
            HBRUSH b = CreateSolidBrush(act ? RGB(0xE8, 0xF1, 0xFE) : RGB(0xF4, 0xF6, 0xF9));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        if (act) {
            RECT bar{r.left, r.top + 1, r.left + 3, r.bottom - 1};
            HBRUSH b = CreateSolidBrush(RGB(0x2E, 0x7C, 0xE0));
            FillRect(dc, &bar, b);
            DeleteObject(b);
        }

        // 状态圆点
        COLORREF c = ss->on ? RGB(0x2E, 0xA0, 0x43) : RGB(0xD8, 0x3B, 0x3B);
        int d = 8, cx = r.left + 8, cy = r.top + (r.bottom - r.top - d) / 2;
        HBRUSH db = CreateSolidBrush(c);
        HGDIOBJ ob2 = SelectObject(dc, db);
        HGDIOBJ op2 = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx, cy, cx + d, cy + d);
        SelectObject(dc, op2);
        SelectObject(dc, ob2);
        DeleteObject(db);

        // 名字 + 状态：名字量宽后紧接状态；放不下就把状态贴右边、名字省略
        int tx = cx + d + 6;
        int lim = r.right - 4;
        const wchar_t* stx = ss->on ? L"已连接" : L"未连接";
        SIZE sn{0, 0}, ss2{0, 0};
        GetTextExtentPoint32W(dc, ss->name.c_str(), (LONG)ss->name.size(), &sn);
        GetTextExtentPoint32W(dc, stx, (LONG)wcslen(stx), &ss2);
        if (tx + sn.cx + 5 + ss2.cx <= lim) {
            RECT r1{tx, r.top, lim, r.bottom};
            SetTextColor(dc, act ? RGB(0x14, 0x3A, 0x7A) : RGB(0x28, 0x28, 0x28));
            DrawTextW(dc, ss->name.c_str(), -1, &r1,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            RECT r2{tx + sn.cx + 5, r.top, lim, r.bottom};
            SetTextColor(dc, c);
            DrawTextW(dc, stx, -1, &r2,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        } else {
            RECT r2{lim - ss2.cx, r.top, lim, r.bottom};
            SetTextColor(dc, c);
            DrawTextW(dc, stx, -1, &r2,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            RECT r1{tx, r.top, r2.left - 4, r.bottom};
            SetTextColor(dc, act ? RGB(0x14, 0x3A, 0x7A) : RGB(0x28, 0x28, 0x28));
            DrawTextW(dc, ss->name.c_str(), -1, &r1,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }

    // 底部分隔线 + 新建会话
    {
        HPEN lp = CreatePen(PS_SOLID, 1, RGB(0xE6, 0xE6, 0xE6));
        HGDIOBJ o = SelectObject(dc, lp);
        MoveToEx(dc, rc.left + 4, nb.top - 5, nullptr);
        LineTo(dc, rc.right - kHandleW - 2, nb.top - 5);
        SelectObject(dc, o);
        DeleteObject(lp);

        if (g_sideHover == -2) {
            HBRUSH b = CreateSolidBrush(RGB(0xE8, 0xF1, 0xFE));
            FillRect(dc, &nb, b);
            DeleteObject(b);
        }
        SetTextColor(dc, RGB(0x2E, 0x7C, 0xE0));
        DrawTextW(dc, L"＋ 新建会话", -1, &nb,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    SelectObject(dc, of);
}

// 左侧栏右键菜单：按命中的会话给出操作
static void showSideMenu(POINT pt) {
    if (!g_hSide || !g_sideOn) return;
    POINT cp = pt;
    ScreenToClient(g_hSide, &cp);
    int hit = sideHit(cp.y);
    g_ctxSess = (hit >= 0) ? g_sess[hit].get() : g_cur;
    Session* s = g_ctxSess;

    HMENU m = CreatePopupMenu();
    if (hit == -2 || hit < 0) {                       // 空白 / 新建按钮
        mAddMenu(m, MF_STRING, IDM_PORT_OPEN, L"新建会话…\tCtrl+N");
        if (!g_sess.empty()) {
            mAddMenu(m, MF_SEPARATOR, 0, nullptr);
            mAddMenu(m, MF_STRING | (s && s->on ? 0 : MF_GRAYED), IDM_SESS_CLOSE, L"关闭当前会话");
        }
    } else {
        HMENU mb = CreatePopupMenu();
        bool isSsh = s && s->type == CONN_SSH;
        if (!isSsh) {
            for (int i = 0; i < kBaudCount; i++) {
                wchar_t t[32];
                swprintf_s(t, L"%d", kBaits[i]);
                mAddMenu(mb, MF_STRING | (s && s->baud == kBaits[i] ? MF_CHECKED : 0),
                         IDM_BAUD_BASE + i, t);
            }
            mAddMenu(m, MF_POPUP, (UINT_PTR)mb, L"波特率");
            mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        } else {
            DestroyMenu(mb);
        }
        if (isSsh) {
            if (s->on) {
                mAddMenu(m, MF_STRING, IDM_PORT_CLOSE, L"断开 SSH");
            } else {
                std::wstring ct = L"重新连接 " + (s->sshInfo.empty() ? s->name : s->sshInfo);
                mAddMenu(m, MF_STRING, IDM_SIDE_CONN, ct.c_str());
            }
            mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        } else if (s && s->on) {
            mAddMenu(m, MF_STRING, IDM_PORT_CLOSE, L"断开串口");
        } else {
            wchar_t ct[160];
            swprintf_s(ct, L"连接串口 (%ls)", s ? s->name.c_str() : L"");
            mAddMenu(m, MF_STRING, IDM_SIDE_CONN, ct);
        }
        mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        mAddMenu(m, MF_STRING, IDM_PORT_OPEN, L"新建会话…\tCtrl+N");
        mAddMenu(m, MF_STRING, IDM_SESS_CLOSE, L"关闭此会话");
    }

    SetForegroundWindow(g_hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                  pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);                     // 子菜单会一并销毁
    if (cmd) SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)cmd, 0);
    g_ctxSess = nullptr;                // 只在本次菜单内有效，避免菜单栏命令误用
}

// 左侧栏子类化：左键切换会话 / 右键菜单
static LRESULT CALLBACK SideProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
        case WM_LBUTTONUP: {
            int hit = sideHit((short)HIWORD(l));
            if (hit >= 0) switchSession(g_sess[hit].get());
            else if (hit == -2) SendMessageW(g_hwnd, WM_COMMAND, IDM_PORT_OPEN, 0);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int hit = sideHit((short)HIWORD(l));
            if (hit != g_sideHover) { g_sideHover = hit; InvalidateRect(h, nullptr, TRUE); }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int row = (int)g_sess.size() * (kSessRowH + kSessGap);
            int max = row - (int)0;
            RECT rc; GetClientRect(h, &rc);
            max = row - (rc.bottom - 2 * kSessPad - kSessNewH - 8);
            if (max < 0) max = 0;
            int delta = GET_WHEEL_DELTA_WPARAM(w);
            g_sideScroll -= (delta / WHEEL_DELTA) * (kSessRowH + kSessGap);
            if (g_sideScroll > max) g_sideScroll = max;
            if (g_sideScroll < 0) g_sideScroll = 0;
            InvalidateRect(h, nullptr, TRUE);
            return 0;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT pt;
            if (m == WM_CONTEXTMENU && l != (LPARAM)-1) {
                pt.x = (short)LOWORD(l);
                pt.y = (short)HIWORD(l);
            } else GetCursorPos(&pt);
            showSideMenu(pt);
            return 0;
        }
    }
    return DefSubclassProc(h, m, w, l);
}

// 状态栏在「打字 + 回显 + 命令输出」时会被打到几十次每秒，每次都要
// SetWindowText + 重画侧栏，白白拖慢按键响应。这里 100ms 合并一次，
// 尾巴用一次性定时器补刷，所以数字最终仍然准确。
static DWORD g_stTick  = 0;
static bool  g_stDirty = false;

static void updateStatus() {
    DWORD now = GetTickCount();
    if (now - g_stTick < 100) {
        if (!g_stDirty) { g_stDirty = true; SetTimer(g_hwnd, IDT_STATUS, 120, nullptr); }
        return;
    }
    g_stTick  = now;
    g_stDirty = false;
    wchar_t buf[512];
    long long rx = g_cur ? g_cur->rx : 0, tx = g_cur ? g_cur->tx : 0;
    if (g_cur && g_cur->type == CONN_SSH) {
        bool live = (g_cur->ssh && g_cur->ssh->alive());
        swprintf_s(buf, L"%ls %ls | 会话 %d | RX %lld B  TX %lld B | 端口 %d x %d",
                   live ? L"已连接" : L"未连接",
                   g_cur->sshInfo.empty() ? g_cur->host.c_str() : g_cur->sshInfo.c_str(),
                   (int)g_sess.size(), rx, tx,
                   g_cur->term ? g_cur->term->cols() : 0, g_cur->term ? g_cur->term->rows() : 0);
        SetWindowTextW(g_hStatus, buf);
        updateSide();
        return;
    }
    if (curOn()) {
        // 有 AI 连着时把话说白：串口是独占的，这时本地敲键盘会被接管命令打断
        if ((int)g_peerCount > 0)
            swprintf_s(buf, L"已连接 %ls @%d | %ls | 会话 %d | AI 接管中(%d)，本地输入仍可用但会被注入打断 | RX %lld B  TX %lld B",
                       g_cur->name.c_str(), g_cur->baud, encName(), (int)g_sess.size(),
                       (int)g_peerCount, rx, tx);
        else
            swprintf_s(buf, L"已连接 %ls @%d | %ls | 会话 %d | 接管 :7000(0) | RX %lld B  TX %lld B",
                       g_cur->name.c_str(), g_cur->baud, encName(), (int)g_sess.size(), rx, tx);
    } else {
        swprintf_s(buf, L"未连接 %ls | 会话 %d | RX %lld B  TX %lld B | 会话菜单 → 新建会话 (Ctrl+N)",
                   g_cur ? g_cur->name.c_str() : L"", (int)g_sess.size(), rx, tx);
    }
    SetWindowTextW(g_hStatus, buf);
    updateSide();
}

// ---------------- 写字工具 ----------------
static bool serialWrite(const std::string& bytes) {
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
struct Peer {
    SOCKET s;
    std::mutex mtx;
    std::atomic<bool> alive{true};
    // 它最后写进串口的字节是不是停在半行上（没带 \r/\n）。
    // 断开时要替它收尾，否则板子会一直等你把那半条命令敲完——
    // 表现就是"AI 断开后我敲字没反应/命令对不上"。
    std::atomic<bool> openLine{false};
    explicit Peer(SOCKET so) : s(so) {}
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

static std::vector<std::shared_ptr<Peer>> g_peers;
static std::mutex g_peersMtx;

// 接管服务真正能写的串口：优先 g_hubSess，退化到第一个本地串口会话。
// 千万不能用 g_cur —— 本机自环测试时当前会话就是「远程串口」客户端，
// 写 g_cur 等于把数据发回它自己的 socket，会自己跟自己打乒乓。
static Session* hubPort() {
    if (g_hubSess && g_hubSess->on && !g_hubSess->isRemote &&
        g_hubSess->type == CONN_SERIAL && g_hubSess->ser.isOpen())
        return g_hubSess;
    for (auto& sp : g_sess) {
        Session* q = sp.get();
        if (q->on && !q->isRemote && q->type == CONN_SERIAL && q->ser.isOpen()) return q;
    }
    return nullptr;
}

static void broadcastLine(const std::string& line, Peer* skip) {
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

static void postToGui(UINT msg, void* payload) {
    if (g_hwnd) PostMessageW(g_hwnd, msg, 0, (LPARAM)payload);
}

static void postToGuiId(UINT msg, WPARAM id, void* payload) {
    if (g_hwnd) PostMessageW(g_hwnd, msg, id, (LPARAM)payload);
}

static void postText(UINT msg, const std::wstring& s) {
    postToGui(msg, new std::wstring(s));
}

static void peerThread(std::shared_ptr<Peer> p) {
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

static void serverLoop() {
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

static void startServer() {
    if (g_srvRun) return;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_srv == INVALID_SOCKET) return;
    g_srvRun = true;
    g_srvThread = std::thread(serverLoop);
}

static void stopServer() {
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
static void postSessionData(Session* s, const std::string& bytes) {
    if (!g_hwnd) return;
    auto* heap = new std::string(bytes);
    PostMessageW(g_hwnd, WM_APP_DATA, (WPARAM)s->id, (LPARAM)heap);
}

static void readerProc(Session* s) {
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

static void openPort(Session* s) {
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
static bool openRemote(Session* s) {
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

static void closePort(Session* s = nullptr) {
    if (!s) s = g_cur;
    if (!s || !s->on) return;
    if (s->type == CONN_SSH) {
        stopSsh(s);
        if (s == g_cur) {
            appendRun(L"— SSH 连接已断开 —\r\n", C_SYS);
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
struct SshLog {
    std::wstring text;
    int level;
};

static void startSsh(Session* s) {
    if (!s || s->type != CONN_SSH || !g_hwnd) return;
    stopSsh(s);
    {   // 串口路径里才会初始化 winsock，SSH 自己也要确保一次
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    if (!s->term) {
        s->term.reset(new Terminal(g_hwnd, [s](const std::string& b) {
            if (s->ssh) {
                s->ssh->write(b);
                s->tx += (long long)b.size();
            }
        }));
        s->term->setFontPt(g_fontPt);
    }
    s->term->onResize = [s](int c, int r) {          // 窗口变化时同步远端 pty 大小
        if (s->ssh) s->ssh->resize(c, r);
    };
    if (s->sshInfo.empty())
        s->sshInfo = s->sshUser + L"@" + s->host + L":" + std::to_wstring(s->hostPort);
    if (s == g_cur) {
        layout(g_hwnd);                 // 先把终端摆好，拿到真实行列数再协商 pty
        s->term->showWindow(true);
        ShowWindow(g_hView, SW_HIDE);
        s->term->focus();
    }
    int cols = s->term->cols(), rows = s->term->rows();
    if (cols < 20) cols = 80;
    if (rows < 5) rows = 24;

    s->ssh.reset(new SshClient());
    int id = s->id;
    s->ssh->onData = [id](const std::string& d) {
        PostMessageW(g_hwnd, WM_APP_SSH_DATA, (WPARAM)id, (LPARAM)new std::string(d));
    };
    s->ssh->onLog = [id](const std::wstring& t, int lv) {
        PostMessageW(g_hwnd, WM_APP_SSH_LOG, (WPARAM)id, (LPARAM)new SshLog{t, lv});
    };
    s->ssh->onClosed = [id](const std::wstring& why) {
        PostMessageW(g_hwnd, WM_APP_SSH_END, (WPARAM)id, (LPARAM)new std::wstring(why));
    };
    s->on = true;
    s->term->writeText(L"正在连接 " + s->sshInfo + L" …\r\n");
    s->ssh->start(wideToUtf8(s->host), s->hostPort, wideToUtf8(s->sshUser),
                  wideToUtf8(s->sshPass), cols, rows);
    updateStatus();
}

static void stopSsh(Session* s) {
    if (!s) return;
    if (s->ssh) {
        s->ssh->stop();
        s->ssh.reset();
    }
    s->on = false;
}

// ---------------- 终端输入 ----------------
static void termSendBytes(const std::string& bytes, COLORREF color, const wchar_t* tag) {
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
static void lineReset() { g_line.clear(); g_lineLen.clear(); g_hiSur = 0; g_pendingKeys = 0; }

static void lineRecord(const std::wstring& s) {   // 记录一串已发出的字符（含代理对合并）
    size_t i = 0;
    while (i < s.size()) {
        std::wstring unit(1, s[i]);
        if ((unsigned)s[i] >= 0xD800 && (unsigned)s[i] <= 0xDBFF && i + 1 < s.size() &&
            (unsigned)s[i + 1] >= 0xDC00 && (unsigned)s[i + 1] <= 0xDFFF) {
            unit.push_back(s[i + 1]);
            i++;
        }
        std::string b = txEncode(unit);
        if (b.empty()) { i++; continue; }
        g_line += unit;
        g_lineLen.push_back((unsigned char)b.size());
        if (unit.size() == 2) g_lineLen.push_back((unsigned char)0);   // 低半代理不占字节
        g_pendingKeys++;                                               // 未提交的字符 +1
        i++;
    }
}

// 弹掉最后一个字符，返回它发出去时的字节数（0 表示本地没有记录）
static int linePop() {
    if (g_line.empty()) return 0;
    int pend = g_pendingKeys.load();
    if (pend > 0) g_pendingKeys = pend - 1;
    while (!g_lineLen.empty()) {
        unsigned char n = g_lineLen.back();
        g_lineLen.pop_back();
        g_line.pop_back();
        if (n) return (int)n;
    }
    return 0;
}

// 敲一个字符（不本地回显，靠板子回显）
static void termTypeChar(wchar_t ch) {
    if (ch == L'\r') {
        termSendBytes("\r", C_TX, nullptr);
        if (g_hiSur) { lineRecord(std::wstring(1, g_hiSur)); g_hiSur = 0; }   // 落单的代理对
        if (!g_line.empty()) {
            if (g_hist.empty() || g_hist.back() != g_line) g_hist.push_back(g_line);
            if (g_hist.size() > 200) g_hist.erase(g_hist.begin());
        }
        lineReset();
        g_histPos = -1;
        return;
    }
    if (ch == 8) {                       // Backspace：按本地记录精确算字节数，一次发够
        int n = linePop();               // 汉字按实际编码长度回退（UTF-8 3 字节 / GBK 2 字节）
        bool local = (n > 0);            // 这个字是我们自己敲进来的（本地有记录）
        if (!local) n = trailingCharBytes();   // AI 下发的内容本地没记录，按回显行算
        if (n < 1) n = 1;
        termSendBytes(std::string((size_t)n, '\x7f'), C_TX, nullptr);
        // 本地立刻删掉，别等板子回显——否则连按退格时看着像"没反应"
        if (local && g_lc >= (size_t)n && g_lb.size() >= (size_t)n) {
            g_lb.erase(g_lc - n, n);
            g_lc -= n;
            g_bsPend += n;                 // 板子随后会回 n 组 "\b \b"，吞掉其中 n 个 0x08
            termSyncLine();
        }
        return;
    }
    if (ch == 3) {                       // Ctrl+C
        termSendBytes("\x03", C_TX, nullptr);
        lineReset();
        return;
    }
    if (ch == 9) {                       // Tab
        termSendBytes("\t", C_TX, nullptr);
        g_line.push_back(L'\t');
        g_lineLen.push_back(1);
        g_pendingKeys++;
        return;
    }
    if (ch < 32) return;
    // 代理对（emoji 等）要凑齐两半再编码，分开编码会变成两个替换符
    if ((unsigned)ch >= 0xD800 && (unsigned)ch <= 0xDBFF) { g_hiSur = ch; return; }
    std::wstring unit;
    if (g_hiSur && (unsigned)ch >= 0xDC00 && (unsigned)ch <= 0xDFFF) {
        unit.push_back(g_hiSur);
        g_hiSur = 0;
    }
    unit.push_back(ch);
    std::string b = txEncode(unit);
    if (b.empty()) return;
    termLocalEcho(b);                    // 先画到屏幕上，键盘手感不再受链路延迟影响
    termSendBytes(b, C_TX, nullptr);
    lineRecord(unit);
}

// ↑ / ↓ 调历史：先用退格抹掉当前行，再把历史命令发出去（板子会回显）
static void termHistory(int dir) {
    if (g_hist.empty()) return;
    if (g_histPos == -1) {
        g_histTmp = g_line;
        g_histPos = (int)g_hist.size();
    }
    int np = g_histPos + dir;
    if (np < 0) np = 0;
    if (np > (int)g_hist.size()) np = (int)g_hist.size();
    g_histPos = np;

    std::wstring target = (np >= (int)g_hist.size()) ? g_histTmp : g_hist[np];
    // 按**字节**数发退格：中文 UTF-8 一个字 3 字节 / GBK 2 字节，行编辑按字节删
    if (!g_line.empty()) {
        std::string cur = txEncode(g_line);
        termSendBytes(std::string(cur.size(), '\x7f'), C_TX, nullptr);
    }
    if (!target.empty()) termSendBytes(txEncode(target), C_TX, nullptr);
    lineReset();
    lineRecord(target);
}

// 取输入法组合/上屏字符串（拼音, GCS_RESULTSTR = 最终上屏的汉字）
static std::wstring immStr(HWND h, DWORD flag) {
    std::wstring s;
    HIMC himc = ImmGetContext(h);
    if (!himc) return s;
    LONG n = ImmGetCompositionStringW(himc, flag, nullptr, 0);
    if (n > 0) {
        std::wstring buf((size_t)(n / 2) + 2, L'\0');
        LONG got = ImmGetCompositionStringW(himc, flag, buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)));
        if (got > 0) s.assign(buf.data(), (size_t)(got / 2));
    }
    ImmReleaseContext(h, himc);
    return s;
}

// 输出框子类化：直接把击键转成串口字节
// 注意：Enter/Backspace/Tab 会由 TranslateMessage 另外生成一个 WM_CHAR，
// 若不吞掉就会双发（表现为一次回车跑两条命令）。用 g_swallow 精确吞掉那一个。
static LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
        case WM_CHAR:
        case WM_IME_CHAR: {   // 输入法（即使处于英文/半角模式）送字走 WM_IME_CHAR，
                              // 不拦的话字符会被 RichEdit 插进显示区：屏上有字、串口没发出
            wchar_t ch = (wchar_t)w;
            if (ch >= 0x80) dbgLog(L"WM_%sCHAR ch=%04X echo=%d", m == WM_IME_CHAR ? L"IME_" : L"", ch, (int)!g_imeEcho.empty());
            if (!g_imeEcho.empty()) {                   // 上屏内容已转发，这里是重复投递
                if (ch == g_imeEcho.front()) { g_imeEcho.erase(0, 1); return 0; }
                g_imeEcho.clear();                      // 内容对不上，说明是新输入
            }
            if (g_swallow && ch == g_swallow) { g_swallow = 0; return 0; }
            termTypeChar(ch);
            return 0;
        }

        case WM_IME_STARTCOMPOSITION:
            // 拼音过程中 RichEdit 会把拼写串内联插进来（el. "nihao"）。
            // 上屏时这块会被替换成汉字，必须整段撤掉，否则屏上留下幻影字：
            // 看着有字、串口没发过，退格自然删不掉。这里先记住起点。
            g_imeBase = viewTextLen(h);
            dbgLog(L"IME_STARTCOMPOSITION base=%ld", g_imeBase);
            break;

        case WM_IME_COMPOSITION:
            dbgLog(L"IME_COMPOSITION l=%08X result=%d", (unsigned)l, (l & GCS_RESULTSTR) ? 1 : 0);
            if (l & GCS_RESULTSTR) {
                // 上屏结果以输入法给的为准（不看控件相差了几个字：
                // 组合串"nihao"→结果"你好"，控件长度是变短的，按增量算会漏）
                std::wstring res = immStr(h, GCS_RESULTSTR);
                g_selfEdit = true;                          // 组合串/结果的内联改动算我们自己的
                LRESULT r = DefSubclassProc(h, m, w, l);   // 让 RichEdit 完成 组合串→结果 的替换
                g_selfEdit = false;

                long after = viewTextLen(h);
                CHARRANGE cr{};
                SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
                long end = (cr.cpMax > 0 && cr.cpMax <= after) ? cr.cpMax : after;

                // 结果串的落点：优先光标前 res 长度的位置，其次整个组合区
                long cand[2] = { end - (long)res.size(), g_imeBase >= 0 ? g_imeBase : -1 };
                long start = -1;
                if (!res.empty()) {
                    for (long c : cand) {
                        if (c >= 0 && c + (long)res.size() <= after && viewRange(h, c, c + (long)res.size()) == res) {
                            start = c;
                            break;
                        }
                    }
                    if (start < 0 && cand[0] >= 0 && cand[0] + (long)res.size() <= after) start = cand[0];
                }
                if (start >= 0) {
                    viewDeleteRange(h, start, start + (long)res.size());   // 撤掉本地插入
                    for (wchar_t ch : res) termTypeChar(ch);               // 真正发给串口
                    g_imeEcho = res;                                       // 吞掉可能的重复投递
                }
                dbgLog(L"IME_RESULT res=[%ls] start=%ld", res.c_str(), start);
                g_imeBase = -1;
                termColorReset();      // 输入法写进文档的字体不能被后面的输出继承
                return r;
            }
            break;

        case WM_IME_ENDCOMPOSITION:
            dbgLog(L"IME_ENDCOMPOSITION");
            g_imeEcho.clear();
            g_imeBase = -1;
            break;

        case WM_KEYDOWN:
            switch (w) {
                case VK_RETURN: g_swallow = L'\r'; termTypeChar(L'\r'); return 0;
                case VK_BACK:   g_swallow = (wchar_t)8; termTypeChar((wchar_t)8); return 0;
                case VK_TAB:    g_swallow = (wchar_t)9; termTypeChar((wchar_t)9); return 0;
                case VK_UP:     termHistory(-1); return 0;
                case VK_DOWN:   termHistory(1); return 0;
                case VK_ESCAPE: return 0;
                case VK_DELETE: return 0;
                case 'C':
                    if (GetKeyState(VK_CONTROL) & 0x8000) {
                        g_swallow = (wchar_t)3;
                        if (GetKeyState(VK_SHIFT) & 0x8000) {
                            CHARRANGE cr{};
                            SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
                            if (cr.cpMax > cr.cpMin) SendMessageW(h, WM_COPY, 0, 0);
                            g_swallow = 0;
                        } else {
                            termTypeChar((wchar_t)3);   // 如同真终端：Ctrl+C 中断
                        }
                        return 0;
                    }
                    break;
                case 'V':
                    if (GetKeyState(VK_CONTROL) & 0x8000) {
                        if (OpenClipboard(h)) {
                            HANDLE hd = GetClipboardData(CF_UNICODETEXT);
                            if (hd) {
                                auto* txt = (wchar_t*)GlobalLock(hd);
                                if (txt) {
                                    std::wstring s(txt);
                                    GlobalUnlock(hd);
                                    for (auto& ch : s) if (ch == L'\r' || ch == L'\n') ch = L' ';
                                    termSendBytes(txEncode(s), C_TX, nullptr);
                                    lineRecord(s);
                                }
                            }
                            CloseClipboard();
                        }
                        g_swallow = (wchar_t)22;
                        return 0;
                    }
                    break;
                case 'A':
                    if (GetKeyState(VK_CONTROL) & 0x8000) {
                        SendMessageW(h, EM_SETSEL, 0, -1);
                        return 0;
                    }
                    break;
            }
            // 其余 Ctrl+字母 原样透传给板子（Ctrl+U 清行、Ctrl+W 删词、Ctrl+L 清屏、
            // Ctrl+R 搜索历史等），此前这些键全被丢掉，像真终端那样发控制码
            if (w >= 'A' && w <= 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                char code = (char)(w - 'A' + 1);
                g_swallow = (wchar_t)(unsigned char)code;
                if (code == 21) lineReset();         // Ctrl+U：板子行已清空
                termSendBytes(std::string(1, code), C_TX, nullptr);
                return 0;
            }
            break;

        case WM_CONTEXTMENU:
            showViewMenu(h);          // 右键：复制/粘贴/全选/清屏 + 编码、时间戳、日志等功能设置
            return 0;

        case WM_SETFOCUS: {
            // 切回来时插入点可能还停在别处，先让 RichEdit 处理完再归位
            LRESULT r = DefSubclassProc(h, m, w, l);
            termEnsureCaret();
            return r;
        }

        case WM_LBUTTONUP: {
            LRESULT r = DefSubclassProc(h, m, w, l);
            // 单击（没有拖出选区）只会在 RichEdit 里挪走插入点，终端里没意义，退回行尾；
            // 拖选/双击选词保持不动，那是为了复制
            termEnsureCaret();
            return r;
        }

        case WM_PASTE: case WM_CUT: case WM_CLEAR: case WM_UNDO:
            return 0;
    }
    g_selfEdit = true;      // 默认处理里可能发生的文本改动（如传统 IMM 组合串）不算外来
    LRESULT r = DefSubclassProc(h, m, w, l);
    g_selfEdit = false;
    return r;
}

// 输入法是否正处于拼写组合中（TSF 搜狗/微软拼音 + 传统 IMM 都查）
static bool imeComposing() {
    if (g_hView) {
        HIMC himc = ImmGetContext(g_hView);
        if (himc) {
            LONG n = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
            ImmReleaseContext(g_hView, himc);
            if (n > 0) return true;
        }
    }
    static ITfThreadMgr* s_tsf = nullptr;
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&s_tsf));
    }
    if (!s_tsf) return false;
    ITfDocumentMgr* dm = nullptr;
    if (FAILED(s_tsf->GetFocus(&dm)) || !dm) return false;
    ITfContext* ctx = nullptr;
    bool composing = false;
    if (SUCCEEDED(dm->GetTop(&ctx)) && ctx) {
        ITfContextComposition* cc = nullptr;
        if (SUCCEEDED(ctx->QueryInterface(IID_ITfContextComposition, (void**)&cc)) && cc) {
            IEnumITfCompositionView* en = nullptr;
            if (SUCCEEDED(cc->EnumCompositions(&en)) && en) {
                ITfCompositionView* v = nullptr;
                ULONG n = 0;
                if (en->Next(1, &v, &n) == S_OK && n > 0) { v->Release(); composing = true; }
                en->Release();
            }
            cc->Release();
        }
        ctx->Release();
    }
    dm->Release();
    return composing;
}

// EN_CHANGE 防抖到期：把控件里多出来的外来文本（TSF 上屏的汉字等）撤掉并转发给串口
static void syncForeignText() {
    if (!g_hView || g_selfEdit) return;
    if (GetTickCount() < g_foreignGuard) {     // 刚切换过会话：整屏变化不算输入
        g_expect = viewText();
        return;
    }
    std::wstring cur = viewText();
    if (cur == g_expect) return;
    size_t p = 0, n1 = cur.size(), n2 = g_expect.size();
    while (p < n1 && p < n2 && cur[p] == g_expect[p]) p++;
    size_t s = 0;
    while (s < n1 - p && s < n2 - p && cur[n1 - 1 - s] == g_expect[n2 - 1 - s]) s++;
    std::wstring ins = cur.substr(p, n1 - p - s);          // 外来插入的文本
    dbgLog(L"syncForeign p=%zu ins=[%ls]", p, ins.c_str());
    if (ins.size() > 4) {                                  // 整段变化是我们自己刷屏/补放，不是输入
        g_expect = cur;
        return;
    }
    if (n1 - s > p) viewDeleteRange(g_hView, (long)p, (long)(n1 - s));
    g_expect = viewText();
    if (!ins.empty()) {
        for (wchar_t ch : ins) termTypeChar(ch);           // 真正发给串口
        g_imeEcho = ins;                                   // 吞掉可能的重复投递
    }
    // 上面删字会把插入点塌在删除处（屏幕中间），这里退回行尾
    termEnsureCaret();
    // 被删掉的那段是输入法塞进来的，它的字体比 Consolas 大；
    // 删完插入点格式可能还留着那份大字体，强制下一笔重新压实格式
    termColorReset();
}

// ---------------- 字体 ----------------
static void applyFont() {
    if (!g_hView) return;
    for (auto& sp : g_sess)
        if (sp->term) sp->term->setFontPt(g_fontPt);
    if (g_termReady) {
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR;
        cf.yHeight = g_fontPt * 20;
        cf.crTextColor = C_RX;
        cf.bCharSet = DEFAULT_CHARSET;
        lstrcpynW(cf.szFaceName, L"Consolas", LF_FACESIZE);
        SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
        SendMessageW(g_hView, EM_SETBKGNDCOLOR, 0, (LPARAM)C_BG);
    } else {
        HDC dc = GetDC(g_hView);
        int hgt = -MulDiv(g_fontPt, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(g_hView, dc);
        HFONT f = CreateFontW(hgt, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH, L"Consolas");
        SendMessageW(g_hView, WM_SETFONT, (WPARAM)f, TRUE);
    }
}

// ---------------- 菜单 ----------------
static void syncMenuChecks() {
    if (!g_menuTool) return;
    CheckMenuItem(g_menuTool, IDM_PORT_DTR, MF_BYCOMMAND | (g_dtr ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menuTool, IDM_PORT_RTS, MF_BYCOMMAND | (g_rts ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menuTool, IDM_PORT_SERVE, MF_BYCOMMAND | (g_serve ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_VIEW_TS, MF_BYCOMMAND | (g_showTs ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_VIEW_AGENT, MF_BYCOMMAND | (g_showAgent ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menuTool, IDM_TOOL_STTY, MF_BYCOMMAND | (g_sttyAdapt ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(g_menu, IDM_ENC_BASE, IDM_ENC_BASE + 2, IDM_ENC_BASE + g_rxEnc, MF_BYCOMMAND);
    CheckMenuItem(g_menu, IDM_LOG_SAVE, MF_BYCOMMAND | (g_logSave ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_SIDE_TOGGLE, MF_BYCOMMAND | (g_sideOn ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_KEYS_TOGGLE, MF_BYCOMMAND | (g_keysOn ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(g_menuSess, IDM_SESSION_RECONN, MF_BYCOMMAND | (curOn() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(g_menuSess, IDM_PORT_CLOSE, MF_BYCOMMAND | (curOn() ? MF_ENABLED : MF_GRAYED));
    if (g_menuSet) {
        CheckMenuRadioItem(g_menuSet, IDM_SET_MFONT_BASE, IDM_SET_MFONT_BASE + kUiPtCount - 1,
                           IDM_SET_MFONT_BASE + g_menuPtIdx, MF_BYCOMMAND);
        CheckMenuRadioItem(g_menuSet, IDM_SET_DFONT_BASE, IDM_SET_DFONT_BASE + kUiPtCount - 1,
                           IDM_SET_DFONT_BASE + g_dlgPtIdx, MF_BYCOMMAND);
    }
}

// ---------------- 自绘菜单 ----------------
// 菜单项数据：文本（可含 \t 加速键）+ 是否子菜单
struct MenuItemData {
    std::wstring text;
    bool popup = false;
    bool top = false;        // 菜单栏顶层项（画法不同：无勾选列、无箭头）
};
static std::vector<MenuItemData*> g_menuItems;   // 进程生命周期，不释放

static void mAddMenu(HMENU m, UINT flags, UINT_PTR id, const wchar_t* text) {
    if (!text) { AppendMenuW(m, flags, id, nullptr); return; }
    MenuItemData* d = new MenuItemData;
    d->text = text;
    d->popup = (flags & MF_POPUP) != 0;
    d->top = (m == g_menu);
    g_menuItems.push_back(d);
    AppendMenuW(m, flags | MF_OWNERDRAW, id, (LPCWSTR)d);
}

static UINT menuStateIn(HMENU m, UINT_PTR id) {
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        UINT st = GetMenuState(m, i, MF_BYPOSITION);
        if (st == (UINT)-1) continue;
        if (st & MF_POPUP) {
            HMENU sub = GetSubMenu(m, i);
            if (sub) { UINT r = menuStateIn(sub, id); if (r != (UINT)-1) return r; }
        } else if ((UINT_PTR)GetMenuItemID(m, i) == id) {
            return st;
        }
    }
    return (UINT)-1;
}

static void makeMenuFont() {
    HDC dc = GetDC(g_hwnd);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    if (g_fontMenu) DeleteObject(g_fontMenu);
    g_fontMenu = CreateFontW(-MulDiv(kUiPts[g_menuPtIdx], dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g_hwnd, dc);
    g_menuRowH = tm.tmHeight + 8;
}

static void applyMenuStyle() {
    if (!g_menu) return;
    makeMenuFont();
    if (g_brushMenu) DeleteObject(g_brushMenu);
    g_brushMenu = CreateSolidBrush(g_menuBg);
    MENUINFO mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = g_brushMenu;
    SetMenuInfo(g_menu, &mi);
    if (g_hwnd) DrawMenuBar(g_hwnd);
}

static void drawMenuItem(const DRAWITEMSTRUCT* ds, const MenuItemData* d) {
    HDC dc = ds->hDC;
    RECT rc = ds->rcItem;
    bool sel = (ds->itemState & ODS_SELECTED) != 0;
    bool dis = (ds->itemState & ODS_GRAYED) != 0;

    HBRUSH bg = CreateSolidBrush(sel ? g_menuSel : g_menuBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(dc, g_fontMenu);
    SetTextColor(dc, dis ? RGB(0x9A, 0x9A, 0x9A) : (sel ? RGB(0xFF, 0xFF, 0xFF) : g_menuFg));

    // 勾选标记
    if (!d->popup && !d->top) {
        UINT st = g_menu ? menuStateIn(g_menu, ds->itemID) : (UINT)-1;
        if (st != (UINT)-1 && (st & MF_CHECKED)) {
            RECT rcc{rc.left + 7, rc.top, rc.left + 26, rc.bottom};
            DrawTextW(dc, L"\u2713", -1, &rcc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    // 文本 + 右侧加速键
    std::wstring main = d->text, acc;
    size_t tab = main.find(L'\t');
    if (tab != std::wstring::npos) { acc = main.substr(tab + 1); main = main.substr(0, tab); }
    int lx = d->top ? 10 : 27;
    RECT rt{rc.left + lx, rc.top, rc.right - (d->popup && !d->top ? 22 : 10), rc.bottom};
    DrawTextW(dc, main.c_str(), -1, &rt, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (!acc.empty()) {
        RECT ra{rc.left + lx, rc.top, rc.right - (d->popup && !d->top ? 22 : 12), rc.bottom};
        DrawTextW(dc, acc.c_str(), -1, &ra, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    // 子菜单箭头（顶层菜单栏项不画）
    if (d->popup && !d->top) {
        int cx = rc.right - 13, cy = (rc.top + rc.bottom) / 2;
        POINT p[3] = {{cx - 3, cy - 4}, {cx + 2, cy}, {cx - 3, cy + 4}};
        HBRUSH ab = CreateSolidBrush(dis ? RGB(0xAA, 0xAA, 0xAA)
                                         : (sel ? RGB(0xFF, 0xFF, 0xFF) : g_menuFg));
        HGDIOBJ ob = SelectObject(dc, ab);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, p, 3);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(ab);
    }
    SelectObject(dc, of);
}

static void measureMenuItem(MEASUREITEMSTRUCT* ms) {
    MenuItemData* d = (MenuItemData*)ms->itemData;
    if (!d) { ms->itemHeight = g_menuRowH; ms->itemWidth = 120; return; }
    std::wstring main = d->text, acc;
    size_t tab = main.find(L'\t');
    if (tab != std::wstring::npos) { acc = main.substr(tab + 1); main = main.substr(0, tab); }
    main.erase(std::remove(main.begin(), main.end(), L'&'), main.end());

    HDC dc = GetDC(g_hwnd);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    SIZE s1{}, s2{};
    GetTextExtentPoint32W(dc, main.c_str(), (int)main.size(), &s1);
    if (!acc.empty()) GetTextExtentPoint32W(dc, acc.c_str(), (int)acc.size(), &s2);
    SelectObject(dc, of);
    ReleaseDC(g_hwnd, dc);

    ms->itemHeight = g_menuRowH;
    if (d->top) {
        ms->itemWidth = s1.cx + 26;
    } else {
        ms->itemWidth = 36 + s1.cx + (s2.cx ? s2.cx + 34 : 0) + (d->popup ? 18 : 0);
    }
}

static void buildMenu() {
    g_menu = CreateMenu();

    // ---- 会话 ----
    g_menuSess = CreatePopupMenu();
    mAddMenu(g_menuSess, MF_STRING, IDM_PORT_OPEN,      L"新建会话…\tCtrl+N");
    mAddMenu(g_menuSess, MF_STRING, IDM_SESSION_RECONN, L"重新连接\tCtrl+R");
    mAddMenu(g_menuSess, MF_STRING, IDM_PORT_CLOSE,     L"断开连接\tCtrl+D");
    mAddMenu(g_menuSess, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuSess, MF_STRING, IDM_PORT_REFRESH,   L"刷新端口列表\tF5");
    mAddMenu(g_menuSess, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuSess, MF_STRING, IDM_SESSION_EXIT,   L"退出");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)g_menuSess, L"会话(&S)");

    // ---- 工具 ----
    g_menuTool = CreatePopupMenu();
    mAddMenu(g_menuTool, MF_STRING, IDM_PORT_SERVE, L"启用本地接管服务 (127.0.0.1:7000)");
    mAddMenu(g_menuTool, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuTool, MF_STRING, IDM_PORT_DTR, L"DTR 有效");
    mAddMenu(g_menuTool, MF_STRING, IDM_PORT_RTS, L"RTS 有效");
    mAddMenu(g_menuTool, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuTool, MF_STRING, IDM_TOOL_STTY, L"打开会话时适配终端宽度 (stty cols 400)");
    mAddMenu(g_menuTool, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuTool, MF_STRING, IDM_LOG_SAVE, L"保存串口日志");
    mAddMenu(g_menuTool, MF_STRING, IDM_LOG_DIR, L"设置日志目录…");
    mAddMenu(g_menuTool, MF_STRING, IDM_LOG_OPEN, L"打开日志目录");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)g_menuTool, L"工具(&T)");

    // ---- 视图 ----
    HMENU mView = CreatePopupMenu();
    mAddMenu(mView, MF_STRING, IDM_VIEW_BIGGER, L"字体放大\tCtrl+=");
    mAddMenu(mView, MF_STRING, IDM_VIEW_SMALLER, L"字体缩小\tCtrl+-");
    mAddMenu(mView, MF_STRING, IDM_VIEW_RESET, L"字体重置");
    mAddMenu(mView, MF_SEPARATOR, 0, nullptr);
    mAddMenu(mView, MF_STRING, IDM_VIEW_TS, L"显示时间戳");
    mAddMenu(mView, MF_STRING, IDM_VIEW_AGENT, L"显示接管命令");
    mAddMenu(mView, MF_STRING, IDM_SIDE_TOGGLE, L"左侧连接信息栏\tCtrl+B");
    mAddMenu(mView, MF_SEPARATOR, 0, nullptr);
    HMENU mEnc = CreatePopupMenu();
    mAddMenu(mEnc, MF_STRING, IDM_ENC_BASE + 0, L"自动（UTF-8 优先，失败退 GBK）");
    mAddMenu(mEnc, MF_STRING, IDM_ENC_BASE + 1, L"UTF-8");
    mAddMenu(mEnc, MF_STRING, IDM_ENC_BASE + 2, L"文本 ANSI (GBK)");
    mAddMenu(mView, MF_POPUP, (UINT_PTR)mEnc, L"串口编码(&E)");
    mAddMenu(mView, MF_SEPARATOR, 0, nullptr);
    mAddMenu(mView, MF_STRING, IDM_VIEW_CLEAR, L"清屏\tCtrl+L");
    mAddMenu(mView, MF_STRING, IDM_VIEW_SAVE, L"保存日志…");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)mView, L"视图(&V)");

    // ---- 设置 ----
    g_menuSet = CreatePopupMenu();
    HMENU mMF = CreatePopupMenu();
    for (int i = 0; i < kUiPtCount; i++) {
        wchar_t t[16];
        swprintf_s(t, L"%d pt", kUiPts[i]);
        mAddMenu(mMF, MF_STRING, IDM_SET_MFONT_BASE + i, t);
    }
    mAddMenu(g_menuSet, MF_POPUP, (UINT_PTR)mMF, L"菜单字体大小");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_MFG, L"菜单文字颜色…");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_MBG, L"菜单背景颜色…");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_MSEL, L"菜单选中颜色…");
    mAddMenu(g_menuSet, MF_SEPARATOR, 0, nullptr);
    HMENU mDF = CreatePopupMenu();
    for (int i = 0; i < kUiPtCount; i++) {
        wchar_t t[16];
        swprintf_s(t, L"%d pt", kUiPts[i]);
        mAddMenu(mDF, MF_STRING, IDM_SET_DFONT_BASE + i, t);
    }
    mAddMenu(g_menuSet, MF_POPUP, (UINT_PTR)mDF, L"对话框字体大小");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_DFG, L"对话框文字颜色…");
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_DBG, L"对话框背景颜色…");
    mAddMenu(g_menuSet, MF_SEPARATOR, 0, nullptr);
    mAddMenu(g_menuSet, MF_STRING, IDM_SET_RESET, L"恢复默认样式");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)g_menuSet, L"设置(&O)");

    // ---- 帮助 ----
    HMENU mHelp = CreatePopupMenu();
    mAddMenu(mHelp, MF_STRING, IDM_HELP_ABOUT, L"关于 SerialMate");
    mAddMenu(g_menu, MF_POPUP, (UINT_PTR)mHelp, L"帮助(&A)");

    SetMenu(g_hwnd, g_menu);
    applyMenuStyle();
    syncMenuChecks();
}

// 切换串口显示/发送编码：菜单和底部下拉框共用这一个入口
static void applyEnc(int e, bool announce) {
    if (e < 0 || e > 2) e = 0;
    g_rxEnc = e;
    saveSettings();
    syncMenuChecks();                     // 菜单勾选 + 下拉框选中项
    updateStatus();
    if (g_termReady) termSyncLine();      // 还没回车的那一行按新编码重画
    if (announce) {
        appendRun(L"— 串口编码切换为 " + std::wstring(encName()) + L" —\r\n", C_SYS);
        logLine(L"SYS", "enc=" + std::to_string(e));
    }
}

// ---------------- 布局 ----------------
static void layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    int top     = 8;
    int sideW   = g_sideOn ? 196 : 24;
    int statusH = 22;
    int viewH   = H - top - statusH - 8;
    int keysW   = g_keysOn ? g_keysW : 24;            // 收起时只留一条按钮
    int maxKeys = W - 16 - sideW - 6 - 200;           // 终端至少留 200px
    if (keysW > maxKeys) keysW = maxKeys;
    if (keysW < 24) keysW = 24;
    int viewW = W - 16 - sideW - 6 - keysW - 6;
    if (viewW < 120) { keysW = 0; viewW = W - 16 - sideW - 6; }

    // 左侧栏占满整列，折叠把手贴在其右边缘、垂直居中
    MoveWindow(g_hSide, 8, top, sideW, viewH, TRUE);
    if (g_hSideBtn) {
        int bw2 = g_sideOn ? kHandleW : 22;
        int bh2 = g_sideOn ? kHandleH : 46;
        int bx  = 8 + sideW - bw2 - (g_sideOn ? 0 : 1);
        int by  = top + (viewH - bh2) / 2;
        if (by < top) by = top;
        SetWindowPos(g_hSideBtn, HWND_TOP, bx, by, bw2, bh2, SWP_NOACTIVATE);
    }
    MoveWindow(g_hView, 8 + sideW + 6, top, viewW, viewH, TRUE);

    // SSH 会话：终端控件顶替 RichEdit 占同一块区域
    bool sshMode = (g_cur && g_cur->type == CONN_SSH);
    for (auto& sp : g_sess)
        if (sp->term) sp->term->showWindow(false);
    if (sshMode && g_cur->term) {
#ifdef TERM_TRACE
        { FILE* f = fopen("term_size.log", "a");
          if (f) { fprintf(f, "layout W=%d H=%d side=%d keys=%d viewW=%d viewH=%d top=%d\n",
                           W, H, sideW, keysW, viewW, viewH, top); fclose(f); } }
#endif
        g_cur->term->setBounds(8 + sideW + 6, top, viewW, viewH);
        g_cur->term->showWindow(true);
        ShowWindow(g_hView, SW_HIDE);
    } else if (g_hView) {
        ShowWindow(g_hView, SW_SHOW);
    }

    int keysX = 8 + sideW + 6 + viewW + 6;
    if (g_hKeysBtn) {
        if (keysW) {
            // 折叠把手：始终贴在栏的左边缘、垂直居中
            int bw2 = g_keysOn ? kHandleW : 22;
            int bh2 = g_keysOn ? kHandleH : 46;
            int bx  = keysX + (g_keysOn ? 0 : 1);
            int by  = top + (viewH - bh2) / 2;
            if (by < top + kKeyTopH) by = top + kKeyTopH;
            SetWindowPos(g_hKeysBtn, HWND_TOP, bx, by, bw2, bh2, SWP_NOACTIVATE);
            ShowWindow(g_hKeysBtn, SW_SHOW);
        } else {
            ShowWindow(g_hKeysBtn, SW_HIDE);
        }
    }
    // 顶部：按钮/文本 模式切换条
    if (g_hKeyMode) {
        if (keysW && g_keysOn) {
            SetWindowPos(g_hKeyMode, HWND_TOP, keysX, top, keysW, kKeyTopH, SWP_NOACTIVATE);
            ShowWindow(g_hKeyMode, SW_SHOW);
        } else {
            ShowWindow(g_hKeyMode, SW_HIDE);
        }
    }
    if (g_hKeysBar) {
        if (keysW && g_keysOn) {
            int barY = top + kKeyTopH, barH = viewH - kKeyTopH;
            MoveWindow(g_hKeysBar, keysX, barY, keysW, barH, TRUE);   // 栏占满整列，折叠把手浮在其左边缘中部
            ShowWindow(g_hKeysBar, SW_SHOW);
        } else {
            ShowWindow(g_hKeysBar, SW_HIDE);
        }
    }
    // 文本模式：多行编辑框盖住命令按钮区（底部导入/导出仍可见）
    if (g_hKeyText) {
        if (keysW && g_keysOn && g_keyTextMode) {
            int ex = keysX + kHandleW + 6;
            int ey = top + kKeyTopH + 6;
            int ew = keysW - kHandleW - 6 - 8;
            int eh = viewH - kKeyTopH - kKeyFootH - 12;
            if (ew < 40) ew = 40;
            if (eh < 40) eh = 40;
            // HWND_TOP：编辑框必须压在自绘栏之上，否则栏会盖住它导致点不到
            SetWindowPos(g_hKeyText, HWND_TOP, ex, ey, ew, eh, SWP_NOACTIVATE);
            ShowWindow(g_hKeyText, SW_SHOW);
        } else {
            ShowWindow(g_hKeyText, SW_HIDE);
        }
    }
    MoveWindow(g_hStatus, 8, H - 30, W - 16, 20, TRUE);
}

// 拖动中专用：只挪右侧栏和折叠按钮，终端尺寸不动（避免 RichEdit 反复重排造成闪烁/残影）
static void layoutKeysOnly() {
    if (!g_hwnd) return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int top = 8, statusH = 22;
    int sideW = g_sideOn ? 196 : 24;
    int viewH = H - top - statusH - 8;
    int keysW = g_keysW;
    int maxKeys = W - 16 - sideW - 6 - 200;
    if (keysW > maxKeys) keysW = maxKeys;
    if (keysW < 24) keysW = 24;
    int keysX = W - 8 - keysW;

    // 终端只在「栏变窄、会露出空档」时跟着变宽；拖宽栏（栏盖住终端）时终端不动
    // → 最常见的拖宽操作零重排、不闪；拖窄时也不会露出背景灰块
    if (g_hView) {
        RECT vr;
        GetWindowRect(g_hView, &vr);
        int curW  = vr.right - vr.left;
        int needW = keysX - 6 - (8 + sideW + 6);
        if (needW > curW + 1) {
            SetWindowPos(g_hView, nullptr, 8 + sideW + 6, top, needW, viewH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_cur && g_cur->term) {
#ifdef TERM_TRACE
                { FILE* f = fopen("term_size.log", "a");
                  if (f) { fprintf(f, "keysOnly W=%d H=%d keysW=%d keysX=%d needW=%d curW=%d viewH=%d\n",
                                   W, H, keysW, keysX, needW, curW, viewH); fclose(f); } }
#endif
                g_cur->term->setBounds(8 + sideW + 6, top, needW, viewH);
            }
        }
    }
    if (g_hKeysBtn) {
        int bw2 = g_keysOn ? kHandleW : 22;
        int bh2 = g_keysOn ? kHandleH : 46;
        int bx  = keysX + (g_keysOn ? 0 : 1);
        int by  = top + (viewH - bh2) / 2;
        if (by < top + kKeyTopH) by = top + kKeyTopH;
        SetWindowPos(g_hKeysBtn, HWND_TOP, bx, by, bw2, bh2, SWP_NOACTIVATE);
    }
    if (g_hKeyMode)
        SetWindowPos(g_hKeyMode, HWND_TOP, keysX, top, keysW, kKeyTopH, SWP_NOACTIVATE);
    if (g_hKeysBar && g_keysOn) {
        SetWindowPos(g_hKeysBar, nullptr, keysX, top + kKeyTopH, keysW, viewH - kKeyTopH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(g_hKeysBar, nullptr, TRUE);
    }
    if (g_hKeyText && g_keyTextMode) {
        SetWindowPos(g_hKeyText, HWND_TOP, keysX + kHandleW + 6, top + kKeyTopH + 6,
                     keysW - kHandleW - 6 - 8, viewH - kKeyTopH - kKeyFootH - 12,
                     SWP_NOACTIVATE);
    }
}

// ---------------- 控件 ----------------
static HWND mkBtn(HWND p, int id, const wchar_t* t) {
    return CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           0, 0, 80, 24, p, (HMENU)(INT_PTR)id, g_inst, nullptr);
}

static void createControls(HWND hwnd) {
    // 终端：优先 RichEdit50W（支持着色），退化到 RichEdit20W / EDIT
    g_hView = nullptr;
    if (LoadLibraryW(L"Msftedit.dll")) {
        g_hView = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_NOHIDESEL, 0, 0, 100, 100, hwnd,
                                  (HMENU)IDC_VIEW, g_inst, nullptr);
        g_termReady = (g_hView != nullptr);
    }
    if (!g_hView) {
        g_hView = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT20W", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_NOHIDESEL, 0, 0, 100, 100, hwnd,
                                  (HMENU)IDC_VIEW, g_inst, nullptr);
        g_termReady = (g_hView != nullptr);
    }
    if (!g_hView) {
        g_hView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_NOHIDESEL, 0, 0, 100, 100, hwnd,
                                  (HMENU)IDC_VIEW, g_inst, nullptr);
    }
    if (g_hView) {
        SendMessageW(g_hView, EM_EXLIMITTEXT, 0, 20 * 1024 * 1024);
        // 关掉软换行：终端里“一行”就应该是屏幕上的一行，否则 readline 用 \r 重绘时
        // 会以为在刷新屏幕行，而我们按逻辑行处理，长命令就会花屏
        SendMessageW(g_hView, EM_SETTARGETDEVICE, 0, 1);
        // 订阅文本变化：用来抓 TSF 输入法绕过窗口消息直接写进来的上屏内容
        SendMessageW(g_hView, EM_SETEVENTMASK, 0, ENM_CHANGE);
        SetWindowSubclass(g_hView, ViewProc, 1, 0);
    }

    g_hStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                0, 0, 100, 20, hwnd, (HMENU)IDC_STATUS, g_inst, nullptr);

    // 右侧：快捷命令栏（自绘，条目可增删改）+ 折叠按钮
    g_hKeysBtn = CreateWindowExW(0, L"BUTTON", L"»",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                 0, 0, 20, 22, hwnd, (HMENU)IDC_KEYBTN, g_inst, nullptr);
    g_hKeysBar = CreateWindowExW(0, L"STATIC", L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY,
                                 0, 0, 150, 100, hwnd, (HMENU)IDC_KEYS, g_inst, nullptr);
    SetWindowSubclass(g_hKeysBar, KeyProc, 3, 0);

    // 右侧栏顶部：按钮 / 文本 模式切换
    g_hKeyMode = CreateWindowExW(0, L"BUTTON", L"文本模式",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_OWNERDRAW,
                                 0, 0, 150, 24, hwnd, (HMENU)IDC_KEYMODE, g_inst, nullptr);
    // 文本模式编辑框：一行一条命令，双击某行执行
    g_hKeyText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL |
                                 ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL | ES_WANTRETURN,
                                 0, 0, 100, 100, hwnd, (HMENU)IDC_KEYTEXT, g_inst, nullptr);
    if (g_hKeyText) {
        SendMessageW(g_hKeyText, EM_SETLIMITTEXT, 0, 64 * 1024);
        SendMessageW(g_hKeyText, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SetWindowSubclass(g_hKeyText, KeyTextProc, 4, 0);
    }

    // 左侧：连接信息栏（串口名 + 状态），右边缘中部为折叠把手
    g_hSideBtn = CreateWindowExW(0, L"BUTTON", L"«",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                 0, 0, 14, 132, hwnd, (HMENU)IDC_SIDEBTN, g_inst, nullptr);
    g_hSide = CreateWindowExW(0, L"STATIC", L"",
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY,
                              0, 0, 150, 100, hwnd, (HMENU)IDC_SIDE, g_inst, nullptr);
    SetWindowSubclass(g_hSide, SideProc, 2, 0);   // 右键：波特率 / 断开串口

    for (HWND h : {g_hStatus, g_hSide, g_hSideBtn, g_hKeysBar, g_hKeysBtn, g_hKeyMode, g_hKeyText})
        SendMessageW(h, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    updateKeysBtn();
    updateKeysModeBtn();
    applyFont();
    updateSide();
}

static void refreshPorts() {
    auto ports = enumSerialPorts();
    if (ports.empty()) return;
    bool found = false;
    for (auto& p : ports) if (p == g_portName) { found = true; break; }
    if (!found) g_portName = ports[0];
}

// ---------------- 外观：字体与配色 ----------------
static HFONT makeUiFont(int idx) {
    HDC dc = GetDC(g_hwnd);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(g_hwnd, dc);
    return CreateFontW(-MulDiv(kUiPts[idx], dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}

// 对话框字体（同时套到主窗口的 UI 控件上）
static void applyUiFont() {
    HFONT old = g_fontUI;
    g_fontUI = makeUiFont(g_dlgPtIdx);
    if (old) DeleteObject(old);
    for (HWND hw : {g_hStatus, g_hSide, g_hSideBtn, g_hKeysBar, g_hKeysBtn, g_hKeyMode, g_hKeyText})
        if (hw) SendMessageW(hw, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    if (g_hwnd) layout(g_hwnd);
    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
    if (g_hKeysBar) InvalidateRect(g_hKeysBar, nullptr, TRUE);
}

static void applyDlgStyle() {
    if (g_brushDlg) DeleteObject(g_brushDlg);
    g_brushDlg = CreateSolidBrush(g_dlgBg);
    if (g_dlgWnd) InvalidateRect(g_dlgWnd, nullptr, TRUE);
}

static bool pickColor(HWND owner, COLORREF& c) {
    static COLORREF cust[16] = {0};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.rgbResult = c;
    cc.lpCustColors = cust;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return false;
    c = cc.rgbResult;
    return true;
}

// ---------------- 会话对话框 ----------------
static bool     g_dlgDone = false;
static bool     g_dlgOK = false;
static ConnType g_dlgType = CONN_SERIAL;
static std::vector<HWND> g_pg[4];

static HWND g_dcPort, g_dcBaud, g_dcMServe, g_dcMDirect, g_dcDtr, g_dcRts;
static HWND g_dcSshHost, g_dcSshPort, g_dcSshUser, g_dcSshPass;
static HWND g_dcTelHost, g_dcTelPort;
static HWND g_dcRip, g_dcRcom, g_dcRbaud;

// 对话框按字号整体缩放（以 10pt 为基准），避免字变大后控件挤出窗口
static double g_dlgScale = 1.0;
static int DS(int v) { return (int)(v * g_dlgScale + 0.5); }

static HWND dcLabel(HWND p, const wchar_t* t, int x, int y, int w, int h = 20) {
    HWND c = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT,
                             DS(x), DS(y), DS(w), DS(h), p, nullptr, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}
static HWND dcEdit(HWND p, int id, const wchar_t* t, int x, int y, int w, DWORD ex = 0) {
    HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ex,
                             DS(x), DS(y), DS(w), DS(24), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}
static HWND dcRadio(HWND p, int id, const wchar_t* t, int x, int y, int w, bool first) {
    HWND c = CreateWindowExW(0, L"BUTTON", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                             (first ? WS_GROUP : 0),
                             DS(x), DS(y), DS(w), DS(22), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}
static HWND dcCheck(HWND p, int id, const wchar_t* t, int x, int y, int w, bool first) {
    HWND c = CreateWindowExW(0, L"BUTTON", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX |
                             (first ? WS_GROUP : 0),
                             DS(x), DS(y), DS(w), DS(22), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}
static HWND dcCombo(HWND p, int id, int x, int y, int w) {
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                             WS_VSCROLL | CBS_DROPDOWNLIST, DS(x), DS(y), DS(w), DS(240), p,
                             (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}
static HWND dcGroup(HWND p, const wchar_t* t, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                             DS(x), DS(y), DS(w), DS(h), p, nullptr, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}
static HWND dcBtn(HWND p, int id, const wchar_t* t, int x, int y, int w, int h = 28) {
    HWND c = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                             DS(x), DS(y), DS(w), DS(h), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return c;
}

static std::wstring editText(HWND h) {
    wchar_t b[512] = L"";
    if (h) GetWindowTextW(h, b, 512);
    return std::wstring(b);
}
static std::wstring comboText(HWND h) {
    wchar_t b[256] = L"";
    int i = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (i >= 0) SendMessageW(h, CB_GETLBTEXT, i, (LPARAM)b);
    return std::wstring(b);
}

static void dlgFillPorts() {
    if (!g_dcPort) return;
    SendMessageW(g_dcPort, CB_RESETCONTENT, 0, 0);
    auto ports = enumSerialPorts();
    int sel = 0;
    for (int i = 0; i < (int)ports.size(); i++) {
        SendMessageW(g_dcPort, CB_ADDSTRING, 0, (LPARAM)ports[i].c_str());
        if (ports[i] == g_portName) sel = i;
    }
    if (ports.empty()) { SendMessageW(g_dcPort, CB_ADDSTRING, 0, (LPARAM)L"COM3"); sel = 0; }
    SendMessageW(g_dcPort, CB_SETCURSEL, sel, 0);
}
static void dlgFillBaud(HWND combo, int cur) {
    for (int i = 0; i < kBaudCount; i++) {
        wchar_t t[32];
        swprintf_s(t, L"%d", kBaits[i]);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)t);
    }
    int idx = baudIndexOf(cur);
    SendMessageW(combo, CB_SETCURSEL, idx < 0 ? 4 : idx, 0);
}

static void dlgShowPage(ConnType t) {
    g_dlgType = t;
    for (int i = 0; i < 4; i++)
        for (HWND h : g_pg[i]) ShowWindow(h, (i == (int)t) ? SW_SHOW : SW_HIDE);
    if (g_dlgWnd) CheckRadioButton(g_dlgWnd, IDD_SER, IDD_REM, IDD_SER + (int)t);
}

// 通用单行输入对话框：标签 + 单行编辑框 + 确定/取消
static void dlgBuildInput(HWND h) {
    g_dcIbLabel = dcLabel(h, g_ibPrompt.c_str(), 20, 18, 380);
    g_dcIbEdit  = dcEdit(h, IDD_IB_EDIT, g_ibText.c_str(), 20, 46, 380);
    dcBtn(h, IDD_OK,     L"确定", 226, 96, 82, 30);
    dcBtn(h, IDD_CANCEL, L"取消", 318, 96, 82, 30);
    ShowWindow(g_dcIbLabel, SW_SHOW);
    SetFocus(g_dcIbEdit);
    SendMessageW(g_dcIbEdit, EM_SETSEL, 0, -1);
}

static void dlgBuild(HWND h) {
    for (int i = 0; i < 4; i++) g_pg[i].clear();
    if (g_ibMode) { dlgBuildInput(h); return; }   // 通用单行输入对话框

    // 连接类型
    dcGroup(h, L"连接类型", 12, 8, 446, 54);
    dcRadio(h, IDD_SER, L"串口",     30, 32, 72,  true);
    dcRadio(h, IDD_SSH, L"SSH",      112, 32, 66,  false);
    dcRadio(h, IDD_TEL, L"Telnet",   186, 32, 76,  false);
    dcRadio(h, IDD_REM, L"远程串口", 270, 32, 104, false);

    // ---- 页 0：串口 ----
    {
        auto& pg = g_pg[0];
        pg.push_back(dcLabel(h, L"串口:", 20, 80, 46));
        g_dcPort = dcCombo(h, IDD_PORT, 70, 76, 140);
        pg.push_back(g_dcPort);
        pg.push_back(dcBtn(h, IDD_PORTRF, L"刷新", 220, 76, 56, 26));
        pg.push_back(dcLabel(h, L"波特率:", 292, 80, 56));
        g_dcBaud = dcCombo(h, IDD_BAUD, 352, 76, 100);
        pg.push_back(g_dcBaud);

        pg.push_back(dcGroup(h, L"接管模式", 20, 116, 430, 62));
        g_dcMServe  = dcRadio(h, IDD_M_SERVE,  L"被接管（AI 可控制）", 36, 140, 178, true);
        g_dcMDirect = dcRadio(h, IDD_M_DIRECT, L"直连（独占串口）",    228, 140, 150, false);
        pg.push_back(g_dcMServe);
        pg.push_back(g_dcMDirect);

        g_dcDtr = dcCheck(h, IDD_DTR, L"DTR 有效", 36, 190, 100, true);
        g_dcRts = dcCheck(h, IDD_RTS, L"RTS 有效", 150, 190, 100, false);
        pg.push_back(g_dcDtr);
        pg.push_back(g_dcRts);

        pg.push_back(dcLabel(h,
            L"被接管：本程序在 127.0.0.1:7000 开放服务，AI 助手可直接下发命令；"
            L"直连：仅本窗口使用，不监听端口。", 20, 222, 430, 48));
    }

    // ---- 页 1：SSH ----
    {
        auto& pg = g_pg[1];
        pg.push_back(dcLabel(h, L"主机:", 20, 82, 46));
        g_dcSshHost = dcEdit(h, IDD_SSH_HOST, g_sshHost.c_str(), 74, 78, 190);
        pg.push_back(g_dcSshHost);
        pg.push_back(dcLabel(h, L"端口:", 280, 82, 46));
        g_dcSshPort = dcEdit(h, IDD_SSH_PORT, std::to_wstring(g_sshPort).c_str(), 330, 78, 70);
        pg.push_back(g_dcSshPort);

        pg.push_back(dcLabel(h, L"用户名:", 20, 118, 56));
        g_dcSshUser = dcEdit(h, IDD_SSH_USER, g_sshUser.c_str(), 84, 114, 178);
        pg.push_back(g_dcSshUser);
        pg.push_back(dcLabel(h, L"密码:", 280, 118, 46));
        g_dcSshPass = dcEdit(h, IDD_SSH_PASS, g_sshPass.c_str(), 322, 114, 122, ES_PASSWORD);
        pg.push_back(g_dcSshPass);

        pg.push_back(dcLabel(h, L"密码认证（自实现 SSH 协议，无需 PuTTY/plink 等外部程序）。", 20, 156, 430, 48));
    }

    // ---- 页 2：Telnet ----
    {
        auto& pg = g_pg[2];
        pg.push_back(dcLabel(h, L"主机:", 20, 82, 46));
        g_dcTelHost = dcEdit(h, IDD_TEL_HOST, g_telHost.c_str(), 74, 78, 190);
        pg.push_back(g_dcTelHost);
        pg.push_back(dcLabel(h, L"端口:", 280, 82, 46));
        g_dcTelPort = dcEdit(h, IDD_TEL_PORT, std::to_wstring(g_telPort).c_str(), 330, 78, 70);
        pg.push_back(g_dcTelPort);

        pg.push_back(dcLabel(h, L"本轮 Telnet 仅保存连接参数，连接通道待接入。", 20, 120, 430, 48));
    }

    // ---- 页 3：远程串口 ----
    {
        auto& pg = g_pg[3];
        pg.push_back(dcLabel(h, L"远端 IP:", 20, 82, 66));
        g_dcRip = dcEdit(h, IDD_RIP, g_remIp.c_str(), 92, 78, 160);
        pg.push_back(g_dcRip);
        pg.push_back(dcLabel(h, L"远端 COM:", 272, 82, 72));
        g_dcRcom = dcEdit(h, IDD_RCOM, g_remCom.c_str(), 350, 78, 90);
        pg.push_back(g_dcRcom);

        pg.push_back(dcLabel(h, L"波特率:", 20, 118, 56));
        g_dcRbaud = dcCombo(h, IDD_RBAUD, 86, 114, 120);
        pg.push_back(g_dcRbaud);

        pg.push_back(dcLabel(h,
            L"远端 PC 上也运行本程序，勾选「启用本地接管服务」并打开对应串口（默认 7000 端口）。"
            L"连上后本窗口的键盘输入会经网络下发到那台机器的串口。", 20, 156, 430, 60));
    }

    // 底部
    dcBtn(h, IDD_OK,     L"确定", 272, 286, 82, 30);
    dcBtn(h, IDD_CANCEL, L"取消", 366, 286, 82, 30);
}

static void dlgCollect() {
    if (g_dlgType == CONN_SERIAL) {
        std::wstring p = comboText(g_dcPort);
        if (!p.empty()) g_portName = p;
        int bi = (int)SendMessageW(g_dcBaud, CB_GETCURSEL, 0, 0);
        if (bi >= 0 && bi < kBaudCount) { g_baudIdx = bi; g_baud = kBaits[bi]; }
        g_serve = isChecked(g_dcMServe);
        g_dtr = isChecked(g_dcDtr);
        g_rts = isChecked(g_dcRts);
    } else if (g_dlgType == CONN_SSH) {
        g_sshHost = editText(g_dcSshHost);
        g_sshPort = (int)wcstol(editText(g_dcSshPort).c_str(), nullptr, 10);
        g_sshUser = editText(g_dcSshUser);
        g_sshPass = editText(g_dcSshPass);
        if (g_sshPort <= 0) g_sshPort = 22;
    } else if (g_dlgType == CONN_TELNET) {
        g_telHost = editText(g_dcTelHost);
        g_telPort = (int)wcstol(editText(g_dcTelPort).c_str(), nullptr, 10);
        if (g_telPort <= 0) g_telPort = 23;
    } else {
        g_remIp  = editText(g_dcRip);
        g_remCom = editText(g_dcRcom);
        int bi = (int)SendMessageW(g_dcRbaud, CB_GETCURSEL, 0, 0);
        if (bi >= 0 && bi < kBaudCount) g_remBaud = kBaits[bi];
    }
    g_connType = g_dlgType;
}

static LRESULT CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, g_dlgFg);
            SetBkColor(dc, g_dlgBg);
            return (LRESULT)(g_brushDlg ? g_brushDlg : GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_CTLCOLORBTN:
            return (LRESULT)(g_brushDlg ? g_brushDlg : GetSysColorBrush(COLOR_BTNFACE));
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, g_dlgFg);
            SetBkColor(dc, RGB(0xFF, 0xFF, 0xFF));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect((HDC)wp, &rc, g_brushDlg ? g_brushDlg : GetSysColorBrush(COLOR_BTNFACE));
            return 1;
        }
        case WM_CREATE:
            dlgBuild(h);
            if (g_ibMode) return 0;
            dlgFillPorts();
            dlgFillBaud(g_dcBaud, g_baud);
            dlgFillBaud(g_dcRbaud, g_remBaud);
            SendMessageW(g_dcMServe,  BM_SETCHECK, g_serve ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_dcMDirect, BM_SETCHECK, g_serve ? BST_UNCHECKED : BST_CHECKED, 0);
            SendMessageW(g_dcDtr, BM_SETCHECK, g_dtr ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_dcRts, BM_SETCHECK, g_rts ? BST_CHECKED : BST_UNCHECKED, 0);
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id >= IDD_SER && id <= IDD_REM) {
                dlgShowPage((ConnType)(id - IDD_SER));
                return 0;
            }
            if (id == IDD_PORTRF) { dlgFillPorts(); return 0; }
            if (id == IDD_M_SERVE || id == IDD_M_DIRECT) {
                // 两个 radio 需要互斥（不同 WS_GROUP 组，这里手动同步）
                bool serve = (id == IDD_M_SERVE);
                SendMessageW(g_dcMServe,  BM_SETCHECK, serve ? BST_CHECKED : BST_UNCHECKED, 0);
                SendMessageW(g_dcMDirect, BM_SETCHECK, serve ? BST_UNCHECKED : BST_CHECKED, 0);
                return 0;
            }
            if (id == IDD_OK) {
                if (g_ibMode) {                       // 单行输入模式
                    g_ibText = editText(g_dcIbEdit);
                    g_dlgOK = true;
                    DestroyWindow(h);
                    return 0;
                }
                dlgCollect();
                g_dlgOK = true;
                DestroyWindow(h);
                return 0;
            }
            if (id == IDD_CANCEL) { DestroyWindow(h); return 0; }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(h);
            return 0;

        case WM_DESTROY:
            g_dlgDone = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int showSessionDialog() {
    g_dlgType = g_connType;
    g_dlgDone = false;
    g_dlgOK = false;

    RECT pr;
    GetWindowRect(g_hwnd, &pr);
    g_dlgScale = (double)kUiPts[g_dlgPtIdx] / 10.0;
    RECT wr{0, 0, DS(470), DS(328)};
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dw = wr.right - wr.left, dh = wr.bottom - wr.top;
    int x = pr.left + ((pr.right - pr.left) - dw) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - dh) / 2;

    EnableWindow(g_hwnd, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SerialMateDlg", L"新建会话",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               x, y, dw, dh, g_hwnd, nullptr, g_inst, nullptr);
    if (!dlg) { EnableWindow(g_hwnd, TRUE); return 0; }
    g_dlgWnd = dlg;
    dlgShowPage(g_dlgType);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    SetFocus(dlg);

    MSG msg;
    while (!g_dlgDone && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    g_dlgWnd = nullptr;
    EnableWindow(g_hwnd, TRUE);
    SetActiveWindow(g_hwnd);
    return g_dlgOK ? 1 : 0;
}

// 按当前 g_connType 建立连接
// 建连（对指定会话，默认当前会话）
static void connectSession(Session* s) {
    if (!s) s = g_cur;
    if (!s) return;
    if (s->type == CONN_REMOTE) {
        // 注意：**不能** stopServer()。本机自环测试就是「COM3 会话提供接管服务 +
        // 远程串口会话连自己 127.0.0.1:7000」，停掉服务等于把自己掐死。
        openRemote(s);
    } else if (s->type == CONN_SSH) {
        startSsh(s);
    } else if (s->type == CONN_SERIAL) {
        if (g_serve) startServer(); else stopServer();
        openPort(s);
    }
}

// 新建会话：把对话框里的参数做成一条新会话，接进列表并切过去（原会话不动）
static void newSessionDialog() {
    if (!showSessionDialog()) return;
    auto sp = std::make_unique<Session>();
    sp->id      = g_nextSessId++;
    sp->type    = g_connType;
    sp->port    = g_portName;
    sp->baud    = g_baud;
    sp->remIp   = g_remIp;
    sp->remCom  = g_remCom;
    sp->remBaud = g_remBaud;
    sp->host    = (g_connType == CONN_SSH) ? g_sshHost : g_telHost;
    sp->hostPort= (g_connType == CONN_SSH) ? g_sshPort : g_telPort;
    if (g_connType == CONN_SSH) {
        sp->sshUser = g_sshUser;
        sp->sshPass = g_sshPass;
        sp->sshInfo = g_sshUser + L"@" + g_sshHost + L":" + std::to_wstring(g_sshPort);
    }
    sessionNameOf(sp.get());
    Session* s = sp.get();
    g_sess.push_back(std::move(sp));
    switchSession(s);
    connectSession(s);
    saveSettings();
    syncMenuChecks();
    updateStatus();
    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
    if (s->term) s->term->focus();          // SSH 会话要喂终端控件，别把焦点塞给隐藏的 RichEdit
    else if (g_hView) SetFocus(g_hView);
}

// 关闭一条会话（断开并从列表移除）
static void closeSession(Session* s) {
    if (!s) return;
    closePort(s);
    auto it = std::find_if(g_sess.begin(), g_sess.end(),
                           [&](const std::unique_ptr<Session>& p) { return p.get() == s; });
    if (it == g_sess.end()) return;
    if (g_ctxSess == s) g_ctxSess = nullptr;
    bool wasCur = (s == g_cur);
    int idx = (int)(it - g_sess.begin());
    g_sess.erase(it);
    if (wasCur) {
        g_cur = nullptr;
        if (!g_sess.empty()) {
            int ni = idx < (int)g_sess.size() ? idx : (int)g_sess.size() - 1;
            Session* t = g_sess[ni].get();
            g_cur = t;
            applySessionView(t);
            std::string pend;
            { std::lock_guard<std::mutex> lk(t->mtx); pend.swap(t->pending); }
            if (!pend.empty()) showRx(pend);
        }
    }
    updateStatus();
    syncMenuChecks();
    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
}

// ================= 右侧快捷命令栏 =================
static const int kKeyRowH = 26;   // 单个按钮高
static const int kKeyGap  = 5;    // 按钮间距
static const int kKeyPad  = 8;    // 栏内边距
static const int kResizeW = 6;    // 左边缘拖拽热区宽度（拖动改变栏宽）

static std::wstring trimW(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) a++;
    while (b > a && (s[b-1] == L' ' || s[b-1] == L'\t' || s[b-1] == L'\r' || s[b-1] == L'\n')) b--;
    return s.substr(a, b - a);
}

static void saveShortcuts() {
    FILE* f = _wfopen((exeDir() + L"\\shortcuts.ini").c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    for (size_t i = 0; i < g_keys.size(); i++)
        fwprintf(f, L"F%d=%ls\n", (int)i + 1, g_keys[i].c_str());
    fclose(f);
}

static void loadShortcuts() {
    const wchar_t* def[8] = {L"ls -l", L"dmesg | tail -20", L"ifconfig", L"ps",
                             L"free -m", L"df -h", L"ps | head", L"uname -a"};
    std::vector<std::pair<int, std::wstring>> got;
    FILE* f = _wfopen((exeDir() + L"\\shortcuts.ini").c_str(), L"r, ccs=UTF-8");
    if (f) {
        wchar_t line[1024];
        while (fgetws(line, 1024, f)) {
            std::wstring s = trimW(line);
            size_t eq = s.find(L'=');
            if (eq == std::wstring::npos || eq < 2) continue;
            if (s[0] != L'F' && s[0] != L'f') continue;
            int n = _wtoi(s.c_str() + 1);
            if (n < 1 || n > 99) continue;
            got.push_back({n, s.substr(eq + 1)});
        }
        fclose(f);
    }
    std::sort(got.begin(), got.end(),
              [](const std::pair<int, std::wstring>& a, const std::pair<int, std::wstring>& b) {
                  return a.first < b.first;
              });
    g_keys.clear();
    if (got.empty()) {
        for (int i = 0; i < 8; i++) g_keys.push_back(def[i]);
    } else {
        for (auto& kv : got) if (!kv.second.empty()) g_keys.push_back(kv.second);
    }
}

static void keyBarRefresh() {
    if (g_hKeysBar) {
        int maxS = 0;
        RECT rc; GetClientRect(g_hKeysBar, &rc);
        int content = kKeyPad + (int)g_keys.size() * (kKeyRowH + kKeyGap) + kKeyFootH;
        if (content > rc.bottom) maxS = content - rc.bottom;
        if (g_keyScroll > maxS) g_keyScroll = maxS;
        if (g_keyScroll < 0) g_keyScroll = 0;
        InvalidateRect(g_hKeysBar, nullptr, TRUE);
    }
}

// 右侧栏折叠按钮文字：展开时 « 收起，收起时 » 展开
static void updateKeysBtn() {
    if (g_hKeysBtn) SetWindowTextW(g_hKeysBtn, g_keysOn ? L"»" : L"«");
}

// 顶部切换条文字：按钮模式显示「文本模式」（点它去文本），反之亦然
static void updateKeysModeBtn() {
    if (g_hKeyMode) SetWindowTextW(g_hKeyMode, g_keyTextMode ? L"按钮模式" : L"文本模式");
}

// 把编辑框内容按行拆成命令列表（空行忽略），写回 g_keys 并落盘
static void keyTextCollect() {
    if (!g_hKeyText) return;
    int n = (int)SendMessageW(g_hKeyText, WM_GETTEXTLENGTH, 0, 0);
    std::wstring all;
    all.resize((size_t)n + 1);
    int got = (int)SendMessageW(g_hKeyText, WM_GETTEXT, n + 1, (LPARAM)&all[0]);
    all.resize(got > 0 ? (size_t)got : 0);

    std::vector<std::wstring> out;
    size_t p = 0;
    while (p <= all.size()) {
        size_t q = all.find(L'\n', p);
        std::wstring s = all.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p);
        s = trimW(s);
        if (!s.empty()) out.push_back(s);
        if (q == std::wstring::npos) break;
        p = q + 1;
    }
    g_keys.swap(out);
    saveShortcuts();
    keyBarRefresh();
}

// 命令列表 → 编辑框（一行一条）；toEnd=true 时光标停在末尾
static void keyTextReload(bool toEnd) {
    if (!g_hKeyText) return;
    std::wstring all;
    for (size_t i = 0; i < g_keys.size(); i++) {
        if (i) all += L"\r\n";
        all += g_keys[i];
    }
    SetWindowTextW(g_hKeyText, all.c_str());
    SendMessageW(g_hKeyText, EM_SETSEL, toEnd ? (WPARAM)-1 : 0, toEnd ? (LPARAM)-1 : 0);
}

// 按钮模式 <-> 文本模式
static void keyModeApply(bool textMode) {
    if (textMode == g_keyTextMode) return;
    if (textMode) {                              // 命令列表 → 一行一条写进编辑框
        g_keyTextMode = true;
        keyTextReload(false);
    } else {                                     // 编辑框 → 命令列表
        keyTextCollect();
        g_keyTextMode = false;
    }
    updateKeysModeBtn();
    if (g_hwnd) layout(g_hwnd);
    if (g_hKeysBar) InvalidateRect(g_hKeysBar, nullptr, TRUE);
    if (g_keyTextMode) { if (g_hKeyText) SetFocus(g_hKeyText); }
    else if (g_hView) SetFocus(g_hView);
}

// 执行文本模式中的某一行（双击触发）：按回车发送，等同手动敲入
static void keyTextRunLine(int line) {
    if (!g_hKeyText || line < 0) return;
    wchar_t buf[2048];
    *((WORD*)buf) = (WORD)2046;                  // EM_GETLINE 要求首字为缓冲区字符数
    int n = (int)SendMessageW(g_hKeyText, EM_GETLINE, (WPARAM)line, (LPARAM)buf);
    if (n <= 0) return;
    buf[n] = 0;
    std::wstring cmd = trimW(buf);
    if (cmd.empty()) return;
    termSendBytes(txEncode(cmd) + "\r", C_TX, nullptr);
    lineReset();
}

// 文本编辑框：双击某行 → 立即执行该行命令
static LRESULT CALLBACK KeyTextProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_LBUTTONDBLCLK) {
        LRESULT r = DefSubclassProc(h, m, w, l);  // 先让编辑框自己定位光标
        POINT pt{(short)LOWORD(l), (short)HIWORD(l)};
        LRESULT ci = SendMessageW(h, EM_CHARFROMPOS, 0, (LPARAM)&pt);
        int line = (int)SendMessageW(h, EM_LINEFROMCHAR, (WPARAM)(int)ci, 0);
        keyTextRunLine(line);
        return r;
    }
    return DefSubclassProc(h, m, w, l);
}

// 通用单行输入框：返回 true 时 val 为新内容
static bool showInputBox(const wchar_t* title, const wchar_t* prompt, std::wstring& val) {
    g_ibMode   = true;
    g_ibPrompt = prompt;
    g_ibText   = val;
    g_dlgDone  = false;
    g_dlgOK    = false;

    RECT pr;
    GetWindowRect(g_hwnd, &pr);
    g_dlgScale = (double)kUiPts[g_dlgPtIdx] / 10.0;
    RECT wr{0, 0, DS(420), DS(150)};
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dw = wr.right - wr.left, dh = wr.bottom - wr.top;
    int x = pr.left + ((pr.right - pr.left) - dw) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - dh) / 2;

    EnableWindow(g_hwnd, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SerialMateDlg", title,
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, dw, dh,
                               g_hwnd, nullptr, g_inst, nullptr);
    if (!dlg) {
        EnableWindow(g_hwnd, TRUE);
        g_ibMode = false;
        return false;
    }
    g_dlgWnd = dlg;
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    if (g_dcIbEdit) SetFocus(g_dcIbEdit); else SetFocus(dlg);

    MSG msg;
    while (!g_dlgDone && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    g_dlgWnd = nullptr;
    g_dcIbLabel = g_dcIbEdit = nullptr;
    EnableWindow(g_hwnd, TRUE);
    SetActiveWindow(g_hwnd);
    g_ibMode = false;
    if (g_dlgOK) val = g_ibText;
    return g_dlgOK;
}

static void runShortcut(int i) {
    if (i < 0 || i >= (int)g_keys.size()) return;
    termSendBytes(txEncode(g_keys[i]) + "\r", C_TX, nullptr);
    lineReset();
    // 焦点要还给它真正显示的那个控件：SSH 会话的显示区是 Terminal，
    // 原来无条件 SetFocus(g_hView)，会把手写焦点交给一个被隐藏的 RichEdit。
    if (g_cur && g_cur->type == CONN_SSH && g_cur->term) g_cur->term->focus();
    else if (g_hView) SetFocus(g_hView);
}

static void keyAdd() {
    std::wstring v;
    if (!showInputBox(L"新增快捷命令", L"命令内容（点击按钮即回车发送）：", v)) return;
    v = trimW(v);
    if (v.empty()) return;
    g_keys.push_back(v);
    saveShortcuts();
    keyBarRefresh();
    appendRun(L"— 已新增快捷命令：" + v + L" —\r\n", C_SYS);
}

static void keyEdit(int i) {
    if (i < 0 || i >= (int)g_keys.size()) return;
    std::wstring v = g_keys[i];
    if (!showInputBox(L"修改快捷命令", L"命令内容（留空则删除该条）：", v)) return;
    v = trimW(v);
    if (v.empty()) {
        g_keys.erase(g_keys.begin() + i);
        appendRun(L"— 已删除该快捷命令 —\r\n", C_SYS);
    } else {
        g_keys[i] = v;
        appendRun(L"— 快捷命令已改为：" + v + L" —\r\n", C_SYS);
    }
    saveShortcuts();
    keyBarRefresh();
}

static void keyDel(int i) {
    if (i < 0 || i >= (int)g_keys.size()) return;
    std::wstring old = g_keys[i];
    g_keys.erase(g_keys.begin() + i);
    saveShortcuts();
    keyBarRefresh();
    appendRun(L"— 已删除快捷命令：" + old + L" —\r\n", C_SYS);
}

// 读文本文件：优先 UTF-8，非法则按 ANSI(GBK)
static std::wstring readTextAuto(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    DWORD sz = GetFileSize(f, nullptr);
    std::string buf;
    if (sz > 0 && sz < (16u << 20)) {
        buf.resize(sz);
        DWORD rd = 0;
        ReadFile(f, &buf[0], sz, &rd, nullptr);
        buf.resize(rd);
    }
    CloseHandle(f);
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        buf = buf.substr(3);
    if (buf.empty()) return L"";

    int wl = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf.data(), (int)buf.size(), nullptr, 0);
    if (wl > 0) {
        std::wstring w(wl, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), &w[0], wl);
        return w;
    }
    int al = MultiByteToWideChar(CP_ACP, 0, buf.data(), (int)buf.size(), nullptr, 0);
    if (al <= 0) return L"";
    std::wstring w(al, L'\0');
    MultiByteToWideChar(CP_ACP, 0, buf.data(), (int)buf.size(), &w[0], al);
    return w;
}

static void keyExport() {
    if (g_keyTextMode) keyTextCollect();          // 文本模式下先收集改动再导出
    wchar_t path[MAX_PATH] = L"shortcuts.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"文本 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"导出快捷命令";
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    FILE* f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) { appendRun(L"— 无法写入该文件 —\r\n", C_ERR); return; }
    for (auto& s : g_keys) fwprintf(f, L"%ls\n", s.c_str());   // ccs=UTF-8 自带 BOM
    fclose(f);
    appendRun(L"— 已导出 " + std::to_wstring(g_keys.size()) + L" 条快捷命令 —\r\n", C_SYS);
}

static void keyImport() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"文本 (*.txt;*.ini)\0*.txt;*.ini\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"导入快捷命令（每行一条，追加到现有列表）";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring txt = readTextAuto(path);
    if (txt.empty()) { appendRun(L"— 文件为空或读取失败 —\r\n", C_ERR); return; }

    int added = 0;
    size_t start = 0;
    while (start <= txt.size()) {
        size_t nl = txt.find(L'\n', start);
        std::wstring line = (nl == std::wstring::npos) ? txt.substr(start)
                                                       : txt.substr(start, nl - start);
        start = (nl == std::wstring::npos) ? txt.size() + 1 : nl + 1;
        line = trimW(line);
        if (line.empty() || line[0] == L'#') continue;
        // 兼容 "F1=xxx" 这种从 shortcuts.ini 直接拷过来的行
        if (line.size() > 2 && (line[0] == L'F' || line[0] == L'f')) {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos && eq < 4) {
                line = trimW(line.substr(eq + 1));
                if (line.empty()) continue;
            }
        }
        g_keys.push_back(line);
        added++;
    }
    saveShortcuts();
    keyBarRefresh();
    if (g_keyTextMode) keyTextReload(true);        // 文本模式：把导入结果同步到编辑框
    appendRun(L"— 已导入 " + std::to_wstring(added) + L" 条快捷命令 —\r\n", C_SYS);
}

static void drawKeysBar(const DRAWITEMSTRUCT* ds) {
    RECT rc = ds->rcItem;
    int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
    HDC hdc = ds->hDC;
    if (bw < 30 || bh < 4) {                     // 太窄：只铺底色
        HBRUSH bg = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        return;
    }
    HDC dc  = hdc;
    HBITMAP bmp = nullptr;
    HGDIOBJ obmp = nullptr;
    {                                            // 双缓冲：整帧画完再一次性贴回，消除拖宽时的重叠残影
        dc = CreateCompatibleDC(hdc);
        bmp = CreateCompatibleBitmap(hdc, bw, bh);
        if (dc && bmp) {
            obmp = SelectObject(dc, bmp);
            SetViewportOrgEx(dc, -rc.left, -rc.top, nullptr);
        } else {
            if (bmp) DeleteObject(bmp);
            if (dc) DeleteDC(dc);
            dc = hdc; bmp = nullptr;
        }
    }
    HBRUSH bg = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xDC, 0xDC, 0xDC));
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(dc, g_fontUI);

    HPEN   pn   = CreatePen(PS_SOLID, 1, RGB(0xC6, 0xC6, 0xC6));
    HPEN   pnH  = CreatePen(PS_SOLID, 1, RGB(0x2E, 0x7C, 0xE0));
    HBRUSH brN  = CreateSolidBrush(RGB(0xF7, 0xF8, 0xFA));
    HBRUSH brH  = CreateSolidBrush(RGB(0xE8, 0xF1, 0xFE));
    HGDIOBJ onb = SelectObject(dc, GetStockObject(NULL_BRUSH));

    // 列表区域：底部留给「导入 / 导出」，左边缘留给折叠把手
    int fy    = rc.bottom - kKeyFootH;                    // 底部按钮区上边界
    if (g_keysOn) {                                       // 左边缘一条浅色轨道，折叠把手浮在上面
        RECT rail{rc.left + 1, rc.top + 1, rc.left + kHandleW, rc.bottom - 1};
        HBRUSH rb = CreateSolidBrush(RGB(0xF8, 0xF9, 0xFB));
        FillRect(dc, &rail, rb);
        DeleteObject(rb);
    }
    int lpad  = rc.left + kKeyPad + (g_keysOn ? kHandleW : 0);
    int lpadT = rc.left + kKeyPad;                        // 空提示用
    for (int i = 0; i < (int)g_keys.size() && !g_keyTextMode; i++) {
        int y = rc.top + kKeyPad + i * (kKeyRowH + kKeyGap) - g_keyScroll;
        RECT b{lpad, y, rc.right - kKeyPad, y + kKeyRowH};
        if (b.bottom <= rc.top + 1 || b.bottom > fy) continue;   // 超出可视区/压到底部按钮区的不画
        bool hv = (i == g_keyHover);
        FillRect(dc, &b, hv ? brH : brN);
        HGDIOBJ op2 = SelectObject(dc, hv ? pnH : pn);
        Rectangle(dc, b.left, b.top, b.right, b.bottom);
        SelectObject(dc, op2);
        SetTextColor(dc, hv ? RGB(0x1A, 0x4F, 0xA8) : RGB(0x28, 0x28, 0x28));
        RECT tr = b;
        tr.left += 6;
        tr.right -= 6;
        DrawTextW(dc, g_keys[i].c_str(), -1, &tr,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // ---- 底部：导入 / 导出 ----
    {
        HPEN sep = CreatePen(PS_SOLID, 1, RGB(0xDC, 0xDC, 0xDC));
        HGDIOBJ osp = SelectObject(dc, sep);
        MoveToEx(dc, rc.left + 1, fy, nullptr);
        LineTo(dc, rc.right - 1, fy);
        SelectObject(dc, osp);
        DeleteObject(sep);

        int fbh = 24;
        int fby = fy + (kKeyFootH - fbh) / 2;
        int mid = (rc.left + rc.right) / 2;
        RECT fb1{rc.left + kKeyPad, fby, mid - 3, fby + fbh};
        RECT fb2{mid + 3, fby, rc.right - kKeyPad, fby + fbh};
        for (int k = 0; k < 2; k++) {
            RECT b = k ? fb2 : fb1;
            bool hv = (g_keyFootHover == k + 1);
            FillRect(dc, &b, hv ? brH : brN);
            HGDIOBJ op3 = SelectObject(dc, hv ? pnH : pn);
            Rectangle(dc, b.left, b.top, b.right, b.bottom);
            SelectObject(dc, op3);
            SetTextColor(dc, hv ? RGB(0x1A, 0x4F, 0xA8) : RGB(0x28, 0x28, 0x28));
            DrawTextW(dc, k ? L"导出" : L"导入", -1, &b,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }

    SelectObject(dc, onb);
    DeleteObject(pn);
    DeleteObject(pnH);
    DeleteObject(brN);
    DeleteObject(brH);

    if (g_keys.empty() && !g_keyTextMode) {
        RECT tr{lpadT, rc.top + kKeyPad, rc.right - kKeyPad, rc.top + kKeyPad + 20};
        SetTextColor(dc, RGB(0x9A, 0x9A, 0x9A));
        DrawTextW(dc, L"右键新增", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (g_keyDrag) {   // 拖动中：左边缘高亮，提示当前正在调整宽度
        RECT bar{rc.left, rc.top + 1, rc.left + kResizeW, rc.bottom - 1};
        HBRUSH bb = CreateSolidBrush(RGB(0x2E, 0x7C, 0xE0));
        FillRect(dc, &bar, bb);
        DeleteObject(bb);
    }
    SelectObject(dc, of);

    if (bmp) {                                    // 整帧贴回屏幕
        SetViewportOrgEx(dc, 0, 0, nullptr);
        BitBlt(hdc, rc.left, rc.top, bw, bh, dc, rc.left, rc.top, SRCCOPY);
        SelectObject(dc, obmp);
        DeleteObject(bmp);
        DeleteDC(dc);
    }
}

// 命中测试：y 为客户区坐标，返回快捷命令下标（空白处 -1）
static int keyHit(int y) {
    if (g_hKeysBar) {
        RECT rc;
        GetClientRect(g_hKeysBar, &rc);
        if (y >= rc.bottom - kKeyFootH) return -1;        // 底部按钮区不算命令
    }
    int rel = y - kKeyPad + g_keyScroll;
    if (rel < 0) return -1;
    int slot = rel / (kKeyRowH + kKeyGap);
    int in   = rel % (kKeyRowH + kKeyGap);
    if (in >= kKeyRowH) return -1;
    if (slot < 0 || slot >= (int)g_keys.size()) return -1;
    return slot;
}

// 底部按钮命中：0 无，1 导入，2 导出
static int footHit(HWND h, int x, int y) {
    RECT rc;
    GetClientRect(h, &rc);
    if (y < rc.bottom - kKeyFootH) return 0;
    int mid = (rc.left + rc.right) / 2;
    return (x < mid) ? 1 : 2;
}

static void showKeyMenu(POINT pt, int idx) {
    HMENU m = CreatePopupMenu();
    if (idx >= 0 && idx < (int)g_keys.size()) {
        std::wstring t = g_keys[idx];
        if (t.size() > 24) t = t.substr(0, 24) + L"…";
        mAddMenu(m, MF_STRING | MF_DISABLED, 0, (L"「" + t + L"」").c_str());
        mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        mAddMenu(m, MF_STRING, IDM_KEY_EXEC, L"执行此命令");
        mAddMenu(m, MF_STRING, IDM_KEY_EDIT, L"修改…");
        mAddMenu(m, MF_STRING, IDM_KEY_DEL,  L"删除");
        mAddMenu(m, MF_SEPARATOR, 0, nullptr);
        mAddMenu(m, MF_STRING, IDM_KEY_ADD, L"新增快捷命令…");
    } else {
        mAddMenu(m, MF_STRING, IDM_KEY_ADD, L"新增快捷命令…");
    }
    mAddMenu(m, MF_SEPARATOR, 0, nullptr);
    mAddMenu(m, MF_STRING, IDM_KEY_EXPORT, L"导出快捷命令…");
    mAddMenu(m, MF_STRING, IDM_KEY_IMPORT, L"导入快捷命令…");

    SetForegroundWindow(g_hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                  pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)cmd, 0);
}

static LRESULT CALLBACK KeyProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
        case WM_SETCURSOR: {                       // 左边缘显示左右箭头
            POINT pt; GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            if (pt.x >= 0 && pt.x <= kResizeW) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            break;
        }
        case WM_LBUTTONDOWN: {                     // 左边缘按下 → 开始拖宽度
            if ((short)LOWORD(l) <= kResizeW) {
                g_keyDrag = true;
                g_keyHover = -1;
                SetCapture(h);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_keyDrag) {
                g_keyDrag = false;
                ReleaseCapture();
                layout(g_hwnd);                        // 松手才真正重排终端，只有这一次重绘
                if (g_hView) InvalidateRect(g_hView, nullptr, TRUE);
                RedrawWindow(g_hwnd, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
                saveSettings();
                return 0;
            }
            int fx = footHit(h, (short)LOWORD(l), (short)HIWORD(l));
            if (fx == 1) { keyImport(); return 0; }
            if (fx == 2) { keyExport(); return 0; }
            int i = keyHit((short)HIWORD(l));
            if (i >= 0) runShortcut(i);
            return 0;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT pt;
            if (m == WM_CONTEXTMENU && l != (LPARAM)-1) {
                pt.x = (short)LOWORD(l);
                pt.y = (short)HIWORD(l);
            } else GetCursorPos(&pt);
            POINT cp = pt;
            ScreenToClient(h, &cp);
            g_ctxKey = keyHit(cp.y);
            showKeyMenu(pt, g_ctxKey);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(w);
            g_keyScroll -= (delta / WHEEL_DELTA) * (kKeyRowH + kKeyGap);
            keyBarRefresh();
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_keyDrag) {                       // 拖动：按鼠标在父窗口的 x 反算栏宽
                POINT pt{(short)LOWORD(l), (short)HIWORD(l)};
                ClientToScreen(h, &pt);
                ScreenToClient(g_hwnd, &pt);
                RECT wr;
                GetClientRect(g_hwnd, &wr);
                int sideW = g_sideOn ? 196 : 24;
                int maxW  = wr.right - 16 - sideW - 6 - 200;
                if (maxW > 460) maxW = 460;
                int newW = wr.right - 8 - pt.x;
                if (newW < 90) newW = 90;
                if (maxW >= 90 && newW > maxW) newW = maxW;
                if (newW != g_keysW) {
                    g_keysW = newW;
                    layoutKeysOnly();                  // 拖动中终端保持原尺寸，栏直接盖上去，不重排不闪
                }
                return 0;
            }
            int fx = footHit(h, (short)LOWORD(l), (short)HIWORD(l));
            if (fx != g_keyFootHover) {
                g_keyFootHover = fx;
                InvalidateRect(h, nullptr, TRUE);
            }
            int i = keyHit((short)HIWORD(l));
            if (i != g_keyHover) {
                g_keyHover = i;
                InvalidateRect(h, nullptr, TRUE);
            }
            return 0;
        }
    }
    return DefSubclassProc(h, m, w, l);
}


static void saveLog(HWND hwnd) {
    wchar_t path[MAX_PATH] = L"serial-log.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Log (*.log;*.txt)\0*.log;*.txt\0All (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    FILE* f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) return;
    {
        std::lock_guard<std::mutex> lk(g_textMtx);
        fputws(g_allText.c_str(), f);
    }
    fclose(f);
    appendRun(L"— 日志已保存 —\r\n", C_SYS);
}

// ---------------- 输出框右键菜单 ----------------
static void viewCopySel() {
    if (!g_hView) return;
    CHARRANGE cr{};
    SendMessageW(g_hView, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMax <= cr.cpMin) return;
    std::wstring s = viewRange(g_hView, cr.cpMin, cr.cpMax);
    if (s.empty() || !OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(g)) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(g);
            SetClipboardData(CF_UNICODETEXT, g);
        }
    }
    CloseClipboard();
}

// 剪贴板内容当命令发下去（换行压成空格），和 Ctrl+V 一致
static void viewPasteToPort() {
    if (!OpenClipboard(g_hwnd)) return;
    HANDLE hd = GetClipboardData(CF_UNICODETEXT);
    if (hd) {
        if (auto* txt = (wchar_t*)GlobalLock(hd)) {
            std::wstring s(txt);
            GlobalUnlock(hd);
            for (auto& ch : s) if (ch == L'\r' || ch == L'\n') ch = L' ';
            if (!s.empty()) { termSendBytes(txEncode(s), C_TX, nullptr); lineRecord(s); }
        }
    }
    CloseClipboard();
}

static bool pickLogDir(std::wstring& out) {
    BROWSEINFOW bi{};
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"选择串口日志保存目录（serial.log 写在这里）";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t buf[MAX_PATH] = L"";
    bool ok = SHGetPathFromIDListW(pidl, buf) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) out = buf;
    return ok;
}

// 在输出框上右键弹出：编辑命令 + 编码/时间戳/日志/信息栏 等常用设置
static void showViewMenu(HWND h) {
    HMENU m = CreatePopupMenu();
    HMENU mEnc = CreatePopupMenu();
    mAddMenu(mEnc, MF_STRING | (g_rxEnc == 0 ? MF_CHECKED : 0), IDM_ENC_BASE + 0, L"自动（UTF-8 优先，失败退 GBK）");
    mAddMenu(mEnc, MF_STRING | (g_rxEnc == 1 ? MF_CHECKED : 0), IDM_ENC_BASE + 1, L"UTF-8");
    mAddMenu(mEnc, MF_STRING | (g_rxEnc == 2 ? MF_CHECKED : 0), IDM_ENC_BASE + 2, L"文本 ANSI (GBK)");

    CHARRANGE cr{};
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
    bool hasSel = cr.cpMax > cr.cpMin;

    mAddMenu(m, MF_STRING | (hasSel ? 0 : MF_GRAYED), IDM_CTX_COPY, L"复制\tCtrl+C");
    mAddMenu(m, MF_STRING, IDM_CTX_PASTE, L"粘贴到串口\tCtrl+V");
    mAddMenu(m, MF_STRING, IDM_CTX_ALL, L"全选\tCtrl+A");
    mAddMenu(m, MF_STRING, IDM_VIEW_CLEAR, L"清屏\tCtrl+L");
    mAddMenu(m, MF_SEPARATOR, 0, nullptr);
    mAddMenu(m, MF_POPUP, (UINT_PTR)mEnc, L"串口编码");
    mAddMenu(m, MF_STRING | (g_showTs ? MF_CHECKED : 0), IDM_VIEW_TS, L"显示时间戳");
    mAddMenu(m, MF_STRING | (g_showAgent ? MF_CHECKED : 0), IDM_VIEW_AGENT, L"显示接管命令");
    mAddMenu(m, MF_STRING | (g_localEcho ? MF_CHECKED : 0), IDM_LOCAL_ECHO, L"按键即时显示（本地回显）");
    mAddMenu(m, MF_STRING | (g_sideOn ? MF_CHECKED : 0), IDM_SIDE_TOGGLE, L"显示会话列表");
    mAddMenu(m, MF_STRING | (g_keysOn ? MF_CHECKED : 0), IDM_KEYS_TOGGLE, L"显示快捷命令栏");
    mAddMenu(m, MF_SEPARATOR, 0, nullptr);
    mAddMenu(m, MF_STRING | (g_logSave ? MF_CHECKED : 0), IDM_LOG_SAVE, L"保存串口日志");
    mAddMenu(m, MF_STRING, IDM_LOG_DIR, L"设置日志目录…");
    mAddMenu(m, MF_STRING, IDM_LOG_OPEN, L"打开日志目录");

    POINT pt;
    GetCursorPos(&pt);
    int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN,
                                 pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    if (!id) return;
    if (id == IDM_CTX_COPY)  { viewCopySel(); return; }
    if (id == IDM_CTX_PASTE) { viewPasteToPort(); return; }
    if (id == IDM_CTX_ALL)   { SendMessageW(h, EM_SETSEL, 0, -1); return; }
    SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)id, 0);   // 其余走主窗口统一处理
}

// ---------------- 主窗口 ----------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
#ifdef TERM_TRACE
        // 自测用：模拟输入法把「大字体」留在插入点格式上（不写任何文字）
        case WM_APP + 30: {
            CHARRANGE end{-1, -1};
            SendMessageW(g_hView, EM_EXSETSEL, 0, (LPARAM)&end);
            CHARFORMAT2W cf{};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE;
            cf.yHeight = g_fontPt * 2 * 20;                  // 双倍字号
            lstrcpynW(cf.szFaceName, L"SimSun", LF_FACESIZE);
            SendMessageW(g_hView, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            return 0;
        }
        // 自测用：把插入点当前的字符格式写进 fmt.txt
        case WM_APP + 31: {
            CHARFORMAT2W q{};
            q.cbSize = sizeof(q);
            SendMessageW(g_hView, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&q);
            FILE* f = fopen("fmt.txt", "a");
            if (f) { fprintf(f, "yH=%d face=%ls\n", (int)q.yHeight, q.szFaceName); fclose(f); }
            return 0;
        }
        // 自测用：验证快捷命令在 SSH 会话下能不能发出去。
        // +40 = 按钮/右键「执行此命令」那条路（runShortcut）
        // +41 = 文本模式双击那条路（keyTextRunLine）
        case WM_APP + 40: runShortcut(0); return 0;
        case WM_APP + 41:
            if (g_hKeyText && !g_keys.empty()) {
                SetWindowTextW(g_hKeyText, g_keys[0].c_str());
                keyTextRunLine(0);
            }
            return 0;
#endif
        case WM_CREATE:
            g_hwnd = hwnd;
            createControls(hwnd);
            buildMenu();
            applyDlgStyle();
            layout(hwnd);
            refreshPorts();
            applyFont();
            updateStatus();
            appendRun(L"SerialMate " SM_VERSION_W L" · 「会话 → 新建会话」(Ctrl+N) 选择 串口 / SSH / Telnet / 远程串口。\r\n", C_HINT);
            appendRun(L"— 输出区可直接键盘输入（Enter 执行，↑↓ 调历史）—\r\n\r\n", C_HINT);
            // 开箱即用：按上次的配置建成第一条会话并连上
            {
                auto sp = std::make_unique<Session>();
                sp->id      = g_nextSessId++;
                sp->type    = g_connType;
                sp->port    = g_portName;
                sp->baud    = g_baud;
                sp->remIp   = g_remIp;
                sp->remCom  = g_remCom;
                sp->remBaud = g_remBaud;
                sp->host    = (g_connType == CONN_SSH) ? g_sshHost : g_telHost;
                sp->hostPort= (g_connType == CONN_SSH) ? g_sshPort : g_telPort;
                if (g_connType == CONN_SSH) {
                    sp->sshUser = g_sshUser;
                    sp->sshPass = g_sshPass;
                    sp->sshInfo = g_sshUser + L"@" + g_sshHost + L":" + std::to_wstring(g_sshPort);
                }
                sessionNameOf(sp.get());
                Session* s0 = sp.get();
                g_sess.push_back(std::move(sp));
                g_cur = s0;
                connectSession(s0);
                updateStatus();
            }
            if (g_cur && g_cur->term) g_cur->term->focus();
            else SetFocus(g_hView);
            return 0;

        case WM_SIZE:
            layout(hwnd);
            return 0;

        // 焦点必须始终待在输入区，否则键盘敲下去没人接（点到按钮/状态栏后打字全丢）
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE && !g_dlgWnd) {
                if (g_keyTextMode && g_hKeyText) SetFocus(g_hKeyText);
                else if (g_hView) SetFocus(g_hView);
            }
            return 0;

        case WM_SETFOCUS:
            if (!g_dlgWnd) {
                if (g_keyTextMode && g_hKeyText) SetFocus(g_hKeyText);
                else if (g_hView) SetFocus(g_hView);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (!g_dlgWnd) {
                if (g_keyTextMode && g_hKeyText) SetFocus(g_hKeyText);
                else if (g_hView) SetFocus(g_hView);
            }
            return 0;

        case WM_CHAR:                                  // 兜底：字符落到主窗口也喂给终端
            if (!g_dlgWnd && g_hView && wp >= 32) { termTypeChar((wchar_t)wp); return 0; }
            break;

        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = 900;
            mmi->ptMinTrackSize.y = 560;
            return 0;
        }

        case WM_INITMENUPOPUP:
            syncMenuChecks();
            return 0;

        case WM_APP_DATA: {
            auto* d = (std::string*)lp;
            Session* ss = sessionById((int)wp);
            if (ss && ss == g_cur) {
                showRx(*d);
                updateStatus();
            } else if (ss) {                       // 后台会话：先攒着，切回来再放
                std::lock_guard<std::mutex> lk(ss->mtx);
                ss->pending += *d;
                if (ss->pending.size() > 256 * 1024)
                    ss->pending.erase(0, ss->pending.size() - 192 * 1024);
            }
            delete d;
            return 0;
        }

        case WM_APP_TX: {
            auto* pr = (std::pair<std::string, std::string>*)lp;
            if (g_showAgent && (int)wp != 0 && sessionById((int)wp) != g_cur) { delete pr; return 0; }
            if (g_showAgent) {
                std::wstring who = (pr->first == "agent") ? L"AGENT" : utf8ToWide(pr->first);
                for (auto& c : who) c = towupper(c);
                std::wstring body = bytesToWide((const unsigned char*)pr->second.data(), (int)pr->second.size());
                for (auto& ch : body) if (ch == L'\r' || ch == L'\n') ch = L' ';
                std::wstring t = (g_showTs ? timestamp() : L"") + std::wstring(L"◀ [") + who + L"] ";
                appendRun(t + body + L"\r\n", C_AGENT);
            }
            delete pr;
            return 0;
        }

        case WM_APP_META: {
            auto* s = (std::wstring*)lp;
            appendRun(L"— " + *s + L" —\r\n", C_SYS);
            delete s;
            return 0;
        }

        case WM_APP_SSH_DATA: {
            auto* d = (std::string*)lp;
            Session* ss = sessionById((int)wp);
            if (ss && ss->term) {
                ss->term->write(*d);          // 终端自己保存画面，后台会话也能收到
                ss->rx += (long long)d->size();
                if (ss == g_cur) updateStatus();
            }
            delete d;
            return 0;
        }

        case WM_APP_SSH_LOG: {
            auto* p = (SshLog*)lp;
            Session* ss = sessionById((int)wp);
            if (ss && ss->term) {
                if (p->level == 1) {
                    ss->term->writeText(L"— " + p->text + L" —\r\n");
                } else {
                    ss->term->writeText(L"· " + p->text + L"\r\n");
                }
                if (ss == g_cur) updateStatus();
            }
            delete p;
            return 0;
        }

        case WM_APP_SSH_END: {
            auto* why = (std::wstring*)lp;
            Session* ss = sessionById((int)wp);
            if (ss && ss->term && !why->empty())
                ss->term->writeText(L"— " + *why + L" —\r\n");
            if (ss) {
                stopSsh(ss);
                updateStatus();
                if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
            }
            delete why;
            return 0;
        }

        case WM_APP_DEAD: {
            auto* s = (std::wstring*)lp;
            Session* ds = sessionById((int)wp);
            if (!ds) ds = g_cur;
            if (ds == g_cur) appendRun(L"— " + *s + L" —\r\n", C_ERR);
            delete s;
            closePort(ds);
            updateStatus();
            if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
            return 0;
        }

        case WM_APP_PEER:
            updateStatus();
            return 0;

        case WM_APP_INPUT_RESET:
            // AI 注入前已经给板子发了 Ctrl-U，本地那份「还没回车的输入」一起作废，
            // 否则退格会按旧账本多发字节、上翻历史也会拿到一条根本没执行过的命令
            lineReset();
            return 0;

        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* ms = (MEASUREITEMSTRUCT*)lp;
            if (ms && ms->CtlType == ODT_MENU) { measureMenuItem(ms); return TRUE; }
            break;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* ds = (DRAWITEMSTRUCT*)lp;
            if (ds && ds->CtlType == ODT_MENU) {
                if (ds->itemData) drawMenuItem(ds, (const MenuItemData*)ds->itemData);
                else {
                    HBRUSH b = CreateSolidBrush(g_menuBg);
                    FillRect(ds->hDC, &ds->rcItem, b);
                    DeleteObject(b);
                }
                return TRUE;
            }
            if (wp == IDC_SIDE && ds) { drawSideBar(ds); return TRUE; }
            if (wp == IDC_KEYS && ds) { drawKeysBar(ds); return TRUE; }
            if (wp == IDC_KEYBTN && ds) {                 // 右侧栏折叠把手
                drawHandleBtn(ds);
                return TRUE;
            }
            if (wp == IDC_SIDEBTN && ds) {                // 左侧栏折叠把手（样式与右侧一致）
                drawHandleBtn(ds);
                return TRUE;
            }
            if (wp == IDC_KEYMODE && ds) {                // 顶部：按钮 / 文本 模式切换条
                RECT r = ds->rcItem;
                bool down = (ds->itemState & ODS_SELECTED) != 0;
                HBRUSH pb = CreateSolidBrush(down ? RGB(0xDD, 0xE6, 0xF5) : RGB(0xF2, 0xF4, 0xF8));
                FillRect(ds->hDC, &r, pb);
                DeleteObject(pb);
                HPEN sp = CreatePen(PS_SOLID, 1, RGB(0xD4, 0xDA, 0xE4));
                HGDIOBJ osp = SelectObject(ds->hDC, sp);
                MoveToEx(ds->hDC, r.left, r.bottom - 1, nullptr);
                LineTo(ds->hDC, r.right, r.bottom - 1);
                SelectObject(ds->hDC, osp);
                DeleteObject(sp);
                SetBkMode(ds->hDC, TRANSPARENT);
                SetTextColor(ds->hDC, RGB(0x3A, 0x4A, 0x66));
                wchar_t t[32] = L"";
                GetWindowTextW((HWND)ds->hwndItem, t, 32);
                HFONT ofm = (HFONT)SelectObject(ds->hDC, g_fontUI);
                DrawTextW(ds->hDC, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(ds->hDC, ofm);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);

            if (id == IDC_VIEW && HIWORD(wp) == EN_CHANGE) {
                // 文本变了：我们自己的改动 → 刷新权威副本；外来改动（TSF 上屏等）
                // → 起防抖定时器，等拼写结束后再统一撤掉转发
                dbgLog(L"EN_CHANGE self=%d", g_selfEdit ? 1 : 0);
                if (g_selfEdit) g_expect = viewText();
                else SetTimer(hwnd, IDT_IME, 350, nullptr);
                return 0;
            }

            if (id >= IDC_KEYBASE && id < IDC_KEYBASE + 8) {   // 兼容旧按钮 ID（已改用自绘）
                int i = id - IDC_KEYBASE;
                if (i < (int)g_keys.size()) {
                    termSendBytes(txEncode(g_keys[i]) + "\r", C_TX, nullptr);
                    lineReset();
                }
                if (g_hView) SetFocus(g_hView);
                return 0;
            }
            if (id == IDC_KEYMODE) {                            // 顶部：按钮 <-> 文本
                keyModeApply(!g_keyTextMode);
                return 0;
            }
            if (id == IDM_KEYS_TOGGLE || id == IDC_KEYBTN) {   // 右侧快捷命令栏 折叠/展开
                if (!g_keysOn && g_keyTextMode) keyTextCollect();   // 收起前先把文本改动写回
                g_keysOn = !g_keysOn;
                updateKeysBtn();
                saveSettings();
                syncMenuChecks();
                layout(hwnd);
                return 0;
            }
            if (id == IDM_KEY_EXEC)   { runShortcut(g_ctxKey); return 0; }
            if (id == IDM_KEY_EDIT)   { keyEdit(g_ctxKey);     return 0; }
            if (id == IDM_KEY_DEL)    { keyDel(g_ctxKey);      return 0; }
            if (id == IDM_KEY_ADD)    { keyAdd();              return 0; }
            if (id == IDM_KEY_EXPORT) { keyExport();           return 0; }
            if (id == IDM_KEY_IMPORT) { keyImport();           return 0; }
            if (id >= IDM_ENC_BASE && id <= IDM_ENC_BASE + 2) {
                applyEnc(id - IDM_ENC_BASE, true);
                return 0;
            }
            if (id == IDC_SIDEBTN || id == IDM_SIDE_TOGGLE) {   // 左侧信息栏折叠/展开
                g_sideOn = !g_sideOn;
                saveSettings();
                syncMenuChecks();
                layout(hwnd);
                updateSide();
                if (g_hView) SetFocus(g_hView);
                return 0;
            }
            if (id >= IDM_BAUD_BASE && id < IDM_BAUD_BASE + kBaudCount) {
                g_baudIdx = id - IDM_BAUD_BASE;
                g_baud = kBaits[g_baudIdx];
                Session* t = g_ctxSess ? g_ctxSess : g_cur;
                if (t) {
                    bool wasOn = t->on && t->type == CONN_SERIAL;
                    t->baud = g_baud;
                    if (wasOn) { closePort(t); openPort(t); }   // 换速率重开该会话
                    if (t == g_cur) appendRun(L"— 波特率改为 " + std::to_wstring(g_baud) + L" —\r\n", C_SYS);
                }
                saveSettings();
                syncMenuChecks();
                updateStatus();
                if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
                return 0;
            }
            // ---- 设置：菜单/对话框外观 ----
            if (id >= IDM_SET_MFONT_BASE && id < IDM_SET_MFONT_BASE + kUiPtCount) {
                g_menuPtIdx = id - IDM_SET_MFONT_BASE;
                applyMenuStyle();
                saveSettings();
                syncMenuChecks();
                return 0;
            }
            if (id >= IDM_SET_DFONT_BASE && id < IDM_SET_DFONT_BASE + kUiPtCount) {
                g_dlgPtIdx = id - IDM_SET_DFONT_BASE;
                applyUiFont();
                saveSettings();
                syncMenuChecks();
                return 0;
            }
            if (id == IDM_SET_MFG || id == IDM_SET_MBG || id == IDM_SET_MSEL ||
                id == IDM_SET_DFG || id == IDM_SET_DBG) {
                bool changed = false;
                if (id == IDM_SET_MFG)       changed = pickColor(hwnd, g_menuFg);
                else if (id == IDM_SET_MBG)  changed = pickColor(hwnd, g_menuBg);
                else if (id == IDM_SET_MSEL) changed = pickColor(hwnd, g_menuSel);
                else if (id == IDM_SET_DFG)  changed = pickColor(hwnd, g_dlgFg);
                else                         changed = pickColor(hwnd, g_dlgBg);
                if (changed) {
                    if (id == IDM_SET_DFG || id == IDM_SET_DBG) applyDlgStyle();
                    else applyMenuStyle();
                    saveSettings();
                }
                return 0;
            }
            if (id == IDM_SET_RESET) {
                g_menuPtIdx = 1;
                g_dlgPtIdx  = 1;
                g_menuFg  = RGB(0x24, 0x24, 0x24);
                g_menuBg  = RGB(0xF6, 0xF6, 0xF6);
                g_menuSel = RGB(0x2E, 0x7C, 0xE0);
                g_dlgFg   = RGB(0x24, 0x24, 0x24);
                g_dlgBg   = RGB(0xF2, 0xF3, 0xF5);
                applyMenuStyle();
                applyUiFont();
                applyDlgStyle();
                saveSettings();
                syncMenuChecks();
                return 0;
            }
            switch (id) {
                case IDM_PORT_OPEN:                 // 新建会话（原会话保留）
                    newSessionDialog();
                    if (g_hView) SetFocus(g_hView);   // 对话框关掉后把焦点还给输入区
                    return 0;
                case IDM_SESSION_RECONN:
                    if (g_cur) { closePort(g_cur); connectSession(g_cur); }
                    updateStatus();
                    syncMenuChecks();
                    return 0;
                case IDM_PORT_CLOSE: {
                    Session* t = g_ctxSess ? g_ctxSess : g_cur;
                    closePort(t);
                    syncMenuChecks();
                    updateStatus();
                    if (g_hSide) InvalidateRect(g_hSide, nullptr, TRUE);
                    return 0;
                }
                case IDM_SESS_CLOSE: {
                    Session* t = g_ctxSess ? g_ctxSess : g_cur;
                    closeSession(t);
                    return 0;
                }
                case IDM_SIDE_CONN: {   // 左侧栏右键：连接该会话
                    Session* t = g_ctxSess ? g_ctxSess : g_cur;
                    if (t && !t->on) {
                        if (t != g_cur) switchSession(t);
                        connectSession(t);
                        saveSettings();
                        syncMenuChecks();
                        updateStatus();
                    }
                    return 0;
                }
                case IDM_SESSION_EXIT: DestroyWindow(hwnd); return 0;
                case IDM_PORT_REFRESH:
                    refreshPorts();
                    appendRun(L"— 端口列表已刷新 —\r\n", C_SYS);
                    return 0;
                case IDM_TOOL_STTY:
                    g_sttyAdapt = !g_sttyAdapt;
                    saveSettings();
                    syncMenuChecks();
                    if (g_sttyAdapt && curOn() && g_cur->type != CONN_SSH)
                        termSendBytes("\x15shopt -s checkwinsize 2>/dev/null; stty cols 400 rows 100 2>/dev/null; COLUMNS=400; LINES=100; export COLUMNS LINES\r", C_TX, nullptr);
                    return 0;
                case IDM_PORT_DTR:
                    g_dtr = !g_dtr;
                    if (curOn() && !g_cur->isRemote) g_cur->ser.setDtr(g_dtr);
                    saveSettings();
                    syncMenuChecks();
                    return 0;
                case IDM_PORT_RTS:
                    g_rts = !g_rts;
                    if (curOn() && !g_cur->isRemote) g_cur->ser.setRts(g_rts);
                    saveSettings();
                    syncMenuChecks();
                    return 0;
                case IDM_PORT_SERVE:
                    g_serve = !g_serve;
                    if (g_serve) { startServer(); appendRun(L"— 接管服务已启用 127.0.0.1:7000 —\r\n", C_SYS); }
                    else { stopServer(); appendRun(L"— 接管服务已停止 —\r\n", C_SYS); }
                    saveSettings();
                    syncMenuChecks();
                    updateStatus();
                    return 0;
                case IDM_VIEW_BIGGER:
                    if (g_fontPt < 32) { g_fontPt++; applyFont(); saveSettings(); }
                    return 0;
                case IDM_VIEW_SMALLER:
                    if (g_fontPt > 8) { g_fontPt--; applyFont(); saveSettings(); }
                    return 0;
                case IDM_VIEW_RESET:
                    g_fontPt = 12;
                    applyFont();
                    saveSettings();
                    return 0;
                case IDM_VIEW_TS:
                    g_showTs = !g_showTs;
                    saveSettings();
                    syncMenuChecks();
                    return 0;
                case IDM_VIEW_AGENT:
                    g_showAgent = !g_showAgent;
                    saveSettings();
                    syncMenuChecks();
                    return 0;
                case IDM_VIEW_CLEAR:
                    termClearAll();
                    return 0;
                case IDM_VIEW_SAVE:
                    saveLog(hwnd);
                    return 0;
                case IDM_LOCAL_ECHO:
                    g_localEcho = !g_localEcho;
                    saveSettings();
                    appendRun(g_localEcho ? L"— 按键即时显示：开（本地先画，回显到达时丢弃） —\r\n"
                                          : L"— 按键即时显示：关（完全靠板子回显） —\r\n", C_SYS);
                    if (g_hView) SetFocus(g_hView);
                    return 0;
                case IDM_LOG_SAVE:
                    g_logSave = !g_logSave;
                    if (!g_logSave) closeLog();
                    saveSettings();
                    syncMenuChecks();
                    updateSide();
                    appendRun(g_logSave ? L"— 串口日志保存：开 —\r\n" : L"— 串口日志保存：关 —\r\n", C_SYS);
                    return 0;
                case IDM_LOG_DIR: {
                    std::wstring p;
                    if (pickLogDir(p)) {
                        g_logDir = p;
                        closeLog();
                        saveSettings();
                        updateSide();
                        appendRun(L"— 日志目录改为 " + p + L" —\r\n", C_SYS);
                    }
                    if (g_hView) SetFocus(g_hView);
                    return 0;
                }
                case IDM_LOG_OPEN: {
                    std::wstring d = logDirPath();
                    CreateDirectoryW(d.c_str(), nullptr);
                    ShellExecuteW(nullptr, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    if (g_hView) SetFocus(g_hView);
                    return 0;
                }
                case IDM_HELP_ABOUT:
                    MessageBoxW(hwnd,
                                L"SerialMate " SM_VERSION_W L"\n"
                                L"编译于 " SM_BUILD_W L"\n\n"
                                L"· 独占串口，内置接管服务 127.0.0.1:7000\n"
                                L"· 在输出区直接输入；↑↓ 调历史，Ctrl+C 中断\n"
                                L"· 视图菜单可调字号；配置存 serialmate.ini\n\n"
                                L"协议：newline JSON + base64 (in / out / tx / meta)\n"
                                L"命令行：serial_gui.exe --version 查看版本",
                                L"关于 SerialMate " SM_VERSION_W, MB_OK | MB_ICONINFORMATION);
                    return 0;
            }
            break;
        }

        case WM_TIMER:
            if (wp == IDT_IME) {
                KillTimer(hwnd, IDT_IME);
                bool composing = imeComposing();
                dbgLog(L"IDT_IME composing=%d", composing ? 1 : 0);
                if (composing) SetTimer(hwnd, IDT_IME, 300, nullptr);        // 还在拼写，继续等
                else syncForeignText();
                return 0;
            }
            if (wp == IDT_STATUS) {
                KillTimer(hwnd, IDT_STATUS);
                g_stDirty = false;
                g_stTick  = 0;              // 让下面这次一定刷进去
                updateStatus();
                return 0;
            }
            if (wp == IDT_STTY) {
                KillTimer(hwnd, IDT_STTY);
                // 让板子的 readline 知道终端很宽：超宽命令不会自己折行，
                // 避免它按 80 列重绘而与我们不换行的显示错位
                if (g_sttyAdapt && curOn() && g_cur->type != CONN_SSH)
                    termSendBytes("\x15shopt -s checkwinsize 2>/dev/null; stty cols 400 rows 100 2>/dev/null; COLUMNS=400; LINES=100; export COLUMNS LINES\r", C_TX, nullptr);
                return 0;
            }
            break;

        case WM_DESTROY:
            if (g_keyTextMode) keyTextCollect();     // 文本模式下的改动也落盘
            saveSettings();
            for (auto& sp : g_sess) closePort(sp.get());
            stopServer();
            if (g_log) { fclose(g_log); g_log = nullptr; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --version / -v：打印版本后直接退出，不建窗口。
// 这是个 -mwindows 进程，自己没有控制台，要 AttachConsole 挂到调用它的 cmd/bash 才能
// 看到输出；挂不上（比如双击带参数的快捷方式）就退化成弹窗，别让人以为程序没反应。
static bool printVersionIfAsked(LPWSTR cmdLine) {
    if (!cmdLine) return false;
    std::wstring a = cmdLine;
    while (!a.empty() && iswspace(a.front())) a.erase(a.begin());
    while (!a.empty() && iswspace(a.back()))   a.pop_back();
    if (a != L"--version" && a != L"-v" && a != L"-V") return false;

    std::string out = "SerialMate " SM_VERSION " (build " SM_BUILD_A ")\n";
    bool shown = false;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD  n = 0;
        if (h && h != INVALID_HANDLE_VALUE) {
            WriteFile(h, out.c_str(), (DWORD)out.size(), &n, nullptr);
            shown = (n > 0);
        }
        FreeConsole();
    }
    if (!shown)
        MessageBoxW(nullptr, (L"SerialMate " SM_VERSION_W L"\n编译于 " SM_BUILD_W),
                    L"版本", MB_OK | MB_ICONINFORMATION);
    return true;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int) {
    if (printVersionIfAsked(cmdLine)) return 0;
    g_inst = inst;
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // TSF 组合状态查询要用 COM
    loadSettings();
    loadShortcuts();

    g_fontUI = makeUiFont(g_dlgPtIdx);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SerialMateWnd";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    registerTermClass(inst);

    WNDCLASSEXW dc{};
    dc.cbSize = sizeof(dc);
    dc.lpfnWndProc = DlgProc;
    dc.hInstance = inst;
    dc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    dc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    dc.lpszClassName = L"SerialMateDlg";
    dc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&dc);

    HWND hwnd = CreateWindowExW(0, L"SerialMateWnd", L"SerialMate " SM_VERSION_W L" · AI串口",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1020, 700,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 快捷键表
    ACCEL acc[8];
    acc[0] = {FVIRTKEY, VK_F5, IDM_PORT_REFRESH};
    acc[1] = {FVIRTKEY | FCONTROL, 'N', IDM_PORT_OPEN};
    acc[2] = {FVIRTKEY | FCONTROL, 'R', IDM_SESSION_RECONN};
    acc[3] = {FVIRTKEY | FCONTROL, 'D', IDM_PORT_CLOSE};
    acc[4] = {FVIRTKEY | FCONTROL, VK_OEM_PLUS, IDM_VIEW_BIGGER};
    acc[5] = {FVIRTKEY | FCONTROL, VK_OEM_MINUS, IDM_VIEW_SMALLER};
    acc[6] = {FVIRTKEY | FCONTROL, 'L', IDM_VIEW_CLEAR};
    acc[7] = {FVIRTKEY | FCONTROL, 'B', IDM_SIDE_TOGGLE};
    HACCEL hacc = CreateAcceleratorTableW(acc, 7);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (hacc && TranslateAcceleratorW(hwnd, hacc, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
