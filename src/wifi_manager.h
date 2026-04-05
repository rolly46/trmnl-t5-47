#pragma once

#include <Arduino.h>
#include "config.h"

enum class WifiResult {
    CONNECTED,
    TIMEOUT,
    WRONG_PASSWORD,
    NO_AP_FOUND
};

class WifiManager {
public:
    WifiResult connect(const String &ssid, const String &pass);
    bool startCaptivePortal(const char *macAddress);
    String scanNetworksJson();
    String getMacAddress();
};
