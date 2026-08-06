# Troubleshooting — build & flash (Nano 33 BLE)

Running log of two hardware bring-up sessions — **2026-06-01** (aarch64 Asahi
machine, blocked) and **2026-08-05** (x86_64 Fedora host `C2-B5`, **resolved**) —
and the procedures that came out of them.

## TL;DR

- **The toolchain is the whole story.** The mbed core for `nano33ble` needs the
  platform-default **GCC 7.2** (`toolchain-gccarmnoneeabi ~1.70201.0`), which has
  **no `linux_aarch64` binary**. Images built with an arm64 GCC compile cleanly
  and **never boot** — so they never bring up USB.
- **On an x86_64 host, build and flash locally — it just works:**
  `pio run -e nano33ble -t upload`. Verified 2026-08-05.
- **On an aarch64 host, do not build locally.** Use the **GitHub Actions** x86
  build and flash the downloaded `firmware.bin` (fallback path, below).
- **The board is fine.** The 2026-06-01 "board won't enumerate" blocker was
  never a cable or a dead board — see "Resolved" below.
- **On a fresh Fedora host, install the PlatformIO udev rules** so ModemManager
  stops probing `/dev/ttyACM*` — see "Host setup" under Environment notes.

## Root cause: toolchain, not the firmware

The Mahony firmware (commit `a1f6438`) was suspected of hard-faulting, but it is
**not** the cause. Evidence:

- A **minimal** sketch (just `Serial` + LED blink, no IMU, no BLE) built on the
  Asahi machine **also fails to boot** — no amber LED blink, no USB, no BLE.
- Reproduced with **GCC 14.2, 12.3 and 10.3** (all the arm64 toolchains available
  in the PlatformIO registry). All compile cleanly; none produce a bootable image.
- The diff between the last-working firmware (`d051228`, 9-DOF) and Mahony
  (`a1f6438`) is **only** the filter class — `setup()`/USB/BLE init are identical.

The Arduino **mbed** core for `nano33ble` is validated against **GCC 7.2**
(`toolchain-gccarmnoneeabi ~1.70201.0`, the platform default). That version has
**no `linux_aarch64` binary**. The arm64 GCCs that do exist (10/12/14) miscompile
the mbed core badly enough that the image never enumerates USB or runs.

### Why it "worked before"

It was previously built on the **same physical machine when it ran macOS** (M1
MacBook Air). macOS has no arm64 GCC 7.2 either, but it transparently ran the
**x86_64 GCC 7.2 under Rosetta 2**, producing a correct binary. The machine was
reinstalled with **Fedora Asahi** (arm64, **16 KB page kernel**) the day before.
Linux has no Rosetta equivalent, and the 16K-page Asahi kernel **breaks qemu-user
x86 emulation**, so a local x86 build (Podman `--platform linux/amd64`) is also a
dead end here — `podman run` of any amd64 image fails to exec.

## Solution A (preferred): build and flash locally on an x86_64 host

On x86_64, PlatformIO resolves the correct default toolchain
(`toolchain-gccarmnoneeabi @ 1.70201.0` = **GCC 7.2.1**) and the normal one-liner
works end to end — no CI, no manual bossac, no double-tap:

```bash
pio run -e nano33ble -t upload --upload-port /dev/ttyACM0
```

The 1200 bps touch that PlatformIO performs to enter the bootloader works even
when the board is **already sitting in the bootloader**, so no manual RESET
choreography is needed. Verified 2026-08-05: 81 pages written, RAM 26.3%,
Flash 33.6% — the same footprint the x86 CI build produces.

## Solution B (fallback): build on x86 via GitHub Actions, flash locally

Use this **only on an aarch64 host**, where no local GCC 7.2 exists.

`.github/workflows/build-firmware.yml` builds on `ubuntu-latest` (x86_64), which
uses the correct default **GCC 7.2.1**, and uploads `firmware.bin/elf` as the
`firmware-nano33ble` artifact. Confirmed working build: RAM 26.3%, Flash 33.6%.

### Build + download

```bash
# trigger (push to src/** or platformio.ini auto-triggers; or manually:)
gh workflow run build-firmware.yml
gh run watch <run-id> --exit-status

# download the artifact
gh run download <run-id> -n firmware-nano33ble -D /tmp/fw-ci
```

### Flash the CI artifact

On an aarch64 host the local PlatformIO build is unusable (no arm64 GCC 7.2), so
`pio run -t upload` must NOT be used — it would rebuild. Flash the CI `.bin`
directly with the bundled bossac. If the board currently holds a **non-booting**
image it never enumerates as an app, so the 1200 bps auto-reset has nothing to
talk to — **enter the bootloader by hand (double-tap RESET, amber LED
breathing)**, then:

```bash
# user must be in the `dialout` group (sudo usermod -aG dialout $USER; re-login)
# prefix with `sg dialout -c '...'` only if the current shell predates that change.
PORT=$(ls /dev/ttyACM* | head -1)   # may be ttyACM0, ttyACM1, ... after re-enumeration
~/.platformio/packages/tool-bossac-nordicnrf52/bossac \
  --port "$(basename $PORT)" --write --erase -U --reset /tmp/fw-ci/firmware.bin
```

Because the bootloader window is only ~10 s, the reliable pattern is a watcher
that flashes the instant the port appears (run it, then double-tap):

```bash
for i in $(seq 1 120); do
  port=$(ls /dev/ttyACM* 2>/dev/null | head -1)
  if [ -n "$port" ]; then
    ~/.platformio/packages/tool-bossac-nordicnrf52/bossac \
      --port "$(basename $port)" --write --erase -U --reset /tmp/fw-ci/firmware.bin
    exit $?
  fi
  sleep 0.5
done; echo timeout; exit 1
```

## Verify after flashing

```bash
# USB serial diagnostics (115200) — system python3 has pyserial 3.5
python3 -c "
import serial,time; s=serial.Serial('/dev/ttyACM0',115200,timeout=1); t=time.time()+10
while time.time()<t:
    l=s.readline()
    if l: print(l.decode(errors='replace').rstrip())
"
# At boot: [imu] LSM9DS1 OK, accel/gyro ODR NN Hz / [ble] advertising as 'Firefly-BlueGhost-IMU'
# Steady state: [run] fuse NN Hz | q=... | mag READY | ble advertising

# BLE advertising check
timeout 16 bluetoothctl --timeout 13 scan le | grep -i firefly
```

### Catching the boot banner: flash, wait ~4 s, then attach

The banner lines print **once at boot**, so a reader must attach during a narrow
window. The reliable recipe (works once the udev rules below are installed):

```bash
pio run -e nano33ble -t upload --upload-port "$(ls /dev/ttyACM* | head -1)"
sleep 4          # let bossac reset the board and the app re-enumerate
python3 -c "
import serial,glob,time
p=sorted(glob.glob('/dev/ttyACM*'))[0]
s=serial.Serial(p,115200,timeout=1); t=time.time()+8
while time.time()<t:
    l=s.readline()
    if l: print(l.decode(errors='replace').rstrip())
"
```

This reliably captures `[cal] gyro bias (dps): ...` and
`[ble] advertising as 'Firefly-BlueGhost-IMU'`. The earlier
`[imu] LSM9DS1 OK` line prints before the gyro-bias calibration finishes and is
usually already gone — that is expected, not a fault.

**The `sleep` matters.** Attaching *too fast* (a tight poll loop, ~0.3 s) grabs
the **stale pre-reset device node** or the *bootloader's* CDC port, which then
goes away — you get an open handle and zero bytes. A DTR toggle does not help
either; the mbed core does not reset on DTR.

Note the port number **shifts between `ttyACM0` and `ttyACM1`** across
re-enumerations, so always discover it (`ls /dev/ttyACM*`) rather than hardcoding.

> **Superseded note:** an earlier revision of this file claimed the banner was
> "effectively unobservable" and blamed USB CDC timing. That was wrong — the real
> obstacle was **ModemManager probing `/dev/ttyACM*`**. With the udev rules
> installed (see "Host setup"), the banner is capturable.

Confirmed working 2026-08-05: fuse rate **106–118 Hz**, `mag READY`,
`ble advertising`, and `bluetoothctl` sees `Firefly-BlueGhost-IMU`
(`03:CD:48:76:F7:2D`). Reflashed 4× over the session, booting unaided every time.

### Expected: yaw settles for ~1 minute after boot

The fusion filter starts at the identity quaternion and pulls yaw toward the
magnetic heading asymptotically. Measured on 2026-08-05: yaw rotated at ~5.2 °/s
right after boot, decayed to 0.60 °/s, then settled at ~102° with the rate
oscillating around zero (±0.15 °/s, sign-changing — i.e. noise, not drift).

**A stationary board whose yaw is still moving shortly after boot is converging,
not drifting.** Let it sit ~60 s before judging yaw quality.

## Resolved: the 2026-06-01 "board won't enumerate" blocker

**This was never a cable and never dead hardware.** The earlier revision of this
file blamed a "marginal USB-C data cable"; that diagnosis was wrong and
contradicted the toolchain root cause documented above in the same file.

What was actually happening: the board was holding an image built with an **arm64
GCC**. That image never boots, so it never brings up USB, so the board never
enumerates as an app (`2341:805a`). The **bootloader** enumerated fine
(`2341:005a`) precisely because Arduino compiled the bootloader with the correct
toolchain — the same observation that was read as "the cable works sometimes".

Evidence it is resolved (2026-08-05, host `C2-B5`, x86_64):

- Green power LED solid; amber LED breathes on double-tap RESET.
- Bootloader enumerates as `2341:005a` on `/dev/ttyACM0`.
- A minimal blink+serial sketch built **locally on x86_64** flashed cleanly
  (19 pages) and boots unaided, enumerating as `2341:805a` with **no double-tap**
  and printing over USB CDC at 115200. Smoke build: RAM 16.2%, Flash 7.7%.
- The real firmware then built locally (RAM 26.3%, Flash 33.6%), flashed
  (81 pages), booted on its own, and both serial and BLE verified — see above.
- Same USB-C cable throughout.

**Rule of thumb:** on this board, "stops enumerating after a flash" means a **bad
image**, not dead hardware. Recovery is always: double-tap RESET (amber LED
breathing) → reflash a known-good image.

## Dashboard: Web Bluetooth needs a flag on Chrome for Linux

**Symptom:** clicking **"Connect device"** does nothing — the Chrome device
chooser never opens. Diagnosed 2026-08-05 on `C2-B5`.

**Cause:** Chrome on **Linux** does not enable Web Bluetooth by default.
`navigator.bluetooth` is simply **absent**, so `ble.js`'s `bleSupported()`
(`'bluetooth' in navigator`) returns false and `start()` throws before ever
calling `requestDevice()`. Verified over CDP against Chrome 151:

| | `'bluetooth' in navigator` | `getAvailability()` |
|---|---|---|
| default launch | `false` | — (no API) |
| `--enable-experimental-web-platform-features` | `true` | `true` |

`window.isSecureContext` was `true` in both cases, so **HTTPS was never the
problem** — GitHub Pages is fine.

The failure is easy to miss: the error lands in the small `#overlay-note`
paragraph under the buttons, which reads *"Web Bluetooth unavailable — use
Chrome/Edge (desktop or Android). Try demo mode."*

### Fix

Launch Chrome with the flag (keeps the normal profile, does not persist):

```bash
google-chrome --enable-experimental-web-platform-features \
  https://jacedeno.github.io/firefly-imu-dashboard/
```

Or make it permanent for all browsing via
`chrome://flags/#enable-experimental-web-platform-features` → **Relaunch**. Note
this enables *all* experimental web platform features, so the per-launch flag is
the tidier option for a daily-driver browser.

### Verified end to end (2026-08-05)

Automated through CDP (`DeviceAccess.enable` + `selectPrompt` to accept the
chooser programmatically): the dashboard connected and streamed at **53–56 Hz**,
60 FPS, tier HIGH, with the heading updating live. Notes:

- Chrome's chooser shows the name **truncated to 20 chars**
  (`Firefly-BlueGhost-IM`) because that is the BLE advertised local-name limit.
  The full `Firefly-BlueGhost-IMU` appears once connected (GATT device name).
- The first `DeviceAccess.deviceRequestPrompted` event often carries an **empty**
  device list; Chrome emits further events as it discovers. Wait for the update.

### Only one connection at a time

The nRF52840 accepts a **single** central. If the board reports `ble connected`
on serial but nothing is using it, a stale BlueZ link is holding it — clear it:

```bash
bluetoothctl disconnect 03:CD:48:76:F7:2D   # board MAC
```

Serial then returns to `ble advertising`. A leftover dashboard tab (or a stray
`bleak` script) will also hold the link.

## Heading is not repeatable — magnetometer investigation (2026-08-05)

**Symptom:** rotate the board and bring it back to a marked spot, and the heading
lands tens of degrees away. Numbers also appear to "hunt" for a resting place
after every movement.

**Partly fixed, not solved.** Return error went from **+50.5° to −28.5°**. What
follows is the measured evidence so none of it has to be re-derived.

### First: measure before theorising

Serial works at the same time as BLE, so the board can be logged while the
dashboard is connected. Stationary on the desk, the yaw readout is far steadier
than it looks:

| | value |
|---|---|
| yaw drift, stationary | −0.24 °/min |
| yaw peak-to-peak over 91 s | 0.47° |
| real fusion rate | 107–118 Hz (mean 113.4) |

So the "numbers keep changing" complaint at rest is ±0.25° — sensor noise
flickering the last displayed decimal, not drift.

### Root cause: a magnet in the desk, not the code

The decisive measurement was the raw field strength. Earth's field is 25–65 µT:

| where | \|m\| raw |
|---|---|
| the board's usual spot on the desk | **157 µT**, rock stable |
| anywhere else in the room | 25–65 µT (normal) |

That spot has a local source roughly tripling Earth's field, almost all on the Y
axis (+136 µT). **A magnet fixed in the world frame cannot be calibrated out** —
hard/soft-iron calibration only corrects sources that move *with* the board. The
heading was being referenced to a corrupted field, and putting the board back on
its mark put it back in the anomaly.

This is why the firmware now prints `|m| raw` and `cal` on the `[run]` line. It
is the fastest possible check for a bad magnetic environment.

### Second problem: the stored calibration was 28 µT wrong

Re-derived on 2026-08-05 in a clean spot by least-squares sphere fit over 3016
tumble samples:

| | old (2026-05-25) | new |
|---|---|---|
| hard-iron X | −5.7 | −4.5 |
| **hard-iron Y** | **+32.1** | **+60.3** |
| hard-iron Z | −4.5 | −0.2 |
| soft-iron | 1.053, 1.030, 0.927 | 1.0, 1.0, 1.0 |

Fit quality: radius 39.1 µT (plausible), mean residual 0.80 µT = 2.0% of radius.
Soft-iron deliberately left at unity — the Y axis was under-covered in the
tumble, so a fitted scale there would be worse than no correction.

### Other firmware defects fixed at the same time

- **Fixed `dt`.** The filter integrated at a hardcoded 119 Hz while the loop
  really runs 107–118 Hz — a systematic ~5% gyro under-integration, so every
  rotation fell short. Now measured per iteration with `micros()`.
  `IMU.accelerationSampleRate()` returns a library **constant**, not a
  measurement, so it cannot be used for this.
- **Unbounded Mahony integral.** `_ibx/_iby/_ibz` had no clamp and no leak with
  `twoKi = 0.16`; transients wound it up and it took tens of seconds to unwind.
  Now clamped to ±0.05 rad/s (~2.9 dps), the physical scale of real gyro bias.
- **No accelerometer validity gate.** Any nonzero vector was taken as gravity, so
  hand acceleration during a move corrupted the tilt reference — which in 9-DOF
  couples straight into heading. Now requires 0.75–1.25 g.
- **Frozen magnetometer vector.** When the field is rejected, the code used to
  keep feeding the last good vector; frozen in the body frame it rotates with the
  board and actively drags the heading. Now falls back to 6-DOF after 1 s.
- **Gyro bias calibration** now settles 300 ms, discards 30 warm-up samples,
  takes 300 samples and rejects any above 2 dps, and warns instead of silently
  using a bad average.

Cost: stationary noise went from 0.47° to 0.80° peak-to-peak (the integral clamp
absorbs less), with drift unchanged at −0.21 °/min. Accepted.

### Still open — next suspect is the axis sign map

`MAG_FX/FY/FZ = (+1, −1, −1)` was derived in May **using the hard-iron constants
now known to be 28 µT wrong**, so the derivation itself is suspect. A wrong sign
map puts the magnetic vector in a different frame from accel/gyro, producing a
heading error that varies with attitude — exactly the remaining symptom.

To re-derive: log accel and mag together through a full tumble, then pick the
combination minimising the variance of `accel · mag` (that dot product is
invariant, since both are world-fixed vectors). Needs good coverage on **all
three** axes — X and Z reached ~80 µT of range easily, Y only 28 µT, and Y
coverage requires standing the board on its long edge.

**Fallback if the magnetometer stays unreliable:** set `USE_MAG = false` for
6-DOF. Pitch and roll stay exact and rock steady; yaw free-runs on the gyro and
is no longer an absolute heading. For a demo where the board is tilted by hand
that often reads *better* — nothing wanders.

## Plan B: replacement MCU — NOT NEEDED (researched 2026-07-29)

> **Superseded 2026-08-05.** The board is confirmed **fully functional** and the
> project firmware runs on it — see "Resolved" above. Nothing here needs to be
> bought. Kept only as prior research in case the board fails later.

If the board is ever confirmed dead, these are the replacement options **with US
stock** (Seeed's own store ships from China, which was ruled out):

| Board | ~Price | IMU | Verdict |
|---|---|---|---|
| **Adafruit Feather nRF52840 Sense** ([#4516](https://www.adafruit.com/product/4516)) | $32 | **9-DOF onboard**: LSM6DS3TR-C (accel+gyro) + LIS3MDL (mag) | **Recommended** — only US-stock board that replicates the full project (absolute yaw) on one PCB. Adafruit (NY), DigiKey, Mouser, Jameco. |
| Arduino Nano 33 BLE Sense Rev2 | $40–45 | BMI270 + BMM150 (9-DOF) | Drop-in: only swap the IMU lib to `Arduino_BMI270_BMM150` (already planned in CLAUDE.md). But inherits the mbed/GCC 7.2 toolchain pain — keeps the CI-build requirement. DigiKey/Mouser/Amazon US. |
| Seeed XIAO nRF52840 Sense ([Amazon US](https://www.amazon.com/Seeed-Studio-XIAO-nRF52840-Sense/dp/B09T94SZ8K)) | $17 | LSM6DS3TR-C (6-axis only, **no magnetometer**) | Cheapest, Prime stock — but 6-DOF only (yaw drifts; `sensor_fusion.h` fallback `update()`). Full 9-DOF needs an external mag module. |

### Porting notes (Feather nRF52840 Sense)

- Same nRF52840 SoC → BLE GATT notify design and the Web Bluetooth dashboard are
  unchanged (device name aside).
- Uses the **Adafruit nRF52 core (non-mbed)**, which builds with modern GCC → it
  would allow local builds on an **aarch64** host (the GCC 7.2 root cause above is
  mbed-core-specific). Note this advantage is moot on an x86_64 host, where the
  mbed core already builds locally. Trade-off: BLE stack is **Bluefruit**, not
  ArduinoBLE — the GATT service/characteristic code in `src/main.cpp` needs a
  straightforward port.
- IMU libs: `Adafruit_LSM6DS` (accel/gyro) + `Adafruit_LIS3MDL` (mag); feed the
  real ODR as the filter `sampleFreq`. Packing/notify (10× int16, 20 B) is
  unchanged.

## Environment notes

### Current host — `C2-B5` desktop (as of 2026-08-05)

- **HP EliteDesk 800 G6 SFF**, Intel **i7-10700** (16 threads), **31 GiB** RAM.
- **Fedora 44**, kernel `7.1.6-201.fc44.x86_64`, **`x86_64`**.
- Local firmware builds work; **CI is not needed here.** A clean full rebuild
  takes **~3.2 s** (vs. ~8 s incremental on the old machine).
- PlatformIO Core **6.1.19** installed system-wide at **`/usr/bin/pio`** (on PATH).
- Toolchain resolved: `toolchain-gccarmnoneeabi @ 1.70201.0` (**GCC 7.2.1**) — the
  version the mbed core requires. Also `framework-arduino-mbed @ 4.6.0`,
  `tool-bossac-nordicnrf52 @ 1.10901.201022`.
- Libraries: `ArduinoBLE @ 1.5.0`, `Arduino_LSM9DS1 @ 1.1.1`.
- System `python3` is **3.14.6** with **pyserial 3.5** — no need for the
  PlatformIO penv python.
- User is in `dialout`; `/dev/ttyACM*` is accessible directly, **no `sg` wrapper**.
- `pio run -t upload --upload-port /dev/ttyACM0` works directly, including when
  the board is already sitting in the bootloader.
- Board confirmed **Rev1 / LSM9DS1** (per `platformio.ini`), so the current
  `lib_deps` (`Arduino_LSM9DS1`) are correct.
- Bootloader enumerates as `2341:005a`; running app as `2341:805a`.
- **BlueZ 5.87**, adapter `hci0` (`10:7C:61:33:52:93`), powered, not rfkill'd.
- **Google Chrome 151** at `/usr/bin/google-chrome` — needed for Web Bluetooth
  (Firefox does **not** implement Web Bluetooth).
- GitHub Pages live and serving the dashboard over HTTPS (HTTP 200) from
  `main` / `/docs`: <https://jacedeno.github.io/firefly-imu-dashboard/>

### Host setup — udev rules (do this on any new machine)

Fedora runs **ModemManager**, which probes `/dev/ttyACM*` on appearance. On this
board that races the bootloader window and the app's first moments — it was what
made the boot banner impossible to capture. Install PlatformIO's official rules,
which set `MODE:="0666"` plus `ID_MM_DEVICE_IGNORE` / `ID_MM_PORT_IGNORE` for
Arduino's `2341` VID (matching both `005a` bootloader and `805a` app):

```bash
curl -fsSL -o /tmp/99-platformio-udev.rules \
  https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules
sudo install -m 0644 -o root -g root /tmp/99-platformio-udev.rules \
  /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=tty
```

Verify (replug the board first if it was already connected):

```bash
udevadm info -q property -n /dev/ttyACM0 | grep -E "ID_MM_(DEVICE|PORT)_IGNORE|ID_VENDOR_ID"
# expect ID_VENDOR_ID=2341, ID_MM_DEVICE_IGNORE=1, ID_MM_PORT_IGNORE=1
```

Installed on `C2-B5` on 2026-08-05. Note the `0666` mode also removes the hard
dependency on the `dialout` group, though being in `dialout` is still fine.

### Retired host — MacBook Air M1 / Fedora Asahi (2026-06-01 session)

> Work moved off this machine to the `C2-B5` desktop on 2026-08-05.

Kept for context; **this machine is no longer in use.** Everything below is why
the CI fallback exists at all.

- `aarch64`, kernel `…asahi…+16k`. No local GCC 7.2 → local builds unusable.
- PlatformIO Core was at `~/.platformio/penv/bin/pio` (not on PATH).
- The 16K-page kernel broke qemu-user x86 emulation, ruling out a containerised
  x86 build as well.
