// main/main.c —— 小诺简录:在 AI Passport 卡片上查看/勾选中枢简录清单。
//
// 交互:UP/DOWN 移动选中,OK 短按标记完成,OK 长按刷新。
// 本应用为衍生应用,UI 为自设计(见 jianlu_ui.c),不经过基线测试菜单。
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"      // 错误日志里要打印 BSP_LCD_* 引脚号
#include "esp_log.h"
#include "jianlu_app.h"
#include "jianlu_ui.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "小诺简录启动");

    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,应用无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // Wi-Fi 固件要求 NVS 就绪;nvs_flash_init 幂等。
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(nvs_err));
        return;
    }

    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计未应答,电量显示将不可用");
    }

    if (bsp_lvgl_lock(1000)) {
        jianlu_ui_create();
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "LVGL 锁超时,界面未创建");
        return;
    }

    jianlu_app_start();
    ESP_LOGI(TAG, "小诺简录就绪");
}
