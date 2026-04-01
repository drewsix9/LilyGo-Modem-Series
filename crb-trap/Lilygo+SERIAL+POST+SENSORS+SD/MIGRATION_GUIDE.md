# Migration Guide: ASCII + SerialTransfer Hybrid Protocol

For developers integrating this refactored protocol with existing modules (http_upload, sensors, etc.).

---

## Quick Reference: What Changed

| Operation               | Old Code                               | New Code                                   |
| ----------------------- | -------------------------------------- | ------------------------------------------ |
| Slave READY signal      | Binary struct packet                   | `sendReadyPacket(lux, isFallen, w, h, ts)` |
| Master wait READY       | `waitForSlaveReady(metadata, timeout)` | `waitForSlaveReady(timeout)`               |
| Master send capture cmd | `sendSlaveACK()`                       | `sendPhotoCommand(w, h, q)`                |
| Photo reception         | Same                                   | Same (binary chunks unchanged)             |
| Error response          | Binary ERROR packet                    | `sendErrorMessage(code)`                   |
| Connection check        | `isMasterAlive()` / `isSlaveAlive()`   | `isMasterAlive()` / `isSlaveAlive()`       |

---

## Code Migration Examples

### Example 1: Slave Ready Signal

**Old (Binary):**

```cpp
PhotoMetadata metadata;
metadata.luxValue = 0;
metadata.isFallen = 0;
metadata.photoWidth = 640;
metadata.photoHeight = 480;
metadata.timestamp = millis() / 1000;

sendReadyPacket(metadata);
```

**New (ASCII):**

```cpp
uint16_t lux = 0;
uint8_t isFallen = 0;
uint16_t photoWidth = 640;
uint16_t photoHeight = 480;
uint32_t timestamp = millis() / 1000;

sendReadyPacket(lux, isFallen, photoWidth, photoHeight, timestamp);
```

### Example 2: Master Photo Command

**Old (Binary):**

```cpp
if (!waitForSlaveReady(metadata, UART_RX_TIMEOUT_MS)) {
  Serial.println("ERROR: Slave READY timeout");
  return;
}

if (!sendSlaveACK()) {
  Serial.println("ERROR: Failed to send ACK");
  return;
}
```

**New (ASCII):**

```cpp
if (!waitForSlaveReady(UART_RX_TIMEOUT_MS)) {
  Serial.println("ERROR: Slave READY timeout");
  return;
}

if (!sendPhotoCommand(PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY)) {
  Serial.println("ERROR: Failed to send PHOTO command");
  return;
}
```

### Example 3: Slave Command Loop

**Old (Binary):**

```cpp
void loop() {
  yield();
  processIncomingPackets();  // Wait for ACK

  if (!isMasterAlive()) {
    // Master disconnected
  }

  delay(10);
}
```

**New (ASCII):**

```cpp
void loop() {
  yield();

  // Check for incoming ASCII commands
  if (commandAvailable()) {
    PhotoCommand cmd;
    if (getCommand(cmd)) {
      // Process PHOTO, STAT, PING, RESET
      if (strcmp(cmd.cmdName, CMD_PHOTO) == 0) {
        // Capture and send photo
        esp_camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
          sendPhotoChunked(fb->buf, fb->len, cmd.quality);
          esp_camera_fb_return(fb);
        }
      }
    }
  }

  if (!isMasterAlive()) {
    // Master disconnected
  }

  delay(10);
}
```

### Example 4: Command Parser (if adding custom commands)

```cpp
// Example: Add custom "INFO" command that returns slave version
if (commandAvailable()) {
  PhotoCommand cmd;
  if (getCommand(cmd)) {
    if (strcmp(cmd.cmdName, "INFO") == 0) {
      // Send version as ASCII response
      char infoMsg[ASCII_RESP_MAX_LEN] = {0};
      snprintf(infoMsg, ASCII_RESP_MAX_LEN, "INFO:v1.0:ESP32CAM\n");
      UART_PORT.write((const uint8_t *)infoMsg, strlen(infoMsg));
      UART_PORT.flush();
    }
  }
}
```

---

## Integration with Existing Modules

### http_upload Module

No changes needed. The photo buffer format is unchanged:

- `uint8_t *photoBuffer` - still points to complete JPEG
- `uint32_t photoSize` - still total bytes
- Upload process unchanged

```cpp
// Old code still works:
uploadPhotoToServer(photo, photoSize);
freePhotoBuffer();
```

### sensor Module

No changes needed. Sensor readings are still available:

- Read LUX before sending PHOTO command
- Accelerometer data still used for fall detection
- Serial log output is now more readable

```cpp
// Master reading sensors (unchanged):
uint16_t luxValue = readLuxSensor();
Bmi160Reading bmiSample = {0};
bool isFallen = detectFallFromBmi160(bmiSample);

// Send PHOTO with current sensor state
sendPhotoCommand(PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);
```

### power_manager Module

No changes needed. Camera power control is unchanged:

- `powerOnCamera()` - GPIO23 LOW
- `powerOffCamera()` - GPIO23 HIGH
- Deep sleep logic unchanged

---

## Adding New ASCII Commands

To add a custom command (e.g., "CONFIG:param=value"):

### 1. Define Command Constant

```cpp
// In serialtransfer_protocol.h:
#define CMD_CONFIG "CONFIG"
```

### 2. Parse in Slave

```cpp
// In parseCommand() function (serialtransfer_slave.cpp):
if (strcmp(cmdName, CMD_CONFIG) == 0) {
  // Parse parameters
  char param[32] = {0};
  int value = 0;
  int n = sscanf(line, "%*[^:]:%31[^=]=%d", param, &value);

  if (n == 2) {
    // Handle CONFIG command
    sendAckMessage(0);  // Success
  } else {
    sendErrorMessage(ERR_INVALID_PARAMS);
  }
}
```

### 3. Send from Master

```cpp
// In master .ino or master module:
char configCmd[ASCII_CMD_MAX_LEN] = {0};
snprintf(configCmd, ASCII_CMD_MAX_LEN, "%s:brightness=128\n", CMD_CONFIG);
masterUART.write((const uint8_t *)configCmd, strlen(configCmd));
masterUART.flush();
```

### 4. Read Response

```cpp
// Wait for ASCII "ACK:0" or "ERR:code"
char respLine[ASCII_RESP_MAX_LEN] = {0};
// (use readAsciiResponse() if adding to master.cpp)
```

---

## Debugging & Troubleshooting

### Issue: Master timeout waiting for READY

**Symptom:**

```
[MASTER-RX] Waiting for slave READY (timeout=10000 ms)...
[MASTER-RX] ERROR: READY timeout after 10000 ms
```

**Cause:**

- Slave UART not initialized
- Baud rate mismatch
- Wiring loose (TX/RX swapped)
- Slave crashes before sending READY

**Debug:**

1. Check slave serial log for initialization messages
2. Verify baud rate: 115200 both sides
3. Check pin connections: GPIO4→19, GPIO16←18
4. Add delay after UART init: `delay(500)`

### Issue: Corrupted PHOTO command on slave

**Symptom:**

```
[SLAVE] [CMD] Parse error: invalid command format 'PHOTO:...'
```

**Cause:**

- ASCII line buffer overflow
- Incomplete line received
- Noise on serial line

**Debug:**

1. Add `Serial.printf("[CMD] Raw line: %s\n", line);` in parseCommand()
2. Check ASCII_CMD_MAX_LEN (64 bytes)
3. Verify no hardware interference
4. Add CRC check to ASCII lines (optional)

### Issue: Photo chunks corrupted

**Symptom:**

```
[MASTER] ERROR: Photo buffer does not contain valid JPEG
```

**Cause:**

- SerialTransfer CRC error (already detected, should cause retry)
- Buffer overflow during assembly
- Photo size > PHOTO_MAX_SIZE (400KB)

**Debug:**

1. Check SRAM/PSRAM available: `Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram())`
2. Start with smaller photo: `PHOTO_WIDTH=320, PHOTO_HEIGHT=240`
3. Log chunk reassembly: `Serial.printf("Chunk %d: offset=%lu, len=%zu\n", chunkId, offset, len)`

### Issue: Master never receives DONE

**Symptom:**

```
[MASTER-RX] All chunks received, waiting for DONE response...
[MASTER-RX] ERROR: DONE packet timeout
```

**Cause:**

- Slave crashes after photo transmission
- Slave sending DONE but master ASCII parser missing it
- Master still in chunk receive loop

**Debug:**

1. Check slave logs after last chunk: should see `[TX] Sending ASCII DONE`
2. Add timeout longer than chunk timeout: `ASCII_RESPONSE_TIMEOUT_MS = 2000`
3. Verify master exits chunk loop after expectedChunks received

---

## Performance Notes

### Throughput

**Binary Photo Chunks (~52KB photo, 212 chunks):**

- Chunk TX time: ~1-2ms per 250-byte chunk
- Total transmission: ~400-500ms (includes ACKs and processing)
- Effective baud rate: ~100 kbps (115200 × efficiency factor)

**ASCII Commands (~40 bytes):**

- Command TX time: <1ms
- Response RX time: <1ms

### Memory

**Slave:**

- ASCII buffer: 64 bytes (command)
- SerialTransfer buffer: ~254 bytes (packet)
- Total fixed overhead: ~320 bytes

**Master:**

- ASCII buffer: 128 bytes (response)
- SerialTransfer buffer: ~254 bytes (packet)
- Photo buffer: 400KB (dynamic, allocated per reception)
- Total fixed overhead: ~400 bytes

---

## Testing Checklist

Before deploying to production:

- [ ] **Slave boots and sends READY without errors**

  ```
  Serial monitor shows: [STATUS] Setup complete, waiting for PHOTO command...
  ```

- [ ] **Master receives READY and parses metadata**

  ```
  Serial monitor shows: [MASTER-RX] READY received: LUX=250, fallen=0, 640x480
  ```

- [ ] **Master sends PHOTO command**

  ```
  Serial monitor shows: [MASTER-TX] Sending ASCII PHOTO command: PHOTO:640:480:30
  ```

- [ ] **Slave receives PHOTO and captures**

  ```
  Serial monitor shows: [CAM] Photo captured: 52341 bytes
  ```

- [ ] **Master receives all chunks**

  ```
  Serial monitor shows: [MASTER-RX] Progress: 212/212 chunks
  ```

- [ ] **DONE confirmation received**

  ```
  Serial monitor shows: [MASTER-RX] DONE received (status=0)
  ```

- [ ] **Photo validated**

  ```
  Serial monitor shows: [MASTER] JPEG SOI marker found at offset 0
  ```

- [ ] **Buffer freed without leaks**
  ```
  Before sleep: Free PSRAM: 4000000 bytes (similar to after)
  ```

---

## References

- **serialtransfer_protocol.h** - ASCII command constants
- **serialtransfer_slave.h/cpp** - Slave implementation
- **serialtransfer_master.h/cpp** - Master implementation
- **SERIAL_PROTOCOL.md** - Complete protocol specification
- **REFACTORING_SUMMARY.md** - What changed overview

---

**Status:** ✅ Migration Guide Complete  
**Last Updated:** 2026-03-31
