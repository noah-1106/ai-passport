// main/jianlu_timefmt.c —— 见 jianlu_timefmt.h。无 libc time 依赖,host 可测。
#include "jianlu_timefmt.h"

#include <stdio.h>
#include <string.h>

static const char *MONTHS[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

static int month_from_name(const char *name)
{
    for (int i = 0; i < 12; i++) {
        if (memcmp(name, MONTHS[i], 3) == 0) return i + 1;
    }
    return 0;
}

static bool leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

// "Thu, 09 Oct 2026 07:00:00 GMT" → epoch
uint32_t jianlu_time_parse_http_date(const char *date)
{
    if (date == NULL) return 0;
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    char mon[4] = { 0 };
    // 固定格式:跳过星期逗号,读 "09 Oct 2026 07:00:00"
    const char *comma = strchr(date, ',');
    if (comma == NULL) return 0;
    if (sscanf(comma + 1, " %d %3s %d %d:%d:%d", &day, mon, &year,
               &hh, &mm, &ss) != 6) {
        return 0;
    }
    int month = month_from_name(mon);
    static const int MDAYS[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (month == 0 || day < 1 || day > MDAYS[month - 1] + (month == 2 ? 1 : 0) ||
        year < 1970 || year > 2200 || hh > 23 || mm > 59 || ss > 60) {
        return 0;
    }
    uint32_t days = 0;
    for (int y = 1970; y < year; y++) days += leap(y) ? 366 : 365;
    for (int m = 1; m < month; m++) {
        days += (uint32_t)MDAYS[m - 1];
        if (m == 2 && leap(year)) days += 1;
    }
    days += (uint32_t)(day - 1);
    return days * 86400u + (uint32_t)(hh * 3600 + mm * 60 + ss);
}

bool jianlu_time_is_valid(uint32_t epoch)
{
    return epoch >= JIANLU_TIME_VALID_EPOCH;
}

// epoch → y/m/d/hh:mm(UTC+8 固定偏移)
void jianlu_time_format_mmdd_hhmm(uint32_t epoch, char *buf, size_t len)
{
    if (len < 15) {
        if (len > 0) buf[0] = '\0';
        return;
    }
    epoch += 8 * 3600;
    uint32_t days = epoch / 86400u;
    uint32_t secs = epoch % 86400u;
    int hh = (int)(secs / 3600);
    int mm = (int)((secs % 3600) / 60);

    int year = 1970;
    static const int MDAYS[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    while (true) {
        int yl = leap(year) ? 366 : 365;
        if ((int)days < yl) break;
        days -= (uint32_t)yl;
        year++;
    }
    int month = 1;
    while (true) {
        int ml = MDAYS[month - 1] + (month == 2 && leap(year) ? 1 : 0);
        if ((int)days < ml) break;
        days -= (uint32_t)ml;
        month++;
    }
    snprintf(buf, len, "%02d-%02d %02d:%02d", month, (int)days + 1, hh, mm);
}
