# Cinemachine Virtual Camera System

This document describes the design, operational modes, and integration of the **Cinemachine** camera plugin (`plugins/cinemachine`). Inspired by Unity's Cinemachine suite, this system provides procedural virtual cameras that handle tracking, orbiting, first-person bone-locking, damping, and multi-camera priority blending.

---

## 1. System Overview

In traditional game development, programming camera behavior directly inside player scripts leads to jitter, hard-coded offsets, and difficult cinematic transitions. 

The engine decouples camera control using an ECS virtual camera pattern:
* **Physical Camera**: The active scene camera holding [`Camera`](../engine/include/ecs/components/Camera.hpp) and [`Transform`](../engine/include/ecs/components/Transform.hpp).
* **Virtual Cameras ([`CinemachineVirtualCamera`](../plugins/cinemachine/include/CinemachineComponent.hpp))**: Any entity can host a `CinemachineVirtualCamera` defining tracking targets, distances, offsets, and damping parameters.
* **Cinemachine System ([`CinemachineSystem`](../plugins/cinemachine/include/CinemachineSystem.hpp))**: Evaluates active virtual cameras, computes smooth target interpolations, and drives the physical camera's transform and FOV.

```mermaid
graph LR
    Target[Player / Target Entity] -->|Follow / LookAt| VCam1[Virtual Camera 1 (Priority 10)]
    Target -->|Follow / LookAt| VCam2[Virtual Camera 2 (Priority 20)]
    VCam2 -->|Highest Priority Active| CS[CinemachineSystem]
    CS -->|Damped Interpolation| PhysCam[Physical Camera Transform & FOV]
    PhysCam --> Vulkan[Vulkan Viewport Rendering]
```

---

## 2. Tracking Modes (`CinemachineMode`)

The virtual camera supports four distinct operating modes:

### 1. `ThirdPersonFollow`
* **Behavior**: Positions the camera behind the follow target at `followOffset` and `targetDistance`.
* **Mouse Orbit (`mouseOrbit = true`)**: The player can freely rotate the camera view around the target using mouse movement.
* **Pitch Clamping**: Enforces realistic viewing angles using `minPitch` (default \(-80^\circ\)) and `maxPitch` (default \(+80^\circ\)).
* **Zoom Control**: Mouse scroll wheel dynamically adjusts `targetDistance` within `[minDistance, maxDistance]`.

### 2. `FirstPerson`
* **Behavior**: Positions the camera directly inside the target entity's head.
* **Skeletal Bone Attachment (`lockToBone`)**: When tracking an animated character possessing a `SkeletonComponent`, the camera automatically binds to the named bone transform (e.g. `"Head"`, `"Neck"`). As the character walks or breathes, the camera naturally reflects the bone motion.
* **Mouse Look (`mouseLook = true`)**: Full first-person mouse pitch and yaw controls.

### 3. `FixedLookAt`
* **Behavior**: Stationary or fixed-path security/cinematic camera.
* **Aiming**: Continuously calculates look-at orientation targeting `lookAtTarget` with smooth spherical rotation damping.

### 4. `Follow2D`
* **Behavior**: Locks the camera axis to a 2D plane (X/Y coordinates) while maintaining fixed Z depth. Ideal for 2D platformers and top-down tilemap games.

---

## 3. Smooth Damping & Dead Zones

Abrupt camera movements cause visual fatigue. Cinemachine applies exponential smoothing to both position and orientation:

### Position Damping
```cpp
// Exponential damping formula
currentPos = glm::mix(currentPos, targetPos, 1.0f - std::exp(-positionDamping * dt));
```
* High damping values (\(> 10.0\)) result in tight, responsive tracking.
* Low damping values (\(1.0 - 3.0\)) create a lazy, cinematic lag effect where the camera gently catches up to fast-moving characters.

### Rotation Damping
Smooths pitch and yaw angular changes, preventing camera snapping during sharp direction changes.

---

## 4. Multi-Camera Priority & Blending

Games often require switching camera perspectives dynamically (e.g., transitioning from standard 3rd-person exploration to a locked combat camera, dialog camera, or death cutscene):

* **Priority Weighting**: Each virtual camera defines a `priority` integer. `CinemachineSystem` continuously selects the virtual camera with the highest priority in the active scene.
* **Blend Transitions**: When active virtual cameras switch, the system transitions between them using configurable easing curves (linear, ease-in-out), smoothly interpolating position, rotation, and field of view (FOV).

---

## 5. Component Configuration Reference

```cpp
// [ReflectClass("Camera/Cinemachine Virtual Camera")]
struct CinemachineVirtualCamera {
    Entity followTarget = Entity();     // Entity handle to follow
    Entity lookAtTarget = Entity();     // Entity handle to aim at
    std::string followTargetName;       // Persistent name for scene saving/loading
    std::string lookAtTargetName;       // Persistent name for scene saving/loading
    std::string lockToBone = "Head";    // Bone name for FirstPerson tracking

    CinemachineMode mode = CinemachineMode::ThirdPersonFollow;
    bool mouseOrbit = true;             // Orbit mouse rotation
    bool mouseLook = true;              // First-person mouse orientation
    float orbitSensitivity = 0.1f;      // Mouse sensitivity
    float orbitYaw = 0.0f;              // Current yaw in degrees
    float orbitPitch = 15.0f;           // Current pitch in degrees
    float minPitch = -80.0f;            // Minimum down-angle
    float maxPitch = 80.0f;             // Maximum up-angle

    float targetDistance = 5.0f;        // 3rd-person follow distance
    float minDistance = 1.5f;           // Minimum zoom distance
    float maxDistance = 20.0f;          // Maximum zoom distance

    glm::vec3 followOffset{ 0.0f, 1.8f, -4.0f }; // Local offset from target
    glm::vec3 lookAtOffset{ 0.0f, 1.5f, 0.0f };  // Local offset from aim point

    float positionDamping = 5.0f;       // Position smoothing speed
    float rotationDamping = 10.0f;      // Rotation smoothing speed
};
```
