// main/jianlu_layout.h —— 顶卡布局契约(纯 C,host 可测)。
//
// 顶卡 208x162。历史 bug:标题按 1 行排版,折行后与摘要重叠。
// 契约:标题固定占 2 行(超出省略号),摘要锚定在标题区之下、
// 元信息行(标签/日期)之上,三区永不重叠。
#pragma once

#define JIANLU_DECK_CARD_H    162
#define JIANLU_DECK_TITLE_Y   8
#define JIANLU_DECK_TITLE_MAX_LINES 2
#define JIANLU_DECK_GAP       4
#define JIANLU_DECK_META_Y    132   // 标签/日期行(固定)

typedef struct {
    int title_y;
    int title_h;    // = 2 * line_h
    int summary_y;  // = title_y + title_h + GAP
    int summary_h;  // = META_Y - GAP - summary_y(>=0)
    int meta_y;
} jianlu_deck_layout_t;

// line_h:字体实际行高(px)。非法值(<=0)按 20 处理。
void jianlu_deck_layout(int line_h, jianlu_deck_layout_t *out);
