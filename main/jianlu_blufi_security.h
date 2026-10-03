// main/jianlu_blufi_security.h —— BLUFI 安全协商(DHM + AES-CFB128 + MD5),
// 移植自仓库 demo/blufi-provisioning 分支的 demo_blufi_security.c
// (其本身改编自 ESP-IDF 5.5 BLUFI 示例,Unlicense/CC0)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

void jianlu_blufi_negotiate(uint8_t *data, int len, uint8_t **output_data,
                            int *output_len, bool *need_free);
int jianlu_blufi_encrypt(uint8_t iv8, uint8_t *data, int len);
int jianlu_blufi_decrypt(uint8_t iv8, uint8_t *data, int len);
uint16_t jianlu_blufi_checksum(uint8_t iv8, uint8_t *data, int len);
int jianlu_blufi_security_init(void);
void jianlu_blufi_security_deinit(void);
