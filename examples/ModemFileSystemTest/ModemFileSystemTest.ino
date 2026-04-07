/**
 * @file      ModemFileSystemTest.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-07-20
 * @note      Example is suitable for A7670X/A7608X/SIM767G/SIM7600 series,SIM7000/SIM7080 series not support
 */

// See all AT commands, if wanted
#define DUMP_AT_COMMANDS

#include "Arduino.h"
#include "utilities.h"
#include <TinyGsmClient.h>

#if defined(TINY_GSM_MODEM_SIM7000SSL) || defined(TINY_GSM_MODEM_SIM7000) || defined(TINY_GSM_MODEM_SIM7080)
#error "This example currently only supports A7670X/A7608X/SIM767G/SIM7600 series"
#endif

#ifdef DUMP_AT_COMMANDS // if enabled it requires the streamDebugger lib
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

static bool runFsRoundTrip(char path, const char *filename, const uint8_t *writeBuffer, size_t testPayloadSize) {
  size_t totalBytes = 0;
  size_t usedBytes = 0;
  size_t fileSize = 0;

  Serial.printf("\n[FS] Testing path %c: with filename '%s'\n", path, filename);

  if (!modem.fs_set_path(path)) {
    Serial.printf("[FS] Failed to select %c: partition\n", path);
    return false;
  }

  modem.fs_mem(totalBytes, usedBytes);
  Serial.printf("[FS] AT+FSMEM response: total=%luKB used=%luKB\n", totalBytes / 1024, usedBytes / 1024);

  size_t writeSize = modem.fs_write(filename, writeBuffer, testPayloadSize);
  if (writeSize != testPayloadSize) {
    Serial.printf("[FS] Write failed on %c:. expected:%lu actual:%lu\n", path, testPayloadSize, writeSize);
    return false;
  }

  if (modem.fs_attri(filename, fileSize) < 0) {
    Serial.printf("[FS] Failed to read attributes on %c:\n", path);
    modem.fs_del(filename);
    return false;
  }

  if (fileSize != testPayloadSize) {
    Serial.printf("[FS] File size mismatch on %c:. expected:%lu actual:%lu\n", path, testPayloadSize, fileSize);
    modem.fs_del(filename);
    return false;
  }

  uint8_t *buffer = (uint8_t *)ps_malloc(fileSize);
  if (!buffer) {
    Serial.println("[FS] Failed to allocate read buffer");
    modem.fs_del(filename);
    return false;
  }

  size_t readSize = modem.fs_read(filename, buffer, fileSize);
  bool readOk = (readSize == fileSize);
  bool cmpOk = readOk && (memcmp(buffer, writeBuffer, testPayloadSize) == 0);

  free(buffer);

  if (!readOk) {
    Serial.printf("[FS] Read failed on %c:. expected:%lu actual:%lu\n", path, fileSize, readSize);
    modem.fs_del(filename);
    return false;
  }

  if (!cmpOk) {
    Serial.printf("[FS] Buffer comparison failed on %c:\n", path);
    modem.fs_del(filename);
    return false;
  }

  if (modem.fs_del(filename) < 0) {
    Serial.printf("[FS] Warning: cleanup delete failed on %c: for '%s'\n", path, filename);
  }

  Serial.printf("[FS] Round-trip PASS on %c:\n", path);
  return true;
}

void setup() {
  Serial.begin(115200); // Set console baud rate

  Serial.println("Start Sketch");

  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

#ifdef BOARD_POWERON_PIN
  /* Set Power control pin output
   * * @note      Known issues, ESP32 (V1.2) version of T-A7670, T-A7608,
   *            when using battery power supply mode, BOARD_POWERON_PIN (IO12) must be set to high level after esp32 starts, otherwise a reset will occur.
   * */
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

  // Set modem reset pin ,reset modem
#ifdef MODEM_RESET_PIN
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
  delay(100);
  digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
  delay(2600);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

#ifdef MODEM_FLIGHT_PIN
  // If there is an airplane mode control, you need to exit airplane mode
  pinMode(MODEM_FLIGHT_PIN, OUTPUT);
  digitalWrite(MODEM_FLIGHT_PIN, HIGH);
#endif

#ifdef MODEM_DTR_PIN
  // Pull down DTR to ensure the modem is not in sleep state
  Serial.printf("Set DTR pin %d LOW\n", MODEM_DTR_PIN);
  pinMode(MODEM_DTR_PIN, OUTPUT);
  digitalWrite(MODEM_DTR_PIN, LOW);
#endif

  // Turn on the modem
  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(MODEM_POWERON_PULSE_WIDTH_MS);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);

  // Check if the modem is online
  Serial.println("Start modem...");

  int retry = 0;
  while (!modem.testAT(1000)) {
    Serial.println(".");
    if (retry++ > 30) {
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      delay(100);
      digitalWrite(BOARD_PWRKEY_PIN, HIGH);
      delay(MODEM_POWERON_PULSE_WIDTH_MS);
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      retry = 0;
    }
  }
  Serial.println();

  delay(5000);

  const char *sdFilenameWithSlash = "/sd_test.bin";
  const char *sdFilenameNoSlash = "sd_test.bin";
  const char *cFilename = "/c_fs_probe.bin";
  const size_t testPayloadSize = 1024;

  uint8_t *writeBuffer = (uint8_t *)ps_malloc(testPayloadSize);
  if (!writeBuffer) {
    Serial.println("Failed to allocate write test buffer");
    return;
  }

  for (size_t i = 0; i < testPayloadSize; ++i) {
    writeBuffer[i] = (uint8_t)((i * 37 + 11) & 0xFF);
  }

  bool dOk = runFsRoundTrip('D', sdFilenameWithSlash, writeBuffer, testPayloadSize);
  if (!dOk) {
    Serial.println("[FS] D: probe failed. Retrying without leading slash...");
    dOk = runFsRoundTrip('D', sdFilenameNoSlash, writeBuffer, testPayloadSize);
  }

  if (!dOk) {
    Serial.println("[FS] D: still failing. Running C: control test...");
    bool cOk = runFsRoundTrip('C', cFilename, writeBuffer, testPayloadSize);
    if (cOk) {
      Serial.println("[FS] RESULT: C: works but D: fails. SD partition is unavailable/unsupported on current modem firmware or hardware setup.");
    } else {
      Serial.println("[FS] RESULT: Both C: and D: tests failed. This is a broader modem filesystem issue, not SD-specific.");
    }
  } else {
    Serial.println("[FS] RESULT: D: round-trip succeeded. SD card partition is writable/readable via modem FS commands.");
  }

  free(writeBuffer);
}

void loop() {
  if (SerialAT.available()) {
    Serial.write(SerialAT.read());
  }
  if (Serial.available()) {
    SerialAT.write(Serial.read());
  }
  delay(1);
}

#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGO-T-A76XX/tree/main/lib) to the arduino libraries directory , See README"
#endif
