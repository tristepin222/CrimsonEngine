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
        // [ReflectField]
        LightType type = LightType::Directional;
        /** @brief Direction vector for directional and spot lights (vector light shines towards). */
        // [ReflectField]
        glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
        /** @brief Pitch / Elevation angle in degrees (0 to 360, where 0=sunrise, 90=zenith, 180=sunset, 270=nadir). */
        // [ReflectField]
        float pitch = 45.0f;
        /** @brief Yaw / Azimuth angle in degrees (0 to 360, compass heading of sun trajectory). */
        // [ReflectField]
        float yaw = 0.0f;
        /** @brief RGB light color vector. */
        // [ReflectField]
        glm::vec3 color{ 1.0f, 1.0f, 1.0f };
        /** @brief Intensity multiplier for brightness. */
        // [ReflectField]
        float intensity = 1.0f;
        /** @brief Attenuation distance range for point and spot lights. */
        // [ReflectField]
        float range = 10.0f; 

        /** @brief Whether this light source casts directional shadows. */
        // [ReflectField]
        bool castShadows = true;

        /** @brief Shadow depth bias factor to prevent surface self-shadowing acne. */
        // [ReflectField]
        float shadowBias = 0.0005f;

        /** @brief Geometric normal offset bias factor (expressed in texels). */
        // [ReflectField]
        float shadowNormalBias = 1.5f;

        /** @brief Maximum shadow view distance (in meters). Set to 0 to inherit project settings. */
        // [ReflectField]
        float shadowDistance = 80.0f; 

        void updateDirectionFromAngles() {
            // Normalize pitch and yaw into [0, 360)
            while (pitch < 0.0f) pitch += 360.0f;
            while (pitch >= 360.0f) pitch -= 360.0f;
            while (yaw < 0.0f) yaw += 360.0f;
            while (yaw >= 360.0f) yaw -= 360.0f;

            float pitchRad = glm::radians(pitch);
            float yawRad = glm::radians(yaw);

            // Sun traces a continuous 360-degree circle in a vertical plane oriented by yaw:
            // At pitch = 0: sun is on horizon (rising) -> sin(0) = 0, cos(0) = 1
            // At pitch = 90: sun is at zenith -> sin(90) = 1, cos(90) = 0
            // At pitch = 180: sun is on opposite horizon (setting) -> sin(180) = 0, cos(180) = -1
            // At pitch = 270: sun is at nadir (underground midnight) -> sin(270) = -1, cos(270) = 0
            float height = std::sin(pitchRad);
            float horiz = std::cos(pitchRad);

            // Direction towards the sun:
            glm::vec3 sunVec = glm::vec3(
                std::sin(yawRad) * horiz,
                height,
                -std::cos(yawRad) * horiz
            );

            // Light shines in the direction the rays travel (towards the ground):
            direction = glm::normalize(-sunVec);
        }

        void updateAnglesFromDirection() {
            if (glm::length(direction) > 0.0001f) {
                glm::vec3 sunVec = glm::normalize(-direction);
                // Pitch from sun vector height and horizontal component
                float horiz = std::sqrt(sunVec.x * sunVec.x + sunVec.z * sunVec.z);
                // Standard atan2 returns [-180, 180], convert to [0, 360)
                float computedPitch = glm::degrees(std::atan2(sunVec.y, horiz));
                if (computedPitch < 0.0f) computedPitch += 360.0f;
                pitch = computedPitch;

                yaw = glm::degrees(std::atan2(sunVec.x, -sunVec.z));
                if (yaw < 0.0f) yaw += 360.0f;
            }
        }

        LightComponent() {
            updateDirectionFromAngles();
        }
        LightComponent(LightType t, const glm::vec3& col, float inten = 1.0f, float rng = 10.0f, const glm::vec3& dir = glm::vec3(0.0f, -1.0f, 0.0f))
            : type(t), direction(dir), color(col), intensity(inten), range(rng) {
            updateAnglesFromDirection();
        }
        LightComponent(LightType t, const glm::vec3& dir, const glm::vec3& col, float inten, float rng)
            : type(t), direction(dir), color(col), intensity(inten), range(rng) {
            updateAnglesFromDirection();
        }
    };

} // namespace Engine
