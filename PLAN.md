# Plan: Migrar a Arduino Nano 33 BLE Sense + BLE + tema Firefly "Blue Ghost"

## Contexto (por qué este cambio)
El proyecto actual (Seeed XIAO ESP32-S3 + MPU-9250 de Amazon, WiFi-AP + WebSocket + Three.js)
tiene dos problemas de confiabilidad: el IMU genérico de $8 es de dudosa calidad (muchos
"MPU-9250" de Amazon son clones o no traen magnetómetro funcional) y el SoftAP del ESP32-S3 es
inestable (brownouts con fuentes débiles). El usuario ya posee un **Arduino Nano 33 BLE Sense**
(de un kit de TinyML), que es de marca reconocida y trae un **IMU de 9 ejes con magnetómetro
integrado** — elimina de raíz el sensor barato y todo el cableado. Aprovechando esto, migramos
el transporte de datos de WiFi a **BLE (Web Bluetooth)** y rebrandeamos el tema de NASA Shuttle
al lander lunar **Blue Ghost de Firefly Aerospace** (donde el hijo del usuario hace internship).

## Decisiones tomadas (confirmadas con el usuario)
- **Hardware**: Arduino Nano 33 BLE. **CONFIRMADO por diagnóstico HW (2026-05-25):** trae el ST
  **LSM9DS1** (9 ejes), `IMU.begin()` OK, ODR accel/gyro **119 Hz**, mag **20 Hz**, lib
  `Arduino_LSM9DS1`. El IMU vive en el bus interno `Wire1`. (No es BMI270/Rev2.)
- **Transporte**: BLE / Web Bluetooth (no WiFi). Acepta que no corre en iOS Safari (sí Chrome/Edge
  en PC y Android).
- **Fusión**: 9-DOF con magnetómetro (yaw absoluto sin deriva) → requiere calibración del mag.
- **Modelo 3D**: Blue Ghost (módulo con patas), parado sobre sus patas sobre el grid/mesa.
- **Colores**: negro mate + verde lima de Firefly (+ acentos dorados de foil MLI, opcional).
- **Hosting del dashboard**: GitHub Pages (HTTPS).
- **Repo nuevo e independiente**: este repo (`mpu9250-imu-3d-visualization`) **se deja intacto**
  como la futura versión ESP32 + ICM-20948. Todo el trabajo va en un repo nuevo
  **`firefly-imu-dashboard`** (`/Users/geekendzone/repos/firefly-imu-dashboard`), creado **copiando
  el código actual como base** (git nuevo, historia limpia) y **publicado en GitHub (público)**.

## Cómo viajan los datos del Nano a la página (aclaración pedida por el usuario)
GitHub Pages y el BLE son **independientes**:
- **GitHub Pages** entrega el *código* del dashboard (HTML/JS/Three.js) **una sola vez**, por HTTPS.
- **El dato en vivo** viaja por radio BLE, local y punto a punto, sin pasar por internet:
  1. El firmware define un **servicio GATT** con una **característica** (un "buzón") que soporta `notify`.
  2. Cada frame, el firmware empaqueta el cuaternión + accel + gyro y escribe ese buffer en la característica.
  3. En Chrome, el usuario pulsa **Conectar** (Web Bluetooth exige un gesto del usuario), elige el
     dispositivo y el navegador se **suscribe** a la característica.
  4. Cada actualización dispara un evento `characteristicvaluechanged` en JS → se parsean los bytes
     → se aplican al modelo Three.js y a las gráficas.

```
[Nano 33 BLE] --radio BLE (local, p2p)--> [Bluetooth del PC] --> [Chrome] --> JS --> Three.js
      ▲ el firmware actualiza la característica 60x/seg
      └─ el código de la página vino de GitHub Pages (HTTPS) una sola vez
```
HTTPS es obligatorio porque Web Bluetooth solo funciona en "secure context" (por eso GitHub Pages).

---

## Fase 0 — Crear el repo nuevo `firefly-imu-dashboard`
1. Copiar el contenido del repo actual a `/Users/geekendzone/repos/firefly-imu-dashboard`
   **excluyendo `.git/`** (y `data/three.min.js.gz`, que ya no se usa). El repo original **no se toca**.
2. `git init` (historia limpia), `git branch -m main`.
3. Escribir los nuevos `README.md`, `CLAUDE.md` y copiar este plan a `docs/PLAN.md` (o `plan.md`) dentro del repo.
4. `gh repo create geekendzone/firefly-imu-dashboard --public --source=. --remote=origin` + primer commit + `git push -u origin main`.
5. Todo lo de "Cambios de FIRMWARE/FRONTEND/Rebrand" de abajo se aplica **dentro de este nuevo repo**.

### `README.md` (nuevo repo) — contenido
- Título y pitch: dashboard 3D de orientación en tiempo real con **Arduino Nano 33 BLE Sense**,
  fusión 9-DOF, streaming por **BLE / Web Bluetooth**, tema **Firefly "Blue Ghost"**.
- Hardware: Nano 33 BLE Sense (Rev1 LSM9DS1 / Rev2 BMI270+BMM150, ambos 9 ejes), sin cableado externo.
- Arquitectura (incluir el diagrama de flujo BLE de arriba: la placa notifica una característica GATT;
  el navegador se suscribe por Web Bluetooth; GitHub Pages solo sirve el código).
- Cómo construir/subir firmware (`pio run -e nano33ble -t upload`), cómo calibrar el magnetómetro
  (figura-8), y cómo abrir el dashboard (URL de GitHub Pages, navegador Chrome/Edge, botón Conectar).
- Limitaciones: no iOS Safari (Web Bluetooth); requiere HTTPS.
- Crédito: GeekendZone. Mención al contexto Firefly Aerospace.

### `CLAUDE.md` (nuevo repo) — contenido
Reescritura del CLAUDE.md actual adaptado al nuevo stack:
- Overview: Nano 33 BLE Sense + IMU 9 ejes onboard → fusión 9-DOF → BLE → Web Bluetooth → Three.js (Blue Ghost).
- Quick Reference: board `nano33ble` (platform `nordicnrf52`), puerto serial del Nano, nombre BLE
  `Firefly-BlueGhost-IMU`, UUID del servicio/característica, formato del paquete (10×int16 = 20 bytes).
- Build & upload (`pio run -e nano33ble`, `-t upload`); cómo leer serial (adaptar el snippet pyserial al puerto del Nano).
- Key files: `src/main.cpp` (IMU + Madgwick 9-DOF + BLE), `src/sensor_fusion.h` (9-DOF), `docs/` (frontend Web Bluetooth + GitHub Pages).
- Notas importantes: librería IMU según revisión; ODR real ~119 Hz; calibración del mag; Web Bluetooth solo HTTPS/Chrome.

---

## Cambios de FIRMWARE (`src/`, `platformio.ini`) — en el repo nuevo

### `platformio.ini` — nuevo entorno para el Nano
Reemplazar el bloque `[env:seeed_xiao_esp32s3]` por:
```ini
[env:nano33ble]
platform = nordicnrf52
board = nano33ble
framework = arduino
monitor_speed = 115200
lib_deps =
    arduino-libraries/ArduinoBLE@^1.3.7
    ; IMU según la revisión (confirmar en casa, dejar la que aplique):
    arduino-libraries/Arduino_LSM9DS1@^1.1.1        ; Rev1 (ST LSM9DS1)
    ; arduino-libraries/Arduino_BMI270_BMM150@^1.2.0 ; Rev2 (Bosch BMI270+BMM150)
```
Se eliminan `MPU9250`, `ESPAsyncWebServer`, `ArduinoJson` y `board_build.filesystem = littlefs`
(ya no hay servidor web ni LittleFS).

### `src/main.cpp` — reescritura del núcleo (placa de 1 core, sin WiFi)
El modelo de doble tarea (Core1 sensor / Core0 WiFi) del ESP32 ya no aplica. Se colapsa a un
único `loop()` con temporización fija. Cambios concretos sobre lo mapeado:
- **Quitar** todo lo de WiFi-AP, DNS captive portal, `ESPAsyncWebServer`, WebSocket broadcast
  (líneas ~19, ~99, y todo el servidor/portal) y la inicialización I2C/MPU
  (`MPU_ADDR 0x68`, `PIN_SDA/SCL`, `Wire.begin`, `imu.begin()`, `setAccelRange`… líneas 13-16, 43, 102-137).
- **Quitar** la creación de la tarea FreeRTOS `sensorTask` pinneada a Core 1 (líneas 51-84, 211-213).
- **Añadir** init del IMU onboard:
  - Rev1: `IMU.begin()` de `Arduino_LSM9DS1` → `readAcceleration` (g), `readGyroscope` (dps), `readMagneticField` (µT).
  - Rev2: `Arduino_BMI270_BMM150` (misma forma de API).
  - **Conversión de unidades** para Madgwick: gyro `rad/s = dps * PI/180`; accel `m/s² = g * 9.80665`.
  - **Sample rate real**: el LSM9DS1 entrega ~119 Hz (accel/gyro); ajustar `SENSOR_HZ` a la ODR real
    (~104-119 Hz) en lugar de 200 Hz, y pasar esa frecuencia al filtro Madgwick.
  - **Bias de gyro**: promediar ~1 s en reposo al arranque (sustituye a `imu.calibrateGyro()`).
- **BLE GATT** con `ArduinoBLE`:
  - Servicio custom (UUID 128-bit, p.ej. `19B10000-E8F2-537E-4F6C-D104768A1214`).
  - Característica `notify`, **20 bytes** = 10×`int16` empaquetados (cuaternión w,x,y,z + ax,ay,az + gx,gy,gz,
    cada uno escalado a int16). 20 bytes caben en el payload BLE por defecto → robusto sin negociar MTU.
  - `BLE.setLocalName("Firefly-BlueGhost-IMU")`, `BLE.setDeviceName(...)`, `BLE.advertise()`.
  - `BLE.setConnectionInterval(12, 24)` (~15-30 ms) para soportar ~60 Hz de notificaciones.
  - En `loop()`: `BLE.poll()` + leer IMU a `SENSOR_HZ` + `filter.updateMag(...)` + empaquetar + `characteristic.writeValue(buf, 20)`.

### `src/sensor_fusion.h` — extender a 9-DOF
Hoy solo tiene `update(gx,gy,gz, ax,ay,az)` (6-DOF, sin mag; líneas 10-14, 82-85). Añadir:
- `void updateMag(float gx,float gy,float gz, float ax,float ay,float az, float mx,float my,float mz)`
  con la fórmula AHRS 9-DOF de Madgwick (gradiente con referencia magnética). Reusar `_q0.._q3` y los
  getters `w()/x()/y()/z()` existentes.
- Mantener `update()` 6-DOF como fallback.

### Calibración del magnetómetro (hard/soft-iron)
- Modo calibración: si no hay offsets guardados (o por comando serial), recolectar muestras del mag
  mientras se mueve la placa en forma de "8" ~15-20 s; calcular offset hard-iron `=(max+min)/2` por eje
  y escala soft-iron simple; restar/escalar antes de `updateMag`.
- **Persistencia** en flash del nRF52 vía `mbed` `KVStore`/`TDBStore`. Fallback pragmático: imprimir
  los offsets por serial y fijarlos como constantes si la persistencia resulta engorrosa.

---

## Cambios de FRONTEND (mover `data/` → `docs/` para GitHub Pages)

### Transporte: WebSocket → Web Bluetooth (`docs/app.js`)
- **Reemplazar** todo el bloque WebSocket (líneas 1-54: `connectWebSocket`, `socket.onmessage`…).
- **Añadir** cliente Web Bluetooth con botón **Conectar** (gesto de usuario obligatorio):
  `navigator.bluetooth.requestDevice({filters:[{services:[SERVICE_UUID]}]})` → `gatt.connect()` →
  `getPrimaryService` → `getCharacteristic` → `startNotifications()` →
  listener `characteristicvaluechanged` que lee el `DataView` (10×int16, des-escalar) y llama a
  `onSensorData(d)` (que ya existe, líneas 288-306) — se **reutiliza** la lógica de
  `shuttle.quaternion.set(x,y,z,w)` (línea 296) y las gráficas.
- Actualizar el indicador de estado (los `status-dot` / `ws-status`) a estado BLE.

### Three.js: cargar desde CDN (`docs/index.html`)
- Ya no se sirve `three.min.js.gz` desde LittleFS. Cargar Three.js r128 desde CDN (hay internet en
  GitHub Pages). **Borrar** `data/three.min.js.gz`.

### Modelo 3D: Shuttle → Blue Ghost (`docs/app.js` líneas 150-207)
- Reemplazar el `THREE.Group()` del shuttle (fuselaje, nariz, alas, cola, motores) por un **lander
  Blue Ghost**: cuerpo central octogonal/caja bajo y ancho, cubierta superior con paneles/payloads,
  **4 patas** anguladas con footpads, antena de alta ganancia (disco) opcional, panel solar.
- Orientación neutra: el lander **parado sobre sus patas** sobre el `GridHelper` (la "mesa"), de modo
  que la rotación del IMU lo incline desde esa pose. (Deja sitio para que el usuario meta la placa en
  un modelo 3D-printed después.)
- Renombrar la variable `shuttle` → `lander` y los comentarios "shuttle" (líneas 57, 74-75, 150, 295).

### Rebrand de tema Firefly (colores y textos)
- **Colores** (negro mate + verde lima Firefly, p.ej. fondo `#0a0a0a`, acento `#9aca3c`/verde lima,
  acentos dorados foil opcionales):
  - `docs/style.css`: sustituir `#00ff99` (cian, líneas 23,49,50,59,69,97,120,153) y `#0a0a1a`
    (líneas 8,114,139).
  - `docs/app.js`: `scene.background` (línea 62), `GridHelper` (línea 141), colores del modelo
    (líneas 156,163,171,184,191,203).
- **Textos / nombres**:
  - `docs/index.html` título (línea 6) → "Firefly Blue Ghost — IMU Dashboard".
  - Crédito "Crafted by GeekendZone" (línea 18) → se mantiene.
  - Comentarios de cabecera de `style.css` (líneas 1-2) → describir Nano 33 BLE / Blue Ghost.
  - Nombre BLE en firmware → `Firefly-BlueGhost-IMU` (sustituye al SSID `NASA-Shuttle-IMU`).

### Hosting en GitHub Pages
- Mover el frontend de `data/` a `docs/` y activar Pages desde **rama `main`, carpeta `/docs`**
  (Settings → Pages). Queda un link HTTPS compartible.

---

## Hitos / orden de ejecución
0. **Fase 0**: crear `firefly-imu-dashboard` (copia del actual, git nuevo), escribir README.md +
   CLAUDE.md + PLAN, `gh repo create --public` + primer push. Repo original intacto.
1. ~~Confirmar revisión del Nano~~ ✅ **HECHO (2026-05-25):** LSM9DS1 9-ejes confirmado por HW
   (ODR 119/119/20 Hz, bus `Wire1`, lib `Arduino_LSM9DS1`). Toolchain `nordicnrf52` ya instalado.
2. **Firmware mínimo**: IMU 9 ejes + Madgwick 9-DOF + BLE notify (int16×10). Verificar con app
   **nRF Connect** (móvil) o un scanner BLE que la característica notifica datos coherentes.
3. **Calibración del mag**: rutina figura-8 + persistencia; validar yaw estable apuntando al norte.
4. **Frontend**: cliente Web Bluetooth + botón Conectar; verificar rotación en vivo con el shuttle aún.
5. **Rebrand**: modelo Blue Ghost + colores Firefly + textos; mover a `docs/` + activar Pages.

## Verificación end-to-end
- `pio run -e nano33ble` compila; `pio run -e nano33ble -t upload` sube al Nano.
- Leer serial (método de CLAUDE.md, puerto del Nano) → ver "IMU OK", Hz de muestreo y, tras
  calibrar, valores de mag con offset aplicado.
- Abrir la URL de GitHub Pages en **Chrome** → **Conectar** → elegir `Firefly-BlueGhost-IMU` →
  inclinar/rotar la placa: el **Blue Ghost** rota en vivo, las gráficas accel/gyro se mueven, y el
  **yaw mantiene el rumbo sin derivar** tras la calibración. Probar también en Chrome Android.

## Riesgos / notas
- **iOS Safari no soporta Web Bluetooth** (decisión aceptada). En iPhone solo funcionaría con apps
  tipo "Bluefy"; no es objetivo.
- **ODR real < 200 Hz** en el LSM9DS1 (~119 Hz). Es suficiente para display a 60 Hz; ajustar el
  `sampleFreq` del filtro a la ODR real para que la fusión sea correcta.
- **Calibración del mag** es el punto más delicado (hard/soft-iron, interferencia metálica cercana).
  Si da problemas, primer milestone usable es 6-DOF (sin yaw absoluto) y se activa el mag después.
- **Throughput BLE**: 20 bytes @ ~60 Hz es trivial; el empaquetado int16 evita depender de negociar MTU.
- No se renombra el repo (decisión del usuario); solo se rebrandea el contenido.
