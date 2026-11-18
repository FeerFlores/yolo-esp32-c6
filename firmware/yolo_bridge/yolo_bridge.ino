#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <DevLabDDP.h>
#include <DevLab_I2C_Orchestrator.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#if !defined(CONFIG_IDF_TARGET_ESP32C6)
#error "Este firmware requiere ESP32-C6"
#endif

// Cambiar por los datos de la misma red Wi-Fi que usa la computadora.
const char *WIFI_SSID = "devlab";
const char *WIFI_PASSWORD = "devlab12345678";

constexpr int SDA_PIN = 6;
constexpr int SCL_PIN = 7;
constexpr uint8_t BUTTON_ADDR = 0x30;
constexpr uint8_t LIGHT_ADDR = 0x26;
constexpr uint8_t SCD41_ADDR = 0x62;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t TRAFFIC_ADDR = 0x36;
constexpr uint8_t TRAFFIC_READ = 0x80;
constexpr uint8_t TRAFFIC_SET = 0xA8;
constexpr uint8_t TRAFFIC_GREEN = 1U << 0;
constexpr uint8_t TRAFFIC_YELLOW = 1U << 1;
constexpr uint8_t TRAFFIC_RED = 1U << 2;
constexpr uint32_t YOLO_TIMEOUT_MS = 4000;
constexpr uint32_t NORMAL_RED_MS = 5000;
constexpr uint32_t NORMAL_GREEN_MS = 5000;
constexpr uint32_t NORMAL_YELLOW_MS = 2000;

WebServer server(80);
DevLab_I2C_Orchestrator i2cBus(Wire, 100000U);
DevLabDDP::Master button(i2cBus, 0x0110);
DevLabDDP::Master light(i2cBus, DevLabDDP::DEVICE_GT36537);
DevLabDDP::Master traffic(i2cBus, 0x0116);
SensirionI2cScd4x scd41;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

bool buttonReady = false;
bool lightReady = false;
bool scd41Ready = false;
bool trafficReady = false;
bool oledReady = false;
bool buttonHasData = false;
bool lightHasData = false;
bool scd41HasData = false;
bool buttonPressed = false;
uint16_t lightValue = 0;
uint16_t co2 = 0;
float temperature = 0;
float humidity = 0;
uint32_t lastYoloMessage = 0;
uint32_t lastTrafficAttempt = 0;
uint8_t appliedTrafficMask = 0xFF;
uint16_t personCount = 0;
float personConfidence = 0;
bool wifiWasConnected = false;
uint8_t normalPhase = 0;
uint32_t phaseStart = 0;

bool yoloOnline() {
  return lastYoloMessage != 0 && millis() - lastYoloMessage <= YOLO_TIMEOUT_MS;
}

uint8_t desiredTrafficMask() {
  uint32_t now = millis();
  // El semaforo peatonal sigue su ciclo; YOLO y el boton solo aportan datos.
  const uint32_t durations[] = {NORMAL_RED_MS, NORMAL_GREEN_MS, NORMAL_YELLOW_MS};
  if (now - phaseStart >= durations[normalPhase]) {
    normalPhase = (normalPhase + 1) % 3;
    phaseStart = now;
  }
  const uint8_t masks[] = {TRAFFIC_RED, TRAFFIC_GREEN, TRAFFIC_YELLOW};
  return masks[normalPhase];
}

bool setTrafficLights(uint8_t mask) {
  uint8_t status = 0xFF;
  if (!traffic.readCommand(TRAFFIC_ADDR, TRAFFIC_SET | mask, &status, 1, 2) || status != 0) {
    return false;
  }
  uint8_t actual = 0;
  return traffic.readCommand(TRAFFIC_ADDR, TRAFFIC_READ, &actual, 1, 2) &&
         (actual & 0x07) == mask;
}

void updateTraffic() {
  if (!trafficReady) return;
  uint8_t desired = desiredTrafficMask();
  if (appliedTrafficMask == desired) return;
  if (appliedTrafficMask == 0xFF && millis() - lastTrafficAttempt < 1000 &&
      lastTrafficAttempt != 0) return;
  lastTrafficAttempt = millis();
  if (setTrafficLights(desired)) {
    appliedTrafficMask = desired;
    Serial.println(desired == TRAFFIC_GREEN ? "Semaforo: VERDE / PASE" :
                   desired == TRAFFIC_YELLOW ? "Semaforo: AMARILLO / ESPERE" :
                   "Semaforo: ROJO / NO PASE");
  } else {
    appliedTrafficMask = 0xFF;
    Serial.println("Error al cambiar el semaforo I2C");
  }
}

String trafficName() {
  if (!trafficReady || appliedTrafficMask == 0xFF) return "ERROR";
  return appliedTrafficMask == TRAFFIC_GREEN ? "VERDE" :
         appliedTrafficMask == TRAFFIC_YELLOW ? "AMARILLO" : "ROJO";
}

void handleStatus() {
  String json;
  json.reserve(360);
  json += "{\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",\"wifi\":";
  json += WiFi.status() == WL_CONNECTED ? "true" : "false";
  json += ",\"yolo_online\":";
  json += yoloOnline() ? "true" : "false";
  json += ",\"person_count\":";
  json += personCount;
  json += ",\"confidence\":";
  json += String(personConfidence, 3);
  json += ",\"button\":";
  json += buttonHasData ? (buttonPressed ? "true" : "false") : "null";
  json += ",\"light_adc\":";
  json += lightHasData ? String(lightValue) : "null";
  json += ",\"co2_ppm\":";
  json += scd41HasData ? String(co2) : "null";
  json += ",\"temperature_c\":";
  json += scd41HasData ? String(temperature, 2) : "null";
  json += ",\"humidity_pct\":";
  json += scd41HasData ? String(humidity, 2) : "null";
  json += ",\"traffic\":\"";
  json += trafficName();
  json += "\",\"pass_allowed\":";
  json += (!trafficReady || appliedTrafficMask == 0xFF) ? "null" :
          appliedTrafficMask == TRAFFIC_GREEN ? "true" : "false";
  json += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleDetection() {
  if (!server.hasArg("count") || !server.hasArg("confidence")) {
    server.send(400, "text/plain", "Faltan count y confidence");
    return;
  }
  String countText = server.arg("count");
  String confidenceText = server.arg("confidence");
  if (countText.isEmpty() || countText.length() > 3 || confidenceText.isEmpty()) {
    server.send(400, "text/plain", "Datos invalidos");
    return;
  }
  for (size_t i = 0; i < countText.length(); i++) {
    if (!isDigit(countText[i])) {
      server.send(400, "text/plain", "count invalido");
      return;
    }
  }
  char *end = nullptr;
  float confidence = strtof(confidenceText.c_str(), &end);
  if (end == confidenceText.c_str() || *end != '\0' || !isfinite(confidence) ||
      confidence < 0 || confidence > 1) {
    server.send(400, "text/plain", "confidence invalido");
    return;
  }
  personCount = countText.toInt();
  personConfidence = confidence;
  lastYoloMessage = millis();
  if (lastYoloMessage == 0) lastYoloMessage = 1;
  updateTraffic();
  handleStatus();
}

void updateOLED() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ESP32-C6 + YOLO");
  display.setCursor(0, 10);
  display.print("IP: ");
  display.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "sin WiFi");
  display.setCursor(0, 20);
  display.print("YOLO: ");
  display.print(yoloOnline() ? "OK" : "SIN SENAL");
  display.print(" P:");
  display.println(personCount);
  display.setCursor(0, 30);
  display.print("CO2:");
  if (scd41HasData) display.print(co2); else display.print("--");
  display.print(" T:");
  if (scd41HasData) display.print(temperature, 1); else display.print("--");
  display.setCursor(0, 40);
  display.print("Luz:");
  if (lightHasData) display.print(lightValue); else display.print("--");
  display.print(" B:");
  if (buttonHasData) display.print(buttonPressed ? "ON" : "OFF"); else display.print("--");
  display.setCursor(0, 52);
  display.print(trafficName());
  display.print(" ");
  display.print(!trafficReady || appliedTrafficMask == 0xFF ? "SIN DATOS" :
                appliedTrafficMask == TRAFFIC_GREEN ? "PASE" : "NO PASE");
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== YOLO BRIDGE ESP32-C6 ===");
  i2cBus.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  buttonReady = button.matchesExpectedDevice(BUTTON_ADDR);
  lightReady = light.matchesExpectedDevice(LIGHT_ADDR);
  trafficReady = traffic.matchesExpectedDevice(TRAFFIC_ADDR);
  Serial.printf("Boton: %s | Luz: %s | Semaforo: %s\n",
                buttonReady ? "OK" : "ausente", lightReady ? "OK" : "ausente",
                trafficReady ? "OK" : "ausente");
  if (trafficReady && setTrafficLights(TRAFFIC_RED)) appliedTrafficMask = TRAFFIC_RED;

  Wire.beginTransmission(SCD41_ADDR);
  scd41Ready = Wire.endTransmission() == 0;
  if (scd41Ready) {
    scd41.begin(Wire, SCD41_ADDR);
    scd41.stopPeriodicMeasurement();
    delay(500);
    scd41Ready = scd41.startPeriodicMeasurement() == 0;
  }
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  updateOLED();

  WiFi.mode(WIFI_STA);
  Serial.print("Conectando Wi-Fi a SSID: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/detect", HTTP_POST, handleDetection);
  Serial.println("API: GET /status, POST /detect");
}

void loop() {
  static uint32_t lastButton = 0;
  static uint32_t lastLight = 0;
  static uint32_t lastSCD41 = 0;
  static uint32_t lastOLED = 0;
  static uint32_t lastWiFiRetry = 0;
  uint32_t now = millis();

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected != wifiWasConnected) {
    wifiWasConnected = wifiConnected;
    if (wifiConnected) server.begin(); else server.stop();
    Serial.print("Wi-Fi: ");
    Serial.println(wifiConnected ? WiFi.localIP().toString() : "desconectado");
  }
  if (!wifiConnected && now - lastWiFiRetry >= 10000) {
    lastWiFiRetry = now;
    Serial.printf("Wi-Fi sin conexion (estado %d); reintentando...\n", WiFi.status());
    WiFi.reconnect();
  }
  if (wifiConnected) server.handleClient();

  if (buttonReady && now - lastButton >= 200) {
    lastButton = now;
    int value = button.readButtons(BUTTON_ADDR, 2);
    if (value >= 0) {
      buttonPressed = (value & 0x01) != 0;
      buttonHasData = true;
    }
  }
  if (lightReady && now - lastLight >= 500) {
    lastLight = now;
    uint8_t bytes[2];
    if (light.readCommand(LIGHT_ADDR, CMD_READ_ADC0, bytes, 2, 2)) {
      lightValue = uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
      lightHasData = true;
    }
  }
  if (scd41Ready && now - lastSCD41 >= 1000) {
    lastSCD41 = now;
    bool ready = false;
    if (scd41.getDataReadyStatus(ready) == 0 && ready) {
      uint16_t measuredCO2 = 0;
      float measuredTemperature = 0;
      float measuredHumidity = 0;
      if (scd41.readMeasurement(measuredCO2, measuredTemperature, measuredHumidity) == 0 &&
          measuredCO2 != 0) {
        co2 = measuredCO2;
        temperature = measuredTemperature;
        humidity = measuredHumidity;
        scd41HasData = true;
      }
    }
  }
  updateTraffic();
  if (now - lastOLED >= 500) {
    lastOLED = now;
    updateOLED();
  }
}
