#include "AStarSystem.hpp"
#include "ecs/components/Tilemap.hpp"
#include "ecs/components/GridWorldComponent.hpp"
#include "ecs/components/TerrainComponent.hpp"
#include "ecs/components/Transform.hpp"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <queue>
#include <iostream>

namespace AStar {

    // =======================================================================
    // 2D Tilemap Pathfinding (Priority Queue Optimized)
    // =======================================================================

    struct Node2D {
        int x;
        int y;
        float g = 0.0f;
        float h = 0.0f;
        float f() const { return g + h; }
        Node2D* parent = nullptr;
        bool closed = false;
        bool open = false;

        Node2D(int xVal, int yVal) : x(xVal), y(yVal) {}
    };

    struct CoordHash2D {
        size_t operator()(const glm::ivec2& p) const {
            return (static_cast<size_t>(p.x) * 73856093) ^ (static_cast<size_t>(p.y) * 19349663);
        }
    };

    struct PQItem2D {
        float f;
        float h;
        Node2D* node;

        bool operator>(const PQItem2D& other) const {
            if (f != other.f) return f > other.f;
            return h > other.h;
        }
    };

    float getHeuristic2D(int x1, int y1, int x2, int y2, bool allowDiagonal) {
        if (allowDiagonal) {
            int dx = std::abs(x1 - x2);
            int dy = std::abs(y1 - y2);
            return (dx > dy) ? (1.414f * dy + (dx - dy)) : (1.414f * dx + (dy - dx));
        } else {
            return static_cast<float>(std::abs(x1 - x2) + std::abs(y1 - y2));
        }
    }

    std::vector<glm::ivec2> findPath(
        const Engine::TilemapComponent& tilemap,
        const glm::ivec2& start,
        const glm::ivec2& end,
        const std::vector<int>& blockedTileIds,
        bool allowDiagonal
    ) {
        (void)blockedTileIds;
        std::vector<glm::ivec2> path;

        auto isPassable = [&](int x, int y) {
            return !tilemap.isTileSolid(x, y);
        };

        if (!isPassable(end.x, end.y)) {
            return path;
        }

        std::vector<std::unique_ptr<Node2D>> nodePool;
        std::unordered_map<glm::ivec2, Node2D*, CoordHash2D> nodeMap;

        auto getNode = [&](int x, int y) -> Node2D* {
            glm::ivec2 coord(x, y);
            auto it = nodeMap.find(coord);
            if (it != nodeMap.end()) return it->second;
            auto newNode = std::make_unique<Node2D>(x, y);
            Node2D* ptr = newNode.get();
            nodePool.push_back(std::move(newNode));
            nodeMap[coord] = ptr;
            return ptr;
        };

        std::priority_queue<PQItem2D, std::vector<PQItem2D>, std::greater<PQItem2D>> openQueue;

        Node2D* startNode = getNode(start.x, start.y);
        startNode->g = 0.0f;
        startNode->h = getHeuristic2D(start.x, start.y, end.x, end.y, allowDiagonal);
        startNode->open = true;
        openQueue.push({ startNode->f(), startNode->h, startNode });

        int iterations = 0;
        const int maxIterations = 2000;

        static const int dx[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        static const int dy[] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        int directions = allowDiagonal ? 8 : 4;

        while (!openQueue.empty() && iterations++ < maxIterations) {
            Node2D* current = openQueue.top().node;
            openQueue.pop();

            if (current->closed) continue;
            current->open = false;
            current->closed = true;

            if (current->x == end.x && current->y == end.y) {
                Node2D* temp = current;
                while (temp != nullptr) {
                    path.push_back(glm::ivec2(temp->x, temp->y));
                    temp = temp->parent;
                }
                std::reverse(path.begin(), path.end());
                return path;
            }

            for (int i = 0; i < directions; ++i) {
                int nx = current->x + dx[i];
                int ny = current->y + dy[i];

                if (!isPassable(nx, ny)) continue;

                if (i >= 4) {
                    if (!isPassable(current->x, ny) || !isPassable(nx, current->y)) {
                        continue;
                    }
                }

                Node2D* neighbor = getNode(nx, ny);
                if (neighbor->closed) continue;

                float moveCost = (i >= 4) ? 1.414f : 1.0f;
                float tentativeG = current->g + moveCost;

                if (!neighbor->open || tentativeG < neighbor->g) {
                    neighbor->g = tentativeG;
                    neighbor->h = getHeuristic2D(nx, ny, end.x, end.y, allowDiagonal);
                    neighbor->parent = current;
                    neighbor->open = true;
                    openQueue.push({ neighbor->f(), neighbor->h, neighbor });
                }
            }
        }

        return path;
    }

    bool isTileBlocked(const Engine::TilemapComponent& tilemap, const glm::ivec2& point, const std::vector<int>& blockedTileIds) {
        (void)blockedTileIds;
        return tilemap.isTileSolid(point.x, point.y);
    }

    // =======================================================================
    // 3D Terrain / GridWorld Pathfinding (Priority Queue Optimized)
    // =======================================================================

    struct Node3D {
        glm::ivec3 coord;
        float g = 0.0f;
        float h = 0.0f;
        float f() const { return g + h; }
        Node3D* parent = nullptr;
        bool closed = false;
        bool open = false;

        Node3D(const glm::ivec3& c) : coord(c) {}
    };

    struct CoordHash3D {
        size_t operator()(const glm::ivec3& p) const {
            return (static_cast<size_t>(p.x) * 73856093) ^ 
                   (static_cast<size_t>(p.y) * 19349663) ^ 
                   (static_cast<size_t>(p.z) * 83492791);
        }
    };

    struct PQItem3D {
        float f;
        float h;
        Node3D* node;

        bool operator>(const PQItem3D& other) const {
            if (f != other.f) return f > other.f;
            return h > other.h;
        }
    };

    float getHeuristic3D(const glm::ivec3& a, const glm::ivec3& b) {
        int dx = std::abs(a.x - b.x);
        int dy = std::abs(a.y - b.y);
        int dz = std::abs(a.z - b.z);
        return static_cast<float>(dx + dz) + static_cast<float>(dy) * 1.5f;
    }

    std::vector<glm::ivec3> findPath3D(
        const Engine::GridWorldComponent* grid,
        const Engine::TerrainComponent* terrain,
        const glm::mat4& terrainMatrix,
        float cellSize,
        const glm::vec3& gridOrigin,
        const glm::ivec3& start,
        const glm::ivec3& end,
        bool allowDiagonal,
        int maxElevationStep,
        int maxSearchNodes,
        const std::function<bool(int, int)>& isCustomBlocked
    ) {
        std::vector<glm::ivec3> path;

        auto isTileBlocked = [&](int x, int z) -> bool {
            if (isCustomBlocked && isCustomBlocked(x, z)) return true;
            if (grid && (grid->isColumnBlocked(x, z) || !grid->isColumnWalkable(x, z))) return true;
            return false;
        };

        // If end is blocked, abort immediately
        if (isTileBlocked(end.x, end.z)) {
            return path;
        }

        std::vector<std::unique_ptr<Node3D>> nodePool;
        std::unordered_map<glm::ivec3, Node3D*, CoordHash3D> nodeMap;
        glm::mat4 invTerrainM = terrain ? glm::inverse(terrainMatrix) : glm::mat4(1.0f);

        auto getNode = [&](const glm::ivec3& coord) -> Node3D* {
            auto it = nodeMap.find(coord);
            if (it != nodeMap.end()) return it->second;
            auto newNode = std::make_unique<Node3D>(coord);
            Node3D* ptr = newNode.get();
            nodePool.push_back(std::move(newNode));
            nodeMap[coord] = ptr;
            return ptr;
        };

        auto getElevationAtXZ = [&](int cx, int cz) -> float {
            if (terrain) {
                float wx = static_cast<float>(cx) * cellSize + gridOrigin.x;
                float wz = static_cast<float>(cz) * cellSize + gridOrigin.z;
                glm::vec3 localPos = glm::vec3(invTerrainM * glm::vec4(wx, 0.0f, wz, 1.0f));
                return terrain->getInterpolatedHeight(localPos.x, localPos.z);
            }
            return 0.0f;
        };

        std::priority_queue<PQItem3D, std::vector<PQItem3D>, std::greater<PQItem3D>> openQueue;

        Node3D* startNode = getNode(start);
        startNode->g = 0.0f;
        startNode->h = getHeuristic3D(start, end);
        startNode->open = true;
        openQueue.push({ startNode->f(), startNode->h, startNode });

        static const glm::ivec2 dirs4[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        static const glm::ivec2 dirsDiag[4] = { {1,1}, {1,-1}, {-1,1}, {-1,-1} };

        int iterations = 0;
        while (!openQueue.empty() && iterations++ < maxSearchNodes) {
            Node3D* current = openQueue.top().node;
            openQueue.pop();

            if (current->closed) continue;
            current->open = false;
            current->closed = true;

            if (current->coord.x == end.x && current->coord.z == end.z) {
                Node3D* temp = current;
                while (temp != nullptr) {
                    path.push_back(temp->coord);
                    temp = temp->parent;
                }
                std::reverse(path.begin(), path.end());
                return path;
            }

            float currentElev = getElevationAtXZ(current->coord.x, current->coord.z);

            auto testCandidate = [&](int dx, int dz, float moveDist) {
                int nx = current->coord.x + dx;
                int nz = current->coord.z + dz;

                // Check obstacle blockage first
                if (isTileBlocked(nx, nz)) {
                    return; // Cell column blocked by obstacle or occupied structure
                }

                // Test vertical elevation step
                int bestDy = 0;
                bool validStep = false;

                if (terrain) {
                    float neighborElev = getElevationAtXZ(nx, nz);
                    float elevDiff = std::abs(neighborElev - currentElev);
                    if (elevDiff <= static_cast<float>(maxElevationStep) * 1.5f + 0.5f) {
                        bestDy = static_cast<int>(std::round(neighborElev - currentElev));
                        validStep = true;
                    }
                } else if (grid) {
                    for (int dy = -maxElevationStep; dy <= maxElevationStep; ++dy) {
                        glm::ivec3 nCoord(nx, current->coord.y + dy, nz);
                        const auto* c = grid->getCell(nCoord);
                        if (c ? c->isWalkable() : (nCoord.y == 0)) {
                            bestDy = dy;
                            validStep = true;
                            break;
                        }
                    }
                } else {
                    validStep = true;
                }

                if (!validStep) return;

                glm::ivec3 nCoord(nx, current->coord.y + bestDy, nz);
                Node3D* neighbor = getNode(nCoord);
                if (neighbor->closed) return;

                float cost = moveDist + std::abs(bestDy) * 1.5f;
                float tentativeG = current->g + cost;

                if (!neighbor->open || tentativeG < neighbor->g) {
                    neighbor->g = tentativeG;
                    neighbor->h = getHeuristic3D(nCoord, end);
                    neighbor->parent = current;
                    neighbor->open = true;
                    openQueue.push({ neighbor->f(), neighbor->h, neighbor });
                }
            };

            for (const auto& d : dirs4) {
                testCandidate(d.x, d.y, 1.0f);
            }
            if (allowDiagonal) {
                for (const auto& d : dirsDiag) {
                    testCandidate(d.x, d.y, 1.414f);
                }
            }
        }

        return path;
    }
}

AStarSystem::AStarSystem(Registry& reg, VulkanRenderer& rend, EditorModeState& mode)
    : registry(reg), renderer(rend), editorMode(mode) {}

void AStarSystem::update(float dt) {
    // 1. Locate active navigation surfaces in the scene
    Engine::TilemapComponent* tilemap = nullptr;
    Transform* tilemapTransform = nullptr;
    for (auto [ent, tm, trans] : registry.view<Engine::TilemapComponent, Transform>()) {
        tilemap = &tm;
        tilemapTransform = &trans;
        break;
    }

    Engine::GridWorldComponent* gridWorld = nullptr;
    Transform* gridTransform = nullptr;
    for (auto [ent, gw] : registry.view<Engine::GridWorldComponent>()) {
        if (!gridWorld || !gw.chunks.empty()) {
            gridWorld = &gw;
            gridTransform = registry.get<Transform>(ent);
        }
    }

    Engine::TerrainComponent* terrain = nullptr;
    Transform* terrainTransform = nullptr;
    for (auto [ent, ter] : registry.view<Engine::TerrainComponent>()) {
        terrain = &ter;
        terrainTransform = registry.get<Transform>(ent);
        break;
    }

    if (!tilemap && !gridWorld && !terrain) return;

    glm::mat4 tilemapModel = tilemapTransform ? tilemapTransform->matrix() : glm::mat4(1.0f);
    glm::mat4 tilemapInv = tilemapTransform ? glm::inverse(tilemapModel) : glm::mat4(1.0f);

    // 2. Process all entities with an AStarAgent and Transform component
    for (auto [entity, agent, trans] : registry.view<AStarAgent, Transform>()) {
        if (agent.repathCooldown > 0.0f) {
            agent.repathCooldown -= dt;
        }

        // Resolve effective navigation domain
        AStarNavMode mode = agent.navMode;
        if (mode == AStarNavMode::Auto) {
            // Prioritize 3D navigation if Terrain or GridWorld is present
            if (terrain || gridWorld) {
                mode = AStarNavMode::GridWorld3D;
            } else if (tilemap) {
                mode = AStarNavMode::Tilemap2D;
            } else {
                continue;
            }
        }

        // -------------------------------------------------------------------
        // 2D Tilemap Navigation
        // -------------------------------------------------------------------
        if (mode == AStarNavMode::Tilemap2D && tilemap) {
            glm::ivec2 targetTile(-9999);
            if (registry.isValid(agent.targetTransform)) {
                if (const auto* targetTrans = registry.get<Transform>(agent.targetTransform)) {
                    glm::vec3 targetLocalPos = glm::vec3(tilemapInv * glm::vec4(targetTrans->position, 1.0f));
                    targetTile.x = static_cast<int>(std::floor(targetLocalPos.x / tilemap->tileSize));
                    targetTile.y = static_cast<int>(std::floor(targetLocalPos.y / tilemap->tileSize));
                }
            }

            if (targetTile == glm::ivec2(-9999)) {
                agent.path.clear();
                agent.lastTarget = glm::ivec2(-9999);
                continue;
            }

            glm::vec3 localPos = glm::vec3(tilemapInv * glm::vec4(trans.position, 1.0f));
            int startX = static_cast<int>(std::floor(localPos.x / tilemap->tileSize));
            int startY = static_cast<int>(std::floor(localPos.y / tilemap->tileSize));
            glm::ivec2 currentStart(startX, startY);

            int firstBlockedIdx = -1;
            for (int i = 0; i < static_cast<int>(agent.path.size()); ++i) {
                if (AStar::isTileBlocked(*tilemap, agent.path[i], { 1 })) {
                    firstBlockedIdx = i;
                    break;
                }
            }
            bool pathBlocked = (firstBlockedIdx != -1);
            bool targetMoved = (targetTile != agent.lastTarget);
            bool startMovedInEditor = (!editorMode.isPlaying && currentStart != agent.lastStart);
            bool needsInitialPath = (agent.path.empty() && currentStart != targetTile);

            if (startMovedInEditor || targetMoved) {
                agent.repathCooldown = 0.0f;
            }

            bool shouldRepath = (targetMoved || startMovedInEditor || pathBlocked || needsInitialPath);

            if (shouldRepath && agent.repathCooldown <= 0.0f) {
                std::vector<glm::ivec2> newPath = AStar::findPath(*tilemap, currentStart, targetTile, { 1 }, agent.allowDiagonal);

                if (!newPath.empty()) {
                    if (newPath.size() >= 2) {
                        glm::vec2 w0(newPath[0].x + 0.5f, newPath[0].y + 0.5f);
                        glm::vec2 w1(newPath[1].x + 0.5f, newPath[1].y + 0.5f);
                        glm::vec2 agentLocal(localPos.x / tilemap->tileSize, localPos.y / tilemap->tileSize);

                        glm::vec2 dir = glm::normalize(w1 - w0);
                        glm::vec2 agentVec = agentLocal - w0;

                        if (glm::dot(agentVec, dir) > 0.0f) {
                            newPath.erase(newPath.begin());
                        }
                    }
                    agent.path = std::move(newPath);
                    agent.repathCooldown = 0.05f;
                } else {
                    agent.path.clear();
                    agent.repathCooldown = 0.5f; // Wait before retrying failed search
                }

                agent.lastTarget = targetTile;
                agent.lastStart = currentStart;
            }

            if (editorMode.isPlaying) {
                while (!agent.path.empty()) {
                    glm::ivec2 nextTile = agent.path[0];
                    glm::vec3 localTarget((nextTile.x + 0.5f) * tilemap->tileSize, (nextTile.y + 0.5f) * tilemap->tileSize, localPos.z);
                    glm::vec3 worldTarget = glm::vec3(tilemapModel * glm::vec4(localTarget, 1.0f));

                    glm::vec3 toTarget = worldTarget - trans.position;
                    float dist = glm::length(toTarget);

                    if (dist < 0.10f) {
                        agent.path.erase(agent.path.begin());
                    } else {
                        float step = agent.speed * dt;
                        if (step >= dist) {
                            trans.position = worldTarget;
                            agent.path.erase(agent.path.begin());
                        } else {
                            trans.position += (toTarget / dist) * step;
                        }
                        break;
                    }
                }
            }
        }
        // -------------------------------------------------------------------
        // 3D GridWorld & Terrain Navigation
        // -------------------------------------------------------------------
        else if (mode == AStarNavMode::GridWorld3D && (gridWorld || terrain)) {
            float cellSize = (gridWorld && gridWorld->cellSize > 0.05f) ? gridWorld->cellSize : 1.0f;
            glm::vec3 gridOrigin = gridWorld ? gridWorld->originOffset : (terrainTransform ? terrainTransform->position : glm::vec3(0.0f));

            glm::ivec3 targetCell(-9999);
            if (registry.isValid(agent.targetTransform)) {
                if (const auto* targetTrans = registry.get<Transform>(agent.targetTransform)) {
                    if (gridWorld) {
                        targetCell = gridWorld->worldToCell(targetTrans->position);
                    } else {
                        targetCell.x = static_cast<int>(std::floor((targetTrans->position.x - gridOrigin.x) / cellSize));
                        targetCell.y = 0;
                        targetCell.z = static_cast<int>(std::floor((targetTrans->position.z - gridOrigin.z) / cellSize));
                    }
                }
            }

            if (targetCell == glm::ivec3(-9999)) {
                agent.path3D.clear();
                agent.lastTarget3D = glm::ivec3(-9999);
                continue;
            }

            glm::ivec3 currentStart(0);
            if (gridWorld) {
                currentStart = gridWorld->worldToCell(trans.position);
            } else {
                currentStart.x = static_cast<int>(std::floor((trans.position.x - gridOrigin.x) / cellSize));
                currentStart.y = 0;
                currentStart.z = static_cast<int>(std::floor((trans.position.z - gridOrigin.z) / cellSize));
            }

            auto isBlockedInScene = [&](int gx, int gz) -> bool {
                for (auto [ent, gw] : registry.view<Engine::GridWorldComponent>()) {
                    if (gw.isColumnBlocked(gx, gz) || !gw.isColumnWalkable(gx, gz)) {
                        return true;
                    }
                }
                return false;
            };

            int firstBlockedIdx = -1;
            for (int i = 0; i < static_cast<int>(agent.path3D.size()); ++i) {
                if (isBlockedInScene(agent.path3D[i].x, agent.path3D[i].z)) {
                    firstBlockedIdx = i;
                    break;
                }
            }

            bool pathBlocked = (firstBlockedIdx != -1);
            bool targetMoved = (targetCell != agent.lastTarget3D);
            bool startMovedInEditor = (!editorMode.isPlaying && currentStart != agent.lastStart3D);
            bool needsInitialPath = (agent.path3D.empty() && currentStart != targetCell);
            bool agentOffCourse = false;
            if (editorMode.isPlaying && !agent.path3D.empty()) {
                glm::vec3 waypoint0(
                    (static_cast<float>(agent.path3D[0].x) + 0.5f) * cellSize + gridOrigin.x,
                    trans.position.y,
                    (static_cast<float>(agent.path3D[0].z) + 0.5f) * cellSize + gridOrigin.z
                );
                if (glm::distance(glm::vec2(trans.position.x, trans.position.z), glm::vec2(waypoint0.x, waypoint0.z)) > cellSize * 3.5f) {
                    agentOffCourse = true;
                }
            }

            if (startMovedInEditor || targetMoved) {
                agent.repathCooldown = 0.0f;
            }

            bool shouldRepath = (targetMoved || startMovedInEditor || pathBlocked || needsInitialPath || agentOffCourse);

            if (shouldRepath && agent.repathCooldown <= 0.0f) {
                glm::mat4 terrainMatrix = terrainTransform ? terrainTransform->matrix() : glm::mat4(1.0f);
                std::vector<glm::ivec3> newPath;
                bool detourFound = false;

                if (pathBlocked && !targetMoved && firstBlockedIdx > 0) {
                    glm::ivec3 replStart = agent.path3D[firstBlockedIdx - 1];
                    std::vector<glm::ivec3> detourPath = AStar::findPath3D(
                        gridWorld, terrain, terrainMatrix, cellSize, gridOrigin,
                        replStart, targetCell, agent.allowDiagonal, agent.maxElevationStep, 1500,
                        isBlockedInScene
                    );
                    if (!detourPath.empty()) {
                        newPath = agent.path3D;
                        newPath.resize(firstBlockedIdx - 1);
                        newPath.insert(newPath.end(), detourPath.begin(), detourPath.end());
                        detourFound = true;
                    }
                }

                if (!detourFound) {
                    newPath = AStar::findPath3D(
                        gridWorld, terrain, terrainMatrix, cellSize, gridOrigin,
                        currentStart, targetCell, agent.allowDiagonal, agent.maxElevationStep, 1500,
                        isBlockedInScene
                    );
                }

                if (!newPath.empty()) {
                    if (newPath.size() >= 2) {
                        float w0x = (static_cast<float>(newPath[0].x) + 0.5f) * cellSize + gridOrigin.x;
                        float w0z = (static_cast<float>(newPath[0].z) + 0.5f) * cellSize + gridOrigin.z;
                        float w1x = (static_cast<float>(newPath[1].x) + 0.5f) * cellSize + gridOrigin.x;
                        float w1z = (static_cast<float>(newPath[1].z) + 0.5f) * cellSize + gridOrigin.z;

                        glm::vec2 dir = glm::normalize(glm::vec2(w1x - w0x, w1z - w0z));
                        glm::vec2 agentVec = glm::vec2(trans.position.x - w0x, trans.position.z - w0z);

                        if (glm::dot(agentVec, dir) > 0.0f) {
                            newPath.erase(newPath.begin());
                        }
                    }
                    agent.path3D = std::move(newPath);
                    agent.repathCooldown = 0.05f;
                } else {
                    agent.path3D.clear();
                    agent.repathCooldown = 0.5f; // Prevent 60 FPS busy loop when destination is unreachable
                }

                agent.lastTarget3D = targetCell;
                agent.lastStart3D = currentStart;
            }

            if (editorMode.isPlaying) {
                while (!agent.path3D.empty()) {
                    glm::ivec3 nextCell = agent.path3D[0];
                    glm::vec3 worldTarget(
                        (static_cast<float>(nextCell.x) + 0.5f) * cellSize + gridOrigin.x,
                        trans.position.y,
                        (static_cast<float>(nextCell.z) + 0.5f) * cellSize + gridOrigin.z
                    );

                    if (terrain && terrainTransform) {
                        glm::mat4 invM = glm::inverse(terrainTransform->matrix());
                        glm::vec3 localPos = glm::vec3(invM * glm::vec4(worldTarget.x, 0.0f, worldTarget.z, 1.0f));
                        worldTarget.y = terrain->getInterpolatedHeight(localPos.x, localPos.z) * terrainTransform->scale.y + terrainTransform->position.y;
                    }

                    glm::vec3 toTarget = worldTarget - trans.position;
                    float horizontalDist = glm::length(glm::vec2(toTarget.x, toTarget.z));

                    if (horizontalDist < 0.15f) {
                        agent.path3D.erase(agent.path3D.begin());
                    } else {
                        float step = agent.speed * dt;
                        glm::vec2 moveDir = glm::normalize(glm::vec2(toTarget.x, toTarget.z));

                        if (step >= horizontalDist) {
                            trans.position.x = worldTarget.x;
                            trans.position.z = worldTarget.z;
                            agent.path3D.erase(agent.path3D.begin());
                        } else {
                            trans.position.x += moveDir.x * step;
                            trans.position.z += moveDir.y * step;
                        }

                        if (terrain && terrainTransform) {
                            glm::mat4 invM = glm::inverse(terrainTransform->matrix());
                            glm::vec3 localPos = glm::vec3(invM * glm::vec4(trans.position.x, 0.0f, trans.position.z, 1.0f));
                            trans.position.y = terrain->getInterpolatedHeight(localPos.x, localPos.z) * terrainTransform->scale.y + terrainTransform->position.y;
                        } else {
                            trans.position.y = worldTarget.y;
                        }

                        if (moveDir.x * moveDir.x + moveDir.y * moveDir.y > 1e-4f) {
                            float targetYaw = std::atan2(moveDir.x, moveDir.y);
                            trans.rotation.y = glm::mix(trans.rotation.y, targetYaw, glm::clamp(agent.turnSpeed * dt, 0.0f, 1.0f));
                        }
                        break;
                    }
                }
            }
        }
    }
}

void AStarSystem::renderDebugUI() {
    Engine::TilemapComponent* tilemap = nullptr;
    Transform* tilemapTransform = nullptr;
    for (auto [ent, tm, trans] : registry.view<Engine::TilemapComponent, Transform>()) {
        tilemap = &tm;
        tilemapTransform = &trans;
        break;
    }

    Engine::GridWorldComponent* gridWorld = nullptr;
    Transform* gridTransform = nullptr;
    for (auto [ent, gw] : registry.view<Engine::GridWorldComponent>()) {
        if (!gridWorld || !gw.chunks.empty()) {
            gridWorld = &gw;
            gridTransform = registry.get<Transform>(ent);
        }
    }

    Engine::TerrainComponent* terrain = nullptr;
    Transform* terrainTransform = nullptr;
    for (auto [ent, ter] : registry.view<Engine::TerrainComponent>()) {
        terrain = &ter;
        terrainTransform = registry.get<Transform>(ent);
        break;
    }

    if (!tilemap && !gridWorld && !terrain) return;

    glm::mat4 tilemapModel = tilemapTransform ? tilemapTransform->matrix() : glm::mat4(1.0f);
    glm::mat4 tilemapInv = tilemapTransform ? glm::inverse(tilemapModel) : glm::mat4(1.0f);
    glm::mat4 viewProj = renderer.getActiveCameraViewProj();
    ImGuiIO& io = ImGui::GetIO();

    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool {
        glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
        if (clipPos.w < 0.0001f) return false;
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        screenPos.x = (ndc.x + 1.0f) * 0.5f * io.DisplaySize.x;
        screenPos.y = (ndc.y + 1.0f) * 0.5f * io.DisplaySize.y;
        return true;
    };

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImU32 pathColor = ImColor(0, 255, 127, 240);    // Spring green
    ImU32 targetColor = ImColor(255, 69, 0, 245);   // Orange red

    for (auto [entity, agent, trans] : registry.view<AStarAgent, Transform>()) {
        if (!agent.showDebugPath) continue;

        AStarNavMode mode = agent.navMode;
        if (mode == AStarNavMode::Auto) {
            if (terrain || gridWorld) {
                mode = AStarNavMode::GridWorld3D;
            } else if (tilemap) {
                mode = AStarNavMode::Tilemap2D;
            } else {
                continue;
            }
        }

        // Draw 2D Tilemap Debug Path
        if (mode == AStarNavMode::Tilemap2D && tilemap && !agent.path.empty()) {
            ImVec2 prevScreen;
            bool prevValid = false;

            for (const auto& point : agent.path) {
                glm::vec3 localPt((point.x + 0.5f) * tilemap->tileSize, (point.y + 0.5f) * tilemap->tileSize, 0.05f);
                glm::vec3 worldPt = glm::vec3(tilemapModel * glm::vec4(localPt, 1.0f));

                ImVec2 currScreen;
                if (projectToScreen(worldPt, currScreen)) {
                    if (prevValid) {
                        drawList->AddLine(prevScreen, currScreen, pathColor, 3.0f);
                    }
                    drawList->AddCircleFilled(currScreen, 4.0f, pathColor);
                    prevScreen = currScreen;
                    prevValid = true;
                } else {
                    prevValid = false;
                }
            }

            glm::ivec2 targetTile(-9999);
            if (registry.isValid(agent.targetTransform)) {
                if (const auto* targetTrans = registry.get<Transform>(agent.targetTransform)) {
                    glm::vec3 targetLocalPos = glm::vec3(tilemapInv * glm::vec4(targetTrans->position, 1.0f));
                    targetTile.x = static_cast<int>(std::floor(targetLocalPos.x / tilemap->tileSize));
                    targetTile.y = static_cast<int>(std::floor(targetLocalPos.y / tilemap->tileSize));
                }
            }

            if (targetTile != glm::ivec2(-9999)) {
                glm::vec3 targetLocal((targetTile.x + 0.5f) * tilemap->tileSize, (targetTile.y + 0.5f) * tilemap->tileSize, 0.06f);
                glm::vec3 targetWorld = glm::vec3(tilemapModel * glm::vec4(targetLocal, 1.0f));
                ImVec2 targetScreen;
                if (projectToScreen(targetWorld, targetScreen)) {
                    drawList->AddCircle(targetScreen, 8.0f, targetColor, 16, 2.5f);
                }
            }
        }
        // Draw 3D GridWorld / Terrain Debug Path
        else if (mode == AStarNavMode::GridWorld3D && (gridWorld || terrain) && !agent.path3D.empty()) {
            float cellSize = (gridWorld && gridWorld->cellSize > 0.05f) ? gridWorld->cellSize : 1.0f;
            glm::vec3 gridOrigin = gridWorld ? gridWorld->originOffset : (terrainTransform ? terrainTransform->position : glm::vec3(0.0f));

            ImVec2 prevScreen;
            bool prevValid = false;

            for (const auto& point : agent.path3D) {
                glm::vec3 worldPt(
                    (static_cast<float>(point.x) + 0.5f) * cellSize + gridOrigin.x,
                    0.0f,
                    (static_cast<float>(point.z) + 0.5f) * cellSize + gridOrigin.z
                );

                if (terrain && terrainTransform) {
                    glm::mat4 invM = glm::inverse(terrainTransform->matrix());
                    glm::vec3 localPos = glm::vec3(invM * glm::vec4(worldPt.x, 0.0f, worldPt.z, 1.0f));
                    worldPt.y = terrain->getInterpolatedHeight(localPos.x, localPos.z) * terrainTransform->scale.y + terrainTransform->position.y;
                }
                worldPt.y += 0.08f; // Bias above terrain surface

                ImVec2 currScreen;
                if (projectToScreen(worldPt, currScreen)) {
                    if (prevValid) {
                        drawList->AddLine(prevScreen, currScreen, pathColor, 3.0f);
                    }
                    drawList->AddCircleFilled(currScreen, 4.5f, pathColor);
                    prevScreen = currScreen;
                    prevValid = true;
                } else {
                    prevValid = false;
                }
            }

            if (registry.isValid(agent.targetTransform)) {
                if (const auto* targetTrans = registry.get<Transform>(agent.targetTransform)) {
                    ImVec2 targetScreen;
                    if (projectToScreen(targetTrans->position + glm::vec3(0.0f, 0.1f, 0.0f), targetScreen)) {
                        drawList->AddCircle(targetScreen, 9.0f, targetColor, 16, 2.5f);
                    }
                }
            }
        }
    }
}
