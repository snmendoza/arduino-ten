#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <vector>
#include "ClimbingWallHelper.h"

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
                    currentHolds.push_back(h);
                    
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

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // Turn on LED to indicate power

  // Initialize the bucket system
  ClimbingWallHelper::initializeBuckets();
  
  // Set LED offset for your specific board size
  // Example: If you want all LEDs shifted by 50 positions
  ClimbingWallHelper::setLEDOffset(50);  // Adjust this value for your setup
  
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