#include <WiFi.h>
#include <WebServer.h>
#include <MFRC522.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "iPhoneKauann";
const char* password = "Kauann123";
const char* apiUrl = "http://172.20.10.2:5263/api/Usuario/BuscarPorRFID?rfid=";

// Pinos do MFRC522 no ESP32-S3
#define RST_PIN 9
#define SS_PIN 10
#define SCK_PIN 18
#define MOSI_PIN 11
#define MISO_PIN 13
#define LED_RED 4
#define LED_GREEN 2

int led_controller;
bool modoCadastro = false; // Variável para indicar modo de cadastro
unsigned long lastRegisterTime = 0; // Pausa o modo leitura após cadastro
unsigned long lastCheckTime = 0; // Controla frequência de leitura no modo contínuo
unsigned long lastTagReadTime = 0; // Pausa após leitura de tag
const unsigned long checkInterval = 50; // Verifica tags a cada 50ms
const unsigned long tagReadPause = 1500; // Pausa de 1,5s após leitura de tag
const unsigned long registerPause = 2500; // Pausa de 2,5s após cadastro

WebServer server(80);
MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  led_controller = 0;

  // Configura os pinos como saída
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(RST_PIN, HIGH);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);

  Serial.begin(115200);
  delay(2000);
  Serial.println("Iniciando ESP32-S3 com MFRC522...");

  rfid.PCD_Init();
  Serial.print("Versão do MFRC522: ");
  rfid.PCD_DumpVersionToSerial();
  Serial.println("Leitor RFID inicializado.");

  Serial.print("Conectando à rede Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("Wi-Fi conectado!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());

  server.on("/read-rfid", HTTP_GET, handleReadRFID);

  // CORS
  server.on("/read-rfid", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });

  server.begin();
  Serial.println("Servidor HTTP iniciado!");
}

void loop() {
  server.handleClient();

  // Leitura contínua apenas se não estiver no modo de cadastro,
  // após 2,5s do fim do modo de cadastro, após 2s da última leitura,
  // a cada 50ms, e com Wi-Fi conectado
  if (!modoCadastro && 
      (millis() - lastRegisterTime > registerPause) && 
      (millis() - lastTagReadTime > tagReadPause) &&
      (millis() - lastCheckTime >= checkInterval) && 
      WiFi.status() == WL_CONNECTED) {
    checkRFIDTag();
    lastCheckTime = millis();
  } else if (millis() - lastTagReadTime <= tagReadPause) {
    // pausa após leitura de tag
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 500) {
      Serial.println("Pausado por 1,5s após leitura de tag.");
      lastDebugTime = millis();
    }
  } else if (millis() - lastRegisterTime <= registerPause) {
    // Depuração: indica pausa após cadastro
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 500) {
      Serial.println("Pausado por 2,5s após modo de cadastro.");
      lastDebugTime = millis();
    }
  }
}

String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

bool checkTagAuthorization(String uid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi desconectado. Não foi possível consultar a API.");
    return false;
  }

  unsigned long startTime = millis();
  HTTPClient http;
  String url = String(apiUrl) + uid;
  Serial.println("Enviando requisição para: " + url);
  http.begin(url);
  http.setTimeout(500); // Timeout de 500ms para a requisição

  int httpCode = http.GET();

  bool authorized = false;
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Resposta da API: " + payload);
    Serial.println("Tempo da requisição: " + String(millis() - startTime) + "ms");

    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.println("Erro ao parsear JSON: " + String(error.c_str()));
    } else {
      authorized = doc["authorized"] | false;
    }
  } else {
    Serial.println("Erro na requisição HTTP: " + String(httpCode));
  }

  http.end();
  return authorized;
}

void checkRFIDTag() {
  unsigned long startTime = millis();
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = getUID();
    Serial.println("Tag lida (verificação): " + uid);

    // Verificar autorização da tag na API (BD)
    bool authorized = checkTagAuthorization(uid);

    // Controla os LEDs com base na autorização
    if (authorized) {
      Serial.println("Tag autorizada!");
      digitalWrite(LED_GREEN, HIGH);
      delay(300);
      digitalWrite(LED_GREEN, LOW);
    } else {
      Serial.println("Tag não autorizada!");
      digitalWrite(LED_RED, HIGH);
      delay(300);
      digitalWrite(LED_RED, LOW);
    }

    // Reinicializa o leitor de tags
    rfid.PICC_HaltA();
    digitalWrite(RST_PIN, LOW);
    delay(10);
    digitalWrite(RST_PIN, HIGH);
    delay(10);
    rfid.PCD_Init();

    lastTagReadTime = millis(); // Marca o tempo da leitura para pausa de 1,5s
    Serial.println("Tempo total de leitura: " + String(millis() - startTime) + "ms");
  }
}

void handleReadRFID() {
  Serial.println("Requisição recebida em /read-rfid (modo de cadastro)");
  modoCadastro = true; // Ativa o modo de cadastro
  String uid = "";
  unsigned long startTime = millis();
  bool ledState = false;

  // Lê a tag por 2 segundos, piscando o LED verde
  while (millis() - startTime < 1700) {
    ledState = !ledState;
    digitalWrite(LED_GREEN, ledState ? HIGH : LOW);
    if (uid == "" && rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.println("Tag detectada no modo de cadastro!");
      led_controller = 0;
      uid = getUID();
      Serial.println("Tag lida: " + uid);
    }
    delay(50);
  }

  digitalWrite(LED_GREEN, LOW);

  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (uid != "") {
    bool authorized = checkTagAuthorization(uid);
    String response = "{\"uid\": \"" + uid + "\", \"authorized\": " + (authorized ? "true" : "false") + "}";
    server.send(200, "application/json", response);
    digitalWrite(LED_GREEN, HIGH); // Sempre verde quando a tag é lida
    delay(300);
    digitalWrite(LED_GREEN, LOW);
  } else {
    led_controller = 1;
    Serial.println("Nenhuma tag detectada após 2 segundos.");
    server.send(404, "application/json", "{\"mensagem\": \"Nenhuma tag detectada. Aproxime uma tag do leitor.\"}");
    digitalWrite(LED_RED, HIGH);
    delay(300);
    digitalWrite(LED_RED, LOW);
  }

  rfid.PICC_HaltA();
  digitalWrite(RST_PIN, LOW);
  delay(10);
  digitalWrite(RST_PIN, HIGH);
  delay(10);
  rfid.PCD_Init();
  Serial.println("Leitor RFID reinicializado para nova leitura.");

  lastRegisterTime = millis(); // Marca o tempo para pausa de 2,5s
  modoCadastro = false; // Desativa o modo de cadastro
}