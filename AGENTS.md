## Context
Real-time IMU orientation visualizer: Arduino Nano 33 BLE Sense → 9-DOF sensor fusion → BLE → a
Web Bluetooth + Three.js dashboard themed as Firefly's Blue Ghost lunar lander. Prioritize clean,
non-blocking firmware and a reliable, brand-name-hardware design (onboard IMU, no external wiring).

## Architectural Constraints
- **Platform:** Arduino Nano 33 BLE Sense (nRF52840, single core). Keep `loop()` non-blocking; run
  `BLE.poll()` every iteration; pace the IMU read with a timer/elapsed check (no `delay()` spins).
- **IMU:** onboard 9-axis. Rev1 = LSM9DS1, Rev2 = BMI270+BMM150 — select the matching library.
- **Sensor Fusion:** 9-DOF Madgwick using the magnetometer for absolute heading. Transmit a
  **quaternion `(w, x, y, z)`** — never raw Euler — to avoid gimbal lock in the 3D model.
- **Transport:** BLE GATT only. One `notify` characteristic; pack data as 10× `int16` (20 bytes).
  No WiFi, no web server, no LittleFS on the device.
- **Frontend:** static site served by GitHub Pages (HTTPS, required by Web Bluetooth). Three.js from CDN.

## BLE Protocol
- **Device name:** `Firefly-BlueGhost-IMU`
- **Service:** custom 128-bit UUID; **Characteristic:** `notify`, 20-byte payload.
- **Payload:** 10× `int16` little-endian = quaternion `w,x,y,z`, accel `ax,ay,az`, gyro `gx,gy,gz`
  (each scaled to int16; de-scale in the browser).

## File Structure
- `src/main.cpp`: entry point, IMU read, 9-DOF fusion, BLE GATT notify (single non-blocking loop).
- `src/sensor_fusion.h`: Madgwick filter — `update()` (6-DOF) and `updateMag()` (9-DOF).
- `data/` → `docs/`: dashboard UI, Web Bluetooth client, Three.js Blue Ghost model.
- `platformio.ini`: board + dependency management.
