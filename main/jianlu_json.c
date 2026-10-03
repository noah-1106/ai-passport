// main/jianlu_json.c —— 见 jianlu_json.h。cJSON 在固件里来自 json 组件,
// host 测试直接编 ESP-IDF 源码树里的 cJSON.c(见 tools/validate.sh)。
#include "jianlu_json.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const char *json_string(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

// 中枢的 id 可能是字符串也可能是数字(实测为整数),统一转成字符串。
static void json_id(const cJSON *obj, char *buf, size_t buf_len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, "id");
    if (cJSON_IsString(item)) {
        jianlu_utf8_copy(buf, buf_len, item->valuestring, buf_len - 1);
    } else if (cJSON_IsNumber(item)) {
        snprintf(buf, buf_len, "%d", item->valueint);
    } else if (buf_len > 0) {
        buf[0] = '\0';
    }
}

// tags 数组最多取 2 个字符串。
static void json_tags(const cJSON *obj, const char **tag1, const char **tag2)
{
    *tag1 = NULL;
    *tag2 = NULL;
    const cJSON *tags = cJSON_GetObjectItemCaseSensitive(obj, "tags");
    if (!cJSON_IsArray(tags)) return;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, tags) {
        if (!cJSON_IsString(item)) continue;
        if (*tag1 == NULL) {
            *tag1 = item->valuestring;
        } else {
            *tag2 = item->valuestring;
            break;
        }
    }
}

int jianlu_json_parse_records(const char *body, size_t len, jianlu_store_t *store)
{
    if (body == NULL || store == NULL) return -1;

    cJSON *root = cJSON_ParseWithLength(body, len);
    if (root == NULL) return -1;

    cJSON *records = cJSON_GetObjectItemCaseSensitive(root, "records");
    if (!cJSON_IsArray(records)) {
        cJSON_Delete(root);
        return -1;
    }

    jianlu_store_replace_begin(store);
    int added = 0;
    char id[JIANLU_ID_LEN];
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, records) {
        if (!cJSON_IsObject(item)) continue;
        json_id(item, id, sizeof(id));
        if (id[0] == '\0') continue;   // 没有 id 的记录无法勾选,跳过
        const char *tag1, *tag2;
        json_tags(item, &tag1, &tag2);
        if (!jianlu_store_add(store,
                              id,
                              json_string(item, "title"),
                              json_string(item, "summary"),
                              jianlu_type_from_string(json_string(item, "type")),
                              json_string(item, "createdAt"), tag1, tag2)) {
            break;   // store 已满:保留已解析的部分,不视为错误
        }
        added++;
    }
    jianlu_store_replace_end(store);

    cJSON_Delete(root);
    return added;
}

int jianlu_json_parse_capture(const char *body, size_t len, jianlu_capture_result_t *out)
{
    if (body == NULL || out == NULL) return -1;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(body, len);
    if (root == NULL) return -1;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }

    jianlu_utf8_copy(out->transcript, sizeof(out->transcript),
                     json_string(root, "transcript"), sizeof(out->transcript) - 1);
    jianlu_utf8_copy(out->reply, sizeof(out->reply),
                     json_string(root, "reply"), sizeof(out->reply) - 1);
    const cJSON *records = cJSON_GetObjectItemCaseSensitive(root, "records");
    if (cJSON_IsArray(records)) {
        out->new_count = cJSON_GetArraySize(records);
        const cJSON *first = cJSON_GetArrayItem(records, 0);
        if (cJSON_IsObject(first)) {
            jianlu_utf8_copy(out->new_title, sizeof(out->new_title),
                             json_string(first, "title"),
                             sizeof(out->new_title) - 1);
            out->new_type = jianlu_type_from_string(json_string(first, "type"));
        }
    }

    cJSON_Delete(root);
    return 0;
}
