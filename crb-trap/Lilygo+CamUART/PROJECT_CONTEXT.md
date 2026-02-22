# LilyGo A7670 + ESP32-CAM UART Control System - Complete Context

## 📋 Project Overview

This is a **two-board embedded system** that implements PIR motion-triggered camera capture with light-adaptive flash control. The LilyGo A7670 board (master) controls an ESP32-CAM (slave) via UART to capture photos, with flash brightness automatically adjusted based on ambient light readings.

### Project Goals

- Enable low-power master board to trigger camera captures via PIR motion detection
- Adaptive flash LED brightness based on LUX (ambient light) sensor readings
- Configurable photo resolution and quality settings
- SD card storage on the slave camera board
- Energy-efficient deep sleep modes for extended battery operation

---

## 📁 File Structure & Purposes

### Main Directory: `Lilygo+CamUART/`

```
Lilygo+CamUART/
├── Lilygo+CamUART.ino          # Master controller (LilyGo A7670)
├── utilities.h                  # Hardware pin definitions for LilyGo boards
├── README.md                    # Complete documentation with schematics
├── QUICK_REFERENCE.md           # Quick command examples and LUX tables
├── WIRING_DIAGRAM.md            # Pin connections and signal flow diagrams
├── PROJECT_CONTEXT.md           # This file - AI context document
└── ESP32CAM_Slave/
    ├── ESP32CAM_Slave.ino       # Slave camera controller (ESP32-CAM)
    └── .pio/                    # PlatformIO build artifacts
```

### File Purposes

| File                   | Purpose                   | Key Contents                                                                     |
| ---------------------- | ------------------------- | -------------------------------------------------------------------------------- |
| **Lilygo+CamUART.ino** | Master board firmware     | PIR wakeup, deep sleep, LUX sensor reading, UART commands, camera power control  |
| **ESP32CAM_Slave.ino** | Slave board firmware      | Camera initialization, SD card management, photo capture, UART response handling |
| **utilities.h**        | Pin configuration library | Board-specific GPIO definitions for various LilyGo models                        |
| **README.md**          | Full documentation        | Complete hardware setup, protocol specs, workflow diagrams                       |
| **QUICK_REFERENCE.md** | Developer reference       | Command examples, LUX values, resolution tables, timing reference                |
| **WIRING_DIAGRAM.md**  | Hardware connections      | Detailed pinout tables, signal flow diagrams, physical layout                    |

---

## 🏗️ System Architecture

### Master-Slave Communication Model

```
┌─────────────────────────────────┐
│   MASTER: LilyGo A7670          │
│   (ESP32 + A7670 Modem)         │
│                                 │
│  • PIR motion detection (GPIO32)│
│  • Light sensor reading (I2C)   │
│  • Deep sleep management        │
│  • UART command transmission    │
└────────────┬────────────────────┘
             │
        UART Protocol
        (GPIO18←→GPIO16)
        (GPIO19←→GPIO13)
             │
┌────────────▼────────────────────┐
│   SLAVE: ESP32-CAM              │
│   (Camera Module with SD Card)   │
│                                 │
│  • Camera initialization        │
│  • SD card file management      │
│  • Photo capture & encoding     │
│  • Flash LED control (GPIO4)    │
│  • UART response transmission   │
└─────────────────────────────────┘
```

### System State Machine

**Master (LilyGo) Flow:**

1. **SLEEP** → Waiting for PIR trigger
2. **WAKE** → External interrupt from PIR (GPIO32)
3. **INIT** → Read LUX sensor, power ON camera
4. **WAIT** → Wait for "READY\n" from slave
5. **COMMAND** → Send photo capture command
6. **WAIT** → Wait for "OK:filename" response
7. **SHUTDOWN** → Power OFF camera, release GPIO
8. **SLEEP** → Return to deep sleep

**Slave (ESP32-CAM) Flow:**

1. **BOOT** → Initialize camera & SD card
2. **READY** → Send "READY\n" to master
3. **IDLE** → Wait for UART commands
4. **CAPTURE** → Receive command, parse parameters
5. **PROCESS** → Configure camera, calculate flash brightness
6. **FLASH** → Turn ON flash LED at calculated PWM
7. **SNAP** → Capture image
8. **SAVE** → Write to SD card, turn OFF flash
9. **RESPOND** → Send "OK:filename.jpg\n" to master
10. **IDLE** → Return to waiting state

---

## 🔌 Hardware Configuration

### Pin Mapping

#### Master Board (LilyGo A7670 ESP32)

| GPIO       | Function    | Purpose                      | Notes                            |
| ---------- | ----------- | ---------------------------- | -------------------------------- |
| **GPIO32** | INPUT (PIR) | Motion detection wakeup      | External interrupt source        |
| **GPIO23** | OUTPUT      | Camera power control         | Inverted logic: LOW=ON, HIGH=OFF |
| **GPIO18** | UART TX     | Send commands to slave       | UART2 transmit                   |
| **GPIO19** | UART RX     | Receive responses from slave | UART2 receive                    |
| **GPIO21** | I2C SDA     | Light sensor data line       | For BH1750 or similar            |
| **GPIO22** | I2C SCL     | Light sensor clock line      | For BH1750 or similar            |
| **GND**    | Ground      | Common reference             | Must be connected to slave       |

#### Slave Board (ESP32-CAM AI-Thinker)

| GPIO       | Function     | Purpose                      | Notes                      |
| ---------- | ------------ | ---------------------------- | -------------------------- |
| **GPIO16** | UART RX      | Receive commands from master | UART2 receive              |
| **GPIO13** | UART TX      | Send responses to master     | UART2 transmit             |
| **GPIO4**  | OUTPUT (PWM) | Built-in flash LED control   | 5kHz PWM, 8-bit resolution |
| **GPIO14** | SD CLK       | SD card clock                | 1-bit mode                 |
| **GPIO15** | SD CMD       | SD card command              | 1-bit mode                 |
| **GPIO2**  | SD DATA0     | SD card data line            | 1-bit mode                 |
| **GPIO32** | PWDN         | Camera power down            | Managed by esp_camera lib  |
| **VCC**    | Power        | 5V or 3.3V input             | Check supply voltage       |
| **GND**    | Ground       | Common reference             | Must match master GND      |

### Power Connections

- **Master to Slave:** GPIO23 (LilyGo) → PWR_EN (ESP32-CAM)
- **Current Flow:** Camera draws ~300-500mA during capture
- **Deep Sleep Current:** Master ≈5-10µA, Slave: OFF (0mA)

### Signal Integrity

- **Baud Rate:** 115200 bps
- **UART Cable Length:** Keep <20cm to minimize noise
- **Logic Levels:** All signals are 3.3V (no level shifters needed)
- **Termination:** At high baud rates, consider 100Ω series resistors on TX lines

---

## 📡 Communication Protocol

### UART Format

All messages are **ASCII text with LF termination (\n)**

#### Command: Master → Slave

```
PHOTO:lux:width:height:quality\n
```

**Parameters:**

- `lux` (0-65535): Ambient light intensity in lux units
- `width` (320-1600): Image width in pixels
- `height` (240-1200): Image height in pixels
- `quality` (1-63): JPEG quality where lower = higher quality, larger file

**Examples:**

```
PHOTO:50:1600:1200:10\n   → Very dark conditions, max resolution
PHOTO:500:1600:1200:10\n  → Medium light, high quality
PHOTO:1000:640:480:20\n   → Bright conditions, low quality
```

#### Response: Slave → Master

**Success Response:**

```
OK:image1.jpg\n
```

**Error Response:**

```
ERROR:SD card failed\n
ERROR:Camera init failed\n
ERROR:Invalid resolution\n
ERROR:Invalid quality\n
ERROR:Capture failed\n
```

**Initialization Handshake:**

```
[READY MESSAGE after boot]
READY\n
```

### Protocol Timing

| Phase                        | Duration | Configurable            |
| ---------------------------- | -------- | ----------------------- |
| Master boot to PIR check     | -        | N/A                     |
| PIR settle time              | 2000 ms  | `PIR_SETTLE_TIME_MS`    |
| Camera power-on delay        | 500 ms   | `CAM_BOOT_TIME_MS`      |
| Camera ready timeout         | 10000 ms | `UART_TIMEOUT_MS`       |
| Flash stabilization          | 200 ms   | Hardcoded               |
| Camera power-on max duration | 15000 ms | `CAMERA_ON_DURATION_MS` |
| Command response timeout     | 10000 ms | `UART_TIMEOUT_MS`       |

---

## ⚡ Power Management & Ghost Power Prevention (CRITICAL FIX)

### Problem: PSRAM Errors Only on External Power

When running on external power with MOSFET switching (GPIO23 control), you may see:

```
PSRAM ID read error: 0xffffffff
```

This happens even though PSRAM works perfectly on USB power. **Root cause: backfeeding through UART lines.**

### Why This Happens

When the ESP32-CAM is powered OFF via MOSFET, the UART data lines remain physically connected to the master board. The ESP32-CAM's input protection diodes can conduct current from these UART signals, creating **ghost power** that keeps the module at ~0.8-2V internally:

```
Master GPIO18 (TX) ──┐
Master GPIO19 (RX) ──├─→ [Protection Diodes] ─→ Partial Power
                     │
                    ESP32-CAM GPIO16 (RX)
                    ESP32-CAM GPIO13 (TX)
```

When the MOSFET turns on and applies real power, the partially-powered module boots in a corrupted analog state, causing PSRAM initialization to fail with `0xffffffff`.

**USB power masks this problem** because USB adapters keep the board continuously powered, so the OFF→ON transition never happens.

### The Fix: Disable UART When Camera Powers Off

In **Lilygo+CamUART.ino**, two new functions prevent backfeeding:

#### `uartToCameraEnable()` - Called When Powering ON

```cpp
void uartToCameraEnable() {
  Serial.println("[UART] Enabling UART connection...");
  CameraSerial.end();
  delay(100);
  pinMode(UART_TX_PIN, OUTPUT);     // Drive these pins actively
  pinMode(UART_RX_PIN, INPUT);      // Set to receive
  CameraSerial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("[UART] UART re-initialized and ready");
}
```

#### `uartToCameraDisable()` - Called When Powering OFF

```cpp
void uartToCameraDisable() {
  Serial.println("[UART] Disabling UART connection (floating pins)...");
  CameraSerial.flush();
  CameraSerial.end();
  delay(100);
  pinMode(UART_TX_PIN, INPUT);      // Float to high-Z (cut off backfeeding)
  pinMode(UART_RX_PIN, INPUT);      // Float to high-Z
  Serial.println("[UART] UART disabled and pins floating (no ghost power)");
}
```

#### Updated Power Control Functions

**`powerOnCamera()` now:**

```cpp
void powerOnCamera() {
  digitalWrite(CAM_PWR_EN_PIN, LOW); // Apply power first
  delay(100);
  uartToCameraEnable();              // Then enable UART communication
}
```

**`powerOffCamera()` now:**

```cpp
void powerOffCamera() {
  digitalWrite(CAM_PWR_EN_PIN, HIGH); // Turn OFF power
  delay(100);
  uartToCameraDisable();              // CRITICAL: Float pins BEFORE holding GPIO
  gpio_hold_en((gpio_num_t)CAM_PWR_EN_PIN);
  gpio_deep_sleep_hold_en();
}
```

### Why This Works

By setting UART pins to high-impedance INPUT mode before power-off:

- ✅ Blocks the feedback path through protection diodes
- ✅ Allows the module to power down completely (0V)
- ✅ Ensures clean boot when power is reapplied
- ✅ PSRAM initialization succeeds on next power-on

### Optional Hardware Improvements (Complementary)

For maximum robustness, also add series resistors on UART lines:

| Connection                                 | Resistor  |
| ------------------------------------------ | --------- |
| LilyGo GPIO18 (TX) → ESP32-CAM GPIO16 (RX) | 1k-4.7k Ω |
| ESP32-CAM GPIO13 (TX) → LilyGo GPIO19 (RX) | 1k-4.7k Ω |

These resistors further reduce the feedback current and improve signal integrity at 115200 baud.

### Power Supply Checklist

Ensure your external power supply is stable:

- [ ] 5V supply has 470µF + 10µF + 0.1µF capacitors near ESP32-CAM
- [ ] 3.3V on camera has 10µF + 0.1µF capacitors (if accessible)
- [ ] Power and ground wires are short and thick (18+ AWG)
- [ ] MOSFET can handle peak camera current (~500mA) without voltage sag
- [ ] If using boost converter: monitor output for ripple/noise

### Testing the Fix

After implementing the UART enable/disable functions:

1. **Upload updated code** to LilyGo master
2. **Provide external power** (no USB)
3. **Trigger PIR sensor** or reset the master
4. **Monitor serial output** - should see:
   ```
   [POWER] Turning camera ON (GPIO23 → LOW - inverted logic)
   [UART] Enabling UART connection...
   [UART] UART re-initialized and ready
   [STATUS] Camera powered ON
   ```
5. **Slave should boot cleanly** without PSRAM errors
6. **Photos should save** successfully to SD card

---

## 💡 Flash LED Brightness Algorithm

The flash brightness is automatically calculated based on ambient light (LUX) to avoid overexposure in bright conditions and provide adequate lighting in dark conditions.

### Brightness Lookup Table

| LUX Range | Environment         | Flash Brightness | PWM (0-255) | Use Case                  |
| --------- | ------------------- | ---------------- | ----------- | ------------------------- |
| 0 - 50    | Very dark (night)   | 100%             | 255         | Dark room, night          |
| 50 - 150  | Dark (dusk/evening) | 75%              | 192         | Dusk conditions           |
| 150 - 300 | Dim (indoor)        | 50%              | 128         | Indoor with low light     |
| 300 - 500 | Normal indoor       | 25%              | 64          | Office/room lighting      |
| 500+      | Bright (daylight)   | OFF              | 0           | Daylight, no flash needed |

### Implementation (ESP32CAM_Slave.ino)

```cpp
uint8_t calculateFlashBrightness(uint16_t lux) {
  if (lux < 50) {
    return 255;      // 100% brightness
  } else if (lux < 150) {
    return 192;      // 75% brightness
  } else if (lux < 300) {
    return 128;      // 50% brightness
  } else if (lux < 500) {
    return 64;       // 25% brightness
  } else {
    return 0;        // Flash OFF
  }
}
```

### Customization

To adjust thresholds for your specific use case:

```cpp
// Example: Adjust for translucent PLA container
uint8_t calculateFlashBrightness(uint16_t lux) {
  if (lux < 100) return 255;      // Lower threshold for enclosed spaces
  else if (lux < 200) return 192;
  else if (lux < 400) return 128;
  else if (lux < 700) return 64;
  else return 0;
}
```

---

## 📸 Photo Resolution & Quality Settings

### Supported Resolutions

| Name     | Dimensions  | Frame Size     | Memory | Quality | Use Case                      |
| -------- | ----------- | -------------- | ------ | ------- | ----------------------------- |
| **UXGA** | 1600 × 1200 | FRAMESIZE_UXGA | High   | Best    | High-res archival, scientific |
| **SXGA** | 1280 × 1024 | FRAMESIZE_SXGA | Medium | Good    | General purpose, balanced     |
| **XGA**  | 1024 × 768  | FRAMESIZE_XGA  | Medium | Good    | Default with PSRAM            |
| **SVGA** | 800 × 600   | FRAMESIZE_SVGA | Low    | Fair    | Small file size, fast         |
| **VGA**  | 640 × 480   | FRAMESIZE_VGA  | Low    | Poor    | Testing only                  |

### JPEG Quality Parameter

| Quality Value | Compression | File Size | Use Case                      |
| ------------- | ----------- | --------- | ----------------------------- |
| 1-10          | Minimal     | Largest   | Archival/scientific documents |
| 10-20         | Low         | Large     | Photo-quality images          |
| 20-30         | Moderate    | Medium    | General purpose (recommended) |
| 30-40         | High        | Small     | Web thumbnails                |
| 40-63         | Very High   | Smallest  | Testing/preview only          |

**Recommendation:** Use quality **10-15** for best balance of file size and image quality.

### Memory Constraints

- **With PSRAM:** Can handle up to UXGA (1600×1200) with quality 10-12
- **Without PSRAM:** Limited to VGA (640×480) or SVGA (800×600) with quality 15-20
- **Frame Buffer:** Set to 1 buffer to conserve memory

---

## 🔧 Key Functions & Implementation Details

### Master (Lilygo+CamUART.ino)

#### `powerOnCamera()`

```cpp
void powerOnCamera() {
  digitalWrite(CAM_PWR_EN_PIN, LOW);  // Inverted logic: LOW = ON
}
```

- Powers ON the ESP32-CAM board via GPIO23
- **Note:** Uses inverted logic due to MOSFET driver configuration

#### `powerOffCamera()`

```cpp
void powerOffCamera() {
  digitalWrite(CAM_PWR_EN_PIN, HIGH);  // Inverted logic: HIGH = OFF
  gpio_hold_en((gpio_num_t)CAM_PWR_EN_PIN);
  gpio_deep_sleep_hold_en();
}
```

- Powers OFF the ESP32-CAM board
- Holds GPIO23 state during deep sleep to prevent accidental power-on

#### `waitForCameraReady()`

- Blocks until "READY\n" is received from slave
- Timeout: `UART_TIMEOUT_MS` (default 10 seconds)
- Allows time for camera initialization and SD card mounting

#### `sendPhotoCommand(lux, width, height, quality)`

- Formats and sends PHOTO command to slave
- Waits for response ("OK:" or "ERROR:")
- Returns `true` if successful, `false` otherwise

#### `readLuxSensor()`

- Reads ambient light intensity from I2C sensor
- Default implementation returns simulated value (500 LUX)
- To enable real sensor: uncomment BH1750 code and implement actual reading
- **Future:** Integrate with `#include <BH1750.h>` library

#### `enterDeepSleep()`

- Configures GPIO32 as external interrupt (PIR sensor)
- Calls `esp_deep_sleep_start()` to enter deep sleep
- System wakes only on high signal from PIR sensor

### Slave (ESP32CAM_Slave.ino)

#### `initCamera()`

- Initializes OV2640 camera with AI-THINKER pin configuration
- Disables brownout detector to prevent reset during initialization
- Sets JPEGQ quality and frame size based on PSRAM availability
- Applies sensor adjustments for optimal image quality

#### `initSDCard()`

- Mounts SD_MMC card in 1-bit mode
- Supports MMC, SDSC, and SDHC cards
- Checks card type and reads total capacity
- **Critical:** Must be called BEFORE camera initialization

#### `calculateFlashBrightness(lux)`

- Determines LED brightness (PWM 0-255) based on ambient light
- Uses predefined LUX thresholds
- **Easily customizable** for different use cases

#### `setFlashBrightness(brightness)`

- Configured LED pin with 5kHz PWM frequency
- 8-bit resolution (0-255)
- Uses newer ESP32 Arduino Core 3.x: `ledcAttach()` and `ledcWrite()`

#### `captureAndSavePhoto(lux, width, height, quality)`

- Core photo capture routine
- Steps:
  1. Validate SD card is initialized
  2. Get camera sensor object
  3. Set frame size using `selectFrameSize()`
  4. Set JPEG quality parameter
  5. Calculate flash brightness from LUX
  6. Turn ON flash LED with stabilization delay
  7. Capture image with retry logic (3 attempts)
  8. Turn OFF flash immediately
  9. Generate filename using EEPROM counter
  10. Write to SD card in 1KB chunks to prevent memory overflow
  11. Update EEPROM counter for next photo
  12. Send "OK:filename.jpg\n" response to master

#### `selectFrameSize(width, height)`

- Maps requested dimensions to supported frame sizes
- Returns closest standard resolution
- Prevents camera driver errors from invalid sizes

#### `processCommand(command)`

- Parses incoming UART command
- Validates parameter ranges
- Calls `captureAndSavePhoto()` if valid
- Sends error response if invalid

#### `loop()`

- Continuously monitors UART for incoming commands
- Buffers characters until '\n' is received
- Calls `processCommand()` when complete command arrives
- Prevents buffer overflow with size check

---

## ⚙️ Configuration Constants

### Master Configuration

```cpp
// From Lilygo+CamUART.ino (top of file)

#define PIR_SENSOR_PIN 32           // PIR wakeup GPIO
#define CAM_PWR_EN_PIN 23           // Camera power control
#define UART_TX_PIN 18              // UART2 transmit
#define UART_RX_PIN 19              // UART2 receive
#define I2C_SDA 21                  // Light sensor SDA
#define I2C_SCL 22                  // Light sensor SCL

#define UART_BAUD_RATE 115200       // Serial communication speed
#define UART_TIMEOUT_MS 10000       // Response wait timeout
#define CAM_BOOT_TIME_MS 500        // Power-on stabilization delay

#define PHOTO_WIDTH 1600            // Default image width
#define PHOTO_HEIGHT 1200           // Default image height
#define PHOTO_QUALITY 10            // Default JPEG quality

#define CAMERA_ON_DURATION_MS 15000 // Max power-on time
#define PIR_SETTLE_TIME_MS 2000     // PIR sensor settle time
```

### Slave Configuration

```cpp
// From ESP32CAM_Slave.ino (pin definitions section)

#define UART_BAUD_RATE 115200       // Must match master
#define CMD_BUFFER_SIZE 128         // UART command buffer
#define EEPROM_SIZE 1               // Photo counter (0-255)
#define FLASH_LED_PIN 4             // Built-in flash LED

// AI-THINKER ESP32-CAM camera pins
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
// ... (see complete pinout in ESP32CAM_Slave.ino)
```

---

## 🛠️ Development Workflow

### Build Environment

- **Master:** Arduino IDE + ESP32 Board Support (v2.x+)
- **Slave:** Arduino IDE + ESP32 Board Support (v2.x+)
- **Alternative:** PlatformIO with platform-io.ini configuration

### Upload Process

**Master Board:**

1. Connect LilyGo A7670 via USB
2. Select Board: "ESP32 Dev Module" or specific LilyGo model
3. Select COM port and 115200 baud rate
4. Upload Lilygo+CamUART.ino

**Slave Board:**

1. Connect ESP32-CAM via USB-Serial adapter (CH340G, etc.)
2. **IMPORTANT:** Disconnect GPIO16/GPIO13 UART wires during upload
3. Select Board: "AI Thinker ESP32-CAM"
4. Put camera in download mode: Short GPIO0 to GND
5. Upload ESP32CAM_Slave.ino
6. Reconnect UART wires after successful upload

### Debugging

**Monitor Master Output:**

```
Port: COM3 (or /dev/ttyUSB0)
Baud: 115200
Line Ending: Newline (\n)
```

**Monitor Slave Output:**

```
Port: COM4 (or /dev/ttyUSB1)
Baud: 115200
Line Ending: Newline (\n)
```

**Recommended:** Open two serial monitor windows side-by-side for simultaneous monitoring.

---

## 📊 LUX Reference Values

### Common Environments

| Environment             | LUX Range        | Recommended Command     |
| ----------------------- | ---------------- | ----------------------- |
| Moonless night          | 0.01 - 0.1       | PHOTO:10:1600:1200:10   |
| Full moon               | 0.1 - 1          | PHOTO:10:1600:1200:10   |
| Dark indoor (night)     | 10 - 50          | PHOTO:30:1600:1200:10   |
| Normal indoor (evening) | 100 - 300        | PHOTO:200:1280:1024:12  |
| Office lighting         | 300 - 500        | PHOTO:400:1024:768:15   |
| Sunrise/sunset          | 400              | PHOTO:400:1024:768:15   |
| Overcast day            | 1,000            | PHOTO:1000:800:600:18   |
| Full daylight           | 10,000 - 25,000  | PHOTO:15000:640:480:20  |
| Direct sunlight         | 32,000 - 100,000 | PHOTO:100000:640:480:25 |

### Translucent Container Adjustment

For a translucent PLA housing, light transmission is approximately 30-50% of external values:

| Outside Condition | Estimated Interior LUX | Suggested Command      |
| ----------------- | ---------------------- | ---------------------- |
| Night (dark room) | 5-20                   | PHOTO:15:1600:1200:10  |
| Indoor lighting   | 50-150                 | PHOTO:100:1600:1200:12 |
| Near window (day) | 200-400                | PHOTO:300:1280:1024:15 |
| Bright room       | 500-800                | PHOTO:600:1024:768:18  |

**Tip:** Test with your actual container to determine the specific light reduction factor.

---

## 🔍 Troubleshooting

### Common Issues

#### Camera Not Responding (Timeout)

**Symptom:** Master prints "ERROR: Camera READY timeout!"

**Causes & Solutions:**

1. **Check UART connections:** Verify GPIO18↔GPIO16 and GPIO19↔GPIO13 are connected correctly
2. **Baud rate mismatch:** Ensure BOTH boards use 115200 baud
3. **Loose wires:** Try shorting wires and checking connections with multimeter
4. **Timing issue:** Increase `CAM_BOOT_TIME_MS` from 500 to 1000-2000
5. **Slave upload failed:** Re-upload ESP32CAM_Slave.ino and verify success

#### Photos Not Saving

**Symptom:** "OK:image1.jpg" received but file doesn't exist

**Causes & Solutions:**

1. **SD card not mounted:** Check SD_MMC.begin() returns true
2. **Corrupted SD card:** Format to FAT32 and retry
3. **Fill file system:** Check available space on card (need 1-5MB per photo)
4. **Permission issue:** Verify `/` directory is writable
5. **EEPROM corruption:** Reset EEPROM.write(0, 1) to restart counter

#### Flash LED Not Lighting

**Symptom:** Photos are dark despite flash LED instructions

**Causes & Solutions:**

1. **Wrong LED pin:** Verify GPIO4 is open-drain compatible
2. **Inverted logic:** Check pin configuration (Flash shares pins with other peripherals)
3. **LED hardware:** Test LED with `digitalWrite(4, HIGH)` in simple sketch
4. **Power supply:** Ensure 3.3V supply can handle LED current (~50-100mA)
5. **PWM conflict:** Check GPIO4 isn't used by other drivers

#### Master Won't Wake From Deep Sleep

**Symptom:** PIR sensor triggered but ESP32 not waking

**Causes & Solutions:**

1. **PIR not configured correctly:** Verify `gpio_pulldown_en()` and `esp_sleep_enable_ext0_wakeup()`
2. **Low-to-high trigger:** Ensure PIR signal goes HIGH when motion detected
3. **GPIO32 in use:** Check GPIO32 isn't claimed by SDIO/other peripherals
4. **Deep sleep hang:** Make sure PIR settle time doesn't block startup

---

## 📋 Testing Checklist

### Hardware Verification

- [ ] GPIO18 → GPIO16 connected and soldered
- [ ] GPIO19 → GPIO13 connected and soldered
- [ ] GPIO23 → PWR_EN connected and soldered
- [ ] GPIO32 → PIR sensor connected (with pull-down)
- [ ] GND connected between both boards
- [ ] Power supply voltage correct for both boards
- [ ] SD card inserted and formatted FAT32
- [ ] Flash LED connection verified

### Software Verification

- [ ] Both boards compile without errors
- [ ] UART baud rates match (115200)
- [ ] Board selections correct in Arduino IDE
- [ ] USB drivers installed (CH340G for ESP32-CAM)
- [ ] No COM port conflicts

### Functional Testing

- [ ] Master serial monitor shows boot messages
- [ ] Slave serial monitor shows SD card mounted
- [ ] Master sends PHOTO command manually via serial
- [ ] Slave receives command and responds OK:image1.jpg
- [ ] Image file exists on SD card and is readable
- [ ] Flash LED brightness varies with LUX values
- [ ] Master enters deep sleep after photo
- [ ] PIR triggers wakeup correctly

---

## 📚 Related Resources

### Documentation Files

- **README.md** - Complete hardware setup guide with workflow diagrams
- **QUICK_REFERENCE.md** - Command syntax, examples, and timing tables
- **WIRING_DIAGRAM.md** - Detailed pin connections and physical layout

### External Libraries Used

- **esp_camera** (built-in) - Camera control
- **SD_MMC** (built-in) - SD card access
- **Wire** (built-in) - I2C communication
- **EEPROM** (built-in) - Persistent photo counter
- **BH1750** (optional) - Light sensor driver

### Datasheets Referenced

- OV2640 camera sensor
- ESP32 technical reference manual
- AI-THINKER ESP32-CAM pinout
- LilyGo A7670 schematic
- BH1750 light sensor (if used)

---

## 🎯 Quick Start Summary

### Hardware Setup (5 minutes)

1. Connect UART pins: GPIO18/19 (master) ↔ GPIO16/13 (slave)
2. Connect power control: GPIO23 (master) → PWR_EN (slave)
3. Connect PIR sensor to GPIO32 on master
4. Insert SD card into slave camera
5. Connect GND between both boards

### Software Setup (10 minutes)

1. Upload Lilygo+CamUART.ino to master board
2. Upload ESP32CAM_Slave.ino to slave board
3. Open two serial monitors (115200 baud, newline ending)
4. Watch for "READY\n" from slave

### First Test (5 minutes)

1. Manually send: `PHOTO:500:1600:1200:10` to slave via serial
2. Observe slave capture and save photo
3. Check SD card for image file
4. Trigger master's PIR sensor to test full workflow

### Function & Customize (30 minutes)

1. Adjust flash brightness thresholds in `calculateFlashBrightness()`
2. Connect real BH1750 light sensor to I2C bus
3. Uncomment BH1750 initialization code
4. Implement `readLuxSensor()` to read real light values
5. Test complete system with motion detection

---

## 📝 Code Style & Conventions

- **Comments:** Comprehensive block comments for functions with `@brief`, `@param`, `@return`
- **Naming:** UPPER_CASE_WITH_UNDERSCORES for constants, camelCase for functions
- **Formatting:** 4-space indentation, K&R brace style
- **Error Handling:** Serial output with [TAG] prefix (e.g., "[UART]", "[CAM]", "[ERROR]")
- **Logging Levels:** STATUS, UART, SENSOR, GPIO, POWER indicate operation source

---

**Document Generated:** February 21, 2026  
**System Version:** 1.0  
**Last Updated:** Complete code review and documentation
