// tests/test_jianlu_json.c —— 中枢响应解析的 host 侧测试。
// cJSON 直接编 ESP-IDF 源码树里的实现(tools/validate.sh 负责定位)。
#include <assert.h>
#include <string.h>

#include "jianlu_json.h"

static const char BODY[] =
    "{\"total\":2,\"records\":["
    "{\"id\":\"rec_001\",\"title\":\"牙医复诊\",\"summary\":\"周三上午十点\",\"type\":\"todo\",\"status\":\"pending\",\"tags\":[\"健康\"],\"createdAt\":\"2026-10-01T09:00:00Z\"},"
    "{\"id\":\"rec_002\",\"title\":\"LVGL 中文字体集成\",\"summary\":\"读 lvgl-chinese-fonts.md\",\"type\":\"article\",\"status\":\"pending\",\"tags\":[],\"createdAt\":\"2026-10-02T10:00:00Z\"}"
    "]}";

static void test_parse_happy_path(void) {
    jianlu_store_t store;
    jianlu_store_init(&store);

    int n = jianlu_json_parse_records(BODY, sizeof(BODY) - 1, &store);
    assert(n == 2);
    assert(store.count == 2);

    assert(strcmp(store.records[0].id, "rec_001") == 0);
    assert(strcmp(store.records[0].title, "牙医复诊") == 0);
    assert(strcmp(store.records[0].summary, "周三上午十点") == 0);
    assert(store.records[0].type == JIANLU_TYPE_TODO);
    assert(!store.records[0].completing);
    assert(strcmp(store.records[0].date, "2026-10-01") == 0);
    assert(store.records[0].tag_count == 1);
    assert(strcmp(store.records[0].tags[0], "健康") == 0);

    assert(strcmp(store.records[1].id, "rec_002") == 0);
    assert(store.records[1].type == JIANLU_TYPE_ARTICLE);
}

static void test_parse_missing_fields(void) {
    // 缺 summary/type:按空串与 OTHER 处理,不崩
    static const char body[] =
        "{\"records\":[{\"id\":\"r1\",\"title\":\"只有标题\"}]}";
    jianlu_store_t store;
    jianlu_store_init(&store);
    int n = jianlu_json_parse_records(body, sizeof(body) - 1, &store);
    assert(n == 1);
    assert(strcmp(store.records[0].summary, "") == 0);
    assert(store.records[0].type == JIANLU_TYPE_OTHER);
}

static void test_parse_invalid_json(void) {
    jianlu_store_t store;
    jianlu_store_init(&store);
    // 预填一条:解析失败不得清掉现有内容
    jianlu_store_replace_begin(&store);
    jianlu_store_add(&store, "keep", "t", "s", JIANLU_TYPE_TODO, NULL, NULL, NULL);
    jianlu_store_replace_end(&store);

    static const char bad[] = "{\"records\":[";
    assert(jianlu_json_parse_records(bad, sizeof(bad) - 1, &store) == -1);
    assert(store.count == 1);
    assert(strcmp(store.records[0].id, "keep") == 0);

    static const char not_array[] = "{\"records\":{}}";
    assert(jianlu_json_parse_records(not_array, sizeof(not_array) - 1, &store) == -1);
    assert(store.count == 1);
}

static void test_parse_long_fields_truncated(void) {
    // 超长标题(60 个汉字 = 180 字节)必须按 UTF-8 边界截断,不留半个字
    static const char body[] =
        "{\"records\":[{\"id\":\"r1\",\"title\":\""
        "一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十"
        "一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十"
        "\",\"summary\":\"s\",\"type\":\"other\"}]}";
    jianlu_store_t store;
    jianlu_store_init(&store);
    int n = jianlu_json_parse_records(body, sizeof(body) - 1, &store);
    assert(n == 1);
    size_t len = strlen(store.records[0].title);
    assert(len % 3 == 0);   // 纯中文,截断后字节数必须是 3 的倍数
    assert(len > JIANLU_TITLE_LEN - 8 && len < JIANLU_TITLE_LEN);   // 贴近容量上限
}

static void test_parse_numeric_id(void) {
    // 中枢实测响应:id 是整数而非字符串,且带 content/link 等多余字段
    static const char body[] =
        "{\"total\":1,\"page\":1,\"pageSize\":20,\"records\":[{"
        "\"id\":2,\"title\":\"牙医复诊\",\"content\":\"后天上午十点约了牙医复诊。\","
        "\"summary\":\"牙医复诊\",\"type\":\"todo\",\"status\":\"pending\","
        "\"tags\":[\"健康\",\"预约\"],\"link\":\"\",\"startTime\":null,"
        "\"endTime\":null,\"createdAt\":\"2026-10-03 11:53:28\"}]}";
    jianlu_store_t store;
    jianlu_store_init(&store);
    int n = jianlu_json_parse_records(body, sizeof(body) - 1, &store);
    assert(n == 1);
    assert(strcmp(store.records[0].id, "2") == 0);
    assert(strcmp(store.records[0].title, "牙医复诊") == 0);
}

static void test_parse_capture_response(void) {
    // 语音上传响应:transcript + reply + 新建 records
    static const char body[] =
        "{\"transcript\":\"记一下今天下午三点开会\","
        "\"reply\":\"好的,已记下今天下午三点开会\","
        "\"records\":[{\"id\":7,\"title\":\"下午三点开会\",\"type\":\"todo\"}]}";
    jianlu_capture_result_t out;
    assert(jianlu_json_parse_capture(body, sizeof(body) - 1, &out) == 0);
    assert(strcmp(out.transcript, "记一下今天下午三点开会") == 0);
    assert(strcmp(out.reply, "好的,已记下今天下午三点开会") == 0);
    assert(out.new_count == 1);
    // records[0] 概要(确认页展示)
    assert(strcmp(out.new_title, "下午三点开会") == 0);
    assert(out.new_type == JIANLU_TYPE_TODO);

    // 字段缺失容忍
    static const char partial[] = "{\"transcript\":\"嗯\"}";
    assert(jianlu_json_parse_capture(partial, sizeof(partial) - 1, &out) == 0);
    assert(strcmp(out.transcript, "嗯") == 0);
    assert(out.reply[0] == '\0');
    assert(out.new_count == 0);

    // 坏 JSON
    static const char bad[] = "{\"transcript\":";
    assert(jianlu_json_parse_capture(bad, sizeof(bad) - 1, &out) == -1);
}

static void test_parse_profile(void) {
    static const char body[] =
        "{\"nickname\":\"陈一诺\",\"signature\":\"随时记录\","
        "\"hasAvatar\":true,\"hasQrcode\":false}";
    jianlu_profile_t out;
    assert(jianlu_json_parse_profile(body, sizeof(body) - 1, &out) == 0);
    assert(strcmp(out.nickname, "陈一诺") == 0);
    assert(strcmp(out.signature, "随时记录") == 0);
    assert(out.has_avatar);
    assert(!out.has_qrcode);

    // 空资料(出厂)
    static const char empty[] =
        "{\"nickname\":\"\",\"signature\":\"\",\"hasAvatar\":false,\"hasQrcode\":false}";
    assert(jianlu_json_parse_profile(empty, sizeof(empty) - 1, &out) == 0);
    assert(out.nickname[0] == '\0');
    assert(!out.has_avatar);
    assert(out.avatar_version == 0);   // 无版本字段按 0

    // 版本号字段(mtime 毫秒,浮点)
    static const char ver[] =
        "{\"nickname\":\"N\",\"signature\":\"\",\"hasAvatar\":true,"
        "\"hasQrcode\":true,\"avatarVersion\":1791292974659.5,"
        "\"qrcodeVersion\":1791292974686.8}";
    assert(jianlu_json_parse_profile(ver, sizeof(ver) - 1, &out) == 0);
    assert(out.avatar_version == 1791292974659ULL);
    assert(out.qrcode_version == 1791292974686ULL);
    // 相同版本 → 调用方跳过下载(决策函数见 test_jianlu_config)

    static const char bad[] = "{\"nickname\":";
    assert(jianlu_json_parse_profile(bad, sizeof(bad) - 1, &out) == -1);
}

int main(void) {
    test_parse_happy_path();
    test_parse_numeric_id();
    test_parse_missing_fields();
    test_parse_invalid_json();
    test_parse_long_fields_truncated();
    test_parse_capture_response();
    test_parse_profile();
    return 0;
}
