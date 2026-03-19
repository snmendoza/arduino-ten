// uart_bridge.ino — UART state machine for receiving pre-parsed routes from ESP32

// Poll ESP32 UART link for any pending pre-parsed route 2 updates
// Frame: [0xAA][0x55][count][(posL,posH,colorByte) * count][checksum]
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

                // Checksum valid — push current route2 to history, then commit new one
                if (!route2Holds.empty()) {
                    historyPush(1, route2Holds);
                }
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

                for (Hold& h : route2Holds) {
                    applyAltColors(h.color, h.r, h.g, h.b);
                }

                historyResetBrowsing(1);
                Serial.println("[UART] Route 2 received from ESP32 (checksum OK)");

                updateOverlapState();
                lastRouteUpdateMillis = millis();
                pendingLEDUpdate = true;

                uartRouteState = UART_WAIT_HEADER1;
                uartRoutePayload.clear();
                break;
            }
        }
    }
}
