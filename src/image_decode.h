#pragma once

#include <Arduino.h>

enum class DecodeResult {
    SUCCESS,
    DOWNLOAD_FAILED,
    DECODE_FAILED,
    OUT_OF_MEMORY,
    UNSUPPORTED_FORMAT
};

class ImageDecoder {
public:
    DecodeResult downloadAndDecode(const String &url, uint8_t *framebuffer, int fbWidth, int fbHeight);

private:
    DecodeResult decodeBMP(uint8_t *data, size_t length, uint8_t *framebuffer, int fbWidth, int fbHeight);
    DecodeResult decodePNG(uint8_t *data, size_t length, uint8_t *framebuffer, int fbWidth, int fbHeight);
    void setPixel4bpp(uint8_t *fb, int fbWidth, int x, int y, uint8_t gray4);
};
