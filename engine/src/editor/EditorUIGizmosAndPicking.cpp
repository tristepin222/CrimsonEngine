#include "editor/EditorUI.hpp"
#include "editor/EditorUIInternal.hpp"
#include "ecs/Registry.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Material.hpp"
#include "ecs/components/Mesh.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/Camera.hpp"
#include "ecs/components/Grid.hpp"
#include "ecs/components/Skeleton.hpp"
#include "ecs/components/Animator.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/EditorCamera.hpp"
#include "ecs/components/AnimationController.hpp"
#include "ecs/components/IKSolver.hpp"
#include "ecs/components/Collider.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Tilemap.hpp"
#include "renderer/VulkanRenderer.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include "ImGuizmo.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace ImGui;
using namespace std;

void EditorUI::drawGizmo()
{
    if (!hasSelection || editorMode.flyMode)
        return;

    Transform* transform = registry.get<Transform>(selectedEntity);
    if (!transform)
        return;

    ImGuiIO& io = ImGui::GetIO();

    ImGuizmo::BeginFrame();
    ImGuizmo::Enable(true);
    ImGuizmo::SetOrthographic(false);

    // draw directly to background drawlist
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());

    ImGuizmo::SetRect(
        0.0f,
        0.0f,
        io.DisplaySize.x,
        io.DisplaySize.y
    );

    glm::mat4 view = renderer.getActiveCameraView();
    glm::mat4 proj = renderer.getActiveCameraProjection();

    proj[1][1] *= -1.0f; // Vulkan

    glm::mat4 parentWorldMatrix = glm::mat4(1.0f);
    bool hasParent = false;
    if (auto* hierarchy = registry.get<HierarchyComponent>(selectedEntity)) {
        if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
            parentWorldMatrix = getEntityWorldMatrix(hierarchy->parent);
            hasParent = true;
        }
    }

    glm::mat4 worldMatrix = parentWorldMatrix * transform->matrix();

    // Safety guard: do not invoke ImGuizmo if worldMatrix contains NaNs/Infs
    bool hasNaN = false;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float val = worldMatrix[col][row];
            if (std::isnan(val) || std::isinf(val)) {
                hasNaN = true;
                break;
            }
        }
        if (hasNaN) break;
    }

    if (hasNaN) {
        // Reset transform components to clean states to recover from NaN
        transform->position = glm::vec3(0.0f);
        transform->rotation = glm::vec3(0.0f);
        transform->scale = glm::vec3(1.0f);
        worldMatrix = parentWorldMatrix * transform->matrix();
    }

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (gizmoOperation == 1) op = ImGuizmo::ROTATE;
    else if (gizmoOperation == 2) op = ImGuizmo::SCALE;

    ImGuizmo::MODE mode = (gizmoMode == 1) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    float snapValue[3] = { snapTranslation, snapTranslation, snapTranslation };
    if (op == ImGuizmo::ROTATE) {
        snapValue[0] = snapRotation;
        snapValue[1] = snapRotation;
        snapValue[2] = snapRotation;
    } else if (op == ImGuizmo::SCALE) {
        snapValue[0] = snapScale;
        snapValue[1] = snapScale;
        snapValue[2] = snapScale;
    }

    float* pSnap = (useSnap || io.KeyCtrl) ? snapValue : nullptr;

    ImGuizmo::Manipulate(
        &view[0][0],
        &proj[0][0],
        op,
        mode,
        &worldMatrix[0][0],
        nullptr,
        pSnap
    );

    if (ImGuizmo::IsUsing()) {
        bool worldNaN = false;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float val = worldMatrix[col][row];
                if (std::isnan(val) || std::isinf(val)) {
                    worldNaN = true;
                    break;
                }
            }
            if (worldNaN) break;
        }

        if (!worldNaN) {
            glm::mat4 newLocalMatrix = worldMatrix;
            if (hasParent) {
                float det = glm::determinant(parentWorldMatrix);
                if (std::abs(det) > 1e-5f) {
                    newLocalMatrix = glm::inverse(parentWorldMatrix) * worldMatrix;
                }
            }
            decomposeMatrixToTransform(newLocalMatrix, *transform);
        }

        if (auto* rb = registry.get<RigidBodyComponent>(selectedEntity)) {
            rb->velocity = glm::vec3(0.0f);
            rb->force = glm::vec3(0.0f);
        }
    }
}

void EditorUI::decomposeMatrixToTransform(const glm::mat4& mat, Transform& t)
{
    // Safety check BEFORE calling ImGuizmo decomposition to prevent infinite loops in ImGuizmo
    bool hasNaNOrInf = false;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float val = mat[col][row];
            if (std::isnan(val) || std::isinf(val)) {
                hasNaNOrInf = true;
                break;
            }
        }
        if (hasNaNOrInf) break;
    }

    if (hasNaNOrInf) {
        t.position = glm::vec3(0.0f);
        t.rotation = glm::vec3(0.0f);
        t.scale = glm::vec3(1.0f);
        return;
    }

    float matrixTranslation[3];
    float matrixRotation[3];
    float matrixScale[3];

    ImGuizmo::DecomposeMatrixToComponents(
        &mat[0][0],
        matrixTranslation,
        matrixRotation,
        matrixScale
    );

    t.position = glm::vec3(
        matrixTranslation[0],
        matrixTranslation[1],
        matrixTranslation[2]
    );

    // Keep rotation and scale unchanged to prevent decomposition drift/erratic rotation when using translation gizmo
    /*
    t.rotation = glm::vec3(
        matrixRotation[0],
        matrixRotation[1],
        matrixRotation[2]
    );

    t.scale = glm::vec3(
        matrixScale[0],
        matrixScale[1],
        matrixScale[2]
    );
    */

    // Safety guards to prevent NaN/Inf propagation to the C++ transform state
    if (std::isnan(t.position.x) || std::isinf(t.position.x) ||
        std::isnan(t.position.y) || std::isinf(t.position.y) ||
        std::isnan(t.position.z) || std::isinf(t.position.z)) {
        t.position = glm::vec3(0.0f);
    }
    if (std::isnan(t.rotation.x) || std::isinf(t.rotation.x) ||
        std::isnan(t.rotation.y) || std::isinf(t.rotation.y) ||
        std::isnan(t.rotation.z) || std::isinf(t.rotation.z)) {
        t.rotation = glm::vec3(0.0f);
    }
    if (std::isnan(t.scale.x) || std::isinf(t.scale.x) || t.scale.x < 1e-4f ||
        std::isnan(t.scale.y) || std::isinf(t.scale.y) || t.scale.y < 1e-4f ||
        std::isnan(t.scale.z) || std::isinf(t.scale.z) || t.scale.z < 1e-4f) {
        t.scale = glm::vec3(1.0f);
    }
}

void EditorUI::handleViewportPicking() {
    if (ImGuizmo::IsOver() || ImGuizmo::IsUsing())
        return;

    if (!window || editorMode.flyMode) {
        previousLeftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        return;
    }

    // Brush painting / erasing intercept — requires a selected TilemapComponent entity
    if (!hasSelection || !registry.isValid(selectedEntity) || !registry.has<Engine::TilemapComponent>(selectedEntity)) {
        s_brushModeActive = false;
        return;
    }

    if (s_brushModeActive || s_tilemapTool == TilemapTool::Eraser) {
        if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_R)) {
            s_brushRotation = (s_brushRotation + 1) % 4;
            statusMessage = "Tile rotation: " + std::to_string(s_brushRotation * 90) + " deg";
        }

        s_brushTilemapEntity = selectedEntity;
        Entity paintTarget = selectedEntity;

        if (registry.isValid(paintTarget)) {
            const bool leftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool rightMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            if ((leftMouseDown || rightMouseDown || s_boxDragging) && !ImGui::GetIO().WantCaptureMouse) {
                if (renderer.hasActiveCamera()) {
                    int width = 0, height = 0;
                    glfwGetWindowSize(window, &width, &height);
                    if (width > 0 && height > 0) {
                        double mouseX = 0.0, mouseY = 0.0;
                        glfwGetCursorPos(window, &mouseX, &mouseY);

                        const float normalizedX = static_cast<float>((2.0 * mouseX) / static_cast<double>(width) - 1.0);
                        const float normalizedY = static_cast<float>((2.0 * mouseY) / static_cast<double>(height) - 1.0); // Vulkan Correct Y

                        const glm::mat4 inverseViewProjection = glm::inverse(renderer.getActiveCameraViewProj());
                        const glm::vec4 nearClip = inverseViewProjection * glm::vec4(normalizedX, normalizedY, -1.0f, 1.0f);
                        const glm::vec4 farClip = inverseViewProjection * glm::vec4(normalizedX, normalizedY, 1.0f, 1.0f);

                        if (glm::abs(nearClip.w) >= 0.0001f && glm::abs(farClip.w) >= 0.0001f) {
                            const glm::vec3 nearPoint = glm::vec3(nearClip) / nearClip.w;
                            const glm::vec3 farPoint = glm::vec3(farClip) / farClip.w;
                            const glm::vec3 rayOrigin = nearPoint;
                            const glm::vec3 rayDirection = glm::normalize(farPoint - nearPoint);

                            auto* tm = registry.get<Engine::TilemapComponent>(paintTarget);
                            auto* transform = registry.get<Transform>(paintTarget);
                            if (tm && transform) {
                                if (!s_editingTilesetPath.empty() && tm->tilesetPath != s_editingTilesetPath) {
                                    tm->tilesetPath = s_editingTilesetPath;
                                    tm->isDirty = true;
                                }

                                if (tm->width <= 0 || tm->height <= 0 || tm->tileSize <= 0.0001f) {
                                    previousLeftMouseDown = leftMouseDown;
                                    return;
                                }

                                glm::mat4 modelMatrix = transform->matrix();
                                glm::mat4 invModel = glm::inverse(modelMatrix);

                                glm::vec4 localOrigin4 = invModel * glm::vec4(rayOrigin, 1.0f);
                                glm::vec3 localOrigin = glm::vec3(localOrigin4) / localOrigin4.w;
                                glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDirection, 0.0f)));

                                if (glm::abs(localDir.z) > 0.0001f) {
                                    float t = -localOrigin.z / localDir.z;
                                    if (t >= 0.0f) {
                                        glm::vec3 hitLocal = localOrigin + t * localDir;
                                        int cellX = static_cast<int>(std::floor(hitLocal.x / tm->tileSize));
                                        int cellY = static_cast<int>(std::floor(hitLocal.y / tm->tileSize));

                                        // Clamp painting layer to valid bounds
                                        if (s_tilemapActiveLayer < 0) s_tilemapActiveLayer = 0;
                                        if (s_tilemapActiveLayer >= (int)tm->layers.size()) {
                                            s_tilemapActiveLayer = (int)tm->layers.size() - 1;
                                        }

                                        if (s_tilemapTool == TilemapTool::Pencil || s_tilemapTool == TilemapTool::Eraser || rightMouseDown) {
                                            int newValue = (leftMouseDown && s_tilemapTool == TilemapTool::Pencil) ? s_brushTileId : -1;
                                            int currentVal = tm->getTile(s_tilemapActiveLayer, cellX, cellY);
                                            uint8_t currentRot = tm->getRotation(s_tilemapActiveLayer, cellX, cellY);
                                            if (currentVal != newValue || (newValue != -1 && currentRot != s_brushRotation)) {
                                                tm->setTile(s_tilemapActiveLayer, cellX, cellY, newValue, s_brushRotation);
                                            }
                                        } else if (s_tilemapTool == TilemapTool::BoxOutline || s_tilemapTool == TilemapTool::BoxFill) {
                                            if (leftMouseDown) {
                                                if (!s_boxDragging) {
                                                    s_boxDragging = true;
                                                    s_boxStartCol = cellX;
                                                    s_boxStartRow = cellY;
                                                }
                                                s_boxCurrentCol = cellX;
                                                s_boxCurrentRow = cellY;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (!leftMouseDown && s_boxDragging) {
                    s_boxDragging = false;
                    auto* tm = registry.get<Engine::TilemapComponent>(paintTarget);
                    if (tm && s_tilemapActiveLayer >= 0 && s_tilemapActiveLayer < static_cast<int>(tm->layers.size())) {
                        int minC = std::min(s_boxStartCol, s_boxCurrentCol);
                        int maxC = std::max(s_boxStartCol, s_boxCurrentCol);
                        int minR = std::min(s_boxStartRow, s_boxCurrentRow);
                        int maxR = std::max(s_boxStartRow, s_boxCurrentRow);

                        for (int r = minR; r <= maxR; ++r) {
                            for (int c = minC; c <= maxC; ++c) {
                                bool isBorder = (c == minC || c == maxC || r == minR || r == maxR);
                                if (s_tilemapTool == TilemapTool::BoxFill || isBorder) {
                                    tm->setTile(s_tilemapActiveLayer, c, r, s_brushTileId, s_brushRotation);
                                }
                            }
                        }
                    }
                }

                previousLeftMouseDown = leftMouseDown;
                return;
            } else if (!leftMouseDown && s_boxDragging) {
                s_boxDragging = false;
                auto* tm = registry.get<Engine::TilemapComponent>(paintTarget);
                if (tm && s_tilemapActiveLayer >= 0 && s_tilemapActiveLayer < static_cast<int>(tm->layers.size())) {
                    int minC = std::min(s_boxStartCol, s_boxCurrentCol);
                    int maxC = std::max(s_boxStartCol, s_boxCurrentCol);
                    int minR = std::min(s_boxStartRow, s_boxCurrentRow);
                    int maxR = std::max(s_boxStartRow, s_boxCurrentRow);

                    for (int r = minR; r <= maxR; ++r) {
                        for (int c = minC; c <= maxC; ++c) {
                            bool isBorder = (c == minC || c == maxC || r == minR || r == maxR);
                            if (s_tilemapTool == TilemapTool::BoxFill || isBorder) {
                                tm->setTile(s_tilemapActiveLayer, c, r, s_brushTileId, s_brushRotation);
                            }
                        }
                    }
                }
            }
        }
    }

    const bool leftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool clickStarted = leftMouseDown && !previousLeftMouseDown;
    previousLeftMouseDown = leftMouseDown;

    if (!clickStarted)
        return;

    if (GetIO().WantCaptureMouse)
        return;

    if (!renderer.hasActiveCamera()) {
        statusMessage = "No active camera available for picking.";
        lastPickResult = statusMessage;
        lastPickNearestEntityName = "None";
        lastPickNearestDistance = -1.0f;
        return;
    }

    int width = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        lastPickResult = "Viewport size is invalid for picking.";
        lastPickNearestEntityName = "None";
        lastPickNearestDistance = -1.0f;
        return;
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    const float normalizedX = static_cast<float>((2.0 * mouseX) / static_cast<double>(width) - 1.0);
    const float normalizedY = static_cast<float>((2.0 * mouseY) / static_cast<double>(height) - 1.0); // Vulkan Correct Y

    const glm::mat4 inverseViewProjection = glm::inverse(renderer.getActiveCameraViewProj());
    const glm::vec4 nearClip = inverseViewProjection * glm::vec4(normalizedX, normalizedY, -1.0f, 1.0f);
    const glm::vec4 farClip = inverseViewProjection * glm::vec4(normalizedX, normalizedY, 1.0f, 1.0f);
    if (glm::abs(nearClip.w) < 0.0001f || glm::abs(farClip.w) < 0.0001f) {
        statusMessage = "Viewport picking could not unproject the mouse ray.";
        lastPickResult = statusMessage;
        lastPickNearestEntityName = "None";
        lastPickNearestDistance = -1.0f;
        return;
    }

    const glm::vec3 nearPoint = glm::vec3(nearClip) / nearClip.w;
    const glm::vec3 farPoint = glm::vec3(farClip) / farClip.w;
    const glm::vec3 rayDirection = glm::normalize(farPoint - nearPoint);
    const glm::vec3 rayOrigin = nearPoint;
    lastPickRayOrigin = rayOrigin;
    lastPickRayDirection = rayDirection;

    Entity hitEntity{};
    Name* hitName = nullptr;
    float nearestHitDistance = std::numeric_limits<float>::max();
    lastPickNearestEntityName = "None";
    lastPickNearestDistance = -1.0f;

    for (auto [entity, name, transform, mesh] : registry.view<Name, Transform, Mesh>()) {
        if (mesh.vertices.empty() || registry.has<Grid>(entity)) {
            continue;
        }

        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());
        const glm::mat4 model = transform.matrix();

        for (const Vertex& vertex : mesh.vertices) {
            const glm::vec3 worldPosition = glm::vec3(model * glm::vec4(vertex.position, 1.0f));
            worldMin = glm::min(worldMin, worldPosition);
            worldMax = glm::max(worldMax, worldPosition);
        }

        // --- Bounding sphere from AABB ---
        glm::vec3 center = (worldMin + worldMax) * 0.5f;
        float radius = glm::length(worldMax - center);
        float distanceToCamera = glm::length(center - rayOrigin);
        radius += distanceToCamera * 0.06f;
        radius *= 1.6f;

        // --- Ray-sphere intersection ---
        glm::vec3 oc = rayOrigin - center;

        float a = glm::dot(rayDirection, rayDirection);
        float b = 2.0f * glm::dot(oc, rayDirection);
        float c = glm::dot(oc, oc) - radius * radius;

        float discriminant = b * b - 4.0f * a * c;

        if (discriminant < 0.0f) {
            continue;
        }

        // nearest intersection
        float sqrtD = sqrt(discriminant);
        float t0 = (-b - sqrtD) / (2.0f * a);
        float t1 = (-b + sqrtD) / (2.0f * a);

        // pick closest valid hit
        float hitDistance = (t0 > 0.0f) ? t0 : t1;

        if (hitDistance > 0.0f && hitDistance < nearestHitDistance) {
            nearestHitDistance = hitDistance;
            hitEntity = entity;
            hitName = &name;
            lastPickNearestEntityName = name.value;
            lastPickNearestDistance = hitDistance;
        }
    }

    if (hitName) {
        selectedEntity = hitEntity;
        hasSelection = true;
        renameBuffer = hitName->value;
        statusMessage = "Selected " + hitName->value + " from viewport.";
        lastPickResult = statusMessage;
        return;
    }

    hasSelection = false;
    selectedEntity = Entity();
    renameBuffer.clear();
    statusMessage = "Viewport selection cleared.";
    lastPickResult = statusMessage;
}

glm::mat4 EditorUI::getEntityWorldMatrix(Entity entity, int depth) {
    if (depth > 20) return glm::mat4(1.0f);
    
    auto* transform = registry.get<Transform>(entity);
    glm::mat4 localMat = transform ? transform->matrix() : glm::mat4(1.0f);

    if (auto* hierarchy = registry.get<HierarchyComponent>(entity)) {
        if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
            return getEntityWorldMatrix(hierarchy->parent, depth + 1) * localMat;
        }
    }
    return localMat;
}

void EditorUI::drawColliderDebugOverlay() {
    if (!showColliders) return;

    ImGuiIO& io = ImGui::GetIO();
    glm::mat4 viewProj = renderer.getActiveCameraViewProj();

    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool {
        glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
        if (clipPos.w < 0.0001f) return false;
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        screenPos.x = (ndc.x + 1.0f) * 0.5f * io.DisplaySize.x;
        screenPos.y = (ndc.y + 1.0f) * 0.5f * io.DisplaySize.y;
        return true;
    };

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    for (auto [entity, transform, col] : registry.view<Transform, ColliderComponent>()) {
        glm::mat4 worldM = getEntityWorldMatrix(entity);
        ImU32 color = (hasSelection && entity == selectedEntity) ? ImColor(0, 255, 0, 255) : ImColor(255, 120, 0, 200);

        if (col.shape == ColliderShape::Sphere) {
            const int segments = 24;
            float radius = col.radius;
            glm::vec3 center = glm::vec3(worldM * glm::vec4(col.offset, 1.0f));

            auto drawRing = [&](const glm::vec3& u, const glm::vec3& v) {
                ImVec2 prevScreen;
                bool prevValid = false;
                for (int step = 0; step <= segments; ++step) {
                    float angle = (float)step / (float)segments * 2.0f * 3.14159265f;
                    glm::vec3 offset = radius * (std::cos(angle) * u + std::sin(angle) * v);
                    ImVec2 currScreen;
                    if (projectToScreen(center + offset, currScreen)) {
                        if (prevValid) {
                            drawList->AddLine(prevScreen, currScreen, color, 1.5f);
                        }
                        prevScreen = currScreen;
                        prevValid = true;
                    } else {
                        prevValid = false;
                    }
                }
            };

            glm::vec3 axisX(1.0f, 0.0f, 0.0f);
            glm::vec3 axisY(0.0f, 1.0f, 0.0f);
            glm::vec3 axisZ(0.0f, 0.0f, 1.0f);

            drawRing(axisX, axisY);
            drawRing(axisX, axisZ);
            drawRing(axisY, axisZ);

        } else if (col.shape == ColliderShape::AABB) {
            glm::vec3 center = glm::vec3(worldM * glm::vec4(col.offset, 1.0f));
            glm::vec3 worldCorners[8] = {
                center + col.extents * glm::vec3(-1, -1, -1),
                center + col.extents * glm::vec3(1, -1, -1),
                center + col.extents * glm::vec3(1, 1, -1),
                center + col.extents * glm::vec3(-1, 1, -1),
                center + col.extents * glm::vec3(-1, -1, 1),
                center + col.extents * glm::vec3(1, -1, 1),
                center + col.extents * glm::vec3(1, 1, 1),
                center + col.extents * glm::vec3(-1, 1, 1)
            };

            ImVec2 screenCorners[8];
            bool valid[8];

            for (int k = 0; k < 8; ++k) {
                valid[k] = projectToScreen(worldCorners[k], screenCorners[k]);
            }

            auto drawEdge = [&](int i, int j) {
                if (valid[i] && valid[j]) {
                    drawList->AddLine(screenCorners[i], screenCorners[j], color, 1.5f);
                }
            };

            drawEdge(0, 1); drawEdge(1, 2); drawEdge(2, 3); drawEdge(3, 0);
            drawEdge(4, 5); drawEdge(5, 6); drawEdge(6, 7); drawEdge(7, 4);
            drawEdge(0, 4); drawEdge(1, 5); drawEdge(2, 6); drawEdge(3, 7);

        } else if (col.shape == ColliderShape::OBB) {
            glm::vec3 localCorners[8] = {
                col.offset + col.extents * glm::vec3(-1, -1, -1),
                col.offset + col.extents * glm::vec3(1, -1, -1),
                col.offset + col.extents * glm::vec3(1, 1, -1),
                col.offset + col.extents * glm::vec3(-1, 1, -1),
                col.offset + col.extents * glm::vec3(-1, -1, 1),
                col.offset + col.extents * glm::vec3(1, -1, 1),
                col.offset + col.extents * glm::vec3(1, 1, 1),
                col.offset + col.extents * glm::vec3(-1, 1, 1)
            };

            glm::vec3 worldCorners[8];
            ImVec2 screenCorners[8];
            bool valid[8];

            for (int k = 0; k < 8; ++k) {
                worldCorners[k] = glm::vec3(worldM * glm::vec4(localCorners[k], 1.0f));
                valid[k] = projectToScreen(worldCorners[k], screenCorners[k]);
            }

            auto drawEdge = [&](int i, int j) {
                if (valid[i] && valid[j]) {
                    drawList->AddLine(screenCorners[i], screenCorners[j], color, 1.5f);
                }
            };

            drawEdge(0, 1); drawEdge(1, 2); drawEdge(2, 3); drawEdge(3, 0);
            drawEdge(4, 5); drawEdge(5, 6); drawEdge(6, 7); drawEdge(7, 4);
            drawEdge(0, 4); drawEdge(1, 5); drawEdge(2, 6); drawEdge(3, 7);

        } else if (col.shape == ColliderShape::Capsule) {
            const int segments = 16;
            float radius = col.radius;
            float halfHeight = std::max(0.0f, (col.height - 2.0f * radius) * 0.5f);

            glm::vec3 bottomCenter = glm::vec3(worldM * glm::vec4(col.offset - glm::vec3(0.0f, halfHeight, 0.0f), 1.0f));
            glm::vec3 topCenter = glm::vec3(worldM * glm::vec4(col.offset + glm::vec3(0.0f, halfHeight, 0.0f), 1.0f));

            // Extract world-space axes from worldM to draw rings aligned with the entity's orientation
            glm::vec3 axisX = glm::normalize(glm::vec3(worldM[0]));
            glm::vec3 axisY = glm::normalize(glm::vec3(worldM[1]));
            glm::vec3 axisZ = glm::normalize(glm::vec3(worldM[2]));

            auto drawRing = [&](const glm::vec3& center, const glm::vec3& u, const glm::vec3& v) {
                ImVec2 prevScreen;
                bool prevValid = false;
                for (int step = 0; step <= segments; ++step) {
                    float angle = (float)step / (float)segments * 2.0f * 3.14159265f;
                    glm::vec3 offset = radius * (std::cos(angle) * u + std::sin(angle) * v);
                    ImVec2 currScreen;
                    if (projectToScreen(center + offset, currScreen)) {
                        if (prevValid) {
                            drawList->AddLine(prevScreen, currScreen, color, 1.5f);
                        }
                        prevScreen = currScreen;
                        prevValid = true;
                    } else {
                        prevValid = false;
                    }
                }
            };

            // Draw horizontal rings at the top and bottom hemispherical centers
            drawRing(bottomCenter, axisX, axisZ);
            drawRing(topCenter, axisX, axisZ);

            // Draw hemispherical dome wireframes (vertical arcs)
            auto drawDome = [&](const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, bool isTop) {
                ImVec2 prevScreen;
                bool prevValid = false;
                for (int step = 0; step <= segments / 2; ++step) {
                    float angle = (float)step / (float)(segments / 2) * 3.14159265f * 0.5f;
                    if (!isTop) angle = -angle;
                    glm::vec3 offset = radius * (std::cos(angle) * u + std::sin(angle) * v);
                    ImVec2 currScreen;
                    if (projectToScreen(center + offset, currScreen)) {
                        if (prevValid) {
                            drawList->AddLine(prevScreen, currScreen, color, 1.5f);
                        }
                        prevScreen = currScreen;
                        prevValid = true;
                    } else {
                        prevValid = false;
                    }
                }
            };

            // Draw vertical dome arcs (XY and ZY planes)
            drawDome(topCenter, axisX, axisY, true);
            drawDome(topCenter, axisZ, axisY, true);
            drawDome(bottomCenter, axisX, axisY, false);
            drawDome(bottomCenter, axisZ, axisY, false);

            // Connect top and bottom hemispherical rings with 4 vertical lines (along Y axis)
            auto drawLine = [&](const glm::vec3& localOffset) {
                glm::vec3 worldOffset = localOffset.x * axisX + localOffset.y * axisY + localOffset.z * axisZ;
                ImVec2 p1, p2;
                if (projectToScreen(bottomCenter + worldOffset, p1) && projectToScreen(topCenter + worldOffset, p2)) {
                    drawList->AddLine(p1, p2, color, 1.5f);
                }
            };

            drawLine(glm::vec3(radius, 0.0f, 0.0f));
            drawLine(glm::vec3(-radius, 0.0f, 0.0f));
            drawLine(glm::vec3(0.0f, 0.0f, radius));
            drawLine(glm::vec3(0.0f, 0.0f, -radius));
        }
    }
}

void EditorUI::drawPhysgunDebugOverlay() {
    // No-op (PhysgunScript is a user component, not built into engine core)
}

void EditorUI::drawTilemapGridOverlay() {
    if (!s_openTilesetEditorWindow && !s_brushModeActive) return;

    if (!hasSelection || !registry.isValid(selectedEntity) || !registry.has<Engine::TilemapComponent>(selectedEntity)) {
        return;
    }

    Entity targetEntity = selectedEntity;
    s_brushTilemapEntity = selectedEntity;

    auto* tm = registry.get<Engine::TilemapComponent>(targetEntity);
    auto* transform = registry.get<Transform>(targetEntity);
    if (!tm || !transform) return;

    ImGuiIO& io = ImGui::GetIO();
    glm::mat4 viewProj = renderer.getActiveCameraViewProj();

    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool {
        glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
        if (clipPos.w < 0.0001f) return false;
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        screenPos.x = (ndc.x + 1.0f) * 0.5f * io.DisplaySize.x;
        screenPos.y = (ndc.y + 1.0f) * 0.5f * io.DisplaySize.y;
        return true;
    };

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    glm::mat4 modelMatrix = transform->matrix();

    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    tm->getBounds(minX, minY, maxX, maxY);
    if (minX == 0 && minY == 0 && maxX == 0 && maxY == 0) {
        minX = -16; minY = -16; maxX = 16; maxY = 16;
    } else {
        minX = std::min(minX - 4, -16);
        minY = std::min(minY - 4, -16);
        maxX = std::max(maxX + 4, 16);
        maxY = std::max(maxY + 4, 16);
    }

    float minXWorld = minX * tm->tileSize;
    float maxXWorld = (maxX + 1) * tm->tileSize;
    float minYWorld = minY * tm->tileSize;
    float maxYWorld = (maxY + 1) * tm->tileSize;

    ImU32 lineCol = ImColor(0, 191, 255, 120);
    ImU32 borderCol = ImColor(255, 215, 0, 220);

    for (int c = minX; c <= maxX + 1; ++c) {
        float x = c * tm->tileSize;
        glm::vec3 localStart(x, minYWorld, 0.0f);
        glm::vec3 localEnd(x, maxYWorld, 0.0f);
        glm::vec3 worldStart(modelMatrix * glm::vec4(localStart, 1.0f));
        glm::vec3 worldEnd(modelMatrix * glm::vec4(localEnd, 1.0f));

        ImVec2 screenStart, screenEnd;
        if (projectToScreen(worldStart, screenStart) && projectToScreen(worldEnd, screenEnd)) {
            bool isBorder = (c == minX || c == maxX + 1);
            drawList->AddLine(screenStart, screenEnd, isBorder ? borderCol : lineCol, isBorder ? 2.5f : 1.0f);
        }
    }

    for (int r = minY; r <= maxY + 1; ++r) {
        float y = r * tm->tileSize;
        glm::vec3 localStart(minXWorld, y, 0.0f);
        glm::vec3 localEnd(maxXWorld, y, 0.0f);
        glm::vec3 worldStart(modelMatrix * glm::vec4(localStart, 1.0f));
        glm::vec3 worldEnd(modelMatrix * glm::vec4(localEnd, 1.0f));

        ImVec2 screenStart, screenEnd;
        if (projectToScreen(worldStart, screenStart) && projectToScreen(worldEnd, screenEnd)) {
            bool isBorder = (r == minY || r == maxY + 1);
            drawList->AddLine(screenStart, screenEnd, isBorder ? borderCol : lineCol, isBorder ? 2.5f : 1.0f);
        }
    }

    if (s_brushModeActive && s_brushTileId >= 0 && !io.WantCaptureMouse) {
        int w = 0, h = 0;
        glfwGetWindowSize(window, &w, &h);
        if (w > 0 && h > 0) {
            double mouseX = 0.0, mouseY = 0.0;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            const float normalizedX = static_cast<float>((2.0 * mouseX) / static_cast<double>(w) - 1.0);
            const float normalizedY = static_cast<float>((2.0 * mouseY) / static_cast<double>(h) - 1.0); // Vulkan Correct Y

            const glm::mat4 inverseViewProjection = glm::inverse(renderer.getActiveCameraViewProj());
            const glm::vec4 nearClip = inverseViewProjection * glm::vec4(normalizedX, normalizedY, -1.0f, 1.0f);
            const glm::vec4 farClip = inverseViewProjection * glm::vec4(normalizedX, normalizedY, 1.0f, 1.0f);

            if (glm::abs(nearClip.w) >= 0.0001f && glm::abs(farClip.w) >= 0.0001f) {
                const glm::vec3 nearPoint = glm::vec3(nearClip) / nearClip.w;
                const glm::vec3 farPoint = glm::vec3(farClip) / farClip.w;
                const glm::vec3 rayOrigin = nearPoint;
                const glm::vec3 rayDirection = glm::normalize(farPoint - nearPoint);

                glm::mat4 invModel = glm::inverse(modelMatrix);
                glm::vec4 localOrigin4 = invModel * glm::vec4(rayOrigin, 1.0f);
                glm::vec3 localOrigin = glm::vec3(localOrigin4) / localOrigin4.w;
                glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDirection, 0.0f)));

                if (glm::abs(localDir.z) > 0.0001f) {
                    float t = -localOrigin.z / localDir.z;
                    if (t >= 0.0f) {
                        glm::vec3 hitLocal = localOrigin + t * localDir;
                        int cellX = static_cast<int>(std::floor(hitLocal.x / tm->tileSize));
                        int cellY = static_cast<int>(std::floor(hitLocal.y / tm->tileSize));

                        float ts = tm->tileSize;
                        glm::vec3 corners[4] = {
                            glm::vec3(cellX * ts, cellY * ts, 0.001f),
                            glm::vec3((cellX + 1) * ts, cellY * ts, 0.001f),
                            glm::vec3((cellX + 1) * ts, (cellY + 1) * ts, 0.001f),
                            glm::vec3(cellX * ts, (cellY + 1) * ts, 0.001f)
                        };

                        ImVec2 sc[4];
                        bool allValid = true;
                        for (int i = 0; i < 4; ++i) {
                            allValid &= projectToScreen(glm::vec3(modelMatrix * glm::vec4(corners[i], 1.0f)), sc[i]);
                        }

                        if (allValid) {
                            drawList->AddQuad(sc[0], sc[1], sc[2], sc[3], ImColor(255, 255, 255, 255), 2.0f);
                            drawList->AddQuadFilled(sc[0], sc[1], sc[2], sc[3], ImColor(255, 255, 255, 40));
                        }
                    }
                }
            }
        }
    }

    if (s_boxDragging) {
        int minC = std::min(s_boxStartCol, s_boxCurrentCol);
        int maxC = std::max(s_boxStartCol, s_boxCurrentCol);
        int minR = std::min(s_boxStartRow, s_boxCurrentRow);
        int maxR = std::max(s_boxStartRow, s_boxCurrentRow);

        float ts = tm->tileSize;
        glm::vec3 boxCorners[4] = {
            glm::vec3(minC * ts, minR * ts, 0.002f),
            glm::vec3((maxC + 1) * ts, minR * ts, 0.002f),
            glm::vec3((maxC + 1) * ts, (maxR + 1) * ts, 0.002f),
            glm::vec3(minC * ts, (maxR + 1) * ts, 0.002f)
        };
        ImVec2 bSc[4];
        bool bValid = true;
        for (int i = 0; i < 4; ++i) {
            bValid &= projectToScreen(glm::vec3(modelMatrix * glm::vec4(boxCorners[i], 1.0f)), bSc[i]);
        }
        if (bValid) {
            drawList->AddQuad(bSc[0], bSc[1], bSc[2], bSc[3], ImColor(80, 200, 255, 255), 2.5f);
            drawList->AddQuadFilled(bSc[0], bSc[1], bSc[2], bSc[3], ImColor(80, 200, 255, 45));
        }
    }

    if (s_brushModeActive) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 270.f, 45.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(540.f, 44.f));
        if (ImGui::Begin("##TilemapToolOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            ImGui::TextDisabled("Tool:"); ImGui::SameLine();
            if (ImGui::RadioButton("Pencil", s_tilemapTool == TilemapTool::Pencil)) s_tilemapTool = TilemapTool::Pencil;
            ImGui::SameLine();
            if (ImGui::RadioButton("Outline", s_tilemapTool == TilemapTool::BoxOutline)) s_tilemapTool = TilemapTool::BoxOutline;
            ImGui::SameLine();
            if (ImGui::RadioButton("Fill", s_tilemapTool == TilemapTool::BoxFill)) s_tilemapTool = TilemapTool::BoxFill;
            ImGui::SameLine();
            if (ImGui::RadioButton("Eraser", s_tilemapTool == TilemapTool::Eraser)) s_tilemapTool = TilemapTool::Eraser;
            ImGui::SameLine(); ImGui::Text(" | "); ImGui::SameLine();
            std::string rotStr = "Rot: " + std::to_string(s_brushRotation * 90) + " (R) ";
            if (ImGui::Button(rotStr.c_str())) {
                s_brushRotation = (s_brushRotation + 1) % 4;
            }
            ImGui::End();
        }
    }
}

