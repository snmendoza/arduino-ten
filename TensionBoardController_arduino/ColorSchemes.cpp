#include <Arduino.h>
#include <FastLED.h>
#include "ColorSchemes.h"

CRGB decodeColor(uint8_t colorEncoded) {
    uint8_t r = ((colorEncoded >> 5) & 0x07) * 255 / 7;
    uint8_t g = ((colorEncoded >> 2) & 0x07) * 255 / 7;
    uint8_t b = (colorEncoded & 0x03) * 255 / 3;
    Serial.print("[ColorSchemes] Decoded color: ");
    Serial.print(r); Serial.print(","); Serial.print(g); Serial.print(","); Serial.println(b);
    return CRGB(r, g, b);
}

String getColorName(uint8_t r, uint8_t g, uint8_t b) {
    if (r > 200 && g < 50 && b < 50) return "Red";
    if (r < 50 && g > 200 && b < 50) return "Green";
    if (r < 50 && g < 50 && b > 200) return "Blue";
    if (r > 200 && g < 50 && b > 200) return "Pink";
    if (r > 200 && g > 200 && b < 50) return "Yellow";
    if (r > 200 && g > 200 && b > 200) return "White";
    if (r < 50 && g < 50 && b < 50) return "Black";
    return "Unknown";
}

void applyPrincipalColors(String colorName, uint8_t& r, uint8_t& g, uint8_t& b) {
    colorName.toLowerCase();
    if (colorName == "green") { r = 0; g = 255; b = 30; }
    else if (colorName == "blue") { r = 50; g = 50; b = 255; }
    else if (colorName == "purple" || colorName == "pink") { r = 150; g = 0; b = 255; }
    else if (colorName == "red") { r = 255; g = 0; b = 0; }
    else if (colorName == "yellow") { r = 255; g = 255; b = 100; }
    else if (colorName == "white") { r = 255; g = 255; b = 255; }
    // If unknown color, keep original values
    Serial.print("[ColorSchemes] Principal color applied: ");
    Serial.println(colorName);
}

void applyAltColors(String colorName, uint8_t& r, uint8_t& g, uint8_t& b) {
    colorName.toLowerCase();
    if (colorName == "green") { r = 100; g = 255; b = 0; }
    else if (colorName == "blue") { r = 0; g = 200; b = 255; }
    else if (colorName == "purple" || colorName == "pink") { r = 255; g = 0; b = 100; }
    else if (colorName == "red") { r = 255; g = 100; b = 0; }
    else if (colorName == "yellow") { r = 200; g = 255; b = 0; }
    else if (colorName == "white") { r = 255; g = 200; b = 200; }
    // If unknown color, keep original values
    Serial.print("[ColorSchemes] Alt color applied: ");
    Serial.println(colorName);
}