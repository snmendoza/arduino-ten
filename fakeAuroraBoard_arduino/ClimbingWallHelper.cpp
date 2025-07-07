#include "ClimbingWallHelper.h"

// Initialize static members
Climb ClimbingWallHelper::bucketA = {std::vector<Hold>(), false, 0};
Climb ClimbingWallHelper::bucketB = {std::vector<Hold>(), false, 0};
bool ClimbingWallHelper::nextIsA = true;
int16_t ClimbingWallHelper::ledOffset = 0;

// Core functions moved from main file
void ClimbingWallHelper::decodeColor(uint8_t colorByte, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = ((colorByte >> 5) & 0x07) * 255 / 7;  // 3 bits for red
    g = ((colorByte >> 2) & 0x07) * 255 / 7;  // 3 bits for green
    b = (colorByte & 0x03) * 255 / 3;         // 2 bits for blue
}

String ClimbingWallHelper::getColorName(uint8_t r, uint8_t g, uint8_t b) {
    if (r > 200 && g < 50 && b < 50) return "Red";
    if (r < 50 && g > 200 && b < 50) return "Green";
    if (r < 50 && g < 50 && b > 200) return "Blue";
    if (r > 200 && g < 50 && b > 200) return "Pink";
    if (r > 200 && g > 200 && b < 50) return "Yellow";
    if (r > 200 && g > 200 && b > 200) return "White";
    if (r < 50 && g < 50 && b < 50) return "Black";
    return "Unknown";
}

uint8_t ClimbingWallHelper::calculateChecksum(const std::vector<uint8_t>& data) {
    uint8_t sum = 0;
    for (uint8_t byte : data) {
        sum = (sum + byte) & 255;
    }
    return (~sum) & 255;
}

bool ClimbingWallHelper::isValidHoldPosition(uint16_t position) {
    // TODO: Implement based on your wall's layout
    // Example: return position < MAX_HOLDS;
    return true;
}

uint16_t ClimbingWallHelper::mapHoldPosition(uint16_t originalPosition) {
    // TODO: Implement mapping from original wall positions to your wall positions
    // Example: return lookupTable[originalPosition];
    return originalPosition;
}

void ClimbingWallHelper::adjustBrightness(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t brightness) {
    r = (r * brightness) / 255;
    g = (g * brightness) / 255;
    b = (b * brightness) / 255;
}

std::vector<Hold> ClimbingWallHelper::generateTrainingPattern(const std::vector<Hold>& currentHolds) {
    // TODO: Implement training pattern generation
    // Example: Create a sequence that helps with training specific moves
    return currentHolds;
}

std::vector<Hold> ClimbingWallHelper::mirrorPattern(const std::vector<Hold>& currentHolds) {
    std::vector<Hold> mirroredHolds;
    // TODO: Implement mirroring logic based on your wall's layout
    // This could be useful for training both sides equally
    return mirroredHolds;
}

// Bucket system implementation
void ClimbingWallHelper::initializeBuckets() {
    bucketA.holds.clear();
    bucketB.holds.clear();
    bucketA.isActive = false;
    bucketB.isActive = false;
    bucketA.lastUpdateTime = 0;
    bucketB.lastUpdateTime = 0;
    nextIsA = true;
}

void ClimbingWallHelper::addNewClimb(const std::vector<Hold>& holds) {
    // Create a new vector with mapped LED positions
    std::vector<Hold> mappedHolds;
    for (const Hold& hold : holds) {
        Hold mappedHold = hold;
        mappedHold.position = mapLEDPosition(hold.position);
        if (mappedHold.position < TOTAL_LEDS) {  // Only add if within valid range
            mappedHolds.push_back(mappedHold);
        }
    }
    
    if (nextIsA) {
        bucketA.holds = mappedHolds;
        bucketA.isActive = true;
        bucketA.lastUpdateTime = millis();
    } else {
        bucketB.holds = mappedHolds;
        bucketB.isActive = true;
        bucketB.lastUpdateTime = millis();
    }
    nextIsA = !nextIsA;
}

std::vector<Hold> ClimbingWallHelper::getActiveBucketHolds() {
    std::vector<Hold> allHolds;
    
    // Add holds from both active buckets
    if (bucketA.isActive) {
        std::vector<Hold> bucketAHolds = bucketA.holds;
        applyBucketColorScheme(bucketAHolds, true);
        allHolds.insert(allHolds.end(), bucketAHolds.begin(), bucketAHolds.end());
    }
    
    if (bucketB.isActive) {
        std::vector<Hold> bucketBHolds = bucketB.holds;
        applyBucketColorScheme(bucketBHolds, false);
        allHolds.insert(allHolds.end(), bucketBHolds.begin(), bucketBHolds.end());
    }
    
    return allHolds;
}

void ClimbingWallHelper::applyBucketColorScheme(std::vector<Hold>& holds, bool isBucketA) {
    for (Hold& hold : holds) {
        if (isBucketA) {
            // Bucket A color scheme: Enhance red component
            hold.r = min(255, hold.r + 50);
            hold.g = max(0, hold.g - 30);
            hold.b = max(0, hold.b - 30);
        } else {
            // Bucket B color scheme: Enhance blue component
            hold.r = max(0, hold.r - 30);
            hold.g = max(0, hold.g - 30);
            hold.b = min(255, hold.b + 50);
        }
    }
}

// LED mapping system implementation
void ClimbingWallHelper::clearLEDMappings() {
    ledMappings.clear();
}

void ClimbingWallHelper::addLEDMapping(uint16_t startIndex, uint16_t endIndex) {
    if (startIndex >= TOTAL_LEDS || endIndex >= TOTAL_LEDS || startIndex > endIndex) {
        return;  // Invalid mapping
    }
    
    // Calculate the offset for this region
    int16_t offset = endIndex - startIndex + 1;
    
    // Add the mapping
    LEDRegion region = {startIndex, endIndex, offset};
    ledMappings.push_back(region);
    
    // Sort mappings by startIndex to ensure correct sequential processing
    std::sort(ledMappings.begin(), ledMappings.end(), 
              [](const LEDRegion& a, const LEDRegion& b) {
                  return a.startIndex < b.startIndex;
              });
}

uint16_t ClimbingWallHelper::mapLEDPosition(uint16_t originalPosition) {
    if (originalPosition >= TOTAL_LEDS) {
        return originalPosition;  // Invalid position
    }
    
    uint16_t mappedPosition = originalPosition + ledOffset;
    
    // Apply offsets from each mapping region
    for (const LEDRegion& region : ledMappings) {
        if (originalPosition > region.endIndex) {
            // If the original position is after this region, add the offset
            mappedPosition += region.offset;
        } else if (originalPosition >= region.startIndex) {
            // If the original position is within this region, skip to the end
            mappedPosition = originalPosition + region.offset;
        }
    }
    
    return mappedPosition;
}

bool ClimbingWallHelper::isValidLEDPosition(uint16_t position) {
    if (position >= TOTAL_LEDS) {
        return false;
    }
    
    // Check if the position falls within any skip region
    for (const LEDRegion& region : ledMappings) {
        if (position >= region.startIndex && position <= region.endIndex) {
            return false;
        }
    }
    
    return true;
} 