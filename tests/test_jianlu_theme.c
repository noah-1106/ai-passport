// tests/test_jianlu_theme.c —— 色彩主题调色板的 host 侧测试。
#include <assert.h>
#include <string.h>

#include "jianlu_theme.h"

int main(void) {
    assert(JIANLU_THEME_COUNT == 4);

    // 名称与基础可寻址性
    assert(strcmp(jianlu_theme_name(0), "墨夜") == 0);
    assert(strcmp(jianlu_theme_name(1), "素笺") == 0);
    assert(strcmp(jianlu_theme_name(2), "青瓷") == 0);
    assert(strcmp(jianlu_theme_name(3), "绛紫") == 0);
    // 越界按默认(墨夜),不越界访问
    assert(strcmp(jianlu_theme_name(-1), "墨夜") == 0);
    assert(strcmp(jianlu_theme_name(99), "墨夜") == 0);
    assert(jianlu_theme_get(-1) == jianlu_theme_get(0));
    assert(jianlu_theme_get(99) == jianlu_theme_get(0));

    // 墨夜(默认)与历史硬编码配色一致(回归锚点)
    const jianlu_theme_t *t = jianlu_theme_get(0);
    assert(t->bg == 0x10141A && t->accent == 0xE8A33D);
    assert(t->ink == 0xF2EDE3 && t->dim == 0x8A94A3);
    assert(t->card == 0x1F2733 && t->card_sel == 0x27313F);
    assert(t->card_top == 0x232C38 && t->card_back == 0x151B23);

    // 素笺是唯一浅色底;其余深色
    assert(jianlu_theme_is_light(1));
    assert(!jianlu_theme_is_light(0));
    assert(!jianlu_theme_is_light(2));
    assert(!jianlu_theme_is_light(3));

    // 各主题调色板完整性:全字段非零,且四套强调色互不相同
    uint32_t accents[JIANLU_THEME_COUNT] = { 0 };
    for (int i = 0; i < JIANLU_THEME_COUNT; i++) {
        const jianlu_theme_t *p = jianlu_theme_get(i);
        assert(p->bg && p->card_top && p->card_mid && p->card_back);
        assert(p->card && p->card_sel && p->accent && p->ink && p->dim);
        assert(p->badge_art && p->badge_ins && p->badge_oth);
        // 深色主题:文字比底色亮(粗略亮度比较);浅色相反
        uint32_t bg_l = ((p->bg >> 16) & 0xFF) + ((p->bg >> 8) & 0xFF) + (p->bg & 0xFF);
        uint32_t ink_l = ((p->ink >> 16) & 0xFF) + ((p->ink >> 8) & 0xFF) + (p->ink & 0xFF);
        if (jianlu_theme_is_light(i)) assert(ink_l < bg_l);
        else assert(ink_l > bg_l);
        // 卡片层次:顶卡亮于底卡(堆叠纵深可辨)
        uint32_t top_l = ((p->card_top >> 16) & 0xFF) + ((p->card_top >> 8) & 0xFF)
                       + (p->card_top & 0xFF);
        uint32_t back_l = ((p->card_back >> 16) & 0xFF) + ((p->card_back >> 8) & 0xFF)
                        + (p->card_back & 0xFF);
        assert(top_l > back_l);
        // 选中比未选中醒目(亮度差存在)
        uint32_t sel_l = ((p->card_sel >> 16) & 0xFF) + ((p->card_sel >> 8) & 0xFF)
                       + (p->card_sel & 0xFF);
        uint32_t card_l = ((p->card >> 16) & 0xFF) + ((p->card >> 8) & 0xFF)
                        + (p->card & 0xFF);
        assert(sel_l != card_l);
        accents[i] = p->accent;
    }
    for (int i = 0; i < JIANLU_THEME_COUNT; i++) {
        for (int j = i + 1; j < JIANLU_THEME_COUNT; j++) {
            assert(accents[i] != accents[j]);   // 四套主题强调色互异
        }
    }
    return 0;
}
