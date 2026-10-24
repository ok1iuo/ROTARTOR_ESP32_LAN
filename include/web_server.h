#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_http_server.h>

// Starts the HTTP server (page at "/", WebSocket at "/ws") and the state broadcast task.
// Safe to call again on every IP_EVENT_ETH_GOT_IP: a running server is kept.
httpd_handle_t web_server_start(void);

// Stops the HTTP server.
void web_server_stop(void);

#endif // WEB_SERVER_H
