# Monitor de peatones con YOLO y ESP32-C6

Sistema de detección de personas mediante una cámara OV7670 conectada a un RP2350, con procesamiento YOLO en Ubuntu y comunicación Wi-Fi con una Pulsar ESP32-C6.

## Funcionamiento

1. La **OV7670** transmite video a Ubuntu mediante USB.
2. **YOLO** procesa las imágenes y detecta personas.
3. **Python** envía las detecciones a la ESP32-C6 mediante HTTP.
4. La **ESP32-C6** proporciona el estado del semáforo y las mediciones de sus sensores.
5. El **dashboard** muestra los resultados en tiempo real y permite exportar reportes CSV.

## Instalación

```bash
sudo apt update
sudo apt install -y python3 python3-pip python3-venv v4l-utils

cd yolo
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Para identificar la cámara:

```bash
v4l2-ctl --list-devices
```

## Ejecutar el dashboard

Con el firmware `firmware/yolo_bridge/yolo_bridge.ino` cargado en la Pulsar ESP32-C6:

```bash
python dashboard.py \
  --esp-ip 192.168.15.133 \
  --camera /dev/video2 \
  --camera-fps 7
```

Sustituye la IP y la ruta de cámara por las correspondientes a tus dispositivos.

Abre el navegador en:

**http://localhost:8000**

Para detener el programa, presiona `Ctrl+C` en la terminal.

## Funciones del dashboard

| Sección | Descripción |
|---|---|
| Video en vivo | Imágenes de la OV7670 con detecciones YOLO. |
| Estado peatonal | Estado actual del semáforo I²C. |
| Detección | Cantidad de personas y confianza del modelo. |
| ESP32-C6 | Conexión Wi-Fi y mediciones de los sensores. |
| Rendimiento | FPS, tiempo de inferencia y latencia de comunicación. |
| Gráficas | Historial de personas, CO₂, temperatura e iluminación. |
| Reportes | Exportación de mediciones a CSV. |

## Archivos principales

| Archivo | Descripción |
|---|---|
| `yolo/dashboard.py` | Procesamiento YOLO y servidor web. |
| `yolo/dashboard.html` | Interfaz gráfica del dashboard. |
| `yolo/requirements.txt` | Dependencias de Python. |
| `firmware/yolo_bridge/yolo_bridge.ino` | Firmware de la ESP32-C6. |

**Nota:** el firmware `yolo_bridge.ino` mantiene el ciclo automático del semáforo. YOLO proporciona información sobre las personas detectadas, pero no modifica directamente sus fases.
