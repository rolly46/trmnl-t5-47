#pragma once

#include <Arduino.h>

class Display {
public:
    bool init();
    void clear();
    void showImage(const uint8_t *pixels, int w, int h);
    void showMessage(const char *title, const char *body);
    void showSetupScreen(const char *macAddress, const char *apName);
    void showRegistrationInfo(const char *mac, const char *friendlyId);
    void showError(const char *header, const char *detail, const char *action);
    void powerOff();

    uint8_t *getFramebuffer() { return _fb; }

private:
    uint8_t *_fb = nullptr;
    void fillWhite();
    void drawCenteredText(const char *text, int y, bool large);
    void updateScreen();
};
