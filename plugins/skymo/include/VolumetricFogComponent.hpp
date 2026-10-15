#pragma once

#include "ecs/Entity.hpp"
#include "core/EngineAPI.hpp"
#include <glm/vec3.hpp>

enum class FogMode : int {
    AtmosphericHaze = 0,
    DenseFog = 1
};

/**
 * @struct VolumetricFogComponent
 * @brief Configures aerial perspective and volumetric atmospheric fog in the scene.
 *        Can be placed on any entity to act as a global or bounded atmospheric volume.
 */
// [ReflectClass("Environment/Volumetric Fog")]
struct VolumetricFogComponent {
    /** @brief Whether volumetric fog is actively computed and rendered. */
    // [ReflectField]
    bool enabled = true;

    /** @brief Fog mode: AtmosphericHaze (physical sky blending) or DenseFog (stylized/disappearing fog). */
    // [ReflectField]
    FogMode mode = FogMode::AtmosphericHaze;

    /** @brief When true, fog also blankets the sky/horizon (ideal for dense fog / Hell environments). */
    // [ReflectField]
    bool affectSky = false;

    /** @brief When true, fog applies across the whole scene. Future support for bounded box/sphere volumes. */
    // [ReflectField]
    bool isGlobal = true;

    /** @brief Distance at which the 3D Camera Volume LUT reaches 100% depth slice span (in world units/meters). */
    // [ReflectField]
    float maxDistance = 3000.0f;

    /** @brief Maximum distance for local high-resolution volumetric shadow shafts and froxel grid (default: 96.0m). */
    // [ReflectField]
    float volumetricFogDistance = 96.0f;

    /** @brief Distance in front of camera before fog starts accumulating. */
    // [ReflectField]
    float startDistance = 0.0f;

    /** @brief Overall density multiplier on atmospheric extinction and in-scattering. */
    // [ReflectField]
    float densityMultiplier = 1.0f;

    /** @brief Color tint applied to the in-scattered atmospheric radiance. */
    // [ReflectField]
    glm::vec3 fogColorTint{ 1.0f, 1.0f, 1.0f };

    /** @brief Forward scattering anisotropy parameter (g) for Mie phase (default: 0.35). */
    // [ReflectField]
    float anisotropy = 0.35f;

    /** @brief World altitude (Y) where fog is at base density. */
    // [ReflectField]
    float baseHeight = 0.0f;

    /** @brief Exponential decay rate with altitude. Higher values confine fog closer to baseHeight (e.g. 0.005 - 0.05). */
    // [ReflectField]
    float heightFalloff = 0.01f;

    /** @brief How strongly fog attenuates direct directional sunlight on ground meshes (0.0 = none, 1.0 = physical). */
    // [ReflectField]
    float groundSunAttenuation = 1.0f;

    /** @brief Whether directional cascaded shadows cast volumetric light shafts (God Rays) through fog. */
    // [ReflectField]
    bool castShadows = true;

    /** @brief Contrast and intensity of volumetric shadow shafts (0.0 = unshadowed, 1.0 = full physical occlusion). */
    // [ReflectField]
    float shadowIntensity = 1.0f;

    /** @brief Number of raymarching steps per froxel voxel cell (8, 16, or 32). Default: 16. */
    // [ReflectField]
    int marchSteps = 16;
};
