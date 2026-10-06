// main/jianlu_theme.c —— 见 jianlu_theme.h。
#include "jianlu_theme.h"

#include <stdbool.h>

static const jianlu_theme_t THEMES[JIANLU_THEME_COUNT] = {
    [0] = {   // 墨夜:深底琥珀(原配色)
        .bg = 0x10141A, .card_top = 0x232C38, .card_mid = 0x1B222C,
        .card_back = 0x151B23, .card = 0x1F2733, .card_sel = 0x27313F,
        .accent = 0xE8A33D, .ink = 0xF2EDE3, .dim = 0x8A94A3,
        .badge_art = 0x5B9BD5, .badge_ins = 0x9B7EDE, .badge_oth = 0x6B7280,
    },
    [1] = {   // 素笺:米白底墨字,深琥珀点缀(浅色)
        .bg = 0xEFE9DC, .card_top = 0xFFFFFF, .card_mid = 0xF7F2E8,
        .card_back = 0xE7E0D0, .card = 0xF7F2E8, .card_sel = 0xF2E3C2,
        .accent = 0xB97E1C, .ink = 0x32302B, .dim = 0x8B8577,
        .badge_art = 0x3A6EA5, .badge_ins = 0x7A5BC0, .badge_oth = 0x6E6A5E,
    },
    [2] = {   // 青瓷:深底青瓷点缀
        .bg = 0x0E1513, .card_top = 0x22332E, .card_mid = 0x1A2823,
        .card_back = 0x141F1B, .card = 0x1E2C27, .card_sel = 0x27382F,
        .accent = 0x53B8AD, .ink = 0xEDF3F0, .dim = 0x82A79E,
        .badge_art = 0x5B9BD5, .badge_ins = 0x9B7EDE, .badge_oth = 0x5F7A72,
    },
    [3] = {   // 绛紫:深底紫点缀
        .bg = 0x130F1A, .card_top = 0x271F33, .card_mid = 0x1E1829,
        .card_back = 0x171221, .card = 0x211A2C, .card_sel = 0x2C2339,
        .accent = 0xB48AE8, .ink = 0xF1ECF7, .dim = 0x9C91B2,
        .badge_art = 0x5B9BD5, .badge_ins = 0x8F6FD0, .badge_oth = 0x655B78,
    },
};

static const char *const NAMES[JIANLU_THEME_COUNT] = {
    "墨夜", "素笺", "青瓷", "绛紫",
};

const jianlu_theme_t *jianlu_theme_get(int id)
{
    if (id < 0 || id >= JIANLU_THEME_COUNT) return &THEMES[0];
    return &THEMES[id];
}

const char *jianlu_theme_name(int id)
{
    if (id < 0 || id >= JIANLU_THEME_COUNT) return NAMES[0];
    return NAMES[id];
}

bool jianlu_theme_is_light(int id)
{
    return id == 1;   // 素笺
}
