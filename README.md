# Person Detection System

Sistema de detección de personas mediante visión artificial,
utilizando una cámara OV7670, una Pulsar RP2350 y
una TouchDot S3.

## 1. Descripción

Este proyecto tiene como objetivo implementar un sistema
de reconocimiento de personas mediante una cámara OV7670
conectada a un RP2350.

El RP2350 transmite las imágenes a una computadora con
Ubuntu 24.04 mediante USB Video Class (UVC).

La computadora procesa las imágenes utilizando Python,
OpenCV y un modelo YOLO para identificar personas.

Cuando se detecta una persona, el sistema envía una
petición HTTP a la TouchDot ESP32-S3.

El ESP32-S3 recibe la petición mediante Wi-Fi, muestra el mensaje
"PERSONA DETECTADA" en una pantalla OLED SSD1315.

## 2. Arquitectura

```text
        OV7670 Camera
              |
              v
         RP2350 MCU
         TinyUSB UVC
              |
              | USB
              v
         Ubuntu 24.04
        Python + OpenCV
              |
              v
        YOLO Detection
              |
              | HTTP / Wi-Fi
              v
       UNIT TouchDot S3
           ESP32-S3
              |
        +-----+------+
        |            |
        v            v
    Built-in LED   SSD1315 OLED
       500 ms      Detection alert
```

## 3. Hardware

| Componente | Función |
|---|---|
| OV7670 | Captura de imágenes |
| RP2350 | Interfaz USB UVC |
| Computadora Ubuntu | Procesamiento de imágenes |
| TouchDot ESP32-S3 | Recepción de eventos Wi-Fi |
| SSD1315 OLED | Visualización de alertas |

### Conexiones de la pantalla OLED

| SSD1315 | TouchDot S3 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO5 |
| SCL | GPIO6 |

Configuración:

- Interfaz: I2C
- Dirección: 0x3C
- Resolución: 128 x 64 píxeles
- LED convencional TouchDot S3: GPIO7

## 4. Software

### Sistema operativo

Ubuntu 24.04 LTS

### Herramientas

- Python 3
- OpenCV
- Ultralytics YOLO (pendiente de integrar)
- Arduino IDE
- TinyUSB
- Git

### Bibliotecas ESP32-S3

- WiFi.h
- WebServer.h
- Wire.h
- Adafruit_GFX.h
- Adafruit_SSD1306.h

## 5. Instalación

### 5.1 Clonar el repositorio

```bash
git clone https://github.com/FeerFlores/yolo-esp32-c6.git
cd yolo-esp32-c6
```

### 5.2 Instalar dependencias del sistema

```bash
sudo apt update

sudo apt install \
    python3 \
    python3-pip \
    python3-venv \
    v4l-utils
```

### 5.3 Crear un entorno virtual

```bash
cd yolo

python3 -m venv .venv

source .venv/bin/activate
```

### 5.4 Instalar OpenCV

```bash
pip install opencv-python
```

Verificar la instalación:

```bash
python -c "import cv2; print(cv2.__version__)"
```

### 5.5 Dependencias para detección

pendiente :D

## 6. Cámara OV7670

La cámara utiliza un RP2350 con firmware TinyUSB.

Ubuntu reconoce el dispositivo como:

```text
RP2350 OV7670 Camera: TinyUSB U
```

Para identificar los dispositivos:

```bash
v4l2-ctl --list-devices
```

En las pruebas iniciales se detectaron:

```text
/dev/video5
/dev/video6
/dev/media1
```

Para comprobar los formatos disponibles:

```bash
v4l2-ctl -d /dev/video5 --list-formats-ext
```

## 7. Comunicación Wi-Fi

El ESP32-S3 ejecuta un servidor HTTP en el puerto 80.

### Endpoint

```http
GET /pulse
```

### Ejemplo de petición

```bash
curl http://192.168.15.130/pulse
```

La dirección IP corresponde a la configuración
utilizada durante las pruebas y **puede cambiar**.

### Respuesta esperada

```text
Persona detectada - OK
```

### Comportamiento

Al recibir una petición HTTP:

1. Se enciende el LED BUILTIN.
2. El LED permanece encendido 500 ms.
3. La OLED muestra "PERSONA DETECTADA".
4. El mensaje permanece visible durante 3 segundos.
5. El sistema regresa al estado de espera.

Los temporizadores se implementan mediante millis().

## 8. Detección de personas

La detección mediante YOLO se encuentra en desarrollo.

El objetivo es reconocer objetos de la clase "person"
en los fotogramas capturados por la OV7670.

Cuando se confirme una nueva detección, Python
enviará una petición al ESP32-S3:

```python
import requests

ESP32_IP = "192.168.15.130"

requests.get(
    f"http://{ESP32_IP}/pulse",
    timeout=1
)
```

El sistema incorporará lógica para evitar
enviar solicitudes continuamente mientras
una misma persona permanece frente a la cámara.

## 9. Integración ESP32-C6 + YOLO + semáforo peatonal

La integración nueva está en `firmware/yolo_bridge/yolo_bridge.ino` y
`yolo/dashboard.py`. Los ejemplos anteriores de ESP32-S3 siguen disponibles.

### Hardware y red

El ESP32-C6 usa SDA GPIO6 y SCL GPIO7 para los módulos I²C del ejemplo
`firmware/sensor/sensor.ino`:

| Módulo | Dirección | Uso |
|---|---|---|
| Botón | `0x30` | Estado mostrado en la telemetría |
| Sensor de luz GT36537 | `0x26` | Lectura para el reporte |
| SCD41 | `0x62` | CO₂, temperatura y humedad |
| OLED SSD1306 | `0x3C` | IP, lecturas y estado |
| Semáforo UE0183 | `0x36` | Rojo, amarillo y verde |

El ESP32-C6 y la computadora deben estar en la misma red Wi-Fi. Antes de
compilar, revisa `WIFI_SSID` y `WIFI_PASSWORD` al inicio del firmware.
La IP se asigna por DHCP: aparece en la OLED y en el monitor serie a
115200 baudios. Si cambia, usa el valor nuevo en `--esp-ip`.
El ESP32-C6 recibe `POST /detect` en su puerto 80. El panel web de la
computadora usa el puerto indicado por `--web-port` (8000 u 8001); son
servidores y puertos distintos.

Compilar con Arduino CLI y las bibliotecas `DevLabDDP`, `DevLab_Interface`,
`Sensirion I2C SCD4x`, `Adafruit GFX Library` y `Adafruit SSD1306`:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c6 firmware/yolo_bridge
arduino-cli upload --fqbn esp32:esp32:esp32c6 -p /dev/ttyACM0 firmware/yolo_bridge
```

Sustituye `/dev/ttyACM0` por el puerto real del ESP32-C6. Al arrancar,
`GET http://IP_DEL_ESP/status` entrega JSON con sensores, semáforo,
personas e IP. YOLO envía al menos cada segundo `POST /detect` con los campos
`count` y `confidence` y lee `/status` para el panel y el reporte.

En el monitor serie a 115200 baudios, el firmware correcto comienza con
`=== YOLO BRIDGE ESP32-C6 ===`, muestra el SSID y luego `Wi-Fi: <IP>`.
Si aparece `UNIT DevLab I2C Monitor` y el semáforo recorre
`RED → YELLOW → GREEN → OFF`, sigue cargado `firmware/sensor/sensor.ino`:
ese ejemplo prueba sensores pero no conecta Wi-Fi. Selecciona y carga
`firmware/yolo_bridge/yolo_bridge.ino` en el ESP32-C6. Si el firmware
correcto informa `Wi-Fi sin conexion`, revisa `WIFI_SSID`,
`WIFI_PASSWORD` y que el punto de acceso sea de 2,4 GHz.

### Regla del semáforo

- El semáforo peatonal sigue el ciclo normal: rojo 5 s (**no pase**),
  verde 5 s (**pase**) y amarillo 2 s (**espere**).
- YOLO registra los peatones y el botón se muestra en la telemetría;
  ninguno de los dos cambia el color del semáforo.
- Si se pierde Wi-Fi o YOLO deja de enviar datos, el ciclo continúa.
  El panel indica **sin datos** cuando no puede consultar el semáforo.
  Un fallo de comunicación con el módulo I²C se muestra como error.

Este prototipo controla el semáforo peatonal UE0183, no un cruce vial real.

### Iniciar cámara, gráficas y reporte

Desde `yolo`, usa el entorno virtual y la IP que muestra la OLED:
Sustituye `IP_DEL_ESP32_C6` por una dirección numérica real; no escribas
ese texto literalmente. `192.168.15.123` es la IP de la computadora en este
ejemplo, por lo que no sirve como valor de `--esp-ip`.

```bash
source venv/bin/activate
unset PYTHONPATH
python -m pip install -r requirements.txt
python dashboard.py --esp-ip IP_DEL_ESP32_C6 --web-host 0.0.0.0
```

El modelo `yolo26n.pt` debe estar en `yolo/`. Desde otro equipo de la misma
red, abre `http://IP_DE_LA_COMPUTADORA:8000`; por ejemplo, si la computadora
tiene `192.168.15.123`, abre `http://192.168.15.123:8000`. Allí aparecen
la cámara con detecciones, estado del semáforo, IP, sensores y gráficas
de los últimos 10 minutos. **Descargar reporte CSV** entrega todas las lecturas de la
sesión. El archivo también se guarda en `yolo/reportes/` cada segundo.
Pulsa Ctrl+C para detener YOLO; el ESP32-C6 mantiene el ciclo del semáforo.
La cámara predeterminada es `/dev/video0`, a 320 × 240 y 7 FPS, igual que
`yolo/main.py`. Puedes cambiarla con `--camera /dev/video5` o ajustar
los FPS con `--camera-fps 7`. Cierra `main.py` antes de iniciar el panel
para que ambos procesos no intenten usar la misma cámara.
Si el puerto 8000 está ocupado, cierra el panel anterior o añade
`--web-port 8001` y abre `http://IP_DE_LA_COMPUTADORA:8001`.
Si la cámara no abre, cierra `main.py` y otros paneles que puedan usarla;
comprueba la ruta con `ls /dev/video*` y, si hace falta, usa `--camera /dev/videoN`.
