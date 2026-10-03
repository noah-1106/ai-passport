// main/jianlu_glyph.h —— 动态网络文本的字形处置分类(纯 C,host 可测)。
//
// 策略(取代"一律 □"):emoji 与装饰性符号静默丢弃(用户嫌 □ 难看);
// 只有"可能是正经文字但字体没有"的字符才显示 □(如 CJK 扩展区)。
// 字体覆盖判定不在此模块(需要 LVGL 字体描述符),由调用方对
// JIANLU_GLYPH_ASK_FONT 的码点查字体:有字形→保留,没有→□。
#pragma once

#include <stdint.h>

typedef enum {
    JIANLU_GLYPH_KEEP = 0,   // 直接保留(ASCII、换行等)
    JIANLU_GLYPH_DROP,       // 静默丢弃(emoji、装饰符号、变体选择符、ZWJ)
    JIANLU_GLYPH_ASK_FONT,   // 查字体决定:有→保留,无→□
} jianlu_glyph_action_t;

jianlu_glyph_action_t jianlu_glyph_action(uint32_t cp);
