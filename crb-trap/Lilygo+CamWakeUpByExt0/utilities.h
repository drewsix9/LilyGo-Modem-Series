/**
 * @file      utilities.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-04-30
 *
 */

#pragma once

// Note:

// When using ArduinoIDE, you must select a corresponding board type.
// If you don't know which board type you have, please click on the link to view it.
// 使用ArduinoIDE ,必须选择一个对应的板型 ,如果你不知道你的板型是哪种，请点击链接进行查看

// The model name with S3 after it is the ESP32-S3 version, otherwise it is the ESP32 version
// 型号名称后面带S3的为ESP32-S3版本，否则是ESP32版本

// Products Link:https://www.lilygo.cc/products/t-sim-a7670e
#define LILYGO_T_A7670

// There are two versions of T-Call A7670X. Please be careful to distinguish them. Please check the silkscreen on the front of the board to distinguish them.
// T-Call A7670X 有两个版本,请注意区分，如何区分请查看板子正面丝印.
// #define LILYGO_T_CALL_A7670_V1_0

// There are two versions of T-Call A7670X. Please be careful to distinguish them. Please check the silkscreen on the front of the board to distinguish them.
// T-Call A7670X 有两个版本,请注意区分，如何区分请查看板子正面丝印.
// #define LILYGO_T_CALL_A7670_V1_1

// Products Link: https://lilygo.cc/products/t-sim-7670g-s3
// #define LILYGO_T_SIM7670G_S3

// Products Link: https://lilygo.cc/products/t-a7608e-h?variant=42860532433077
// #define LILYGO_T_A7608X

// Products Link: https://lilygo.cc/products/t-a7608e-h?variant=43932699033781
// #define LILYGO_T_A7608X_S3

// Products Link: https://lilygo.cc/products/t-sim7000g
// #define LILYGO_SIM7000G

// Products Link: https://lilygo.cc/products/t-sim7070g
// #define LILYGO_SIM7070G

// Products Link: https://lilygo.cc/products/a-t-pcie?variant=42335922094261
// #define LILYGO_T_PCIE_A767X

// Products Link: https://lilygo.cc/products/a-t-pcie?variant=42335921897653
// #define LILYGO_T_PCIE_SIM7000G

// Products Link: https://lilygo.cc/products/a-t-pcie?variant=42335921995957
// #define LILYGO_T_PCIE_SIM7080G

// Products Link: ......
// #define LILYGO_T_PCIE_SIM7670G

// Products Link: https://lilygo.cc/products/t-eth-elite-1?variant=44498205049013
// #define LILYGO_T_ETH_ELITE_A7670X
// #define LILYGO_T_ETH_ELITE_SIM7000X
// #define LILYGO_T_ETH_ELITE_SIM7080G
// #define LILYGO_T_ETH_ELITE_SIM7600X
// #define LILYGO_T_ETH_ELITE_SIM7670G


// https://lilygo.cc/products/t-sim7600
// #define LILYGO_SIM7600X

// Products Link: ......
// #define LILYGO_T_RELAY_S3_SIMSHIELD

// https://lilygo.cc/products/t-internet-com
// #define LILYGO_T_INTERNET_COM_A7670X
// #define LILYGO_T_INTERNET_COM_SIM7000X
// #define LILYGO_T_INTERNET_COM_SIM7080G
// #define LILYGO_T_INTERNET_COM_SIM7600X
// #define LILYGO_T_INTERNET_COM_SIM7670G

// SIMCOM standard interface series
// #define LILYGO_SIM7000G_S3_STAN
// #define LILYGO_SIM7080G_S3_STAN
// #define LILYGO_SIM7670G_S3_STAN
// #define LILYGO_A7670X_S3_STAN
// #define LILYGO_SIM7600X_S3_STAN


#if defined(LILYGO_T_A7670)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_DTR_PIN                       (25)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN                    (4)
    // The modem power switch must be set to HIGH for the modem to supply power.
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)
    #define SDCARD_PRESENT_PIN                  (34)

    #define GPS_BAUD_RATE                       (9600)
    #define GPS_RX_PIN                          (36)
    #define GPS_TX_PIN                          (13)

    #define I2C_SDA_PIN                         (21)
    #define I2C_SCL_PIN                         (22)

    #define LED_PIN                             (12)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

    #ifndef SerialGPS
    #define SerialGPS                           Serial2
    #endif


#elif defined(LILYGO_T_CALL_A7670_V1_0)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_DTR_PIN                       (25)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN                    (4)
    // The modem power switch must be set to HIGH for the modem to supply power.
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)

    #define GPS_BAUD_RATE                       (9600)
    #define GPS_RX_PIN                          (36)
    #define GPS_TX_PIN                          (13)

    #define I2C_SDA_PIN                         (21)
    #define I2C_SCL_PIN                         (22)

    #define LED_PIN                             (12)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

    #ifndef SerialGPS
    #define SerialGPS                           Serial2
    #endif

#elif defined(LILYGO_T_CALL_A7670_V1_1)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_DTR_PIN                       (25)
    #define MODEM_TX_PIN                        (27)
    #define MODEM_RX_PIN                        (26)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN                    (4)
    // The modem power switch must be set to HIGH for the modem to supply power.
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)

    #define GPS_BAUD_RATE                       (9600)
    #define GPS_RX_PIN                          (36)
    #define GPS_TX_PIN                          (13)

    #define I2C_SDA_PIN                         (21)
    #define I2C_SCL_PIN                         (22)

    #define LED_PIN                             (12)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

    #ifndef SerialGPS
    #define SerialGPS                           Serial2
    #endif


#elif defined(LILYGO_T_SIM7670G_S3)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (16)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)
    #define BOARD_MISO_PIN                      (8)
    #define BOARD_MOSI_PIN                      (10)
    #define BOARD_SCK_PIN                       (9)
    #define BOARD_SD_CS_PIN                     (11)
    #define SDCARD_PRESENT_PIN                  (12)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define ADC_PIN                             (1)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (500)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7670

    #define SerialAT                            Serial1

#elif defined(LILYGO_T_A7608X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (25)
    #define BOARD_PWRKEY_PIN                    (4)
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)
    #define BOARD_SD_DETECT_PIN                 (34)

    #define GPS_BAUD_RATE                       (115200)
    #define GPS_RX_PIN                          (36)
    #define GPS_TX_PIN                          (13)

    #define I2C_SDA_PIN                         (21)
    #define I2C_SCL_PIN                         (22)

    #define LED_PIN                             (12)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_A7608

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

    #ifndef SerialGPS
    #define SerialGPS                           Serial2
    #endif

#elif defined(LILYGO_T_A7608X_S3)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (16)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)
    #define BOARD_MISO_PIN                      (8)
    #define BOARD_MOSI_PIN                      (10)
    #define BOARD_SCK_PIN                       (9)
    #define BOARD_SD_CS_PIN                     (11)
    #define SDCARD_PRESENT_PIN                  (12)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define ADC_PIN                             (1)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (500)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_A7608

    #define SerialAT                            Serial1

    #define GPS_BAUD_RATE                       (115200)
    #define GPS_RX_PIN                          (4)
    #define GPS_TX_PIN                          (5)


#elif defined(LILYGO_T_A7608X_DC_S3)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (13)
    #define MODEM_RX_PIN                        (12)
    #define MODEM_DTR_PIN                       (11)
    #define MODEM_RING_PIN                      (3)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (14)
    #define BOARD_POWERON_PIN                   (48)
    #define BOARD_MISO_PIN                      (8)
    #define BOARD_MOSI_PIN                      (10)
    #define BOARD_SCK_PIN                       (9)
    #define BOARD_SD_CS_PIN                     (2)

    #define BOARD_LED_PIN                       (21)
    #define LED_ON                              (HIGH)

    #define ADC_PIN                             (1)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_A7608_DC_S3

    #define SerialAT                            Serial1

    #define GPS_BAUD_RATE                       (115200)
    #define GPS_RX_PIN                          (4)
    #define GPS_TX_PIN                          (5)


#elif defined(LILYGO_SIM7000G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (32)
    #define BOARD_PWRKEY_PIN                    (4)
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)

    #define GPS_BAUD_RATE                       (115200)
    #define GPS_RX_PIN                          (34)
    #define GPS_TX_PIN                          (12)

    #define I2C_SDA_PIN                         (21)
    #define I2C_SCL_PIN                         (22)

    #define LED_PIN                             (13)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7000

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

    #ifndef SerialGPS
    #define SerialGPS                           Serial2
    #endif

#elif defined(LILYGO_SIM7070G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (32)
    #define BOARD_PWRKEY_PIN                    (4)
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)
    #define SDCARD_PRESENT_PIN                  (34)

    #define GPS_BAUD_RATE                       (115200)
    #define GPS_RX_PIN                          (34)
    #define GPS_TX_PIN                          (12)

    #define I2C_SDA_PIN                         (21)
    #define I2C_SCL_PIN                         (22)

    #define LED_PIN                             (13)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7080

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

    #ifndef SerialGPS
    #define SerialGPS                           Serial2
    #endif

#elif defined(LILYGO_SIM7600X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (32)
    #define BOARD_PWRKEY_PIN                    (4)
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)
    #define SDCARD_PRESENT_PIN                  (34)

    #define GPS_BAUD_RATE                       (115200)
    #define GPS_RX_PIN                          (36)
    #define GPS_TX_PIN                          (13)

    #define I2C_SDA_PIN                         (21)
    #define I2C_SCL_PIN                         (22)

    #define LED_PIN                             (12)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7600

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

    #ifndef SerialGPS
    #define SerialGPS                           Serial2
    #endif

#elif defined(LILYGO_SIM7080G_S3)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)
    #define BOARD_MISO_PIN                      (12)
    #define BOARD_MOSI_PIN                      (11)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)

    #define BOARD_LED_PIN                       (16)
    #define LED_ON                              (LOW)

    #define ADC_PIN                             (1)

    #define I2C_SDA_PIN                         (7)
    #define I2C_SCL_PIN                         (6)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (500)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7080

    #define SerialAT                            Serial1


//! ===============================PCIE VARIANT====================================

#elif defined(LILYGO_T_PCIE_A767X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (32)
    #define BOARD_PWRKEY_PIN                    (4)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define LED_PIN                             (12)

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_A7670

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

#elif defined(LILYGO_T_PCIE_SIM7000G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (32)
    #define BOARD_PWRKEY_PIN                    (4)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define LED_PIN                             (12)

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7000

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

#elif defined(LILYGO_T_PCIE_SIM7080G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (32)
    #define BOARD_PWRKEY_PIN                    (4)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define LED_PIN                             (12)

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7080

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

#elif defined(LILYGO_T_PCIE_SIM7600X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (32)
    #define BOARD_PWRKEY_PIN                    (4)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define LED_PIN                             (12)

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7600

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

#elif defined(LILYGO_T_PCIE_SIM7670G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    #define MODEM_DTR_PIN                       (32)
    #define BOARD_PWRKEY_PIN                    (4)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define LED_PIN                             (12)

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7670

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #ifndef SerialAT
    #define SerialAT                            Serial1
    #endif

//! ===============================ETH-Elite VARIANT================================

#elif defined(LILYGO_T_ETH_ELITE_A7670X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RST_PIN                         (5)
    #define ETH_MISO_PIN                        (11)
    #define ETH_MOSI_PIN                        (12)
    #define ETH_SCK_PIN                         (10)
    #define ETH_CS_PIN                          (9)
    #define ETH_INT_PIN                         (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_A7670

    #define SerialAT                            Serial1



#elif defined(LILYGO_T_ETH_ELITE_SIM7600X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RST_PIN                         (5)
    #define ETH_MISO_PIN                        (11)
    #define ETH_MOSI_PIN                        (12)
    #define ETH_SCK_PIN                         (10)
    #define ETH_CS_PIN                          (9)
    #define ETH_INT_PIN                         (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7600

    #define SerialAT                            Serial1

#elif defined(LILYGO_T_ETH_ELITE_SIM7000X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RST_PIN                         (5)
    #define ETH_MISO_PIN                        (11)
    #define ETH_MOSI_PIN                        (12)
    #define ETH_SCK_PIN                         (10)
    #define ETH_CS_PIN                          (9)
    #define ETH_INT_PIN                         (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7000

    #define SerialAT                            Serial1

#elif defined(LILYGO_T_ETH_ELITE_SIM7080G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RST_PIN                         (5)
    #define ETH_MISO_PIN                        (11)
    #define ETH_MOSI_PIN                        (12)
    #define ETH_SCK_PIN                         (10)
    #define ETH_CS_PIN                          (9)
    #define ETH_INT_PIN                         (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7080

    #define SerialAT                            Serial1


#elif defined(LILYGO_T_ETH_ELITE_SIM7670G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RST_PIN                         (5)
    #define ETH_MISO_PIN                        (11)
    #define ETH_MOSI_PIN                        (12)
    #define ETH_SCK_PIN                         (10)
    #define ETH_CS_PIN                          (9)
    #define ETH_INT_PIN                         (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7670

    #define SerialAT                            Serial1

//! ===============================Internet-Com VARIANT================================

#elif defined(LILYGO_T_INTERNET_COM_A7670X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_MISO_PIN                      (11)
    #define BOARD_MOSI_PIN                      (12)
    #define BOARD_SCK_PIN                       (10)
    #define BOARD_SD_CS_PIN                     (9)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RESET_PIN                       (5)
    #define ETH_CS_PIN                          (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_A7670

    #define SerialAT                            Serial1


#elif defined(LILYGO_T_INTERNET_COM_SIM7600X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_MISO_PIN                      (11)
    #define BOARD_MOSI_PIN                      (12)
    #define BOARD_SCK_PIN                       (10)
    #define BOARD_SD_CS_PIN                     (9)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RESET_PIN                       (5)
    #define ETH_CS_PIN                          (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7600

    #define SerialAT                            Serial1


#elif defined(LILYGO_T_INTERNET_COM_SIM7000X)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_MISO_PIN                      (11)
    #define BOARD_MOSI_PIN                      (12)
    #define BOARD_SCK_PIN                       (10)
    #define BOARD_SD_CS_PIN                     (9)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RESET_PIN                       (5)
    #define ETH_CS_PIN                          (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7000

    #define SerialAT                            Serial1

#elif defined(LILYGO_T_INTERNET_COM_SIM7080G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_MISO_PIN                      (11)
    #define BOARD_MOSI_PIN                      (12)
    #define BOARD_SCK_PIN                       (10)
    #define BOARD_SD_CS_PIN                     (9)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RESET_PIN                       (5)
    #define ETH_CS_PIN                          (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7080

    #define SerialAT                            Serial1

#elif defined(LILYGO_T_INTERNET_COM_SIM7670G)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (45)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (47)
    #define BOARD_PWRKEY_PIN                    (46)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)

    #define BOARD_MISO_PIN                      (11)
    #define BOARD_MOSI_PIN                      (12)
    #define BOARD_SCK_PIN                       (10)
    #define BOARD_SD_CS_PIN                     (9)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define ETH_ADDR                            0
    #define ETH_RESET_PIN                       (5)
    #define ETH_CS_PIN                          (8)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (1000)
    #define MODEM_RESET_LEVEL                   HIGH

    #define BOARD_VARIANT_A7670          0
    #define BOARD_VARIANT_A7608          1
    #define BOARD_VARIANT_SIM7080        2
    #define BOARD_VARIANT_SIM7000        3
    #define BOARD_VARIANT_SIM7600        4
    #define BOARD_VARIANT_SIM7670        5
    #define BOARD_VARIANT_A7608_DC_S3    6

    #define BOARD_VARIANT   BOARD_VARIANT_SIM7670

    #define SerialAT                            Serial1

//! ===============================STANDARD SERIES VARIANT================================

#elif defined(LILYGO_SIMCOM_STANDARD_PIN)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_TX_PIN                        (17)
    #define MODEM_RX_PIN                        (18)
    #define MODEM_DTR_PIN                       (16)
    #define MODEM_RING_PIN                      (14)
    #define MODEM_RESET_PIN                     (1)
    #define BOARD_PWRKEY_PIN                    (2)
    #define MODEM_FLIGHT_PIN                    (21)
    #define BOARD_POWERON_PIN                   (15)
    #define BOARD_MISO_PIN                      (8)
    #define BOARD_MOSI_PIN                      (10)
    #define BOARD_SCK_PIN                       (9)
    #define BOARD_SD_CS_PIN                     (11)
    #define SDCARD_PRESENT_PIN                  (12)

    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              (LOW)

    #define ADC_PIN                             (4)

    #define I2C_SDA_PIN                         (6)
    #define I2C_SCL_PIN                         (7)

    #define GPS_SHIELD_WAKEUP_PIN               (48)
    #define GPS_SHIELD_RX_PIN                   (42)
    #define GPS_SHIELD_TX_PIN                   (41)
    #define GPS_SHIELD_BAUD                     (9600)

    #define MODEM_POWERON_PULSE_WIDTH_MS        (500)
    #define MODEM_RESET_LEVEL                   HIGH

    #define SerialAT                            Serial1


    #if defined(LILYGO_A7670X_S3_STAN)
        #define BOARD_VARIANT_A7670          0
        #define BOARD_VARIANT_A7608          1
        #define BOARD_VARIANT_SIM7080        2
        #define BOARD_VARIANT_SIM7000        3
        #define BOARD_VARIANT_SIM7600        4
        #define BOARD_VARIANT_SIM7670        5
        #define BOARD_VARIANT_A7608_DC_S3    6

        #define BOARD_VARIANT   BOARD_VARIANT_A7670
    #elif defined(LILYGO_SIM7000G_S3_STAN)
        #define BOARD_VARIANT_A7670          0
        #define BOARD_VARIANT_A7608          1
        #define BOARD_VARIANT_SIM7080        2
        #define BOARD_VARIANT_SIM7000        3
        #define BOARD_VARIANT_SIM7600        4
        #define BOARD_VARIANT_SIM7670        5
        #define BOARD_VARIANT_A7608_DC_S3    6

        #define BOARD_VARIANT   BOARD_VARIANT_SIM7000
    #elif defined(LILYGO_SIM7080G_S3_STAN)
        #define BOARD_VARIANT_A7670          0
        #define BOARD_VARIANT_A7608          1
        #define BOARD_VARIANT_SIM7080        2
        #define BOARD_VARIANT_SIM7000        3
        #define BOARD_VARIANT_SIM7600        4
        #define BOARD_VARIANT_SIM7670        5
        #define BOARD_VARIANT_A7608_DC_S3    6

        #define BOARD_VARIANT   BOARD_VARIANT_SIM7080
    #elif defined(LILYGO_SIM7670G_S3_STAN)
        #define BOARD_VARIANT_A7670          0
        #define BOARD_VARIANT_A7608          1
        #define BOARD_VARIANT_SIM7080        2
        #define BOARD_VARIANT_SIM7000        3
        #define BOARD_VARIANT_SIM7600        4
        #define BOARD_VARIANT_SIM7670        5
        #define BOARD_VARIANT_A7608_DC_S3    6

        #define BOARD_VARIANT   BOARD_VARIANT_SIM7670

    #elif defined(LILYGO_SIM7600X_S3_STAN)
        #define BOARD_VARIANT_A7670          0
        #define BOARD_VARIANT_A7608          1
        #define BOARD_VARIANT_SIM7080        2
        #define BOARD_VARIANT_SIM7000        3
        #define BOARD_VARIANT_SIM7600        4
        #define BOARD_VARIANT_SIM7670        5
        #define BOARD_VARIANT_A7608_DC_S3    6

        #define BOARD_VARIANT   BOARD_VARIANT_SIM7600
    #endif

#else

    #error "Please select a board for use."

#endif
