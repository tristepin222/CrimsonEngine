#include "CinemachineSystem.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "ecs/components/Camera.hpp"
#include "ecs/components/EditorCamera.hpp"
#include "ecs/components/Transform.hpp"
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <algorithm>

#include "ecs/components/Name.hpp"
#include "ecs/components/inputComponent.hpp"
#include "ecs/components/Mesh.hpp"
#include "ecs/components/PlayerControllerComponent.hpp"

#include "ecs/components/Skeleton.hpp"
#include "ecs/components/Hierarchy.hpp"

// Standalone function to compute world matrix of any entity by traversing HierarchyComponent
glm::mat4 getEntityWorldMatrix(Registry& registry, Entity entity, int depth = 0) {
    if (depth > 50) return glm::mat4(1.0f); // Safety depth limit
    glm::mat4 model = glm::mat4(1.0f);
    if (auto* transform = registry.get<Transform>(entity)) {
        model = transform->matrix();
    }
    if (auto* hierarchy = registry.get<HierarchyComponent>(entity)) {
        if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
            model = getEntityWorldMatrix(registry, hierarchy->parent, depth + 1) * model;
        }
    }
    return model;
}

// Helper function to resolve the world matrix of a specific joint bone by name
glm::mat4 getJointWorldMatrix(Registry& registry, Entity entity, const std::string& jointName) {
    auto* skeleton = registry.get<SkeletonComponent>(entity);
    auto* transform = registry.get<Transform>(entity);
    if (!skeleton || !transform) {
        return getEntityWorldMatrix(registry, entity);
    }

    int jointIndex = -1;
    std::string queryLower = jointName;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

    for (int i = 0; i < static_cast<int>(skeleton->joints.size()); ++i) {
        std::string nameLower = skeleton->joints[i].name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

        // Substring match so "head" matches "mixamorig:head" or "mixamorig:Head"
        if (nameLower == queryLower || nameLower.find(":" + queryLower) != std::string::npos || nameLower.find(queryLower) != std::string::npos) {
            jointIndex = i;
            break;
        }
    }

    if (jointIndex == -1) {
        return getEntityWorldMatrix(registry, entity);
    }

    // Traverse upwards to compute model-space matrix
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    int curr = jointIndex;
    while (curr != -1) {
        modelMatrix = skeleton->joints[curr].localTransform * modelMatrix;
        curr = skeleton->joints[curr].parentIndex;
    }

    return getEntityWorldMatrix(registry, entity) * modelMatrix;
}

CinemachineSystem::CinemachineSystem(Registry& reg, VulkanRenderer& renderer, EditorModeState& mode)
    : registry(reg), editorMode(mode) {}

void CinemachineSystem::update(float dt) {
    // Resolve targets by name on load or if target IDs are invalid/incorrect
    for (auto [entity, vcamDummy] : registry.view<CinemachineVirtualCamera>()) {
        auto* vcam = registry.get<CinemachineVirtualCamera>(entity);
        if (!vcam) continue;

        // 1. Resolve follow target
        bool needResolveFollow = false;
        if (vcam->followTarget.getId() == Entity::INVALID_ENTITY || !registry.isValid(vcam->followTarget)) {
            needResolveFollow = true;
        } else {
            auto* nameComp = registry.get<Name>(vcam->followTarget);
            if (!nameComp || nameComp->value != vcam->followTargetName) {
                needResolveFollow = true;
            }
        }
        if (needResolveFollow && !vcam->followTargetName.empty()) {
            for (auto [ent, nameComp] : registry.view<Name>()) {
                if (nameComp.value == vcam->followTargetName) {
                    vcam->followTarget = ent;
                    break;
                }
            }
        }

        // 2. Resolve LookAt target
        bool needResolveLookAt = false;
        if (vcam->lookAtTarget.getId() == Entity::INVALID_ENTITY || !registry.isValid(vcam->lookAtTarget)) {
            needResolveLookAt = true;
        } else {
            auto* nameComp = registry.get<Name>(vcam->lookAtTarget);
            if (!nameComp || nameComp->value != vcam->lookAtTargetName) {
                needResolveLookAt = true;
            }
        }
        if (needResolveLookAt && !vcam->lookAtTargetName.empty()) {
            for (auto [ent, nameComp] : registry.view<Name>()) {
                if (nameComp.value == vcam->lookAtTargetName) {
                    vcam->lookAtTarget = ent;
                    break;
                }
            }
        }
    }

    // 1. Find the highest priority active virtual camera
    Entity activeVcamEntity;
    int highestPriority = -999999;

    for (auto [entity, vcam] : registry.view<CinemachineVirtualCamera>()) {
        if (vcam.active && vcam.priority > highestPriority) {
            highestPriority = vcam.priority;
            activeVcamEntity = entity;
        }
    }

    if (activeVcamEntity.getId() == Entity::INVALID_ENTITY) {
        return; // No virtual camera active
    }

    CinemachineVirtualCamera* activeVcam = registry.get<CinemachineVirtualCamera>(activeVcamEntity);
    Transform* activeVcamTransform = registry.get<Transform>(activeVcamEntity);

    if (!activeVcam || !activeVcamTransform) {
        return;
    }

    // Configure player controller orient-to-movement behavior based on active camera mode (during Play Mode only)
    if (editorMode.isPlaying && activeVcam->followTarget.getId() != Entity::INVALID_ENTITY && registry.isValid(activeVcam->followTarget)) {
        if (auto* pc = registry.get<PlayerControllerComponent>(activeVcam->followTarget)) {
            if (activeVcam->mode == CinemachineMode::FirstPerson || (activeVcam->mode == CinemachineMode::ThirdPersonFollow && activeVcam->mouseOrbit)) {
                pc->orientToMovement = false; // Strafe mode (player always faces camera yaw)
            } else {
                pc->orientToMovement = true;  // Turn to face movement in third person (free run)
            }
        }
    }

    // Ensure active virtual camera has an InputComponent if mouseOrbit or mouseLook is active
    if ((activeVcam->mouseOrbit || activeVcam->mouseLook) && !registry.has<InputComponent>(activeVcamEntity)) {
        registry.emplace<InputComponent>(activeVcamEntity, InputComponent{});
    }

    // 2. Resolve the virtual camera's tracking target position and rotation
    glm::vec3 targetPos = activeVcamTransform->position;
    glm::vec3 targetRot = activeVcamTransform->rotation;

    // One-time initialization on scene load or component creation
    if (!activeVcam->initialized) {
        if (activeVcam->cameraYaw == 0.0f && activeVcam->orbitYaw != 0.0f) {
            activeVcam->cameraYaw = activeVcam->orbitYaw;
        } else if (activeVcam->cameraYaw == 0.0f && activeVcam->mode == CinemachineMode::Follow2D) {
            activeVcam->cameraYaw = -90.0f;
            activeVcam->orbitYaw = -90.0f;
        } else {
            activeVcam->orbitYaw = activeVcam->cameraYaw;
        }

        activeVcamTransform->rotation.y = activeVcam->cameraYaw;
        activeVcamTransform->rotation.x = activeVcam->cameraPitch;
        activeVcam->lastTransformYaw = activeVcamTransform->rotation.y;
        activeVcam->lastTransformPitch = activeVcamTransform->rotation.x;

        activeVcam->currentPosition = targetPos;
        activeVcam->currentRotationEuler = glm::vec3(activeVcam->cameraPitch, activeVcam->cameraYaw, activeVcamTransform->rotation.z);
        activeVcam->initialized = true;
    } else {
        // Sync orbit angles to camera yaw/pitch if cameraYaw/cameraPitch were edited in Inspector
        activeVcam->orbitYaw = activeVcam->cameraYaw;
        activeVcam->orbitPitch = activeVcam->cameraPitch;
    }

    if (activeVcam->mode == CinemachineMode::ThirdPersonFollow) {
        // Follow Target
        if (activeVcam->followTarget.getId() != Entity::INVALID_ENTITY && registry.isValid(activeVcam->followTarget)) {
            if (auto* targetTrans = registry.get<Transform>(activeVcam->followTarget)) {
                glm::vec3 followBasePos = targetTrans->position;

                // Handle mouse orbit (in Play Mode only)
                if (activeVcam->mouseOrbit) {
                    if (editorMode.isPlaying) {
                        if (auto* input = registry.get<InputComponent>(activeVcamEntity)) {
                            activeVcam->orbitYaw   += input->look.x * activeVcam->orbitSensitivity;
                            activeVcam->orbitPitch += input->look.y * activeVcam->orbitSensitivity;
                            activeVcam->orbitPitch = std::clamp(activeVcam->orbitPitch, -80.0f, 80.0f);
                            activeVcam->cameraYaw = activeVcam->orbitYaw;
                            activeVcam->cameraPitch = activeVcam->orbitPitch;
                        }
                    }

                    glm::quat orbitRot = glm::quat(glm::vec3(glm::radians(activeVcam->orbitPitch), glm::radians(activeVcam->orbitYaw), 0.0f));
                    glm::vec3 worldOffset = orbitRot * activeVcam->followOffset;
                    targetPos = followBasePos + worldOffset;
                } else {
                    // Use static world-space offset so the camera does not spin when the character turns
                    glm::vec3 worldOffset = activeVcam->followOffset;
                    targetPos = followBasePos + worldOffset;
                }
            }
        }

        // Aim / LookAt Target
        if (activeVcam->lookAtTarget.getId() != Entity::INVALID_ENTITY && registry.isValid(activeVcam->lookAtTarget)) {
            if (auto* lookAtTrans = registry.get<Transform>(activeVcam->lookAtTarget)) {
                glm::vec3 targetDir = lookAtTrans->position - activeVcam->currentPosition;
                float dirLen = glm::length(targetDir);
                if (dirLen > 1e-3f) {
                    targetDir = glm::normalize(targetDir);
                    float pitchRad = asin(targetDir.y);
                    float yawRad = atan2(targetDir.z, targetDir.x);
                    targetRot.x = glm::degrees(pitchRad);
                    targetRot.y = glm::degrees(yawRad);
                    targetRot.z = 0.0f;
                }
            }
        } else if (activeVcam->followTarget.getId() != Entity::INVALID_ENTITY && registry.isValid(activeVcam->followTarget)) {
            // Default look at follow target
            if (auto* targetTrans = registry.get<Transform>(activeVcam->followTarget)) {
                glm::vec3 targetDir = targetTrans->position - activeVcam->currentPosition;
                float dirLen = glm::length(targetDir);
                if (dirLen > 1e-3f) {
                    targetDir = glm::normalize(targetDir);
                    float pitchRad = asin(targetDir.y);
                    float yawRad = atan2(targetDir.z, targetDir.x);
                    targetRot.x = glm::degrees(pitchRad);
                    targetRot.y = glm::degrees(yawRad);
                    targetRot.z = 0.0f;
                }
            }
        }

        // Write back resolved camera target yaw rotation to target entity so player character aligns with camera look direction
        if (editorMode.isPlaying && activeVcam->mouseOrbit && activeVcam->followTarget.getId() != Entity::INVALID_ENTITY && registry.isValid(activeVcam->followTarget)) {
            if (auto* targetTrans = registry.get<Transform>(activeVcam->followTarget)) {
                targetTrans->rotation.y = -targetRot.y + 90.0f;
            }
        }
    } else if (activeVcam->mode == CinemachineMode::FirstPerson) {
        // First Person Mode
        if (activeVcam->followTarget.getId() != Entity::INVALID_ENTITY && registry.isValid(activeVcam->followTarget)) {
            if (auto* targetTrans = registry.get<Transform>(activeVcam->followTarget)) {
                glm::vec3 basePos = targetTrans->position;
                
                // If a skeleton exists and a bone name is specified, track the bone's world position directly
                if (!activeVcam->lockToBone.empty() && registry.has<SkeletonComponent>(activeVcam->followTarget)) {
                    glm::mat4 jointMat = getJointWorldMatrix(registry, activeVcam->followTarget, activeVcam->lockToBone);
                    basePos = glm::vec3(jointMat[3]);
                }

                if (activeVcam->mouseLook) {
                    if (editorMode.isPlaying) {
                        if (auto* input = registry.get<InputComponent>(activeVcamEntity)) {
                            activeVcam->cameraYaw   += input->look.x * activeVcam->orbitSensitivity;
                            activeVcam->cameraPitch += input->look.y * activeVcam->orbitSensitivity;
                            activeVcam->cameraPitch = std::clamp(activeVcam->cameraPitch, -89.0f, 89.0f);
                            activeVcam->orbitYaw = activeVcam->cameraYaw;
                            activeVcam->orbitPitch = activeVcam->cameraPitch;
                        }
                    }
                    targetRot = glm::vec3(activeVcam->cameraPitch, activeVcam->cameraYaw, 0.0f);
                    
                    // Write back yaw rotation to target entity so player character aligns with camera look direction
                    if (editorMode.isPlaying) {
                        targetTrans->rotation.y = -activeVcam->cameraYaw + 90.0f;
                    }
                } else {
                    targetRot = targetTrans->rotation;
                }

                float camYawVal = activeVcam->cameraYaw;
                if (!activeVcam->mouseLook) {
                    camYawVal = -targetTrans->rotation.y + 90.0f;
                }

                // Compute world offset rotated by camera look yaw
                glm::quat cameraRotQuat = glm::quat(glm::radians(glm::vec3(0.0f, camYawVal, 0.0f)));
                glm::vec3 worldOffset = cameraRotQuat * activeVcam->followOffset;
                targetPos = basePos + worldOffset;
            }
        }
    } else if (activeVcam->mode == CinemachineMode::FixedLookAt) {
        // Fixed LookAt Mode
        targetPos = activeVcamTransform->position;

        if (activeVcam->lookAtTarget.getId() != Entity::INVALID_ENTITY && registry.isValid(activeVcam->lookAtTarget)) {
            if (auto* lookAtTrans = registry.get<Transform>(activeVcam->lookAtTarget)) {
                glm::vec3 targetDir = lookAtTrans->position - targetPos;
                float dirLen = glm::length(targetDir);
                if (dirLen > 1e-3f) {
                    targetDir = glm::normalize(targetDir);
                    float pitchRad = asin(targetDir.y);
                    float yawRad = atan2(targetDir.z, targetDir.x);
                    targetRot.x = glm::degrees(pitchRad);
                    targetRot.y = glm::degrees(yawRad);
                    targetRot.z = 0.0f;
                }
            }
        }
    } else if (activeVcam->mode == CinemachineMode::Follow2D) {
        // 2D Follow Mode
        if (activeVcam->followTarget.getId() != Entity::INVALID_ENTITY && registry.isValid(activeVcam->followTarget)) {
            if (auto* targetTrans = registry.get<Transform>(activeVcam->followTarget)) {
                targetPos.x = targetTrans->position.x + activeVcam->followOffset.x;
                targetPos.y = targetTrans->position.y + activeVcam->followOffset.y;
                float defaultZ = (activeVcam->followOffset.z != 0.0f) ? activeVcam->followOffset.z : 10.0f;
                targetPos.z = targetTrans->position.z + defaultZ;
            }
        }
        targetRot = glm::vec3(activeVcam->cameraPitch, activeVcam->cameraYaw, activeVcamTransform->rotation.z);
    }

    // Apply Damping to position (disable damping in Editor mode so slider changes respond instantly)
    float followDampingVal = editorMode.isPlaying ? activeVcam->followDamping : 0.0f;
    if (activeVcam->mode == CinemachineMode::FixedLookAt) {
        followDampingVal = 0.0f;
    }

    if (followDampingVal > 0.0f) {
        float blendFactor = glm::clamp(dt * (5.0f / followDampingVal), 0.0f, 1.0f);
        activeVcam->currentPosition = glm::mix(activeVcam->currentPosition, targetPos, blendFactor);
    } else {
        activeVcam->currentPosition = targetPos;
    }

    // Apply Damping to rotation
    float lookAtDampingVal = editorMode.isPlaying ? activeVcam->lookAtDamping : 0.0f;
    if (activeVcam->mode == CinemachineMode::FirstPerson || activeVcam->mode == CinemachineMode::Follow2D) {
        lookAtDampingVal = 0.0f;
    }

    if (lookAtDampingVal > 0.0f) {
        float blendFactor = glm::clamp(dt * (5.0f / lookAtDampingVal), 0.0f, 1.0f);
        
        // Handle angle wrap-around for smooth interpolation
        float diffX = targetRot.x - activeVcam->currentRotationEuler.x;
        float diffY = targetRot.y - activeVcam->currentRotationEuler.y;
        
        // Wrap differences to [-180, 180]
        diffX = fmod(diffX + 180.0f, 360.0f);
        if (diffX < 0.0f) diffX += 360.0f;
        diffX -= 180.0f;
        
        diffY = fmod(diffY + 180.0f, 360.0f);
        if (diffY < 0.0f) diffY += 360.0f;
        diffY -= 180.0f;

        activeVcam->currentRotationEuler.x += diffX * blendFactor;
        activeVcam->currentRotationEuler.y += diffY * blendFactor;
        activeVcam->currentRotationEuler.z = targetRot.z;
    } else {
        activeVcam->currentRotationEuler = targetRot;
    }

    // Write back resolved values to the virtual camera's transform component so it can be inspected
    activeVcamTransform->position = activeVcam->currentPosition;
    activeVcamTransform->rotation = activeVcam->currentRotationEuler;
    activeVcam->lastTransformYaw = activeVcamTransform->rotation.y;
    activeVcam->lastTransformPitch = activeVcamTransform->rotation.x;

    // 3. Handle Cinemachine Brain Blending transitions
    if (activeVcamEntity != lastActiveCameraEntity) {
        if (lastActiveCameraEntity.getId() != Entity::INVALID_ENTITY && registry.isValid(lastActiveCameraEntity)) {
            // Start a new blend
            isBlending = true;
            blendTimer = 0.0f;
            
            // Start blend from the last camera's final position and rotation
            if (auto* lastTrans = registry.get<Transform>(lastActiveCameraEntity)) {
                blendStartPos = lastTrans->position;
                blendStartRot = lastTrans->rotation;
            } else {
                blendStartPos = activeVcamTransform->position;
                blendStartRot = activeVcamTransform->rotation;
            }
        }
        lastActiveCameraEntity = activeVcamEntity;
    }

    glm::vec3 finalPos = activeVcamTransform->position;
    glm::vec3 finalRot = activeVcamTransform->rotation;

    if (isBlending) {
        blendTimer += dt;
        float t = glm::clamp(blendTimer / blendDuration, 0.0f, 1.0f);
        
        // Smoothstep (Ease-In-Out) blending
        float smoothT = t * t * (3.0f - 2.0f * t);

        finalPos = glm::mix(blendStartPos, activeVcamTransform->position, smoothT);
        
        // Interpolate angles smoothly
        float diffX = activeVcamTransform->rotation.x - blendStartRot.x;
        float diffY = activeVcamTransform->rotation.y - blendStartRot.y;
        diffX = fmod(diffX + 180.0f, 360.0f);
        if (diffX < 0) diffX += 360.0f;
        diffX -= 180.0f;

        diffY = fmod(diffY + 180.0f, 360.0f);
        if (diffY < 0) diffY += 360.0f;
        diffY -= 180.0f;

        finalRot.x = blendStartRot.x + diffX * smoothT;
        finalRot.y = blendStartRot.y + diffY * smoothT;
        finalRot.z = glm::mix(blendStartRot.z, activeVcamTransform->rotation.z, smoothT);

        if (t >= 1.0f) {
            isBlending = false;
        }
    }

    // 4. Update the main scene camera (first active non-editor camera in registry)
    for (auto [entity, camDummy] : registry.view<Camera>()) {
        if (!registry.has<EditorCamera>(entity)) {
            if (auto* transform = registry.get<Transform>(entity)) {
                transform->position = finalPos;
                transform->rotation = finalRot;
            }
            if (auto* cam = registry.get<Camera>(entity)) {
                cam->fov = activeVcam->fov;
            }
            break; // Only update one main camera
        }
    }
}
