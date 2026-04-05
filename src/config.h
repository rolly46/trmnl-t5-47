#pragma once

#define FIRMWARE_VERSION    "1.0.0"
#define DEVICE_NAME_PREFIX  "TRMNL"
#define DEVICE_MODEL        "lilygo_t5_47"

// NVS
#define NVS_NAMESPACE       "trmnl"

// WiFi
#define WIFI_CONNECT_TIMEOUT_MS  20000
#define WIFI_RETRY_COUNT         3
#define WIFI_RETRY_DELAY_MS      2000

// API
#define DEFAULT_API_BASE    "https://usetrmnl.com"
#define DEFAULT_REFRESH_RATE 1800
#define SETUP_REFRESH_RATE   60
#define ERROR_REFRESH_RATE   300

// Display
#define DISPLAY_WIDTH       960
#define DISPLAY_HEIGHT      540

// Battery
#define BATTERY_ADC_PIN     14
#define BATTERY_LOW_VOLTAGE 3.3f

// Button
#define BUTTON_PIN          39
#define LONG_PRESS_MS       3000

// Captive Portal
#define PORTAL_TIMEOUT_MS   600000  // 10 minutes
