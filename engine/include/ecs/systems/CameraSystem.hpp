#pragma once
#include "../Registry.hpp"
#include "../components/Camera.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include "../System.hpp"
#include "../../renderer/VulkanRenderer.hpp"
#include "../components/Transform.hpp"
#include "../components/inputComponent.hpp"
#include "editor/EditorModeState.hpp"
#include "ecs/components/EditorCamera.hpp"
#include <imgui.h>
#include "ImGuizmo.h"
#include <algorithm>
#include <cmath>
#include <iostream>

/**
 * @class CameraSystem
 * @brief System that processes camera entities, updating their orientation, movement, aspect ratio,
 * and submitting camera matrices to the renderer. Features industry-standard editor navigation controls.
 */
class CameraSystem : public System {
public:
    /**
     * @brief Construct a new Camera System object.
     * @param reg Reference to the ECS Registry.
     * @param renderer Reference to the Vulkan Renderer.
     * @param editorMode Reference to the Editor Mode State.
     */
    CameraSystem(Registry& reg, VulkanRenderer& renderer, EditorModeState& editorMode)
        : registry(reg), renderer(renderer), editorMode(editorMode) {}

    /**
     * @brief Updates the camera's rotation, movement, aspect ratio, and view-projection matrices.
     * @param dt Delta time in seconds.
     */
    void update(float dt) override {
        GLFWwindow* window = renderer.getWindow();
        if (!window) return;

        // Ensure chained scroll callback is hooked to capture mouse wheel zoom & speed adjustment
        static bool s_callbackHooked = false;
        if (!s_callbackHooked) {
            s_prevScrollCallback = glfwSetScrollCallback(window, scrollCallback);
            s_callbackHooked = true;
        }

        int width = 0;
        int height = 0;
        glfwGetWindowSize(window, &width, &height);
        float aspect = 1.0f;
        if (height > 0) {
            aspect = static_cast<float>(width) / static_cast<float>(height);
        }

        float scroll = static_cast<float>(s_scrollDeltaY);
        s_scrollDeltaY = 0.0;

        double mouseX = 0.0, mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        if (!hasInitialMouse) {
            lastMouseX = mouseX;
            lastMouseY = mouseY;
            hasInitialMouse = true;
        }
        double dx = mouseX - lastMouseX;
        double dy = lastMouseY - mouseY; // Invert Y so up is positive pitch
        lastMouseX = mouseX;
        lastMouseY = mouseY;

        if (!editorMode.isPlaying) {
            // Editor Mode: process rich camera navigation (Fly, Pan, Orbit, Zoom) for EditorCamera
            bool foundEditorCam = false;
            for (auto [entity, cam, transform, edCam] : registry.view<Camera, Transform, EditorCamera>()) {
                foundEditorCam = true;

                bool wantCaptureMouse = (ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().WantCaptureMouse;
                bool isGuizmoUsing = ImGuizmo::IsUsing();

                bool rmbDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) && (!wantCaptureMouse || isFlying);
                bool mmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
                bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                bool altDown = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
                bool shiftDown = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
                bool ctrlDown = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);

                // --- F Key Toggle for Fly Mode (when not typing in an ImGui text field) ---
                bool wantCaptureKeyboard = (ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().WantCaptureKeyboard;
                bool fDown = (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS);
                bool fPressed = fDown && !prevFDown && (!wantCaptureKeyboard || editorMode.flyToggled) && !shiftDown;
                prevFDown = fDown;

                bool escDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
                bool escPressed = escDown && !prevEscDown;
                prevEscDown = escDown;

                if (fPressed) {
                    editorMode.flyToggled = !editorMode.flyToggled;
                    std::cout << "[CameraSystem] 'F' key pressed -> flyToggled=" << (editorMode.flyToggled ? "ON" : "OFF") << std::endl;
                }
                if (escPressed && editorMode.flyToggled) {
                    editorMode.flyToggled = false;
                    std::cout << "[CameraSystem] Escape pressed -> flyToggled=OFF" << std::endl;
                }

                // Fly mode is active if user is holding RMB (temporary) or if toggled ON with 'F' key
                bool isFlyActive = rmbDown || editorMode.flyToggled;
                editorMode.flyMode = isFlyActive;

                // --- 1. Fly Mode: Hold RMB or Toggle with 'F' key ---
                if (isFlyActive) {
                    if (!isFlying) {
                        isFlying = true;
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        glfwGetCursorPos(window, &mouseX, &mouseY);
                        lastMouseX = mouseX;
                        lastMouseY = mouseY;
                        dx = 0.0;
                        dy = 0.0;
                    }

                    // Mouse Look
                    transform.rotation.y += static_cast<float>(dx) * cam.mouseSensitivity;
                    transform.rotation.x += static_cast<float>(dy) * cam.mouseSensitivity;
                    transform.rotation.x = glm::clamp(transform.rotation.x, -89.0f, 89.0f);

                    float pitch = glm::radians(transform.rotation.x);
                    float yaw = glm::radians(transform.rotation.y);
                    glm::vec3 forward;
                    forward.x = cos(pitch) * cos(yaw);
                    forward.y = sin(pitch);
                    forward.z = cos(pitch) * sin(yaw);
                    forward = glm::normalize(forward);

                    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
                    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
                    glm::vec3 up = glm::normalize(glm::cross(right, forward));

                    // Speed Multipliers
                    float speed = cam.moveSpeed;
                    if (shiftDown) speed *= 3.5f;
                    else if (ctrlDown) speed *= 0.25f;

                    // Mouse wheel while in fly mode scales movement speed
                    if (scroll != 0.0f) {
                        cam.moveSpeed = glm::clamp(cam.moveSpeed * (1.0f + scroll * 0.15f), 0.5f, 150.0f);
                    }

                    // WASD + QE Fly Movement
                    glm::vec3 moveDir(0.0f);
                    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += forward;
                    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= forward;
                    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += right;
                    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= right;
                    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) moveDir += worldUp;
                    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) moveDir -= worldUp;

                    if (glm::length(moveDir) > 0.001f) {
                        transform.position += glm::normalize(moveDir) * speed * dt;
                    }

                    edCam.pivot = transform.position + forward * edCam.pivotDistance;
                } else {
                    if (isFlying) {
                        isFlying = false;
                        editorMode.flyMode = false;
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        glfwGetCursorPos(window, &mouseX, &mouseY);
                        lastMouseX = mouseX;
                        lastMouseY = mouseY;
                        dx = 0.0;
                        dy = 0.0;
                    }

                    float pitch = glm::radians(transform.rotation.x);
                    float yaw = glm::radians(transform.rotation.y);
                    glm::vec3 forward;
                    forward.x = cos(pitch) * cos(yaw);
                    forward.y = sin(pitch);
                    forward.z = cos(pitch) * sin(yaw);
                    forward = glm::normalize(forward);

                    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
                    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
                    glm::vec3 up = glm::normalize(glm::cross(right, forward));

                    // --- 2. Orbit Mode: Alt + Left Mouse Button (Maya / Unity Style) ---
                    if (altDown && lmbDown && !isGuizmoUsing && (!wantCaptureMouse || isOrbiting)) {
                        if (!isOrbiting) {
                            isOrbiting = true;
                            lastMouseX = mouseX;
                            lastMouseY = mouseY;
                            dx = 0.0;
                            dy = 0.0;
                        }

                        transform.rotation.y += static_cast<float>(dx) * cam.mouseSensitivity;
                        transform.rotation.x += static_cast<float>(dy) * cam.mouseSensitivity;
                        transform.rotation.x = glm::clamp(transform.rotation.x, -89.0f, 89.0f);

                        float newPitch = glm::radians(transform.rotation.x);
                        float newYaw = glm::radians(transform.rotation.y);
                        glm::vec3 newForward;
                        newForward.x = cos(newPitch) * cos(newYaw);
                        newForward.y = sin(newPitch);
                        newForward.z = cos(newPitch) * sin(newYaw);
                        newForward = glm::normalize(newForward);

                        transform.position = edCam.pivot - newForward * edCam.pivotDistance;
                    } else {
                        isOrbiting = false;

                        // --- 3. Pan Mode: Middle Mouse Button Drag (Blender / Maya / Unity Style) ---
                        if (mmbDown && (!wantCaptureMouse || isPanning)) {
                            if (!isPanning) {
                                isPanning = true;
                                lastMouseX = mouseX;
                                lastMouseY = mouseY;
                                dx = 0.0;
                                dy = 0.0;
                            }

                            float panSpeed = cam.moveSpeed * 0.0018f * std::max(1.0f, edCam.pivotDistance * 0.12f);
                            transform.position -= right * static_cast<float>(dx) * panSpeed;
                            transform.position -= up * static_cast<float>(dy) * panSpeed;
                            edCam.pivot = transform.position + forward * edCam.pivotDistance;
                        } else {
                            isPanning = false;

                            // --- 4. Zoom Mode: Mouse Wheel Scroll ---
                            if (!wantCaptureMouse && scroll != 0.0f) {
                                float zoomStep = scroll * (cam.moveSpeed * 0.35f);
                                transform.position += forward * zoomStep;
                                edCam.pivotDistance = std::max(0.5f, edCam.pivotDistance - zoomStep);
                            }
                        }
                    }
                }

                cam.aspect = aspect;
                cam.farPlane = 25000.0f;
                renderer.setActiveCamera(cam.projection(), transform.position, cam.view(transform));
                return; // Done
            }

            if (!foundEditorCam) {
                static bool warnedOnce = false;
                if (!warnedOnce) {
                    std::cout << "[CameraSystem] WARNING: EditorCamera was not found in view<Camera, Transform, EditorCamera>!" << std::endl;
                    warnedOnce = true;
                }
            }
        } else {
            // Play Mode: update and render through the Scene's Camera (the first non-editor camera)
            Entity fallbackCam = Entity();
            for (auto [entity, cam, transform] : registry.view<Camera, Transform>()) {
                if (!registry.has<EditorCamera>(entity)) {
                    cam.aspect = aspect;
                    cam.farPlane = std::max(cam.farPlane, 25000.0f);
                    glm::mat4 vp = cam.projection() * cam.view(transform);
                    renderer.setActiveCamera(cam.projection(), transform.position, cam.view(transform));
                    renderer.setGameplayCamera(vp, transform.position);
                    return; // Done
                } else {
                    fallbackCam = entity;
                }
            }

            // Fallback to Editor Camera if no scene camera exists
            if (fallbackCam.getId() != Entity::INVALID_ENTITY && registry.isValid(fallbackCam)) {
                auto* cam = registry.get<Camera>(fallbackCam);
                auto* transform = registry.get<Transform>(fallbackCam);
                if (cam && transform) {
                    cam->aspect = aspect;
                    cam->farPlane = std::max(cam->farPlane, 25000.0f);
                    glm::mat4 vp = cam->projection() * cam->view(*transform);
                    renderer.setActiveCamera(cam->projection(), transform->position, cam->view(*transform));
                    renderer.setGameplayCamera(vp, transform->position);
                }
            }
        }
    }

private:
    /** @brief Reference to the entity registry. */
    Registry& registry;
    /** @brief Reference to the renderer. */
    VulkanRenderer& renderer;
    /** @brief Reference to the editor mode state. */
    EditorModeState& editorMode;

    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool hasInitialMouse = false;
    bool isFlying = false;
    bool prevFDown = false;
    bool prevEscDown = false;
    bool isOrbiting = false;
    bool isPanning = false;

    // Chained GLFW scroll callback state
    static inline double s_scrollDeltaY = 0.0;
    static inline GLFWscrollfun s_prevScrollCallback = nullptr;

    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        s_scrollDeltaY += yoffset;
        if (s_prevScrollCallback) {
            s_prevScrollCallback(window, xoffset, yoffset);
        }
    }
};
