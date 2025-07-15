#include <ArduinoBLE.h>
#include <vector>
#include <FastLED.h>

/*
 * Tension Board 2 Custom Problem Receiver - Arduino R4 WiFi Climbing Training Board Simulator
 * ============================================================
 * 
 * PURPOSE: Simulates an Aurora Board climbing training device using Arduino R4 WiFi + LED strip
 * 
 * HARDWARE: 
 * - Arduino R4 WiFi with BLE capability
 * - WS2811 LED strip (500 LEDs on pin 6)
 * - Mode toggle button (pin 2 with internal pull-up resistor)
 * - Each LED represents a climbing hold position (0-499)
 * 
 * COMMUNICATION:
 * - BLE peripheral device advertising as "Tension Board 2@3"
 * - Implements Aurora Board protocol (API Level 3)
 * - Uses Nordic UART Service UUIDs for data transfer via ArduinoBLE
 * 
 * FUNCTIONALITY:
 * - Receives climbing route data via BLE packets
 * - Displays routes as colored LEDs on strip
 * - Runtime mode switching: Single (one route) or Dual (two routes simultaneously)
 * - Button press toggles between modes with debouncing
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

// Runtime mode toggle: Button on MODE_PIN toggles between single and dual route modes
bool dualRouteMode = false;  // Start in single route mode

// Aurora Board protocol UUIDs
#define ADVERTISING_SERVICE_UUID "4488B571-7806-4DF6-BCFF-A2897E4953FF"  // Aurora Board advertising service (required for app discovery)
#define DATA_TRANSFER_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART Service
#define DATA_TRANSFER_CHARACTERISTIC "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write characteristic
#define NOTIFY_CHARACTERISTIC "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify characteristic

#define MODE_PIN 2   // pin to toggle between single and dual route mode (Arduino R4 WiFi)
#define LED_PIN 6    // LED pin to connect RGB led string to (ws2811) - Arduino R4 WiFi
#define NUM_LEDS 500  // 
#define DELAY_TIME 3  // Delay between LED movements (in milliseconds)

// Constants for color processing and debouncing
#define GREEN_BIAS_INCREASE 60    // Amount to increase green component in dual mode
#define RED_BLUE_BIAS_DECREASE 20 // Amount to decrease red/blue in dual mode
#define DEBOUNCE_DELAY_MS 50      // Button debounce delay in milliseconds

// Structure to store hold information
struct Hold {
    uint16_t position;
    uint8_t r, g, b;
    String colorName;
};

CRGB leds[NUM_LEDS];

// ArduinoBLE objects (matching ESP32 structure)
BLEService* pAdvertisingService = nullptr;
BLEService* pDataTransferService = nullptr;
BLECharacteristic* pDataTransferCharacteristic = nullptr;
BLECharacteristic* pNotifyCharacteristic = nullptr;

// Create the actual service and characteristic objects
BLEService advertisingService(ADVERTISING_SERVICE_UUID);
BLEService dataTransferService(DATA_TRANSFER_SERVICE_UUID);
BLECharacteristic dataTransferCharacteristic(DATA_TRANSFER_CHARACTERISTIC, BLEWrite | BLEWriteWithoutResponse, 244);
BLECharacteristic notifyCharacteristic(NOTIFY_CHARACTERISTIC, BLERead | BLENotify, 244);
BLEDescriptor notifyDescriptor("2902", "");

bool deviceConnected = false;

// Global route storage for LED display (always available for runtime mode switching)
std::vector<Hold> route1Holds;     // First route storage
std::vector<Hold> route2Holds;     // Second route storage

// Track which LED positions are currently lit for efficient updates
bool ledStates[NUM_LEDS] = {false}; // Track which LEDs are currently on (initialized to false)

// Global variables for packet processing (moved up for event handlers)
std::vector<Hold> tempHolds;       // Temporary storage for incoming route
bool activeRoute = true;           // true = route1, false = route2 (for dual mode)
bool isNewClimb = true;            // Flag to track if this is a new climb sequence
std::vector<uint8_t> packetBuffer; // Buffer for incomplete packets (handles fragmentation)
size_t bufferReadIndex = 0;        // Index for efficient buffer reading (avoids expensive erase operations)

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
    g = min(255, (int)g + GREEN_BIAS_INCREASE);           // Increase green
    r = max(0, (int)r - RED_BLUE_BIAS_DECREASE);          // Slightly reduce red
    b = max(0, (int)b - RED_BLUE_BIAS_DECREASE);          // Slightly reduce blue
}

// Data validation functions
bool isValidPosition(uint16_t position) {
    return position < NUM_LEDS;
}

bool isValidRGBValue(uint8_t value) {
    return true; // uint8_t is inherently 0-255, so always valid
}

bool isValidHold(uint16_t position, uint8_t r, uint8_t g, uint8_t b) {
    return isValidPosition(position) && isValidRGBValue(r) && isValidRGBValue(g) && isValidRGBValue(b);
}

/*
 * LED Control System
 * 
 * Controls a WS2811 LED strip of 500 LEDs connected to pin 6.
 * Each hold position corresponds to an LED index (0-499).
 * 
 * Single Mode: Displays only route1Holds with original colors
 * Dual Mode: Displays both routes simultaneously
 *   - Route 1: Original colors
 *   - Route 2: Green-biased colors
 * 
 * LEDs are updated automatically when:
 * - Complete routes are received via BLE
 * - Mode is toggled via button press
 */

// LED Control Functions
// Clear only the LEDs that are currently on
void clearActiveLEDs() {
    for (int i = 0; i < NUM_LEDS; i++) {
        if (ledStates[i]) {
            leds[i] = CRGB::Black;
            ledStates[i] = false;
        }
    }
}

// Initialize all LEDs to black and reset state tracking
void initializeLEDs() {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
        ledStates[i] = false;
    }
    FastLED.show();
}

// Set an LED to a specific color and update state tracking
void setLED(uint16_t position, uint8_t r, uint8_t g, uint8_t b) {
    if (isValidPosition(position)) {
        leds[position] = CRGB(r, g, b);
        ledStates[position] = true;
    } else {
        Serial.print("ERROR: Attempting to set invalid LED position ");
        Serial.println(position);
    }
}

// Display a single route on the LED strip (efficient version)
void displayRoute(const std::vector<Hold>& route) {
    for (const Hold& hold : route) {
        setLED(hold.position, hold.r, hold.g, hold.b);
    }
}

// Update LED display based on current mode (efficient version)
void updateLEDDisplay() {
    // Clear only the LEDs that are currently on
    clearActiveLEDs();
    
    if (dualRouteMode) {
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
        Serial.print("LED Display Updated (Dual Mode) - Total LEDs lit: ");
        Serial.println(totalLEDs);
    } else {
        // Single route mode: Display only route 1
        if (!route1Holds.empty()) {
            displayRoute(route1Holds);
            Serial.print("LED Display Updated (Single Mode) - LEDs lit: ");
            Serial.println(route1Holds.size());
        }
    }
    
    FastLED.show();
}

// BLE event handlers for ArduinoBLE
void blePeripheralConnectHandler(BLEDevice central) {
  deviceConnected = true;
  Serial.print("Connected to central: ");
  Serial.println(central.address());
  Serial.println("Connection successful - waiting for service discovery");
}

void blePeripheralDisconnectHandler(BLEDevice central) {
  deviceConnected = false;
  Serial.print("Disconnected from central: ");
  Serial.println(central.address());
  Serial.println("Connection lost - check if app found expected characteristics");
}

/*
 * BLE Characteristic Callback Handler
 * 
 * This class handles incoming data from BLE clients and processes Aurora Board protocol packets.
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
 * - END: 0x03 (1 byte)
 * 
 * Packet Types:
 * - 'R': Route packet (partial data)
 * - 'S': Set packet (complete route data)
 * - 'Q': Query packet
 * - 'T': Test packet (single route, clears previous holds)
 * 
 * Operating Modes (controlled by runtime dualRouteMode variable):
 * - Single Mode (false): Only route1Holds used, route2Holds cleared
 * - Dual Route Mode (true): Stores two routes alternately with different color schemes
 *   Route 1: Original colors
 *   Route 2: Green-biased colors (enhanced green, reduced red/blue)
 *   Alternation: Route1 -> Route2 -> Route1 -> Route2...
 * - Mode Toggle: Button on MODE_PIN switches between modes at runtime
 */
// (Variables moved up above for event handlers)

// Packet processing function (extracted from old callback class)
void processPacketData(const uint8_t* data, size_t length) {
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
        // Check buffer size before adding data (Arduino R4 WiFi memory constraint)
        if (packetBuffer.size() + length > 800) {  // Conservative limit for Arduino R4 WiFi
            Serial.println("WARNING: Packet buffer near capacity, clearing");
            packetBuffer.clear();
            bufferReadIndex = 0;
        }
        
        for (int i = 0; i < length; i++) {
            packetBuffer.push_back(data[i]);
        }
        
        // Process all complete packets in the buffer
        while (bufferReadIndex < packetBuffer.size() && (packetBuffer.size() - bufferReadIndex) >= 5) {  // Minimum packet size
            // Packet parsing: Find the start marker (0x01) from current read position
            size_t startIdx = bufferReadIndex;
            while (startIdx < packetBuffer.size() && packetBuffer[startIdx] != 1) {
                startIdx++;
            }
            
            // Check if we have enough data for packet header
            if (startIdx >= packetBuffer.size() - 4) {
                // Not enough data for a complete packet header
                break;
            }
            
            // Extract packet length and validate we have complete packet
            uint8_t length = packetBuffer[startIdx + 1];
            if (startIdx + length + 4 > packetBuffer.size()) {
                // Not enough data for the full packet (including END marker)
                break;
            }
            
            // Validate packet structure: Check type marker (should be 0x02)
            if (packetBuffer[startIdx + 3] != 2) {
                Serial.println("Invalid packet format - second byte should be 2");
                // Skip invalid byte and try again
                bufferReadIndex = startIdx + 1;
                continue;
            }
            
            // Extract and validate packet type
            uint8_t packetType = packetBuffer[startIdx + 4];
            char packetTypeChar = (char)packetType;
            
            // Verify packet type is valid for API level 3
            if (packetTypeChar != 'R' && packetTypeChar != 'S' && 
                packetTypeChar != 'Q' && packetTypeChar != 'T') {
                Serial.println("Invalid packet type for API level 3");
                // Skip invalid byte and try again
                bufferReadIndex = startIdx + 1;
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
            for (size_t i = startIdx + 4; i < startIdx + length + 3; i++) {
                dataBytes.push_back(packetBuffer[i]);
            }
            uint8_t calculatedChecksum = calculateChecksum(dataBytes);
            uint8_t receivedChecksum = packetBuffer[startIdx + 2];
            
            // Debug output: Show packet details
            Serial.println("\nDecoded packet:");
            Serial.print("Length: "); Serial.println(length);
            Serial.print("Checksum: "); Serial.println(receivedChecksum);
            Serial.print("Calculated checksum: "); Serial.println(calculatedChecksum);
            Serial.print("Packet type: "); Serial.println(packetTypeChar);
            
            // Validate checksum - reject packet if invalid
            if (calculatedChecksum != receivedChecksum) {
                Serial.println("ERROR: Checksum mismatch - packet corrupted, skipping");
                // Skip invalid packet and try again
                bufferReadIndex = startIdx + 1;
                continue;
            }
            
            // Decode hold data (3 bytes per hold for API level 3)
            // Format: [position_low][position_high][color_encoded]
            for (size_t i = startIdx + 5; i < startIdx + length + 3; i += 3) {
                if (i + 2 < packetBuffer.size()) {
                    // Reconstruct 16-bit position from little-endian bytes
                    uint16_t position = (packetBuffer[i+1] << 8) + packetBuffer[i];
                    
                    // Decode compressed RGB color from single byte
                    uint8_t r, g, b;
                    decodeColor(packetBuffer[i+2], r, g, b);
                    
                    // Validate hold data before storing
                    if (isValidHold(position, r, g, b)) {
                        String colorName = getColorName(r, g, b);
                        
                        // Store hold information
                        Hold h = {position, r, g, b, colorName};
                        tempHolds.push_back(h);  // Always use tempHolds for incoming data
                        
                        // Debug output: Show decoded hold
                        Serial.print("Hold at position "); Serial.print(position);
                        Serial.print(" with color "); Serial.print(colorName);
                        Serial.print(" RGB("); Serial.print(r);
                        Serial.print(","); Serial.print(g);
                        Serial.print(","); Serial.print(b);
                        Serial.println(")");
                    } else {
                        // Invalid hold data - log error and skip
                        Serial.print("ERROR: Invalid hold data - Position: "); Serial.print(position);
                        Serial.print(" RGB("); Serial.print(r);
                        Serial.print(","); Serial.print(g);
                        Serial.print(","); Serial.print(b);
                        Serial.println(") - Skipping this hold");
                    }
                }
            }
            
            // Route completion handling
            // 'S' (Set) and 'T' (Test) packets indicate complete route transmission
            if (packetTypeChar == 'S' || packetTypeChar == 'T') {
                if (dualRouteMode) {
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
                    
                } else {
                    // Single route mode: Store route in route1Holds only
                    route1Holds = tempHolds;
                    route2Holds.clear();  // Clear route 2 in single mode
                    Serial.println("\nSingle route stored:");
                    for (const Hold& h : route1Holds) {
                        Serial.print("Position "); Serial.print(h.position);
                        Serial.print(": "); Serial.println(h.colorName);
                    }
                }
                
                // Clear temporary storage for next route
                tempHolds.clear();
                
                // Update LED display with new route data
                updateLEDDisplay();
                Serial.println();
            }
            
            // Advance read index past the processed packet
            bufferReadIndex = startIdx + length + 4;
        }
        
        // Buffer management and overflow protection (Arduino R4 WiFi memory constraints)
        // If we've processed a significant portion of the buffer, compact it
        if (bufferReadIndex > 400 && bufferReadIndex < packetBuffer.size()) {
            // Move unprocessed data to beginning of buffer
            packetBuffer.erase(packetBuffer.begin(), packetBuffer.begin() + bufferReadIndex);
            bufferReadIndex = 0;
        }
        
        // Clear buffer if it grows too large (prevents memory issues on Arduino R4 WiFi)
        if (packetBuffer.size() > 800) {
            Serial.println("Buffer overflow, clearing");
            packetBuffer.clear();
            bufferReadIndex = 0;
        }
      }
}

// BLE characteristic event handler
void dataTransferCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
  Serial.print("Data received from central: ");
  Serial.print(central.address());
  Serial.print(" (");
  Serial.print(characteristic.valueLength());
  Serial.println(" bytes)");
  
  const uint8_t* data = pDataTransferCharacteristic->value();
  size_t length = pDataTransferCharacteristic->valueLength();
  
  processPacketData(data, length);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Wait for serial port to connect

  // Initialize mode toggle button
  pinMode(MODE_PIN, INPUT_PULLUP);  // Arduino R4 WiFi uses INPUT_PULLUP
  
  FastLED.addLeds<WS2811, LED_PIN, RGB>(leds, NUM_LEDS);
  initializeLEDs();  // Initialize all LEDs to off and reset state tracking
  
  // Send initial API level
  Serial.write(4);
  Serial.write(API_LEVEL);

  char boardName[64];  // Increased buffer size for safety
  snprintf(boardName, sizeof(boardName), "%s@%d", DISPLAY_NAME, API_LEVEL);

  Serial.println("Initializing BLE...");
  
  // Initialize ArduinoBLE
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }
  
  // Set device name
  BLE.setLocalName(boardName);
  
  // Assign pointers (matching ESP32 structure)
  pAdvertisingService = &advertisingService;
  pDataTransferService = &dataTransferService;
  pDataTransferCharacteristic = &dataTransferCharacteristic;
  pNotifyCharacteristic = &notifyCharacteristic;
  
  // Add services
  BLE.addService(advertisingService);
  BLE.addService(dataTransferService);
  
  // Add characteristics to data transfer service
  dataTransferService.addCharacteristic(dataTransferCharacteristic);
  dataTransferService.addCharacteristic(notifyCharacteristic);
  
  // Initialize the notify characteristic
  notifyCharacteristic.writeValue("ready");
  
  // Set event handlers
  dataTransferCharacteristic.setEventHandler(BLEWritten, dataTransferCharacteristicWritten);
  BLE.setEventHandler(BLEConnected, blePeripheralConnectHandler);
  BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);
  
  // Start advertising with the discovery service
  BLE.setAdvertisedService(advertisingService);
  BLE.advertise();
  
  Serial.println("BLE device ready");
  Serial.print("Device name: ");
  Serial.println(boardName);
  Serial.println("Services initialized:");
  Serial.println("  - Advertising Service: " + String(ADVERTISING_SERVICE_UUID));
  Serial.println("  - Data Transfer Service: " + String(DATA_TRANSFER_SERVICE_UUID));
  Serial.println("  - Write Characteristic: " + String(DATA_TRANSFER_CHARACTERISTIC));
  Serial.println("  - Notify Characteristic: " + String(NOTIFY_CHARACTERISTIC));
  Serial.println("Waiting for connections...");
}

void loop() {
  // Poll ArduinoBLE for events
  BLE.poll();
  
  // Handle serial communication for API level queries
  if (Serial.available() > 0) {
    int inByte = Serial.read();
    if (inByte == 4) {
      // Respond with API level
      Serial.write(4);
      Serial.write(API_LEVEL);
    }
  }
  
  // Handle mode toggle with proper debouncing
  // Note: Using INPUT_PULLUP so button logic is inverted (LOW when pressed)
  static bool lastStableButtonState = true;  // Start with HIGH (not pressed)
  static bool lastReadButtonState = true;
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = DEBOUNCE_DELAY_MS;
  
  bool currentButtonState = digitalRead(MODE_PIN);
  
  // If button state changed, reset debounce timer
  if (currentButtonState != lastReadButtonState) {
    lastDebounceTime = millis();
  }
  
  // If button state has been stable for debounce period
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // If the stable state has changed from our last stable state
    if (currentButtonState != lastStableButtonState) {
      lastStableButtonState = currentButtonState;
      
      // On button press (falling edge for INPUT_PULLUP)
      if (!currentButtonState) {
        dualRouteMode = !dualRouteMode;
        Serial.print("Mode toggled to: ");
        Serial.println(dualRouteMode ? "Dual Route Mode" : "Single Route Mode");
        
        // Update LED display immediately to reflect mode change
        updateLEDDisplay();
      }
    }
  }
  
  lastReadButtonState = currentButtonState;
  
  delay(DELAY_TIME);
}