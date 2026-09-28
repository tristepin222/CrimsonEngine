#pragma once

#include "ecs/Entity.hpp"
#include "core/EngineAPI.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// [ReflectStruct]
struct AtmosphereDensityLayer
{
    // [ReflectField]
    float width = 0.0f;
    // [ReflectField]
    float expTerm = 0.0f;
    // [ReflectField]
    float expScale = 0.0f;
    // [ReflectField]
    float linearTerm = 0.0f;
    // [ReflectField]
    float constantTerm = 0.0f;
};

// [ReflectStruct]
struct AtmosphereDensityProfile
{
    // [ReflectField]
    AtmosphereDensityLayer layers[2];
};

namespace AtmosphereDefaults
{
    inline AtmosphereDensityProfile rayleighDensity()
    {
        AtmosphereDensityProfile profile{};

        profile.layers[0] = {
            .width = 0.0f,
            .expTerm = 1.0f,
            .expScale = -1.0f / 8000.0f,
            .linearTerm = 0.0f,
            .constantTerm = 0.0f
        };

        profile.layers[1] = {};

        return profile;
    }

    inline AtmosphereDensityProfile mieDensity()
    {
        AtmosphereDensityProfile profile{};

        profile.layers[0] = {
            .width = 0.0f,
            .expTerm = 1.0f,
            .expScale = -1.0f / 1200.0f,
            .linearTerm = 0.0f,
            .constantTerm = 0.0f
        };

        profile.layers[1] = {};

        return profile;
    }

    inline AtmosphereDensityProfile ozoneDensity()
    {
        AtmosphereDensityProfile profile{};

        profile.layers[0] = {
            .width = 25'000.0f,
            .expTerm = 0.0f,
            .expScale = 0.0f,
            .linearTerm = 1.0f / 15'000.0f,
            .constantTerm = 0.0f
        };

        profile.layers[1] = {
            .width = 15'000.0f,
            .expTerm = 0.0f,
            .expScale = 0.0f,
            .linearTerm = -1.0f / 15'000.0f,
            .constantTerm = 1.0f
        };

        return profile;
    }
};

// [ReflectClass("Atmosphere/Atmosphere Component")]
struct AtmosphereComponent
{
    // Planet geometry

    // [ReflectField]
    float bottomRadius = 6'371'000.0f;
    // [ReflectField]
    float topRadius = 6'471'000.0f;

    // Optical properties

    // [ReflectField]
    glm::vec3 rayleighScattering{
        5.802e-6f,
        13.558e-6f,
        33.100e-6f
    };

    // [ReflectField]
    glm::vec3 rayleighAbsorption{0.0f};

    // [ReflectField]
    glm::vec3 mieScattering{
        3.996e-6f
    };

    // [ReflectField]
    glm::vec3 mieAbsorption{
        4.400e-6f
    };

    // [ReflectField]
    glm::vec3 absorptionExtinction{0.0f};

    // Density profiles

    // [ReflectField]
    AtmosphereDensityProfile rayleighDensity =
        AtmosphereDefaults::rayleighDensity();

    // [ReflectField]
    AtmosphereDensityProfile mieDensity =
        AtmosphereDefaults::mieDensity();

    // [ReflectField]
    AtmosphereDensityProfile absorptionDensity =
        AtmosphereDefaults::ozoneDensity();

    // Ground and lighting

    // [ReflectField]
    glm::vec3 groundAlbedo{0.1f};
    // [ReflectField]
    glm::vec3 solarIrradiance{1.0f};

    // [ReflectField]
    float sunAngularRadius = 0.004675f;

    // Mie phase function

    // [ReflectField]
    float mieAnisotropy = 0.8f;

    // Runtime controls

    // [ReflectField]
    bool enableScattering = true;
    // [ReflectField]
    bool enableOzone = true;

    // Night Sky & Celestial Bodies

    // [ReflectField]
    bool enableStars = true;
    // [ReflectField]
    float starIntensity = 1.0f;
    // [ReflectField]
    float starTwinkleSpeed = 1.0f;

    // [ReflectField]
    bool enableMoon = true;
    // [ReflectField]
    float moonAngularRadius = 0.025f;
    // [ReflectField]
    float moonIntensity = 1.5f;
    // [ReflectField]
    float moonPhase = -1.0f;
    // [ReflectField]
    glm::vec3 moonDirection{0.0f, -1.0f, 0.0f};
    // [ReflectField]
    bool autoMoonDirection = true;
};