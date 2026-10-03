// main/jianlu_store.c —— 见 jianlu_store.h。纯 C,host 侧测试覆盖。
#include "jianlu_store.h"

#include <string.h>

void jianlu_utf8_copy(char *dst, size_t dst_size, const char *src, size_t max_bytes)
{
    if (dst_size == 0) return;
    if (src == NULL) src = "";

    size_t limit = dst_size - 1;
    if (max_bytes < limit) limit = max_bytes;

    size_t n = 0;
    while (n < limit && src[n] != '\0') n++;
    // 回退到字符边界:UTF-8 续字节形如 10xxxxxx,尾部不能留半个字。
    while (n > 0 && (src[n] & 0xC0) == 0x80) n--;

    memcpy(dst, src, n);
    dst[n] = '\0';
}

void jianlu_store_init(jianlu_store_t *store)
{
    memset(store, 0, sizeof(*store));
    store->view = JIANLU_VIEW_BOOT;
}

void jianlu_store_set_view(jianlu_store_t *store, jianlu_view_t view, const char *err)
{
    store->view = view;
    jianlu_utf8_copy(store->error, sizeof(store->error), err, sizeof(store->error) - 1);
}

void jianlu_store_replace_begin(jianlu_store_t *store)
{
    store->count = 0;
    store->selected = 0;
}

bool jianlu_store_add(jianlu_store_t *store, const char *id, const char *title,
                      const char *summary, jianlu_type_t type,
                      const char *date, const char *tag1, const char *tag2)
{
    if (store->count >= JIANLU_MAX_RECORDS) return false;

    jianlu_record_t *rec = &store->records[store->count];
    jianlu_utf8_copy(rec->id, sizeof(rec->id), id, sizeof(rec->id) - 1);
    jianlu_utf8_copy(rec->title, sizeof(rec->title), title, sizeof(rec->title) - 1);
    jianlu_utf8_copy(rec->summary, sizeof(rec->summary), summary, sizeof(rec->summary) - 1);
    rec->type = type;
    rec->completing = false;
    // 日期只取 "YYYY-MM-DD" 前 10 字节(纯 ASCII,不涉及 UTF-8 边界)
    jianlu_utf8_copy(rec->date, sizeof(rec->date), date, 10);
    rec->tag_count = 0;
    if (tag1 != NULL && tag1[0] != '\0') {
        jianlu_utf8_copy(rec->tags[rec->tag_count], JIANLU_TAG_LEN, tag1, JIANLU_TAG_LEN - 1);
        rec->tag_count++;
    }
    if (tag2 != NULL && tag2[0] != '\0' && rec->tag_count < JIANLU_MAX_TAGS) {
        jianlu_utf8_copy(rec->tags[rec->tag_count], JIANLU_TAG_LEN, tag2, JIANLU_TAG_LEN - 1);
        rec->tag_count++;
    }
    store->count++;
    return true;
}

void jianlu_store_replace_end(jianlu_store_t *store)
{
    if (store->count == 0) {
        store->selected = 0;
    } else if (store->selected >= store->count) {
        store->selected = store->count - 1;
    }
}

void jianlu_store_move(jianlu_store_t *store, int delta)
{
    if (store->count <= 0) return;
    int next = (store->selected + delta) % store->count;
    if (next < 0) next += store->count;
    store->selected = next;
}

const jianlu_record_t *jianlu_store_selected(const jianlu_store_t *store)
{
    if (store->count == 0 || store->selected < 0 || store->selected >= store->count) {
        return NULL;
    }
    return &store->records[store->selected];
}

bool jianlu_store_set_completing(jianlu_store_t *store, const char *id, bool completing)
{
    if (id == NULL) return false;
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->records[i].id, id) == 0) {
            store->records[i].completing = completing;
            return true;
        }
    }
    return false;
}

bool jianlu_store_remove(jianlu_store_t *store, const char *id)
{
    if (id == NULL) return false;
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->records[i].id, id) != 0) continue;
        memmove(&store->records[i], &store->records[i + 1],
                (size_t)(store->count - i - 1) * sizeof(store->records[0]));
        store->count--;
        if (store->selected >= store->count) {
            store->selected = store->count > 0 ? store->count - 1 : 0;
        }
        return true;
    }
    return false;
}

jianlu_type_t jianlu_type_from_string(const char *type)
{
    if (type == NULL) return JIANLU_TYPE_OTHER;
    if (strcmp(type, "todo") == 0) return JIANLU_TYPE_TODO;
    if (strcmp(type, "article") == 0) return JIANLU_TYPE_ARTICLE;
    if (strcmp(type, "inspiration") == 0) return JIANLU_TYPE_INSPIRATION;
    return JIANLU_TYPE_OTHER;
}

const char *jianlu_type_badge(jianlu_type_t type)
{
    switch (type) {
    case JIANLU_TYPE_TODO:        return "办";
    case JIANLU_TYPE_ARTICLE:     return "读";
    case JIANLU_TYPE_INSPIRATION: return "感";
    default:                      return "其";
    }
}
