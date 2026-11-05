// term.h —— 简易 VT100/xterm 终端控件（SSH 会话用）
// 支持：光标定位、清屏/清行、滚屏区、插入删除字符、SGR 16/256/真彩色、alternate screen、
//       UTF-8（含 CJK 双宽）、回滚缓冲、鼠标框选复制、Ctrl+Shift+C/V
#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include <deque>
#include <cstdint>

class Terminal {
public:
    using InputFn = std::function<void(const std::string&)>;

    Terminal(HWND parent, InputFn onInput);
    ~Terminal();

    HWND hwnd() const { return h_; }
    void setFontPt(int pt);
    void setBounds(int x, int y, int w, int h);
    void showWindow(bool s);
    void focus();

    void write(const std::string& bytes);          // 服务器输出（UTF-8，可含转义序列）
    void writeText(const std::wstring& text);      // 本地提示文字（按普通输出处理）
    void clear();

    int cols() const { return cols_; }
    int rows() const { return rows_; }

    std::function<void(int, int)> onResize;   // 列数/行数变化（通知远端改 pty 大小）

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    struct Cell {
        uint32_t ch = ' ';
        uint32_t fg = 0;
        uint32_t bg = 0;
        uint8_t  attr = 0;      // 1 粗体 2 下划线 4 双宽后半格 8 反显
    };
    typedef std::vector<Cell> Row;

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void   makeFont();
    void   layoutMetrics();
    void   resetScreen(bool keepHist);
    void   resizeGrid(int cols, int rows);
    void   paint(HDC dc, const RECT& rc);
    void   paintRows(HDC dc, int x0, int y0);
    void   invalidate();

    // 解析
    void   feed(char c);
    void   feedUtf8(unsigned char c);
    void   escDispatch();
    void   csiDispatch(char final);
    void   sgr();
    void   putChar(uint32_t cp);
    void   newLine();
    void   carriageReturn();
    void   scrollUp(int n);
    void   scrollDown(int n);
    void   clearCells(Row& r, int from, int to);
    Cell   blank() const;
    Row&   lineAt(int abs);             // 绝对行（hist + grid）
    const Row* lineAtC(int abs) const;
    int    histCount() const { return (int)hist_.size(); }
    int    totalLines() const { return (int)hist_.size() + rows_; }

    // 输入
    void   sendKey(const char* s);
    void   sendChar(uint32_t cp);
    void   copySelection();
    void   pasteClipboard();
    void   posToCell(int px, int py, int* vline, int* col);
    std::wstring selectionText();

    HWND        h_ = nullptr;
    HWND        parent_ = nullptr;
    InputFn     onInput_;

    int  cols_ = 80, rows_ = 24;
    int  cellW_ = 8, cellH_ = 16, padX_ = 2, padY_ = 2;
    int  fontPt_ = 11;
    HFONT font_ = nullptr;
    int  fontPx_ = 16;

    std::vector<Row> grid_;
    std::deque<Row>  hist_;
    int  cx_ = 0, cy_ = 0;
    int  savedX_ = 0, savedY_ = 0;
    bool wrapPending_ = false;

    // 滚屏区
    int  top_ = 0, bot_ = 0;

    // 备用屏幕
    bool alt_ = false;
    std::vector<Row> altGrid_;
    int  altCx_ = 0, altCy_ = 0;
    std::vector<Row> savedGrid_;

    int  sbOffset_ = 0;                 // 向上回滚的行数

    // 属性
    uint32_t fg_ = 0xD8DEE9, bg_ = 0x1B1E24;
    uint8_t  attr_ = 0;
    bool     cursorOn_ = true;
    bool     appCursor_ = false;
    bool     bracketedPaste_ = false;
    int      curStyle_ = 0;

    // 解析状态
    int   st_ = 0;                      // 0 普通 1 ESC 2 CSI 3 OSC 4 忽略一个字节
    std::string csi_;
    int   params_[16] = {0};
    int   nparam_ = 0;
    bool  private_ = false;
    uint32_t utf8Cp_ = 0;
    int   utf8Need_ = 0;

    // 选择
    bool  seling_ = false;
    int   selV0_ = 0, selC0_ = 0, selV1_ = 0, selC1_ = 0;

    // 光标闪烁
    bool  blinkOn_ = true;
    UINT_PTR timer_ = 0;

    HBRUSH bgBrush_ = nullptr;
};

void registerTermClass(HINSTANCE inst);
