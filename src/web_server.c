#include "web_server.h"
#include "rs485_handler.h" // Include to access send_rs485_command and xRS485CommandQueue
#include "radio.h"

#include <esp_http_server.h> // Zde je definována httpd_req_to_sockfd() a další funkce WebSocket serveru
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h" // Keep if you use GPIOs here
#include "cJSON.h"       // For JSON parsing and creation

static const char *TAG_WEB = "WEB_SERVER";
static const char *TAG_SEND_TASK = "WS_SEND_TASK";     // Tag for the send task
static const char *TAG_WS_RECV = "WS_RECV_HANDLER"; // Tag for WebSocket receive handler

// Global handle for the HTTP server
static httpd_handle_t server = NULL; 

// HTML content with embedded JavaScript for the web client
const char *INDEX_HTML = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <title>Rotator Control</title>\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <meta charset=\"UTF-8\">\n"
"    <style>\n"
"        body {\n"
"            font-family: Arial, sans-serif;\n"
"            display: flex;\n"
"            justify-content: center;\n"
"            align-items: flex-start; /* Zarovnání nahoru */\n"
"            min-height: 100vh;\n"
"            background-color: #f0f0f0;\n"
"            margin: 20px; /* Přidáme okraj */\n"
"        }\n"
"        .container {\n"
"            display: flex;\n"
"            flex-direction: row; /* Prvky vedle sebe */\n"
"            align-items: flex-start; /* Zarovnání nahoru */\n"
"            gap: 40px; /* Mezera mezi šipkou a bargrafem */\n"
"            background-color: #fff;\n"
"            padding: 30px;\n"
"            border-radius: 10px;\n"
"            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);\n"
"        }\n"
"        .gauge-container {\n"
"            text-align: center;\n"
"        }\n"
"        .arrow-gauge {\n"
"            width: 300px;\n"
"            height: 300px;\n"
"            border: 2px solid #333;\n"
"            border-radius: 50%;\n"
"            position: relative;\n"
"            overflow: hidden;\n"
"            margin-bottom: 20px;\n"
"            cursor: pointer; /* Aby bylo klikatelné */\n"
"        }\n"
"        .arrow {\n"
"            position: absolute;\n"
"            top: 50%;\n"
"            left: 50%;\n"
"            width: 150px; /* Polovina šířky gauge pro šipku */\n"
"            height: 10px; /* Tloušťka šipky */\n"
"            background-color: red;\n"
"            transform-origin: 0% 50%; /* Rotace kolem středu kruhu */\n"
"            transform: translate(0, -50%) rotate(0deg);\n"
"            transition: transform 0.2s ease-out; /* Plynulá animace rotace */\n"
"        }\n"
"        .arrow::after {\n"
"            content: '';\n"
"            position: absolute;\n"
"            right: -10px; /* Konec šipky */\n"
"            top: 50%;\n"
"            transform: translateY(-50%);\n"
"            border-top: 10px solid transparent;\n"
"            border-bottom: 10px solid transparent;\n"
"            border-left: 20px solid red; /* Tvar hrotu šipky */\n"
"        }\n"
"        .center-dot {\n"
"             position: absolute;\n"
"             top: 50%;\n"
"             left: 50%;\n"
"             width: 20px;\n"
"             height: 20px;\n"
"             background-color: #333;\n"
"             border-radius: 50%;\n"
"             transform: translate(-50%, -50%);\n"
"             z-index: 1; /* Zajistí, že tečka bude nad šipkou */\n"
"        }\n"
"        /* Popisky stupnice rotátoru */\n"
"        .arrow-gauge div[style*=\"position: absolute;\"] {\n"
"            font-weight: bold;\n"
"            color: #555;\n"
"            font-size: 0.9em;\n"
"            pointer-events: none; /* Umožní kliknutí skrz na kružnici */\n"
"        }\n"
"\n"
"        .bargraph-container {\n"
"            text-align: center;\n"
"        }\n"
"        .bargraph {\n"
"            width: 80px; /* Šířka bargrafu */\n"
"            height: 400px; /* Výška bargrafu pro rozsah -10 až 90 (celkem 100 jednotek) */\n"
"            border: 2px solid #333;\n"
"            position: relative;\n"
"            margin: 0 auto; /* Centrování bargrafu */\n"
"            display: flex;\n"
"            flex-direction: column-reverse; /* Spodní část grafu je 0 */\n"
"            align-items: center;\n"
"        }\n"
"        .bar {\n"
"            width: 100%;\n"
"            background-color: steelblue;\n"
"            position: absolute;\n"
"            bottom: 0; /* Bar začíná odspodu */\n"
"            left: 0;\n"
"            transition: height 0.2s ease-out; /* Plynulá animace výšky */\n"
"        }\n"
"        .scale-label {\n"
"            position: absolute;\n"
"            left: -30px; /* Umístění popisků vlevo od bargrafu */\n"
"            text-align: right;\n"
"            width: 25px;\n"
"            font-size: 0.8em;\n"
"        }\n"
"        .scale-label.top { top: 0; }\n"
"        .scale-label.middle { top: 50%; transform: translateY(-50%); }\n"
"        .scale-label.bottom { bottom: 0; }\n"
"        .scale-label.zero { bottom: calc(10% - 0.4em); } /* 0 je na 10% výšky (pro rozsah -10 až 90) */\n"
"        .config-container {\n"
"            background-color: #fff;\n"
"            padding: 30px;\n"
"            border-radius: 10px;\n"
"            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);\n"
"            margin-left: 40px;\n"
"        }\n"
"        table { width: 100%; border-collapse: collapse; margin-top: 10px; }\n"
"        th, td { border: 1px solid #ddd; padding: 8px; text-align: center; }\n"
"        th { background-color: #f2f2f2; }\n"
"        input[type=number] { width: 60px; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <div class=\"gauge-container\">\n"
"            <h2>Azimut</h2>\n"
"            <div class=\"arrow-gauge\" id=\"azimuth-dial\">\n"
"                <div class=\"arrow\" id=\"azimuth-arrow\"></div>\n"
"                <div class=\"center-dot\"></div>\n"
"                <div style=\"position: absolute; top: 5px; left: 50%; transform: translateX(-50%);\">0°</div>\n"
"                <div style=\"position: absolute; bottom: 5px; left: 50%; transform: translateX(-50%);\">180°</div>\n"
"                <div style=\"position: absolute; top: 50%; left: 5px; transform: translateY(-50%);\">270°</div>\n"
"                <div style=\"position: absolute; top: 50%; right: 5px; transform: translateY(-50%);\">90°</div>\n"
"            </div>\n"
"            <p>Azimut: <span id=\"azimuth-value\">0.0</span>°</p>\n"
"        </div>\n"
"\n"
"        <div class=\"bargraph-container\">\n"
"            <h2>Elevace</h2>\n"
"            <div class=\"bargraph\" id=\"elevation-bargraph\">\n"
"                <div class=\"bar\" id=\"elevation-bar\"></div>\n"
"                <div class=\"scale-label top\">90</div>\n"
"                <div class=\"scale-label zero\">0</div>\n"
"                <div class=\"scale-label bottom\">-10</div>\n"
"            </div>\n"
"             <p>Elevace: <span id=\"elevation-value\">0.0</span>°</p>\n"
"        </div>\n"
"        <div class=\"config-container\">\n"
"            <h2>Konfigurace Sítě</h2>\n"
"            <div>\n"
"                <label>Cílové ID pro čas: <input type=\"number\" id=\"timeTarget\" min=\"0\" max=\"255\"></label>\n"
"                <button onclick=\"saveConfig()\">Uložit vše</button>\n"
"            </div>\n"
"            <h3>Routovací Tabulka</h3>\n"
"            <table>\n"
"                <thead><tr><th>Index</th><th>Cíl (Target)</th><th>Přes (Next Hop)</th></tr></thead>\n"
"                <tbody id=\"routeTableBody\">\n"
"                    <!-- Rows generated by JS -->\n"
"                </tbody>\n"
"            </table>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <script>\n"
"        var websocket; \n"
"        var azimuthArrow = document.getElementById('azimuth-arrow');\n"
"        var azimuthValue = document.getElementById('azimuth-value');\n"
"        var elevationBar = document.getElementById('elevation-bar');\n"
"        var elevationValue = document.getElementById('elevation-value');\n"
"        var azimuthDial = document.getElementById('azimuth-dial'); // Přidáno pro klikání\n"
"        var bargraphHeight = 400; // Výška bargrafu v pixelech\n"
"        var elevationRange = 100; // Rozsah elevace (-10 až 90)\n"
"\n"
"        function connectWebSocket() {\n"
"            var serverIp = window.location.host;\n"
"            var wsUrl = 'ws://' + serverIp + '/ws'; // Cesta k WebSocket endpointu\n"
"\n"
"            websocket = new WebSocket(wsUrl);\n"
"\n"
"            websocket.onopen = function(event) {\n"
"                console.log('WebSocket connection opened');\n"
"            };\n"
"\n"
"            websocket.onmessage = function(event) {\n"
"                console.log('WebSocket message received:', event.data);\n"
"                try {\n"
"                    var data = JSON.parse(event.data);\n"
"                    if (data.hasOwnProperty('angle')) { // Očekáváme 'angle' místo 'azimuth'\n"
"                        updateAzimuth(data.angle);\n"
"                    } else if (data.hasOwnProperty('elevation')) {\n"
"                        updateElevation(data.elevation); // Pokud by ESP32 posílal elevaci\n"
"                    }\n"
"                } catch (e) {\n"
"                    console.error(\"Failed to parse WebSocket message:\", e);\n"
"                }\n"
"            };\n"
"\n"
"            websocket.onerror = function(event) {\n"
"                console.error(\"WebSocket error observed:\", event);\n"
"            };\n"
"\n"
"            websocket.onclose = function(event) {\n"
"                console.log('WebSocket connection closed:', event.code, event.reason);\n"
"                setTimeout(connectWebSocket, 5000); // Pokusit se znovu připojit po chvíli\n"
"            };\n"
"        }\n"
"\n"
"        function updateAzimuth(azimuth) {\n"
"            var normalizedAzimuth = (azimuth % 360 + 360) % 360;\n"
"            // Rotace šipky. 0 stupňů je nahoru. CSS transform rotate(0deg) je vpravo.\n"
"            // Proto odečítáme 90, aby 0° azimutu směřovalo nahoru.\n"
"            azimuthArrow.style.transform = 'translate(0, -50%) rotate(' + (normalizedAzimuth - 90) + 'deg)';\n"
"            azimuthValue.textContent = normalizedAzimuth.toFixed(1); \n"
"        }\n"
"\n"
"        function updateElevation(elevation) {\n"
"            var clampedElevation = Math.max(-10, Math.min(90, elevation));\n"
"            var barHeight = ((clampedElevation + 10) / elevationRange) * bargraphHeight;\n"
"            elevationBar.style.height = barHeight + 'px';\n"
"            elevationValue.textContent = clampedElevation.toFixed(1); \n"
"        }\n"
"\n"
"        // --- Nová logika pro kliknutí na kružnici a odeslání příkazu F ---\n"
"        azimuthDial.addEventListener('click', function(event) {\n"
"            const rect = azimuthDial.getBoundingClientRect();\n"
"            const centerX = rect.left + rect.width / 2;\n"
"            const centerY = rect.top + rect.height / 2;\n"
"\n"
"            const clickX = event.clientX;\n"
"            const clickY = event.clientY;\n"
"\n"
"            let angleRad = Math.atan2(clickY - centerY, clickX - centerX);\n"
"            let angleDeg = angleRad * (180 / Math.PI); \n"
"\n"
"            // Převod na úhel od 0 do 360 stupňů, kde 0 je nahoře (sever) a otáčí se po směru hodinových ručiček\n"
"            angleDeg = (angleDeg + 90 + 360) % 360; \n"
"\n"
"            const angleToSet = Math.round(angleDeg); // Zaokrouhlíme na celé stupně\n"
"            console.log('Clicked angle:', angleToSet);\n"
"\n"
"            // Odešleme příkaz F XXX přes WebSocket\n"
"            if (websocket && websocket.readyState === WebSocket.OPEN) {\n"
"                // Používáme JSON.stringify pro odeslání objektu jako string\n"
"                websocket.send(JSON.stringify({ command: 'F', value: angleToSet }));\n"
"                console.log(`Sent F ${angleToSet} command via WebSocket.`);\n"
"            } else {\n"
"                console.error('WebSocket is not open. Cannot send command F.');\n"
"            }\n"
"        });\n"
"\n"
"        function loadConfig() {\n"
"            fetch('/api/config')\n"
"                .then(response => response.json())\n"
"                .then(data => {\n"
"                    document.getElementById('timeTarget').value = data.time_target;\n"
"                    const tbody = document.getElementById('routeTableBody');\n"
"                    tbody.innerHTML = '';\n"
"                    data.routes.forEach((route, index) => {\n"
"                        const tr = document.createElement('tr');\n"
"                        tr.innerHTML = `\n"
"                            <td>${index}</td>\n"
"                            <td><input type='number' id='target_${index}' value='${route.target}' min='0' max='255'></td>\n"
"                            <td><input type='number' id='next_${index}' value='${route.next}' min='0' max='255'></td>\n"
"                        `;\n"
"                        tbody.appendChild(tr);\n"
"                    });\n"
"                })\n"
"                .catch(err => console.error('Error loading config:', err));\n"
"        }\n"
"\n"
"        function saveConfig() {\n"
"            const timeTarget = parseInt(document.getElementById('timeTarget').value);\n"
"            const routes = [];\n"
"            const tbody = document.getElementById('routeTableBody');\n"
"            const rows = tbody.getElementsByTagName('tr');\n"
"            for (let i = 0; i < rows.length; i++) {\n"
"                const target = parseInt(document.getElementById(`target_${i}`).value);\n"
"                const next = parseInt(document.getElementById(`next_${i}`).value);\n"
"                routes.push({ target: target, next: next });\n"
"            }\n"
"\n"
"            const data = {\n"
"                time_target: timeTarget,\n"
"                routes: routes\n"
"            };\n"
"\n"
"            fetch('/api/config', {\n"
"                method: 'POST',\n"
"                headers: {\n"
"                    'Content-Type': 'application/json',\n"
"                },\n"
"                body: JSON.stringify(data),\n"
"            })\n"
"            .then(response => {\n"
"                if (response.ok) alert('Konfigurace uložena');\n"
"                else alert('Chyba při ukládání');\n"
"            })\n"
"            .catch(err => console.error('Error saving config:', err));\n"
"        }\n"
"\n"
"        // Spustit připojení k WebSocketu při načtení stránky\n"
"        window.onload = function() {\n"
"            connectWebSocket();\n"
"            loadConfig();\n"
"        };\n"
"\n"
"    </script>\n"
"</body>\n"
"</html>\n";

// --- Functions for communication between web server and RS485 handler ---

// This function is called by the WebSocket handler when a command is received from a client.
// It queues the command for the RS485 task.
void web_server_send_rs485_command_from_client(const char *command_type, int value) {
    if (strcmp(command_type, "F") == 0) {
        if (send_rs485_command("F", value)) {
            ESP_LOGI(TAG_WS_RECV, "RS485 'F' command (angle %d) successfully queued.", value);
        } else {
            ESP_LOGE(TAG_WS_RECV, "Failed to queue RS485 'F' command (angle %d).", value);
        }
    } else if (strcmp(command_type, "C") == 0) {
        // Explicitně předáváme 0 jako druhý parametr
        if (send_rs485_command("C", 0)) { 
            ESP_LOGI(TAG_WS_RECV, "RS485 'C' command successfully queued.");
        } else {
            ESP_LOGE(TAG_WS_RECV, "Failed to queue RS485 'C' command (value %d).", 0);
        }
    } else if (strcmp(command_type, "ST") == 0) {
        // Explicitně předáváme 0 jako druhý parametr
        if (send_rs485_command("ST", 0)) {
            ESP_LOGI(TAG_WS_RECV, "RS485 'ST' command successfully queued.");
        } else {
            ESP_LOGE(TAG_WS_RECV, "Failed to queue RS485 'ST' command (value %d).", 0);
        }
    } else {
        ESP_LOGW(TAG_WS_RECV, "Unknown command type from client: %s", command_type);
    }
}

// This function sends angle updates to all connected WebSocket clients.
// It's called by the `angle_update_websocket_task`.
void web_server_send_angle_update(int angle) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG_SEND_TASK, "Failed to create cJSON object for angle update.");
        return;
    }
    cJSON_AddNumberToObject(root, "angle", angle); // Klient očekává klíč 'angle'
    
    char *json_string = cJSON_PrintUnformatted(root); 
    if (json_string == NULL) {
        ESP_LOGE(TAG_SEND_TASK, "Failed to print cJSON string for angle update.");
        cJSON_Delete(root);
        return;
    }
    
    if (server) {
        size_t fds = CONFIG_LWIP_MAX_LISTENING_TCP; // Používáme přímo konstantu z menuconfig
        int *client_fds = (int *)malloc(fds * sizeof(int)); // Alokujeme dle fds
        if (!client_fds) {
            ESP_LOGE(TAG_SEND_TASK, "Failed to allocate memory for client_fds");
            cJSON_Delete(root);
            free(json_string);
            return;
        }

        esp_err_t ret_get_list = httpd_get_client_list(server, &fds, client_fds);
        // --- OPRAVENO: Kontroluje se správná proměnná 'ret_get_list' ---
        if (ret_get_list != ESP_OK) { 
            ESP_LOGE(TAG_SEND_TASK, "httpd_get_client_list failed: %s", esp_err_to_name(ret_get_list));
            free(client_fds);
            cJSON_Delete(root);
            free(json_string);
            return;
        }

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.payload = (uint8_t*)json_string; // Odesíláme čistý JSON
        ws_pkt.len = strlen(json_string);
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;    

        for (int i = 0; i < fds; i++) {
            int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
            if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
                // --- OPRAVENO: Používáme správnou asynchronní funkci ---
                esp_err_t err = httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt); 
                if (err != ESP_OK) {
                    ESP_LOGE(TAG_SEND_TASK, "Failed to send WS frame to client %d: %s", client_fds[i], esp_err_to_name(err));
                } else {
                    ESP_LOGI(TAG_SEND_TASK, "Sent angle update to client %d", client_fds[i]);
                }
            }
        }
        free(client_fds);
    } else {
        ESP_LOGE(TAG_SEND_TASK, "HTTP server handle is NULL, cannot send WebSocket message.");
    }
    
    cJSON_Delete(root);
    free(json_string); 
}

// FreeRTOS task to monitor the angle update queue and send data via WebSocket.
// This task replaces your previous `websocket_send_data_task`.
void angle_update_websocket_task(void *pvParameters) {
    int angle_to_send;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // 100 ms perioda pro příkaz 'C'
    xLastWakeTime = xTaskGetTickCount();

    ESP_LOGI(TAG_SEND_TASK, "Angle update WebSocket task started. Will periodically request 'C' command.");

    while (1) {
        // --- 1. Periodicky odesíláme příkaz 'C' do RS485 fronty ---
        // Tím zajistíme, že se rotátor pravidelně dotazuje na aktuální úhel.
        if (!send_rs485_command("C", 0)) { // Explicitně 0 jako druhý parametr
            ESP_LOGE(TAG_SEND_TASK, "Failed to queue periodic 'C' command.");
        }

        // --- 2. Zkusíme přijmout aktualizovaný úhel z fronty (z RS485 tasku) ---
        // Čekáme krátkou dobu, aby se uvolnil čas pro jiné tasky, pokud ve frontě nic není hned.
        // Dlouhé čekání není potřeba, protože příkaz C už byl odeslán.
        if (xQueueReceive(xAngleUpdateQueue, &angle_to_send, 0) == pdPASS) {
            ESP_LOGI(TAG_SEND_TASK, "Received new angle %d from update queue. Sending to WebSocket clients.", angle_to_send);
            web_server_send_angle_update(angle_to_send); // Odešleme úhel všem připojeným klientům
        }

        // Zpoždění pro udržení periody 100ms
        vTaskDelay( 300 / portTICK_PERIOD_MS);
    }
}

// Handler pro získání konfigurace (GET /api/config)
static esp_err_t api_config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "time_target", radio_get_time_sync_target());
    
    cJSON *routes = cJSON_CreateArray();
    for (int i = 0; i < ROUTE_TABLE_SIZE; i++) {
        uint8_t target, next;
        radio_get_route(i, &target, &next);
        cJSON *route = cJSON_CreateObject();
        cJSON_AddNumberToObject(route, "target", target);
        cJSON_AddNumberToObject(route, "next", next);
        cJSON_AddItemToArray(routes, route);
    }
    cJSON_AddItemToObject(root, "routes", routes);

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free((void*)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pro uložení konfigurace (POST /api/config)
static esp_err_t api_config_post_handler(httpd_req_t *req) {
    char buf[1024];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *time_target = cJSON_GetObjectItem(root, "time_target");
    if (cJSON_IsNumber(time_target)) {
        radio_set_time_sync_target((uint8_t)time_target->valueint);
    }

    cJSON *routes = cJSON_GetObjectItem(root, "routes");
    if (cJSON_IsArray(routes)) {
        int array_size = cJSON_GetArraySize(routes);
        for (int i = 0; i < array_size && i < ROUTE_TABLE_SIZE; i++) {
            cJSON *route = cJSON_GetArrayItem(routes, i);
            cJSON *target = cJSON_GetObjectItem(route, "target");
            cJSON *next = cJSON_GetObjectItem(route, "next");
            
            if (cJSON_IsNumber(target) && cJSON_IsNumber(next)) {
                radio_set_route(i, (uint8_t)target->valueint, (uint8_t)next->valueint);
            }
        }
    }

    cJSON_Delete(root);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


// HTTP GET handler for the root path ("/")
static esp_err_t http_server_uri_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG_WEB, "Serving index.html");
    httpd_resp_set_type(req, "text/html"); 
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN); 
}

/**
 * @brief WebSocket event handler
 * Tato funkce zpracovává události WebSocketu (připojení, data, zavření, chyba).
 * Byla rozšířena o parsování příchozích JSON zpráv pro příkazy F.
 */
static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // HTTP GET request for WebSocket handshake
        ESP_LOGI(TAG_WS_RECV, "WebSocket connection requested for client %d", httpd_req_to_sockfd(req));
        return ESP_OK; 
    }

    // Pokud se dostaneme sem, jedná se o WebSocket frame (data)
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT; 

    // Přijmeme délku dat
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0); 
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS_RECV, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        ws_pkt.payload = (uint8_t*)malloc(ws_pkt.len + 1);
        if (!ws_pkt.payload) {
            ESP_LOGE(TAG_WS_RECV, "Failed to allocate memory for WebSocket payload");
            return ESP_ERR_NO_MEM;
        }
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len); 
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_WS_RECV, "httpd_ws_recv_frame failed with %d", ret);
            free(ws_pkt.payload);
            return ret;
        }
        ws_pkt.payload[ws_pkt.len] = '\0'; // Null-terminace

        ESP_LOGI(TAG_WS_RECV, "Received WebSocket message from client %d: %s", 
                 httpd_req_to_sockfd(req), (char*)ws_pkt.payload);

        // --- Zpracování JSON zprávy z klienta ---
        // Klient posílá JSON přímo, ne Socket.IO obálku.
        cJSON *root = cJSON_Parse((char*)ws_pkt.payload); // Parsujeme přímo payload
        if (root != NULL) {
            cJSON *command_obj = cJSON_GetObjectItemCaseSensitive(root, "command");
            cJSON *value_obj = cJSON_GetObjectItemCaseSensitive(root, "value");

            if (cJSON_IsString(command_obj) && strcmp(command_obj->valuestring, "F") == 0 && cJSON_IsNumber(value_obj)) {
                int angle = value_obj->valueint;
                web_server_send_rs485_command_from_client("F", angle);
            } else {
                ESP_LOGW(TAG_WS_RECV, "Unknown or invalid command/JSON from client: %s", (char*)ws_pkt.payload);
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG_WS_RECV, "Failed to parse JSON from client message: '%s'", (char*)ws_pkt.payload);
        }
        free(ws_pkt.payload); 
    }
    return ESP_OK;
}

// Funkce pro inicializaci a spuštění webového serveru
httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10; 
    config.stack_size = 8192; 
    
    ESP_LOGI(TAG_WEB, "Starting HTTP server on port: '%d'", config.server_port);
    esp_err_t ret = httpd_start(&server, &config); 
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WEB, "Error starting server! %s", esp_err_to_name(ret));
        return NULL; 
    }

    // Registrace HTTP GET handleru pro kořenovou cestu
    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = http_server_uri_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_get);
    ESP_LOGI(TAG_WEB, "Registered URI handler for '/'");

    // Registrace WebSocket handleru
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true 
    };
    httpd_register_uri_handler(server, &ws_uri);
    ESP_LOGI(TAG_WEB, "Registered WebSocket handler for '/ws'");

    // Registrace API handlerů
    httpd_uri_t uri_api_config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = api_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_config_get);

    httpd_uri_t uri_api_config_post = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = api_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_config_post);
    ESP_LOGI(TAG_WEB, "Registered API handlers for '/api/config'");

    // Vytvoření tasku pro odesílání úhlových aktualizací a periodické volání příkazu 'C'.
    BaseType_t xReturned = xTaskCreate(angle_update_websocket_task, "angle_update_ws_task", 4096, NULL, tskIDLE_PRIORITY + 5, NULL);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG_WEB, "Failed to create Angle Update WebSocket task");
        httpd_stop(server);
        return NULL;
    }
    ESP_LOGI(TAG_WEB, "Angle Update WebSocket task created.");
     
    return server; 
}    

// Funkce pro zastavení webového serveru
void web_server_stop(void) {
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG_WEB, "HTTP server stopped.");
        server = NULL;
    }
}