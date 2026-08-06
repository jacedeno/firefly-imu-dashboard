# Demo runbook — presenting from a Fedora laptop

How to run the Blue Ghost IMU demo on a machine that is **not** the dev desktop.
Written 2026-08-05 for a **Lenovo ThinkPad T470, Fedora 44, x86_64**.

**Scope: presenting only.** The board is already flashed, so you do **not** need
PlatformIO, the ARM toolchain, or any build step on the laptop. Fewer moving
parts is exactly what you want live. (If you ever do need to build/flash on
another machine, that is `TROUBLESHOOTING.md`.)

---

## The one thing that will bite you

**Chrome on Linux ships with Web Bluetooth OFF.** `navigator.bluetooth` does not
exist, so the dashboard's **"Connect device"** button does nothing at all — no
error dialog, no device chooser. The only clue is a small line of grey text under
the buttons reading *"Web Bluetooth unavailable"*.

This is not a bug in the dashboard and not a problem with the board. Always
launch Chrome like this:

```bash
google-chrome --enable-experimental-web-platform-features \
  https://jacedeno.github.io/firefly-imu-dashboard/
```

> The flag must be present **when Chrome starts**. If a Chrome window is already
> open, the command above just opens a tab in the existing process and the flag
> is silently ignored. **Quit Chrome completely first** (check with
> `pgrep -c chrome`), then run it.

Firefox will never work — it does not implement Web Bluetooth at all.

---

## Setup on the laptop (~5 minutes, do this before demo day)

### 1. Chrome

```bash
rpm -q google-chrome-stable || sudo dnf install -y google-chrome-stable
```

If the repo is missing on a fresh Fedora, enable it in **Software → Repositories
→ third-party**, or install the RPM straight from google.com/chrome.

### 2. Bluetooth

```bash
systemctl is-active bluetooth          # want: active
rfkill list bluetooth                  # want: Soft/Hard blocked: no
bluetoothctl show | grep -E "Powered|Controller"
```

If soft-blocked: `rfkill unblock bluetooth`. On a ThinkPad also check the BIOS
and that no laptop-wide airplane-mode hotkey is on.

### 3. Confirm the laptop can see the board

Power the board (any USB port or a USB power bank — it does **not** need a data
connection to run, only power) and wait ~10 s, then:

```bash
timeout 16 bluetoothctl --timeout 13 scan le | grep -i firefly
```

Expect a line with `Firefly-BlueGhost-IMU`. If you see it, the laptop side is
done — everything else is browser configuration.

### 4. Dry-run the whole thing once, at home, on the laptop

Do not let demo day be the first time this laptop talks to the board.

---

## Demo day — the 60-second sequence

1. **Power the board.** Green LED solid. Give it ~10 s.
2. **Quit Chrome entirely** (`pgrep -c chrome` → 0).
3. **Launch with the flag** (command above).
4. Click **"Connect device"**.
5. Pick **`Firefly-BlueGhost-IM`** in the chooser → **Connect**.
6. The overlay disappears, the lander starts tracking, HUD shows ~50–60 Hz.

### Two things that look wrong but are not

- **The chooser shows a truncated name**, `Firefly-BlueGhost-IM` (20 chars).
  That is the BLE advertised local-name limit. The full name appears once
  connected.
- **Yaw drifts for the first ~60 s.** The Madgwick filter starts at the identity
  quaternion and converges asymptotically onto magnetic north. Measured: ~5 °/s
  at boot, decaying to ~0.1 °/s. Pitch and roll are correct immediately.
  **Power the board a minute before you present** and it will be settled.

### Keep the board away from magnets

Heading comes from the magnetometer, and a magnet near the board wrecks it. On
2026-08-05 one spot on the dev desk read **157 µT** where Earth's field is 25–65 —
enough to make the heading land tens of degrees off. Speakers, magnetic phone
mounts, laptop lids, hard drives and metal table frames all do this, and **no
calibration can correct a magnet that stays in the room** rather than on the board.

Check the venue table before presenting — plug in USB and watch the `[run]` line:

```bash
python3 -c "
import serial,glob,time
p=sorted(glob.glob('/dev/ttyACM*'))[0]
s=serial.Serial(p,115200,timeout=1); t=time.time()+8
while time.time()<t:
    l=s.readline()
    if l: print(l.decode(errors='replace').rstrip())
"
# |m| raw should read 25-65. Much higher = magnet nearby, move the board.
```

Pitch and roll are unaffected by magnetic fields — only heading is. If the venue
is magnetically dirty, demo tilt and roll and don't lean on the heading number.

### Presenter's tip

There is a **"Run in demo mode"** button next to Connect. It animates the lander
from a simulated source with no hardware at all. If BLE fails in front of an
audience, click it and keep talking — nobody needs to know.

---

## Offline fallback (recommended for any venue)

The hosted dashboard pulls **Three.js from unpkg.com (9 scripts)** and **fonts
from Google Fonts**. On flaky venue Wi-Fi the page loads but the 3D lander never
renders. Build a fully self-contained copy **while you still have good internet**:

```bash
./tools/make-offline-bundle.sh ~/blueghost-demo
```

Then, at the venue:

```bash
cd ~/blueghost-demo && python3 -m http.server 8765
# quit Chrome, then:
google-chrome --enable-experimental-web-platform-features http://localhost:8765/
```

**`http://localhost` is a secure context**, so Web Bluetooth works over plain
HTTP there — no certificate, no HTTPS, no internet required.

Verified 2026-08-05 with every external host blackholed at the browser level
(`--host-resolver-rules="MAP * 127.0.0.1:9, EXCLUDE localhost"`):

| check | result |
|---|---|
| `isSecureContext` | `true` |
| `navigator.bluetooth` | `true` |
| `THREE`, `OrbitControls`, `UnrealBloomPass` | all loaded |
| Rajdhani webfont | loaded locally |

Copy the whole `~/blueghost-demo` folder to the laptop on a USB stick if you
prefer — it has no external dependencies left.

---

## If it fails live

| Symptom | Cause | Fix |
|---|---|---|
| "Connect device" does nothing, no chooser | Chrome without the flag (most likely) | Quit Chrome fully, relaunch with `--enable-experimental-web-platform-features` |
| Chooser opens but stays empty | Board not powered, or out of range | Check green LED; move closer; `bluetoothctl scan le \| grep -i firefly` |
| Board not in the chooser, but `bluetoothctl` sees it | Something already holds the link — the nRF52840 takes **one** connection | Close old dashboard tabs; `bluetoothctl disconnect <MAC>` |
| **You pick the board, then "Connection Error: Connection attempt failed."** | Stale BlueZ cache entry for the device — very likely after a browser crash or a killed tab | `bluetoothctl remove 03:CD:48:76:F7:2D`, then reconnect. Hit and fixed this way on 2026-08-05. |
| Connected but the lander does not move | Stale tab holding the stream | Reload the page and reconnect |
| Lander spins slowly on its own | Normal post-boot yaw convergence | Wait ~60 s, or power the board earlier |
| Page loads but no 3D lander | CDN blocked / no internet | Use the offline bundle above |
| Nothing works, audience waiting | — | **"Run in demo mode"** |

Board MAC on the current unit: `03:CD:48:76:F7:2D` (it can change if the board is
reflashed with a different stack).

### Useful one-liners

```bash
# is the board advertising and free?
timeout 16 bluetoothctl --timeout 13 scan le | grep -i firefly

# force-release a stale link (closing the browser does NOT always drop it —
# the board can still report "ble connected" with no browser running)
bluetoothctl disconnect 03:CD:48:76:F7:2D

# clear a stale cache entry after a failed connect, then retry in the browser
bluetoothctl remove 03:CD:48:76:F7:2D

# does this Chrome actually have the API? (paste in DevTools console)
'bluetooth' in navigator
```

If you have the USB cable and pyserial, the board narrates its own state — this
is the fastest ground truth, and it tells you `connected` vs `advertising`:

```bash
python3 -c "
import serial,glob,time
p=sorted(glob.glob('/dev/ttyACM*'))[0]
s=serial.Serial(p,115200,timeout=1); t=time.time()+6
while time.time()<t:
    l=s.readline()
    if l: print(l.decode(errors='replace').rstrip())
"
# [run] fuse 113 Hz | q=... | mag READY | ble advertising   <- waiting for a browser
# [run] fuse 113 Hz | q=... | mag READY | ble connected     <- a browser has it
```
