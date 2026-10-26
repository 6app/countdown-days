// 系统托盘图标
#pragma once

#include <windows.h>

bool TrayAdd(HWND owner, HINSTANCE inst);   // 资源管理器重启后再次调用即可恢复
void TrayRemove(HWND owner);
void TrayBalloon(HWND owner, const wchar_t* title, const wchar_t* text);  // 气泡提示
