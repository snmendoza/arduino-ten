#include <Arduino.h>
#include <FastLED.h>
#include "RouteHandler.h"
#include "ColorSchemes.h"

extern CRGB leds[];
extern const int NUM_LEDS;

void parseRouteData(const uint8_t* data, size_t length, std::vector<Hold>& holds) {
    Serial.print("[RouteHandler] Parsing route data, length: ");
    Serial.println(length);
    holds.clear();
    for (size_t i = 0; i + 2 < length; i += 3) {
        uint16_t position = (data[i+1] << 8) + data[i];
        CRGB color = decodeColor(data[i+2]);
        String colorName = getColorName(color.r, color.g, color.b);
        uint8_t r = color.r, g = color.g, b = color.b;
        applyPrincipalColors(colorName, r, g, b);
        holds.push_back({position, r, g, b, colorName});
        Serial.print("[RouteHandler] Hold at position ");
        Serial.print(position);
        Serial.print(" with color ");
        Serial.print(colorName);
        Serial.print(" RGB(");
        Serial.print(r); Serial.print(","); Serial.print(g); Serial.print(","); Serial.print(b); Serial.println(")");
    }
}

void updateRouteDisplay(const std::vector<Hold>& holds) {
    Serial.print("[RouteHandler] Updating route display, holds count: ");
    Serial.println(holds.size());
    for (const Hold& hold : holds) {
        if (hold.position < NUM_LEDS) {
            leds[hold.position] = CRGB(hold.r, hold.g, hold.b);
        }
    }
    FastLED.show();
}