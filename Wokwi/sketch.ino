#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// --- Configurações de rede ---
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";


const char* BACKEND_URL = "http://35.192.65.120:8081/telemetry";


const char* DEVICE_ID = "pico-001";


#define PIN_BUTTON 15   
#define PIN_POT    26   

// --- Debouncing ---
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;
int lastButtonState = LOW;
int buttonState     = LOW;

// --- Média móvel para o analógico ---
const int WINDOW_SIZE = 10;
int readings[WINDOW_SIZE];
int readIndex = 0;
long total    = 0;

// --- Intervalo de envio ---
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 3000;


// Funções de Wi-Fi
void connectWiFi() {
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFalha ao conectar. Tentando novamente em 5s...");
  }
}

void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Desconectado! Reconectando...");
    WiFi.disconnect();
    connectWiFi();
  }
}

// Gera timestamp RFC3339
String getTimestamp() {
  unsigned long ms = millis();
  unsigned long s  = ms / 1000;
  unsigned long m  = s / 60;
  unsigned long h  = m / 60;

  char buf[30];
  snprintf(buf, sizeof(buf), "2025-01-01T%02lu:%02lu:%02luZ",
           h % 24, m % 60, s % 60);
  return String(buf);
}


// Envia telemetria para o backend
bool sendTelemetry(String sensorType, String readingType, String value) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Sem Wi-Fi, abortando envio.");
    return false;
  }

  String timestamp = getTimestamp();
  String body = "{";
  body += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  body += "\"sensor_type\":\"" + sensorType + "\",";
  body += "\"reading_type\":\"" + readingType + "\",";
  body += "\"timestamp\":\"" + timestamp + "\",";
  body += "\"value\":" + value;
  body += "}";

  Serial.println("[HTTP] Enviando: " + body);

  // Tentativa 1
  HTTPClient http;
  http.begin(BACKEND_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  int httpCode = http.POST(body);
  http.end();

  if (httpCode == 202) {
    Serial.println("[HTTP] Telemetria aceita!");
    return true;
  }

  Serial.print("[HTTP] Falhou (");
  Serial.print(httpCode);
  Serial.println("), tentando novamente...");
  delay(1000);

  // Retry — novo objeto HTTPClient
  HTTPClient http2;
  http2.begin(BACKEND_URL);
  http2.addHeader("Content-Type", "application/json");
  http2.setTimeout(5000);
  int retryCode = http2.POST(body);
  http2.end();

  if (retryCode == 202) {
    Serial.println("[HTTP] Retry bem-sucedido!");
    return true;
  }

  Serial.println("[HTTP] Retry falhou também.");
  return false;
}


// Setup
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_BUTTON, INPUT_PULLDOWN);

  // Inicializa média móvel
  for (int i = 0; i < WINDOW_SIZE; i++) readings[i] = 0;

  Serial.println("=== Ponderada 2 — Iniciando ===");
  connectWiFi();
  Serial.println("================================");
}


void loop() {
  checkWiFi();

  // SENSOR DIGITAL
  int reading = digitalRead(PIN_BUTTON);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      int value = (buttonState == HIGH) ? 1 : 0;
      Serial.print("[DIGITAL] button = ");
      Serial.println(value);

      sendTelemetry("button", "discrete", String(value));
    }
  }
  lastButtonState = reading;

  // SENSOR ANALÓGICO: potenciômetro com média móvel 
  total -= readings[readIndex];
  readings[readIndex] = analogRead(PIN_POT);
  total += readings[readIndex];
  readIndex = (readIndex + 1) % WINDOW_SIZE;
  float average = (float)total / WINDOW_SIZE;
  float percent = (average / 4095.0) * 100.0;

  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();

    // Analógico
    Serial.print("[ANALOG]  potentiometer = ");
    Serial.print(percent, 1);
    Serial.println("%");
    sendTelemetry("potentiometer", "analog", String((int)average));

    // Digital periódico
    int digitalValue = (buttonState == HIGH) ? 1 : 0;
    Serial.print("[DIGITAL] button (periodic) = ");
    Serial.println(digitalValue);
    sendTelemetry("button", "discrete", String(digitalValue));
  }

  delay(50);
}