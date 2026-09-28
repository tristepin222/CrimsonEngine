#pragma once
#include "ecs/Entity.hpp"
#include <glm/glm.hpp>
#include <string>
#include "core/EngineAPI.hpp"

/**
 * @enum CinemachineMode
 * @brief Operating camera mode for Cinemachine virtual camera tracking.
 */
enum class CinemachineMode {
    /** @brief Orbiting third-person follow view behind target. */
    ThirdPersonFollow = 0,
    /** @brief Attached first-person perspective locked to target or bone. */
    FirstPerson = 1,
    /** @brief Stationary or offset camera looking continuously at a target. */
    FixedLookAt = 2,
    /** @brief 2D orthographic/flat tracking following target X/Y coordinates. */
    Follow2D = 3
};

/**
 * @struct CinemachineVirtualCamera
 * @brief Addon component that represents a virtual camera.
 *        Managed by CinemachineSystem to calculate tracking offsets, damping, and priority-blending.
 */
// [ReflectClass("Camera/Cinemachine Virtual Camera")]
struct CinemachineVirtualCamera {
    /** @brief Target entity handle to follow. */
    Entity followTarget = Entity();
    /** @brief Target entity handle to look at. */
    Entity lookAtTarget = Entity();

    /** @brief Name of follow target entity for serialization lookup. */
    // [ReflectField]
    std::string followTargetName;
    /** @brief Name of look-at target entity for serialization lookup. */
    // [ReflectField]
    std::string lookAtTargetName;
    /** @brief Skeletal bone name to lock camera to in FirstPerson mode. */
    // [ReflectField]
    std::string lockToBone = "Head";

    /** @brief Active camera tracking mode. */
    // [ReflectField]
    CinemachineMode mode = CinemachineMode::ThirdPersonFollow;
    /** @brief Enable mouse orbit control around follow target. */
    // [ReflectField]
    bool mouseOrbit = true;
    /** @brief Enable free mouse look orientation. */
    // [ReflectField]
    bool mouseLook = true;
    /** @brief Sensitivity multiplier for mouse orbit input. */
    // [ReflectField]
    float orbitSensitivity = 0.1f;
    /** @brief Current orbit yaw angle in degrees. */
    // [ReflectField]
    float orbitYaw = 0.0f;
    /** @brief Current orbit pitch angle in degrees. */
    // [ReflectField]
    float orbitPitch = 0.0f;
    /** @brief Current camera yaw angle in degrees. */
    // [ReflectField]
    float cameraYaw = 0.0f;
    /** @brief Current camera pitch angle in degrees. */
    // [ReflectField]
    float cameraPitch = 0.0f;

    /** @brief 3D position offset relative to target. */
    // [ReflectField]
    glm::vec3 followOffset = glm::vec3(0.0f, 4.0f, 8.0f);
    /** @brief Damping rate for smooth position tracking (higher = slower, 0.0f = instant). */
    // [ReflectField]
    float followDamping = 2.0f;
    /** @brief Damping rate for smooth rotation tracking. */
    // [ReflectField]
    float lookAtDamping = 1.0f;

    /** @brief Field of view in degrees. */
    // [ReflectField]
    float fov = 45.0f;
    /** @brief Priority rank used to pick active virtual camera (higher priority wins). */
    // [ReflectField]
    int priority = 10;
    /** @brief Master active flag for this virtual camera. */
    // [ReflectField]
    bool active = true;


    /** @brief Cached world-space position for smooth frame interpolation. */
    glm::vec3 currentPosition = glm::vec3(0.0f);
    /** @brief Cached Euler rotation angles for smooth frame interpolation. */
    glm::vec3 currentRotationEuler = glm::vec3(0.0f);
    /** @brief Cached previous transform yaw angle for inspector edit detection. */
    float lastTransformYaw = 0.0f;
    /** @brief Cached previous transform pitch angle for inspector edit detection. */
    float lastTransformPitch = 0.0f;
    /** @brief Flag indicating whether camera initial transform was initialized. */
    bool initialized = false;
};