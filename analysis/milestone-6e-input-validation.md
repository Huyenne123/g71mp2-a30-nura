# Milestone 6E Phase 2: Input Device Discovery & Validation

## 1. Executive Summary

Milestone 6E Phase 2 audited all input devices present on the Samsung Galaxy A30 (`SM-A305F`, Exynos 7904) downstream kernel `4.4.302-p6-neyuKernel_legacy` within the Alpine Linux v3.21.8 ARM64 (`musl 1.2.5`) environment.

All evdev capabilities, event codes, coordinate spaces, and `libinput` / `udev` properties were empirically probed using `getevent`, `evtest`, and `libinput record`.

---

## 2. Hardware Input Device Inventory

Probing `/proc/bus/input/devices` and `/dev/input/*` identified 16 event nodes:

| Event Node | Device Name | Bus / Type | Driver / Subsystem | Primary Capabilities |
| :--- | :--- | :--- | :--- | :--- |
| `/dev/input/event0` | `meta_event` | Virtual | Sensor/Framework | Relative motion metadata |
| `/dev/input/event1` | `sec_touchscreen` | I2C (`0x0018`) | `mms_ts` (`11-0048`) | **10-Point Multitouch Digitizer** |
| `/dev/input/event2` | `sec_touchproximity`| I2C (`0x0018`) | `11-0048` | Touch proximity switch |
| `/dev/input/event3` | `sec_touchpad` | I2C (`0x0018`) | `11-0048` | Touchpad / mouse emulation |
| `/dev/input/event4` | `accelerometer_sensor` | Virtual | IIO / Sensors | 3-axis acceleration |
| `/dev/input/event5` | `gyro_sensor` | Virtual | IIO / Sensors | 3-axis angular velocity |
| `/dev/input/event6` | `SignificantMotionDetector` | Virtual | IIO / Sensors | Motion wakeup trigger |
| `/dev/input/event7` | `tilt_sensor` | Virtual | IIO / Sensors | Device tilt angle |
| `/dev/input/event8` | `step_cnt_sensor` | Virtual | IIO / Sensors | Step counter |
| `/dev/input/event9` | `step_det_sensor` | Virtual | IIO / Sensors | Step detector |
| `/dev/input/event10` | `magnetic_sensor` | Virtual | IIO / Sensors | Compass / magnetometer |
| `/dev/input/event11` | `proximity_sensor`| Virtual | IIO / Sensors | Optical proximity sensor |
| `/dev/input/event12` | `light_sensor` | Virtual | IIO / Sensors | Ambient light sensor |
| `/dev/input/event13` | `Codec3035 Headset Events` | Platform | Audio Codec | Jack detect, headset hook/mic buttons |
| `/dev/input/event14` | `gpio_keys` | Platform (`0x0019`) | `gpio_keys` | **Hardware Physical Buttons** |
| `/dev/input/event15` | `flip_cover` | Platform | Hall sensor | Magnetic flip cover switch |

---

## 3. Touchscreen Digitizer Analysis (`/dev/input/event1`)

### 3.1 Device Specifications
- **Hardware Controller**: Melfas MMS-series I2C capacitive touch controller (`11-0048`, driver `mms_ts`).
- **Input Property**: `INPUT_PROP_DIRECT` (`0x01` — touch coordinates map directly to display pixels).
- **Multitouch Slots**: 10 simultaneous contact points (`ABS_MT_SLOT` min 0, max 9).

### 3.2 Coordinate Space vs Display Geometry
The physical digitizer reports absolute position coordinates over an expanded range compared to the display panel:

| Axis | Digitizer Min | Digitizer Max | Physical Panel Resolution | Coordinate Scaling Ratio |
| :--- | :--- | :--- | :--- | :--- |
| **X Axis** (`ABS_MT_POSITION_X`, 0x35) | 0 | 4095 | 1080 px | `1080 / 4096 = 0.26367` |
| **Y Axis** (`ABS_MT_POSITION_Y`, 0x36) | 0 | 4095 | 2340 px | `2340 / 4096 = 0.57129` |

### 3.3 Event Capabilities (`evtest` & `libinput record`)
- `EV_SYN` (`0x00`): Full sync packet support.
- `EV_KEY` (`0x01`):
  - `BTN_TOUCH` (`0x14a` / 330): Contact state.
  - `BTN_TOOL_FINGER` (`0x145` / 325): Finger tool type.
  - `KEY_WAKEUP` (`0x08f` / 143): Touch-to-wake.
- `EV_ABS` (`0x03`):
  - `ABS_MT_SLOT` (`0x2f` / 47): 0..9
  - `ABS_MT_TOUCH_MAJOR` (`0x30` / 48): Contact ellipse major axis (0..255)
  - `ABS_MT_TOUCH_MINOR` (`0x31` / 49): Contact ellipse minor axis (0..255)
  - `ABS_MT_POSITION_X` (`0x35` / 53): 0..4095
  - `ABS_MT_POSITION_Y` (`0x36` / 54): 0..4095
  - `ABS_MT_TRACKING_ID` (`0x39` / 57): Unique contact identifier (0..65535)
  - `ABS_MT_TOOL_TYPE` (`0x3e` / 62): Contact tool classification (0..1)

---

## 4. Hardware Keys Analysis (`/dev/input/event14`)

The physical buttons on the Galaxy A30 chassis are handled by the platform `gpio_keys` driver:

| Key Code | Hex | Linux Definition | Physical Button |
| :--- | :--- | :--- | :--- |
| **114** | `0x0072` | `KEY_VOLUMEDOWN` | Volume Down rocker switch |
| **115** | `0x0073` | `KEY_VOLUMEUP` | Volume Up rocker switch |
| **116** | `0x0074` | `KEY_POWER` | Side Power / Wake button |

All keys generate standard `EV_KEY` press (`value = 1`), release (`value = 0`), and autorepeat (`value = 2`) sequences.

---

## 5. Libinput & Weston Integration Audit

### 5.1 Libinput Validation
`libinput-tools` (v1.27.0) verified that `libinput record` parses `/dev/input/event1` correctly:
```yaml
devices:
- node: /dev/input/event1
  evdev:
    name: "sec_touchscreen"
    id: [24, 0, 0, 0]
    properties: [1] # INPUT_PROP_DIRECT
```

### 5.2 Current Backend State & Limitations
- **Current Presentation Engine**: The `decon-backend.so` implemented in Milestone 6B/6D initializes display heads and renderbuffers directly via Samsung DECON Window 3 and ION DMA-BUFs.
- **Seat Initialization**: The backend currently does **not** initialize an input seat (`struct udev_input` or `weston_seat`). As a result, Weston runs strictly as an output display compositor.
- **Wayland Client Interactivity**: Non-interactive Wayland clients (`weston-simple-shm`, `weston-simple-damage`) run and composite without requiring an input seat. Clients requiring pointer/keyboard events (`weston-terminal`) require an active `wl_seat`.
- **Architectural Path for Milestone 7**: In Weston 14, input is managed via the internal `dep_libinput_backend` static library (`libinput-seat.c`, `libinput-device.c`). Adding `dep_libinput_backend` to `backend-decon/meson.build` and calling `udev_input_init(&b->input, ...)` will provide full out-of-the-box multitouch and hardware button handling.
