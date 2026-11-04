#ifndef DOMAIN_I_MQTT_ADAPTER_H
#define DOMAIN_I_MQTT_ADAPTER_H

#include <Arduino.h>
#include <functional>

namespace Domain {

using MessageCallback = std::function<void(const String& topic, const String& payload)>;

class IMqttAdapter {
public:
    virtual ~IMqttAdapter() = default;

    virtual void begin() = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() = 0;
    virtual void loop() = 0;

    virtual bool publish(const String& topic, const String& payload, int qos = 0, bool retain = false) = 0;
    virtual bool subscribe(const String& topic, MessageCallback callback) = 0;
};

} // namespace Domain

#endif // DOMAIN_I_MQTT_ADAPTER_H

