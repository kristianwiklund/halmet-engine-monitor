#pragma once

// ============================================================
//  N2kSenders.h  —  NMEA 2000 PGN transmission helpers
//
//  Wraps the tNMEA2000 library calls for the PGNs used by this
//  project.  All values follow SI units as required by N2K:
//    Temperature  → Kelvin
//    Pressure     → Pascal
//    Speed/RPM    → as defined per PGN (double/rpm)
// ============================================================

#include <Arduino.h>
#include <NMEA2000.h>
#include <N2kMessages.h>

namespace N2kSenders {

// ----------------------------------------------------------
//  PGN 127488 — Engine Rapid Update  (10 Hz recommended)
//  Sends: engine RPM, boost pressure (0 if unknown), trim
// ----------------------------------------------------------
void sendEngineRapidUpdate(tNMEA2000& nmea2000,
                           uint8_t    engineInstance,
                           double     rpmValue);

// ----------------------------------------------------------
//  PGN 127489 — Engine Dynamic Parameters  (1 Hz)
//  Sends: oil pressure, coolant temperature, status bits
// ----------------------------------------------------------
void sendEngineDynamic(tNMEA2000& nmea2000,
                       uint8_t    engineInstance,
                       double     coolantTempK,
                       double     oilPressurePa,   // 0 if only binary available
                       bool       oilPressureLow,
                       bool       overTemperature,
                       double     alternatorVoltage = N2kDoubleNA);

// ----------------------------------------------------------
//  PGN 127505 — Fluid Level  (1 Hz)
//  Sends: tank fluid level as 0.0–100.0 percent
// ----------------------------------------------------------
void sendFluidLevel(tNMEA2000&      nmea2000,
                    uint8_t         tankInstance,
                    tN2kFluidType   fluidType,
                    double          levelPct,       // 0.0–100.0
                    double          capacityL);

// ----------------------------------------------------------
//  PGN 130316 — Temperature Extended Range  (0.1 Hz suggested)
//  Used for DS18B20 engine-room probes
// ----------------------------------------------------------
void sendTemperatureExtended(tNMEA2000&             nmea2000,
                             uint8_t                sensorInstance,
                             tN2kTempSource         source,
                             double                 actualTempK,
                             double                 setTempK = N2kDoubleNA);

}  // namespace N2kSenders
