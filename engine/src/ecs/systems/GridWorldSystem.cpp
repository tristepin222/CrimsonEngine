#include "ecs/systems/GridWorldSystem.hpp"
#include "ecs/components/TerrainComponent.hpp"
#include "ecs/systems/TerrainSystem.hpp"
#include <iostream>
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <GLFW/glfw3.h>
#include <imgui.h>

namespace Engine {

    GridWorldSystem::GridWorldSystem(Registry& reg, VulkanRenderer& rend, EditorModeState& mode)
        : registry(reg), renderer(rend), editorMode(mode) {
    }

    void GridWorldSystem::update(float dt) {
        (void)dt;
        updateHoveredCell();
    }

    float GridWorldSystem::sampleSurfaceElevation(float worldX, float worldZ, float defaultY) const {
        for (auto [tEnt, tComp, tTrans] : registry.view<TerrainComponent, Transform>()) {
            glm::mat4 invM = glm::inverse(tTrans.matrix());
            glm::vec3 lPos = glm::vec3(invM * glm::vec4(worldX, 0.0f, worldZ, 1.0f));
            float halfX = tComp.sizeX * 0.5f;
            float halfZ = tComp.sizeZ * 0.5f;
            if (lPos.x >= -halfX && lPos.x <= halfX && lPos.z >= -halfZ && lPos.z <= halfZ) {
                float lY = tComp.getInterpolatedHeight(lPos.x, lPos.z);
                return (tTrans.matrix() * glm::vec4(lPos.x, lY, lPos.z, 1.0f)).y;
            }
        }
        return defaultY;
    }

    void GridWorldSystem::updateHoveredCell() {
        m_hasHoveredCell = false;
        m_hoveredGridEntity = Entity();

        GLFWwindow* window = renderer.getWindow();
        if (!window) return;

        // If in fly-camera mode with mouse captured, do not pick via cursor
        if (editorMode.flyMode && !editorMode.isEditorActive) {
            return;
        }

        int width = 0, height = 0;
        renderer.getWindowSize(&width, &height);
        if (width <= 0 || height <= 0) return;

        double mouseX = 0.0, mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        float leftWidth = glm::clamp(static_cast<float>(width) * 0.20f, 260.0f, 400.0f);
        float rightWidth = glm::clamp(static_cast<float>(width) * 0.22f, 320.0f, 460.0f);
        float topY = 22.0f;
        float workHeight = static_cast<float>(height) - topY;
        float bottomHeight = workHeight * 0.32f;

        if (mouseX < leftWidth || mouseX > (static_cast<float>(width) - rightWidth) ||
            mouseY < topY || mouseY > (static_cast<float>(height) - bottomHeight)) {
            return;
        }

        glm::vec3 camPos = renderer.getActiveCameraPosition();
        glm::vec2 mousePos(static_cast<float>(mouseX), static_cast<float>(mouseY));
        if (mousePos == m_prevMousePos && camPos == m_prevCameraPos && m_hasHoveredCell) {
            return;
        }
        m_prevMousePos = mousePos;
        m_prevCameraPos = camPos;

        const float normalizedX = static_cast<float>((2.0 * mouseX) / static_cast<double>(width) - 1.0);
        const float normalizedY = static_cast<float>((2.0 * mouseY) / static_cast<double>(height) - 1.0);

        const glm::mat4 inverseVP = glm::inverse(renderer.getActiveCameraViewProj());
        const glm::vec4 clipMid = inverseVP * glm::vec4(normalizedX, normalizedY, 0.5f, 1.0f);
        if (std::abs(clipMid.w) < 1e-7f) {
            return;
        }

        const glm::vec3 ptMid = glm::vec3(clipMid) / clipMid.w;
        const glm::vec3 rayOrigin = renderer.getActiveCameraPosition();
        const glm::vec3 rayDir = glm::normalize(ptMid - rayOrigin);

        GridRaycastHit hit{};
        if (raycastGrid(rayOrigin, rayDir, 250.0f, hit)) {
            m_hasHoveredCell = true;
            m_hoveredCellCoord = hit.cellCoord;
            m_hoveredGridEntity = hit.gridEntity;
            m_hoveredHitPoint = hit.hitPoint;
        }
    }

    bool GridWorldSystem::raycastGrid(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        float maxDistance,
        GridRaycastHit& outHit,
        Entity targetEntity
    ) const {
        outHit.hit = false;
        outHit.distance = maxDistance;

        // 1. Raycast against active 3D terrains first!
        Entity hitTerrainEntity{};
        glm::vec3 hitTerrainWorldPos(0.0f);
        float closestTerrainDist = maxDistance;

        for (auto [tEnt, tComp, tTrans] : registry.view<TerrainComponent, Transform>()) {
            glm::vec3 tHitWorld;
            glm::vec2 tHitLocal;
            if (TerrainSystem::raycast(rayOrigin, rayDir, tTrans.matrix(), tComp, tHitWorld, tHitLocal)) {
                float dist = glm::distance(rayOrigin, tHitWorld);
                if (dist < closestTerrainDist) {
                    closestTerrainDist = dist;
                    hitTerrainWorldPos = tHitWorld;
                    hitTerrainEntity = tEnt;
                }
            }
        }

        auto testGrid = [&](Entity entity, const GridWorldComponent& grid, const Transform& transform) -> bool {
            if (hitTerrainEntity.getId() != Entity::INVALID_ENTITY) {
                outHit.hit = true;
                outHit.distance = closestTerrainDist;
                outHit.hitPoint = hitTerrainWorldPos;
                outHit.hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                outHit.gridEntity = entity;
                outHit.cellCoord = grid.worldToCell(hitTerrainWorldPos);
                const GridCell* c = grid.getCell(outHit.cellCoord);
                if (c) outHit.cellData = *c;
                return true;
            }

            glm::mat4 model = transform.matrix();
            glm::mat4 invModel = glm::inverse(model);

            // Transform ray into grid local space
            glm::vec4 localOrigin4 = invModel * glm::vec4(rayOrigin, 1.0f);
            glm::vec3 localOrigin = glm::vec3(localOrigin4) / localOrigin4.w;
            glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));

            float cellSize = grid.cellSize > 0.001f ? grid.cellSize : 1.0f;
            float cellHeight = grid.cellHeight > 0.001f ? grid.cellHeight : 0.5f;

            // 1. First, check if ray hits horizontal ground plane at Y = originOffset.y
            bool groundHitFound = false;
            float groundDist = maxDistance;
            glm::ivec3 groundCellCoord{ 0 };
            glm::vec3 groundHitPoint{ 0.0f };

            if (glm::abs(localDir.y) > 0.00001f) {
                float tGround = (grid.originOffset.y - localOrigin.y) / localDir.y;
                if (tGround > 0.0f && tGround < maxDistance) {
                    glm::vec3 hitLocal = localOrigin + localDir * tGround;
                    glm::ivec3 cellCoord = grid.worldToCell(hitLocal);

                    // Ensure within reasonable bounds (-500 to +500 cells)
                    if (cellCoord.x >= -500 && cellCoord.x <= 500 &&
                        cellCoord.z >= -500 && cellCoord.z <= 500) {
                        groundHitFound = true;
                        groundDist = tGround;
                        groundCellCoord = cellCoord;
                        groundCellCoord.y = 0; // Ground plane elevation
                        groundHitPoint = glm::vec3(model * glm::vec4(hitLocal, 1.0f));
                    }
                }
            }

            // 2. 3D Voxel DDA traversal across populated chunks
            // Digital Differential Analyzer steps grid cell-by-cell in local space
            glm::ivec3 currentCell = grid.worldToCell(localOrigin);
            glm::vec3 step(
                (localDir.x >= 0.0f) ? 1.0f : -1.0f,
                (localDir.y >= 0.0f) ? 1.0f : -1.0f,
                (localDir.z >= 0.0f) ? 1.0f : -1.0f
            );

            glm::vec3 cellMin = grid.cellToWorld(currentCell);
            glm::vec3 cellMax = cellMin + glm::vec3(cellSize, cellHeight, cellSize);

            glm::vec3 nextBoundary(
                (step.x > 0.0f) ? cellMax.x : cellMin.x,
                (step.y > 0.0f) ? cellMax.y : cellMin.y,
                (step.z > 0.0f) ? cellMax.z : cellMin.z
            );

            glm::vec3 tMax(
                (glm::abs(localDir.x) > 1e-6f) ? (nextBoundary.x - localOrigin.x) / localDir.x : 1e9f,
                (glm::abs(localDir.y) > 1e-6f) ? (nextBoundary.y - localOrigin.y) / localDir.y : 1e9f,
                (glm::abs(localDir.z) > 1e-6f) ? (nextBoundary.z - localOrigin.z) / localDir.z : 1e9f
            );

            glm::vec3 tDelta(
                (glm::abs(localDir.x) > 1e-6f) ? cellSize / glm::abs(localDir.x) : 1e9f,
                (glm::abs(localDir.y) > 1e-6f) ? cellHeight / glm::abs(localDir.y) : 1e9f,
                (glm::abs(localDir.z) > 1e-6f) ? cellSize / glm::abs(localDir.z) : 1e9f
            );

            float travelled = 0.0f;
            glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
            bool voxelHitFound = false;

            int maxSteps = std::min(300, static_cast<int>(maxDistance / std::min(cellSize, cellHeight)));
            for (int s = 0; s < maxSteps && travelled < maxDistance; ++s) {
                const GridCell* cell = grid.getCell(currentCell);
                if (cell && (cell->isOccupied() || (cell->flags & GridCell_Blocked))) {
                    voxelHitFound = true;
                    outHit.hit = true;
                    outHit.cellCoord = currentCell;
                    outHit.cellData = *cell;
                    outHit.distance = travelled;
                    outHit.hitPoint = glm::vec3(model * glm::vec4(localOrigin + localDir * travelled, 1.0f));
                    outHit.hitNormal = glm::normalize(glm::vec3(model * glm::vec4(hitNormal, 0.0f)));
                    outHit.gridEntity = entity;
                    return true;
                }

                // Advance to next voxel along closest axis
                if (tMax.x < tMax.y) {
                    if (tMax.x < tMax.z) {
                        travelled = tMax.x;
                        tMax.x += tDelta.x;
                        currentCell.x += static_cast<int>(step.x);
                        hitNormal = glm::vec3(-step.x, 0.0f, 0.0f);
                    } else {
                        travelled = tMax.z;
                        tMax.z += tDelta.z;
                        currentCell.z += static_cast<int>(step.z);
                        hitNormal = glm::vec3(0.0f, 0.0f, -step.z);
                    }
                } else {
                    if (tMax.y < tMax.z) {
                        travelled = tMax.y;
                        tMax.y += tDelta.y;
                        currentCell.y += static_cast<int>(step.y);
                        hitNormal = glm::vec3(0.0f, -step.y, 0.0f);
                    } else {
                        travelled = tMax.z;
                        tMax.z += tDelta.z;
                        currentCell.z += static_cast<int>(step.z);
                        hitNormal = glm::vec3(0.0f, 0.0f, -step.z);
                    }
                }
            }

            // Fallback: If no elevated obstacle voxel was hit, use ground plane intersection
            if (groundHitFound && groundDist < outHit.distance) {
                outHit.hit = true;
                outHit.cellCoord = groundCellCoord;
                outHit.distance = groundDist;
                outHit.hitPoint = groundHitPoint;
                outHit.hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                outHit.gridEntity = entity;
                const GridCell* c = grid.getCell(groundCellCoord);
                if (c) outHit.cellData = *c;
                return true;
            }

            return false;
        };

        if (targetEntity.getId() != Entity::INVALID_ENTITY && registry.isValid(targetEntity)) {
            auto* grid = registry.get<GridWorldComponent>(targetEntity);
            auto* trans = registry.get<Transform>(targetEntity);
            if (grid && trans) {
                return testGrid(targetEntity, *grid, *trans);
            }
        }

        // Test all grid worlds
        bool anyHit = false;
        for (auto [e, grid, trans] : registry.view<GridWorldComponent, Transform>()) {
            if (testGrid(e, grid, trans)) {
                anyHit = true;
            }
        }

        return anyHit;
    }

    bool GridWorldSystem::getHoveredCell(glm::ivec3& outCell, Entity* outGridEntity) const {
        if (!m_hasHoveredCell) return false;
        outCell = m_hoveredCellCoord;
        if (outGridEntity) *outGridEntity = m_hoveredGridEntity;
        return true;
    }

    bool GridWorldSystem::isCellWalkable(Entity gridEntity, const glm::ivec3& cellCoord) const {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return false;

        const GridCell* cell = grid->getCell(cellCoord);
        if (!cell) {
            // Default unallocated cells on ground level are walkable
            return cellCoord.y == 0;
        }
        return cell->isWalkable();
    }

    bool GridWorldSystem::isCellOccupied(Entity gridEntity, const glm::ivec3& cellCoord) const {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return false;

        const GridCell* cell = grid->getCell(cellCoord);
        return cell && cell->isOccupied();
    }

    Entity GridWorldSystem::getOccupant(Entity gridEntity, const glm::ivec3& cellCoord) const {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return Entity();

        const GridCell* cell = grid->getCell(cellCoord);
        if (cell && cell->occupantId != 0) {
            Entity ent = registry.getEntity(cell->occupantId);
            if (registry.isValid(ent)) return ent;
        }
        return Entity();
    }

    bool GridWorldSystem::setOccupant(Entity gridEntity, const glm::ivec3& cellCoord, Entity occupant, bool markOccupied) {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return false;

        GridCell& cell = grid->getOrCreateCell(cellCoord);
        cell.occupantId = occupant.getId();
        if (markOccupied) {
            cell.flags |= GridCell_Occupied;
        }
        return true;
    }

    bool GridWorldSystem::clearOccupant(Entity gridEntity, const glm::ivec3& cellCoord) {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return false;

        GridCell* cell = grid->getCell(cellCoord);
        if (cell) {
            cell->occupantId = 0;
            cell->flags &= ~GridCell_Occupied;
            return true;
        }
        return false;
    }

    bool GridWorldSystem::isCellBuildable(Entity gridEntity, const glm::ivec3& cellCoord) const {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return false;
        const GridCell* cell = grid->getCell(cellCoord);
        if (!cell) return true; // Default unallocated cells are buildable
        return cell->isBuildable();
    }

    bool GridWorldSystem::isAreaBuildable(Entity gridEntity, const glm::ivec3& originCell, const glm::ivec2& footprintSize) const {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return false;
        return grid->isAreaBuildable(glm::ivec2(originCell.x, originCell.z), originCell.y, footprintSize);
    }

    bool GridWorldSystem::setAreaOccupied(Entity gridEntity, const glm::ivec3& originCell, const glm::ivec2& footprintSize, Entity occupant, bool markOccupied) {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return false;
        return grid->setAreaOccupied(glm::ivec2(originCell.x, originCell.z), originCell.y, footprintSize, occupant.getId(), markOccupied);
    }

    bool GridWorldSystem::isCellBlocked(Entity gridEntity, const glm::ivec3& cellCoord) const {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return true;
        const GridCell* cell = grid->getCell(cellCoord);
        return cell ? cell->isBlocked() : false;
    }

    void GridWorldSystem::setCellBlocked(Entity gridEntity, const glm::ivec3& cellCoord, bool blocked) {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return;
        grid->setCellBlocked(cellCoord, blocked);
    }

    bool GridWorldSystem::setAreaBlocked(Entity gridEntity, const glm::ivec3& originCell, const glm::ivec2& footprintSize, bool blocked) {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return false;
        return grid->setAreaBlocked(glm::ivec2(originCell.x, originCell.z), originCell.y, footprintSize, blocked);
    }

    glm::vec3 GridWorldSystem::snapPosition(Entity gridEntity, const glm::vec3& worldPos) const {
        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return worldPos;
        return grid->snapPositionToGrid(worldPos);
    }

    std::vector<glm::ivec3> GridWorldSystem::getNeighbors(
        Entity gridEntity,
        const glm::ivec3& cellCoord,
        bool includeDiagonals,
        int maxElevationStep
    ) const {
        std::vector<glm::ivec3> results;
        results.reserve(includeDiagonals ? 8 : 4);

        auto* grid = registry.get<GridWorldComponent>(gridEntity);
        if (!grid) return results;

        static const glm::ivec2 orthogonalDirs[4] = {
            { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
        };
        static const glm::ivec2 diagonalDirs[4] = {
            { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
        };

        auto testNeighbor = [&](int dx, int dz) {
            int nx = cellCoord.x + dx;
            int nz = cellCoord.z + dz;

            if (grid->isColumnBlocked(nx, nz) || !grid->isColumnWalkable(nx, nz)) {
                return;
            }

            // Check elevation steps (climb up or down within maxElevationStep)
            for (int dy = -maxElevationStep; dy <= maxElevationStep; ++dy) {
                glm::ivec3 stepped(nx, cellCoord.y + dy, nz);
                const GridCell* c = grid->getCell(stepped);
                if (c && c->isWalkable()) {
                    results.push_back(stepped);
                    return;
                }
            }

            // If no cell recorded, ground level (y=0) is considered walkable
            if (cellCoord.y == 0) {
                results.push_back(glm::ivec3(nx, 0, nz));
            }
        };

        for (const auto& d : orthogonalDirs) {
            testNeighbor(d.x, d.y);
        }

        if (includeDiagonals) {
            for (const auto& d : diagonalDirs) {
                testNeighbor(d.x, d.y);
            }
        }

        return results;
    }

    std::vector<glm::ivec3> GridWorldSystem::getCellsInRadius(
        Entity gridEntity,
        const glm::ivec3& center,
        float radius
    ) const {
        std::vector<glm::ivec3> results;
        int r = static_cast<int>(std::ceil(radius));
        float rSq = radius * radius;

        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                if (static_cast<float>(dx * dx + dz * dz) <= rSq) {
                    results.emplace_back(center.x + dx, center.y, center.z + dz);
                }
            }
        }
        return results;
    }

    std::vector<glm::ivec3> GridWorldSystem::getCellsInBounds(
        Entity gridEntity,
        const glm::ivec3& minCell,
        const glm::ivec3& maxCell
    ) const {
        (void)gridEntity;
        std::vector<glm::ivec3> results;
        for (int y = minCell.y; y <= maxCell.y; ++y) {
            for (int z = minCell.z; z <= maxCell.z; ++z) {
                for (int x = minCell.x; x <= maxCell.x; ++x) {
                    results.emplace_back(x, y, z);
                }
            }
        }
        return results;
    }

    // -------------------------------------------------------------------
    // 3D Grid A* Pathfinding
    // -------------------------------------------------------------------
    struct AStarNode {
        glm::ivec3 coord;
        float fScore;

        bool operator>(const AStarNode& other) const {
            return fScore > other.fScore;
        }
    };

    struct IVec3Hash {
        size_t operator()(const glm::ivec3& v) const noexcept {
            size_t h1 = std::hash<int>()(v.x);
            size_t h2 = std::hash<int>()(v.y);
            size_t h3 = std::hash<int>()(v.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    std::vector<glm::ivec3> GridWorldSystem::findPath(
        Entity gridEntity,
        const glm::ivec3& startCell,
        const glm::ivec3& targetCell,
        bool allowDiagonal,
        int maxElevationStep,
        int maxSearchNodes
    ) const {
        std::vector<glm::ivec3> path;
        if (startCell == targetCell) {
            path.push_back(startCell);
            return path;
        }

        std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;
        std::unordered_map<glm::ivec3, glm::ivec3, IVec3Hash> cameFrom;
        std::unordered_map<glm::ivec3, float, IVec3Hash> gScore;

        auto heuristic = [](const glm::ivec3& a, const glm::ivec3& b) -> float {
            return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z);
        };

        gScore[startCell] = 0.0f;
        openSet.push({ startCell, heuristic(startCell, targetCell) });

        int nodesSearched = 0;
        bool found = false;

        while (!openSet.empty() && nodesSearched < maxSearchNodes) {
            glm::ivec3 current = openSet.top().coord;
            openSet.pop();
            nodesSearched++;

            if (current == targetCell) {
                found = true;
                break;
            }

            std::vector<glm::ivec3> neighbors = getNeighbors(gridEntity, current, allowDiagonal, maxElevationStep);
            for (const auto& neighbor : neighbors) {
                float moveCost = (neighbor.x != current.x && neighbor.z != current.z) ? 1.414f : 1.0f;
                // Add vertical step cost
                moveCost += std::abs(neighbor.y - current.y) * 1.5f;

                float tentativeG = gScore[current] + moveCost;
                auto it = gScore.find(neighbor);
                if (it == gScore.end() || tentativeG < it->second) {
                    cameFrom[neighbor] = current;
                    gScore[neighbor] = tentativeG;
                    float f = tentativeG + heuristic(neighbor, targetCell);
                    openSet.push({ neighbor, f });
                }
            }
        }

        if (found) {
            glm::ivec3 curr = targetCell;
            while (curr != startCell) {
                path.push_back(curr);
                curr = cameFrom[curr];
            }
            path.push_back(startCell);
            std::reverse(path.begin(), path.end());
        }

        return path;
    }

    // -------------------------------------------------------------------
    // Debug Visualizer & Overlay
    // -------------------------------------------------------------------
    void GridWorldSystem::renderDebugUI() {
        // Viewport grid drawing, terrain conforming, and interactive farming tools
        // are handled in EditorUI::drawGridWorldOverlay()
    }

} // namespace Engine
