#pragma once

#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "editor/EditorModeState.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "ecs/components/GridWorldComponent.hpp"
#include "ecs/components/Transform.hpp"
#include "core/EngineAPI.hpp"
#include <vector>
#include <optional>
#include <functional>
#include <glm/glm.hpp>

namespace Engine {

    /**
     * @struct GridRaycastHit
     * @brief Detailed information returned when raycasting against the 3D grid world.
     */
    struct GridRaycastHit {
        bool hit = false;
        glm::ivec3 cellCoord{ 0 };
        glm::vec3 hitPoint{ 0.0f };
        glm::vec3 hitNormal{ 0.0f, 1.0f, 0.0f };
        float distance = 0.0f;
        Entity gridEntity = Entity();
        GridCell cellData{};
    };

    /**
     * @class GridWorldSystem
     * @brief High-performance spatial indexing, 3D voxel DDA raycasting,
     * cell interaction, farming state queries, and 3D A* pathfinding.
     */
    class ENGINE_API GridWorldSystem : public System {
    public:
        const char* getName() const override { return "GridWorldSystem"; }

        GridWorldSystem(Registry& reg, VulkanRenderer& renderer, EditorModeState& editorMode);
        ~GridWorldSystem() override = default;

        void update(float dt) override;
        void renderDebugUI() override;

        // -------------------------------------------------------------------
        // Spatial Raycasting & Picking
        // -------------------------------------------------------------------

        /**
         * @brief Casts a ray through the 3D grid using voxel DDA traversal to find intersected cells.
         * @param rayOrigin Starting world position of the ray.
         * @param rayDir Normalized ray direction vector.
         * @param maxDistance Maximum distance to traverse along ray.
         * @param outHit Detailed hit information struct.
         * @param targetEntity Optional specific GridWorld entity. If not provided, tests all grid worlds in the scene.
         * @return True if a non-empty or ground cell was intersected.
         */
        bool raycastGrid(
            const glm::vec3& rayOrigin,
            const glm::vec3& rayDir,
            float maxDistance,
            GridRaycastHit& outHit,
            Entity targetEntity = Entity()
        ) const;

        /**
         * @brief Samples terrain elevation at a given world (X, Z) coordinate across active terrains.
         * Falls back to defaultY if no terrain is found at that coordinate.
         */
        float sampleSurfaceElevation(float worldX, float worldZ, float defaultY = 0.0f) const;

        /**
         * @brief Returns the grid cell currently hovered by the mouse/camera cursor.
         */
        bool getHoveredCell(glm::ivec3& outCell, Entity* outGridEntity = nullptr) const;

        // -------------------------------------------------------------------
        // Cell Queries & Modification
        // -------------------------------------------------------------------

        bool isCellWalkable(Entity gridEntity, const glm::ivec3& cellCoord) const;
        bool isCellOccupied(Entity gridEntity, const glm::ivec3& cellCoord) const;

        Entity getOccupant(Entity gridEntity, const glm::ivec3& cellCoord) const;
        bool setOccupant(Entity gridEntity, const glm::ivec3& cellCoord, Entity occupant, bool markOccupied = true);
        bool clearOccupant(Entity gridEntity, const glm::ivec3& cellCoord);

        bool isCellBuildable(Entity gridEntity, const glm::ivec3& cellCoord) const;
        bool isAreaBuildable(Entity gridEntity, const glm::ivec3& originCell, const glm::ivec2& footprintSize) const;
        bool setAreaOccupied(Entity gridEntity, const glm::ivec3& originCell, const glm::ivec2& footprintSize, Entity occupant, bool markOccupied = true);
        bool isCellBlocked(Entity gridEntity, const glm::ivec3& cellCoord) const;
        void setCellBlocked(Entity gridEntity, const glm::ivec3& cellCoord, bool blocked);
        bool setAreaBlocked(Entity gridEntity, const glm::ivec3& originCell, const glm::ivec2& footprintSize, bool blocked);
        glm::vec3 snapPosition(Entity gridEntity, const glm::vec3& worldPos) const;

        // -------------------------------------------------------------------
        // Neighbors & Area Queries
        // -------------------------------------------------------------------

        std::vector<glm::ivec3> getNeighbors(
            Entity gridEntity,
            const glm::ivec3& cellCoord,
            bool includeDiagonals = false,
            int maxElevationStep = 1
        ) const;

        std::vector<glm::ivec3> getCellsInRadius(
            Entity gridEntity,
            const glm::ivec3& center,
            float radius
        ) const;

        std::vector<glm::ivec3> getCellsInBounds(
            Entity gridEntity,
            const glm::ivec3& minCell,
            const glm::ivec3& maxCell
        ) const;

        // -------------------------------------------------------------------
        // 3D Grid Pathfinding (A*)
        // -------------------------------------------------------------------

        /**
         * @brief Finds the shortest walkable path between startCell and targetCell across 3D terrain elevation steps.
         */
        std::vector<glm::ivec3> findPath(
            Entity gridEntity,
            const glm::ivec3& startCell,
            const glm::ivec3& targetCell,
            bool allowDiagonal = false,
            int maxElevationStep = 1,
            int maxSearchNodes = 2000
        ) const;

    private:
        Registry& registry;
        VulkanRenderer& renderer;
        EditorModeState& editorMode;

        // Active hover state
        bool m_hasHoveredCell = false;
        glm::ivec3 m_hoveredCellCoord{ 0 };
        Entity m_hoveredGridEntity = Entity();
        glm::vec3 m_hoveredHitPoint{ 0.0f };
        glm::vec2 m_prevMousePos{ -1.0f, -1.0f };
        glm::vec3 m_prevCameraPos{ 0.0f };

        void updateHoveredCell();
    };

} // namespace Engine
