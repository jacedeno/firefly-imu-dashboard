// Firefly Blue Ghost IMU — Arduino Nano 33 BLE (LSM9DS1) firmware.
// Reads the onboard 9-axis IMU, runs 9-DOF Madgwick fusion (with a running
// hard-iron magnetometer calibration), and streams the orientation over BLE
// to the Web Bluetooth dashboard.
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

// ---- Magnetometer axis sign correction (LSM9DS1 frame vs accel/gyro).
// If heading turns the wrong way or is unstable, flip one of these to -1.
static const float MAG_FX = 1.0f, MAG_FY = 1.0f, MAG_FZ = 1.0f;

MadgwickFilter filter(0.12f, 119.0f);

// Gyro bias (dps), measured at boot while still.
float gbx = 0, gby = 0, gbz = 0;

// Running hard-iron min/max (raw uT) + latest calibrated mag.
float mnX = 1e9, mnY = 1e9, mnZ = 1e9, mxX = -1e9, mxY = -1e9, mxZ = -1e9;
float magX = 0, magY = 0, magZ = 0;
bool  haveMag = false, magReady = false;

uint32_t lastNotify = 0, lastDbg = 0, sampleCount = 0, hzCount = 0, hzTime = 0;

static int16_t clamp16(float v) {
  if (v > 32767.0f) return 32767;
  if (v < -32768.0f) return -32768;
  return (int16_t)lroundf(v);
}

void calibrateGyro() {
  Serial.println("[cal] keep board STILL — measuring gyro bias...");
  const int N = 200;
  double sx = 0, sy = 0, sz = 0; int got = 0;
  uint32_t t0 = millis();
  while (got < N && millis() - t0 < 4000) {
    if (IMU.gyroscopeAvailable()) {
      float x, y, z; IMU.readGyroscope(x, y, z);
      sx += x; sy += y; sz += z; got++;
    }
  }
  if (got > 0) { gbx = sx / got; gby = sy / got; gbz = sz / got; }
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

  // Magnetometer (~20 Hz): update running hard-iron calibration.
  if (IMU.magneticFieldAvailable()) {
    float rx, ry, rz; IMU.readMagneticField(rx, ry, rz);
    mnX = min(mnX, rx); mxX = max(mxX, rx);
    mnY = min(mnY, ry); mxY = max(mxY, ry);
    mnZ = min(mnZ, rz); mxZ = max(mxZ, rz);
    magX = (rx - (mnX + mxX) * 0.5f) * MAG_FX;
    magY = (ry - (mnY + mxY) * 0.5f) * MAG_FY;
    magZ = (rz - (mnZ + mxZ) * 0.5f) * MAG_FZ;
    haveMag = true;
    magReady = (mxX - mnX) > 25 && (mxY - mnY) > 25 && (mxZ - mnZ) > 25;
  }

  // Accel + gyro (~119 Hz): run fusion.
  if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
    float ax, ay, az, gx, gy, gz;
    IMU.readAcceleration(ax, ay, az);          // g
    IMU.readGyroscope(gx, gy, gz);             // dps
    float rgx = (gx - gbx) * DEG2RAD;
    float rgy = (gy - gby) * DEG2RAD;
    float rgz = (gz - gbz) * DEG2RAD;

    if (magReady && haveMag) filter.updateMag(rgx, rgy, rgz, ax, ay, az, magX, magY, magZ);
    else                     filter.update(rgx, rgy, rgz, ax, ay, az);

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
    Serial.print(" | mag "); Serial.print(magReady ? "READY" : "calibrating(move it)");
    Serial.print(" | ble "); Serial.println(BLE.connected() ? "connected" : "advertising");
  }
}
