#include "tray.h"
#include "app.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>

namespace {
constexpr UINT kTrayId = 1;
HICON g_trayIcon = nullptr;
}  // namespace

bool TrayAdd(HWND owner, HINSTANCE inst) {
    // LoadIconMetric 会按当前 DPI 挑选最合适的图标尺寸（4K 200% 下是 32px）
    if (g_trayIcon) {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }
    if (FAILED(LoadIconMetric(inst, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &g_trayIcon)))
        g_trayIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = g_trayIcon;
    lstrcpynW(nid.szTip, L"倒计日", ARRAYSIZE(nid.szTip));

    Shell_NotifyIconW(NIM_DELETE, &nid);  // 防止重复添加
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return true;
}

void TrayRemove(HWND owner) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (g_trayIcon) {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }
}

void TrayBalloon(HWND owner, const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    nid.hBalloonIcon = g_trayIcon;
    lstrcpynW(nid.szInfoTitle, title, ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text, ARRAYSIZE(nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
