#include "editor/EditorUI.hpp"
#include "editor/EditorUIInternal.hpp"
#include <glm/gtc/type_ptr.hpp>
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "editor/NodeGraphFramework.hpp"
#include "editor/EntityArchetypeRegistry.hpp"
#include "meta/ComponentReflection.hpp"
#include "scenes/JSONUtils.hpp"
#include "editor/AssetBrowserRegistry.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "scenes/Scene.hpp"
#include "scenes/SceneManager.hpp"
#include "scenes/SceneSerializer.hpp"
#include "renderer/ResourceManager.hpp"
#include "ecs/Registry.hpp"
#include "ecs/components/Material.hpp"
#include "ecs/components/Mesh.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Camera.hpp"
#include "ecs/components/Grid.hpp"
#include "ecs/components/PrefabComponent.hpp"
#include "ecs/components/Skeleton.hpp"
#include "ecs/components/Animator.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/EntityCloner.hpp"
#include "profiler/Profiler.hpp"

namespace {
    inline bool isPrefabFile(const std::string& pathStr) {
        if (pathStr.empty()) return false;
        std::filesystem::path p(pathStr);
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(::tolower(c));
        return ext == ".prefab";
    }
}
#include "ecs/components/EditorCamera.hpp"
#include "ecs/components/AnimationController.hpp"
#include "ecs/components/IKSolver.hpp"
#include "ecs/components/Collider.hpp"
#include "ecs/components/Tilemap.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/SpriteRenderer.hpp"
#include "ufbx.h"
#include "cgltf.h"

#include <cstdlib>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>

using namespace ImGui;
using namespace std;

static void drawRegisteredAssetBrowserMenu(const std::filesystem::path& folderPath) {
    for (const auto& opt : Engine::AssetBrowserRegistry::getOptions()) {
        size_t slashPos = opt.labelPath.find('/');
        if (slashPos != std::string::npos) {
            std::string menuName = opt.labelPath.substr(0, slashPos);
            std::string itemName = opt.labelPath.substr(slashPos + 1);
            if (ImGui::BeginMenu(menuName.c_str())) {
                if (ImGui::MenuItem(itemName.c_str())) {
                    opt.callback(folderPath);
                }
                ImGui::EndMenu();
            }
        } else {
            if (ImGui::MenuItem(opt.labelPath.c_str())) {
                opt.callback(folderPath);
            }
        }
    }
}

static std::string acceptDroppedAssetPath() {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
        return std::string((const char*)payload->Data);
    }
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS")) {
        std::string s((const char*)payload->Data);
        size_t pipe = s.find('|');
        if (pipe != std::string::npos) s = s.substr(0, pipe);
        return s;
    }
    return "";
}

static void ensureClipLoaded(AnimatorComponent* animator, Registry& registry, Entity selectedEntity, const std::string& cName, VulkanRenderer& renderer) {
    if (!animator || cName.empty()) return;
    for (const auto& cl : animator->animations) {
        if (cl.name == cName || std::filesystem::path(cl.name).stem().string() == cName) return;
    }
    std::vector<std::string> candidates = {
        cName, cName + ".anim",
        "assets/" + cName, "assets/" + cName + ".anim",
        "assets/animations/" + cName, "assets/animations/" + cName + ".anim"
    };
    SkeletonComponent* skeleton = registry.get<SkeletonComponent>(selectedEntity);
    SkeletonComponent dummySkel;
    for (const auto& cand : candidates) {
        if (std::filesystem::exists(cand)) {
            renderer.resourceManager->loadBinarySkeletonAndAnimations(cand, skeleton ? *skeleton : dummySkel, *animator, true);
            return;
        }
    }
}

static bool s_openSSConverterWindow = false;
static std::filesystem::path s_ssConverterAssetPath;
static int s_ssCellWidth = 64;
static int s_ssCellHeight = 64;
static float s_ssFrameRate = 12.0f;
static std::string s_ssOutputPrefix = "";
static std::set<std::filesystem::path> s_selectedAssetPaths;
static bool s_ssCombineSingleFile = false;
static std::vector<EditorUI::SpritesheetAnimConfig> s_ssAnimConfigs = {
    { "Idle", 3, 0 },
    { "Walk", 5, 3 },
    { "Run", 8, 8 }
};

void EditorUI::drawPanels() {
    if (!initialized) {
        return;
    }

    // Safety check: clear selection if the selected entity was destroyed/invalidated
    if (hasSelection && (!registry.isValid(selectedEntity) || selectedEntity.getId() == Entity::INVALID_ENTITY)) {
        selectedEntity = Entity();
        hasSelection = false;
        renameBuffer.clear();
    }

    ImGuiIO& io = ImGui::GetIO();
    float width = io.DisplaySize.x;
    float height = io.DisplaySize.y;

    // 1. Top Menu Bar (Main Menu Bar)
    float topY = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        topY = ImGui::GetWindowSize().y; // dynamic height of the menu bar
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                if (Scene* currentScene = sceneManager.getCurrentScene()) {
                    currentScene->saveToFile(scenePath);
                    statusMessage = "Scene saved successfully.";
                }
            }
            if (ImGui::MenuItem("Load Scene", "Ctrl+L")) {
                if (Scene* currentScene = sceneManager.getCurrentScene()) {
                    SceneSerializer serializer(registry, renderer);
                    std::vector<Entity> loadedEntities;
                    if (serializer.deserialize(scenePath, loadedEntities)) {
                        statusMessage = "Scene loaded successfully.";
                    } else {
                        statusMessage = "Failed to load scene.";
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build Settings", "Ctrl+Shift+B")) {
                showBuildSettings = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Tileset Editor")) {
                s_openTilesetEditorWindow = true;
            }
            if (ImGui::MenuItem("Animation Editor")) {
                s_openAnimationEditorWindow = true;
            }
            if (ImGui::MenuItem("Animator Controller")) {
                s_openAnimatorControllerWindow = true;
            }
            if (ImGui::MenuItem("Node Graph Demo")) {
                s_openNodeGraphDemoWindow = true;
            }
            if (ImGui::MenuItem("Sprite Sheet Slicer")) {
                s_openSpriteSlicerWindow = true;
            }
            if (ImGui::MenuItem("Profiler")) {
                showProfilerPanel = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Duplicate Entity", "Ctrl+D", false, hasSelection)) {
                if (hasSelection && registry.isValid(selectedEntity)) {
                    Entity duplicated = EntityCloner::clone(registry, renderer, selectedEntity);
                    if (duplicated.getId() != Entity::INVALID_ENTITY) {
                        selectedEntity = duplicated;
                        hasSelection = true;
                        if (auto* name = registry.get<Name>(duplicated)) renameBuffer = name->value;
                        statusMessage = "Duplicated entity hierarchy.";
                    }
                }
            }
            if (ImGui::MenuItem("Delete Entity", "Delete", false, hasSelection)) {
                if (hasSelection && registry.isValid(selectedEntity)) {
                    std::vector<Entity> toDelete;
                    std::function<void(Entity)> collectChildren = [&](Entity parent) {
                        toDelete.push_back(parent);
                        for (auto [e, hierarchy] : registry.view<HierarchyComponent>()) {
                            if (hierarchy.parent == parent && e != parent && registry.isValid(e)) {
                                collectChildren(e);
                            }
                        }
                    };
                    collectChildren(selectedEntity);
                    for (Entity e : toDelete) {
                        if (registry.isValid(e)) registry.destroy(e);
                    }
                    hasSelection = false;
                    selectedEntity = Entity();
                    statusMessage = "Deleted entity hierarchy.";
                }
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        // Gizmo Mode Buttons
        auto pushActiveStyle = [](bool active) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.75f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.38f, 0.65f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.26f, 0.32f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
            }
        };

        pushActiveStyle(gizmoOperation == 0);
        if (ImGui::Button("Move [W]")) gizmoOperation = 0;
        ImGui::PopStyleColor(3);
        ImGui::SameLine();

        pushActiveStyle(gizmoOperation == 1);
        if (ImGui::Button("Rotate [E]")) gizmoOperation = 1;
        ImGui::PopStyleColor(3);
        ImGui::SameLine();

        pushActiveStyle(gizmoOperation == 2);
        if (ImGui::Button("Scale [R]")) gizmoOperation = 2;
        ImGui::PopStyleColor(3);

        ImGui::Separator();

        // Gizmo Space
        if (ImGui::Button(gizmoMode == 0 ? "Space: World" : "Space: Local")) {
            gizmoMode = (gizmoMode == 0) ? 1 : 0;
        }

        ImGui::Separator();

        // Grid Snap Toggle & Values
        pushActiveStyle(useSnap);
        if (ImGui::Button(useSnap ? "Snap: ON" : "Snap: OFF")) {
            useSnap = !useSnap;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.0f);
        if (gizmoOperation == 0) {
            ImGui::DragFloat("##snapT", &snapTranslation, 0.1f, 0.05f, 10.0f, "%.2fm");
        } else if (gizmoOperation == 1) {
            ImGui::DragFloat("##snapR", &snapRotation, 1.0f, 1.0f, 90.0f, "%.0f°");
        } else {
            ImGui::DragFloat("##snapS", &snapScale, 0.05f, 0.01f, 2.0f, "%.2fx");
        }

        // Center-aligned Play / Stop buttons in the Main Menu Bar
        float menuBarWidth = ImGui::GetWindowWidth();
        float buttonGroupWidth = 80.0f; // estimated width
        ImGui::SameLine(menuBarWidth * 0.5f - buttonGroupWidth * 0.5f);
        
        if (!editorMode.isPlaying) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.48f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.65f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.35f, 0.08f, 1.0f));
            if (ImGui::Button("PLAY", ImVec2(80, 0))) {
                editorMode.pendingPlay = true;
                statusMessage = "Entering Play Mode...";
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.68f, 0.12f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.08f, 0.08f, 1.0f));
            if (ImGui::Button("STOP", ImVec2(80, 0))) {
                editorMode.pendingStop = true;
                statusMessage = "Stopping simulation...";
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::EndMainMenuBar();
    }

    // Process Safe Keyboard Shortcuts
    ImGuiIO& activeIO = ImGui::GetIO();
    if (!activeIO.WantCaptureKeyboard && !editorMode.flyMode) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmoOperation = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmoOperation = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoOperation = 2;
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_D)) {
            if (hasSelection && registry.isValid(selectedEntity)) {
                Entity duplicated = EntityCloner::clone(registry, renderer, selectedEntity);
                if (duplicated.getId() != Entity::INVALID_ENTITY) {
                    selectedEntity = duplicated;
                    hasSelection = true;
                    if (auto* name = registry.get<Name>(duplicated)) renameBuffer = name->value;
                    statusMessage = "Duplicated entity hierarchy.";
                }
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (hasSelection && registry.isValid(selectedEntity)) {
                std::vector<Entity> toDelete;
                std::function<void(Entity)> collectChildren = [&](Entity parent) {
                    toDelete.push_back(parent);
                    for (auto [e, hierarchy] : registry.view<HierarchyComponent>()) {
                        if (hierarchy.parent == parent && e != parent && registry.isValid(e)) {
                            collectChildren(e);
                        }
                    }
                };
                collectChildren(selectedEntity);
                for (Entity e : toDelete) {
                    if (registry.isValid(e)) registry.destroy(e);
                }
                hasSelection = false;
                selectedEntity = Entity();
                statusMessage = "Deleted selected entity and hierarchy.";
            }
        }
    }

    // Fallback if MainMenuBar is not showing
    if (topY == 0.0f) {
        topY = 22.0f; 
    }

    float workHeight = height - topY;

    // Sidebar dimensions (snapped layout)
    float leftWidth = glm::clamp(width * 0.20f, 260.0f, 400.0f);
    float rightWidth = glm::clamp(width * 0.22f, 320.0f, 460.0f);
    float centerWidth = width - leftWidth - rightWidth;
    float bottomHeight = workHeight * 0.32f;
    float topPanelHeight = workHeight - bottomHeight;

    // 2. Hierarchy Panel (Left - Top)
    ImGui::SetNextWindowPos(ImVec2(0.0f, topY));
    ImGui::SetNextWindowSize(ImVec2(leftWidth, topPanelHeight));
    drawHierarchyPanel();

    // 3. Debug Panel (Left - Bottom)
    ImGui::SetNextWindowPos(ImVec2(0.0f, topY + topPanelHeight));
    ImGui::SetNextWindowSize(ImVec2(leftWidth, bottomHeight));
    drawDebugPanel();

    // 4. Asset Browser (Center - Bottom)
    ImGui::SetNextWindowPos(ImVec2(leftWidth, topY + topPanelHeight));
    ImGui::SetNextWindowSize(ImVec2(centerWidth, bottomHeight));
    drawAssetBrowser();

    // 5. Inspector Panel (Right)
    ImGui::SetNextWindowPos(ImVec2(width - rightWidth, topY));
    ImGui::SetNextWindowSize(ImVec2(rightWidth, workHeight));
    drawInspectorPanel();

    // 6. Draw Gizmo and Viewport overlay controls (drawn on top of clear center area)
    drawGizmo();
    drawColliderDebugOverlay();
    drawPhysgunDebugOverlay();
    drawTilemapGridOverlay();
    handleViewportPicking();

    // Viewport Drag and Drop Target for Prefabs
    ImGui::SetNextWindowPos(ImVec2(leftWidth, topY));
    ImGui::SetNextWindowSize(ImVec2(width - leftWidth - rightWidth, workHeight - bottomHeight));
    ImGui::Begin("##ViewportDropTargetWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui::End();

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
            const char* pathStr = (const char*)payload->Data;
            std::string path(pathStr);
            if (isPrefabFile(path)) {
                Scene* currentScene = sceneManager.getCurrentScene();
                if (currentScene) {
                    Entity created = currentScene->instantiatePrefab(path);
                    if (registry.isValid(created)) {
                        selectedEntity = created;
                        hasSelection = true;
                        statusMessage = "Instantiated prefab " + path + " into viewport.";
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    
    // 7. Float Import Settings panel
    drawImportSettingsWindow();
    
    // 7b. Floating Tileset Editor window
    drawTilesetEditorWindow();

    // 7c. Floating Animation Editor window
    drawAnimationEditorWindow();

    // 7d. Floating Animator Controller window
    drawAnimatorControllerWindow();

    // 7e. Floating Node Graph Demo window
    drawNodeGraphDemoWindow();

    // 7f. Floating Sprite Sheet Slicer window
    drawSpriteSlicerWindow();

    // 7g. Floating Spritesheet to Animations converter window
    drawSpritesheetConverterWindow();

    // 8. Build Settings panel (floating modal)
    if (showBuildSettings) {
        drawBuildSettingsPanel();
    }

    // 9. Profiler panel
    if (showProfilerPanel) {
        drawProfilerPanel();
    }
}

void EditorUI::drawSceneControls() {
    if (Button(editorMode.flyMode ? "Switch To Edit Mode" : "Switch To Fly Mode")) {
        editorMode.flyMode = !editorMode.flyMode;
        applyInputMode();
        statusMessage = editorMode.flyMode ? "Fly mode enabled." : "Edit mode enabled.";
    }
    SameLine();
    TextUnformatted(editorMode.flyMode ? "Camera controls active" : "Editor controls active");

    char pathBuffer[260]{};
    scenePath.copy(pathBuffer, scenePath.size(), 0);
    pathBuffer[scenePath.size()] = '\0';
    if (InputText("Scene Path", pathBuffer, sizeof(pathBuffer))) {
        scenePath = pathBuffer;
    }

    Scene* currentScene = sceneManager.getCurrentScene();
    if (Button("Save Scene")) {
        if (currentScene && currentScene->saveToFile(scenePath)) {
            statusMessage = "Scene saved to " + scenePath;
        } else {
            statusMessage = "Failed to save scene.";
        }
    }
    SameLine();
    if (Button("Load Scene")) {
        if (currentScene && currentScene->loadFromFile(scenePath)) {
            statusMessage = "Scene loaded from " + scenePath;
            hasSelection = false;
            selectedEntity = Entity();
            renameBuffer.clear();
        } else {
            statusMessage = "Failed to load scene.";
        }
    }
    TextWrapped("%s", statusMessage.c_str());
}

// ---------------------------------------------------------------------------
// Shared helper: renders entity-creation menu items from EntityArchetypeRegistry.
// `onCreate` is called with (typeString, isPrimitive) when the user picks an item.
// ---------------------------------------------------------------------------
static void drawEntityCreationMenus(const std::function<void(const std::string&, bool)>& onCreate) {
    using namespace Engine;
    const auto& archetypes = EntityArchetypeRegistry::getInstance().getArchetypes();

    // Group entries by category (preserve insertion order via vector of pairs)
    std::vector<std::pair<std::string, std::vector<const EntityArchetype*>>> groups;
    std::vector<const EntityArchetype*> topLevel; // items with no '/' in path

    for (const auto& arch : archetypes) {
        size_t slash = arch.menuPath.rfind('/');
        if (slash == std::string::npos) {
            topLevel.push_back(&arch);
        } else {
            std::string cat = arch.menuPath.substr(0, slash);
            // Find or create group
            bool found = false;
            for (auto& [groupCat, entries] : groups) {
                if (groupCat == cat) { entries.push_back(&arch); found = true; break; }
            }
            if (!found) groups.push_back({ cat, { &arch } });
        }
    }

    // Render category submenus
    for (auto& [cat, entries] : groups) {
        if (ImGui::BeginMenu(cat.c_str())) {
            for (const auto* arch : entries) {
                if (arch->separatorBefore) ImGui::Separator();
                std::string label = arch->menuPath.substr(arch->menuPath.rfind('/') + 1);
                if (ImGui::MenuItem(label.c_str())) {
                    onCreate(arch->typeString, arch->isPrimitive);
                }
            }
            ImGui::EndMenu();
        }
    }

    // Top-level items (e.g. "Empty GameObject") get a separator above them
    if (!topLevel.empty()) {
        ImGui::Separator();
        for (const auto* arch : topLevel) {
            if (ImGui::MenuItem(arch->menuPath.c_str())) {
                onCreate(arch->typeString, arch->isPrimitive);
            }
        }
    }
}

void EditorUI::drawHierarchyPanel() {
    Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    Scene* currentScene = sceneManager.getCurrentScene();

    // ---- Top Toolbar ----
    float panelWidth = ImGui::GetContentRegionAvail().x;

    // [+ Create] button as a popup
    if (ImGui::Button("+ Create", ImVec2(panelWidth * 0.48f, 0))) {
        ImGui::OpenPopup("CreateEntityPopup");
    }

    if (ImGui::BeginPopup("CreateEntityPopup")) {
        drawEntityCreationMenus([&](const std::string& typeStr, bool isPrimitive) {
            if (!currentScene) return;
            Entity e = isPrimitive ? currentScene->createPrimitiveEntity(typeStr)
                                   : currentScene->createEntityOfType(typeStr);
            if (e.getId() != Entity::INVALID_ENTITY) {
                selectedEntity = e; hasSelection = true;
                if (auto* n = registry.get<Name>(e)) renameBuffer = n->value;
            }
        });
        ImGui::EndPopup();
    }




    ImGui::SameLine();

    // [Duplicate] button — only enabled when something is selected
    BeginDisabled(!hasSelection || !currentScene);
    if (ImGui::Button("Duplicate", ImVec2(panelWidth * 0.48f, 0))) {
        Entity duplicated = currentScene->duplicateEntity(selectedEntity);
        if (duplicated.getId() != Entity::INVALID_ENTITY) {
            selectedEntity = duplicated;
            hasSelection = true;
            if (auto* n = registry.get<Name>(duplicated)) renameBuffer = n->value;
            statusMessage = "Duplicated selected entity.";
        }
    }
    EndDisabled();

    Separator();

    // ---- Entity Tree ----
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 3));

    // Scrollable child window for the entity tree list
    // Height is negative to leave space for the bottom delete button footer (approx 42px)
    ImGui::BeginChild("HierarchyTreeChild", ImVec2(0, -42.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Entity to delete deferred (can't destroy during iteration)
    Entity pendingDelete;
    bool hasPendingDelete = false;

    std::function<void(Entity, int)> drawEntityNode = [&](Entity entity, int depth) {
        if (depth > 10) return;
        if (registry.has<EditorCamera>(entity)) return;
        Name* nameComp = registry.get<Name>(entity);
        if (!nameComp) return;

        bool selected = (hasSelection && entity == selectedEntity);

        if (depth > 0) ImGui::Indent(depth * 16.0f);

        // Highlight selected row with accent color
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.00f, 0.40f, 0.70f, 0.60f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.00f, 0.48f, 0.80f, 0.80f));
        }

        bool isPrefab = registry.has<Engine::PrefabComponent>(entity);
        if (isPrefab) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.80f, 1.00f, 1.00f));
        }

        std::string displayName = (isPrefab ? "[P] " : "") + nameComp->value;
        std::string label = displayName + "##" + std::to_string(entity.getId());
        if (Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
            selectedEntity = entity;
            hasSelection = true;
            renameBuffer = nameComp->value;
        }

        if (isPrefab) {
            ImGui::PopStyleColor();
        }

        // Drag source for hierarchy moving/rearranging
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            std::uint32_t entId = entity.getId();
            ImGui::SetDragDropPayload("DND_PAYLOAD_HIERARCHY_ENTITY", &entId, sizeof(entId));
            ImGui::Text("Move: %s", nameComp->value.c_str());
            ImGui::EndDragDropSource();
        }

        // Drop target for parenting or instantiating prefabs under this entity
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_HIERARCHY_ENTITY")) {
                std::uint32_t draggedId = *static_cast<const std::uint32_t*>(payload->Data);
                Entity draggedEntity(draggedId);
                
                // Avoid parenting an entity to itself, or to any of its descendants (cycles)
                bool isSelfOrDescendant = (draggedEntity == entity);
                Entity check = entity;
                while (check.getId() != Entity::INVALID_ENTITY && registry.isValid(check)) {
                    if (auto* checkHierarchy = registry.get<HierarchyComponent>(check)) {
                        if (checkHierarchy->parent == draggedEntity) {
                            isSelfOrDescendant = true;
                            break;
                        }
                        check = checkHierarchy->parent;
                    } else {
                        break;
                    }
                }
                
                if (!isSelfOrDescendant) {
                    if (auto* hc = registry.get<HierarchyComponent>(draggedEntity)) {
                        hc->parent = entity;
                    } else {
                        registry.emplace<HierarchyComponent>(draggedEntity, HierarchyComponent{ entity });
                    }
                    statusMessage = "Parented entity under " + nameComp->value;
                } else {
                    statusMessage = "Cannot parent an entity to itself or its descendants!";
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
                const char* pathStr = (const char*)payload->Data;
                std::string path(pathStr);
                if (isPrefabFile(path)) {
                    Scene* activeScene = currentScene ? currentScene : sceneManager.getCurrentScene();
                    if (activeScene) {
                        Entity created = activeScene->instantiatePrefab(path, glm::vec3(0.0f), entity);
                        if (registry.isValid(created)) {
                            selectedEntity = created;
                            hasSelection = true;
                            statusMessage = "Instantiated prefab under " + nameComp->value;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (selected) ImGui::PopStyleColor(2);

        // Right-click context menu on any entity node
        std::string ctxId = "EntityCtx##" + std::to_string(entity.getId());
        if (ImGui::BeginPopupContextItem(ctxId.c_str())) {
            selectedEntity = entity;
            hasSelection = true;
            renameBuffer = nameComp->value;

            ImGui::TextDisabled("%s", nameComp->value.c_str());
            ImGui::Separator();

            if (ImGui::MenuItem("Duplicate")) {
                if (currentScene) {
                    Entity dup = currentScene->duplicateEntity(entity);
                    if (dup.getId() != Entity::INVALID_ENTITY) {
                        selectedEntity = dup;
                        hasSelection = true;
                        if (auto* n = registry.get<Name>(dup)) renameBuffer = n->value;
                        statusMessage = "Duplicated entity.";
                    }
                }
            }

            auto createAndParentChild = [&](const std::string& typeStr, bool isPrimitive) {
                if (!currentScene) return;
                Entity created;
                if (isPrimitive) {
                    created = currentScene->createPrimitiveEntity(typeStr);
                } else {
                    created = currentScene->createEntityOfType(typeStr);
                }
                if (created.getId() != Entity::INVALID_ENTITY) {
                    if (typeStr != "Canvas") {
                        if (auto* hc = registry.get<HierarchyComponent>(created)) {
                            hc->parent = entity;
                        } else {
                            registry.emplace<HierarchyComponent>(created, HierarchyComponent{ entity });
                        }
                    }
                    selectedEntity = created;
                    hasSelection = true;
                    if (auto* n = registry.get<Name>(created)) renameBuffer = n->value;
                    statusMessage = "Created " + typeStr + " child under " + nameComp->value;
                }
            };

            if (ImGui::BeginMenu("Create Child")) {
                drawEntityCreationMenus([&](const std::string& typeStr, bool isPrimitive) {
                    createAndParentChild(typeStr, isPrimitive);
                });
                ImGui::EndMenu();
            }



            if (auto* hc = registry.get<HierarchyComponent>(entity)) {
                if (hc->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hc->parent)) {
                    if (ImGui::MenuItem("Unparent (Make Root)")) {
                        hc->parent = Entity();
                        statusMessage = "Unparented entity to root.";
                    }
                }
            }

            if (hasSelection && selectedEntity != entity) {
                bool isDescendant = false;
                Entity check = entity;
                while (check.getId() != Entity::INVALID_ENTITY && registry.isValid(check)) {
                    if (auto* checkHierarchy = registry.get<HierarchyComponent>(check)) {
                        if (checkHierarchy->parent == selectedEntity) {
                            isDescendant = true;
                            break;
                        }
                        check = checkHierarchy->parent;
                    } else {
                        break;
                    }
                }
                if (!isDescendant) {
                    if (ImGui::MenuItem("Parent Selected to This")) {
                        if (auto* hc = registry.get<HierarchyComponent>(selectedEntity)) {
                            hc->parent = entity;
                        } else {
                            registry.emplace<HierarchyComponent>(selectedEntity, HierarchyComponent{ entity });
                        }
                        statusMessage = "Parented selected entity.";
                    }
                }
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            if (ImGui::MenuItem("Delete")) {
                pendingDelete = entity;
                hasPendingDelete = true;
            }
            ImGui::PopStyleColor();

            ImGui::EndPopup();
        }

        if (depth > 0) ImGui::Unindent(depth * 16.0f);

        // Draw children recursively
        for (auto [childEntity, hierarchy] : registry.view<HierarchyComponent>()) {
            if (hierarchy.parent == entity) {
                drawEntityNode(childEntity, depth + 1);
            }
        }
    };

    // Draw all root entities (those with no valid parent)
    for (auto [entity, name] : registry.view<Name>()) {
        if (registry.has<EditorCamera>(entity)) continue;
        bool hasParent = false;
        if (auto* hierarchy = registry.get<HierarchyComponent>(entity)) {
            if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
                hasParent = true;
            }
        }
        if (!hasParent) {
            drawEntityNode(entity, 0);
        }
    }

    // Drop target on empty space of child window to unparent to root
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_HIERARCHY_ENTITY")) {
            std::uint32_t draggedId = *static_cast<const std::uint32_t*>(payload->Data);
            Entity draggedEntity(draggedId);
            if (auto* hc = registry.get<HierarchyComponent>(draggedEntity)) {
                hc->parent = Entity();
                statusMessage = "Unparented entity to root.";
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
            if (payload->Data && payload->DataSize > 0) {
                const char* pathStr = (const char*)payload->Data;
                std::string path(pathStr);
                if (isPrefabFile(path)) {
                    Scene* activeScene = currentScene ? currentScene : sceneManager.getCurrentScene();
                    if (activeScene) {
                        Entity created = activeScene->instantiatePrefab(path);
                        if (registry.isValid(created)) {
                            selectedEntity = created;
                            hasSelection = true;
                            statusMessage = "Instantiated prefab " + path;
                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click context menu on empty background area of Hierarchy panel

    if (ImGui::BeginPopupContextWindow("HierarchyBgCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
        auto createRootObject = [&](const std::string& typeStr, bool isPrimitive) {
            if (!currentScene) return;
            Entity created = isPrimitive ? currentScene->createPrimitiveEntity(typeStr)
                                         : currentScene->createEntityOfType(typeStr);
            if (created.getId() != Entity::INVALID_ENTITY) {
                selectedEntity = created; hasSelection = true;
                if (auto* n = registry.get<Name>(created)) renameBuffer = n->value;
                statusMessage = "Created " + typeStr + ".";
            }
        };
        drawEntityCreationMenus(createRootObject);
        ImGui::EndPopup();
    }

    ImGui::EndChild(); // End of HierarchyTreeChild scrolling area

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
            if (payload->Data && payload->DataSize > 0) {
                const char* pathStr = (const char*)payload->Data;
                std::string path(pathStr);
                if (isPrefabFile(path)) {
                    Scene* activeScene = currentScene ? currentScene : sceneManager.getCurrentScene();
                    if (activeScene) {
                        Entity created = activeScene->instantiatePrefab(path);
                        if (registry.isValid(created)) {
                            selectedEntity = created;
                            hasSelection = true;
                            statusMessage = "Instantiated prefab " + path;
                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }


    PopStyleVar();

    // ---- Delete button (bottom of panel, red, always visible) ----
    Separator();

    bool canDelete = hasSelection && currentScene != nullptr;
    BeginDisabled(!canDelete);
    ImGui::PushStyleColor(ImGuiCol_Button,        canDelete ? ImVec4(0.65f, 0.10f, 0.10f, 1.0f) : ImVec4(0.30f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.08f, 0.08f, 1.0f));
    if (ImGui::Button("Delete Selected", ImVec2(-1, 0))) {
        if (currentScene->deleteEntity(selectedEntity)) {
            statusMessage = "Deleted selected entity.";
            hasSelection = false;
            selectedEntity = Entity();
            renameBuffer.clear();
        }
    }
    ImGui::PopStyleColor(3);
    EndDisabled();

    // Process deferred deletion from context menu
    if (hasPendingDelete && currentScene) {
        if (currentScene->deleteEntity(pendingDelete)) {
            statusMessage = "Deleted entity.";
            if (selectedEntity == pendingDelete) {
                hasSelection = false;
                selectedEntity = Entity();
                renameBuffer.clear();
            }
        }
    }

    End();
}

void EditorUI::drawInspectorPanel() {
    Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    TextUnformatted("Runtime ECS Editor");
    drawSceneControls();

    if (!hasSelection) {
        Separator();
        TextUnformatted("Select an entity in the hierarchy.");
        End();
        return;
    }

    Name* name = registry.get<Name>(selectedEntity);
    if (!name) {
        hasSelection = false;
        Separator();
        TextUnformatted("Selection is no longer valid.");
        End();
        return;
    }

    Separator();
    Text("Selected: %s", name->value.c_str());

    char renameBufferChars[128]{};
    renameBuffer.copy(renameBufferChars, std::min(renameBuffer.size(), sizeof(renameBufferChars) - 1), 0);
    if (InputText("Name", renameBufferChars, sizeof(renameBufferChars))) {
        renameBuffer = renameBufferChars;
    }
    SameLine();
    if (Button("Rename Selected")) {
        if (!renameBuffer.empty()) {
            name->value = renameBuffer;
            statusMessage = "Renamed selected entity.";
        } else {
            statusMessage = "Name cannot be empty.";
        }
    }
    SameLine();
    if (auto* existingPrefab = registry.get<Engine::PrefabComponent>(selectedEntity)) {
        if (!existingPrefab->prefabAssetPath.empty()) {
            if (Button("Save Prefab")) {
                SceneSerializer serializer(registry, renderer);
                if (serializer.serializePrefab(existingPrefab->prefabAssetPath, selectedEntity)) {
                    statusMessage = "Saved changes to prefab asset: " + existingPrefab->prefabAssetPath;
                } else {
                    statusMessage = "Failed to save prefab asset: " + existingPrefab->prefabAssetPath;
                }
            }
            SameLine();
        }
    }
    if (Button("Save as New Prefab")) {
        std::string prefabDir = "assets/prefabs";
        if (!std::filesystem::exists(prefabDir)) {
            std::filesystem::create_directories(prefabDir);
        }
        std::string prefabPath = prefabDir + "/" + name->value + ".prefab";
        SceneSerializer serializer(registry, renderer);
        if (serializer.serializePrefab(prefabPath, selectedEntity)) {
            registry.emplace_or_replace<Engine::PrefabComponent>(selectedEntity, Engine::PrefabComponent{ prefabPath, true });
            statusMessage = "Saved prefab to " + prefabPath;
        } else {
            statusMessage = "Failed to save prefab.";
        }
    }

    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));

    drawSectionHeader(name->value.c_str());

    drawTransformEditor();
    drawMeshEditor();
    drawMaterialEditor();
    drawSkeletonEditor();
    drawAnimatorEditor();
    drawHierarchyEditor();
    drawIKSolverEditor();
    drawAnimationControllerEditor();
    drawReflectedComponentsEditor();
    drawColliderEditor();
    drawTilemapInspector();
    drawSpriteRendererInspector();
    drawUIComponentsEditor();
    drawGridEditor();
    drawCameraEditor();

    // Render dynamic plugin component editors
    for (auto& [compName, callback] : getDynamicInspectors()) {
        callback(registry, selectedEntity);
    }

    ImGui::Separator();
    if (ImGui::Button("+ Add Component", ImVec2(-1, 30))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    if (ImGui::BeginPopup("AddComponentPopup")) {
        // Optional search filter
        static char s_searchBuf[128] = {};
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##comp_search", "Search...", s_searchBuf, sizeof(s_searchBuf));
        const std::string searchStr(s_searchBuf);
        bool filtering = !searchStr.empty();

        // Collect all reflected components grouped by category
        struct MenuEntry {
            const Engine::ComponentReflection* refl;
            std::string label; // final display label
            bool alreadyAttached;
        };
        std::map<std::string, std::vector<MenuEntry>> grouped; // category -> entries

        for (const auto& refl : Engine::ComponentReflectionRegistry::getInstance().getReflections()) {
            // Skip internal / always-present components
            if (refl.name == "Transform" || refl.name == "Name" || refl.name == "Hierarchy") continue;

            bool alreadyAttached = refl.has(registry, selectedEntity);

            // Derive display label: use registered displayName if set, otherwise prettify the type name
            std::string label = refl.displayName.empty() ? refl.name : refl.displayName;

            // Filter
            if (filtering) {
                std::string haystack = label;
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::string needle = searchStr;
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
                if (haystack.find(needle) == std::string::npos) continue;
            }

            std::string cat = refl.category.empty() ? "General" : refl.category;
            grouped[cat].push_back({ &refl, label, alreadyAttached });
        }

        // Render grouped menu (flat when filtering, nested submenus otherwise)
        for (auto& [category, entries] : grouped) {
            bool inSubMenu = !filtering && ImGui::BeginMenu(category.c_str());

            for (auto& entry : entries) {
                bool renderItem = filtering || inSubMenu;
                if (!renderItem) continue;

                std::string itemLabel = entry.label + (entry.alreadyAttached ? " (Attached)" : "");
                if (ImGui::MenuItem(itemLabel.c_str(), nullptr, false, !entry.alreadyAttached)) {
                    if (!entry.alreadyAttached) {
                        entry.refl->add(registry, selectedEntity);
                    }

                    // Auto-attach RectTransform for any UI component
                    if ((entry.refl->category == "UI" || entry.refl->category.rfind("UI/", 0) == 0) &&
                        entry.refl->name != "Canvas" && entry.refl->name != "CanvasComponent" &&
                        !registry.has<Engine::RectTransform>(selectedEntity)) {
                        registry.emplace<Engine::RectTransform>(selectedEntity, Engine::RectTransform{});
                    }

                    // Special post-add setup for Material
                    if (entry.refl->name == "Material") {
                        if (auto* mat = registry.get<Material>(selectedEntity)) {
                            bool hasSkin = entityHasSkin(registry, selectedEntity);
                            std::string vert = hasSkin ? "skinned.vert.spv" : "unlit.vert.spv";
                            PipelineHandle pipe = renderer.createPipelineForShaders(
                                renderer.resolveShaderPath("build/shaders/" + vert),
                                renderer.resolveShaderPath("build/shaders/unlit.frag.spv")
                            );
                            mat->pipeline       = pipe.pipeline;
                            mat->pipelineLayout = pipe.layout;
                            renderer.resourceManager->updateMaterialDescriptorSet(*mat, renderer);
                        }
                    }

                    statusMessage = "Added " + entry.label + " component.";
                    s_searchBuf[0] = '\0'; // clear search after selection
                }
            }

            if (!filtering && inSubMenu) ImGui::EndMenu();
        }

        // Dynamic plugin components (keep in their own section)
        bool hasPlugins = false;
        for (auto& [compName, callback] : getDynamicAddCallbacks()) {
            if (filtering) {
                std::string lower = compName;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                std::string needle = searchStr;
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
                if (lower.find(needle) == std::string::npos) continue;
            }
            if (!hasPlugins && !filtering) { ImGui::Separator(); hasPlugins = true; }
            if (ImGui::MenuItem(compName.c_str())) {
                callback(registry, selectedEntity);
                statusMessage = "Added " + compName + " component.";
                s_searchBuf[0] = '\0';
            }
        }

        ImGui::EndPopup();
    }

    PopStyleVar(2);

    End();
}


void EditorUI::drawDebugPanel() {
    Begin("Debug", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    TextUnformatted("Picking Debug");
    Separator();

    Text("Result: %s", lastPickResult.c_str());
    Text("Nearest Candidate: %s", lastPickNearestEntityName.c_str());

    if (lastPickNearestDistance >= 0.0f) {
        Text("Nearest Distance: %.3f", lastPickNearestDistance);
    }
    else {
        TextUnformatted("Nearest Distance: none");
    }

    Spacing();

    TextUnformatted("Ray Origin:");
    Text("(%.2f, %.2f, %.2f)",
        lastPickRayOrigin.x,
        lastPickRayOrigin.y,
        lastPickRayOrigin.z);

    TextUnformatted("Ray Direction:");
    Text("(%.2f, %.2f, %.2f)",
        lastPickRayDirection.x,
        lastPickRayDirection.y,
        lastPickRayDirection.z);

    Spacing();
    Separator();
    TextUnformatted("Clip Depth Mode: OpenGL-style (-1..1)");

    Spacing();
    Separator();
    TextUnformatted("Gameplay Camera Matrices from Renderer:");
    Text("Det(VP): %.4f", glm::determinant(renderer.getGameplayCameraViewProj()));
    Text("Pos: (%.2f, %.2f, %.2f)", 
         renderer.getGameplayCameraPosition().x, 
         renderer.getGameplayCameraPosition().y, 
         renderer.getGameplayCameraPosition().z);

    Spacing();
    Separator();
    Checkbox("Show Colliders", &showColliders);

    End();
}

void openInExplorer(const std::filesystem::path& path);

void EditorUI::drawAssetBrowser() {
    Begin("Asset Browser", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (!std::filesystem::exists("assets")) {
        std::filesystem::create_directories("assets");
        std::filesystem::create_directories("assets/models");
        std::filesystem::create_directories("assets/textures");
        std::filesystem::create_directories("assets/prefabs");
    }

    // Static variables to maintain state for rename and create popups between frames
    static std::filesystem::path s_renameTargetPath;
    static char s_renameBuffer[256] = "";
    static std::filesystem::path s_createFolderParentPath;
    static char s_createFolderBuffer[256] = "";
    static std::filesystem::path s_createSceneParentPath;
    static char s_createSceneBuffer[256] = "";
    static std::filesystem::path s_createFileParentPath;
    static char s_createFileBuffer[256] = "";
    static bool s_openCreateFolderPopup = false;
    static bool s_openCreateScenePopup = false;
    static bool s_openCreateFilePopup = false;
    static bool s_openRenamePopup = false;
    static std::filesystem::path s_lastSelectedAssetPath;

    std::vector<std::filesystem::path> visiblePaths;

    // ---- Toolbar ----
    if (Button("Refresh")) {
        statusMessage = "Refreshed asset directories.";
    }
    Separator();

    // ---- Recursive Directory Tree drawing lambda ----
    std::function<void(const std::filesystem::path&)> drawDirectoryNode = [&](const std::filesystem::path& dirPath) {
        if (!std::filesystem::exists(dirPath)) return;

        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            std::string name = entry.path().filename().string();
            // Skip hidden items
            if (name.empty() || name[0] == '.') {
                continue;
            }

            std::string pathStr = entry.path().generic_string();

            if (entry.is_directory()) {
                visiblePaths.push_back(entry.path());
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
                std::string label = "[] " + name + "##" + pathStr;
                
                bool isSelected = s_selectedAssetPaths.find(entry.path()) != s_selectedAssetPaths.end();
                if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

                bool open = TreeNodeEx(label.c_str(), flags);

                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    if (ImGui::GetIO().KeyShift && !s_lastSelectedAssetPath.empty()) {
                        auto itLast = std::find(visiblePaths.begin(), visiblePaths.end(), s_lastSelectedAssetPath);
                        auto itCurr = std::find(visiblePaths.begin(), visiblePaths.end(), entry.path());
                        if (itLast != visiblePaths.end() && itCurr != visiblePaths.end()) {
                            int idxA = std::distance(visiblePaths.begin(), itLast);
                            int idxB = std::distance(visiblePaths.begin(), itCurr);
                            int startIdx = std::min(idxA, idxB);
                            int endIdx = std::max(idxA, idxB);
                            if (!ImGui::GetIO().KeyCtrl) {
                                s_selectedAssetPaths.clear();
                            }
                            for (int idx = startIdx; idx <= endIdx; ++idx) {
                                s_selectedAssetPaths.insert(visiblePaths[idx]);
                            }
                        }
                    } else if (ImGui::GetIO().KeyCtrl) {
                        if (isSelected) s_selectedAssetPaths.erase(entry.path());
                        else s_selectedAssetPaths.insert(entry.path());
                        s_lastSelectedAssetPath = entry.path();
                    } else {
                        s_selectedAssetPaths.clear();
                        s_selectedAssetPaths.insert(entry.path());
                        s_lastSelectedAssetPath = entry.path();
                    }
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    if (s_selectedAssetPaths.find(entry.path()) == s_selectedAssetPaths.end()) {
                        s_selectedAssetPaths.clear();
                        s_selectedAssetPaths.insert(entry.path());
                    }
                }

                if (BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    std::string multiPaths;
                    for (const auto& p : s_selectedAssetPaths) {
                        if (!multiPaths.empty()) multiPaths += "|";
                        multiPaths += p.generic_string();
                    }
                    if (s_selectedAssetPaths.find(entry.path()) == s_selectedAssetPaths.end()) {
                        multiPaths = entry.path().generic_string();
                    }

                    if (s_selectedAssetPaths.size() > 1 && s_selectedAssetPaths.find(entry.path()) != s_selectedAssetPaths.end()) {
                        SetDragDropPayload("DND_PAYLOAD_MULTI_ASSETS", multiPaths.c_str(), multiPaths.size() + 1);
                        Text("Dragging %d assets", (int)s_selectedAssetPaths.size());
                    } else {
                        SetDragDropPayload("DND_PAYLOAD_ASSET_PATH", pathStr.c_str(), pathStr.size() + 1);
                        Text("Dragging folder %s", name.c_str());
                    }
                    EndDragDropSource();
                }

                if (BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS")) {
                        if (payload->Data && payload->DataSize > 0) {
                            std::string pathsStr((const char*)payload->Data);
                            std::stringstream ss(pathsStr);
                            std::string item;
                            int movedCount = 0;
                            while (std::getline(ss, item, '|')) {
                                std::filesystem::path src(item);
                                std::filesystem::path dest = entry.path() / src.filename();
                                
                                std::string srcStr = src.generic_string();
                                std::string destStr = dest.generic_string();
                                if (srcStr != destStr && destStr.rfind(srcStr + "/", 0) != 0) {
                                    try {
                                        std::filesystem::rename(src, dest);
                                        movedCount++;
                                    } catch (...) {}
                                }
                            }
                            if (movedCount > 0) {
                                statusMessage = "Moved " + std::to_string(movedCount) + " assets to " + name;
                            }
                        }
                    } else if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
                        if (payload->Data && payload->DataSize > 0) {
                            const char* srcPath = (const char*)payload->Data;
                            std::filesystem::path src(srcPath);
                            std::filesystem::path dest = entry.path() / src.filename();
                            
                            std::string srcStr = src.generic_string();
                            std::string destStr = dest.generic_string();
                            if (srcStr != destStr && destStr.rfind(srcStr + "/", 0) != 0) {
                                try {
                                    std::filesystem::rename(src, dest);
                                    statusMessage = "Moved " + src.filename().string() + " to " + name;
                                } catch (...) {}
                            }
                        }
                    } else if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_HIERARCHY_ENTITY")) {
                        if (payload->Data && payload->DataSize > 0) {
                            std::uint32_t entId = *static_cast<const std::uint32_t*>(payload->Data);
                            Entity draggedEntity(entId);
                            if (registry.isValid(draggedEntity)) {
                                Name* nameComp = registry.get<Name>(draggedEntity);
                                std::string entName = nameComp ? nameComp->value : "Prefab";
                                std::filesystem::path targetDir = entry.path();
                                std::filesystem::create_directories(targetDir);
                                std::filesystem::path prefabPath = targetDir / (entName + ".prefab");
                                
                                SceneSerializer serializer(registry, renderer);
                                if (serializer.serializePrefab(prefabPath.string(), draggedEntity)) {
                                    registry.emplace_or_replace<Engine::PrefabComponent>(draggedEntity, Engine::PrefabComponent{ prefabPath.string(), true });
                                    statusMessage = "Created prefab " + prefabPath.string();
                                }
                            }
                        }
                    }
                    EndDragDropTarget();
                }

                // Right click context menu on folders
                if (BeginPopupContextItem(pathStr.c_str())) {
                    TextDisabled("Folder: %s", name.c_str());
                    Separator();
                    if (MenuItem("Show in Explorer")) {
                        openInExplorer(entry.path());
                    }
                    Separator();
                    if (BeginMenu("Create")) {
                        if (MenuItem("Folder")) {
                            s_createFolderParentPath = entry.path();
                            s_createFolderBuffer[0] = '\0';
                            s_openCreateFolderPopup = true;
                        }
                        if (MenuItem("Scene")) {
                            s_createSceneParentPath = entry.path();
                            s_createSceneBuffer[0] = '\0';
                            s_openCreateScenePopup = true;
                        }
                        if (MenuItem("File")) {
                            s_createFileParentPath = entry.path();
                            s_createFileBuffer[0] = '\0';
                            s_openCreateFilePopup = true;
                        }
                        if (MenuItem("Animation File (.anim)")) {
                            s_createFileParentPath = entry.path();
                            strcpy_s(s_createFileBuffer, "new_animation.anim");
                            s_openCreateFilePopup = true;
                        }

                        // Custom options registered to the asset browser menu
                        drawRegisteredAssetBrowserMenu(entry.path());

                        ImGui::EndMenu();
                    }
                    if (MenuItem("Rename")) {
                        s_renameTargetPath = entry.path();
                        strncpy_s(s_renameBuffer, name.c_str(), sizeof(s_renameBuffer) - 1);
                        s_openRenamePopup = true;
                    }
                    PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                    if (s_selectedAssetPaths.size() > 1 && s_selectedAssetPaths.find(entry.path()) != s_selectedAssetPaths.end()) {
                        std::string delLabel = "Delete Selected (" + std::to_string(s_selectedAssetPaths.size()) + ")";
                        if (MenuItem(delLabel.c_str())) {
                            int delCount = 0;
                            for (const auto& p : s_selectedAssetPaths) {
                                try {
                                    if (std::filesystem::exists(p)) {
                                        std::filesystem::remove_all(p);
                                        delCount++;
                                    }
                                } catch (...) {}
                            }
                            statusMessage = "Deleted " + std::to_string(delCount) + " assets.";
                            s_selectedAssetPaths.clear();
                        }
                    } else {
                        if (MenuItem("Delete Folder")) {
                            try {
                                std::filesystem::path activePath = entry.path();
                                std::filesystem::remove_all(activePath);
                                statusMessage = "Deleted folder: " + name;
                                s_selectedAssetPaths.erase(activePath);
                            } catch (const std::exception& e) {
                                statusMessage = std::string("Failed to delete folder: ") + e.what();
                            }
                        }
                    }
                    PopStyleColor();
                    EndPopup();
                }

                if (open) {
                    drawDirectoryNode(entry.path());
                    TreePop();
                }
            } else if (entry.is_regular_file()) {
                visiblePaths.push_back(entry.path());
                auto ext = entry.path().extension().string();
                bool isModel = (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".FBX");
                bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga");
                bool isPrefab = (ext == ".prefab");
                bool isScene = (ext == ".json");
                bool isTileset = (ext == ".tileset");
                bool isTile    = (ext == ".tile");

                std::string prefix = "  ";
                if (isModel)   prefix = "[] ";
                else if (isTexture) prefix = "[] ";
                else if (isPrefab)  prefix = "[] ";
                else if (isScene)   prefix = "[] ";
                else if (isTileset) prefix = "[] ";
                else if (isTile)    prefix = "[] ";

                std::string labelStr = prefix + name + "##" + pathStr;
                
                bool isSelected = s_selectedAssetPaths.find(entry.path()) != s_selectedAssetPaths.end();
                
                if (Selectable(labelStr.c_str(), isSelected, ImGuiSelectableFlags_AllowOverlap)) {
                    if (ImGui::GetIO().KeyShift && !s_lastSelectedAssetPath.empty()) {
                        auto itLast = std::find(visiblePaths.begin(), visiblePaths.end(), s_lastSelectedAssetPath);
                        auto itCurr = std::find(visiblePaths.begin(), visiblePaths.end(), entry.path());
                        if (itLast != visiblePaths.end() && itCurr != visiblePaths.end()) {
                            int idxA = std::distance(visiblePaths.begin(), itLast);
                            int idxB = std::distance(visiblePaths.begin(), itCurr);
                            int startIdx = std::min(idxA, idxB);
                            int endIdx = std::max(idxA, idxB);
                            if (!ImGui::GetIO().KeyCtrl) {
                                s_selectedAssetPaths.clear();
                            }
                            for (int idx = startIdx; idx <= endIdx; ++idx) {
                                s_selectedAssetPaths.insert(visiblePaths[idx]);
                            }
                        }
                    } else if (ImGui::GetIO().KeyCtrl) {
                        if (isSelected) s_selectedAssetPaths.erase(entry.path());
                        else s_selectedAssetPaths.insert(entry.path());
                        s_lastSelectedAssetPath = entry.path();
                    } else {
                        s_selectedAssetPaths.clear();
                        s_selectedAssetPaths.insert(entry.path());
                        s_lastSelectedAssetPath = entry.path();
                    }
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    if (s_selectedAssetPaths.find(entry.path()) == s_selectedAssetPaths.end()) {
                        s_selectedAssetPaths.clear();
                        s_selectedAssetPaths.insert(entry.path());
                    }
                }

                if (BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    std::string multiPaths;
                    for (const auto& p : s_selectedAssetPaths) {
                        if (!multiPaths.empty()) multiPaths += "|";
                        multiPaths += p.generic_string();
                    }
                    if (s_selectedAssetPaths.find(entry.path()) == s_selectedAssetPaths.end()) {
                        multiPaths = entry.path().generic_string();
                    }

                    if (s_selectedAssetPaths.size() > 1 && s_selectedAssetPaths.find(entry.path()) != s_selectedAssetPaths.end()) {
                        SetDragDropPayload("DND_PAYLOAD_MULTI_ASSETS", multiPaths.c_str(), multiPaths.size() + 1);
                        Text("Dragging %d assets", (int)s_selectedAssetPaths.size());
                    } else {
                        SetDragDropPayload("DND_PAYLOAD_ASSET_PATH", pathStr.c_str(), pathStr.size() + 1);
                        Text("Dragging %s", name.c_str());
                    }
                    EndDragDropSource();
                }

                // Right click context menu on files (must follow Selectable immediately to bind correctly)
                if (BeginPopupContextItem(pathStr.c_str())) {
                    TextDisabled("File: %s", name.c_str());
                    Separator();
                    if (MenuItem("Show in Explorer")) {
                        openInExplorer(entry.path());
                    }
                    Separator();
                    if (isModel || isTexture) {
                        std::string importLabel = (s_selectedAssetPaths.size() > 1) ? "Batch Import Settings..." : "Import Settings...";
                        if (MenuItem(importLabel.c_str())) {
                            s_importSettingsAssetPath = entry.path();
                            s_triggerLoadImportSettings = true;
                        }
                        if (isTexture && MenuItem("Sprite Sheet Slicer...")) {
                            s_spriteSlicerAssetPath = entry.path();
                            s_openSpriteSlicerWindow = true;
                            s_sliceCellWidth = 64;
                            s_sliceCellHeight = 64;
                            s_sliceOutputPrefix = s_spriteSlicerAssetPath.stem().string();
                        }
                        if (isTexture && MenuItem("Convert Spritesheet to Animations...")) {
                            s_ssConverterAssetPath = entry.path();
                            s_openSSConverterWindow = true;
                            s_ssCellWidth = 64;
                            s_ssCellHeight = 64;
                            s_ssFrameRate = 12.0f;
                            s_ssOutputPrefix = s_ssConverterAssetPath.stem().string();
                        }
                        Separator();
                    }
                    if (isModel && MenuItem("Load Mesh to Selected")) {
                        if (hasSelection && registry.isValid(selectedEntity)) {
                            try {
                                int primCount = renderer.resourceManager->getMeshPrimitiveCount(pathStr);
                                auto emplaceOrReplaceMesh = [&](Entity ent, Mesh&& mesh) {
                                    if (registry.has<Mesh>(ent)) {
                                        registry.getRef<Mesh>(ent) = std::move(mesh);
                                    } else {
                                        registry.emplace<Mesh>(ent, std::move(mesh));
                                    }
                                };
                                if (primCount > 1) {
                                    for (int i = 0; i < primCount; ++i) {
                                        Entity subEntity = selectedEntity;
                                        if (i > 0) {
                                            subEntity = registry.create();
                                            registry.emplace<Name>(subEntity, Name{ name + "_primitive_" + std::to_string(i) });
                                            registry.emplace<Transform>(subEntity, Transform{ glm::vec3(0.f) });
                                            registry.emplace<HierarchyComponent>(subEntity, HierarchyComponent{ selectedEntity });
                                        }
                                        Mesh loadedMesh = renderer.resourceManager->loadMesh(pathStr, renderer, i);
                                        emplaceOrReplaceMesh(subEntity, std::move(loadedMesh));
                                    }
                                } else {
                                    Mesh loadedMesh = renderer.resourceManager->loadMesh(pathStr, renderer);
                                    emplaceOrReplaceMesh(selectedEntity, std::move(loadedMesh));
                                }

                                if (auto* material = registry.get<Material>(selectedEntity)) {
                                    bool hasSkin = entityHasSkin(registry, selectedEntity);
                                    PipelineHandle pipeline = renderer.createPipelineForShaders(
                                        hasSkin ? renderer.resolveShaderPath("build/shaders/skinned.vert.spv") : renderer.resolveShaderPath("build/shaders/unlit.vert.spv"),
                                        renderer.resolveShaderPath("build/shaders/unlit.frag.spv")
                                    );
                                    material->pipeline = pipeline.pipeline;
                                    material->pipelineLayout = pipeline.layout;
                                }

                                statusMessage = "Loaded glTF mesh successfully.";
                            } catch (const std::exception& e) {
                                statusMessage = std::string("Failed to load glTF: ") + e.what();
                            }
                        }
                    }
                    if (isTexture && MenuItem("Apply Texture to Selected Material")) {
                        if (hasSelection && registry.isValid(selectedEntity)) {
                            if (auto* material = registry.get<Material>(selectedEntity)) {
                                material->texturePath = pathStr;
                                renderer.resourceManager->updateMaterialDescriptorSet(*material, renderer);
                                statusMessage = "Applied texture to selected entity's material.";
                            } else {
                                statusMessage = "Selected entity has no Material component.";
                            }
                        } else {
                            statusMessage = "No entity selected.";
                        }
                    }
                    if (isPrefab && MenuItem("Instantiate Prefab")) {
                        SceneSerializer serializer(registry, renderer);
                        std::vector<Entity> loadedEntities;
                        Entity instantiated = serializer.deserializePrefab(pathStr, loadedEntities);
                        if (instantiated.getId() != Entity::INVALID_ENTITY) {
                            selectedEntity = instantiated;
                            hasSelection = true;
                            if (auto* n = registry.get<Name>(instantiated)) renameBuffer = n->value;
                            statusMessage = "Instantiated prefab.";
                        } else {
                            statusMessage = "Failed to instantiate prefab.";
                        }
                    }
                    if (isScene && MenuItem("Load Scene")) {
                        if (Scene* currentScene = sceneManager.getCurrentScene()) {
                            if (currentScene->loadFromFile(pathStr)) {
                                statusMessage = "Loaded scene " + name;
                                hasSelection = false;
                                selectedEntity = Entity();
                                renameBuffer.clear();
                            } else {
                                statusMessage = "Failed to load scene.";
                            }
                        }
                    }
                    if (MenuItem("Rename")) {
                        s_renameTargetPath = entry.path();
                        strncpy_s(s_renameBuffer, name.c_str(), sizeof(s_renameBuffer) - 1);
                        s_openRenamePopup = true;
                    }
                    PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                    if (s_selectedAssetPaths.size() > 1 && s_selectedAssetPaths.find(entry.path()) != s_selectedAssetPaths.end()) {
                        std::string delLabel = "Delete Selected (" + std::to_string(s_selectedAssetPaths.size()) + ")";
                        if (MenuItem(delLabel.c_str())) {
                            int delCount = 0;
                            for (const auto& p : s_selectedAssetPaths) {
                                try {
                                    if (std::filesystem::exists(p)) {
                                        std::filesystem::remove(p);
                                        delCount++;
                                    }
                                } catch (...) {}
                            }
                            statusMessage = "Deleted " + std::to_string(delCount) + " assets.";
                            s_selectedAssetPaths.clear();
                        }
                    } else {
                        if (MenuItem("Delete File")) {
                            try {
                                std::filesystem::path activePath = entry.path();
                                std::filesystem::remove(activePath);
                                statusMessage = "Deleted file: " + name;
                                s_selectedAssetPaths.erase(activePath);
                            } catch (const std::exception& e) {
                                statusMessage = std::string("Failed to delete file: ") + e.what();
                            }
                        }
                    }
                    PopStyleColor();
                    EndPopup();
                }
            }
        }
    };

    // Draw active running assets folder
    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
    std::string rootLabel = "[] assets##assets_root";
    if (TreeNodeEx(rootLabel.c_str(), rootFlags)) {
        if (BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS")) {
                if (payload->Data && payload->DataSize > 0) {
                    std::string pathsStr((const char*)payload->Data);
                    std::stringstream ss(pathsStr);
                    std::string item;
                    int movedCount = 0;
                    while (std::getline(ss, item, '|')) {
                        std::filesystem::path src(item);
                        std::filesystem::path dest = std::filesystem::path("assets") / src.filename();
                        
                        std::string srcStr = src.generic_string();
                        std::string destStr = dest.generic_string();
                        if (srcStr != destStr && destStr.rfind(srcStr + "/", 0) != 0) {
                            try {
                                std::filesystem::rename(src, dest);
                                movedCount++;
                            } catch (...) {}
                        }
                    }
                    if (movedCount > 0) {
                        statusMessage = "Moved " + std::to_string(movedCount) + " assets to assets root.";
                    }
                }
            } else if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
                if (payload->Data && payload->DataSize > 0) {
                    const char* srcPath = (const char*)payload->Data;
                    std::filesystem::path src(srcPath);
                    std::filesystem::path dest = std::filesystem::path("assets") / src.filename();
                    
                    std::string srcStr = src.generic_string();
                    std::string destStr = dest.generic_string();
                    if (srcStr != destStr && destStr.rfind(srcStr + "/", 0) != 0) {
                        try {
                            std::filesystem::rename(src, dest);
                            statusMessage = "Moved " + src.filename().string() + " to assets root.";
                        } catch (const std::exception& e) {
                            statusMessage = std::string("Failed to move: ") + e.what();
                        }
                    }
                }
            } else if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_HIERARCHY_ENTITY")) {
                if (payload->Data && payload->DataSize > 0) {
                    std::uint32_t entId = *static_cast<const std::uint32_t*>(payload->Data);
                    Entity draggedEntity(entId);
                    if (registry.isValid(draggedEntity)) {
                        Name* nameComp = registry.get<Name>(draggedEntity);
                        std::string entName = nameComp ? nameComp->value : "Prefab";
                        std::string prefabDir = "assets/prefabs";
                        std::filesystem::create_directories(prefabDir);
                        std::string prefabPath = prefabDir + "/" + entName + ".prefab";
                        
                        SceneSerializer serializer(registry, renderer);
                        if (serializer.serializePrefab(prefabPath, draggedEntity)) {
                            registry.emplace_or_replace<Engine::PrefabComponent>(draggedEntity, Engine::PrefabComponent{ prefabPath, true });
                            statusMessage = "Created prefab " + prefabPath;
                        }
                    }
                }
            }
            EndDragDropTarget();
        }

        // Right click context menu on assets root node
        if (BeginPopupContextItem("assets_root_ctx")) {
            if (MenuItem("Show in Explorer")) {
                openInExplorer("assets");
            }
            Separator();
            if (BeginMenu("Create")) {
                if (MenuItem("Folder")) {
                    s_createFolderParentPath = "assets";
                    s_createFolderBuffer[0] = '\0';
                    s_openCreateFolderPopup = true;
                }
                if (MenuItem("Scene")) {
                    s_createSceneParentPath = "assets";
                    s_createSceneBuffer[0] = '\0';
                    s_openCreateScenePopup = true;
                }
                if (MenuItem("File")) {
                    s_createFileParentPath = "assets";
                    s_createFileBuffer[0] = '\0';
                    s_openCreateFilePopup = true;
                }
                if (MenuItem("Animation File (.anim)")) {
                    s_createFileParentPath = "assets";
                    strcpy_s(s_createFileBuffer, "new_animation.anim");
                    s_openCreateFilePopup = true;
                }
                drawRegisteredAssetBrowserMenu("assets");
                ImGui::EndMenu();
            }
            EndPopup();
        }

        drawDirectoryNode("assets");
        TreePop();
    }

    // ---- Popups for File Creation and Renaming ----

    if (s_openCreateFolderPopup) {
        OpenPopup("Create Folder");
        s_openCreateFolderPopup = false;
    }
    if (BeginPopupModal("Create Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Text("Folder Name:");
        InputText("##foldername", s_createFolderBuffer, sizeof(s_createFolderBuffer));
        if (Button("Create", ImVec2(120, 0))) {
            if (s_createFolderBuffer[0] != '\0') {
                std::filesystem::path newActive = s_createFolderParentPath / s_createFolderBuffer;
                std::filesystem::create_directories(newActive);
                statusMessage = "Created folder: " + std::string(s_createFolderBuffer);
            }
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel", ImVec2(120, 0))) {
            CloseCurrentPopup();
        }
        EndPopup();
    }

    if (s_openCreateScenePopup) {
        OpenPopup("Create Scene");
        s_openCreateScenePopup = false;
    }
    if (BeginPopupModal("Create Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Text("Scene Name:");
        InputText("##scenename", s_createSceneBuffer, sizeof(s_createSceneBuffer));
        if (Button("Create", ImVec2(120, 0))) {
            if (s_createSceneBuffer[0] != '\0') {
                std::string sname = s_createSceneBuffer;
                if (sname.rfind(".json") == std::string::npos) sname += ".json";
                std::filesystem::path newActive = s_createSceneParentPath / sname;
                std::ofstream fActive(newActive);
                if (fActive.is_open()) { fActive << "[]"; fActive.close(); }
                statusMessage = "Created scene: " + sname;
            }
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel", ImVec2(120, 0))) {
            CloseCurrentPopup();
        }
        EndPopup();
    }

    if (s_openCreateFilePopup) {
        OpenPopup("Create File");
        s_openCreateFilePopup = false;
    }
    if (BeginPopupModal("Create File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Text("File Name (including extension):");
        InputText("##filename", s_createFileBuffer, sizeof(s_createFileBuffer));
        if (Button("Create", ImVec2(120, 0))) {
            if (s_createFileBuffer[0] != '\0') {
                std::filesystem::path newActive = s_createFileParentPath / s_createFileBuffer;
                auto writeNewFile = [](const std::filesystem::path& p) {
                    std::ofstream f(p, std::ios::binary);
                    if (f.is_open()) {
                        if (p.extension().string() == ".anim") {
                            char magic[4] = {'A', 'N', 'I', 'M'};
                            f.write(magic, 4);
                            uint32_t version = 2;
                            f.write(reinterpret_cast<const char*>(&version), sizeof(version));
                            uint32_t jointCount = 0;
                            f.write(reinterpret_cast<const char*>(&jointCount), sizeof(jointCount));
                            uint32_t animCount = 0;
                            f.write(reinterpret_cast<const char*>(&animCount), sizeof(animCount));
                        }
                        f.close();
                    }
                };
                writeNewFile(newActive);
                if (newActive.extension().string() == ".anim") {
                    statusMessage = "Created animation file: " + std::string(s_createFileBuffer);
                } else {
                    statusMessage = "Created empty file: " + std::string(s_createFileBuffer);
                }
            }
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel", ImVec2(120, 0))) {
            CloseCurrentPopup();
        }
        EndPopup();
    }

    if (s_openRenamePopup) {
        OpenPopup("Rename Asset");
        s_openRenamePopup = false;
    }
    if (BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Text("New Name:");
        InputText("##renamebuf", s_renameBuffer, sizeof(s_renameBuffer));
        if (Button("Rename", ImVec2(120, 0))) {
            if (s_renameBuffer[0] != '\0' && !s_renameTargetPath.empty()) {
                try {
                    std::filesystem::path parent = s_renameTargetPath.parent_path();
                    std::filesystem::path newActive = parent / s_renameBuffer;
                    std::filesystem::rename(s_renameTargetPath, newActive);
                    statusMessage = "Renamed " + s_renameTargetPath.filename().string() + " to " + s_renameBuffer;
                } catch (const std::exception& e) {
                    statusMessage = std::string("Rename failed: ") + e.what();
                }
            }
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel", ImVec2(120, 0))) {
            CloseCurrentPopup();
        }
        EndPopup();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        s_selectedAssetPaths.clear();
    }

    End();
}

void EditorUI::drawImportSettingsWindow() {
    if (s_triggerLoadImportSettings) {
        loadImportSettingsMetadata(s_importSettingsAssetPath);
        s_openImportSettingsWindow = true;
        s_triggerLoadImportSettings = false;
    }

    if (!s_openImportSettingsWindow) return;

    // Check if multiple assets are selected in Asset Browser
    std::vector<std::filesystem::path> selectedTextures;
    if (s_selectedAssetPaths.size() > 1) {
        for (const auto& p : s_selectedAssetPaths) {
            std::string pExt = p.extension().string();
            std::string lowerPExt = pExt;
            for (char& c : lowerPExt) c = (char)std::tolower((unsigned char)c);
            if (lowerPExt == ".png" || lowerPExt == ".jpg" || lowerPExt == ".jpeg" || lowerPExt == ".tga") {
                selectedTextures.push_back(p);
            }
        }
    }

    if (!selectedTextures.empty()) {
        Begin("Batch Texture Import Settings", &s_openImportSettingsWindow, ImGuiWindowFlags_AlwaysAutoResize);
        Text("Batch Selected Textures: %d files", static_cast<int>(selectedTextures.size()));
        Separator();
        drawSectionHeader("Batch Texture Filter Mode");

        static int batchFilterIdx = 0; // 0: Nearest (Point), 1: Bilinear, 2: Trilinear
        const char* filterModes[] = { "Nearest (Point)", "Bilinear", "Trilinear" };
        Combo("Filter Mode", &batchFilterIdx, filterModes, IM_ARRAYSIZE(filterModes));

        Spacing();
        Separator();
        std::string applyLabel = "Apply to All " + std::to_string(selectedTextures.size()) + " Selected Textures";
        if (Button(applyLabel.c_str())) {
            TextureFilterMode targetFilter = TextureFilterMode::Bilinear;
            if (batchFilterIdx == 0) targetFilter = TextureFilterMode::Nearest;
            else if (batchFilterIdx == 2) targetFilter = TextureFilterMode::Trilinear;

            std::string filterStr = "Bilinear";
            if (targetFilter == TextureFilterMode::Nearest) filterStr = "Nearest";
            else if (targetFilter == TextureFilterMode::Trilinear) filterStr = "Trilinear";

            int updatedCount = 0;
            for (const auto& texPath : selectedTextures) {
                std::string relativePath = texPath.generic_string() + ".meta";
                if (!texPath.parent_path().empty()) {
                    std::filesystem::create_directories(texPath.parent_path());
                }
                std::ofstream f(relativePath);
                if (f.is_open()) {
                    f << "{\n"
                      << "  \"filterMode\": \"" << filterStr << "\"\n"
                      << "}\n";
                    f.close();
                }
                renderer.resourceManager->updateTextureFilterMode(texPath.generic_string(), renderer, targetFilter);
                updatedCount++;
            }
            statusMessage = "Batch updated import settings for " + std::to_string(updatedCount) + " textures to " + filterStr + "!";
            s_openImportSettingsWindow = false;
        }
        SameLine();
        if (Button("Cancel")) {
            s_openImportSettingsWindow = false;
        }
        End();
        return;
    }

    Begin("Import Settings", &s_openImportSettingsWindow, ImGuiWindowFlags_AlwaysAutoResize);

    if (s_importSettingsAssetPath.empty()) {
        Text("No asset selected.");
        End();
        return;
    }

    std::string ext = s_importSettingsAssetPath.extension().string();
    bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga");
    if (isTexture) {
        Text("Source Texture: %s", s_importMetadata.assetPath.c_str());
        Separator();
        drawSectionHeader("Texture Import Settings");

        const char* filterModes[] = { "Nearest (Point)", "Bilinear", "Trilinear" };
        int currentFilterIdx = 1; // Bilinear
        if (s_importMetadata.filterMode == TextureFilterMode::Nearest) currentFilterIdx = 0;
        else if (s_importMetadata.filterMode == TextureFilterMode::Trilinear) currentFilterIdx = 2;

        if (Combo("Filter Mode", &currentFilterIdx, filterModes, IM_ARRAYSIZE(filterModes))) {
            if (currentFilterIdx == 0) s_importMetadata.filterMode = TextureFilterMode::Nearest;
            else if (currentFilterIdx == 2) s_importMetadata.filterMode = TextureFilterMode::Trilinear;
            else s_importMetadata.filterMode = TextureFilterMode::Bilinear;
        }

        Spacing();
        Separator();
        if (Button("Apply Settings")) {
            saveImportSettings();
            renderer.resourceManager->updateTextureFilterMode(s_importMetadata.assetPath, renderer, s_importMetadata.filterMode);
            statusMessage = "Texture import settings applied live!";
            s_openImportSettingsWindow = false;
        }
        SameLine();
        if (Button("Cancel")) {
            s_openImportSettingsWindow = false;
        }
        End();
        return;
    }

    Text("Source Asset: %s", s_importMetadata.assetPath.c_str());
    Separator();

    // 1. General Import Options
    drawSectionHeader("Import Settings");
    InputFloat("Scale Factor", &s_importMetadata.scale, 0.01f, 0.1f, "%.4f");
    Checkbox("Generate Missing Normals", &s_importMetadata.generateNormals);
    Checkbox("Allow Missing Vertex Positions", &s_importMetadata.allowMissingPos);
    Checkbox("Force In-Place (Strip Root Motion XZ)", &s_importMetadata.forceInPlace);

    // 2. Animations List & Extraction
    Spacing();
    drawSectionHeader("Animations");
    if (s_importMetadata.animations.empty()) {
        TextDisabled("No animations found in this asset.");
    } else {
        if (Button("Extract All Animations")) {
            SkeletonComponent tempSkel{};
            AnimatorComponent tempAnim{};
            if (renderer.resourceManager->loadSkeletonAndAnimations(s_importMetadata.assetPath, tempSkel, tempAnim)) {
                std::string baseName = s_importSettingsAssetPath.stem().string();
                std::string relativePath = "assets/animations/" + baseName + ".anim";
                
                std::filesystem::create_directories("assets/animations");
                bool success = renderer.resourceManager->saveBinarySkeletonAndAnimations(relativePath, tempSkel, tempAnim);
                
                if (success) {
                    statusMessage = "Extracted all animations to " + relativePath;
                } else {
                    statusMessage = "Failed to save animations.";
                }
            } else {
                statusMessage = "Failed to load animation source.";
            }
        }
        
        for (const auto& anim : s_importMetadata.animations) {
            Text("  - %s (%.2fs)", anim.name.c_str(), anim.duration);
            SameLine(320);
            PushID(anim.name.c_str());
            if (Button("Extract")) {
                SkeletonComponent tempSkel{};
                AnimatorComponent tempAnim{};
                if (renderer.resourceManager->loadSkeletonAndAnimations(s_importMetadata.assetPath, tempSkel, tempAnim)) {
                    std::vector<AnimationClip> filtered;
                    for (const auto& clip : tempAnim.animations) {
                        if (clip.name == anim.name) {
                            filtered.push_back(clip);
                        }
                    }
                    if (!filtered.empty()) {
                        tempAnim.animations = filtered;
                        std::string baseName = s_importSettingsAssetPath.stem().string();
                        std::string relativePath = "assets/animations/" + baseName + "_" + anim.name + ".anim";
                        
                        std::filesystem::create_directories("assets/animations");
                        bool success = renderer.resourceManager->saveBinarySkeletonAndAnimations(relativePath, tempSkel, tempAnim);
                        
                        if (success) {
                            statusMessage = "Extracted animation to " + relativePath;
                        } else {
                            statusMessage = "Failed to save binary animation.";
                        }
                    } else {
                        statusMessage = "Animation clip not found.";
                    }
                } else {
                    statusMessage = "Failed to load skeleton/animation source.";
                }
            }
            PopID();
        }
    }

    // 3. Embedded Textures List & Extraction
    Spacing();
    drawSectionHeader("Embedded Textures");
    if (s_importMetadata.textures.empty()) {
        TextDisabled("No embedded textures found in this asset.");
    } else {
        if (Button("Extract All Textures")) {
            ufbx_load_opts opts = { 0 };
            ufbx_error error;
            ufbx_scene* scene = ufbx_load_file(s_importMetadata.assetPath.c_str(), &opts, &error);
            if (scene) {
                int count = 0;
                for (size_t i = 0; i < scene->texture_files.count; ++i) {
                    ufbx_texture_file& tf = scene->texture_files.data[i];
                    if (tf.content.size > 0) {
                        std::string outName = std::filesystem::path(tf.filename.data ? tf.filename.data : "").filename().string();
                        if (outName.empty()) outName = "extracted_texture_" + std::to_string(i) + ".png";
                        std::string relativePath = "assets/textures/" + outName;
                        if (writeExtractedFile(relativePath, tf.content.data, tf.content.size)) {
                            count++;
                        }
                    }
                }
                statusMessage = "Extracted " + std::to_string(count) + " textures.";
                ufbx_free_scene(scene);
            } else {
                statusMessage = "Failed to open FBX scene.";
            }
        }
        
        for (const auto& tex : s_importMetadata.textures) {
            Text("  - %s (%s)", tex.name.c_str(), tex.hasEmbeddedContent ? "embedded" : "reference");
            if (tex.hasEmbeddedContent) {
                SameLine(320);
                PushID(static_cast<int>(tex.index));
                if (Button("Extract")) {
                    ufbx_load_opts opts = { 0 };
                    ufbx_error error;
                    ufbx_scene* scene = ufbx_load_file(s_importMetadata.assetPath.c_str(), &opts, &error);
                    if (scene) {
                        if (tex.index < scene->texture_files.count) {
                            ufbx_texture_file& tf = scene->texture_files.data[tex.index];
                            std::string outName = std::filesystem::path(tf.filename.data ? tf.filename.data : "").filename().string();
                            if (outName.empty()) outName = "extracted_texture_" + std::to_string(tex.index) + ".png";
                            std::string relativePath = "assets/textures/" + outName;
                            if (writeExtractedFile(relativePath, tf.content.data, tf.content.size)) {
                                statusMessage = "Extracted texture to " + relativePath;
                            } else {
                                statusMessage = "Failed to write extracted file.";
                            }
                        }
                        ufbx_free_scene(scene);
                    }
                }
                PopID();
            }
        }
    }

    Separator();
    Spacing();

    // 4. Import / Apply Button
    if (Button("Apply & Re-import", ImVec2(150, 30))) {
        saveImportSettings();
        
        renderer.resourceManager->clearMeshCache(s_importMetadata.assetPath);
        
        if (hasSelection && registry.isValid(selectedEntity)) {
            if (auto* mesh = registry.get<Mesh>(selectedEntity)) {
                if (mesh->gltfPath == s_importMetadata.assetPath) {
                    try {
                        int primCount = renderer.resourceManager->getMeshPrimitiveCount(s_importMetadata.assetPath);
                        if (primCount > 1) {
                            Mesh loaded = renderer.resourceManager->loadMesh(s_importMetadata.assetPath, renderer, 0);
                            mesh->vertices = loaded.vertices;
                            mesh->indices = loaded.indices;
                            mesh->vertexBuffer = loaded.vertexBuffer;
                            mesh->indexBuffer = loaded.indexBuffer;
                            mesh->id = loaded.id;
                        } else {
                            Mesh loaded = renderer.resourceManager->loadMesh(s_importMetadata.assetPath, renderer);
                            mesh->vertices = loaded.vertices;
                            mesh->indices = loaded.indices;
                            mesh->vertexBuffer = loaded.vertexBuffer;
                            mesh->indexBuffer = loaded.indexBuffer;
                            mesh->id = loaded.id;
                        }
                        statusMessage = "Applied import settings and re-imported active mesh!";
                    } catch (const std::exception& e) {
                        statusMessage = std::string("Re-import failed: ") + e.what();
                    }
                }
            }
        }
        s_openImportSettingsWindow = false;
    }
    SameLine();
    if (Button("Cancel", ImVec2(100, 30))) {
        s_openImportSettingsWindow = false;
    }

    End();
}

void EditorUI::drawSpriteSlicerWindow() {
    if (!s_openSpriteSlicerWindow) return;

    ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);
    
    ImGui::Begin("Sprite Sheet Slicer", &s_openSpriteSlicerWindow);

    std::string pathStr = s_spriteSlicerAssetPath.empty() ? "None (Drag & Drop texture here)" : s_spriteSlicerAssetPath.generic_string();
    
    ImGui::Button("Source Texture", ImVec2(-1, 40));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
            std::filesystem::path droppedPath = (const char*)payload->Data;
            std::string ext = droppedPath.extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
                s_spriteSlicerAssetPath = droppedPath;
                s_sliceOutputPrefix = s_spriteSlicerAssetPath.stem().string();
            }
        }
        ImGui::EndDragDropTarget();
    }
    
    if (!s_spriteSlicerAssetPath.empty()) {
        ImGui::Text("Selected: %s", pathStr.c_str());
        
        int texWidth = 0, texHeight = 0, texChannels = 0;
        if (stbi_info(s_spriteSlicerAssetPath.string().c_str(), &texWidth, &texHeight, &texChannels)) {
            ImGui::Text("Dimensions: %d x %d (%d channels)", texWidth, texHeight, texChannels);
            
            ImGui::Separator();
            drawSectionHeader("Slicing Settings");
            
            ImGui::InputInt("Cell Width", &s_sliceCellWidth);
            ImGui::InputInt("Cell Height", &s_sliceCellHeight);
                       char prefixBuf[256];
            strncpy_s(prefixBuf, s_sliceOutputPrefix.c_str(), sizeof(prefixBuf) - 1);
            if (ImGui::InputText("Output Prefix", prefixBuf, sizeof(prefixBuf))) {
                s_sliceOutputPrefix = prefixBuf;
            }
            
            ImGui::Checkbox("Skip Empty Tiles", &s_sliceSkipEmptyTiles);
            
            if (s_sliceCellWidth <= 0) s_sliceCellWidth = 16;
            if (s_sliceCellHeight <= 0) s_sliceCellHeight = 16;
            
            int cols = texWidth / s_sliceCellWidth;
            int rows = texHeight / s_sliceCellHeight;
            int totalSprites = cols * rows;
            
            ImGui::Text("This will generate up to %d textures (%d columns x %d rows)", totalSprites, cols, rows);
            
            ImGui::Spacing();
            if (totalSprites > 0) {
                if (ImGui::Button("Slice and Save Sprite Sheet", ImVec2(-1, 30))) {
                    sliceSpriteSheet(s_spriteSlicerAssetPath, s_sliceCellWidth, s_sliceCellHeight, s_sliceOutputPrefix, s_sliceSkipEmptyTiles);
                }
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Warning: Cell size is larger than texture dimensions!");
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to read texture headers.");
        }
    } else {
        ImGui::Text("Drag and drop a texture file from the Asset Browser here.");
    }
    
    ImGui::End();
}

std::vector<std::filesystem::path> EditorUI::sliceSpriteSheet(const std::filesystem::path& path, int cellWidth, int cellHeight, const std::string& prefix, bool skipEmptyTiles) {
    std::vector<std::filesystem::path> outputPaths;
    stbi_set_flip_vertically_on_load(false);
    
    int texWidth = 0, texHeight = 0, texChannels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    
    if (!pixels) {
        statusMessage = "Error: Failed to load source texture: " + path.string();
        return outputPaths;
    }
    
    if (cellWidth <= 0 || cellHeight <= 0 || cellWidth > texWidth || cellHeight > texHeight) {
        statusMessage = "Error: Invalid cell dimensions.";
        stbi_image_free(pixels);
        return outputPaths;
    }
    
    int cols = texWidth / cellWidth;
    int rows = texHeight / cellHeight;
    
    if (cols <= 0 || rows <= 0) {
        statusMessage = "Error: Cell dimensions too large.";
        stbi_image_free(pixels);
        return outputPaths;
    }
    
    std::filesystem::path parentDir = path.parent_path();
    
    int count = 0;
    std::vector<stbi_uc> cellBuffer(cellWidth * cellHeight * 4);
    
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int startX = c * cellWidth;
            int startY = r * cellHeight;
            
            for (int y = 0; y < cellHeight; ++y) {
                int sourceY = startY + y;
                int sourceX = startX;
                
                stbi_uc* sourceRow = &pixels[(sourceY * texWidth + sourceX) * 4];
                stbi_uc* destRow = &cellBuffer[y * cellWidth * 4];
                std::memcpy(destRow, sourceRow, cellWidth * 4);
            }
            
            if (skipEmptyTiles) {
                bool isNonEmpty = false;
                for (size_t i = 0; i < cellBuffer.size(); i += 4) {
                    if (cellBuffer[i + 3] != 0) {
                        isNonEmpty = true;
                        break;
                    }
                }
                if (!isNonEmpty) continue;
            }
            
            std::string outputName = prefix + "_" + std::to_string(count) + ".png";
            std::filesystem::path outputPath = parentDir / outputName;
            
            if (stbi_write_png(outputPath.string().c_str(), cellWidth, cellHeight, 4, cellBuffer.data(), cellWidth * 4)) {
                outputPaths.push_back(outputPath);
                count++;
            } else {
                std::cerr << "[SpriteSlicer] Failed to write file: " << outputPath << std::endl;
            }
        }
    }
    
    stbi_image_free(pixels);
    
    statusMessage = "Successfully sliced " + std::to_string(count) + " sprites to " + parentDir.generic_string();
    return outputPaths;
}

void EditorUI::drawSpritesheetConverterWindow() {
    if (!s_openSSConverterWindow) return;

    ImGui::SetNextWindowSize(ImVec2(550, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Convert Spritesheet to Animations", &s_openSSConverterWindow)) {
        ImGui::End();
        return;
    }

    std::string pathStr = s_ssConverterAssetPath.empty() ? "None (Drag & Drop texture asset here)" : s_ssConverterAssetPath.generic_string();

    ImGui::Text("Source Texture:");
    ImGui::Button(pathStr.c_str(), ImVec2(-1, 35));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
            std::filesystem::path droppedPath = (const char*)payload->Data;
            std::string ext = droppedPath.extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
                s_ssConverterAssetPath = droppedPath;
                s_ssOutputPrefix = s_ssConverterAssetPath.stem().string();
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (!s_ssConverterAssetPath.empty()) {
        int texWidth = 0, texHeight = 0, texChannels = 0;
        if (stbi_info(s_ssConverterAssetPath.string().c_str(), &texWidth, &texHeight, &texChannels)) {
            ImGui::Text("Dimensions: %d x %d px (%d channels)", texWidth, texHeight, texChannels);
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "1. Sprite Sheet Slicer Settings");
            ImGui::InputInt("Cell Width (px)", &s_ssCellWidth);
            ImGui::InputInt("Cell Height (px)", &s_ssCellHeight);
            ImGui::InputFloat("Playback Frame Rate (FPS)", &s_ssFrameRate, 1.0f, 5.0f, "%.1f");

            if (s_ssCellWidth <= 0) s_ssCellWidth = 16;
            if (s_ssCellHeight <= 0) s_ssCellHeight = 16;
            if (s_ssFrameRate <= 0.1f) s_ssFrameRate = 12.0f;

            int cols = texWidth / s_ssCellWidth;
            int rows = texHeight / s_ssCellHeight;
            int totalFramesAvailable = cols * rows;

            ImGui::Text("Grid: %d Cols x %d Rows -> %d Total Sliced Sprite Frames Available", cols, rows, totalFramesAvailable);

            char prefixBuf[256];
            strncpy_s(prefixBuf, s_ssOutputPrefix.c_str(), sizeof(prefixBuf) - 1);
            if (ImGui::InputText("Sprite Frame Output Prefix", prefixBuf, sizeof(prefixBuf))) {
                s_ssOutputPrefix = prefixBuf;
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "2. Animation Clips Configuration");

            int numAnims = static_cast<int>(s_ssAnimConfigs.size());
            if (ImGui::InputInt("Number of Animations", &numAnims)) {
                if (numAnims < 1) numAnims = 1;
                s_ssAnimConfigs.resize(numAnims);
            }

            // Automatically compute start frame indices continuously
            int currentFrameAccum = 0;
            for (size_t i = 0; i < s_ssAnimConfigs.size(); ++i) {
                if (s_ssAnimConfigs[i].name.empty()) {
                    s_ssAnimConfigs[i].name = "Anim_" + std::to_string(i);
                }
                s_ssAnimConfigs[i].startFrameIndex = currentFrameAccum;
                currentFrameAccum += std::max(1, s_ssAnimConfigs[i].frameCount);
            }

            ImGui::BeginChild("AnimConfigsList", ImVec2(0, 180), true);
            for (size_t i = 0; i < s_ssAnimConfigs.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                char nameBuf[128];
                strncpy_s(nameBuf, s_ssAnimConfigs[i].name.c_str(), sizeof(nameBuf) - 1);

                ImGui::Text("Anim %d:", static_cast<int>(i + 1));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("##AnimName", nameBuf, sizeof(nameBuf))) {
                    s_ssAnimConfigs[i].name = nameBuf;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                ImGui::InputInt("Frames##FrameCnt", &s_ssAnimConfigs[i].frameCount);
                if (s_ssAnimConfigs[i].frameCount < 1) s_ssAnimConfigs[i].frameCount = 1;

                ImGui::SameLine();
                int startIdx = s_ssAnimConfigs[i].startFrameIndex;
                int endIdx = startIdx + s_ssAnimConfigs[i].frameCount - 1;
                float duration = s_ssAnimConfigs[i].frameCount * (1.0f / s_ssFrameRate);
                ImGui::Text(" (Frames %d..%d, %.2fs)", startIdx, endIdx, duration);

                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::Checkbox("Combine Clips into Single .anim File", &s_ssCombineSingleFile);

            ImGui::Spacing();
            if (ImGui::Button("Slice Spritesheet and Generate .anim File(s)", ImVec2(-1, 35))) {
                convertSpritesheetToAnimations(
                    s_ssConverterAssetPath,
                    s_ssCellWidth,
                    s_ssCellHeight,
                    s_ssFrameRate,
                    s_ssAnimConfigs,
                    s_ssCombineSingleFile
                );
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to read source texture header.");
        }
    } else {
        ImGui::Text("Drag and drop a texture asset from the Asset Browser here.");
    }

    ImGui::End();
}

void EditorUI::convertSpritesheetToAnimations(
    const std::filesystem::path& spritesheetPath,
    int cellWidth,
    int cellHeight,
    float frameRate,
    const std::vector<SpritesheetAnimConfig>& animConfigs,
    bool combineIntoSingleFile
) {
    if (spritesheetPath.empty() || animConfigs.empty() || frameRate <= 0.0f) {
        statusMessage = "Error: Invalid parameters for spritesheet animation conversion.";
        return;
    }

    // Step 1: Utilize Sprite Sheet Slicer logic to slice the texture into individual PNG frames
    std::string prefix = s_ssOutputPrefix.empty() ? spritesheetPath.stem().string() : s_ssOutputPrefix;
    std::vector<std::filesystem::path> framePaths = sliceSpriteSheet(spritesheetPath, cellWidth, cellHeight, prefix, false);

    if (framePaths.empty()) {
        statusMessage = "Error: Sprite Sheet Slicer generated no frame textures.";
        return;
    }

    float frameDuration = 1.0f / frameRate;
    std::filesystem::path parentDir = spritesheetPath.parent_path();
    SkeletonComponent emptySkeleton{};

    if (combineIntoSingleFile) {
        // Create 1 combined AnimatorComponent with all AnimationClips
        AnimatorComponent animator{};
        for (const auto& config : animConfigs) {
            if (config.frameCount <= 0) continue;
            AnimationClip clip{};
            clip.name = config.name;
            clip.duration = config.frameCount * frameDuration;

            PropertyChannel channel{};
            channel.componentName = "SpriteRenderer";
            channel.fieldName = "texturePath";
            channel.type = Engine::FieldType::String;

            for (int f = 0; f < config.frameCount; ++f) {
                int frameIdx = config.startFrameIndex + f;
                if (frameIdx >= 0 && frameIdx < static_cast<int>(framePaths.size())) {
                    PropertyKeyframe key{};
                    key.time = f * frameDuration;
                    key.stringValue = framePaths[frameIdx].generic_string();
                    channel.keys.push_back(key);
                }
            }

            clip.propertyChannels.push_back(channel);
            animator.animations.push_back(clip);
        }

        if (!animator.animations.empty()) {
            std::string outFileName = prefix + ".anim";
            std::filesystem::path outFilePath = parentDir / outFileName;
            if (renderer.resourceManager->saveBinarySkeletonAndAnimations(outFilePath.generic_string(), emptySkeleton, animator)) {
                statusMessage = "Successfully generated combined animation file: " + outFileName + " (" + std::to_string(animator.animations.size()) + " clips)";
            } else {
                statusMessage = "Failed to save animation file: " + outFilePath.generic_string();
            }
        }
    } else {
        // Create separate .anim files for each animation configuration
        int generatedCount = 0;
        for (const auto& config : animConfigs) {
            if (config.frameCount <= 0) continue;
            AnimatorComponent animator{};
            AnimationClip clip{};
            clip.name = config.name;
            clip.duration = config.frameCount * frameDuration;

            PropertyChannel channel{};
            channel.componentName = "SpriteRenderer";
            channel.fieldName = "texturePath";
            channel.type = Engine::FieldType::String;

            for (int f = 0; f < config.frameCount; ++f) {
                int frameIdx = config.startFrameIndex + f;
                if (frameIdx >= 0 && frameIdx < static_cast<int>(framePaths.size())) {
                    PropertyKeyframe key{};
                    key.time = f * frameDuration;
                    key.stringValue = framePaths[frameIdx].generic_string();
                    channel.keys.push_back(key);
                }
            }

            clip.propertyChannels.push_back(channel);
            animator.animations.push_back(clip);

            std::string outFileName = prefix + "_" + config.name + ".anim";
            std::filesystem::path outFilePath = parentDir / outFileName;
            if (renderer.resourceManager->saveBinarySkeletonAndAnimations(outFilePath.generic_string(), emptySkeleton, animator)) {
                generatedCount++;
            }
        }

        statusMessage = "Successfully generated " + std::to_string(generatedCount) + " .anim animation file(s) in " + parentDir.generic_string();
    }
}

void EditorUI::drawBuildSettingsPanel() {
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 260.0f,
               ImGui::GetIO().DisplaySize.y * 0.5f - 210.0f),
        ImGuiCond_FirstUseEver
    );

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Build Settings", &showBuildSettings, flags)) {
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.75f, 0.2f, 1.0f));
    ImGui::Text("[ Build Settings ]");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("Platform");
    ImGui::PopStyleColor();
    ImGui::SameLine(120);
    ImGui::Text("Windows x64");
    ImGui::Spacing();

    ImGui::Text("Output Path");
    ImGui::SameLine(120);
    ImGui::SetNextItemWidth(260);
    static char outputBuf[512];
    strncpy_s(outputBuf, buildOutputPath.c_str(), sizeof(outputBuf) - 1);
    if (ImGui::InputText("##build_output", outputBuf, sizeof(outputBuf))) {
        buildOutputPath = outputBuf;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
    if (ImGui::Button("...##browse")) {
        // Future: open folder browser dialog
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("Included in build:");
    ImGui::PopStyleColor();
    ImGui::BulletText("game_runtime.exe -> game.exe");
    ImGui::BulletText("engine.dll");
    ImGui::BulletText("plugins/  (engine plugins)");
    ImGui::BulletText("scripts/  (compiled user script DLLs)");
    ImGui::BulletText("assets/");
    ImGui::BulletText("scenes/");
    ImGui::BulletText("shaders/");
    ImGui::BulletText("project.settings");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!buildStatusMessage.empty()) {
        bool isError = buildStatusMessage.find("[ERROR]") != std::string::npos ||
                       buildStatusMessage.find("FAIL") != std::string::npos;
        ImVec4 statusColor = isError
            ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f)
            : ImVec4(0.3f, 0.85f, 0.3f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
        ImGui::TextWrapped("%s", buildStatusMessage.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    float buttonWidth = 180.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonWidth) * 0.5f);

    if (buildInProgress) {
        ImGui::BeginDisabled();
        ImGui::Button("Building...", ImVec2(buttonWidth, 32));
        ImGui::EndDisabled();
    } else {
        // Button 1: Recompile Scripts (Green)
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.6f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f,  0.75f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.1f,  0.45f, 0.2f, 1.0f));

        if (ImGui::Button("Recompile Scripts", ImVec2(buttonWidth, 32))) {
            buildInProgress = true;
            buildStatusMessage = "Recompiling scripts...";

            int result = compileScriptsCallback ? compileScriptsCallback(".") : -1;

            if (result == 0) {
                buildStatusMessage = "[OK] Scripts compiled & reloaded successfully.";
                std::cout << "[BuildSystem] Script compilation completed successfully." << std::endl;
            } else {
                buildStatusMessage = "[ERROR] Script compilation failed (exit code " + std::to_string(result) + ")";
                std::cerr << "[BuildSystem] Script compilation failed with exit code: " << result << std::endl;
            }

            buildInProgress = false;
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonWidth) * 0.5f);

        // Button 2: Build Game (Blue)
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.45f, 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f,  0.55f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.1f,  0.35f, 0.65f, 1.0f));

        if (ImGui::Button("Build Game", ImVec2(buttonWidth, 32))) {
            buildInProgress = true;
            buildStatusMessage = "Building...";

            std::filesystem::path outPath = std::filesystem::absolute(buildOutputPath);
            int result = buildGameCallback ? buildGameCallback(".", outPath.string()) : -1;

            if (result == 0) {
                buildStatusMessage = "[OK] Build succeeded -> " + outPath.string();
                std::cout << "[BuildSystem] Build completed successfully." << std::endl;
            } else {
                buildStatusMessage = "[ERROR] Build failed (exit code " + std::to_string(result) + ")";
                std::cerr << "[BuildSystem] Build failed with exit code: " << result << std::endl;
            }

            buildInProgress = false;
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();
    ImGui::End();
}

void EditorUI::drawTilesetEditorWindow() {
    if (!s_openTilesetEditorWindow) return;

    ImGui::SetNextWindowSize(ImVec2(900, 650), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tileset Editor", &s_openTilesetEditorWindow,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

    // LEFT PANEL: Tileset file list + new tileset button
    const float listW = 190.f;
    BeginChild("##tsFileList", ImVec2(listW, 0), true);
    {
        TextDisabled("Tilesets");
        Separator();
        Spacing();

        std::filesystem::path tilesetDir = "assets/tilesets";
        if (!std::filesystem::exists(tilesetDir))
            std::filesystem::create_directories(tilesetDir);

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(tilesetDir, ec)) {
            if (entry.path().extension() != ".tileset") continue;
            std::string fname = entry.path().stem().string();
            std::string fpath = entry.path().generic_string();
            bool selected = (fpath == s_editingTilesetPath);

            PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f, 0.45f, 0.70f, 1.f));
            PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.f));
            if (Selectable(fname.c_str(), selected, 0, ImVec2(-1, 0))) {
                s_editingTilesetPath = fpath;
                Engine::invalidateTilesetCache(fpath);
                if (auto* ts = Engine::loadOrGetTileset(fpath, renderer)) {
                    s_editingTileset = *ts;
                } else {
                    s_editingTileset = Engine::TilesetAsset::loadFromFile(fpath);
                    s_editingTileset.buildAtlas(renderer);
                }
                s_tilesetLoaded      = true;
                s_tsPanOffset        = ImVec2(0.f, 0.f);

                for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                    if (s_brushTilemapEntity == tmEnt || !registry.isValid(s_brushTilemapEntity)) {
                        tm.tilesetPath = s_editingTilesetPath;
                        tm.isDirty = true;
                    }
                }
            }
            PopStyleColor(2);
        }

        Spacing(); Separator(); Spacing();

        if (Button("+ New Tileset", ImVec2(-1, 0)))
            OpenPopup("##NewTilesetPopup");

        static char s_newTsName[128] = "NewTileset";
        SetNextWindowSize(ImVec2(280, 0));
        if (BeginPopup("##NewTilesetPopup")) {
            Text("Tileset name:");
            SetNextItemWidth(-1);
            InputText("##newtsname", s_newTsName, sizeof(s_newTsName));
            Spacing();
            if (Button("Create", ImVec2(120, 0))) {
                std::string safeName = s_newTsName;
                if (safeName.empty()) safeName = "NewTileset";
                std::string newPath = (tilesetDir / (safeName + ".tileset")).generic_string();
                Engine::TilesetAsset newTs;
                newTs.name       = safeName;
                newTs.filePath   = newPath;
                newTs.tileWidth  = 16;
                newTs.tileHeight = 16;
                Engine::TilesetAsset::saveToFile(newTs);
                Engine::invalidateTilesetCache(newPath);
                if (auto* ts = Engine::loadOrGetTileset(newPath, renderer)) {
                    newTs.atlas = ts->atlas;
                }
                s_editingTilesetPath = newPath;
                s_editingTileset     = std::move(newTs);
                s_tilesetLoaded      = true;
                s_tsPanOffset        = ImVec2(0.f, 0.f);

                for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                    if (s_brushTilemapEntity == tmEnt || !registry.isValid(s_brushTilemapEntity)) {
                        tm.tilesetPath = s_editingTilesetPath;
                        tm.isDirty = true;
                    }
                }

                statusMessage = "Created tileset: " + safeName;
                CloseCurrentPopup();
            }
            SameLine();
            if (Button("Cancel", ImVec2(120, 0))) CloseCurrentPopup();
            EndPopup();
        }

        // Separator + tileset settings if one is loaded
        if (s_tilesetLoaded) {
            if (!s_editingTileset.atlas.atlasBuilt) {
                if (!s_editingTilesetPath.empty()) {
                    Engine::invalidateTilesetCache(s_editingTilesetPath);
                    if (auto* ts = Engine::loadOrGetTileset(s_editingTilesetPath, renderer)) {
                        s_editingTileset = *ts;
                    } else {
                        s_editingTileset.buildAtlas(renderer);
                    }
                } else {
                    s_editingTileset.buildAtlas(renderer);
                }
            }

            Spacing(); Separator(); Spacing();
            TextDisabled("Settings");

            char nameBuf[128] = {};
            strncpy_s(nameBuf, s_editingTileset.name.c_str(), sizeof(nameBuf) - 1);
            SetNextItemWidth(-1);
            if (InputText("##tsname", nameBuf, sizeof(nameBuf)))
                s_editingTileset.name = nameBuf;

            TextDisabled("Tile W/H (px)");
            SetNextItemWidth(-1);
            DragInt("##tsTW", &s_editingTileset.tileWidth,  1.f, 1, 512, "W: %d px");
            SetNextItemWidth(-1);
            DragInt("##tsTH", &s_editingTileset.tileHeight, 1.f, 1, 512, "H: %d px");
            if (s_editingTileset.tileWidth  < 1) s_editingTileset.tileWidth  = 1;
            if (s_editingTileset.tileHeight < 1) s_editingTileset.tileHeight = 1;

            if (s_brushTileId >= 0 && s_brushTileId < static_cast<int>(s_editingTileset.tiles.size())) {
                auto& curTile = s_editingTileset.tiles[s_brushTileId];
                Spacing(); Separator(); Spacing();
                TextDisabled("Tile: %s", curTile.name.c_str());
                if (ColorEdit4("Color Tint##selTileColor", &curTile.colorTint.r)) {
                    std::filesystem::path tsDir = std::filesystem::path(s_editingTileset.filePath).parent_path();
                    std::string tilePath = (tsDir / s_editingTileset.name / (curTile.name + ".tile")).generic_string();
                    Engine::TilesetAsset::saveTileFile(curTile, tilePath);
                    Engine::TilesetAsset::saveToFile(s_editingTileset);
                    Engine::invalidateTilesetCache(s_editingTilesetPath);
                    if (auto* ts = Engine::loadOrGetTileset(s_editingTilesetPath, renderer)) {
                        s_editingTileset = *ts;
                        for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                            if (tm.tilesetPath == s_editingTilesetPath) {
                                tm.isDirty = true;
                                if (auto* mat = registry.get<Material>(tmEnt)) {
                                    mat->descriptorSet = ts->atlas.descriptorSet;
                                }
                            }
                        }
                    }
                }
            }

            Spacing();
            PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.48f, 0.22f, 1.f));
            PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.60f, 0.28f, 1.f));
            if (Button("Save Tileset", ImVec2(-1, 0))) {
                Engine::TilesetAsset::saveToFile(s_editingTileset);
                std::filesystem::path tsDir = std::filesystem::path(s_editingTileset.filePath).parent_path();
                std::filesystem::path tileSubDir = tsDir / s_editingTileset.name;
                std::filesystem::create_directories(tileSubDir);
                for (auto& tile : s_editingTileset.tiles) {
                    std::string tilePath = (tileSubDir / (tile.name + ".tile")).generic_string();
                    Engine::TilesetAsset::saveTileFile(tile, tilePath);
                }
                Engine::invalidateTilesetCache(s_editingTilesetPath);
                if (auto* ts = Engine::loadOrGetTileset(s_editingTilesetPath, renderer)) {
                    s_editingTileset = *ts;
                    for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                        if (tm.tilesetPath == s_editingTilesetPath) {
                            tm.isDirty = true;
                            if (auto* mat = registry.get<Material>(tmEnt)) {
                                mat->descriptorSet = ts->atlas.descriptorSet;
                            }
                        }
                    }
                }
                statusMessage = "Saved tileset: " + s_editingTileset.name;
            }
            PopStyleColor(2);
        }
    }
    EndChild();

    SameLine();

    // RIGHT PANEL: Infinite grid palette canvas
    BeginChild("##tsGrid", ImVec2(0, 0), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (!s_tilesetLoaded) {
        ImVec2 sz = GetContentRegionAvail();
        ImVec2 cp = GetCursorScreenPos();
        GetWindowDrawList()->AddRectFilled(cp, ImVec2(cp.x+sz.x, cp.y+sz.y), IM_COL32(28,28,35,255));
        ImVec2 tc = ImVec2(cp.x + sz.x*0.5f - 180.f, cp.y + sz.y*0.5f - 10.f);
        GetWindowDrawList()->AddText(nullptr, 16.f, tc, IM_COL32(100,100,120,200),
            "Select or create a tileset on the left.");
    } else {
        // Build a map from (gridX,gridY) -> tile index for fast lookup
        std::unordered_map<uint64_t, int> cellMap;
        auto cellKey = [](int gx, int gy) -> uint64_t {
            return ((uint64_t)(uint32_t)gx) | (((uint64_t)(uint32_t)gy) << 32);
        };
        for (int i = 0; i < (int)s_editingTileset.tiles.size(); ++i) {
            auto& t = s_editingTileset.tiles[i];
            cellMap[cellKey(t.gridX, t.gridY)] = i;
        }

        // Canvas region
        ImVec2 canvasPos  = GetCursorScreenPos();
        ImVec2 canvasSize = GetContentRegionAvail();
        if (canvasSize.x < 10.f) canvasSize.x = 10.f;
        if (canvasSize.y < 10.f) canvasSize.y = 10.f;

        // Invisible button to capture mouse events
        InvisibleButton("##tsCanvas", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft  |
            ImGuiButtonFlags_MouseButtonRight |
            ImGuiButtonFlags_MouseButtonMiddle);
        const bool canvasHovered = IsItemHovered();
        const bool canvasActive  = IsItemActive();

        ImVec2 mousePos = GetIO().MousePos;

        // --- Zoom (scroll wheel) ---
        if (canvasHovered) {
            float wheel = GetIO().MouseWheel;
            if (wheel != 0.f) {
                float zoomFactor = (wheel > 0) ? 1.12f : (1.f / 1.12f);
                float newCell = s_tsCellSize * zoomFactor;
                newCell = std::max(12.f, std::min(256.f, newCell));
                ImVec2 mouseInCanvas = ImVec2(mousePos.x - canvasPos.x, mousePos.y - canvasPos.y);
                float scale = newCell / s_tsCellSize;
                s_tsPanOffset.x = mouseInCanvas.x - scale * (mouseInCanvas.x - s_tsPanOffset.x);
                s_tsPanOffset.y = mouseInCanvas.y - scale * (mouseInCanvas.y - s_tsPanOffset.y);
                s_tsCellSize = newCell;
            }
        }

        // --- Pan (middle mouse or right mouse drag) ---
        bool wantPan = canvasActive && (
            IsMouseDown(ImGuiMouseButton_Middle) ||
            (IsMouseDown(ImGuiMouseButton_Right) && !IsAnyItemHovered()));

        if (wantPan && !s_tsIsPanning) {
            s_tsIsPanning = true;
            s_tsPanStart  = mousePos;
            s_tsPanOffsetStart = s_tsPanOffset;
        }
        if (!IsMouseDown(ImGuiMouseButton_Middle) && !IsMouseDown(ImGuiMouseButton_Right))
            s_tsIsPanning = false;

        if (s_tsIsPanning) {
            s_tsPanOffset.x = s_tsPanOffsetStart.x + (mousePos.x - s_tsPanStart.x);
            s_tsPanOffset.y = s_tsPanOffsetStart.y + (mousePos.y - s_tsPanStart.y);
        }

        // Draw grid lines and cells
        ImDrawList* dl = GetWindowDrawList();
        dl->PushClipRect(canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        // Dark background
        dl->AddRectFilled(canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
            IM_COL32(28, 28, 36, 255));

        const float cs = s_tsCellSize;
        const float ox = canvasPos.x + s_tsPanOffset.x;
        const float oy = canvasPos.y + s_tsPanOffset.y;

        // Determine visible cell range
        int colMin = (int)std::floor((canvasPos.x - ox) / cs) - 1;
        int colMax = (int)std::ceil ((canvasPos.x + canvasSize.x - ox) / cs) + 1;
        int rowMin = (int)std::floor((canvasPos.y - oy) / cs) - 1;
        int rowMax = (int)std::ceil ((canvasPos.y + canvasSize.y - oy) / cs) + 1;

        const int MAX_RANGE = 64;
        if (colMax - colMin > MAX_RANGE) { colMin = -MAX_RANGE/2; colMax = MAX_RANGE/2; }
        if (rowMax - rowMin > MAX_RANGE) { rowMin = -MAX_RANGE/2; rowMax = MAX_RANGE/2; }

        // Grid lines
        ImU32 gridLineCol   = IM_COL32(55, 55, 70, 200);
        ImU32 originLineCol = IM_COL32(80, 80, 110, 255);
        for (int col = colMin; col <= colMax; ++col) {
            float x = ox + col * cs;
            ImU32 c = (col == 0) ? originLineCol : gridLineCol;
            dl->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y), c, col == 0 ? 2.f : 1.f);
        }
        for (int row = rowMin; row <= rowMax; ++row) {
            float y = oy + row * cs;
            ImU32 c = (row == 0) ? originLineCol : gridLineCol;
            dl->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y), c, row == 0 ? 2.f : 1.f);
        }

        // Coordinate labels on empty cells (only when zoomed in enough)
        if (cs >= 48.f) {
            for (int col = colMin; col <= colMax; ++col) {
                for (int row = rowMin; row <= rowMax; ++row) {
                    auto it = cellMap.find(cellKey(col, row));
                    if (it == cellMap.end()) {
                        float x = ox + col * cs;
                        float y = oy + row * cs;
                        char lbl[32];
                        snprintf(lbl, sizeof(lbl), "%d,%d", col, row);
                        dl->AddText(ImVec2(x+3.f, y+3.f), IM_COL32(60,60,80,180), lbl);
                    }
                }
            }
        }

        // Draw placed tiles
        static int s_rightClickedTileIdx = -1;
        for (auto& [key, tileIdx] : cellMap) {
            if (tileIdx < 0 || tileIdx >= (int)s_editingTileset.tiles.size()) continue;
            auto& tile = s_editingTileset.tiles[tileIdx];
            float x = ox + tile.gridX * cs;
            float y = oy + tile.gridY * cs;
            ImVec2 tl = ImVec2(x, y);
            ImVec2 br = ImVec2(x + cs, y + cs);
            bool isSelected = (s_brushTileId == tileIdx);

            dl->AddRectFilled(tl, br,
                isSelected ? IM_COL32(30, 90, 180, 200) : IM_COL32(40, 40, 55, 220));

            // Texture thumbnail (uses packed atlas singleDescriptorSet for 1-draw-call lag-free rendering)
            if (s_editingTileset.atlas.valid && s_editingTileset.atlas.singleDescriptorSet != VK_NULL_HANDLE) {
                ImVec2 uv0(tile.atlasUV.x, tile.atlasUV.y);
                ImVec2 uv1(tile.atlasUV.z, tile.atlasUV.w);
                dl->AddImage((ImTextureID)s_editingTileset.atlas.singleDescriptorSet, tl, br, uv0, uv1);
            } else if (!tile.texturePath.empty()) {
                Texture* tex = renderer.resourceManager->loadTexture(tile.texturePath, renderer);
                if (tex && tex->singleDescriptorSet != VK_NULL_HANDLE) {
                    dl->AddImage((ImTextureID)tex->singleDescriptorSet, tl, br);
                }
            }

            // Solid tint
            if (tile.isSolid)
                dl->AddRectFilled(tl, br, IM_COL32(220, 40, 40, 70));

            // Border
            ImU32 borderCol = isSelected ? IM_COL32(80, 160, 255, 255) : IM_COL32(110, 110, 140, 200);
            dl->AddRect(tl, br, borderCol, 0.f, 0, isSelected ? 2.5f : 1.5f);

            // Tile name label strip
            if (cs >= 36.f) {
                float labelH = std::min(14.f, cs * 0.20f);
                ImVec2 lblTL = ImVec2(tl.x, br.y - labelH);
                dl->AddRectFilled(lblTL, br, IM_COL32(0, 0, 0, 160));
                dl->AddText(ImVec2(lblTL.x + 2.f, lblTL.y),
                    IM_COL32(220, 220, 220, 255), tile.name.c_str());
            }
        }

        // --- Mouse interaction ---
        ImVec2 mouseInCanvas = ImVec2(mousePos.x - canvasPos.x, mousePos.y - canvasPos.y);
        int hovCol = (int)std::floor((mouseInCanvas.x - s_tsPanOffset.x) / cs);
        int hovRow = (int)std::floor((mouseInCanvas.y - s_tsPanOffset.y) / cs);

        // Hover highlight
        if (canvasHovered && !s_tsIsPanning) {
            float hx = ox + hovCol * cs;
            float hy = oy + hovRow * cs;
            dl->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + cs, hy + cs), IM_COL32(255, 255, 255, 18));
            dl->AddRect(ImVec2(hx, hy), ImVec2(hx + cs, hy + cs), IM_COL32(180, 180, 200, 120));

            // Tooltip
            auto it = cellMap.find(cellKey(hovCol, hovRow));
            BeginTooltip();
            if (it != cellMap.end() && it->second >= 0 && it->second < (int)s_editingTileset.tiles.size()) {
                auto& t = s_editingTileset.tiles[it->second];
                Text("[%d, %d]  ID=%d  %s", hovCol, hovRow, t.id, t.name.c_str());
                Text("Texture: %s", t.texturePath.c_str());
                Text(t.isSolid ? "Solid: YES" : "Solid: NO");
                Text("LMB=Select brush  RMB=Options");
            } else {
                Text("[%d, %d]  (empty)", hovCol, hovRow);
                Text("Drag a texture here to place a tile");
            }
            EndTooltip();
        }

        // Left-click: select/deselect brush
        if (canvasHovered && IsMouseClicked(ImGuiMouseButton_Left) && !s_tsIsPanning) {
            auto it = cellMap.find(cellKey(hovCol, hovRow));
            if (it != cellMap.end() && it->second >= 0 && it->second < (int)s_editingTileset.tiles.size()) {
                int idx = it->second;
                if (s_brushTileId == idx) {
                    s_brushTileId = -1;
                    s_brushModeActive = false;
                    statusMessage = "Brush cleared.";
                } else {
                    s_brushTileId = idx;
                    s_brushModeActive = true;
                    statusMessage = "Brush: " + s_editingTileset.tiles[idx].name;
                }
            }
        }

        // Right-click context menu
        if (canvasHovered && IsMouseClicked(ImGuiMouseButton_Right) && !s_tsIsPanning) {
            auto it = cellMap.find(cellKey(hovCol, hovRow));
            if (it != cellMap.end()) {
                s_rightClickedTileIdx = it->second;
                OpenPopup("##TileCtxMenu");
            }
        }
        if (BeginPopup("##TileCtxMenu")) {
            int idx = s_rightClickedTileIdx;
            if (idx >= 0 && idx < (int)s_editingTileset.tiles.size()) {
                auto& tile = s_editingTileset.tiles[idx];
                TextDisabled("%s  [%d,%d]", tile.name.c_str(), tile.gridX, tile.gridY);
                Separator();
                bool solid = tile.isSolid;
                if (Checkbox("Solid Collider", &solid)) {
                    tile.isSolid = solid;
                    std::filesystem::path tsDir = std::filesystem::path(s_editingTileset.filePath).parent_path();
                    std::string tilePath = (tsDir / s_editingTileset.name / (tile.name + ".tile")).generic_string();
                    Engine::TilesetAsset::saveTileFile(tile, tilePath);
                    Engine::TilesetAsset::saveToFile(s_editingTileset);
                    Engine::invalidateTilesetCache(s_editingTilesetPath);
                    if (auto* ts = Engine::loadOrGetTileset(s_editingTilesetPath, renderer)) {
                        s_editingTileset = *ts;
                        for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                            if (tm.tilesetPath == s_editingTilesetPath) {
                                tm.isDirty = true;
                                if (auto* mat = registry.get<Material>(tmEnt)) {
                                    mat->descriptorSet = ts->atlas.descriptorSet;
                                }
                            }
                        }
                    }
                }
                if (ImGui::ColorEdit4("Tile Color Tint", &tile.colorTint.r)) {
                    std::filesystem::path tsDir = std::filesystem::path(s_editingTileset.filePath).parent_path();
                    std::string tilePath = (tsDir / s_editingTileset.name / (tile.name + ".tile")).generic_string();
                    Engine::TilesetAsset::saveTileFile(tile, tilePath);
                    Engine::TilesetAsset::saveToFile(s_editingTileset);
                    Engine::invalidateTilesetCache(s_editingTilesetPath);
                    if (auto* ts = Engine::loadOrGetTileset(s_editingTilesetPath, renderer)) {
                        s_editingTileset = *ts;
                        for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                            if (tm.tilesetPath == s_editingTilesetPath) {
                                tm.isDirty = true;
                                if (auto* mat = registry.get<Material>(tmEnt)) {
                                    mat->descriptorSet = ts->atlas.descriptorSet;
                                }
                            }
                        }
                    }
                }
                Separator();
                PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
                if (MenuItem("Remove Tile")) {
                    if (s_brushTileId == idx)       { s_brushTileId = -1; s_brushModeActive = false; }
                    else if (s_brushTileId > idx)   { s_brushTileId--; }
                    s_editingTileset.tiles.erase(s_editingTileset.tiles.begin() + idx);
                    for (int ti = 0; ti < (int)s_editingTileset.tiles.size(); ++ti)
                        s_editingTileset.tiles[ti].id = ti;
                    Engine::TilesetAsset::saveToFile(s_editingTileset);
                    Engine::invalidateTilesetCache(s_editingTilesetPath);
                    if (auto* ts = Engine::loadOrGetTileset(s_editingTilesetPath, renderer)) {
                        s_editingTileset.atlas = ts->atlas;
                        for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                            if (tm.tilesetPath == s_editingTilesetPath) {
                                tm.isDirty = true;
                                if (auto* mat = registry.get<Material>(tmEnt)) {
                                    mat->descriptorSet = ts->atlas.descriptorSet;
                                }
                            }
                        }
                    }
                    statusMessage = "Removed tile.";
                    s_rightClickedTileIdx = -1;
                }
                PopStyleColor();
            }
            EndPopup();
        }

        // --- Drag-drop: accept texture files onto the grid ---
        SetCursorScreenPos(canvasPos);
        InvisibleButton("##tsDropTarget", canvasSize);
        if (BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS")) {
                if (payload->Data && payload->DataSize > 0 && !s_editingTileset.filePath.empty()) {
                    std::string pathsStr((const char*)payload->Data);
                    std::stringstream ss(pathsStr);
                    std::string droppedPath;
                    std::vector<std::string> imagesToProcess;
                    while (std::getline(ss, droppedPath, '|')) {
                        auto ext = std::filesystem::path(droppedPath).extension().string();
                        bool isImg = (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".tga");
                        if (isImg) {
                            imagesToProcess.push_back(droppedPath);
                        }
                    }

                    if (!imagesToProcess.empty()) {
                        ImVec2 dropMouse     = GetIO().MousePos;
                        ImVec2 dropInCanvas  = ImVec2(dropMouse.x - canvasPos.x, dropMouse.y - canvasPos.y);
                        int startCol = (int)std::floor((dropInCanvas.x - s_tsPanOffset.x) / cs);
                        int startRow = (int)std::floor((dropInCanvas.y - s_tsPanOffset.y) / cs);

                        int colOffset = 0;
                        for (const auto& path : imagesToProcess) {
                            int targetCol = startCol + colOffset;
                            int targetRow = startRow;
                            
                            uint64_t key = cellKey(targetCol, targetRow);
                            auto it = cellMap.find(key);
                            if (it != cellMap.end() && it->second >= 0 && it->second < (int)s_editingTileset.tiles.size()) {
                                // Overwrite existing tile's texture
                                auto& existing   = s_editingTileset.tiles[it->second];
                                existing.texturePath = path;
                                existing.name        = std::filesystem::path(path).stem().string();
                                std::filesystem::path tsDir = std::filesystem::path(s_editingTileset.filePath).parent_path();
                                std::string tilePath = (tsDir / s_editingTileset.name / (existing.name + ".tile")).generic_string();
                                Engine::TilesetAsset::saveTileFile(existing, tilePath);
                            } else {
                                // New tile at this position
                                Engine::TileAsset newTile;
                                newTile.id          = static_cast<int>(s_editingTileset.tiles.size());
                                newTile.name        = std::filesystem::path(path).stem().string();
                                newTile.texturePath = path;
                                newTile.isSolid     = false;
                                newTile.gridX       = targetCol;
                                newTile.gridY       = targetRow;

                                std::filesystem::path tsDir = std::filesystem::path(s_editingTileset.filePath).parent_path();
                                std::filesystem::path tileSubDir = tsDir / s_editingTileset.name;
                                std::filesystem::create_directories(tileSubDir);
                                std::string tilePath = (tileSubDir / (newTile.name + ".tile")).generic_string();
                                Engine::TilesetAsset::saveTileFile(newTile, tilePath);
                                
                                cellMap[key] = newTile.id;
                                s_editingTileset.tiles.push_back(std::move(newTile));
                            }
                            colOffset++;
                        }

                        Engine::TilesetAsset::saveToFile(s_editingTileset);
                        Engine::invalidateTilesetCache(s_editingTilesetPath);
                        if (auto* ts = Engine::loadOrGetTileset(s_editingTilesetPath, renderer)) {
                            s_editingTileset = *ts;
                            for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                                if (tm.tilesetPath == s_editingTilesetPath) {
                                    tm.isDirty = true;
                                    if (auto* mat = registry.get<Material>(tmEnt)) {
                                        mat->descriptorSet = ts->atlas.descriptorSet;
                                    }
                                }
                            }
                        }
                        statusMessage = "Added/updated " + std::to_string(imagesToProcess.size()) + " tiles.";
                    }
                }
            } else if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
                if (payload->Data && payload->DataSize > 0 && !s_editingTileset.filePath.empty()) {
                    std::string droppedPath = (const char*)payload->Data;
                    auto ext = std::filesystem::path(droppedPath).extension().string();
                    bool isImg = (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".tga");
                    if (isImg) {
                        ImVec2 dropMouse     = GetIO().MousePos;
                        ImVec2 dropInCanvas  = ImVec2(dropMouse.x - canvasPos.x, dropMouse.y - canvasPos.y);
                        int dropCol = (int)std::floor((dropInCanvas.x - s_tsPanOffset.x) / cs);
                        int dropRow = (int)std::floor((dropInCanvas.y - s_tsPanOffset.y) / cs);

                        uint64_t key = cellKey(dropCol, dropRow);
                        auto it = cellMap.find(key);
                        if (it != cellMap.end() && it->second >= 0 && it->second < (int)s_editingTileset.tiles.size()) {
                            // Overwrite existing tile's texture
                            auto& existing   = s_editingTileset.tiles[it->second];
                            existing.texturePath = droppedPath;
                            existing.name        = std::filesystem::path(droppedPath).stem().string();
                            std::filesystem::path tsDir = std::filesystem::path(s_editingTileset.filePath).parent_path();
                            std::string tilePath = (tsDir / s_editingTileset.name / (existing.name + ".tile")).generic_string();
                            Engine::TilesetAsset::saveTileFile(existing, tilePath);
                            statusMessage = "Replaced tile at [" + std::to_string(dropCol) + "," + std::to_string(dropRow) + "]";
                        } else {
                            // New tile at this position
                            Engine::TileAsset newTile;
                            newTile.id          = static_cast<int>(s_editingTileset.tiles.size());
                            newTile.name        = std::filesystem::path(droppedPath).stem().string();
                            newTile.texturePath = droppedPath;
                            newTile.isSolid     = false;
                            newTile.gridX       = dropCol;
                            newTile.gridY       = dropRow;

                            std::filesystem::path tsDir = std::filesystem::path(s_editingTileset.filePath).parent_path();
                            std::filesystem::path tileSubDir = tsDir / s_editingTileset.name;
                            std::filesystem::create_directories(tileSubDir);
                            std::string tilePath = (tileSubDir / (newTile.name + ".tile")).generic_string();
                            Engine::TilesetAsset::saveTileFile(newTile, tilePath);
                            
                            cellMap[key] = newTile.id;
                            s_editingTileset.tiles.push_back(std::move(newTile));
                            statusMessage = "Added tile at [" + std::to_string(dropCol) + "," + std::to_string(dropRow) + "]";
                        }

                        Engine::TilesetAsset::saveToFile(s_editingTileset);
                        Engine::invalidateTilesetCache(s_editingTilesetPath);
                        if (auto* ts = Engine::loadOrGetTileset(s_editingTilesetPath, renderer)) {
                            s_editingTileset = *ts;
                            for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                                if (tm.tilesetPath == s_editingTilesetPath) {
                                    tm.isDirty = true;
                                    if (auto* mat = registry.get<Material>(tmEnt)) {
                                        mat->descriptorSet = ts->atlas.descriptorSet;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            EndDragDropTarget();
        }

        dl->PopClipRect();

        // --- Bottom HUD bar ---
        {
            const float barH = 22.f;
            ImVec2 barTL = ImVec2(canvasPos.x, canvasPos.y + canvasSize.y - barH);
            ImVec2 barBR = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
            GetWindowDrawList()->AddRectFilled(barTL, barBR, IM_COL32(20, 20, 28, 220));

            char hudBuf[256];
            if (s_brushTileId >= 0 && s_brushTileId < (int)s_editingTileset.tiles.size()) {
                auto& bt = s_editingTileset.tiles[s_brushTileId];
                snprintf(hudBuf, sizeof(hudBuf),
                    "  Brush: [%d] %s  |  Zoom: %.0fpx  |  Scroll=Zoom  MMB/RMB=Pan  LMB=Select  RMB=Options",
                    s_brushTileId, bt.name.c_str(), cs);
            } else {
                snprintf(hudBuf, sizeof(hudBuf),
                    "  No brush selected  |  Zoom: %.0fpx  |  Scroll=Zoom  MMB/RMB=Pan  LMB=Select  Drag texture=Place",
                    cs);
            }
            GetWindowDrawList()->AddText(
                ImVec2(barTL.x + 4.f, barTL.y + 3.f),
                s_brushTileId >= 0 ? IM_COL32(100, 200, 255, 255) : IM_COL32(140, 140, 160, 220),
                hudBuf);
        }

        // --- Paint target dropdown (top-right corner overlay) ---
        if (s_tilesetLoaded) {
            const float comboW = 190.f;
            const float comboH = 26.f;
            SetCursorScreenPos(ImVec2(canvasPos.x + canvasSize.x - comboW - 4.f, canvasPos.y + 4.f));
            if (BeginChild("##tmTargetOverlay", ImVec2(comboW, comboH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground)) {
                PushItemWidth(comboW);
                std::string previewLabel = "Select Tilemap...";
                for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                    if (s_brushTilemapEntity == tmEnt) {
                        if (auto* n = registry.get<Name>(tmEnt)) previewLabel = n->value;
                        else previewLabel = "Entity " + std::to_string(tmEnt.getId());
                        break;
                    }
                }
                if (BeginCombo("##tmTarget", previewLabel.c_str(), ImGuiComboFlags_HeightSmall)) {
                    for (auto [tmEnt, tm] : registry.view<Engine::TilemapComponent>()) {
                        std::string lbl;
                        if (auto* n = registry.get<Name>(tmEnt)) lbl = n->value;
                        else lbl = "Entity " + std::to_string(tmEnt.getId());
                        bool sel = (s_brushTilemapEntity == tmEnt);
                        if (Selectable(lbl.c_str(), sel)) {
                            s_brushTilemapEntity = tmEnt;
                            selectedEntity = tmEnt;
                            hasSelection = true;
                        }
                    }
                    EndCombo();
                }
                PopItemWidth();
            }
            EndChild();
        }
    }
    EndChild();

    End();
}

void EditorUI::drawAnimationEditorWindow() {
    if (!s_openAnimationEditorWindow) return;

    ImGui::SetNextWindowSize(ImVec2(800, 450), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Editor", &s_openAnimationEditorWindow)) {
        ImGui::End();
        return;
    }

    if (!hasSelection || !registry.isValid(selectedEntity)) {
        ImGui::Text("Select an Entity with an AnimatorComponent to edit animations.");
        ImGui::End();
        return;
    }

    // Check if the entity has an AnimatorComponent
    AnimatorComponent* animator = registry.get<AnimatorComponent>(selectedEntity);
    if (!animator) {
        ImGui::Text("The selected entity does not have an AnimatorComponent.");
        if (ImGui::Button("Add Animator Component")) {
            registry.emplace<AnimatorComponent>(selectedEntity, AnimatorComponent{});
        }
        ImGui::End();
        return;
    }

    // Active Clip dropdown
    static char s_newClipName[128] = "New Clip";
    AnimationClip* activeClip = nullptr;
    if (animator->activeAnimationIndex >= 0 && animator->activeAnimationIndex < static_cast<int>(animator->animations.size())) {
        activeClip = &animator->animations[animator->activeAnimationIndex];
    }

    // Toolbar Row
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Active Clip:");
    ImGui::SameLine();
    ImGui::PushItemWidth(200);
    
    std::string previewLabel = activeClip ? activeClip->name : "None";
    if (ImGui::BeginCombo("##active_clip", previewLabel.c_str())) {
        for (int i = 0; i < static_cast<int>(animator->animations.size()); ++i) {
            bool isSelected = (i == animator->activeAnimationIndex);
            if (ImGui::Selectable(animator->animations[i].name.c_str(), isSelected)) {
                animator->activeAnimationIndex = i;
                animator->currentTime = 0.0f;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Create New Clip")) {
        ImGui::OpenPopup("CreateNewClipPopup");
    }

    // Modal popup to create new clip
    if (ImGui::BeginPopup("CreateNewClipPopup")) {
        ImGui::Text("Clip Name:");
        ImGui::InputText("##clip_name_input", s_newClipName, sizeof(s_newClipName));
        if (ImGui::Button("Create##btn")) {
            AnimationClip newClip;
            newClip.name = s_newClipName;
            newClip.duration = 2.0f; // Default 2 seconds
            animator->animations.push_back(newClip);
            animator->activeAnimationIndex = animator->animations.size() - 1;
            animator->currentTime = 0.0f;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    static char s_saveLoadPath[256] = "assets/animations/cube.anim";
    if (!animator->loadedAnimPath.empty() && strcmp(s_saveLoadPath, animator->loadedAnimPath.c_str()) != 0) {
        strcpy_s(s_saveLoadPath, animator->loadedAnimPath.c_str());
    }
    ImGui::PushItemWidth(200);
    ImGui::InputText("Path##save_load", s_saveLoadPath, sizeof(s_saveLoadPath));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        SkeletonComponent dummySkeleton;
        if (auto* skel = registry.get<SkeletonComponent>(selectedEntity)) {
            renderer.resourceManager->saveBinarySkeletonAndAnimations(s_saveLoadPath, *skel, *animator);
        } else {
            renderer.resourceManager->saveBinarySkeletonAndAnimations(s_saveLoadPath, dummySkeleton, *animator);
        }
        statusMessage = "Saved animations to: " + std::string(s_saveLoadPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        SkeletonComponent dummySkeleton;
        SkeletonComponent* skelPtr = &dummySkeleton;
        if (auto* skel = registry.get<SkeletonComponent>(selectedEntity)) {
            skelPtr = skel;
        }
        if (renderer.resourceManager->loadBinarySkeletonAndAnimations(s_saveLoadPath, *skelPtr, *animator, false)) {
            statusMessage = "Loaded animations from: " + std::string(s_saveLoadPath);
            animator->loadedAnimPath = s_saveLoadPath;
        } else {
            statusMessage = "Failed to load animations from: " + std::string(s_saveLoadPath);
        }
    }

    if (!activeClip) {
        ImGui::TextDisabled("No active animation clip selected. Create or load one to begin animating.");
        ImGui::End();
        return;
    }

    // Timeline settings and playback
    ImGui::Separator();
    
    // Play/Pause buttons
    bool isPlaying = (animator->playbackSpeed > 0.0f);
    if (ImGui::Button(isPlaying ? "Pause" : "Play")) {
        if (isPlaying) {
            animator->playbackSpeed = 0.0f;
        } else {
            animator->playbackSpeed = 1.0f;
            animator->isPreviewing = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        animator->playbackSpeed = 0.0f;
        animator->currentTime = 0.0f;
        if (auto* ctrl = registry.get<AnimationControllerComponent>(selectedEntity)) {
            ctrl->currentStateTime = 0.0f;
        }
    }
    
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Time:");
    ImGui::SameLine();
    ImGui::PushItemWidth(80);
    if (ImGui::DragFloat("##current_time", &animator->currentTime, 0.01f, 0.0f, activeClip->duration, "%.2fs")) {
        if (auto* ctrl = registry.get<AnimationControllerComponent>(selectedEntity)) {
            ctrl->currentStateTime = animator->currentTime;
        }
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Duration:");
    ImGui::SameLine();
    ImGui::PushItemWidth(80);
    ImGui::DragFloat("##duration_input", &activeClip->duration, 0.1f, 0.1f, 100.0f, "%.1fs");
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::Checkbox("Loop", &animator->loop);

    ImGui::SameLine();
    if (ImGui::Button("Add Property")) {
        ImGui::OpenPopup("AddPropertyPopup");
    }

    if (ImGui::BeginPopup("AddPropertyPopup")) {
        auto& reflReg = Engine::ComponentReflectionRegistry::getInstance();
        for (const auto& refl : reflReg.getReflections()) {
            if (refl.has(registry, selectedEntity)) {
                if (ImGui::BeginMenu(refl.name.c_str())) {
                    for (const auto& field : refl.fields) {
                        if (field.type == Engine::FieldType::Float ||
                            field.type == Engine::FieldType::Int ||
                            field.type == Engine::FieldType::Bool ||
                            field.type == Engine::FieldType::String ||
                            field.type == Engine::FieldType::Vec2 ||
                            field.type == Engine::FieldType::Vec3 ||
                            field.type == Engine::FieldType::Vec4) {
                            
                            bool exists = false;
                            for (const auto& chan : activeClip->propertyChannels) {
                                if (chan.componentName == refl.name && chan.fieldName == field.name) {
                                    exists = true;
                                    break;
                                }
                            }

                            if (!exists) {
                                if (ImGui::MenuItem(field.name.c_str())) {
                                    PropertyChannel newChan;
                                    newChan.componentName = refl.name;
                                    newChan.fieldName = field.name;
                                    newChan.type = field.type;

                                    // Automatically capture current field value as initial keyframe at 0.0s
                                    PropertyKeyframe initKey;
                                    initKey.time = 0.0f;
                                    bool capOk = false;

                                    std::string normC = refl.name;
                                    for (char& c : normC) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                    std::string normF = field.name;
                                    for (char& c : normF) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                    if (normC.find("sprite") != std::string::npos || normC == "spriterenderer") {
                                        if (auto* spr = registry.get<Engine::SpriteRenderer>(selectedEntity)) {
                                            if (normF.find("texture") != std::string::npos || normF.find("image") != std::string::npos) {
                                                initKey.stringValue = spr->texturePath;
                                                capOk = true;
                                            } else if (normF.find("color") != std::string::npos) {
                                                initKey.value = spr->color;
                                                capOk = true;
                                            } else if (normF.find("flipx") != std::string::npos) {
                                                initKey.value.x = spr->flipX ? 1.0f : 0.0f;
                                                capOk = true;
                                            } else if (normF.find("flipy") != std::string::npos) {
                                                initKey.value.x = spr->flipY ? 1.0f : 0.0f;
                                                capOk = true;
                                            } else if (normF.find("sortorder") != std::string::npos) {
                                                initKey.value.x = static_cast<float>(spr->sortOrder);
                                                capOk = true;
                                            }
                                        }
                                    }

                                    if (!capOk && refl.has(registry, selectedEntity)) {
                                        void* compPtr = refl.get(registry, selectedEntity);
                                        if (compPtr) {
                                            char* fieldPtr = static_cast<char*>(compPtr) + field.offset;
                                            if (field.type == Engine::FieldType::Float) initKey.value.x = *reinterpret_cast<float*>(fieldPtr);
                                            else if (field.type == Engine::FieldType::Int) initKey.value.x = static_cast<float>(*reinterpret_cast<int*>(fieldPtr));
                                            else if (field.type == Engine::FieldType::Bool) initKey.value.x = *reinterpret_cast<bool*>(fieldPtr) ? 1.0f : 0.0f;
                                            else if (field.type == Engine::FieldType::Vec2) initKey.value = glm::vec4(*reinterpret_cast<glm::vec2*>(fieldPtr), 0.0f, 0.0f);
                                            else if (field.type == Engine::FieldType::Vec3) initKey.value = glm::vec4(*reinterpret_cast<glm::vec3*>(fieldPtr), 0.0f);
                                            else if (field.type == Engine::FieldType::Vec4) initKey.value = *reinterpret_cast<glm::vec4*>(fieldPtr);
                                            else if (field.type == Engine::FieldType::String) initKey.stringValue = *reinterpret_cast<std::string*>(fieldPtr);
                                            capOk = true;
                                        }
                                    }

                                    if (capOk) {
                                        newChan.keys.push_back(initKey);
                                    }

                                    activeClip->propertyChannels.push_back(newChan);
                                }
                            } else {
                                ImGui::MenuItem(field.name.c_str(), nullptr, false, false);
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    static int s_selectedTrackIndex = -1;
    static int s_selectedKeyIndex = -1;

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    float leftColWidth = 250.0f;
    float rightColWidth = contentSize.x - leftColWidth - 8.0f;

    ImGui::BeginChild("##left_properties", ImVec2(leftColWidth, contentSize.y - 120.0f), true);
    ImGui::Text("Properties");
    ImGui::Separator();
    
    int trackToDelete = -1;
    if (ImGui::BeginTable("##properties_table", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoKeepColumnsVisible)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("KeyCol", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("DelCol", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        
        for (int t = 0; t < static_cast<int>(activeClip->propertyChannels.size()); ++t) {
            auto& chan = activeClip->propertyChannels[t];
            ImGui::TableNextRow();

            const float rowHeight = ImGui::GetFrameHeight();
            
            // Column 0: Property Name Selectable
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(t);
            bool isSelected = (s_selectedTrackIndex == t);
            char label[256];
            snprintf(label, sizeof(label), "%s.%s", chan.componentName.c_str(), chan.fieldName.c_str());
            if (ImGui::Selectable(label, isSelected, 0, ImVec2(0, rowHeight))) {
                s_selectedTrackIndex = t;
                s_selectedKeyIndex = -1;
            }
            ImGui::PopID();

            // Column 1: Key Button
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(t + 1000);
            if (ImGui::Button("Key", ImVec2(0, rowHeight))) {
                auto& reflReg = Engine::ComponentReflectionRegistry::getInstance();

                auto normalizeName = [](const std::string& s) {
                    std::string res;
                    for (char c : s) {
                        if (c != ' ' && c != '_' && c != '&' && c != '/') {
                            res += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        }
                    }
                    if (res.size() > 9 && res.substr(res.size() - 9) == "component") {
                        res = res.substr(0, res.size() - 9);
                    }
                    return res;
                };

                auto stripPrefix = [](const std::string& s) {
                    if (s.rfind("spr", 0) == 0 && s.size() > 3) return s.substr(3);
                    if (s.rfind("m_", 0) == 0 && s.size() > 2) return s.substr(2);
                    if (s.rfind("s_", 0) == 0 && s.size() > 2) return s.substr(2);
                    return s;
                };

                std::string normChanComp = normalizeName(chan.componentName);
                std::string normChanField = normalizeName(chan.fieldName);
                std::string sfn = stripPrefix(normChanField);

                const Engine::ComponentReflection* targetRefl = nullptr;
                for (const auto& refl : reflReg.getReflections()) {
                    if (refl.name == chan.componentName ||
                        normalizeName(refl.name) == normChanComp ||
                        normalizeName(refl.displayName) == normChanComp) {
                        targetRefl = &refl;
                        break;
                    }
                }

                glm::vec4 capturedVal(0.0f);
                std::string capturedStr = "";
                bool capturedSuccess = false;

                if (normChanComp.find("sprite") != std::string::npos || normChanComp == "spriterenderer") {
                    if (auto* spr = registry.get<Engine::SpriteRenderer>(selectedEntity)) {
                        if (sfn.find("texture") != std::string::npos || sfn.find("image") != std::string::npos) {
                            capturedStr = spr->texturePath;
                            capturedSuccess = true;
                        } else if (sfn.find("color") != std::string::npos) {
                            capturedVal = spr->color;
                            capturedSuccess = true;
                        } else if (sfn.find("flipx") != std::string::npos) {
                            capturedVal.x = spr->flipX ? 1.0f : 0.0f;
                            capturedSuccess = true;
                        } else if (sfn.find("flipy") != std::string::npos) {
                            capturedVal.x = spr->flipY ? 1.0f : 0.0f;
                            capturedSuccess = true;
                        } else if (sfn.find("sortorder") != std::string::npos) {
                            capturedVal.x = static_cast<float>(spr->sortOrder);
                            capturedSuccess = true;
                        }
                    }
                }

                if (!capturedSuccess && targetRefl && targetRefl->has(registry, selectedEntity)) {
                    void* compPtr = targetRefl->get(registry, selectedEntity);
                    if (compPtr) {
                        const Engine::ComponentField* targetField = nullptr;
                        for (const auto& f : targetRefl->fields) {
                            std::string fn = normalizeName(f.name);
                            std::string sfn2 = stripPrefix(fn);
                            if (f.name == chan.fieldName || fn == normChanField || sfn == sfn2 ||
                                (sfn.find("texture") != std::string::npos && sfn2.find("texture") != std::string::npos) ||
                                (sfn.find("image") != std::string::npos && sfn2.find("texture") != std::string::npos) ||
                                (sfn.find("texture") != std::string::npos && sfn2.find("image") != std::string::npos)) {
                                targetField = &f;
                                break;
                            }
                        }
                        if (targetField) {
                            char* fieldPtr = static_cast<char*>(compPtr) + targetField->offset;
                            if (chan.type == Engine::FieldType::Float) {
                                capturedVal.x = *reinterpret_cast<float*>(fieldPtr);
                            } else if (chan.type == Engine::FieldType::Int) {
                                capturedVal.x = static_cast<float>(*reinterpret_cast<int*>(fieldPtr));
                            } else if (chan.type == Engine::FieldType::Bool) {
                                capturedVal.x = *reinterpret_cast<bool*>(fieldPtr) ? 1.0f : 0.0f;
                            } else if (chan.type == Engine::FieldType::Vec2) {
                                auto* v = reinterpret_cast<glm::vec2*>(fieldPtr);
                                capturedVal = glm::vec4(v->x, v->y, 0.0f, 0.0f);
                            } else if (chan.type == Engine::FieldType::Vec3) {
                                auto* v = reinterpret_cast<glm::vec3*>(fieldPtr);
                                capturedVal = glm::vec4(v->x, v->y, v->z, 0.0f);
                            } else if (chan.type == Engine::FieldType::Vec4) {
                                capturedVal = *reinterpret_cast<glm::vec4*>(fieldPtr);
                            } else if (chan.type == Engine::FieldType::String) {
                                capturedStr = *reinterpret_cast<std::string*>(fieldPtr);
                            }
                            capturedSuccess = true;
                        }
                    }
                }

                if (capturedSuccess) {
                    PropertyKeyframe newKey;
                    newKey.time = animator->currentTime;
                    newKey.value = capturedVal;
                    newKey.stringValue = capturedStr;

                    bool found = false;
                    for (auto& key : chan.keys) {
                        if (std::abs(key.time - animator->currentTime) < 0.01f) {
                            key.value = capturedVal;
                            key.stringValue = capturedStr;
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        chan.keys.push_back(newKey);
                        std::sort(chan.keys.begin(), chan.keys.end(), [](const PropertyKeyframe& a, const PropertyKeyframe& b) {
                            return a.time < b.time;
                        });
                    }
                    statusMessage = "Added keyframe for " + chan.componentName + "." + chan.fieldName + " at " + std::to_string(animator->currentTime) + "s";
                }
            }
            ImGui::PopID();

            // Column 2: Delete Button
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(t + 2000);
            if (ImGui::Button("X", ImVec2(0, rowHeight))) {
                trackToDelete = t;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (trackToDelete != -1) {
        activeClip->propertyChannels.erase(activeClip->propertyChannels.begin() + trackToDelete);
        s_selectedTrackIndex = -1;
        s_selectedKeyIndex = -1;
    }

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##right_timeline", ImVec2(rightColWidth, contentSize.y - 120.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 startCursorPos = ImGui::GetCursorScreenPos();
    float timelineDuration = activeClip->duration;
    
    float pixelsPerSecond = 150.0f;
    float trackWidth = std::max(timelineDuration * pixelsPerSecond, 100.0f);
    float trackHeight = 20.0f;
    float totalHeight = 25.0f + activeClip->propertyChannels.size() * trackHeight + 50.0f;

    // Reserve layout space up front so that custom SetCursorPosY/SetCursorScreenPos calls
    // stay within window bounds and scrollbars function correctly.
    ImGui::Dummy(ImVec2(trackWidth, totalHeight));
    ImGui::SetCursorScreenPos(startCursorPos);

    ImVec2 rulerStart = startCursorPos;
    ImVec2 rulerEnd = ImVec2(rulerStart.x + trackWidth, rulerStart.y + 25.0f);
    
    drawList->AddRectFilled(rulerStart, rulerEnd, IM_COL32(40, 40, 40, 255));
    
    float playheadX = rulerStart.x + animator->currentTime * pixelsPerSecond;
    
    for (float tSec = 0.0f; tSec <= timelineDuration; tSec += 0.1f) {
        float xPos = rulerStart.x + tSec * pixelsPerSecond;
        bool isMajor = (std::fmod(tSec + 0.001f, 0.5f) < 0.01f);
        float tickH = isMajor ? 12.0f : 6.0f;
        drawList->AddLine(ImVec2(xPos, rulerStart.y + 25.0f - tickH), ImVec2(xPos, rulerStart.y + 25.0f), IM_COL32(150, 150, 150, 255));
        
        if (isMajor) {
            char label[16];
            snprintf(label, sizeof(label), "%.1fs", tSec);
            drawList->AddText(ImVec2(xPos + 2.0f, rulerStart.y + 2.0f), IM_COL32(200, 200, 200, 255), label);
        }
    }

    ImGui::InvisibleButton("##ruler_scrub", ImVec2(trackWidth, 25.0f));
    if (ImGui::IsItemActive()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float localMouseX = mousePos.x - rulerStart.x;
        animator->currentTime = glm::clamp(localMouseX / pixelsPerSecond, 0.0f, timelineDuration);
        if (auto* ctrl = registry.get<AnimationControllerComponent>(selectedEntity)) {
            ctrl->currentStateTime = animator->currentTime;
        }
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);

    for (int t = 0; t < static_cast<int>(activeClip->propertyChannels.size()); ++t) {
        auto& chan = activeClip->propertyChannels[t];
        ImVec2 laneStart = ImGui::GetCursorScreenPos();
        ImVec2 laneEnd = ImVec2(laneStart.x + trackWidth, laneStart.y + trackHeight);

        ImU32 laneColor = (t % 2 == 0) ? IM_COL32(50, 50, 50, 255) : IM_COL32(45, 45, 45, 255);
        if (s_selectedTrackIndex == t) {
            laneColor = IM_COL32(70, 70, 70, 255);
        }
        drawList->AddRectFilled(laneStart, laneEnd, laneColor);

        ImGui::PushID(t);
        ImGui::SetCursorScreenPos(laneStart);
        ImGui::InvisibleButton("##lane_btn", ImVec2(trackWidth, trackHeight));
        
        if (ImGui::IsItemClicked()) {
            s_selectedTrackIndex = t;
            s_selectedKeyIndex = -1;
            
            ImVec2 mPos = ImGui::GetMousePos();
            for (int k = 0; k < static_cast<int>(chan.keys.size()); ++k) {
                float keyX = laneStart.x + chan.keys[k].time * pixelsPerSecond;
                if (std::abs(mPos.x - keyX) < 8.0f) {
                    s_selectedKeyIndex = k;
                    animator->currentTime = chan.keys[k].time;
                    if (auto* ctrl = registry.get<AnimationControllerComponent>(selectedEntity)) {
                        ctrl->currentStateTime = animator->currentTime;
                    }
                    break;
                }
            }
        }
        ImGui::PopID();

        for (int k = 0; k < static_cast<int>(chan.keys.size()); ++k) {
            float keyX = laneStart.x + chan.keys[k].time * pixelsPerSecond;
            float keyY = laneStart.y + trackHeight * 0.5f;

            bool isKeySelected = (s_selectedTrackIndex == t && s_selectedKeyIndex == k);
            ImU32 keyColor = isKeySelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(220, 220, 220, 255);
            
            ImVec2 points[4] = {
                ImVec2(keyX, keyY - 5.0f),
                ImVec2(keyX + 5.0f, keyY),
                ImVec2(keyX, keyY + 5.0f),
                ImVec2(keyX - 5.0f, keyY)
            };
            drawList->AddConvexPolyFilled(points, 4, keyColor);
            drawList->AddPolyline(points, 4, IM_COL32(0, 0, 0, 255), true, 1.0f);
        }

        ImGui::SetCursorScreenPos(ImVec2(laneStart.x, laneStart.y + trackHeight));
    }

    float playheadBottomY = ImGui::GetCursorScreenPos().y;
    drawList->AddLine(
        ImVec2(playheadX, rulerStart.y),
        ImVec2(playheadX, playheadBottomY),
        IM_COL32(70, 180, 255, 200),
        2.0f
    );
    drawList->AddTriangleFilled(
        ImVec2(playheadX - 6.0f, rulerStart.y + 20.0f),
        ImVec2(playheadX + 6.0f, rulerStart.y + 20.0f),
        ImVec2(playheadX, rulerStart.y + 25.0f),
        IM_COL32(70, 180, 255, 255)
    );

    ImGui::EndChild();

    ImGui::Separator();
    if (s_selectedTrackIndex >= 0 && s_selectedTrackIndex < static_cast<int>(activeClip->propertyChannels.size())) {
        auto& chan = activeClip->propertyChannels[s_selectedTrackIndex];
        
        ImGui::Text("Track Keyframes: %s.%s", chan.componentName.c_str(), chan.fieldName.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Add Keyframe at Current Time")) {
            PropertyKeyframe newKey;
            newKey.time = animator->currentTime;
            
            // Auto capture current
            if (chan.type == Engine::FieldType::String || chan.fieldName.find("texture") != std::string::npos || chan.fieldName.find("path") != std::string::npos) {
                if (auto* spr = registry.get<Engine::SpriteRenderer>(selectedEntity)) {
                    newKey.stringValue = spr->texturePath;
                }
            } else if (auto* spr = registry.get<Engine::SpriteRenderer>(selectedEntity)) {
                if (chan.fieldName.find("color") != std::string::npos) newKey.value = spr->color;
                else if (chan.fieldName.find("flipx") != std::string::npos) newKey.value.x = spr->flipX ? 1.0f : 0.0f;
                else if (chan.fieldName.find("flipy") != std::string::npos) newKey.value.x = spr->flipY ? 1.0f : 0.0f;
            }

            bool found = false;
            for (auto& k : chan.keys) {
                if (std::abs(k.time - animator->currentTime) < 0.01f) {
                    k.stringValue = newKey.stringValue;
                    k.value = newKey.value;
                    found = true;
                    break;
                }
            }
            if (!found) {
                chan.keys.push_back(newKey);
                std::sort(chan.keys.begin(), chan.keys.end(), [](const PropertyKeyframe& a, const PropertyKeyframe& b) {
                    return a.time < b.time;
                });
            }
        }

        bool isTexChan = (chan.type == Engine::FieldType::String ||
                          chan.fieldName.find("texture") != std::string::npos ||
                          chan.fieldName.find("path") != std::string::npos ||
                          chan.fieldName.find("image") != std::string::npos);

        if (ImGui::BeginTable("##track_keys_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Key Index", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Time (s)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Value / Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            int keyToDelete = -1;
            for (int k = 0; k < static_cast<int>(chan.keys.size()); ++k) {
                auto& key = chan.keys[k];
                ImGui::TableNextRow();
                ImGui::PushID(k + 5000);

                // Col 0: Index
                ImGui::TableSetColumnIndex(0);
                bool isSel = (s_selectedKeyIndex == k);
                if (ImGui::Selectable(("Key " + std::to_string(k)).c_str(), isSel, ImGuiSelectableFlags_SpanAllColumns)) {
                    s_selectedKeyIndex = k;
                    animator->currentTime = key.time;
                }

                // Col 1: Time
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::DragFloat("##ktime", &key.time, 0.01f, 0.0f, activeClip->duration, "%.2fs")) {
                    std::sort(chan.keys.begin(), chan.keys.end(), [](const PropertyKeyframe& a, const PropertyKeyframe& b) {
                        return a.time < b.time;
                    });
                }

                // Col 2: Value / Path
                ImGui::TableSetColumnIndex(2);
                if (isTexChan) {
                    char strBuf[512] = "";
                    strncpy_s(strBuf, key.stringValue.c_str(), sizeof(strBuf) - 1);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##kpath", strBuf, sizeof(strBuf))) {
                        key.stringValue = strBuf;
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
                            key.stringValue = (const char*)payload->Data;
                        }
                        ImGui::EndDragDropTarget();
                    }
                } else if (chan.type == Engine::FieldType::Bool || chan.fieldName.find("flip") != std::string::npos) {
                    bool bVal = (key.value.x > 0.5f);
                    if (ImGui::Checkbox("##kbool", &bVal)) {
                        key.value.x = bVal ? 1.0f : 0.0f;
                    }
                } else if (chan.type == Engine::FieldType::Float) {
                    ImGui::SetNextItemWidth(150.0f);
                    ImGui::DragFloat("##kfloat", &key.value.x, 0.05f);
                } else if (chan.type == Engine::FieldType::Vec4 || chan.fieldName.find("color") != std::string::npos) {
                    ImGui::SetNextItemWidth(250.0f);
                    ImGui::ColorEdit4("##kcolor", &key.value.x);
                } else {
                    ImGui::SetNextItemWidth(250.0f);
                    ImGui::DragFloat4("##kvec4", &key.value.x, 0.05f);
                }

                // Col 3: Actions
                ImGui::TableSetColumnIndex(3);
                if (ImGui::Button("Del")) {
                    keyToDelete = k;
                }

                ImGui::PopID();
            }

            if (keyToDelete != -1) {
                chan.keys.erase(chan.keys.begin() + keyToDelete);
                s_selectedKeyIndex = -1;
            }

            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("Select a property track in the list above to view and edit all its keyframes.");
    }

    ImGui::End();
}


void EditorUI::drawNodeGraphDemoWindow() {
    if (!s_openNodeGraphDemoWindow) return;

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Node Graph Demo", &s_openNodeGraphDemoWindow)) {
        ImGui::End();
        return;
    }

    static Engine::NodeGraph graph;
    static bool initialized = false;

    if (!initialized) {
        Engine::NodePinType pinFloat   { "float",    IM_COL32(100, 200, 100, 255) };
        Engine::NodePinType pinExec    { "exec",     IM_COL32(220, 220, 220, 255) };
        Engine::NodePinType pinDialogue{ "dialogue", IM_COL32(200, 100, 200, 255) };
        Engine::NodePinType pinColor   { "color",    IM_COL32(200, 180,  50, 255) };

        // 1. Speech Node — detail panel shows the text field
        graph.registerNodeType("Dialogue", "DialogueSpeech", "Speech Node",
            [pinDialogue](uint32_t nodeId) {
                graph.addInputPin(nodeId, "Prev", pinDialogue);
                graph.addOutputPin(nodeId, "Next", pinDialogue);
            },
            nullptr, // no inline widget — moved to detail panel
            [](Engine::Node& nodeRef) {
                ImGui::TextDisabled("Speech Text:");
                char buf[256] = "";
                strncpy_s(buf, nodeRef.customData.c_str(), sizeof(buf) - 1);
                ImGui::PushItemWidth(-1);
                if (ImGui::InputTextMultiline("##speechTxt", buf, sizeof(buf), ImVec2(0, 80)))
                    nodeRef.customData = buf;
                ImGui::PopItemWidth();
            }
        );

        // 2. Choice Node — detail panel shows choice labels
        graph.registerNodeType("Dialogue", "DialogueChoice", "Choice Node",
            [pinDialogue](uint32_t nodeId) {
                graph.addInputPin(nodeId, "Prev", pinDialogue);
                graph.addOutputPin(nodeId, "Option A", pinDialogue);
                graph.addOutputPin(nodeId, "Option B", pinDialogue);
            },
            nullptr,
            [](Engine::Node& nodeRef) {
                ImGui::TextDisabled("Choice node.");
                ImGui::TextWrapped("Each output pin represents a dialogue branch option.");
            }
        );

        // 3. Math Add Node
        graph.registerNodeType("Math", "MathAdd", "Add Node",
            [pinFloat](uint32_t nodeId) {
                graph.addInputPin(nodeId, "A", pinFloat);
                graph.addInputPin(nodeId, "B", pinFloat);
                graph.addOutputPin(nodeId, "Result", pinFloat);
            },
            nullptr,
            [](Engine::Node& nodeRef) {
                ImGui::TextDisabled("Adds two floats.");
            }
        );

        // 4. Branch Node — detail panel shows condition boolean
        graph.registerNodeType("Logic", "Branch", "Branch Node",
            [pinExec](uint32_t nodeId) {
                graph.addInputPin(nodeId, "In", pinExec);
                graph.addOutputPin(nodeId, "True", pinExec);
                graph.addOutputPin(nodeId, "False", pinExec);
            },
            nullptr,
            [](Engine::Node& nodeRef) {
                ImGui::TextDisabled("Condition:");
                bool val = (nodeRef.customData == "1");
                if (ImGui::Checkbox("Active##branch_cond", &val))
                    nodeRef.customData = val ? "1" : "0";
            }
        );

        // 5. Color Constant Node — detail panel shows color picker
        graph.registerNodeType("Visual", "ColorConstant", "Color Constant",
            [pinColor](uint32_t nodeId) {
                graph.addOutputPin(nodeId, "Out", pinColor);
            },
            nullptr,
            [](Engine::Node& nodeRef) {
                static float color[4] = { 0.15f, 0.45f, 0.8f, 1.0f };
                if (!nodeRef.customData.empty()) {
                    std::stringstream css(nodeRef.customData);
                    css >> color[0] >> color[1] >> color[2] >> color[3];
                }
                if (ImGui::ColorEdit4("Color##cc", color)) {
                    std::stringstream css;
                    css << color[0] << " " << color[1] << " " << color[2] << " " << color[3];
                    nodeRef.customData = css.str();
                }
            }
        );

        // Pre-spawn nodes
        uint32_t n1Id    = graph.createNode("Speech Node",  "DialogueSpeech",  ImVec2( 50.0f, 150.0f));
        if (Engine::Node* n1 = graph.findNode(n1Id))
            n1->customData = "Hello adventurer!";
        graph.addInputPin(n1Id,  "Prev", pinDialogue);
        uint32_t n1Out = graph.addOutputPin(n1Id, "Next", pinDialogue);

        uint32_t n2Id    = graph.createNode("Choice Node",  "DialogueChoice",  ImVec2(320.0f, 120.0f));
        uint32_t n2In    = graph.addInputPin(n2Id,  "Prev",     pinDialogue);
        graph.addOutputPin(n2Id, "Option A", pinDialogue);
        graph.addOutputPin(n2Id, "Option B", pinDialogue);
        graph.addLink(n1Out, n2In);

        initialized = true;
    }

    // Toolbar
    if (ImGui::Button("Clear")) graph.clear();
    ImGui::SameLine();
    if (ImGui::Button("Save JSON")) {
        std::ofstream out("assets/node_graph_demo.json");
        if (out.is_open()) { out << graph.serialize(); statusMessage = "Saved node graph."; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load JSON")) {
        std::ifstream in("assets/node_graph_demo.json");
        if (in.is_open()) {
            std::stringstream ss; ss << in.rdbuf();
            graph.deserialize(ss.str()); statusMessage = "Loaded node graph.";
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|  R-Click canvas to create nodes. Drag pins to link.");

    ImGui::Separator();
    graph.draw("DemoEditor", ImVec2(0, 0), 240.0f);

    ImGui::End();
}

// =============================================================================
// Animator Controller Editor
// =============================================================================


void EditorUI::drawAnimatorControllerWindow() {
    if (!s_openAnimatorControllerWindow) return;

    ImGui::SetNextWindowSize(ImVec2(1050, 650), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animator Controller", &s_openAnimatorControllerWindow,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }

    // -----------------------------------------------------------------------
    // Guard: need selected entity with AnimatorComponent
    // -----------------------------------------------------------------------
    if (!hasSelection || !registry.isValid(selectedEntity)) {
        ImGui::TextDisabled("Select an entity with an AnimatorComponent and AnimationControllerComponent.");
        ImGui::End();
        return;
    }

    AnimatorComponent* animator = registry.get<AnimatorComponent>(selectedEntity);
    AnimationControllerComponent* controller = registry.get<AnimationControllerComponent>(selectedEntity);

    if (!animator || !controller) {
        ImGui::TextDisabled("The selected entity needs both AnimatorComponent and AnimationControllerComponent.");
        if (!animator && ImGui::Button("Add AnimatorComponent"))
            registry.emplace<AnimatorComponent>(selectedEntity, AnimatorComponent{});
        ImGui::SameLine();
        if (!controller && ImGui::Button("Add AnimationControllerComponent"))
            registry.emplace<AnimationControllerComponent>(selectedEntity, AnimationControllerComponent{});
        ImGui::End();
        return;
    }

    // -----------------------------------------------------------------------
    // Static per-window state
    // -----------------------------------------------------------------------
    static Engine::NodeGraph s_ctrlGraph;
    static uint32_t          s_lastBuiltEntityId = 0;
    static size_t            s_lastStateCount = 0;
    static size_t            s_lastTransitionCount = 0;
    static std::string       s_lastCurrentState = "";
    static std::unordered_map<std::string, ImVec2> s_statePositions;

    for (const auto& st : controller->states) {
        if (st.isBlendTree) {
            for (const auto& bn : st.blendTree.nodes) ensureClipLoaded(animator, registry, selectedEntity, bn.clipName, renderer);
        } else {
            ensureClipLoaded(animator, registry, selectedEntity, st.clipName, renderer);
        }
    }

    // Collect all clip names (loaded + scanned from disk assets)
    std::vector<std::string> clipNames;
    std::set<std::string> seenClips;
    for (const auto& clip : animator->animations) {
        if (!clip.name.empty() && seenClips.find(clip.name) == seenClips.end()) {
            seenClips.insert(clip.name);
            clipNames.push_back(clip.name);
        }
        std::string stem = std::filesystem::path(clip.name).stem().string();
        if (!stem.empty() && seenClips.find(stem) == seenClips.end()) {
            seenClips.insert(stem);
            clipNames.push_back(stem);
        }
    }
    try {
        if (std::filesystem::exists("assets")) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator("assets")) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
                    if (ext == ".anim") {
                        std::string relPath = entry.path().generic_string();
                        std::string stem = entry.path().stem().string();
                        if (seenClips.find(stem) == seenClips.end()) {
                            seenClips.insert(stem);
                            clipNames.push_back(stem);
                        }
                        if (seenClips.find(relPath) == seenClips.end()) {
                            seenClips.insert(relPath);
                            clipNames.push_back(relPath);
                        }
                    }
                }
            }
        }
    } catch (...) {}

    auto processDroppedAnimationAsset = [this, animator](const std::string& pathStr) -> std::string {
        std::filesystem::path p(pathStr);
        std::string ext = p.extension().string();
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
        if (lowerExt == ".anim" || lowerExt == ".fbx" || lowerExt == ".gltf" || lowerExt == ".glb") {
            SkeletonComponent* skeleton = registry.get<SkeletonComponent>(selectedEntity);
            bool isAnimFile = (lowerExt == ".anim");
            if (!skeleton && !isAnimFile) {
                registry.emplace<SkeletonComponent>(selectedEntity, SkeletonComponent{});
                skeleton = registry.get<SkeletonComponent>(selectedEntity);
            }
            size_t prevCount = animator->animations.size();
            bool loaded = false;
            if (isAnimFile) {
                SkeletonComponent dummySkel;
                loaded = renderer.resourceManager->loadBinarySkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator, true);
            } else {
                loaded = renderer.resourceManager->loadSkeletonAndAnimations(pathStr, *skeleton, *animator, true);
            }
            if (loaded) {
                if (animator->animations.size() > prevCount) {
                    return animator->animations.back().name;
                }
                std::string stem = p.stem().string();
                for (const auto& clip : animator->animations) {
                    if (clip.name == stem) return clip.name;
                }
                if (!animator->animations.empty()) {
                    return animator->animations.front().name;
                }
            }
            return p.stem().string();
        }
        return "";
    };

    // -----------------------------------------------------------------------
    // Rebuild graph when entity or state list changes
    // -----------------------------------------------------------------------
    auto rebuildGraph = [&]() {
        std::string selectedStateName;
        std::string selectedTypeName;
        if (uint32_t selId = s_ctrlGraph.getSelectedNodeId()) {
            if (Engine::Node* selNode = s_ctrlGraph.findNode(selId)) {
                selectedTypeName = selNode->typeName;
                if (selNode->userData) {
                    selectedStateName = static_cast<AnimationState*>(selNode->userData)->name;
                }
            }
        }

        s_ctrlGraph.clear();


        // Pin type for state transitions
        Engine::NodePinType pinState{ "state", IM_COL32(255, 165, 80, 255) };

        // Register node types (no-op if already registered since we clear each time)
        // Entry node
        s_ctrlGraph.registerNodeType("State", "CtrlEntry", "Entry",
            [pinState](uint32_t nodeId) {
                s_ctrlGraph.addOutputPin(nodeId, "Start", pinState);
            },
            nullptr,
            [](Engine::Node& n) {
                ImGui::TextDisabled("Entry point of the state machine.");
                ImGui::TextWrapped("Connect this to the first default state.");
            }
        );

        // Any State node
        s_ctrlGraph.registerNodeType("State", "CtrlAnyState", "Any State",
            [pinState](uint32_t nodeId) {
                s_ctrlGraph.addOutputPin(nodeId, "To", pinState);
            },
            nullptr,
            [](Engine::Node& n) {
                ImGui::TextDisabled("Transitions from Any State fire regardless of the current active state.");
            }
        );

        // Regular animation state node (detail panel = full state editor)
        s_ctrlGraph.registerNodeType("State", "CtrlState", "State",
            [pinState](uint32_t nodeId) {
                s_ctrlGraph.addInputPin(nodeId,  "In",  pinState);
                s_ctrlGraph.addOutputPin(nodeId, "Out", pinState);
            },
            nullptr,
            [controller, animator, clipNames, processDroppedAnimationAsset, this](Engine::Node& n) {

                if (!n.userData) { ImGui::TextDisabled("Invalid state reference."); return; }
                AnimationState* state = static_cast<AnimationState*>(n.userData);

                // State name
                ImGui::TextDisabled("State Name:");
                char nameBuf[128];
                strncpy_s(nameBuf, state->name.c_str(), sizeof(nameBuf) - 1);
                ImGui::PushItemWidth(-1);
                if (ImGui::InputText("##stateName", nameBuf, sizeof(nameBuf)))
                    state->name = nameBuf;
                ImGui::PopItemWidth();
                ImGui::Spacing();

                ImGui::TextDisabled("Is Blend Tree:");
                ImGui::Checkbox("##isBlendTree", &state->isBlendTree);

                if (!state->isBlendTree) {
                    // Clip picker
                    ImGui::Spacing();
                    ImGui::TextDisabled("Animation Clip:");
                    ImGui::PushItemWidth(-1);
                    if (ImGui::BeginCombo("##stateClip", state->clipName.empty() ? "(none)" : state->clipName.c_str())) {
                        if (ImGui::Selectable("(none)", state->clipName.empty()))
                            state->clipName.clear();
                        for (const auto& cn : clipNames) {
                            bool sel = (cn == state->clipName);
                            if (ImGui::Selectable(cn.c_str(), sel))
                                state->clipName = cn;
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        std::string droppedPath = acceptDroppedAssetPath();
                        if (!droppedPath.empty()) {
                            std::string resName = processDroppedAnimationAsset(droppedPath);
                            if (!resName.empty()) {
                                state->clipName = resName;
                                statusMessage = "Set clip for state '" + state->name + "' to '" + resName + "'.";
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopItemWidth();

                } else {
                    // Blend tree editor
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextDisabled("Blend Tree");
                    ImGui::Separator();

                    std::vector<std::string> pList;
                    for (const auto& [pn, pv] : controller->parameters) pList.push_back(pn);

                    ImGui::TextDisabled("Parameter X:");
                    if (!pList.empty()) {
                        ImGui::PushItemWidth(-1);
                        if (ImGui::BeginCombo("##btParamCombo", state->blendTree.parameterName.empty() ? "(select param)" : state->blendTree.parameterName.c_str())) {
                            for (const auto& pn : pList) {
                                bool sSelected = (pn == state->blendTree.parameterName);
                                if (ImGui::Selectable(pn.c_str(), sSelected)) state->blendTree.parameterName = pn;
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();
                    } else {
                        char paramBuf[64];
                        strncpy_s(paramBuf, state->blendTree.parameterName.c_str(), sizeof(paramBuf) - 1);
                        ImGui::PushItemWidth(-1);
                        if (ImGui::InputText("##btParam", paramBuf, sizeof(paramBuf)))
                            state->blendTree.parameterName = paramBuf;
                        ImGui::PopItemWidth();
                    }

                    ImGui::Checkbox("2D Blend", &state->blendTree.is2D);
                    if (state->blendTree.is2D) {
                        ImGui::TextDisabled("Parameter Y:");
                        if (!pList.empty()) {
                            ImGui::PushItemWidth(-1);
                            if (ImGui::BeginCombo("##btParamYCombo", state->blendTree.parameterYName.empty() ? "(select param)" : state->blendTree.parameterYName.c_str())) {
                                for (const auto& pn : pList) {
                                    bool sSelected = (pn == state->blendTree.parameterYName);
                                    if (ImGui::Selectable(pn.c_str(), sSelected)) state->blendTree.parameterYName = pn;
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::PopItemWidth();
                        } else {
                            char paramYBuf[64];
                            strncpy_s(paramYBuf, state->blendTree.parameterYName.c_str(), sizeof(paramYBuf) - 1);
                            ImGui::PushItemWidth(-1);
                            if (ImGui::InputText("##btParamY", paramYBuf, sizeof(paramYBuf)))
                                state->blendTree.parameterYName = paramYBuf;
                            ImGui::PopItemWidth();
                        }
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Blend Nodes:");
                    int toRemoveBlend = -1;
                    for (int bi = 0; bi < (int)state->blendTree.nodes.size(); ++bi) {
                        ImGui::PushID(bi);
                        auto& bn = state->blendTree.nodes[bi];
                        if (state->blendTree.is2D) {
                            ImGui::PushItemWidth(110);
                            ImGui::DragFloat2("##th2D", &bn.threshold2D.x, 0.05f, -100.0f, 100.0f, "%.2f");
                            ImGui::PopItemWidth();
                        } else {
                            ImGui::PushItemWidth(70);
                            ImGui::DragFloat("##th", &bn.threshold, 0.05f, -100.0f, 100.0f, "%.2f");
                            ImGui::PopItemWidth();
                        }
                        ImGui::SameLine();
                        ImGui::PushItemWidth(-30);
                        if (ImGui::BeginCombo("##bnClip", bn.clipName.empty() ? "(none)" : bn.clipName.c_str())) {
                            if (ImGui::Selectable("(none)", bn.clipName.empty())) bn.clipName.clear();
                            for (const auto& cn : clipNames) {
                                bool s2 = (cn == bn.clipName);
                                if (ImGui::Selectable(cn.c_str(), s2)) {
                                    bn.clipName = cn;
                                    ensureClipLoaded(animator, registry, selectedEntity, cn, renderer);
                                }
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            std::string droppedPath = acceptDroppedAssetPath();
                            if (!droppedPath.empty()) {
                                std::string resName = processDroppedAnimationAsset(droppedPath);
                                if (!resName.empty()) {
                                    bn.clipName = resName;
                                    statusMessage = "Set blend motion clip to '" + resName + "'.";
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::PopItemWidth();

                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##rmbn")) toRemoveBlend = bi;
                        ImGui::PopID();
                    }
                    if (toRemoveBlend >= 0)
                        state->blendTree.nodes.erase(state->blendTree.nodes.begin() + toRemoveBlend);
                    if (ImGui::SmallButton("+ Blend Node"))
                        state->blendTree.nodes.push_back(BlendNode{});
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Playback:");
                ImGui::DragFloat("Speed##stSpeed", &state->speed, 0.01f, 0.0f, 10.0f);
                ImGui::Checkbox("Loop##stLoop", &state->isLooping);

                // Transitions from this state
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Outgoing Transitions:");
                for (auto& trans : controller->transitions) {
                    if (trans.fromState != state->name) continue;
                    ImGui::PushID(&trans);
                    ImGui::Text("-> %s", trans.toState.c_str());
                    ImGui::SameLine();
                    ImGui::DragFloat("Fade##xf", &trans.crossfadeDuration, 0.01f, 0.0f, 2.0f);

                    int toRemoveCond = -1;
                    for (int ci = 0; ci < (int)trans.conditions.size(); ++ci) {
                        ImGui::PushID(ci);
                        auto& cond = trans.conditions[ci];
                        char pBuf[64]; strncpy_s(pBuf, cond.parameterName.c_str(), sizeof(pBuf)-1);
                        ImGui::PushItemWidth(80);
                        if (ImGui::InputText("##cp", pBuf, sizeof(pBuf))) cond.parameterName = pBuf;
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        const char* ops[] = { ">", "<", "==" };
                        int opIdx = (cond.op == ">") ? 0 : (cond.op == "<") ? 1 : 2;
                        ImGui::PushItemWidth(45);
                        if (ImGui::Combo("##cop", &opIdx, ops, 3)) cond.op = ops[opIdx];
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        ImGui::PushItemWidth(60);
                        ImGui::DragFloat("##cv", &cond.value, 0.01f);
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##rc")) toRemoveCond = ci;
                        ImGui::PopID();
                    }
                    if (toRemoveCond >= 0)
                        trans.conditions.erase(trans.conditions.begin() + toRemoveCond);
                    if (ImGui::SmallButton("+ Condition"))
                        trans.conditions.push_back(TransitionCondition{});

                    ImGui::PopID();
                    ImGui::Separator();
                }
            }
        );

        // Blend Tree state node — shares the full state detail panel
        // (the isBlendTree checkbox inside it switches the view)
        s_ctrlGraph.registerNodeType("State", "CtrlBlendTree", "Blend Tree",
            [pinState](uint32_t nodeId) {
                s_ctrlGraph.addInputPin(nodeId,  "In",  pinState);
                s_ctrlGraph.addOutputPin(nodeId, "Out", pinState);
            },
            nullptr,
            [controller, animator, clipNames, processDroppedAnimationAsset, this](Engine::Node& n) {

                // Re-use the same logic: userData points to an AnimationState
                if (!n.userData) { ImGui::TextDisabled("Invalid state reference."); return; }
                AnimationState* state = static_cast<AnimationState*>(n.userData);

                // --- State Name ---
                ImGui::TextDisabled("State Name:");
                char nameBuf[128];
                strncpy_s(nameBuf, state->name.c_str(), sizeof(nameBuf) - 1);
                ImGui::PushItemWidth(-1);
                if (ImGui::InputText("##btStateName", nameBuf, sizeof(nameBuf)))
                    state->name = nameBuf;
                ImGui::PopItemWidth();
                ImGui::Spacing();

                // Force isBlendTree = true for this node type
                state->isBlendTree = true;

                // ---------------------------------------------------------------
                // Blend Tree Editor
                // ---------------------------------------------------------------
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 220, 200, 255));
                ImGui::TextUnformatted("Blend Tree");
                ImGui::PopStyleColor();
                ImGui::Separator();

                // Blend type toggle
                bool is2D = state->blendTree.is2D;
                if (ImGui::RadioButton("1D##bt1d", !is2D)) state->blendTree.is2D = false;
                ImGui::SameLine();
                if (ImGui::RadioButton("2D##bt2d",  is2D)) state->blendTree.is2D = true;

                ImGui::Spacing();

                // Parameter name(s)
                std::vector<std::string> pList;
                if (controller) {
                    for (const auto& [pn, pv] : controller->parameters) pList.push_back(pn);
                }

                if (!state->blendTree.is2D) {
                    ImGui::TextDisabled("Parameter (X):");
                    if (!pList.empty()) {
                        ImGui::PushItemWidth(-1);
                        if (ImGui::BeginCombo("##btPCombo", state->blendTree.parameterName.empty() ? "(select param)" : state->blendTree.parameterName.c_str())) {
                            for (const auto& pn : pList) {
                                bool sSel = (pn == state->blendTree.parameterName);
                                if (ImGui::Selectable(pn.c_str(), sSel)) state->blendTree.parameterName = pn;
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();
                    } else {
                        char pBuf[64]; strncpy_s(pBuf, state->blendTree.parameterName.c_str(), sizeof(pBuf)-1);
                        ImGui::PushItemWidth(-1);
                        if (ImGui::InputText("##btP", pBuf, sizeof(pBuf)))
                            state->blendTree.parameterName = pBuf;
                        ImGui::PopItemWidth();
                    }
                } else {
                    ImGui::TextDisabled("Parameter X:");
                    if (!pList.empty()) {
                        ImGui::PushItemWidth(-1);
                        if (ImGui::BeginCombo("##btPXCombo", state->blendTree.parameterName.empty() ? "(select param)" : state->blendTree.parameterName.c_str())) {
                            for (const auto& pn : pList) {
                                bool sSel = (pn == state->blendTree.parameterName);
                                if (ImGui::Selectable(pn.c_str(), sSel)) state->blendTree.parameterName = pn;
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();
                    } else {
                        char pBuf[64]; strncpy_s(pBuf, state->blendTree.parameterName.c_str(), sizeof(pBuf)-1);
                        ImGui::PushItemWidth(-1);
                        if (ImGui::InputText("##btPX", pBuf, sizeof(pBuf)))
                            state->blendTree.parameterName = pBuf;
                        ImGui::PopItemWidth();
                    }

                    ImGui::TextDisabled("Parameter Y:");
                    if (!pList.empty()) {
                        ImGui::PushItemWidth(-1);
                        if (ImGui::BeginCombo("##btPYCombo", state->blendTree.parameterYName.empty() ? "(select param)" : state->blendTree.parameterYName.c_str())) {
                            for (const auto& pn : pList) {
                                bool sSel = (pn == state->blendTree.parameterYName);
                                if (ImGui::Selectable(pn.c_str(), sSel)) state->blendTree.parameterYName = pn;
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();
                    } else {
                        char pyBuf[64]; strncpy_s(pyBuf, state->blendTree.parameterYName.c_str(), sizeof(pyBuf)-1);
                        ImGui::PushItemWidth(-1);
                        if (ImGui::InputText("##btPY", pyBuf, sizeof(pyBuf)))
                            state->blendTree.parameterYName = pyBuf;
                        ImGui::PopItemWidth();
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Motion List:");

                // --- 1D visual threshold bar ---
                if (!state->blendTree.is2D && !state->blendTree.nodes.empty()) {
                    // Find min/max threshold range
                    float tMin = state->blendTree.nodes[0].threshold;
                    float tMax = state->blendTree.nodes[0].threshold;
                    for (auto& bn : state->blendTree.nodes) {
                        tMin = std::min(tMin, bn.threshold);
                        tMax = std::max(tMax, bn.threshold);
                    }
                    if (tMax - tMin < 0.01f) tMax = tMin + 1.0f;

                    ImVec2 barPos = ImGui::GetCursorScreenPos();
                    float barW = ImGui::GetContentRegionAvail().x;
                    float barH = 8.0f;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                        IM_COL32(50, 50, 50, 255), 3.0f);
                    for (auto& bn : state->blendTree.nodes) {
                        float t = (bn.threshold - tMin) / (tMax - tMin);
                        float px = barPos.x + t * barW;
                        dl->AddTriangleFilled(
                            ImVec2(px, barPos.y + barH),
                            ImVec2(px - 5.0f, barPos.y + barH + 6.0f),
                            ImVec2(px + 5.0f, barPos.y + barH + 6.0f),
                            IM_COL32(255, 165, 80, 255));
                    }
                    ImGui::Dummy(ImVec2(barW, barH + 8.0f));
                    ImGui::Spacing();
                }

                // --- Motion rows ---
                int toRemove = -1;
                for (int bi = 0; bi < (int)state->blendTree.nodes.size(); ++bi) {
                    ImGui::PushID(bi);
                    auto& bn = state->blendTree.nodes[bi];

                    // Row: clip combo
                    ImGui::PushItemWidth(-58);
                    const char* clipLabel = bn.clipName.empty() ? "(none)" : bn.clipName.c_str();
                    if (ImGui::BeginCombo("##bclip", clipLabel)) {
                        if (ImGui::Selectable("(none)", bn.clipName.empty()))
                            bn.clipName.clear();
                        for (const auto& cn : clipNames) {
                            bool sel = (cn == bn.clipName);
                            if (ImGui::Selectable(cn.c_str(), sel)) {
                                bn.clipName = cn;
                                ensureClipLoaded(animator, registry, selectedEntity, cn, renderer);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        std::string droppedPath = acceptDroppedAssetPath();
                        if (!droppedPath.empty()) {
                            std::string resName = processDroppedAnimationAsset(droppedPath);
                            if (!resName.empty()) {
                                bn.clipName = resName;
                                ensureClipLoaded(animator, registry, selectedEntity, resName, renderer);
                                statusMessage = "Set motion clip to '" + resName + "'.";
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-##rm")) toRemove = bi;

                    // Threshold
                    if (!state->blendTree.is2D) {
                        ImGui::PushItemWidth(-1);
                        ImGui::DragFloat("##thr", &bn.threshold, 0.01f, -999.f, 999.f, "Thr %.2f");
                        ImGui::PopItemWidth();
                    } else {
                        ImGui::PushItemWidth((ImGui::GetContentRegionAvail().x - 4) * 0.5f);
                        ImGui::DragFloat("##thrX", &bn.threshold2D.x, 0.01f, -999.f, 999.f, "X:%.2f");
                        ImGui::PopItemWidth();
                        ImGui::SameLine(0, 4);
                        ImGui::PushItemWidth(-1);
                        ImGui::DragFloat("##thrY", &bn.threshold2D.y, 0.01f, -999.f, 999.f, "Y:%.2f");
                        ImGui::PopItemWidth();
                    }

                    ImGui::PopID();
                    ImGui::Spacing();
                }
                if (toRemove >= 0)
                    state->blendTree.nodes.erase(state->blendTree.nodes.begin() + toRemove);
                if (ImGui::Button("+ Add Motion", ImVec2(-1, 0)))
                    state->blendTree.nodes.push_back(BlendNode{});
                if (ImGui::BeginDragDropTarget()) {
                    std::string droppedPath = acceptDroppedAssetPath();
                    if (!droppedPath.empty()) {
                        std::string resName = processDroppedAnimationAsset(droppedPath);
                        if (!resName.empty()) {
                            BlendNode newBn;
                            newBn.clipName = resName;
                            state->blendTree.nodes.push_back(newBn);
                            statusMessage = "Added motion clip '" + resName + "' to blend tree.";
                        }
                    }
                    ImGui::EndDragDropTarget();
                }


                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Playback:");
                ImGui::DragFloat("Speed##btSpeed", &state->speed, 0.01f, 0.0f, 10.0f);
                ImGui::Checkbox("Loop##btLoop", &state->isLooping);

                // Outgoing transitions
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Outgoing Transitions:");
                for (auto& trans : controller->transitions) {
                    if (trans.fromState != state->name) continue;
                    ImGui::PushID(&trans);
                    ImGui::Text("-> %s", trans.toState.c_str());
                    ImGui::SameLine();
                    ImGui::DragFloat("Fade##btxf", &trans.crossfadeDuration, 0.01f, 0.0f, 2.0f);
                    int toRemoveCond = -1;
                    for (int ci = 0; ci < (int)trans.conditions.size(); ++ci) {
                        ImGui::PushID(ci);
                        auto& cond = trans.conditions[ci];
                        char pBuf2[64]; strncpy_s(pBuf2, cond.parameterName.c_str(), sizeof(pBuf2)-1);
                        ImGui::PushItemWidth(75); if (ImGui::InputText("##bcp", pBuf2, sizeof(pBuf2))) cond.parameterName = pBuf2; ImGui::PopItemWidth();
                        ImGui::SameLine();
                        const char* ops[] = { ">", "<", "==" };
                        int opIdx = (cond.op == ">") ? 0 : (cond.op == "<") ? 1 : 2;
                        ImGui::PushItemWidth(42); if (ImGui::Combo("##bcop", &opIdx, ops, 3)) cond.op = ops[opIdx]; ImGui::PopItemWidth();
                        ImGui::SameLine();
                        ImGui::PushItemWidth(55); ImGui::DragFloat("##bcv", &cond.value, 0.01f); ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##brc")) toRemoveCond = ci;
                        ImGui::PopID();
                    }
                    if (toRemoveCond >= 0)
                        trans.conditions.erase(trans.conditions.begin() + toRemoveCond);
                    if (ImGui::SmallButton("+ Cond##btcond"))
                        trans.conditions.push_back(TransitionCondition{});
                    ImGui::PopID();
                    ImGui::Separator();
                }
            }
        );

        // -----------------------------------------------------------------------
        // Spawn Entry + Any State nodes
        // -----------------------------------------------------------------------
        uint32_t entryId = s_ctrlGraph.createNode("Entry", "CtrlEntry", ImVec2(20.0f, 160.0f));
        s_ctrlGraph.setNodeHeaderColor(entryId, IM_COL32(40, 140, 60, 255));
        s_ctrlGraph.addOutputPin(entryId, "Start", pinState);

        uint32_t anyId = s_ctrlGraph.createNode("Any State", "CtrlAnyState", ImVec2(20.0f, 260.0f));
        s_ctrlGraph.setNodeHeaderColor(anyId, IM_COL32(100, 40, 130, 255));
        s_ctrlGraph.addOutputPin(anyId, "To", pinState);

        // -----------------------------------------------------------------------
        // Spawn one node per state
        // -----------------------------------------------------------------------
        std::vector<uint32_t> stateNodeIds;
        float xOff = 260.0f;
        for (size_t si = 0; si < controller->states.size(); ++si) {
            auto& state = controller->states[si];
            bool isBT = state.isBlendTree;

            ImVec2 spawnPos = ImVec2(xOff, 80.0f + si * 110.0f);
            auto itPos = s_statePositions.find(state.name);
            if (itPos != s_statePositions.end()) {
                spawnPos = itPos->second;
            }

            uint32_t nid = s_ctrlGraph.createNode(
                state.name,
                isBT ? "CtrlBlendTree" : "CtrlState",
                spawnPos
            );


            // Header color: blue for regular, teal for blend tree
            s_ctrlGraph.setNodeHeaderColor(nid, isBT
                ? IM_COL32(30, 110, 110, 255)
                : IM_COL32(40, 80,  150, 255));

            Engine::NodePinType pinState2{ "state", IM_COL32(255, 165, 80, 255) };
            s_ctrlGraph.addInputPin(nid,  "In",  pinState2);
            s_ctrlGraph.addOutputPin(nid, "Out", pinState2);

            // Bind userData to the actual state (pointer is stable while controller lives)
            if (Engine::Node* n = s_ctrlGraph.findNode(nid))
                n->userData = &controller->states[si];

            stateNodeIds.push_back(nid);
        }

        // Restore previous selection accurately
        uint32_t restoreSelId = 0;
        if (selectedTypeName == "CtrlEntry") {
            restoreSelId = entryId;
        } else if (selectedTypeName == "CtrlAnyState") {
            restoreSelId = anyId;
        } else if (!selectedStateName.empty()) {
            for (uint32_t nid : stateNodeIds) {
                if (Engine::Node* n = s_ctrlGraph.findNode(nid)) {
                    if (n->userData && static_cast<AnimationState*>(n->userData)->name == selectedStateName) {
                        restoreSelId = nid;
                        break;
                    }
                }
            }
        }
        s_ctrlGraph.setSelectedNodeId(restoreSelId);

        // -----------------------------------------------------------------------
        // Spawn links for existing transitions
        // -----------------------------------------------------------------------
        // Build fromStateName -> node output pin id map
        // State name -> {nodeId, inPinId, outPinId}
        struct StateNodeInfo { uint32_t nodeId, inPinId, outPinId; };
        std::vector<StateNodeInfo> stateInfos;
        for (size_t si = 0; si < controller->states.size(); ++si) {
            uint32_t nid = stateNodeIds[si];
            Engine::Node* n = s_ctrlGraph.findNode(nid);
            if (!n) continue;
            uint32_t inPin = n->inputs.empty()  ? 0 : n->inputs[0].id;
            uint32_t outPin = n->outputs.empty() ? 0 : n->outputs[0].id;
            stateInfos.push_back({nid, inPin, outPin});
        }

        // Entry -> first state (if any)
        if (!stateInfos.empty() && !controller->currentState.empty()) {
            Engine::Node* entryNode = s_ctrlGraph.findNode(entryId);
            if (entryNode && !entryNode->outputs.empty()) {
                // Find the default state
                for (size_t si = 0; si < controller->states.size(); ++si) {
                    if (controller->states[si].name == controller->currentState) {
                        s_ctrlGraph.addLink(entryNode->outputs[0].id, stateInfos[si].inPinId);
                        break;
                    }
                }
            }
        }

        for (const auto& trans : controller->transitions) {
            uint32_t fromOutPin = 0, toInPin = 0;

            // Resolve from: could be "Any State" or a regular state
            if (trans.fromState == "Any State") {
                Engine::Node* anyNode = s_ctrlGraph.findNode(anyId);
                if (anyNode && !anyNode->outputs.empty())
                    fromOutPin = anyNode->outputs[0].id;
            } else {
                for (size_t si = 0; si < controller->states.size(); ++si) {
                    if (controller->states[si].name == trans.fromState && si < stateInfos.size()) {
                        fromOutPin = stateInfos[si].outPinId;
                        break;
                    }
                }
            }
            for (size_t si = 0; si < controller->states.size(); ++si) {
                if (controller->states[si].name == trans.toState && si < stateInfos.size()) {
                    toInPin = stateInfos[si].inPinId;
                    break;
                }
            }

            if (fromOutPin && toInPin)
                s_ctrlGraph.addLink(fromOutPin, toInPin);
        }

        // -----------------------------------------------------------------------
        // Register link created/deleted callbacks
        // -----------------------------------------------------------------------
        s_ctrlGraph.onLinkCreated = [controller, &stateInfos, anyId, entryId, this](uint32_t fromPin, uint32_t toPin) {
            // Identify from/to state names from pin -> node -> state
            auto resolveStateName = [&](uint32_t pinId, bool isOutput) -> std::string {
                Engine::NodePin* p = s_ctrlGraph.findPin(pinId);
                if (!p) return "";
                Engine::Node* n = s_ctrlGraph.findNode(p->nodeId);
                if (!n) return "";
                if (n->id == anyId) return "Any State";
                if (n->id == entryId) return "__Entry__";
                if (n->userData) return static_cast<AnimationState*>(n->userData)->name;
                return "";
            };

            std::string from = resolveStateName(fromPin, true);
            std::string to   = resolveStateName(toPin,  false);
            if (from.empty() || to.empty()) return;

            if (from == "__Entry__") {
                controller->currentState = to;
                controller->currentStateTime = 0.0f;
                s_lastCurrentState = controller->currentState;
                statusMessage = "Set default active state to '" + to + "'.";
                return;
            }

            // Check not a duplicate
            for (const auto& t : controller->transitions)
                if (t.fromState == from && t.toState == to) return;

            AnimationTransition newTrans;
            newTrans.fromState = from;
            newTrans.toState   = to;
            newTrans.crossfadeDuration = 0.2f;
            controller->transitions.push_back(newTrans);
            s_lastTransitionCount = controller->transitions.size();
        };

        s_ctrlGraph.onLinkDeleted = [controller, anyId, entryId, this](uint32_t fromPin, uint32_t toPin) {
            auto resolveStateName = [&](uint32_t pinId) -> std::string {
                Engine::NodePin* p = s_ctrlGraph.findPin(pinId);
                if (!p) return "";
                Engine::Node* n = s_ctrlGraph.findNode(p->nodeId);
                if (!n) return "";
                if (n->id == anyId) return "Any State";
                if (n->id == entryId) return "__Entry__";
                if (n->userData) return static_cast<AnimationState*>(n->userData)->name;
                return "";
            };

            std::string from = resolveStateName(fromPin);
            std::string to   = resolveStateName(toPin);

            if (from == "__Entry__") {
                if (controller->currentState == to) {
                    controller->currentState.clear();
                    s_lastCurrentState = controller->currentState;
                    statusMessage = "Cleared default active state.";
                }
                return;
            }

            controller->transitions.erase(
                std::remove_if(controller->transitions.begin(), controller->transitions.end(),
                    [&](const AnimationTransition& t) {
                        return t.fromState == from && t.toState == to;
                    }),
                controller->transitions.end());
            s_lastTransitionCount = controller->transitions.size();
        };

        // When a state node is deleted, remove the matching AnimationState
        // and all transitions referencing it, then force a graph rebuild.
        s_ctrlGraph.onNodeDeleted = [controller](uint32_t nodeId) {
            // Find which state this node was linked to via userData
            // (userData was already cleared by the time callback fires, so we
            //  scan transitions for dangling state names after deletion)
            // Simpler: force a rebuild flag so the graph re-syncs from controller->states.
            // The actual removal is handled by the user pressing delete in the detail panel,
            // OR we can scan the graph nodes for orphaned states.
            // Best approach: mark rebuild needed.
            (void)nodeId;
            // We'll detect the mismatch next frame via s_lastStateCount check.
            // However we also need to remove states whose userData node no longer exists.
            // We do this by collecting all userDatas still alive in the graph:
            std::vector<AnimationState*> alive;
            for (const auto& gn : s_ctrlGraph.getNodes()) {
                if (gn.userData)
                    alive.push_back(static_cast<AnimationState*>(gn.userData));
            }
            // Remove states not in alive list
            auto removedIt = std::remove_if(controller->states.begin(), controller->states.end(),
                [&](const AnimationState& st) {
                    for (auto* a : alive)
                        if (a == &st) return false;
                    return true;
                });
            // Remove transitions referencing deleted state names
            std::vector<std::string> removedNames;
            for (auto it = removedIt; it != controller->states.end(); ++it)
                removedNames.push_back(it->name);
            controller->states.erase(removedIt, controller->states.end());
            for (const auto& rn : removedNames) {
                controller->transitions.erase(
                    std::remove_if(controller->transitions.begin(), controller->transitions.end(),
                        [&](const AnimationTransition& t) {
                            return t.fromState == rn || t.toState == rn;
                        }),
                    controller->transitions.end());
            }
            s_lastStateCount = (size_t)-1; // Force graph rebuild next frame
        };


        // Drag and drop animation files from Asset Browser onto grid area -> create state node at drop position
        s_ctrlGraph.onCanvasAssetDropped = [controller, processDroppedAnimationAsset, this](const std::string& assetPath, const ImVec2& dropCanvasPos) {

            std::string clipName = processDroppedAnimationAsset(assetPath);
            if (!clipName.empty()) {
                bool exists = false;
                for (const auto& st : controller->states) {
                    if (st.name == clipName) { exists = true; break; }
                }
                if (!exists) {
                    AnimationState newState;
                    newState.name = clipName;
                    newState.clipName = clipName;
                    controller->states.push_back(newState);
                    s_statePositions[clipName] = dropCanvasPos;
                    s_lastStateCount = (size_t)-1; // Force graph rebuild next tick
                    statusMessage = "Created animation state '" + clipName + "' from asset drop onto grid.";
                }
            }
        };

        // Drag and drop animation files from Asset Browser onto details area -> set clip or add motion
        s_ctrlGraph.onDetailPanelAssetDropped = [controller, processDroppedAnimationAsset, this](const std::string& assetPath) {
            uint32_t selId = s_ctrlGraph.getSelectedNodeId();
            Engine::Node* selNode = s_ctrlGraph.findNode(selId);
            if (!selNode || !selNode->userData) return;
            AnimationState* state = static_cast<AnimationState*>(selNode->userData);
            std::string clipName = processDroppedAnimationAsset(assetPath);
            std::string relPath = assetPath;
            try {
                std::filesystem::path p(assetPath);
                if (p.is_absolute()) {
                    relPath = std::filesystem::relative(p, std::filesystem::current_path()).generic_string();
                } else {
                    relPath = p.generic_string();
                }
            } catch (...) {}

            std::string storedName = clipName.empty() ? relPath : clipName;
            if (storedName.empty()) return;

            if (!state->isBlendTree) {
                state->clipName = storedName;
                statusMessage = "Assigned animation clip '" + storedName + "' to state '" + state->name + "'.";
            } else {
                BlendNode bn;
                bn.clipName = storedName;
                state->blendTree.nodes.push_back(bn);
                statusMessage = "Added motion clip '" + storedName + "' to blend tree.";
            }
        };

        // When a node is created via right-click context menu on canvas -> add AnimationState to controller
        s_ctrlGraph.onNodeCreated = [controller, this](uint32_t nodeId, const std::string& typeName, const ImVec2& spawnPos) {
            if (typeName == "CtrlState" || typeName == "CtrlBlendTree") {
                bool isBT = (typeName == "CtrlBlendTree");
                AnimationState newState;
                newState.name = (isBT ? "Blend Tree " : "New State ") + std::to_string(controller->states.size() + 1);
                newState.isBlendTree = isBT;
                controller->states.push_back(newState);
                s_statePositions[newState.name] = spawnPos;
                s_lastStateCount = (size_t)-1; // Force graph rebuild so node userData is bound to state
                statusMessage = "Created new " + std::string(isBT ? "Blend Tree" : "State") + " node.";
            }
        };

        s_lastBuiltEntityId    = selectedEntity.getId();
        s_lastStateCount       = controller->states.size();
        s_lastTransitionCount  = controller->transitions.size();
        s_lastCurrentState     = controller->currentState;
    };

    // Rebuild when entity, state list, transitions, or default state changes
    if (s_lastBuiltEntityId    != selectedEntity.getId() ||
        s_lastStateCount       != controller->states.size() ||
        s_lastTransitionCount  != controller->transitions.size() ||
        s_lastCurrentState     != controller->currentState) {
        rebuildGraph();
    }

    // -----------------------------------------------------------------------
    // Layout: [Left param panel 140px] | [NodeGraph + built-in detail panel]
    // -----------------------------------------------------------------------
    float leftW = 140.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    // --- Left: Parameters ---
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(25, 25, 30, 255));
    ImGui::BeginChild("##ctrl_params", ImVec2(leftW, avail.y), true);
    ImGui::TextDisabled("Parameters");
    ImGui::Separator();

    // Add parameter
    static char s_newParamName[64] = "speed";
    ImGui::PushItemWidth(-1);
    ImGui::InputText("##newParamName", s_newParamName, sizeof(s_newParamName));
    ImGui::PopItemWidth();
    if (ImGui::Button("+ Add Param", ImVec2(-1, 0))) {
        if (strlen(s_newParamName) > 0 && controller->parameters.find(s_newParamName) == controller->parameters.end()) {
            controller->parameters[s_newParamName] = 0.0f;
        }
    }
    ImGui::Separator();

    std::vector<std::string> toRemoveParams;
    for (auto& [name, val] : controller->parameters) {
        ImGui::PushID(name.c_str());
        ImGui::TextUnformatted(name.c_str());
        ImGui::PushItemWidth(-22);
        ImGui::DragFloat("##pval", &val, 0.01f);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) toRemoveParams.push_back(name);
        ImGui::PopID();
    }
    for (const auto& rp : toRemoveParams) controller->parameters.erase(rp);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Add State:");
    if (ImGui::Button("+ State", ImVec2(-1, 0))) {
        AnimationState newState;
        newState.name = "New State " + std::to_string(controller->states.size());
        controller->states.push_back(newState);
        // Force rebuild on next frame
        s_lastStateCount = (size_t)-1;
    }
    if (ImGui::Button("+ Blend Tree", ImVec2(-1, 0))) {
        AnimationState newState;
        newState.name = "Blend Tree " + std::to_string(controller->states.size());
        newState.isBlendTree = true;
        controller->states.push_back(newState);
        s_lastStateCount = (size_t)-1;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Checkbox("Preview Mode", &animator->isPreviewing);

    if (!controller->currentState.empty()) {
        ImGui::TextDisabled("Active State:");
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 220, 100, 255));
        ImGui::TextWrapped("%s", controller->currentState.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 4.0f);

    // --- Center + Right: NodeGraph (framework renders canvas + detail panel) ---
    float graphW = avail.x - leftW - 4.0f;
    s_ctrlGraph.draw("AnimCtrlGraph", ImVec2(graphW, avail.y), 260.0f);

    // Sync dragged node positions back so node dragging is continuously saved
    for (const auto& gn : s_ctrlGraph.getNodes()) {
        if (gn.userData) {
            AnimationState* st = static_cast<AnimationState*>(gn.userData);
            s_statePositions[st->name] = gn.position;
        }
    }

    ImGui::End();
}

void EditorUI::drawProfilerPanel() {
    ImGui::SetNextWindowSize(ImVec2(940, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Profiler & Diagnostics", &showProfilerPanel)) {
        ImGui::End();
        return;
    }

    auto& profiler = Engine::Profiler::getInstance();
    bool isPaused = profiler.isPaused();
    int selectedFrameIdx = profiler.getSelectedFrameIndex();

    // Top Header & Controls
    ImGui::BeginGroup();
    if (ImGui::Button(isPaused ? "  Resume  " : "  Pause  ")) {
        profiler.setPaused(!isPaused);
    }
    ImGui::SameLine();
    if (selectedFrameIdx >= 0) {
        if (ImGui::Button("  Live Unfreeze  ")) {
            profiler.setSelectedFrameIndex(-1);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "[FROZEN AT FRAME #%d]", selectedFrameIdx);
        ImGui::SameLine();
    }
    if (ImGui::Button("  Clear Data  ")) {
        profiler.clearHistory();
    }
    ImGui::SameLine();

    static int s_targetBudgetIdx = 0; // 0 = 60 FPS (16.66ms), 1 = 144 FPS (6.94ms), 2 = 240 FPS (4.16ms)
    const char* budgets[] = { "60 FPS (16.66ms)", "144 FPS (6.94ms)", "240 FPS (4.16ms)" };
    ImGui::SetNextItemWidth(160);
    ImGui::Combo("Target Budget", &s_targetBudgetIdx, budgets, IM_ARRAYSIZE(budgets));
    ImGui::EndGroup();

    float targetFrameMs = (s_targetBudgetIdx == 0) ? 16.66f : (s_targetBudgetIdx == 1 ? 6.94f : 4.16f);

    ImGui::Separator();

    const auto& history = profiler.getFrameHistory();
    Engine::FrameProfileData frameData = profiler.getLatestFrame();
    double globalTotalFrameMs = frameData.frameTimeMs > 0.0 ? frameData.frameTimeMs : static_cast<double>(targetFrameMs);

    // Line Category Toggles Bar
    static bool s_showTotalLine = true;
    static bool s_showRenderingLine = true;
    static bool s_showPhysicsLine = true;
    static bool s_showSystemsLine = true;
    static bool s_showEcsCoreLine = true;
    static bool s_showEditorUiLine = true;

    ImGui::TextDisabled("LINES:"); ImGui::SameLine();
    ImGui::Checkbox("Total", &s_showTotalLine); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.56f, 0.99f, 1.0f));
    ImGui::Checkbox("Rendering", &s_showRenderingLine); ImGui::PopStyleColor(); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.18f, 0.8f, 0.44f, 1.0f));
    ImGui::Checkbox("Physics", &s_showPhysicsLine); ImGui::PopStyleColor(); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.61f, 0.07f, 1.0f));
    ImGui::Checkbox("Systems", &s_showSystemsLine); ImGui::PopStyleColor(); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.61f, 0.35f, 0.71f, 1.0f));
    ImGui::Checkbox("ECS Core", &s_showEcsCoreLine); ImGui::PopStyleColor(); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.74f, 0.61f, 1.0f));
    ImGui::Checkbox("Editor UI", &s_showEditorUiLine); ImGui::PopStyleColor();

    // Multi-Line Graph Canvas
    if (!history.empty()) {
        ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        float canvasWidth = ImGui::GetContentRegionAvail().x;
        float canvasHeight = 130.0f;
        ImVec2 canvasMax = ImVec2(canvasMin.x + canvasWidth, canvasMin.y + canvasHeight);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(20, 22, 26, 255), 4.0f);
        drawList->AddRect(canvasMin, canvasMax, IM_COL32(50, 55, 65, 255), 4.0f);

        float maxMs = targetFrameMs * 2.0f;
        for (const auto& f : history) {
            if (f.frameTimeMs > maxMs) maxMs = static_cast<float>(f.frameTimeMs);
        }

        // Draw Budget Line
        float targetY = canvasMax.y - (targetFrameMs / maxMs) * canvasHeight;
        drawList->AddLine(ImVec2(canvasMin.x, targetY), ImVec2(canvasMax.x, targetY), IM_COL32(255, 255, 255, 100), 1.5f);

        size_t count = history.size();
        if (count >= 2) {
            std::vector<ImVec2> totalPts(count), rendPts(count), physPts(count), sysPts(count), ecsPts(count), uiPts(count);

            for (size_t i = 0; i < count; ++i) {
                float x = canvasMin.x + (static_cast<float>(i) / static_cast<float>(count - 1)) * canvasWidth;
                const auto& f = history[i];

                totalPts[i] = ImVec2(x, canvasMax.y - (static_cast<float>(f.frameTimeMs) / maxMs) * canvasHeight);
                rendPts[i]  = ImVec2(x, canvasMax.y - (static_cast<float>(f.renderingMs) / maxMs) * canvasHeight);
                physPts[i]  = ImVec2(x, canvasMax.y - (static_cast<float>(f.physicsMs) / maxMs) * canvasHeight);
                sysPts[i]   = ImVec2(x, canvasMax.y - (static_cast<float>(f.systemsMs) / maxMs) * canvasHeight);
                ecsPts[i]   = ImVec2(x, canvasMax.y - (static_cast<float>(f.ecsCoreMs) / maxMs) * canvasHeight);
                uiPts[i]    = ImVec2(x, canvasMax.y - (static_cast<float>(f.editorUiMs) / maxMs) * canvasHeight);
            }

            if (s_showRenderingLine) drawList->AddPolyline(rendPts.data(), static_cast<int>(count), IM_COL32(52, 152, 219, 255), false, 2.0f);
            if (s_showPhysicsLine)   drawList->AddPolyline(physPts.data(), static_cast<int>(count), IM_COL32(46, 204, 113, 255), false, 2.0f);
            if (s_showSystemsLine)   drawList->AddPolyline(sysPts.data(), static_cast<int>(count), IM_COL32(243, 156, 18, 255), false, 2.0f);
            if (s_showEcsCoreLine)   drawList->AddPolyline(ecsPts.data(), static_cast<int>(count), IM_COL32(155, 89, 182, 255), false, 2.0f);
            if (s_showEditorUiLine)  drawList->AddPolyline(uiPts.data(), static_cast<int>(count), IM_COL32(26, 188, 156, 255), false, 2.0f);
            if (s_showTotalLine)     drawList->AddPolyline(totalPts.data(), static_cast<int>(count), IM_COL32(231, 76, 60, 255), false, 2.5f);
        }

        // Invisible button over canvas for interaction
        ImGui::SetCursorScreenPos(canvasMin);
        ImGui::InvisibleButton("##MultiLineCanvas", ImVec2(canvasWidth, canvasHeight));

        if (ImGui::IsItemHovered()) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float fraction = (mousePos.x - canvasMin.x) / canvasWidth;
            int hoveredIdx = static_cast<int>(fraction * count);
            if (hoveredIdx >= 0 && hoveredIdx < static_cast<int>(count)) {
                const auto& hFrame = history[hoveredIdx];
                double hTotal = hFrame.frameTimeMs > 0.0 ? hFrame.frameTimeMs : 1.0;

                ImGui::BeginTooltip();
                ImGui::Text("Frame #%d (Total: %.2f ms, %.1f FPS)", hoveredIdx, hFrame.frameTimeMs, hFrame.fps);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.95f, 0.61f, 0.07f, 1.0f), "Systems: %.2f ms (%.1f%%)", hFrame.systemsMs, hFrame.systemsMs / hTotal * 100.0);
                ImGui::TextColored(ImVec4(0.3f, 0.56f, 0.99f, 1.0f), "Rendering: %.2f ms (%.1f%%)", hFrame.renderingMs, hFrame.renderingMs / hTotal * 100.0);
                ImGui::TextColored(ImVec4(0.18f, 0.8f, 0.44f, 1.0f), "Physics: %.2f ms (%.1f%%)", hFrame.physicsMs, hFrame.physicsMs / hTotal * 100.0);
                ImGui::TextColored(ImVec4(0.61f, 0.35f, 0.71f, 1.0f), "ECS Core: %.2f ms (%.1f%%)", hFrame.ecsCoreMs, hFrame.ecsCoreMs / hTotal * 100.0);
                ImGui::TextColored(ImVec4(0.1f, 0.74f, 0.61f, 1.0f), "Editor UI: %.2f ms (%.1f%%)", hFrame.editorUiMs, hFrame.editorUiMs / hTotal * 100.0);
                ImGui::EndTooltip();

                if (ImGui::IsMouseClicked(0)) {
                    profiler.setSelectedFrameIndex(hoveredIdx);
                }
            }
        }
    } else {
        ImGui::TextDisabled("No frame history recorded yet.");
    }

    ImGui::Spacing();

    if (ImGui::BeginTabBar("ProfilerTabs")) {

        // TAB 1: Global Comparative Analysis
        if (ImGui::BeginTabItem("Global Comparison")) {
            std::vector<Engine::FunctionStats> fnStats = profiler.getFunctionStats();

            ImGui::Text("Function & Scope Cost Breakdown (Global Frame Total = %.2f ms)", globalTotalFrameMs);
            ImGui::Spacing();

            static ImGuiTableFlags globalTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;

            if (ImGui::BeginTable("GlobalComparativeTable", 8, globalTableFlags)) {
                ImGui::TableSetupColumn("Function / Component", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Calls/Frame", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                ImGui::TableSetupColumn("% Global Frame", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                ImGui::TableSetupColumn("Max (ms)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                ImGui::TableSetupColumn("Global Budget Share Bar", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableHeadersRow();

                for (const auto& stat : fnStats) {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", stat.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImVec4 catColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                    switch (stat.category) {
                        case Engine::ProfileCategory::Rendering: catColor = ImVec4(0.3f, 0.56f, 0.99f, 1.0f); ImGui::TextColored(catColor, "Rendering"); break;
                        case Engine::ProfileCategory::Physics:   catColor = ImVec4(0.18f, 0.8f, 0.44f, 1.0f); ImGui::TextColored(catColor, "Physics"); break;
                        case Engine::ProfileCategory::Systems:   catColor = ImVec4(0.95f, 0.61f, 0.07f, 1.0f); ImGui::TextColored(catColor, "Systems"); break;
                        case Engine::ProfileCategory::ECS_Core:  catColor = ImVec4(0.61f, 0.35f, 0.71f, 1.0f); ImGui::TextColored(catColor, "ECS Core"); break;
                        case Engine::ProfileCategory::Editor_UI: catColor = ImVec4(0.1f, 0.74f, 0.61f, 1.0f); ImGui::TextColored(catColor, "Editor UI"); break;
                        default:                                 ImGui::TextDisabled("Custom"); break;
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", stat.avgCallsPerFrame);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", stat.lastMs);

                    ImGui::TableSetColumnIndex(4);
                    double percentGlobal = (globalTotalFrameMs > 0.0) ? (stat.lastMs / globalTotalFrameMs * 100.0) : 0.0;
                    ImGui::Text("%.1f%%", percentGlobal);

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.3f", stat.avgMs);

                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%.3f", stat.maxMs);

                    ImGui::TableSetColumnIndex(7);
                    float fraction = static_cast<float>(stat.lastMs / globalTotalFrameMs);
                    if (fraction > 1.0f) fraction = 1.0f;
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.1f%%", percentGlobal);

                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, catColor);
                    ImGui::ProgressBar(fraction, ImVec2(-1, 0), buf);
                    ImGui::PopStyleColor();
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        // TAB 2: ECS Systems Performance Table
        if (ImGui::BeginTabItem("ECS Systems")) {
            std::vector<Engine::SystemStats> sysStats = profiler.getSystemStats();

            ImGui::Text("Active ECS Systems (%d registered)", static_cast<int>(sysStats.size()));
            ImGui::Spacing();

            static ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;

            if (ImGui::BeginTable("ECSSystemsTable", 7, tableFlags)) {
                ImGui::TableSetupColumn("System Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Last (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Min (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Max (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Global %", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Budget Bar", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();

                for (const auto& stat : sysStats) {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", stat.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", stat.lastMs);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", stat.avgMs);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", stat.minMs);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.3f", stat.maxMs);

                    ImGui::TableSetColumnIndex(5);
                    double percentGlobal = (globalTotalFrameMs > 0.0) ? (stat.lastMs / globalTotalFrameMs * 100.0) : 0.0;
                    ImGui::Text("%.1f%%", percentGlobal);

                    ImGui::TableSetColumnIndex(6);
                    float fraction = static_cast<float>(stat.avgMs / targetFrameMs);
                    if (fraction > 1.0f) fraction = 1.0f;
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.1f%%", stat.percentShare);

                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.95f, 0.61f, 0.07f, 1.0f));
                    ImGui::ProgressBar(fraction, ImVec2(-1, 0), buf);
                    ImGui::PopStyleColor();
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        // TAB 3: Rendering & Scene Stats
        if (ImGui::BeginTabItem("Rendering & Scene")) {
            const auto& rStats = frameData.renderStats;

            ImGui::Columns(2, "RenderStatsCols", false);

            drawSectionHeader("VULKAN RENDERER");
            ImGui::BulletText("Draw Calls: %u", rStats.drawCalls);
            ImGui::BulletText("Triangles Rendered: %u", rStats.triangleCount);
            ImGui::BulletText("Vertices Rendered: %u", rStats.vertexCount);
            ImGui::BulletText("Mesh SoA GPU Buffer: %.2f KB", static_cast<double>(rStats.meshMemoryBytes) / 1024.0);

            ImGui::NextColumn();

            drawSectionHeader("ECS SCENE POPULATION");
            ImGui::BulletText("Active Entities: %u", rStats.entityCount);
            ImGui::BulletText("Tracked Systems: %d", static_cast<int>(profiler.getSystemStats().size()));

            ImGui::Columns(1);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}



