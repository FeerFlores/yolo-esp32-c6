"""OV7670 (RP2350 USB UVC) -> YOLO -> Pulsar ESP32-C6 via HTTP.

Run: source .venv/bin/activate && python main.py
Press q to exit. Change ESP32_IP to the IP printed by the ESP32-C6.
"""
import time

import cv2
import requests
from ultralytics import YOLO

CAMERA = "/dev/video5"
WIDTH, HEIGHT, FPS = 320, 240, 7
MODEL_PATH = "yolo26n.pt"
CONFIDENCE = 0.35
IMAGE_SIZE = 320
ESP32_IP = "192.168.15.129" 
ESP32_URL = f"http://{ESP32_IP}/detect"

DETECTION_FRAMES = 2
RESET_FRAMES = 3
HTTP_RETRY_SECONDS = 3.0

print("Loading YOLO model...")
model = YOLO(MODEL_PATH)

cap = cv2.VideoCapture(CAMERA, cv2.CAP_V4L2)
if not cap.isOpened():
    raise RuntimeError(f"Cannot open camera {CAMERA}")

cap.set(cv2.CAP_PROP_FRAME_WIDTH, WIDTH)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, HEIGHT)
cap.set(cv2.CAP_PROP_FPS, FPS)
cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

consecutive_detected = 0
consecutive_absent = 0
event_active = False
sent_events = 0
retry_after = 0.0
previous_time = time.perf_counter()


def send_detection(person_count: int, best_confidence: float) -> bool:
    """Send an event only at the start of a confirmed detection episode."""
    try:
        response = requests.get(
            ESP32_URL,
            params={
                "count": person_count,
                "confidence": f"{best_confidence:.3f}",
            },
            timeout=1.0,
        )
        response.raise_for_status()
        print("Event acknowledged by Pulsar ESP32-C6:", response.text)
        return True
    except requests.RequestException as exc:
        print("Could not send event:", exc)
        return False


print(f"Camera: {CAMERA} | Pulsar URL: {ESP32_URL}")
print("Press q to exit")

try:
    while True:
        ok, frame = cap.read()
        if not ok:
            print("Camera capture failed")
            break

        result = model.predict(
            source=frame,
            classes=[0],  # COCO class 0: person
            conf=CONFIDENCE,
            imgsz=IMAGE_SIZE,
            device="cpu",
            verbose=False,
        )[0]

        count = len(result.boxes)
        detected = count > 0
        best_conf = float(result.boxes.conf.max().item()) if detected else 0.0

        if detected:
            consecutive_detected += 1
            consecutive_absent = 0
            if consecutive_detected >= DETECTION_FRAMES and not event_active:
                if time.monotonic() >= retry_after:
                    if send_detection(count, best_conf):
                        event_active = True
                        sent_events += 1
                    else:
                        retry_after = time.monotonic() + HTTP_RETRY_SECONDS
        else:
            consecutive_detected = 0
            consecutive_absent += 1
            if consecutive_absent >= RESET_FRAMES:
                if event_active:
                    print("System rearmed")
                event_active = False

        annotated = result.plot()
        now = time.perf_counter()
        elapsed = now - previous_time
        real_fps = 1.0 / elapsed if elapsed > 0 else 0.0
        previous_time = now

        label = "PERSONA DETECTADA" if detected else "SIN PERSONAS"
        color = (0, 255, 0) if detected else (0, 0, 255)
        cv2.putText(annotated, label, (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.55, color, 2)
        cv2.putText(annotated, f"Personas: {count}  Eventos: {sent_events}",
                    (10, 48), cv2.FONT_HERSHEY_SIMPLEX, 0.43, (255, 255, 255), 1)
        cv2.putText(annotated, f"FPS: {real_fps:.1f}",
                    (10, 67), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
        cv2.imshow("OV7670 - YOLO - Pulsar ESP32-C6", annotated)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break
finally:
    cap.release()
    cv2.destroyAllWindows()
    print("Detection stopped")
