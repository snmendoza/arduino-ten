/*
 * Dual-Mode Aurora Board — Arduino R4 WiFi
 * =====================================================================
 *
 * PURPOSE: Simulates an Aurora Board climbing training device with dual-route
 *          display, BLE communication, IR remote control, and ESP32 bridge.
 *
 * HARDWARE:
 * - Arduino R4 WiFi with BLE capability
 * - WS2811 LED strip (478 LEDs on pin 13)
 * - IR receiver on pin 9
 * - ESP32 peripheral connected via Serial1 (UART)
 *
 * FILE STRUCTURE:
 * - fakeAuroraBoard_arduino.ino  — Main sketch: globals, setup, loop
 * - aurora_protocol.h            — Protocol constants and UUIDs
 * - types.h                      — Data types and function prototypes
 * - color_mapping.ino            — Color decoding and route color schemes
 * - led_display.ino              — LED rendering, animation, overlap blending
 * - ir_handler.ino               — IR remote control processing
 * - ble_handler.ino              — BLE connection and Aurora packet parsing
 * - uart_bridge.ino              — UART state machine for ESP32 communication
 */

#include <ArduinoBLE.h>
#include <vector>
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <tuple>
#include <IRremote.h>

#include "aurora_protocol.h"
#include "types.h"

// =====================================================================
// Global state
// =====================================================================

// Active lane: 0 = route1 (principal colors), 1 = route2 (alt colors)
uint8_t currentLane = 0;

// Global board name buffer for BLE
char boardName[64];

// LED array
CRGB leds[NUM_LEDS];

// ArduinoBLE objects
BLEService advertisingService(ADVERTISING_SERVICE_UUID);
BLEService dataTransferService(DATA_TRANSFER_SERVICE_UUID);
BLECharacteristic dataTransferCharacteristic(DATA_TRANSFER_CHARACTERISTIC, BLEWrite, 512);
BLECharacteristic notifyCharacteristic(NOTIFY_CHARACTERISTIC, BLENotify | BLERead, 512);

bool deviceConnected = false;

// Route storage
std::vector<Hold> route1Holds;
std::vector<Hold> route2Holds;
bool route1On = true;
bool route2On = true;

// Per-lane route history
LaneHistory laneHistory[NUM_LANES] = {};
bool flashDirty = false;  // set true when history changes, cleared on save

// Timeout tracking: last time any route was updated
unsigned long lastRouteUpdateMillis = 0;

// Deferred LED update flag (CRITICAL: prevents BLE callback deadlock)
volatile bool pendingLEDUpdate = false;

// IR activity tracking to pause animations during IR reception
unsigned long lastIRCheckMillis = 0;
bool irRecentlyActive = false;
const unsigned long IR_COOLDOWN_MS = 500UL;

// Overlap animation state
std::vector<OverlapInfo> overlappingHolds;
std::vector<Hold> route2OnlyHolds;
bool hasOverlap = false;
unsigned long overlapAnimStartMillis = 0;
unsigned long lastAnimUpdateMillis = 0;
const unsigned long ANIM_UPDATE_INTERVAL_MS = 50UL;  // 20fps

// BLE packet parsing state
std::vector<Hold> tempHolds;
bool isNewClimb = true;
std::vector<uint8_t> packetBufferBLE;

// UART state machine state
UartRouteState uartRouteState = UART_WAIT_HEADER1;
uint8_t uartRouteCount = 0;
uint16_t uartRouteExpectedBytes = 0;
std::vector<uint8_t> uartRoutePayload;

// =====================================================================
// Route state helpers
// =====================================================================

void clearBoardExceptRoute1() {
    setRouteStates(true, false);
}

void setRouteState(bool routeOn) {
    if (currentLane == 0) {
        route1On = routeOn;
    } else {
        route2On = routeOn;
    }
    updateOverlapState();
    updateBoardState();
}

void setRouteStates(bool route1State, bool route2State) {
    route1On = route1State;
    route2On = route2State;
    updateOverlapState();
    updateBoardState();
}

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
    updateOverlapState();
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

// =====================================================================
// Setup
// =====================================================================

void setup() {
  Serial.begin(115200);
  { unsigned long t0 = millis(); while (!Serial && millis() - t0 < 3000); }

  // UART link to ESP32
  Serial1.begin(115200);

  FastLED.addLeds<WS2811, LED_PIN, RGB>(leds, NUM_LEDS);
  FastLED.clear();

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);

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

  if (!BLE.begin()) {
    Serial.println("Starting Bluetooth® Low Energy module failed!");
    while (1);
  }

  BLE.setLocalName(boardName);
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

  Serial.println("BLE device ready to connect");
  Serial.print("Device name: ");
  Serial.println(boardName);

  // Startup animation
  startupLEDs();

  // Load saved routes and history from flash
  if (flashLoad()) {
    Serial.println("Restored routes from flash — use Left/Right arrows to browse history");
    updateOverlapState();
    updateBoardState();
  }
}

// =====================================================================
// Main loop
// =====================================================================

void loop() {
  // Handle serial communication for API level queries
  if (Serial.available() > 0) {
    int inByte = Serial.read();
    if (inByte == 4) {
      Serial.write(4);
      Serial.write(API_LEVEL);
    }
  }

  BLE.poll();

  // Process deferred LED updates (CRITICAL: never call updateBoardState from callbacks)
  if (pendingLEDUpdate) {
    pendingLEDUpdate = false;
    updateBoardState();
  }

  pollESP32RouteUART();

  unsigned long now = millis();

  // Lane timeout: auto-switch from lane 1 back to lane 0 after 15s of no new route data.
  // Fires once, then disarms until the next route update or manual lane switch.
  if (currentLane == 1 && lastRouteUpdateMillis != 0) {
    if (now - lastRouteUpdateMillis > 15000UL) {
      Serial.println("[TIMEOUT] No new routes on lane 1 for 15s; switching back to lane 0");
      currentLane = 0;
      lastRouteUpdateMillis = 0;
    }
  }

  // Ensure BLE is always advertising for quick device switching (throttled to every 3s)
  {
    static unsigned long lastAdvMillis = 0;
    if (!deviceConnected && (now - lastAdvMillis > 3000UL)) {
      BLE.advertise();
      lastAdvMillis = now;
    }
  }

  // Check IR before and after animation to improve responsiveness
  checkIRRemote();
  if (irRecentlyActive && (now - lastIRCheckMillis > IR_COOLDOWN_MS)) {
    irRecentlyActive = false;
  }

  // Overlap animation: continuously update when both routes are active and overlapping
  // Pauses during IR activity to prevent missed signals
  if (route1On && route2On && hasOverlap && !pendingLEDUpdate && !irRecentlyActive) {
    if (now - lastAnimUpdateMillis >= ANIM_UPDATE_INTERVAL_MS) {
      updateBoardState();
      lastAnimUpdateMillis = now;
    }
  }

  // Check IR again after potential animation update
  checkIRRemote();

  // Periodic flash save (every 10 minutes if dirty)
  flashSaveIfDirty();

  delay(DELAY_TIME);
}
