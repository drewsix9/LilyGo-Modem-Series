# Servo Controller Unit Tests

This directory contains unit tests and interactive tests for the `servo_controller` module.

## Files

### 1. `test_servo_controller.cpp`

Comprehensive unit test suite using the Unity testing framework.

**Tests included:**

- Initialization with default and custom parameters
- Pulse width clamping and validation
- Start angle handling (0°, 90°, 180°)
- Angle writing and reading (0°, 90°, 180°)
- Angle boundary clamping
- Servo attachment/detachment
- Pin retrieval
- Multiple angle sequences
- Interactive angle input test

**Run with PlatformIO:**

```bash
platformio test --environment T-A7670X
```

### 2. `servo_controller_interactive_test.ino`

Interactive test sketch for manual testing with user input.

**Features:**

- Command-line interface for servo control
- User input for custom angles (0-180°)
- Real-time angle reading
- Test sequences
- Servo attach/detach control
- Status information

**Commands:**

- `w` - Write angle (prompts for value 0-180)
- `r` - Read current angle
- `i` - Show servo info (status, pin, angle)
- `t` - Run automated test sequence (0→45→90→135→180)
- `d` - Detach servo
- `a` - Attach servo
- `h` - Show help menu
- `q` - Quit

**Usage:**

1. Upload the sketch to your LilyGo board
2. Open Serial Monitor (115200 baud)
3. Follow the on-screen menu
4. Enter angles when prompted (e.g., type `45` for 45 degrees)

## Running Tests

### Unit Tests

```bash
cd "d:/Github Local FIles/LilyGo-Modem-Series/crb-trap/Lilygo+SERIAL+POST+SENSORS+SD_ON_MASTER"
platformio test --environment T-A7670X
```

### Interactive Tests

```bash
platformio run -t upload --environment T-A7670X
platformio device monitor --environment T-A7670X
```

Then use the serial monitor interface to input angles and test servo behavior.

## Test Coverage

### Angle Range Tests

- **0°** - Minimum angle
- **45°** - Quarter rotation
- **90°** - Mid-range (default start position)
- **135°** - Three-quarter rotation
- **180°** - Maximum angle

### Parameter Validation

- Minimum pulse width clamping
- Maximum pulse width clamping
- Pulse width range enforcement (max > min)

### State Management

- Servo initialization
- Attachment/detachment
- Pin management
- Multiple write/read cycles

## Example Interactive Session

```
========================================
  Servo Controller Interactive Test
========================================

[INIT] Initializing servo on pin 13
  Min Pulse: 500 µs
  Max Pulse: 2400 µs
  Start Angle: 90°
[OK] Servo initialized successfully!

========== COMMANDS ==========
  w: Write angle (prompt for value)
  r: Read current angle
  i: Show servo info
  t: Run test sequence
  d: Detach servo
  a: Attach servo
  h: Show this menu
  q: Quit
==============================

> w
[WRITE] Enter angle (0-180 degrees):
> 45
[ACTION] Writing angle 45° to servo...
[OK] Successfully wrote angle 45° (read back: 45°)

> r
[READ] Reading angle from servo...
[OK] Current angle: 45°

> t
[TEST] Running angle sequence test (0° → 90° → 180°)...
[TEST] Step 1/5: Writing 0°... OK (read: 0°)
[TEST] Step 2/5: Writing 45°... OK (read: 45°)
[TEST] Step 3/5: Writing 90°... OK (read: 90°)
[TEST] Step 4/5: Writing 135°... OK (read: 135°)
[TEST] Step 5/5: Writing 180°... OK (read: 180°)
[TEST] Sequence test complete!
```

## Hardware Requirements

- LilyGo T-A7670 board
- Servo motor connected to GPIO 13 (or custom pin)
- USB cable for serial communication and programming

## Notes

- Servo power management: The `writeAngle()` function automatically detaches the servo after movement to save power
- Default pulse width range: 500-2400 µs (suitable for most standard servos)
- All angles are constrained to the 0-180° range
- Interactive test has a 10-second timeout for angle input

## Troubleshooting

**Servo not responding:**

- Check power supply to servo
- Verify pin connection (default GPIO 13)
- Ensure servo library is properly installed

**Serial input not working:**

- Verify baud rate is set to 115200
- Check USB cable connection
- Ensure line endings are set to "No line ending" or "Newline" in Serial Monitor

**Test failures:**

- Run with actual hardware connected
- Check servo motor for mechanical issues
- Verify pulse width settings match your servo specifications
