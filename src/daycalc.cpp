#include "daycalc.h"

Date LocalToday() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return Date{st.wYear, st.wMonth, st.wDay};
}

bool IsLeapYear(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int DaysInMonth(int y, int m) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 30;
    return (m == 2 && IsLeapYear(y)) ? 29 : kDays[m - 1];
}

// Howard Hinnant 的 days_from_civil 算法
long long DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

Date CivilFromDays(long long z) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int y = static_cast<int>(static_cast<long long>(yoe) + era * 400 + (m <= 2));
    return Date{y, m, d};
}

int Weekday(const Date& d) {
    const long long z = DaysFromCivil(d.y, d.m, d.d);
    return static_cast<int>(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

// 每年重复时某一年里的对应日期；2 月 29 日在平年落到 2 月 28 日
static Date OccurrenceIn(int year, int m, int d) {
    const int dim = DaysInMonth(year, m);
    return Date{year, m, d > dim ? dim : d};
}

CountResult ComputeCount(const CardData& card, const Date& today) {
    CountResult r;
    const long long t0 = DaysFromCivil(today.y, today.m, today.d);

    Date target{card.year, card.month, card.day};
    if (card.repeat) {
        target = OccurrenceIn(today.y, card.month, card.day);
        if (DaysFromCivil(target.y, target.m, target.d) < t0)
            target = OccurrenceIn(today.y + 1, card.month, card.day);
    }
    r.target = target;
    r.days = DaysFromCivil(target.y, target.m, target.d) - t0;
    if (r.days > 0)
        r.state = CountState::Future;
    else if (r.days == 0)
        r.state = CountState::Today;
    else
        r.state = CountState::Expired;
    return r;
}

UINT MsUntilNextMidnight() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const ULONGLONG msOfDay = ((st.wHour * 60ULL + st.wMinute) * 60ULL + st.wSecond) * 1000ULL + st.wMilliseconds;
    const ULONGLONG day = 24ULL * 3600ULL * 1000ULL;
    ULONGLONG ms = day - msOfDay + 2000;  // 零点后 2 秒
    // 夏令时等情况下当天可能不是 24 小时；零点触发后会再检查日期是否真的变了
    if (ms > day + 3600ULL * 1000ULL) ms = day;
    return static_cast<UINT>(ms);
}
