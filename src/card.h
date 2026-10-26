// 卡片窗口：分层子窗口，挂在桌面图标层下
#pragma once

#include "app.h"

class Card {
public:
    explicit Card(const CardData& d) : data(d) {}
    ~Card() { Destroy(); }
    Card(const Card&) = delete;
    Card& operator=(const Card&) = delete;

    static bool RegisterWindowClass(HINSTANCE inst);

    // host 为桌面 DefView；为 nullptr 时退化为置底的顶层窗口
    bool Create(HWND host);
    void Destroy();
    bool NeedsRecreate(HWND host) const;

    void Redraw();          // 按当前尺寸和日期重绘
    void Place();           // 把 data 中的位置/尺寸应用到窗口
    void EnsureOnScreen();  // 显示器布局变化后，确保卡片仍在某块屏幕上

    HWND Hwnd() const { return hwnd_; }
    int Dpi() const;        // 卡片所在显示器的 DPI

    CardData data;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    int HitZone(POINT client) const;
    void DragTo(POINT cursor);
    void SnapToWorkArea(RECT& rc, int zone) const;

    HWND hwnd_ = nullptr;
    HWND host_ = nullptr;
    bool destroying_ = false;

    int dragZone_ = 0;      // 0 = 未拖动
    POINT dragStart_{};
    RECT startRect_{};
    int lastW_ = 0, lastH_ = 0;
};
