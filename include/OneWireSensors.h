#pragma once

// ============================================================
//  OneWireSensors.h  —  DS18B20 engine-room temperature chain
//
//  Discovers all DS18B20 sensors on the 1-Wire bus at startup,
//  stores their ROM addresses, and provides a simple polling
//  interface returning temperature in °C per sensor.
//
//  Usage:
//    OneWireSensors ow(HALMET_PIN_1WIRE);
//    ow.begin();                        // in setup()
//    ow.requestAll();                   // start conversion
//    delay(750);                        // DS18B20 needs ~750 ms
//    float t = ow.getTemperatureC(0);   // read sensor 0
// ============================================================

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

class OneWireSensors {
public:
    static constexpr int kMaxSensors = 12;

    explicit OneWireSensors(uint8_t pin);

    /// Discover sensors and print ROM addresses to Serial.
    /// Returns the number of sensors found.
    int  begin();

    int  count() const { return _count; }

    /// Trigger temperature conversion on all sensors.
    void requestAll();

    /// Read temperature (°C) for sensor by index.
    /// Returns -127 on error.
    float getTemperatureC(int index);

    /// Read temperature (K) for sensor by index.
    float getTemperatureK(int index);

    /// Copy ROM address of sensor[index] into addr (8 bytes).
    bool getAddress(int index, DeviceAddress addr);

private:
    OneWire          _ow;
    DallasTemperature _dt;
    DeviceAddress    _addrs[kMaxSensors];
    int              _count = 0;
};
