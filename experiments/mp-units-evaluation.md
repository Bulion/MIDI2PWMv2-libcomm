# mp-units Library Evaluation for Embedded Use

## Executive Summary

After evaluating the mp-units library (v2.2.0) for integration into the MIDI2PWMv2 embedded project, the recommendation is to **NOT integrate mp-units** and instead continue with the unit-in-variable-name convention.

## Evaluation Criteria

- Binary size overhead: Must be <10KB for STM32G473 firmware
- Compilation complexity: Must integrate easily with existing CMake build
- Zero runtime overhead: Required for real-time embedded systems
- Ease of use: Should improve, not complicate, code readability

## Findings

### Positive Aspects

1. **Zero Runtime Overhead**: mp-units is designed as a compile-time library with no runtime performance penalty
2. **Type Safety**: Provides strong compile-time type checking for physical quantities
3. **C++20 Compatible**: Aligns with our upgrade to C++20
4. **ISO C++ Standardization**: Being proposed for C++ standard (P2980/P2981)

### Negative Aspects - Integration Blockers

1. **Complex Dependencies**: Requires gsl-lite and other external dependencies
   - CMake configuration error: "By not providing Findgsl-lite.cmake..."
   - Requires specific SOURCE_SUBDIR configuration
   - Not straightforward FetchContent integration

2. **Compilation Time Overhead**: Known compilation time concerns
   - Quote from GitHub discussion: "It's just a library to handle units! It shouldn't take more that millisecond to convert from m/s to km/hr"
   - Template-heavy implementation increases build times
   - Problematic for iterative embedded development

3. **Binary Size Unknown**: Unable to measure due to build failures
   - Could not complete test build to measure actual overhead
   - Risk of exceeding 10KB threshold for embedded target
   - Header-only library may cause code bloat

4. **Learning Curve**: Complex API for straightforward use cases
   - Example: `quantity<isq::electric_current[mA]>` vs `uint32_t currentMilliamps`
   - Overkill for embedded firmware where physical quantities are well-known

### Alternative Approach: Unit-in-Variable-Name Convention

The project already has established clear naming conventions that provide clarity without library overhead:

```cpp
// Clear and self-documenting without mp-units
uint32_t calculateCurrentMilliampsFromAdcValue(uint32_t adcRawValue);
uint32_t calculateVoltageMillivoltsFromAdcValue(uint32_t adcRawValue);
bool isCurrentAboveThresholdMilliamps(uint32_t currentMilliamps,
                                      uint32_t thresholdMilliamps);
```

Benefits of this approach:
- **Zero overhead**: No library, no binary size increase
- **Instantly readable**: Units are explicit in names
- **No build complexity**: Works with any C++20 compiler
- **No learning curve**: Standard C++ types
- **Fast compilation**: No template instantiation overhead

## Recommendation

**Decision: Do NOT integrate mp-units**

**Rationale:**
1. Build integration complexity outweighs benefits for this embedded project
2. Unit-in-variable-name convention already provides clear documentation
3. Cannot verify binary size overhead due to build failures
4. Compilation time overhead is unacceptable for embedded development workflow
5. Type safety benefits do not justify integration complexity

**Action Items:**
1. Continue using verbose variable names with embedded units (Milliseconds, Milliamps, etc.)
2. Do NOT create additional typedefs (per user requirement)
3. Enforce naming convention through code reviews
4. Document unit conventions in coding standards

## Test Artifacts

Attempted build configuration saved in:
- `experiments/mp-units-test/CMakeLists.txt`
- `experiments/mp-units-test/test_with_mp_units.cpp` (example usage)
- `experiments/mp-units-test/test_without_mp_units.cpp` (baseline comparison)

These files demonstrate the intended comparison but could not be completed due to dependency issues.

## Date

2025-10-15

## References

- mp-units GitHub: https://github.com/mpusz/mp-units
- ISO C++ Proposal P2980: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2980r0.html
- ISO C++ Proposal P2981: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2981r1.html
