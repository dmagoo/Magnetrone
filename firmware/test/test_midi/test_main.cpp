// test_midi -- MIDI TX/RX hardware test
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_midi -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_midi
//
// WHAT IT DOES:
//   1. Plays C D E F G out TX on boot -- verify by ear with a connected synth.
//   2. Loop: prints any incoming MIDI bytes to USB serial monitor in hex,
//      so you can verify RX by sending from an external MIDI device.
//
// NOTE: calls Serial1 directly rather than midiNoteOn() / midiNoteOff()
// because those also call audioNoteOn() which talks to the SGTL5000 over I2C
// and will hang if the audio shield is not connected.

#include <Arduino.h>
#include <unity.h>
#include "pins.h"

static void midiSendNoteOn(uint8_t note, uint8_t velocity) {
    Serial1.write(0x90);
    Serial1.write(note & 0x7F);
    Serial1.write(velocity & 0x7F);
}

static void midiSendNoteOff(uint8_t note) {
    Serial1.write(0x80);
    Serial1.write(note & 0x7F);
    Serial1.write(0x00);
}

void test_midi_tx() {
    // Play C D E F G -- verify by ear on a connected synth.
    uint8_t notes[] = { 60, 62, 64, 65, 67 };
    for (uint8_t note : notes) {
        midiSendNoteOn(note, 100);
        delay(300);
        midiSendNoteOff(note);
        delay(100);
    }
    TEST_PASS();
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(31250);
    delay(100);

    UNITY_BEGIN();
    RUN_TEST(test_midi_tx);
    UNITY_END();
}

void loop() {
    static uint32_t lastStatusMs = 0;
    static uint32_t lastRxMs = 0;
    static bool everReceived = false;

    // Print any incoming MIDI bytes.
    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        Serial.print("RX: 0x");
        if (b < 0x10) Serial.print("0");
        Serial.println(b, HEX);
        lastRxMs = millis();
        everReceived = true;
    }

    // Periodic status line every 2 seconds.
    if (millis() - lastStatusMs >= 2000) {
        lastStatusMs = millis();
        Serial.print("-- waiting for RX | last received: ");
        if (!everReceived) {
            Serial.println("never");
        } else {
            Serial.print((millis() - lastRxMs) / 1000);
            Serial.println("s ago");
        }
    }
}
