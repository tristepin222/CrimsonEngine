#pragma once

#include "core/EngineAPI.hpp"
#include <glm/glm.hpp>

/**
 * @struct EditorCamera
 * @brief Tag component identifying and holding navigation state for the persistent editor-only camera entity.
 */
struct ENGINE_API EditorCamera {
    glm::vec3 pivot{ 0.0f, 0.0f, 0.0f };
    float pivotDistance = 8.0f;
    bool isOrbiting = false;
    bool isPanning = false;
};
