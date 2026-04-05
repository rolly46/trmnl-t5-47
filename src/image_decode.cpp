#include "image_decode.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PNGdec.h>

// PNG decode callback context
struct PngContext {
    uint8_t *framebuffer;
    int fbWidth;
    int fbHeight;
    int offsetX;
    int offsetY;
};

// Set a single pixel in the 4bpp framebuffer
// 0x0 = black, 0xF = white (epdiy convention)
void ImageDecoder::setPixel4bpp(uint8_t *fb, int fbWidth, int x, int y, uint8_t gray4) {
    if (x < 0 || x >= fbWidth || y < 0 || y >= DISPLAY_HEIGHT) return;
    int idx = (y * fbWidth + x) / 2;
    if (x & 1) {
        fb[idx] = (fb[idx] & 0xF0) | (gray4 & 0x0F);
    } else {
        fb[idx] = (fb[idx] & 0x0F) | ((gray4 & 0x0F) << 4);
    }
}

// PNGdec draw callback
static int pngDrawCallback(PNGDRAW *pDraw) {
    PngContext *ctx = (PngContext *)pDraw->pUser;
    int destY = ctx->offsetY + pDraw->y;
    if (destY < 0 || destY >= ctx->fbHeight) return 0;

    uint8_t *line = pDraw->pPixels;
    int srcWidth = pDraw->iWidth;

    for (int i = 0; i < srcWidth; i++) {
        int destX = ctx->offsetX + i;
        if (destX < 0 || destX >= ctx->fbWidth) continue;

        uint8_t gray8;
        switch (pDraw->iBpp) {
            case 1: { // 1-bit
                uint8_t bit = (line[i / 8] >> (7 - (i & 7))) & 1;
                gray8 = bit ? 255 : 0;
                break;
            }
            case 4: { // 4-bit grayscale
                if (i & 1) gray8 = (line[i / 2] & 0x0F) * 17;
                else gray8 = ((line[i / 2] >> 4) & 0x0F) * 17;
                break;
            }
            case 8: // 8-bit grayscale or palette index
                gray8 = line[i];
                break;
            case 24: { // RGB
                int off = i * 3;
                gray8 = (uint8_t)(0.299f * line[off] + 0.587f * line[off + 1] + 0.114f * line[off + 2]);
                break;
            }
            case 32: { // RGBA
                int off = i * 4;
                gray8 = (uint8_t)(0.299f * line[off] + 0.587f * line[off + 1] + 0.114f * line[off + 2]);
                break;
            }
            default:
                gray8 = 128;
                break;
        }

        // Map 0-255 to 4-bit: 0x0=black, 0xF=white
        uint8_t gray4 = gray8 >> 4;

        int idx = (destY * ctx->fbWidth + destX) / 2;
        if (destX & 1) {
            ctx->framebuffer[idx] = (ctx->framebuffer[idx] & 0xF0) | (gray4 & 0x0F);
        } else {
            ctx->framebuffer[idx] = (ctx->framebuffer[idx] & 0x0F) | ((gray4 & 0x0F) << 4);
        }
    }
    return 1;
}

DecodeResult ImageDecoder::downloadAndDecode(const String &url, uint8_t *framebuffer, int fbWidth, int fbHeight) {
    Serial.printf("[TRMNL] Downloading image: %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(30000);
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
        Serial.println("[TRMNL] HTTP begin failed for image download");
        return DecodeResult::DOWNLOAD_FAILED;
    }

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[TRMNL] Image download HTTP %d\n", httpCode);
        http.end();
        return DecodeResult::DOWNLOAD_FAILED;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        contentLength = 1024 * 1024; // Assume max 1MB if unknown
    }

    Serial.printf("[TRMNL] Image size: %d bytes\n", contentLength);

    uint8_t *imgBuf = (uint8_t *)heap_caps_malloc(contentLength, MALLOC_CAP_SPIRAM);
    if (!imgBuf) {
        Serial.println("[TRMNL] Failed to allocate image buffer in PSRAM");
        http.end();
        return DecodeResult::OUT_OF_MEMORY;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t totalRead = 0;
    unsigned long lastData = millis();

    while (totalRead < (size_t)contentLength && (millis() - lastData < 30000)) {
        if (stream->available()) {
            size_t toRead = stream->available();
            if (toRead > (size_t)contentLength - totalRead) {
                toRead = (size_t)contentLength - totalRead;
            }
            size_t bytesRead = stream->readBytes(imgBuf + totalRead, toRead);
            totalRead += bytesRead;
            lastData = millis();
        } else {
            delay(10);
        }
    }

    http.end();

    if (totalRead < 4) {
        Serial.printf("[TRMNL] Image too small: %d bytes\n", totalRead);
        heap_caps_free(imgBuf);
        return DecodeResult::DOWNLOAD_FAILED;
    }

    Serial.printf("[TRMNL] Downloaded %d bytes\n", totalRead);

    // Detect format by magic bytes
    DecodeResult result;
    if (imgBuf[0] == 'B' && imgBuf[1] == 'M') {
        Serial.println("[TRMNL] Detected BMP format");
        result = decodeBMP(imgBuf, totalRead, framebuffer, fbWidth, fbHeight);
    } else if (imgBuf[0] == 0x89 && imgBuf[1] == 0x50 && imgBuf[2] == 0x4E && imgBuf[3] == 0x47) {
        Serial.println("[TRMNL] Detected PNG format");
        result = decodePNG(imgBuf, totalRead, framebuffer, fbWidth, fbHeight);
    } else {
        Serial.printf("[TRMNL] Unknown format: %02X %02X %02X %02X\n",
                      imgBuf[0], imgBuf[1], imgBuf[2], imgBuf[3]);
        result = DecodeResult::UNSUPPORTED_FORMAT;
    }

    heap_caps_free(imgBuf);
    return result;
}

DecodeResult ImageDecoder::decodeBMP(uint8_t *data, size_t length, uint8_t *framebuffer, int fbWidth, int fbHeight) {
    if (length < 54) return DecodeResult::DECODE_FAILED;

    // BMP header
    uint32_t dataOffset = *(uint32_t *)(data + 10);
    int32_t imgWidth = *(int32_t *)(data + 18);
    int32_t imgHeight = *(int32_t *)(data + 22);
    uint16_t bitsPerPixel = *(uint16_t *)(data + 28);
    uint32_t compression = *(uint32_t *)(data + 30);

    Serial.printf("[TRMNL] BMP: %dx%d, %d bpp, compression=%d\n",
                  imgWidth, imgHeight, bitsPerPixel, compression);

    if (compression != 0) {
        Serial.println("[TRMNL] Compressed BMP not supported");
        return DecodeResult::DECODE_FAILED;
    }

    bool bottomUp = (imgHeight > 0);
    if (imgHeight < 0) imgHeight = -imgHeight;

    int offsetX = (fbWidth - imgWidth) / 2;
    int offsetY = (fbHeight - imgHeight) / 2;
    if (offsetX < 0) offsetX = 0;
    if (offsetY < 0) offsetY = 0;

    // Row stride is padded to 4 bytes
    int rowStride;
    switch (bitsPerPixel) {
        case 1:
            rowStride = ((imgWidth + 31) / 32) * 4;
            break;
        case 8:
            rowStride = ((imgWidth + 3) / 4) * 4;
            break;
        case 24:
            rowStride = ((imgWidth * 3 + 3) / 4) * 4;
            break;
        default:
            Serial.printf("[TRMNL] Unsupported BMP bpp: %d\n", bitsPerPixel);
            return DecodeResult::UNSUPPORTED_FORMAT;
    }

    // Optional: read palette for 1-bit and 8-bit BMPs
    uint8_t palette[256]; // Grayscale values
    if (bitsPerPixel <= 8) {
        int paletteOffset = 14 + *(uint32_t *)(data + 14); // After DIB header
        int numColors = (bitsPerPixel == 1) ? 2 : 256;
        // BMP palette is BGRA, 4 bytes per entry
        for (int i = 0; i < numColors && (paletteOffset + i * 4 + 2) < (int)length; i++) {
            uint8_t b = data[paletteOffset + i * 4];
            uint8_t g = data[paletteOffset + i * 4 + 1];
            uint8_t r = data[paletteOffset + i * 4 + 2];
            palette[i] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
        }
    }

    int renderH = (imgHeight < fbHeight) ? imgHeight : fbHeight;
    int renderW = (imgWidth < fbWidth) ? imgWidth : fbWidth;

    for (int row = 0; row < renderH; row++) {
        int srcRow = bottomUp ? (imgHeight - 1 - row) : row;
        uint8_t *rowPtr = data + dataOffset + srcRow * rowStride;

        if ((size_t)(rowPtr - data + rowStride) > length) break;

        int destY = offsetY + row;
        if (destY >= fbHeight) break;

        for (int col = 0; col < renderW; col++) {
            int destX = offsetX + col;
            if (destX >= fbWidth) break;

            uint8_t gray8;
            switch (bitsPerPixel) {
                case 1: {
                    int byteIdx = col / 8;
                    int bitIdx = 7 - (col & 7);
                    uint8_t bit = (rowPtr[byteIdx] >> bitIdx) & 1;
                    gray8 = palette[bit];
                    break;
                }
                case 8:
                    gray8 = palette[rowPtr[col]];
                    break;
                case 24: {
                    int off = col * 3;
                    uint8_t b = rowPtr[off];
                    uint8_t g = rowPtr[off + 1];
                    uint8_t r = rowPtr[off + 2];
                    gray8 = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
                    break;
                }
                default:
                    gray8 = 128;
                    break;
            }

            uint8_t gray4 = gray8 >> 4;
            setPixel4bpp(framebuffer, fbWidth, destX, destY, gray4);
        }
    }

    Serial.println("[TRMNL] BMP decode complete");
    return DecodeResult::SUCCESS;
}

DecodeResult ImageDecoder::decodePNG(uint8_t *data, size_t length, uint8_t *framebuffer, int fbWidth, int fbHeight) {
    PNG png;

    int rc = png.openRAM(data, length, pngDrawCallback);
    if (rc != PNG_SUCCESS) {
        Serial.printf("[TRMNL] PNG open failed: %d\n", rc);
        return DecodeResult::DECODE_FAILED;
    }

    int imgWidth = png.getWidth();
    int imgHeight = png.getHeight();
    Serial.printf("[TRMNL] PNG: %dx%d, bpp=%d\n", imgWidth, imgHeight, png.getBpp());

    PngContext ctx;
    ctx.framebuffer = framebuffer;
    ctx.fbWidth = fbWidth;
    ctx.fbHeight = fbHeight;
    ctx.offsetX = (fbWidth - imgWidth) / 2;
    ctx.offsetY = (fbHeight - imgHeight) / 2;
    if (ctx.offsetX < 0) ctx.offsetX = 0;
    if (ctx.offsetY < 0) ctx.offsetY = 0;

    rc = png.decode(&ctx, 0);
    png.close();

    if (rc != PNG_SUCCESS) {
        Serial.printf("[TRMNL] PNG decode failed: %d\n", rc);
        return DecodeResult::DECODE_FAILED;
    }

    Serial.println("[TRMNL] PNG decode complete");
    return DecodeResult::SUCCESS;
}
