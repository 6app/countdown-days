// 全局共享的数据结构和应用层接口
#pragma once

#include <windows.h>
#include <string>
#include <vector>

// 一张卡片的持久化数据。坐标/尺寸均为屏幕物理像素（进程是 PerMonitorV2）。
struct CardData {
    std::wstring title = L"倒计日";
    int year = 2027, month = 1, day = 1;
    bool repeat = false;      // 每年重复
    int x = 100, y = 100;     // 屏幕坐标（左上角）
    int w = 0, h = 0;         // 0 表示使用默认尺寸
    int theme = 0;            // 配色预设下标
    int opacity = 88;         // 背景不透明度 0..100
};

struct AppConfig {
    bool locked = true;       // 锁定后不能拖动/缩放
    std::vector<CardData> cards;
};

// ---- 应用层回调（main.cpp 实现，card.cpp 调用）----
class Card;
bool App_IsLocked();
void App_RequestSave();                       // 延迟合并写盘
void App_ShowCardMenu(Card* card, POINT screenPt);
void App_EditCard(Card* card);
void App_OnCardDestroyed(Card* card);         // 窗口被意外销毁（如资源管理器重启）
HINSTANCE App_Instance();

// 自定义消息
constexpr UINT WM_APP_TRAY = WM_APP + 1;

// 定时器 ID（挂在主窗口上）
constexpr UINT_PTR TIMER_MIDNIGHT = 1;
constexpr UINT_PTR TIMER_SAVE     = 2;
constexpr UINT_PTR TIMER_EMBED    = 3;
