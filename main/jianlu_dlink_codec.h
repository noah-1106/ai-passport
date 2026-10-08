// main/jianlu_dlink_codec.h —— BLE 直连协议编解码(纯 C,host 可测)。
//
// 协议:JSON 行(NUS RX 写入按 '\n' 分帧,TX 通知流同样以 '\n' 结尾,
// 桥接端按行重组)。命令语义照抄中枢 hub API。
//
// 桥→卡片:
//   {"c":"records","total":T,"seq":i,"records":[...]}   分块推送清单
//   {"c":"plist"}                                        要待同步勾选 id 列表
//   {"c":"pdone","id":"7"}                               某勾选已 PUT 成功
//   {"c":"vlist"}                                        要离线语音槽位列表
//   {"c":"vget","slot":1}                                拉取该槽语音(分块)
//   {"c":"vdel","slot":1}                                删除该槽语音
//   {"c":"reset"}                                        清空清单区(重推前)
//   {"c":"time","epoch":<unix秒>}                        对时(卡片 settimeofday)
//   {"c":"profile","nickname":..,"signature":..}         资料(昵称/签名)
//   {"c":"imgb","kind":"avatar"|"qrcode","total":N}      图片开始(N 块)
//   {"c":"imgc","kind":..,"seq":i,"data":"<b64>"}        图片分块(解码后≤1400B/块)
// 卡片→桥:
//   {"r":"ok"[,...]}                                     各命令确认
//   {"r":"plist","ids":["7","9"]}
//   {"r":"vlist","slots":[1,2]}
//   {"r":"vc","slot":1,"seq":i,"total":T,"data":"<b64>"} 语音分块(180B/块)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jianlu_store.h"

#include "jianlu_json.h"     // 昵称/签名缓冲长度与 hub 侧一致

#define JIANLU_DL_CHUNK_RECORDS 2    // 每块最多记录数(由桥分块,卡片只收)
#define JIANLU_DL_VOICE_CHUNK   180  // 语音分块原始字节数(b64 后 240 字符)
#define JIANLU_DL_LINE_MAX      2048  // 单行上限(桥按 2 条/块限长)
#define JIANLU_DL_IMG_RAW_MAX   1400  // imgc 单块解码后原始字节上限(行上限约束)
#define JIANLU_DL_KIND_LEN      8     // "avatar"/"qrcode"

typedef enum {
    JIANLU_DL_CMD_NONE = 0,
    JIANLU_DL_CMD_RECORDS,   // 桥推清单块
    JIANLU_DL_CMD_PLIST,     // 要勾选列表
    JIANLU_DL_CMD_PDONE,     // 某勾选已被桥 PUT 成功
    JIANLU_DL_CMD_VLIST,     // 要语音列表
    JIANLU_DL_CMD_VGET,      // 拉语音
    JIANLU_DL_CMD_VDEL,      // 删语音
    JIANLU_DL_CMD_RESET,     // 清清单
    JIANLU_DL_CMD_TIME,      // 对时
    JIANLU_DL_CMD_PROFILE,   // 资料(昵称/签名)
    JIANLU_DL_CMD_IMGB,      // 图片开始
    JIANLU_DL_CMD_IMGC,      // 图片分块
    JIANLU_DL_CMD_BAD,       // 解析失败/未知
} jianlu_dl_cmd_t;

typedef struct {
    jianlu_dl_cmd_t cmd;
    int seq;
    int total;
    int slot;
    char id[JIANLU_ID_LEN];
    // records 块的载荷(指向行内 JSON 数组,仅解析期间有效)
    const char *records_json;
    size_t records_json_len;
    // time:unix 秒
    int64_t epoch;
    // profile:昵称/签名(字段缺失按空串;UTF-8 安全截断)
    char nickname[JIANLU_NICKNAME_LEN];
    char signature[JIANLU_SIGNATURE_LEN];
    // imgb/imgc:图片类别("avatar"/"qrcode")
    char kind[JIANLU_DL_KIND_LEN];
    // imgc 的 b64 载荷(指向行内原文,仅解析期间有效,调用方立即解码)
    const char *data_b64;
    size_t data_b64_len;
} jianlu_dl_msg_t;

// 解析一行(不含 '\n')。结构非法返回 JIANLU_DL_CMD_BAD。
bool jianlu_dlink_parse(const char *line, size_t len, jianlu_dl_msg_t *out);

// ---- 响应构建(写入 buf,返回长度;cap 不足返回 0)----
size_t jianlu_dlink_build_ok(char *buf, size_t cap);
size_t jianlu_dlink_build_bad(char *buf, size_t cap);
size_t jianlu_dlink_build_status(char *buf, size_t cap, int count, bool wifi_saved);
size_t jianlu_dlink_build_plist(char *buf, size_t cap,
                                const char (*ids)[JIANLU_ID_LEN], int n);
size_t jianlu_dlink_build_vlist(char *buf, size_t cap, const int *slots, int n);
size_t jianlu_dlink_build_vchunk(char *buf, size_t cap, int slot, int seq,
                                 int total, const uint8_t *raw, size_t raw_len);

// base64(标准字母表,无换行);返回写出长度(不含 NUL),cap 不足返回 0
size_t jianlu_dlink_b64_encode(char *dst, size_t cap, const uint8_t *src,
                               size_t len);
// 解码;返回原始字节数,dst 不足(*need 给出所需)返回 SIZE_MAX
size_t jianlu_dlink_b64_decode(uint8_t *dst, size_t cap, const char *src,
                               size_t len, size_t *need);

// records JSON 数组 → 逐条写入 store(begin/end 语义同 jianlu_store)。
// 返回解析成功的条数;-1 结构错误(store 已回滚到本次之前)。
int jianlu_dlink_records_into_store(const char *json, size_t len,
                                   jianlu_store_t *store);
