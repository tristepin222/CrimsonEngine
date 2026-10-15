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
#include "ecs/components/LightComponent.hpp"
#include "ecs/components/TerrainComponent.hpp"
#include "ecs/components/GridWorldComponent.hpp"
#include "ecs/systems/TerrainSystem.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "renderer/ResourceManager.hpp"
#include "scenes/SceneManager.hpp"
#include "scenes/Scene.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include "ImGuizmo.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace ImGui;
using namespace std;

void EditorUI::drawGizmo()
{
    if (!hasSelection || editorMode.flyMode)
        return;

    if (auto* terrain = registry.get<Engine::TerrainComponent>(selectedEntity)) {
        if (terrain->isSculptingActive)
            return;
    }

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

    static bool s_wasUsingGizmo = false;
    if (ImGuizmo::IsUsing()) {
        s_wasUsingGizmo = true;
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
    } else if (s_wasUsingGizmo) {
        s_wasUsingGizmo = false;
        markSceneDirty();
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

    bool altDown = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
    if (!window || editorMode.flyMode || altDown) {
        previousLeftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        return;
    }

    // Intercept picking when sculpt mode is active on a TerrainComponent entity
    if (hasSelection && registry.isValid(selectedEntity) && registry.has<Engine::TerrainComponent>(selectedEntity)) {
        if (auto* terrain = registry.get<Engine::TerrainComponent>(selectedEntity)) {
            if (terrain->isSculptingActive) {
                previousLeftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                return;
            }
        }
    }

    // Brush painting / erasing intercept — requires a selected TilemapComponent entity
    if (!hasSelection || !registry.isValid(selectedEntity) || !registry.has<Engine::TilemapComponent>(selectedEntity)) {
        s_brushModeActive = false;
    } else if (s_brushModeActive || s_tilemapTool == TilemapTool::Eraser) {
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

    // Also test ray-sphere intersection with lights using an adaptive pick radius
    for (auto [entity, transform, light] : registry.view<Transform, Engine::LightComponent>()) {
        glm::mat4 worldM = getEntityWorldMatrix(entity);
        glm::vec3 center = glm::vec3(worldM * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        float distanceToCamera = glm::length(center - rayOrigin);
        float radius = std::max(0.6f, distanceToCamera * 0.045f);

        glm::vec3 oc = rayOrigin - center;
        float a = glm::dot(rayDirection, rayDirection);
        float b = 2.0f * glm::dot(oc, rayDirection);
        float c = glm::dot(oc, oc) - radius * radius;
        float discriminant = b * b - 4.0f * a * c;

        if (discriminant >= 0.0f) {
            float sqrtD = std::sqrt(discriminant);
            float t0 = (-b - sqrtD) / (2.0f * a);
            float t1 = (-b + sqrtD) / (2.0f * a);
            float hitDistance = (t0 > 0.0f) ? t0 : t1;

            if (hitDistance > 0.0f && hitDistance < nearestHitDistance) {
                nearestHitDistance = hitDistance;
                hitEntity = entity;
                hitName = registry.get<Name>(entity);
                lastPickNearestEntityName = hitName ? hitName->value : "Light";
                lastPickNearestDistance = hitDistance;
            }
        }
    }

    // Test ray-terrain intersection with all terrains in the scene
    for (auto [tEnt, terrain, transform] : registry.view<Engine::TerrainComponent, Transform>()) {
        glm::vec3 tHitWorld;
        glm::vec2 tHitLocal;
        if (Engine::TerrainSystem::raycast(rayOrigin, rayDirection, transform.matrix(), terrain, tHitWorld, tHitLocal)) {
            float hitDistance = glm::distance(rayOrigin, tHitWorld);
            if (hitDistance > 0.0f && hitDistance < nearestHitDistance) {
                nearestHitDistance = hitDistance;
                hitEntity = tEnt;
                hitName = registry.get<Name>(tEnt);
                lastPickNearestEntityName = hitName ? hitName->value : "Terrain";
                lastPickNearestDistance = hitDistance;
            }
        }
    }

    if (registry.isValid(hitEntity)) {
        selectedEntity = hitEntity;
        hasSelection = true;
        renameBuffer = hitName ? hitName->value : "Entity";
        statusMessage = "Selected " + renameBuffer + " from viewport.";
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

void EditorUI::drawLightGizmoOverlay() {
    if (!showLightGizmos) return;

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

    for (auto [entity, transform, light] : registry.view<Transform, Engine::LightComponent>()) {
        if (!registry.isComponentEnabled<Engine::LightComponent>(entity)) continue;

        glm::mat4 worldM = getEntityWorldMatrix(entity);
        glm::vec3 lightPos = glm::vec3(worldM * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        bool isSelected = (hasSelection && entity == selectedEntity);

        ImVec2 centerScreen;
        bool centerVisible = projectToScreen(lightPos, centerScreen);

        glm::vec3 c = glm::clamp(light.color, 0.0f, 1.0f);
        ImU32 iconFillCol = ImColor(c.r, c.g, c.b, 0.85f);
        ImU32 iconBorderCol = isSelected ? ImColor(255, 255, 255, 255) : ImColor(30, 30, 30, 220);
        ImU32 wireColor = isSelected ? ImColor(255, 220, 60, 240) : ImColor(220, 190, 50, 110);

        // 1. Draw Billboard Light Icon
        if (centerVisible) {
            float iconSize = isSelected ? 8.5f : 6.5f;

            if (light.type == Engine::LightType::Directional) {
                // Sun Glyph: Central circle with 8 radial rays
                drawList->AddCircleFilled(centerScreen, iconSize, iconFillCol, 16);
                drawList->AddCircle(centerScreen, iconSize, iconBorderCol, 16, 2.0f);
                for (int ray = 0; ray < 8; ++ray) {
                    float a = ray * (3.14159265f / 4.0f);
                    ImVec2 rayStart(centerScreen.x + std::cos(a) * (iconSize + 2.0f), centerScreen.y + std::sin(a) * (iconSize + 2.0f));
                    ImVec2 rayEnd(centerScreen.x + std::cos(a) * (iconSize + 6.0f), centerScreen.y + std::sin(a) * (iconSize + 6.0f));
                    drawList->AddLine(rayStart, rayEnd, iconBorderCol, 2.0f);
                    drawList->AddLine(rayStart, rayEnd, iconFillCol, 1.0f);
                }
            } else if (light.type == Engine::LightType::Spot) {
                // Spot Glyph: Flashlight diamond
                ImVec2 pTop(centerScreen.x, centerScreen.y - iconSize * 1.25f);
                ImVec2 pRight(centerScreen.x + iconSize, centerScreen.y);
                ImVec2 pBot(centerScreen.x, centerScreen.y + iconSize * 1.25f);
                ImVec2 pLeft(centerScreen.x - iconSize, centerScreen.y);
                drawList->AddQuadFilled(pTop, pRight, pBot, pLeft, iconFillCol);
                drawList->AddQuad(pTop, pRight, pBot, pLeft, iconBorderCol, 2.0f);
                drawList->AddCircleFilled(centerScreen, 2.5f, ImColor(255, 255, 255, 255));
            } else {
                // Point Light Glyph: Diamond with white core
                ImVec2 pTop(centerScreen.x, centerScreen.y - iconSize);
                ImVec2 pRight(centerScreen.x + iconSize, centerScreen.y);
                ImVec2 pBot(centerScreen.x, centerScreen.y + iconSize);
                ImVec2 pLeft(centerScreen.x - iconSize, centerScreen.y);
                drawList->AddQuadFilled(pTop, pRight, pBot, pLeft, iconFillCol);
                drawList->AddQuad(pTop, pRight, pBot, pLeft, iconBorderCol, 2.0f);
                drawList->AddCircleFilled(centerScreen, 2.5f, ImColor(255, 255, 255, 255));
            }

            if (isSelected) {
                std::string label = (light.type == Engine::LightType::Directional) ? "Sun Light" :
                                    (light.type == Engine::LightType::Spot) ? "Spot Light" : "Point Light";
                drawList->AddText(ImVec2(centerScreen.x + 14.0f, centerScreen.y - 7.0f), ImColor(255, 255, 255, 220), label.c_str());
            }
        }

        // 2. If this light is selected, draw detailed 3D wireframe gizmo
        if (!isSelected) continue;

        if (light.type == Engine::LightType::Directional) {
            glm::vec3 dir = (glm::length(light.direction) > 0.0001f) ? glm::normalize(light.direction) : glm::vec3(0.0f, -1.0f, 0.0f);
            glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::normalize(glm::cross(dir, up));
            up = glm::normalize(glm::cross(right, dir));

            const int diskSegs = 32;
            float diskRadius = 1.5f;
            ImVec2 prevScreen;
            bool prevValid = false;
            for (int s = 0; s <= diskSegs; ++s) {
                float a = (float)s / (float)diskSegs * 2.0f * 3.14159265f;
                glm::vec3 pt = lightPos + diskRadius * (std::cos(a) * right + std::sin(a) * up);
                ImVec2 currScreen;
                if (projectToScreen(pt, currScreen)) {
                    if (prevValid) drawList->AddLine(prevScreen, currScreen, wireColor, 2.0f);
                    prevScreen = currScreen;
                    prevValid = true;
                } else {
                    prevValid = false;
                }
            }

            auto drawArrow = [&](const glm::vec3& start, const glm::vec3& end, float headSize) {
                ImVec2 s0, s1;
                if (projectToScreen(start, s0) && projectToScreen(end, s1)) {
                    drawList->AddLine(s0, s1, wireColor, 2.0f);
                    glm::vec3 arrowDir = glm::normalize(end - start);
                    glm::vec3 side = right * headSize;
                    glm::vec3 headLeft = end - arrowDir * (headSize * 1.6f) + side;
                    glm::vec3 headRight = end - arrowDir * (headSize * 1.6f) - side;
                    ImVec2 hl, hr;
                    if (projectToScreen(headLeft, hl)) drawList->AddLine(s1, hl, wireColor, 2.0f);
                    if (projectToScreen(headRight, hr)) drawList->AddLine(s1, hr, wireColor, 2.0f);
                }
            };

            drawArrow(lightPos, lightPos + dir * 3.5f, 0.4f);
            float rOffsets[4] = { diskRadius * 0.8f, -diskRadius * 0.8f, 0.0f, 0.0f };
            float uOffsets[4] = { 0.0f, 0.0f, diskRadius * 0.8f, -diskRadius * 0.8f };
            for (int i = 0; i < 4; ++i) {
                glm::vec3 rStart = lightPos + right * rOffsets[i] + up * uOffsets[i];
                glm::vec3 rEnd = rStart + dir * 2.5f;
                drawArrow(rStart, rEnd, 0.25f);
            }

        } else if (light.type == Engine::LightType::Spot) {
            glm::vec3 dir = (glm::length(light.direction) > 0.0001f) ? glm::normalize(light.direction) : glm::vec3(0.0f, -1.0f, 0.0f);
            glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::normalize(glm::cross(dir, up));
            up = glm::normalize(glm::cross(right, dir));

            float range = std::max(light.range, 0.5f);
            float outerAngleRad = glm::radians(35.0f);
            float innerAngleRad = glm::radians(25.0f);

            float outerRadius = range * std::tan(outerAngleRad);
            float innerRadius = range * std::tan(innerAngleRad);
            glm::vec3 baseCenter = lightPos + dir * range;

            const int segments = 32;
            auto drawConeRing = [&](const glm::vec3& center, float radius, ImU32 col, float thickness) {
                ImVec2 prevScreen;
                bool prevValid = false;
                for (int s = 0; s <= segments; ++s) {
                    float a = (float)s / (float)segments * 2.0f * 3.14159265f;
                    glm::vec3 pt = center + radius * (std::cos(a) * right + std::sin(a) * up);
                    ImVec2 currScreen;
                    if (projectToScreen(pt, currScreen)) {
                        if (prevValid) drawList->AddLine(prevScreen, currScreen, col, thickness);
                        prevScreen = currScreen;
                        prevValid = true;
                    } else {
                        prevValid = false;
                    }
                }
            };

            drawConeRing(baseCenter, outerRadius, wireColor, 2.0f);
            drawConeRing(baseCenter, innerRadius, ImColor(255, 230, 80, 100), 1.2f);

            glm::vec3 cardinalPoints[4] = {
                baseCenter + right * outerRadius,
                baseCenter - right * outerRadius,
                baseCenter + up * outerRadius,
                baseCenter - up * outerRadius
            };
            for (int i = 0; i < 4; ++i) {
                ImVec2 s0, s1;
                if (projectToScreen(lightPos, s0) && projectToScreen(cardinalPoints[i], s1)) {
                    drawList->AddLine(s0, s1, wireColor, 2.0f);
                }
            }

            ImVec2 apexScreen, baseScreen;
            if (projectToScreen(lightPos, apexScreen) && projectToScreen(baseCenter, baseScreen)) {
                drawList->AddLine(apexScreen, baseScreen, ImColor(255, 255, 255, 120), 1.0f);
            }

        } else {
            float radius = std::max(light.range, 0.2f);
            const int segments = 32;

            auto drawCircleRing = [&](const glm::vec3& u, const glm::vec3& v, ImU32 col) {
                ImVec2 prevScreen;
                bool prevValid = false;
                for (int s = 0; s <= segments; ++s) {
                    float a = (float)s / (float)segments * 2.0f * 3.14159265f;
                    glm::vec3 pt = lightPos + radius * (std::cos(a) * u + std::sin(a) * v);
                    ImVec2 currScreen;
                    if (projectToScreen(pt, currScreen)) {
                        if (prevValid) drawList->AddLine(prevScreen, currScreen, col, 1.8f);
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

            drawCircleRing(axisX, axisZ, wireColor); // Horizontal (XZ)
            drawCircleRing(axisX, axisY, wireColor); // Vertical (XY)
            drawCircleRing(axisY, axisZ, wireColor); // Vertical (YZ)

            float crossLen = std::min(radius * 0.4f, 1.0f);
            auto drawCrossLine = [&](const glm::vec3& axis) {
                ImVec2 p0, p1;
                if (projectToScreen(lightPos - axis * crossLen, p0) && projectToScreen(lightPos + axis * crossLen, p1)) {
                    drawList->AddLine(p0, p1, ImColor(255, 255, 255, 160), 1.2f);
                }
            };
            drawCrossLine(axisX);
            drawCrossLine(axisY);
            drawCrossLine(axisZ);
        }
    }
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

void EditorUI::drawTerrainSculptOverlay() {
    if (!hasSelection || !registry.isValid(selectedEntity) || !registry.has<Engine::TerrainComponent>(selectedEntity)) {
        return;
    }

    auto* terrain = registry.get<Engine::TerrainComponent>(selectedEntity);
    auto* transform = registry.get<Transform>(selectedEntity);
    if (!terrain || !transform || !terrain->isSculptingActive) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    glm::mat4 viewProj = renderer.getActiveCameraViewProj();

    int w = 0, h = 0;
    glfwGetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) return;

    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool {
        glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
        if (clipPos.w < 0.0001f) return false;
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        screenPos.x = (ndc.x + 1.0f) * 0.5f * io.DisplaySize.x;
        screenPos.y = (ndc.y + 1.0f) * 0.5f * io.DisplaySize.y;
        return true;
    };

    double mouseX = 0.0, mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    float leftWidth = glm::clamp(static_cast<float>(w) * 0.20f, 260.0f, 400.0f);
    float rightWidth = glm::clamp(static_cast<float>(w) * 0.22f, 320.0f, 460.0f);
    float topY = 22.0f;
    float workHeight = static_cast<float>(h) - topY;
    float bottomHeight = workHeight * 0.32f;

    static bool s_isSculptingStroke = false;
    bool isMouseDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) || ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (!isMouseDown && s_isSculptingStroke) {
        s_isSculptingStroke = false;
        if (terrain) {
            terrain->commitStroke();
        }
        markSceneDirty();
    }

    bool inViewport = (mouseX >= leftWidth && mouseX <= (static_cast<float>(w) - rightWidth) &&
                       mouseY >= topY && mouseY <= (static_cast<float>(h) - bottomHeight));

    // Handle Undo (Ctrl+Z) / Redo (Ctrl+Y or Ctrl+Shift+Z)
    if (inViewport && !editorMode.flyMode && !io.WantTextInput && terrain) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                if (terrain->redo()) {
                    statusMessage = "Redo terrain action.";
                    markSceneDirty();
                }
            } else {
                if (terrain->undo()) {
                    statusMessage = "Undo terrain action.";
                    markSceneDirty();
                }
            }
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            if (terrain->redo()) {
                statusMessage = "Redo terrain action.";
                markSceneDirty();
            }
        }
    }

    bool overUI = io.WantCaptureMouse ||
                  ImGui::IsAnyItemActive() ||
                  ImGui::IsAnyItemHovered() ||
                  ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_RootAndChildWindows) ||
                  !inViewport;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!overUI && !editorMode.flyMode && inViewport) {
            s_isSculptingStroke = true;
            if (terrain) {
                Engine::TerrainComponent::TerrainUndoType uType = Engine::TerrainComponent::TerrainUndoType::Heights;
                const char* strokeName = "Sculpt";
                if (terrain->toolMode == Engine::TerrainToolMode::PaintTexture) {
                    uType = Engine::TerrainComponent::TerrainUndoType::Splatmap;
                    strokeName = "Paint Texture";
                } else if (terrain->toolMode == Engine::TerrainToolMode::PaintFoliage) {
                    uType = Engine::TerrainComponent::TerrainUndoType::Foliage;
                    strokeName = "Paint Foliage";
                }
                terrain->beginStroke(uType, strokeName);
            }
        } else {
            s_isSculptingStroke = false;
        }
    }

    if (overUI || editorMode.flyMode || !inViewport) {
        return;
    }

    const float normalizedX = static_cast<float>((2.0 * mouseX) / static_cast<double>(w) - 1.0);
    const float normalizedY = static_cast<float>((2.0 * mouseY) / static_cast<double>(h) - 1.0);

    const glm::mat4 inverseViewProjection = glm::inverse(viewProj);
    const glm::vec4 clipMid = inverseViewProjection * glm::vec4(normalizedX, normalizedY, 0.5f, 1.0f);
    if (std::abs(clipMid.w) < 1e-7f) {
        return;
    }

    const glm::vec3 ptMid = glm::vec3(clipMid) / clipMid.w;
    const glm::vec3 rayOrigin = renderer.getActiveCameraPosition();
    const glm::vec3 rayDirection = glm::normalize(ptMid - rayOrigin);

    Entity hitTerrainEntity{};
    glm::vec3 hitWorldPos(0.0f);
    glm::vec2 hitLocalXZ(0.0f);
    float closestDist = std::numeric_limits<float>::max();

    for (auto [tEnt, tComp, tTrans] : registry.view<Engine::TerrainComponent, Transform>()) {
        glm::vec3 tHitWorld;
        glm::vec2 tHitLocal;
        if (Engine::TerrainSystem::raycast(rayOrigin, rayDirection, tTrans.matrix(), tComp, tHitWorld, tHitLocal)) {
            float dist = glm::distance(rayOrigin, tHitWorld);
            if (dist < closestDist) {
                closestDist = dist;
                hitWorldPos = tHitWorld;
                hitLocalXZ = tHitLocal;
                hitTerrainEntity = tEnt;
            }
        }
    }

    if (!registry.isValid(hitTerrainEntity)) {
        return;
    }

    auto* hitTerrain = registry.get<Engine::TerrainComponent>(hitTerrainEntity);
    if (!hitTerrain) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // Determine brush radius & falloff based on active tool mode
    bool isPainting = (terrain->toolMode == Engine::TerrainToolMode::PaintTexture);
    bool isFoliage = (terrain->toolMode == Engine::TerrainToolMode::PaintFoliage);
    float currentBrushRadius = isPainting ? terrain->paintBrushRadius : (isFoliage ? terrain->foliageBrushRadius : terrain->brushRadius);

    // Brush indicator color based on mode
    bool invert = io.KeyShift;
    ImU32 brushColor = ImColor(0, 220, 255, 230); // Default Cyan for Raise

    if (isFoliage) {
        bool erase = terrain->foliageBrushErase || invert;
        brushColor = erase ? ImColor(255, 80, 50, 230) : ImColor(50, 240, 110, 230);
    } else if (isPainting) {
        int lIdx = std::clamp(terrain->activeLayerIndex, 0, 3);
        glm::vec4 tCol = terrain->layers[lIdx].tintColor;
        brushColor = ImColor(tCol.r, tCol.g, tCol.b, 0.95f);
    } else {
        Engine::TerrainBrushMode activeMode = terrain->brushMode;
        if (invert) {
            if (activeMode == Engine::TerrainBrushMode::Raise) activeMode = Engine::TerrainBrushMode::Lower;
            else if (activeMode == Engine::TerrainBrushMode::Lower) activeMode = Engine::TerrainBrushMode::Raise;
        }

        switch (activeMode) {
        case Engine::TerrainBrushMode::Raise:
            brushColor = ImColor(0, 220, 255, 230);
            break;
        case Engine::TerrainBrushMode::Lower:
            brushColor = ImColor(255, 90, 50, 230);
            break;
        case Engine::TerrainBrushMode::Smooth:
            brushColor = ImColor(160, 100, 255, 230);
            break;
        case Engine::TerrainBrushMode::Flatten:
            brushColor = ImColor(50, 240, 140, 230);
            break;
        case Engine::TerrainBrushMode::Noise:
            brushColor = ImColor(255, 120, 220, 230);
            break;
        }
    }

    // Sampling helper across all active terrain surfaces in world space
    auto sampleWorldTerrainHeight = [&](float wx, float wz) -> float {
        for (auto [tEnt, tComp, tTrans] : registry.view<Engine::TerrainComponent, Transform>()) {
            glm::mat4 invM = glm::inverse(tTrans.matrix());
            glm::vec3 lPos = glm::vec3(invM * glm::vec4(wx, 0.0f, wz, 1.0f));
            float halfX = tComp.sizeX * 0.5f;
            float halfZ = tComp.sizeZ * 0.5f;
            if (lPos.x >= -halfX && lPos.x <= halfX && lPos.z >= -halfZ && lPos.z <= halfZ) {
                float lY = tComp.getInterpolatedHeight(lPos.x, lPos.z);
                return (tTrans.matrix() * glm::vec4(lPos.x, lY, lPos.z, 1.0f)).y;
            }
        }
        return hitWorldPos.y;
    };

    // Render outer 3D circle projected onto the terrain surface
    const int segments = 36;
    ImVec2 screenPoints[36];
    bool validPoints[36];

    for (int i = 0; i < segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        float wx = hitWorldPos.x + std::cos(angle) * currentBrushRadius;
        float wz = hitWorldPos.z + std::sin(angle) * currentBrushRadius;
        float wy = sampleWorldTerrainHeight(wx, wz) + 0.05f;

        validPoints[i] = projectToScreen(glm::vec3(wx, wy, wz), screenPoints[i]);
    }

    for (int i = 0; i < segments; ++i) {
        int next = (i + 1) % segments;
        if (validPoints[i] && validPoints[next]) {
            drawList->AddLine(screenPoints[i], screenPoints[next], brushColor, 2.5f);
        }
    }

    // Draw inner falloff ring
    const float innerRadius = currentBrushRadius * 0.5f;
    ImVec2 innerScreenPoints[36];
    bool innerValidPoints[36];
    for (int i = 0; i < segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        float wx = hitWorldPos.x + std::cos(angle) * innerRadius;
        float wz = hitWorldPos.z + std::sin(angle) * innerRadius;
        float wy = sampleWorldTerrainHeight(wx, wz) + 0.05f;

        innerValidPoints[i] = projectToScreen(glm::vec3(wx, wy, wz), innerScreenPoints[i]);
    }

    ImU32 innerColor = (brushColor & 0x00FFFFFF) | (0x60 << 24);
    for (int i = 0; i < segments; ++i) {
        int next = (i + 1) % segments;
        if (innerValidPoints[i] && innerValidPoints[next]) {
            drawList->AddLine(innerScreenPoints[i], innerScreenPoints[next], innerColor, 1.2f);
        }
    }

    // Center dot indicator
    ImVec2 centerScreen;
    if (projectToScreen(hitWorldPos + glm::vec3(0, 0.08f, 0), centerScreen)) {
        drawList->AddCircleFilled(centerScreen, 4.0f, ImColor(255, 255, 255, 255));
        drawList->AddCircle(centerScreen, 4.0f, brushColor, 0, 1.5f);
    }

    // Handle interactive deformation or texture painting on mouse hold across ALL touched terrains
    if (s_isSculptingStroke && isMouseDown && !overUI && !editorMode.flyMode) {
        float dt = (io.DeltaTime > 0.0001f && io.DeltaTime < 0.1f) ? io.DeltaTime : 0.016f;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dt = std::max(dt, 0.08f);
        }

        if (isFoliage) {
            bool erase = terrain->foliageBrushErase || invert;
            float radius = terrain->foliageBrushRadius;
            float density = terrain->foliageBrushDensity;

            // Separate active slots into grass vs prop
            bool hasActiveGrass = false;
            std::vector<const Engine::DetailPrototype*> activeProps;
            for (const auto& proto : terrain->detailPalette) {
                if (proto.enabled) {
                    if (proto.type == Engine::DetailType::GrassClump) hasActiveGrass = true;
                    else if (proto.type == Engine::DetailType::PropEntity) activeProps.push_back(&proto);
                }
            }

            // 1. Paint / Erase GPU instanced grass
            if (hasActiveGrass || erase) {
                for (auto [tEnt, tComp, tTrans] : registry.view<Engine::TerrainComponent, Transform>()) {
                    glm::mat4 invMat = glm::inverse(tTrans.matrix());
                    glm::vec3 localCenter = glm::vec3(invMat * glm::vec4(hitWorldPos, 1.0f));
                    float halfX = tComp.sizeX * 0.5f;
                    float halfZ = tComp.sizeZ * 0.5f;

                    if (localCenter.x + radius >= -halfX && localCenter.x - radius <= halfX &&
                        localCenter.z + radius >= -halfZ && localCenter.z - radius <= halfZ) {
                        tComp.paintFoliage(localCenter.x, localCenter.z, radius, density, dt, erase, terrain->foliageBrushLayerFilter);
                    }
                }
            }

            // 2. Paint / Erase Prop Entities (Trees, Rocks, etc.)
            Scene* currentScene = sceneManager.getCurrentScene();
            if (currentScene) {
                // Find or create Foliage_Props folder
                Entity folFolder{};
                for (auto [fEnt, fName] : registry.view<Name>()) {
                    if (fName.value == "Foliage_Props") { folFolder = fEnt; break; }
                }

                if (erase) {
                    // Erase any prop entity within brush radius
                    if (registry.isValid(folFolder)) {
                        std::vector<Entity> toRemove;
                        for (auto [pEnt, hier, pTrans] : registry.view<HierarchyComponent, Transform>()) {
                            if (hier.parent == folFolder) {
                                float dist = glm::distance(glm::vec2(pTrans.position.x, pTrans.position.z), glm::vec2(hitWorldPos.x, hitWorldPos.z));
                                if (dist <= radius) {
                                    toRemove.push_back(pEnt);
                                }
                            }
                        }
                        for (Entity re : toRemove) {
                            currentScene->deleteEntity(re);
                        }
                    }
                } else if (!activeProps.empty()) {
                    if (!registry.isValid(folFolder)) {
                        folFolder = registry.create();
                        registry.emplace<Name>(folFolder, Name{ "Foliage_Props" });
                        registry.emplace<Transform>(folFolder, Transform{ glm::vec3(0.0f) });
                        currentScene->trackEntity(folFolder);
                    }

                    static float s_propSpawnAccum = 0.0f;
                    s_propSpawnAccum += dt * (density * 0.6f);
                    if (s_propSpawnAccum >= 1.0f) {
                        int spawnCount = static_cast<int>(s_propSpawnAccum);
                        s_propSpawnAccum -= static_cast<float>(spawnCount);
                        spawnCount = std::min(spawnCount, 3);

                        static uint32_t s_propRng = 839211u;
                        auto nextRand = []() -> float {
                            s_propRng = s_propRng * 1664525u + 1013904223u;
                            return static_cast<float>(s_propRng & 0x00FFFFFFu) / 16777215.0f;
                        };

                        for (int sc = 0; sc < spawnCount; ++sc) {
                            size_t pIdx = static_cast<size_t>(nextRand() * static_cast<float>(activeProps.size())) % activeProps.size();
                            const auto* proto = activeProps[pIdx];

                            float r = radius * std::sqrt(nextRand());
                            float theta = nextRand() * 6.2831853f;
                            glm::vec3 candWorldPos = hitWorldPos + glm::vec3(r * std::cos(theta), 0.0f, r * std::sin(theta));

                            // Find terrain height and surface normal
                            glm::vec3 terrainNorm(0.0f, 1.0f, 0.0f);
                            bool foundTerrain = false;

                            for (auto [tEnt, tComp, tTrans] : registry.view<Engine::TerrainComponent, Transform>()) {
                                glm::mat4 invM = glm::inverse(tTrans.matrix());
                                glm::vec3 lPos = glm::vec3(invM * glm::vec4(candWorldPos, 1.0f));
                                float halfX = tComp.sizeX * 0.5f;
                                float halfZ = tComp.sizeZ * 0.5f;
                                if (lPos.x >= -halfX && lPos.x <= halfX && lPos.z >= -halfZ && lPos.z <= halfZ) {
                                    // Layer filter check
                                    int propLayer = terrain->foliageBrushLayerFilter >= 0 ? terrain->foliageBrushLayerFilter : proto->targetLayer;
                                    if (propLayer >= 0 && propLayer <= 3 && !tComp.splatmapData.empty() && 
                                        tComp.splatmapData.size() == (static_cast<size_t>(tComp.splatmapResolution) * tComp.splatmapResolution * 4)) {
                                        float u = (lPos.x + halfX) / tComp.sizeX;
                                        float v = (lPos.z + halfZ) / tComp.sizeZ;
                                        int sx = std::clamp(static_cast<int>(u * static_cast<float>(tComp.splatmapResolution)), 0, static_cast<int>(tComp.splatmapResolution - 1));
                                        int sz = std::clamp(static_cast<int>(v * static_cast<float>(tComp.splatmapResolution)), 0, static_cast<int>(tComp.splatmapResolution - 1));
                                        size_t sIdx = (static_cast<size_t>(sz) * tComp.splatmapResolution + sx) * 4;
                                        float w = static_cast<float>(tComp.splatmapData[sIdx + propLayer]) / 255.0f;
                                        if (w < 0.15f) break;
                                    }

                                    float lY = tComp.getInterpolatedHeight(lPos.x, lPos.z);
                                    candWorldPos.y = (tTrans.matrix() * glm::vec4(lPos.x, lY, lPos.z, 1.0f)).y;
                                    glm::vec3 lNorm = tComp.getInterpolatedNormal(lPos.x, lPos.z);
                                    terrainNorm = glm::normalize(glm::mat3(tTrans.matrix()) * lNorm);
                                    foundTerrain = true;
                                    break;
                                }
                            }
                            if (!foundTerrain) continue;

                            // Check slope cutoff
                            float slopeFactor = 1.0f - std::max(0.0f, terrainNorm.y);
                            if (slopeFactor > proto->maxSlope) continue;

                            // Check spacing
                            float minSpacing = std::max(1.5f, 12.0f / std::max(0.1f, proto->density));
                            bool tooClose = false;
                            for (auto [exEnt, exHier, exTrans] : registry.view<HierarchyComponent, Transform>()) {
                                if (exHier.parent == folFolder) {
                                    if (glm::distance(glm::vec2(exTrans.position.x, exTrans.position.z), glm::vec2(candWorldPos.x, candWorldPos.z)) < minSpacing) {
                                        tooClose = true;
                                        break;
                                    }
                                }
                            }
                            if (tooClose) continue;

                            candWorldPos.y += proto->sinkOffset;

                            // Compute scale
                            float sVal = proto->scaleMin + nextRand() * (proto->scaleMax - proto->scaleMin);
                            glm::vec3 scaleVec(sVal);
                            if (!proto->uniformScale) {
                                scaleVec.x *= (0.85f + nextRand() * 0.30f);
                                scaleVec.z *= (0.85f + nextRand() * 0.30f);
                            }

                            // Compute rotation
                            float yawDeg = proto->randomYaw ? (nextRand() * 360.0f) : 0.0f;
                            glm::quat qYaw = glm::angleAxis(glm::radians(yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
                            glm::quat finalRot = qYaw;
                            if (proto->alignToNormal) {
                                glm::vec3 u = glm::vec3(0.0f, 1.0f, 0.0f);
                                glm::vec3 v = terrainNorm;
                                float cosTheta = glm::dot(u, v);
                                if (cosTheta < 0.9999f && cosTheta > -0.9999f) {
                                    glm::vec3 axis = glm::cross(u, v);
                                    float s = std::sqrt((1.0f + cosTheta) * 2.0f);
                                    float invs = 1.0f / s;
                                    glm::quat qAlign = glm::normalize(glm::quat(s * 0.5f, axis.x * invs, axis.y * invs, axis.z * invs));
                                    finalRot = qAlign * qYaw;
                                }
                            }

                            // Spawn entity
                            Entity newProp{};
                            if (!proto->prefabPath.empty()) {
                                newProp = currentScene->instantiatePrefab(proto->prefabPath, candWorldPos, folFolder);
                                if (registry.isValid(newProp)) {
                                    if (auto* tr = registry.get<Transform>(newProp)) {
                                        tr->rotation = finalRot;
                                        tr->scale = scaleVec;
                                    }
                                }
                            } else if (!proto->meshPath.empty()) {
                                newProp = registry.create();
                                std::string uName = currentScene->makeUniqueEntityName(proto->name);
                                registry.emplace<Name>(newProp, Name{ uName });
                                Transform propTransform{ candWorldPos, glm::vec3(0.0f), scaleVec };
                                propTransform.rotation = finalRot;
                                registry.emplace<Transform>(newProp, std::move(propTransform));
                                registry.emplace<HierarchyComponent>(newProp, HierarchyComponent{ folFolder });

                                try {
                                    Mesh loadedMesh = renderer.resourceManager->loadMesh(proto->meshPath, renderer);
                                    registry.emplace<Mesh>(newProp, std::move(loadedMesh));
                                    Material mat{ proto->tint };
                                    if (!proto->texturePath.empty()) {
                                        mat.texturePath = proto->texturePath;
                                        renderer.resourceManager->updateMaterialDescriptorSet(mat, renderer);
                                    }
                                    PipelineHandle pipeline = renderer.createPipelineForShaders(
                                        renderer.resolveShaderPath("build/shaders/lit.vert.spv"),
                                        renderer.resolveShaderPath("build/shaders/lit.frag.spv")
                                    );
                                    mat.pipeline = pipeline.pipeline;
                                    mat.pipelineLayout = pipeline.layout;
                                    registry.emplace<Material>(newProp, std::move(mat));
                                } catch (...) {}

                                // Collider
                                if (proto->collisionShape != Engine::DetailCollisionShape::None) {
                                    ColliderComponent col{};
                                    if (proto->collisionShape == Engine::DetailCollisionShape::Box) {
                                        col.shape = ColliderShape::OBB;
                                        col.extents = proto->colliderExtents * scaleVec;
                                    } else if (proto->collisionShape == Engine::DetailCollisionShape::Sphere) {
                                        col.shape = ColliderShape::Sphere;
                                        col.radius = proto->colliderExtents.x * scaleVec.x;
                                    } else if (proto->collisionShape == Engine::DetailCollisionShape::Capsule) {
                                        col.shape = ColliderShape::Capsule;
                                        col.radius = proto->colliderExtents.x * scaleVec.x;
                                        col.height = proto->colliderExtents.y * scaleVec.y;
                                    }
                                    registry.emplace<ColliderComponent>(newProp, std::move(col));
                                    if (proto->isStatic) {
                                        registry.emplace<RigidBodyComponent>(newProp, RigidBodyComponent{ RigidBodyType::Static });
                                    }
                                }
                                currentScene->trackEntity(newProp);
                            }
                        }
                    }
                }
            }
        } else if (isPainting) {
            int activeLayer = terrain->activeLayerIndex;
            float radius = terrain->paintBrushRadius;
            float opacity = terrain->paintBrushOpacity;
            auto falloff = terrain->paintBrushFalloff;

            for (auto [tEnt, tComp, tTrans] : registry.view<Engine::TerrainComponent, Transform>()) {
                glm::mat4 invMat = glm::inverse(tTrans.matrix());
                glm::vec3 localCenter = glm::vec3(invMat * glm::vec4(hitWorldPos, 1.0f));
                float halfX = tComp.sizeX * 0.5f;
                float halfZ = tComp.sizeZ * 0.5f;

                if (localCenter.x + radius >= -halfX && localCenter.x - radius <= halfX &&
                    localCenter.z + radius >= -halfZ && localCenter.z - radius <= halfZ) {
                    tComp.paintSplatmap(localCenter.x, localCenter.z, activeLayer, radius, opacity, dt, falloff);
                }
            }
        } else {
            float activeBrushRadius = terrain->brushRadius;
            float activeBrushStrength = terrain->brushStrength;
            auto activeBrushMode = terrain->brushMode;
            auto activeBrushFalloff = terrain->brushFalloff;
            float activeFlattenTarget = terrain->flattenTargetHeight;

            for (auto [tEnt, tComp, tTrans] : registry.view<Engine::TerrainComponent, Transform>()) {
                glm::mat4 invMat = glm::inverse(tTrans.matrix());
                glm::vec3 localCenter = glm::vec3(invMat * glm::vec4(hitWorldPos, 1.0f));
                float halfX = tComp.sizeX * 0.5f;
                float halfZ = tComp.sizeZ * 0.5f;

                // Check if brush circle overlaps terrain bounds
                if (localCenter.x + activeBrushRadius >= -halfX && localCenter.x - activeBrushRadius <= halfX &&
                    localCenter.z + activeBrushRadius >= -halfZ && localCenter.z - activeBrushRadius <= halfZ) {
                    tComp.brushRadius = activeBrushRadius;
                    tComp.brushStrength = activeBrushStrength;
                    tComp.brushMode = activeBrushMode;
                    tComp.brushFalloff = activeBrushFalloff;
                    tComp.flattenTargetHeight = activeFlattenTarget;

                    tComp.applyBrush(localCenter.x, localCenter.z, dt, invert);
                }
            }

            // Seamless border synchronization across all touched terrains
            Engine::TerrainSystem::synchronizeNeighborBorders(registry, hitTerrainEntity, false);
        }
    }
}

void EditorUI::drawTerrainChunkBordersOverlay() {
    int w = 0, h = 0;
    glfwGetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) return;

    float leftWidth = glm::clamp(static_cast<float>(w) * 0.20f, 260.0f, 400.0f);
    float rightWidth = glm::clamp(static_cast<float>(w) * 0.22f, 320.0f, 460.0f);
    float topY = 22.0f;
    float workHeight = static_cast<float>(h) - topY;
    float bottomHeight = workHeight * 0.32f;

    ImVec2 clipMin(leftWidth, topY);
    ImVec2 clipMax(static_cast<float>(w) - rightWidth, static_cast<float>(h) - bottomHeight);

    ImGuiIO& io = ImGui::GetIO();
    glm::mat4 viewProj = renderer.getActiveCameraViewProj();

    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool {
        glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
        if (clipPos.w < 0.05f) return false;
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
        screenPos.x = (ndc.x + 1.0f) * 0.5f * io.DisplaySize.x;
        screenPos.y = (ndc.y + 1.0f) * 0.5f * io.DisplaySize.y;
        return true;
    };

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    for (auto [entity, terrain, transform] : registry.view<Engine::TerrainComponent, Transform>()) {
        if (!terrain.showChunkBorders) continue;
        if (terrain.chunkCountX == 0 || terrain.chunkCountZ == 0) continue;

        drawList->PushClipRect(clipMin, clipMax, true);

        glm::mat4 worldMatrix = transform.matrix();
        float halfX = terrain.sizeX * 0.5f;
        float halfZ = terrain.sizeZ * 0.5f;
        float chunkSizeX = terrain.sizeX / static_cast<float>(terrain.chunkCountX);
        float chunkSizeZ = terrain.sizeZ / static_cast<float>(terrain.chunkCountZ);

        ImU32 innerCol = IM_COL32(0, 220, 255, 190);
        ImU32 outerCol = IM_COL32(255, 215, 0, 240);

        const int stepsPerChunk = 8;

        // Draw boundary lines along Z (constant X seams)
        for (uint32_t cx = 0; cx <= terrain.chunkCountX; ++cx) {
            float localX = -halfX + static_cast<float>(cx) * chunkSizeX;
            bool isOuter = (cx == 0 || cx == terrain.chunkCountX);
            ImU32 col = isOuter ? outerCol : innerCol;
            float thickness = isOuter ? 2.5f : 1.5f;

            for (uint32_t cz = 0; cz < terrain.chunkCountZ; ++cz) {
                float zStart = -halfZ + static_cast<float>(cz) * chunkSizeZ;
                float dz = chunkSizeZ / static_cast<float>(stepsPerChunk);

                for (int s = 0; s < stepsPerChunk; ++s) {
                    float z0 = zStart + static_cast<float>(s) * dz;
                    float z1 = zStart + static_cast<float>(s + 1) * dz;

                    float y0 = terrain.getInterpolatedHeight(localX, z0) + 0.15f;
                    float y1 = terrain.getInterpolatedHeight(localX, z1) + 0.15f;

                    glm::vec3 w0 = glm::vec3(worldMatrix * glm::vec4(localX, y0, z0, 1.0f));
                    glm::vec3 w1 = glm::vec3(worldMatrix * glm::vec4(localX, y1, z1, 1.0f));

                    ImVec2 s0, s1;
                    if (projectToScreen(w0, s0) && projectToScreen(w1, s1)) {
                        drawList->AddLine(s0, s1, col, thickness);
                    }
                }
            }
        }

        // Draw boundary lines along X (constant Z seams)
        for (uint32_t cz = 0; cz <= terrain.chunkCountZ; ++cz) {
            float localZ = -halfZ + static_cast<float>(cz) * chunkSizeZ;
            bool isOuter = (cz == 0 || cz == terrain.chunkCountZ);
            ImU32 col = isOuter ? outerCol : innerCol;
            float thickness = isOuter ? 2.5f : 1.5f;

            for (uint32_t cx = 0; cx < terrain.chunkCountX; ++cx) {
                float xStart = -halfX + static_cast<float>(cx) * chunkSizeX;
                float dx = chunkSizeX / static_cast<float>(stepsPerChunk);

                for (int s = 0; s < stepsPerChunk; ++s) {
                    float x0 = xStart + static_cast<float>(s) * dx;
                    float x1 = xStart + static_cast<float>(s + 1) * dx;

                    float y0 = terrain.getInterpolatedHeight(x0, localZ) + 0.15f;
                    float y1 = terrain.getInterpolatedHeight(x1, localZ) + 0.15f;

                    glm::vec3 w0 = glm::vec3(worldMatrix * glm::vec4(x0, y0, localZ, 1.0f));
                    glm::vec3 w1 = glm::vec3(worldMatrix * glm::vec4(x1, y1, localZ, 1.0f));

                    ImVec2 s0, s1;
                    if (projectToScreen(w0, s0) && projectToScreen(w1, s1)) {
                        drawList->AddLine(s0, s1, col, thickness);
                    }
                }
            }
        }

        // Draw chunk coordinate labels at center of each chunk
        for (uint32_t cz = 0; cz < terrain.chunkCountZ; ++cz) {
            for (uint32_t cx = 0; cx < terrain.chunkCountX; ++cx) {
                float centerLocalX = -halfX + (static_cast<float>(cx) + 0.5f) * chunkSizeX;
                float centerLocalZ = -halfZ + (static_cast<float>(cz) + 0.5f) * chunkSizeZ;
                float centerLocalY = terrain.getInterpolatedHeight(centerLocalX, centerLocalZ) + 0.5f;

                glm::vec3 wCenter = glm::vec3(worldMatrix * glm::vec4(centerLocalX, centerLocalY, centerLocalZ, 1.0f));

                ImVec2 sPos;
                if (projectToScreen(wCenter, sPos)) {
                    char label[32];
                    std::snprintf(label, sizeof(label), "[%u, %u]", cx, cz);
                    ImVec2 textSize = ImGui::CalcTextSize(label);

                    ImVec2 pMin(sPos.x - textSize.x * 0.5f - 4.0f, sPos.y - textSize.y * 0.5f - 2.0f);
                    ImVec2 pMax(sPos.x + textSize.x * 0.5f + 4.0f, sPos.y + textSize.y * 0.5f + 2.0f);

                    drawList->AddRectFilled(pMin, pMax, IM_COL32(15, 20, 30, 210), 3.0f);
                    drawList->AddRect(pMin, pMax, IM_COL32(0, 220, 255, 180), 3.0f, 0, 1.0f);
                    drawList->AddText(ImVec2(sPos.x - textSize.x * 0.5f, sPos.y - textSize.y * 0.5f), IM_COL32(230, 245, 255, 240), label);
                }
            }
        }

        drawList->PopClipRect();
    }
}

void EditorUI::drawGridWorldOverlay() {
    int w = 0, h = 0;
    glfwGetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) return;

    float leftWidth = glm::clamp(static_cast<float>(w) * 0.20f, 260.0f, 400.0f);
    float rightWidth = glm::clamp(static_cast<float>(w) * 0.22f, 320.0f, 460.0f);
    float topY = 22.0f;
    float workHeight = static_cast<float>(h) - topY;
    float bottomHeight = workHeight * 0.32f;

    ImVec2 clipMin(leftWidth, topY);
    ImVec2 clipMax(static_cast<float>(w) - rightWidth, static_cast<float>(h) - bottomHeight);

    double mouseX = 0.0, mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    bool inViewport = (mouseX >= leftWidth && mouseX <= (static_cast<float>(w) - rightWidth) &&
                       mouseY >= topY && mouseY <= (static_cast<float>(h) - bottomHeight));

    ImGuiIO& io = ImGui::GetIO();
    glm::mat4 viewProj = renderer.getActiveCameraViewProj();

    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool {
        glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
        if (clipPos.w < 0.05f) return false;
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
        screenPos.x = (ndc.x + 1.0f) * 0.5f * io.DisplaySize.x;
        screenPos.y = (ndc.y + 1.0f) * 0.5f * io.DisplaySize.y;
        return true;
    };

    // Pre-cache active terrain components and inverse matrices once per frame
    struct CachedTerrain {
        const Engine::TerrainComponent* comp = nullptr;
        glm::mat4 worldM{ 1.0f };
        glm::mat4 invM{ 1.0f };
        float halfX = 0.0f;
        float halfZ = 0.0f;
    };
    std::vector<CachedTerrain> cachedTerrains;
    for (auto [tEnt, tComp, tTrans] : registry.view<Engine::TerrainComponent, Transform>()) {
        CachedTerrain ct;
        ct.comp = &tComp;
        ct.worldM = tTrans.matrix();
        ct.invM = glm::inverse(ct.worldM);
        ct.halfX = tComp.sizeX * 0.5f;
        ct.halfZ = tComp.sizeZ * 0.5f;
        cachedTerrains.push_back(ct);
    }

    auto sampleWorldElevation = [&](float wx, float wz, float defY) -> float {
        for (const auto& ct : cachedTerrains) {
            glm::vec3 lPos = glm::vec3(ct.invM * glm::vec4(wx, 0.0f, wz, 1.0f));
            if (lPos.x >= -ct.halfX && lPos.x <= ct.halfX && lPos.z >= -ct.halfZ && lPos.z <= ct.halfZ) {
                float lY = ct.comp->getInterpolatedHeight(lPos.x, lPos.z);
                return (ct.worldM * glm::vec4(lPos.x, lY, lPos.z, 1.0f)).y;
            }
        }
        return defY;
    };

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // 1. Render Active Grids and Modified Farmland Cells
    for (auto [entity, grid, transform] : registry.view<Engine::GridWorldComponent, Transform>()) {
        bool isSelected = (hasSelection && entity == selectedEntity);
        if (!grid.showGridOverlay && !isSelected) continue;

        drawList->PushClipRect(clipMin, clipMax, true);

        glm::vec3 camPos = renderer.getActiveCameraPosition();
        glm::ivec3 centerCell = grid.worldToCell(camPos);
        float cs = grid.cellSize > 0.05f ? grid.cellSize : 1.0f;
        int radius = isSelected ? 20 : 14;

        ImU32 lineCol = isSelected ? IM_COL32(75, 230, 150, 160) :
            IM_COL32(static_cast<int>(grid.gridLineColor.r * 255.f),
                     static_cast<int>(grid.gridLineColor.g * 255.f),
                     static_cast<int>(grid.gridLineColor.b * 255.f),
                     static_cast<int>(grid.gridLineColor.a * 255.f));

        int diameter = radius * 2;
        int vertexDim = diameter + 1;
        std::vector<ImVec2> screenVertices(vertexDim * vertexDim);
        std::vector<uint8_t> validVertices(vertexDim * vertexDim, 0);

        for (int rz = 0; rz <= diameter; ++rz) {
            int z = (centerCell.z - radius) + rz;
            float wz = static_cast<float>(z) * cs + grid.originOffset.z;
            int rowOffset = rz * vertexDim;

            for (int rx = 0; rx <= diameter; ++rx) {
                int x = (centerCell.x - radius) + rx;
                float wx = static_cast<float>(x) * cs + grid.originOffset.x;
                float wy = sampleWorldElevation(wx, wz, grid.originOffset.y) + 0.04f;

                ImVec2 sPos;
                if (projectToScreen(glm::vec3(wx, wy, wz), sPos)) {
                    screenVertices[rowOffset + rx] = sPos;
                    validVertices[rowOffset + rx] = 1;
                }
            }
        }

        // Draw horizontal grid lines along X
        for (int rz = 0; rz <= diameter; ++rz) {
            int rowOffset = rz * vertexDim;
            for (int rx = 0; rx < diameter; ++rx) {
                int idx0 = rowOffset + rx;
                int idx1 = rowOffset + rx + 1;
                if (validVertices[idx0] && validVertices[idx1]) {
                    drawList->AddLine(screenVertices[idx0], screenVertices[idx1], lineCol, 1.2f);
                }
            }
        }

        // Draw vertical grid lines along Z
        for (int rx = 0; rx <= diameter; ++rx) {
            for (int rz = 0; rz < diameter; ++rz) {
                int idx0 = rz * vertexDim + rx;
                int idx1 = (rz + 1) * vertexDim + rx;
                if (validVertices[idx0] && validVertices[idx1]) {
                    drawList->AddLine(screenVertices[idx0], screenVertices[idx1], lineCol, 1.2f);
                }
            }
        }

        // Render Occupied Structure Footprint Tiles
        if (!grid.chunks.empty()) {
            for (const auto& [key, chunk] : grid.chunks) {
                if (!chunk.hasModifications) continue;

                int cx, cz;
                Engine::unpackGridChunkKey(key, cx, cz);

                int chunkOriginX = cx * Engine::GRID_CHUNK_WIDTH;
                int chunkOriginZ = cz * Engine::GRID_CHUNK_LENGTH;

                float chunkMidX = (static_cast<float>(chunkOriginX) + 8.0f) * cs + grid.originOffset.x;
                float chunkMidZ = (static_cast<float>(chunkOriginZ) + 8.0f) * cs + grid.originOffset.z;
                if (glm::distance(glm::vec2(camPos.x, camPos.z), glm::vec2(chunkMidX, chunkMidZ)) > 140.0f) {
                    continue;
                }

                for (int ly = 0; ly < Engine::GRID_CHUNK_HEIGHT; ++ly) {
                    for (int lz = 0; lz < Engine::GRID_CHUNK_LENGTH; ++lz) {
                        for (int lx = 0; lx < Engine::GRID_CHUNK_WIDTH; ++lx) {
                            const auto& cell = chunk.at(lx, ly, lz);
                            if (!cell.isOccupied() && !cell.isBlocked()) continue;

                            int gx = chunkOriginX + lx;
                            int gz = chunkOriginZ + lz;

                            float wx0 = static_cast<float>(gx) * cs + grid.originOffset.x;
                            float wx1 = static_cast<float>(gx + 1) * cs + grid.originOffset.x;
                            float wz0 = static_cast<float>(gz) * cs + grid.originOffset.z;
                            float wz1 = static_cast<float>(gz + 1) * cs + grid.originOffset.z;

                            float wy00 = sampleWorldElevation(wx0, wz0, grid.originOffset.y) + 0.06f;
                            float wy10 = sampleWorldElevation(wx1, wz0, grid.originOffset.y) + 0.06f;
                            float wy11 = sampleWorldElevation(wx1, wz1, grid.originOffset.y) + 0.06f;
                            float wy01 = sampleWorldElevation(wx0, wz1, grid.originOffset.y) + 0.06f;

                            ImVec2 p0, p1, p2, p3;
                            if (projectToScreen(glm::vec3(wx0, wy00, wz0), p0) &&
                                projectToScreen(glm::vec3(wx1, wy10, wz0), p1) &&
                                projectToScreen(glm::vec3(wx1, wy11, wz1), p2) &&
                                projectToScreen(glm::vec3(wx0, wy01, wz1), p3)) {

                                if (cell.isBlocked()) {
                                    // Red / dark-red obstacle tile
                                    drawList->AddQuadFilled(p0, p1, p2, p3, IM_COL32(220, 50, 50, 95));
                                    drawList->AddQuad(p0, p1, p2, p3, IM_COL32(240, 70, 70, 230), 2.0f);
                                } else if (cell.isOccupied()) {
                                    // Blue foundation tile for occupied grid cells
                                    drawList->AddQuadFilled(p0, p1, p2, p3, IM_COL32(50, 120, 210, 85));
                                    drawList->AddQuad(p0, p1, p2, p3, IM_COL32(70, 160, 240, 220), 2.0f);
                                }
                            }
                        }
                    }
                }
            }
        }

        drawList->PopClipRect();
    }

    // 2. Interactive Building & Object Placement on Grid
    auto* selGrid = (hasSelection && registry.isValid(selectedEntity)) ? registry.get<Engine::GridWorldComponent>(selectedEntity) : nullptr;
    if (selGrid) {
        // Floating Placement Toolbar in Viewport
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 240.f, 45.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(480.f, 44.f));
        if (ImGui::Begin("##GridPlacementOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            ImGui::TextDisabled("Grid Build:"); ImGui::SameLine();
            if (ImGui::RadioButton("Place (1)", gridPlacementTool == GridPlacementTool::Place)) gridPlacementTool = GridPlacementTool::Place;
            ImGui::SameLine();
            if (ImGui::RadioButton("Block (2)", gridPlacementTool == GridPlacementTool::Block)) gridPlacementTool = GridPlacementTool::Block;
            ImGui::SameLine();
            if (ImGui::RadioButton("Clear (3)", gridPlacementTool == GridPlacementTool::Clear)) gridPlacementTool = GridPlacementTool::Clear;
            ImGui::SameLine(); ImGui::Text("| Footprint:"); ImGui::SameLine();
            const char* fpSizes[] = { "1x1", "2x2", "3x3", "4x4" };
            int currentFp = (gridFootprintSize.x <= 1) ? 0 : ((gridFootprintSize.x == 2) ? 1 : ((gridFootprintSize.x == 3) ? 2 : 3));
            if (ImGui::Button(fpSizes[currentFp])) {
                int nextSize = (currentFp + 1) % 4 + 1;
                gridFootprintSize = glm::ivec2(nextSize, nextSize);
            }
            ImGui::End();
        }

        // Shortcut keys 1, 2, and 3
        if (inViewport && !io.WantTextInput && !editorMode.flyMode) {
            if (ImGui::IsKeyPressed(ImGuiKey_1, false)) gridPlacementTool = GridPlacementTool::Place;
            if (ImGui::IsKeyPressed(ImGuiKey_2, false)) gridPlacementTool = GridPlacementTool::Block;
            if (ImGui::IsKeyPressed(ImGuiKey_3, false)) gridPlacementTool = GridPlacementTool::Clear;
        }

        // Viewport raycasting to find hovered cell on terrain or ground
        bool overUI = io.WantCaptureMouse ||
                      ImGui::IsAnyItemActive() ||
                      ImGui::IsAnyItemHovered() ||
                      ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_RootAndChildWindows) ||
                      !inViewport;

        if (!overUI && !editorMode.flyMode && inViewport) {
            const float normalizedX = static_cast<float>((2.0 * mouseX) / static_cast<double>(w) - 1.0);
            const float normalizedY = static_cast<float>((2.0 * mouseY) / static_cast<double>(h) - 1.0);

            const glm::mat4 inverseVP = glm::inverse(viewProj);
            const glm::vec4 clipMid = inverseVP * glm::vec4(normalizedX, normalizedY, 0.5f, 1.0f);
            if (std::abs(clipMid.w) > 1e-7f) {
                const glm::vec3 ptMid = glm::vec3(clipMid) / clipMid.w;
                const glm::vec3 rayOrigin = renderer.getActiveCameraPosition();
                const glm::vec3 rayDir = glm::normalize(ptMid - rayOrigin);

                // Raycast against cached terrains first
                Entity hitTerrainEntity{};
                glm::vec3 hitWorldPos(0.0f);
                float closestDist = std::numeric_limits<float>::max();

                for (const auto& ct : cachedTerrains) {
                    glm::vec3 tHitWorld;
                    glm::vec2 tHitLocal;
                    if (Engine::TerrainSystem::raycast(rayOrigin, rayDir, ct.worldM, *ct.comp, tHitWorld, tHitLocal)) {
                        float dist = glm::distance(rayOrigin, tHitWorld);
                        if (dist < closestDist) {
                            closestDist = dist;
                            hitWorldPos = tHitWorld;
                            hitTerrainEntity = selectedEntity;
                        }
                    }
                }

                // If no terrain hit, test ground plane Y = originOffset.y
                if (!registry.isValid(hitTerrainEntity)) {
                    if (std::abs(rayDir.y) > 1e-5f) {
                        float t = (selGrid->originOffset.y - rayOrigin.y) / rayDir.y;
                        if (t > 0.0f) {
                            hitWorldPos = rayOrigin + t * rayDir;
                            hitTerrainEntity = selectedEntity;
                        }
                    }
                }

                if (registry.isValid(hitTerrainEntity)) {
                    glm::ivec3 targetCell = selGrid->worldToCell(hitWorldPos);
                    float cs = selGrid->cellSize > 0.05f ? selGrid->cellSize : 1.0f;

                    drawList->PushClipRect(clipMin, clipMax, true);

                    // Check if entire footprint is buildable
                    bool canBuild = selGrid->isAreaBuildable(glm::ivec2(targetCell.x, targetCell.z), targetCell.y, gridFootprintSize);

                    ImU32 outlineColor = IM_COL32(255, 130, 40, 240);
                    if (gridPlacementTool == GridPlacementTool::Place) {
                        outlineColor = canBuild ? IM_COL32(60, 235, 110, 240) : IM_COL32(245, 55, 55, 240);
                    } else if (gridPlacementTool == GridPlacementTool::Block) {
                        outlineColor = IM_COL32(235, 45, 45, 240);
                    }
                    ImU32 fillColor = (outlineColor & 0x00FFFFFF) | (0x45 << 24);

                    for (int dx = 0; dx < gridFootprintSize.x; ++dx) {
                        for (int dz = 0; dz < gridFootprintSize.y; ++dz) {
                            int gx = targetCell.x + dx;
                            int gz = targetCell.z + dz;

                            float wx0 = static_cast<float>(gx) * cs + selGrid->originOffset.x;
                            float wx1 = static_cast<float>(gx + 1) * cs + selGrid->originOffset.x;
                            float wz0 = static_cast<float>(gz) * cs + selGrid->originOffset.z;
                            float wz1 = static_cast<float>(gz + 1) * cs + selGrid->originOffset.z;

                            float wy00 = sampleWorldElevation(wx0, wz0, selGrid->originOffset.y) + 0.08f;
                            float wy10 = sampleWorldElevation(wx1, wz0, selGrid->originOffset.y) + 0.08f;
                            float wy11 = sampleWorldElevation(wx1, wz1, selGrid->originOffset.y) + 0.08f;
                            float wy01 = sampleWorldElevation(wx0, wz1, selGrid->originOffset.y) + 0.08f;

                            ImVec2 p0, p1, p2, p3;
                            if (projectToScreen(glm::vec3(wx0, wy00, wz0), p0) &&
                                projectToScreen(glm::vec3(wx1, wy10, wz0), p1) &&
                                projectToScreen(glm::vec3(wx1, wy11, wz1), p2) &&
                                projectToScreen(glm::vec3(wx0, wy01, wz1), p3)) {

                                drawList->AddQuadFilled(p0, p1, p2, p3, fillColor);
                                drawList->AddQuad(p0, p1, p2, p3, outlineColor, 2.5f);
                            }
                        }
                    }

                    drawList->PopClipRect();

                    // Handle Mouse Click / Drag to place, block, or clear structure footprint
                    bool isMouseDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) || ImGui::IsMouseDown(ImGuiMouseButton_Left);
                    if (isMouseDown) {
                        if (gridPlacementTool == GridPlacementTool::Place) {
                            selGrid->setAreaOccupied(glm::ivec2(targetCell.x, targetCell.z), targetCell.y, gridFootprintSize, selectedEntity.getId(), true);
                        } else if (gridPlacementTool == GridPlacementTool::Block) {
                            selGrid->setAreaBlocked(glm::ivec2(targetCell.x, targetCell.z), targetCell.y, gridFootprintSize, true);
                        } else if (gridPlacementTool == GridPlacementTool::Clear) {
                            for (int dx = 0; dx < gridFootprintSize.x; ++dx) {
                                for (int dz = 0; dz < gridFootprintSize.y; ++dz) {
                                    selGrid->clearColumn(targetCell.x + dx, targetCell.z + dz);
                                }
                            }
                        }
                        markSceneDirty();
                    }
                }
            }
        }
    }
}



