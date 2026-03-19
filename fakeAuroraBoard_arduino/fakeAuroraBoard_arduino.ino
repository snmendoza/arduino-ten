#include <ArduinoBLE.h>
#include <Wire.h>          // I2C for ESP32 route bridge
#include <vector>
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <tuple>
#include <IRremote.h>

/*
 * Run tension board receiver on arduino r4
 * =====================================================================
 * 
 * PURPOSE: Simulates an Aurora Board climbing training device using Arduino R4 WiFi + LED strip
 * 
 * HARDWARE: 
 * - Arduino R4 WiFi with BLE capability
 * - WS2811 LED strip (500 LEDs on pin 13)
 * - Each LED represents a climbing hold position (0-499)
 * 
 * COMMUNICATION:
 * - BLE peripheral device advertising as "Tension Board 2@3"
 * - Implements Aurora Board protocol (API Level 3)
 * - Uses Nordic UART Service UUIDs for data transfer
 * 
 * FUNCTIONALITY:
 * - Receives climbing route data via BLE packets
 * - Displays routes as colored LEDs on strip
 * - always in dual route mode, where each route can be toggled on and off via IR remote
 * - Second route used alternative colors for route differentiation
 * 
 * DATA FORMAT:
 * - Packets: [START][LENGTH][CHECKSUM][TYPE_MARKER][PACKET_TYPE][HOLD_DATA...][END]
 * - Hold data: 3 bytes per hold (position_low, position_high, color_encoded)
 * - Colors: 8-bit compressed RGB (3R:3G:2B bits)
 */

#define DISPLAY_NAME "Tension Board 2"
#define API_LEVEL 3

// Active lane: 0 = route1 (principal colors), 1 = route2 (alt colors)
uint8_t currentLane = 0;

// Device switching toggle: Set to true to continue advertising when connected (allows quick device switching)
#define CONTINUOUS_ADVERTISING true

// Aurora Board protocol UUIDs
#define ADVERTISING_SERVICE_UUID "4488B571-7806-4DF6-BCFF-A2897E4953FF"  // Aurora Board advertising service
#define DATA_TRANSFER_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART Service
#define DATA_TRANSFER_CHARACTERISTIC "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write characteristic
#define NOTIFY_CHARACTERISTIC "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify characteristic

#define LED_PIN 13  // LED pin for Arduino R4 WiFi (changed from 25 to 13)
#define NUM_LEDS 478  // Replace with the number of LEDs in your strip
#define DELAY_TIME 10  // Delay between LED movements (in milliseconds)

#define IR_RECEIVE_PIN 9  // Define the IR receiver pin

// Global board name buffer for BLE
char boardName[64];

// Color categories for hold classification
enum HoldColor : uint8_t {
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_PINK,
    COLOR_YELLOW,
    COLOR_WHITE,
    COLOR_BLACK,
    COLOR_UNKNOWN
};

// Structure to store hold information
struct Hold {
    uint16_t position;
    uint8_t r, g, b;
    HoldColor color;
};

CRGB leds[NUM_LEDS];


// ArduinoBLE objects (different from ESP32 BLE)
BLEService advertisingService(ADVERTISING_SERVICE_UUID);
BLEService dataTransferService(DATA_TRANSFER_SERVICE_UUID);
BLECharacteristic dataTransferCharacteristic(DATA_TRANSFER_CHARACTERISTIC, BLEWrite, 512);
BLECharacteristic notifyCharacteristic(NOTIFY_CHARACTERISTIC, BLENotify | BLERead, 512);

bool deviceConnected = false;

// Global route storage for LED display
std::vector<Hold> route1Holds;         // First route storage
std::vector<Hold> route2Holds;         // Second route storage
std::vector<Hold> route1HoldsLast;     // First route storage for last route(history)
std::vector<Hold> route2HoldsLast;     // Second route storage for last route(history)
bool route1On = true;
bool route2On = true;

// Timeout tracking: last time any route was updated (route1 or route2)
unsigned long lastRouteUpdateMillis = 0;

// Deferred LED update flag (CRITICAL: prevents BLE callback deadlock)
volatile bool pendingLEDUpdate = false;

// IR activity tracking to pause animations during IR reception
unsigned long lastIRCheckMillis = 0;
bool irRecentlyActive = false;
const unsigned long IR_COOLDOWN_MS = 500UL; // Pause animation for 500ms after IR activity to ensure command completes

// Overlap animation state (route1 vs route2)
struct OverlapInfo {
    uint16_t position;
    CRGB colorRoute1;
    CRGB colorRoute2;
};
std::vector<OverlapInfo> overlappingHolds;
std::vector<Hold> route2OnlyHolds;  // Route2 holds not present in route1 (pre-computed)
bool hasOverlap = false;
unsigned long overlapAnimStartMillis = 0;
unsigned long lastAnimUpdateMillis = 0;
const unsigned long ANIM_UPDATE_INTERVAL_MS = 50UL; // Update animation every 50ms (20fps) - pauses during IR activity

// Helper function to decode RGB color from API level 3 format
void decodeColor(uint8_t colorByte, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = ((colorByte >> 5) & 0x07) * 255 / 7;  // 3 bits for red
    g = ((colorByte >> 2) & 0x07) * 255 / 7;  // 3 bits for green
    b = (colorByte & 0x03) * 255 / 3;         // 2 bits for blue
}

// Helper function to classify decoded RGB into a hold color category
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
// Helper function to calculate checksum
uint8_t calculateChecksum(const std::vector<uint8_t>& data) {
    uint8_t sum = 0;
    for (uint8_t byte : data) {
        sum = (sum + byte) & 255;
    }
    return (~sum) & 255;
}

// Helper function to apply principal colors (Route 1)
// Green	(0, 255, 0)	Neon green
// Blue	(0, 0, 255)	pure blue
// Purple	(100, 0, 255)	Vivid violet
// Red	(255, 0, 0)	Bright scarlet
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

// Helper function to apply alternative colors (Route 2)
// Green	to (40, 255, 80)	Lime chartreuse
// Blue	to (0, 220, 255)	Cyan-blue glow
// Purple	to (225, 255, 255)	Hot magenta
// Red	to (250, 150, 0)	Vivid orange-red
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

// Helper: recompute overlapping holds between route1 and route2 for animation
void updateOverlapState() {
    overlappingHolds.clear();
    hasOverlap = false;

    if (route1Holds.empty() || route2Holds.empty()) {
        return;
    }

    // Build a position lookup from route1 for O(n+m) instead of O(n*m)
    static bool route1PosPresent[NUM_LEDS];
    memset(route1PosPresent, 0, sizeof(route1PosPresent));

    for (const Hold& h1 : route1Holds) {
        if (h1.position < NUM_LEDS) {
            route1PosPresent[h1.position] = true;
        }
    }

    // Find overlapping holds and pre-compute route2-only holds
    route2OnlyHolds.clear();
    for (const Hold& h2 : route2Holds) {
        if (h2.position >= NUM_LEDS) continue;
        if (route1PosPresent[h2.position]) {
            // Overlapping — find route1's color for this position
            for (const Hold& h1 : route1Holds) {
                if (h1.position == h2.position) {
                    OverlapInfo info;
                    info.position = h1.position;
                    info.colorRoute1 = CRGB(h1.r, h1.g, h1.b);
                    info.colorRoute2 = CRGB(h2.r, h2.g, h2.b);
                    overlappingHolds.push_back(info);
                    break;
                }
            }
        } else {
            route2OnlyHolds.push_back(h2);
        }
    }

    hasOverlap = !overlappingHolds.empty();
    if (hasOverlap) {
        overlapAnimStartMillis = millis();
    }
}

// Simple color blend helper for overlap animation
CRGB blendColors(const CRGB& c1, const CRGB& c2, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint8_t r = static_cast<uint8_t>(c1.r + (int16_t)(c2.r - c1.r) * t);
    uint8_t g = static_cast<uint8_t>(c1.g + (int16_t)(c2.g - c1.g) * t);
    uint8_t b = static_cast<uint8_t>(c1.b + (int16_t)(c2.b - c1.b) * t);
    return CRGB(r, g, b);
}

//combines both route data into one vector, where data are overlapping (eg route 1 and 2 both have a hold at position 100),
// route 1 takes priority, route 2 is overwritten
void updateBoardState() {
    // both off, just clear
    if (!route1On && !route2On) {
        FastLED.clear();
        FastLED.show();
        return;
    }
    
    // if only one route is on, send it to the board    
    if (route1On && !route2On) {
        // setBoardLEDs handles its own timing, no need to disable interrupts here
        setBoardLEDs(route1Holds);
    }
    else if (route2On && !route1On) {
        // setBoardLEDs handles its own timing, no need to disable interrupts here
        setBoardLEDs(route2Holds);
    }
    // if both routes are on, calculate union of both routes
    else if (route1On && route2On) {
        // If there is no overlap, fall back to existing combined behavior
        if (!hasOverlap) {
            std::vector<Hold> boardState;
            boardState.insert(boardState.end(), route1Holds.begin(), route1Holds.end());
            boardState.insert(boardState.end(), route2Holds.begin(), route2Holds.end());
            // setBoardLEDs handles its own timing, no need to disable interrupts here
            setBoardLEDs(boardState);
        } else {
            // Overlap present: animate overlapping holds with hold-fade pattern
            // Pattern: Hold color1 (1s) -> Fade to color2 (0.75s) -> Hold color2 (1s) -> Fade to color1 (0.75s) -> repeat
            const unsigned long periodMs = 2200UL; // Total period: 750ms + 350ms + 750ms + 350ms = 2200ms
            const unsigned long hold1Ms = 750UL;  // Hold at color1
            const unsigned long fade1Ms = 350UL;  // Fade to color2
            const unsigned long hold2Ms = 750UL; // Hold at color2
            const unsigned long fade2Ms = 350UL;  // Fade back to color1
            
            unsigned long now = millis();
            unsigned long elapsed = (now - overlapAnimStartMillis) % periodMs;
            float t;
            
            if (elapsed < hold1Ms) {
                // Phase A: Hold at color1 (t=0)
                t = 0.0f;
            } else if (elapsed < hold1Ms + fade1Ms) {
                // Phase B: Fade from color1 to color2 (t: 0 -> 1)
                unsigned long fadeElapsed = elapsed - hold1Ms;
                t = static_cast<float>(fadeElapsed) / static_cast<float>(fade1Ms);
            } else if (elapsed < hold1Ms + fade1Ms + hold2Ms) {
                // Phase C: Hold at color2 (t=1)
                t = 1.0f;
            } else {
                // Phase D: Fade from color2 to color1 (t: 1 -> 0)
                unsigned long fadeElapsed = elapsed - (hold1Ms + fade1Ms + hold2Ms);
                t = 1.0f - (static_cast<float>(fadeElapsed) / static_cast<float>(fade2Ms));
            }

            // CRITICAL FIX: Do ALL LED calculations with interrupts ENABLED
            // Only disable interrupts for the final FastLED.show() call
            
            // Clear all LEDs first (interrupts enabled)
            FastLED.clear();

            // First, draw all route1 holds (base colors) - interrupts enabled
            for (const Hold& h1 : route1Holds) {
                if (h1.position < NUM_LEDS) {
                    leds[h1.position] = CRGB(h1.r, h1.g, h1.b);
                }
            }

            // Then draw route2-only holds (pre-computed, no per-frame lookup needed)
            for (const Hold& h2 : route2OnlyHolds) {
                if (h2.position < NUM_LEDS) {
                    leds[h2.position] = CRGB(h2.r, h2.g, h2.b);
                }
            }

            // Finally, blend overlapping holds between route1 and route2 colors - interrupts enabled
            for (const OverlapInfo& info : overlappingHolds) {
                if (info.position < NUM_LEDS) {
                    leds[info.position] = blendColors(info.colorRoute1, info.colorRoute2, t);
                }
            }

            // Use FastLED.show() which handles interrupt timing automatically
            FastLED.show();
        }
    }
}

// interpret a full board state vector, and displays it to the LEDs
void setBoardLEDs(const std::vector<Hold>& boardState) {
    // Clear all LEDs first (interrupts enabled for calculations)
    FastLED.clear();
    
    // Set new LED values (interrupts enabled)
    for (const Hold& hold : boardState) {
        if (hold.position < NUM_LEDS) {  // Safety check
            leds[hold.position] = CRGB(hold.r, hold.g, hold.b);
        }
    }
    
    // Update the physical LEDs (FastLED.show() handles interrupt timing internally)
    FastLED.show();
}


// mirror current problem horizontally
// 1. compute position within column
// 2. compute column
// 3. calculate new column
// 4. calculate new position
int mirrorPosition(int pos) {
    int remainder = pos % 29; // position within column
    int col = (pos - remainder) / 29; // index of column location
    int new_col = 16 - col;
    if (remainder > 14) {
        new_col -= 1;
    }
    int new_position = new_col * 29 + remainder;
    return new_position;
}

void mirrorCurrentLane() {
    if (currentLane == 0) {
        for (size_t i = 0; i < route1Holds.size(); i++) {
            route1Holds[i].position = mirrorPosition(route1Holds[i].position);
        }
    } else {
        for (size_t i = 0; i < route2Holds.size(); i++) {
            route2Holds[i].position = mirrorPosition(route2Holds[i].position);
        }
    }
    // Recalculate overlap state after changing positions
    updateOverlapState();
    updateBoardState(); // Update the display after mirroring
}

// Convert a wavelength (400-700nm) to an RGB CRGB value using piecewise linear approximation
CRGB wavelengthToRGB(float wavelength) {
    float r = 0.0f, g = 0.0f, b = 0.0f;

    if (wavelength >= 400 && wavelength < 440) {
        r = -(wavelength - 440) / (440 - 400);
        b = 1.0f;
    } else if (wavelength >= 440 && wavelength < 490) {
        g = (wavelength - 440) / (490 - 440);
        b = 1.0f;
    } else if (wavelength >= 490 && wavelength < 510) {
        g = 1.0f;
        b = -(wavelength - 510) / (510 - 490);
    } else if (wavelength >= 510 && wavelength < 580) {
        r = (wavelength - 510) / (580 - 510);
        g = 1.0f;
    } else if (wavelength >= 580 && wavelength < 645) {
        r = 1.0f;
        g = -(wavelength - 645) / (645 - 580);
    } else if (wavelength >= 645 && wavelength <= 700) {
        r = 1.0f;
    }

    return CRGB(
        static_cast<uint8_t>(constrain(r, 0.0f, 1.0f) * 255),
        static_cast<uint8_t>(constrain(g, 0.0f, 1.0f) * 255),
        static_cast<uint8_t>(constrain(b, 0.0f, 1.0f) * 255)
    );
}

// on startup, show this sequence
// smoothly cycles through spectrum colors equivalent to 400nm to 700nm
// then reverse it and end on purple
void startupLEDs() {
    if (NUM_LEDS <= 0) return;

    const int totalSteps = 50;
    const int delayMs = 5;
    const float startWl = 400.0f;
    const float endWl = 700.0f;
    const float range = endWl - startWl;

    // Forward sweep: violet → red
    for (int step = 0; step < totalSteps; step++) {
        float wl = startWl + (step * range / totalSteps);
        fill_solid(leds, NUM_LEDS, wavelengthToRGB(wl));
        FastLED.show();
        delay(delayMs);
    }

    // Reverse sweep: red → violet
    for (int step = totalSteps - 1; step >= 0; step--) {
        float wl = startWl + (step * range / totalSteps);
        fill_solid(leds, NUM_LEDS, wavelengthToRGB(wl));
        FastLED.show();
        delay(delayMs);
    }

    // Hold purple
    fill_solid(leds, NUM_LEDS, CRGB(128, 0, 128));
    FastLED.show();
}


void clearBoardExceptRoute1() {     
    //tempHolds.clear();
    setRouteStates(true, false);
}
//specify desired state for active route
void setRouteState(bool routeOn) {
    if (currentLane == 0) {
        route1On = routeOn;
    } else {
        route2On = routeOn;
    }
    // Recalculate overlap state when changing visibility
    updateOverlapState();
    updateBoardState();
}

//specify desired states for both routes
void setRouteStates(bool route1State, bool route2State) {
    route1On = route1State;
    route2On = route2State;
    // Recalculate overlap state when changing visibility
    updateOverlapState();
    updateBoardState();
}
// toggle route visibility of active route, based on current  lane and current state
void toggleRouteVisibility() {
    if (currentLane == 0) {
        route1On = !route1On;
        Serial.print("Route 1 visibility toggled to: ");
        Serial.println(route1On ? "ON" : "OFF");
    } else {
        route2On = !route2On;
        Serial.print("Route 2 visibility toggled to: ");
        Serial.println(route2On ? "ON" : "OFF");
    }
    // CRITICAL: Recalculate overlap state after visibility change
    // This ensures overlappingHolds vector is fresh when animation resumes
    updateOverlapState();
    
    // Force immediate LED update after toggle
    updateBoardState();
    Serial.print("  [After toggle: route1On=");
    Serial.print(route1On);
    Serial.print(", route2On=");
    Serial.print(route2On);
    Serial.print(", hasOverlap=");
    Serial.print(hasOverlap);
    Serial.print(", route1Holds.size=");
    Serial.print(route1Holds.size());
    Serial.print(", route2Holds.size=");
    Serial.print(route2Holds.size());
    Serial.println("]");
}

// BLE event handlers for ArduinoBLE
void onBLEConnected(BLEDevice central) {
    deviceConnected = true;
    Serial.print("Device connected: ");
    Serial.println(central.address());
    //Serial.println(central.localName());
    #if CONTINUOUS_ADVERTISING
    // Continue advertising to allow immediate device switching
    BLE.advertise();
    #endif
}


void onBLEDisconnected(BLEDevice central) {
    deviceConnected = false;
    Serial.print("Device disconnected: ");
    Serial.println(central.address());

    // Restart BLE stack to recover from stuck state
    BLE.end();
    delay(100);
    BLE.begin();
    BLE.setLocalName(boardName); // If boardName is global or static
    BLE.setDeviceName(boardName);
    BLE.setEventHandler(BLEConnected, onBLEConnected);
    BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);
    dataTransferService.addCharacteristic(dataTransferCharacteristic);
    dataTransferService.addCharacteristic(notifyCharacteristic);
    BLE.addService(advertisingService);
    BLE.addService(dataTransferService);
    dataTransferCharacteristic.setEventHandler(BLEWritten, onDataTransferCharacteristicWritten);
    BLE.setAdvertisedService(advertisingService);
    BLE.advertise();
}

/*
 * BLE Characteristic Write Handler
 * 
 * This function handles incoming data from BLE clients and processes Aurora Board protocol packets.
 * The Aurora Board protocol uses a specific packet structure for transmitting hold information.
 * 
 * Packet Structure (API Level 3):
 * [START][LENGTH][CHECKSUM][TYPE_MARKER][PACKET_TYPE][HOLD_DATA...][END]
 * - START: 0x01 (1 byte)
 * - LENGTH: Number of data bytes (1 byte)
 * - CHECKSUM: Calculated checksum of data portion (1 byte)
 * - TYPE_MARKER: Always 0x02 (1 byte)
 * - PACKET_TYPE: 'R', 'S', 'Q', or 'T' (1 byte)
 * - HOLD_DATA: 3 bytes per hold (position_low, position_high, color_encoded)
 * - END: 0x03 (1 byte)s
 * 
 * Packet Types:
 * - 'R': Route packet (partial data)
 * - 'S': Set packet (complete route data)
 * - 'Q': Query packet
 * - 'T': Test packet (single route, clears previous holds)
 * 
 * Operating Modes (controlled by DUAL_ROUTE_MODE flag):
 * - Basic Mode (false): Single route storage, new routes overwrite previous
 * - Dual Route Mode (true): Stores two routes alternately with different color schemes
 *   Route 1: Original colors
 *   Route 2: Green-biased colors (enhanced green, reduced red/blue)
 *   Alternation: Route1 -> Route2 -> Route1 -> Route2...
 */

// State management for climbing routes
std::vector<Hold> tempHolds;         // Temporary storage for incoming route
bool isNewClimb = true;              // Flag to track if this is a new climb sequence
// Packet buffer for direct BLE Aurora packets (phone -> R4)
std::vector<uint8_t> packetBufferBLE;   // Buffer for incomplete BLE packets

// UART route protocol from ESP32 (pre-parsed route 2)
// Frame: [0xAA][0x55][count][(posL,posH,colorByte) * count][checksum]
// checksum = XOR of count byte and all payload bytes
#define UART_ROUTE_HEADER_1 0xAA
#define UART_ROUTE_HEADER_2 0x55

enum UartRouteState {
    UART_WAIT_HEADER1,
    UART_WAIT_HEADER2,
    UART_WAIT_COUNT,
    UART_READ_PAYLOAD,
    UART_WAIT_CHECKSUM
};

UartRouteState uartRouteState = UART_WAIT_HEADER1;
uint8_t uartRouteCount = 0;          // number of holds expected
uint16_t uartRouteExpectedBytes = 0; // 3 * count
std::vector<uint8_t> uartRoutePayload;

void onDataTransferCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
    // Get written data
    const uint8_t* data = characteristic.value();
    int length = characteristic.valueLength();
    

    if (length > 0) {
        // Debug output: Show raw received bytes (COMMENTED OUT to prevent BLE timeout)
        // Serial.print("Received command from ");
        // Serial.print(BLE.address());
        // Serial.print(": ");
        // for (int i = 0; i < length; i++) {
        //   Serial.print((int)data[i]);
        //   Serial.print(" ");
        // }
        // Serial.println();
        
        // Accumulate received bytes in BLE buffer to handle packet fragmentation
        // BLE may split packets across multiple writes
        for (int i = 0; i < length; i++) {
            packetBufferBLE.push_back(data[i]);
        }
        
        // Parse BLE packet buffer
        while (packetBufferBLE.size() >= 5) {  // Minimum packet size: START + LENGTH + CHECKSUM + TYPE_MARKER + PACKET_TYPE
            // Packet parsing: Find the start marker (0x01)
            size_t startIdx = 0;
            while (startIdx < packetBufferBLE.size() && packetBufferBLE[startIdx] != 1) {
                startIdx++;
            }
            
            // Check if we have enough data for packet header
            if (startIdx >= packetBufferBLE.size() - 4) {
                // Not enough data for a complete packet header
                break;
            }
            
            // Extract packet length and validate we have complete packet
            uint8_t packetLength = packetBufferBLE[startIdx + 1];
            if (startIdx + packetLength + 4 >= packetBufferBLE.size()) {
                // Not enough data for the full packet (including END marker)
                break;
            }
            
            // Validate packet structure: Check type marker (should be 0x02)
            if (packetBufferBLE[startIdx + 3] != 2) {
                Serial.println("Invalid packet format - second byte should be 2");
                // Remove invalid byte and try again
                packetBufferBLE.erase(packetBufferBLE.begin(), packetBufferBLE.begin() + startIdx + 1);
                continue;
            }
            
            // Extract and validate packet type
            uint8_t packetType = packetBufferBLE[startIdx + 4];
            char packetTypeChar = (char)packetType;
            
            // Verify packet type is valid for API level 3
            if (packetTypeChar != 'R' && packetTypeChar != 'S' && 
                packetTypeChar != 'Q' && packetTypeChar != 'T') {
                Serial.println("Invalid packet type for API level 3");
                // Remove invalid byte and try again
                packetBufferBLE.erase(packetBufferBLE.begin(), packetBufferBLE.begin() + startIdx + 1);
                continue;
            }
            
            // Handle route state management
            // 'T' packets indicate a new test route, clear previous holds
            if (packetTypeChar == 'T') {
                tempHolds.clear();  // Clear temporary storage for new route
                isNewClimb = true;
            }
            
            // Checksum validation
            // Extract data portion (from packet type to end of data)
            std::vector<uint8_t> dataBytes;
            for (size_t i = startIdx + 4; i < startIdx + packetLength + 3; i++) {
                dataBytes.push_back(packetBufferBLE[i]);
            }
            uint8_t calculatedChecksum = calculateChecksum(dataBytes);
            uint8_t receivedChecksum = packetBufferBLE[startIdx + 2];
            
            // Debug output: Show packet details (COMMENTED OUT to prevent BLE timeout)
            // Serial.print("\n___________________Decoded packet (at millis: "); Serial.print(millis()); Serial.println(")___________________");
            // Serial.print("Length: "); Serial.println(packetLength);
            // Serial.print("Checksum: "); Serial.println(receivedChecksum);
            // Serial.print("Calculated checksum: "); Serial.println(calculatedChecksum);
            // Serial.print("Packet type: "); Serial.println(packetTypeChar);
            
            // Decode hold data (3 bytes per hold for API level 3)
            // Format: [position_low][position_high][color_encoded]
            for (size_t i = startIdx + 5; i < startIdx + packetLength + 3; i += 3) {
                if (i + 2 < packetBufferBLE.size()) {
                    // Reconstruct 16-bit position from little-endian bytes
                    uint16_t position = (packetBufferBLE[i+1] << 8) + packetBufferBLE[i];
                    
                    // Decode compressed RGB color from single byte
                    uint8_t r, g, b;
                    decodeColor(packetBufferBLE[i+2], r, g, b);
                    HoldColor color = classifyColor(r, g, b);

                    // Create hold with original decoded colors
                    Hold h = {position, r, g, b, color};
                    tempHolds.push_back(h);
                }
            }
            
            // Route completion handling
            // 'S' (Set) and 'T' (Test) packets indicate complete route transmission
            if (packetTypeChar == 'S' || packetTypeChar == 'T') {
                    if (currentLane == 0) {
                        // Clear active route, then assign new holds
                        route1Holds.clear();
                        route1Holds = tempHolds;
                        // Apply principal colors to the stored route
                        for (Hold& h : route1Holds) {
                            applyPrincipalColors(h.color, h.r, h.g, h.b);
                        }
                        Serial.println("Route 1 received");
                    } else if (currentLane == 1) {
                        // Clear active route, then assign new holds
                        route2Holds.clear();
                        route2Holds = tempHolds;
                        // Apply alternative colors to the stored route
                        for (Hold& h : route2Holds) {
                            applyAltColors(h.color, h.r, h.g, h.b);
                        }
                        Serial.println("Route 2 received");
                    }
                    // Debug: Print state after updating routes (COMMENTED OUT to prevent BLE timeout)
                    // Serial.print("[DEBUG] After update: activeRoute=");
                    // Serial.print(activeRoute);
                    // Serial.print(" | route1Holds size=");
                    // Serial.print(route1Holds.size());
                    // Serial.print(" | route2Holds size=");
                    // Serial.print(route2Holds.size());
                    // Serial.print(" | tempHolds size=");
                    // Serial.println(tempHolds.size());
                    
                    // Show the route that was just stored
                    // for (const Hold& h : tempHolds) {
                    //     Serial.print("Position "); Serial.print(h.position);
                    //     Serial.print(": "); Serial.println(h.colorName);
                    // }
                    
                    // Show complete dual route summary
                    // Serial.println("\n=== DUAL ROUTE SUMMARY ===");
                    // if (!route1Holds.empty()) {
                    //     Serial.println("Route 1:");
                    //     for (const Hold& h : route1Holds) {
                    //         Serial.print("  Position "); Serial.print(h.position);
                    //         Serial.print(": "); Serial.println(h.colorName);
                    //     }
                    // }
                    // if (!route2Holds.empty()) {
                    //     Serial.println("Route 2:");
                    //     for (const Hold& h : route2Holds) {
                    //         Serial.print("  Position "); Serial.print(h.position);
                    //         Serial.print(": "); Serial.println(h.colorName);
                    //     }
                    // }
                    // Serial.println("========================");
                
                // Clear temporary storage for next route
                tempHolds.clear();
                
                // Update overlap state and timeout timestamp, then set flag for deferred LED update
                updateOverlapState();
                lastRouteUpdateMillis = millis();
                pendingLEDUpdate = true;  // ← Set flag instead of calling updateBoardState() directly
                Serial.println();
            }
            
            // Remove processed packet from buffer
            // This advances the buffer past the current packet
            packetBufferBLE.erase(packetBufferBLE.begin(), packetBufferBLE.begin() + startIdx + packetLength + 4);
        }
        
        // Buffer overflow protection
        // Clear buffer if it grows too large (prevents memory issues)
        if (packetBufferBLE.size() > 1000) {
            Serial.println("Buffer overflow, clearing");
            packetBufferBLE.clear();
        }
    }
}

// Poll ESP32 (I2C slave) for any pending BLE bytes and feed them into
// the same packetBuffer/parser used for BLE on this board.
// All ESP32-sourced routes are forced onto channel 2 (alternate colors).
// Poll ESP32 UART link for any pending pre-parsed route 2 updates
void pollESP32RouteUART() {
    while (Serial1.available() > 0) {
        uint8_t b = static_cast<uint8_t>(Serial1.read());
        switch (uartRouteState) {
            case UART_WAIT_HEADER1:
                if (b == UART_ROUTE_HEADER_1) {
                    uartRouteState = UART_WAIT_HEADER2;
                }
                break;

            case UART_WAIT_HEADER2:
                if (b == UART_ROUTE_HEADER_2) {
                    uartRouteState = UART_WAIT_COUNT;
                } else if (b == UART_ROUTE_HEADER_1) {
                    // Stay in header2 state if we see first header byte again
                    uartRouteState = UART_WAIT_HEADER2;
                } else {
                    uartRouteState = UART_WAIT_HEADER1;
                }
                break;

            case UART_WAIT_COUNT:
                uartRouteCount = b;
                if (uartRouteCount == 0) {
                    uartRouteState = UART_WAIT_HEADER1;
                } else {
                    uartRoutePayload.clear();
                    uartRouteExpectedBytes = static_cast<uint16_t>(uartRouteCount) * 3;
                    uartRoutePayload.reserve(uartRouteExpectedBytes);
                    uartRouteState = UART_READ_PAYLOAD;
                }
                break;

            case UART_READ_PAYLOAD:
                uartRoutePayload.push_back(b);
                if (uartRoutePayload.size() >= uartRouteExpectedBytes) {
                    // Payload complete, wait for checksum byte
                    uartRouteState = UART_WAIT_CHECKSUM;
                }
                break;

            case UART_WAIT_CHECKSUM: {
                // Validate checksum: XOR of count + all payload bytes
                uint8_t expected = uartRouteCount;
                for (uint8_t byte : uartRoutePayload) {
                    expected ^= byte;
                }
                if (b != expected) {
                    Serial.print("[UART] Checksum mismatch: got 0x");
                    Serial.print(b, HEX);
                    Serial.print(" expected 0x");
                    Serial.println(expected, HEX);
                    uartRouteState = UART_WAIT_HEADER1;
                    uartRoutePayload.clear();
                    break;
                }

                // Checksum valid — commit route2
                route2Holds.clear();

                for (uint8_t i = 0; i < uartRouteCount; i++) {
                    size_t idx = static_cast<size_t>(i) * 3;
                    uint16_t position = static_cast<uint16_t>(uartRoutePayload[idx]) |
                                        (static_cast<uint16_t>(uartRoutePayload[idx + 1]) << 8);
                    uint8_t colorByte = uartRoutePayload[idx + 2];

                    uint8_t r, g, bColor;
                    decodeColor(colorByte, r, g, bColor);
                    HoldColor color = classifyColor(r, g, bColor);

                    Hold h = {position, r, g, bColor, color};
                    route2Holds.push_back(h);
                }

                // Apply alternate color mapping for route 2
                for (Hold& h : route2Holds) {
                    applyAltColors(h.color, h.r, h.g, h.b);
                }

                Serial.println("[UART] Route 2 received from ESP32 (checksum OK)");

                // Update overlap state and timeout timestamp for UART-delivered route2
                updateOverlapState();
                lastRouteUpdateMillis = millis();
                pendingLEDUpdate = true;

                // Reset state machine for next frame
                uartRouteState = UART_WAIT_HEADER1;
                uartRoutePayload.clear();
                break;
            }
        }
    }
}

void setup() {
  Serial.begin(115200);
  { unsigned long t0 = millis(); while (!Serial && millis() - t0 < 3000); }  // Wait up to 3s for serial

  // Hardware UART1 for ESP32 link (pins depend on board; on UNO R4 WiFi:
  // Serial1 RX/TX are exposed on the UART header; wire to ESP32 TX2/RX2).
  Serial1.begin(115200);

  FastLED.addLeds<WS2811, LED_PIN, RGB>(leds, NUM_LEDS);
  FastLED.clear();

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK); // Start IR receiver
  
  // Send initial API level
  Serial.write(4);
  Serial.write(API_LEVEL);

  snprintf(boardName, sizeof(boardName), "%s@%d", DISPLAY_NAME, API_LEVEL);

  Serial.println("Initializing BLE...");
  Serial.println("Using UUIDs:");
  Serial.println(ADVERTISING_SERVICE_UUID);
  Serial.println(DATA_TRANSFER_SERVICE_UUID);
  Serial.println(DATA_TRANSFER_CHARACTERISTIC);
  Serial.println(NOTIFY_CHARACTERISTIC);
  
  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("Starting Bluetooth® Low Energy module failed!");
    while (1);
  }

  // Set device name and local name
  BLE.setLocalName(boardName);
  BLE.setDeviceName(boardName);

  // Set up event handlers
  BLE.setEventHandler(BLEConnected, onBLEConnected);
  BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);

  // Add characteristics to services
  dataTransferService.addCharacteristic(dataTransferCharacteristic);
  dataTransferService.addCharacteristic(notifyCharacteristic);

  // Add services to BLE
  BLE.addService(advertisingService);
  BLE.addService(dataTransferService);

  // Set characteristic event handlers
  dataTransferCharacteristic.setEventHandler(BLEWritten, onDataTransferCharacteristicWritten);

  // Set the advertised service
  BLE.setAdvertisedService(advertisingService);

  // Start advertising
  BLE.advertise();
  
  Serial.println("BLE device ready to connect");
  Serial.print("Device name: ");
  Serial.println(boardName);
  // show startup sequence once we have booted fully
  startupLEDs();
}

void checkIRRemote() {
    if (IrReceiver.decode()) {
        // Ignore NEC repeat frames to prevent double-firing on a single press
        if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) {
            IrReceiver.resume();
            return;
        }

        // Mark IR as recently active to pause animations
        irRecentlyActive = true;
        lastIRCheckMillis = millis();

        Serial.print("Protocol: ");
        Serial.print(IrReceiver.decodedIRData.protocol);
        Serial.print(" Address: 0x");
        Serial.print(IrReceiver.decodedIRData.address, HEX);
        Serial.print(" Command: 0x");
        Serial.println(IrReceiver.decodedIRData.command, HEX);
        // dual route toggle, disables
        /* if (IrReceiver.decodedIRData.protocol == NEC &&
            IrReceiver.decodedIRData.address == 0xC7EA &&
            IrReceiver.decodedIRData.command == 0x61) {
            dualRouteMode = !dualRouteMode;
            Serial.print("IR: Toggled dualRouteMode. Now: ");
            Serial.println(dualRouteMode ? "DUAL" : "SINGLE");
        } */
        // 0xC7EA Command: 0x19 == up arrow == select lane ONE
        if (IrReceiver.decodedIRData.protocol == NEC &&
                 IrReceiver.decodedIRData.address == 0xC7EA &&
                 IrReceiver.decodedIRData.command == 0x19) {
            // If already in lane 1 (alt context), repeated request toggles visibility
            if (currentLane == 1) {
                Serial.println("IR: Up arrow pressed - toggling route2 visibility");
                toggleRouteVisibility();
            } else {
                Serial.print("IR: Up arrow pressed - switching from lane ");
                Serial.print(currentLane);
                Serial.println(" to lane 1");
                currentLane = 1;
                lastRouteUpdateMillis = millis();  // Reset timeout for manual switch
                // Provide visual feedback by refreshing display
                updateBoardState();
            }
        }
        // 0xC7EA Command: 0x33 == down arrow == select lane ZERO
        else if (IrReceiver.decodedIRData.protocol == NEC &&
                 IrReceiver.decodedIRData.address == 0xC7EA &&
                 IrReceiver.decodedIRData.command == 0x33) {
            // If already in lane 0 (normal context), repeated request toggles visibility
            if (currentLane == 0) {
                Serial.println("IR: Down arrow pressed - toggling route1 visibility");
                toggleRouteVisibility();
            } else {
                Serial.print("IR: Down arrow pressed - switching from lane ");
                Serial.print(currentLane);
                Serial.println(" to lane 0");
                currentLane = 0;
                // Provide visual feedback by refreshing display
                updateBoardState();
            }
        }
        //0xC7EA Command: 0x3 == home button, go to single mode, keep route 1
        else if (IrReceiver.decodedIRData.protocol == NEC &&
                 IrReceiver.decodedIRData.address == 0xC7EA &&
                 IrReceiver.decodedIRData.command == 0x3) {
            Serial.println("IR: Home pressed");
            clearBoardExceptRoute1();
        }
        // 0xC7EA Command: 0x78 => return/back button, mirror current lane route
        else if (IrReceiver.decodedIRData.protocol == NEC &&
                 IrReceiver.decodedIRData.address == 0xC7EA &&
                 IrReceiver.decodedIRData.command == 0x78) {
            Serial.println("IR: Return pressed, mirroring current lane");
            mirrorCurrentLane();
        }
        //0x17 ==> power button, toggle visibility of current lane
        else if (IrReceiver.decodedIRData.protocol == NEC &&
                 IrReceiver.decodedIRData.address == 0xC7EA &&
                 IrReceiver.decodedIRData.command == 0x17) {
            Serial.println("IR: Power pressed");
            toggleRouteVisibility();
        }
        IrReceiver.resume();
    }
}

void loop() {
  // Handle serial communication for API level queries
  if (Serial.available() > 0) {
    int inByte = Serial.read();
    if (inByte == 4) {
      // Respond with API level
      Serial.write(4);
      Serial.write(API_LEVEL);
    }
  }
  
  // Poll BLE events
  BLE.poll();

  // Process deferred LED updates (CRITICAL: never call updateBoardState from callbacks)
  if (pendingLEDUpdate) {
    pendingLEDUpdate = false;
    updateBoardState();
  }

  // Poll ESP32 UART link for any pending pre-parsed route 2 updates
    pollESP32RouteUART();

  unsigned long now = millis();

  // Lane timeout: when alt lane (1) is selected, switch back to primary
  // lane after 15s without any new route data. Does not clear route2.
  // Fires once, then disarms until the next route update or manual lane switch.
  if (currentLane == 1 && lastRouteUpdateMillis != 0) {
    if (now - lastRouteUpdateMillis > 15000UL) {
      Serial.println("[TIMEOUT] No new routes on lane 1 for 15s; switching back to lane 0");
      currentLane = 0;
      lastRouteUpdateMillis = 0;  // Disarm so it doesn't fire every loop
    }
  }

  // Ensure BLE is always advertising for quick device switching (throttled)
  {
    static unsigned long lastAdvMillis = 0;
    if (!deviceConnected && (now - lastAdvMillis > 3000UL)) {
      BLE.advertise();
      lastAdvMillis = now;
    }
  }

  // CRITICAL: Check IR multiple times before animation to improve responsiveness
  // IR decoding requires precise timing and can miss signals if FastLED.show() blocks
  checkIRRemote();
  if (irRecentlyActive && (now - lastIRCheckMillis > IR_COOLDOWN_MS)) {
    irRecentlyActive = false;
  }
  
  // LED display is updated automatically when routes are received
  // and during overlap animation when both routes are active.
  // Continuously update animation when both routes are on and overlapping
  // CRITICAL: Pause animation completely during IR activity to prevent missed signals
  if (route1On && route2On && hasOverlap && !pendingLEDUpdate && !irRecentlyActive) {
    if (now - lastAnimUpdateMillis >= ANIM_UPDATE_INTERVAL_MS) {
      // Safe to update - no recent IR activity detected
      updateBoardState();
      lastAnimUpdateMillis = now;
    }
  }
  
  // Check IR again after potential animation update
  checkIRRemote();
  
  delay(DELAY_TIME);
}