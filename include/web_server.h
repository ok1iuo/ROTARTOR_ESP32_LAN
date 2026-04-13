#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"    // Potřebné pro QueueHandle_t
#include <esp_http_server.h>   // Potřebné pro httpd_handle_t

// Externí deklarace fronty pro aktualizace úhlu (plní ji rs485_handler.cpp)
extern QueueHandle_t xAngleUpdateQueue; 

// Funkce pro odeslání úhlu klientům přes WebSocket.
void web_server_send_angle_update(int angle); 

// Funkce pro odeslání příkazu z webového klienta do RS485 fronty.
void web_server_send_rs485_command_from_client(const char *command_type, int value);

// FreeRTOS task pro monitorování fronty aktualizací úhlu a odesílání dat přes WebSocket.
void angle_update_websocket_task(void *pvParameters); 

// Funkce pro inicializaci a spuštění HTTP a WebSocket serverů.
httpd_handle_t web_server_start(void); 

// Funkce pro zastavení webového serveru.
void web_server_stop(void);

#endif // WEB_SERVER_H
