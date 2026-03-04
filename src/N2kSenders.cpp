#include "N2kSenders.h"
#include <N2kMessages.h>

// ============================================================
//  N2kSenders.cpp
// ============================================================

namespace N2kSenders {

// ----------------------------------------------------------
void sendEngineRapidUpdate(tNMEA2000& nmea2000,
                           uint8_t    engineInstance,
                           double     rpmValue) {
    tN2kMsg msg;
    // boost pressure and trim not applicable on MD7A — pass NA
    SetN2kEngineParamRapid(msg,
                           engineInstance,
                           rpmValue,
                           N2kDoubleNA,   // boost pressure (Pa)
                           N2kInt8NA);    // trim
    nmea2000.SendMsg(msg);
}

// ----------------------------------------------------------
void sendEngineDynamic(tNMEA2000& nmea2000,
                       uint8_t    engineInstance,
                       double     coolantTempK,
                       bool       oilPressureLow,
                       bool       overTemperature) {

    tN2kMsg msg;
    tN2kEngineDiscreteStatus1 status1 = {};
    tN2kEngineDiscreteStatus2 status2 = {};

    status1.Bits.LowOilPressure  = oilPressureLow                    ? 1 : 0;
    status1.Bits.OverTemperature = overTemperature                    ? 1 : 0;
    status1.Bits.CheckEngine     = (oilPressureLow || overTemperature) ? 1 : 0;

    SetN2kEngineDynamicParam(msg,
                             engineInstance,
                             N2kDoubleNA,   // EngineOilPress  — not measurable on MD7A
                             N2kDoubleNA,   // EngineOilTemp   — not measurable on MD7A
                             coolantTempK,  // EngineCoolantTemp (K)
                             N2kDoubleNA,   // AlternatorVoltage — not measurable on MD7A
                             N2kDoubleNA,   // FuelRate
                             N2kDoubleNA,   // EngineHours
                             N2kDoubleNA,   // EngineCoolantPressure
                             N2kDoubleNA,   // FuelPressure
                             N2kInt8NA,     // EngineLoad
                             N2kInt8NA,     // EngineTorque
                             status1,
                             status2);
    nmea2000.SendMsg(msg);
}

// ----------------------------------------------------------
void sendBinaryStatus(tNMEA2000& nmea2000,
                      uint8_t    bankInstance,
                      bool       relayOn) {
    tN2kMsg msg;
    tN2kBinaryStatus bankStatus;
    N2kResetBinaryStatus(bankStatus);
    N2kSetStatusBinaryOnStatus(bankStatus,
                               relayOn ? N2kOnOff_On : N2kOnOff_Off,
                               1);  // switch index 1 (1-based in library)
    SetN2kBinaryStatus(msg, bankInstance, bankStatus);
    nmea2000.SendMsg(msg);
}

// ----------------------------------------------------------
void sendFluidLevel(tNMEA2000&    nmea2000,
                    uint8_t       tankInstance,
                    tN2kFluidType fluidType,
                    double        levelPct,
                    double        capacityL) {
    tN2kMsg msg;
    SetN2kFluidLevel(msg,
                     tankInstance,
                     fluidType,
                     levelPct,
                     capacityL);
    nmea2000.SendMsg(msg);
}

// ----------------------------------------------------------
void sendTemperatureExtended(tNMEA2000&     nmea2000,
                             uint8_t        sensorInstance,
                             tN2kTempSource source,
                             double         actualTempK,
                             double         setTempK) {
    tN2kMsg msg;
    SetN2kTemperatureExt(msg,
                         0xFF,            // SID — not used
                         sensorInstance,
                         source,
                         actualTempK,
                         setTempK);
    nmea2000.SendMsg(msg);
}

}  // namespace N2kSenders
