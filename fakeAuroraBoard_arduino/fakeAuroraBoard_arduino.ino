#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <vector>
<<<<<<< Updated upstream
#include "ClimbingWallHelper.h"
=======
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
 * - Mode toggle button on pin 7 (with internal pullup)
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
 * - Mode toggleable at runtime with button press on pin 7
 * - Dual mode uses green-biased colors for route differentiation
 * 
 * DATA FORMAT:
 * - Packets: [START][LENGTH][CHECKSUM][TYPE_MARKER][PACKET_TYPE][HOLD_DATA...][END]
 * - Hold data: 3 bytes per hold (position_low, position_high, color_encoded)
 * - Colors: 8-bit compressed RGB (3R:3G:2B bits)
 * 
 * USE CASE: Indoor climbing training with app-controlled route lighting
 */
>>>>>>> Stashed changes

#define DISPLAY_NAME "Tension Board 2"
#define API_LEVEL 3

// Aurora Board protocol UUIDs
#define ADVERTISING_SERVICE_UUID "4488B571-7806-4DF6-BCFF-A2897E4953FF"  // Aurora Board advertising service
#define DATA_TRANSFER_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART Service
#define DATA_TRANSFER_CHARACTERISTIC "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write characteristic
#define NOTIFY_CHARACTERISTIC "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify characteristic

<<<<<<< Updated upstream
#define LED_PIN 2  // Built-in LED on most ESP32 dev boards

BLEServer* pServer = nullptr;
BLEService* pAdvertisingService = nullptr;
BLEService* pDataTransferService = nullptr;
BLECharacteristic* pDataTransferCharacteristic = nullptr;
BLECharacteristic* pNotifyCharacteristic = nullptr;
bool deviceConnected = false;
=======
#define LED_PIN 13  // LED pin for Arduino R4 WiFi
#define BUTTON_PIN 7  // Button pin for mode toggle (with internal pullup)
#define NUM_LEDS 500  // Replace with the number of LEDs in your strip
#define DELAY_TIME 10  // Delay between LED movements (in milliseconds)
>>>>>>> Stashed changes

// Structure to store hold information
struct Hold {
    uint16_t position;
    uint8_t r, g, b;
    String colorName;
};

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device connected");
    };

<<<<<<< Updated upstream
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Device disconnected");
      // Restart advertising
      pServer->getAdvertising()->start();
=======
// ArduinoBLE objects (different from ESP32 BLE)
BLEService advertisingService(ADVERTISING_SERVICE_UUID);
BLEService dataTransferService(DATA_TRANSFER_SERVICE_UUID);
BLECharacteristic dataTransferCharacteristic(DATA_TRANSFER_CHARACTERISTIC, BLEWrite, 512);
BLECharacteristic notifyCharacteristic(NOTIFY_CHARACTERISTIC, BLENotify | BLERead, 512);

bool deviceConnected = false;

// Runtime mode control
bool dualRouteMode = false;  // Runtime toggleable: false = single route, true = dual route
bool lastButtonState = HIGH;  // Button state tracking for edge detection
unsigned long lastDebounceTime = 0;  // Debouncing timer
const unsigned long debounceDelay = 50;  // Debounce delay in milliseconds

// Global route storage for LED display (always allocated for both modes)
std::vector<Hold> route1Holds;     // First route storage (or single route in basic mode)
std::vector<Hold> route2Holds;     // Second route storage (dual mode only)

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
>>>>>>> Stashed changes
    }
};

class CharacteristicCallbacks: public BLECharacteristicCallbacks {
    private:
        std::vector<Hold> currentHolds;
        bool isNewClimb = true;
        std::vector<uint8_t> packetBuffer;

<<<<<<< Updated upstream
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
=======
/*
 * Button Handling System
 * 
 * Simple button on pin 7 with internal pullup resistor.
 * Press button to toggle between single route and dual route modes.
 * Includes debouncing to prevent multiple triggers.
 */

void handleButtonPress() {
    int buttonReading = digitalRead(BUTTON_PIN);
    
    // Check if button state changed
    if (buttonReading != lastButtonState) {
        lastDebounceTime = millis();
    }
    
    // If enough time has passed since last state change, consider it stable
    if ((millis() - lastDebounceTime) > debounceDelay) {
        // Button state is stable - check for falling edge (button press)
        if (lastButtonState == HIGH && buttonReading == LOW) {
            // Button was pressed - toggle mode
            dualRouteMode = !dualRouteMode;
            
            // Clear routes when switching modes for clean state
            route1Holds.clear();
            route2Holds.clear();
            clearAllLEDs();
            
            // Provide feedback
            Serial.println();
            Serial.print("=== MODE CHANGED === ");
            if (dualRouteMode) {
                Serial.println("DUAL ROUTE MODE ACTIVATED");
                Serial.println("Two routes can now be displayed simultaneously");
            } else {
                Serial.println("SINGLE ROUTE MODE ACTIVATED");
                Serial.println("Only one route will be displayed");
            }
            Serial.println("Routes cleared. Ready for new route data.");
            Serial.println();
        }
    }
    
    lastButtonState = buttonReading;
}

/*
 * LED Control System
 * 
 * Controls a WS2811 LED strip of 500 LEDs connected to pin 13.
 * Each hold position corresponds to an LED index (0-499).
 * 
 * Single Mode: Displays one route in route1Holds with original colors
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
        Serial.print("LED Display Updated (DUAL MODE) - Total LEDs lit: ");
        Serial.println(totalLEDs);
    } else {
        // Single route mode: Display only route1
        if (!route1Holds.empty()) {
            displayRoute(route1Holds);
            Serial.print("LED Display Updated (SINGLE MODE) - LEDs lit: ");
            Serial.println(route1Holds.size());
        }
    }
    
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
 * - END: 0x03 (1 byte)
 * 
 * Packet Types:
 * - 'R': Route packet (partial data)
 * - 'S': Set packet (complete route data)
 * - 'Q': Query packet
 * - 'T': Test packet (single route, clears previous holds)
 * 
 * Operating Modes (controlled by dualRouteMode flag):
 * - Single Mode (false): Route storage in route1Holds, new routes overwrite previous
 * - Dual Route Mode (true): Stores two routes alternately with different color schemes
 *   Route 1: Original colors in route1Holds
 *   Route 2: Green-biased colors in route2Holds
 *   Alternation: Route1 -> Route2 -> Route1 -> Route2...
 */

// State management for climbing routes
std::vector<Hold> tempHolds;       // Temporary storage for incoming route
bool activeRoute = true;           // true = route1, false = route2 (for dual mode)
bool isNewClimb = true;            // Flag to track if this is a new climb sequence
std::vector<uint8_t> packetBuffer; // Buffer for incomplete packets (handles fragmentation)

void onDataTransferCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
    // Get written data
    const uint8_t* data = characteristic.value();
    int length = characteristic.valueLength();
    
    if (length > 0) {
        // Debug output: Show raw received bytes
>>>>>>> Stashed changes
        Serial.print("Received command: ");
        for (int i = 0; i < value.length(); i++) {
          Serial.print((int)value[i]);
          Serial.print(" ");
        }
        Serial.println();
        
        // Add received bytes to buffer
        for (int i = 0; i < value.length(); i++) {
            packetBuffer.push_back(value[i]);
        }
        
        // Try to process complete packets
        while (packetBuffer.size() >= 5) {  // Minimum packet size
            // Look for packet start (1) and end (3)
            size_t startIdx = 0;
            while (startIdx < packetBuffer.size() && packetBuffer[startIdx] != 1) {
                startIdx++;
            }
            
            if (startIdx >= packetBuffer.size() - 4) {
                // Not enough data for a complete packet
                break;
            }
            
            uint8_t length = packetBuffer[startIdx + 1];
            if (startIdx + length + 4 >= packetBuffer.size()) {
                // Not enough data for the full packet
                break;
            }
            
            // Verify packet structure
            if (packetBuffer[startIdx + 3] != 2) {
                Serial.println("Invalid packet format - second byte should be 2");
                packetBuffer.erase(packetBuffer.begin(), packetBuffer.begin() + startIdx + 1);
                continue;
            }
            
            uint8_t packetType = packetBuffer[startIdx + 4];
            char packetTypeChar = (char)packetType;
            
            // Verify packet type is valid for API level 3
            if (packetTypeChar != 'R' && packetTypeChar != 'S' && 
                packetTypeChar != 'Q' && packetTypeChar != 'T') {
                Serial.println("Invalid packet type for API level 3");
                packetBuffer.erase(packetBuffer.begin(), packetBuffer.begin() + startIdx + 1);
                continue;
            }
            
            // If this is a new climb (T packet), clear previous holds
            if (packetTypeChar == 'T') {
<<<<<<< Updated upstream
                currentHolds.clear();
=======
                if (dualRouteMode) {
                    tempHolds.clear();  // Clear temporary storage for new route
                } else {
                    route1Holds.clear();  // Single mode: clear current route
                }
>>>>>>> Stashed changes
                isNewClimb = true;
            }
            
            // Calculate checksum
            std::vector<uint8_t> dataBytes;
            for (size_t i = startIdx + 4; i < startIdx + length + 3; i++) {
                dataBytes.push_back(packetBuffer[i]);
            }
            uint8_t calculatedChecksum = ClimbingWallHelper::calculateChecksum(dataBytes);
            uint8_t receivedChecksum = packetBuffer[startIdx + 2];
            
            Serial.println("\nDecoded packet:");
            Serial.print("Length: "); Serial.println(length);
            Serial.print("Checksum: "); Serial.println(receivedChecksum);
            Serial.print("Calculated checksum: "); Serial.println(calculatedChecksum);
            Serial.print("Packet type: "); Serial.println(packetTypeChar);
            
            // Decode holds (3 bytes per hold for API level 3)
            for (size_t i = startIdx + 5; i < startIdx + length + 3; i += 3) {
                if (i + 2 < packetBuffer.size()) {
                    uint16_t position = (packetBuffer[i+1] << 8) + packetBuffer[i];
                    
                    // Map the position to our wall's layout
                    position = ClimbingWallHelper::mapHoldPosition(position);
                    
                    if (!ClimbingWallHelper::isValidHoldPosition(position)) {
                        continue;  // Skip invalid positions
                    }
                    
                    uint8_t r, g, b;
                    ClimbingWallHelper::decodeColor(packetBuffer[i+2], r, g, b);
                    
                    // Adjust brightness if needed
                    ClimbingWallHelper::adjustBrightness(r, g, b, 200);  // Example: 80% brightness
                    
                    String colorName = ClimbingWallHelper::getColorName(r, g, b);
                    
                    // Add to current holds
                    Hold h = {position, r, g, b, colorName};
<<<<<<< Updated upstream
                    currentHolds.push_back(h);
=======
                    if (dualRouteMode) {
                        tempHolds.push_back(h);
                    } else {
                        route1Holds.push_back(h);
                    }
>>>>>>> Stashed changes
                    
                    Serial.print("Hold at position "); Serial.print(position);
                    Serial.print(" with color "); Serial.print(colorName);
                    Serial.print(" RGB("); Serial.print(r);
                    Serial.print(","); Serial.print(g);
                    Serial.print(","); Serial.print(b);
                    Serial.println(")");
                }
            }
            
            // If this is the last packet (S or T), process the complete climb
            if (packetTypeChar == 'S' || packetTypeChar == 'T') {
<<<<<<< Updated upstream
                Serial.println("\nComplete climb summary:");
                for (const Hold& h : currentHolds) {
                    Serial.print("Position "); Serial.print(h.position);
                    Serial.print(": "); Serial.println(h.colorName);
                }
                
                // Add the climb to the next available bucket
                ClimbingWallHelper::addNewClimb(currentHolds);
                
                // Get all active holds from both buckets and update the LEDs
                std::vector<Hold> activeHolds = ClimbingWallHelper::getActiveBucketHolds();
                
                // TODO: Update your LED strip/matrix with the activeHolds
                // This will depend on your specific LED hardware setup
                updateLEDs(activeHolds);
=======
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
                    
                    // Clear temporary storage for next route
                    tempHolds.clear();
                } else {
                    // Single mode: Route already stored in route1Holds during packet processing
                    Serial.println("\nComplete climb summary (single mode):");
                    for (const Hold& h : route1Holds) {
                        Serial.print("Position "); Serial.print(h.position);
                        Serial.print(": "); Serial.println(h.colorName);
                    }
                }
>>>>>>> Stashed changes
                
                Serial.println();
            }
            
            // Remove processed packet from buffer
            packetBuffer.erase(packetBuffer.begin(), packetBuffer.begin() + startIdx + length + 4);
        }
        
        // Clear buffer if it gets too large
        if (packetBuffer.size() > 1000) {
            Serial.println("Buffer overflow, clearing");
            packetBuffer.clear();
        }
      }
    }

    void updateLEDs(const std::vector<Hold>& holds) {
        // TODO: Implement this based on your LED hardware
        // This function should update your physical LEDs with the hold positions and colors
        for (const Hold& hold : holds) {
            Serial.print("Updating LED at position "); Serial.print(hold.position);
            Serial.print(" with RGB("); Serial.print(hold.r);
            Serial.print(","); Serial.print(hold.g);
            Serial.print(","); Serial.print(hold.b);
            Serial.println(")");
        }
    }
};

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Wait for serial port to connect

<<<<<<< Updated upstream
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // Turn on LED to indicate power

  // Initialize the bucket system
  ClimbingWallHelper::initializeBuckets();
  
  // Set LED offset for your specific board size
  // Example: If you want all LEDs shifted by 50 positions
  ClimbingWallHelper::setLEDOffset(50);  // Adjust this value for your setup
=======
  // Initialize LED strip
  FastLED.addLeds<WS2811, LED_PIN, RGB>(leds, NUM_LEDS);
  clearAllLEDs();  // Initialize all LEDs to off
>>>>>>> Stashed changes
  
  // Initialize button with internal pullup
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Send initial API level
  Serial.write(4);
  Serial.write(API_LEVEL);

  char boardName[2 + sizeof(DISPLAY_NAME)];
  snprintf(boardName, sizeof(boardName), "%s%s%d", DISPLAY_NAME, "@", API_LEVEL);

  Serial.println("Initializing BLE...");
  Serial.println("Using UUIDs:");
  Serial.println(ADVERTISING_SERVICE_UUID);
  Serial.println(DATA_TRANSFER_SERVICE_UUID);
  Serial.println(DATA_TRANSFER_CHARACTERISTIC);
  Serial.println(NOTIFY_CHARACTERISTIC);
  
  // Initialize BLE
  BLEDevice::init(boardName);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Create services
  pAdvertisingService = pServer->createService(ADVERTISING_SERVICE_UUID);
  pDataTransferService = pServer->createService(DATA_TRANSFER_SERVICE_UUID);

  // Create characteristics
  pDataTransferCharacteristic = pDataTransferService->createCharacteristic(
    DATA_TRANSFER_CHARACTERISTIC,
    BLECharacteristic::PROPERTY_WRITE
  );
  pDataTransferCharacteristic->setCallbacks(new CharacteristicCallbacks());

  pNotifyCharacteristic = pDataTransferService->createCharacteristic(
    NOTIFY_CHARACTERISTIC,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pNotifyCharacteristic->addDescriptor(new BLE2902());

  // Start services
  pAdvertisingService->start();
  pDataTransferService->start();

  // Configure advertising
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(ADVERTISING_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();
  
  Serial.println("BLE device ready to connect");
  Serial.print("Device name: ");
  Serial.println(boardName);
  Serial.println();
  
  // Show initial mode
  Serial.println("=== FAKE AURORA BOARD READY ===");
  Serial.print("Current mode: ");
  if (dualRouteMode) {
      Serial.println("DUAL ROUTE MODE");
  } else {
      Serial.println("SINGLE ROUTE MODE");
  }
  Serial.println("Press button on pin 7 to toggle mode");
  Serial.println("Ready for route data...");
  Serial.println();
}

void loop() {
<<<<<<< Updated upstream
  // Blink LED to show device is running
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
  }

=======
  // Handle button press for mode toggle
  handleButtonPress();
  
  // Handle serial communication for API level queries
>>>>>>> Stashed changes
  if (Serial.available() > 0) {
    int inByte = Serial.read();
    if (inByte == 4) {
      // Respond with API level
      Serial.write(4);
      Serial.write(API_LEVEL);
    }
  }
  delay(10);
}