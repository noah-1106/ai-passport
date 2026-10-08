// tests/test_jianlu_dlink_codec.c —— 直连协议编解码 host 侧测试。
// 覆盖:基础命令、time/profile/imgb/imgc 新命令解析、imgc b64 载荷解码。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jianlu_dlink_codec.h"

static void parse(const char *line, jianlu_dl_msg_t *m)
{
    assert(jianlu_dlink_parse(line, strlen(line), m));
}

static void parse_bad(const char *line)
{
    jianlu_dl_msg_t m;
    assert(!jianlu_dlink_parse(line, strlen(line), &m));
    assert(m.cmd == JIANLU_DL_CMD_BAD);
}

static void test_basic_cmds(void)
{
    jianlu_dl_msg_t m;
    parse("{\"c\":\"plist\"}", &m);
    assert(m.cmd == JIANLU_DL_CMD_PLIST);
    parse("{\"c\":\"pdone\",\"id\":\"21\"}", &m);
    assert(m.cmd == JIANLU_DL_CMD_PDONE && strcmp(m.id, "21") == 0);
    parse("{\"c\":\"pdone\",\"id\":7}", &m);
    assert(m.cmd == JIANLU_DL_CMD_PDONE && strcmp(m.id, "7") == 0);
    parse("{\"c\":\"vget\",\"slot\":3}", &m);
    assert(m.cmd == JIANLU_DL_CMD_VGET && m.slot == 3);
    parse("{\"c\":\"vdel\",\"slot\":1}", &m);
    assert(m.cmd == JIANLU_DL_CMD_VDEL && m.slot == 1);
    parse("{\"c\":\"reset\"}", &m);
    assert(m.cmd == JIANLU_DL_CMD_RESET);
    parse_bad("{\"c\":\"unknown\"}");
    parse_bad("not json");
    parse_bad("{\"c\":\"vget\"}");   // 缺 slot
}

static void test_time(void)
{
    jianlu_dl_msg_t m;
    parse("{\"c\":\"time\",\"epoch\":1760000000}", &m);
    assert(m.cmd == JIANLU_DL_CMD_TIME && m.epoch == 1760000000);
    // 缺 epoch → 非法
    parse_bad("{\"c\":\"time\"}");
    parse_bad("{\"c\":\"time\",\"epoch\":\"x\"}");
}

static void test_profile(void)
{
    jianlu_dl_msg_t m;
    parse("{\"c\":\"profile\",\"nickname\":\"小诺\",\"signature\":\"今天也要认真生活\"}", &m);
    assert(m.cmd == JIANLU_DL_CMD_PROFILE);
    assert(strcmp(m.nickname, "小诺") == 0);
    assert(strcmp(m.signature, "今天也要认真生活") == 0);
    // 字段缺失按空串(允许部分更新)
    parse("{\"c\":\"profile\",\"nickname\":\"AA\"}", &m);
    assert(strcmp(m.nickname, "AA") == 0 && m.signature[0] == '\0');
    parse("{\"c\":\"profile\",\"signature\":\"S\"}", &m);
    assert(m.nickname[0] == '\0' && strcmp(m.signature, "S") == 0);
    // 超长昵称:UTF-8 安全截断(逐字符走一遍,不允许在半个字符处截断)
    char longline[512];
    snprintf(longline, sizeof(longline), "{\"c\":\"profile\",\"nickname\":\"%s\"}",
             "超长昵称测试超长昵称测试超长昵称测试超长昵称测试超长昵称测试"
             "超长昵称测试超长昵称测试超长昵称测试超长昵称测试超长昵称测试");
    parse(longline, &m);
    assert(strlen(m.nickname) >= JIANLU_NICKNAME_LEN - 3);   // 截到容量附近
    size_t ci = 0;
    while (ci < strlen(m.nickname)) {
        unsigned char b = (unsigned char)m.nickname[ci];
        size_t clen = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2
                    : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
        assert(clen >= 1);
        assert(ci + clen <= strlen(m.nickname));   // 不得切半个字符
        ci += clen;
    }
}

static void test_img(void)
{
    jianlu_dl_msg_t m;
    parse("{\"c\":\"imgb\",\"kind\":\"avatar\",\"total\":12}", &m);
    assert(m.cmd == JIANLU_DL_CMD_IMGB);
    assert(strcmp(m.kind, "avatar") == 0 && m.total == 12);
    parse("{\"c\":\"imgb\",\"kind\":\"qrcode\",\"total\":1}", &m);
    assert(strcmp(m.kind, "qrcode") == 0);
    // 非法:缺 kind / 缺 total / total=0
    parse_bad("{\"c\":\"imgb\",\"total\":3}");
    parse_bad("{\"c\":\"imgb\",\"kind\":\"avatar\"}");
    parse_bad("{\"c\":\"imgb\",\"kind\":\"avatar\",\"total\":0}");

    // imgc:b64 载荷指向行内原文,可立即解码
    parse("{\"c\":\"imgc\",\"kind\":\"qrcode\",\"seq\":3,\"data\":\"QUJDRA==\"}", &m);
    assert(m.cmd == JIANLU_DL_CMD_IMGC);
    assert(strcmp(m.kind, "qrcode") == 0 && m.seq == 3);
    assert(m.data_b64 != NULL && m.data_b64_len == 8);
    uint8_t raw[16];
    size_t n = jianlu_dlink_b64_decode(raw, sizeof(raw), m.data_b64,
                                       m.data_b64_len, NULL);
    assert(n == 4 && memcmp(raw, "ABCD", 4) == 0);
    // 空 data → 非法
    parse_bad("{\"c\":\"imgc\",\"kind\":\"avatar\",\"seq\":0,\"data\":\"\"}");
    // 缺 data → 非法
    parse_bad("{\"c\":\"imgc\",\"kind\":\"avatar\",\"seq\":0}");
    // 二进制安全:载荷含任意 b64 字母表字符
    parse("{\"c\":\"imgc\",\"kind\":\"avatar\",\"seq\":0,\"data\":\"+/09\"}", &m);
    assert(m.data_b64_len == 4);
}

static void test_img_assembly_logic(void)
{
    // 分块重组的纯逻辑样板:seq 严格递增 + 末块判定(与 dlink.c 落地逻辑同式)
    int total = 3, last = -1;
    const char *chunks[] = {"AAEC", "AAEC", "AAEC"};
    for (int seq = 0; seq < total; seq++) {
        assert(seq == last + 1);   // 乱序即中止
        uint8_t raw[8];
        size_t n = jianlu_dlink_b64_decode(raw, sizeof(raw), chunks[seq], 4, NULL);
        assert(n == 3);
        last = seq;
    }
    assert(last + 1 == total);     // 收齐
}

int main(void) {
    test_basic_cmds();
    test_time();
    test_profile();
    test_img();
    test_img_assembly_logic();
    printf("test_jianlu_dlink_codec: all passed\n");
    return 0;
}
