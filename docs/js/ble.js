// Web Bluetooth source. Connects to the Nano 33 BLE Sense GATT service and
// subscribes to the IMU characteristic (notify). Payload = 10x int16 LE:
//   q.w q.x q.y q.z  ax ay az  gx gy gz
// De-scaling below MUST match the firmware packer (see PLAN.md):
//   quaternion: int16 = q  * 30000        -> q  = v / 30000   (range +/-1.09)
//   accel m/s²: int16 = a  * 100          -> a  = v / 100      (range +/-327)
//   gyro rad/s: int16 = g  * 1000         -> g  = v / 1000     (range +/-32.7)
export const SERVICE_UUID = '19b10000-e8f2-537e-4f6c-d104768a1214';
export const CHAR_UUID    = '19b10001-e8f2-537e-4f6c-d104768a1214';

const Q_SCALE = 30000, A_SCALE = 100, G_SCALE = 1000;

export function bleSupported() {
  return typeof navigator !== 'undefined' && 'bluetooth' in navigator;
}

export function createBleSource() {
  let device = null, characteristic = null, onDataCb = null;

  function handleValue(ev) {
    const dv = ev.target.value; // DataView, 20 bytes
    if (dv.byteLength < 20) return;
    const v = (i) => dv.getInt16(i * 2, true);
    onDataCb && onDataCb({
      w: v(0) / Q_SCALE, x: v(1) / Q_SCALE, y: v(2) / Q_SCALE, z: v(3) / Q_SCALE,
      ax: v(4) / A_SCALE, ay: v(5) / A_SCALE, az: v(6) / A_SCALE,
      gx: v(7) / G_SCALE, gy: v(8) / G_SCALE, gz: v(9) / G_SCALE,
    });
  }

  return {
    name: 'BLE',
    live: true,
    async start(onData) {
      if (!bleSupported()) throw new Error('Web Bluetooth not supported in this browser.');
      onDataCb = onData;
      device = await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(SERVICE_UUID);
      characteristic = await service.getCharacteristic(CHAR_UUID);
      await characteristic.startNotifications();
      characteristic.addEventListener('characteristicvaluechanged', handleValue);
      return device.name || 'Blue Ghost IMU';
    },
    stop() {
      try {
        characteristic && characteristic.removeEventListener('characteristicvaluechanged', handleValue);
        device && device.gatt.connected && device.gatt.disconnect();
      } catch (_) {}
      device = characteristic = onDataCb = null;
    },
    onDisconnect(cb) {
      if (device) device.addEventListener('gattserverdisconnected', cb);
    },
  };
}
