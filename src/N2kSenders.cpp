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
    // boost pressure and trim are not available — pass NA
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
                       double     oilPressurePa,
                       bool       oilPressureLow,
                       bool       overTemperature,
                       double     alternatorVoltage) {

    tN2kMsg msg;
    tN2kEngineDiscreteStatus1 status1;
    tN2kEngineDiscreteStatus2 status2;

    status1.Bits.LowOilPressure   = oilPressureLow    ? 1 : 0;
    status1.Bits.OverTemperature  = overTemperature    ? 1 : 0;

    // Actual library signature (from N2kMessages.h:1172):
    // (msg, instance, OilPress, OilTemp, CoolantTemp, AlternatorVoltage,
    //  FuelRate, EngineHours, CoolantPressure, FuelPressure,
    //  EngineLoad, EngineTorque, Status1, Status2)
    // Note: OilTemp is a separate field from CoolantTemp.
    //       Status structs come AFTER the two int8_t load/torque fields.
    SetN2kEngineDynamicParam(msg,
                             engineInstance,
                             oilPressurePa,     // EngineOilPress (Pa)
                             N2kDoubleNA,       // EngineOilTemp  (K) — not measured
                             coolantTempK,      // EngineCoolantTemp (K)
                             alternatorVoltage, // AlternatorVoltage (V)
                             N2kDoubleNA,       // FuelRate (L/h)
                             N2kDoubleNA,       // EngineHours (s)
                             N2kDoubleNA,       // EngineCoolantPressure
                             N2kDoubleNA,       // FuelPressure
                             N2kInt8NA,         // EngineLoad (%)
                             N2kInt8NA,         // EngineTorque (%)
                             status1,
                             status2);
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
