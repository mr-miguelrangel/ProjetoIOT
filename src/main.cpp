#include <Arduino.h>
#include <WiFi.h>

#if __has_include(<config.h>)
    #include <config.h>
#elif __has_include("config.h")
    #include "config.h"
#else
    #include "config.h.example"
#endif

#include "domain/TelemetryPayload.h"
#include "domain/Command.h"
#include "infrastructure/HiveMqAdapter.h"

// Instância do Adaptador MQTT
Infrastructure::HiveMqAdapter mqttAdapter(
    MQTT_BROKER_HOST,
    MQTT_BROKER_PORT,
    MQTT_CLIENT_ID
);

unsigned long lastTelemetryMillis = 0;
const unsigned long TELEMETRY_INTERVAL = 10000; // a cada 10 segundos

void setupWifi() {
    Serial.println();
    Serial.printf("[Wi-Fi] Conectando a %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Conexão inicial com timeout de verificação não-travante longo
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[Wi-Fi] Conectado!");
        Serial.printf("[Wi-Fi] IP: %s | MAC: %s\n", WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
    } else {
        Serial.println("\n[Wi-Fi] Conexão em andamento em segundo plano.");
    }
}

void onCommandReceived(const String& topic, const String& payload) {
    Serial.printf("[Comando Recebido] Tópico: %s\nPayload: %s\n", topic.c_str(), payload.c_str());

    Domain::Command cmd = Domain::Command::fromJson(payload);
    if (!cmd.isValid) {
        Serial.println("[Comando] Payload JSON inválido ou campos ausentes.");
        return;
    }

    Serial.printf("[Comando Válido] Destino: %s | Ação: %s | Parâmetros: %s\n", 
                  cmd.deviceId.c_str(), cmd.action.c_str(), cmd.params.c_str());

    // Exemplo de atuação local ou roteamento futuro para LoRa
    if (cmd.action == "ping") {
        Serial.println("[Ação Executada] Pong!");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- [ProjetoIOT] Inicializando Master Gateway ESP32 ---");

    setupWifi();

    // Iniciar Adapter MQTT
    mqttAdapter.begin();

    // Inscrever no tópico de comandos deste dispositivo
    String commandTopic = String(MQTT_TOPIC_PREFIX) + "/" + WiFi.macAddress() + "/commands";
    mqttAdapter.subscribe(commandTopic, onCommandReceived);
    Serial.printf("[MQTT] Inscrito para escuta em: %s\n", commandTopic.c_str());
}

void loop() {
    // Processamento do ciclo do Adaptador MQTT (reconexão não-bloqueante + eventos)
    mqttAdapter.loop();

    // Envio periódico de telemetria simulando leitura local do Master ou recebida de Worker
    unsigned long now = millis();
    if (now - lastTelemetryMillis >= TELEMETRY_INTERVAL) {
        lastTelemetryMillis = now;

        if (mqttAdapter.isConnected()) {
            Domain::TelemetryData data;
            data.sensor = "temperatura";
            data.value = 25.0f + (float)(random(-20, 20)) / 10.0f; // Variação simulada
            data.unit = "C";

            Domain::TelemetryPayload payload(WiFi.macAddress(), now / 1000, data);
            payload.setMetadata(WiFi.RSSI(), 100);

            String topic = String(MQTT_TOPIC_PREFIX) + "/" + WiFi.macAddress() + "/telemetry";
            String jsonStr = payload.toJson();

            Serial.printf("[Telemetria] Publicando em %s: %s\n", topic.c_str(), jsonStr.c_str());
            mqttAdapter.publish(topic, jsonStr, 1);
        }
    }
}

