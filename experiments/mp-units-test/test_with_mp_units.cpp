#include <mp-units/systems/si/si.h>
#include <mp-units/systems/isq/isq.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

constexpr auto ADC_TO_MILLIAMPS_SCALE_FACTOR = 10000;
constexpr auto CURRENT_SENSE_AMPLIFIER_GAIN = 1196;
constexpr quantity<isq::electric_current[mA]> ADC_ZERO_CURRENT_OFFSET = 0 * mA;
constexpr auto ADC_ZERO_CURRENT_OFFSET_RAW_VALUE = 1987;

quantity<isq::electric_current[mA]> calculateCurrentFromAdcValue(uint32_t adcRawValue) {
    if (adcRawValue < ADC_ZERO_CURRENT_OFFSET_RAW_VALUE) {
        return 0 * mA;
    }

    uint32_t adcValueWithoutOffset = adcRawValue - ADC_ZERO_CURRENT_OFFSET_RAW_VALUE;
    uint32_t currentMilliampsRaw = adcValueWithoutOffset * ADC_TO_MILLIAMPS_SCALE_FACTOR / CURRENT_SENSE_AMPLIFIER_GAIN;

    return currentMilliampsRaw * mA;
}

quantity<isq::voltage[mV]> calculateVoltageFromAdcValue(uint32_t adcRawValue) {
    constexpr uint32_t ADC_MAX_VALUE = 4095;
    constexpr auto REFERENCE_VOLTAGE = 3300 * mV;

    auto voltageMillivoltsRaw = (adcRawValue * REFERENCE_VOLTAGE.numerical_value_in(mV)) / ADC_MAX_VALUE;
    return voltageMillivoltsRaw * mV;
}

bool isCurrentAboveThreshold(quantity<isq::electric_current[mA]> current,
                             quantity<isq::electric_current[mA]> threshold) {
    return current > threshold;
}

int main() {
    uint32_t adcValue = 2500;
    auto current = calculateCurrentFromAdcValue(adcValue);
    auto voltage = calculateVoltageFromAdcValue(adcValue);

    constexpr auto MAXIMUM_SAFE_CURRENT = 5000 * mA;

    if (isCurrentAboveThreshold(current, MAXIMUM_SAFE_CURRENT)) {
        return 1;
    }

    return static_cast<int>(current.numerical_value_in(mA) + voltage.numerical_value_in(mV));
}
