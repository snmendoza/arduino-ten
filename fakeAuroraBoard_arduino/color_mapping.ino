// color_mapping.ino — Color decoding, classification, and route color schemes

// Decode RGB color from Aurora API level 3 compressed format
void decodeColor(uint8_t colorByte, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = ((colorByte >> 5) & 0x07) * 255 / 7;  // 3 bits for red
    g = ((colorByte >> 2) & 0x07) * 255 / 7;  // 3 bits for green
    b = (colorByte & 0x03) * 255 / 3;         // 2 bits for blue
}

// Classify decoded RGB into a hold color category
HoldColor classifyColor(uint8_t r, uint8_t g, uint8_t b) {
    if (r > 200 && g < 50 && b < 50) return COLOR_RED;
    if (r < 50 && g > 200 && b < 50) return COLOR_GREEN;
    if (r < 50 && g < 50 && b > 200) return COLOR_BLUE;
    if (r > 50 && g < 50 && b > 200) return COLOR_PINK;
    if (r > 200 && g > 200 && b < 50) return COLOR_YELLOW;
    if (r > 200 && g > 200 && b > 200) return COLOR_WHITE;
    if (r < 50 && g < 50 && b < 50) return COLOR_BLACK;
    return COLOR_UNKNOWN;
}

// Apply principal colors (Route 1)
// Green (0, 255, 0)  |  Blue (0, 0, 255)  |  Pink/Purple (100, 0, 255)  |  Red (255, 0, 0)
void applyPrincipalColors(HoldColor color, uint8_t& r, uint8_t& g, uint8_t& b) {
    switch (color) {
        case COLOR_GREEN:  r = 0;   g = 255; b = 0;   break;
        case COLOR_BLUE:   r = 0;   g = 0;   b = 255; break;
        case COLOR_PINK:   r = 100; g = 0;   b = 255; break;
        case COLOR_RED:    r = 255; g = 0;   b = 0;   break;
        case COLOR_YELLOW: r = 255; g = 255; b = 100; break;
        case COLOR_WHITE:  r = 255; g = 255; b = 255; break;
        default:
            Serial.print("Unknown color: ");
            Serial.print(r); Serial.print(", ");
            Serial.print(g); Serial.print(", ");
            Serial.println(b);
            break;
    }
}

// Apply alternative colors (Route 2)
// Green (40, 255, 80)  |  Blue (0, 220, 255)  |  Pink (225, 255, 255)  |  Red (250, 150, 0)
void applyAltColors(HoldColor color, uint8_t& r, uint8_t& g, uint8_t& b) {
    switch (color) {
        case COLOR_GREEN:  r = 40;  g = 255; b = 80;  break;
        case COLOR_BLUE:   r = 0;   g = 220; b = 255; break;
        case COLOR_PINK:   r = 225; g = 255; b = 255; break;
        case COLOR_RED:    r = 250; g = 150; b = 0;   break;
        case COLOR_YELLOW: r = 220; g = 100; b = 100; break;
        case COLOR_WHITE:  r = 200; g = 200; b = 180; break;
        default: break; // keep original values
    }
}
