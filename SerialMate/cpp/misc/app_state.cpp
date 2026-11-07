// app_state.cpp - 全局状态 / 配置读写(ini) / 编码转换 / 日志落盘

#include "sm.h"


// ---------------- 全局状态（声明见 sm.h） ----------------
HINSTANCE g_inst;
HWND g_hwnd, g_hView, g_hStatus;
HWND g_hSide = nullptr, g_hSideBtn = nullptr;   // 左侧连接信息栏 + 折叠按钮
bool g_sideOn = true;
HWND g_hKeysBar = nullptr;                      // 右侧快捷命令栏（自绘）
HWND g_hKeysBtn = nullptr;                      // 右侧栏折叠/展开按钮
HWND g_hKeyMode = nullptr;                      // 右侧栏顶部：按钮/文本 切换
HWND g_hKeyText = nullptr;                      // 文本模式：多行编辑框
bool g_keyTextMode = false;                     // true=文本模式，false=按钮模式
bool g_keysOn = true;
int  g_keysW  = 150;                            // 右侧栏宽度（可拖动调整）
bool g_keyDrag = false;                         // 正在拖动改变宽度
int  g_keyScroll = 0;                           // 快捷命令栏滚动偏移（像素）
int  g_keyHover  = -1;
int  g_keyFootHover = 0;                        // 底部按钮悬停：0 无 1 导入 2 导出
int  g_ctxKey    = -1;                          // 右键命中的快捷命令索引
bool         g_ibMode = false;
std::wstring g_ibPrompt, g_ibText;
HWND         g_dcIbLabel = nullptr, g_dcIbEdit = nullptr;
HFONT g_fontUI;
HMENU g_menu, g_menuSess, g_menuTool, g_menuSet;
HWND  g_dlgWnd = nullptr;
bool g_termReady = false;      // 是否是 RichEdit（支持着色）
ConnType g_connType = CONN_SERIAL;
std::wstring g_remIp  = L"192.168.0.110";
std::wstring g_remCom = L"COM3";
int          g_remBaud = 115200;
std::wstring g_sshHost = L"192.168.0.110";
int          g_sshPort = 22;
std::wstring g_sshUser = L"xzs";
std::wstring g_sshPass;
std::wstring g_telHost = L"192.168.0.110";
int          g_telPort = 23;
std::vector<std::unique_ptr<Session>> g_sess;
Session* g_cur = nullptr;       // 当前显示中的会话
Session* g_hubSess = nullptr;   // 提供接管服务的本地串口会话（接管服务写数据要写它，
int      g_nextSessId = 1;
std::mutex g_wrMtx;             // 串口写锁（读不持锁）
SOCKET g_srv = INVALID_SOCKET;
std::atomic<bool> g_srvRun{false};
std::thread g_srvThread;
std::atomic<int> g_peerCount{0};
std::wstring g_line;                 // 当前输入行
std::atomic<int> g_pendingKeys{0};
std::vector<unsigned char> g_lineLen;// g_line 每个字符实际发下去的字节数（汉字 3/2 字节，
wchar_t g_hiSur = 0;                 // 攒着的高半代理，等低半部到齐再一起下发
std::vector<std::wstring> g_hist;    // 历史命令
int g_histPos = -1;
std::wstring g_histTmp;
wchar_t g_swallow = 0;               // 已由 KEYDOWN 处理，吞掉随之而来的 WM_CHAR
std::wstring g_imeEcho;              // 已转发出去的上屏内容，用来吞掉随后重复的 WM_IME_CHAR
long g_imeBase = -1;                 // 输入法开始组合时的文本长度（组合串会被内联插在它之后）
int g_baud = 115200;
int g_baudIdx = 4;
std::wstring g_portName = L"COM3";
int g_fontPt = 12;
bool g_showTs = true;
bool g_tsNeed = true;      // 下一段输出落在新行行首，需要补时间戳
int      g_menuPtIdx = 1;   // 菜单字体：kUiPts 下标
int      g_dlgPtIdx  = 1;   // 对话框字体：kUiPts 下标
COLORREF g_menuFg   = RGB(0x24, 0x24, 0x24);
COLORREF g_menuBg   = RGB(0xF6, 0xF6, 0xF6);
COLORREF g_menuSel  = RGB(0x2E, 0x7C, 0xE0);
COLORREF g_dlgFg    = RGB(0x24, 0x24, 0x24);
COLORREF g_dlgBg    = RGB(0xF2, 0xF3, 0xF5);
HFONT    g_fontMenu = nullptr;
int      g_menuRowH = 26;
HBRUSH   g_brushMenu = nullptr, g_brushDlg = nullptr;
bool g_showAgent = true;
bool g_sttyAdapt = true;   // 打开会话后发 stty 让板子知道终端很宽（长命令不错位）
bool g_serve = true;
DevState g_devState = DEV_UNKNOWN;   // 串口对端在线状态（打开后探测才知道）
long long g_probeRx = 0;
bool g_localEcho = true;   // 按键本地先显示，不等板子回显（关掉则完全靠回显）
bool g_dtr = false, g_rts = false;
int g_rxEnc = 0;
std::wstring g_allText;
std::mutex g_textMtx;
FILE* g_log = nullptr;
bool  g_logSave = true;              // 是否落盘保存串口日志
std::wstring g_logDir;               // 空 = exe 目录下 logs
std::vector<std::wstring> g_keys;    // 快捷命令（可增删改、导入导出）
FILE* g_dbg = nullptr;
std::wstring g_logDirOpen;            // 当前已打开的日志目录
std::string  g_lb;                 // 当前行原始字节
size_t       g_lc      = 0;        // 光标在本行中的字节偏移
long         g_lcp     = 0;        // 当前行文本在控件中的起始字符位置
std::wstring g_lshow;              // 当前行已渲染到控件里的渲染串（含时间戳/折行）
std::wstring g_curTs;              // 当前行的时间戳（缓存，避免渲染串每次都变）
int          g_termCols = 0;       // 终端可用列数（按字体实测，0=未测）
int          g_esc     = 0;        // 0=普通 1=ESC 2=CSI 3=OSC 4=两字节转义
std::wstring g_csi;                // CSI 参数缓存
COLORREF     g_tcolor  = C_RX;     // 当前输出颜色
int          g_bsPend  = 0;        // 本地已删、等待板子回显的 0x08 个数（吞掉避免重复删）
int          g_bsSkip  = 0;        // 0=正常 1=刚吞掉 0x08（可能是 "\b \b" 前半） 2=已吞空格
std::string  g_echoQ;
DWORD        g_echoStamp = 0;
std::wstring g_expect;               // 控件中应有的文本（我们自己维护）
bool g_selfEdit = false;             // 我们自己正在改控件文本
DWORD g_foreignGuard = 0;            // 换会话后的一小段时间内不把整屏变化当外来输入
COLORREF     g_appliedColor = 0xFFFFFFFF;  // 插入点当前生效的颜色
long         g_colorEnd     = -1;          // 该颜色文本的末尾位置（插入点在此可复用，不必重设）
int g_sideHover  = -1;
int g_sideScroll = 0;
Session* g_ctxSess = nullptr;      // 右键菜单作用的会话
std::wstring g_sideSig;
bool         g_sideSigOn = false;
DWORD g_stTick  = 0;
bool  g_stDirty = false;
std::vector<std::shared_ptr<Peer>> g_peers;
std::mutex g_peersMtx;
std::vector<MenuItemData*> g_menuItems;   // 进程生命周期，不释放
bool     g_dlgDone = false;
bool     g_dlgOK = false;
ConnType g_dlgType = CONN_SERIAL;
std::vector<HWND> g_pg[4];
HWND g_dcPort, g_dcBaud, g_dcMServe, g_dcMDirect, g_dcDtr, g_dcRts;
HWND g_dcSshHost, g_dcSshPort, g_dcSshUser, g_dcSshPass;
HWND g_dcTelHost, g_dcTelPort;
HWND g_dcRip, g_dcRcom, g_dcRbaud;
double g_dlgScale = 1.0;

std::wstring exeDir() {
    wchar_t p[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    size_t k = s.find_last_of(L'\\');
    return (k == std::wstring::npos) ? L"." : s.substr(0, k);
}

std::wstring timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[40];
    swprintf_s(b, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    return b;
}

int baudIndexOf(int baud) {
    for (int i = 0; i < kBaudCount; i++) if (kBaits[i] == baud) return i;
    return 4;
}


// ---------------- 显示编码 ----------------
// 0=自动（UTF-8 优先，不合法就退到 GBK）  1=UTF-8  2=GBK(文本/ANSI)
// 说明：切换后 RX 显示、日志存档、以及中文/其它非 ASCII 字符的**发送**都走同一套码位，
// 这样“屏幕上看到的”和“板子实际收到的”永远一致（设备是 GBK 就该发 GBK，反之 UTF-8）。

// GBK 解码：末尾落单的首字节（半个汉字）先不显示，等下一个字节补齐

std::wstring gbkToWide(const std::string& s) {
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

std::wstring rxDecode(const std::string& s) {
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

std::string txEncode(const std::wstring& w) {
    if (w.empty()) return "";
    // SSH 走的是标准终端协议，编码固定 UTF-8，不吃串口那边的 GBK 设置
    if (g_cur && isTermSess(g_cur)) return wideToUtf8(w);
    if (g_rxEnc == 2) {
        int n = WideCharToMultiByte(936, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return "";
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(936, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }
    return wideToUtf8(w);
}

const wchar_t* encName() {
    return (g_rxEnc == 1) ? L"UTF-8" : (g_rxEnc == 2) ? L"文本 ANSI/GBK" : L"自动";
}


// ---------------- 配置读写 ----------------


void loadSettings() {
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
    if (g_keysW < kKeyMinW || g_keysW > kKeyMaxW) g_keysW = 150;
}

void saveSettings() {
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

void dbgLog(const wchar_t* fmt, ...) {
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

std::wstring logDirPath() {
    return g_logDir.empty() ? (exeDir() + L"\\logs") : g_logDir;
}

void closeLog() {
    if (g_log) { fclose(g_log); g_log = nullptr; }
    g_logDirOpen.clear();
}

void ensureLog() {
    std::wstring dir = logDirPath();
    if (g_log && dir == g_logDirOpen) return;
    closeLog();
    CreateDirectoryW(dir.c_str(), nullptr);
    g_log = _wfopen((dir + L"\\serial.log").c_str(), L"a, ccs=UTF-8");
    if (g_log) g_logDirOpen = dir;
}

void logLine(const wchar_t* dir, const std::string& bytes) {
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

// —— 本地回显 ——
// 串口链路是「我们发→板子收→板子回显→我们收」，一来一回几十毫秒（USB 转串口
// 芯片的 latency timer 占大头）。完全靠板子回显，敲键就会慢半拍，退格更明显。
// 所以按键先在本地画出来（零延迟），板子随后把同样的字节回显回来时再丢掉。
// g_echoQ 存「本地已画、还没被回显确认」的原始字节，按**内容**逐字节比对：
// 只认一模一样的字节，一旦对不上（板子关着 echo、或者已经开始打命令输出），
// 立刻清空队列按正常输出处理——早先用计数的方式在 `stty -echo` 下会把
// `/home/root` 的前几个字节当成回显吃掉，屏幕上就成 "me/root"。



// —— 终端自绘内容的权威副本 ——
// 新版输入法（搜狗/微软拼音走 TSF）会把上屏结果**直接**写进 RichEdit 文档，
// 不经过 WM_IME_CHAR / WM_IME_COMPOSITION：字出现在屏上、串口却一无所知，
// 退格自然也删不掉（这就是"中文删不掉"的另一条路径）。
// 办法：g_expect 记录"我们认为控件里应该有的文本"；外来改动（EN_CHANGE）做
// 前后缀 diff，把外来内容撤掉、转发给串口。TSF 拼写过程中的组合串不算外来，
// 用防抖定时器 + TSF 组合状态查询区分（见 syncForeignText）。

// 我们自己的文本改动都要包一层（EN_CHANGE 里据此区分内外）

Session* sessionById(int id) {
    for (auto& s : g_sess) if (s->id == id) return s.get();
    return nullptr;
}


// 侧栏显示名：串口名 / 远端 IP:串口 / 主机

void sessionNameOf(Session* s) {
    if (!s) return;
    if (s->type == CONN_REMOTE)       s->name = s->remIp + L":" + s->remCom;
    else if (s->type == CONN_SSH)     s->name = s->host.empty() ? s->remIp : s->host;
    else if (s->type == CONN_TELNET)  s->name = s->host.empty() ? s->remIp : s->host;
    else                              s->name = s->port;
}


// ---- 终端画面存取（RTF，保留颜色）----

std::wstring trimW(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) a++;
    while (b > a && (s[b-1] == L' ' || s[b-1] == L'\t' || s[b-1] == L'\r' || s[b-1] == L'\n')) b--;
    return s.substr(a, b - a);
}

bool curOn() { return g_cur && g_cur->on; }

bool isChecked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }

int  comboIndex(HWND h) { return (int)SendMessageW(h, CB_GETCURSEL, 0, 0); }

std::wstring iniPath() { return exeDir() + L"\\serialmate.ini"; }
