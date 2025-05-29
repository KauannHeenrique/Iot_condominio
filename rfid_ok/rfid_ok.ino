#include <WiFi.h>
#include <WebServer.h>
#include <MFRC522.h>
#include <SPI.h>

const char* ssid = "iPhoneKauann";
const char* password = "Kauann123";

// Pinos do MFRC522 no ESP32-S3
#define RST_PIN 9
#define SS_PIN 10
#define SCK_PIN 18
#define MOSI_PIN 11
#define MISO_PIN 13
#define LED_RED 4
#define LED_GREEN 2

int led_controller;

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
  Serial.println("Leitor RFID inicializado. Aproxime uma tag para testar...");

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

  // Suporte a preflight request (CORS)
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
}

void handleReadRFID() {
  Serial.println("Requisição recebida em /read-rfid");
  String uid = "";
  unsigned long startTime = millis();
  bool ledState = false;

  // Tenta ler a tag por 1 segundo, piscando o LED verde
  while (millis() - startTime < 1000) {
    // Pisca o LED verde (100 ms ligado, 100 ms desligado)
    ledState = !ledState;
    digitalWrite(LED_GREEN, ledState ? HIGH : LOW);
    // Só tenta ler a tag se ainda não foi lida
    if (uid == "" && rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.println("Tag detectada!");
      led_controller = 0;
      for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) {
          uid += "0";
        }
        uid += String(rfid.uid.uidByte[i], HEX);
      }
      uid.toUpperCase();
      Serial.println("Tag lida: " + uid);
    }
    delay(100); // Sincroniza com o pisca do LED
  }

  // Desliga o LED verde após o loop de leitura
  digitalWrite(LED_GREEN, LOW);

  // Envia a resposta HTTP imediatamente
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (uid != "") {
    server.send(200, "application/json", "{\"uid\": \"" + uid + "\"}");
    // Sucesso: LED verde acende por 1500 ms
    digitalWrite(LED_GREEN, HIGH);
    delay(1500);
    digitalWrite(LED_GREEN, LOW);
  } else {
    led_controller = 1;
    Serial.println("Nenhuma tag detectada após 1 segundo.");
    server.send(404, "application/json", "{\"mensagem\": \"Nenhuma tag detectada. Aproxime uma tag do leitor.\"}");
    // Falha: LED vermelho acende por 1500 ms
    digitalWrite(LED_RED, HIGH);
    delay(1500);
    digitalWrite(LED_RED, LOW);
  }

  // Finaliza a leitura e reinicializa o leitor (sem LEDs)
  rfid.PICC_HaltA();
  digitalWrite(RST_PIN, LOW);
  delay(20);
  digitalWrite(RST_PIN, HIGH);
  delay(20);
  rfid.PCD_Init();
  Serial.println("Leitor RFID reinicializado para nova leitura.");
}