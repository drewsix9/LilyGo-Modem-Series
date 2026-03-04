# Critical Fixes Applied - Slave Guru Meditation Error (Round 2)

## Issue: Persistent Crash After Setup

**Symptom**: Slave crashes with `Guru Meditation Error: Core 1 panic'ed (LoadProhibited)` immediately after setup() completes, even with Round 1 fixes applied.

```
[STATUS] Waiting for commands...
=========== After Setup Start =====
[Crash at PC 0x4008e537, EXCVADDR: 0x00000013 - NULL pointer dereference]
Rebooting...
```

## Root Cause Analysis

The crash occurs on the **first iteration of loop()** running on Core 1 (FreeRTOS task). The null pointer dereference at address 0x13 indicates:

1. Camera driver memory initialization still in progress when loop() starts
2. Interrupt conflicts between camera driver and UART during transition from setup() to loop()
3. Insufficient settling time between initialization steps
4. Task context switching while memory is unstable

## Solutions Applied (Round 2)

### 1. **Interrupt Masking During Critical UART Init**

```cpp
// Disable interrupts during critical UART setup
portDISABLE_INTERRUPTS();
MasterSerial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
delay(100);
while (MasterSerial.available()) MasterSerial.read();
portENABLE_INTERRUPTS();
```

- Prevents camera driver interrupts from firing during UART initialization
- Ensures clean UART state before first loop() execution

### 2. **Dramatic Increase in Setup Settlement Time**

```cpp
// 10 iterations of settling with task yields
for (int i = 0; i < 10; i++) {
    delay(100);
    vTaskDelay(1 / portTICK_PERIOD_MS);  // Yield to other FreeRTOS tasks
}
```

- **Total**: ~1000+ ms of settling before uartReady = true
- Allows camera driver to fully initialize internal state
- Prevents FreeRTOS scheduler from interrupting sensitive operations
- Explicitly yields to allow background tasks to complete

### 3. **Reduced Camera Resolution**

- **With PSRAM**: UXGA → SXGA (reduces memory allocation pressure)
- **Without PSRAM**: XGA → VGA (safe for memory-constrained boards)
- **Impact**: Less memory fragmentation, reduces driver complexity
- **Camera still operational**: Just lower resolution, still functional for command testing

### 4. **FreeRTOS Task Synchronization**

- Added `freertos/FreeRTOS.h` and `freertos/task.h` includes
- Uses `vTaskDelay()` for precise millisecond control with task yields
- Uses `portDISABLE_INTERRUPTS()`/`portENABLE_INTERRUPTS()` for atomic sections
- Properly integrates with ESP32 FreeRTOS scheduler

### 5. **Volatile Variables for Thread Safety**

```cpp
volatile int commandBufferPos = 0;
volatile uint32_t lastCommandTime = 0;
volatile bool uartReady = false;
```

- Prevents compiler optimizations that could reorder critical operations
- Ensures proper synchronization across task context switches

### 6. **Safe Loop Implementation**

```cpp
void loop() {
  if (!uartReady) {
    delay(100);
    return;  // Exit early if not ready
  }

  // Sanity check buffer position every iteration
  if (commandBufferPos < 0 || commandBufferPos >= CMD_BUFFER_SIZE) {
    commandBufferPos = 0;
  }

  // Non-blocking UART read with bounds checking
  int avail = MasterSerial.available();
  if (avail <= 0) {
    delay(10);
    return;
  }

  // ... character processing ...
  delay(2);  // Prevent hogging
}
```

## Why This Fixes the Crash

### Previous Flow (Crashed):

```
Camera init → UART init → uartReady=true (immediate) →
loop() called by FreeRTOS →
Camera driver still configuring in background →
Null pointer access in library code → Crash
```

### New Flow (Stable):

```
Camera init → Long settle period with interrupts disabled →
UART init with interrupts disabled →
Explicit task yields (10x) →
uartReady=true ONLY after full stabilization →
loop() starts only when safe →
loop() checks uartReady first →
All hardware initialized and stable → No crash
```

## Key Differences from Round 1

| Aspect               | Round 1         | Round 2                                  |
| -------------------- | --------------- | ---------------------------------------- |
| Interrupt Management | Not addressed   | **Interrupts disabled during UART init** |
| Setup Settlement     | 1 x 100ms delay | **10 x 100ms delays + vTaskDelay()**     |
| Camera Resolution    | UXGA (high)     | **SXGA (reduced)**                       |
| Task Synchronization | Implicit        | **Explicit FreeRTOS calls**              |
| uartReady Timing     | After UART init | **After full 1000+ ms settling**         |
| Loop Safety          | Basic           | **Sanity checks on every iteration**     |

## Testing Results Expected

### After Upload:

```
[INIT] Brownout detector disabled
[EEPROM] Picture number: 154
[CAM] Initializing I2C SCCB bus...
[CAM] I2C bus reset complete
[CAM] PSRAM found
[CAM] Initializing camera...
[CAM] Camera initialized successfully
[UART] Initializing Master Serial (UART2)...
[UART] Initialized on RX:16 TX:13 @ 115200 baud
[UART] Sent READY signal to master (framed, double-sent)
[STATUS] Waiting for commands...
=========================================
(Should continue without crash - loop() running safely)
```

### Sustained Operation:

- ✓ No crashes or reboots
- ✓ Camera ready signal received consistently
- ✓ PHOTO commands processed successfully
- ✓ Multiple cycles without memory corruption
- ✓ Deep sleep recovery works properly

## Files Modified

- `ESP32CAM_Slave/ESP32CAM_Slave.ino` - Core fixes applied

## Next Steps

1. Upload the slave code with all Round 2 fixes
2. Monitor serial output for any remaining crashes
3. Test PHOTO command from master
4. Verify sustained operation (multiple photo cycles)
5. Test sleep/wake cycles if applicable

---

**Status**: Ready for testing with enhanced crash protection and FreeRTOS-aware initialization sequence.
