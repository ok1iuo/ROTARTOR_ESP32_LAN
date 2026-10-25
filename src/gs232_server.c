#include "gs232_server.h"

#include <stdarg.h>
#include <string.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <nvs.h>

#include "rotator.h"
#include "rotator_proto.h"

static const char *TAG = "GS232";

#define NVS_NAMESPACE   "rotator"
#define NVS_KEY_VARIANT "gs232"

static volatile gs232_variant_t s_variant = GS232_VARIANT_A;
static int s_tcp_clients = 0;   // accessed with __atomic builtins

// --- command execution (shared by all transports) ------------------------------

static void current_position(gs232_position_t *pos)
{
    rotator_state_t st;
    rotator_get_state(&st);
    pos->az_ok = st.axis[ROTATOR_AXIS_AZ].ok;
    pos->az = st.axis[ROTATOR_AXIS_AZ].deg;
    pos->el_installed = st.axis[ROTATOR_AXIS_EL].installed;
    pos->el_ok = st.axis[ROTATOR_AXIS_EL].ok;
    pos->el = st.axis[ROTATOR_AXIS_EL].deg;
}

static int clamp_el(int el)
{
    return el < ROTATOR_EL_MIN_DEG ? ROTATOR_EL_MIN_DEG : el > ROTATOR_EL_MAX_DEG ? ROTATOR_EL_MAX_DEG : el;
}

// Runs one command line and returns the reply length written to reply.
static size_t execute_line(const char *line, char *reply, size_t reply_size)
{
    gs232_position_t pos;
    gs232_action_t act;
    current_position(&pos);
    size_t len = gs232_handle(line, s_variant, &pos, &act, reply, reply_size);

    switch (act.type) {
    case GS232_ACT_AZ_GOTO:
        rotator_cmd_az_goto(act.az);
        break;
    case GS232_ACT_AZEL_GOTO:
        rotator_cmd_az_goto(act.az);
        rotator_cmd_el_goto(clamp_el(act.el));
        break;
    case GS232_ACT_EL_GOTO:
        rotator_cmd_el_goto(clamp_el(act.el));
        break;
    case GS232_ACT_STOP_ALL:
        rotator_cmd_az_stop();
        rotator_cmd_el_stop();
        break;
    case GS232_ACT_STOP_AZ:
        rotator_cmd_az_stop();
        break;
    case GS232_ACT_STOP_EL:
        rotator_cmd_el_stop();
        break;
    case GS232_ACT_NONE:
        break;
    }
    return len;
}

// --- TCP ----------------------------------------------------------------------

static void tcp_client_task(void *arg)
{
    int sock = (int)(intptr_t)arg;
    rotator_line_reader_t reader;
    rotator_line_reader_reset(&reader);
    char buf[64], line[96], reply[48];

    while (true) {
        int n = recv(sock, buf, sizeof buf, 0);
        if (n <= 0) {
            break;
        }
        for (int i = 0; i < n; i++) {
            if (rotator_line_reader_feed(&reader, buf[i], line, sizeof line)) {
                size_t len = execute_line(line, reply, sizeof reply);
                if (len > 0 && send(sock, reply, len, 0) < 0) {
                    goto done;
                }
            }
        }
    }
done:
    close(sock);
    int left = __atomic_sub_fetch(&s_tcp_clients, 1, __ATOMIC_SEQ_CST);
    ESP_LOGI(TAG, "TCP client disconnected (%d left)", left);
    vTaskDelete(NULL);
}

static void tcp_server_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(GS232_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(listen_sock, 2) != 0) {
        ESP_LOGE(TAG, "bind/listen on port %d failed: errno %d", GS232_TCP_PORT, errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "GS-232 TCP server on port %d", GS232_TCP_PORT);

    while (true) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof peer;
        int sock = accept(listen_sock, (struct sockaddr *)&peer, &peer_len);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (__atomic_load_n(&s_tcp_clients, __ATOMIC_SEQ_CST) >= GS232_TCP_MAX_CLIENTS) {
            ESP_LOGW(TAG, "Too many TCP clients, refusing");
            close(sock);
            continue;
        }
        int nodelay = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
        int count = __atomic_add_fetch(&s_tcp_clients, 1, __ATOMIC_SEQ_CST);
        ESP_LOGI(TAG, "TCP client connected (%d)", count);
        if (xTaskCreate(tcp_client_task, "gs232_tcp", 3072, (void *)(intptr_t)sock, 4, NULL) != pdPASS) {
            close(sock);
            __atomic_sub_fetch(&s_tcp_clients, 1, __ATOMIC_SEQ_CST);
        }
    }
}

// --- USB serial (UART0) -------------------------------------------------------

static int log_discard(const char *fmt, va_list args)
{
    return 0;
}

static void usb_task(void *arg)
{
    // Keep the boot log (IP address etc.) readable on USB for a moment, then take over UART0.
    vTaskDelay(pdMS_TO_TICKS(GS232_USB_TAKEOVER_DELAY_MS));
    ESP_LOGI(TAG, "USB serial switches to GS-232 at %d Bd, logging on USB ends here", GS232_USB_BAUD_RATE);
    vTaskDelay(pdMS_TO_TICKS(50));   // let the message leave the UART
    esp_log_set_vprintf(log_discard);
    if (!uart_is_driver_installed(UART_NUM_0)) {
        uart_driver_install(UART_NUM_0, 256, 256, 0, NULL, 0);
    }
    uart_set_baudrate(UART_NUM_0, GS232_USB_BAUD_RATE);
    uart_flush_input(UART_NUM_0);

    rotator_line_reader_t reader;
    rotator_line_reader_reset(&reader);
    char line[96], reply[48];
    while (true) {
        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) == 1 &&
            rotator_line_reader_feed(&reader, (char)ch, line, sizeof line)) {
            size_t len = execute_line(line, reply, sizeof reply);
            if (len > 0) {
                uart_write_bytes(UART_NUM_0, reply, len);
            }
        }
    }
}

// --- settings -----------------------------------------------------------------

static gs232_variant_t load_variant(void)
{
    uint8_t value = GS232_VARIANT_A;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_VARIANT, &value);
        nvs_close(h);
    }
    return value == GS232_VARIANT_B ? GS232_VARIANT_B : GS232_VARIANT_A;
}

gs232_variant_t gs232_server_get_variant(void)
{
    return s_variant;
}

bool gs232_server_set_variant(gs232_variant_t variant)
{
    s_variant = variant;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, NVS_KEY_VARIANT, (uint8_t)variant);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Protocol set to GS-232%c%s", variant == GS232_VARIANT_B ? 'B' : 'A', err == ESP_OK ? "" : " (not saved)");
    return err == ESP_OK;
}

void gs232_server_start(void)
{
    s_variant = load_variant();
    ESP_LOGI(TAG, "Protocol GS-232%c", s_variant == GS232_VARIANT_B ? 'B' : 'A');
    xTaskCreate(tcp_server_task, "gs232_srv", 3072, NULL, 4, NULL);
#if GS232_USB_ENABLED
    xTaskCreate(usb_task, "gs232_usb", 3072, NULL, 4, NULL);
#endif
}
