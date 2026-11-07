#include "infrastructure/HiveMqAdapter.h"

namespace Infrastructure {

HiveMqAdapter* HiveMqAdapter::_instance = nullptr;

HiveMqAdapter::HiveMqAdapter(const String& host, uint16_t port, const String& clientId, 
                             const String& user, const String& pass)
    : _brokerHost(host),
      _brokerPort(port),
      _clientId(clientId),
      _user(user),
      _pass(pass),
      _lastReconnectAttempt(0),
      _reconnectInterval(5000) {
    _mqttClient.setClient(_wifiClient);
    _instance = this;
}

void HiveMqAdapter::staticMqttCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->handleMqttMessage(topic, payload, length);
    }
}

void HiveMqAdapter::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String payloadStr;
    payloadStr.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }

    // Procura por correspondência exata ou prefixo
    for (const auto& pair : _subscriptions) {
        if (pair.first == topicStr || pair.first == "#") {
            pair.second(topicStr, payloadStr);
        }
    }
}

void HiveMqAdapter::begin() {
    _mqttClient.setServer(_brokerHost.c_str(), _brokerPort);
    _mqttClient.setCallback(HiveMqAdapter::staticMqttCallback);
}

bool HiveMqAdapter::connect() {
    if (_mqttClient.connected()) {
        return true;
    }

    bool success = false;
    if (_user.length() > 0) {
        success = _mqttClient.connect(_clientId.c_str(), _user.c_str(), _pass.c_str());
    } else {
        success = _mqttClient.connect(_clientId.c_str());
    }

    if (success) {
        Serial.println("[MQTT] Conectado com sucesso ao Broker HiveMQ!");
        // Reinscrever tópicos existentes
        for (const auto& pair : _subscriptions) {
            _mqttClient.subscribe(pair.first.c_str());
        }
    } else {
        Serial.printf("[MQTT] Falha na conexão. Código rc=%d\n", _mqttClient.state());
    }

    return success;
}

void HiveMqAdapter::disconnect() {
    _mqttClient.disconnect();
}

bool HiveMqAdapter::isConnected() {
    return _mqttClient.connected();
}

void HiveMqAdapter::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        return; // Aguarda estabilidade do Wi-Fi
    }

    if (!_mqttClient.connected()) {
        unsigned long now = millis();
        // Tentativa de reconexão não-bloqueante
        if (now - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = now;
            Serial.println("[MQTT] Tentando reconectar ao Broker...");
            connect();
        }
    } else {
        _mqttClient.loop();
    }
}

bool HiveMqAdapter::publish(const String& topic, const String& payload, int qos, bool retain) {
    if (!_mqttClient.connected()) {
        Serial.println("[MQTT] Não é possível publicar: desconectado.");
        return false;
    }

    // PubSubClient suporta publish básico; QoS 1 é gerenciado pelo broker/stack
    return _mqttClient.publish(topic.c_str(), payload.c_str(), retain);
}

bool HiveMqAdapter::subscribe(const String& topic, Domain::MessageCallback callback) {
    _subscriptions[topic] = callback;

    if (_mqttClient.connected()) {
        return _mqttClient.subscribe(topic.c_str());
    }
    return true; // Ficará registrado e se inscreverá assim que conectar
}

} // namespace Infrastructure

