// ir_handler.ino — IR remote control processing
//
// IR Remote Button Map (NEC protocol, address 0xC7EA):
//   Up Arrow   (0x19): Switch to lane 1, or toggle route2 visibility if already there
//   Down Arrow (0x33): Switch to lane 0, or toggle route1 visibility if already there
//   Home       (0x03): Single mode — show only route 1
//   Return     (0x78): Mirror current lane horizontally
//   Power      (0x17): Toggle visibility of current lane

void checkIRRemote() {
    if (IrReceiver.decode()) {
        // Ignore NEC repeat frames to prevent double-firing on a single press
        if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) {
            IrReceiver.resume();
            return;
        }

        // Mark IR as recently active to pause animations
        irRecentlyActive = true;
        lastIRCheckMillis = millis();

        Serial.print("Protocol: ");
        Serial.print(IrReceiver.decodedIRData.protocol);
        Serial.print(" Address: 0x");
        Serial.print(IrReceiver.decodedIRData.address, HEX);
        Serial.print(" Command: 0x");
        Serial.println(IrReceiver.decodedIRData.command, HEX);

        // Only handle commands from our remote (NEC, address 0xC7EA)
        if (IrReceiver.decodedIRData.protocol == NEC &&
            IrReceiver.decodedIRData.address == 0xC7EA) {

            switch (IrReceiver.decodedIRData.command) {
                case 0x19:  // Up arrow — select lane 1
                    if (currentLane == 1) {
                        Serial.println("IR: Up arrow pressed - toggling route2 visibility");
                        toggleRouteVisibility();
                    } else {
                        Serial.print("IR: Up arrow pressed - switching from lane ");
                        Serial.print(currentLane);
                        Serial.println(" to lane 1");
                        currentLane = 1;
                        lastRouteUpdateMillis = millis();  // Reset timeout for manual switch
                        updateBoardState();
                    }
                    break;

                case 0x33:  // Down arrow — select lane 0
                    if (currentLane == 0) {
                        Serial.println("IR: Down arrow pressed - toggling route1 visibility");
                        toggleRouteVisibility();
                    } else {
                        Serial.print("IR: Down arrow pressed - switching from lane ");
                        Serial.print(currentLane);
                        Serial.println(" to lane 0");
                        currentLane = 0;
                        updateBoardState();
                    }
                    break;

                case 0x03:  // Home — single mode, keep route 1
                    Serial.println("IR: Home pressed");
                    clearBoardExceptRoute1();
                    break;

                case 0x78:  // Return/Back — mirror current lane
                    Serial.println("IR: Return pressed, mirroring current lane");
                    mirrorCurrentLane();
                    break;

                case 0x17:  // Power — toggle visibility of current lane
                    Serial.println("IR: Power pressed");
                    toggleRouteVisibility();
                    break;
            }
        }

        IrReceiver.resume();
    }
}
