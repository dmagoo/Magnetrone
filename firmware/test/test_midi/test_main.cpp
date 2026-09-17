// test_midi -- MIDI TX/RX hardware test
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_midi -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_midi
//
// WHAT IT DOES:
//   1. Plays C D E F G out TX on boot, then repeats it every TX_REPEAT_MS --
//      verify by ear with a connected synth, no reboot needed between tries.
//   2. Loop: prints any incoming MIDI bytes to USB serial monitor in hex,
//      so you can verify RX by sending from an external MIDI device.
//
// NOTE: calls Serial1 directly rather than midiNoteOn() / midiNoteOff()
// because those also call audioNoteOn() which talks to the SGTL5000 over I2C
// and will hang if the audio shield is not connected.

#include <Arduino.h>
#include <unity.h>
#include "pins.h"

// How often loop() replays the TX scale, in ms.
constexpr uint32_t TX_REPEAT_MS = 3000;

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

// Play C D E F G -- verify by ear on a connected synth.
// Plain function so loop() can call it too; the Unity assertion stays in the
// RUN_TEST wrapper below. (Calling TEST_PASS() from loop() longjmps into a
// stack frame that no longer exists -- see the reboot loop in bringup_status.)
static void playScale() {
    uint8_t notes[] = { 60, 62, 64, 65, 67 };
    for (uint8_t note : notes) {
        midiSendNoteOn(note, 100);
        delay(300);
        midiSendNoteOff(note);
        delay(100);
    }
}

void test_midi_tx() {
    playScale();
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

    // Replay the scale periodically so TX can be checked without rebooting.
    // TX and RX are separate pins running full duplex, so this does not
    // interfere with reception; bytes arriving mid-scale sit in the buffer.
    static uint32_t lastScaleMs = 0;
    if (millis() - lastScaleMs >= TX_REPEAT_MS) {
        Serial.println("-- TX: C D E F G");
        playScale();
        lastScaleMs = millis();
    }
}
