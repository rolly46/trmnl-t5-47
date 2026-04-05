#include "battery.h"

float Battery::readVoltage() {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogReadMilliVolts(BATTERY_ADC_PIN);
    }
    float avgMv = (float)sum / 16.0f;
    // Voltage divider factor of ~2
    float voltage = (avgMv / 1000.0f) * 2.0f;
    return voltage;
}

bool Battery::isLow() {
    return readVoltage() < BATTERY_LOW_VOLTAGE;
}
