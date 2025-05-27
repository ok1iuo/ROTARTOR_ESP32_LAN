#include "web_server.h"

#include <esp_http_server.h>
//#include "esp_websocket_server.h" // Zahrnutí hlavičkového souboru pro WebSocket server
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h" // Pro případné ovládání GPIO pro indikaci stavu serveru
#include "cJSON.h" // Pro práci s JSON daty
#include "web_server.h"

static const char *TAG_WEB = "WEB_SERVER";
static const char *TAG_SEND = "WEBSOCKET SEND";


static QueueHandle_t sensor_data_queue = NULL;

// HTML obsah webové stránky
// Tato stránka obsahuje SVG pro šipku a bargraf a JavaScript pro WebSocket komunikaci a aktualizaci.
const char *INDEX_HTML = R"raw(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Senzor Data</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta charset="UTF-8">
    <style>
        body {
            font-family: Arial, sans-serif;
            display: flex;
            justify-content: center;
            align-items: flex-start; /* Zarovnání nahoru */
            min-height: 100vh;
            background-color: #f0f0f0;
            margin: 20px; /* Přidáme okraj */
        }
        .container {
            display: flex;
            flex-direction: row; /* Prvky vedle sebe */
            align-items: flex-start; /* Zarovnání nahoru */
            gap: 40px; /* Mezera mezi šipkou a bargrafem */
            background-color: #fff;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
        }
        .gauge-container {
            text-align: center;
        }
        .arrow-gauge {
            width: 300px;
            height: 300px;
            border: 2px solid #333;
            border-radius: 50%;
            position: relative;
            overflow: hidden;
            margin-bottom: 20px;
        }
        .arrow {
            position: absolute;
            top: 50%;
            left: 50%;
            width: 150px; /* Polovina šířky gauge pro šipku */
            height: 10px; /* Tloušťka šipky */
            background-color: red;
            transform-origin: 0% 50%; /* Rotace kolem středu kruhu */
            transform: translate(0, -50%) rotate(0deg); /* Počáteční pozice a rotace */
            transition: transform 0.2s ease-out; /* Plynulá animace rotace */
        }
        .arrow::after {
            content: '';
            position: absolute;
            right: -10px; /* Konec šipky */
            top: 50%;
            transform: translateY(-50%);
            border-top: 10px solid transparent;
            border-bottom: 10px solid transparent;
            border-left: 20px solid red; /* Tvar hrotu šipky */
        }
        .center-dot {
             position: absolute;
             top: 50%;
             left: 50%;
             width: 20px;
             height: 20px;
             background-color: #333;
             border-radius: 50%;
             transform: translate(-50%, -50%);
             z-index: 1; /* Zajistí, že tečka bude nad šipkou */
        }

        .bargraph-container {
            text-align: center;
        }
        .bargraph {
            width: 80px; /* Šířka bargrafu */
            height: 400px; /* Výška bargrafu pro rozsah -10 až 90 (celkem 100 jednotek) */
            border: 2px solid #333;
            position: relative;
            margin: 0 auto; /* Centrování bargrafu */
            display: flex;
            flex-direction: column-reverse; /* Spodní část grafu je 0 */
            align-items: center;
        }
        .bar {
            width: 100%;
            background-color: steelblue;
            position: absolute;
            bottom: 0; /* Bar začíná odspodu */
            left: 0;
            transition: height 0.2s ease-out; /* Plynulá animace výšky */
        }
        .scale-label {
            position: absolute;
            left: -30px; /* Umístění popisků vlevo od bargrafu */
            text-align: right;
            width: 25px;
            font-size: 0.8em;
        }
        .scale-label.top { top: 0; }
        .scale-label.middle { top: 50%; transform: translateY(-50%); }
        .scale-label.bottom { bottom: 0; }
        .scale-label.zero { bottom: calc(10% - 0.4em); } /* 0 je na 10% výšky (pro rozsah -10 až 90) */

    </style>
</head>
<body>
    <div class="container">
        <div class="gauge-container">
            <h2>Azimut</h2>
            <div class="arrow-gauge">
                <div class="arrow" id="azimuth-arrow"></div>
                <div class="center-dot"></div>
                <div style="position: absolute; top: 5px; left: 50%; transform: translateX(-50%);">0°</div>
                 <div style="position: absolute; bottom: 5px; left: 50%; transform: translateX(-50%);">180°</div>
                 <div style="position: absolute; top: 50%; left: 5px; transform: translateY(-50%);">270°</div>
                 <div style="position: absolute; top: 50%; right: 5px; transform: translateY(-50%);">90°</div>
            </div>
            <p>Azimut: <span id="azimuth-value">0.0</span>°</p>
        </div>

        <div class="bargraph-container">
            <h2>Elevace</h2>
            <div class="bargraph" id="elevation-bargraph">
                <div class="bar" id="elevation-bar"></div>
                <div class="scale-label top">90</div>
                <div class="scale-label zero">0</div>
                <div class="scale-label bottom">-10</div>
            </div>
             <p>Elevace: <span id="elevation-value">0.0</span>°</p>
        </div>
    </div>

    <script>
        var websocket;
        var azimuthArrow = document.getElementById('azimuth-arrow');
        var azimuthValue = document.getElementById('azimuth-value');
        var elevationBar = document.getElementById('elevation-bar');
        var elevationValue = document.getElementById('elevation-value');
        var bargraphHeight = 400; // Výška bargrafu v pixelech
        var elevationRange = 100; // Rozsah elevace (-10 až 90)

        function connectWebSocket() {
            // Získání IP adresy serveru z URL stránky
            var serverIp = window.location.hostname;
            // Použití portu 80 pro HTTP a WebSockets
            var wsUrl = 'ws://' + serverIp + '/ws'; // Cesta k WebSocket endpointu

            websocket = new WebSocket(wsUrl);

            websocket.onopen = function(event) {
                console.log('WebSocket connection opened');
            };

            websocket.onmessage = function(event) {
                console.log('WebSocket message received:', event.data);
                try {
                    var data = JSON.parse(event.data);
                    if (data.hasOwnProperty('azimuth')) {
                        updateAzimuth(data.azimuth);
                    }
                    if (data.hasOwnProperty('elevation')) {
                        updateElevation(data.elevation);
                    }
                } catch (e) {
                    console.error("Failed to parse WebSocket message:", e);
                }
            };

            websocket.onerror = function(event) {
                console.error("WebSocket error observed:", event);
            };

            websocket.onclose = function(event) {
                console.log('WebSocket connection closed:', event.code, event.reason);
                // Pokusit se znovu připojit po chvíli
                setTimeout(connectWebSocket, 5000);
            };
        }

        function updateAzimuth(azimuth) {
            // Zajistí, že azimut je v rozsahu 0-360
            var normalizedAzimuth = (azimuth % 360 + 360) % 360;
            // Rotace šipky. 0 stupňů je nahoru, rotace po směru hodinových ručiček.
            // CSS transform rotate(0deg) je vpravo. Potřebujeme posun o -90 stupňů,
            // aby 0° azimutu směřovalo nahoru.
            azimuthArrow.style.transform = 'translate(0, -50%) rotate(' + (normalizedAzimuth - 90) + 'deg)';
            azimuthValue.textContent = normalizedAzimuth.toFixed(1); // Zobrazení hodnoty s jednou desetinnou tečkou
        }

        function updateElevation(elevation) {
            // Omezení elevace na rozsah -10 až 90
            var clampedElevation = Math.max(-10, Math.min(90, elevation));
            // Přepočet hodnoty elevace na výšku baru v pixelech
            // Rozsah -10 až 90 je 100 jednotek.
            // 0 elevace by měla být na 10% výšky bargrafu odspodu (100px pro 400px výšku).
            // Hodnota -10 odpovídá 0px výšky baru.
            // Hodnota 90 odpovídá 400px výšky baru.
            var barHeight = ((clampedElevation + 10) / elevationRange) * bargraphHeight;
            elevationBar.style.height = barHeight + 'px';
            elevationValue.textContent = clampedElevation.toFixed(1); // Zobrazení hodnoty s jednou desetinnou tečkou
        }

        // Spustit připojení k WebSocketu při načtení stránky
        window.onload = connectWebSocket;

    </script>
</body>
</html>
)raw";

/**
 * @brief HTTP GET handler for the root path ("/")
 * Tato funkce obsluhuje požadavky na hlavní stránku a vrací HTML obsah.
 */
static esp_err_t http_server_uri_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG_WEB, "Serving index.html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief WebSocket event handler
 * Tato funkce zpracovává události WebSocketu (připojení, data, zavření, chyba).
 */
static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG_WEB, "WebSocket connection requested");
        return ESP_OK; // Přijmout WebSocket handshake
    }



    // Zpracování přijatých dat (pokud nějaká přijdou)
    // V tomto příkladu neočekáváme data od klienta, pouze posíláme aktualizace.
    // Pokud byste chtěli přijímat data, museli byste je zde zpracovat.

    return ESP_OK;
}

void websocket_send_data_task( void * pvParameters ){
    sensor_data_t data; 
    data.azimuth=0;
    data.elevation=45;
    while(1){
        ESP_LOGI(TAG_SEND, "Attempting to send 'ahoj' to all connected clients.");
        // Vytvoření JSON objektu
            if (data.azimuth>=360){
                data.azimuth=0;
            }else {
                data.azimuth+=5;
            }
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "azimuth", data.azimuth);
            cJSON_AddNumberToObject(root, "elevation", data.elevation);
            char *json_string = cJSON_PrintUnformatted(root);
            httpd_ws_frame_t ws_pkt;
            memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
            ws_pkt.len = strlen(json_string);
            ws_pkt.payload = (uint8_t *)json_string;
            ws_pkt.type = HTTPD_WS_TYPE_TEXT;    
            static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
           size_t fds = max_clients;
            int client_fds[max_clients];

            esp_err_t ret = httpd_get_client_list(server, &fds, client_fds);

            ESP_LOGI(TAG_SEND, "Attempting to send 'MSG' to   connected clients. %d",fds);

            for (int i = 0; i < fds; i++) {
                int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
                if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
                    httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt);
                }
            }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Krátké zpoždění, aby se neblokovalo CPU

    }


}


httpd_handle_t setup_websocket_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10; // Zvýšení počtu handlerů, pokud je potřeba více stránek/endpointů
    config.stack_size = 8192; // Zvýšení velikosti stacku pro HTTP server tasku

    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = http_server_uri_handler,
        .user_ctx = NULL};

    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true};

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &ws);
    }


    // Vytvoření tasky pro odesílání dat přes WebSocket
    BaseType_t xReturned = xTaskCreate(websocket_send_data_task, "ws_send_data", 4096, NULL, tskIDLE_PRIORITY + 5, NULL);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG_WEB, "Failed to create WebSocket send data task");
    }
     ESP_LOGI(TAG_WEB, "WebSocket send data task created");
     
     return server;

}    
