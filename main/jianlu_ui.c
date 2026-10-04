// main/jianlu_ui.c —— 见 jianlu_ui.h。
#include "jianlu_ui.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "jianlu_glyph.h"
#include "jianlu_json.h"
#include "jianlu_layout.h"
#include "jianlu_pager.h"
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
static lv_obj_t *s_offline;

static lv_obj_t *s_voice_panel;
static lv_obj_t *s_voice_title;
static lv_obj_t *s_voice_body;
static lv_obj_t *s_voice_hint;
static lv_obj_t *s_voice_dot;
static lv_obj_t *s_voice_bars[5];
static lv_obj_t *s_conf_badge;
static lv_obj_t *s_conf_badge_text;
static lv_obj_t *s_conf_title;
static lv_obj_t *s_pager_ind;
static lv_obj_t *s_flash;
static lv_obj_t *s_flash_l1;
static lv_obj_t *s_flash_l2;
static lv_obj_t *s_flash_title;
static lv_obj_t *s_flash_body;
static lv_timer_t *s_flash_timer;

// 分页查看器状态(text 在 s_pager_full 内,与 pager 同生命周期)
#define PAGER_COLS 22   // 每行显示单元(CJK=2):约 11 汉字,保守防重叠
#define PAGER_ROWS 6    // 每页行数
static jianlu_pager_t s_pager;
static bool s_pager_open;
static char s_pager_full[JIANLU_TRANSCRIPT_LEN + JIANLU_REPLY_LEN + 8];

static volatile uint8_t s_voice_level;   // 录音任务写入,UI 定时器读取
static uint8_t s_bar_values[5];          // 带衰减的当前条高
static bool s_wave_active;               // 录音覆盖层显示中(定时器据此工作)
static uint8_t s_breath;                 // 呼吸相位

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

// 动态网络文本(中枢 ASR/LLM 产出)清洗:emoji/装饰符静默丢弃
// (jianlu_glyph_action 分类),"可能是正经文字但字体没有"才显 □。
static void sanitize_impl(char *dst, size_t dst_size, const char *src, bool keep_lf)
{
    if (dst_size == 0) return;
    if (src == NULL) src = "";
    size_t used = 0;
    while (*src != '\0' && used + 4 < dst_size) {   // □ 与汉字都是 3 字节,+NUL
        const char *before = src;
        uint32_t cp = utf8_next(&src);
        if (cp == '\n') {
            dst[used++] = keep_lf ? '\n' : ' ';
            continue;
        }
        if (cp == '\t') cp = ' ';
        switch (jianlu_glyph_action(cp)) {
        case JIANLU_GLYPH_DROP:
            break;   // 静默移除,不留痕迹
        case JIANLU_GLYPH_KEEP:
            memcpy(dst + used, before, (size_t)(src - before));
            used += (size_t)(src - before);
            break;
        case JIANLU_GLYPH_ASK_FONT:
            if (glyph_covered(cp)) {
                memcpy(dst + used, before, (size_t)(src - before));
                used += (size_t)(src - before);
            } else {
                memcpy(dst + used, "□", 3);
                used += 3;
            }
            break;
        }
    }
    dst[used] = '\0';
}

static void sanitize_text(char *dst, size_t dst_size, const char *src)
{
    sanitize_impl(dst, dst_size, src, false);
}

// 确认页专用:保留换行
static void sanitize_text_keep_lf(char *dst, size_t dst_size, const char *src)
{
    sanitize_impl(dst, dst_size, src, true);
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

    // 布局契约(jianlu_layout):标题固定 2 行,摘要锚定其下,三区不重叠
    jianlu_deck_layout_t dl;
    jianlu_deck_layout(lv_font_get_line_height(&s_font_cjk), &dl);

    s_title = text_create(top, 40, dl.title_y, 158, UI_INK);
    lv_obj_set_height(s_title, dl.title_h);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);   // 折行+末行省略号

    s_summary = text_create(top, 12, dl.summary_y, 184, UI_DIM);
    lv_obj_set_height(s_summary, dl.summary_h);
    lv_label_set_long_mode(s_summary, LV_LABEL_LONG_DOT);

    s_tags = text_create(top, 12, dl.meta_y, 110, UI_BADGE_ART);
    lv_label_set_long_mode(s_tags, LV_LABEL_LONG_DOT);

    s_date = text_create(top, 122, dl.meta_y, 74, UI_DIM);
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
    bool pending = rec->completing || rec->sync_pending;
    lv_obj_set_style_bg_color(s_badge, badge_color(rec->type), 0);
    lv_label_set_text(s_badge_text,
        rec->voice_placeholder ? "音" : jianlu_type_badge(rec->type));

    static char title[JIANLU_TITLE_LEN];     // 锁定上下文单线程使用
    static char summary[JIANLU_SUMMARY_LEN];
    sanitize_text(title, sizeof(title), rec->title);
    sanitize_text(summary, sizeof(summary), rec->summary);
    lv_label_set_text(s_title, title);
    lv_obj_set_style_text_decor(s_title,
        pending ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, 0);
    lv_obj_set_style_text_color(s_title,
        lv_color_hex(pending ? UI_DIM : UI_INK), 0);

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
    // 待同步标记:日期位改显"待同步"(琥珀);普通卡显示 MM-DD(灰)
    if (rec->sync_pending) {
        lv_label_set_text(s_date, "待同步");
        lv_obj_set_style_text_color(s_date, lv_color_hex(UI_ACCENT), 0);
    } else {
        lv_label_set_text(s_date, strlen(rec->date) >= 10 ? rec->date + 5 : rec->date);
        lv_obj_set_style_text_color(s_date, lv_color_hex(UI_DIM), 0);
    }
}

static void fly_exec(void *obj, int32_t v)
{
    // v: 0..100。向上飞 28px 并淡出。
    lv_obj_set_y((lv_obj_t *)obj, DECK_TOP_Y - v * 28 / 100);
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)(255 - v * 255 / 100), 0);
}

static void arrive_exec(void *obj, int32_t v)
{
    // v: 0..100。从上方 24px 滑入并淡入。
    lv_obj_set_y((lv_obj_t *)obj, DECK_TOP_Y - 24 + v * 24 / 100);
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)(v * 255 / 100), 0);
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
        lv_anim_delete(s_slots[0], arrive_exec);
        s_anim_running = false;
        lv_obj_set_y(s_slots[0], DECK_TOP_Y);
        lv_obj_set_style_opa(s_slots[0], LV_OPA_COVER, 0);
        anim = JIANLU_ANIM_NONE;
    }

    if (anim == JIANLU_ANIM_ARRIVE && store->view == JIANLU_VIEW_READY
        && store->count > 0) {
        // 先落内容,再从上方滑入
        s_pending_store = NULL;
        deck_apply(store);
        s_anim_running = true;
        lv_obj_set_y(s_slots[0], DECK_TOP_Y - 24);
        lv_obj_set_style_opa(s_slots[0], LV_OPA_TRANSP, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_slots[0]);
        lv_anim_set_values(&a, 0, 100);
        lv_anim_set_duration(&a, 160);
        lv_anim_set_exec_cb(&a, arrive_exec);
        lv_anim_set_completed_cb(&a, fly_done);
        lv_anim_start(&a);
        return;
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
// 状态层(配网/连接中/发现中枢/加载中/出错/空清单)
// ---------------------------------------------------------------------------
static char s_prov_name[24];
static bool s_prov_phone;
static char s_prov_text[128];

void jianlu_ui_provision_info(const char *dev_name, bool phone_connected)
{
    jianlu_utf8_copy(s_prov_name, sizeof(s_prov_name),
                     dev_name ? dev_name : "", sizeof(s_prov_name) - 1);
    s_prov_phone = phone_connected;
}

static const char *status_text(const jianlu_store_t *store)
{
    switch (store->view) {
    case JIANLU_VIEW_BOOT:       return "启动中…";
    case JIANLU_VIEW_NO_CONFIG:  return "网络未配置\n请在固件配置中填写";
    case JIANLU_VIEW_PROVISIONING:
        if (s_prov_phone) {
            return "手机已连接\n请在 App 里下发\nWi-Fi 名称和密码";
        }
        snprintf(s_prov_text, sizeof(s_prov_text),
                 "配网模式\n\n设备: %s\n\n请用 EspBlufi App\n搜索设备并配网",
                 s_prov_name[0] ? s_prov_name : "…");
        return s_prov_text;
    case JIANLU_VIEW_CONNECTING: return "连接 Wi-Fi…";
    case JIANLU_VIEW_DISCOVERING: return "正在寻找小诺中枢…";
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
    lv_obj_set_style_bg_opa(s_voice_panel, LV_OPA_COVER, 0);   // 不透明,不透底卡文字
    lv_obj_set_style_border_width(s_voice_panel, 1, 0);
    lv_obj_set_style_border_color(s_voice_panel, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_border_opa(s_voice_panel, LV_OPA_40, 0);

    s_voice_title = text_create(s_voice_panel, 0, 20, 220, UI_INK);
    lv_obj_set_style_text_align(s_voice_title, LV_TEXT_ALIGN_CENTER, 0);

    s_voice_body = text_create(s_voice_panel, 14, 60, 192, UI_INK);
    lv_obj_set_height(s_voice_body, 120);

    s_voice_hint = text_create(s_voice_panel, 0, 200, 220, UI_DIM);
    lv_obj_set_style_text_align(s_voice_hint, LV_TEXT_ALIGN_CENTER, 0);

    // 录音红点(呼吸)
    s_voice_dot = panel_create(s_voice_panel, 105, 24, 12, 12, 0xD96A5A, 6);
    lv_obj_add_flag(s_voice_dot, LV_OBJ_FLAG_HIDDEN);

    // 声波竖条:5 根,底部对齐,高度由电平驱动
    for (int i = 0; i < 5; i++) {
        s_voice_bars[i] = panel_create(s_voice_panel, 70 + i * 18, 150, 10, 4,
                                       UI_ACCENT, 3);
        lv_obj_add_flag(s_voice_bars[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 确认页新卡区:徽标 + 大标题(默认隐藏)
    s_conf_badge = panel_create(s_voice_panel, 34, 54, 24, 24, UI_ACCENT, 6);
    lv_obj_add_flag(s_conf_badge, LV_OBJ_FLAG_HIDDEN);
    s_conf_badge_text = lv_label_create(s_conf_badge);
    lv_obj_set_style_text_font(s_conf_badge_text, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_conf_badge_text, lv_color_hex(UI_BG), 0);
    lv_obj_center(s_conf_badge_text);
    s_conf_title = text_create(s_voice_panel, 68, 52, 120, UI_INK);
    lv_obj_set_height(s_conf_title, 66);
    lv_obj_add_flag(s_conf_title, LV_OBJ_FLAG_HIDDEN);

    // 分页页码指示(右上角)
    s_pager_ind = text_create(s_voice_panel, 170, 16, 40, UI_DIM);
    lv_obj_set_style_text_align(s_pager_ind, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(s_pager_ind, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

// 100ms 声波动画:电平驱动 5 根竖条(带衰减),红点呼吸。
static void wave_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_wave_active) return;
    static const uint8_t spread[5] = { 60, 85, 100, 80, 55 };   // 各条灵敏差异
    uint8_t level = s_voice_level;
    s_breath = (uint8_t)(s_breath + 1);
    // 呼吸:0..10 锯齿 → 透明度 80..255..80
    uint8_t phase = s_breath % 10;
    lv_obj_set_style_opa(s_voice_dot,
        (lv_opa_t)(phase < 5 ? 80 + phase * 35 : 80 + (9 - phase) * 35), 0);
    for (int i = 0; i < 5; i++) {
        uint8_t target = (uint8_t)((uint16_t)level * spread[i] / 100);
        uint8_t cur = s_bar_values[i];
        s_bar_values[i] = target > cur ? target
                          : (uint8_t)(cur * 3 / 4);   // 上升即跟,下降缓释
        int h = 4 + s_bar_values[i] * 64 / 100;
        lv_obj_set_height(s_voice_bars[i], h);
        lv_obj_set_y(s_voice_bars[i], 154 - h);
    }
}

static void flash_hide(lv_timer_t *timer)
{
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    if (s_flash_timer) {
        lv_timer_delete(s_flash_timer);
        s_flash_timer = NULL;
    }
    (void)timer;
}

void jianlu_ui_success_flash(void)
{
    lv_obj_add_flag(s_flash_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_flash_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_flash_l1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_flash_l2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    if (s_flash_timer) lv_timer_delete(s_flash_timer);
    s_flash_timer = lv_timer_create(flash_hide, 800, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
}

static void flash_build(lv_obj_t *scr)
{
    s_flash = panel_create(scr, 70, 120, 100, 80, UI_CARD_TOP, 12);
    lv_obj_set_style_border_width(s_flash, 1, 0);
    lv_obj_set_style_border_color(s_flash, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_border_opa(s_flash, LV_OPA_40, 0);

    // 对勾:两条 lv_line(短撇 + 长捺),坐标相对 line 对象
    static lv_point_precise_t s_pts1[2];
    static lv_point_precise_t s_pts2[2];
    s_pts1[0].x = 32; s_pts1[0].y = 42;
    s_pts1[1].x = 44; s_pts1[1].y = 54;
    s_pts2[0].x = 44; s_pts2[0].y = 54;
    s_pts2[1].x = 68; s_pts2[1].y = 28;
    s_flash_l1 = lv_line_create(s_flash);
    lv_line_set_points(s_flash_l1, s_pts1, 2);
    lv_obj_set_style_line_width(s_flash_l1, 5, 0);
    lv_obj_set_style_line_color(s_flash_l1, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_line_rounded(s_flash_l1, true, 0);
    s_flash_l2 = lv_line_create(s_flash);
    lv_line_set_points(s_flash_l2, s_pts2, 2);
    lv_obj_set_style_line_width(s_flash_l2, 5, 0);
    lv_obj_set_style_line_color(s_flash_l2, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_line_rounded(s_flash_l2, true, 0);

    // 文字模式(overlay_flash 用,默认隐藏)
    s_flash_title = text_create(s_flash, 0, 12, 100, UI_INK);
    lv_obj_set_style_text_align(s_flash_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_flash_title, LV_OBJ_FLAG_HIDDEN);
    s_flash_body = text_create(s_flash, 10, 40, 80, UI_DIM);
    lv_obj_set_style_text_align(s_flash_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_flash_body, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
}

static void voice_show(const char *title, uint32_t title_color,
                       const char *body, const char *hint)
{
    lv_label_set_text(s_voice_title, title);
    lv_obj_set_style_text_color(s_voice_title, lv_color_hex(title_color), 0);
    lv_obj_set_y(s_voice_title, 20);
    lv_label_set_text(s_voice_body, body);
    lv_obj_set_y(s_voice_body, 60);
    lv_obj_set_height(s_voice_body, 120);
    lv_obj_set_style_text_color(s_voice_body, lv_color_hex(UI_INK), 0);
    lv_label_set_text(s_voice_hint, hint);
    lv_obj_set_y(s_voice_hint, 200);
    lv_obj_set_height(s_voice_hint, 24);
    lv_obj_set_style_text_color(s_voice_hint, lv_color_hex(UI_DIM), 0);
    s_wave_active = false;
    s_pager_open = false;
    lv_obj_add_flag(s_voice_dot, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 5; i++) lv_obj_add_flag(s_voice_bars[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_conf_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_conf_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pager_ind, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

void jianlu_ui_voice_set_level(uint8_t level)
{
    s_voice_level = level;
}

void jianlu_ui_voice_idle(void)
{
    s_wave_active = false;
    lv_obj_add_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

void jianlu_ui_voice_recording(int elapsed_sec)
{
    char body[40];
    int sec = elapsed_sec;
    if (sec > JIANLU_VOICE_MAX_SEC) sec = JIANLU_VOICE_MAX_SEC;
    if (sec < 0) sec = 0;
    snprintf(body, sizeof(body), "%02d / %ds", sec, JIANLU_VOICE_MAX_SEC);
    voice_show("录音中", UI_ACCENT, body, "松开结束");
    // 声波:红点 + 竖条显示,秒数文本上移让位
    lv_obj_set_y(s_voice_title, 6);
    lv_obj_set_y(s_voice_dot, 10);
    lv_obj_set_y(s_voice_body, 40);
    lv_obj_remove_flag(s_voice_dot, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 5; i++) {
        s_bar_values[i] = 0;
        lv_obj_remove_flag(s_voice_bars[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_wave_active = true;
}

void jianlu_ui_voice_sending(void)
{
    voice_show("上传中…", UI_INK, "", "说完啦,正在识别");
}

void jianlu_ui_voice_confirm(const jianlu_capture_result_t *res)
{
    static char safe_transcript[JIANLU_TRANSCRIPT_LEN];
    static char safe_reply[JIANLU_REPLY_LEN];
    sanitize_text_keep_lf(safe_transcript, sizeof(safe_transcript),
                          res ? res->transcript : NULL);
    sanitize_text_keep_lf(safe_reply, sizeof(safe_reply), res ? res->reply : NULL);

    if (res == NULL || res->new_count == 0) {
        // 没建出卡片:以 reply 为主
        const char *body = safe_reply[0] ? safe_reply : safe_transcript;
        voice_show("记下了", UI_ACCENT, body, "OK 返回");
        return;
    }

    // 以新卡片为主:徽标 + 大标题居中;transcript 次之;reply 缩为底部小字
    static char safe_title[JIANLU_TITLE_LEN];
    sanitize_text(safe_title, sizeof(safe_title), res->new_title);
    lv_obj_set_style_bg_color(s_conf_badge, badge_color(res->new_type), 0);
    lv_label_set_text(s_conf_badge_text, jianlu_type_badge(res->new_type));
    lv_label_set_text(s_conf_title, safe_title);
    lv_obj_remove_flag(s_conf_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_conf_title, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(s_voice_title, "记下了");
    lv_obj_set_style_text_color(s_voice_title, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_y(s_voice_title, 16);

    lv_label_set_text(s_voice_body, safe_transcript);
    lv_obj_set_y(s_voice_body, 132);
    lv_obj_set_height(s_voice_body, 36);
    lv_obj_set_style_text_color(s_voice_body, lv_color_hex(UI_DIM), 0);
    lv_label_set_long_mode(s_voice_body, LV_LABEL_LONG_DOT);

    char bottom[240];
    snprintf(bottom, sizeof(bottom), "%s\n新增 %d 条 · OK 返回",
             safe_reply, res->new_count);
    lv_label_set_text(s_voice_hint, bottom);
    lv_obj_set_y(s_voice_hint, 172);
    lv_obj_set_height(s_voice_hint, 44);
    lv_obj_set_style_text_color(s_voice_hint, lv_color_hex(UI_DIM), 0);
    lv_label_set_long_mode(s_voice_hint, LV_LABEL_LONG_DOT);

    lv_obj_remove_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// 分页文本查看器(通用组件)
// ---------------------------------------------------------------------------
static void pager_render(void)
{
    char page[JIANLU_TRANSCRIPT_LEN + JIANLU_REPLY_LEN + 8];
    jianlu_pager_page_text(&s_pager, page, sizeof(page));
    lv_label_set_text(s_voice_body, page);
    lv_obj_set_y(s_voice_body, 48);
    lv_obj_set_height(s_voice_body, 150);
    lv_obj_set_style_text_color(s_voice_body, lv_color_hex(UI_INK), 0);

    if (s_pager.pages > 1) {
        lv_obj_remove_flag(s_pager_ind, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_pager_ind, "%d/%d", s_pager.page + 1, s_pager.pages);
    } else {
        lv_obj_add_flag(s_pager_ind, LV_OBJ_FLAG_HIDDEN);
    }
}

void jianlu_ui_pager_open(const char *title, const char *text)
{
    sanitize_text_keep_lf(s_pager_full, sizeof(s_pager_full), text);
    jianlu_pager_init(&s_pager, s_pager_full, PAGER_COLS, PAGER_ROWS);

    lv_label_set_text(s_voice_title, title ? title : "");
    lv_obj_set_style_text_color(s_voice_title, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_y(s_voice_title, 16);
    lv_label_set_long_mode(s_voice_title, LV_LABEL_LONG_DOT);

    lv_label_set_text(s_voice_hint, "UP/DOWN 翻页 · OK 返回");
    lv_obj_set_y(s_voice_hint, 200);
    lv_obj_set_height(s_voice_hint, 24);
    lv_obj_set_style_text_color(s_voice_hint, lv_color_hex(UI_DIM), 0);

    s_wave_active = false;
    lv_obj_add_flag(s_voice_dot, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 5; i++) lv_obj_add_flag(s_voice_bars[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_conf_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_conf_title, LV_OBJ_FLAG_HIDDEN);

    pager_render();
    s_pager_open = true;
    lv_obj_remove_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

bool jianlu_ui_pager_is_open(void)
{
    return s_pager_open;
}

void jianlu_ui_pager_close(void)
{
    s_pager_open = false;
    lv_obj_add_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
}

void jianlu_ui_pager_next(void)
{
    if (s_pager_open && jianlu_pager_next(&s_pager)) pager_render();
}

void jianlu_ui_pager_prev(void)
{
    if (s_pager_open && jianlu_pager_prev(&s_pager)) pager_render();
}

bool jianlu_ui_confirm_present(const jianlu_capture_result_t *res)
{
    // 组完整内容:transcript 为主,reply 附后(有一即可)
    static char full[JIANLU_TRANSCRIPT_LEN + JIANLU_REPLY_LEN + 8];
    static char t[JIANLU_TRANSCRIPT_LEN];
    static char r[JIANLU_REPLY_LEN];
    sanitize_text_keep_lf(t, sizeof(t), res ? res->transcript : NULL);
    sanitize_text_keep_lf(r, sizeof(r), res ? res->reply : NULL);
    if (t[0] && r[0]) {
        snprintf(full, sizeof(full), "%s\n\n%s", t, r);
    } else {
        snprintf(full, sizeof(full), "%s%s", t, r);
    }

    bool fits = jianlu_pager_count_pages(full, PAGER_COLS, PAGER_ROWS) <= 1;
    if (fits) {
        jianlu_ui_voice_confirm(res);   // 经典布局(新卡为主 / reply 为主)
        return false;
    }
    // 分页模式:标题行带新卡名(有一行省略),正文全文分页
    if (res != NULL && res->new_count > 0) {
        char name[JIANLU_TITLE_LEN];
        sanitize_text(name, sizeof(name), res->new_title);
        char title[JIANLU_TITLE_LEN + 12];
        snprintf(title, sizeof(title), "记下了 · %.80s", name);
        jianlu_ui_pager_open(title, full);
    } else {
        jianlu_ui_pager_open("记下了", full);
    }
    return true;
}

void jianlu_ui_voice_error(void)
{
    voice_show("发送失败", 0xD96A5A, "录音已保留\n联网后自动补传", "OK 返回");
}

void jianlu_ui_overlay(const char *title, uint32_t color_hex,
                       const char *body, const char *hint)
{
    voice_show(title, color_hex, body, hint);
}

void jianlu_ui_overlay_hide(void)
{
    jianlu_ui_voice_idle();
}

// 短暂提示浮层(1.5s 自动消失,不拦截按键):复用 flash 面板
void jianlu_ui_overlay_flash(const char *title, uint32_t color_hex, const char *body)
{
    static char safe[64];
    sanitize_text(safe, sizeof(safe), body);
    lv_obj_add_flag(s_flash_l1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_flash_l2, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_flash_title, title ? title : "");
    lv_obj_set_style_text_color(s_flash_title, lv_color_hex(color_hex), 0);
    lv_label_set_text(s_flash_body, safe);
    lv_obj_remove_flag(s_flash_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_flash_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    if (s_flash_timer) lv_timer_delete(s_flash_timer);
    s_flash_timer = lv_timer_create(flash_hide, 1500, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
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

    s_offline = lv_label_create(scr);
    lv_obj_set_pos(s_offline, 140, 15);
    lv_obj_set_style_text_font(s_offline, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_offline, lv_color_hex(UI_ACCENT), 0);
    lv_label_set_text(s_offline, "离线");
    lv_obj_add_flag(s_offline, LV_OBJ_FLAG_HIDDEN);

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
    flash_build(scr);

    lv_timer_create(wave_tick, 100, NULL);
    lv_timer_create(battery_tick, 10000, NULL);
    battery_tick(NULL);

    lv_screen_load(scr);
}

void jianlu_ui_refresh(const jianlu_store_t *store, jianlu_anim_t anim)
{
    deck_refresh(store, anim);
    bool show_list = store->view == JIANLU_VIEW_READY && store->count > 0;
    lv_label_set_text(s_status, show_list ? "" : status_text(store));
    if (store->offline && store->view == JIANLU_VIEW_READY) {
        lv_obj_remove_flag(s_offline, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_offline, LV_OBJ_FLAG_HIDDEN);
    }
}
