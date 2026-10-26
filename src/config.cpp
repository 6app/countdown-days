#include "config.h"
#include "daycalc.h"
#include "renderer.h"

#include <cwchar>

namespace {

// ---------- 极简 JSON（只覆盖本程序需要的子集，足够健壮即可） ----------

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::wstring s;
    std::vector<JVal> arr;
    std::vector<std::pair<std::wstring, JVal>> obj;

    const JVal* Get(const wchar_t* key) const {
        if (type != Obj) return nullptr;
        for (auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(const std::wstring& text) : p_(text.c_str()), end_(text.c_str() + text.size()) {}

    bool Parse(JVal& out) {
        if (!Value(out, 0)) return false;
        Ws();
        return p_ == end_;
    }

private:
    const wchar_t* p_;
    const wchar_t* end_;

    void Ws() {
        while (p_ < end_ && (*p_ == L' ' || *p_ == L'\t' || *p_ == L'\r' || *p_ == L'\n')) ++p_;
    }

    bool Lit(const wchar_t* lit) {
        const size_t len = wcslen(lit);
        if (static_cast<size_t>(end_ - p_) < len || wcsncmp(p_, lit, len) != 0) return false;
        p_ += len;
        return true;
    }

    static int Hex(wchar_t c) {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    }

    bool String(std::wstring& out) {
        if (p_ >= end_ || *p_ != L'"') return false;
        ++p_;
        while (p_ < end_ && *p_ != L'"') {
            wchar_t c = *p_++;
            if (c != L'\\') {
                out.push_back(c);
                continue;
            }
            if (p_ >= end_) return false;
            c = *p_++;
            switch (c) {
                case L'"': out.push_back(L'"'); break;
                case L'\\': out.push_back(L'\\'); break;
                case L'/': out.push_back(L'/'); break;
                case L'b': out.push_back(L'\b'); break;
                case L'f': out.push_back(L'\f'); break;
                case L'n': out.push_back(L'\n'); break;
                case L'r': out.push_back(L'\r'); break;
                case L't': out.push_back(L'\t'); break;
                case L'u': {
                    if (end_ - p_ < 4) return false;
                    int v = 0;
                    for (int i = 0; i < 4; ++i) {
                        const int h = Hex(p_[i]);
                        if (h < 0) return false;
                        v = v * 16 + h;
                    }
                    p_ += 4;
                    out.push_back(static_cast<wchar_t>(v));  // UTF-16 代理对原样保留
                    break;
                }
                default: return false;
            }
        }
        if (p_ >= end_) return false;
        ++p_;  // 结尾引号
        return true;
    }

    bool Value(JVal& v, int depth) {
        if (depth > 32) return false;
        Ws();
        if (p_ >= end_) return false;
        const wchar_t c = *p_;
        if (c == L'{') {
            ++p_;
            v.type = JVal::Obj;
            Ws();
            if (p_ < end_ && *p_ == L'}') { ++p_; return true; }
            for (;;) {
                Ws();
                std::pair<std::wstring, JVal> kv;
                if (!String(kv.first)) return false;
                Ws();
                if (p_ >= end_ || *p_ != L':') return false;
                ++p_;
                if (!Value(kv.second, depth + 1)) return false;
                v.obj.push_back(std::move(kv));
                Ws();
                if (p_ < end_ && *p_ == L',') { ++p_; continue; }
                if (p_ < end_ && *p_ == L'}') { ++p_; return true; }
                return false;
            }
        }
        if (c == L'[') {
            ++p_;
            v.type = JVal::Arr;
            Ws();
            if (p_ < end_ && *p_ == L']') { ++p_; return true; }
            for (;;) {
                JVal item;
                if (!Value(item, depth + 1)) return false;
                v.arr.push_back(std::move(item));
                Ws();
                if (p_ < end_ && *p_ == L',') { ++p_; continue; }
                if (p_ < end_ && *p_ == L']') { ++p_; return true; }
                return false;
            }
        }
        if (c == L'"') {
            v.type = JVal::Str;
            return String(v.s);
        }
        if (Lit(L"true")) { v.type = JVal::Bool; v.b = true; return true; }
        if (Lit(L"false")) { v.type = JVal::Bool; v.b = false; return true; }
        if (Lit(L"null")) { v.type = JVal::Null; return true; }

        wchar_t* stop = nullptr;
        v.n = wcstod(p_, &stop);
        if (stop == p_) return false;
        p_ = stop;
        v.type = JVal::Num;
        return true;
    }
};

void AppendEscaped(std::wstring& out, const std::wstring& s) {
    out.push_back(L'"');
    for (wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back(L'"');
}

int GetInt(const JVal& o, const wchar_t* key, int def) {
    const JVal* v = o.Get(key);
    return (v && v->type == JVal::Num) ? static_cast<int>(v->n) : def;
}

bool GetBool(const JVal& o, const wchar_t* key, bool def) {
    const JVal* v = o.Get(key);
    return (v && v->type == JVal::Bool) ? v->b : def;
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------- 文件读写 ----------

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf, n);
    const size_t pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : s.substr(0, pos);
}

bool ReadFileUtf8(const std::wstring& path, std::wstring& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (4 << 20);
    std::string bytes;
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = bytes.empty() || (ReadFile(f, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
    }
    CloseHandle(f);
    if (!ok) return false;

    size_t start = 0;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
        start = 3;  // 跳过 BOM（用户可能用记事本改过）
    const int srcLen = static_cast<int>(bytes.size() - start);
    if (srcLen == 0) { out.clear(); return true; }
    const int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + start, srcLen, nullptr, 0);
    if (n <= 0) return false;
    out.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, bytes.data() + start, srcLen, &out[0], n);
    return true;
}

bool WriteFileUtf8(const std::wstring& path, const std::wstring& text) {
    std::string bytes;
    if (!text.empty()) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        bytes.resize(n);
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &bytes[0], n, nullptr, nullptr);
    }
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = bytes.empty() || (WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size());
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

}  // namespace

std::wstring ConfigPath() {
    static std::wstring cached;
    if (!cached.empty()) return cached;

    const std::wstring portable = ExeDir() + L"\\config.json";
    if (GetFileAttributesW(portable.c_str()) != INVALID_FILE_ATTRIBUTES) {
        cached = portable;
        return cached;
    }
    wchar_t appdata[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        cached = portable;
        return cached;
    }
    std::wstring dir = std::wstring(appdata) + L"\\CountDay";
    CreateDirectoryW(dir.c_str(), nullptr);
    cached = dir + L"\\config.json";
    return cached;
}

bool LoadConfig(AppConfig& cfg) {
    std::wstring text;
    if (!ReadFileUtf8(ConfigPath(), text)) return false;
    JVal root;
    if (!Parser(text).Parse(root) || root.type != JVal::Obj) return false;

    cfg.locked = GetBool(root, L"locked", true);
    cfg.cards.clear();
    const JVal* cards = root.Get(L"cards");
    if (!cards || cards->type != JVal::Arr) return true;

    for (const JVal& c : cards->arr) {
        if (c.type != JVal::Obj) continue;
        CardData d;
        if (const JVal* t = c.Get(L"title"); t && t->type == JVal::Str) d.title = t->s;
        d.year = Clamp(GetInt(c, L"year", d.year), 1601, 9999);
        d.month = Clamp(GetInt(c, L"month", d.month), 1, 12);
        d.day = Clamp(GetInt(c, L"day", d.day), 1, 31);
        if (!(d.month == 2 && d.day == 29))  // 2/29 保留，给每年重复用
            d.day = Clamp(d.day, 1, DaysInMonth(d.year, d.month));
        d.repeat = GetBool(c, L"repeat", false);
        d.x = GetInt(c, L"x", d.x);
        d.y = GetInt(c, L"y", d.y);
        d.w = Clamp(GetInt(c, L"w", 0), 0, 8000);
        d.h = Clamp(GetInt(c, L"h", 0), 0, 8000);
        d.theme = Clamp(GetInt(c, L"theme", 0), 0, ThemeCount() - 1);
        d.opacity = Clamp(GetInt(c, L"opacity", d.opacity), 10, 100);
        cfg.cards.push_back(std::move(d));
    }
    return true;
}

bool SaveConfig(const AppConfig& cfg) {
    std::wstring out = L"{\n  \"version\": 1,\n  \"locked\": ";
    out += cfg.locked ? L"true" : L"false";
    out += L",\n  \"cards\": [";
    for (size_t i = 0; i < cfg.cards.size(); ++i) {
        const CardData& d = cfg.cards[i];
        out += i ? L",\n    {" : L"\n    {";
        out += L"\"title\": ";
        AppendEscaped(out, d.title);
        wchar_t buf[256];
        swprintf(buf, 256,
                 L", \"year\": %d, \"month\": %d, \"day\": %d, \"repeat\": %ls, "
                 L"\"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"theme\": %d, \"opacity\": %d}",
                 d.year, d.month, d.day, d.repeat ? L"true" : L"false",
                 d.x, d.y, d.w, d.h, d.theme, d.opacity);
        out += buf;
    }
    out += cfg.cards.empty() ? L"]\n}\n" : L"\n  ]\n}\n";
    return WriteFileUtf8(ConfigPath(), out);
}
