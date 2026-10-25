#include "rotator.h"

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <nvs.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "rotator_proto.h"

static const char *TAG = "ROTATOR";

#define ANSWER_TIMEOUT_MS   100   // units answer within a few ms; leave margin for I2C reads
#define STATUS_TIMEOUT_MS   400   // multi-line ST/ST2 answer

#define NVS_NAMESPACE        "rotator"
#define NVS_KEY_EL_INSTALLED "el_inst"

typedef enum {
    CMD_AZ_GOTO,
    CMD_AZ_STOP,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    int deg;
} cmd_t;

typedef struct {
    const char *get_cmd;     // angle query
    const char *goto_cmd;    // goto command prefix
    const char *status_cmd;  // status query (contains "ver:")
    int min_deg;
    int max_deg;
} axis_proto_t;

static const axis_proto_t AXIS_PROTO[ROTATOR_AXIS_COUNT] = {
    [ROTATOR_AXIS_AZ] = { "C",  "F",  "ST",  0,   360 },
    [ROTATOR_AXIS_EL] = { "C2", "F2", "ST2", -90, 180 },
};

typedef struct {
    bool installed;
    bool answered;
    int deg;
    bool has_target;
    int target;
    int64_t last_ok_us;
    bool ver_tried;
    char ver[24];
} axis_runtime_t;

static axis_runtime_t s_axis[ROTATOR_AXIS_COUNT];
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_cmd_queue;

// --- bus helpers (called only from the bus task) ---

static void bus_send(const char *cmd, int value, bool with_value)
{
    char buf[24];
    // Units terminate commands with CR only; an extra LF would stick to the next command.
    int n = with_value ? snprintf(buf, sizeof buf, "%s %d\r", cmd, value)
                       : snprintf(buf, sizeof buf, "%s\r", cmd);
    uart_flush_input(RS485_UART_PORT);   // drop leftovers of earlier answers
    uart_write_bytes(RS485_UART_PORT, buf, n);
    uart_wait_tx_done(RS485_UART_PORT, pdMS_TO_TICKS(20));
}

// Reads lines until accept() takes one or the timeout expires.
typedef bool (*line_accept_fn)(const char *line, void *ctx);

static bool bus_read_until(int timeout_ms, line_accept_fn accept, void *ctx)
{
    rotator_line_reader_t reader;
    rotator_line_reader_reset(&reader);
    char line[96];
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        uint8_t ch;
        if (uart_read_bytes(RS485_UART_PORT, &ch, 1, pdMS_TO_TICKS(5)) == 1 &&
            rotator_line_reader_feed(&reader, (char)ch, line, sizeof line) &&
            accept(line, ctx)) {
            return true;
        }
    }
    return false;
}

typedef struct {
    const axis_proto_t *proto;
    int deg;
} angle_ctx_t;

static bool accept_angle(const char *line, void *ctx)
{
    angle_ctx_t *a = ctx;
    return rotator_parse_angle(line, a->proto->min_deg, a->proto->max_deg, &a->deg);
}

static bool accept_version(const char *line, void *ctx)
{
    char *ver = ctx;
    return rotator_parse_version(line, ver, 24);
}

static bool accept_nothing(const char *line, void *ctx)
{
    return false;
}

static void poll_angle(rotator_axis_t axis)
{
    angle_ctx_t ctx = { .proto = &AXIS_PROTO[axis] };
    bus_send(ctx.proto->get_cmd, 0, false);
    if (!bus_read_until(ANSWER_TIMEOUT_MS, accept_angle, &ctx)) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    axis_runtime_t *a = &s_axis[axis];
    if (!a->answered) {
        ESP_LOGI(TAG, "Unit %s answered, angle %d", ctx.proto->get_cmd, ctx.deg);
    }
    a->answered = true;
    a->deg = ctx.deg;
    a->last_ok_us = esp_timer_get_time();
    if (a->has_target && rotator_target_reached(a->deg, a->target)) {
        a->has_target = false;
    }
    xSemaphoreGive(s_lock);
}

static void query_version(rotator_axis_t axis)
{
    char ver[24] = "";
    bus_send(AXIS_PROTO[axis].status_cmd, 0, false);
    bool found = bus_read_until(STATUS_TIMEOUT_MS, accept_version, ver);
    bus_read_until(50, accept_nothing, NULL);   // let the rest of the status text pass
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_axis[axis].ver_tried = true;
    if (found) {
        strcpy(s_axis[axis].ver, ver);
        ESP_LOGI(TAG, "Unit %s firmware %s", AXIS_PROTO[axis].get_cmd, ver);
    }
    xSemaphoreGive(s_lock);
}

static void send_goto(rotator_axis_t axis, int deg)
{
    bus_send(AXIS_PROTO[axis].goto_cmd, deg, true);
    bus_read_until(ANSWER_TIMEOUT_MS, accept_nothing, NULL);   // consume the "new ... =" confirmation
}

static void handle_cmd(const cmd_t *cmd)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    axis_runtime_t *az = &s_axis[ROTATOR_AXIS_AZ];
    int deg = cmd->type == CMD_AZ_STOP ? az->deg : cmd->deg;
    az->has_target = cmd->type == CMD_AZ_GOTO;
    az->target = deg;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%s azimuth -> %d", cmd->type == CMD_AZ_STOP ? "Stop" : "Goto", deg);
    send_goto(ROTATOR_AXIS_AZ, deg);
}

static void bus_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        cmd_t cmd;
        while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdPASS) {
            handle_cmd(&cmd);
        }

        poll_angle(ROTATOR_AXIS_AZ);
        if (s_axis[ROTATOR_AXIS_EL].installed) {
            poll_angle(ROTATOR_AXIS_EL);
        }

        for (int i = 0; i < ROTATOR_AXIS_COUNT; i++) {
            if (s_axis[i].installed && s_axis[i].answered && !s_axis[i].ver_tried &&
                esp_timer_get_time() - s_axis[i].last_ok_us < ROTATOR_OFFLINE_AFTER_MS * 1000LL) {
                query_version((rotator_axis_t)i);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ROTATOR_POLL_PERIOD_MS));
    }
}

// --- settings ---

static bool load_el_installed(void)
{
    uint8_t value = 1;   // default: installed, so a dead unit shows up as a fault
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_EL_INSTALLED, &value);
        nvs_close(h);
    }
    return value != 0;
}

static bool save_el_installed(bool installed)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, NVS_KEY_EL_INSTALLED, installed ? 1 : 0);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving EL setting failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

// --- public API ---

void rotator_start(void)
{
    uart_config_t uart_config = {
        .baud_rate = RS485_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_driver_install(RS485_UART_PORT, 512, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(RS485_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(RS485_UART_PORT, RS485_TX_PIN, RS485_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_mode(RS485_UART_PORT, UART_MODE_RS485_HALF_DUPLEX));

    s_lock = xSemaphoreCreateMutex();
    s_cmd_queue = xQueueCreate(8, sizeof(cmd_t));
    configASSERT(s_lock && s_cmd_queue);

    s_axis[ROTATOR_AXIS_AZ].installed = true;
    s_axis[ROTATOR_AXIS_EL].installed = load_el_installed();
    ESP_LOGI(TAG, "Elevation unit %s", s_axis[ROTATOR_AXIS_EL].installed ? "installed" : "not installed");

    xTaskCreate(bus_task, "rotator_bus", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "RS485 on UART%d at %d Bd, polling every %d ms", RS485_UART_PORT, RS485_BAUD_RATE, ROTATOR_POLL_PERIOD_MS);
}

void rotator_get_state(rotator_state_t *out)
{
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < ROTATOR_AXIS_COUNT; i++) {
        const axis_runtime_t *a = &s_axis[i];
        rotator_axis_state_t *o = &out->axis[i];
        o->installed = a->installed;
        o->answered = a->answered;
        o->age_ms = a->answered ? (uint32_t)((now - a->last_ok_us) / 1000) : 0;
        o->ok = a->installed && a->answered && o->age_ms < ROTATOR_OFFLINE_AFTER_MS;
        o->deg = a->deg;
        o->has_target = a->has_target;
        o->target = a->target;
        strcpy(o->ver, a->ver);
    }
    xSemaphoreGive(s_lock);
}

static bool enqueue(cmd_type_t type, int deg)
{
    cmd_t cmd = { .type = type, .deg = deg };
    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdPASS) {
        ESP_LOGE(TAG, "Command queue full");
        return false;
    }
    return true;
}

bool rotator_cmd_az_goto(int deg)
{
    if (deg < 0 || deg > 359) {
        return false;
    }
    return enqueue(CMD_AZ_GOTO, deg);
}

bool rotator_cmd_az_stop(void)
{
    return enqueue(CMD_AZ_STOP, 0);
}

bool rotator_set_el_installed(bool installed)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_axis[ROTATOR_AXIS_EL].installed = installed;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Elevation unit set to %s", installed ? "installed" : "not installed");
    return save_el_installed(installed);
}
