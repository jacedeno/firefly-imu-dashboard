# Plan: Migrate to Arduino Nano 33 BLE Sense + BLE + Firefly "Blue Ghost" theme

## Context (why this change)
The current project (Seeed XIAO ESP32-S3 + Amazon MPU-9250, WiFi-AP + WebSocket + Three.js)
has two reliability problems: the generic $8 IMU is of dubious quality (many Amazon
"MPU-9250" boards are clones or ship without a working magnetometer) and the ESP32-S3
SoftAP is unstable (brownouts with weak power supplies). The user already owns an
**Arduino Nano 33 BLE Sense** (from a TinyML kit), which is a reputable brand-name board
with an onboard **9-axis IMU with integrated magnetometer** — eliminating the cheap sensor
and all external wiring at the root. Taking advantage of this, we migrate the data
transport from WiFi to **BLE (Web Bluetooth)** and rebrand the theme from NASA Shuttle to
the **Blue Ghost** lunar lander from **Firefly Aerospace** (where the user's son is doing
an internship).

## Decisions made (confirmed with the user)
- **Hardware**: Arduino Nano 33 BLE. **CONFIRMED by HW diagnostics (2026-05-25):** it has
  the ST **LSM9DS1** (9-axis), `IMU.begin()` OK, accel/gyro ODR **119 Hz**, mag **20 Hz**,
  lib `Arduino_LSM9DS1`. The IMU lives on the internal `Wire1` bus. (Not BMI270/Rev2.)
- **Transport**: BLE / Web Bluetooth (not WiFi). Accepted that it doesn't run on iOS Safari
  (Chrome/Edge on PC and Android do work).
- **Fusion**: 9-DOF with magnetometer (absolute yaw, no drift) → requires mag calibration.
- **3D model**: Blue Ghost (module with legs), standing on its legs on the grid/table.
- **Colors**: matte black + Firefly lime green (+ optional MLI-foil gold accents).
- **Dashboard hosting**: GitHub Pages (HTTPS).
- **New, independent repo**: the original repo (`mpu9250-imu-3d-visualization`) **is left
  untouched** as the future ESP32 + ICM-20948 version. All work goes into a new repo,
  **`firefly-imu-dashboard`** (`/Users/geekendzone/repos/firefly-imu-dashboard`), created by
  **copying the current code as a base** (fresh git, clean history) and **published on
  GitHub (public)**.

## How the data travels from the Nano to the page (clarification requested by the user)
GitHub Pages and BLE are **independent**:
- **GitHub Pages** delivers the dashboard *code* (HTML/JS/Three.js) **once**, over HTTPS.
- **The live data** travels over the BLE radio, locally and point-to-point, never touching
  the internet:
  1. The firmware defines a **GATT service** with a **characteristic** (a "mailbox") that
     supports `notify`.
  2. Each frame, the firmware packs the quaternion + accel + gyro and writes that buffer to
     the characteristic.
  3. In Chrome, the user clicks **Connect** (Web Bluetooth requires a user gesture), picks
     the device, and the browser **subscribes** to the characteristic.
  4. Every update fires a `characteristicvaluechanged` event in JS → the bytes are parsed →
     applied to the Three.js model and the charts.

```
[Nano 33 BLE] --BLE radio (local, p2p)--> [PC Bluetooth] --> [Chrome] --> JS --> Three.js
      ▲ the firmware updates the characteristic 60x/sec
      └─ the page code came from GitHub Pages (HTTPS) exactly once
```
HTTPS is mandatory because Web Bluetooth only works in a "secure context" (hence GitHub Pages).

---

## Phase 0 — Create the new repo `firefly-imu-dashboard`
1. Copy the current repo's contents to `/Users/geekendzone/repos/firefly-imu-dashboard`
   **excluding `.git/`** (and `data/three.min.js.gz`, no longer used). The original repo is
   **not touched**.
2. `git init` (clean history), `git branch -m main`.
3. Write the new `README.md`, `CLAUDE.md`, and copy this plan to `docs/PLAN.md` (or
   `plan.md`) inside the repo.
4. `gh repo create geekendzone/firefly-imu-dashboard --public --source=. --remote=origin` +
   first commit + `git push -u origin main`.
5. Everything under "FIRMWARE/FRONTEND/Rebrand changes" below is applied **inside this new
   repo**.

### `README.md` (new repo) — contents
- Title and pitch: real-time 3D orientation dashboard with an **Arduino Nano 33 BLE Sense**,
  9-DOF fusion, streaming over **BLE / Web Bluetooth**, **Firefly "Blue Ghost"** theme.
- Hardware: Nano 33 BLE Sense (Rev1 LSM9DS1 / Rev2 BMI270+BMM150, both 9-axis), no external
  wiring.
- Architecture (include the BLE flow diagram above: the board notifies a GATT
  characteristic; the browser subscribes via Web Bluetooth; GitHub Pages only serves the code).
- How to build/upload firmware (`pio run -e nano33ble -t upload`), how to calibrate the
  magnetometer (figure-8), and how to open the dashboard (GitHub Pages URL, Chrome/Edge
  browser, Connect button).
- Limitations: no iOS Safari (Web Bluetooth); requires HTTPS.
- Credit: GeekendZone. Mention of the Firefly Aerospace context.

### `CLAUDE.md` (new repo) — contents
Rewrite of the current CLAUDE.md adapted to the new stack:
- Overview: Nano 33 BLE Sense + onboard 9-axis IMU → 9-DOF fusion → BLE → Web Bluetooth →
  Three.js (Blue Ghost).
- Quick Reference: board `nano33ble` (platform `nordicnrf52`), the Nano's serial port, BLE
  name `Firefly-BlueGhost-IMU`, service/characteristic UUID, packet format
  (10×int16 = 20 bytes).
- Build & upload (`pio run -e nano33ble`, `-t upload`); how to read serial (adapt the
  pyserial snippet to the Nano's port).
- Key files: `src/main.cpp` (IMU + 9-DOF Madgwick + BLE), `src/sensor_fusion.h` (9-DOF),
  `docs/` (Web Bluetooth frontend + GitHub Pages).
- Important notes: IMU library per board revision; real ODR ~119 Hz; mag calibration;
  Web Bluetooth is HTTPS/Chrome only.

---

## FIRMWARE changes (`src/`, `platformio.ini`) — in the new repo

### `platformio.ini` — new environment for the Nano
Replace the `[env:seeed_xiao_esp32s3]` block with:
```ini
[env:nano33ble]
platform = nordicnrf52
board = nano33ble
framework = arduino
monitor_speed = 115200
lib_deps =
    arduino-libraries/ArduinoBLE@^1.3.7
    ; IMU per board revision (confirm at home, keep whichever applies):
    arduino-libraries/Arduino_LSM9DS1@^1.1.1        ; Rev1 (ST LSM9DS1)
    ; arduino-libraries/Arduino_BMI270_BMM150@^1.2.0 ; Rev2 (Bosch BMI270+BMM150)
```
`MPU9250`, `ESPAsyncWebServer`, `ArduinoJson`, and `board_build.filesystem = littlefs` are
removed (no more web server or LittleFS).

### `src/main.cpp` — core rewrite (single-core board, no WiFi)
The ESP32 dual-task model (Core1 sensor / Core0 WiFi) no longer applies. It collapses into
a single `loop()` with fixed timing. Concrete changes against what was mapped:
- **Remove** everything WiFi-AP, DNS captive portal, `ESPAsyncWebServer`, WebSocket
  broadcast (lines ~19, ~99, and the whole server/portal), and the I2C/MPU initialization
  (`MPU_ADDR 0x68`, `PIN_SDA/SCL`, `Wire.begin`, `imu.begin()`, `setAccelRange`…
  lines 13-16, 43, 102-137).
- **Remove** the creation of the FreeRTOS `sensorTask` pinned to Core 1 (lines 51-84,
  211-213).
- **Add** onboard IMU init:
  - Rev1: `IMU.begin()` from `Arduino_LSM9DS1` → `readAcceleration` (g), `readGyroscope`
    (dps), `readMagneticField` (µT).
  - Rev2: `Arduino_BMI270_BMM150` (same API shape).
  - **Unit conversion** for Madgwick: gyro `rad/s = dps * PI/180`; accel
    `m/s² = g * 9.80665`.
  - **Real sample rate**: the LSM9DS1 delivers ~119 Hz (accel/gyro); set `SENSOR_HZ` to the
    real ODR (~104-119 Hz) instead of 200 Hz, and feed that frequency to the Madgwick filter.
  - **Gyro bias**: average ~1 s at rest on startup (replaces `imu.calibrateGyro()`).
- **BLE GATT** with `ArduinoBLE`:
  - Custom service (128-bit UUID, e.g. `19B10000-E8F2-537E-4F6C-D104768A1214`).
  - `notify` characteristic, **20 bytes** = 10×`int16` packed (quaternion w,x,y,z +
    ax,ay,az + gx,gy,gz, each scaled to int16). 20 bytes fits the default BLE payload →
    robust without MTU negotiation.
  - `BLE.setLocalName("Firefly-BlueGhost-IMU")`, `BLE.setDeviceName(...)`, `BLE.advertise()`.
  - `BLE.setConnectionInterval(12, 24)` (~15-30 ms) to support ~60 Hz notifications.
  - In `loop()`: `BLE.poll()` + read IMU at `SENSOR_HZ` + `filter.updateMag(...)` + pack +
    `characteristic.writeValue(buf, 20)`.

### `src/sensor_fusion.h` — extend to 9-DOF
Today it only has `update(gx,gy,gz, ax,ay,az)` (6-DOF, no mag; lines 10-14, 82-85). Add:
- `void updateMag(float gx,float gy,float gz, float ax,float ay,float az, float mx,float my,float mz)`
  with Madgwick's 9-DOF AHRS formula (gradient descent with magnetic reference). Reuse the
  existing `_q0.._q3` and the `w()/x()/y()/z()` getters.
- Keep 6-DOF `update()` as a fallback.

### Magnetometer calibration (hard/soft-iron)
- Calibration mode: if no stored offsets (or on serial command), collect mag samples while
  moving the board in a figure-8 for ~15-20 s; compute the hard-iron offset `=(max+min)/2`
  per axis and a simple soft-iron scale; subtract/scale before `updateMag`.
- **Persistence** in nRF52 flash via `mbed` `KVStore`/`TDBStore`. Pragmatic fallback: print
  the offsets over serial and hard-code them as constants if persistence proves cumbersome.

---

## FRONTEND changes (move `data/` → `docs/` for GitHub Pages)

### Transport: WebSocket → Web Bluetooth (`docs/app.js`)
- **Replace** the whole WebSocket block (lines 1-54: `connectWebSocket`,
  `socket.onmessage`…).
- **Add** a Web Bluetooth client with a **Connect** button (mandatory user gesture):
  `navigator.bluetooth.requestDevice({filters:[{services:[SERVICE_UUID]}]})` →
  `gatt.connect()` → `getPrimaryService` → `getCharacteristic` → `startNotifications()` →
  `characteristicvaluechanged` listener that reads the `DataView` (10×int16, un-scale) and
  calls `onSensorData(d)` (which already exists, lines 288-306) — **reusing** the
  `shuttle.quaternion.set(x,y,z,w)` logic (line 296) and the charts.
- Update the status indicator (the `status-dot` / `ws-status`) to BLE state.

### Three.js: load from CDN (`docs/index.html`)
- `three.min.js.gz` is no longer served from LittleFS. Load Three.js r128 from a CDN
  (internet is available on GitHub Pages). **Delete** `data/three.min.js.gz`.

### 3D model: Shuttle → Blue Ghost (`docs/app.js` lines 150-207)
- Replace the shuttle `THREE.Group()` (fuselage, nose, wings, tail, engines) with a
  **Blue Ghost lander**: low, wide octagonal/box central body, top deck with
  panels/payloads, **4 angled legs** with footpads, optional high-gain antenna (dish),
  solar panel.
- Neutral orientation: the lander **standing on its legs** on the `GridHelper` (the
  "table"), so IMU rotation tilts it from that pose. (Leaves room for the user to fit the
  board into a 3D-printed model later.)
- Rename the `shuttle` variable → `lander` and the "shuttle" comments (lines 57, 74-75,
  150, 295).

### Firefly theme rebrand (colors and copy)
- **Colors** (matte black + Firefly lime green, e.g. background `#0a0a0a`, accent
  `#9aca3c`/lime green, optional foil-gold accents):
  - `docs/style.css`: replace `#00ff99` (cyan, lines 23,49,50,59,69,97,120,153) and
    `#0a0a1a` (lines 8,114,139).
  - `docs/app.js`: `scene.background` (line 62), `GridHelper` (line 141), model colors
    (lines 156,163,171,184,191,203).
- **Copy / names**:
  - `docs/index.html` title (line 6) → "Firefly Blue Ghost — IMU Dashboard".
  - "Crafted by GeekendZone" credit (line 18) → kept.
  - `style.css` header comments (lines 1-2) → describe Nano 33 BLE / Blue Ghost.
  - BLE name in firmware → `Firefly-BlueGhost-IMU` (replaces the `NASA-Shuttle-IMU` SSID).

### Hosting on GitHub Pages
- Move the frontend from `data/` to `docs/` and enable Pages from **branch `main`, folder
  `/docs`** (Settings → Pages). Yields a shareable HTTPS link.

---

## Milestones / execution order
0. **Phase 0**: create `firefly-imu-dashboard` (copy of the current repo, fresh git), write
   README.md + CLAUDE.md + PLAN, `gh repo create --public` + first push. Original repo
   untouched.
1. ~~Confirm the Nano's revision~~ ✅ **DONE (2026-05-25):** LSM9DS1 9-axis confirmed by HW
   (ODR 119/119/20 Hz, `Wire1` bus, lib `Arduino_LSM9DS1`). `nordicnrf52` toolchain already
   installed.
2. **Minimal firmware**: 9-axis IMU + 9-DOF Madgwick + BLE notify (int16×10). Verify with
   the **nRF Connect** app (mobile) or a BLE scanner that the characteristic notifies
   coherent data.
3. **Mag calibration**: figure-8 routine + persistence; validate stable yaw pointing north.
4. **Frontend**: Web Bluetooth client + Connect button; verify live rotation with the
   shuttle model still in place.
5. **Rebrand**: Blue Ghost model + Firefly colors + copy; move to `docs/` + enable Pages.

## End-to-end verification
- `pio run -e nano33ble` compiles; `pio run -e nano33ble -t upload` flashes the Nano.
- Read serial (CLAUDE.md method, the Nano's port) → see "IMU OK", the sample rate and,
  after calibrating, mag values with the offset applied.
- Open the GitHub Pages URL in **Chrome** → **Connect** → pick `Firefly-BlueGhost-IMU` →
  tilt/rotate the board: the **Blue Ghost** rotates live, the accel/gyro charts move, and
  the **yaw holds heading without drifting** after calibration. Also test on Chrome for
  Android.

## Risks / notes
- **iOS Safari does not support Web Bluetooth** (accepted decision). On iPhone it would
  only work with apps like "Bluefy"; not a goal.
- **Real ODR < 200 Hz** on the LSM9DS1 (~119 Hz). Sufficient for a 60 Hz display; set the
  filter's `sampleFreq` to the real ODR so the fusion is correct.
- **Mag calibration** is the most delicate part (hard/soft-iron, nearby metal
  interference). If it causes trouble, the first usable milestone is 6-DOF (no absolute
  yaw) and the mag is enabled later.
- **BLE throughput**: 20 bytes @ ~60 Hz is trivial; the int16 packing avoids depending on
  MTU negotiation.
- The repo is not renamed (user decision); only the content is rebranded.
