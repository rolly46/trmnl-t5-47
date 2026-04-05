#include "display.h"
#include "config.h"
#include "epd_driver.h"
#include "firasans.h"

bool Display::init() {
    epd_init();
    _fb = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
    if (!_fb) {
        Serial.println("[TRMNL] FATAL: Failed to allocate framebuffer in PSRAM");
        return false;
    }
    memset(_fb, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    return true;
}

void Display::fillWhite() {
    if (_fb) {
        memset(_fb, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    }
}

void Display::updateScreen() {
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), _fb);
    epd_poweroff();
}

void Display::clear() {
    fillWhite();
    epd_poweron();
    epd_clear();
    epd_poweroff();
}

void Display::drawCenteredText(const char *text, int y, bool large) {
    if (!_fb || !text) return;

    const GFXfont *font = &FiraSans;
    int32_t x1 = 0, y1 = 0;
    int32_t w = 0, h = 0;
    int32_t tx = 0, ty = y;
    FontProperties props = {
        .fg_color = 0,
        .bg_color = 15,
        .fallback_glyph = 0,
        .flags = 0
    };

    get_text_bounds(font, text, &tx, &ty, &x1, &y1, &w, &h, &props);
    int32_t cx = (DISPLAY_WIDTH - w) / 2;
    int32_t cy = y;
    write_mode(font, text, &cx, &cy, _fb, BLACK_ON_WHITE, &props);
}

void Display::showImage(const uint8_t *pixels, int w, int h) {
    // If source is not the framebuffer, copy it in
    if (pixels != _fb) {
        fillWhite();

        int offsetX = (DISPLAY_WIDTH - w) / 2;
        int offsetY = (DISPLAY_HEIGHT - h) / 2;
        if (offsetX < 0) offsetX = 0;
        if (offsetY < 0) offsetY = 0;

        int srcStride = w / 2;
        int dstStride = DISPLAY_WIDTH / 2;

        int copyW = (w < DISPLAY_WIDTH) ? w : DISPLAY_WIDTH;
        int copyH = (h < DISPLAY_HEIGHT) ? h : DISPLAY_HEIGHT;
        int copyBytes = copyW / 2;

        for (int row = 0; row < copyH; row++) {
            int srcIdx = row * srcStride;
            int dstIdx = (row + offsetY) * dstStride + (offsetX / 2);
            memcpy(&_fb[dstIdx], &pixels[srcIdx], copyBytes);
        }
    }
    // else: image was decoded directly into _fb, just update screen

    updateScreen();
}

void Display::showMessage(const char *title, const char *body) {
    fillWhite();

    int y = 180;
    if (title && strlen(title) > 0) {
        drawCenteredText(title, y, true);
        y += 60;
    }

    // Draw body line by line
    if (body && strlen(body) > 0) {
        char buf[512];
        strncpy(buf, body, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *line = strtok(buf, "\n");
        while (line) {
            drawCenteredText(line, y, false);
            y += 40;
            line = strtok(nullptr, "\n");
        }
    }

    updateScreen();
}

void Display::showSetupScreen(const char *macAddress, const char *apName) {
    fillWhite();

    int y = 100;
    drawCenteredText("TRMNL Setup", y, true);
    y += 70;
    drawCenteredText("Connect to WiFi network:", y, false);
    y += 50;
    drawCenteredText(apName, y, true);
    y += 70;
    drawCenteredText("Then open your browser to:", y, false);
    y += 50;
    drawCenteredText("http://192.168.4.1", y, false);
    y += 70;
    char macLine[64];
    snprintf(macLine, sizeof(macLine), "MAC: %s", macAddress);
    drawCenteredText(macLine, y, false);

    updateScreen();
}

void Display::showRegistrationInfo(const char *mac, const char *friendlyId) {
    fillWhite();

    int y = 120;
    drawCenteredText("Device Registered!", y, true);
    y += 70;

    char macLine[64];
    snprintf(macLine, sizeof(macLine), "MAC: %s", mac);
    drawCenteredText(macLine, y, false);
    y += 50;

    char idLine[64];
    snprintf(idLine, sizeof(idLine), "ID: %s", friendlyId);
    drawCenteredText(idLine, y, false);
    y += 70;

    drawCenteredText("Add this MAC in your TRMNL dashboard", y, false);
    y += 40;
    drawCenteredText("Device Model: 960x540", y, false);

    updateScreen();
}

void Display::showError(const char *header, const char *detail, const char *action) {
    fillWhite();

    int y = 150;
    drawCenteredText(header, y, true);
    y += 60;

    if (detail && strlen(detail) > 0) {
        // Split detail by newlines
        char buf[256];
        strncpy(buf, detail, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *line = strtok(buf, "\n");
        while (line) {
            drawCenteredText(line, y, false);
            y += 40;
            line = strtok(nullptr, "\n");
        }
    }

    if (action && strlen(action) > 0) {
        y += 20;
        drawCenteredText(action, y, false);
    }

    updateScreen();
}

void Display::powerOff() {
    epd_poweroff_all();
}
