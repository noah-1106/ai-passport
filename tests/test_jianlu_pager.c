// tests/test_jianlu_pager.c —— 分页器纯逻辑的 host 侧测试。
#include <assert.h>
#include <string.h>

#include "jianlu_pager.h"

static void test_empty_and_short(void) {
    jianlu_pager_t p;
    jianlu_pager_init(&p, NULL, 22, 6);
    assert(p.pages == 0);
    jianlu_pager_init(&p, "", 22, 6);
    assert(p.pages == 0);

    jianlu_pager_init(&p, "短句", 22, 6);
    assert(p.pages == 1);
    assert(p.page == 0);
    char buf[128];
    jianlu_pager_page_text(&p, buf, sizeof(buf));
    assert(strcmp(buf, "短句") == 0);
    // 单页时翻页无效
    assert(!jianlu_pager_next(&p));
    assert(!jianlu_pager_prev(&p));
}

static void test_ascii_wrap_and_pages(void) {
    // 40 个 ASCII 字符,cols=10 → 每行 10 单元,rows=2 → 每页 2 行=20 字符
    // → 2 页
    jianlu_pager_t p;
    jianlu_pager_init(&p, "0123456789abcdefghij0123456789ABCDEFGHIJ", 10, 2);
    assert(p.pages == 2);
    char buf[64];
    jianlu_pager_page_text(&p, buf, sizeof(buf));
    assert(strcmp(buf, "0123456789abcdefghij") == 0);
    assert(jianlu_pager_next(&p));
    jianlu_pager_page_text(&p, buf, sizeof(buf));
    assert(strcmp(buf, "0123456789ABCDEFGHIJ") == 0);
    assert(!jianlu_pager_next(&p));
    assert(jianlu_pager_prev(&p));
    assert(p.page == 0);
    assert(!jianlu_pager_prev(&p));
}

static void test_cjk_units(void) {
    // 10 个汉字 = 20 单元,cols=8 → 每行 4 字,rows=2 → 每页 8 字 → 2 页
    jianlu_pager_t p;
    jianlu_pager_init(&p, "一二三四五六七八九十", 8, 2);
    assert(p.pages == 2);
    char buf[64];
    jianlu_pager_page_text(&p, buf, sizeof(buf));
    assert(strcmp(buf, "一二三四五六七八") == 0);
    jianlu_pager_next(&p);
    jianlu_pager_page_text(&p, buf, sizeof(buf));
    assert(strcmp(buf, "九十") == 0);
    // 折行不在 UTF-8 字符中间切:第一页字节数必须是 3 的倍数
    assert((p.page_start[1] % 3) == 0);
}

static void test_newlines(void) {
    // 显式换行参与计行:3 行,rows=2 → 2 页
    jianlu_pager_t p;
    jianlu_pager_init(&p, "第一行\n第二行\n第三行", 22, 2);
    assert(p.pages == 2);
    char buf[64];
    jianlu_pager_page_text(&p, buf, sizeof(buf));
    assert(strcmp(buf, "第一行\n第二行\n") == 0 || strcmp(buf, "第一行\n第二行") == 0);
    jianlu_pager_next(&p);
    jianlu_pager_page_text(&p, buf, sizeof(buf));
    assert(strcmp(buf, "第三行") == 0);
}

static void test_max_pages_cap(void) {
    // 超长文本:页数不超过 JIANLU_PAGER_MAX_PAGES,末页包含剩余全部
    char big[2000];
    char *w = big;
    for (int i = 0; i < 600; i++) { memcpy(w, "一", 3); w += 3; }
    *w = '\0';
    jianlu_pager_t p;
    jianlu_pager_init(&p, big, 22, 6);
    assert(p.pages == JIANLU_PAGER_MAX_PAGES);
    // 翻到最后一页,文本非空且 next 无效
    while (jianlu_pager_next(&p)) {}
    char buf[1800];
    size_t n = jianlu_pager_page_text(&p, buf, sizeof(buf));
    assert(n > 0);
    assert(!jianlu_pager_next(&p));
}

static void test_count_pages(void) {
    assert(jianlu_pager_count_pages(NULL, 22, 6) == 0);
    assert(jianlu_pager_count_pages("", 22, 6) == 0);
    assert(jianlu_pager_count_pages("短", 22, 6) == 1);
    // 50 字 reply:100 单元,22 单元/行 ≈ 5 行 → 1 页(6 行/页)
    assert(jianlu_pager_count_pages(
        "好的,已经帮你记下来了,今天下午三点开会,到时候提前十分钟提醒你哦", 22, 6) == 1);
}

int main(void) {
    test_empty_and_short();
    test_ascii_wrap_and_pages();
    test_cjk_units();
    test_newlines();
    test_max_pages_cap();
    test_count_pages();
    return 0;
}
