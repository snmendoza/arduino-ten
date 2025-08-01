#ifndef COLOR_SCHEMES_H
#define COLOR_SCHEMES_H

#include <FastLED.h>
#include <vector>
#include <Arduino.h>

struct Hold {
    uint16_t position;
    uint8_t r, g, b;
    String colorName;
};

// Decodes compressed RGB color from API level 3 format
CRGB decodeColor(uint8_t colorEncoded);
String getColorName(uint8_t r, uint8_t g, uint8_t b);
void applyPrincipalColors(String colorName, uint8_t& r, uint8_t& g, uint8_t& b);
void applyAltColors(String colorName, uint8_t& r, uint8_t& g, uint8_t& b);

#endif // COLOR_SCHEMES_H