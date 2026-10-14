#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "editor/EditorModeState.hpp"

/**
 * @class PlayerControllerSystem
 * @brief Built-in system that processes PlayerControllerComponent entities to drive
 *        movement (WASD), jumping (Space), and interaction (E) via physics impulses.
 *        Runs automatically in play mode when an entity has PlayerControllerComponent + RigidBodyComponent.
 */
class PlayerControllerSystem : public System {
public:
    const char* getName() const override { return "PlayerControllerSystem"; }
    SystemStage getStage() const override { return SystemStage::Update; }

    PlayerControllerSystem(Registry& reg, VulkanRenderer& renderer, EditorModeState& editorMode);

    void update(float dt) override;


private:
    Registry& registry;
    VulkanRenderer& renderer;
    EditorModeState& editorMode;
};
