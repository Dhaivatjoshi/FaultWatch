#include <FaultWatch.h>

// Create an FaultWatch object for a single-color LED on pin 13
FaultWatch singleColorLED(3);

// Example byte variable representing error states (e.g., 0b00100100)
byte errorByte = 0b00011000;

void setup() {
  // Set up serial communication
  Serial.begin(115200);

  // Set the error byte for the LED
  singleColorLED.setErrorByte(errorByte);
}

void loop() {
  // Check if data is available to read from the serial port
  if (Serial.available() > 0) {
    // Read the incoming byte
    String input = Serial.readStringUntil('\n');

    // Try to convert the input to an integer
    int newErrorByte = input.toInt();

    // Validate the input and update errorByte if valid
    if (newErrorByte >= 0 && newErrorByte <= 255) {
      errorByte = (byte)newErrorByte;
      singleColorLED.setErrorByte(errorByte);
      Serial.print("Updated error byte to: 0b");
      Serial.println(errorByte, BIN);
    } else {
      Serial.println("Invalid input. Please enter a value between 0 and 255.");
    }
  }

  // Update the LED states (non-blocking, with on/off periods)
  singleColorLED.update();
}
