/**
 * @file      uart_config.h
 * @brief     UART pin and configuration for ESP32-CAM slave
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-21
 * @note      Configuration for Serial2 communication to LilyGo master via SerialTransfer
 *
 * Pin Assignment:
 *  GPIO4  → UART TX (slave sends to master) [Flash LED pin]
 *  GPIO16 → UART RX (slave receives from master)
 *  Baud:    115200
 *
 * Verified NO conflicts:
 *  - Camera pins: GPIO0, GPIO5, GPIO18-27, GPIO32-35
 *  - SD Card (1-bit): GPIO14 (CLK), GPIO15 (MOSI), GPIO2 (MISO), GPIO13 (CS)
 *  - UART TX on GPIO4 avoids boot strapping issues and SD card conflicts
 */

#ifndef UART_CONFIG_H_
#define UART_CONFIG_H_

#include <Arduino.h>

// ==================== UART CONFIGURATION ====================

#define UART_PORT Serial2     /**< HardwareSerial instance (Serial2 = UART1) */
#define UART_TX_PIN 4         /**< GPIO4 → data OUT to master (Flash LED pin, safe from boot issues) */
#define UART_RX_PIN 16        /**< GPIO16 → data IN from master */
#define UART_BAUD_RATE 115200 /**< Baud rate (bits/sec) */

// ==================== DERIVED SETTINGS ====================

#define UART_CONFIG SERIAL_8N1 /**< 8 data bits, No parity, 1 stop bit */
#define UART_BUFFER_SIZE 512   /**< RX/TX buffer size (256 min, 512+ recommended for large packets) */

#endif // UART_CONFIG_H_
