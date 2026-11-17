
#include <Arduino.h>
#include <Wire.h>
#include <DevLabDDP.h>
#include <DevLab_I2C_Orchestrator.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#if !defined(CONFIG_IDF_TARGET_ESP32C6)
  #error "This example targets ESP32-C6"
#endif

// --------------------------------------------------
// I2C configuration
// --------------------------------------------------

constexpr int SDA_PIN = 6;
constexpr int SCL_PIN = 7;

TwoWire &sensorBus = Wire;

// Device addresses
constexpr uint8_t BUTTON_ADDR  = 0x30;
constexpr uint8_t LIGHT_ADDR   = 0x26;
constexpr uint8_t SCD41_ADDR   = 0x62;
constexpr uint8_t PIXEL_ADDR   = 0x37;
constexpr uint8_t OLED_ADDR    = 0x3C;
constexpr uint8_t TRAFFIC_ADDR = 0x36;

// Device IDs
constexpr uint16_t BUTTON_ID  = 0x0110;
constexpr uint16_t TRAFFIC_ID = 0x0116;

// Timing
constexpr uint32_t BUTTON_INTERVAL_MS  = 200;
constexpr uint32_t LIGHT_INTERVAL_MS   = 250;
constexpr uint32_t SCD41_INTERVAL_MS   = 1000;
constexpr uint32_t OLED_INTERVAL_MS    = 500;
constexpr uint32_t TRAFFIC_INTERVAL_MS = 1000;

constexpr uint16_t RESPONSE_DELAY_MS = 2;
constexpr uint8_t BUTTON_MASK = 0x01;

// --------------------------------------------------
// Controllers
// --------------------------------------------------

DevLab_I2C_Orchestrator i2cBus(sensorBus, 100000U);

DevLabDDP::Master button(i2cBus, BUTTON_ID);
DevLabDDP::Master light(
    i2cBus, DevLabDDP::DEVICE_GT36537);
DevLabDDP::Master traffic(i2cBus, TRAFFIC_ID);

SensirionI2cScd4x scd41;

Adafruit_SSD1306 display(
    128, 64, &sensorBus, -1);

// --------------------------------------------------
// Device status
// --------------------------------------------------

bool buttonReady = false;
bool lightReady = false;
bool scd41Ready = false;
bool pixelReady = false;
bool oledReady = false;
bool trafficReady = false;

// Sensor data
bool buttonHasData = false;
bool lightHasData = false;
bool scd41HasData = false;

bool lastButtonPressed = false;
uint16_t lastLightValue = 0;
uint16_t lastCO2 = 0;
float lastTemperature = 0;
float lastHumidity = 0;

// --------------------------------------------------
// RGB configuration
// --------------------------------------------------

struct Color {
  const char *name;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

const Color COLORS[] = {
  {"red",       255,   0,   0},
  {"orange",    255,  90,   0},
  {"yellow",    255, 200,   0},
  {"green",       0, 255,   0},
  {"turquoise",   0, 255, 130},
  {"cyan",        0, 255, 255},
  {"blue",        0,   0, 255},
  {"violet",    130,   0, 255},
  {"magenta",   255,   0, 180},
  {"white",     255, 255, 255}
};

constexpr uint8_t COLOR_COUNT =
    sizeof(COLORS) / sizeof(COLORS[0]);

uint8_t colorIndex = 0;
uint8_t rgbPhase = 0;
uint32_t lastRGB = 0;

const char *led0Color = "--";
const char *led1Color = "--";

// --------------------------------------------------
// Traffic light configuration
// --------------------------------------------------

constexpr uint8_t TRAFFIC_READ = 0x80;
constexpr uint8_t TRAFFIC_SET = 0xA8;

constexpr uint8_t TRAFFIC_GREEN  = 1U << 0;
constexpr uint8_t TRAFFIC_YELLOW = 1U << 1;
constexpr uint8_t TRAFFIC_RED    = 1U << 2;

uint8_t trafficStep = 0;
uint32_t lastTraffic = 0;

const char *trafficState = "--";

// --------------------------------------------------
// I2C probe
// --------------------------------------------------

bool probe(uint8_t address) {
  sensorBus.beginTransmission(address);
  return sensorBus.endTransmission() == 0;
}

// --------------------------------------------------
// RGB commands with diagnostics
// --------------------------------------------------

bool sendPixelCommand(
    uint8_t command,
    const uint8_t *data,
    uint8_t length) {

  sensorBus.beginTransmission(PIXEL_ADDR);
  sensorBus.write(command);

  for (uint8_t i = 0; i < length; i++) {
    sensorBus.write(data[i]);
  }

  uint8_t status = sensorBus.endTransmission();

  if (status != 0) {
    Serial.printf(
        "RGB 0x%02X TX error: %u\n",
        command, status);
    return false;
  }

  // PY32 prepares response after STOP
  delay(5);

  int received = sensorBus.requestFrom(
      (int)PIXEL_ADDR, 1);

  if (received != 1) {
    Serial.printf(
        "RGB 0x%02X: no response\n",
        command);
    return false;
  }

  uint8_t response = sensorBus.read();

  if (response != 0) {
    Serial.printf(
        "RGB 0x%02X response: 0x%02X\n",
        command, response);
    return false;
  }

  return true;
}

bool setPixel(
    uint8_t index,
    uint8_t red,
    uint8_t green,
    uint8_t blue) {

  const uint8_t data[] = {
    index, red, green, blue
  };

  return sendPixelCommand(
      0xB0, data, sizeof(data));
}

bool fillPixel(
    uint8_t red,
    uint8_t green,
    uint8_t blue) {

  const uint8_t data[] = {
    red, green, blue
  };

  return sendPixelCommand(
      0xB1, data, sizeof(data));
}

// --------------------------------------------------
// Light sensor
// --------------------------------------------------

bool readLight(uint16_t &value) {
  uint8_t bytes[2];

  if (!light.readCommand(
      LIGHT_ADDR,
      CMD_READ_ADC0,
      bytes,
      2,
      RESPONSE_DELAY_MS)) {
    return false;
  }

  value = (uint16_t)bytes[0] |
          ((uint16_t)bytes[1] << 8);

  return true;
}

// --------------------------------------------------
// Traffic light commands
// --------------------------------------------------

bool setTrafficLights(uint8_t mask) {
  uint8_t status = 0xFF;

  if (!traffic.readCommand(
      TRAFFIC_ADDR,
      TRAFFIC_SET | (mask & 0x07),
      &status,
      1,
      RESPONSE_DELAY_MS) || status != 0) {

    Serial.println("Traffic I2C set failed");
    return false;
  }

  uint8_t actual = 0;

  if (!traffic.readCommand(
      TRAFFIC_ADDR,
      TRAFFIC_READ,
      &actual,
      1,
      RESPONSE_DELAY_MS)) {

    Serial.println("Traffic I2C read failed");
    return false;
  }

  if ((actual & 0x07) != (mask & 0x07)) {
    Serial.printf(
        "Traffic mismatch: expected 0x%02X, got 0x%02X\n",
        mask, actual);
    return false;
  }

  return true;
}

// --------------------------------------------------
// RGB sequence
// --------------------------------------------------

void updateRGB() {
  if (!pixelReady) return;

  uint32_t now = millis();

  const Color &first = COLORS[colorIndex];
  const Color &second =
      COLORS[(colorIndex + 5) % COLOR_COUNT];

  if (rgbPhase == 0) {

    bool led0OK = setPixel(
        0, first.red, first.green, first.blue);

    bool led1OK = setPixel(
        1, second.red, second.green, second.blue);

    if (led0OK) led0Color = first.name;
    if (led1OK) led1Color = second.name;

    Serial.printf(
        "RGB LED0: %s | LED1: %s\n",
        led0Color, led1Color);

    if (!led0OK || !led1OK)
      Serial.println("RGB individual pixel error");

    lastRGB = millis();
    rgbPhase = 1;

  } else if (rgbPhase == 1 &&
             now - lastRGB >= 1200) {

    if (fillPixel(
        first.red, first.green, first.blue)) {

      led0Color = first.name;
      led1Color = first.name;

      Serial.print("RGB fill: ");
      Serial.println(first.name);
    } else {
      Serial.println("RGB fill error");
    }

    lastRGB = millis();
    rgbPhase = 2;

  } else if (rgbPhase == 2 &&
             now - lastRGB >= 700) {

    colorIndex = (colorIndex + 1) % COLOR_COUNT;
    rgbPhase = 0;
  }
}

// --------------------------------------------------
// Traffic light sequence
// --------------------------------------------------

void updateTraffic() {
  if (!trafficReady) return;

  uint32_t now = millis();

  if (now - lastTraffic < TRAFFIC_INTERVAL_MS)
    return;

  lastTraffic = now;

  const uint8_t masks[] = {
    TRAFFIC_RED,
    TRAFFIC_YELLOW,
    TRAFFIC_GREEN,
    0U
  };

  const char *names[] = {
    "RED",
    "YELLOW",
    "GREEN",
    "OFF"
  };

  uint8_t mask = masks[trafficStep];

  if (setTrafficLights(mask)) {
    trafficState = names[trafficStep];

    Serial.print("Traffic: ");
    Serial.println(trafficState);
  }

  trafficStep = (trafficStep + 1) % 4;
}

// --------------------------------------------------
// OLED display
// --------------------------------------------------

void updateOLED() {
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("UNIT DevLab Monitor");

  display.drawLine(
      0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.print("CO2:");
  if (scd41HasData) {
    display.print(lastCO2);
  } else {
    display.print("--");
  }
  display.print("ppm");

  display.setCursor(0, 24);
  display.print("T:");
  if (scd41HasData)
    display.print(lastTemperature, 1);
  else
    display.print("--");

  display.print("C H:");
  if (scd41HasData)
    display.print(lastHumidity, 1);
  else
    display.print("--");
  display.print("%");

  display.setCursor(0, 34);
  display.print("L:");
  if (lightHasData)
    display.print(lastLightValue);
  else
    display.print("--");

  display.print(" B:");
  if (buttonHasData)
    display.print(lastButtonPressed ? "ON" : "OFF");
  else
    display.print("--");

  display.setCursor(0, 44);
  display.print("RGB:");
  display.print(led0Color);
  display.print("/");
  display.print(led1Color);

  display.setCursor(0, 54);
  display.print("Traffic: ");
  display.print(trafficState);

  display.display();
}

// --------------------------------------------------
// Setup
// --------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  i2cBus.begin(SDA_PIN, SCL_PIN);

  Serial.println();
  Serial.println("UNIT DevLab I2C Monitor");
  Serial.println("Initializing six modules...");

  // Button
  DevLabDDP::DeviceInfo buttonInfo;

  buttonReady =
      button.matchesExpectedDevice(
          BUTTON_ADDR, &buttonInfo);

  Serial.println(buttonReady ?
      "Button ready at 0x30" :
      "Button not detected");

  // Light
  DevLabDDP::DeviceInfo lightInfo;

  lightReady =
      light.matchesExpectedDevice(
          LIGHT_ADDR, &lightInfo);

  Serial.println(lightReady ?
      "GT36537 ready at 0x26" :
      "GT36537 not detected");

  // SCD41
  scd41Ready = probe(SCD41_ADDR);

  if (scd41Ready) {
    scd41.begin(sensorBus, SCD41_ADDR);

    int16_t stopError =
        scd41.stopPeriodicMeasurement();

    if (stopError) {
      Serial.printf(
          "SCD41 stop status: %d\n",
          stopError);
    }

    delay(500);

    int16_t error =
        scd41.startPeriodicMeasurement();

    if (error) {
      scd41Ready = false;
      Serial.printf(
          "SCD41 start error: %d\n",
          error);
    } else {
      Serial.println("SCD41 ready at 0x62");
    }
  } else {
    Serial.println("SCD41 not detected");
  }

  // RGB
  pixelReady = probe(PIXEL_ADDR);

  if (pixelReady) {
    Serial.println("RGB detected at 0x37");

    const uint8_t count = 2;
    const uint8_t brightness = 128;

    bool countOK = sendPixelCommand(
        0xBA, &count, 1);

    bool brightnessOK = sendPixelCommand(
        0xB5, &brightness, 1);

    Serial.printf(
        "RGB count: %s | brightness: %s\n",
        countOK ? "OK" : "ERROR",
        brightnessOK ? "OK" : "ERROR");

    bool led0OK = setPixel(0, 255, 0, 0);
    bool led1OK = setPixel(1, 0, 0, 255);

    Serial.printf(
        "LED0: %s | LED1: %s\n",
        led0OK ? "OK" : "ERROR",
        led1OK ? "OK" : "ERROR");

    if (led0OK) led0Color = "red";
    if (led1OK) led1Color = "blue";

  } else {
    Serial.println("RGB not detected at 0x37");

    if (probe(0x24)) {
      Serial.println(
          "Old WS12xx firmware may be at 0x24");
    }
  }

  // OLED
  oledReady = display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDR);

  Serial.println(oledReady ?
      "OLED ready at 0x3C" :
      "OLED not detected");

  // Traffic light
  DevLabDDP::DeviceInfo trafficInfo;

  trafficReady =
      traffic.matchesExpectedDevice(
          TRAFFIC_ADDR, &trafficInfo);

  if (trafficReady) {
    Serial.println("UE0183 ready at 0x36");

    if (setTrafficLights(0U)) {
      trafficState = "OFF";
    }
  } else {
    Serial.println("UE0183 not detected at 0x36");
  }

  Serial.println("Initialization completed");
}

// --------------------------------------------------
// Main loop
// --------------------------------------------------

void loop() {
  static uint32_t lastButton = 0;
  static uint32_t lastLight = 0;
  static uint32_t lastSCD41 = 0;
  static uint32_t lastOLED = 0;

  uint32_t now = millis();

  // Button
  if (buttonReady &&
      now - lastButton >= BUTTON_INTERVAL_MS) {

    lastButton = now;

    int raw = button.readButtons(
        BUTTON_ADDR, RESPONSE_DELAY_MS);

    if (raw >= 0) {
      lastButtonPressed =
          (raw & BUTTON_MASK) != 0;
      buttonHasData = true;
    } else {
      Serial.println("Button read error");
    }
  }

  // Light
  if (lightReady &&
      now - lastLight >= LIGHT_INTERVAL_MS) {

    lastLight = now;

    uint16_t raw = 0;

    if (readLight(raw)) {
      lastLightValue = raw;
      lightHasData = true;
    } else {
      Serial.println("Light read error");
    }
  }

  // SCD41
  if (scd41Ready &&
      now - lastSCD41 >= SCD41_INTERVAL_MS) {

    lastSCD41 = now;

    bool ready = false;

    int16_t error =
        scd41.getDataReadyStatus(ready);

    if (error) {
      Serial.printf(
          "SCD41 status error: %d\n",
          error);
    } else if (ready) {

      uint16_t co2 = 0;
      float temperature = 0;
      float humidity = 0;

      error = scd41.readMeasurement(
          co2, temperature, humidity);

      if (!error && co2 != 0) {
        lastCO2 = co2;
        lastTemperature = temperature;
        lastHumidity = humidity;
        scd41HasData = true;

        Serial.printf(
            "CO2: %u ppm | T: %.2f C | RH: %.2f %%\n",
            co2, temperature, humidity);

      } else if (error) {
        Serial.printf(
            "SCD41 read error: %d\n",
            error);
      }
    }
  }

  // RGB sequence
  updateRGB();

  // Traffic light sequence
  updateTraffic();

  // OLED
  if (oledReady &&
      now - lastOLED >= OLED_INTERVAL_MS) {

    lastOLED = now;
    updateOLED();
  }
}
