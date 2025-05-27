#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <esp_http_server.h>


// Struktura pro přenos dat azimutu a elevace
typedef struct {
    float azimuth;
    float elevation;
} sensor_data_t;

/**
 * @brief Inicializuje a spustí webový server a WebSocket server.
 *
 * @param data_queue Handle FreeRTOS fronty pro příjem dat senzoru (azimut, elevace).
 * @return esp_err_t ESP_OK při úspěchu, jinak chybový kód.
 */
static httpd_handle_t server;

 httpd_handle_t setup_websocket_server(void);


#endif // WEB_SERVER_H
