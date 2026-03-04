#pragma once

// ============================================================
//  N2kSenders.h  —  NMEA 2000 PGN transmission helpers
//
//  Wraps the tNMEA2000 library calls for the PGNs used by this
//  project.  All values follow SI units as required by N2K:
//    Temperature  → Kelvin
//    Speed/RPM    → as defined per PGN (double/rpm)
//
//  Fields not measurable on this engine (Volvo Penta MD7A):
//    boost pressure, trim, oil pressure — omitted; library
//    receives N2kDoubleNA / N2kInt8NA internally.
// ============================================================

#include <Arduino.h>
#include <NMEA2000.h>
#include <N2kMessages.h>

namespace N2kSenders {

// ----------------------------------------------------------
//  PGN 127488 — Engine Rapid Update  (10 Hz recommended)
//  Sends: engine RPM only (boost/trim not available on MD7A)
// ----------------------------------------------------------
void sendEngineRapidUpdate(tNMEA2000& nmea2000,
                           uint8_t    engineInstance,
                           double     rpmValue);

// ----------------------------------------------------------
//  PGN 127489 — Engine Dynamic Parameters  (1 Hz)
//  Sends: coolant temperature, oil/overheat alarm status bits
//  (oil pressure not measurable on MD7A — digital alarm only)
// ----------------------------------------------------------
void sendEngineDynamic(tNMEA2000& nmea2000,
                       uint8_t    engineInstance,
                       double     coolantTempK,
                       bool       oilPressureLow,
                       bool       overTemperature);

// ----------------------------------------------------------
//  PGN 127501 — Binary Switch Bank Status  (1 Hz)
//  Reports relay on/off state so MFDs stay in sync with the
//  bilge fan.  Switch bank instance 0, switch index 1.
// ----------------------------------------------------------
void sendBinaryStatus(tNMEA2000& nmea2000,
                      uint8_t    bankInstance,
                      bool       relayOn);

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
