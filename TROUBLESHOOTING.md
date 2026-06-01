# Troubleshooting — build & flash (Nano 33 BLE)

Running log of a hardware bring-up session (2026-06-01) and the procedures that
came out of it. Read this before trying to build/flash on the Asahi machine.

## TL;DR

- **Do NOT build firmware on the aarch64 (Asahi Linux) machine.** It compiles but
  produces an image that does not boot. Build on x86 via **GitHub Actions** and
  flash the resulting `firmware.bin` locally.
- Flashing from the Asahi machine works fine (bossac is native arm64).
- Current open blocker: the board's USB only enumerates intermittently — see
  "Open issue" below. Likely a marginal USB-C data cable.

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

## Solution: build on x86 (GitHub Actions), flash locally

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

### Flash (from the Asahi machine)

The local PlatformIO build is intentionally broken (no arm64 GCC 7.2), so
`pio run -t upload` will NOT work — it would rebuild. Flash the CI `.bin`
directly with the bundled bossac. The Nano 33 BLE app won't enumerate until it
boots, so the 1200 bps auto-reset does not apply — **enter the bootloader by
hand (double-tap RESET, amber LED breathing)**, then:

```bash
# user must be in the `dialout` group (sudo usermod -aG dialout $USER; re-login)
# run under `sg dialout -c '...'` if the current shell predates the group change.
PORT=$(ls /dev/ttyACM* | head -1)   # may be ttyACM0, ttyACM1, ... after re-enumeration
~/.platformio/packages/tool-bossac-nordicnrf52/bossac \
  --port "$(basename $PORT)" --write --erase -U --reset /tmp/fw-ci/firmware.bin
```

Because the bootloader window is only ~10 s, the reliable pattern is a watcher
that flashes the instant the port appears (run it, then double-tap):

```bash
sg dialout -c '
  for i in $(seq 1 120); do
    port=$(ls /dev/ttyACM* 2>/dev/null | head -1)
    if [ -n "$port" ]; then
      ~/.platformio/packages/tool-bossac-nordicnrf52/bossac \
        --port "$(basename $port)" --write --erase -U --reset /tmp/fw-ci/firmware.bin
      exit $?
    fi
    sleep 0.5
  done; echo timeout; exit 1'
```

### Verify after flashing

```bash
# USB serial diagnostics (115200)
PORT=$(ls /dev/ttyACM* | head -1)
sg dialout -c "~/.platformio/penv/bin/python3 -c \"
import serial,time; s=serial.Serial('$PORT',115200,timeout=1); t=time.time()+10
while time.time()<t:
    l=s.readline()
    if l: print(l.decode(errors='replace').rstrip())
\""
# Expect: [imu] LSM9DS1 OK ... / [ble] advertising as 'Firefly-BlueGhost-IMU' / [run] fuse NN Hz

# BLE advertising check
timeout 16 bluetoothctl --timeout 13 scan le | grep -i firefly
```

## Open issue (blocker as of 2026-06-01)

After several dozen connect/flash cycles the board's USB stopped enumerating
reliably:

- Double-tap RESET → **amber LED breathes** (bootloader IS running) and the
  **green power LED stays on**, but **no `/dev/ttyACM*` and nothing in `lsusb`**
  (`2341:*` never appears) for 40+ s.
- Earlier in the same session the same cable enumerated the bootloader fine
  (`2341:005A`), and we successfully flashed 3× — so the mechanism works.

Most likely **marginal USB-C data cable** (power pins seat, data pins
intermittent) or a **wedged host USB controller** after many re-enumerations.

### To try next (need hardware on hand)

1. **A different known-good USB *data* cable** (the #1 suspect — only had one).
2. Plug into the **other USB-C port** on the MacBook Air (fresh controller path).
3. Reboot the laptop if the host xHCI is wedged.
4. Once any `/dev/ttyACM*` shows on double-tap, use the watcher above to flash the
   CI `firmware.bin`.

## Environment notes

- Host: MacBook Air **M1**, **Fedora Asahi** (`aarch64`, kernel `…asahi…+16k`).
- User must be in `dialout` for port access (done this session).
- PlatformIO Core installed in `~/.platformio/penv/bin/pio` (not on PATH).
- Board confirmed **Rev1 / LSM9DS1** (per `platformio.ini`).
- Bootloader enumerates as `2341:005A`; running app as `2341:805A`.
