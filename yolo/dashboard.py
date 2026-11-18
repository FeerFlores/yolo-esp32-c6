"""YOLO + telemetria ESP32-C6 + panel web local (baja latencia) y reporte CSV.

Arquitectura (cada etapa en su propio hilo para que ninguna bloquee a otra):
  camara  -> siempre guarda solo el fotograma mas reciente (sin cola acumulada)
  YOLO    -> procesa el ultimo fotograma y publica JPEG + cajas
  ESP32   -> POST /detect (la respuesta ya trae el estado), nunca frena el video
  web     -> /stream.mjpg (video empujado) y /events (SSE con estado y cajas)
"""

import argparse
import csv
import errno
import ipaddress
import json
import threading
import time
from collections import deque
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

import cv2
import numpy as np
import requests
from ultralytics import YOLO


HERE = Path(__file__).resolve().parent
FIELDS = (
    "fecha_hora", "personas", "confianza", "fps", "esp_conectado", "ip_esp",
    "semaforo", "paso_permitido", "boton", "luz_adc", "co2_ppm",
    "temperatura_c", "humedad_pct",
)
ESP_PERIOD_S = 1.0        # el firmware marca YOLO offline tras 4 s sin mensajes
ESP_TIMEOUT_S = 2.5       # el ESP32 puede tardar mas de 0.8 s entre Wi-Fi e I2C
SSE_MIN_INTERVAL_S = 0.05  # maximo ~20 actualizaciones/s por cliente
BOUNDARY = "frame"


def ema(previous: float, value: float, alpha: float = 0.2) -> float:
    return value if previous <= 0 else previous + alpha * (value - previous)


class SharedState:
    def __init__(self, esp_ip: str, report_path: Path):
        self.cond = threading.Condition()
        self.stop = threading.Event()
        self.esp_wake = threading.Event()
        self.version = 0
        self.frame_seq = 0
        self.frame_jpeg = None
        self.rows_total = 0
        self.history = deque(maxlen=600)
        self.report_path = report_path
        self.latest = {
            "camera": "iniciando", "esp_connected": False, "esp_ip": esp_ip,
            "persons": 0, "confidence": 0.0,
            "confirmed_persons": 0, "confirmed_confidence": 0.0,
            "fps": 0.0, "camera_fps": 0.0, "inference_ms": 0.0,
            "pipeline_ms": 0.0, "esp_rtt_ms": None, "esp_errors": 0,
            "frame_seq": 0, "frame_size": [0, 0], "boxes": [],
            "device": None, "report_name": report_path.name,
        }

    def update(self, **values):
        with self.cond:
            self.latest.update(values)
            self.version += 1
            self.cond.notify_all()

    def snapshot(self):
        with self.cond:
            return dict(self.latest)

    def samples(self):
        with self.cond:
            return list(self.history)


# --------------------------------------------------------------------------- camara
class CameraReader(threading.Thread):
    """Lee la camara sin parar y conserva solo el fotograma mas nuevo."""

    def __init__(self, state: SharedState, device: str, fps: int):
        super().__init__(daemon=True)
        self.state, self.device, self.fps = state, device, fps
        self.open_failed_reported = False
        self.cond = threading.Condition()
        self.frame = None
        self.captured_at = 0.0
        self.seq = 0

    def open(self):
        source = int(self.device) if self.device.isdigit() else self.device
        if isinstance(source, str) and source.startswith("/dev/") and not Path(source).exists():
            if not self.open_failed_reported:
                print(f"Camara {source} no existe. Revisa ls /dev/video* o usa --camera con el dispositivo correcto.")
                self.open_failed_reported = True
            return None
        cap = cv2.VideoCapture(source, cv2.CAP_V4L2)
        if not cap.isOpened():
            cap.release()
            if not self.open_failed_reported:
                print(f"No se pudo abrir la camara {self.device}. Cierra main.py u otro dashboard.py, "
                      "o indica el dispositivo correcto con --camera.")
                self.open_failed_reported = True
            return None
        self.open_failed_reported = False
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 320)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)
        cap.set(cv2.CAP_PROP_FPS, self.fps)
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        print(f"Camara {self.device}: {int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))}x"
              f"{int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))} @ {cap.get(cv2.CAP_PROP_FPS):.1f} fps")
        return cap

    def run(self):
        cap = None
        camera_fps = 0.0
        previous = 0.0
        while not self.state.stop.is_set():
            if cap is None:
                cap = self.open()
                if cap is None:
                    self.state.update(camera="sin señal (¿otro programa usa la cámara?)")
                    self.state.stop.wait(2)
                    continue
                self.state.update(camera="activa")
            ok, frame = cap.read()
            now = time.monotonic()
            if not ok:
                print("Camara sin imagen; reconectando...")
                cap.release()
                cap = None
                self.state.update(camera="reconectando")
                time.sleep(1)
                continue
            if previous:
                camera_fps = ema(camera_fps, 1.0 / max(now - previous, 1e-3))
            previous = now
            with self.cond:
                self.frame, self.captured_at = frame, now
                self.seq += 1
                self.cond.notify_all()
            self.state.latest["camera_fps"] = round(camera_fps, 1)
        if cap is not None:
            cap.release()

    def wait_next(self, last_seq: int, timeout: float = 1.0):
        with self.cond:
            self.cond.wait_for(lambda: self.seq != last_seq, timeout=timeout)
            return self.seq, self.frame, self.captured_at


# --------------------------------------------------------------------------- ESP32
def report_row(now: datetime, persons: int, confidence: float, fps: float,
               device: dict | None, esp_connected: bool, esp_ip: str):
    device = device or {}
    return {
        "fecha_hora": now.astimezone().isoformat(timespec="seconds"),
        "personas": persons,
        "confianza": round(confidence, 3),
        "fps": round(fps, 1),
        "esp_conectado": esp_connected,
        "ip_esp": device.get("ip", esp_ip),
        "semaforo": device.get("traffic", "SIN CONEXION"),
        "paso_permitido": device.get("pass_allowed"),
        "boton": device.get("button"),
        "luz_adc": device.get("light_adc"),
        "co2_ppm": device.get("co2_ppm"),
        "temperatura_c": device.get("temperature_c"),
        "humedad_pct": device.get("humidity_pct"),
    }


def esp_worker(state: SharedState, esp_ip: str, report_path: Path):
    """Envia la deteccion al ESP32 sin bloquear la camara ni YOLO."""
    session = requests.Session()
    session.trust_env = False
    url = f"http://{esp_ip}/detect"
    last_row = 0.0
    was_connected = None
    errors = 0
    with report_path.open("a", newline="", encoding="utf-8") as report:
        writer = csv.DictWriter(report, fieldnames=FIELDS)
        while not state.stop.is_set():
            state.esp_wake.wait(ESP_PERIOD_S)
            state.esp_wake.clear()
            s = state.snapshot()
            persons, confidence = s["confirmed_persons"], s["confirmed_confidence"]
            started = time.monotonic()
            try:
                # /detect responde con el mismo JSON que /status: un solo viaje.
                response = session.post(url, data={"count": str(persons),
                                                   "confidence": f"{confidence:.3f}"},
                                        timeout=ESP_TIMEOUT_S)
                response.raise_for_status()
                device, connected = response.json(), True
                rtt = round((time.monotonic() - started) * 1000, 1)
            except (requests.RequestException, ValueError) as exc:
                device, connected, rtt = None, False, None
                errors += 1
                if was_connected is not False:
                    print(f"ESP32-C6 sin respuesta: {exc}")
            if connected and was_connected is not True:
                print(f"ESP32-C6 conectado ({rtt} ms)")
            was_connected = connected
            state.update(esp_connected=connected, device=device, esp_rtt_ms=rtt,
                         esp_errors=errors)

            now = time.monotonic()
            if now - last_row >= 1.0:
                last_row = now
                row = report_row(datetime.now(), persons, confidence, s["fps"],
                                 device, connected, esp_ip)
                writer.writerow(row)
                report.flush()
                with state.cond:
                    state.history.append(row)
                    state.rows_total += 1
                    state.version += 1
                    state.cond.notify_all()
            if not connected:
                # Evita saturar la red si el ESP esta apagado.
                state.stop.wait(1.0)
    session.close()


# --------------------------------------------------------------------------- web
def make_handler(state: SharedState):
    html_path = HERE / "dashboard.html"

    class Handler(BaseHTTPRequestHandler):
        def send_bytes(self, data: bytes, content_type: str, filename: str = ""):
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            if filename:
                self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def start_stream(self, content_type: str):
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.send_header("X-Accel-Buffering", "no")
            self.end_headers()

        def stream_mjpeg(self):
            self.start_stream(f"multipart/x-mixed-replace; boundary={BOUNDARY}")
            last = -1
            try:
                while not state.stop.is_set():
                    with state.cond:
                        state.cond.wait_for(lambda: state.frame_seq != last, timeout=2)
                        if state.frame_seq == last or state.frame_jpeg is None:
                            continue
                        last, jpeg = state.frame_seq, state.frame_jpeg
                    self.wfile.write(
                        f"--{BOUNDARY}\r\nContent-Type: image/jpeg\r\n"
                        f"Content-Length: {len(jpeg)}\r\n\r\n".encode() + jpeg + b"\r\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                pass

        def stream_events(self):
            self.start_stream("text/event-stream; charset=utf-8")
            version = -1
            rows_sent = state.rows_total
            try:
                self.wfile.write(b"retry: 1000\n\n")
                while not state.stop.is_set():
                    with state.cond:
                        changed = state.cond.wait_for(lambda: state.version != version,
                                                      timeout=10)
                        version = state.version
                        payload = dict(state.latest)
                        new_rows = state.rows_total - rows_sent
                        rows = list(state.history)[-new_rows:] if new_rows > 0 else []
                        rows_sent = state.rows_total
                    if not changed:
                        self.wfile.write(b": keepalive\n\n")
                    else:
                        payload["server_time"] = time.time()
                        out = "event: status\ndata: " + json.dumps(payload, ensure_ascii=False) + "\n\n"
                        if rows:
                            out += "event: rows\ndata: " + json.dumps(rows, ensure_ascii=False) + "\n\n"
                        self.wfile.write(out.encode())
                    self.wfile.flush()
                    time.sleep(SSE_MIN_INTERVAL_S)
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                pass

        def do_GET(self):
            path = urlsplit(self.path).path
            if path == "/":
                # Se relee para poder editar el HTML sin reiniciar.
                self.send_bytes(html_path.read_bytes(), "text/html; charset=utf-8")
            elif path == "/stream.mjpg":
                self.stream_mjpeg()
            elif path == "/events":
                self.stream_events()
            elif path == "/api/status":
                data = json.dumps(state.snapshot(), ensure_ascii=False).encode()
                self.send_bytes(data, "application/json; charset=utf-8")
            elif path == "/api/history":
                data = json.dumps(state.samples(), ensure_ascii=False).encode()
                self.send_bytes(data, "application/json; charset=utf-8")
            elif path == "/camera.jpg":
                with state.cond:
                    frame = state.frame_jpeg
                if frame is None:
                    self.send_error(503, "Camara iniciando")
                else:
                    self.send_bytes(frame, "image/jpeg")
            elif path == "/report.csv":
                self.send_bytes(state.report_path.read_bytes(), "text/csv; charset=utf-8",
                                state.report_path.name)
            else:
                self.send_error(404)

        def log_message(self, _format, *_args):
            pass

    return Handler


# --------------------------------------------------------------------------- main
def parse_args():
    def esp_ipv4(value: str) -> str:
        try:
            return str(ipaddress.IPv4Address(value))
        except ipaddress.AddressValueError as exc:
            raise argparse.ArgumentTypeError(
                "escribe la IP numerica real del ESP32-C6 (por ejemplo 192.168.15.42), "
                "no el texto IP_DEL_ESP32_C6"
            ) from exc

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--esp-ip", required=True, type=esp_ipv4,
                        help="IP numerica mostrada en la OLED o el monitor serie del ESP32-C6")
    parser.add_argument("--camera", default="/dev/video0", help="Dispositivo UVC (ruta o indice)")
    parser.add_argument("--camera-fps", type=int, default=7,
                        help="FPS solicitados (el RP2350/OV7670 solo ofrece 320x240 @ 7)")
    parser.add_argument("--model", default=str(HERE / "yolo26n.pt"), help="Archivo del modelo YOLO")
    parser.add_argument("--conf", type=float, default=0.35, help="Confianza minima de YOLO")
    parser.add_argument("--jpeg-quality", type=int, default=90, help="Calidad JPEG del video (50-95)")
    parser.add_argument("--web-host", default="127.0.0.1", help="Interfaz del panel web (0.0.0.0 = red)")
    parser.add_argument("--web-port", type=int, default=8000, help="Puerto del panel web")
    parser.add_argument("--report-dir", type=Path, default=HERE / "reportes", help="Directorio de CSV")
    return parser.parse_args()


def main():
    args = parse_args()
    args.report_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.report_dir / f"reporte-{datetime.now().strftime('%Y%m%d-%H%M%S-%f')}.csv"
    with report_path.open("w", newline="", encoding="utf-8") as report:
        csv.DictWriter(report, fieldnames=FIELDS).writeheader()
    state = SharedState(args.esp_ip, report_path)

    print(f"Cargando modelo: {args.model}")
    model = YOLO(args.model)
    # Calentamiento: la primera inferencia es lenta; que no la pague el primer fotograma.
    model.predict(np.zeros((240, 320, 3), np.uint8), classes=[0], imgsz=320,
                  device="cpu", verbose=False)

    try:
        server = ThreadingHTTPServer((args.web_host, args.web_port), make_handler(state))
    except OSError as exc:
        if exc.errno == errno.EADDRINUSE:
            raise SystemExit(
                f"El puerto {args.web_port} ya esta ocupado. Cierra el panel anterior "
                f"o prueba --web-port {args.web_port + 1}."
            ) from None
        raise
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    camera = CameraReader(state, args.camera, args.camera_fps)
    camera.start()
    esp = threading.Thread(target=esp_worker, args=(state, args.esp_ip, report_path), daemon=True)
    esp.start()
    if args.web_host == "0.0.0.0":
        print(f"Panel local: http://127.0.0.1:{args.web_port}")
        print(f"Panel en red: abre la IP de esta computadora en el puerto {args.web_port}")
    else:
        print(f"Panel: http://{args.web_host}:{args.web_port}")
    print(f"ESP32-C6 (API, puerto 80): http://{args.esp_ip}:80/status")
    print(f"Reporte: {report_path}")

    encode_params = [cv2.IMWRITE_JPEG_QUALITY, max(50, min(95, args.jpeg_quality))]
    last_seq = 0
    consecutive_detected = consecutive_absent = 0
    confirmed_persons, confirmed_confidence = 0, 0.0
    fps, previous = 0.0, 0.0
    try:
        while True:
            seq, frame, captured_at = camera.wait_next(last_seq)
            if seq == last_seq or frame is None:
                continue
            last_seq = seq
            result = model.predict(frame, classes=[0], conf=args.conf, imgsz=320,
                                   device="cpu", verbose=False)[0]
            boxes = result.boxes
            persons = len(boxes)
            confidence = float(boxes.conf.max().item()) if persons else 0.0
            # Filtro temporal: 2 fotogramas para confirmar, 3 para liberar.
            previous_confirmed = confirmed_persons
            if persons:
                consecutive_detected += 1
                consecutive_absent = 0
                if consecutive_detected >= 2:
                    confirmed_persons, confirmed_confidence = persons, confidence
            else:
                consecutive_detected = 0
                consecutive_absent += 1
                if consecutive_absent >= 3:
                    confirmed_persons, confirmed_confidence = 0, 0.0
            if (confirmed_persons > 0) != (previous_confirmed > 0):
                state.esp_wake.set()  # avisa al ESP32 de inmediato, sin esperar el periodo

            # Video limpio: las cajas se dibujan en el navegador a resolucion de pantalla.
            ok, encoded = cv2.imencode(".jpg", frame, encode_params)
            now = time.monotonic()
            if previous:
                fps = ema(fps, 1.0 / max(now - previous, 1e-3))
            previous = now
            box_list = [
                {"x1": round(x1, 4), "y1": round(y1, 4), "x2": round(x2, 4), "y2": round(y2, 4),
                 "conf": round(c, 3)}
                for (x1, y1, x2, y2), c in zip(boxes.xyxyn.tolist(), boxes.conf.tolist())
            ]
            speed = result.speed or {}
            with state.cond:
                if ok:
                    state.frame_jpeg = encoded.tobytes()
                    state.frame_seq += 1
                state.latest.update(
                    camera="activa", persons=persons, confidence=round(confidence, 3),
                    confirmed_persons=confirmed_persons,
                    confirmed_confidence=round(confirmed_confidence, 3),
                    fps=round(fps, 1), boxes=box_list, frame_seq=state.frame_seq,
                    frame_size=[frame.shape[1], frame.shape[0]],
                    inference_ms=round(sum(speed.values()), 1),
                    pipeline_ms=round((now - captured_at) * 1000, 1),
                )
                state.version += 1
                state.cond.notify_all()
    except KeyboardInterrupt:
        print("Detenido por el usuario")
    finally:
        state.stop.set()
        state.esp_wake.set()
        with state.cond:
            state.cond.notify_all()
        camera.join(timeout=2)
        esp.join(timeout=2)
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
