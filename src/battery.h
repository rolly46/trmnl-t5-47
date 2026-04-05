#pragma once

#include <Arduino.h>
#include "config.h"

class Battery {
public:
    float readVoltage();
    bool isLow();
};
