#ifndef LANE_HANDLER_H
#define LANE_HANDLER_H

#include <vector>
#include <Arduino.h>

struct DeviceInfo {
    String address;
    uint8_t colorSchemeFlag;
    unsigned long lastSeen;
};

void registerDevice(const String& address);
uint8_t getNextColorSchemeFlag();

#endif // LANE_HANDLER_H