// test_uart -- Serial8 (pins 34/35) loopback test
//
// HOW TO RUN:
//   pio test -e teensy41_test_uart
//
// WIRING:
//   Disconnect the driver. Jumper pin 35 (TX) directly to pin 34 (RX).
//   No resistor, nothing else.
//
// WHAT IT DOES:
//   Sends bytes out TX and checks they come back in on RX. If they match,
//   the Teensy pins and Serial8 are good and the problem is on the driver side.

#include <Arduino.h>
#include <unity.h>

void test_uart_loopback() {
    Serial8.begin(115200);
    delay(50);

    // flush any stale bytes
    while (Serial8.available()) Serial8.read();

    const uint8_t pattern[] = {0xA5, 0x3C, 0x00, 0xFF, 0x21};
    for (uint8_t sent : pattern) {
        Serial8.write(sent);
        Serial8.flush();

        uint32_t start = millis();
        while (!Serial8.available() && millis() - start < 100) { /* wait */ }

        if (!Serial8.available()) {
            Serial.print("No byte came back for 0x");
            Serial.println(sent, HEX);
            TEST_FAIL_MESSAGE("Loopback: nothing received -- check the 35->34 jumper");
        }

        int got = Serial8.read();
        Serial.print("sent 0x"); Serial.print(sent, HEX);
        Serial.print("  got 0x"); Serial.println(got, HEX);
        TEST_ASSERT_EQUAL_MESSAGE(sent, got, "Loopback byte mismatch");
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    UNITY_BEGIN();
    RUN_TEST(test_uart_loopback);
    UNITY_END();
}

void loop() {}
