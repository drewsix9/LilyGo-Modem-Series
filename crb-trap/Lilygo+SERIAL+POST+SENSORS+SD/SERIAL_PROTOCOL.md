# Hybrid Serial Protocol: ASCII Commands + SerialTransfer Image Data

**Version:** 1.0  
**Date:** 2026-03-31  
**Status:** Implementation Complete

## Overview

The refactored protocol splits communication into two modes:

- **ASCII Text Mode** (Simple Serial)
  - Used for: Commands, status queries, handshakes, responses
  - Format: Line-terminated ASCII strings (`\n` = 0x0A)
  - Medium: Regular UART Serial.write() / Serial.read()
  - **Advantages**: Human-readable logs, simple parsing, easy debugging

- **Binary Packet Mode** (SerialTransfer Library)
  - Used for: Photo image data only (chunks of binary JPEG data)
  - Format: SerialTransfer framed packets with automatic COBS encoding, CRC8, and framing
  - Medium: UART via SerialTransfer library
  - **Advantages**: Robust error handling, automatic CRC validation, efficient framing

This hybrid approach simplifies command handling while maintaining robust image transmission.

---

## Hardware Setup

### Slave: ESP32-CAM

- **Port:** Serial2 (UART1)
- **TX:** GPIO4 → Master RX (GPIO19)
- **RX:** GPIO16 ← Master TX (GPIO18)
- **Baud:** 115200 bps

### Master: LilyGo (T-A7670)

- **Port:** Serial1 (UART1)
- **TX:** GPIO18 → Slave RX (GPIO16)
- **RX:** GPIO19 ← Slave TX (GPIO4)
- **Baud:** 115200 bps

**Connection:** Crossed wired serial (TX ↔ RX on both sides)

---

## Protocol Flow

### 1. Initialization Sequence

```
Master (Power-on)
├─ Initialize UART1 (GPIO18|19 @ 115200 baud)
├─ Initialize SerialTransfer (binary mode)
├─ Power on camera (GPIO23 LOW)
├─ Wait 2500ms for camera boot
│
└─> READY FOR COMMUNICATION

Slave (Power-on)
├─ Initialize UART2 (GPIO4|16 @ 115200 baud)
├─ Initialize SerialTransfer (binary mode)
├─ Initialize camera
├─ Send ASCII: "READY:lux:fall:ts:width:height\n"
│
└─> WAITING FOR COMMAND
```

### 2. Photo Capture Sequence

```
Master                                    Slave
  │
  ├── Wait for "READY:..." (ASCII) ──────┐
  │                                       │
  │◄──── ASCII: "READY:250:0:160000000:640:480"
  │      Meaning: LUX=250, not fallen, ts=160000000, 640x480
  │
  ├── Send "PHOTO:640:480:30\n" (ASCII) ──────┐
  │      Meaning: capture 640x480 JPEG quality=30
  │                                           ├─ Parse command
  │                                           ├─ Validate parameters
  │                                           ├─ Capture photo
  │
  │◄──── Binary: PHOTO_HEADER
  │      (Via SerialTransfer: totalSize, numChunks, quality)
  │      ├─ Allocate buffer on master
  │
  │◄──── Binary: PHOTO_CHUNK[0] (chunkId=0, data...)
  │      ├─ Reassemble in buffer
  │
  │◄──── Binary: PHOTO_CHUNK[1] (chunkId=1, data...)
  │      ├─ Reassemble in buffer
  │
  │◄──── ... (more chunks)
  │
  │◄──── ASCII: "DONE:0" (status=0 for success)
  │
  └──────► Photo reception complete
           (Buffer can now be processed/uploaded)
```

---

## Message Formats

### ASCII Commands (Master → Slave)

All ASCII commands are newline-terminated: `\n` (0x0A)

#### PHOTO (Trigger Photo Capture)

```
Format:   PHOTO:width:height:quality\n
Example:  PHOTO:640:480:30\n
Response: (Binary photo stream + ASCII "DONE:0\n")

Parameters:
  width:    Photo width in pixels (320-1600)
  height:   Photo height in pixels (240-1200)
  quality:  JPEG quality 1-63 (lower=better quality but larger size)
```

#### STAT (Request Status)

```
Format:   STAT\n
Response: ACK:0\n (or ACK:1\n if error)
```

#### PING (Connection Check)

```
Format:   PING\n
Response: ACK:0\n
```

#### RESET (Abort and Reset)

```
Format:   RESET\n
Response: (none - slave restarts)
```

---

### ASCII Responses (Slave → Master)

All ASCII responses are newline-terminated: `\n` (0x0A)

#### READY (Slave Ready with Metadata)

```
Format:   READY:lux:fall:timestamp:width:height\n
Example:  READY:250:0:160000000:640:480\n

Fields:
  lux:       Ambient light intensity (0-65535 LUX) - sensor reading
  fall:      Fall detection status (0=not fallen, 1=fallen)
  timestamp: Epoch time or system uptime (seconds, 32-bit)
  width:     Camera capture width (pixels) that will be used
  height:    Camera capture height (pixels) that will be used
```

#### DONE (Photo Transmission Complete)

```
Format:   DONE:status\n
Example:  DONE:0\n

Status codes:
  0: Success - photo transmitted without errors
  1: Error - photo transmission failed
  2: Timeout - master never acknowledged
  3: Invalid parameters - capture parameters out of range
```

#### ERR (Error Response)

```
Format:   ERR:code\n
Example:  ERR:2\n

Error codes:
  0: ERR_NONE               - No error
  1: ERR_TIMEOUT            - Communication timeout
  2: ERR_INVALID_PARAMS     - Invalid command parameters
  3: ERR_CAMERA_FAIL        - Camera capture failed
  4: ERR_BUFFER_OVERFLOW    - Photo too large for buffer
  5: ERR_CRC_ERROR          - CRC validation failed
  6: ERR_GENERIC            - Unknown/other error
```

#### ACK (Acknowledgment)

```
Format:   ACK:status\n
Status:   0 = OK, non-zero = error
```

---

### Binary Packets (SerialTransfer Mode - Image Data Only)

Binary packets are automatically framed by SerialTransfer library:

```
[START_BYTE(0x7E)] [PACKET_ID] [COBS_OVERHEAD] [LENGTH] [PAYLOAD...] [CRC8] [STOP_BYTE(0x81)]
```

#### PACKET_ID_PHOTO_HEADER (0x03)

```
Structure: PhotoHeader (8 bytes, sent as SerialTransfer payload)
  ├─ uint32_t totalSize     - Total JPEG file size in bytes
  ├─ uint16_t numChunks     - Total number of chunks to follow
  ├─ uint8_t  quality       - JPEG quality (1-63)
  └─ uint8_t  reserved      - Reserved/future use

Max packet size: 254 bytes payload (SerialTransfer limit)
Chunk size: 250 bytes PHOTO_CHUNK_SIZE
```

#### PACKET_ID_PHOTO_CHUNK (0x04)

```
Structure: PhotoChunk (variable length, max 250 bytes)
  ├─ uint16_t chunkId          - Sequence number (0 to numChunks-1)
  └─ uint8_t  data[248]        - Actual payload (variable, up to 248 bytes)

Layout: [2 bytes chunkId | N bytes data]
        where N ≤ 248 (to keep total ≤ 250 bytes)
```

---

## Timeout Parameters

| Parameter                   | Value   | Usage                                          |
| --------------------------- | ------- | ---------------------------------------------- |
| `ASCII_RESPONSE_TIMEOUT_MS` | 2000 ms | Max wait for ASCII command response            |
| `SERIAL_CHUNK_TIMEOUT_MS`   | 5000 ms | Max wait for binary chunk packet               |
| `RETRY_MAX_ATTEMPTS`        | 3       | Number of retries on failed chunk transmission |
| `RETRY_BACKOFF_MS`          | 500 ms  | Delay between retries                          |
| `CAM_BOOT_TIME_MS`          | 2500 ms | Camera power-on to UART-ready time             |

---

## Error Handling & Retry Logic

### Master Receiving Photo Chunks

```
for each chunk:
  attempt = 0
  while attempt < RETRY_MAX_ATTEMPTS:
    send tick() to SerialTransfer
    if PHOTO_CHUNK received within SERIAL_CHUNK_TIMEOUT_MS:
      unpack chunk data
      append to photo buffer
      break (success)
    else:
      attempt++
      wait RETRY_BACKOFF_MS
      retry

  if all retries exhausted:
    abort photo reception
    return error
```

### Slave Sending Photo

- Sends photo in fixed-size chunks (250 bytes per packet)
- No explicit ACK per chunk needed (SerialTransfer handles framing)
- Final ASCII `DONE:status` indicates end of transmission

---

## State Diagrams

### Master State Machine

```
[IDLE]
  ↓ (PIR wakeup)
[INIT UART] → [WAIT_READY] → [SEND_PHOTO_CMD] → [RX_PHOTO] → [PROCESS] → [SLEEP]
  ↑                  ↓            ↓                  ↓           ↓
  └─────── ERROR ─→ [POWER_OFF] → [SLEEP]
```

### Slave State Machine

```
[INIT] → [READY_SIGNAL] → [CMD_WAIT] → [CMD_PARSE] ─┐
  ↑                           ↑            ↓        │
  │                           └─── ERROR ──┘        │
  │                                                  ↓
  └───────────────────── [CAPTURE] → [TX_PHOTO] ────┘
```

---

## Example Communication Trace

### Successful Photo Capture (640×480 ~50KB)

```
Master: [Wait for READY]
Slave:  → READY:250:0:160000000:640:480

Master: → PHOTO:640:480:30
Slave:  [Parse, validate, capture photo: 52341 bytes]
Slave:  [Calculate chunks: ceil(52341 / 248) = 212 chunks]
Slave:  ← PHOTO_HEADER (totalSize=52341, numChunks=212, quality=30)
Master: [Allocate 52KB buffer]

Slave:  ← PHOTO_CHUNK[0]  (248 bytes)
Master: [Copy to buffer, progress: 248 / 52341]
Slave:  ← PHOTO_CHUNK[1]  (248 bytes)
Master: [Copy to buffer, progress: 496 / 52341]
...
Slave:  ← PHOTO_CHUNK[211] (101 bytes - last chunk, smaller)
Master: [Copy to buffer, progress: 52341 / 52341]

Slave:  → DONE:0
Master: [JPEG validation OK, ready to upload]
```

---

## Debugging & Diagnostics

### Serial Monitor Output

When enabled, both slave and master output detailed logs:

**Slave:**

```
[UART] Initializing Serial2 (UART1) for hybrid protocol...
[UART] Configured: GPIO4 (TX) + GPIO16 (RX) @ 115200 baud
[UART] SerialTransfer initialized (binary image data mode)
[UART] ASCII command parser ready (text mode)
[STATUS] Setup complete, waiting for PHOTO command from master...
[CMD] Received command: PHOTO
[CMD] PHOTO command: 640x480 quality=30
[CAM] Photo captured: 52341 bytes
[TX] Starting photo transmission: 52341 bytes, quality=30
[TX] Sending PHOTO_HEADER via SerialTransfer: size=52341 bytes, chunks=212
[TX] Progress: 10/212 chunks sent (2480/52341 bytes)
...
[TX] All 212 chunks sent (52341 bytes)
[TX] Sending ASCII DONE: DONE:0
```

**Master:**

```
[MASTER-UART] Initializing master UART for hybrid protocol...
[MASTER-UART] Configured: GPIO19 (RX) + GPIO18 (TX) @ 115200 baud
[MASTER-RX] Waiting for slave READY (timeout=10000 ms)...
[MASTER-RX] READY received: LUX=250, fallen=0, 640x480
[MASTER-TX] Sending ASCII PHOTO command: PHOTO:640:480:30
[MASTER-RX] Waiting for PHOTO_HEADER from slave...
[MASTER-RX] PHOTO_HEADER received: size=52341, chunks=212, quality=30
[MASTER-RX] Buffer allocated: 52341 bytes
[MASTER-RX] Progress: 10/212 chunks (2480/52341 bytes)
...
[MASTER-RX] All chunks received, waiting for DONE response...
[MASTER-RX] DONE received (status=0)
[MASTER] JPEG SOI marker found at offset 0
[MASTER-RX] Photo reception COMPLETE: 52341 bytes
[SUCCESS] Photo received: 52341 bytes
```

### Diagnostic Commands

**Slave:** `diagnosticUARTStatus()`

```
[DIAG] UART/SerialTransfer Status:
  Port:             Serial2 (UART1)
  TX Pin:           GPIO4
  RX Pin:           GPIO16
  Baud Rate:        115200
  Last Command:     150 ms ago
  Master Alive:     YES
  Protocol Mode:    HYBRID (ASCII commands + SerialTransfer image data)
  ASCII Buffer:     0/64 bytes used
```

**Master:** `diagnosticMasterUARTStatus()`

```
[MASTER-DIAG] UART Status (Hybrid Protocol):
  Port:             Serial1 (UART1)
  Last Activity:    200 ms ago
  Slave Alive:      YES
  Photo Buffer:     ALLOCATED (52341 bytes)
  Photo Complete:   YES
  Protocol Mode:    HYBRID (ASCII commands + SerialTransfer image data)
```

---

## Advantages of Hybrid Approach

| Aspect              | ASCII Commands       | SerialTransfer Image Data |
| ------------------- | -------------------- | ------------------------- |
| **Debugging**       | Human-readable logs  | Binary frames in hex      |
| **Parsing**         | Simple `sscanf()`    | Automatic by library      |
| **Error Detection** | Protocol version     | CRC8 per packet           |
| **Efficiency**      | Low overhead (small) | Optimized binary (large)  |
| **Complexity**      | Minimal              | Well-tested library       |
| **Extensibility**   | Easy to add commands | Fixed packet structure    |

---

## Testing Checklist

- [ ] ASCII READY message parsed correctly on master
- [ ] PHOTO command parameters validated on slave
- [ ] Photo chunks assembled without corruption
- [ ] JPEG SOI marker validated
- [ ] Timeout handling works (connection hangup recovery)
- [ ] Retry logic activates on packet loss
- [ ] Photo buffer allocation succeeds
- [ ] Serial logs are clean and informative
- [ ] Works with >400KB photos (PHOTO_MAX_SIZE)
- [ ] Baud rate 115200 maintains sync over long transfers

---

## References

- **SerialTransfer Library:** https://github.com/PowerBroker2/SerialTransfer
- **ESP32-CAM Camera API:** https://docs.espressif.com/projects/esp-idf/
- **JPEG Format:** https://en.wikipedia.org/wiki/JPEG#Syntax_and_structure

---

## Revision History

| Date       | Version | Change                                                                  |
| ---------- | ------- | ----------------------------------------------------------------------- |
| 2026-03-31 | 1.0     | Initial hybrid protocol refactor: ASCII commands + SerialTransfer image |
| (previous) | 0.9     | All-binary SerialTransfer protocol (deprecated)                         |
