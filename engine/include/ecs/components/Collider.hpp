#pragma once
#include <glm/glm.hpp>

#include "core/EngineAPI.hpp"
#include "meta/ComponentReflection.hpp"


/**
 * @enum ColliderShape
 * @brief Geometric primitive shapes supported for physics collision detection.
 */
enum class ColliderShape {
    /** @brief Spherical collision volume defined by radius. */
    Sphere,
    /** @brief Axis-Aligned Bounding Box volume. */
    AABB,
    /** @brief Oriented Bounding Box volume. */
    OBB,
    /** @brief Capsule volume defined by radius and height. */
    Capsule
};

/**
 * @struct ColliderComponent
 * @brief Represents a collision volume (Sphere, Axis-Aligned Bounding Box, Oriented Bounding Box, or Capsule).
 */
// [ReflectClass("Physics/Collider")]
struct ENGINE_API ColliderComponent {
    /** @brief Active primitive collision shape. */
    ColliderShape shape = ColliderShape::AABB;
    /** @brief Radius used for Sphere and Capsule colliders. */
    // [ReflectField]
    float radius = 1.0f;
    /** @brief Total height (including hemispherical caps) for Capsule colliders. */
    // [ReflectField]
    float height = 2.0f;
    /** @brief Half-extents vector for AABB and OBB colliders. */
    // [ReflectField]
    glm::vec3 extents = glm::vec3(0.5f);
    /** @brief Local position offset relative to entity transform origin. */
    // [ReflectField]
    glm::vec3 offset = glm::vec3(0.0f);
};








