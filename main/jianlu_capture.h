// main/jianlu_capture.h —— 语音采集固件侧:SPIFFS 多槽队列、录音、WAV 上传、回放。
//
// 队列模型(移位 FIFO,见 jianlu_voiceq.h):离线录音依次占槽 p1.wav..p4.wav,
// 满 4 槽(或分区将满)拒绝新录音,绝不静默覆盖;联网后从头(p1)逐条补传,
// 每条成功即删并下移。旧版单槽 pending.wav 开机自动迁移为 p1.wav。
// 数据流:ES8311(I2S,16kHz/16bit/mono)→ 2KB 块 → SPIFFS(先写 44 字节
// 占位头,结束回填)→ esp_http_client 分块 POST。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "jianlu_json.h"

// 录音块大小(字节):每块约 64ms 音频,也是松开轮询的粒度。
#define JIANLU_REC_CHUNK_BYTES 2048

// 挂载 voicefs 分区(SPIFFS,format_if_mount_failed),并迁移旧版 pending.wav。幂等。
esp_err_t jianlu_capture_init(void);

// 提前初始化音频(I2S DMA 缓冲):须在堆宽裕时调用一次(拿到 IP 后、
// 网络任务创建前),否则首次录音时堆水位可能已不够 I2S DMA 分配。
esp_err_t jianlu_capture_prepare_audio(void);

// ---- 队列状态 ----
int jianlu_capture_queue_count(void);    // 队列深度 0..4
bool jianlu_capture_queue_full(void);    // 满 4 槽或分区剩余不足(此时禁止再录)

// 录音回调:返回 false 立即停止(用于松开检测/时长上限)。
// 在录音任务上下文调用,每次写入一块 PCM 后触发;pcm/len 为本块数据
// (可用来做电平分析,只读)。
typedef bool (*jianlu_rec_poll_t)(void *user, const uint8_t *pcm, size_t len);

// 阻塞式录音到队尾新槽:写 WAV 占位头后循环 bsp_audio_read → 写文件,
// 直到 poll 返回 false 或达到 JIANLU_VOICE_MAX_BYTES,最后回填头长度。
// 输出 pcm_bytes(不含头)。音频/文件错误返回非 ESP_OK。
// 调用前应用须先查 jianlu_capture_queue_full()(满则应拒录)。
esp_err_t jianlu_capture_record(jianlu_rec_poll_t poll, void *user, size_t *pcm_bytes);

// 删除队尾刚录的槽(误触太短取消时用)。
void jianlu_capture_discard_tail(void);

// 上传队头(p1.wav)到 /api/device/capture,响应解析进 result。
// 结果语义(应用据此决定补传循环是否继续):
//   ESP_OK                  成功,文件已删并下移
//   ESP_ERR_INVALID_RESPONSE 中枢已应答的拒收(如静音 502),文件已删,可继续下一条
//   其他(传输层)            连不上中枢,文件保留,应停止本轮补传
esp_err_t jianlu_capture_upload_head(jianlu_capture_result_t *result);

// 本地回放指定槽(1..4):跳过 WAV 头,分块送扬声器;*stop_flag 置位
// (任意键)或读完即停。在录音任务上下文调用(与录音互斥,应用保证)。
esp_err_t jianlu_capture_play_slot(int slot, volatile bool *stop_flag,
                                   size_t *played_bytes);
