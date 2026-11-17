#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

const char *ssid = "devlab";
const char *password = "devlab12345678";

// Duración del pulso (ms)
constexpr unsigned long PULSE_MS = 500;

WebServer server(80);

bool ledActive = false;
unsigned long pulseStart = 0;

void handlePulse()
{

    digitalWrite(LED_BUILTIN, HIGH);

    pulseStart = millis();
    ledActive = true;

    Serial.println("Persona detectada");
    Serial.println("LED ENCENDIDO");

    server.send(200, "text/plain", "OK");
}

void setup()
{

    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.println("Conectando a WiFi...");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi conectado");

    Serial.print("IP del ESP32-C6: ");
    Serial.println(WiFi.localIP());
    server.on("/pulse", HTTP_GET, handlePulse);

    server.begin();

    Serial.println("Servidor HTTP iniciado");
}

void loop()
{

    server.handleClient();

    // Apagar LED después del pulso :P
    if (ledActive && millis() - pulseStart >= PULSE_MS)
    {

        digitalWrite(LED_BUILTIN, LOW);

        ledActive = false;

        Serial.println("LED APAGADO");
    }
}
