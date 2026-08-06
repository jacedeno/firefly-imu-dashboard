// Firefly Blue Ghost IMU — Arduino Nano 33 BLE (LSM9DS1) firmware.
// Reads the onboard 9-axis IMU, runs Mahony fusion and streams the orientation
// over BLE to the Web Bluetooth dashboard. Currently 6-DOF (USE_MAG=false):
// pitch/roll are absolute, yaw is relative and drifts slowly. See USE_MAG.
//
// BLE characteristic payload = 10x int16 little-endian (20 bytes), matching
// docs/js/ble.js:
//   q.w q.x q.y q.z (x30000)  ax ay az [m/s^2] (x100)  gx gy gz [rad/s] (x1000)
#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Arduino_LSM9DS1.h>
#include "sensor_fusion.h"

// ---- BLE (UUIDs must match docs/js/ble.js) ----
BLEService          imuService("19b10000-e8f2-537e-4f6c-d104768a1214");
BLECharacteristic   imuChar("19b10001-e8f2-537e-4f6c-d104768a1214", BLERead | BLENotify, 20);

// ---- Scales (must match the dashboard de-scaling) ----
static const float Q_SCALE = 30000.0f, A_SCALE = 100.0f, G_SCALE = 1000.0f;
static const float DEG2RAD = 0.01745329252f;
static const float G_MS2   = 9.80665f;

// ---- Magnetometer calibration (re-derived 2026-08-05 by least-squares sphere
// fit over 3016 tumble samples: centre (-4.5, +60.3, -0.2) uT, radius 39.1 uT,
// mean residual 0.80 uT = 2.0% of radius).
// The previous constants (offset Y = +32.1) were 28 uT off on the dominant axis,
// which is what made the heading unrepeatable: returning the board to a marked
// spot landed 50 deg away.
// Soft-iron left at unity on purpose - the Y axis was under-covered in the
// tumble, so a fitted scale there would be worse than no correction at all.
// LSM9DS1 mag axes vs accel/gyro frame: X aligned, Y & Z negated (180deg about X).
static const float MAG_FX = 1.0f, MAG_FY = -1.0f, MAG_FZ = -1.0f;   // axis sign map
static const float MAG_OFF[3] = { -4.5f, 60.3f, -0.2f };            // hard-iron (uT)
static const float MAG_SCL[3] = { 1.0f, 1.0f, 1.0f };               // soft-iron: unity
// 6-DOF is the default. Measured 2026-08-05, rotate-the-board-and-put-it-back
// against a physical mark:
//   9-DOF, old calibration : +50.5 deg error
//   9-DOF, new calibration : -28.5 deg error, >46 s to settle
//   6-DOF                  :  +3.4 deg error,   0 s to settle
// Yaw is then relative rather than magnetic and drifts ~0.9 deg/min, which is
// a far better trade than a heading that lands 28 deg out and hunts for a
// minute. Set true to go back to 9-DOF once the mag axis sign map is re-derived
// (see TROUBLESHOOTING.md).
static const bool  USE_MAG = false;                                 // false = 6-DOF

// Mahony AHRS: Kp=responsiveness, Ki=online gyro-bias estimation (kills yaw drift
// without a perfect boot calibration). Same update()/updateMag()/getters as before.
MahonyFilter filter(0.4f, 0.08f, 119.0f);

// Gyro bias (dps), measured at boot while still.
float gbx = 0, gby = 0, gbz = 0;

// Latest calibrated magnetometer (body frame).
float magX = 0, magY = 0, magZ = 0;
bool  haveMag = false, magReady = false;

uint32_t lastNotify = 0, lastDbg = 0, sampleCount = 0, hzCount = 0, hzTime = 0;
uint32_t lastFuseUs = 0;   // for the measured per-iteration dt
float magNorm = 0, magRawNorm = 0;   // field-strength diagnostics
uint32_t lastMagOkMs = 0;

static int16_t clamp16(float v) {
  if (v > 32767.0f) return 32767;
  if (v < -32768.0f) return -32768;
  return (int16_t)lroundf(v);
}

void calibrateGyro() {
  Serial.println("[cal] keep board STILL — measuring gyro bias...");

  // Let the sensor settle and throw away the first samples: reading immediately
  // after IMU.begin() bakes power-up transients into the bias permanently.
  delay(300);
  for (int i = 0; i < 30; i++) {
    if (IMU.gyroscopeAvailable()) { float x, y, z; IMU.readGyroscope(x, y, z); }
  }

  const int N = 300;
  const float MAX_DPS = 2.0f;   // reject samples taken while the board moved
  double sx = 0, sy = 0, sz = 0; int got = 0, rejected = 0;
  uint32_t t0 = millis();
  while (got < N && millis() - t0 < 5000) {
    if (IMU.gyroscopeAvailable()) {
      float x, y, z; IMU.readGyroscope(x, y, z);
      if (fabsf(x) > MAX_DPS || fabsf(y) > MAX_DPS || fabsf(z) > MAX_DPS) { rejected++; continue; }
      sx += x; sy += y; sz += z; got++;
    }
  }
  if (got >= N / 3) { gbx = sx / got; gby = sy / got; gbz = sz / got; }
  else Serial.println("[cal] WARNING: too few still samples — bias left at 0, keep it still and reset");
  Serial.print("[cal] samples "); Serial.print(got);
  Serial.print(", rejected "); Serial.println(rejected);
  Serial.print("[cal] gyro bias (dps): ");
  Serial.print(gbx, 3); Serial.print(", "); Serial.print(gby, 3); Serial.print(", "); Serial.println(gbz, 3);
}

void onConnect(BLEDevice c) { Serial.print("[ble] connected: "); Serial.println(c.address()); }
void onDisconnect(BLEDevice c) { Serial.println("[ble] disconnected — re-advertising"); BLE.advertise(); }

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) {}

  Serial.println("\n=== Firefly Blue Ghost IMU (Nano 33 BLE / LSM9DS1) ===");

  if (!IMU.begin()) {
    Serial.println("[imu] LSM9DS1 begin() FAILED — halting");
    while (1) { delay(1000); }
  }
  float odr = IMU.accelerationSampleRate();
  filter.setSampleFreq(odr > 1.0f ? odr : 119.0f);
  Serial.print("[imu] LSM9DS1 OK, accel/gyro ODR "); Serial.print(odr); Serial.println(" Hz");

  calibrateGyro();

  if (!BLE.begin()) {
    Serial.println("[ble] begin() FAILED — halting");
    while (1) { delay(1000); }
  }
  BLE.setLocalName("Firefly-BlueGhost-IMU");
  BLE.setDeviceName("Firefly-BlueGhost-IMU");
  BLE.setAdvertisedService(imuService);
  imuService.addCharacteristic(imuChar);
  BLE.addService(imuService);
  uint8_t zero[20] = {0};
  imuChar.writeValue(zero, 20);
  BLE.setConnectionInterval(12, 24); // 15–30 ms → supports ~60 Hz notify
  BLE.setEventHandler(BLEConnected, onConnect);
  BLE.setEventHandler(BLEDisconnected, onDisconnect);
  BLE.advertise();
  Serial.println("[ble] advertising as 'Firefly-BlueGhost-IMU'");
  hzTime = millis();
}

void loop() {
  BLE.poll();

  // Magnetometer (~20 Hz): fixed hard/soft-iron + axis-sign map.
  if (IMU.magneticFieldAvailable()) {
    float rx, ry, rz; IMU.readMagneticField(rx, ry, rz);
    float cx = (rx - MAG_OFF[0]) * MAG_SCL[0] * MAG_FX;
    float cy = (ry - MAG_OFF[1]) * MAG_SCL[1] * MAG_FY;
    float cz = (rz - MAG_OFF[2]) * MAG_SCL[2] * MAG_FZ;
    // Reject implausible field strength. Earth's field is ~25-65 uT; anything
    // outside that is a nearby magnet, a screw or a laptop, and feeding it in
    // would swing the heading reference. Without this the magnetometer fails
    // silently and yaw simply ends up somewhere wrong.
    float mn = sqrtf(cx*cx + cy*cy + cz*cz);
    magNorm = mn;
    magRawNorm = sqrtf(rx*rx + ry*ry + rz*rz);
    if (mn > 20.0f && mn < 80.0f) {
      magX = cx; magY = cy; magZ = cz;
      haveMag = true; magReady = true;
      lastMagOkMs = millis();
    } else {
      magReady = false;
      // Do NOT keep feeding the last good vector: frozen in the body frame it
      // rotates with the board and actively drags the heading. After a short
      // grace period drop to 6-DOF, where yaw free-runs on the gyro instead of
      // being pulled somewhere wrong.
      if (millis() - lastMagOkMs > 1000) haveMag = false;
    }
  }

  // Accel + gyro (~119 Hz): run fusion.
  if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
    float ax, ay, az, gx, gy, gz;
    IMU.readAcceleration(ax, ay, az);          // g
    IMU.readGyroscope(gx, gy, gz);             // dps
    float rgx = (gx - gbx) * DEG2RAD;
    float rgy = (gy - gby) * DEG2RAD;
    float rgz = (gz - gbz) * DEG2RAD;

    // Measure the real timestep. The nominal 119 Hz is a library constant, not
    // a measurement; the loop actually runs 107-118 Hz, and integrating with a
    // dt ~5% short makes every rotation come up short.
    uint32_t nowUs = micros();
    if (lastFuseUs != 0) filter.setDt((nowUs - lastFuseUs) * 1e-6f);
    lastFuseUs = nowUs;

    if (USE_MAG && haveMag) filter.updateMag(rgx, rgy, rgz, ax, ay, az, magX, magY, magZ);
    else                    filter.update(rgx, rgy, rgz, ax, ay, az);

    sampleCount++; hzCount++;

    uint32_t now = millis();
    if (now - lastNotify >= 16 && BLE.connected()) {  // ~60 Hz
      lastNotify = now;
      int16_t p[10];
      p[0] = clamp16(filter.w() * Q_SCALE);
      p[1] = clamp16(filter.x() * Q_SCALE);
      p[2] = clamp16(filter.y() * Q_SCALE);
      p[3] = clamp16(filter.z() * Q_SCALE);
      p[4] = clamp16(ax * G_MS2 * A_SCALE);
      p[5] = clamp16(ay * G_MS2 * A_SCALE);
      p[6] = clamp16(az * G_MS2 * A_SCALE);
      p[7] = clamp16(rgx * G_SCALE);
      p[8] = clamp16(rgy * G_SCALE);
      p[9] = clamp16(rgz * G_SCALE);
      imuChar.writeValue((uint8_t *)p, 20);     // Cortex-M is little-endian
    }
  }

  uint32_t now = millis();
  if (now - lastDbg >= 1000) {
    float hz = hzCount * 1000.0f / (now - hzTime);
    hzCount = 0; hzTime = now; lastDbg = now;
    Serial.print("[run] fuse "); Serial.print(hz, 0); Serial.print(" Hz | q=");
    Serial.print(filter.w(), 3); Serial.print(","); Serial.print(filter.x(), 3); Serial.print(",");
    Serial.print(filter.y(), 3); Serial.print(","); Serial.print(filter.z(), 3);
    // |m| is the fastest way to spot a bad magnetic environment: raw should be
    // 25-65 uT and cal should sit near the calibration radius (~39 uT). A raw
    // reading of ~157 uT is what exposed a magnet under the desk.
    Serial.print(" | |m| raw "); Serial.print(magRawNorm, 1);
    Serial.print(" cal "); Serial.print(magNorm, 1);
    Serial.print(" | mag "); Serial.print(magReady ? "READY" : "REJECTED");
    Serial.print(" | ble "); Serial.println(BLE.connected() ? "connected" : "advertising");
  }
}
