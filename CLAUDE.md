# CLAUDE.md - Firefly IMU Dashboard

## Project Overview
Arduino Nano 33 BLE Sense (nRF52840) + onboard 9-axis IMU → 9-DOF Madgwick fusion → quaternion
streamed over **BLE (GATT notify)** → **Web Bluetooth** dashboard (Three.js) themed as Firefly's
**Blue Ghost** lunar lander. Hosted on GitHub Pages.

> This is the reliable rebuild of the older ESP32-S3 + MPU-9250 + WiFi project (kept in a separate
> repo). Key differences: onboard brand-name IMU (no external sensor/wiring), BLE instead of WiFi-AP,
> 9-DOF fusion (magnetometer → absolute yaw), Firefly Blue Ghost theme.

## Quick Reference
- **Board**: `nano33ble` (platform `nordicnrf52`, framework `arduino` / mbed-based, nRF52840)
- **IMU (depends on board revision)**:
  - Rev1 → ST **LSM9DS1** → lib `arduino-libraries/Arduino_LSM9DS1`
  - Rev2 → Bosch **BMI270 + BMM150** → lib `arduino-libraries/Arduino_BMI270_BMM150`
  - Confirm the revision before building; only the IMU library + read calls differ.
- **BLE device name**: `Firefly-BlueGhost-IMU`
- **GATT**: custom 128-bit service UUID; one `notify` characteristic.
- **Packet**: 20 bytes = 10× `int16` (scaled) = quaternion `w,x,y,z` + accel `ax,ay,az` + gyro `gx,gy,gz`.
  20 bytes fits the default BLE payload — no MTU negotiation needed.
- **Sample rate**: ~104–119 Hz (real ODR of the onboard IMU; feed this as the Madgwick `sampleFreq`).
- **Serial**: 115200 baud (USB CDC port of the Nano — check the actual `/dev/cu.*` / `/dev/ttyACM*`).

## Build & Upload
```bash
pio run -e nano33ble                 # build firmware
pio run -e nano33ble -t upload       # flash the Nano
```
No filesystem upload step — the device no longer serves the web page (BLE only).

## Reading Serial Output (from Claude Code)
`pio device monitor` doesn't work in non-interactive terminals. Use pyserial (adjust the port to the
Nano's actual CDC device, e.g. `/dev/cu.usbmodemXXXX`):
```python
python3 -c "
import serial, time
ser = serial.Serial('/dev/cu.usbmodem<PORT>', 115200, timeout=1)
end = time.time() + 10
while time.time() < end:
    line = ser.readline()
    if line:
        print(line.decode('utf-8', errors='replace').strip())
ser.close()
"
```

## Key Files
- `src/main.cpp` — firmware: IMU init + read, gyro-bias calibration, 9-DOF Madgwick, magnetometer
  calibration, BLE GATT service/characteristic, single `loop()` (BLE.poll + read + fuse + notify).
- `src/sensor_fusion.h` — Madgwick filter. `update()` = 6-DOF (fallback); `updateMag()` = 9-DOF.
- `data/` (→ `docs/` for GitHub Pages) — dashboard: `index.html`, `app.js` (Web Bluetooth client +
  Three.js Blue Ghost model + rolling charts), `style.css` (Firefly black/lime theme).
- `platformio.ini` — board + lib config.
- `PLAN.md` — full implementation plan and rationale.

## Architecture
- **Single core (nRF52840)**: one `loop()` reads the IMU at the sensor rate, runs 9-DOF Madgwick to
  get a quaternion, packs 10× int16, and updates the BLE characteristic (`notify`). `BLE.poll()` each loop.
- **Transport**: BLE GATT notify → Web Bluetooth in the browser (Chrome/Edge desktop + Android).
- **Frontend**: Three.js Blue Ghost lander rotated by the quaternion, resting on legs on a grid;
  rolling accel/gyro charts. Three.js loaded from CDN (page is served by GitHub Pages over HTTPS).

## Important Notes
- **Web Bluetooth requires HTTPS + a user gesture**; works in Chrome/Edge on desktop & Android,
  **NOT iOS Safari**. The dashboard must be served from GitHub Pages (or another HTTPS origin).
- **No WiFi, no web server, no LittleFS, no captive portal** — all removed vs. the ESP32 version.
- **9-DOF fusion needs magnetometer calibration** (hard/soft-iron). If calibration is troublesome,
  fall back to 6-DOF (`update()`) — pitch/roll stay good, yaw drifts slowly.
- **Quaternion convention**: Three.js uses `(x, y, z, w)` order — `quaternion.set(x, y, z, w)`.
- Pick the IMU library to match the **board revision** (LSM9DS1 vs BMI270+BMM150).
