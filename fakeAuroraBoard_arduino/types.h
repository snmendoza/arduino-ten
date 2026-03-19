#pragma once
#include <FastLED.h>

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

// Overlap animation state (route1 vs route2)
struct OverlapInfo {
    uint16_t position;
    CRGB colorRoute1;
    CRGB colorRoute2;
};

// Route history configuration
#define MAX_HISTORY_DEPTH 5
#define MAX_HOLDS_PER_ROUTE 50
#define NUM_LANES 2

// Fixed-size route snapshot for history storage (no heap allocation)
struct RouteSnapshot {
    Hold holds[MAX_HOLDS_PER_ROUTE];
    uint8_t count;  // actual number of holds (0 = empty slot)
};

// Per-lane history buffer: fixed FIFO ring of RouteSnapshots
// Index 0 = most recent, MAX_HISTORY_DEPTH-1 = oldest
struct LaneHistory {
    RouteSnapshot slots[MAX_HISTORY_DEPTH];
    uint8_t depth;        // how many slots are occupied (0..MAX_HISTORY_DEPTH)
    int8_t browseIndex;   // -1 = live, 0..depth-1 = browsing history
};

// UART state machine states
enum UartRouteState {
    UART_WAIT_HEADER1,
    UART_WAIT_HEADER2,
    UART_WAIT_COUNT,
    UART_READ_PAYLOAD,
    UART_WAIT_CHECKSUM
};

// ---- Function prototypes for functions using custom types ----
// (Arduino auto-forward-declaration can fail for functions with custom types)

// color_mapping.ino
void decodeColor(uint8_t colorByte, uint8_t& r, uint8_t& g, uint8_t& b);
HoldColor classifyColor(uint8_t r, uint8_t g, uint8_t b);
void applyPrincipalColors(HoldColor color, uint8_t& r, uint8_t& g, uint8_t& b);
void applyAltColors(HoldColor color, uint8_t& r, uint8_t& g, uint8_t& b);

// led_display.ino
CRGB wavelengthToRGB(float wavelength);
CRGB blendColors(const CRGB& c1, const CRGB& c2, float t);
void updateOverlapState();
void updateBoardState();
void setBoardLEDs(const std::vector<Hold>& boardState);
void startupLEDs();
int mirrorPosition(int pos);
void mirrorCurrentLane();

// ir_handler.ino
void checkIRRemote();

// uart_bridge.ino
void pollESP32RouteUART();

// route_history.ino
void historyPush(uint8_t lane, const std::vector<Hold>& route);
bool historyNavigate(uint8_t lane, int8_t direction);  // -1 = older, +1 = newer
const std::vector<Hold>& historyGetDisplayRoute(uint8_t lane);
void historyResetBrowsing(uint8_t lane);
RouteSnapshot snapshotFromVector(const std::vector<Hold>& route);
void vectorFromSnapshot(const RouteSnapshot& snap, std::vector<Hold>& out);

// flash_storage.ino
void flashSave();
bool flashLoad();
void flashSaveIfDirty();

// ble_handler.ino
void onBLEConnected(BLEDevice central);
void onBLEDisconnected(BLEDevice central);
void onDataTransferCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic);
