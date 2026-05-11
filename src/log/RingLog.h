#pragma once
#include <Arduino.h>

class RingLog {
public:
    static constexpr uint8_t  MAX_ENTRIES = 60;
    static constexpr uint8_t  ENTRY_LEN   = 120;

    void log(const char* tag, const String& msg);
    void log(const String& msg) { log("", msg); }

    // Call from loop() — dumps buffer when serial terminal connects
    void tick();

    uint8_t count() const { return _count; }
    void    clear();

private:
    void _dump();

    char    _buf[MAX_ENTRIES][ENTRY_LEN];
    uint8_t _head        = 0;
    uint8_t _count       = 0;
    bool    _wasSerial   = false;
};

// Global instance — include this header and use Log.log() anywhere
extern RingLog Log;
