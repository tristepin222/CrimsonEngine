#pragma once
#include <glm/glm.hpp>

#include "core/EngineAPI.hpp"
#include "meta/ComponentReflection.hpp"


enum class RigidBodyType {
    Dynamic,
    Static
};

/**
 * @struct RigidBodyComponent
 * @brief Holds mass, velocity, acceleration, forces, restitution, bounciness, and gravity settings.
 */
// [ReflectClass("Physics/Rigid Body")]
struct ENGINE_API RigidBodyComponent {
    RigidBodyType type = RigidBodyType::Dynamic;
    // [ReflectField]
    float mass = 1.0f;
    // [ReflectField]
    glm::vec3 velocity = glm::vec3(0.0f);
    // [ReflectField]
    glm::vec3 acceleration = glm::vec3(0.0f);
    // [ReflectField]
    glm::vec3 force = glm::vec3(0.0f);
    // [ReflectField]
    float gravityScale = 1.0f;
    // [ReflectField]
    float restitution = 0.5f; // Bounciness coefficient
    // [ReflectField]
    float friction = 0.3f;    // Sliding friction coefficient

    // Rotational physics fields
    // [ReflectField]
    glm::vec3 angularVelocity = glm::vec3(0.0f); // Rad/s
    // [ReflectField]
    glm::vec3 torque = glm::vec3(0.0f);
    // [ReflectField]
    float angularDrag = 0.5f; // Damping
    // [ReflectField]
    float linearDrag = 0.0f;  // Damping

    // Sleep system
    float sleepTimer           = 0.0f;   // Accumulated time below sleep threshold
    bool  sleeping             = false;  // True when body is fully at rest
    bool  hadContactThisFrame  = false;  // Set by collision resolver; cleared each integration pass
                                         // Used to prevent free-falling bodies from sleeping
    bool  unstableContactThisFrame = false; // Contact support is producing torque; don't sleep yet

    // Constraints (Freeze axes)
    // [ReflectField]
    bool freezePositionX = false;
    // [ReflectField]
    bool freezePositionY = false;
    // [ReflectField]
    bool freezePositionZ = false;
    // [ReflectField]
    bool freezeRotationX = false;
    // [ReflectField]
    bool freezeRotationY = false;
    // [ReflectField]
    bool freezeRotationZ = false;
};







