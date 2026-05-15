#pragma once
#include <Arduino.h>

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual bool publish(const String& topic, const String& payload, bool retain = false) = 0;
    virtual bool subscribe(const String& topic) = 0;
};
