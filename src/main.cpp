// 倒计日：嵌入桌面的倒数日卡片
//
// 进程结构：一个隐藏的顶层主窗口（托盘消息、定时器、系统广播）+ 若干卡片子窗口。
// 空闲时没有任何轮询：只在本地零点、改系统时间、睡眠唤醒、解锁时重绘一次。

#include "app.h"
#include "card.h"
#include "config.h"
#include "daycalc.h"
#include "desktop_host.h"
#include "edit_dialog.h"
#include "renderer.h"
#include "resource.h"
#include "tray.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <memory>

namespace {

const wchar_t kMainClass[] = L"CountDay.Main";
const wchar_t kMutexName[] = L"Local\\CountDay.SingleInstance";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"CountDay";

constexpr UINT WM_APP_SHOWMENU = WM_APP + 2;  // 第二个实例启动时通知已运行的实例

constexpr int kMaxEmbedTries = 30;            // 资源管理器未就绪时最多重试 30 秒

enum MenuId : UINT {
    ID_NEW = 100,
    ID_LOCK,
    ID_AUTOSTART,
    ID_EXIT,
    ID_EDIT,
    ID_DELETE,
    ID_THEME_BASE = 200,
    ID_OPACITY_BASE = 300,
};
const int kMenuOpacities[] = {100, 80, 60, 40, 20, 0};

HINSTANCE g_inst = nullptr;
HWND g_main = nullptr;
HWND g_host = nullptr;
UINT g_msgTaskbarCreated = 0;
bool g_locked = true;
bool g_busy = false;  // 对话框或菜单打开中
int g_embedTries = 0;
Date g_lastDay{};
std::vector<std::unique_ptr<Card>> g_cards;

// ---------- 工具 ----------

void TrimMemory() {
    // 把暂时用不到的页换出工作集，任务管理器里的内存占用会降到很低
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

int PrimaryDpi() {
    HDC dc = GetDC(nullptr);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return dpi > 0 ? dpi : 96;
}

RECT WorkAreaAt(POINT pt) {
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi.rcWork;
}

std::wstring ExePath() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

bool IsAutostart() {
    wchar_t buf[MAX_PATH + 8] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return false;
    return CompareStringOrdinal(buf, -1, (L"\"" + ExePath() + L"\"").c_str(), -1, TRUE) == CSTR_EQUAL;
}

void SetAutostart(bool on) {
    if (on) {
        const std::wstring cmd = L"\"" + ExePath() + L"\"";
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, cmd.c_str(),
                        static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    }
}

// ---------- 配置 ----------

void SaveNow() {
    KillTimer(g_main, TIMER_SAVE);
    AppConfig cfg;
    cfg.locked = g_locked;
    for (auto& c : g_cards) cfg.cards.push_back(c->data);
    SaveConfig(cfg);
}

// ---------- 卡片管理 ----------

void RedrawAll() {
    for (auto& c : g_cards) c->Redraw();
}

void EmbedCards() {
    HWND host = FindDesktopHost();
    if (!host && g_embedTries < kMaxEmbedTries) {
        ++g_embedTries;
        SetTimer(g_main, TIMER_EMBED, 1000, nullptr);
        return;
    }
    KillTimer(g_main, TIMER_EMBED);
    g_embedTries = 0;
    g_host = host;  // 仍为 nullptr 时退化为置底顶层窗口

    for (auto& c : g_cards) {
        if (!c->NeedsRecreate(host)) continue;
        if (!c->Create(host) && host) c->Create(nullptr);
    }
    TrimMemory();
}

void RefreshIfDayChanged() {
    const Date today = LocalToday();
    if (today.y != g_lastDay.y || today.m != g_lastDay.m || today.d != g_lastDay.d) {
        g_lastDay = today;
        RedrawAll();
        TrimMemory();
    }
}

void ScheduleMidnight() {
    SetTimer(g_main, TIMER_MIDNIGHT, MsUntilNextMidnight(), nullptr);
}

CardData MakeDefaultCard(POINT anchor, int index) {
    const int dpi = PrimaryDpi();
    CardData d;
    d.w = MulDiv(300, dpi, 96);
    d.h = MulDiv(190, dpi, 96);
    const RECT wa = WorkAreaAt(anchor);
    const int margin = MulDiv(40, dpi, 96);
    const int step = MulDiv(24, dpi, 96) * (index % 8);  // 连续新建时错开，避免完全重叠
    d.x = wa.right - d.w - margin - step;
    d.y = wa.top + margin + step;
    return d;
}

void AddCard(POINT anchor) {
    const Date today = LocalToday();
    CardData d = MakeDefaultCard(anchor, static_cast<int>(g_cards.size()));
    d.title = L"新的倒计日";
    const Date target = CivilFromDays(DaysFromCivil(today.y, today.m, today.d) + 30);
    d.year = target.y;
    d.month = target.m;
    d.day = target.d;

    g_busy = true;
    const bool ok = EditCardDialog(d, true, anchor);
    g_busy = false;
    if (ok) {
        g_cards.push_back(std::make_unique<Card>(d));
        Card* c = g_cards.back().get();
        if (!c->Create(g_host) && g_host) c->Create(nullptr);
        SaveNow();
    }
    TrimMemory();
}

void DeleteCard(Card* card) {
    wchar_t msg[160];
    wsprintfW(msg, L"确定删除「%s」吗？", card->data.title.c_str());
    g_busy = true;
    const int r = MessageBoxW(nullptr, msg, L"倒计日", MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
    g_busy = false;
    if (r != IDYES) return;
    for (auto it = g_cards.begin(); it != g_cards.end(); ++it) {
        if (it->get() == card) {
            g_cards.erase(it);  // 析构时销毁窗口
            break;
        }
    }
    SaveNow();
}

void SetLocked(bool locked) {
    g_locked = locked;
    RedrawAll();  // 编辑模式显示虚线框
    App_RequestSave();
}

// ---------- 菜单 ----------

void ShowMenu(Card* card, POINT pt) {
    if (g_busy) {
        if (HWND dlg = EditDialogWindow()) SetForegroundWindow(dlg);
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (card) {
        AppendMenuW(menu, MF_STRING, ID_EDIT, L"编辑(&E)…");

        HMENU themes = CreatePopupMenu();
        for (int i = 0; i < ThemeCount(); ++i)
            AppendMenuW(themes, MF_STRING | (card->data.theme == i ? MF_CHECKED : 0), ID_THEME_BASE + i, ThemeName(i));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themes), L"配色(&C)");

        HMENU opac = CreatePopupMenu();
        for (int i = 0; i < static_cast<int>(sizeof(kMenuOpacities) / sizeof(int)); ++i) {
            wchar_t buf[32];
            if (kMenuOpacities[i] == 0)
                lstrcpynW(buf, L"0%（只显示文字）", 32);
            else
                wsprintfW(buf, L"%d%%", kMenuOpacities[i]);
            AppendMenuW(opac, MF_STRING | (card->data.opacity == kMenuOpacities[i] ? MF_CHECKED : 0),
                        ID_OPACITY_BASE + i, buf);
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(opac), L"不透明度(&O)");
        AppendMenuW(menu, MF_STRING, ID_DELETE, L"删除(&D)");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, ID_NEW, L"新建倒计日(&N)…");
    AppendMenuW(menu, MF_STRING | (g_locked ? MF_CHECKED : 0), ID_LOCK, L"锁定位置和大小(&L)");
    AppendMenuW(menu, MF_STRING | (IsAutostart() ? MF_CHECKED : 0), ID_AUTOSTART, L"开机自动启动(&S)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"退出(&X)");

    // 标准做法：先把隐藏主窗口设为前台，菜单才会在点击别处时正常关闭
    SetForegroundWindow(g_main);
    g_busy = true;
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, g_main, nullptr));
    g_busy = false;
    PostMessageW(g_main, WM_NULL, 0, 0);
    DestroyMenu(menu);  // 会一并销毁子菜单

    switch (cmd) {
        case 0: break;
        case ID_NEW: AddCard(pt); break;
        case ID_LOCK: SetLocked(!g_locked); break;
        case ID_AUTOSTART: SetAutostart(!IsAutostart()); break;
        case ID_EXIT: DestroyWindow(g_main); return;
        case ID_EDIT: App_EditCard(card); break;
        case ID_DELETE: DeleteCard(card); break;
        default:
            if (card && cmd >= ID_THEME_BASE && cmd < ID_THEME_BASE + static_cast<UINT>(ThemeCount())) {
                card->data.theme = static_cast<int>(cmd - ID_THEME_BASE);
                card->Redraw();
                App_RequestSave();
            } else if (card && cmd >= ID_OPACITY_BASE &&
                       cmd < ID_OPACITY_BASE + sizeof(kMenuOpacities) / sizeof(int)) {
                card->data.opacity = kMenuOpacities[cmd - ID_OPACITY_BASE];
                card->Redraw();
                App_RequestSave();
            }
    }
    TrimMemory();
}

// ---------- 主窗口 ----------

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
        // 资源管理器重启：恢复托盘图标，重新嵌入桌面
        TrayAdd(hwnd, g_inst);
        g_embedTries = 0;
        EmbedCards();
        return 0;
    }

    switch (msg) {
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT: {
                    POINT pt;
                    GetCursorPos(&pt);
                    ShowMenu(nullptr, pt);
                    break;
                }
            }
            return 0;

        case WM_APP_SHOWMENU: {
            POINT pt;
            GetCursorPos(&pt);
            ShowMenu(nullptr, pt);
            return 0;
        }

        case WM_TIMER:
            switch (wp) {
                case TIMER_MIDNIGHT:
                    RefreshIfDayChanged();
                    ScheduleMidnight();
                    break;
                case TIMER_SAVE:
                    SaveNow();
                    break;
                case TIMER_EMBED:
                    KillTimer(hwnd, TIMER_EMBED);
                    EmbedCards();
                    break;
            }
            return 0;

        case WM_TIMECHANGE:  // 改了系统时间或时区
            RefreshIfDayChanged();
            ScheduleMidnight();
            return 0;

        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
                RefreshIfDayChanged();
                ScheduleMidnight();
            }
            return TRUE;

        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_UNLOCK || wp == WTS_SESSION_LOGON) {
                RefreshIfDayChanged();
                ScheduleMidnight();
            }
            return 0;

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            for (auto& c : g_cards) {
                c->EnsureOnScreen();
                c->Place();
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (wp == SPI_SETWORKAREA)
                for (auto& c : g_cards) {
                    c->EnsureOnScreen();
                    c->Place();
                }
            return 0;

        case WM_QUERYENDSESSION:
            return TRUE;

        case WM_ENDSESSION:
            if (wp) {
                SaveNow();
                // 安装器（重启管理器）要求关闭程序以便替换文件
                if (lp & ENDSESSION_CLOSEAPP) DestroyWindow(hwnd);
            }
            return 0;

        case WM_DESTROY:
            SaveNow();
            WTSUnRegisterSessionNotification(hwnd);
            TrayRemove(hwnd);
            g_cards.clear();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void EnableMixedDpiHosting() {
    // 允许挂到 DPI 感知模式不同的父窗口下（Win10 1803+）；旧系统上没有该函数，忽略
    using Fn = DPI_HOSTING_BEHAVIOR(WINAPI*)(DPI_HOSTING_BEHAVIOR);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(user32, "SetThreadDpiHostingBehavior"))))
            fn(DPI_HOSTING_BEHAVIOR_MIXED);
    }
}

}  // namespace

// ---------- 供 card.cpp 调用的应用层接口 ----------

HINSTANCE App_Instance() { return g_inst; }
bool App_IsLocked() { return g_locked; }

void App_RequestSave() {
    if (g_main) SetTimer(g_main, TIMER_SAVE, 800, nullptr);
}

void App_ShowCardMenu(Card* card, POINT pt) { ShowMenu(card, pt); }

void App_EditCard(Card* card) {
    if (g_busy) {
        if (HWND dlg = EditDialogWindow()) SetForegroundWindow(dlg);
        return;
    }
    POINT anchor{card->data.x + card->data.w / 2, card->data.y + card->data.h / 2};
    CardData d = card->data;
    g_busy = true;
    const bool ok = EditCardDialog(d, false, anchor);
    g_busy = false;
    if (ok) {
        // 位置和尺寸以窗口当前状态为准
        d.x = card->data.x;
        d.y = card->data.y;
        d.w = card->data.w;
        d.h = card->data.h;
        card->data = d;
        card->Redraw();
        SaveNow();
    }
    TrimMemory();
}

void App_OnCardDestroyed(Card*) {
    // 多半是资源管理器崩溃/重启导致桌面窗口被销毁，稍后重新嵌入
    if (g_main) {
        g_embedTries = 0;
        SetTimer(g_main, TIMER_EMBED, 1500, nullptr);
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g_inst = inst;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kMainClass, nullptr)) {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(other, WM_APP_SHOWMENU, 0, 0);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    EnableMixedDpiHosting();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_DATE_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    if (!RendererInit()) {
        MessageBoxW(nullptr, L"无法初始化 Direct2D / DirectWrite。", L"倒计日", MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.lpszClassName = kMainClass;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassExW(&wc);
    Card::RegisterWindowClass(inst);

    // 隐藏的顶层窗口（不能用 HWND_MESSAGE，否则收不到 TaskbarCreated 等广播）
    g_main = CreateWindowExW(WS_EX_TOOLWINDOW, kMainClass, L"CountDay", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst,
                             nullptr);
    if (!g_main) return 1;

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // 以普通权限运行时也能收到资源管理器（可能更高完整性级别）的广播
    ChangeWindowMessageFilterEx(g_main, g_msgTaskbarCreated, MSGFLT_ALLOW, nullptr);

    TrayAdd(g_main, inst);
    WTSRegisterSessionNotification(g_main, NOTIFY_FOR_THIS_SESSION);

    AppConfig cfg;
    bool firstRun = false;
    if (LoadConfig(cfg)) {
        g_locked = cfg.locked;
        for (auto& d : cfg.cards) g_cards.push_back(std::make_unique<Card>(d));
    } else {
        // 首次运行：放一张示例卡片——下一个元旦
        firstRun = true;
        const Date today = LocalToday();
        POINT primary{0, 0};
        CardData d = MakeDefaultCard(primary, 0);
        d.title = L"元旦";
        d.year = today.y + 1;
        d.month = 1;
        d.day = 1;
        d.repeat = true;
        g_cards.push_back(std::make_unique<Card>(d));
    }

    g_lastDay = LocalToday();
    EmbedCards();
    ScheduleMidnight();
    if (firstRun) {
        SaveNow();
        TrayBalloon(g_main, L"倒计日已启动",
                    L"右键卡片可编辑、换配色；在托盘菜单取消“锁定位置和大小”后，可拖动卡片或拖边角调整大小。");
    }
    TrimMemory();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    RendererShutdown();
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
