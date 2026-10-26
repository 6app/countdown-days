// 日期与天数计算
#pragma once

#include "app.h"

struct Date {
    int y = 1970, m = 1, d = 1;
};

enum class CountState {
    Future,   // 还有 N 天
    Today,    // 就是今天
    Expired,  // 已到期（非重复且日期已过）
};

struct CountResult {
    CountState state = CountState::Future;
    long long days = 0;   // 距离目标的天数（Future 时 > 0）
    Date target;          // 实际目标日（每年重复时是下一次的日期）
};

Date LocalToday();
bool IsLeapYear(int y);
int DaysInMonth(int y, int m);
long long DaysFromCivil(int y, int m, int d);   // 1970-01-01 起的天数
Date CivilFromDays(long long z);
int Weekday(const Date& d);                     // 0 = 周日
CountResult ComputeCount(const CardData& card, const Date& today);

// 距离下一个本地零点的毫秒数（多加 2 秒余量，避免早触发）
UINT MsUntilNextMidnight();
