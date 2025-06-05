#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <vector>

#define DISPLAY_NAME "Tension Board 2"
#define API_LEVEL 3

// Aurora Board protocol UUIDs
#define ADVERTISING_SERVICE_UUID "4488B571-7806-4DF6-BCFF-A2897E4953FF"  // Aurora Board advertising service
#define DATA_TRANSFER_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART Service
#define DATA_TRANSFER_CHARACTERISTIC "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write characteristic
#define NOTIFY_CHARACTERISTIC "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify characteristic

#define LED_PIN 2  // Built-in LED on most ESP32 dev boards

BLEServer* pServer = nullptr;
BLEService* pAdvertisingService = nullptr;
BLEService* pDataTransferService = nullptr;
BLECharacteristic* pDataTransferCharacteristic = nullptr;
BLECharacteristic* pNotifyCharacteristic = nullptr;
bool deviceConnected = false;

// Structure to store hold information
struct Hold {
    uint16_t position;
    uint8_t r, g, b;
    String colorName;
};

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

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Device disconnected");
      // Restart advertising
      pServer->getAdvertising()->start();
    }
};

class CharacteristicCallbacks: public BLECharacteristicCallbacks {
    private:
        std::vector<Hold> currentHolds;
        bool isNewClimb = true;
        std::vector<uint8_t> packetBuffer;

    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
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
                currentHolds.clear();
                isNewClimb = true;
            }
            
            // Calculate checksum
            std::vector<uint8_t> dataBytes;
            for (size_t i = startIdx + 4; i < startIdx + length + 3; i++) {
                dataBytes.push_back(packetBuffer[i]);
            }
            uint8_t calculatedChecksum = calculateChecksum(dataBytes);
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
                    uint8_t r, g, b;
                    decodeColor(packetBuffer[i+2], r, g, b);
                    String colorName = getColorName(r, g, b);
                    
                    // Add to current holds
                    Hold h = {position, r, g, b, colorName};
                    currentHolds.push_back(h);
                    
                    Serial.print("Hold at position "); Serial.print(position);
                    Serial.print(" with color "); Serial.print(colorName);
                    Serial.print(" RGB("); Serial.print(r);
                    Serial.print(","); Serial.print(g);
                    Serial.print(","); Serial.print(b);
                    Serial.println(")");
                }
            }
            
            // If this is the last packet (S or T), print summary
            if (packetTypeChar == 'S' || packetTypeChar == 'T') {
                Serial.println("\nComplete climb summary:");
                for (const Hold& h : currentHolds) {
                    Serial.print("Position "); Serial.print(h.position);
                    Serial.print(": "); Serial.println(h.colorName);
                }
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
};

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Wait for serial port to connect

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // Turn on LED to indicate power

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
}

void loop() {
  // Blink LED to show device is running
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
  }

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