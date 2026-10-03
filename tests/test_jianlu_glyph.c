// tests/test_jianlu_glyph.c —— 字形处置分类的 host 侧测试。
#include <assert.h>

#include "jianlu_glyph.h"

int main(void) {
    // ASCII 与 CJK 基本区走字体查询路径
    assert(jianlu_glyph_action('A') == JIANLU_GLYPH_KEEP);
    assert(jianlu_glyph_action(' ') == JIANLU_GLYPH_KEEP);
    assert(jianlu_glyph_action(0x4E2D) == JIANLU_GLYPH_ASK_FONT);   // 中
    assert(jianlu_glyph_action(0x9FFF) == JIANLU_GLYPH_ASK_FONT);
    // CJK 扩展区(可能是正经汉字但字体没有)→ 查询路径(将显 □)
    assert(jianlu_glyph_action(0x20000) == JIANLU_GLYPH_ASK_FONT);
    // □ 本身(0x25A1)不能被丢弃(它是兜底字符,且在 0x2600 之下)
    assert(jianlu_glyph_action(0x25A1) == JIANLU_GLYPH_ASK_FONT);

    // emoji:静默丢弃
    assert(jianlu_glyph_action(0x1F600) == JIANLU_GLYPH_DROP);   // 😀
    assert(jianlu_glyph_action(0x1F4CC) == JIANLU_GLYPH_DROP);   // 📌
    assert(jianlu_glyph_action(0x1FA90) == JIANLU_GLYPH_DROP);
    // 装饰符号:静默丢弃
    assert(jianlu_glyph_action(0x2705) == JIANLU_GLYPH_DROP);    // ✅
    assert(jianlu_glyph_action(0x26A0) == JIANLU_GLYPH_DROP);    // ⚠
    assert(jianlu_glyph_action(0x2B50) == JIANLU_GLYPH_DROP);    // ⭐
    assert(jianlu_glyph_action(0x27BF) == JIANLU_GLYPH_DROP);    // 边界
    assert(jianlu_glyph_action(0x2BFF) == JIANLU_GLYPH_DROP);    // 边界
    // 变体选择符 / ZWJ / 组合符 / 标签 / 解码失败符
    assert(jianlu_glyph_action(0xFE0F) == JIANLU_GLYPH_DROP);
    assert(jianlu_glyph_action(0xFE00) == JIANLU_GLYPH_DROP);
    assert(jianlu_glyph_action(0x200D) == JIANLU_GLYPH_DROP);
    assert(jianlu_glyph_action(0x20E3) == JIANLU_GLYPH_DROP);
    assert(jianlu_glyph_action(0xE0001) == JIANLU_GLYPH_DROP);
    assert(jianlu_glyph_action(0xFFFD) == JIANLU_GLYPH_DROP);

    // 常规标点/全角字符不丢
    assert(jianlu_glyph_action(0x3002) == JIANLU_GLYPH_ASK_FONT); // 。
    assert(jianlu_glyph_action(0xFF0C) == JIANLU_GLYPH_ASK_FONT); // ,
    assert(jianlu_glyph_action(0x2022) == JIANLU_GLYPH_ASK_FONT); // •
    return 0;
}
