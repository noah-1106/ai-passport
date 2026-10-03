// tests/test_jianlu_layout.c —— 顶卡布局契约的 host 侧测试。
#include <assert.h>

#include "jianlu_layout.h"

int main(void) {
    jianlu_deck_layout_t l;

    // 常规行高 20:标题 8..48,摘要 52..128,元信息 132,互不重叠
    jianlu_deck_layout(20, &l);
    assert(l.title_y == JIANLU_DECK_TITLE_Y);
    assert(l.title_h == 40);
    assert(l.summary_y == l.title_y + l.title_h + JIANLU_DECK_GAP);
    assert(l.summary_y >= l.title_y + l.title_h);          // 摘要在标题区之下
    assert(l.summary_y + l.summary_h <= l.meta_y);         // 摘要不压元信息行
    assert(l.meta_y + 16 <= JIANLU_DECK_CARD_H + 10);      // 元信息在卡内

    // 小行高同样成立
    jianlu_deck_layout(16, &l);
    assert(l.summary_y == JIANLU_DECK_TITLE_Y + 32 + JIANLU_DECK_GAP);
    assert(l.summary_y + l.summary_h <= l.meta_y);

    // 非法行高按 20 兜底,不为负
    jianlu_deck_layout(0, &l);
    assert(l.title_h == 40);
    jianlu_deck_layout(-5, &l);
    assert(l.title_h == 40);

    // 极端大行高:摘要高度不变成负数
    jianlu_deck_layout(100, &l);
    assert(l.summary_h == 0);
    return 0;
}
