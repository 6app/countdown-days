// 卡片绘制：Direct2D(软件) + DirectWrite → DIB → UpdateLayeredWindow
//
// 只在内容/尺寸变化时调用；绘制完成后位图立刻释放，
// 窗口内容由系统（DWM）保存，进程空闲时不占额外内存和 CPU。
#pragma once

#include "app.h"
#include "daycalc.h"

int ThemeCount();
const wchar_t* ThemeName(int index);

bool RendererInit();
void RendererShutdown();

bool RenderCard(HWND hwnd, const CardData& card, const CountResult& count,
                int width, int height, bool editMode);
