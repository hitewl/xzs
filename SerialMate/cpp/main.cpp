// main.cpp - 程序入口：命令行处理 + 建主窗口 + 消息循环

#include "sm.h"
bool printVersionIfAsked(LPWSTR cmdLine) {
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
