#ifndef DOMAIN_COMMAND_H
#define DOMAIN_COMMAND_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Domain {

class Command {
public:
    String deviceId;
    String action;
    String params;
    bool isValid;

    Command() : isValid(false) {}

    static Command fromJson(const String& jsonString) {
        Command cmd;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonString);

        if (error) {
            cmd.isValid = false;
            return cmd;
        }

        cmd.deviceId = doc["deviceId"] | "";
        cmd.action = doc["action"] | "";
        
        if (doc["params"].is<JsonObject>() || doc["params"].is<JsonArray>()) {
            String p;
            serializeJson(doc["params"], p);
            cmd.params = p;
        } else if (doc["params"].is<const char*>()) {
            cmd.params = doc["params"].as<String>();
        }

        cmd.isValid = (cmd.deviceId.length() > 0 && cmd.action.length() > 0);
        return cmd;
    }
};

} // namespace Domain

#endif // DOMAIN_COMMAND_H

