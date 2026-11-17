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
