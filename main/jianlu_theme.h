// main/jianlu_theme.h —— 色彩主题调色板(纯 C,host 可测)。
//
// 四套预设:墨夜(默认,深底琥珀)/素笺(米白浅色)/青瓷(深底青瓷)/绛紫(深底紫)。
// jianlu_ui 通过宏重定向读取当前主题(UI_BG → s_theme->bg),切换即重建全屏。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define JIANLU_THEME_COUNT 4

// 字段名与 jianlu_ui 原配色宏一一对应(小写)
typedef struct {
    uint32_t bg;        // 屏幕底色
    uint32_t card_top;  // 顶卡
    uint32_t card_mid;  // 第二层
    uint32_t card_back; // 第三层
    uint32_t card;      // 普通卡片(主页菜单格/设置行)
    uint32_t card_sel;  // 焦点/选中提亮
    uint32_t accent;    // 强调色(描边/徽标/录音点)
    uint32_t ink;       // 主文字
    uint32_t dim;       // 次要文字
    uint32_t badge_art; // 文章徽标
    uint32_t badge_ins; // 灵感徽标
    uint32_t badge_oth; // 其他徽标
} jianlu_theme_t;

// id 越界按 0(墨夜)处理
const jianlu_theme_t *jianlu_theme_get(int id);

// 设置行显示名(静态串,勿释放)
const char *jianlu_theme_name(int id);

// 主题是否为浅色底(浅色主题下特殊对象需要描边等协调)
bool jianlu_theme_is_light(int id);
