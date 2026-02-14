# UART Protocol Quick Reference

## Command Format

```
PHOTO:lux:width:height:quality\n
```

## Example Commands

| Environment          | Command                    | Description              |
| -------------------- | -------------------------- | ------------------------ |
| 🌙 Very Dark (Night) | `PHOTO:20:1600:1200:10\n`  | Full flash, high quality |
| 🌆 Dark (Dusk)       | `PHOTO:100:1600:1200:12\n` | 75% flash, high quality  |
| 🏠 Indoor            | `PHOTO:300:1280:1024:15\n` | 50% flash, good quality  |
| ☀️ Bright            | `PHOTO:800:1024:768:20\n`  | No flash, medium quality |
| 🌞 Very Bright       | `PHOTO:2000:640:480:25\n`  | No flash, fast capture   |

## LUX Reference Values

| Environment     | Typical LUX      | Flash Brightness | PWM Value |
| --------------- | ---------------- | ---------------- | --------- |
| Moonless night  | 0.01 - 0.1       | 100%             | 255       |
| Full moon       | 0.1 - 1          | 100%             | 255       |
| Dark indoor     | 10 - 50          | 100%             | 255       |
| Normal indoor   | 100 - 300        | 50-75%           | 128-192   |
| Office lighting | 300 - 500        | 25-50%           | 64-128    |
| Sunrise/sunset  | 400              | 25%              | 64        |
| Overcast day    | 1,000            | OFF              | 0         |
| Full daylight   | 10,000 - 25,000  | OFF              | 0         |
| Direct sunlight | 32,000 - 100,000 | OFF              | 0         |

## Your Use Case: 3D Printed PLA Container

For a **translucent PLA container**, expect these approximate values:

| Outside Condition | Inside Container (estimated) | Suggested Command          |
| ----------------- | ---------------------------- | -------------------------- |
| Night (dark room) | 5-20 LUX                     | `PHOTO:15:1600:1200:10\n`  |
| Indoor lighting   | 50-150 LUX                   | `PHOTO:100:1600:1200:12\n` |
| Near window (day) | 200-400 LUX                  | `PHOTO:300:1280:1024:15\n` |
| Bright room       | 500-800 LUX                  | `PHOTO:600:1024:768:18\n`  |

💡 **Tip:** Test with your actual container to determine the LUX reduction factor!

## Resolution Presets

### High Quality (Best detail)

```cpp
PHOTO:lux:1600:1200:10\n  // UXGA - Large file (~250KB)
```

### Balanced (Good quality, smaller file)

```cpp
PHOTO:lux:1280:1024:15\n  // SXGA - Medium file (~180KB)
```

### Fast Capture (Quick response)

```cpp
PHOTO:lux:800:600:20\n    // SVGA - Small file (~80KB)
```

### Low Storage (Minimal space)

```cpp
PHOTO:lux:640:480:25\n    // VGA - Tiny file (~40KB)
```

## Quality Parameter Guide

| Quality Value | File Size | Use Case            |
| ------------- | --------- | ------------------- |
| 1-10          | Largest   | Scientific/archival |
| 10-20         | Large     | High quality photos |
| 20-30         | Medium    | General purpose     |
| 30-40         | Small     | Web thumbnails      |
| 40-63         | Smallest  | Testing only        |

**Recommended:** Use **10-15** for best balance.

## Response Messages

### Success Response

```
OK:IMG_0001.jpg\n
```

### Error Responses

```
ERROR:SD card failed\n
ERROR:Camera init failed\n
ERROR:Invalid resolution\n
ERROR:Invalid quality\n
ERROR:Capture failed\n
ERROR:Invalid format\n
ERROR:Unknown command\n
```

## Timing Reference

| Phase               | Duration | Configurable?               |
| ------------------- | -------- | --------------------------- |
| Camera boot         | 3000 ms  | Yes (CAM_BOOT_TIME_MS)      |
| Ready timeout       | 5000 ms  | Yes (UART_TIMEOUT_MS)       |
| Response timeout    | 5000 ms  | Yes (UART_TIMEOUT_MS)       |
| Flash stabilization | 200 ms   | Hardcoded                   |
| Max power-on time   | 15000 ms | Yes (CAMERA_ON_DURATION_MS) |

## Flash Brightness Algorithm

Current implementation in `calculateFlashBrightness()`:

```cpp
LUX Range    │ Brightness │ PWM Value
─────────────┼────────────┼──────────
0   -  50    │   100%     │   255
50  - 150    │    75%     │   192
150 - 300    │    50%     │   128
300 - 500    │    25%     │    64
500+         │    OFF     │     0
```

### Customization Template

```cpp
uint8_t calculateFlashBrightness(uint16_t lux) {
  if (lux < YOUR_THRESHOLD_1) return 255;
  else if (lux < YOUR_THRESHOLD_2) return 192;
  else if (lux < YOUR_THRESHOLD_3) return 128;
  else if (lux < YOUR_THRESHOLD_4) return 64;
  else return 0;
}
```

## Baud Rate Settings

**Current:** 115200 baud

**Alternatives:** (if experiencing communication issues)

```cpp
#define UART_BAUD_RATE  9600    // More reliable, slower
#define UART_BAUD_RATE  38400   // Good compromise
#define UART_BAUD_RATE  57600   // Balanced
#define UART_BAUD_RATE  115200  // Fast (current)
#define UART_BAUD_RATE  230400  // Very fast (may be unstable)
```

⚠️ **Important:** Both master and slave must use the **same baud rate**.

## Pin Quick Reference

| Signal        | LilyGo (Master) | ESP32CAM (Slave) |
| ------------- | --------------- | ---------------- |
| TX→RX         | GPIO1 →         | → GPIO16         |
| RX←TX         | GPIO3 ←         | ← GPIO13         |
| Power Control | GPIO23 →        | → PWR_EN         |
| PIR Wakeup    | GPIO32 (input)  | -                |
| Flash LED     | -               | GPIO4 (output)   |
| Ground        | GND ←→          | ←→ GND           |

## Serial Monitor Setup

### Master (LilyGo)

- **Port:** Usually COM3-COM10 (Windows) or /dev/ttyUSB0 (Linux)
- **Baud:** 115200
- **Line Ending:** Newline

### Slave (ESP32CAM)

- **Port:** Different from master
- **Baud:** 115200
- **Line Ending:** Newline

💡 **Tip:** Open two serial monitor windows side-by-side to watch both boards!

## Testing Command Sequence

### Test 1: Basic Communication

```
Expected Master Output:
[UART] Waiting for camera READY signal...
[UART] Camera is READY

Expected Slave Output:
[UART] Sent READY signal to master
```

### Test 2: Photo Capture (Low Light)

```
Send: PHOTO:50:1600:1200:10\n

Expected Slave:
[PARSE] LUX=50, Size=1600x1200, Quality=10
[FLASH] LUX=50 → Brightness=255/255
[CAM] Capturing image...
[SD] SUCCESS: /IMG_0001.jpg saved

Expected Master:
[SUCCESS] Photo saved as: /IMG_0001.jpg
```

### Test 3: Photo Capture (Bright Light)

```
Send: PHOTO:800:800:600:15\n

Expected Slave:
[PARSE] LUX=800, Size=800x600, Quality=15
[FLASH] LUX=800 → Brightness=0/255
[LED] Flash OFF
[CAM] Capturing image...
[SD] SUCCESS: /IMG_0002.jpg saved
```

## Error Conditions

| Error Message       | Cause             | Solution                                |
| ------------------- | ----------------- | --------------------------------------- |
| No READY received   | Camera not booted | Increase CAM_BOOT_TIME_MS               |
| Invalid resolution  | Out of range      | Use 320-1600 (width), 240-1200 (height) |
| SD card failed      | Card missing      | Insert FAT32 formatted SD card          |
| Camera init failed  | Hardware issue    | Check camera ribbon cable               |
| Unexpected response | UART noise        | Check connections, add pullups          |

## Configuration File Locations

### Master Configuration

**File:** `Lilygo+CamUART.ino`

```cpp
// Line ~44-48: Photo parameters
#define PHOTO_WIDTH       1600
#define PHOTO_HEIGHT      1200
#define PHOTO_QUALITY     10

// Line ~51-54: Timing
#define CAM_BOOT_TIME_MS  3000
#define UART_TIMEOUT_MS   5000
```

### Slave Configuration

**File:** `ESP32CAM_Slave.ino`

```cpp
// Line ~35-36: UART pins
#define UART_RX_PIN       16
#define UART_TX_PIN       13

// Line ~39-40: Communication
#define UART_BAUD_RATE    115200

// Line ~209-218: Flash algorithm
uint8_t calculateFlashBrightness(uint16_t lux) {
  // Modify thresholds here
}
```

## Power States

```
┌──────────┐
│  SLEEP   │ ← PIR not triggered (5mA)
└────┬─────┘
     │ PIR motion detected
     ▼
┌──────────┐
│  WAKE    │ ← Reading sensors (100mA)
└────┬─────┘
     │ GPIO23 = HIGH
     ▼
┌──────────┐
│ CAM BOOT │ ← Camera initializing (300mA)
└────┬─────┘
     │ READY received
     ▼
┌──────────┐
│  PHOTO   │ ← Capturing image (400-500mA with flash)
└────┬─────┘
     │ Photo saved
     ▼
┌──────────┐
│ SHUTDOWN │ ← GPIO23 = LOW
└────┬─────┘
     │
     ▼
   SLEEP
```

## Filename Format

Current format: `IMG_xxxx.jpg` where xxxx is a 4-digit counter.

**Customization example** (in ESP32CAM_Slave.ino):

```cpp
// Timestamp format
snprintf(filename, sizeof(filename),
         "/%04d%02d%02d_%02d%02d%02d.jpg",
         year, month, day, hour, minute, second);

// Date + counter format
snprintf(filename, sizeof(filename),
         "/%04d%02d%02d_IMG_%03d.jpg",
         year, month, day, photoCounter);
```

## File Size Estimates

| Resolution | Quality 10 | Quality 15 | Quality 20 | Quality 30 |
| ---------- | ---------- | ---------- | ---------- | ---------- |
| 1600x1200  | ~250 KB    | ~180 KB    | ~130 KB    | ~80 KB     |
| 1280x1024  | ~180 KB    | ~130 KB    | ~95 KB     | ~60 KB     |
| 1024x768   | ~120 KB    | ~85 KB     | ~65 KB     | ~40 KB     |
| 800x600    | ~80 KB     | ~60 KB     | ~45 KB     | ~28 KB     |
| 640x480    | ~40 KB     | ~30 KB     | ~23 KB     | ~15 KB     |

**SD Card Capacity:**

- 1 GB = ~4,000 high-quality photos (1600x1200, Q=10)
- 8 GB = ~32,000 photos
- 32 GB = ~128,000 photos

## Advanced: Batch Commands

Future enhancement idea - send multiple commands:

```
PHOTO:500:1600:1200:10\n
DELAY:2000\n
PHOTO:500:800:600:15\n
```

This would require modifications to the command parser on the slave side.

## Debug Mode

To enable verbose debugging, add to both files:

```cpp
#define DEBUG_MODE 1

#if DEBUG_MODE
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif
```

## Checklist Before First Use

- [ ] Verify all wire connections match wiring diagram
- [ ] Confirm both boards power on independently
- [ ] Check SD card is formatted FAT32 and inserted
- [ ] Verify UART baud rates match (115200)
- [ ] Test PIR sensor triggers GPIO32 correctly
- [ ] Confirm light sensor reads reasonable LUX values
- [ ] Open two serial monitors for both boards
- [ ] Trigger PIR and watch serial output
- [ ] Check SD card for IMG_0001.jpg after test
- [ ] Verify photo quality and flash operation

---

## Quick Command Generator

For your PLA container trap application:

**Daytime (bright ambient):**

```bash
PHOTO:400:1280:1024:15\n
```

**Evening (low light):**

```bash
PHOTO:100:1600:1200:10\n
```

**Night (very dark):**

```bash
PHOTO:20:1600:1200:10\n
```

**Battery saving mode (fast capture):**

```bash
PHOTO:300:640:480:20\n
```

---

**🎯 Remember:** Adjust the LUX values based on actual measurements from your light sensor inside the 3D printed container!
