#ifndef DOMAIN_TELEMETRY_PAYLOAD_H
#define DOMAIN_TELEMETRY_PAYLOAD_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Domain {

struct TelemetryData {
    String sensor;
    float value;
    String unit;
};

struct TelemetryMetadata {
    int rssi;
    int batteryLevel; // Porcentagem ou mV
};

class TelemetryPayload {
public:
    String deviceId;
    unsigned long timestamp;
    TelemetryData data;
    TelemetryMetadata metadata;
    bool hasMetadata;

    TelemetryPayload(const String& id, unsigned long ts, const TelemetryData& d)
        : deviceId(id), timestamp(ts), data(d), hasMetadata(false) {}

    void setMetadata(int rssi, int batteryLevel) {
        metadata.rssi = rssi;
        metadata.batteryLevel = batteryLevel;
        hasMetadata = true;
    }

    String toJson() const {
        JsonDocument doc;
        doc["deviceId"] = deviceId;
        doc["timestamp"] = timestamp;

        JsonObject dataObj = doc["data"].to<JsonObject>();
        dataObj["sensor"] = data.sensor;
        dataObj["value"] = data.value;
        dataObj["unit"] = data.unit;

        if (hasMetadata) {
            JsonObject metaObj = doc["metadata"].to<JsonObject>();
            metaObj["rssi"] = metadata.rssi;
            metaObj["battery"] = metadata.batteryLevel;
        }

        String output;
        serializeJson(doc, output);
        return output;
    }
};

} // namespace Domain

#endif // DOMAIN_TELEMETRY_PAYLOAD_H

