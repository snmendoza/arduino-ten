#pragma once

// Aurora Board protocol constants — shared between R4 and ESP32 sketches
// =====================================================================

#define DISPLAY_NAME "Tension Board 2"
#define API_LEVEL 3

// Device switching toggle: continue advertising when connected
#define CONTINUOUS_ADVERTISING true

// Aurora Board BLE UUIDs
#define ADVERTISING_SERVICE_UUID    "4488B571-7806-4DF6-BCFF-A2897E4953FF"
#define DATA_TRANSFER_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define DATA_TRANSFER_CHARACTERISTIC "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NOTIFY_CHARACTERISTIC       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Hardware configuration
#define LED_PIN 13
#define NUM_LEDS 478
#define DELAY_TIME 10
#define IR_RECEIVE_PIN 9

// UART route protocol from ESP32 (pre-parsed route 2)
// Frame: [0xAA][0x55][count][(posL,posH,colorByte) * count][checksum]
// checksum = XOR of count byte and all payload bytes
#define UART_ROUTE_HEADER_1 0xAA
#define UART_ROUTE_HEADER_2 0x55

// Aurora packet checksum (bitwise NOT of byte sum)
inline uint8_t calculateChecksum(const std::vector<uint8_t>& data) {
    uint8_t sum = 0;
    for (uint8_t byte : data) {
        sum = (sum + byte) & 255;
    }
    return (~sum) & 255;
}
