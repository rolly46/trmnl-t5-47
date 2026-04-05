#include "wifi_manager.h"
#include "portal_html.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// Forward declarations for portal handlers
static WebServer *_server = nullptr;
static bool _portalConfigSaved = false;
static String _savedSSID, _savedPass, _savedApiBase;
static String _cachedScanJson;

String WifiManager::getMacAddress() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

WifiResult WifiManager::connect(const String &ssid, const String &pass) {
    Serial.printf("[TRMNL] Connecting to WiFi: %s\n", ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    for (int attempt = 0; attempt < WIFI_RETRY_COUNT; attempt++) {
        unsigned long start = millis();
        while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[TRMNL] WiFi connected, IP: %s, RSSI: %d\n",
                              WiFi.localIP().toString().c_str(), WiFi.RSSI());
                return WifiResult::CONNECTED;
            }
            if (WiFi.status() == WL_CONNECT_FAILED) {
                break;
            }
            if (WiFi.status() == WL_NO_SSID_AVAIL) {
                Serial.println("[TRMNL] WiFi SSID not found");
                return WifiResult::NO_AP_FOUND;
            }
            delay(100);
        }

        wl_status_t status = WiFi.status();
        if (status == WL_CONNECT_FAILED) {
            Serial.println("[TRMNL] WiFi wrong password or connection refused");
            return WifiResult::WRONG_PASSWORD;
        }

        if (attempt < WIFI_RETRY_COUNT - 1) {
            Serial.printf("[TRMNL] WiFi attempt %d failed (status %d), retrying...\n",
                          attempt + 1, status);
            WiFi.disconnect();
            delay(WIFI_RETRY_DELAY_MS);
            WiFi.begin(ssid.c_str(), pass.c_str());
        }
    }

    Serial.println("[TRMNL] WiFi connection timed out");
    return WifiResult::TIMEOUT;
}

String WifiManager::scanNetworksJson() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"";
        // Escape any quotes in SSID
        String ssid = WiFi.SSID(i);
        ssid.replace("\"", "\\\"");
        json += ssid;
        json += "\",\"rssi\":";
        json += String(WiFi.RSSI(i));
        json += "}";
    }
    json += "]";
    WiFi.scanDelete();
    return json;
}

bool WifiManager::startCaptivePortal(const char *macAddress) {
    _portalConfigSaved = false;
    _savedSSID = "";
    _savedPass = "";
    _savedApiBase = DEFAULT_API_BASE;

    // Scan networks BEFORE entering AP mode (scanning in AP mode is unreliable)
    Serial.println("[TRMNL] Scanning WiFi networks...");
    WiFi.mode(WIFI_STA);
    _cachedScanJson = scanNetworksJson();
    Serial.printf("[TRMNL] Found networks: %s\n", _cachedScanJson.c_str());
    WiFi.disconnect();
    delay(100);

    // Build AP name from MAC
    String mac = String(macAddress);
    String suffix = mac.substring(mac.length() - 8);
    suffix.replace(":", "");
    String apName = String(DEVICE_NAME_PREFIX) + "-" + suffix;

    Serial.printf("[TRMNL] Starting captive portal AP: %s\n", apName.c_str());

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str());
    delay(500);  // Give AP time to start

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    DNSServer dnsServer;
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);

    WebServer server(80);
    _server = &server;

    // Serve the portal page
    server.on("/", HTTP_GET, []() {
        _server->send_P(200, "text/html", PORTAL_HTML);
    });

    // Scan endpoint for AJAX — returns cached scan results
    server.on("/scan", HTTP_GET, []() {
        _server->send(200, "application/json", _cachedScanJson);
    });

    // Save endpoint
    server.on("/save", HTTP_POST, []() {
        String ssid = _server->arg("wifi_ssid");
        String pass = _server->arg("wifi_pass");
        String api = _server->arg("api_base");

        if (ssid.length() == 0) {
            _server->send(400, "text/html", "<h2>Error: WiFi network name is required</h2>");
            return;
        }

        _savedSSID = ssid;
        _savedPass = pass;
        _savedApiBase = api.length() > 0 ? api : DEFAULT_API_BASE;
        _portalConfigSaved = true;

        _server->send(200, "text/html",
            "<!DOCTYPE html><html><body style='font-family:sans-serif;text-align:center;padding:60px'>"
            "<h1>Saved!</h1><p>Device will restart now...</p></body></html>");
    });

    // Captive portal detection — serve HTML directly (redirects often fail on mobile)
    server.on("/generate_204", HTTP_GET, []() {
        _server->send_P(200, "text/html", PORTAL_HTML);
    });
    server.on("/hotspot-detect.html", HTTP_GET, []() {
        _server->send_P(200, "text/html", PORTAL_HTML);
    });
    server.on("/connecttest.txt", HTTP_GET, []() {
        _server->send_P(200, "text/html", PORTAL_HTML);
    });
    server.on("/fwlink", HTTP_GET, []() {
        _server->send_P(200, "text/html", PORTAL_HTML);
    });
    server.on("/canonical.html", HTTP_GET, []() {
        _server->send_P(200, "text/html", PORTAL_HTML);
    });
    server.on("/success.txt", HTTP_GET, []() {
        _server->send(200, "text/plain", "");
    });

    // Catch-all — serve portal HTML for any unknown path
    server.onNotFound([]() {
        _server->send_P(200, "text/html", PORTAL_HTML);
    });

    server.begin();
    Serial.println("[TRMNL] Captive portal server started");

    unsigned long portalStart = millis();
    while (!_portalConfigSaved) {
        dnsServer.processNextRequest();
        server.handleClient();
        delay(10);

        if (millis() - portalStart > PORTAL_TIMEOUT_MS) {
            Serial.println("[TRMNL] Captive portal timed out");
            server.stop();
            _server = nullptr;
            return false;
        }
    }

    Serial.printf("[TRMNL] Config saved: SSID=%s, API=%s\n",
                  _savedSSID.c_str(), _savedApiBase.c_str());

    // Give the response time to send
    unsigned long saveTime = millis();
    while (millis() - saveTime < 2000) {
        dnsServer.processNextRequest();
        server.handleClient();
        delay(10);
    }

    server.stop();
    _server = nullptr;
    return true;
}

// Accessors for saved portal config — used by main.cpp after portal returns
String portalGetSSID() { return _savedSSID; }
String portalGetPass() { return _savedPass; }
String portalGetApiBase() { return _savedApiBase; }
