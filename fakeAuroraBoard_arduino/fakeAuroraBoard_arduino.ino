#include <ArduinoBLE.h>
#include <vector>
#include <FastLED.h>
#include <tuple>

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

// Device switching toggle: Set to true to continue advertising when connected (allows quick device switching)
#define CONTINUOUS_ADVERTISING true

// Aurora Board protocol UUIDs
#define ADVERTISING_SERVICE_UUID "4488B571-7806-4DF6-BCFF-A2897E4953FF"  // Aurora Board advertising service
#define DATA_TRANSFER_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART Service
#define DATA_TRANSFER_CHARACTERISTIC "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write characteristic
#define NOTIFY_CHARACTERISTIC "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify characteristic

#define LED_PIN 13  // LED pin for Arduino R4 WiFi (changed from 25 to 13)
#define NUM_LEDS 500  // Replace with the number of LEDs in your strip
#define DELAY_TIME 10  // Delay between LED movements (in milliseconds)

// Global board name buffer for BLE
char boardName[64];

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

// Helper function to apply principal colors (Route 1)
// Green	(0, 255, 80)	#00FF50	Neon green
// Blue	(0, 0, 255)	#0000FF pure blue
// Purple	(100, 0, 255)	#B400FF	Vivid violet
// Red	(255, 25, 25)	#FF3232	Bright scarlet
void applyPrincipalColors(String colorName, uint8_t& r, uint8_t& g, uint8_t& b) {
    colorName.toLowerCase();  // Case-insensitive comparison
    if (colorName == "green") { r = 0; g = 255; b = 80; }
    else if (colorName == "blue") { r = 0; g = 0; b = 255; }
    else if (colorName == "purple" || colorName == "pink") { r = 150; g = 0; b = 255; }
    else if (colorName == "red") { r = 255; g = 0; b = 0; }
    else if (colorName == "yellow") { r = 255; g = 255; b = 0; }  // Add yellow support
    else if (colorName == "white") { r = 255; g = 255; b = 255; }  // Add white support
    // If unknown color, keep original values
}

// Helper function to apply alternative colors (Route 2)
// Green	to (100, 255, 0)	#64FF00	Lime chartreuse
// Blue	to (0, 200, 255)	#00C8FF	Cyan-blue glow
// Purple	to (255, 0, 100)	#FF00C8	Hot magenta
// Red	to (255, 100, 0)	#FF6400	Vivid orange-red
void applyAltColors(String colorName, uint8_t& r, uint8_t& g, uint8_t& b) {
    colorName.toLowerCase();  // Case-insensitive comparison
    if (colorName == "green") { r = 100; g = 255; b = 0; }
    else if (colorName == "blue") { r = 0; g = 200; b = 255; }
    else if (colorName == "purple" || colorName == "pink") { r = 255; g = 0; b = 100; }
    else if (colorName == "red") { r = 255; g = 100; b = 0; }
    else if (colorName == "yellow") { r = 200; g = 255; b = 0; }  // Add yellow support
    else if (colorName == "white") { r = 255; g = 200; b = 200; }  // Add white support
    // If unknown color, keep original values
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
// on startup, show this sequence
// smoothly cycles through spectrum colors equivalent to 400nm to 700nm, taking 5 seconds to complete
// at each time step the entire board is a single color, slowly changing to the next
// then reverse it and end on purple
void startupLEDs() {
    // Validate NUM_LEDS to prevent buffer overflow
    if (NUM_LEDS <= 0) return;
    
    // Calculate number of steps needed for 5 second animation
    const int totalSteps = 50; 
    const int delayMs = 5;
    const float startWavelength = 400.0;
    const float endWavelength = 700.0;
    const float wavelengthRange = endWavelength - startWavelength;
    
    // Cycle through visible spectrum wavelengths (400nm to 700nm)
    for (int step = 0; step < totalSteps; step++) {
        // Convert step to wavelength (400-700nm)
        float wavelength = startWavelength + (step * wavelengthRange / totalSteps);
        
        // Convert wavelength to RGB using approximation
        float r = 0.0, g = 0.0, b = 0.0;
        
        if (wavelength >= 400 && wavelength < 440) {
            r = -(wavelength - 440) / (440 - 400);
            g = 0.0;
            b = 1.0;
        } else if (wavelength >= 440 && wavelength < 490) {
            r = 0.0;
            g = (wavelength - 440) / (490 - 440);
            b = 1.0;
        } else if (wavelength >= 490 && wavelength < 510) {
            r = 0.0;
            g = 1.0;
            b = -(wavelength - 510) / (510 - 490);
        } else if (wavelength >= 510 && wavelength < 580) {
            r = (wavelength - 510) / (580 - 510);
            g = 1.0;
            b = 0.0;
        } else if (wavelength >= 580 && wavelength < 645) {
            r = 1.0;
            g = -(wavelength - 645) / (645 - 580);
            b = 0.0;
        } else if (wavelength >= 645 && wavelength <= 700) {
            r = 1.0;
            g = 0.0;
            b = 0.0;
        }
        
        // Ensure RGB values are in valid range [0,1]
        r = constrain(r, 0.0, 1.0);
        g = constrain(g, 0.0, 1.0);
        b = constrain(b, 0.0, 1.0);
        
        // Scale RGB values to 0-255 range
        uint8_t red = r * 255;
        uint8_t green = g * 255; 
        uint8_t blue = b * 255;
        
        // Set all LEDs to the same color
        for (int i = 0; i < NUM_LEDS; i++) {
            leds[i] = CRGB(red, green, blue);
        }
        FastLED.show();
        delay(delayMs);
    }
    
    // Reverse the sequence
    for (int step = totalSteps - 1; step >= 0; step--) {
        // Convert step to wavelength (400-700nm)
        float wavelength = startWavelength + (step * wavelengthRange / totalSteps);
        
        // Convert wavelength to RGB using approximation
        float r = 0.0, g = 0.0, b = 0.0;
        
        if (wavelength >= 400 && wavelength < 440) {
            r = -(wavelength - 440) / (440 - 400);
            g = 0.0;
            b = 1.0;
        } else if (wavelength >= 440 && wavelength < 490) {
            r = 0.0;
            g = (wavelength - 440) / (490 - 440);
            b = 1.0;
        } else if (wavelength >= 490 && wavelength < 510) {
            r = 0.0;
            g = 1.0;
            b = -(wavelength - 510) / (510 - 490);
        } else if (wavelength >= 510 && wavelength < 580) {
            r = (wavelength - 510) / (580 - 510);
            g = 1.0;
            b = 0.0;
        } else if (wavelength >= 580 && wavelength < 645) {
            r = 1.0;
            g = -(wavelength - 645) / (645 - 580);
            b = 0.0;
        } else if (wavelength >= 645 && wavelength <= 700) {
            r = 1.0;
            g = 0.0;
            b = 0.0;
        }
        
        // Ensure RGB values are in valid range [0,1]
        r = constrain(r, 0.0, 1.0);
        g = constrain(g, 0.0, 1.0);
        b = constrain(b, 0.0, 1.0);
        
        // Scale RGB values to 0-255 range
        uint8_t red = r * 255;
        uint8_t green = g * 255; 
        uint8_t blue = b * 255;
        
        // Set all LEDs to the same color
        for (int i = 0; i < NUM_LEDS; i++) {
            leds[i] = CRGB(red, green, blue);
        }
        FastLED.show();
        delay(delayMs);
    }
    
    // Hold the last color (purple) and stay there, not changin until user says so
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(128, 0, 128);  // Purple color
    }
    FastLED.show();

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
    
    #if CONTINUOUS_ADVERTISING
    // Continue advertising to allow immediate device switching
    BLE.advertise();
    Serial.println("Ready for next user - staying visible for seamless handoff");
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
    Serial.println("Resumed BLE advertising for new connections (BLE stack restarted)");
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
                    tempHolds.push_back(h);
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
                    // Route 1: Apply principal color scheme
                    route1Holds = tempHolds;
                    for (Hold& h : route1Holds) {
                        h.colorName = getColorName(h.r, h.g, h.b);  // Get original color name
                        applyPrincipalColors(h.colorName, h.r, h.g, h.b);  // Apply principal colors
                    }
                    Serial.println("\nRoute 1 stored (principal colors):");
                } else {
                    // Route 2: Apply alternative color scheme
                    route2Holds = tempHolds;
                    for (Hold& h : route2Holds) {
                        h.colorName = getColorName(h.r, h.g, h.b);  // Get original color name
                        applyAltColors(h.colorName, h.r, h.g, h.b);  // Apply alternative colors
                    }
                    Serial.println("\nRoute 2 stored (alternative colors):");
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

  // Ensure BLE is always advertising for quick device switching
  if (!deviceConnected) {
    BLE.advertise();
    // Optional: Serial.println("Ensuring BLE advertising (loop watchdog)");
  }
  
  // LED display is updated automatically when routes are received
  // No need for continuous updates unless you want animations
  
  delay(DELAY_TIME);
}