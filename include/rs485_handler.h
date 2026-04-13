#ifndef RS485_HANDLER_H
#define RS485_HANDLER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <driver/gpio.h>

// IMPORTANT: CONFIGURE THESE PINS AND BAUD RATE FOR YOUR HARDWARE!
#define UART_PORT_NUM      UART_NUM_1  // Using UART1 (or UART2 if UART0 is for console)
#define UART_TX_PIN        GPIO_NUM_4 // Example: Change to your actual RS485 TX pin
#define UART_RX_PIN        GPIO_NUM_36 // Example: Change to your actual RS485 RX pin
//#define UART_RS485_DE_PIN  GPIO_NUM_21 // Example: Change to your actual RS485 Data Enable (DE) pin

// Baudrate for RS485 communication
#define RS485_BAUD_RATE    38400 // IMPORTANT: Match this to your rotator's baud rate!

// Size of the FreeRTOS queue for RS485 commands
#define RS485_COMMAND_QUEUE_SIZE 10

// Structure to hold RS485 commands
typedef struct {
    char command[32]; // Max length of the command string (e.g., "C", "F", "ST")
    int value;        // Value for 'F' command (angle), 0 for 'C' or 'ST'
} rs485_command_t;

// Declare the FreeRTOS queues (defined and created in rs485_handler.cpp)
// This queue is for commands sent FROM the web server TO the RS485 task.
extern QueueHandle_t xRS485CommandQueue;
// This queue is for angle updates sent FROM the RS485 task TO the web server's WebSocket task.
extern QueueHandle_t xAngleUpdateQueue; 

// Function to initialize RS485 (UART driver setup, queue creation)
void rs485_init();

// FreeRTOS task to handle RS485 communication (sending commands, receiving responses)
void rs485_task(void *pvParameters);

// Function to send a command to the RS485 command queue (called from web_server.c)
bool send_rs485_command(const char *command, int value);

#endif // RS485_HANDLER_H