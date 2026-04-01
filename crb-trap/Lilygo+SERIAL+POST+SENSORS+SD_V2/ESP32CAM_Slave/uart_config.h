/**
 * @file      uart_config.h
 * @brief     UART pin and port mapping for ESP32-CAM slave.
 */

#pragma once

#include <Arduino.h>

#define UART_PORT Serial2
#define UART_TX_PIN 4
#define UART_RX_PIN 16
#define UART_BAUD_RATE 115200
#define UART_CONFIG SERIAL_8N1
