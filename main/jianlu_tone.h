// main/jianlu_tone.h —— 音效:ES8311 扬声器程序合成提示音(正弦波表,零音频文件)。
//
// 非阻塞:jianlu_tone_play 只入队,专用任务在小缓冲里逐块生成 PCM 并
// bsp_audio_write。与录音共用 16kHz/16bit/mono 格式(全双工 I2S,TX/RX 独立),
// 录音期间仍可播(应用层避免在录音中触发即可)。
#pragma once

typedef enum {
    JIANLU_TONE_REC_START = 0,  // 短促"滴"(按住说话开始)
    JIANLU_TONE_SUCCESS,        // 上扬两音阶(记下/上传成功)
    JIANLU_TONE_COMPLETE,       // 清脆单音"叮"(勾选完成)
    JIANLU_TONE_FAIL,           // 低沉提示音(失败)
} jianlu_tone_t;

// 首次播放时懒初始化音频与播放任务,故无需显式 init。
// 非阻塞;队列满时丢弃(音效可丢,不可卡)。
void jianlu_tone_play(jianlu_tone_t tone);
