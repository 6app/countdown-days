#include "renderer.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cwchar>

namespace {

// 最小 COM 智能指针，避免依赖 WRL
template <class T>
class Com {
public:
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Com& operator=(Com&& o) noexcept {
        if (this != &o) {
            Reset();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }
    ~Com() { Reset(); }
    void Reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    T** Put() {
        Reset();
        return &p_;
    }
    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

struct Theme {
    const wchar_t* name;
    UINT32 bgTop, bgBottom;   // 背景渐变
    UINT32 title, number, sub;
    UINT32 border;            // 1px 描边（带 alpha 的 0xAARRGGBB）
};

const Theme kThemes[] = {
    {L"墨夜", 0x2B2F36, 0x1A1D22, 0xF3F4F6, 0x60A5FA, 0x9CA3AF, 0x22FFFFFF},
    {L"雪白", 0xFFFFFF, 0xEEF1F5, 0x1F2937, 0x2563EB, 0x6B7280, 0x1A000000},
    {L"晴空蓝", 0x3B82F6, 0x1D4ED8, 0xFFFFFF, 0xFFFFFF, 0xDBEAFE, 0x26FFFFFF},
    {L"樱桃红", 0xF43F5E, 0xBE123C, 0xFFFFFF, 0xFFFFFF, 0xFFE4E6, 0x26FFFFFF},
    {L"森林绿", 0x10B981, 0x047857, 0xFFFFFF, 0xFFFFFF, 0xD1FAE5, 0x26FFFFFF},
    {L"暮光紫", 0x8B5CF6, 0x6D28D9, 0xFFFFFF, 0xFFFFFF, 0xEDE9FE, 0x26FFFFFF},
    {L"琥珀橙", 0xFB923C, 0xC2410C, 0xFFFFFF, 0xFFFFFF, 0xFFEDD5, 0x26FFFFFF},
};
constexpr int kThemeCount = static_cast<int>(sizeof(kThemes) / sizeof(kThemes[0]));

const wchar_t* const kWeekdays[7] = {L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
const wchar_t* const kCjkFont = L"Microsoft YaHei UI";
const wchar_t* const kDigitFont = L"Segoe UI";

ID2D1Factory* g_d2d = nullptr;
IDWriteFactory* g_dw = nullptr;

D2D1_COLOR_F Rgb(UINT32 rgb, float a = 1.0f) {
    return D2D1::ColorF(rgb, a);
}

D2D1_COLOR_F Argb(UINT32 argb) {
    return D2D1::ColorF(argb & 0xFFFFFF, ((argb >> 24) & 0xFF) / 255.0f);
}

Com<IDWriteTextFormat> MakeFormat(const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size) {
    Com<IDWriteTextFormat> fmt;
    g_dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                           DWRITE_FONT_STRETCH_NORMAL, size, L"zh-cn", fmt.Put());
    if (fmt) fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return fmt;
}

// 单行文本：超长时显示省略号
void DrawLine(ID2D1RenderTarget* rt, const std::wstring& text, const wchar_t* family,
              DWRITE_FONT_WEIGHT weight, float size, D2D1_RECT_F box,
              ID2D1Brush* brush, DWRITE_TEXT_ALIGNMENT align) {
    Com<IDWriteTextFormat> fmt = MakeFormat(family, weight, size);
    if (!fmt) return;
    fmt->SetTextAlignment(align);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    Com<IDWriteInlineObject> ellipsis;
    g_dw->CreateEllipsisTrimmingSign(fmt.Get(), ellipsis.Put());
    fmt->SetTrimming(&trim, ellipsis.Get());
    rt->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt.Get(), box, brush,
                 D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// 主体文字："还有 123 天" / "就是今天" / "已到期"
struct Run {
    std::wstring text;
    const wchar_t* family;
    DWRITE_FONT_WEIGHT weight;
    float size;
};

Com<IDWriteTextLayout> BuildLayout(const Run* runs, int count, float scale, float maxW, float maxH) {
    std::wstring all;
    for (int i = 0; i < count; ++i) all += runs[i].text;

    Com<IDWriteTextFormat> fmt = MakeFormat(runs[0].family, runs[0].weight, runs[0].size * scale);
    Com<IDWriteTextLayout> layout;
    if (!fmt) return layout;
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_dw->CreateTextLayout(all.c_str(), static_cast<UINT32>(all.size()), fmt.Get(), maxW, maxH, layout.Put());
    if (!layout) return layout;

    UINT32 pos = 0;
    for (int i = 0; i < count; ++i) {
        DWRITE_TEXT_RANGE range{pos, static_cast<UINT32>(runs[i].text.size())};
        layout->SetFontFamilyName(runs[i].family, range);
        layout->SetFontWeight(runs[i].weight, range);
        layout->SetFontSize(runs[i].size * scale, range);
        pos += range.length;
    }
    return layout;
}

void DrawMain(ID2D1RenderTarget* rt, const Run* runs, int count, D2D1_RECT_F box,
              ID2D1Brush* brush, ID2D1Brush* shadow, float shadowOffset) {
    const float w = box.right - box.left, h = box.bottom - box.top;
    if (w <= 1 || h <= 1) return;

    // 先按理想字号排版，放不下就整体等比缩小
    Com<IDWriteTextLayout> layout = BuildLayout(runs, count, 1.0f, 100000.0f, 100000.0f);
    if (!layout) return;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    float scale = 1.0f;
    if (m.widthIncludingTrailingWhitespace > w) scale = std::min(scale, w / m.widthIncludingTrailingWhitespace);
    if (m.height > h) scale = std::min(scale, h / m.height);

    layout = BuildLayout(runs, count, scale * 0.98f, w, h);
    if (!layout) return;
    if (shadow)
        rt->DrawTextLayout(D2D1::Point2F(box.left + shadowOffset, box.top + shadowOffset), layout.Get(), shadow);
    rt->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout.Get(), brush);
}

}  // namespace

int ThemeCount() { return kThemeCount; }

const wchar_t* ThemeName(int index) {
    return (index >= 0 && index < kThemeCount) ? kThemes[index].name : L"";
}

bool RendererInit() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                 reinterpret_cast<void**>(&g_d2d))))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&g_dw))))
        return false;
    return true;
}

void RendererShutdown() {
    if (g_dw) { g_dw->Release(); g_dw = nullptr; }
    if (g_d2d) { g_d2d->Release(); g_d2d = nullptr; }
}

bool RenderCard(HWND hwnd, const CardData& card, const CountResult& count, int W, int H, bool editMode) {
    if (!g_d2d || !g_dw || W <= 0 || H <= 0) return false;
    const Theme& th = kThemes[(card.theme >= 0 && card.theme < kThemeCount) ? card.theme : 0];

    // ---- 32 位 DIB，作为 D2D 的绘制目标 ----
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;  // 自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC memDC = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memDC || !dib) {
        if (dib) DeleteObject(dib);
        if (memDC) DeleteDC(memDC);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(memDC, dib);

    // 软件渲染：不创建 D3D 设备，内存占用小；一天只画一次，速度无所谓
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    Com<ID2D1DCRenderTarget> rt;
    bool ok = SUCCEEDED(g_d2d->CreateDCRenderTarget(&props, rt.Put()));
    RECT bindRc{0, 0, W, H};
    ok = ok && SUCCEEDED(rt->BindDC(memDC, &bindRc));

    if (ok) {
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0.0f));
        // 透明背景上不能用 ClearType，用灰度抗锯齿
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        const float w = static_cast<float>(W), h = static_cast<float>(H);
        const float u = std::min(h, w * 0.62f);          // 排版基准尺寸
        const float radius = std::min(w, h) * 0.08f;
        const float pad = u * 0.09f;

        // ---- 背景 ----
        // 不透明度为 0 时仍保留极低 alpha，否则鼠标会穿透，没法拖动或右键
        const float bgAlpha = std::max(card.opacity / 100.0f, 2.0f / 255.0f);
        D2D1_GRADIENT_STOP stops[2] = {{0.0f, Rgb(th.bgTop, bgAlpha)}, {1.0f, Rgb(th.bgBottom, bgAlpha)}};
        Com<ID2D1GradientStopCollection> stopColl;
        rt->CreateGradientStopCollection(stops, 2, stopColl.Put());
        Com<ID2D1LinearGradientBrush> bg;
        if (stopColl)
            rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, h)),
                stopColl.Get(), bg.Put());
        const D2D1_ROUNDED_RECT shape = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), radius, radius);
        if (bg) rt->FillRoundedRectangle(shape, bg.Get());

        Com<ID2D1SolidColorBrush> border;
        UINT32 borderArgb = th.border;
        const UINT32 borderA = static_cast<UINT32>(((borderArgb >> 24) & 0xFF) * std::min(1.0f, card.opacity / 60.0f));
        borderArgb = (borderA << 24) | (borderArgb & 0xFFFFFF);
        rt->CreateSolidColorBrush(Argb(borderArgb), border.Put());
        if (border) rt->DrawRoundedRectangle(shape, border.Get(), 1.0f);

        // 背景很透明时给文字加阴影，保证在各种壁纸上都看得清
        Com<ID2D1SolidColorBrush> shadow;
        const float shadowOffset = std::max(1.0f, u * 0.008f);
        if (card.opacity < 50) rt->CreateSolidColorBrush(D2D1::ColorF(0, 0.45f), shadow.Put());

        Com<ID2D1SolidColorBrush> titleBr, numBr, subBr;
        rt->CreateSolidColorBrush(Rgb(th.title), titleBr.Put());
        rt->CreateSolidColorBrush(Rgb(th.number), numBr.Put());
        rt->CreateSolidColorBrush(Rgb(th.sub), subBr.Put());

        // ---- 标题 ----
        const float titleSize = u * 0.115f;
        const float titleH = titleSize * 1.45f;
        const D2D1_RECT_F titleBox = D2D1::RectF(pad, pad, w - pad, pad + titleH);
        if (shadow)
            DrawLine(rt.Get(), card.title, kCjkFont, DWRITE_FONT_WEIGHT_SEMI_BOLD, titleSize,
                     D2D1::RectF(titleBox.left + shadowOffset, titleBox.top + shadowOffset,
                                 titleBox.right + shadowOffset, titleBox.bottom + shadowOffset),
                     shadow.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawLine(rt.Get(), card.title, kCjkFont, DWRITE_FONT_WEIGHT_SEMI_BOLD, titleSize, titleBox,
                 titleBr.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);

        // ---- 底部日期 ----
        const float footSize = u * 0.085f;
        const float footH = footSize * 1.5f;
        wchar_t foot[96];
        swprintf(foot, 96, L"%d年%d月%d日 %ls%ls", count.target.y, count.target.m, count.target.d,
                 kWeekdays[Weekday(count.target)], card.repeat ? L" · 每年" : L"");
        const D2D1_RECT_F footBox = D2D1::RectF(pad, h - pad - footH, w - pad, h - pad);
        if (shadow)
            DrawLine(rt.Get(), foot, kCjkFont, DWRITE_FONT_WEIGHT_NORMAL, footSize,
                     D2D1::RectF(footBox.left + shadowOffset, footBox.top + shadowOffset,
                                 footBox.right + shadowOffset, footBox.bottom + shadowOffset),
                     shadow.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawLine(rt.Get(), foot, kCjkFont, DWRITE_FONT_WEIGHT_NORMAL, footSize, footBox, subBr.Get(),
                 DWRITE_TEXT_ALIGNMENT_LEADING);

        // ---- 主体 ----
        const D2D1_RECT_F mainBox = D2D1::RectF(pad, titleBox.bottom, w - pad, footBox.top);
        const float small = u * 0.12f, big = u * 0.42f;
        if (count.state == CountState::Future) {
            wchar_t num[32];
            swprintf(num, 32, L"%lld", count.days);
            const Run runs[] = {
                {L"还有 ", kCjkFont, DWRITE_FONT_WEIGHT_NORMAL, small},
                {num, kDigitFont, DWRITE_FONT_WEIGHT_SEMI_BOLD, big},
                {L" 天", kCjkFont, DWRITE_FONT_WEIGHT_NORMAL, small},
            };
            DrawMain(rt.Get(), runs, 3, mainBox, numBr.Get(), shadow.Get(), shadowOffset);
        } else {
            const Run run = {count.state == CountState::Today ? L"就是今天" : L"已到期", kCjkFont,
                             DWRITE_FONT_WEIGHT_SEMI_BOLD, u * 0.24f};
            DrawMain(rt.Get(), &run, 1, mainBox, numBr.Get(), shadow.Get(), shadowOffset);
        }

        // ---- 编辑模式：虚线框 + 右下角缩放手柄 ----
        if (editMode) {
            Com<ID2D1StrokeStyle> dash;
            g_d2d->CreateStrokeStyle(
                D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                            D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_DASH, 0.0f),
                nullptr, 0, dash.Put());
            Com<ID2D1SolidColorBrush> edit;
            rt->CreateSolidColorBrush(D2D1::ColorF(0xF59E0B), edit.Put());
            const float stroke = std::max(2.0f, u * 0.008f);
            const float in = stroke * 1.5f;
            if (edit) {
                rt->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(in, in, w - in, h - in), radius, radius), edit.Get(), stroke, dash.Get());
                const float g = std::max(10.0f, u * 0.05f);
                for (int i = 1; i <= 3; ++i) {
                    const float d = g * i / 3.0f;
                    rt->DrawLine(D2D1::Point2F(w - in * 2 - d, h - in * 2), D2D1::Point2F(w - in * 2, h - in * 2 - d),
                                 edit.Get(), stroke);
                }
            }
        }

        ok = SUCCEEDED(rt->EndDraw());
    }

    if (ok) {
        // 子窗口：位置由 SetWindowPos 管理，这里只提交内容
        SIZE size{W, H};
        POINT src{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        ok = UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, memDC, &src, 0, &blend, ULW_ALPHA) != 0;
    }

    rt.Reset();
    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
    return ok;
}
