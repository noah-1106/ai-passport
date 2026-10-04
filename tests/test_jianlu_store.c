// tests/test_jianlu_store.c —— 小诺简录清单状态机与 UTF-8 截断的 host 侧测试。
#include <assert.h>
#include <string.h>

#include "jianlu_store.h"

static void test_utf8_copy(void) {
    char buf[8];

    // ASCII:受 max_bytes 截断
    jianlu_utf8_copy(buf, sizeof(buf), "abcdefgh", 3);
    assert(strcmp(buf, "abc") == 0);

    // 中文 3 字节/字:max_bytes=4 只能容下 1 个字,不能切成 1.33 个
    jianlu_utf8_copy(buf, sizeof(buf), "牙医复诊", 4);
    assert(strcmp(buf, "牙") == 0);

    // dst 容量优先于 max_bytes
    jianlu_utf8_copy(buf, 4, "牙医复诊", 100);
    assert(strcmp(buf, "牙") == 0);

    // 恰好落在字符边界时不回退
    jianlu_utf8_copy(buf, sizeof(buf), "牙医复诊", 6);
    assert(strcmp(buf, "牙医") == 0);

    // NULL 源得到空串
    jianlu_utf8_copy(buf, sizeof(buf), NULL, 4);
    assert(buf[0] == '\0');

    // 混合中英文:在 'A' 前停下
    jianlu_utf8_copy(buf, sizeof(buf), "牙A医", 4);
    assert(strcmp(buf, "牙A") == 0);
}

static void test_add_move_wrap(void) {
    jianlu_store_t store;
    jianlu_store_init(&store);
    assert(store.view == JIANLU_VIEW_BOOT);
    assert(jianlu_store_selected(&store) == NULL);

    jianlu_store_replace_begin(&store);
    assert(jianlu_store_add(&store, "id1", "牙医复诊", "周三上午", JIANLU_TYPE_TODO,
                            "2026-10-03 11:53:28", "健康", "预约"));
    assert(strcmp(store.records[0].date, "2026-10-03") == 0);   // 只留前 10 字节
    assert(store.records[0].tag_count == 2);
    assert(strcmp(store.records[0].tags[0], "健康") == 0);
    assert(strcmp(store.records[0].tags[1], "预约") == 0);
    assert(jianlu_store_add(&store, "id2", "读:固件指南", "第 3 章", JIANLU_TYPE_ARTICLE, NULL, NULL, NULL));
    assert(jianlu_store_add(&store, "id3", "灵感", "做成卡片", JIANLU_TYPE_INSPIRATION, NULL, NULL, NULL));
    jianlu_store_replace_end(&store);
    assert(store.count == 3);
    assert(store.selected == 0);

    // 上移到顶再上移 = 循环到尾
    jianlu_store_move(&store, -1);
    assert(store.selected == 2);
    jianlu_store_move(&store, 1);
    assert(store.selected == 0);
    jianlu_store_move(&store, 1);
    assert(store.selected == 1);
    assert(strcmp(jianlu_store_selected(&store)->id, "id2") == 0);
}

static void test_capacity(void) {
    jianlu_store_t store;
    jianlu_store_init(&store);
    jianlu_store_replace_begin(&store);
    for (int i = 0; i < JIANLU_MAX_RECORDS; i++) {
        assert(jianlu_store_add(&store, "x", "t", "s", JIANLU_TYPE_OTHER, NULL, NULL, NULL));
    }
    // 满了返回 false,不越界
    assert(!jianlu_store_add(&store, "x", "t", "s", JIANLU_TYPE_OTHER, NULL, NULL, NULL));
    assert(store.count == JIANLU_MAX_RECORDS);
}

static void test_remove_clamps_selection(void) {
    jianlu_store_t store;
    jianlu_store_init(&store);
    jianlu_store_replace_begin(&store);
    jianlu_store_add(&store, "a", "t", "s", JIANLU_TYPE_TODO, NULL, NULL, NULL);
    jianlu_store_add(&store, "b", "t", "s", JIANLU_TYPE_TODO, NULL, NULL, NULL);
    jianlu_store_add(&store, "c", "t", "s", JIANLU_TYPE_TODO, NULL, NULL, NULL);
    jianlu_store_replace_end(&store);

    store.selected = 2;
    assert(jianlu_store_remove(&store, "c"));
    assert(store.count == 2);
    assert(store.selected == 1);   // 尾删后收敛到最后一条

    assert(jianlu_store_remove(&store, "a"));
    assert(jianlu_store_remove(&store, "b"));
    assert(store.count == 0);
    assert(store.selected == 0);
    assert(jianlu_store_selected(&store) == NULL);
    assert(!jianlu_store_remove(&store, "a"));
    // 空清单移动是安全的空操作
    jianlu_store_move(&store, 1);
}

static void test_completing_and_types(void) {
    jianlu_store_t store;
    jianlu_store_init(&store);
    jianlu_store_replace_begin(&store);
    jianlu_store_add(&store, "a", "t", "s", JIANLU_TYPE_TODO, NULL, NULL, NULL);
    jianlu_store_replace_end(&store);

    assert(jianlu_store_set_completing(&store, "a", true));
    assert(store.records[0].completing);
    assert(jianlu_store_set_completing(&store, "a", false));
    assert(!store.records[0].completing);
    assert(!jianlu_store_set_completing(&store, "nobody", true));

    assert(jianlu_type_from_string("todo") == JIANLU_TYPE_TODO);
    assert(jianlu_type_from_string("article") == JIANLU_TYPE_ARTICLE);
    assert(jianlu_type_from_string("inspiration") == JIANLU_TYPE_INSPIRATION);
    assert(jianlu_type_from_string("unknown") == JIANLU_TYPE_OTHER);
    assert(jianlu_type_from_string(NULL) == JIANLU_TYPE_OTHER);
    assert(strcmp(jianlu_type_badge(JIANLU_TYPE_TODO), "办") == 0);
}

static void test_view_error(void) {
    jianlu_store_t store;
    jianlu_store_init(&store);
    jianlu_store_set_view(&store, JIANLU_VIEW_ERROR, "连不上中枢");
    assert(store.view == JIANLU_VIEW_ERROR);
    assert(strcmp(store.error, "连不上中枢") == 0);
    jianlu_store_set_view(&store, JIANLU_VIEW_READY, NULL);
    assert(store.error[0] == '\0');
}

static void test_voice_placeholder(void) {
    jianlu_store_t store;
    jianlu_store_init(&store);
    jianlu_store_replace_begin(&store);
    jianlu_store_add(&store, "a", "t1", "s", JIANLU_TYPE_TODO, NULL, NULL, NULL);
    jianlu_store_add(&store, "b", "t2", "s", JIANLU_TYPE_TODO, NULL, NULL, NULL);
    jianlu_store_replace_end(&store);
    store.selected = 1;

    // 3 张占位卡置顶(对应槽 1..3),选中跟随下移
    assert(jianlu_store_set_voice_placeholders(&store, 3) == 3);
    assert(store.count == 5);
    assert(store.selected == 4);
    for (int i = 0; i < 3; i++) {
        assert(jianlu_store_is_voice_placeholder(&store.records[i]));
        assert(store.records[i].voice_slot == i + 1);
        assert(strcmp(store.records[i].title, "语音 · 未识别") == 0);
    }
    assert(!jianlu_store_is_voice_placeholder(&store.records[3]));
    assert(strcmp(jianlu_store_selected(&store)->id, "b") == 0);

    // 精确同步为 1 张:多余的消失,槽位重排为 1
    assert(jianlu_store_set_voice_placeholders(&store, 1) == 1);
    assert(store.count == 3);
    assert(store.records[0].voice_slot == 1);
    assert(!jianlu_store_is_voice_placeholder(&store.records[1]));

    // 清空
    assert(jianlu_store_set_voice_placeholders(&store, 0) == 0);
    assert(store.count == 2);
    assert(strcmp(jianlu_store_selected(&store)->id, "b") == 0);
    assert(!jianlu_store_is_voice_placeholder(NULL));
}

int main(void) {
    test_utf8_copy();
    test_add_move_wrap();
    test_capacity();
    test_remove_clamps_selection();
    test_completing_and_types();
    test_view_error();
    test_voice_placeholder();
    return 0;
}
