#pragma once
#include <glm/glm.hpp>
#include "core/EngineAPI.hpp"

namespace Engine {

    /**
     * @enum LightType
     * @brief Specifies the light emission type (Directional, Point, Spot).
     */
    enum class LightType {
        Directional,
        Point,
        Spot
    };

    /**
     * @struct LightComponent
     * @brief Component representing a light source in the scene.
     */
    // [ReflectClass("Rendering & Lights/Light")]
    struct ENGINE_API LightComponent {
        /** @brief Type classification of the light source. */
        LightType type = LightType::Directional;
        /** @brief RGB light color vector. */
        // [ReflectField]
        glm::vec3 color{ 1.0f, 1.0f, 1.0f };
        /** @brief Intensity multiplier for brightness. */
        // [ReflectField]
        float intensity = 1.0f;
        /** @brief Attenuation distance range for point and spot lights. */
        // [ReflectField]
        float range = 10.0f; 
    };

} // namespace Engine
