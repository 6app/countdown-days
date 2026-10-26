// 找到桌面图标层窗口，卡片作为它的子窗口嵌入桌面。
//
// Win10 的桌面结构：
//   Progman（或 WorkerW）
//     └─ SHELLDLL_DefView      ← 卡片挂在这里，与图标列表同级、位于其上方
//          └─ SysListView32    ← 桌面图标
//
// 作为 DefView 的子窗口：永远位于所有应用窗口下方，Win+D 不会被隐藏，
// 也能正常接收鼠标。资源管理器重启时 DefView 会销毁，需重新查找并重建卡片。
#pragma once

#include <windows.h>

// 返回 SHELLDLL_DefView，找不到（资源管理器未就绪）返回 nullptr
HWND FindDesktopHost();

// 屏幕坐标 <-> 宿主客户区坐标（host 为 nullptr 时即屏幕坐标）
POINT ScreenToHost(HWND host, POINT screenPt);
