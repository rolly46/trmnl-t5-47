#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
#include "config_store.h"
#include "display.h"
#include "wifi_manager.h"
#include "trmnl_api.h"
#include "image_decode.h"
#include "battery.h"

// External accessors for captive portal saved config
extern String portalGetSSID();
extern String portalGetPass();
extern String portalGetApiBase();

ConfigStore configStore;
Display display;
WifiManager wifiManager;
Battery battery;

void deepSleep(uint32_t seconds) {
    Serial.printf("[TRMNL] Deep sleep for %d seconds\n", seconds);
    display.powerOff();
    configStore.end();

    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0); // Wake on LOW
    esp_deep_sleep_start();
}

void showErrorAndSleep(const char *header, const char *detail, const char *action, uint32_t sleepSec) {
    display.showError(header, detail, action);
    deepSleep(sleepSec);
}

bool checkLongPress() {
    // GPIO39 is active LOW
    if (digitalRead(BUTTON_PIN) == LOW) {
        unsigned long start = millis();
        while (digitalRead(BUTTON_PIN) == LOW) {
            if (millis() - start >= LONG_PRESS_MS) {
                return true;
            }
            delay(50);
        }
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.printf("[TRMNL] Firmware v%s booting\n", FIRMWARE_VERSION);

    // Read wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
    Serial.printf("[TRMNL] Wake reason: %d\n", wakeReason);

    // Init button pin
    pinMode(BUTTON_PIN, INPUT);

    // Init display
    if (!display.init()) {
        Serial.println("[TRMNL] FATAL: Display init failed (PSRAM?)");
        // Can't show anything on screen — just deep sleep and hope for the best
        esp_sleep_enable_timer_wakeup(ERROR_REFRESH_RATE * 1000000ULL);
        esp_deep_sleep_start();
    }

    // Open NVS
    if (!configStore.begin()) {
        showErrorAndSleep("Storage Error", "Failed to open NVS storage", "Device will retry", ERROR_REFRESH_RATE);
    }

    // Check for long press → factory reset
    if (wakeReason == ESP_SLEEP_WAKEUP_EXT0 || wakeReason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        if (checkLongPress()) {
            Serial.println("[TRMNL] Long press detected — factory reset");
            display.showMessage("Factory Reset", "Clearing all settings...\nDevice will restart");
            configStore.clearAll();
            configStore.end();
            delay(2000);
            ESP.restart();
        }
    }

    // Check WiFi config
    if (!configStore.hasWifiConfig()) {
        Serial.println("[TRMNL] No WiFi config — starting captive portal");

        String mac = wifiManager.getMacAddress();
        String suffix = mac.substring(mac.length() - 8);
        suffix.replace(":", "");
        String apName = String(DEVICE_NAME_PREFIX) + "-" + suffix;

        display.showSetupScreen(mac.c_str(), apName.c_str());

        bool saved = wifiManager.startCaptivePortal(mac.c_str());
        if (saved) {
            // Save config from portal
            configStore.setWifiSSID(portalGetSSID());
            configStore.setWifiPass(portalGetPass());
            configStore.setApiBase(portalGetApiBase());
            configStore.end();
            Serial.println("[TRMNL] Config saved, restarting");
            delay(1000);
            ESP.restart();
        } else {
            // Portal timed out
            showErrorAndSleep("Setup Timed Out",
                "No configuration received",
                "Press reset to try again",
                ERROR_REFRESH_RATE);
        }
    }

    // WiFi config exists — connect
    String ssid = configStore.getWifiSSID();
    String pass = configStore.getWifiPass();

    WifiResult wifiResult = wifiManager.connect(ssid, pass);
    if (wifiResult != WifiResult::CONNECTED) {
        const char *reason;
        switch (wifiResult) {
            case WifiResult::TIMEOUT:       reason = "Connection timed out"; break;
            case WifiResult::WRONG_PASSWORD: reason = "Wrong password"; break;
            case WifiResult::NO_AP_FOUND:    reason = "Network not found"; break;
            default:                         reason = "Unknown error"; break;
        }
        char detail[128];
        snprintf(detail, sizeof(detail), "SSID: %s\n%s", ssid.c_str(), reason);
        showErrorAndSleep("WiFi Failed", detail,
            "Hold button 3s to reconfigure", ERROR_REFRESH_RATE);
    }

    // WiFi connected — get device info
    String mac = wifiManager.getMacAddress();
    String apiBase = configStore.getApiBase();
    String apiKey = configStore.getApiKey();
    String friendlyId = configStore.getFriendlyId();
    uint32_t refreshRate = configStore.getRefreshRate();
    float battVoltage = battery.readVoltage();
    int rssi = WiFi.RSSI();

    Serial.printf("[TRMNL] MAC: %s, API: %s, Battery: %.2fV, RSSI: %d\n",
                  mac.c_str(), apiBase.c_str(), battVoltage, rssi);

    // If no API key, run setup
    if (apiKey.length() == 0) {
        Serial.println("[TRMNL] No API key — calling /api/setup");

        TrmnlApi api(apiBase, mac, "");
        SetupResult setupResult = api.setup();

        if (setupResult.success) {
            Serial.printf("[TRMNL] Setup OK: key=%s, id=%s\n",
                          setupResult.apiKey.c_str(), setupResult.friendlyId.c_str());

            configStore.setApiKey(setupResult.apiKey);
            configStore.setFriendlyId(setupResult.friendlyId);

            // Download and display the welcome image if provided
            if (setupResult.imageUrl.length() > 0) {
                // Fill framebuffer white before decode
                memset(display.getFramebuffer(), 0xFF, DISPLAY_WIDTH * DISPLAY_HEIGHT / 2);
                ImageDecoder decoder;
                DecodeResult dr = decoder.downloadAndDecode(
                    setupResult.imageUrl, display.getFramebuffer(),
                    DISPLAY_WIDTH, DISPLAY_HEIGHT);

                if (dr == DecodeResult::SUCCESS) {
                    display.showImage(display.getFramebuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
                } else {
                    // Show registration info instead
                    display.showRegistrationInfo(mac.c_str(), setupResult.friendlyId.c_str());
                }
            } else {
                display.showRegistrationInfo(mac.c_str(), setupResult.friendlyId.c_str());
            }

            deepSleep(SETUP_REFRESH_RATE);
        } else if (setupResult.httpCode == 404) {
            char detail[192];
            snprintf(detail, sizeof(detail),
                "MAC: %s\n\nRegister at usetrmnl.com\nor check your BYOS server URL", mac.c_str());
            showErrorAndSleep("Device Not Found", detail,
                "Will retry in 5 minutes", ERROR_REFRESH_RATE);
        } else {
            char detail[192];
            snprintf(detail, sizeof(detail), "HTTP %d\n%s",
                     setupResult.httpCode, setupResult.message.c_str());
            showErrorAndSleep("Setup Failed", detail,
                "Will retry in 5 minutes", ERROR_REFRESH_RATE);
        }
    }

    // Have API key — fetch display content
    Serial.println("[TRMNL] Fetching display content");
    TrmnlApi api(apiBase, mac, apiKey);
    DisplayResult dispResult = api.fetchDisplay(refreshRate, battVoltage, rssi);

    // Handle reset_firmware flag
    if (dispResult.resetFirmware) {
        Serial.println("[TRMNL] Server requested firmware reset — clearing credentials");
        configStore.clearApiCredentials();
        configStore.end();
        delay(1000);
        ESP.restart();
    }

    // Handle update_firmware flag (log only in v1)
    if (dispResult.updateFirmware) {
        Serial.println("[TRMNL] Server requested firmware update — OTA not implemented in v1");
    }

    // Store updated refresh rate
    if (dispResult.refreshRate > 0 && dispResult.refreshRate != refreshRate) {
        configStore.setRefreshRate(dispResult.refreshRate);
        refreshRate = dispResult.refreshRate;
    }

    if (dispResult.success && dispResult.imageUrl.length() > 0) {
        // Download and display image
        memset(display.getFramebuffer(), 0xFF, DISPLAY_WIDTH * DISPLAY_HEIGHT / 2);
        ImageDecoder decoder;
        DecodeResult dr = decoder.downloadAndDecode(
            dispResult.imageUrl, display.getFramebuffer(),
            DISPLAY_WIDTH, DISPLAY_HEIGHT);

        if (dr == DecodeResult::SUCCESS) {
            display.showImage(display.getFramebuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
            deepSleep(refreshRate);
        } else {
            const char *errMsg;
            switch (dr) {
                case DecodeResult::DOWNLOAD_FAILED:   errMsg = "Download failed"; break;
                case DecodeResult::DECODE_FAILED:      errMsg = "Image decode error"; break;
                case DecodeResult::OUT_OF_MEMORY:      errMsg = "Out of memory"; break;
                case DecodeResult::UNSUPPORTED_FORMAT: errMsg = "Unsupported image format"; break;
                default:                               errMsg = "Unknown error"; break;
            }
            showErrorAndSleep("Image Error", errMsg, "Retrying in 5 minutes", ERROR_REFRESH_RATE);
        }
    } else if (!dispResult.success) {
        if (dispResult.httpCode == 401 || dispResult.httpCode == 403) {
            // Credentials already cleared above via resetFirmware flag
            showErrorAndSleep("Auth Error", "API key rejected by server",
                "Device will re-register", ERROR_REFRESH_RATE);
        } else {
            char detail[192];
            snprintf(detail, sizeof(detail), "HTTP %d\n%s",
                     dispResult.httpCode, dispResult.message.c_str());
            showErrorAndSleep("Display Error", detail,
                "Retrying in 5 minutes", ERROR_REFRESH_RATE);
        }
    } else {
        // Success but no image URL (e.g. status 202 — no user attached)
        friendlyId = configStore.getFriendlyId();
        char detail[128];
        snprintf(detail, sizeof(detail), "ID: %s\n%s",
                 friendlyId.c_str(), dispResult.message.c_str());
        display.showMessage("Waiting for Setup", detail);
        deepSleep(refreshRate);
    }
}

void loop() {
    // Never reached — device always deep sleeps after setup()
}
