#include <ArduinoBLE.h>
#include <vector>
#include <FastLED.h>

/*
 * FAKE AURORA BOARD - Arduino R4 WiFi Climbing Training Board Simulator
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
 * - Two modes: Basic (single route) or Dual (two routes simultaneously)
 * - Dual mode uses green-biased colors for route differentiation
 * 
 * DATA FORMAT:
 * - Packets: [START][LENGTH][CHECKSUM][TYPE_MARKER][PACKET_TYPE][HOLD_DATA...][END]
 * - Hold data: 3 bytes per hold (position_low, position_high, color_encoded)
 * - Colors: 8-bit compressed RGB (3R:3G:2B bits)
 * 
 * USE CASE: Indoor climbing training with app-controlled route lighting
 */

#define DISPLAY_NAME "Tension Board 2"
#define API_LEVEL 3

// Feature toggle: Set to true for dual-route functionality, false for basic single-route mode
#define DUAL_ROUTE_MODE false

// Aurora Board protocol UUIDs
#define ADVERTISING_SERVICE_UUID "4488B571-7806-4DF6-BCFF-A2897E4953FF"  // Aurora Board advertising service
#define DATA_TRANSFER_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART Service
#define DATA_TRANSFER_CHARACTERISTIC "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write characteristic
#define NOTIFY_CHARACTERISTIC "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify characteristic

#define LED_PIN 13  // LED pin for Arduino R4 WiFi (changed from 25 to 13)
#define NUM_LEDS 500  // Replace with the number of LEDs in your strip
#define DELAY_TIME 10  // Delay between LED movements (in milliseconds)

// Structure to store hold information
struct Hold {
    uint16_t position;
    uint8_t r, g, b;
    String colorName;
};

CRGB leds[NUM_LEDS];

// ArduinoBLE objects (different from ESP32 BLE)
BLEService advertisingService(ADVERTISING_SERVICE_UUID);
BLEService dataTransferService(DATA_TRANSFER_SERVICE_UUID);
BLECharacteristic dataTransferCharacteristic(DATA_TRANSFER_CHARACTERISTIC, BLEWrite, 512);
BLECharacteristic notifyCharacteristic(NOTIFY_CHARACTERISTIC, BLENotify | BLERead, 512);

bool deviceConnected = false;

// Global route storage for LED display
#if DUAL_ROUTE_MODE
std::vector<Hold> route1Holds;     // First route storage
std::vector<Hold> route2Holds;     // Second route storage
#else
std::vector<Hold> currentHolds;    // Single route storage
#endif

// Helper function to decode RGB color from API level 3 format
void decodeColor(uint8_t colorByte, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = ((colorByte >> 5) & 0x07) * 255 / 7;  // 3 bits for red
    g = ((colorByte >> 2) & 0x07) * 255 / 7;  // 3 bits for green
    b = (colorByte & 0x03) * 255 / 3;         // 2 bits for blue
}

// Helper function to get color name
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

// Helper function to calculate checksum
uint8_t calculateChecksum(const std::vector<uint8_t>& data) {
    uint8_t sum = 0;
    for (uint8_t byte : data) {
        sum = (sum + byte) & 255;
    }
    return (~sum) & 255;
}

// Helper function to apply green bias to colors for dual-route mode
// Used to differentiate Route 2 from Route 1 with a green-tinted color scheme
void applyGreenBias(uint8_t& r, uint8_t& g, uint8_t& b) {
    // Boost green component while slightly reducing red and blue
    // This creates a green-tinted version of the original color
    g = min(255, (int)g + 60);           // Increase green (+60)
    r = max(0, (int)r - 20);             // Slightly reduce red (-20)
    b = max(0, (int)b - 20);             // Slightly reduce blue (-20)
}

/*
 * LED Control System
 * 
 * Controls a WS2811 LED strip of 500 LEDs connected to pin 13.
 * Each hold position corresponds to an LED index (0-499).
 * 
 * Basic Mode: Displays single route with original colors
 * Dual Mode: Displays both routes simultaneously
 *   - Route 1: Original colors
 *   - Route 2: Green-biased colors
 * 
 * LEDs are updated automatically when complete routes are received.
 */

// LED Control Functions
// Clear all LEDs to black
void clearAllLEDs() {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
    }
    FastLED.show();
}

// Display a single route on the LED strip
void displayRoute(const std::vector<Hold>& route) {
    for (const Hold& hold : route) {
        if (hold.position < NUM_LEDS) {  // Safety check
            leds[hold.position] = CRGB(hold.r, hold.g, hold.b);
        }
    }
}

// Update LED display based on current mode
void updateLEDDisplay() {
    // Clear all LEDs first
    clearAllLEDs();
    
    #if DUAL_ROUTE_MODE
    // Dual route mode: Display both routes
    int totalLEDs = 0;
    if (!route1Holds.empty()) {
        displayRoute(route1Holds);
        totalLEDs += route1Holds.size();
    }
    if (!route2Holds.empty()) {
        displayRoute(route2Holds);
        totalLEDs += route2Holds.size();
    }
    Serial.print("LED Display Updated - Total LEDs lit: ");
    Serial.println(totalLEDs);
    #else
    // Basic mode: Display single route
    if (!currentHolds.empty()) {
        displayRoute(currentHolds);
        Serial.print("LED Display Updated - LEDs lit: ");
        Serial.println(currentHolds.size());
    }
    #endif
    
    FastLED.show();
}

// BLE event handlers for ArduinoBLE
void onBLEConnected(BLEDevice central) {
    deviceConnected = true;
    Serial.print("Device connected: ");
    Serial.println(central.address());
}

void onBLEDisconnected(BLEDevice central) {
    deviceConnected = false;
    Serial.print("Device disconnected: ");
    Serial.println(central.address());
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
std::vector<Hold> tempHolds;       // Temporary storage for incoming route
#if DUAL_ROUTE_MODE
bool activeRoute = true;           // true = route1, false = route2
#endif
bool isNewClimb = true;            // Flag to track if this is a new climb sequence
std::vector<uint8_t> packetBuffer; // Buffer for incomplete packets (handles fragmentation)

void onDataTransferCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
    // Get written data
    const uint8_t* data = characteristic.value();
    int length = characteristic.valueLength();
    
    if (length > 0) {
        // Debug output: Show raw received bytes
        Serial.print("Received command: ");
        for (int i = 0; i < length; i++) {
          Serial.print((int)data[i]);
          Serial.print(" ");
        }
        Serial.println();
        
        // Accumulate received bytes in buffer to handle packet fragmentation
        // BLE may split packets across multiple writes
        for (int i = 0; i < length; i++) {
            packetBuffer.push_back(data[i]);
        }
        
        // Process all complete packets in the buffer
        while (packetBuffer.size() >= 5) {  // Minimum packet size: START + LENGTH + CHECKSUM + TYPE_MARKER + PACKET_TYPE
            // Packet parsing: Find the start marker (0x01)
            size_t startIdx = 0;
            while (startIdx < packetBuffer.size() && packetBuffer[startIdx] != 1) {
                startIdx++;
            }
            
            // Check if we have enough data for packet header
            if (startIdx >= packetBuffer.size() - 4) {
                // Not enough data for a complete packet header
                break;
            }
            
            // Extract packet length and validate we have complete packet
            uint8_t packetLength = packetBuffer[startIdx + 1];
            if (startIdx + packetLength + 4 >= packetBuffer.size()) {
                // Not enough data for the full packet (including END marker)
                break;
            }
            
            // Validate packet structure: Check type marker (should be 0x02)
            if (packetBuffer[startIdx + 3] != 2) {
                Serial.println("Invalid packet format - second byte should be 2");
                // Remove invalid byte and try again
                packetBuffer.erase(packetBuffer.begin(), packetBuffer.begin() + startIdx + 1);
                continue;
            }
            
            // Extract and validate packet type
            uint8_t packetType = packetBuffer[startIdx + 4];
            char packetTypeChar = (char)packetType;
            
            // Verify packet type is valid for API level 3
            if (packetTypeChar != 'R' && packetTypeChar != 'S' && 
                packetTypeChar != 'Q' && packetTypeChar != 'T') {
                Serial.println("Invalid packet type for API level 3");
                // Remove invalid byte and try again
                packetBuffer.erase(packetBuffer.begin(), packetBuffer.begin() + startIdx + 1);
                continue;
            }
            
            // Handle route state management
            // 'T' packets indicate a new test route, clear previous holds
            if (packetTypeChar == 'T') {
                #if DUAL_ROUTE_MODE
                tempHolds.clear();  // Clear temporary storage for new route
                #else
                currentHolds.clear();  // Basic mode: clear current route
                #endif
                isNewClimb = true;
            }
            
            // Checksum validation
            // Extract data portion (from packet type to end of data)
            std::vector<uint8_t> dataBytes;
            for (size_t i = startIdx + 4; i < startIdx + packetLength + 3; i++) {
                dataBytes.push_back(packetBuffer[i]);
            }
            uint8_t calculatedChecksum = calculateChecksum(dataBytes);
            uint8_t receivedChecksum = packetBuffer[startIdx + 2];
            
            // Debug output: Show packet details
            Serial.println("\nDecoded packet:");
            Serial.print("Length: "); Serial.println(packetLength);
            Serial.print("Checksum: "); Serial.println(receivedChecksum);
            Serial.print("Calculated checksum: "); Serial.println(calculatedChecksum);
            Serial.print("Packet type: "); Serial.println(packetTypeChar);
            
            // Decode hold data (3 bytes per hold for API level 3)
            // Format: [position_low][position_high][color_encoded]
            for (size_t i = startIdx + 5; i < startIdx + packetLength + 3; i += 3) {
                if (i + 2 < packetBuffer.size()) {
                    // Reconstruct 16-bit position from little-endian bytes
                    uint16_t position = (packetBuffer[i+1] << 8) + packetBuffer[i];
                    
                    // Decode compressed RGB color from single byte
                    uint8_t r, g, b;
                    decodeColor(packetBuffer[i+2], r, g, b);
                    String colorName = getColorName(r, g, b);
                    
                    // Store hold information
                    Hold h = {position, r, g, b, colorName};
                    #if DUAL_ROUTE_MODE
                    tempHolds.push_back(h);
                    #else
                    currentHolds.push_back(h);
                    #endif
                    
                    // Debug output: Show decoded hold
                    Serial.print("Hold at position "); Serial.print(position);
                    Serial.print(" with color "); Serial.print(colorName);
                    Serial.print(" RGB("); Serial.print(r);
                    Serial.print(","); Serial.print(g);
                    Serial.print(","); Serial.print(b);
                    Serial.println(")");
                }
            }
            
            // Route completion handling
            // 'S' (Set) and 'T' (Test) packets indicate complete route transmission
            if (packetTypeChar == 'S' || packetTypeChar == 'T') {
                #if DUAL_ROUTE_MODE
                // Dual route mode: Store completed route and alternate between route slots
                if (activeRoute) {
                    // Route 1: Store with original colors
                    route1Holds = tempHolds;
                    Serial.println("\nRoute 1 stored (original colors):");
                } else {
                    // Route 2: Store with green-biased colors
                    route2Holds = tempHolds;
                    // Apply green bias to all holds in route 2
                    for (Hold& h : route2Holds) {
                        applyGreenBias(h.r, h.g, h.b);
                        h.colorName = getColorName(h.r, h.g, h.b);  // Update color name
                    }
                    Serial.println("\nRoute 2 stored (green-biased colors):");
                }
                
                // Show the route that was just stored
                for (const Hold& h : tempHolds) {
                    Serial.print("Position "); Serial.print(h.position);
                    Serial.print(": "); Serial.println(h.colorName);
                }
                
                // Toggle to next route slot for the next sequence
                activeRoute = !activeRoute;
                
                // Show complete dual route summary
                Serial.println("\n=== DUAL ROUTE SUMMARY ===");
                if (!route1Holds.empty()) {
                    Serial.println("Route 1:");
                    for (const Hold& h : route1Holds) {
                        Serial.print("  Position "); Serial.print(h.position);
                        Serial.print(": "); Serial.println(h.colorName);
                    }
                }
                if (!route2Holds.empty()) {
                    Serial.println("Route 2:");
                    for (const Hold& h : route2Holds) {
                        Serial.print("  Position "); Serial.print(h.position);
                        Serial.print(": "); Serial.println(h.colorName);
                    }
                }
                Serial.println("========================");
                
                #else
                // Basic mode: Store route from tempHolds to currentHolds
                currentHolds = tempHolds;
                Serial.println("\nComplete climb summary:");
                for (const Hold& h : currentHolds) {
                    Serial.print("Position "); Serial.print(h.position);
                    Serial.print(": "); Serial.println(h.colorName);
                }
                #endif
                
                // Clear temporary storage for next route
                tempHolds.clear();
                
                // Update LED display with new route data
                updateLEDDisplay();
                Serial.println();
            }
            
            // Remove processed packet from buffer
            // This advances the buffer past the current packet
            packetBuffer.erase(packetBuffer.begin(), packetBuffer.begin() + startIdx + packetLength + 4);
        }
        
        // Buffer overflow protection
        // Clear buffer if it grows too large (prevents memory issues)
        if (packetBuffer.size() > 1000) {
            Serial.println("Buffer overflow, clearing");
            packetBuffer.clear();
        }
    }
}

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Wait for serial port to connect

  FastLED.addLeds<WS2811, LED_PIN, RGB>(leds, NUM_LEDS);
  clearAllLEDs();  // Initialize all LEDs to off
  
  // Send initial API level
  Serial.write(4);
  Serial.write(API_LEVEL);

  char boardName[64];  // Increased buffer size for safety
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
  
  // LED display is updated automatically when routes are received
  // No need for continuous updates unless you want animations
  
  delay(DELAY_TIME);
}