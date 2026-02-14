# LilyGo A7670 + ESP32-CAM UART Control System

Complete UART communication system for controlling ESP32-CAM from LilyGo A7670 master board with PIR motion detection and adaptive flash lighting.

## 📋 Overview

This system allows the LilyGo A7670 (master) to control an ESP32-CAM (slave) via UART to capture photos with adaptive flash brightness based on ambient light conditions.

### Key Features

- ✅ PIR motion-triggered camera activation
- ✅ UART command-based photo capture
- ✅ Light-adaptive flash LED control (based on LUX sensor)
- ✅ Configurable photo resolution and quality
- ✅ Handshake protocol with acknowledgments
- ✅ SD card storage on ESP32-CAM
- ✅ Low power deep sleep mode on master
- ✅ Complete error handling and logging

---

## 🔌 Hardware Connections

### Pin Connections Table

| **LilyGo A7670** | **Function**  | **ESP32-CAM** | **Notes**                   |
| ---------------- | ------------- | ------------- | --------------------------- |
| GPIO1 (TX)       | UART TX       | GPIO16 (RX)   | Master → Slave data         |
| GPIO3 (RX)       | UART RX       | GPIO13 (TX)   | Slave → Master data         |
| GPIO23           | Power Control | Power Enable  | HIGH = Camera ON            |
| GPIO32           | PIR Sensor    | -             | Wakeup trigger              |
| GND              | Ground        | GND           | Common ground               |
| 3.3V/5V          | Power         | VCC           | Check voltage compatibility |

### Additional Hardware

**On LilyGo A7670:**

- PIR motion sensor connected to GPIO32
- I2C Light sensor (e.g., BH1750) on GPIO21 (SDA) and GPIO22 (SCL)

**On ESP32-CAM:**

- SD card inserted in slot (required for photo storage)
- Flash LED already built-in on GPIO4

---

## 📡 Communication Protocol

### Command Format

```
PHOTO:lux:width:height:quality\n
```

### Parameters

- **lux**: Light intensity value (0-65535 LUX)
- **width**: Image width in pixels (320-1600)
- **height**: Image height in pixels (240-1200)
- **quality**: JPEG quality (1-63, lower = higher quality)

### Example Commands

```
PHOTO:500:1600:1200:10\n    → Medium light, UXGA resolution, high quality
PHOTO:50:800:600:15\n       → Dark environment, SVGA resolution, medium quality
PHOTO:1000:640:480:20\n     → Bright light, VGA resolution, lower quality
```

### Response Messages

- `READY\n` - Camera initialized and ready for commands
- `OK:IMG_0001.jpg\n` - Photo captured successfully
- `ERROR:message\n` - Error occurred with description

---

## 💡 Flash LED Brightness Algorithm

The flash LED brightness is automatically calculated based on ambient light (LUX):

| **LUX Range** | **Condition** | **Flash Brightness** | **PWM Value** |
| ------------- | ------------- | -------------------- | ------------- |
| 0 - 50        | Very Dark     | 100%                 | 255           |
| 50 - 150      | Dark          | 75%                  | 192           |
| 150 - 300     | Dim           | 50%                  | 128           |
| 300 - 500     | Indoor        | 25%                  | 64            |
| 500+          | Bright        | OFF                  | 0             |

### Customization

Modify the `calculateFlashBrightness()` function in `ESP32CAM_Slave.ino` to adjust thresholds:

```cpp
uint8_t calculateFlashBrightness(uint16_t lux) {
  if (lux < 50) return 255;      // Adjust threshold here
  else if (lux < 150) return 192; // And here
  // ... etc
}
```

---

## 📸 Supported Photo Resolutions

| **Resolution** | **Dimensions** | **Frame Size** | **Use Case**             |
| -------------- | -------------- | -------------- | ------------------------ |
| UXGA           | 1600 × 1200    | FRAMESIZE_UXGA | High quality, large file |
| SXGA           | 1280 × 1024    | FRAMESIZE_SXGA | Good balance             |
| XGA            | 1024 × 768     | FRAMESIZE_XGA  | Medium quality           |
| SVGA           | 800 × 600      | FRAMESIZE_SVGA | Fast capture             |
| VGA            | 640 × 480      | FRAMESIZE_VGA  | Small file size          |

**Note:** UXGA requires PSRAM. Without PSRAM, SVGA or lower is recommended.

---

## 🔄 System Workflow

```
┌─────────────────────────────────────────────────────────┐
│                    MASTER (LilyGo)                      │
├─────────────────────────────────────────────────────────┤
│ 1. Deep Sleep Mode (waiting for PIR trigger)           │
│         ↓ [PIR detects motion on GPIO32]                │
│ 2. Wake Up from Deep Sleep                             │
│ 3. Read LUX sensor value                               │
│ 4. Power ON ESP32-CAM (GPIO23 = HIGH)                  │
│ 5. Wait 3 seconds for camera boot                      │
│ 6. Wait for "READY\n" from camera                      │
│ 7. Send command: "PHOTO:lux:w:h:q\n"                   │
│ 8. Wait for "OK:filename\n" or "ERROR:msg\n"           │
│ 9. Power OFF camera (GPIO23 = LOW)                     │
│ 10. Return to Deep Sleep                               │
└─────────────────────────────────────────────────────────┘
                            ↕ UART
┌─────────────────────────────────────────────────────────┐
│                    SLAVE (ESP32-CAM)                    │
├─────────────────────────────────────────────────────────┤
│ 1. Power ON (receives power from master)               │
│ 2. Initialize Camera & SD Card                         │
│ 3. Send "READY\n" to master                            │
│ 4. Wait for command on UART                            │
│ 5. Receive: "PHOTO:lux:w:h:q\n"                        │
│ 6. Calculate flash brightness from LUX                 │
│ 7. Configure camera (resolution, quality)              │
│ 8. Turn ON flash LED at calculated brightness          │
│ 9. Capture photo                                       │
│ 10. Turn OFF flash LED                                 │
│ 11. Save image to SD card                              │
│ 12. Send "OK:IMG_xxxx.jpg\n" to master                 │
│ 13. Power OFF (master cuts power)                      │
└─────────────────────────────────────────────────────────┘
```

---

## 🛠️ Installation & Setup

### Step 1: Hardware Assembly

1. Connect all pins according to the connection table above
2. Insert SD card into ESP32-CAM slot
3. Connect PIR sensor to LilyGo GPIO32
4. (Optional) Connect I2C light sensor to LilyGo

### Step 2: ESP32-CAM Configuration (Slave)

1. Open `ESP32CAM_Slave.ino` in Arduino IDE
2. Select board: **AI Thinker ESP32-CAM**
3. Install required libraries:
   ```
   - ESP32 Board Support
   - SD_MMC (built-in)
   - esp_camera (built-in)
   ```
4. **IMPORTANT:** Disconnect GPIO1/GPIO3 UART connection temporarily for upload
5. Upload the sketch via USB-Serial adapter
6. Reconnect UART after upload

### Step 3: LilyGo A7670 Configuration (Master)

1. Open `Lilygo+CamUART.ino` in Arduino IDE
2. Select board: **ESP32 Dev Module** or your specific LilyGo board
3. Install required libraries:
   ```
   - ESP32 Board Support
   - Wire (built-in)
   - (Optional) BH1750 library for light sensor
   ```
4. If using BH1750 light sensor, uncomment these lines in the code:
   ```cpp
   #include <BH1750.h>
   BH1750 lightMeter;
   ```
5. Update `readLuxSensor()` function with your sensor code
6. Upload the sketch

### Step 4: Light Sensor Integration (Optional but Recommended)

If using **BH1750** light sensor:

```cpp
// In setup():
#include <BH1750.h>
BH1750 lightMeter;

if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
  Serial.println("[I2C] Light sensor initialized");
}

// In readLuxSensor():
uint16_t readLuxSensor() {
  if (lightMeter.measurementReady()) {
    return (uint16_t)lightMeter.readLightLevel();
  }
  return 500; // Default fallback
}
```

---

## 🧪 Testing

### Test 1: Power Control

```cpp
// Both boards: Monitor serial output
// Expected: Camera powers ON, LED flashes, powers OFF
```

### Test 2: UART Communication

```cpp
// Master serial: "Sent READY signal"
// Slave serial: "Received: PHOTO:..."
```

### Test 3: Photo Capture

1. Trigger PIR sensor
2. Check LilyGo serial output for status messages
3. Check ESP32-CAM SD card for `IMG_0001.jpg`

### Test 4: Flash Brightness

Test different LUX values:

```cpp
// Manually modify readLuxSensor() to return test values:
return 30;   // Should give full brightness (255)
return 200;  // Should give medium (128)
return 600;  // Should give no flash (0)
```

---

## 📝 Configuration Options

### Master (Lilygo+CamUART.ino)

```cpp
// Photo settings
#define PHOTO_WIDTH       1600   // Change resolution
#define PHOTO_HEIGHT      1200
#define PHOTO_QUALITY     10     // 1-63 (lower = better)

// Timing
#define CAM_BOOT_TIME_MS  3000   // Camera boot wait time
#define UART_TIMEOUT_MS   5000   // Response timeout
#define CAMERA_ON_DURATION_MS 15000  // Max power-on time

// UART
#define UART_BAUD_RATE    115200 // Match with slave
```

### Slave (ESP32CAM_Slave.ino)

```cpp
// UART
#define UART_BAUD_RATE    115200 // Must match master

// Flash LED adjustment
uint8_t calculateFlashBrightness(uint16_t lux) {
  // Customize thresholds here
}

// Camera settings (in initCamera())
config.jpeg_quality = 10;  // Default quality
config.frame_size = FRAMESIZE_UXGA;  // Default size
```

---

## 🐛 Troubleshooting

### Problem: Camera doesn't respond with "READY"

**Solutions:**

- Check UART connections (TX→RX, RX→TX)
- Verify baud rate matches on both sides (115200)
- Ensure camera has power via GPIO23
- Increase `CAM_BOOT_TIME_MS` to 5000ms
- Check ESP32-CAM USB serial output for errors

### Problem: Photos are too dark/bright

**Solutions:**

- Verify LUX sensor is working correctly
- Check `calculateFlashBrightness()` thresholds
- Test with manual LUX values first
- Adjust camera exposure settings in `initCamera()`

### Problem: SD card mount failed

**Solutions:**

- Reformat SD card as FAT32
- Check SD card is fully inserted
- Try different SD card (Class 10 recommended)
- Verify 1-bit SD mode: `SD_MMC.begin("/sdcard", true)`

### Problem: UART communication errors

**Solutions:**

- Add pullup resistors (4.7kΩ) on UART lines if needed
- Check for loose connections
- Reduce baud rate to 9600 for testing
- Monitor both serial outputs simultaneously

### Problem: ESP32-CAM won't program

**Solutions:**

- Disconnect GPIO1/GPIO3 from LilyGo before uploading
- Pull GPIO0 to GND during reset for boot mode
- Use external 5V power supply (not just USB)

---

## 📊 Serial Monitor Output Examples

### Master (LilyGo) Output:

```
===========================================
LILYGO A7670 - Camera UART Master
===========================================
[WAKEUP] External signal (PIR Sensor) via RTC_IO
[EVENT] Motion detected by PIR sensor!
[SENSOR] Current light intensity: 280 LUX
[POWER] Turning camera ON (GPIO23 → HIGH)
[STATUS] Camera powered ON
[WAIT] Waiting 3000 ms for camera boot...
[UART] Waiting for camera READY signal...
[UART] Camera is READY
[UART] Sending command: PHOTO:280:1600:1200:10
[UART] Waiting for confirmation...
[SUCCESS] Photo saved as: /IMG_0001.jpg
[POWER] Turning camera OFF (GPIO23 → LOW)
[SLEEP] Entering deep sleep mode...
===========================================
```

### Slave (ESP32-CAM) Output:

```
===========================================
ESP32-CAM UART Slave
===========================================
[UART] Initialized on RX:16 TX:13 @ 115200 baud
[SD] Initializing SD card...
[SD] Card Type: SDHC
[SD] Card Size: 32768MB
[SD] SD card initialized successfully
[CAM] Initializing camera...
[CAM] PSRAM found - using high quality settings
[CAM] Camera initialized successfully
[UART] Sent READY signal to master
[STATUS] Waiting for commands...
===========================================

[UART] Received: PHOTO:280:1600:1200:10
[PARSE] LUX=280, Size=1600x1200, Quality=10
[CAM] Frame size set to 1600x1200
[CAM] JPEG quality set to 10
[FLASH] LUX=280 → Brightness=128/255
[LED] Flash brightness set to 128/255
[CAM] Capturing image...
[CAM] Image captured: 245780 bytes
[SD] Saving to /IMG_0001.jpg...
[SD] SUCCESS: /IMG_0001.jpg saved (245780 bytes)
[UART] Sent: OK:/IMG_0001.jpg
```

---

## 🔧 Customization Ideas

### 1. Add Timestamp to Photos

```cpp
// Include RTC library and format filename with timestamp
snprintf(filename, sizeof(filename), "/%04d%02d%02d_%02d%02d%02d.jpg",
         year, month, day, hour, minute, second);
```

### 2. Implement Retry Logic

```cpp
// In master code
for (int retry = 0; retry < 3; retry++) {
  if (sendPhotoCommand(...)) break;
  delay(1000);
}
```

### 3. Add Multiple Photo Modes

```cpp
// Send different commands:
"PHOTO:500:1600:1200:10\n"   // High quality
"TIMELAPSE:500:5:800:600\n"  // 5 photos in sequence
"VIDEO:500:10\n"             // 10 second video
```

### 4. Battery Monitoring

```cpp
// Add battery voltage reading before deep sleep
float batteryVoltage = analogRead(35) * (3.3 / 4095.0) * 2;
Serial.printf("[BATTERY] %.2fV\n", batteryVoltage);
```

---

## 📚 Files in This Project

```
Lilygo+CamUART/
├── Lilygo+CamUART.ino      ← Master (LilyGo A7670)
├── ESP32CAM_Slave.ino      ← Slave (ESP32-CAM)
├── utilities.h              ← Basic board definitions
└── README.md               ← This file
```

---

## 🎯 Tested Configurations

| **Component** | **Model**            | **Status**     |
| ------------- | -------------------- | -------------- |
| Master Board  | LilyGo T-A7670       | ✅ Tested      |
| Slave Board   | AI-Thinker ESP32-CAM | ✅ Tested      |
| Camera        | OV2640               | ✅ Working     |
| Light Sensor  | BH1750               | ✅ Optional    |
| SD Card       | 32GB Class 10        | ✅ Recommended |
| PIR Sensor    | HC-SR501             | ✅ Tested      |

---

## 📄 License

MIT License - Feel free to modify and distribute

---

## 🤝 Support

For issues or questions:

1. Check the Troubleshooting section
2. Verify all hardware connections
3. Review serial monitor output from both boards
4. Test each component independently

---

## 🎉 Quick Start Checklist

- [ ] Hardware connected per wiring diagram
- [ ] SD card formatted (FAT32) and inserted in ESP32-CAM
- [ ] ESP32-CAM code uploaded (UART disconnected during upload)
- [ ] LilyGo master code uploaded
- [ ] PIR sensor connected to GPIO32
- [ ] Light sensor connected (optional)
- [ ] Both boards powered and serial monitors open
- [ ] Trigger PIR sensor and verify photo captured
- [ ] Check SD card for IMG_0001.jpg file

**You're ready to go! 🚀**
