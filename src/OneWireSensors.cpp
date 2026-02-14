#include "OneWireSensors.h"

// ============================================================
//  OneWireSensors.cpp
// ============================================================

OneWireSensors::OneWireSensors(uint8_t pin)
    : _ow(pin), _dt(&_ow) {}

int OneWireSensors::begin() {
    _dt.begin();
    _count = 0;

    int found = _dt.getDeviceCount();
    if (found > kMaxSensors) found = kMaxSensors;

    for (int i = 0; i < found; i++) {
        if (_dt.getAddress(_addrs[i], i)) {
            _count++;
            Serial.printf("[1-Wire] Sensor %d ROM: ", i);
            for (int b = 0; b < 8; b++) {
                Serial.printf("%02X", _addrs[i][b]);
                if (b < 7) Serial.print(":");
            }
            Serial.println();
        } else {
            Serial.printf("[1-Wire] Failed to get address for sensor %d\n", i);
        }
    }

    // Use 12-bit resolution (0.0625 °C, ~750 ms conversion time)
    _dt.setResolution(12);

    Serial.printf("[1-Wire] Found %d DS18B20 sensor(s)\n", _count);
    return _count;
}

void OneWireSensors::requestAll() {
    _dt.requestTemperatures();
}

float OneWireSensors::getTemperatureC(int index) {
    if (index < 0 || index >= _count) return DEVICE_DISCONNECTED_C;
    return _dt.getTempC(_addrs[index]);
}

float OneWireSensors::getTemperatureK(int index) {
    float c = getTemperatureC(index);
    if (c == DEVICE_DISCONNECTED_C) return -1.0f;
    return c + 273.15f;
}

bool OneWireSensors::getAddress(int index, DeviceAddress addr) {
    if (index < 0 || index >= _count) return false;
    memcpy(addr, _addrs[index], sizeof(DeviceAddress));
    return true;
}
