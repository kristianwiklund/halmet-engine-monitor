// ============================================================
//  analog_inputs.cpp — Coolant temp (INA226), tank level,
//                      battery voltage, ADS1115 recovery
// ============================================================

#include "analog_inputs.h"

#include <Arduino.h>
#include <Wire.h>
#include <cmath>
#include <Adafruit_ADS1X15.h>
#include <INA226_WE.h>
#include <N2kMsg.h>
#include <sensesp.h>
#include <sensesp/sensors/sensor.h>
#include <sensesp/system/observablevalue.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/ui/config_item.h>

#include "halmet_config.h"
#include "engine_state.h"

using namespace sensesp;

namespace analog_inputs {

// Helper: update coolant alert state and emit SK notification
static void updateCoolantAlert(EngineState* st, float celsius,
                               PersistingObservableValue<float>* povWarn,
                               PersistingObservableValue<float>* povAlarm,
                               SKOutputRawJson* skNotif) {
    float warnC  = povWarn  ? povWarn->get()  : DEFAULT_COOLANT_WARN_C;
    float alarmC = povAlarm ? povAlarm->get() : DEFAULT_COOLANT_ALARM_C;
    auto newState = CoolantAlertState::NORMAL;
    if (celsius >= alarmC)      newState = CoolantAlertState::ALARM;
    else if (celsius >= warnC)  newState = CoolantAlertState::WARN;

    if (newState != st->coolantAlertState) {
        st->coolantAlertState = newState;
        if (skNotif) {
            if (newState == CoolantAlertState::NORMAL) {
                skNotif->set("null");
            } else {
                const char* state = (newState == CoolantAlertState::ALARM) ? "alarm" : "warn";
                char buf[192];
                snprintf(buf, sizeof(buf),
                    "{\"state\":\"%s\",\"method\":[\"visual\",\"sound\"],"
                    "\"message\":\"Coolant %.0f°C (%s threshold)\"}",
                    state, celsius, state);
                skNotif->set(String(buf));
            }
        }
    }
}

void init(const InitParams& p) {
    EngineState*                       st      = p.state;
    Adafruit_ADS1115*                  ads     = p.ads;
    INA226_WE*                         ina     = p.ina226;
    SKOutputRawJson*                   skNotif = p.skCoolantNotification;
    PersistingObservableValue<float>*  povWarn  = p.coolantWarnC;
    PersistingObservableValue<float>*  povAlarm = p.coolantAlarmC;

    // ---- Coolant temp via INA226 (resistance → °C) ----
    auto* resCurve = new CurveInterpolator(nullptr, "/coolant/resistance_curve");
    resCurve->set_input_title("Sender Resistance (ohms)")
            ->set_output_title("Temperature (°C)");

    if (resCurve->get_samples().empty()) {
        // Default: VDO Type A (European) NTC curve
        resCurve->clear_samples();
        resCurve->add_sample(CurveInterpolator::Sample(287.0, 40.0));
        resCurve->add_sample(CurveInterpolator::Sample(187.0, 50.0));
        resCurve->add_sample(CurveInterpolator::Sample(124.0, 60.0));
        resCurve->add_sample(CurveInterpolator::Sample(84.0,  70.0));
        resCurve->add_sample(CurveInterpolator::Sample(58.5,  80.0));
        resCurve->add_sample(CurveInterpolator::Sample(41.0,  90.0));
        resCurve->add_sample(CurveInterpolator::Sample(29.7, 100.0));
        resCurve->add_sample(CurveInterpolator::Sample(21.5, 110.0));
        resCurve->add_sample(CurveInterpolator::Sample(16.0, 120.0));
    }

    ConfigItem(resCurve)
        ->set_title("Coolant temperature curve")
        ->set_description("Sender resistance (ohms) to temperature (°C). Default: VDO Type A (European)");

    event_loop()->onRepeat(INTERVAL_INA226_MS, [st, ina, resCurve, skNotif, povWarn, povAlarm]() {
        if (!ina || !st->ina226Ok) return;
        float busV   = ina->getBusVoltage_V();
        float shuntI = ina->getCurrent_mA() / 1000.0f;  // convert to A
        if (shuntI <= 0.001f) {
            st->coolantK = N2kDoubleNA;
            st->senderResistanceOhm = NAN;
            return;
        }
        float resistance = busV / shuntI;
        st->senderResistanceOhm = resistance;

        if (resistance < COOLANT_RESISTANCE_MIN_OHM || resistance > COOLANT_RESISTANCE_MAX_OHM) {
            st->coolantK = N2kDoubleNA;
            return;
        }

        // Feed resistance into CurveInterpolator
        resCurve->set(resistance);
        float celsius = resCurve->get();
        if (std::isnan(celsius)) {
            st->coolantK = N2kDoubleNA;
        } else {
            st->coolantK = celsius + 273.15;
            st->coolantLastUpdateMs = millis();
            updateCoolantAlert(st, celsius, povWarn, povAlarm, skNotif);
        }
    });

#ifdef TANK_SENSOR_GOBIUS
    // Gobius Pro binary threshold sensors on ADS ch1 + ch2 (500 ms)
    auto* skTankLevel = new SKOutputFloat("tanks.fuel.0.currentLevel");
    event_loop()->onRepeat(INTERVAL_TANK_MS, [st, ads, skTankLevel]() {
        if (!st->adsOk) return;
        bool below3q = ads->computeVolts(ads->readADC_SingleEnded(1))
                       < GOBIUS_THRESHOLD_VOLTAGE;
        bool below1q = ads->computeVolts(ads->readADC_SingleEnded(2))
                       < GOBIUS_THRESHOLD_VOLTAGE;

        if (below1q)      st->tankLevelPct = TANK_LEVEL_LOW_PCT;
        else if (below3q) st->tankLevelPct = TANK_LEVEL_MID_PCT;
        else              st->tankLevelPct = TANK_LEVEL_HIGH_PCT;

        skTankLevel->set(st->tankLevelPct / 100.0f);
    });
#else
    // Resistive sender on ADS ch1 via 10 mA constant-current source (500 ms)
    // R = V_adc / I  (no voltage divider on this input)
    auto* resistance = new RepeatSensor<float>(
        INTERVAL_TANK_MS, [st, ads]() -> float {
            if (!st->adsOk) return NAN;
            float v = ads->computeVolts(ads->readADC_SingleEnded(TANK_SENDER_CHANNEL));
            float r = v / TANK_MEASUREMENT_CURRENT;
            return (r < 0.0f || r > TANK_RESISTANCE_MAX_OHM) ? NAN : r;
        });

    auto* curve = new CurveInterpolator(nullptr, "/tank/resistance_curve");
    curve->set_input_title("Sender Resistance (ohms)")
         ->set_output_title("Tank Level (ratio 0-1)");

    if (curve->get_samples().empty()) {
        // Default: European VDO fuel sender (10 Ω = empty, 180 Ω = full)
        curve->clear_samples();
        curve->add_sample(CurveInterpolator::Sample(TANK_RESISTANCE_EMPTY_OHM, 0.0));
        curve->add_sample(CurveInterpolator::Sample(TANK_RESISTANCE_FULL_OHM,  1.0));
    }

    ConfigItem(curve)
        ->set_title("Tank level curve")
        ->set_description("Calibration table: sender resistance (ohms) to level (0=empty, 1=full)");

    resistance->connect_to(curve);
    curve->connect_to(new SKOutputFloat("tanks.fuel.0.currentLevel"));

    // Update shared state for N2K PGN 127505
    curve->attach([curve, st]() {
        float ratio = curve->get();
        if (!std::isnan(ratio)) {
            st->tankLevelPct = ratio * 100.0f;
        }
    });
#endif

    // Battery voltage on A4 / ADS ch3
    PersistingObservableValue<float>* povVoltMul = p.voltageMultiplier;
    auto* skVoltage = new SKOutputFloat("electrical.batteries.0.voltage");
    event_loop()->onRepeat(INTERVAL_VOLTAGE_MS, [st, ads, povVoltMul, skVoltage]() {
        if (!st->adsOk) return;
        float v = ads->computeVolts(ads->readADC_SingleEnded(VOLTAGE_CHANNEL));
        st->supplyVoltageV = v * povVoltMul->get();
        skVoltage->set(st->supplyVoltageV);
    });

    // ADS1115 I2C recovery (retry when not present)
    event_loop()->onRepeat(INTERVAL_ADS_RETRY_MS, [st, ads]() {
        if (st->adsOk) return;
        Wire.begin(HALMET_PIN_SDA, HALMET_PIN_SCL);
        Wire.setClock(400000);
        st->adsOk = ads->begin(ADS1115_I2C_ADDRESS, &Wire);
        if (st->adsOk) {
            ads->setGain(GAIN_ONE);
            ads->setDataRate(RATE_ADS1115_8SPS);
            ESP_LOGI("HALMET", "ADS1115 recovered on I2C retry");
        } else {
            st->adsFailCount++;
        }
    });
}

}  // namespace analog_inputs
