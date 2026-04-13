#include "rs485_handler.h"
#include "web_server.h" // For access to the xAngleUpdateQueue (declared in web_server.h)
#include <string.h>
#include <stdio.h>
#include <esp_log.h>

static const char *TAG = "RS485_HANDLER";

// Define and create the FreeRTOS queues here
QueueHandle_t xRS485CommandQueue;
QueueHandle_t xAngleUpdateQueue; 

// Static variable to hold the current angle received from the rotator
static int current_angle = 0;

void rs485_init() {
    // UART configuration structure
    uart_config_t uart_config = {
        .baud_rate = RS485_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    // Install UART driver (RX buffer size 256, no TX buffer, no event queue)
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, 256, 0, 0, NULL, 0));
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    // Set UART pins, including the DE pin for RS485 half-duplex
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // Set UART mode to RS485 half-duplex
    ESP_ERROR_CHECK(uart_set_mode(UART_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX));

    // Create the queue for incoming RS485 commands
    xRS485CommandQueue = xQueueCreate(RS485_COMMAND_QUEUE_SIZE, sizeof(rs485_command_t));
    if (xRS485CommandQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create RS485 command queue");
        // Handle error: perhaps restart or indicate failure
    }

    // Create the queue for angle updates to be sent to the WebSocket clients
    xAngleUpdateQueue = xQueueCreate(5, sizeof(int)); // Small queue for angle integers
    if (xAngleUpdateQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create Angle Update queue");
        // Handle error
    }

    ESP_LOGI(TAG, "RS485 initialized on UART%d  Baudrate %d", 
             UART_PORT_NUM, RS485_BAUD_RATE);
}

void rs485_task(void *pvParameters) {
    rs485_command_t received_command;
    char rx_buffer[64]; // Buffer for receiving responses from rotator
    int len;

    ESP_LOGI(TAG, "RS485 task started. Waiting for commands...");

    while (1) {
        // Wait indefinitely for a command to arrive in the queue
        if (xQueueReceive(xRS485CommandQueue, &received_command, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Received command from queue: %s, value: %d", received_command.command, received_command.value);

            // Process the received command
            if (strcmp(received_command.command, "C") == 0) {
                // Send 'C' command to the rotator to request current angle
                uart_write_bytes(UART_PORT_NUM, "C  \n\r", strlen("C  \n\r"));
                ESP_LOGI(TAG, "Sent 'C' command to RS485");

                // Wait for a response from the rotator (20ms timeout)
                // Adjust timeout based on your rotator's response time
                len = uart_read_bytes(UART_PORT_NUM, (uint8_t*)rx_buffer, sizeof(rx_buffer) - 1, 30 / portTICK_PERIOD_MS);
                if (len > 0) {
                    rx_buffer[len] = '\0'; // Null-terminate the received string
                    ESP_LOGI(TAG, "Received RS485 response: '%s'", rx_buffer);

                    // Attempt to parse the angle from the response (e.g., "+123", "-045")
                    int new_angle;
                    // sscanf handles leading '+' or '-' and parses as integer
                    if (sscanf(rx_buffer, "%d", &new_angle) == 1) {
                        if (new_angle != current_angle) {
                            current_angle = new_angle;
                            ESP_LOGI(TAG, "Angle changed to: %d. Pushing to update queue.", current_angle);
                            // Send the new angle to the WebSocket update queue.
                            // Do not block if queue is full (timeout 0).
                            if (xQueueSend(xAngleUpdateQueue, &current_angle, 0) != pdPASS) {
                                ESP_LOGE(TAG, "Failed to send angle to update queue (queue full?)");
                            }
                        } else {
                            ESP_LOGI(TAG, "Angle unchanged: %d", current_angle);
                        }
                    } else {
                        ESP_LOGW(TAG, "Failed to parse angle from RS485 response: '%s'", rx_buffer);
                    }
                } else {
                    ESP_LOGW(TAG, "No response or timeout from RS485 for 'C' command");
                }
            } else if (strcmp(received_command.command, "F") == 0) {
                // Send 'F XXX' command to set the rotator's angle
                char cmd_buffer[32]; // Sufficient size for "F XXX\r\n"
                snprintf(cmd_buffer, sizeof(cmd_buffer), "F %d\r\n", received_command.value);
                uart_write_bytes(UART_PORT_NUM, cmd_buffer, strlen(cmd_buffer));
                ESP_LOGI(TAG, "Sent 'F %d' command to RS485", received_command.value);

                // You might add logic here to wait for an acknowledgment from the rotator if needed.
            } else if (strcmp(received_command.command, "ST") == 0) {
                // Logic for 'ST' command (to be implemented later)
                ESP_LOGI(TAG, "Received 'ST' command - to be implemented");
                uart_write_bytes(UART_PORT_NUM, "ST\r\n", strlen("ST\r\n"));
            } else {
                ESP_LOGW(TAG, "Unknown RS485 command received from queue: %s", received_command.command);
            }
            // Add a small delay after each RS485 operation to allow transceiver to settle
            vTaskDelay(pdMS_TO_TICKS(50)); 
        }
    }
}

// Function to add a command to the RS485 command queue from other tasks (e.g., web server)
bool send_rs485_command(const char *command, int value) {
    rs485_command_t cmd;
    // Copy the command string and ensure null-termination
    strncpy(cmd.command, command, sizeof(cmd.command) - 1);
    cmd.command[sizeof(cmd.command) - 1] = '\0';
    cmd.value = value;

    // Send the command to the queue, do not block if the queue is full (timeout 0)
    if (xQueueSend(xRS485CommandQueue, &cmd, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send RS485 command '%s' to queue (queue full?)", command);
        return false;
    }
    ESP_LOGI(TAG, "RS485 command '%s' (value %d) queued successfully.", command, value);
    return true;
}