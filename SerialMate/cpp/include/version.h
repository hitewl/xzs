// version.h - SerialMate 版本号唯一来源（三段式 MAJOR.MINOR.PATCH）
//
// 只改下面三个数字，其余的都由它们推导：
//   窗口标题 / 关于框 / `serial_gui.exe --version` / exe 右键属性里的版本
// 全工程不存在第二处硬编码的版本号。
//
// 升哪一位（详见 DEVELOP.md 第 7 节）：
//   MAJOR  不兼容变更：ini 键的语义变了、接管协议 JSON 字段变了、默认快捷键改了
//   MINOR  新增功能：新会话类型、新菜单项、新协议命令，旧的用法照旧可用
//   PATCH  修 bug、显示/性能优化，对外行为不变
#ifndef SM_VERSION_H
#define SM_VERSION_H

#define SM_VER_MAJOR 1
#define SM_VER_MINOR 3
#define SM_VER_PATCH 5

// 宏 -> 字符串必须两层展开，否则拿到的是宏名本身
#define SM_STR2(x) #x
#define SM_STR(x)  SM_STR2(x)
#define SM_WSTR2(x) L##x
#define SM_WSTR(x)  SM_WSTR2(x)

// "1.0.0"：给 .rc 资源、--version 用
#define SM_VERSION SM_STR(SM_VER_MAJOR) "." SM_STR(SM_VER_MINOR) "." SM_STR(SM_VER_PATCH)
// L"1.0.0"：给窗口标题、MessageBox 用。
// 注意不能写 SM_WSTR(SM_VER_MAJOR)——L##1 会拼成标识符 L1，必须先变成字符串再加前缀。
#define SM_VERSION_W \
    SM_WSTR(SM_STR(SM_VER_MAJOR)) L"." SM_WSTR(SM_STR(SM_VER_MINOR)) L"." SM_WSTR(SM_STR(SM_VER_PATCH))

// 编译时间，关于框里显示，用来确认手上这个 exe 是哪一次编的
#define SM_BUILD_A __DATE__ " " __TIME__
#define SM_BUILD_W SM_WSTR(__DATE__) L" " SM_WSTR(__TIME__)

#endif // SM_VERSION_H
