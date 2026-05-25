# Firefly IMU Dashboard 🚀🌙

Real-time 3D orientation dashboard for an **Arduino Nano 33 BLE Sense**, streaming a
9-DOF sensor-fusion quaternion over **Bluetooth Low Energy** to a browser dashboard themed
after Firefly Aerospace's **Blue Ghost** lunar lander.

Tilt and rotate the board on your desk → a Blue Ghost lander rotates live in 3D, with rolling
accelerometer/gyroscope charts. The lander rests on its legs on a grid "surface," so you can later
drop the board into a 3D-printed Blue Ghost shell.

> Sister project to the ESP32-S3 + MPU-9250 version (kept separately). This repo is the reliable,
> brand-name-hardware rebuild: onboard IMU, no external wiring, BLE instead of a flaky WiFi AP.

## Why this design

| Problem in the old build | Fix here |
|--------------------------|----------|
| $8 Amazon "MPU-9250" (often counterfeit / no working magnetometer) | **Onboard 9-axis IMU** on a brand-name Arduino board — no external sensor, no wiring |
| ESP32-S3 SoftAP unstable / brownouts | **BLE** streaming — local, low-power, no WiFi AP, no web server on the device |
| Yaw drift (6-DOF fusion) | **9-DOF Madgwick** using the magnetometer for absolute heading |

## Hardware

- **Board:** Arduino Nano 33 BLE Sense (nRF52840).
  - **Rev1** → ST **LSM9DS1** (accel + gyro + mag, 9-axis)
  - **Rev2** → Bosch **BMI270 + BMM150** (9-axis)
  - Both are 9-axis with a magnetometer; only the firmware library differs. Confirm your revision
    (printed on the board / check the IMU chip) and select the matching library in `platformio.ini`.
- No external sensor or wiring — the IMU is on the board.

## How the data reaches the page (BLE + Web Bluetooth)

GitHub Pages and BLE are independent:

```
[Nano 33 BLE] --BLE radio (local, peer-to-peer)--> [PC/phone Bluetooth] --> [Chrome] --> JS --> Three.js
      ▲ firmware updates a GATT characteristic ~60x/sec
      └─ the page's CODE was served once by GitHub Pages (HTTPS)
```

1. The firmware exposes a **GATT service** with one **characteristic** (a "mailbox") that supports `notify`.
2. Each frame it packs the quaternion + accel + gyro into 20 bytes (10× `int16`) and writes the characteristic.
3. In Chrome you press **Connect** (Web Bluetooth requires a user gesture), pick `Firefly-BlueGhost-IMU`,
   and the browser **subscribes**.
4. Every update fires a `characteristicvaluechanged` event → JS parses the bytes → updates Three.js + charts.

The live sensor data **never goes through the internet or GitHub** — GitHub Pages only delivers the
static app code (over HTTPS, which Web Bluetooth requires).

## Quick start

### Firmware
```bash
pio run -e nano33ble                 # build
pio run -e nano33ble -t upload       # flash the Nano
```
First boot: run the **magnetometer calibration** (move the board in a figure-8 for ~15–20 s) so the
9-DOF fusion has an absolute, drift-free heading. Calibration is stored in flash.

### Dashboard
Open the GitHub Pages URL in **Chrome or Edge** (desktop or Android — *not* iOS Safari):

> **https://jacedeno.github.io/firefly-imu-dashboard/**

Click **Connect**, choose `Firefly-BlueGhost-IMU`, then move the board.

## Limitations
- **Web Bluetooth** works in Chrome/Edge on desktop and Android. **iOS Safari is not supported.**
- Requires **HTTPS** (hence GitHub Pages) — Web Bluetooth only runs in a secure context.

## Project layout
- `src/main.cpp` — firmware: IMU read, 9-DOF Madgwick fusion, BLE GATT notify.
- `src/sensor_fusion.h` — Madgwick filter (6-DOF + 9-DOF).
- `data/` → `docs/` — the dashboard (HTML/JS/CSS, Three.js Blue Ghost model, Web Bluetooth client),
  published via GitHub Pages.
- `platformio.ini` — board + library config.
- `PLAN.md` — the full implementation plan.

## Credits
Crafted by [GeekendZone](https://geekendzone.com). Blue Ghost theme inspired by
[Firefly Aerospace](https://fireflyspace.com).
