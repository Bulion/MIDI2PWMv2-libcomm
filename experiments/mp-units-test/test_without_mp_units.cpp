constexpr uint32_t ADC_TO_MILLIAMPS_SCALE_FACTOR = 10000;
constexpr uint32_t CURRENT_SENSE_AMPLIFIER_GAIN = 1196;
constexpr uint16_t ADC_ZERO_CURRENT_OFFSET_VALUE = 1987;

uint32_t calculateCurrentMilliampsFromAdcValue(uint32_t adcRawValue) {
    if (adcRawValue < ADC_ZERO_CURRENT_OFFSET_VALUE) {
        return 0;
    }

    uint32_t adcValueWithoutOffset = adcRawValue - ADC_ZERO_CURRENT_OFFSET_VALUE;
    return adcValueWithoutOffset * ADC_TO_MILLIAMPS_SCALE_FACTOR / CURRENT_SENSE_AMPLIFIER_GAIN;
}

uint32_t calculateVoltageMillivoltsFromAdcValue(uint32_t adcRawValue) {
    constexpr uint32_t ADC_MAX_VALUE = 4095;
    constexpr uint32_t REFERENCE_VOLTAGE_MILLIVOLTS = 3300;

    return (adcRawValue * REFERENCE_VOLTAGE_MILLIVOLTS) / ADC_MAX_VALUE;
}

bool isCurrentAboveThresholdMilliamps(uint32_t currentMilliamps, uint32_t thresholdMilliamps) {
    return currentMilliamps > thresholdMilliamps;
}

int main() {
    uint32_t adcValue = 2500;
    uint32_t currentMilliamps = calculateCurrentMilliampsFromAdcValue(adcValue);
    uint32_t voltageMillivolts = calculateVoltageMillivoltsFromAdcValue(adcValue);

    constexpr uint32_t MAXIMUM_SAFE_CURRENT_MILLIAMPS = 5000;

    if (isCurrentAboveThresholdMilliamps(currentMilliamps, MAXIMUM_SAFE_CURRENT_MILLIAMPS)) {
        return 1;
    }

    return static_cast<int>(currentMilliamps + voltageMillivolts);
}
