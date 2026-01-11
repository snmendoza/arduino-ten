/*
 * ESP32 WROOM Dev Module - BLE-to-UART Aurora Bridge
 * =====================================================================
 *
 * PURPOSE:
 *   - Act as an Aurora Board–compatible BLE peripheral
 *   - Receive route data from Tension Board / Aurora app over BLE
 *   - Parse Aurora packets on the ESP32
 *   - Send fully decoded route holds to the Arduino R4 over UART
 *
 * This sketch is ONLY for ESP32 (e.g. "ESP32 Dev Module").
 */

#include <vector>
 
 #ifdef ESP32
   #include <BLEDevice.h>
   #include <BLEServer.h>
   #include <BLEUtils.h>
   #include <BLE2902.h>
 #else
   #error "Select an ESP32 board (e.g. ESP32 Dev Module) to build this sketch."
 #endif
 
 // Aurora Board protocol UUIDs
 #define ADVERTISING_SERVICE_UUID       "4488B571-7806-4DF6-BCFF-A2897E4953FF"
 #define DATA_TRANSFER_SERVICE_UUID     "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
 #define DATA_TRANSFER_CHARACTERISTIC   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
 #define NOTIFY_CHARACTERISTIC         "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
 
 #define DISPLAY_NAME   "Tension Board 2 - It's a jug!"
 #define API_LEVEL      3
 
// UART configuration: ESP32 hardware UART (Serial2) for link to R4
// Default pins on many ESP32 Dev Modules: RX2 = GPIO16, TX2 = GPIO17
// WIRING: ESP32 TX2 (GPIO17) -> R4 RX1, ESP32 GND -> R4 GND
// FOR LOOPBACK TEST: Connect GPIO17 (TX) to GPIO16 (RX) with a jumper wire
#define UART_TX_PIN 17   // ESP32 TX2 -> R4 RX1
#define UART_RX_PIN 16   // ESP32 RX2 <- R4 TX1 (optional / currently unused)

// Uncomment to enable UART loopback self-test mode
// Connect GPIO17 (TX) to GPIO16 (RX) with a jumper wire to test
// #define UART_LOOPBACK_TEST
 
 // BLE state
 BLEServer*         g_server          = nullptr;
 BLECharacteristic* g_rxChar          = nullptr;  // app → ESP32
 bool               g_deviceConnected = false;
 bool               g_prevConnected   = false;
// Buffer for fragmented BLE Aurora packets
std::vector<uint8_t> g_blePacketBuffer;

// Simple representation of an Aurora hold to forward over UART
struct AuroraHold {
  uint16_t position;
  uint8_t  colorByte;   // Compressed Aurora RGB (3R:3G:2B)
};

// Temporary storage for a single complete route
std::vector<AuroraHold> g_routeTemp;

// Calculate Aurora checksum (same logic as on R4)
uint8_t calculateChecksum(const std::vector<uint8_t>& data) {
  uint8_t sum = 0;
  for (uint8_t byte : data) {
    sum = (sum + byte) & 0xFF;
  }
  return static_cast<uint8_t>((~sum) & 0xFF);
}

// Send a fully parsed route over UART to the R4.
// Frame format:
//   [0xAA][0x55][count][(posL,posH,colorByte) * count]
//   - count: number of holds (0–255)
//   - position: little-endian uint16
//   - colorByte: original Aurora 8-bit color encoding
void sendRouteOverUART(const std::vector<AuroraHold>& route) {
  if (route.empty()) {
    Serial.println("UART: route empty, nothing to send");
    return;
  }

  uint8_t count = (route.size() > 255) ? 255 : static_cast<uint8_t>(route.size());

  Serial.print("UART: sending route with ");
  Serial.print(count);
  Serial.println(" holds to R4");
  
  // Send header
  Serial2.write(0xAA);
  Serial2.write(0x55);
  Serial2.write(count);
  
  // Send hold data
  for (uint8_t i = 0; i < count; i++) {
    uint16_t pos = route[i].position;
    uint8_t colorByte = route[i].colorByte;
    Serial2.write((uint8_t)(pos & 0xFF));        // low
    Serial2.write((uint8_t)((pos >> 8) & 0xFF)); // high
    Serial2.write(colorByte);
  }
  Serial2.flush();
  
  // Debug: print what we sent
  Serial.print("UART: sent header [0xAA 0x55 ");
  Serial.print(count);
  Serial.print("] + ");
  Serial.print(count * 3);
  Serial.println(" bytes of hold data");
}

// UART loopback self-test function
// Sends test data and reads it back to verify UART is working
// Connect GPIO17 (TX) to GPIO16 (RX) with a jumper wire to test
void uartLoopbackTest() {
  Serial.println("\n=== UART LOOPBACK TEST ===");
  
  // Test data: header + 2 test holds
  uint8_t testData[] = {
    0xAA, 0x55, 0x02,  // Header: 0xAA, 0x55, count=2
    0x64, 0x00, 0xFF,  // Hold 1: position=100 (0x0064), color=0xFF
    0xC8, 0x00, 0xAA   // Hold 2: position=200 (0x00C8), color=0xAA
  };
  const size_t testDataLen = sizeof(testData);
  
  // Clear any existing data in RX buffer
  while (Serial2.available()) {
    Serial2.read();
  }
  
  Serial.print("Sending ");
  Serial.print(testDataLen);
  Serial.println(" bytes...");
  
  // Send test data
  for (size_t i = 0; i < testDataLen; i++) {
    Serial2.write(testData[i]);
    Serial.print("TX[");
    Serial.print(i);
    Serial.print("] = 0x");
    if (testData[i] < 0x10) Serial.print("0");
    Serial.println(testData[i], HEX);
  }
  Serial2.flush();
  
  // Wait a bit for data to loop back
  delay(50);
  
  // Read back the data
  Serial.println("\nReading back data...");
  uint8_t received[sizeof(testData)];
  size_t receivedCount = 0;
  unsigned long startTime = millis();
  
  // Read with timeout (200ms)
  while (receivedCount < testDataLen && (millis() - startTime) < 200) {
    if (Serial2.available()) {
      received[receivedCount] = Serial2.read();
      Serial.print("RX[");
      Serial.print(receivedCount);
      Serial.print("] = 0x");
      if (received[receivedCount] < 0x10) Serial.print("0");
      Serial.println(received[receivedCount], HEX);
      receivedCount++;
    }
    delay(1);
  }
  
  // Verify received data
  Serial.println("\nVerifying data...");
  bool testPassed = true;
  
  if (receivedCount != testDataLen) {
    Serial.print("FAIL: Expected ");
    Serial.print(testDataLen);
    Serial.print(" bytes, received ");
    Serial.println(receivedCount);
    testPassed = false;
  } else {
    for (size_t i = 0; i < testDataLen; i++) {
      if (received[i] != testData[i]) {
        Serial.print("FAIL: Byte ");
        Serial.print(i);
        Serial.print(" mismatch: expected 0x");
        if (testData[i] < 0x10) Serial.print("0");
        Serial.print(testData[i], HEX);
        Serial.print(", got 0x");
        if (received[i] < 0x10) Serial.print("0");
        Serial.println(received[i], HEX);
        testPassed = false;
        break;
      }
    }
  }
  
  if (testPassed) {
    Serial.println("✓ UART LOOPBACK TEST PASSED!");
  } else {
    Serial.println("✗ UART LOOPBACK TEST FAILED!");
    Serial.println("Check: Is GPIO17 (TX) connected to GPIO16 (RX)?");
  }
  Serial.println("========================\n");
}
 
 // --- BLE server callbacks ---
 class ServerCallbacks : public BLEServerCallbacks {
   void onConnect(BLEServer*) override {
     g_deviceConnected = true;
     Serial.println("BLE: device connected");
   }
   void onDisconnect(BLEServer*) override {
     g_deviceConnected = false;
     Serial.println("BLE: device disconnected");
   }
 };
 
// --- BLE characteristic callbacks (write from app) ---
// We fully parse Aurora packets on the ESP32. For each complete route
// (packet types 'S' or 'T'), we send a compact route representation to
// the R4 over UART using sendRouteOverUART().
class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue();
    int len = value.length();
    if (len == 0) return;

    Serial.print("BLE: received ");
    Serial.print(len);
    Serial.println(" bytes");

    // Append raw bytes (including any 0x00) to local Aurora packet buffer
    for (int i = 0; i < len; i++) {
      g_blePacketBuffer.push_back(static_cast<uint8_t>(value[i]));
    }

    // Parse accumulated Aurora packets (API level 3)
    while (g_blePacketBuffer.size() >= 5) {  // Minimum header size
      // Find START marker (0x01)
      size_t startIdx = 0;
      while (startIdx < g_blePacketBuffer.size() &&
             g_blePacketBuffer[startIdx] != 0x01) {
        startIdx++;
      }

      // Not enough bytes for header
      if (startIdx >= g_blePacketBuffer.size() - 4) {
        break;
      }

      uint8_t packetLength = g_blePacketBuffer[startIdx + 1];
      // Require full packet (header + data + END)
      if (startIdx + packetLength + 4 >= g_blePacketBuffer.size()) {
        break;
      }

      // TYPE_MARKER must be 0x02
      if (g_blePacketBuffer[startIdx + 3] != 0x02) {
        Serial.println("ESP32: invalid packet format - TYPE_MARKER != 0x02");
        g_blePacketBuffer.erase(g_blePacketBuffer.begin(),
                                g_blePacketBuffer.begin() + startIdx + 1);
        continue;
      }

      uint8_t packetType = g_blePacketBuffer[startIdx + 4];
      char packetTypeChar = static_cast<char>(packetType);

      if (packetTypeChar != 'R' && packetTypeChar != 'S' &&
          packetTypeChar != 'Q' && packetTypeChar != 'T') {
        Serial.println("ESP32: invalid packet type for API level 3");
        g_blePacketBuffer.erase(g_blePacketBuffer.begin(),
                                g_blePacketBuffer.begin() + startIdx + 1);
        continue;
      }

      // Optional checksum debug (same as R4)
      std::vector<uint8_t> dataBytes;
      for (size_t i = startIdx + 4;
           i < startIdx + packetLength + 3; i++) {
        dataBytes.push_back(g_blePacketBuffer[i]);
      }
      uint8_t calculatedChecksum = calculateChecksum(dataBytes);
      uint8_t receivedChecksum = g_blePacketBuffer[startIdx + 2];

      Serial.print("ESP32: Decoded packet, len=");
      Serial.print(packetLength);
      Serial.print(" type=");
      Serial.print(packetTypeChar);
      Serial.print(" checksum=");
      Serial.print(receivedChecksum);
      Serial.print(" calc=");
      Serial.println(calculatedChecksum);

      // Decode hold data (3 bytes per hold: posL, posH, colorByte)
      for (size_t i = startIdx + 5;
           i < startIdx + packetLength + 3; i += 3) {
        if (i + 2 < g_blePacketBuffer.size()) {
          uint16_t position =
            (static_cast<uint16_t>(g_blePacketBuffer[i + 1]) << 8) +
            static_cast<uint16_t>(g_blePacketBuffer[i]);
          uint8_t colorByte = g_blePacketBuffer[i + 2];

          AuroraHold h{position, colorByte};
          g_routeTemp.push_back(h);
        }
      }

      // 'S' and 'T' mark the end of a complete route
      if (packetTypeChar == 'S' || packetTypeChar == 'T') {
        Serial.print("ESP32: Route complete, ");
        Serial.print(g_routeTemp.size());
        Serial.println(" holds accumulated");
        sendRouteOverUART(g_routeTemp);
        g_routeTemp.clear();
      }

      // Discard processed packet
      g_blePacketBuffer.erase(
        g_blePacketBuffer.begin(),
        g_blePacketBuffer.begin() + startIdx + packetLength + 4);
    }

    // Safety: prevent buffer from growing unbounded
    if (g_blePacketBuffer.size() > 1000) {
      Serial.println("ESP32: BLE packet buffer overflow, clearing");
      g_blePacketBuffer.clear();
    }
  }
};
 
 // --- Setup & loop for ESP32 Dev Module ---
 void setup() {
   Serial.begin(115200);
   delay(500);
 
  Serial.println();
  Serial.println("ESP32 WROOM BLE-to-UART Aurora Bridge");

  // UART link to R4
  Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.print("UART: Serial2 initialized on RX=");
  Serial.print(UART_RX_PIN);
  Serial.print(" TX=");
  Serial.println(UART_TX_PIN);
  
#ifdef UART_LOOPBACK_TEST
  Serial.println("\n*** UART LOOPBACK TEST MODE ENABLED ***");
  Serial.println("Connect GPIO17 (TX) to GPIO16 (RX) with a jumper wire");
  Serial.println("Waiting 3 seconds for you to connect the wire...");
  delay(3000);
  uartLoopbackTest();
#else
  // Quick test - send a simple pattern
  delay(100);
  Serial.println("UART: Sending initial test pattern...");
  Serial2.write(0xAA);
  Serial2.write(0x55);
  Serial2.write(0x00);  // count = 0 (empty route)
  Serial2.flush();
  Serial.println("UART: Test pattern sent [0xAA 0x55 0x00]");
#endif
   char boardName[64];
   snprintf(boardName, sizeof(boardName), "%s@%d", DISPLAY_NAME, API_LEVEL);
 
   Serial.print("BLE: init as ");
   Serial.println(boardName);
   BLEDevice::init(boardName);
   delay(100);  // Allow BLE stack to initialize

   g_server = BLEDevice::createServer();
   g_server->setCallbacks(new ServerCallbacks());

   // Create advertising service (required for app to discover device)
   // The app filters by this UUID, so it must be advertised
   BLEService* advertisingService = g_server->createService(BLEUUID(ADVERTISING_SERVICE_UUID));
   advertisingService->start();

   // Create data transfer service (for actual route data)
   BLEService* service = g_server->createService(BLEUUID(DATA_TRANSFER_SERVICE_UUID));
 
   // RX characteristic: app writes Aurora packets here
   g_rxChar = service->createCharacteristic(
     BLEUUID(DATA_TRANSFER_CHARACTERISTIC),
     BLECharacteristic::PROPERTY_WRITE
   );
   g_rxChar->setCallbacks(new RxCallbacks());
 
   // Optional notify characteristic (kept for compatibility)
   BLECharacteristic* notifyChar = service->createCharacteristic(
     BLEUUID(NOTIFY_CHARACTERISTIC),
     BLECharacteristic::PROPERTY_NOTIFY
   );
   notifyChar->addDescriptor(new BLE2902());
 
   service->start();
   delay(100);  // Allow services to fully start

   // Configure advertising
   BLEAdvertising* adv = BLEDevice::getAdvertising();
   // Add the advertising service UUID - this is what the app filters by
   adv->addServiceUUID(BLEUUID(ADVERTISING_SERVICE_UUID));
   // Enable scan response to include more data if needed
   adv->setScanResponse(true);
   adv->setMinPreferred(0x06);  // helps with iPhone connections issue
   adv->setMaxPreferred(0x12);
   // Set advertising interval (lower = more frequent, but uses more power)
   // Default is usually fine, but we can be explicit
   
   // Start advertising
   delay(100);  // Brief delay before starting advertising
   BLEDevice::startAdvertising();
   
   Serial.println("BLE: advertising started");
   Serial.print("BLE: Device name: ");
   Serial.println(boardName);
   Serial.print("BLE: Advertising service UUID: ");
   Serial.println(ADVERTISING_SERVICE_UUID);
 }
 
void loop() {
  // Handle connection state changes
  if (!g_deviceConnected && g_prevConnected) {
    // Disconnected: restart advertising
    delay(500);
    BLEDevice::startAdvertising();
    Serial.println("BLE: restarted advertising");
    g_prevConnected = g_deviceConnected;
  }

  if (g_deviceConnected && !g_prevConnected) {
    // Just connected
    g_prevConnected = g_deviceConnected;
    Serial.println("BLE: connected");
  }

  // Ensure advertising continues (ESP32 may stop advertising after a period)
  if (!g_deviceConnected) {
    // Keep advertising active - restart if needed
    static unsigned long lastAdvCheck = 0;
    unsigned long now = millis();
    if (now - lastAdvCheck > 5000) {  // Check every 5 seconds
      BLEDevice::startAdvertising();
      lastAdvCheck = now;
    }
  }

#ifdef UART_LOOPBACK_TEST
  // Continuous UART loopback test - runs every 5 seconds
  static unsigned long lastTest = 0;
  unsigned long now = millis();
  if (now - lastTest > 5000) {
    uartLoopbackTest();
    lastTest = now;
  }
#endif

  delay(10);
}