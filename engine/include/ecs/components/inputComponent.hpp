#pragma once
#include <glm/glm.hpp>

/**
 * @struct InputComponent
 * @brief Component representing input state for an entity (e.g., player or camera).
 */
// [ReflectClass("Gameplay/Input")]
struct InputComponent {
    // [ReflectField]
    glm::vec3 movement{ 0 }; // forward/back, left/right, up/down
    // [ReflectField]
    glm::vec2 look{ 0 };     // mouse delta
};
