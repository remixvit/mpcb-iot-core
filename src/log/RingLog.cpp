#include "RingLog.h"

RingLog Log;

void RingLog::log(const char* tag, const String& msg) {
    // Always try Serial — on HWCDC this works when terminal is open,
    // silently drops when FIFO is full (no terminal). Accepted tradeoff.
    if (tag && tag[0]) {
        Serial.print('['); Serial.print(tag); Serial.print("] ");
    }
    Serial.println(msg);

    // Build timestamped entry for ring buffer
    char entry[ENTRY_LEN];
    uint32_t s = millis() / 1000;
    snprintf(entry, ENTRY_LEN, "[%lu:%02lu]%s%s%s %s",
        s / 60, s % 60,
        (tag && tag[0]) ? "[" : "",
        (tag && tag[0]) ? tag  : "",
        (tag && tag[0]) ? "]"  : "",
        msg.c_str());

    strncpy(_buf[_head], entry, ENTRY_LEN - 1);
    _buf[_head][ENTRY_LEN - 1] = '\0';
    _head = (_head + 1) % MAX_ENTRIES;
    if (_count < MAX_ENTRIES) _count++;
}

void RingLog::tick() {
    // On HWCDC: dump buffer when Serial input received (user pressed key)
    // This is the only reliable way to detect "terminal is open" on HWCDC
    if (Serial.available() > 0) {
        while (Serial.available()) Serial.read();  // drain input
        if (_count > 0) {
            Serial.println(F("\n=== Log buffer ==="));
            _dumpSerial();
            Serial.println(F("==================\n"));
        } else {
            Serial.println(F("[LOG] Buffer empty"));
        }
    }

    // Periodic hint so user knows to press a key
    if (_count > 0 && millis() - _lastHint > 8000) {
        _lastHint = millis();
        Serial.print(F("[LOG] "));
        Serial.print(_count);
        Serial.println(F(" entries buffered — press any key to show, or open /logs in browser"));
    }
}

void RingLog::_dumpSerial() {
    uint8_t start = (_head - _count + MAX_ENTRIES) % MAX_ENTRIES;
    for (uint8_t i = 0; i < _count; i++) {
        Serial.println(_buf[(start + i) % MAX_ENTRIES]);
    }
    clear();
}

String RingLog::toText() const {
    if (_count == 0) return "-- buffer empty --\n";
    String out;
    out.reserve(_count * 60);
    uint8_t start = (_head - _count + MAX_ENTRIES) % MAX_ENTRIES;
    for (uint8_t i = 0; i < _count; i++) {
        out += _buf[(start + i) % MAX_ENTRIES];
        out += '\n';
    }
    return out;
}

void RingLog::clear() {
    _head  = 0;
    _count = 0;
}
