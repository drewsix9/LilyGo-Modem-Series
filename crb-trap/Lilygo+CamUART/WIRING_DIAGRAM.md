# Wiring Diagram - LilyGo A7670 + ESP32-CAM UART System

## Complete Connection Diagram

```
┌─────────────────────────────────────────┐
│      LilyGo A7670 (Master)              │
│                                         │
│  ┌────────────────────────────────┐    │
│  │         ESP32                  │    │
│  │                                │    │
│  │  GPIO32 ◄─── PIR Sensor        │    │
│  │  GPIO23 ───► Camera Power EN   │────┼────► Power Control
│  │  GPIO1  ───► UART TX           │────┼────► Data to Slave
│  │  GPIO3  ◄─── UART RX           │◄───┼───── Data from Slave
│  │  GPIO21 ───┐                   │    │
│  │  GPIO22 ───┤ I2C Light Sensor  │    │
│  │            └                   │    │
│  │  GND    ─────────────────      │────┼────► Common Ground
│  │  3V3    ─────────────────      │    │
│  └────────────────────────────────┘    │
└─────────────────────────────────────────┘
                │     │      │
                │     │      │
                │     │      ├─────────────────────┐
                │     │      │                     │
                │     │      └──────────┐          │
                │     │                 │          │
                │     └─────────┐       │          │
                │               │       │          │
┌───────────────┼───────────────┼───────┼──────────┼────────┐
│               │               │       │          │        │
│               Power           TX      RX         GND      │
│                 │             │       │          │        │
│  ┌──────────────▼─────────────▼───────▼──────────▼─────┐ │
│  │           ESP32-CAM (Slave)                         │ │
│  │                                                     │ │
│  │   PWR_EN ◄─── (controlled by GPIO23)               │ │
│  │   GPIO16 ◄─── UART RX (from LilyGo TX/GPIO1)       │ │
│  │   GPIO13 ───► UART TX (to LilyGo RX/GPIO3)         │ │
│  │   GPIO4  ───► Flash LED (built-in)                 │ │
│  │   GPIO14 ───┐                                      │ │
│  │   GPIO15 ───┤                                      │ │
│  │   GPIO2  ───┤ SD Card (1-bit mode)                 │ │
│  │             └                                      │ │
│  │   GND    ────────────────                          │ │
│  │   VCC    ────────────────                          │ │
│  │                                                     │ │
│  │   [OV2640 Camera]                                  │ │
│  │   [SD Card Slot]                                   │ │
│  └─────────────────────────────────────────────────────┘ │
│                                                           │
│              ESP32-CAM Board                              │
└───────────────────────────────────────────────────────────┘
```

## Detailed Pin Mapping

### Master (LilyGo A7670) Pinout

| Pin        | Function | Connected To     | Description                  |
| ---------- | -------- | ---------------- | ---------------------------- |
| **GPIO32** | INPUT    | PIR Sensor OUT   | Motion detection wakeup      |
| **GPIO23** | OUTPUT   | Power Control    | HIGH = Camera ON, LOW = OFF  |
| **GPIO1**  | UART TX  | ESP32CAM GPIO16  | Transmit commands to slave   |
| **GPIO3**  | UART RX  | ESP32CAM GPIO13  | Receive responses from slave |
| **GPIO21** | I2C SDA  | Light Sensor SDA | Read ambient light level     |
| **GPIO22** | I2C SCL  | Light Sensor SCL | I2C clock for sensor         |
| **GND**    | Ground   | Common GND       | Shared ground                |
| **3V3**    | Power    | Components       | Power for sensors            |

### Slave (ESP32-CAM) Pinout

| Pin        | Function      | Connected To  | Description                   |
| ---------- | ------------- | ------------- | ----------------------------- |
| **GPIO16** | UART RX       | LilyGo GPIO1  | Receive commands from master  |
| **GPIO13** | UART TX       | LilyGo GPIO3  | Send responses to master      |
| **GPIO4**  | OUTPUT        | Flash LED     | Built-in LED (PWM controlled) |
| **GPIO14** | SD CLK        | SD Card       | SD card clock                 |
| **GPIO15** | SD CMD        | SD Card       | SD card command               |
| **GPIO2**  | SD DATA0      | SD Card       | SD card data (1-bit mode)     |
| **GND**    | Ground        | Common GND    | Shared ground                 |
| **VCC**    | Power         | Power Source  | 5V or 3.3V (check spec)       |
| **PWR_EN** | Power Control | LilyGo GPIO23 | Controlled by master          |

## Physical Layout Example

```
     PIR Sensor               Light Sensor (BH1750)
         │                          │
         │ (Signal)           (SDA) │ (SCL)
         │                          │
         └──────┐          ┌────────┴────────┐
                │          │                 │
         ┌──────▼──────────▼─────────────────▼────┐
         │                                         │
         │        LilyGo A7670 Master Board        │
         │                                         │
         │  [ESP32]  [A7670 Modem]  [Antenna]     │
         │                                         │
         └──┬─────┬──────┬──────────────────┬─────┘
            │     │      │                  │
         (PWR) (TX)   (RX)               (GND)
            │     │      │                  │
            │     │      │                  │
            │     └──────┼─────────┐        │
            │            │         │        │
            └────────────┼─────┐   │        │
                         │     │   │        │
         ┌───────────────▼─────▼───▼────────▼─────┐
         │       ESP32-CAM Slave Board             │
         │                                         │
         │  [ESP32]  [OV2640 Camera]  [Flash LED] │
         │          [SD Card Slot]                 │
         │                                         │
         └─────────────────────────────────────────┘
```

## Signal Flow Diagram

```
POWER UP SEQUENCE:
══════════════════

PIR Sensor   ──┐
               │ Motion Detected
               ▼
LilyGo   [SLEEP] ──► [WAKE] ──► Read LUX ──► GPIO23=HIGH
                                                    │
                                                    ▼
ESP32CAM         ───────────────► [POWER ON] ──► Init Camera/SD
                                                    │
                                                    ▼
LilyGo           ◄──────────────── "READY\n"
    │
    └──► "PHOTO:500:1600:1200:10\n" ──►
                                        │
                                        ▼
ESP32CAM                          Process Command
                                        │
                                        ├──► Calculate Flash (LUX=500)
                                        ├──► Set Resolution (1600x1200)
                                        ├──► Set Quality (10)
                                        ├──► Flash ON (brightness)
                                        ├──► Capture Photo
                                        ├──► Flash OFF
                                        ├──► Save to SD
                                        │
                                        ▼
                                 "OK:IMG_0001.jpg\n" ──►
                                                        │
                                                        ▼
LilyGo      [Photo Confirmed] ──► GPIO23=LOW ──► [DEEP SLEEP]
                                        │
                                        ▼
ESP32CAM                          [POWER OFF]


POWER DOWN SEQUENCE:
════════════════════

LilyGo          GPIO23 = LOW
                    │
                    ▼
ESP32CAM    [Power Removed] ──► All circuits OFF
                    │
                    ▼
LilyGo      [Configure PIR Wakeup] ──► Deep Sleep ──► [SLEEP MODE]
                                                            │
                                                Waiting for PIR
```

## Cable Requirements

| Cable | Length  | Type         | Notes                      |
| ----- | ------- | ------------ | -------------------------- |
| TX→RX | 10-20cm | Jumper wire  | Keep short to reduce noise |
| RX→TX | 10-20cm | Jumper wire  | Keep short to reduce noise |
| Power | 10-20cm | Thicker wire | Handle current for camera  |
| GND   | 10-20cm | Any gauge    | Common ground essential    |

## Level Shifting (if needed)

Both LilyGo (ESP32) and ESP32-CAM operate at **3.3V logic**, so **no level shifters are required**.

⚠️ **Warning:** Never connect 5V signals directly to ESP32 pins!

## Testing Points

For troubleshooting, you can measure these signals:

| Test Point           | Expected Voltage     | Description                        |
| -------------------- | -------------------- | ---------------------------------- |
| LilyGo GPIO23        | 3.3V (ON) / 0V (OFF) | Camera power control               |
| LilyGo GPIO1 (TX)    | ~1.65V idle          | UART transmit (varies during data) |
| ESP32CAM GPIO16 (RX) | ~1.65V idle          | Should match TX when connected     |
| ESP32CAM VCC         | 5V or 3.3V           | Check your power supply            |
| Flash LED (GPIO4)    | 0-3.3V PWM           | Varies with brightness setting     |

## Power Consumption Notes

| State              | LilyGo Current | ESP32CAM Current | Total  |
| ------------------ | -------------- | ---------------- | ------ |
| Deep Sleep         | ~5mA           | OFF (0mA)        | ~5mA   |
| Active (no camera) | ~100mA         | OFF              | ~100mA |
| Camera boot        | ~100mA         | ~200mA           | ~300mA |
| Photo capture      | ~100mA         | ~300mA           | ~400mA |
| Flash ON           | ~100mA         | ~400mA           | ~500mA |

💡 **Power Supply:** Use a power source that can provide **at least 1A** to handle peak current.

## Troubleshooting with Multimeter

1. **Check Power:**
   - Measure GPIO23: Should be 0V in sleep, 3.3V when active
   - Measure ESP32CAM VCC: Should match your power supply

2. **Check UART:**
   - Measure TX/RX pins with oscilloscope or logic analyzer
   - Should see data bursts at 115200 baud

3. **Check Connections:**
   - Continuity test between LilyGo GPIO1 and ESP32CAM GPIO16
   - Continuity test between LilyGo GPIO3 and ESP32CAM GPIO13
   - Verify common ground between boards

## Common Wiring Mistakes ❌

| Mistake          | Symptom              | Fix                      |
| ---------------- | -------------------- | ------------------------ |
| TX→TX, RX→RX     | No communication     | Swap: TX→RX, RX→TX       |
| Missing GND      | Erratic behavior     | Connect common ground    |
| Long wires       | Communication errors | Use <20cm wires          |
| Wrong voltage    | Damaged board        | Verify 3.3V logic levels |
| No power control | Camera always on     | Connect GPIO23 to PWR_EN |

## Recommended Wire Colors

For easy identification:

- **Red:** Power (VCC/3V3)
- **Black:** Ground (GND)
- **Yellow:** TX (transmit data)
- **Green:** RX (receive data)
- **Blue:** I2C SDA
- **White:** I2C SCL
- **Orange:** Control signals (GPIO23)

---

**Note:** This diagram shows the logical connections. Physical pin locations vary by board revision—refer to the pinout images provided earlier for exact positions.
