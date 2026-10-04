// main/jianlu_store.h —— 小诺简录:清单数据与视图状态机(纯 C,不依赖 ESP-IDF/LVGL)。
//
// 设计约束:ESP32-C3 无 PSRAM,所有字符串都是定长缓冲,UTF-8 截断必须落在
// 字符边界上(中文 3 字节/字),否则 LVGL 会画出半个字符的乱码。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define JIANLU_MAX_RECORDS  12
#define JIANLU_ID_LEN       32
#define JIANLU_TITLE_LEN    96    // 字节,约 31 个汉字
#define JIANLU_SUMMARY_LEN  128   // 字节,约 42 个汉字
#define JIANLU_ERROR_LEN    48
#define JIANLU_MAX_TAGS     2     // 顶卡最多展示 2 个标签
#define JIANLU_TAG_LEN      24    // 字节,约 7 个汉字
#define JIANLU_DATE_LEN     11    // "2026-10-03"(取 createdAt 前 10 字符)

typedef enum {
    JIANLU_TYPE_TODO = 0,
    JIANLU_TYPE_ARTICLE,
    JIANLU_TYPE_INSPIRATION,
    JIANLU_TYPE_OTHER,
} jianlu_type_t;

typedef struct {
    char id[JIANLU_ID_LEN];
    char title[JIANLU_TITLE_LEN];
    char summary[JIANLU_SUMMARY_LEN];
    jianlu_type_t type;
    bool completing;    // 已发出完成请求、等待中枢确认
    bool sync_pending;  // 离线勾选,待联网同步
    bool voice_placeholder; // 语音占位卡(未识别的排队录音,非中枢数据)
    int  voice_slot;        // 占位卡对应的队列槽位 1..4;0 = 非占位卡
    char date[JIANLU_DATE_LEN];                 // createdAt 的日期部分,可为空串
    char tags[JIANLU_MAX_TAGS][JIANLU_TAG_LEN]; // 前 tag_count 个有效
    int tag_count;
} jianlu_record_t;

// 语音占位卡 id 前缀:以 0x01 开头,不可能与中枢的数字/字符串 id 冲突。
#define JIANLU_VOICE_PLACEHOLDER_ID "\x01VOICE_P"

// 视图状态机:UI 与网络事件都收敛到这里,刷新时只读它。
typedef enum {
    JIANLU_VIEW_BOOT = 0,     // 启动中
    JIANLU_VIEW_NO_CONFIG,    // 未配置 Wi-Fi 凭据或中枢地址(保留给 Kconfig 缺省提示)
    JIANLU_VIEW_PROVISIONING, // 配网态:BLUFI 广播,等待手机下发凭据
    JIANLU_VIEW_CONNECTING,   // 正在连接 Wi-Fi
    JIANLU_VIEW_DISCOVERING,  // Wi-Fi 已通,mDNS 寻找中枢
    JIANLU_VIEW_LOADING,      // 中枢已确定,正在拉取清单
    JIANLU_VIEW_READY,        // 清单可用(可能为空)
    JIANLU_VIEW_ERROR,        // 出错,error 里有说明
} jianlu_view_t;

typedef struct {
    jianlu_record_t records[JIANLU_MAX_RECORDS];
    int count;
    int selected;             // 0..count-1;count==0 时无意义
    jianlu_view_t view;
    bool offline;             // 离线模式:清单来自本地快照,操作只记待同步
    char error[JIANLU_ERROR_LEN];
} jianlu_store_t;

void jianlu_store_init(jianlu_store_t *store);

// 切换视图;err 仅对 JIANLU_VIEW_ERROR 有意义,传 NULL 表示无附加说明。
void jianlu_store_set_view(jianlu_store_t *store, jianlu_view_t view, const char *err);

// 批量替换清单:begin 清空 → 逐条 add(满了返回 false)→ end 收敛选中项。
// date/tag1/tag2 可传 NULL 或空串;tag 顺序保持中枢原序,最多存 2 个。
void jianlu_store_replace_begin(jianlu_store_t *store);
bool jianlu_store_add(jianlu_store_t *store, const char *id, const char *title,
                      const char *summary, jianlu_type_t type,
                      const char *date, const char *tag1, const char *tag2);
void jianlu_store_replace_end(jianlu_store_t *store);

// 选中项循环移动;delta 取 ±1。空清单为空操作。
void jianlu_store_move(jianlu_store_t *store, int delta);

const jianlu_record_t *jianlu_store_selected(const jianlu_store_t *store);

// 按 id 标记/清除"完成中";返回是否找到。
bool jianlu_store_set_completing(jianlu_store_t *store, const char *id, bool completing);

// 按 id 标记/清除"离线待同步";返回是否找到。
bool jianlu_store_set_sync_pending(jianlu_store_t *store, const char *id, bool pending);

// ---- 语音占位卡(队列录音的清单内呈现)----
// 精确同步为 N 张占位卡(N=队列深度 0..4):先移除全部旧占位卡,再把
// 槽位 1..N 的卡依次插到清单顶部,选中跟随平移。返回实际放置的张数
// (清单空间不足时少于 N)。
int jianlu_store_set_voice_placeholders(jianlu_store_t *store, int count);
bool jianlu_store_is_voice_placeholder(const jianlu_record_t *rec);

// 按 id 删除并收敛选中项;返回是否找到。
bool jianlu_store_remove(jianlu_store_t *store, const char *id);

jianlu_type_t jianlu_type_from_string(const char *type);
// 徽标单字(待办→办 文章→读 灵感→感 其他→其),静态字符串,勿释放。
const char *jianlu_type_badge(jianlu_type_t type);

// UTF-8 安全截断:把 src 拷进 dst(容量 dst_size,含 NUL),最多 max_bytes 字节,
// 不在多字节字符中间切断。dst_size==0 时不写任何内容。
void jianlu_utf8_copy(char *dst, size_t dst_size, const char *src, size_t max_bytes);
