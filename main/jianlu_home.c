// main/jianlu_home.c —— 见 jianlu_home.h。
#include "jianlu_home.h"

#include <string.h>

#include "jianlu_timefmt.h"

const jianlu_record_t *jianlu_store_first_todo(const jianlu_store_t *store)
{
    if (store == NULL) return NULL;
    for (int i = 0; i < store->count; i++) {
        const jianlu_record_t *rec = &store->records[i];
        if (rec->voice_slot > 0) continue;   // 跳过语音占位卡
        if (rec->type == JIANLU_TYPE_TODO) return rec;
    }
    return NULL;
}

void jianlu_home_build(const jianlu_store_t *store, int pending_sync,
                       uint32_t epoch, jianlu_home_model_t *out)
{
    memset(out, 0, sizeof(*out));
    if (jianlu_time_is_valid(epoch)) {
        // 复用 MM-DD HH:mm 取后 5 位
        char full[16];
        jianlu_time_format_mmdd_hhmm(epoch, full, sizeof(full));
        jianlu_utf8_copy(out->time_hhmm, sizeof(out->time_hhmm),
                         full + 6, sizeof(out->time_hhmm) - 1);
    } else {
        jianlu_utf8_copy(out->time_hhmm, sizeof(out->time_hhmm), "--:--", 5);
    }
    out->offline = store != NULL && store->offline;
    out->pending_sync = pending_sync;

    const jianlu_record_t *todo = jianlu_store_first_todo(store);
    if (todo != NULL) {
        out->has_todo = true;
        jianlu_utf8_copy(out->todo_title, sizeof(out->todo_title), todo->title,
                         sizeof(out->todo_title) - 1);
        // "2026-10-06" → "10-06"
        jianlu_utf8_copy(out->todo_date, sizeof(out->todo_date),
                         strlen(todo->date) >= 10 ? todo->date + 5 : todo->date,
                         sizeof(out->todo_date) - 1);
    }
}
