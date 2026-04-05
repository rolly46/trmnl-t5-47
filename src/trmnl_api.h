#pragma once

#include <Arduino.h>

struct SetupResult {
    bool success;
    String apiKey;
    String friendlyId;
    String imageUrl;
    int httpCode;
    String message;
};

struct DisplayResult {
    bool success;
    String imageUrl;
    String filename;
    uint32_t refreshRate;
    bool resetFirmware;
    bool updateFirmware;
    int httpCode;
    String message;
};

class TrmnlApi {
public:
    TrmnlApi(const String &apiBase, const String &macAddress, const String &apiKey);

    SetupResult setup();
    DisplayResult fetchDisplay(uint32_t currentRefreshRate, float batteryVoltage, int rssi);

private:
    String _apiBase;
    String _mac;
    String _apiKey;
};
