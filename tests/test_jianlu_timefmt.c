// tests/test_jianlu_timefmt.c —— HTTP Date 解析与本地时间格式化的 host 侧测试。
#include <assert.h>
#include <string.h>

#include "jianlu_timefmt.h"

int main(void) {
    // 标准格式
    uint32_t e = jianlu_time_parse_http_date("Thu, 09 Oct 2026 07:00:00 GMT");
    assert(e != 0);
    assert(jianlu_time_is_valid(e));
    // 独立验算:2026-10-09 07:00:00 UTC
    // 2026-01-01 = 56*365+14 闰 = 20454 天;+281 天到 10-09;= 20735 天
    assert(e == 20735u * 86400u + 7u * 3600u);

    // 星期无关/单数字日
    assert(jianlu_time_parse_http_date("Mon, 1 Jan 2025 00:00:00 GMT")
           == 1735689600u);
    // 垃圾输入
    assert(jianlu_time_parse_http_date(NULL) == 0);
    assert(jianlu_time_parse_http_date("") == 0);
    assert(jianlu_time_parse_http_date("not a date") == 0);
    assert(jianlu_time_parse_http_date("Thu, 32 Oct 2026 07:00:00 GMT") == 0);
    assert(jianlu_time_parse_http_date("Thu, 09 Foo 2026 07:00:00 GMT") == 0);

    // 有效性阈值
    assert(!jianlu_time_is_valid(0));
    assert(jianlu_time_is_valid(JIANLU_TIME_VALID_EPOCH));

    // 格式化:固定 UTC+8
    char buf[16];
    // 2026-10-09 07:00:00 UTC → 15:00 +8
    jianlu_time_format_mmdd_hhmm(e, buf, sizeof(buf));
    assert(strcmp(buf, "10-09 15:00") == 0);
    // 跨日:2025-01-01 00:00:00 UTC → 08:00
    jianlu_time_format_mmdd_hhmm(1735689600u, buf, sizeof(buf));
    assert(strcmp(buf, "01-01 08:00") == 0);
    // 跨月:2025-02-28 20:00 UTC → 03-01 04:00
    jianlu_time_format_mmdd_hhmm(1735689600u + 58u * 86400u + 20u * 3600u,
                                 buf, sizeof(buf));
    assert(strcmp(buf, "03-01 04:00") == 0);
    return 0;
}
