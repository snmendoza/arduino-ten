// flash_storage.ino — Persist route history to EEPROM (RA4M1 data flash)
//
// Format:
//   [MAGIC_1][MAGIC_2][VERSION]
//   For each lane (0..1):
//     [current_route_count][current_route_holds * count]
//     [history_depth]
//     For each history slot (0..depth-1):
//       [hold_count][holds * count]
//
// Each hold is stored as 6 bytes: posL, posH, r, g, b, color

#include <EEPROM.h>

#define FLASH_MAGIC_1 0xAB
#define FLASH_MAGIC_2 0xCD
#define FLASH_VERSION 1
#define FLASH_SAVE_INTERVAL_MS 600000UL  // 10 minutes

static unsigned long lastFlashSaveMillis = 0;

// Write a single RouteSnapshot at the given EEPROM address.
// Returns the address after the written data.
static int flashWriteSnapshot(int addr, const RouteSnapshot& snap) {
    EEPROM.write(addr++, snap.count);
    for (uint8_t i = 0; i < snap.count; i++) {
        const Hold& h = snap.holds[i];
        EEPROM.write(addr++, h.position & 0xFF);
        EEPROM.write(addr++, (h.position >> 8) & 0xFF);
        EEPROM.write(addr++, h.r);
        EEPROM.write(addr++, h.g);
        EEPROM.write(addr++, h.b);
        EEPROM.write(addr++, static_cast<uint8_t>(h.color));
    }
    return addr;
}

// Read a single RouteSnapshot from the given EEPROM address.
// Returns the address after the read data.
static int flashReadSnapshot(int addr, RouteSnapshot& snap) {
    snap.count = EEPROM.read(addr++);
    if (snap.count > MAX_HOLDS_PER_ROUTE) {
        snap.count = 0;  // corrupt data guard
        return addr;
    }
    for (uint8_t i = 0; i < snap.count; i++) {
        Hold& h = snap.holds[i];
        uint8_t posL = EEPROM.read(addr++);
        uint8_t posH = EEPROM.read(addr++);
        h.position = static_cast<uint16_t>(posL) | (static_cast<uint16_t>(posH) << 8);
        h.r = EEPROM.read(addr++);
        h.g = EEPROM.read(addr++);
        h.b = EEPROM.read(addr++);
        h.color = static_cast<HoldColor>(EEPROM.read(addr++));
    }
    return addr;
}

// Save all current routes and history to EEPROM
void flashSave() {
    int addr = 0;

    // Header
    EEPROM.write(addr++, FLASH_MAGIC_1);
    EEPROM.write(addr++, FLASH_MAGIC_2);
    EEPROM.write(addr++, FLASH_VERSION);

    // Lane 0: current route + history
    RouteSnapshot currentSnap0 = snapshotFromVector(route1Holds);
    addr = flashWriteSnapshot(addr, currentSnap0);
    EEPROM.write(addr++, laneHistory[0].depth);
    for (uint8_t i = 0; i < laneHistory[0].depth; i++) {
        addr = flashWriteSnapshot(addr, laneHistory[0].slots[i]);
    }

    // Lane 1: current route + history
    RouteSnapshot currentSnap1 = snapshotFromVector(route2Holds);
    addr = flashWriteSnapshot(addr, currentSnap1);
    EEPROM.write(addr++, laneHistory[1].depth);
    for (uint8_t i = 0; i < laneHistory[1].depth; i++) {
        addr = flashWriteSnapshot(addr, laneHistory[1].slots[i]);
    }

    Serial.print("[FLASH] Saved ");
    Serial.print(addr);
    Serial.println(" bytes to EEPROM");

    flashDirty = false;
    lastFlashSaveMillis = millis();
}

// Load current routes and history from EEPROM.
// Returns true if valid data was found and loaded.
bool flashLoad() {
    int addr = 0;

    // Validate header
    if (EEPROM.read(addr++) != FLASH_MAGIC_1) return false;
    if (EEPROM.read(addr++) != FLASH_MAGIC_2) return false;
    if (EEPROM.read(addr++) != FLASH_VERSION) return false;

    // Lane 0: current route
    RouteSnapshot snap0;
    addr = flashReadSnapshot(addr, snap0);
    vectorFromSnapshot(snap0, route1Holds);

    // Lane 0: history
    uint8_t depth0 = EEPROM.read(addr++);
    if (depth0 > MAX_HISTORY_DEPTH) depth0 = MAX_HISTORY_DEPTH;
    laneHistory[0].depth = depth0;
    laneHistory[0].browseIndex = -1;
    for (uint8_t i = 0; i < depth0; i++) {
        addr = flashReadSnapshot(addr, laneHistory[0].slots[i]);
    }

    // Lane 1: current route
    RouteSnapshot snap1;
    addr = flashReadSnapshot(addr, snap1);
    vectorFromSnapshot(snap1, route2Holds);

    // Lane 1: history
    uint8_t depth1 = EEPROM.read(addr++);
    if (depth1 > MAX_HISTORY_DEPTH) depth1 = MAX_HISTORY_DEPTH;
    laneHistory[1].depth = depth1;
    laneHistory[1].browseIndex = -1;
    for (uint8_t i = 0; i < depth1; i++) {
        addr = flashReadSnapshot(addr, laneHistory[1].slots[i]);
    }

    Serial.print("[FLASH] Loaded ");
    Serial.print(addr);
    Serial.print(" bytes from EEPROM (lane0: ");
    Serial.print(route1Holds.size());
    Serial.print(" holds + ");
    Serial.print(depth0);
    Serial.print(" history, lane1: ");
    Serial.print(route2Holds.size());
    Serial.print(" holds + ");
    Serial.print(depth1);
    Serial.println(" history)");

    flashDirty = false;
    return true;
}

// Called from loop() — saves to flash if dirty and interval has elapsed
void flashSaveIfDirty() {
    if (flashDirty && (millis() - lastFlashSaveMillis > FLASH_SAVE_INTERVAL_MS)) {
        flashSave();
    }
}
