// main/jianlu_capture.h —— 语音采集固件侧:SPIFFS 暂存、录音任务、WAV 上传。
//
// 数据流:ES8311(I2S,16kHz/16bit/mono)→ 2KB 块 → SPIFFS /voicefs/rec.wav
// (先写 44 字节占位头,结束后回填长度)→ esp_http_client 分块 POST →
// 响应(transcript/reply)由调用方解析。全程只有一个录音任务和一个网络任务,
// 无并发文件访问。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "jianlu_json.h"

// 录音块大小(字节):每块约 64ms 音频,也是松开轮询的粒度。
#define JIANLU_REC_CHUNK_BYTES 2048

// 挂载 voicefs 分区(SPIFFS,format_if_mount_failed)。幂等。
esp_err_t jianlu_capture_init(void);

// 提前初始化音频(I2S DMA 缓冲):须在堆宽裕时调用一次(拿到 IP 后、
// 网络任务创建前),否则首次录音时堆水位可能已不够 I2S DMA 分配。
esp_err_t jianlu_capture_prepare_audio(void);

// 是否存在待补传的录音(上次上传失败保留的)。
bool jianlu_capture_pending_exists(void);

// 录音回调:返回 false 立即停止(用于松开检测/时长上限)。
// 在录音任务上下文调用,每次写入一块 PCM 后触发;pcm/len 为本块数据
// (可用来做电平分析,只读)。
typedef bool (*jianlu_rec_poll_t)(void *user, const uint8_t *pcm, size_t len);

// 阻塞式录音:写 WAV 占位头后循环 bsp_audio_read → 写文件,直到 poll 返回
// false 或达到 JIANLU_VOICE_MAX_BYTES,最后回填 WAV 头长度。
// 输出 pcm_bytes(不含头)。音频/文件错误返回非 ESP_OK。
esp_err_t jianlu_capture_record(jianlu_rec_poll_t poll, void *user, size_t *pcm_bytes);

// 删除当前录音(误触取消时用)。
void jianlu_capture_discard(void);

// 本地回放 pending.wav:跳过 WAV 头,分块送扬声器;*stop_flag 置位
// (任意键)或读完即停。在录音任务上下文调用(与录音互斥,应用保证)。
// played_bytes 输出已播 PCM 字节数(可为 NULL)。
esp_err_t jianlu_capture_play_pending(volatile bool *stop_flag, size_t *played_bytes);

// 把指定文件(录音或 pending)分块 POST 到 /api/device/capture,
// 响应解析进 result。成功 ESP_OK;网络/HTTP/解析失败返回错误,
// 失败时若 keep_on_fail 为 true,文件被保留为 pending(下次补传)。
esp_err_t jianlu_capture_upload(bool use_pending, jianlu_capture_result_t *result);

// 上传成功后调用:删除已上传的文件(录音或 pending)。
void jianlu_capture_delete(bool use_pending);
