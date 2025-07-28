#include <WiFi.h>
#include <WebServer.h>
#include <MFRC522.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "Kondrusik-2G";
const char* password = "84788347";
const char* apiUrl = "http://192.168.1.9:5263/api/Usuario/BuscarPorRFID?rfid=";
const char* registroUrl = "http://192.168.1.9:5263/api/AcessoEntradaMorador/RegistrarEntrada";

// Pinos do MFRC522 no ESP32-S3
#define RST_PIN 9     // GPIO9 - Reset do RC522
#define SS_PIN 10     // GPIO10 - SDA / Chip Select
#define SCK_PIN 12    // GPIO12 - Clock SPI
#define MOSI_PIN 11   // GPIO11 - Master Out
#define MISO_PIN 13   // GPIO13 - Master In
#define LED_RED 4
#define LED_GREEN 2
const int buzzerPin = 5;
int count;

int led_controller;
bool modoCadastro = false;
unsigned long lastRegisterTime = 0;
unsigned long lastCheckTime = 0;
unsigned long lastTagReadTime = 0;
const unsigned long checkInterval = 50;
const unsigned long tagReadPause = 1500;
const unsigned long registerPause = 2500;

WebServer server(80);
MFRC522 rfid(SS_PIN, RST_PIN);


void setup() {
  led_controller = 0;

pinMode(buzzerPin, OUTPUT);

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
  server.on("/read-rfid", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });

  server.begin();

  Serial.println("Servidor HTTP iniciado!");
  toqueBoasVindas();
}

void loop() {
  server.handleClient();

  if (!modoCadastro &&
      (millis() - lastRegisterTime > registerPause) &&
      (millis() - lastTagReadTime > tagReadPause) &&
      (millis() - lastCheckTime >= checkInterval) &&
      WiFi.status() == WL_CONNECTED) {
    checkRFIDTag();
    lastCheckTime = millis();
  } else if (millis() - lastTagReadTime <= tagReadPause) {
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 500) {
      Serial.println("Pausado por 1,5s após leitura de tag.");
      lastDebugTime = millis();
    }
  } else if (millis() - lastRegisterTime <= registerPause) {
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
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
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

  HTTPClient http;
  String url = String(apiUrl) + uid;
  Serial.println("Enviando requisição para: " + url);
  http.begin(url);
  http.setTimeout(500);

  int httpCode = http.GET();
  bool authorized = false;

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Resposta da API: " + payload);
    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      authorized = doc["authorized"] | false;
    } else {
      Serial.println("Erro ao parsear JSON: " + String(error.c_str()));
    }
  } else {
    Serial.println("Erro na requisição HTTP: " + String(httpCode));
  }

  http.end();
  return authorized;
}

void registrarEntrada(String uid) {
  HTTPClient http;
  http.begin(registroUrl);
  http.addHeader("Content-Type", "application/json");

  String body = "{\"CodigoRFID\": \"" + uid + "\"}";
  int responseCode = http.POST(body);

  if (responseCode == HTTP_CODE_OK) {
    String resposta = http.getString();
    Serial.println("Entrada registrada com sucesso: " + resposta);
  } else {
    Serial.println("Erro ao registrar entrada. Código HTTP: " + String(responseCode));
  }

  http.end();
}

void checkRFIDTag() {
  unsigned long startTime = millis();
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = getUID();
    Serial.println("Tag lida (verificação): " + uid);

    bool authorized = checkTagAuthorization(uid);

    if (authorized) {
      Serial.println("Tag autorizada!");
      digitalWrite(LED_GREEN, HIGH);
      delay(300);
      digitalWrite(LED_GREEN, LOW);
      toqueSucesso();
      registrarEntrada(uid);  // ✅ Registra o acesso se autorizado
    } else {
      Serial.println("Tag não autorizada!");
      digitalWrite(LED_RED, HIGH);
      delay(300);
      toqueRecusado();
      digitalWrite(LED_RED, LOW);
    }

    rfid.PICC_HaltA();
    digitalWrite(RST_PIN, LOW);
    delay(10);
    digitalWrite(RST_PIN, HIGH);
    delay(10);
    rfid.PCD_Init();

    lastTagReadTime = millis();
    Serial.println("Tempo total de leitura: " + String(millis() - startTime) + "ms");
  }
}

void handleReadRFID() {
  Serial.println("Requisição recebida em /read-rfid (modo de cadastro)");
  
  // ✅ Pausar imediatamente a leitura automática
  modoCadastro = true;

  String uid = "";
  unsigned long startTime = millis();
  bool ledState = false;

  // Pisca o LED por ~1.7 segundos enquanto espera a tag
  while (millis() - startTime < 1700) {
    ledState = !ledState;
    digitalWrite(LED_GREEN, ledState ? HIGH : LOW);
    
    if (uid == "" && rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.println("Tag detectada no modo de cadastro!");
      uid = getUID();
      Serial.println("Tag lida: " + uid);
    }

    delay(50);
  }

  digitalWrite(LED_GREEN, LOW);

  // CORS
  server.sendHeader("Access-Control-Allow-Origin", "*");

  if (uid != "") {
    // Verifica se é autorizada (mas não registra!)
    bool authorized = checkTagAuthorization(uid);

    // ✅ Apenas envia resposta — quem decide registrar é o front
    String response = "{\"uid\": \"" + uid + "\", \"authorized\": " + (authorized ? "true" : "false") + "}";
    server.send(200, "application/json", response);

    digitalWrite(LED_GREEN, HIGH);
    delay(300);
    digitalWrite(LED_GREEN, LOW);
  } else {
    Serial.println("Nenhuma tag detectada após 2 segundos.");
    server.send(404, "application/json", "{\"mensagem\": \"Nenhuma tag detectada. Aproxime uma tag do leitor.\"}");

    digitalWrite(LED_RED, HIGH);
    delay(300);
    digitalWrite(LED_RED, LOW);
  }

  // Reinicializa o leitor para evitar travamentos
  rfid.PICC_HaltA();
  digitalWrite(RST_PIN, LOW);
  delay(10);
  digitalWrite(RST_PIN, HIGH);
  delay(10);
  rfid.PCD_Init();
  Serial.println("Leitor RFID reinicializado para nova leitura.");

  // Aguarda o tempo de cooldown antes de permitir nova leitura automática
  lastRegisterTime = millis();

  // ✅ Finaliza o modo de cadastro
  modoCadastro = false;
}

void toqueSucesso() {
  tone(buzzerPin, 1100); delay(120);  // nota 1 (base)
  noTone(buzzerPin);     delay(60);

  tone(buzzerPin, 1400); delay(120);  // nota 2 (meio)
  noTone(buzzerPin);     delay(60);

  tone(buzzerPin, 1700); delay(180);  // nota 3 (brilho final)
  noTone(buzzerPin);
}

void toqueRecusado() {
  for (int i = 0; i < 2; i++) {
    tone(buzzerPin, 800);  // tom agudo suave
    delay(100);
    noTone(buzzerPin);
    delay(100);
  }
}

void toqueBoasVindas() {
  int melodia[] = {880, 988, 1046, 988, 1175, 1318};  // sequência suave
  int duracao[] = {150, 150, 200, 150, 200, 250};     // duração em ms

  for (int i = 0; i < 6; i++) {
    tone(buzzerPin, melodia[i]);
    delay(duracao[i]);
    noTone(buzzerPin);
    delay(60);  // pausa entre as notas
  }
}


