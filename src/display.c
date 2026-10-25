#include "display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_st7796.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "display_ui.h"
#include "fw_version.h"
#include "rotator.h"

static const char *TAG = "DISPLAY";

static void current_ip(char *out, size_t size)
{
    esp_netif_ip_info_t info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    out[0] = '\0';
    if (netif && esp_netif_is_netif_up(netif) && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
        snprintf(out, size, IPSTR, IP2STR(&info.ip));
    }
}

static void display_task(void *arg)
{
    rotator_state_t st;
    char ip[16];
    while (true) {
        rotator_get_state(&st);
        current_ip(ip, sizeof(ip));
        if (lvgl_port_lock(0)) {
            display_ui_update(&st, ip, FW_VERSION);
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_PERIOD_MS));
    }
}

void display_start(void)
{
    const spi_bus_config_t bus = {
        .mosi_io_num = DISPLAY_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,   // display is write-only; IO12 is a strapping pin
        .sclk_io_num = DISPLAY_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = DISPLAY_UI_WIDTH * DISPLAY_DRAW_LINES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = DISPLAY_PIN_CS,
        .dc_gpio_num = DISPLAY_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = DISPLAY_SPI_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI_HOST, &io_cfg, &io));

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISPLAY_PIN_RST,
        .rgb_ele_order = DISPLAY_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLORS));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = DISPLAY_UI_WIDTH * DISPLAY_DRAW_LINES,
        .double_buffer = true,
        .hres = DISPLAY_UI_WIDTH,
        .vres = DISPLAY_UI_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = DISPLAY_SWAP_XY,
            .mirror_x = DISPLAY_MIRROR_X,
            .mirror_y = DISPLAY_MIRROR_Y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,   // SPI sends the high byte first
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "LVGL display init failed");
        return;
    }

    lvgl_port_lock(0);
    display_ui_create(lv_display_get_screen_active(disp));
    lvgl_port_unlock();

    xTaskCreate(display_task, "display", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "Display started (%dx%d, SPI %d Hz)", DISPLAY_UI_WIDTH, DISPLAY_UI_HEIGHT, DISPLAY_SPI_CLOCK_HZ);
}
