import cv2
import time

CAMERA_DEVICE = "/dev/video5"
WINDOW_NAME = "OV7670 - RP2350"

print("Inicializando cámara OV7670...")

cap = cv2.VideoCapture(CAMERA_DEVICE, cv2.CAP_V4L2)

if not cap.isOpened():
    raise RuntimeError(
        f"No se pudo abrir {CAMERA_DEVICE}"
    )

# Resolución solicitada
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 320)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)

width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

print("Cámara inicializada")

print(f"Dispositivo: {CAMERA_DEVICE}")

print(f"Resolución: {width}x{height}")

print("Presiona Q para salir")

previous_time = time.perf_counter()

while True:

    ret, frame = cap.read()

    if not ret:
        print("Error al recibir un fotograma")
        break

    # Calcular FPS
    current_time = time.perf_counter()

    delta = current_time - previous_time

    fps = 1.0 / delta if delta > 0 else 0

    previous_time = current_time

    # Mostrar información
    cv2.putText(
        frame,
        f"FPS: {fps:.1f}",
        (10, 25),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        (0, 255, 0),
        2
    )

    cv2.imshow(WINDOW_NAME, frame)

    # Salir presionando Q
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()

print("Cámara desconectada")
