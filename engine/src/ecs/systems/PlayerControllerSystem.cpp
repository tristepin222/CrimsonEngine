#include "ecs/systems/PlayerControllerSystem.hpp"
#include "ecs/components/PlayerControllerComponent.hpp"
#include "ecs/components/Camera.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/EditorCamera.hpp"
#include "ecs/components/AnimationController.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>

PlayerControllerSystem::PlayerControllerSystem(Registry& reg, VulkanRenderer& renderer, EditorModeState& editorMode)
    : registry(reg), renderer(renderer), editorMode(editorMode) {}

void PlayerControllerSystem::update(float dt) {
    if (!editorMode.isPlaying || !editorMode.flyMode) {
        return;
    }

    GLFWwindow* window = renderer.getWindow();
    if (!window) return;

    // Find camera direction (projected on horizontal plane)
    glm::vec3 camForward(0.0f, 0.0f, -1.0f);
    glm::vec3 camRight(1.0f, 0.0f, 0.0f);

    for (auto [camEntity, cam, camTransform] : registry.view<Camera, Transform>()) {
        if (registry.has<EditorCamera>(camEntity)) continue; // Skip editor camera in play mode
        
        float yaw = camTransform.rotation.y;
        camForward.x = cos(glm::radians(yaw));
        camForward.y = 0.0f;
        camForward.z = sin(glm::radians(yaw));
        if (glm::length(camForward) > 1e-4f) {
            camForward = glm::normalize(camForward);
        }
        camRight = glm::normalize(glm::cross(camForward, glm::vec3(0.0f, 1.0f, 0.0f)));
        break; // Use the first gameplay camera found
    }

    // Process all entities with PlayerControllerComponent + Transform
    for (auto [entity, pc, transform] : registry.view<PlayerControllerComponent, Transform>()) {
        auto* rb = registry.get<RigidBodyComponent>(entity);
        if (!rb) continue;

        // Force character transform to stay perfectly upright (0 pitch and roll) to prevent toppling/wobbling
        transform.rotation.x = 0.0f;
        transform.rotation.z = 0.0f;



        // Freeze physics rotation to prevent tipping over or spinning due to contact friction
        rb->freezeRotationX = true;
        rb->freezeRotationY = true;
        rb->freezeRotationZ = true;

        pc.debugRunningCount++;

        // 1. WASD Movement
        glm::vec3 moveDir(0.0f);
        if (renderer.getKey(GLFW_KEY_W)) moveDir += camForward;
        if (renderer.getKey(GLFW_KEY_S)) moveDir -= camForward;
        if (renderer.getKey(GLFW_KEY_A)) moveDir -= camRight;
        if (renderer.getKey(GLFW_KEY_D)) moveDir += camRight;

        pc.debugMoveDirLength = glm::length(moveDir);

        if (glm::length(moveDir) > 1e-4f) {
            moveDir = glm::normalize(moveDir);
            glm::vec3 targetVel = moveDir * pc.speed;
            rb->velocity.x = targetVel.x;
            rb->velocity.z = targetVel.z;
            rb->sleeping = false;
            rb->sleepTimer = 0.0f;

            if (pc.orientToMovement) {
                float targetPhysicsYaw = glm::degrees(atan2(moveDir.z, moveDir.x));
                float targetMeshYaw = -targetPhysicsYaw + 90.0f;
                float currentYaw = transform.rotation.y;
                float diff = targetMeshYaw - currentYaw;
                while (diff < -180.0f) diff += 360.0f;
                while (diff > 180.0f) diff -= 360.0f;
                transform.rotation.y = currentYaw + diff * std::min(1.0f, 10.0f * dt);
            }
        } else {
            // Decay horizontal velocity when no keys held
            rb->velocity.x *= std::exp(-15.0f * dt);
            rb->velocity.z *= std::exp(-15.0f * dt);
        }
        pc.debugRbVelocity = rb->velocity;

        // Automatically update Animation Controller locomotion parameters based on WASD keys
        if (auto* animController = registry.get<AnimationControllerComponent>(entity)) {
            float targetX = 0.0f;
            float targetY = 0.0f;

            if (renderer.getKey(GLFW_KEY_W)) targetY += 1.0f;
            if (renderer.getKey(GLFW_KEY_S)) targetY -= 1.0f;
            if (renderer.getKey(GLFW_KEY_D)) targetX += 1.0f;
            if (renderer.getKey(GLFW_KEY_A)) targetX -= 1.0f;

            // Smoothly interpolate the parameters towards targets
            float currentX = animController->parameters["velocityX"];
            float currentY = animController->parameters["velocityY"];

            currentX += (targetX - currentX) * std::min(1.0f, 15.0f * dt);
            currentY += (targetY - currentY) * std::min(1.0f, 15.0f * dt);

            animController->parameters["velocityX"] = currentX;
            animController->parameters["velocityY"] = currentY;
            animController->parameters["speed"] = glm::length(glm::vec2(currentX, currentY));
        }

        // 2. Jump (Space)
        bool spaceDown = renderer.getKey(GLFW_KEY_SPACE);
        if (spaceDown && !pc.wasJumpPressed) {
            if (rb->hadContactThisFrame || std::abs(rb->velocity.y) < 0.05f) {
                rb->velocity.y = pc.jumpForce;
                rb->sleeping = false;
                rb->sleepTimer = 0.0f;
            }
        }
        pc.wasJumpPressed = spaceDown;

        // 3. Interact (E)
        bool interactDown = renderer.getKey(GLFW_KEY_E);
        if (interactDown && !pc.wasInteractPressed) {
            Entity closestEntity = Entity();
            float closestDist = pc.interactRange;

            for (auto [otherEntity, otherTransform, otherRb] : registry.view<Transform, RigidBodyComponent>()) {
                if (otherEntity == entity) continue;
                float dist = glm::distance(transform.position, otherTransform.position);
                if (dist < closestDist) {
                    closestDist = dist;
                    closestEntity = otherEntity;
                }
            }

            if (closestEntity.getId() != Entity::INVALID_ENTITY && registry.isValid(closestEntity)) {
                std::string nameStr = "Entity " + std::to_string(closestEntity.getId());
                if (auto* nameComp = registry.get<Name>(closestEntity)) {
                    nameStr = nameComp->value;
                }
                std::cout << "[PlayerController] Interacted with: " << nameStr << std::endl;

                auto* otherRb = registry.get<RigidBodyComponent>(closestEntity);
                auto* closestTransform = registry.get<Transform>(closestEntity);
                if (otherRb) {
                    glm::vec3 pushDir(0.0f, 1.0f, 0.0f);
                    if (closestTransform) {
                        pushDir = closestTransform->position - transform.position;
                        pushDir.y = 0.0f;
                    }
                    if (glm::length(pushDir) < 1e-4f) {
                        pushDir = glm::vec3(0.0f, 1.0f, 0.0f);
                    } else {
                        pushDir = glm::normalize(pushDir);
                        pushDir.y = 0.5f;
                        pushDir = glm::normalize(pushDir);
                    }
                    otherRb->velocity = pushDir * 6.0f;
                    otherRb->sleeping = false;
                    otherRb->sleepTimer = 0.0f;
                }
            }
        }
        pc.wasInteractPressed = interactDown;
    }
}
