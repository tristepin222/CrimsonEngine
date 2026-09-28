#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>

/**
 * @enum WeatherType
 * @brief Categorization of active atmospheric precipitation and storm conditions.
 */
enum class WeatherType : uint32_t {
    Clear = 0,
    Rain = 1,
    Snow = 2,
    Storm = 3
};

/**
 * @struct WeatherComponent
 * @brief Configures dynamic weather, precipitation (rain & snow), ground surface wetness,
 *        wind, and thunderstorm lightning/audio simulation.
 */
// [ReflectClass("Environment/Weather")]
struct WeatherComponent {
    /** @brief Active weather condition. */
    // [ReflectField]
    WeatherType type = WeatherType::Clear;

    /** @brief Internal state to detect user-initiated weather changes in Inspector. */
    WeatherType lastAppliedType = WeatherType::Clear;

    /** @brief Precipitation density and streak/snowflake count (0.0 = dry, 1.0 = heavy downpour / blizzard). */
    // [ReflectField]
    float precipitationIntensity = 0.0f;

    /** @brief Ground surface wetness fraction and puddle accumulation (0.0 = bone dry, 1.0 = saturated puddles). */
    // [ReflectField]
    float wetness = 0.0f;

    /** @brief Automatically accumulates wetness during precipitation and dries under clear skies. */
    // [ReflectField]
    bool autoWetness = true;

    /** @brief Evaporation rate of ground surface wetness per second when not raining. */
    // [ReflectField]
    float dryingSpeed = 0.04f;

    /** @brief Accumulation rate of ground surface wetness per second during active rain. */
    // [ReflectField]
    float wetnessAccumSpeed = 0.12f;

    // --- Wind ---

    /** @brief Wind direction in the XZ plane (will be normalized). Controls precipitation drift angle. */
    // [ReflectField]
    glm::vec2 windDirection = glm::vec2(1.0f, 0.0f);

    /** @brief Wind speed in meters per second. Affects precipitation streak angle and drift speed. */
    // [ReflectField]
    float windSpeed = 8.0f;

    // --- Lightning ---

    /** @brief Whether procedural lightning strikes and thunder are active (typically in Storm). */
    // [ReflectField]
    bool enableLightning = true;

    /** @brief Average interval in seconds between procedural lightning strikes. */
    // [ReflectField]
    float lightningFrequency = 7.0f;

    /** @brief Brightness and illumination strength of lightning flashes. */
    // [ReflectField]
    float lightningIntensity = 1.0f;

    // --- Audio ---

    /** @brief Enables ambient rain audio loop and thunderclap sound effects. */
    // [ReflectField]
    bool enableAudio = true;

    /** @brief Volume multiplier for ambient rain sound loop (0.0 to 1.0). */
    // [ReflectField]
    float rainVolume = 0.70f;

    /** @brief Volume multiplier for thunder sound effects (0.0 to 1.0). */
    // [ReflectField]
    float thunderVolume = 0.90f;

    // --- Runtime Simulation State (Not serialized/reflected) ---
    float lightningTimer = 3.0f;          // Time until next strike
    float flashIntensity = 0.0f;          // Current instantaneous flash output [0, 1]
    float flashDuration = 0.0f;           // Elapsed duration of active flash sequence
    int flashBurstsRemaining = 0;         // Multi-burst flickering count
    float nextBurstTimer = 0.0f;          // Sub-flash timing
    float thunderAudioDelay = -1.0f;      // Speed-of-sound delay countdown
    bool isDistantThunder = false;        // Whether next thunder sound is distant or direct strike

    /** @brief Applies default physical parameters corresponding to the chosen weather condition. */
    void applyWeatherType(WeatherType targetType) {
        type = targetType;
        switch (targetType) {
            case WeatherType::Clear:
                precipitationIntensity = 0.0f;
                windSpeed = 3.0f;
                enableLightning = false;
                break;
            case WeatherType::Rain:
                precipitationIntensity = 0.65f;
                windSpeed = 6.0f;
                enableLightning = false;
                break;
            case WeatherType::Snow:
                precipitationIntensity = 0.60f;
                windSpeed = 4.0f;
                enableLightning = false;
                break;
            case WeatherType::Storm:
                precipitationIntensity = 0.95f;
                windSpeed = 14.0f;
                enableLightning = true;
                lightningFrequency = 6.0f;
                lightningIntensity = 1.0f;
                break;
            default:
                break;
        }
    }
};
