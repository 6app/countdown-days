#include "edit_dialog.h"
#include "daycalc.h"
#include "renderer.h"
#include "resource.h"

#include <commctrl.h>

#include <cstdlib>

namespace {

const int kOpacities[] = {100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0};
constexpr int kOpacityCount = static_cast<int>(sizeof(kOpacities) / sizeof(kOpacities[0]));

struct DialogCtx {
    CardData* data;
    bool isNew;
    POINT anchor;
};

HWND g_dialog = nullptr;

void CenterOnMonitor(HWND dlg, POINT anchor) {
    HMONITOR mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    RECT rc;
    GetWindowRect(dlg, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    const RECT& wa = mi.rcWork;
    SetWindowPos(dlg, nullptr, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void InitDialog(HWND dlg, const DialogCtx& ctx) {
    const CardData& d = *ctx.data;
    SetWindowTextW(dlg, ctx.isNew ? L"新建倒计日" : L"编辑倒计日");

    HINSTANCE inst = App_Instance();
    HICON big = nullptr, small = nullptr;
    LoadIconMetric(inst, MAKEINTRESOURCEW(IDI_APP), LIM_LARGE, &big);
    LoadIconMetric(inst, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &small);
    if (big) SendMessageW(dlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    if (small) SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));

    SendDlgItemMessageW(dlg, IDC_TITLE, EM_LIMITTEXT, 40, 0);
    SetDlgItemTextW(dlg, IDC_TITLE, d.title.c_str());

    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(d.year);
    st.wMonth = static_cast<WORD>(d.month);
    st.wDay = static_cast<WORD>(d.day > DaysInMonth(d.year, d.month) ? DaysInMonth(d.year, d.month) : d.day);
    SendDlgItemMessageW(dlg, IDC_DATE, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&st));

    CheckDlgButton(dlg, IDC_REPEAT, d.repeat ? BST_CHECKED : BST_UNCHECKED);

    HWND theme = GetDlgItem(dlg, IDC_THEME);
    for (int i = 0; i < ThemeCount(); ++i) SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ThemeName(i)));
    SendMessageW(theme, CB_SETCURSEL, d.theme, 0);

    HWND opac = GetDlgItem(dlg, IDC_OPACITY);
    int best = 0;
    for (int i = 0; i < kOpacityCount; ++i) {
        wchar_t buf[32];
        if (kOpacities[i] == 0)
            lstrcpynW(buf, L"0%（只显示文字）", 32);
        else
            wsprintfW(buf, L"%d%%", kOpacities[i]);
        SendMessageW(opac, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
        if (std::abs(kOpacities[i] - d.opacity) < std::abs(kOpacities[best] - d.opacity)) best = i;
    }
    SendMessageW(opac, CB_SETCURSEL, best, 0);

    CenterOnMonitor(dlg, ctx.anchor);
    SetForegroundWindow(dlg);
    SetFocus(GetDlgItem(dlg, IDC_TITLE));
    SendDlgItemMessageW(dlg, IDC_TITLE, EM_SETSEL, 0, -1);
}

void ReadDialog(HWND dlg, CardData& d) {
    wchar_t title[64] = {};
    GetDlgItemTextW(dlg, IDC_TITLE, title, 64);
    d.title = title;
    // 去掉首尾空白
    const auto first = d.title.find_first_not_of(L" \t");
    const auto last = d.title.find_last_not_of(L" \t");
    d.title = (first == std::wstring::npos) ? L"倒计日" : d.title.substr(first, last - first + 1);

    SYSTEMTIME st{};
    if (SendDlgItemMessageW(dlg, IDC_DATE, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&st)) == GDT_VALID) {
        d.year = st.wYear;
        d.month = st.wMonth;
        d.day = st.wDay;
    }
    d.repeat = IsDlgButtonChecked(dlg, IDC_REPEAT) == BST_CHECKED;

    const LRESULT t = SendDlgItemMessageW(dlg, IDC_THEME, CB_GETCURSEL, 0, 0);
    if (t >= 0 && t < ThemeCount()) d.theme = static_cast<int>(t);
    const LRESULT o = SendDlgItemMessageW(dlg, IDC_OPACITY, CB_GETCURSEL, 0, 0);
    if (o >= 0 && o < kOpacityCount) d.opacity = kOpacities[o];
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* ctx = reinterpret_cast<DialogCtx*>(lp);
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            g_dialog = dlg;
            InitDialog(dlg, *ctx);
            return FALSE;  // 已手动设置焦点
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                auto* ctx = reinterpret_cast<DialogCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
                ReadDialog(dlg, *ctx->data);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_DESTROY: {
            for (WPARAM which : {static_cast<WPARAM>(ICON_BIG), static_cast<WPARAM>(ICON_SMALL)}) {
                HICON icon = reinterpret_cast<HICON>(SendMessageW(dlg, WM_GETICON, which, 0));
                if (icon) DestroyIcon(icon);
            }
            g_dialog = nullptr;
            break;
        }
    }
    return FALSE;
}

}  // namespace

bool EditCardDialog(CardData& data, bool isNew, POINT anchor) {
    CardData copy = data;
    DialogCtx ctx{&copy, isNew, anchor};
    // 不设所有者：对话框有自己的任务栏按钮，不会被其他窗口挡住后找不到
    const INT_PTR r = DialogBoxParamW(App_Instance(), MAKEINTRESOURCEW(IDD_EDIT), nullptr, DialogProc,
                                      reinterpret_cast<LPARAM>(&ctx));
    if (r != IDOK) return false;
    data = copy;
    return true;
}

HWND EditDialogWindow() { return g_dialog; }
