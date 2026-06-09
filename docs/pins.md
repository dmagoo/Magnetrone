# Pin Assignments

> **Source of truth.** This file is the authoritative record for all pin assignments.
> `firmware/include/pins.h` must be kept in sync with this document. Update here first, then reflect changes in the header.
>
> Teensy 4.1 only. All pin numbers are GPIO numbers (Teensy 4.1 Arduino API).

## Notes on key constraints

- **Pin 7**: Do not use - I2S DOUT (audio shield)
- **Pin 8**: Do not use - I2S DIN (audio shield)
- **Pin 20**: Do not use - I2S LRCLK (audio shield)
- **Pin 21**: Do not use - I2S BCLK (audio shield)
- **Pin 23**: Do not use - I2S MCLK (audio shield)
- **Pin 12**: Reserved - SPI MISO, do not use if external SD fitted
- **Pin 13**: Do not use - onboard LED / SPI SCK
- **Pin 15**: Do not use - audio shield volume pot / 0.1uF cap to AGND loads the pin

## Pin Table

| GPIO | T4.1 Pad | Signal               | Notes                                              |
|-----:|---------:|----------------------|----------------------------------------------------|
|  n/a |        1 | GND                  |                                                    |
|    0 |        2 | midi_rx              | Serial1 RX                                         |
|    1 |        3 | midi_tx              | Serial1 TX                                         |
|    2 |        4 | stepper_dir          |                                                    |
|    3 |        5 | stepper_step         |                                                    |
|    4 |        6 | stepper_sleep        | power-save; not yet implemented                    |
|    5 |        7 | stepper_enable       |                                                    |
|    6 |        8 | menu_encoder_a       |                                                    |
|    7 |        9 | (do not use)         | I2S DOUT (audio shield)                            |
|    8 |       10 | (do not use)         | I2S DIN (audio shield)                             |
|    9 |       11 | menu_encoder_b       |                                                    |
|   10 |       12 | menu_encoder_btn     |                                                    |
|   11 |       13 | (reserve)            | SPI MOSI                                           |
|   12 |       14 | (reserve)            | SPI MISO - do not use if external SD fitted        |
|  n/a |       15 | +3.3V                |                                                    |
|   24 |       16 | speed_encoder_a      |                                                    |
|   25 |       17 | speed_encoder_b      |                                                    |
|   26 |       18 | hall_7               | analog; moved from pin 15 (audio shield conflict)  |
|   27 |       19 | volume_encoder_a     |                                                    |
|   28 |       20 | volume_encoder_b     |                                                    |
|   29 |       21 | volume_encoder_btn   | press = mute/unmute                                |
|   30 |       22 | encoder_aux1_a       | reserved                                           |
|   31 |       23 | encoder_aux1_b       | reserved                                           |
|   32 |       24 | encoder_aux1_btn     | reserved                                           |
|   33 |       25 | encoder_aux2_a       | reserved                                           |
|   34 |       26 | encoder_aux2_b       | reserved                                           |
|   35 |       27 | encoder_aux2_btn     | reserved                                           |
|   36 |       28 | aux1_btn             | reserved                                           |
|   37 |       29 | speed_encoder_btn    | press = stop/resume; moved from pin 26             |
|   38 |       30 | hall_4               |                                                    |
|   39 |       31 | hall_3               |                                                    |
|   40 |       32 | hall_2               |                                                    |
|   41 |       33 | hall_1               |                                                    |
|  n/a |       34 | GND                  |                                                    |
|   13 |       35 | (do not use)         | onboard LED / SPI SCK                              |
|   14 |       36 | hall_8               | analog                                             |
|   15 |       37 | (do not use)         | audio shield volume pot / cap to AGND              |
|   16 |       38 | hall_6               | analog                                             |
|   17 |       39 | hall_5               | analog                                             |
|   18 |       40 | i2c_sda              | shared bus: LCD + audio shield                     |
|   19 |       41 | i2c_scl              | shared bus: LCD + audio shield                     |
|   20 |       42 | (do not use)         | I2S LRCLK (audio shield)                           |
|   21 |       43 | (do not use)         | I2S BCLK (audio shield)                            |
|   22 |       44 | (reserve)            |                                                    |
|   23 |       45 | (do not use)         | I2S MCLK (audio shield)                            |
|  n/a |       46 | +3.3V                |                                                    |
|  n/a |       47 | GND                  |                                                    |
|  n/a |       48 | VIN                  | +5V in                                             |
|  n/a |       49 | VUSB                 |                                                    |

## PCB Test Status

| Subsystem | Pins | Test Method | Status | Notes |
|---|---|---|---|---|
| I2C / LCD | 18, 19 | Actual firmware | pass | Breadboard |
| Menu encoder + button | 6, 9, 10 | Actual firmware | pass | Breadboard |
| MIDI TX | 1 | app.midiano.com via USB; `pio test -e teensy41_test_midi` | pass | Breadboard |
| MIDI RX | 0 | `pio test -e teensy41_test_midi` | pass | Breadboard |
| Stepper | 2, 3, 4, 5 | `pio test -e teensy41_test_stepper` | pass | Breadboard; 1/16 microstepping required; speed changes must ramp |
| Speed encoder + button | 24, 25, 26 | | - | |
| Volume encoder + button | 27, 28, 29 | | - | |
| Hall sensors | 14-17, 26, 38-41 | `pio test -e teensy41_test_hall` | pass | Breadboard; pin 15 unusable (audio shield); hall7 moved to pin 26 |
| Audio shield | 7, 8, 20, 21, 23 | Headphone output via actual firmware | pass | Direct connection to Teensy |
