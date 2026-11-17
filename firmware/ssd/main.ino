#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char *ssid = "devlab";
const char *password = "devlab12345678";

WebServer server(80);

#define SDA_PIN 5
#define SCL_PIN 6

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

constexpr unsigned long PULSE_MS = 500;
constexpr unsigned long MESSAGE_MS = 3000;

bool ledActive = false;
bool messageActive = false;

unsigned long pulseStart = 0;
unsigned long messageStart = 0;

void showStandby()
{
    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(5, 22);
    display.println("Esperando...");

    display.setCursor(0, 43);
    display.print("IP:");

    display.setCursor(0, 54);
    display.println(WiFi.localIP());

    display.display();
}

void showDetection()
{
    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(32, 3);
    display.println("ALERTA");

    display.drawLine(
        0, 16, 127, 16, SSD1306_WHITE);

    display.setTextSize(2);

    display.setCursor(22, 23);
    display.println("PERSONA");

    display.setCursor(10, 43);
    display.println("DETECTADA");

    display.display();
}

void handlePulse()
{
    digitalWrite(LED_BUILTIN, HIGH);

    pulseStart = millis();
    ledActive = true;

    // Mostrar deteccion en OLED
    showDetection();

    messageStart = millis();
    messageActive = true;

    Serial.println("Persona detectada");
    Serial.println("LED ENCENDIDO");
    Serial.println("OLED: PERSONA DETECTADA");

    server.send(200, "text/plain", "Persona detectada - OK");
}

void setup()
{
    Serial.begin(115200);

    // LED integrado
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Inicializar I2C
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);

    // Inicializar OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS))
    {
        Serial.println("Error OLED");

        while (true)
        {
            delay(1000);
        }
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 15);
    display.println("TOUCHDOT S3");

    display.setCursor(0, 35);
    display.println("Conectando WiFi...");

    display.display();

    // Conectar a Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.print("Conectando a WiFi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi conectado");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Mostrar IP en OLED
    showStandby();

    // Servidor HTTP
    server.on("/pulse", HTTP_GET, handlePulse);

    server.begin();

    Serial.println("Servidor iniciado");
}

void loop()
{
    server.handleClient();

    // Apagar LED despues de 500 ms
    if (ledActive && millis() - pulseStart >= PULSE_MS)
    {
        digitalWrite(LED_BUILTIN, LOW);
        ledActive = false;
        Serial.println("LED APAGADO");
    }

    // Regresar a pantalla de espera
    if (messageActive && millis() - messageStart >= MESSAGE_MS)
    {
        messageActive = false;
        showStandby();
        Serial.println("Esperando deteccion...");
    }
}
