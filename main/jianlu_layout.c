// main/jianlu_layout.c —— 见 jianlu_layout.h。
#include "jianlu_layout.h"

void jianlu_deck_layout(int line_h, jianlu_deck_layout_t *out)
{
    if (line_h <= 0) line_h = 20;
    out->title_y = JIANLU_DECK_TITLE_Y;
    out->title_h = JIANLU_DECK_TITLE_MAX_LINES * line_h;
    out->summary_y = out->title_y + out->title_h + JIANLU_DECK_GAP;
    out->meta_y = JIANLU_DECK_META_Y;
    out->summary_h = out->meta_y - JIANLU_DECK_GAP - out->summary_y;
    if (out->summary_h < 0) out->summary_h = 0;
}
