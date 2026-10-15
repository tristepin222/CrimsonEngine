#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "editor/EditorModeState.hpp"
#include "renderer/VulkanRenderer.hpp"
#include <vector>
#include <functional>
#include <glm/glm.hpp>

#include "meta/ComponentReflection.hpp"

// [ReflectEnum]
enum class AStarNavMode {
    Auto = 0,
    Tilemap2D = 1,
    GridWorld3D = 2
};

// [ReflectClass]
struct AStarAgent {
    // [ReflectField]
    AStarNavMode navMode = AStarNavMode::Auto;
    // [ReflectField]
    Entity targetTransform = Entity();
    // [ReflectField]
    float speed = 2.0f;
    // [ReflectField]
    bool allowDiagonal = true;
    // [ReflectField]
    int maxElevationStep = 1;
    // [ReflectField]
    float turnSpeed = 10.0f;
    // [ReflectField]
    bool showDebugPath = true;

    // Cached computed 2D path
    std::vector<glm::ivec2> path;
    glm::ivec2 lastStart = glm::ivec2(-9999);
    glm::ivec2 lastTarget = glm::ivec2(-9999);

    // Cached computed 3D path
    std::vector<glm::ivec3> path3D;
    glm::ivec3 lastStart3D = glm::ivec3(-9999);
    glm::ivec3 lastTarget3D = glm::ivec3(-9999);

    // Cooldown timer to avoid running A* every frame if target is unreachable
    float repathCooldown = 0.0f;
};
REGISTER_COMPONENT(AStarAgent, "AI/AStar Agent");

namespace Engine {
    struct TilemapComponent;
    struct GridWorldComponent;
    struct TerrainComponent;
}

namespace AStar {
    /**
     * @brief High-level A* pathfinding calculation on a Tilemap Component.
     */
    std::vector<glm::ivec2> findPath(
        const Engine::TilemapComponent& tilemap,
        const glm::ivec2& start,
        const glm::ivec2& end,
        const std::vector<int>& blockedTileIds = {},
        bool allowDiagonal = true
    );

    /**
     * @brief High-level A* pathfinding calculation on a GridWorld Component or Terrain (3D terrain).
     */
    std::vector<glm::ivec3> findPath3D(
        const Engine::GridWorldComponent* grid,
        const Engine::TerrainComponent* terrain,
        const glm::mat4& terrainMatrix,
        float cellSize,
        const glm::vec3& gridOrigin,
        const glm::ivec3& start,
        const glm::ivec3& end,
        bool allowDiagonal = true,
        int maxElevationStep = 1,
        int maxSearchNodes = 1500,
        const std::function<bool(int, int)>& isCustomBlocked = nullptr
    );
}

// [ReflectClass]
class AStarSystem : public System {
public:
    AStarSystem(Registry& reg, VulkanRenderer& renderer, EditorModeState& editorMode);
    ~AStarSystem() = default;

    void update(float dt) override;
    void renderDebugUI() override;

private:
    Registry& registry;
    VulkanRenderer& renderer;
    EditorModeState& editorMode;
};
