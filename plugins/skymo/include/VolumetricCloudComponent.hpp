#pragma once

#include "ecs/Entity.hpp"
#include "core/EngineAPI.hpp"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

/**
 * @enum CloudPreset
 * @brief High-level weather presets for rapid visual atmosphere styling.
 */
enum class CloudPreset : int {
    Custom = 0,
    ClearSky = 1,
    FairWeather = 2,
    Scattered = 3,
    Broken = 4,
    Overcast = 5,
    Stormy = 6
};

/**
 * @struct VolumetricCloudComponent
 * @brief Configures physically-based volumetric raymarched clouds in the atmosphere.
 *        Can be placed on any entity to define a dynamic 3D cloud deck.
 */
// [ReflectClass("Environment/Volumetric Clouds")]
struct VolumetricCloudComponent {
    /** @brief Weather preset for rapid atmospheric setup. */
    // [ReflectField]
    CloudPreset preset = CloudPreset::Scattered;

    /** @brief Internal tracking to apply preset when changed in the Inspector. */
    CloudPreset lastAppliedPreset = CloudPreset::Scattered;

    /** @brief Whether volumetric clouds are actively simulated and rendered. */
    // [ReflectField]
    bool enabled = true;

    /** @brief Altitude (meters above ground) where the base of the cloud layer begins. */
    // [ReflectField]
    float bottomAltitude = 1200.0f;

    /** @brief Altitude (meters above ground) where the summit of the cloud layer ends. */
    // [ReflectField]
    float topAltitude = 3800.0f;

    /** @brief Global cloud coverage fraction from 0.0 (clear sky) to 1.0 (overcast). */
    // [ReflectField]
    float coverage = 0.45f;

    /** @brief Optical density multiplier controlling cloud opacity and thickness. */
    // [ReflectField]
    float density = 1.0f;

    /** @brief World-space spatial frequency for base cumulus cauliflower billows. */
    // [ReflectField]
    float cloudScale = 0.00018f;

    /** @brief World-space spatial frequency for fine edge wisps and erosion. */
    // [ReflectField]
    float detailScale = 0.0011f;

    /** @brief Strength of high-frequency Worley erosion on cloud perimeter. */
    // [ReflectField]
    float detailErosion = 0.25f;

    /** @brief Horizontal wind drift direction vector. */
    // [ReflectField]
    glm::vec2 windDirection{1.0f, 0.0f};

    /** @brief Wind drift velocity in meters per second. */
    // [ReflectField]
    float windSpeed = 20.0f;

    /** @brief Forward-scattering silver-lining rim glow intensity multiplier. */
    // [ReflectField]
    float silverLining = 1.5f;

    /** @brief Darkening factor for deep cloud creases and interior folds (sugar/powder effect). */
    // [ReflectField]
    float powderEffect = 0.8f;

    /** @brief Albedo tint color applied to cloud droplets. */
    // [ReflectField]
    glm::vec3 cloudColor{1.0f, 1.0f, 1.0f};

    /** @brief Ambient skylight reflection color for the underside and shaded faces. */
    // [ReflectField]
    glm::vec3 ambientColor{0.35f, 0.45f, 0.65f};

    /** @brief Number of primary raymarch steps through the cloud layer. */
    // [ReflectField]
    int marchSteps = 56;

    /** @brief Whether clouds cast dynamic shadows onto the ground and scene geometry. */
    // [ReflectField]
    bool castShadows = true;

    /** @brief Darkness of cloud shadows on the scene (0.0 = none, 1.0 = deep shadow). */
    // [ReflectField]
    float shadowIntensity = 0.65f;

    /** @brief Applies tuned physical parameters corresponding to the given preset. */
    void applyPreset(CloudPreset targetPreset) {
        preset = targetPreset;
        switch (targetPreset) {
            case CloudPreset::ClearSky:
                coverage = 0.0f;
                density = 0.0f;
                bottomAltitude = 1500.0f;
                topAltitude = 3500.0f;
                castShadows = false;
                shadowIntensity = 0.0f;
                break;
            case CloudPreset::FairWeather:
                coverage = 0.35f;
                density = 1.30f;
                bottomAltitude = 1400.0f;
                topAltitude = 3200.0f;
                cloudScale = 0.00022f;
                detailScale = 0.0013f;
                detailErosion = 0.28f;
                silverLining = 1.8f;
                powderEffect = 0.85f;
                cloudColor = glm::vec3(1.0f, 1.0f, 1.0f);
                ambientColor = glm::vec3(0.40f, 0.50f, 0.70f);
                castShadows = true;
                shadowIntensity = 0.55f;
                break;
            case CloudPreset::Scattered:
                coverage = 0.48f;
                density = 1.40f;
                bottomAltitude = 1200.0f;
                topAltitude = 3800.0f;
                cloudScale = 0.00018f;
                detailScale = 0.0011f;
                detailErosion = 0.25f;
                silverLining = 1.6f;
                powderEffect = 0.80f;
                cloudColor = glm::vec3(1.0f, 1.0f, 1.0f);
                ambientColor = glm::vec3(0.35f, 0.45f, 0.65f);
                castShadows = true;
                shadowIntensity = 0.70f;
                break;
            case CloudPreset::Broken:
                coverage = 0.68f;
                density = 1.70f;
                bottomAltitude = 1000.0f;
                topAltitude = 4500.0f;
                cloudScale = 0.00014f;
                detailScale = 0.00085f;
                detailErosion = 0.22f;
                silverLining = 1.3f;
                powderEffect = 0.75f;
                cloudColor = glm::vec3(0.92f, 0.93f, 0.96f);
                ambientColor = glm::vec3(0.30f, 0.38f, 0.55f);
                castShadows = true;
                shadowIntensity = 0.80f;
                break;
            case CloudPreset::Overcast:
                coverage = 0.88f;
                density = 2.4f;
                bottomAltitude = 800.0f;
                topAltitude = 3200.0f;
                cloudScale = 0.00008f;
                detailScale = 0.0005f;
                detailErosion = 0.16f;
                silverLining = 0.6f;
                powderEffect = 0.60f;
                cloudColor = glm::vec3(0.82f, 0.84f, 0.88f);
                ambientColor = glm::vec3(0.28f, 0.32f, 0.45f);
                castShadows = true;
                shadowIntensity = 0.85f;
                break;
            case CloudPreset::Stormy:
                coverage = 0.85f;
                density = 3.2f;
                bottomAltitude = 650.0f;
                topAltitude = 5200.0f;
                cloudScale = 0.00010f;
                detailScale = 0.0006f;
                detailErosion = 0.26f;
                silverLining = 1.1f;
                powderEffect = 0.90f;
                cloudColor = glm::vec3(0.52f, 0.55f, 0.62f);
                ambientColor = glm::vec3(0.18f, 0.22f, 0.32f);
                castShadows = true;
                shadowIntensity = 0.92f;
                break;
            case CloudPreset::Custom:
            default:
                break;
        }
    }
};
