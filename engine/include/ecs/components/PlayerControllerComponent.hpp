#pragma once

#include "core/EngineAPI.hpp"
#include "meta/ComponentReflection.hpp"

/**
 * @struct PlayerControllerComponent
 * @brief Component to mark and control player movement and interaction.
 */
// [ReflectClass("Gameplay/Player Controller")]
struct ENGINE_API PlayerControllerComponent {
    /** @brief Movement speed multiplier (units/sec). */
    // [ReflectField]
    float speed = 5.0f;
    /** @brief Vertical impulse force applied when jumping. */
    // [ReflectField]
    float jumpForce = 6.0f;
    /** @brief Maximum interaction distance for objects. */
    // [ReflectField]
    float interactRange = 3.0f;

    /** @brief Whether character rotates to face the direction of movement. */
    // [ReflectField]
    bool orientToMovement = true;

    /** @brief Transient flag set when jump key is pressed. */
    bool wasJumpPressed = false;
    /** @brief Transient flag set when interact key is pressed. */
    bool wasInteractPressed = false;

    /** @brief Debug frame count diagnostic. */
    int debugRunningCount = 0;
    /** @brief Debug rigid body velocity diagnostic vector. */
    glm::vec3 debugRbVelocity{0.0f};
    /** @brief Debug movement input magnitude. */
    float debugMoveDirLength = 0.0f;
};
