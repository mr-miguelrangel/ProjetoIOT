#ifndef INFRASTRUCTURE_HIVE_MQ_ADAPTER_H
#define INFRASTRUCTURE_HIVE_MQ_ADAPTER_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <map>
#include "domain/IMqttAdapter.h"

namespace Infrastructure {

class HiveMqAdapter : public Domain::IMqttAdapter {
private:
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;

    String _brokerHost;
    uint16_t _brokerPort;
    String _clientId;
    String _user;
    String _pass;

    unsigned long _lastReconnectAttempt;
    unsigned long _reconnectInterval;

    std::map<String, Domain::MessageCallback> _subscriptions;

    static HiveMqAdapter* _instance;
    static void staticMqttCallback(char* topic, byte* payload, unsigned int length);
    void handleMqttMessage(char* topic, byte* payload, unsigned int length);

public:
    HiveMqAdapter(const String& host, uint16_t port, const String& clientId, 
                  const String& user = "", const String& pass = "");

    void begin() override;
    bool connect() override;
    void disconnect() override;
    bool isConnected() override;
    void loop() override;

    bool publish(const String& topic, const String& payload, int qos = 0, bool retain = false) override;
    bool subscribe(const String& topic, Domain::MessageCallback callback) override;
};

} // namespace Infrastructure

#endif // INFRASTRUCTURE_HIVE_MQ_ADAPTER_H
