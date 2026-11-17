import cv2
import time
import requests
from ultralytics import YOLO

CAMERA = "/dev/video5"

WIDTH = 320
HEIGHT = 240
CAMERA_FPS = 7

MODEL_PATH = "yolo26n.pt"
CONFIDENCE = 0.35
IMAGE_SIZE = 320

ESP32_IP = "192.168.15.130"
ESP32_URL = f"http://{ESP32_IP}/pulse"

# Confirmar deteccion en 2 fotogramas
DETECTION_FRAMES = 2

# Rearmar despues de 3 fotogramas
RESET_FRAMES = 3

print("Cargando modelo YOLO...")

model = YOLO(MODEL_PATH)

print("Modelo cargado")

cap = cv2.VideoCapture(
    CAMERA,
    cv2.CAP_V4L2
)

if not cap.isOpened():
    raise RuntimeError("No se pudo abrir OV7670")

cap.set(cv2.CAP_PROP_FRAME_WIDTH, WIDTH)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, HEIGHT)
cap.set(cv2.CAP_PROP_FPS, CAMERA_FPS)

cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

print("Camara inicializada")
print(f"ESP32-S3: {ESP32_IP}")

detection_count = 0
absence_count = 0

event_active = False
total_events = 0

previous_time = time.perf_counter()

def send_pulse():

    try:
        response = requests.get(
            ESP32_URL,
            timeout=1
        )

        response.raise_for_status()

        print("Pulso enviado al ESP32-S3")
        print(f"Respuesta: {response.text}")

        return True

    except requests.RequestException as error:

        print(f"Error HTTP: {error}")

        return False

try:

    while True:

        ret, frame = cap.read()

        if not ret:
            print("Error de captura")
            break

        results = model.predict(
            source=frame,
            classes=[0],
            conf=CONFIDENCE,
            imgsz=IMAGE_SIZE,
            device="cpu",
            verbose=False
        )

        boxes = results[0].boxes

        person_count = len(boxes)

        detected = person_count > 0

        if detected:

            detection_count += 1
            absence_count = 0

            if (
                detection_count >= DETECTION_FRAMES
                and not event_active
            ):

                print("Persona detectada")

                if send_pulse():

                    event_active = True
                    total_events += 1

        else:

            detection_count = 0
            absence_count += 1

            if absence_count >= RESET_FRAMES:

                if event_active:
                    print("Sistema rearmado")

                event_active = False

        annotated = results[0].plot()

        current_time = time.perf_counter()

        elapsed = current_time - previous_time

        fps = 1.0 / elapsed if elapsed > 0 else 0

        previous_time = current_time

        if detected:

            status = "PERSONA DETECTADA"
            color = (0, 255, 0)

        else:

            status = "SIN PERSONAS"
            color = (0, 0, 255)

        cv2.putText(
            annotated,
            status,
            (10, 25),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            color,
            2
        )

        cv2.putText(
            annotated,
            f"Personas: {person_count}",
            (10, 48),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1
        )

        cv2.putText(
            annotated,
            f"FPS: {fps:.2f}",
            (10, 70),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1
        )

        cv2.putText(
            annotated,
            f"Eventos: {total_events}",
            (10, 92),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1
        )

        cv2.imshow("OV7670 - YOLO - TouchDot - RP2350", annotated)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

finally:

    cap.release()
    cv2.destroyAllWindows()

    print("Programa finalizado")
