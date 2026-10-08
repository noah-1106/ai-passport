// main/jianlu_dlink_codec.c —— 见 jianlu_dlink_codec.h。
#include "jianlu_dlink_codec.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

// ---- base64 ----
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t jianlu_dlink_b64_encode(char *dst, size_t cap, const uint8_t *src,
                               size_t len)
{
    size_t need = ((len + 2) / 3) * 4 + 1;
    if (dst == NULL || cap < need) return 0;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        int rem = (len - i) & 0xFFFF;
        if (rem > 1) v |= (uint32_t)src[i + 1] << 8;
        if (rem > 2) v |= src[i + 2];
        dst[o++] = B64[(v >> 18) & 0x3F];
        dst[o++] = B64[(v >> 12) & 0x3F];
        dst[o++] = (rem > 1) ? B64[(v >> 6) & 0x3F] : '=';
        dst[o++] = (rem > 2) ? B64[v & 0x3F] : '=';
    }
    dst[o] = '\0';
    return o;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t jianlu_dlink_b64_decode(uint8_t *dst, size_t cap, const char *src,
                               size_t len, size_t *need)
{
    // 去掉 padding 计算原始长度
    size_t k = len;
    while (k > 0 && src[k - 1] == '=') k--;
    size_t out_len = (k / 4) * 3 + (k % 4 == 2 ? 1 : (k % 4 == 3 ? 2 : 0));
    if (need != NULL) *need = out_len;
    if (dst == NULL || cap < out_len) return (size_t)-1;
    if (k % 4 == 1) return (size_t)-1;   // 非法长度

    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < k; i++) {
        int v = b64_val(src[i]);
        if (v < 0) return (size_t)-1;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            dst[o++] = (uint8_t)(acc >> bits);
        }
    }
    return o;
}

// ---- 命令解析 ----
static bool get_int(const cJSON *root, const char *key, int *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(v)) return false;
    *out = v->valueint;
    return true;
}

// imgb/imgc 的 kind:非空短串(具体取值由调用方映射到缓存路径)
static bool dlink_get_kind(const cJSON *root, char *dst, size_t cap)
{
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(root, "kind");
    if (!cJSON_IsString(k) || k->valuestring == NULL
        || k->valuestring[0] == '\0' || strlen(k->valuestring) >= cap) {
        return false;
    }
    snprintf(dst, cap, "%s", k->valuestring);
    return true;
}

// 在原始行里找 "key":"value" 的 value 段(不依赖 cJSON 树的生命周期)。
// 协议载荷是 base64(不含 '"' 与转义),扫到下一个 '"' 即值尾。
static bool find_raw_string(const char *line, size_t len, const char *key,
                            const char **out, size_t *out_len)
{
    char pat[16];
    int pn = snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    if (pn <= 0 || (size_t)pn >= sizeof(pat)) return false;
    for (size_t i = 0; i + (size_t)pn < len; i++) {
        if (memcmp(line + i, pat, (size_t)pn) != 0) continue;
        size_t start = i + (size_t)pn;
        for (size_t j = start; j < len; j++) {
            if (line[j] == '"') {
                if (j == start) return false;   // 空值
                *out = line + start;
                *out_len = j - start;
                return true;
            }
        }
        return false;
    }
    return false;
}

bool jianlu_dlink_parse(const char *line, size_t len, jianlu_dl_msg_t *out)
{
    memset(out, 0, sizeof(*out));
    if (line == NULL || len == 0 || len > JIANLU_DL_LINE_MAX) {
        out->cmd = JIANLU_DL_CMD_BAD;
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(line, len);
    if (root == NULL || !cJSON_IsObject(root)) {
        out->cmd = JIANLU_DL_CMD_BAD;
        cJSON_Delete(root);
        return false;
    }
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "c");
    bool ok = cJSON_IsString(c) && c->valuestring != NULL;
    if (ok) {
        const char *s = c->valuestring;
        if (strcmp(s, "records") == 0) {
            out->cmd = JIANLU_DL_CMD_RECORDS;
            const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "records");
            if (!cJSON_IsArray(arr)) {
                ok = false;
            } else {
                // 载荷 = 整行(records_into_store 只读 "records" 键)
                out->records_json = line;
                out->records_json_len = len;
                get_int(root, "seq", &out->seq);
                get_int(root, "total", &out->total);
            }
        } else if (strcmp(s, "plist") == 0) out->cmd = JIANLU_DL_CMD_PLIST;
        else if (strcmp(s, "pdone") == 0) {
            out->cmd = JIANLU_DL_CMD_PDONE;
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
            if (cJSON_IsString(id)) {
                jianlu_utf8_copy(out->id, sizeof(out->id), id->valuestring,
                                 sizeof(out->id) - 1);
            } else if (cJSON_IsNumber(id)) {
                snprintf(out->id, sizeof(out->id), "%d", id->valueint);
            } else {
                ok = false;
            }
        } else if (strcmp(s, "vlist") == 0) out->cmd = JIANLU_DL_CMD_VLIST;
        else if (strcmp(s, "vget") == 0 || strcmp(s, "vdel") == 0) {
            out->cmd = (s[1] == 'g') ? JIANLU_DL_CMD_VGET : JIANLU_DL_CMD_VDEL;
            ok = get_int(root, "slot", &out->slot);
        } else if (strcmp(s, "reset") == 0) out->cmd = JIANLU_DL_CMD_RESET;
        else if (strcmp(s, "time") == 0) {
            out->cmd = JIANLU_DL_CMD_TIME;
            const cJSON *e = cJSON_GetObjectItemCaseSensitive(root, "epoch");
            if (cJSON_IsNumber(e)) {
                out->epoch = (int64_t)e->valuedouble;
            } else {
                ok = false;
            }
        } else if (strcmp(s, "profile") == 0) {
            out->cmd = JIANLU_DL_CMD_PROFILE;
            const cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "nickname");
            const cJSON *g = cJSON_GetObjectItemCaseSensitive(root, "signature");
            if (!cJSON_IsString(n) && n != NULL) ok = false;
            if (!cJSON_IsString(g) && g != NULL) ok = false;
            if (ok) {
                jianlu_utf8_copy(out->nickname, sizeof(out->nickname),
                                 cJSON_IsString(n) ? n->valuestring : "",
                                 sizeof(out->nickname) - 1);
                jianlu_utf8_copy(out->signature, sizeof(out->signature),
                                 cJSON_IsString(g) ? g->valuestring : "",
                                 sizeof(out->signature) - 1);
            }
        } else if (strcmp(s, "imgb") == 0) {
            out->cmd = JIANLU_DL_CMD_IMGB;
            ok = dlink_get_kind(root, out->kind, sizeof(out->kind))
                 && get_int(root, "total", &out->total) && out->total >= 1;
        } else if (strcmp(s, "imgc") == 0) {
            out->cmd = JIANLU_DL_CMD_IMGC;
            ok = dlink_get_kind(root, out->kind, sizeof(out->kind))
                 && get_int(root, "seq", &out->seq);
            if (ok) {
                // b64 载荷直接指行内原文(cJSON 树销毁后仍有效);
                // b64 字母表不含 '"' 与 '\',扫到下一个引号即值尾
                const char *p = NULL;
                size_t plen = 0;
                if (find_raw_string(line, len, "data", &p, &plen)) {
                    out->data_b64 = p;
                    out->data_b64_len = plen;
                } else {
                    ok = false;
                }
            }
        } else ok = false;
    } else {
        ok = false;
    }
    cJSON_Delete(root);
    if (!ok) {
        out->cmd = JIANLU_DL_CMD_BAD;
        return false;
    }
    return true;
}

// ---- 响应构建 ----
size_t jianlu_dlink_build_ok(char *buf, size_t cap)
{
    int n = snprintf(buf, cap, "{\"r\":\"ok\"}");
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t jianlu_dlink_build_bad(char *buf, size_t cap)
{
    int n = snprintf(buf, cap, "{\"r\":\"bad\"}");
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t jianlu_dlink_build_status(char *buf, size_t cap, int count, bool wifi_saved)
{
    int n = snprintf(buf, cap, "{\"r\":\"ok\",\"mode\":\"dlink\",\"cnt\":%d,\"wifi\":%s}",
                     count, wifi_saved ? "saved" : "none");
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t jianlu_dlink_build_plist(char *buf, size_t cap,
                                const char (*ids)[JIANLU_ID_LEN], int n_)
{
    size_t used = 0;
    int n = snprintf(buf, cap, "{\"r\":\"plist\",\"ids\":[");
    if (n <= 0 || (size_t)n >= cap) return 0;
    used = (size_t)n;
    for (int i = 0; i < n_; i++) {
        const char *piece = i ? ",\"%s\"" : "\"%s\"";
        n = snprintf(buf + used, cap - used, piece, ids[i]);
        if (n <= 0 || (size_t)n >= cap - used) return 0;
        used += (size_t)n;
    }
    n = snprintf(buf + used, cap - used, "]}");
    if (n <= 0 || (size_t)n >= cap - used) return 0;
    return used + (size_t)n;
}

size_t jianlu_dlink_build_vlist(char *buf, size_t cap, const int *slots, int n_)
{
    size_t used = 0;
    int n = snprintf(buf, cap, "{\"r\":\"vlist\",\"slots\":[");
    if (n <= 0 || (size_t)n >= cap) return 0;
    used = (size_t)n;
    for (int i = 0; i < n_; i++) {
        int n2 = snprintf(buf + used, cap - used, i ? ",%d" : "%d", slots[i]);
        if (n2 <= 0 || (size_t)n2 >= cap - used) return 0;
        used += (size_t)n2;
    }
    n = snprintf(buf + used, cap - used, "]}");
    if (n <= 0 || (size_t) n >= cap - used) return 0;
    return used + (size_t)n;
}

size_t jianlu_dlink_build_vchunk(char *buf, size_t cap, int slot, int seq,
                                 int total, const uint8_t *raw, size_t raw_len)
{
    if (raw == NULL || raw_len == 0 || raw_len > JIANLU_DL_VOICE_CHUNK) return 0;
    // {"r":"vc","slot":1,"seq":0,"total":8,"data":"..."} 前缀约 48 字符
    static const char FMT[] =
        "{\"r\":\"vc\",\"slot\":%d,\"seq\":%d,\"total\":%d,\"data\":\"";
    int n = snprintf(buf, cap, FMT, slot, seq, total);
    if (n <= 0 || (size_t)n + 1 >= cap) return 0;
    size_t used = (size_t)n;
    size_t enc = jianlu_dlink_b64_encode(buf + used, cap - used - 2, raw, raw_len);
    if (enc == 0) return 0;
    used += enc;
    if (used + 3 > cap) return 0;
    buf[used++] = '"';
    buf[used++] = '}';
    buf[used] = '\0';
    return used;
}

// ---- records → store ----
static const char *json_string_(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

int jianlu_dlink_records_into_store(const char *json, size_t len,
                                   jianlu_store_t *store)
{
    if (json == NULL) return -1;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) return -1;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "records");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return -1;
    }
    int before = store->count;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (!cJSON_IsObject(item)) continue;
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        char idbuf[JIANLU_ID_LEN];
        if (cJSON_IsString(id)) {
            jianlu_utf8_copy(idbuf, sizeof(idbuf), id->valuestring,
                             sizeof(idbuf) - 1);
        } else if (cJSON_IsNumber(id)) {
            snprintf(idbuf, sizeof(idbuf), "%d", id->valueint);
        } else {
            continue;
        }
        jianlu_store_add(store, idbuf,
                         json_string_(item, "title"),
                         json_string_(item, "summary"),
                         jianlu_type_from_string(json_string_(item, "type")),
                         json_string_(item, "createdAt"),
                         NULL, NULL);
    }
    cJSON_Delete(root);
    return store->count - before;
}
