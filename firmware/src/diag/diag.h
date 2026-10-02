#pragma once

// Diagnostics mode: a bench state for chasing hardware faults (the LCD / I2C
// bus, the motor driver, the hall sensors) over the USB serial monitor.
//
// Entered by holding the menu button at power-up, or by typing "diag" in the
// serial monitor while the table is running normally (it reboots into the
// mode). "exit" or a power cycle returns to normal.
//
// In the mode the motor stays off and nothing plays unless commanded. A report
// prints every 5 s with a beep; faults print the moment they are seen. Type
// "help" for the commands.

// Call first thing in setup(). True if this boot is a diagnostics boot (the
// flag left by "diag", or the menu button held). Clears the flag.
bool diagWanted();

void diagSetup();   // instead of the normal setup
void diagLoop();    // instead of the normal loop

// Normal mode: call every loop. Watches the serial monitor for "diag", and
// for "scenes", which prints the saved scenes as JSON (scene_dump.h).
struct SavedConfig;
void diagPollSerial(const SavedConfig& cfg);
