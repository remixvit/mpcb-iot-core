#pragma once
#include <Arduino.h>

class RingLog {
public:
    static constexpr uint8_t  MAX_ENTRIES = 60;
    static constexpr uint8_t  ENTRY_LEN   = 120;

    void log(const char* tag, const String& msg);
    void log(const String& msg) { log("", msg); }

    // Call from loop()
    void tick();

    // Returns all buffered entries as plain text (for HTTP endpoint)
    String toText() const;

    uint8_t count() const { return _count; }
    void    clear();

private:
    void _dumpSerial();

    char     _buf[MAX_ENTRIES][ENTRY_LEN];
    uint8_t  _head      = 0;
    uint8_t  _count     = 0;
    uint32_t _lastHint  = 0;
};

// Global instance — include this header and use Log.log() anywhere
extern RingLog Log;
