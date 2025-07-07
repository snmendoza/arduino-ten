#ifndef CLIMBING_WALL_HELPER_H
#define CLIMBING_WALL_HELPER_H

#include <Arduino.h>
#include <vector>

#define TOTAL_LEDS 500  // Total number of LEDs in the strip

// Structure to store hold information
struct Hold {
    uint16_t position;
    uint8_t r, g, b;
    String colorName;
};

// Structure to store a complete climb
struct Climb {
    std::vector<Hold> holds;
    bool isActive;  // Whether this climb is currently being displayed
    unsigned long lastUpdateTime;  // When this climb was last updated/added
};

class ClimbingWallHelper {
public:
    // Core color/hold handling (moved from main file)
    static void decodeColor(uint8_t colorByte, uint8_t& r, uint8_t& g, uint8_t& b);
    static String getColorName(uint8_t r, uint8_t g, uint8_t b);
    static uint8_t calculateChecksum(const std::vector<uint8_t>& data);

    // Bucket system
    static void initializeBuckets();
    static void addNewClimb(const std::vector<Hold>& holds);
    static std::vector<Hold> getActiveBucketHolds();
    static void applyBucketColorScheme(std::vector<Hold>& holds, bool isBucketA);
    
    // Simple LED offset
    static void setLEDOffset(int16_t offset) { ledOffset = offset; }
    static uint16_t mapLEDPosition(uint16_t position) { return position + ledOffset; }
    
    // Custom wall functions
    static bool isValidHoldPosition(uint16_t position);
    static uint16_t mapHoldPosition(uint16_t originalPosition);
    static void adjustBrightness(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t brightness);
    
    // Pattern generation
    static std::vector<Hold> generateTrainingPattern(const std::vector<Hold>& currentHolds);
    static std::vector<Hold> mirrorPattern(const std::vector<Hold>& currentHolds);

private:
    static Climb bucketA;
    static Climb bucketB;
    static bool nextIsA;  // Determines which bucket gets the next climb
    static int16_t ledOffset;  // Simple offset for LED positions
};

#endif // CLIMBING_WALL_HELPER_H 