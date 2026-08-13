#include "AStarSystem.hpp"
#include "ecs/components/Tilemap.hpp"
#include "ecs/components/Transform.hpp"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <iostream>

namespace AStar {

    struct NodeInternal {
        int x;
        int y;
        float g;
        float h;
        float f() const { return g + h; }
        NodeInternal* parent = nullptr;
        bool closed = false;
        bool open = false;

        NodeInternal(int xVal, int yVal) : x(xVal), y(yVal), g(0.0f), h(0.0f) {}
    };

    struct CoordHash {
        size_t operator()(const glm::ivec2& p) const {
            return (static_cast<size_t>(p.x) * 73856093) ^ (static_cast<size_t>(p.y) * 19349663);
        }
    };

    float getHeuristic(int x1, int y1, int x2, int y2, bool allowDiagonal) {
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
        std::vector<glm::ivec2> path;

        auto isPassable = [&](int x, int y) {
            return !tilemap.isTileSolid(x, y);
        };

        if (!isPassable(end.x, end.y)) {
            return path;
        }

        std::vector<std::unique_ptr<NodeInternal>> nodePool;
        std::unordered_map<glm::ivec2, NodeInternal*, CoordHash> nodeMap;

        auto getNode = [&](int x, int y) -> NodeInternal* {
            glm::ivec2 coord(x, y);
            auto it = nodeMap.find(coord);
            if (it != nodeMap.end()) {
                return it->second;
            }
            auto newNode = std::make_unique<NodeInternal>(x, y);
            NodeInternal* ptr = newNode.get();
            nodePool.push_back(std::move(newNode));
            nodeMap[coord] = ptr;
            return ptr;
        };

        std::vector<NodeInternal*> openList;
        
        NodeInternal* startNode = getNode(start.x, start.y);
        startNode->g = 0.0f;
        startNode->h = getHeuristic(start.x, start.y, end.x, end.y, allowDiagonal);
        startNode->open = true;
        openList.push_back(startNode);

        int iterations = 0;
        const int maxIterations = 3000;

        while (!openList.empty() && iterations++ < maxIterations) {
            auto bestIt = openList.begin();
            for (auto it = openList.begin(); it != openList.end(); ++it) {
                if ((*it)->f() < (*bestIt)->f() || ((*it)->f() == (*bestIt)->f() && (*it)->h < (*bestIt)->h)) {
                    bestIt = it;
                }
            }

            NodeInternal* current = *bestIt;
            openList.erase(bestIt);
            current->open = false;
            current->closed = true;

            if (current->x == end.x && current->y == end.y) {
                NodeInternal* temp = current;
                while (temp != nullptr) {
                    path.push_back(glm::ivec2(temp->x, temp->y));
                    temp = temp->parent;
                }
                std::reverse(path.begin(), path.end());
                return path;
            }

            std::vector<glm::ivec2> neighbors;
            if (allowDiagonal) {
                neighbors = {
                    {0, 1}, {0, -1}, {1, 0}, {-1, 0},
                    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
                };
            } else {
                neighbors = {
                    {0, 1}, {0, -1}, {1, 0}, {-1, 0}
                };
            }

            for (const auto& offset : neighbors) {
                int nx = current->x + offset.x;
                int ny = current->y + offset.y;

                if (!isPassable(nx, ny)) continue;

                if (allowDiagonal && offset.x != 0 && offset.y != 0) {
                    if (!isPassable(current->x + offset.x, current->y) || !isPassable(current->x, current->y + offset.y)) {
                        continue;
                    }
                }

                NodeInternal* neighbor = getNode(nx, ny);
                if (neighbor->closed) continue;

                float moveCost = (offset.x != 0 && offset.y != 0) ? 1.414f : 1.0f;
                float tentativeG = current->g + moveCost;

                if (!neighbor->open || tentativeG < neighbor->g) {
                    neighbor->g = tentativeG;
                    neighbor->h = getHeuristic(nx, ny, end.x, end.y, allowDiagonal);
                    neighbor->parent = current;

                    if (!neighbor->open) {
                        neighbor->open = true;
                        openList.push_back(neighbor);
                    }
                }
            }
        }

        return path;
    }

    bool isTileBlocked(const Engine::TilemapComponent& tilemap, const glm::ivec2& point, const std::vector<int>& blockedTileIds) {
        return tilemap.isTileSolid(point.x, point.y);
    }
}

AStarSystem::AStarSystem(Registry& reg, VulkanRenderer& rend, EditorModeState& mode)
    : registry(reg), renderer(rend), editorMode(mode) {}

void AStarSystem::update(float dt) {
    // 1. Locate the first active tilemap in the scene
    Entity tilemapEntity = Entity();
    Engine::TilemapComponent* tilemap = nullptr;
    Transform* tilemapTransform = nullptr;

    for (auto [ent, tm, trans] : registry.view<Engine::TilemapComponent, Transform>()) {
        tilemapEntity = ent;
        tilemap = &tm;
        tilemapTransform = &trans;
        break;
    }

    if (!tilemap || !tilemapTransform) return;

    glm::mat4 tilemapModel = tilemapTransform->matrix();
    glm::mat4 tilemapInv = glm::inverse(tilemapModel);

    // 2. Process all entities with an AStarAgent and Transform component
    for (auto [entity, agent, trans] : registry.view<AStarAgent, Transform>()) {
        // Resolve target entity's position to local tile coordinates
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

        // Convert agent's current position to local tile coordinates
        glm::vec3 localPos = glm::vec3(tilemapInv * glm::vec4(trans.position, 1.0f));
        int startX = static_cast<int>(std::floor(localPos.x / tilemap->tileSize));
        int startY = static_cast<int>(std::floor(localPos.y / tilemap->tileSize));
        glm::ivec2 currentStart(startX, startY);

        // Check if any tile in the current path is blocked
        int firstBlockedIdx = -1;
        for (int i = 0; i < static_cast<int>(agent.path.size()); ++i) {
            if (AStar::isTileBlocked(*tilemap, agent.path[i], { 1 })) {
                firstBlockedIdx = i;
                break;
            }
        }
        bool pathBlocked = (firstBlockedIdx != -1);
        bool targetChanged = (targetTile != agent.lastTarget);

        // Recalculate path only if target changed or path is blocked
        if (targetChanged || pathBlocked) {
            std::vector<glm::ivec2> newPath;
            bool detourFound = false;
            if (pathBlocked && !targetChanged && firstBlockedIdx > 0) {
                // detouring: recalculate from the node just before the blocked one
                glm::ivec2 replStart = agent.path[firstBlockedIdx - 1];
                std::vector<glm::ivec2> detourPath = AStar::findPath(*tilemap, replStart, targetTile, { 1 }, agent.allowDiagonal);
                if (!detourPath.empty()) {
                    newPath = agent.path;
                    newPath.resize(firstBlockedIdx - 1);
                    newPath.insert(newPath.end(), detourPath.begin(), detourPath.end());
                    detourFound = true;
                }
            }

            if (!detourFound) {
                // fall back to full recalculation
                newPath = AStar::findPath(*tilemap, currentStart, targetTile, { 1 }, agent.allowDiagonal);
            }

            if (!newPath.empty()) {
                // Anti-jittering / anti-backtracking: Truncate waypoint 0 if agent has already passed waypoint 0's center towards waypoint 1
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
            } else {
                agent.path.clear();
            }

            agent.lastTarget = targetTile;
            agent.lastStart = currentStart;
        }

        // Move agent along the path if in play mode
        if (editorMode.isPlaying) {
            while (!agent.path.empty()) {
                glm::ivec2 nextTile = agent.path[0];
                glm::vec3 localTarget((nextTile.x + 0.5f) * tilemap->tileSize, (nextTile.y + 0.5f) * tilemap->tileSize, localPos.z);
                glm::vec3 worldTarget = glm::vec3(tilemapModel * glm::vec4(localTarget, 1.0f));

                glm::vec3 toTarget = worldTarget - trans.position;
                float dist = glm::length(toTarget);

                // If close enough to target waypoint, pop and look at next
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
                    break; // Done moving for this frame
                }
            }
        }
    }
}

void AStarSystem::renderDebugUI() {
    // 1. Locate the first active tilemap in the scene
    Engine::TilemapComponent* tilemap = nullptr;
    Transform* tilemapTransform = nullptr;

    for (auto [ent, tm, trans] : registry.view<Engine::TilemapComponent, Transform>()) {
        tilemap = &tm;
        tilemapTransform = &trans;
        break;
    }

    if (!tilemap || !tilemapTransform) return;

    glm::mat4 tilemapModel = tilemapTransform->matrix();
    glm::mat4 tilemapInv = glm::inverse(tilemapModel);
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

    // 2. Process all entities with an AStarAgent and Transform component
    for (auto [entity, agent, trans] : registry.view<AStarAgent, Transform>()) {
        if (agent.showDebugPath && !agent.path.empty()) {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            ImU32 pathColor = ImColor(0, 255, 127, 240); // Spring green
            ImU32 targetColor = ImColor(255, 69, 0, 245); // Orange red

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

            // Resolve target entity's position to local tile coordinates
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
    }
}
