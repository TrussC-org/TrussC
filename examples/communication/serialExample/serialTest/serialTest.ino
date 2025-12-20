/**
 * TrussC Serial Test for Arduino
 *
 * Test sketch that works the same as openFrameworks' serialExample
 */

int ledPin = 13;   // LED pin
int val = 0;       // Data read from serial

void setup() {
    pinMode(ledPin, OUTPUT);    // Set LED pin as output
    Serial.begin(9600);         // Connect to serial port
}

void loop() {
    // Read from serial port
    val = Serial.read();

    // -1 means no data available
    // When 'a' is received, return "ABC" and blink LED
    //
    // Note: Arduino TX/RX may take 5-10 seconds to stabilize
    // After uploading, wait a moment before opening the serial monitor
    // When you send 'a', you should see ABC returned in the console
    // Verify this first before trying the TrussC sample
    //
    // In TrussC/oF, you need to explicitly specify which serial port to use
    // Check the port under "Tools > Serial Port"
    // Example: "COM7" (Windows), "/dev/tty...." (Mac/Linux)

    if (val != -1) {
        if (val == 'a') {
            Serial.print("ABC");        // Simply return ABC
            digitalWrite(ledPin, HIGH);
            delay(200);
            digitalWrite(ledPin, LOW);
        }
    }
}
