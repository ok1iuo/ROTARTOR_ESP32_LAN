#include "web_server.h"

#include <stdlib.h>
#include <string.h>

#include <esp_http_server.h>
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fw_version.h"
#include "rotator.h"

static const char *TAG_WEB = "WEB_SERVER";

#define STATE_BROADCAST_PERIOD_MS 250   // page treats 3 s of silence as a lost link

// Global handle for the HTTP server
static httpd_handle_t server = NULL;
static TaskHandle_t broadcast_task = NULL;

// Page embedded from src/index.html (board_build.embed_txtfiles, NUL-terminated)
extern const char index_html_start[] asm("_binary_index_html_start");

// --- state message -------------------------------------------------------------

static cJSON *axis_to_json(const rotator_axis_state_t *a)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "present", a->present);
    cJSON_AddBoolToObject(o, "ok", a->ok);
    cJSON_AddNumberToObject(o, "deg", a->deg);
    if (a->has_target) {
        cJSON_AddNumberToObject(o, "target", a->target);
    } else {
        cJSON_AddNullToObject(o, "target");
    }
    cJSON_AddNumberToObject(o, "age_ms", a->age_ms);
    cJSON_AddStringToObject(o, "ver", a->ver);
    return o;
}

// Builds {"ver":..., "az":{...}, "el":{...}}; caller frees the string.
static char *build_state_json(void)
{
    rotator_state_t st;
    rotator_get_state(&st);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ver", FW_VERSION);
    cJSON_AddItemToObject(root, "az", axis_to_json(&st.axis[ROTATOR_AXIS_AZ]));
    cJSON_AddItemToObject(root, "el", axis_to_json(&st.axis[ROTATOR_AXIS_EL]));
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static void broadcast(const char *json)
{
    size_t fds = CONFIG_LWIP_MAX_SOCKETS;
    int client_fds[CONFIG_LWIP_MAX_SOCKETS];
    if (httpd_get_client_list(server, &fds, client_fds) != ESP_OK) {
        return;
    }
    httpd_ws_frame_t pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };
    for (size_t i = 0; i < fds; i++) {
        if (httpd_ws_get_fd_info(server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            esp_err_t err = httpd_ws_send_frame_async(server, client_fds[i], &pkt);
            if (err != ESP_OK) {
                ESP_LOGW(TAG_WEB, "WS send to client %d failed: %s", client_fds[i], esp_err_to_name(err));
            }
        }
    }
}

// Sends the full rotator state to every WebSocket client periodically.
static void state_broadcast_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        if (server) {
            char *json = build_state_json();
            if (json) {
                broadcast(json);
                free(json);
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STATE_BROADCAST_PERIOD_MS));
    }
}

// --- handlers ------------------------------------------------------------------

// HTTP GET handler for the root path ("/")
static esp_err_t http_server_uri_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, HTTPD_RESP_USE_STRLEN);
}

// Commands from the page: {"cmd":"az_goto","deg":N} and {"cmd":"az_stop"}
static void handle_client_command(const char *payload)
{
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) {
        ESP_LOGW(TAG_WEB, "Invalid JSON from client: %s", payload);
        return;
    }
    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    const cJSON *deg = cJSON_GetObjectItemCaseSensitive(root, "deg");
    if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "az_goto") == 0 && cJSON_IsNumber(deg)) {
        rotator_cmd_az_goto(deg->valueint);
    } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "az_stop") == 0) {
        rotator_cmd_az_stop();
    } else {
        ESP_LOGW(TAG_WEB, "Unknown command from client: %s", payload);
    }
    cJSON_Delete(root);
}

static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG_WEB, "WebSocket client %d connected", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    if (pkt.len == 0 || pkt.len > 256) {
        return ESP_OK;
    }
    char buf[257];
    pkt.payload = (uint8_t *)buf;
    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret != ESP_OK) {
        return ret;
    }
    buf[pkt.len] = '\0';
    if (pkt.type == HTTPD_WS_TYPE_TEXT) {
        handle_client_command(buf);
    }
    return ESP_OK;
}

// --- lifecycle -----------------------------------------------------------------

httpd_handle_t web_server_start(void)
{
    if (server) {
        return server;   // already running (IP was re-acquired)
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.stack_size = 8192;

    ESP_LOGI(TAG_WEB, "Starting HTTP server on port: '%d'", config.server_port);
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WEB, "Error starting server! %s", esp_err_to_name(ret));
        server = NULL;
        return NULL;
    }

    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = http_server_uri_handler,
    };
    httpd_register_uri_handler(server, &uri_get);

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_uri);

    if (broadcast_task == NULL &&
        xTaskCreate(state_broadcast_task, "ws_state", 4096, NULL, tskIDLE_PRIORITY + 4, &broadcast_task) != pdPASS) {
        ESP_LOGE(TAG_WEB, "Failed to create state broadcast task");
        broadcast_task = NULL;
    }
    return server;
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG_WEB, "HTTP server stopped.");
        server = NULL;
    }
}
