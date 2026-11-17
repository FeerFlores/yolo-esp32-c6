
import cv2
import time

CAMERA = "/dev/video5"

WIDTH = 320
HEIGHT = 240
TARGET_FPS = 7

cap = cv2.VideoCapture(
    CAMERA,
    cv2.CAP_V4L2
)

if not cap.isOpened():
    raise RuntimeError("No se pudo abrir OV7670")

# Configurar cámara
cap.set(cv2.CAP_PROP_FRAME_WIDTH, WIDTH)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, HEIGHT)
cap.set(cv2.CAP_PROP_FPS, TARGET_FPS)

# Intentar reducir los buffers
cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

print("OV7670 inicializada")

print(
    "Resolución:",
    cap.get(cv2.CAP_PROP_FRAME_WIDTH),
    "x",
    cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
)

print(
    "FPS configurados:",
    cap.get(cv2.CAP_PROP_FPS)
)

# Medición de rendimiento
frames = 0
fps = 0.0
start_time = time.perf_counter()

while True:

    ret, frame = cap.read()

    if not ret:
        print("Error de captura")
        continue

    frames += 1

    now = time.perf_counter()
    elapsed = now - start_time

    # Promedio sobre un segundo
    if elapsed >= 1.0:
        fps = frames / elapsed

        print(f"FPS reales: {fps:.2f}")

        frames = 0
        start_time = now

    cv2.putText(
        frame,
        f"FPS: {fps:.2f}",
        (10, 25),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        (0, 255, 0),
        2
    )

    cv2.imshow("OV7670 - RP2350", frame)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
