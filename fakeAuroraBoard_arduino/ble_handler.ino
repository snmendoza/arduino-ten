// ble_handler.ino — BLE connection management and Aurora packet parsing

// BLE event handlers for ArduinoBLE
void onBLEConnected(BLEDevice central) {
    deviceConnected = true;
    Serial.print("Device connected: ");
    Serial.println(central.address());
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
}

/*
 * BLE Characteristic Write Handler — Aurora Board protocol parser (API Level 3)
 *
 * Packet Structure:
 * [START=0x01][LENGTH][CHECKSUM][TYPE_MARKER=0x02][PACKET_TYPE][HOLD_DATA...][END=0x03]
 *
 * Packet Types:
 * - 'R': Route packet (partial data)
 * - 'S': Set packet (complete route)
 * - 'Q': Query packet
 * - 'T': Test packet (single complete route, clears previous)
 *
 * Hold data: 3 bytes per hold (position_low, position_high, color_encoded)
 */
void onDataTransferCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
    const uint8_t* data = characteristic.value();
    int length = characteristic.valueLength();

    if (length > 0) {
        // Accumulate received bytes in BLE buffer to handle packet fragmentation
        for (int i = 0; i < length; i++) {
            packetBufferBLE.push_back(data[i]);
        }

        // Parse BLE packet buffer
        while (packetBufferBLE.size() >= 5) {
            // Find the start marker (0x01)
            size_t startIdx = 0;
            while (startIdx < packetBufferBLE.size() && packetBufferBLE[startIdx] != 1) {
                startIdx++;
            }

            // Check if we have enough data for packet header
            if (startIdx >= packetBufferBLE.size() - 4) {
                break;
            }

            // Extract packet length and validate we have complete packet
            uint8_t packetLength = packetBufferBLE[startIdx + 1];
            if (startIdx + packetLength + 4 >= packetBufferBLE.size()) {
                break;
            }

            // Validate type marker (should be 0x02)
            if (packetBufferBLE[startIdx + 3] != 2) {
                Serial.println("Invalid packet format - second byte should be 2");
                packetBufferBLE.erase(packetBufferBLE.begin(), packetBufferBLE.begin() + startIdx + 1);
                continue;
            }

            // Extract and validate packet type
            uint8_t packetType = packetBufferBLE[startIdx + 4];
            char packetTypeChar = (char)packetType;

            if (packetTypeChar != 'R' && packetTypeChar != 'S' &&
                packetTypeChar != 'Q' && packetTypeChar != 'T') {
                Serial.println("Invalid packet type for API level 3");
                packetBufferBLE.erase(packetBufferBLE.begin(), packetBufferBLE.begin() + startIdx + 1);
                continue;
            }

            // 'T' packets indicate a new test route, clear previous holds
            if (packetTypeChar == 'T') {
                tempHolds.clear();
                isNewClimb = true;
            }

            // Checksum validation
            std::vector<uint8_t> dataBytes;
            for (size_t i = startIdx + 4; i < startIdx + packetLength + 3; i++) {
                dataBytes.push_back(packetBufferBLE[i]);
            }
            uint8_t calculatedChecksum = calculateChecksum(dataBytes);
            uint8_t receivedChecksum = packetBufferBLE[startIdx + 2];

            // Decode hold data (3 bytes per hold: posL, posH, colorByte)
            for (size_t i = startIdx + 5; i < startIdx + packetLength + 3; i += 3) {
                if (i + 2 < packetBufferBLE.size()) {
                    uint16_t position = (packetBufferBLE[i+1] << 8) + packetBufferBLE[i];

                    uint8_t r, g, b;
                    decodeColor(packetBufferBLE[i+2], r, g, b);
                    HoldColor color = classifyColor(r, g, b);

                    Hold h = {position, r, g, b, color};
                    tempHolds.push_back(h);
                }
            }

            // Route completion: 'S' and 'T' packets indicate complete route
            if (packetTypeChar == 'S' || packetTypeChar == 'T') {
                    if (currentLane == 0) {
                        // Push current route to history before overwriting
                        if (!route1Holds.empty()) {
                            historyPush(0, route1Holds);
                        }
                        route1Holds.clear();
                        route1Holds = tempHolds;
                        for (Hold& h : route1Holds) {
                            applyPrincipalColors(h.color, h.r, h.g, h.b);
                        }
                        historyResetBrowsing(0);
                        Serial.println("Route 1 received");
                    } else if (currentLane == 1) {
                        // Push current route to history before overwriting
                        if (!route2Holds.empty()) {
                            historyPush(1, route2Holds);
                        }
                        route2Holds.clear();
                        route2Holds = tempHolds;
                        for (Hold& h : route2Holds) {
                            applyAltColors(h.color, h.r, h.g, h.b);
                        }
                        historyResetBrowsing(1);
                        Serial.println("Route 2 received");
                    }

                tempHolds.clear();

                updateOverlapState();
                lastRouteUpdateMillis = millis();
                pendingLEDUpdate = true;
                Serial.println();
            }

            // Remove processed packet from buffer
            packetBufferBLE.erase(packetBufferBLE.begin(), packetBufferBLE.begin() + startIdx + packetLength + 4);
        }

        // Buffer overflow protection
        if (packetBufferBLE.size() > 1000) {
            Serial.println("Buffer overflow, clearing");
            packetBufferBLE.clear();
        }
    }
}
