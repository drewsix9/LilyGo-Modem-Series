# SerialTransfer Protocol Refactoring - Implementation Summary

## Overview

Refactored the ESP32-CAM ↔ LilyGo serial communication from an all-binary SerialTransfer protocol to a **hybrid protocol**:

- **ASCII Text** for simple commands and responses (human-readable, easy debugging)
- **SerialTransfer Binary** for efficient image data transmission only

This split simplifies debugging while maintaining robust photo transmission.

---

## Files Modified

### Core Protocol Definition

- **serialtransfer_protocol.h**
  - Removed: `PACKET_ID_READY`, `PACKET_ID_ACK`, `PACKET_ID_COMPLETE`, `PhotoMetadata`, `AckPacket`, `ErrorPacket` structs
  - Added: ASCII command constants (`CMD_PHOTO`, `CMD_READY`, etc.), timeout parameters, frame size constraints
  - Kept: `PACKET_ID_PHOTO_HEADER`, `PACKET_ID_PHOTO_CHUNK`, `PhotoHeader`, `PhotoChunk` structs (binary image data only)

### Slave Implementation (ESP32-CAM)

- **serialtransfer_slave.h** (header)
  - Changed function signatures for new protocol
  - `sendReadyPacket()` now takes individual parameters (lux, isFallen, width, height, timestamp) instead of `PhotoMetadata` struct
  - Added: `commandAvailable()`, `getCommand()`, `sendErrorMessage()`, `sendAckMessage()`
  - Removed: `waitForMasterAck()`, `processIncomingPackets()`, `sendErrorPacket()`; these are now command-based

- **serialtransfer_slave.cpp** (implementation)
  - Added ASCII line parser: `readAsciiLine()`, `parseCommand()`
  - Changed READY signal from binary packet to ASCII text: `"READY:lux:fall:ts:width:height\n"`
  - Added command receiver loop in main: polls `commandAvailable()`, parses PHOTO command, executes capture
  - Photo chunks still use SerialTransfer binary transmission
  - DONE confirmation now ASCII: `"DONE:status\n"` instead of separate binary packet

- **ESP32CAM_Slave.ino** (main)
  - Removed: Binary `PhotoMetadata` struct instantiation
  - Changed: `setup()` now sends ASCII READY with individual parameters
  - Changed: `loop()` now polls for ASCII commands instead of waiting for ACK
  - Added: Command dispatcher for PHOTO/STAT/PING/RESET commands

### Master Implementation (LilyGo)

- **serialtransfer_master.h** (header)
  - `waitForSlaveReady()` simplified: no longer takes `PhotoMetadata` output parameter
  - Added: `sendPhotoCommand()` (sends ASCII PHOTO command)
  - Removed: `sendSlaveACK()`, `sendSlaveError()`, `processIncomingSlavePackets()`
  - Photo reception uses hybrid approach: ASCII DONE response instead of binary COMPLETE packet

- **serialtransfer_master.cpp** (implementation)
  - Added ASCII response parser: `readAsciiResponse()`
  - Changed: READY parsing from struct unpack to ASCII line parsing with `sscanf()`
  - Added: `sendPhotoCommand()` for text-based image capture trigger
  - Photo chunk reception unchanged (still uses SerialTransfer binary packets)
  - DONE confirmation parsing: changed from binary packet decode to ASCII line parse
  - Slave metadata now stored in simple struct instead of complete `PhotoMetadata`

- **Lilygo+CamESPNOW.ino** (main)
  - Removed: `PhotoMetadata metadata` variable and unpacking
  - Changed: `waitForSlaveReady()` call signature (no output parameter)
  - Changed: `sendSlaveACK()` replaced with `sendPhotoCommand(PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY)`

---

## Protocol Changes at a Glance

### Before (Binary-Only)

```
Master ─[Binary PACKET_ID_ACK]─→ Slave
        ←[Binary PACKET_ID_READY]─ (includes PhotoMetadata struct)
        ─[Binary PACKET_ID_ACK: command=0x00]─→
        ←[Binary PACKET_ID_PHOTO_HEADER]─
        ←[Binary PACKET_ID_PHOTO_CHUNK 0]─
        ←[Binary PACKET_ID_PHOTO_CHUNK 1]─
        ← ...
        ←[Binary PACKET_ID_COMPLETE: status]─
```

### After (Hybrid ASCII + Binary)

```
Master ──[Wait for line]────→ Slave
        ←[ASCII: "READY:..."]─ (plain text with metadata inline)
        ──[ASCII: "PHOTO:..."]─→
        ←[Binary PHOTO_HEADER]─ (SerialTransfer packet)
        ←[Binary PHOTO_CHUNK 0]─ (SerialTransfer packet)
        ←[Binary PHOTO_CHUNK 1]─ (SerialTransfer packet)
        ← ...
        ←[ASCII: "DONE:0"]─ (plain text completion)
```

---

## Key Improvements

| Aspect                 | Before                                  | After                                            |
| ---------------------- | --------------------------------------- | ------------------------------------------------ |
| **Command Debugging**  | Hex binary packets                      | Plain text: "PHOTO:640:480:30"                   |
| **READY Parsing**      | Binary struct unpack                    | Simple `sscanf()` parsing                        |
| **Photo Trigger**      | Binary ACK packet with command field    | ASCII text command                               |
| **Completion Signal**  | Binary COMPLETE packet                  | ASCII "DONE:status"                              |
| **Handshake Overhead** | 3 binary packets (READY, ACK, COMPLETE) | 2 ASCII lines + binary chunks                    |
| **Error Messages**     | Binary ERROR packet                     | ASCII "ERR:code"                                 |
| **Serial Logs**        | Hex debug, less readable                | ASCII debug, highly readable                     |
| **Image Transmission** | Binary chunks via SerialTransfer        | **Unchanged** - binary chunks via SerialTransfer |

---

## Data Structure Removals

These binary structs are **no longer used** (command/response now ASCII):

- ~~`PhotoMetadata`~~ → ASCII "READY:..." format
- ~~`AckPacket`~~ → ASCII "ACK:..." or commands like "PHOTO:..."
- ~~`ErrorPacket`~~ → ASCII "ERR:code" format

These **remain** (image transmission):

- `PhotoHeader` - binary SerialTransfer packet
- `PhotoChunk` - binary SerialTransfer packet

---

## Testing Notes

### What to Verify

1. **ASCII Handshake**
   - Master receives `"READY:..."` line
   - Master sends `"PHOTO:..."` command
   - Slave parses and validates parameters

2. **Photo Transmission**
   - Binary photo chunks still stream correctly
   - Master reassembles into complete JPEG

3. **Error Cases**
   - Invalid PHOTO dimensions → `"ERR:2"` (INVALID_PARAMS)
   - Timeout during transmission → retry or abort gracefully
   - Master send/receive times are reasonable

4. **Diagnostics**
   - Serial output is clean and informative
   - No garbage text in logs
   - Progress indicators work

### Debug Tip

Enable serial monitors on both slave and master:

```cpp
// In Arduino IDE:
// Tools → Serial Monitor (open 2 windows at 115200 baud)
// One window for slave debug output (USB serial)
// One window for master debug output (another USB serial port)
```

Then trigger a PIR wakeup and observe the handshake:

```
[Slave]
[STATUS] Setup complete, waiting for PHOTO command from master...
[CMD] Received command: PHOTO
[CMD] PHOTO command: 640x480 quality=30
[CAM] Photo captured: 52341 bytes
[TX] Starting photo transmission: 52341 bytes, quality=30
...

[Master]
[MASTER-RX] READY received: LUX=250, fallen=0, 640x480
[MASTER-TX] Sending ASCII PHOTO command: PHOTO:640:480:30
[MASTER-RX] PHOTO_HEADER received: size=52341, chunks=212, quality=30
...
[SUCCESS] Photo received: 52341 bytes
```

---

## Function Call Changes

### Slave Side

**Before:**

```cpp
PhotoMetadata metadata = {...};
sendReadyPacket(metadata);
waitForMasterAck(5000);
```

**After:**

```cpp
sendReadyPacket(lux, isFallen, photoWidth, photoHeight, timestamp);

// In loop:
if (commandAvailable()) {
  PhotoCommand cmd;
  if (getCommand(cmd)) {
    // Process PHOTO, STAT, etc.
  }
}
```

### Master Side

**Before:**

```cpp
PhotoMetadata metadata;
waitForSlaveReady(metadata, 10000);
sendSlaveACK();
```

**After:**

```cpp
waitForSlaveReady(10000);  // No output parameter
sendPhotoCommand(640, 480, 30);  // Send capture parameters as ASCII command
```

---

## Backward Compatibility

**NOT compatible** with old binary protocol. Both slave and master must be updated together.

If you need to support both old and new protocols, you'd need to:

1. Detect protocol version (e.g., first byte: ASCII 'R' for READY vs binary 0x01 for old PACKET_ID_READY)
2. Maintain parallel state machines
3. This is not recommended; use one protocol consistently

---

## Next Steps

1. **Test on Hardware**
   - Load updated code on both boards
   - Trigger PIR wakeup
   - Verify photo capture and transmission
   - Check serial logs for errors

2. **Integrate with Existing Features**
   - http_upload module should work unchanged (photo buffer format unchanged)
   - Sensor module should work unchanged (sensor reading still happens on master)
   - SD card module should work unchanged (photo buffer format unchanged)

3. **Optional Enhancements**
   - Add command timeout handling (master PHOTO sends but slave doesn't respond)
   - Add rate limiting (prevent rapid photo spam)
   - Add fallback to STAT command to check slave health
   - Add command queue on slave for multiple photos

---

## Document References

- **SERIAL_PROTOCOL.md** - Complete protocol specification
- **serialtransfer_protocol.h** - Protocol constants and structures
- **serialtransfer_slave.h/cpp** - Slave implementation
- **serialtransfer_master.h/cpp** - Master implementation

---

**Status:** ✅ Refactoring Complete  
**Testing:** Awaiting hardware verification  
**Last Updated:** 2026-03-31
