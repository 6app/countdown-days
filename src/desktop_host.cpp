#include "desktop_host.h"

namespace {

struct FindCtx {
    HWND defView = nullptr;
};

BOOL CALLBACK EnumTopLevel(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);
    wchar_t cls[32];
    if (!GetClassNameW(hwnd, cls, 32)) return TRUE;
    if (lstrcmpW(cls, L"WorkerW") != 0 && lstrcmpW(cls, L"Progman") != 0) return TRUE;
    HWND dv = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (dv) {
        ctx->defView = dv;
        return FALSE;
    }
    return TRUE;
}

}  // namespace

HWND FindDesktopHost() {
    // 通常 DefView 在 Progman 下；壁纸幻灯片或动态壁纸软件发送 0x052C 后，
    // 它会被移到某个 WorkerW 下，所以两处都要找。
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman) {
        HWND dv = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (dv) return dv;
    }
    FindCtx ctx;
    EnumWindows(EnumTopLevel, reinterpret_cast<LPARAM>(&ctx));
    return ctx.defView;
}

POINT ScreenToHost(HWND host, POINT pt) {
    if (host) MapWindowPoints(HWND_DESKTOP, host, &pt, 1);
    return pt;
}
