// sm.h - SerialMate 内部接口头
// 这里有且只有三类东西：模块间共享的类型、全局状态、内部函数声明。
// 任何 .cpp 只 include 它，不要互相 include。
#pragma once

#include "common.h"      // windows / winsock / string / vector
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
#define IDT_CMD              4      // 一行多条命令（; 分隔）之间的发送间隔

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

// ---------------- 类型 ----------------
enum ConnType { CONN_SERIAL = 0, CONN_SSH, CONN_TELNET, CONN_REMOTE };

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

// 终端型会话（SSH / Telnet）：画面在 Terminal 控件里，不走 RichEdit
inline bool isTermSess(const Session* s) {
    return s && (s->type == CONN_SSH || s->type == CONN_TELNET);
}

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

struct MenuItemData {
    std::wstring text;
    bool popup = false;
    bool top   = false;      // 菜单栏顶层项（画法不同：无勾选列、无箭头）
};

struct SshLog {
    std::wstring text;
    int level;
};

// ---------------- 全局状态（定义在 app_state.cpp） ----------------
extern HINSTANCE g_inst;
extern HWND g_hwnd, g_hView, g_hStatus;
extern HWND g_hSide;   // 左侧连接信息栏 + 折叠按钮
extern HWND g_hSideBtn;
extern bool g_sideOn;
extern HWND g_hKeysBar;   // 右侧快捷命令栏（自绘）
extern HWND g_hKeysBtn;   // 右侧栏折叠/展开按钮
extern HWND g_hKeyMode;   // 右侧栏顶部：按钮/文本 切换
extern HWND g_hKeyText;   // 文本模式：多行编辑框
extern bool g_keyTextMode;   // true=文本模式，false=按钮模式
extern bool g_keysOn;
extern int g_keysW;   // 右侧栏宽度（可拖动调整）
extern bool g_keyDrag;   // 正在拖动改变宽度
extern int g_keyScroll;   // 快捷命令栏滚动偏移（像素）
extern int g_keyHover;
extern int g_keyFootHover;   // 底部按钮悬停：0 无 1 导入 2 导出
extern int g_ctxKey;   // 右键命中的快捷命令索引
extern bool g_ibMode;
extern std::wstring g_ibPrompt, g_ibText;
extern HWND g_dcIbLabel;
extern HWND g_dcIbEdit;
extern HFONT g_fontUI;
extern HMENU g_menu, g_menuSess, g_menuTool, g_menuSet;
extern HWND g_dlgWnd;
extern bool g_termReady;   // 是否是 RichEdit（支持着色）
extern ConnType g_connType;
extern std::wstring g_remIp;
extern std::wstring g_remCom;
extern int g_remBaud;
extern std::wstring g_sshHost;
extern int g_sshPort;
extern std::wstring g_sshUser;
extern std::wstring g_sshPass;
extern std::wstring g_telHost;
extern int g_telPort;
extern std::vector<std::unique_ptr<Session>> g_sess;
extern Session* g_cur;   // 当前显示中的会话
extern Session* g_hubSess;   // 提供接管服务的本地串口会话（接管服务写数据要写它，
extern int g_nextSessId;
extern std::mutex g_wrMtx;   // 串口写锁（读不持锁）
extern SOCKET g_srv;
extern std::atomic<bool> g_srvRun;
extern std::thread g_srvThread;
extern std::atomic<int> g_peerCount;
extern std::wstring g_line;   // 当前输入行
extern std::atomic<int> g_pendingKeys;
extern std::vector<unsigned char> g_lineLen;   // g_line 每个字符实际发下去的字节数（汉字 3/2 字节，
extern wchar_t g_hiSur;   // 攒着的高半代理，等低半部到齐再一起下发
extern std::vector<std::wstring> g_hist;   // 历史命令
extern int g_histPos;
extern std::wstring g_histTmp;
extern wchar_t g_swallow;   // 已由 KEYDOWN 处理，吞掉随之而来的 WM_CHAR
extern std::wstring g_imeEcho;   // 已转发出去的上屏内容，用来吞掉随后重复的 WM_IME_CHAR
extern long g_imeBase;   // 输入法开始组合时的文本长度（组合串会被内联插在它之后）
extern int g_baud;
extern int g_baudIdx;
extern std::wstring g_portName;
extern int g_fontPt;
extern bool g_showTs;
extern bool g_tsNeed;   // 下一段输出落在新行行首，需要补时间戳
extern int g_menuPtIdx;   // 菜单字体：kUiPts 下标
extern int g_dlgPtIdx;   // 对话框字体：kUiPts 下标
extern COLORREF g_menuFg;
extern COLORREF g_menuBg;
extern COLORREF g_menuSel;
extern COLORREF g_dlgFg;
extern COLORREF g_dlgBg;
extern HFONT g_fontMenu;
extern int g_menuRowH;
extern HBRUSH g_brushMenu;
extern HBRUSH g_brushDlg;
extern bool g_showAgent;
extern bool g_sttyAdapt;   // 打开会话后发 stty 让板子知道终端很宽（长命令不错位）
extern bool g_serve;
extern bool g_localEcho;   // 按键本地先显示，不等板子回显（关掉则完全靠回显）
extern bool g_dtr;
extern bool g_rts;
extern int g_rxEnc;
extern std::wstring g_allText;
extern std::mutex g_textMtx;
extern FILE* g_log;
extern bool g_logSave;   // 是否落盘保存串口日志
extern std::wstring g_logDir;   // 空 = exe 目录下 logs
extern std::vector<std::wstring> g_keys;   // 快捷命令（可增删改、导入导出）
extern FILE* g_dbg;
extern std::wstring g_logDirOpen;   // 当前已打开的日志目录
extern std::string g_lb;   // 当前行原始字节
extern size_t g_lc;   // 光标在本行中的字节偏移
extern long g_lcp;   // 当前行文本在控件中的起始字符位置
extern std::wstring g_lshow;   // 当前行已渲染出来的宽字符（用于整行替换）
extern int g_esc;   // 0=普通 1=ESC 2=CSI 3=OSC 4=两字节转义
extern std::wstring g_csi;   // CSI 参数缓存
extern COLORREF g_tcolor;   // 当前输出颜色
extern int g_bsPend;   // 本地已删、等待板子回显的 0x08 个数（吞掉避免重复删）
extern int g_bsSkip;   // 0=正常 1=刚吞掉 0x08（可能是 "\b \b" 前半） 2=已吞空格
extern std::string g_echoQ;
extern DWORD g_echoStamp;
extern std::wstring g_expect;   // 控件中应有的文本（我们自己维护）
extern bool g_selfEdit;   // 我们自己正在改控件文本
extern DWORD g_foreignGuard;   // 换会话后的一小段时间内不把整屏变化当外来输入
extern COLORREF g_appliedColor;   // 插入点当前生效的颜色
extern long g_colorEnd;   // 该颜色文本的末尾位置（插入点在此可复用，不必重设）
extern int g_sideHover;
extern int g_sideScroll;
extern Session* g_ctxSess;   // 右键菜单作用的会话
extern std::wstring g_sideSig;
extern bool g_sideSigOn;
extern DWORD g_stTick;
extern bool g_stDirty;
extern std::vector<std::shared_ptr<Peer>> g_peers;
extern std::mutex g_peersMtx;
extern std::vector<MenuItemData*> g_menuItems;   // 进程生命周期，不释放
extern bool g_dlgDone;
extern bool g_dlgOK;
extern ConnType g_dlgType;
extern std::vector<HWND> g_pg[4];
extern HWND g_dcPort, g_dcBaud, g_dcMServe, g_dcMDirect, g_dcDtr, g_dcRts;
extern HWND g_dcSshHost, g_dcSshPort, g_dcSshUser, g_dcSshPass;
extern HWND g_dcTelHost, g_dcTelPort;
extern HWND g_dcRip, g_dcRcom, g_dcRbaud;
extern double g_dlgScale;

// ---------------- 常量（每个编译单元一份） ----------------
static const int kBaits[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
static const int kBaudCount = (int)(sizeof(kBaits) / sizeof(kBaits[0]));
static const COLORREF C_BG     = RGB(0x1B, 0x1E, 0x24);
static const COLORREF C_RX     = RGB(0xD8, 0xDE, 0xE9);
static const COLORREF C_TX     = RGB(0x56, 0xD4, 0xDD);
static const COLORREF C_AGENT  = RGB(0xF0, 0xB4, 0x29);
static const COLORREF C_SYS    = RGB(0x7A, 0xA2, 0xF7);
static const COLORREF C_ERR    = RGB(0xEF, 0x6B, 0x73);
static const COLORREF C_HINT   = RGB(0x6B, 0x74, 0x86);
static const int kHandleW = 14;   // 折叠把手宽（贴在栏左边缘中间）
static const int kHandleH = 132;  // 折叠把手高（展开态）
static const int kKeyFootH = 42;  // 右侧栏底部「导入 / 导出」区高
static const int kKeyTopH  = 24;  // 右侧栏顶部「按钮/文本」切换条高
static const int kUiPts[] = {9, 10, 11, 12, 14, 16};
static const int kUiPtCount = (int)(sizeof(kUiPts) / sizeof(kUiPts[0]));
static const int kSessRowH = 26;
static const int kSessGap  = 2;
static const int kSessPad  = 6;
static const int kSessNewH = 26;
static const int kKeyRowH = 26;   // 单个按钮高
static const int kCmdGapMs = 450; // 一行里多条命令（; 分隔）之间的间隔：等上一条跑完再发下一条
static const int kKeyGap  = 5;    // 按钮间距
static const int kKeyPad  = 8;    // 栏内边距
static const int kResizeW = 6;    // 左边缘拖拽热区宽度（拖动改变栏宽）
static const int kSideW    = 196; // 左侧会话栏宽度（展开态）
static const int kKeyDefW  = 150; // 右侧快捷命令栏默认宽度（= app_state 里 g_keysW 初值）
static const int kKeyMinW  = 90;  // 右侧快捷命令栏最小宽度（拖拽下限 / 载入校验）
static const int kKeyMaxW  = 460 + kSideW; // 右侧快捷命令栏拉宽上限 = 原 460 + 一个左侧栏宽度
                                          // （拖拽时还受窗口宽度约束：终端至少留 kMinViewW）
static const int kMinViewW = 150; // 终端区最小宽度：不低于右侧栏默认宽度

// ---------------- 内部接口（按模块分组，实现见对应 .cpp） ----------------
std::wstring exeDir();
std::wstring timestamp();
int baudIndexOf(int baud);
std::wstring gbkToWide(const std::string& s);
std::wstring rxDecode(const std::string& s);
std::string txEncode(const std::wstring& w);
const wchar_t* encName();
void loadSettings();
void saveSettings();
void dbgLog(const wchar_t* fmt, ...);
std::wstring logDirPath();
void closeLog();
void ensureLog();
void logLine(const wchar_t* dir, const std::string& bytes);
void viewReplace(const wchar_t* t);
long caretToEnd();
std::wstring viewText();
std::wstring viewRange(HWND h, long s, long e);
void viewDeleteRange(HWND h, long s, long e);
void cacheText(const std::wstring& t);
void termApplyColor();
long docChars();
void termEnsureCaret();
void termAnchorCheck();
void termSyncLine();
void termInsertTs();
void termInsertBytes(const std::string& s);
bool echoSensitive();
void termLocalEcho(const std::string& b);
void termCommitLine();
void termEchoBackspace();
void termEraseInLine(int mode);
void termClearAll();
void termCsi(const std::wstring& raw, wchar_t fin);
int trailingCharBytes();
void termFeed(const std::string& bytes);
void appendRun(const std::wstring& text, COLORREF color);
void showRx(const std::string& bytes);
void drawHandleBtn(const DRAWITEMSTRUCT* ds);
Session* sessionById(int id);
void sessionNameOf(Session* s);
DWORD CALLBACK rtfOutCb(DWORD_PTR cookie, LPBYTE pb, LONG cb, LONG* pcb);
DWORD CALLBACK rtfInCb(DWORD_PTR cookie, LPBYTE pb, LONG cb, LONG* pcb);
void viewSaveRtf(std::string& out);
void viewLoadRtf(const std::string& in);
void applySessionView(Session* t);
void switchSession(Session* t);
RECT sideRowRect(const RECT& rc, int i);
RECT sideNewRect(const RECT& rc);
int sideHit(int y);
void updateSide();
void drawSideBar(const DRAWITEMSTRUCT* ds);
void showSideMenu(POINT pt);
LRESULT CALLBACK SideProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR);
void updateStatus();
bool serialWrite(const std::string& bytes);
Session* hubPort();
void broadcastLine(const std::string& line, Peer* skip);
void postToGui(UINT msg, void* payload);
void postToGuiId(UINT msg, WPARAM id, void* payload);
void postText(UINT msg, const std::wstring& s);
void peerThread(std::shared_ptr<Peer> p);
void serverLoop();
void startServer();
void stopServer();
void postSessionData(Session* s, const std::string& bytes);
void readerProc(Session* s);
void openPort(Session* s);
bool openRemote(Session* s);
void closePort(Session* s = nullptr);
void startSsh(Session* s);
void stopSsh(Session* s);
void termSendBytes(const std::string& bytes, COLORREF color, const wchar_t* tag);
int linePop();
void termTypeChar(wchar_t ch);
void termHistory(int dir);
std::wstring immStr(HWND h, DWORD flag);
LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR);
bool imeComposing();
void syncForeignText();
void applyFont();
void syncMenuChecks();
void mAddMenu(HMENU m, UINT flags, UINT_PTR id, const wchar_t* text);
UINT menuStateIn(HMENU m, UINT_PTR id);
void makeMenuFont();
void applyMenuStyle();
void drawMenuItem(const DRAWITEMSTRUCT* ds, const MenuItemData* d);
void measureMenuItem(MEASUREITEMSTRUCT* ms);
void buildMenu();
void applyEnc(int e, bool announce);
void layout(HWND hwnd);
void layoutKeysOnly();
HWND mkBtn(HWND p, int id, const wchar_t* t);
void createControls(HWND hwnd);
void refreshPorts();
HFONT makeUiFont(int idx);
void applyUiFont();
void applyDlgStyle();
bool pickColor(HWND owner, COLORREF& c);
HWND dcLabel(HWND p, const wchar_t* t, int x, int y, int w, int h = 20);
HWND dcEdit(HWND p, int id, const wchar_t* t, int x, int y, int w, DWORD ex = 0);
HWND dcRadio(HWND p, int id, const wchar_t* t, int x, int y, int w, bool first);
HWND dcCheck(HWND p, int id, const wchar_t* t, int x, int y, int w, bool first);
HWND dcCombo(HWND p, int id, int x, int y, int w);
HWND dcGroup(HWND p, const wchar_t* t, int x, int y, int w, int h);
HWND dcBtn(HWND p, int id, const wchar_t* t, int x, int y, int w, int h = 28);
std::wstring editText(HWND h);
std::wstring comboText(HWND h);
void dlgFillPorts();
void dlgFillBaud(HWND combo, int cur);
void dlgShowPage(ConnType t);
void dlgBuildInput(HWND h);
void dlgBuild(HWND h);
void dlgCollect();
LRESULT CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
int showSessionDialog();
void connectSession(Session* s);
void newSessionDialog();
void closeSession(Session* s);
std::wstring trimW(const std::wstring& s);
void saveShortcuts();
void loadShortcuts();
void keyBarRefresh();
void updateKeysBtn();
void updateKeysModeBtn();
void keyTextCollect();
void keyTextReload(bool toEnd);
void keyModeApply(bool textMode);
void keyTextRunLine(int line);
LRESULT CALLBACK KeyTextProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR);
bool showInputBox(const wchar_t* title, const wchar_t* prompt, std::wstring& val);
void runShortcut(int i);
void keyRunCommand(const std::wstring& line);   // 一行按 ; 拆开，逐条发送
void keyCmdTick();                             // IDT_CMD 回调：发下一条
void keyAdd();
void keyEdit(int i);
void keyDel(int i);
std::wstring readTextAuto(const wchar_t* path);
void keyExport();
void keyImport();
void drawKeysBar(const DRAWITEMSTRUCT* ds);
int keyHit(int y);
int footHit(HWND h, int x, int y);
void showKeyMenu(POINT pt, int idx);
LRESULT CALLBACK KeyProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR);
void saveLog(HWND hwnd);
void viewCopySel();
void viewPasteToPort();
bool pickLogDir(std::wstring& out);
void showViewMenu(HWND h);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
bool printVersionIfAsked(LPWSTR cmdLine);
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int);
bool curOn();
bool isChecked(HWND h);
int  comboIndex(HWND h);
std::wstring iniPath();
long viewTextLen(HWND h);
void         termColorReset();
void lineReset();
int DS(int v);
void lineRecord(const std::wstring& s);
void startTelnet(Session* s);
void stopTelnet(Session* s);
bool telnetWrite(Session* s, const std::string& b);
