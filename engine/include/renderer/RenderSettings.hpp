#pragma once
#include <cstdint>

namespace Engine {

    /**
     * @struct ShadowSettings
     * @brief Global directional shadow mapping settings.
     */
    struct ShadowSettings {
        /** @brief Global master toggle for directional shadow mapping. */
        bool enabled = true;

        /** @brief Shadow map texture resolution (1024, 2048, 4096). */
        uint32_t resolution = 2048;

        /** @brief Number of directional shadow cascades (1 to 4). */
        uint32_t cascadeCount = 4;

        /** @brief Practical split lambda blending log and linear splits (0.0 to 1.0). */
        float cascadeSplitLambda = 0.85f;

        /** @brief Soft shadow filtering (true = 16-tap Poisson disk, false = 3x3 PCF). */
        bool softShadows = true;

        /** @brief Maximum view distance (in meters) within which shadows are projected. */
        float maxDistance = 80.0f;

        /** @brief Constant/slope depth bias factor to eliminate self-shadowing acne. */
        float bias = 0.0005f;

        /** @brief Geometric normal offset bias factor (expressed in texels). */
        float normalBias = 1.5f;

        /** @brief PCF filter kernel size (1 = single bilateral tap, 9 = 3x3 PCF, 16 = Poisson). */
        int pcfSamples = 16;
    };

    /**
     * @enum TonemapperMode
     * @brief Photographic tone mapping operators.
     */
    enum class TonemapperMode : int {
        ACES = 0,     // Academy Color Encoding System (Narkowicz fit, filmic industry standard)
        Filmic = 1,   // Unreal / Jim Hejl filmic S-curve
        Reinhard = 2, // Extended luminance-based Reinhard
        AgX = 3,      // Modern AgX curve (Blender 4.0 highlight preservation)
        None = 4      // Raw clamp (no tone mapping)
    };

    /**
     * @struct TonemapSettings
     * @brief Configuration for HDR tone mapping post-processing.
     */
    struct TonemapSettings {
        TonemapperMode mode = TonemapperMode::ACES;
        float exposure = 1.0f;
        float gamma = 2.2f;
        float contrast = 1.0f;
        float saturation = 1.0f;
        float whitePoint = 4.0f;
    };

    /**
     * @struct SSAOSettings
     * @brief Configuration for Screen-Space Ambient Occlusion (SSAO) post-processing.
     */
    struct SSAOSettings {
        /** @brief Global master toggle for Screen-Space Ambient Occlusion. */
        bool enabled = true;

        /** @brief View-space sampling radius around surface points (in meters/units). */
        float radius = 0.5f;

        /** @brief Depth bias to eliminate self-occlusion artifacts and acne. */
        float bias = 0.025f;

        /** @brief Occlusion darkness and intensity multiplier. */
        float intensity = 1.5f;

        /** @brief Contrast curve exponent for ambient crevices. */
        float power = 1.5f;

        /** @brief Number of hemisphere occlusion samples (4 to 32). */
        int sampleCount = 16;

        /** @brief Debug mode: render isolated black-and-white AO factor instead of shading scene. */
        bool debugAO = false;
    };

    /**
     * @struct ProjectGraphicsSettings
     * @brief Global graphics configuration options for the engine.
     */
    struct ProjectGraphicsSettings {
        ShadowSettings shadowSettings{};
        TonemapSettings tonemapSettings{};
        SSAOSettings ssaoSettings{};
        bool atmosphereEnabled = true;
        bool vsync = true;
    };

} // namespace Engine
