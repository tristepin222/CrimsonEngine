#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "editor/EditorModeState.hpp"
#include "CinemachineComponent.hpp"

// [ReflectClass]
class CinemachineSystem : public System {
public:
    CinemachineSystem(Registry& reg, class VulkanRenderer& renderer, EditorModeState& editorMode);
    ~CinemachineSystem() = default;

    void update(float dt) override;

private:
    Registry& registry;
    class VulkanRenderer& renderer;
    EditorModeState& editorMode;

    // Blending state
    Entity lastActiveCameraEntity = Entity();
    Entity lastHiddenEntity = Entity();
    glm::vec3 blendStartPos = glm::vec3(0.0f);
    glm::vec3 blendStartRot = glm::vec3(0.0f);
    float blendTimer = 0.0f;
    float blendDuration = 1.5f; // Blend over 1.5 seconds
    bool isBlending = false;
};
