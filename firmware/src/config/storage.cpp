#include "storage.h"
#include <EEPROM.h>

SavedConfig storageDefaults() {
    return {
        EEPROM_MAGIC,
        EEPROM_VERSION,
        DEFAULT_ROOT,
        DEFAULT_SCALE,
        DEFAULT_OCTAVE,
        DEFAULT_VOLUME,
        DEFAULT_RPM,
        false,
        DEFAULT_SENSOR_SHIFT,
        false,
        HALL_THRESHOLD_DEFAULT,
        HALL_BASELINE_DEFAULT,
        1.0f,
        DEFAULT_PLAY_WELCOME_TUNE,
        DEFAULT_LCD_TIMEOUT,
        DEFAULT_BEATS_PER_REV
    };
}

void storageLoad(SavedConfig& cfg) {
    EEPROM.get(EEPROM_ADDRESS, cfg);
    if (cfg.magic != EEPROM_MAGIC || cfg.version != EEPROM_VERSION) {
        cfg = storageDefaults();
        storageSave(cfg);
    }
}

void storageSave(const SavedConfig& cfg) {
    EEPROM.put(EEPROM_ADDRESS, cfg);
}
