#include "card.h"
#include "daycalc.h"
#include "desktop_host.h"
#include "renderer.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>

namespace {

const wchar_t kCardClass[] = L"CountDay.Card";

// 拖动区域标志位
enum Zone : int {
    ZLeft = 1,
    ZTop = 2,
    ZRight = 4,
    ZBottom = 8,
    ZMove = 16,
};

int Scale(int px96, int dpi) { return MulDiv(px96, dpi, 96); }

LPCWSTR CursorFor(int zone) {
    switch (zone) {
        case ZLeft:
        case ZRight: return IDC_SIZEWE;
        case ZTop:
        case ZBottom: return IDC_SIZENS;
        case ZLeft | ZTop:
        case ZRight | ZBottom: return IDC_SIZENWSE;
        case ZRight | ZTop:
        case ZLeft | ZBottom: return IDC_SIZENESW;
        case ZMove: return IDC_SIZEALL;
        default: return IDC_ARROW;
    }
}

}  // namespace

bool Card::RegisterWindowClass(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &Card::WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCardClass;
    return RegisterClassExW(&wc) != 0;
}

int Card::Dpi() const {
    POINT center{data.x + std::max(data.w, 1) / 2, data.y + std::max(data.h, 1) / 2};
    HMONITOR mon = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) return 96;
    return static_cast<int>(dx);
}

bool Card::Create(HWND host) {
    Destroy();
    destroying_ = false;
    host_ = host;

    const int dpi = Dpi();
    if (data.w <= 0 || data.h <= 0) {
        data.w = Scale(300, dpi);
        data.h = Scale(190, dpi);
    }
    EnsureOnScreen();

    // WS_EX_LAYERED 子窗口需要 Win8+ 清单声明（见 app.manifest）
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    const DWORD style = host ? (WS_CHILD | WS_CLIPSIBLINGS) : WS_POPUP;
    const POINT p = ScreenToHost(host, POINT{data.x, data.y});
    hwnd_ = CreateWindowExW(exStyle, kCardClass, L"CountDay", style, p.x, p.y, data.w, data.h, host, nullptr,
                            App_Instance(), this);
    if (!hwnd_) return false;

    lastW_ = lastH_ = 0;
    Redraw();  // 分层窗口在第一次 UpdateLayeredWindow 之前不可见，先画好再显示
    SetWindowPos(hwnd_, host ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

void Card::Destroy() {
    if (hwnd_) {
        destroying_ = true;
        if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Card::NeedsRecreate(HWND host) const {
    return !hwnd_ || !IsWindow(hwnd_) || host_ != host || (host && GetParent(hwnd_) != host);
}

void Card::Redraw() {
    if (!hwnd_) return;
    const CountResult r = ComputeCount(data, LocalToday());
    RenderCard(hwnd_, data, r, data.w, data.h, !App_IsLocked());
    lastW_ = data.w;
    lastH_ = data.h;
    // 资源管理器刷新桌面时可能调整图标列表的层级，顺手把卡片放回最上面（开销可忽略）
    if (host_) SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Card::Place() {
    if (!hwnd_) return;
    const POINT p = ScreenToHost(host_, POINT{data.x, data.y});
    SetWindowPos(hwnd_, nullptr, p.x, p.y, data.w, data.h, SWP_NOZORDER | SWP_NOACTIVATE);
    if (data.w != lastW_ || data.h != lastH_) Redraw();
}

void Card::EnsureOnScreen() {
    RECT rc{data.x, data.y, data.x + data.w, data.y + data.h};
    HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
    if (mon) {
        // 至少要有一块能抓住的区域留在屏幕上
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        RECT vis{};
        IntersectRect(&vis, &rc, &mi.rcWork);
        const int grab = Scale(40, Dpi());
        if (vis.right - vis.left >= grab && vis.bottom - vis.top >= grab) return;
    }
    // 移到最近显示器工作区内
    mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    const RECT& wa = mi.rcWork;
    data.w = std::min<int>(data.w, wa.right - wa.left);
    data.h = std::min<int>(data.h, wa.bottom - wa.top);
    data.x = std::clamp<int>(data.x, wa.left, wa.right - data.w);
    data.y = std::clamp<int>(data.y, wa.top, wa.bottom - data.h);
}

int Card::HitZone(POINT pt) const {
    const int dpi = Dpi();
    const int edge = std::max(6, Scale(8, dpi));
    const int grip = std::max(16, Scale(22, dpi));
    // 右下角手柄区域更大，方便抓取
    if (pt.x >= data.w - grip && pt.y >= data.h - grip) return ZRight | ZBottom;
    int z = 0;
    if (pt.x < edge) z |= ZLeft;
    else if (pt.x >= data.w - edge) z |= ZRight;
    if (pt.y < edge) z |= ZTop;
    else if (pt.y >= data.h - edge) z |= ZBottom;
    return z ? z : ZMove;
}

void Card::SnapToWorkArea(RECT& rc, int zone) const {
    HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return;
    const RECT& wa = mi.rcWork;
    const int snap = Scale(12, Dpi());
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;

    if (zone == ZMove) {
        if (std::abs(rc.left - wa.left) < snap) rc.left = wa.left;
        else if (std::abs(rc.right - wa.right) < snap) rc.left = wa.right - w;
        if (std::abs(rc.top - wa.top) < snap) rc.top = wa.top;
        else if (std::abs(rc.bottom - wa.bottom) < snap) rc.top = wa.bottom - h;
        rc.right = rc.left + w;
        rc.bottom = rc.top + h;
        return;
    }
    if ((zone & ZLeft) && std::abs(rc.left - wa.left) < snap) rc.left = wa.left;
    if ((zone & ZRight) && std::abs(rc.right - wa.right) < snap) rc.right = wa.right;
    if ((zone & ZTop) && std::abs(rc.top - wa.top) < snap) rc.top = wa.top;
    if ((zone & ZBottom) && std::abs(rc.bottom - wa.bottom) < snap) rc.bottom = wa.bottom;
}

void Card::DragTo(POINT cur) {
    const int dx = cur.x - dragStart_.x, dy = cur.y - dragStart_.y;
    const int dpi = Dpi();
    const int minW = Scale(140, dpi), minH = Scale(90, dpi);
    RECT rc = startRect_;

    if (dragZone_ == ZMove) {
        OffsetRect(&rc, dx, dy);
    } else {
        if (dragZone_ & ZLeft) rc.left = std::min<LONG>(startRect_.left + dx, startRect_.right - minW);
        if (dragZone_ & ZRight) rc.right = std::max<LONG>(startRect_.right + dx, startRect_.left + minW);
        if (dragZone_ & ZTop) rc.top = std::min<LONG>(startRect_.top + dy, startRect_.bottom - minH);
        if (dragZone_ & ZBottom) rc.bottom = std::max<LONG>(startRect_.bottom + dy, startRect_.top + minH);
    }
    // 按住 Alt 关闭吸附
    if (GetKeyState(VK_MENU) >= 0) SnapToWorkArea(rc, dragZone_);

    data.x = rc.left;
    data.y = rc.top;
    data.w = rc.right - rc.left;
    data.h = rc.bottom - rc.top;
    Place();
}

LRESULT CALLBACK Card::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Card* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<Card*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Card*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    return self->Handle(hwnd, msg, wp, lp);
}

LRESULT Card::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // 不抢焦点

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && !App_IsLocked()) {
                int zone = dragZone_;
                if (!zone) {
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(hwnd, &pt);
                    zone = HitZone(pt);
                }
                SetCursor(LoadCursorW(nullptr, CursorFor(zone)));
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN:
            if (!App_IsLocked()) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                dragZone_ = HitZone(pt);
                GetCursorPos(&dragStart_);
                startRect_ = RECT{data.x, data.y, data.x + data.w, data.y + data.h};
                SetCapture(hwnd_);
            }
            return 0;

        case WM_MOUSEMOVE:
            if (dragZone_ && GetCapture() == hwnd_) {
                POINT cur;
                GetCursorPos(&cur);
                DragTo(cur);
            }
            return 0;

        case WM_LBUTTONUP:
            if (dragZone_) ReleaseCapture();  // 收尾在 WM_CAPTURECHANGED
            return 0;

        case WM_CAPTURECHANGED:
            if (dragZone_) {
                dragZone_ = 0;
                App_RequestSave();
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            App_EditCard(this);
            return 0;

        // 右键自己处理，不交给 DefWindowProc：否则 WM_CONTEXTMENU 会冒泡到桌面，弹出系统桌面菜单
        case WM_RBUTTONDOWN:
            return 0;
        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            App_ShowCardMenu(this, pt);
            return 0;
        }
        case WM_CONTEXTMENU:
            return 0;

        case WM_WINDOWPOSCHANGING:
            // 退化模式（顶层窗口）：始终压在最底层
            if (!host_) {
                auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
                if (!(pos->flags & SWP_NOZORDER)) pos->hwndInsertAfter = HWND_BOTTOM;
            }
            break;

        case WM_DESTROY: {
            const bool unexpected = !destroying_;
            hwnd_ = nullptr;
            dragZone_ = 0;
            if (unexpected) App_OnCardDestroyed(this);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
