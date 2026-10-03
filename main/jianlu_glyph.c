// main/jianlu_glyph.c —— 见 jianlu_glyph.h。
#include "jianlu_glyph.h"

jianlu_glyph_action_t jianlu_glyph_action(uint32_t cp)
{
    if (cp < 0x80) return JIANLU_GLYPH_KEEP;
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return JIANLU_GLYPH_DROP;  // SMP:emoji 及符号
    // 注意:0x20000 起是 CJK 扩展 B~H(正经汉字),走字体查询,不在丢弃范围
    if (cp >= 0x2600 && cp <= 0x27BF) return JIANLU_GLYPH_DROP;  // 杂项符号/装饰符
    if (cp >= 0x2B00 && cp <= 0x2BFF) return JIANLU_GLYPH_DROP;  // 箭头/星形等装饰
    if (cp >= 0xFE00 && cp <= 0xFE0F) return JIANLU_GLYPH_DROP;  // 变体选择符
    if (cp == 0x200D || cp == 0x20E3) return JIANLU_GLYPH_DROP;  // ZWJ/组合 enclosing
    if (cp >= 0xE0000 && cp <= 0xE01EF) return JIANLU_GLYPH_DROP; // 标签字符
    if (cp == 0xFFFD) return JIANLU_GLYPH_DROP;           // 解码失败的替代符不留痕
    return JIANLU_GLYPH_ASK_FONT;
}
