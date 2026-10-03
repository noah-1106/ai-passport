// main/jianlu_ui.c —— 见 jianlu_ui.h。
#include "jianlu_ui.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "jianlu_json.h"
#include "jianlu_voice.h"
#include "lvgl.h"

// ---- 配色:深色台账风,琥珀色为唯一强调色 ----
#define UI_BG         0x10141A   // 屏幕底色(比最后的卡更深,衬出堆叠)
#define UI_CARD_TOP   0x232C38   // 顶卡
#define UI_CARD_MID   0x1B222C   // 第二层
#define UI_CARD_BACK  0x151B23   // 第三层
#define UI_ACCENT     0xE8A33D   // 琥珀强调色(顶卡描边/待办徽标/录音点)
#define UI_INK        0xF2EDE3   // 主文字
#define UI_DIM        0x8A94A3   // 次要文字
#define UI_BADGE_ART  0x5B9BD5   // 文章徽标
#define UI_BADGE_INS  0x9B7EDE   // 灵感徽标
#define UI_BADGE_OTH  0x6B7280   // 其他徽标

// ---- 堆叠几何:三张卡依次偏移 10px、缩窄 8px,顶卡全显,后卡露头 ----
#define DECK_TOP_X    16
#define DECK_TOP_Y    54
#define DECK_TOP_W    208
#define DECK_H        162
#define DECK_STEP_XY  4     // 每层 x 内收 4(左右各让 4)
#define DECK_STEP_Y   10    // 每层下移 10,形成底部露头
#define DECK_STEP_W   8     // 每层窄 8

// 中文字体:应用自生成子集(Noto Sans SC,GB2312 全集,assets/fonts/jianlu_font_16.c),
// 回退 Montserrat 14 补拉丁/数字。生成命令见 assets/fonts/README.md。
LV_FONT_DECLARE(jianlu_font_16);
static lv_font_t s_font_cjk;

static lv_obj_t *s_slots[3];          // 0=顶 1=中 2=底(创建顺序即 z 序)
static lv_obj_t *s_badge;
static lv_obj_t *s_badge_text;
static lv_obj_t *s_title;
static lv_obj_t *s_summary;
static lv_obj_t *s_tags;
static lv_obj_t *s_date;
static lv_obj_t *s_status;
static lv_obj_t *s_battery;

static lv_obj_t *s_voice_panel;
static lv_obj_t *s_voice_title;
static lv_obj_t *s_voice_body;
static lv_obj_t *s_voice_hint;

static const jianlu_store_t *s_pending_store;   // 动画结束后要落的内容
static bool s_anim_running;

static void fonts_init(void)
{
    s_font_cjk = jianlu_font_16;
    s_font_cjk.fallback = &lv_font_montserrat_14;
}

static lv_color_t badge_color(jianlu_type_t type)
{
    switch (type) {
    case JIANLU_TYPE_TODO:        return lv_color_hex(UI_ACCENT);
    case JIANLU_TYPE_ARTICLE:     return lv_color_hex(UI_BADGE_ART);
    case JIANLU_TYPE_INSPIRATION: return lv_color_hex(UI_BADGE_INS);
    default:                      return lv_color_hex(UI_BADGE_OTH);
    }
}

// ---- 动态网络文本的字形兜底 ----
// 中枢文本(ASR/LLM 产出)可能含 emoji 等 GB2312 之外的字符;逐个码点查字体
// 描述符(含 fallback 链),不覆盖的统一替换为 U+25A1 "□"(GB2312 符号区,
// 生成字体已覆盖)—— deliberate replacement,而不是画出半字乱码或空白。
static uint32_t utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)(*p);
    uint32_t cp;
    int extra;
    if (s[0] < 0x80) {
        cp = s[0];
        extra = 0;
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = s[0] & 0x1F; extra = 1;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = s[0] & 0x0F; extra = 2;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = s[0] & 0x07; extra = 3;
    } else {
        (*p)++;              // 非法起始字节:跳过 1 字节
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { extra = i - 1; break; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += extra + 1;
    return cp;
}

static bool glyph_covered(uint32_t cp)
{
    lv_font_glyph_dsc_t g;
    return lv_font_get_glyph_dsc(&s_font_cjk, &g, cp, 0) && !g.is_placeholder;
}

static void sanitize_text(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) return;
    if (src == NULL) src = "";
    size_t used = 0;
    while (*src != '\0' && used + 4 < dst_size) {   // □ 与汉字都是 3 字节,+NUL
        const char *before = src;
        uint32_t cp = utf8_next(&src);
        if (cp == '\n' || cp == '\t') cp = ' ';     // 交给 LVGL 的换行只保留在确认页
        if (glyph_covered(cp)) {
            size_t n = (size_t)(src - before);
            memcpy(dst + used, before, n);
            used += n;
        } else {
            memcpy(dst + used, "□", 3);
            used += 3;
        }
    }
    dst[used] = '\0';
}

// 确认页专用:保留换行
static void sanitize_text_keep_lf(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) return;
    if (src == NULL) src = "";
    size_t used = 0;
    while (*src != '\0' && used + 4 < dst_size) {
        const char *before = src;
        uint32_t cp = utf8_next(&src);
        if (cp == '\n') {
            dst[used++] = '\n';
            continue;
        }
        if (glyph_covered(cp)) {
            size_t n = (size_t)(src - before);
            memcpy(dst + used, before, n);
            used += n;
        } else {
            memcpy(dst + used, "□", 3);
            used += 3;
        }
    }
    dst[used] = '\0';
}

static lv_obj_t *panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                              uint32_t bg, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *text_create(lv_obj_t *parent, int x, int y, int w, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, &s_font_cjk, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

// ---------------------------------------------------------------------------
// 卡片堆
// ---------------------------------------------------------------------------
static void deck_build(lv_obj_t *scr)
{
    // 先底后顶创建,z 序自然正确
    s_slots[2] = panel_create(scr,
        DECK_TOP_X + 2 * DECK_STEP_XY, DECK_TOP_Y + 2 * DECK_STEP_Y,
        DECK_TOP_W - 2 * DECK_STEP_W, DECK_H, UI_CARD_BACK, 10);
    s_slots[1] = panel_create(scr,
        DECK_TOP_X + DECK_STEP_XY, DECK_TOP_Y + DECK_STEP_Y,
        DECK_TOP_W - DECK_STEP_W, DECK_H, UI_CARD_MID, 10);
    s_slots[0] = panel_create(scr,
        DECK_TOP_X, DECK_TOP_Y, DECK_TOP_W, DECK_H, UI_CARD_TOP, 10);

    lv_obj_t *top = s_slots[0];
    lv_obj_set_style_border_width(top, 1, 0);
    lv_obj_set_style_border_color(top, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_border_opa(top, LV_OPA_40, 0);
    // 深度阴影:只给顶卡,静态绘制,不随帧重算
    lv_obj_set_style_shadow_width(top, 14, 0);
    lv_obj_set_style_shadow_color(top, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(top, LV_OPA_40, 0);
    lv_obj_set_style_shadow_offset_y(top, 4, 0);

    s_badge = panel_create(top, 10, 10, 22, 22, UI_ACCENT, 6);
    s_badge_text = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_badge_text, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_badge_text, lv_color_hex(UI_BG), 0);
    lv_obj_center(s_badge_text);

    s_title = text_create(top, 40, 10, 158, UI_INK);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);

    s_summary = text_create(top, 12, 42, 184, UI_DIM);
    lv_obj_set_height(s_summary, 84);
    lv_label_set_long_mode(s_summary, LV_LABEL_LONG_DOT);

    s_tags = text_create(top, 12, 132, 110, UI_BADGE_ART);
    lv_label_set_long_mode(s_tags, LV_LABEL_LONG_DOT);

    s_date = text_create(top, 122, 132, 74, UI_DIM);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_RIGHT, 0);
}

static void deck_apply(const jianlu_store_t *store)
{
    bool show = store->view == JIANLU_VIEW_READY && store->count > 0;
    for (int i = 0; i < 3; i++) {
        if (show) {
            lv_obj_remove_flag(s_slots[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_slots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!show) return;

    // 后两张卡只在"下面还有货"时出现:剩 1 张只显示顶卡,剩 2 张显示两层
    if (store->count < 3) lv_obj_add_flag(s_slots[2], LV_OBJ_FLAG_HIDDEN);
    if (store->count < 2) lv_obj_add_flag(s_slots[1], LV_OBJ_FLAG_HIDDEN);

    const jianlu_record_t *rec = jianlu_store_selected(store);
    lv_obj_set_style_bg_color(s_badge, badge_color(rec->type), 0);
    lv_label_set_text(s_badge_text, jianlu_type_badge(rec->type));

    static char title[JIANLU_TITLE_LEN];     // 锁定上下文单线程使用
    static char summary[JIANLU_SUMMARY_LEN];
    sanitize_text(title, sizeof(title), rec->title);
    sanitize_text(summary, sizeof(summary), rec->summary);
    lv_label_set_text(s_title, title);
    lv_obj_set_style_text_decor(s_title,
        rec->completing ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, 0);
    lv_obj_set_style_text_color(s_title,
        lv_color_hex(rec->completing ? UI_DIM : UI_INK), 0);

    char tags[2 * JIANLU_TAG_LEN + 8];
    tags[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < rec->tag_count; i++) {
        int n = snprintf(tags + used, sizeof(tags) - used, "%s#%s",
                         i ? " " : "", rec->tags[i]);
        if (n < 0) break;
        used += (size_t)n < sizeof(tags) - used ? (size_t)n : sizeof(tags) - used - 1;
    }
    static char tags_safe[2 * JIANLU_TAG_LEN + 8];
    sanitize_text(tags_safe, sizeof(tags_safe), tags);
    lv_label_set_text(s_tags, tags_safe);
    lv_label_set_text(s_summary, summary);
    // "2026-10-03" → 显示 "10-03"
    lv_label_set_text(s_date, strlen(rec->date) >= 10 ? rec->date + 5 : rec->date);
}

static void fly_exec(void *obj, int32_t v)
{
    // v: 0..100。向上飞 28px 并淡出。
    lv_obj_set_y((lv_obj_t *)obj, DECK_TOP_Y - v * 28 / 100);
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)(255 - v * 255 / 100), 0);
}

static void fly_done(lv_anim_t *anim)
{
    (void)anim;
    s_anim_running = false;
    lv_obj_set_y(s_slots[0], DECK_TOP_Y);
    lv_obj_set_style_opa(s_slots[0], LV_OPA_COVER, 0);
    if (s_pending_store) deck_apply(s_pending_store);
}

static void deck_refresh(const jianlu_store_t *store, jianlu_anim_t anim)
{
    if (s_anim_running) {
        lv_anim_delete(s_slots[0], fly_exec);
        s_anim_running = false;
        lv_obj_set_y(s_slots[0], DECK_TOP_Y);
        lv_obj_set_style_opa(s_slots[0], LV_OPA_COVER, 0);
        anim = JIANLU_ANIM_NONE;
    }

    bool can_anim = store->view == JIANLU_VIEW_READY
                 && anim != JIANLU_ANIM_NONE;
    if (can_anim) {
        s_pending_store = store;
        s_anim_running = true;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_slots[0]);
        lv_anim_set_values(&a, 0, 100);
        lv_anim_set_duration(&a, 140);
        lv_anim_set_exec_cb(&a, fly_exec);
        lv_anim_set_completed_cb(&a, fly_done);
        lv_anim_start(&a);
        return;
    }
    s_pending_store = NULL;
    deck_apply(store);
}

// ---------------------------------------------------------------------------
// 状态层(连接中/加载中/出错/空清单)
// ---------------------------------------------------------------------------
static const char *status_text(const jianlu_store_t *store)
{
    switch (store->view) {
    case JIANLU_VIEW_BOOT:       return "启动中…";
    case JIANLU_VIEW_NO_CONFIG:  return "网络未配置\n请在固件配置中填写";
    case JIANLU_VIEW_CONNECTING: return "连接 Wi-Fi…";
    case JIANLU_VIEW_LOADING:    return "加载简录…";
    case JIANLU_VIEW_ERROR:      return store->error[0] ? store->error : "出错了";
    case JIANLU_VIEW_READY:
    default:                     return store->count == 0 ? "全部完成啦" : "";
    }
}

// ---------------------------------------------------------------------------
// 语音层
// ---------------------------------------------------------------------------
static void voice_build(lv_obj_t *scr)
{
    s_voice_panel = panel_create(scr, 10, 46, 220, 240, UI_BG, 12);
    lv_obj_set_style_bg_opa(s_voice_panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_voice_panel, 1, 0);
    lv_obj_set_style_border_color(s_voice_panel, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_border_opa(s_voice_panel, LV_OPA_40, 0);

    s_voice_title = text_create(s_voice_panel, 0, 20, 220, UI_INK);
    lv_obj_set_style_text_align(s_voice_title, LV_TEXT_ALIGN_CENTER, 0);

    s_voice_body = text_create(s_voice_panel, 14, 60, 192, UI_INK);
    lv_obj_set_height(s_voice_body, 120);

    s_voice_hint = text_create(s_voice_panel, 0, 200, 220, UI_DIM);
    lv_obj_set_style_text_align(s_voice_hint, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

static void voice_show(const char *title, uint32_t title_color,
                       const char *body, const char *hint)
{
    lv_label_set_text(s_voice_title, title);
    lv_obj_set_style_text_color(s_voice_title, lv_color_hex(title_color), 0);
    lv_label_set_text(s_voice_body, body);
    lv_label_set_text(s_voice_hint, hint);
    lv_obj_remove_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

void jianlu_ui_voice_idle(void)
{
    lv_obj_add_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

void jianlu_ui_voice_recording(int elapsed_sec)
{
    char body[40];
    int sec = elapsed_sec;
    if (sec > JIANLU_VOICE_MAX_SEC) sec = JIANLU_VOICE_MAX_SEC;
    if (sec < 0) sec = 0;
    snprintf(body, sizeof(body), "%02d / %ds", sec, JIANLU_VOICE_MAX_SEC);
    voice_show("● 录音中", UI_ACCENT, body, "松开结束");
}

void jianlu_ui_voice_sending(void)
{
    voice_show("上传中…", UI_INK, "", "说完啦,正在识别");
}

void jianlu_ui_voice_confirm(const char *transcript, const char *reply, int new_count)
{
    static char safe_transcript[JIANLU_TRANSCRIPT_LEN];
    static char safe_reply[JIANLU_REPLY_LEN];
    sanitize_text_keep_lf(safe_transcript, sizeof(safe_transcript), transcript);
    sanitize_text_keep_lf(safe_reply, sizeof(safe_reply), reply);

    char body[JIANLU_TRANSCRIPT_LEN + JIANLU_REPLY_LEN + 24];
    if (safe_reply[0] != '\0') {
        snprintf(body, sizeof(body), "%s\n\n%s", safe_transcript, safe_reply);
    } else {
        snprintf(body, sizeof(body), "%s", safe_transcript);
    }
    char hint[40];
    if (new_count > 0) {
        snprintf(hint, sizeof(hint), "新增 %d 条 · OK 返回", new_count);
    } else {
        snprintf(hint, sizeof(hint), "OK 返回");
    }
    voice_show("记下了", UI_ACCENT, body, hint);
}

void jianlu_ui_voice_error(void)
{
    voice_show("发送失败", 0xD96A5A, "录音已保留\n联网后自动补传", "OK 返回");
}

// ---------------------------------------------------------------------------
// 电量与屏幕
// ---------------------------------------------------------------------------
static void battery_tick(lv_timer_t *timer)
{
    (void)timer;
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_obj_add_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_battery, "%d%%", soc);
    }
}

void jianlu_ui_create(void)
{
    fonts_init();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = text_create(scr, 20, 13, 140, UI_INK);
    lv_label_set_text(title, "小诺简录");

    s_battery = lv_label_create(scr);
    lv_obj_set_pos(s_battery, 182, 15);
    lv_obj_set_style_text_font(s_battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(UI_DIM), 0);
    lv_label_set_text(s_battery, "--");

    lv_obj_t *divider = panel_create(scr, 20, 38, 200, 1, 0x2A3442, 0);

    deck_build(scr);

    s_status = text_create(scr, 24, 130, 192, UI_DIM);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_status, "");
    (void)divider;

    lv_obj_t *hint = text_create(scr, 10, 296, 220, UI_DIM);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, "OK 完成 · 按住说话 · 双击刷新");

    voice_build(scr);

    lv_timer_create(battery_tick, 10000, NULL);
    battery_tick(NULL);

    lv_screen_load(scr);
}

void jianlu_ui_refresh(const jianlu_store_t *store, jianlu_anim_t anim)
{
    deck_refresh(store, anim);
    bool show_list = store->view == JIANLU_VIEW_READY && store->count > 0;
    lv_label_set_text(s_status, show_list ? "" : status_text(store));
}
