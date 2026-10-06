// tests/test_jianlu_home.c —— 主页视图模型装配的 host 侧测试。
#include <assert.h>
#include <string.h>

#include "jianlu_home.h"

static void fill(jianlu_store_t *s) {
    jianlu_store_init(s);
    jianlu_store_replace_begin(s);
    jianlu_store_add(s, "1", "语音占位之前的文章", "s", JIANLU_TYPE_ARTICLE,
                     "2026-10-05 09:00:00", NULL, NULL);
    jianlu_store_add(s, "2", "给汽车换机油", "s", JIANLU_TYPE_TODO,
                     "2026-10-07 10:00:00", NULL, NULL);
    jianlu_store_replace_end(s);
}

int main(void) {
    jianlu_home_model_t m;
    jianlu_store_t s;

    // first_todo:跳过占位卡与非 todo
    fill(&s);
    jianlu_store_set_voice_placeholders(&s, 2);
    const jianlu_record_t *todo = jianlu_store_first_todo(&s);
    assert(todo != NULL);
    assert(strcmp(todo->id, "2") == 0);

    // 装配:已对时 + 有待同步 + 离线
    s.offline = true;
    // 2026-10-08 21:00:00 UTC = 20734 天 x 86400 + 75600
    jianlu_home_build(&s, 3, 1791493200u, &m);
    assert(m.offline);
    assert(m.pending_sync == 3);
    assert(m.has_todo);
    assert(strcmp(m.todo_title, "给汽车换机油") == 0);
    assert(strcmp(m.todo_date, "10-07") == 0);
    // UTC 21:00 → UTC+8 05:00(次日)
    assert(strcmp(m.time_hhmm, "05:00") == 0);

    // 未对时回退 "--:--";无待办;无待同步
    jianlu_store_init(&s);
    jianlu_home_build(&s, 0, 0, &m);
    assert(strcmp(m.time_hhmm, "--:--") == 0);
    assert(!m.has_todo);
    assert(m.pending_sync == 0);
    assert(!m.offline);

    // 空清单 first_todo 为 NULL
    assert(jianlu_store_first_todo(&s) == NULL);
    assert(jianlu_store_first_todo(NULL) == NULL);
    return 0;
}
