#include "editor/EditorUI.hpp"
#include "editor/EditorUIInternal.hpp"
#include "meta/ComponentReflection.hpp"
#include "ecs/Registry.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Material.hpp"
#include "ecs/components/Mesh.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/PrimitiveType.hpp"
#include "ecs/components/Camera.hpp"
#include "ecs/components/Grid.hpp"
#include "ecs/components/Skeleton.hpp"
#include "ecs/components/Animator.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/EditorCamera.hpp"
#include "ecs/components/AnimationController.hpp"
#include "ecs/components/IKSolver.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Collider.hpp"
#include "ecs/components/PlayerControllerComponent.hpp"
#include "ecs/components/Tilemap.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/PrefabComponent.hpp"
#include "ecs/components/SpriteRenderer.hpp"
#include "ecs/components/LightComponent.hpp"
#include "ecs/components/TerrainComponent.hpp"
#include "ecs/components/GridWorldComponent.hpp"
#include "ecs/systems/TerrainSystem.hpp"
#include "ui/UIBuilder.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "renderer/ResourceManager.hpp"
#include "scenes/Scene.hpp"
#include "scenes/SceneManager.hpp"
#include "scenes/SceneSerializer.hpp"

#include <set>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace ImGui;
using namespace std;

static std::string acceptDroppedAssetPath() {
    if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
        return std::string((const char*)payload->Data);
    }
    if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS")) {
        std::string s((const char*)payload->Data);
        size_t pipe = s.find('|');
        if (pipe != std::string::npos) s = s.substr(0, pipe);
        return s;
    }
    return "";
}

template<typename T>
static bool drawComponentHeaderWithToggle(Registry& registry, Entity entity, const char* title, bool* visible = nullptr, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen) {
    bool enabled = registry.isComponentEnabled<T>(entity);
    PushID(title);
    if (Checkbox("##compEnabled", &enabled)) {
        registry.setComponentEnabled<T>(entity, enabled);
    }
    SameLine();
    PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.16f, 0.21f, 1.0f));
    PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.22f, 0.29f, 1.0f));
    PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.28f, 0.38f, 1.0f));
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 7.0f));
    bool open = false;
    if (visible) {
        open = CollapsingHeader(title, visible, flags);
    } else {
        open = CollapsingHeader(title, flags);
    }
    PopStyleVar();
    PopStyleColor(3);
    PopID();
    return open;
}

static bool drawReflectedComponentHeaderWithToggle(Registry& registry, Entity entity, const Engine::ComponentReflection& refl, const char* title, bool* visible, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen) {
    bool enabled = refl.isEnabled ? refl.isEnabled(registry, entity) : true;
    PushID(refl.name.c_str());
    if (Checkbox("##compEnabled", &enabled)) {
        if (refl.setEnabled) {
            refl.setEnabled(registry, entity, enabled);
        }
    }
    SameLine();
    PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.16f, 0.21f, 1.0f));
    PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.22f, 0.29f, 1.0f));
    PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.28f, 0.38f, 1.0f));
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 7.0f));
    bool open = CollapsingHeader(title, visible, flags);
    PopStyleVar();
    PopStyleColor(3);
    PopID();
    return open;
}

void EditorUI::drawSectionHeader(const std::string& title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.78f, 0.88f, 1.0f));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Separator();
}

bool EditorUI::drawVec3Control(const char* label, float* values, float speed) {
    bool changed = false;

    PushID(label);

    Columns(2, nullptr, false);
    SetColumnWidth(0, 80.0f);

    TextUnformatted(label);
    NextColumn();

    float width = CalcItemWidth();
    float itemWidth = width / 3.0f - 4.0f;

    PushItemWidth(itemWidth);

    for (int i = 0; i < 3; i++)
    {
        PushID(i);

        changed |= DragFloat("##v", &values[i], speed);

        PopID();

        if (i < 2)
            SameLine();
    }

    PopItemWidth();
    Columns(1);
    PopID();

    return changed;
}

void EditorUI::drawTransformEditor() {
    if (hasSelection && registry.isValid(selectedEntity)) {
        if (registry.has<Engine::CanvasComponent>(selectedEntity) ||
            registry.has<Engine::UIPanelComponent>(selectedEntity) ||
            registry.has<Engine::UITextComponent>(selectedEntity) ||
            registry.has<Engine::UIImageComponent>(selectedEntity) ||
            registry.has<Engine::UIButtonComponent>(selectedEntity)) {
            if (!registry.has<Engine::RectTransform>(selectedEntity)) {
                registry.emplace<Engine::RectTransform>(selectedEntity, Engine::RectTransform{});
            }
        }
    }

    // UI Entities with RectTransform use UI RectTransform in place of 3D Transform
    if (registry.has<Engine::RectTransform>(selectedEntity)) {
        return;
    }

    Transform* transform = registry.get<Transform>(selectedEntity);
    if (!transform || !CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    float position[3] =
    {
        transform->position.x,
        transform->position.y,
        transform->position.z
    };

    if (drawVec3Control("Position", position))
    {
        transform->position =
        {
            position[0],
            position[1],
            position[2]
        };
    }

    float rotation[3] =
    {
        transform->rotation.x,
        transform->rotation.y,
        transform->rotation.z
    };

    if (drawVec3Control("Rotation", rotation, 0.5f))
    {
        transform->rotation =
        {
            rotation[0],
            rotation[1],
            rotation[2]
        };
    }

    float scale[3] =
    {
        transform->scale.x,
        transform->scale.y,
        transform->scale.z
    };

    if (drawVec3Control("Scale", scale, 0.05f))
    {
        transform->scale =
        {
            scale[0],
            scale[1],
            scale[2]
        };
    }

    if (Camera* camera = registry.get<Camera>(selectedEntity)) {
        renderer.setActiveCamera(camera->projection(), transform->position, camera->view(*transform));
    }
}

void EditorUI::drawMaterialEditor() {
    Material* material = registry.get<Material>(selectedEntity);
    if (!material) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<Material>(registry, selectedEntity, "Material", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<Material>(selectedEntity);
        statusMessage = "Removed Material component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }

    // Helper lambda to update descriptors and shaders dynamically
    auto updatePipelineAndDescriptors = [&]() {
        renderer.resourceManager->updateMaterialDescriptorSet(*material, renderer);

        bool hasSkin = entityHasSkin(registry, selectedEntity);
        std::string vertShader = "unlit.vert.spv";
        std::string fragShader = "unlit.frag.spv";
        if (material->shaderName == "Lit") {
            vertShader = hasSkin ? "skinned_lit.vert.spv" : "lit.vert.spv";
            fragShader = "lit.frag.spv";
        } else if (material->shaderName == "Sprite") {
            vertShader = "sprite.vert.spv";
            fragShader = "sprite.frag.spv";
        } else {
            vertShader = hasSkin ? "skinned.vert.spv" : "unlit.vert.spv";
            fragShader = "unlit.frag.spv";
        }

        PipelineHandle pipeline = renderer.createPipelineForShaders(
            renderer.resolveShaderPath("build/shaders/" + vertShader),
            renderer.resolveShaderPath("build/shaders/" + fragShader)
        );
        material->pipeline = pipeline.pipeline;
        material->pipelineLayout = pipeline.layout;
    };

    // 1) Shader Selection
    const char* shaderOptions[] = { "Unlit", "Lit", "Sprite" };
    int currentShaderIdx = 0;
    if (material->shaderName == "Lit") currentShaderIdx = 1;
    else if (material->shaderName == "Sprite") currentShaderIdx = 2;

    if (Combo("Shader", &currentShaderIdx, shaderOptions, IM_ARRAYSIZE(shaderOptions))) {
        material->shaderName = shaderOptions[currentShaderIdx];
        updatePipelineAndDescriptors();
        statusMessage = "Shader changed to: " + material->shaderName;
    }

    Spacing();
    Separator();
    Spacing();

    // 2) Color Picker
    float color[4] = { material->color.r, material->color.g, material->color.b, material->color.a };
    if (ColorEdit4("Base Color", color)) {
        material->color = { color[0], color[1], color[2], color[3] };
    }

    // 3) Diffuse Texture
    char textureBuf[256]{};
    snprintf(textureBuf, sizeof(textureBuf), "%s", material->texturePath.c_str());
    if (InputText("Diffuse Map", textureBuf, sizeof(textureBuf))) {
        material->texturePath = textureBuf;
        updatePipelineAndDescriptors();
    }
    if (BeginDragDropTarget()) {
        const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH");
        if (!payload) payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS");
        if (payload && payload->Data) {
            std::string pathStr((const char*)payload->Data);
            size_t sep = pathStr.find('|');
            if (sep != std::string::npos) pathStr = pathStr.substr(0, sep);
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
            std::string ext = std::filesystem::path(pathStr).extension().string();
            for (char& c : ext) c = (char)::tolower((unsigned char)c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".dds" || ext == ".hdr" || ext == ".webp") {
                material->texturePath = pathStr;
                updatePipelineAndDescriptors();
                statusMessage = "Assigned diffuse texture: " + pathStr;
            } else {
                statusMessage = "Error: Dropped asset is not a valid texture file.";
            }
        }
        EndDragDropTarget();
    }

    // 4) Normal Map
    char normalBuf[256]{};
    snprintf(normalBuf, sizeof(normalBuf), "%s", material->normalMapPath.c_str());
    if (InputText("Normal Map", normalBuf, sizeof(normalBuf))) {
        material->normalMapPath = normalBuf;
        updatePipelineAndDescriptors();
    }
    if (BeginDragDropTarget()) {
        const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH");
        if (!payload) payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS");
        if (payload && payload->Data) {
            std::string pathStr((const char*)payload->Data);
            size_t sep = pathStr.find('|');
            if (sep != std::string::npos) pathStr = pathStr.substr(0, sep);
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
            std::string ext = std::filesystem::path(pathStr).extension().string();
            for (char& c : ext) c = (char)::tolower((unsigned char)c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".dds" || ext == ".hdr" || ext == ".webp") {
                material->normalMapPath = pathStr;
                updatePipelineAndDescriptors();
                statusMessage = "Assigned normal map: " + pathStr;
            } else {
                statusMessage = "Error: Dropped asset is not a valid texture file.";
            }
        }
        EndDragDropTarget();
    }

    // 5) Metallic Map
    char metallicBuf[256]{};
    snprintf(metallicBuf, sizeof(metallicBuf), "%s", material->metallicMapPath.c_str());
    if (InputText("Metallic Map", metallicBuf, sizeof(metallicBuf))) {
        material->metallicMapPath = metallicBuf;
        updatePipelineAndDescriptors();
    }
    if (BeginDragDropTarget()) {
        const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH");
        if (!payload) payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS");
        if (payload && payload->Data) {
            std::string pathStr((const char*)payload->Data);
            size_t sep = pathStr.find('|');
            if (sep != std::string::npos) pathStr = pathStr.substr(0, sep);
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
            std::string ext = std::filesystem::path(pathStr).extension().string();
            for (char& c : ext) c = (char)::tolower((unsigned char)c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".dds" || ext == ".hdr" || ext == ".webp") {
                material->metallicMapPath = pathStr;
                updatePipelineAndDescriptors();
                statusMessage = "Assigned metallic map: " + pathStr;
            } else {
                statusMessage = "Error: Dropped asset is not a valid texture file.";
            }
        }
        EndDragDropTarget();
    }

    // 6) Lit Shader parameters
    if (material->shaderName == "Lit") {
        Spacing();
        Separator();
        Spacing();
        TextDisabled("Lit Shading Parameters");
        SliderFloat("Roughness", &material->roughness, 0.0f, 1.0f);
        SliderFloat("Metallic", &material->metallic, 0.0f, 1.0f);
    }
}

void EditorUI::drawMeshEditor() {
    Mesh* mesh = registry.get<Mesh>(selectedEntity);
    if (!mesh || registry.has<Grid>(selectedEntity)) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<Mesh>(registry, selectedEntity, "Mesh", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<Mesh>(selectedEntity);
        statusMessage = "Removed Mesh component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }

    char gltfBuf[256]{};
    snprintf(gltfBuf, sizeof(gltfBuf), "%s", mesh->gltfPath.c_str());
    if (InputText("glTF Path", gltfBuf, sizeof(gltfBuf))) {
        mesh->gltfPath = gltfBuf;
    }

    if (BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
            const char* droppedPath = (const char*)payload->Data;
            std::string pathStr(droppedPath);
            auto ext = std::filesystem::path(pathStr).extension().string();
            if (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".FBX") {
                try {
                    int primCount = renderer.resourceManager->getMeshPrimitiveCount(pathStr);
                    if (primCount > 1) {
                        std::string baseName = "Model";
                        if (auto* nameComp = registry.get<Name>(selectedEntity)) {
                            baseName = nameComp->value;
                        }
                        
                        mesh->gltfPath = pathStr;
                        mesh->primitiveIndex = 0;
                        Mesh loaded = renderer.resourceManager->loadMesh(pathStr, renderer, 0);
                        mesh->vertices = loaded.vertices;
                        mesh->indices = loaded.indices;
                        mesh->vertexBuffer = loaded.vertexBuffer;
                        mesh->indexBuffer = loaded.indexBuffer;
                        mesh->id = loaded.id;
                        
                        if (auto* nameComp = registry.get<Name>(selectedEntity)) {
                            nameComp->value = loaded.nodeName.empty() ? (baseName + "_part0") : loaded.nodeName;
                            renameBuffer = nameComp->value;
                        }

                        registry.remove<SkeletonComponent>(selectedEntity);
                        registry.remove<AnimatorComponent>(selectedEntity);
                        SkeletonComponent skeleton{};
                        AnimatorComponent animator{};
                        if (renderer.resourceManager->loadSkeletonAndAnimations(pathStr, skeleton, animator)) {
                            registry.emplace<SkeletonComponent>(selectedEntity, std::move(skeleton));
                            registry.emplace<AnimatorComponent>(selectedEntity, std::move(animator));
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

                        Scene* currentScene = sceneManager.getCurrentScene();
                        for (int i = 1; i < primCount; ++i) {
                            Entity child = registry.create();
                            if (child.getId() != Entity::INVALID_ENTITY) {
                                registry.emplace<Transform>(child, Transform{});
                                registry.emplace<PrimitiveType>(child, PrimitiveType{ PrimitiveKind::Cube });
                                registry.emplace<HierarchyComponent>(child, HierarchyComponent{ selectedEntity });
                                
                                Mesh childMesh{};
                                childMesh.gltfPath = pathStr;
                                childMesh.primitiveIndex = i;
                                Mesh loadedChild = renderer.resourceManager->loadMesh(pathStr, renderer, i);
                                childMesh.vertices = loadedChild.vertices;
                                childMesh.indices = loadedChild.indices;
                                childMesh.vertexBuffer = loadedChild.vertexBuffer;
                                childMesh.indexBuffer = loadedChild.indexBuffer;
                                childMesh.id = loadedChild.id;
                                childMesh.nodeName = loadedChild.nodeName;

                                std::string childName = loadedChild.nodeName.empty() ? (baseName + "_part" + std::to_string(i)) : loadedChild.nodeName;
                                registry.emplace<Name>(child, Name{ childName });
                                registry.emplace<Mesh>(child, std::move(childMesh));
                                
                                glm::vec4 color(1.0f);
                                Material material{ color };
                                bool hasSkin = entityHasSkin(registry, child);
                                PipelineHandle pipeline = renderer.createPipelineForShaders(
                                    hasSkin ? renderer.resolveShaderPath("build/shaders/skinned.vert.spv") : renderer.resolveShaderPath("build/shaders/unlit.vert.spv"),
                                    renderer.resolveShaderPath("build/shaders/unlit.frag.spv")
                                );
                                material.pipeline = pipeline.pipeline;
                                material.pipelineLayout = pipeline.layout;
                                registry.emplace<Material>(child, std::move(material));
                                
                                if (currentScene) {
                                    currentScene->trackEntity(child);
                                }
                            }
                        }
                        statusMessage = "Dropped & loaded split glTF: " + pathStr;
                    } else {
                        mesh->gltfPath = pathStr;
                        mesh->primitiveIndex = -1;
                        Mesh loaded = renderer.resourceManager->loadMesh(pathStr, renderer);
                        mesh->vertices = loaded.vertices;
                        mesh->indices = loaded.indices;
                        mesh->vertexBuffer = loaded.vertexBuffer;
                        mesh->indexBuffer = loaded.indexBuffer;
                        mesh->id = loaded.id;

                        registry.remove<SkeletonComponent>(selectedEntity);
                        registry.remove<AnimatorComponent>(selectedEntity);
                        SkeletonComponent skeleton{};
                        AnimatorComponent animator{};
                        if (renderer.resourceManager->loadSkeletonAndAnimations(pathStr, skeleton, animator)) {
                            registry.emplace<SkeletonComponent>(selectedEntity, std::move(skeleton));
                            registry.emplace<AnimatorComponent>(selectedEntity, std::move(animator));
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
                        statusMessage = "Dropped & loaded glTF: " + pathStr;
                    }
                } catch (const std::exception& e) {
                    statusMessage = std::string("Failed to load dropped model: ") + e.what();
                }
            } else {
                statusMessage = "Error: Dropped asset is not a glTF model.";
            }
        }
        EndDragDropTarget();
    }
    SameLine();
    if (Button("Load glTF")) {
        if (!mesh->gltfPath.empty()) {
            try {
                Mesh loaded = renderer.resourceManager->loadMesh(mesh->gltfPath, renderer);
                mesh->vertices = loaded.vertices;
                mesh->indices = loaded.indices;
                mesh->vertexBuffer = loaded.vertexBuffer;
                mesh->indexBuffer = loaded.indexBuffer;
                mesh->id = loaded.id;

                registry.remove<SkeletonComponent>(selectedEntity);
                registry.remove<AnimatorComponent>(selectedEntity);
                SkeletonComponent skeleton{};
                AnimatorComponent animator{};
                if (renderer.resourceManager->loadSkeletonAndAnimations(mesh->gltfPath, skeleton, animator)) {
                    registry.emplace<SkeletonComponent>(selectedEntity, std::move(skeleton));
                    registry.emplace<AnimatorComponent>(selectedEntity, std::move(animator));
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

    Text("Vertices: %d, Indices: %d", (int)mesh->vertices.size(), (int)mesh->indices.size());
}

void EditorUI::drawGridEditor() {
    Grid* grid = registry.get<Grid>(selectedEntity);
    if (!grid) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<Grid>(registry, selectedEntity, "Grid", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<Grid>(selectedEntity);
        statusMessage = "Removed Grid component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }

    DragFloat("Spacing", &grid->spacing, 0.05f, 0.1f, 100.0f);
    DragFloat("Size", &grid->size, 1.0f, 1.0f, 1000.0f);
}

void EditorUI::drawCameraEditor() {
    Camera* camera = registry.get<Camera>(selectedEntity);
    Transform* transform = registry.get<Transform>(selectedEntity);
    if (!camera) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<Camera>(registry, selectedEntity, "Camera", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<Camera>(selectedEntity);
        statusMessage = "Removed Camera component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }

    bool changed = false;
    const char* projectionTypes[] = { "Perspective", "Orthographic" };
    int currentProj = camera->isOrthographic ? 1 : 0;
    if (ImGui::Combo("Projection", &currentProj, projectionTypes, IM_ARRAYSIZE(projectionTypes))) {
        camera->isOrthographic = (currentProj == 1);
        changed = true;
    }

    if (camera->isOrthographic) {
        changed |= DragFloat("Orthographic Size", &camera->orthoSize, 0.1f, 0.1f, 200.0f);
    } else {
        changed |= DragFloat("FOV", &camera->fov, 0.1f, 1.0f, 120.0f);
    }

    changed |= DragFloat("Near Plane", &camera->nearPlane, 0.01f, 0.01f, 10.0f);
    changed |= DragFloat("Far Plane", &camera->farPlane, 1.0f, 1.0f, 5000.0f);
    changed |= DragFloat("Move Speed", &camera->moveSpeed, 0.1f, 0.1f, 100.0f);
    changed |= DragFloat("Mouse Sensitivity", &camera->mouseSensitivity, 0.01f, 0.01f, 5.0f);
}

void EditorUI::drawLightEditor() {
    auto* light = registry.get<Engine::LightComponent>(selectedEntity);
    if (!light) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<Engine::LightComponent>(registry, selectedEntity, "Light", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<Engine::LightComponent>(selectedEntity);
        statusMessage = "Removed Light component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }

    const char* lightTypes[] = { "Directional", "Point", "Spot" };
    int currentType = static_cast<int>(light->type);
    if (ImGui::Combo("Type##light_type", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes))) {
        light->type = static_cast<Engine::LightType>(currentType);
    }

    ImGui::ColorEdit3("Color##light_color", &light->color.x, ImGuiColorEditFlags_Float);
    DragFloat("Intensity##light_intensity", &light->intensity, 0.05f, 0.0f, 100.0f);
    DragFloat("Range##light_range", &light->range, 0.1f, 0.1f, 1000.0f);

    if (light->type == Engine::LightType::Directional || light->type == Engine::LightType::Spot) {
        ImGui::Spacing();
        ImGui::Separator();
        // 1. Sun Orbit / Time of Day (0° = Sunrise, 90° = Zenith / Noon, 180° = Sunset, 270° = Midnight / Nadir, 360° = Sunrise)
        bool angleChanged = false;
        angleChanged |= DragFloat("Sun Orbit (0-360 deg)##light_pitch", &light->pitch, 0.5f, 0.0f, 360.0f, "%.1f deg");
        
        // 2. Azimuth (Compass Heading): 0 to 360 degrees around the horizon
        angleChanged |= DragFloat("Azimuth / Heading##light_yaw", &light->yaw, 0.5f, 0.0f, 360.0f, "%.1f deg");

        // Keep angles normalized in [0, 360)
        while (light->pitch < 0.0f) light->pitch += 360.0f;
        while (light->pitch >= 360.0f) light->pitch -= 360.0f;
        while (light->yaw < 0.0f) light->yaw += 360.0f;
        while (light->yaw >= 360.0f) light->yaw -= 360.0f;

        if (angleChanged) {
            light->updateDirectionFromAngles();
        }

        // 3. Direct Vector Editing
        if (ImGui::DragFloat3("Direction Vector##light_dir", &light->direction.x, 0.01f, -1.0f, 1.0f, "%.3f")) {
            if (glm::length(light->direction) > 0.0001f) {
                light->direction = glm::normalize(light->direction);
                light->updateAnglesFromDirection();
            }
        }
    }

    if (light->type == Engine::LightType::Directional) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.75f, 0.2f, 1.0f));
        ImGui::Text("Directional Shadows");
        ImGui::PopStyleColor();

        Checkbox("Cast Shadows##light_cast_shadows", &light->castShadows);
        if (light->castShadows) {
            DragFloat("Shadow Distance (m)##light_shadow_dist", &light->shadowDistance, 1.0f, 10.0f, 500.0f, "%.1f m");
            DragFloat("Shadow Bias##light_shadow_bias", &light->shadowBias, 0.0001f, 0.0001f, 0.02f, "%.5f");
            DragFloat("Normal Bias (texels)##light_shadow_nbias", &light->shadowNormalBias, 0.05f, 0.0f, 10.0f, "%.2f texels");
        }
    }
}

void EditorUI::drawSkeletonEditor() {
    SkeletonComponent* skeleton = registry.get<SkeletonComponent>(selectedEntity);
    if (!skeleton) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<SkeletonComponent>(registry, selectedEntity, "Skeleton", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<SkeletonComponent>(selectedEntity);
        if (auto* material = registry.get<Material>(selectedEntity)) {
            std::string vertShader = (material->shaderName == "Lit") ? "lit.vert.spv" : "unlit.vert.spv";
            std::string fragShader = (material->shaderName == "Lit") ? "lit.frag.spv" : "unlit.frag.spv";
            PipelineHandle pipeline = renderer.createPipelineForShaders(
                renderer.resolveShaderPath("build/shaders/" + vertShader),
                renderer.resolveShaderPath("build/shaders/" + fragShader)
            );
            material->pipeline = pipeline.pipeline;
            material->pipelineLayout = pipeline.layout;
        }
        statusMessage = "Removed Skeleton component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }
    
    Text("Bones count: %d", (int)skeleton->joints.size());
    if (TreeNode("Bones List")) {
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            const auto& joint = skeleton->joints[i];
            TreeNodeEx((void*)(intptr_t)i, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s (Parent: %d)", joint.name.c_str(), joint.parentIndex);
        }
        TreePop();
    }
}

void EditorUI::drawAnimatorEditor() {
    AnimatorComponent* animator = registry.get<AnimatorComponent>(selectedEntity);
    if (!animator) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<AnimatorComponent>(registry, selectedEntity, "Animator", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<AnimatorComponent>(selectedEntity);
        statusMessage = "Removed Animator component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }

    if (auto* hierarchy = registry.get<HierarchyComponent>(selectedEntity)) {
        if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent) && registry.has<AnimatorComponent>(hierarchy->parent)) {
            TextUnformatted("Animation is driven by parent entity.");
            return;
        }
    }

    // Binary anim loader/saver utility controls
    static Entity lastSelectedEntity{};
    static char animPathBuf[256] = "";
    if (selectedEntity != lastSelectedEntity) {
        lastSelectedEntity = selectedEntity;
        strncpy_s(animPathBuf, animator->loadedAnimPath.c_str(), sizeof(animPathBuf) - 1);
    }

    InputText("Anim Path", animPathBuf, sizeof(animPathBuf));
    if (BeginDragDropTarget()) {
        std::string pathStr = acceptDroppedAssetPath();
        if (!pathStr.empty()) {
            auto ext = std::filesystem::path(pathStr).extension().string();
            std::string lowerExt = ext;
            std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
            if (lowerExt == ".fbx" || lowerExt == ".anim" || lowerExt == ".gltf" || lowerExt == ".glb") {
                strncpy_s(animPathBuf, pathStr.c_str(), sizeof(animPathBuf) - 1);
                
                bool isAnimFile = (lowerExt == ".anim");
                SkeletonComponent* skeleton = registry.get<SkeletonComponent>(selectedEntity);
                if (!skeleton && !isAnimFile) {
                    SkeletonComponent newSkel{};
                    registry.emplace<SkeletonComponent>(selectedEntity, std::move(newSkel));
                    skeleton = registry.get<SkeletonComponent>(selectedEntity);
                }
                
                bool loaded = false;
                if (isAnimFile) {
                    SkeletonComponent dummySkel;
                    loaded = renderer.resourceManager->loadBinarySkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator);
                } else {
                    SkeletonComponent dummySkel;
                    loaded = renderer.resourceManager->loadSkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator);
                }
                
                if (loaded) {
                    animator->loadedAnimPath = pathStr;
                    if (auto* material = registry.get<Material>(selectedEntity)) {
                        bool hasSkin = entityHasSkin(registry, selectedEntity);
                        PipelineHandle pipeline = renderer.createPipelineForShaders(
                            hasSkin ? renderer.resolveShaderPath("build/shaders/skinned.vert.spv") : renderer.resolveShaderPath("build/shaders/unlit.vert.spv"),
                            renderer.resolveShaderPath("build/shaders/unlit.frag.spv")
                        );
                        material->pipeline = pipeline.pipeline;
                        material->pipelineLayout = pipeline.layout;
                    }
                    statusMessage = "Loaded animation successfully via drag & drop.";
                } else {
                    statusMessage = "Failed to load animation via drag & drop.";
                }
            }
        }
        EndDragDropTarget();
    }
    
    if (Button("Load Anim")) {
        std::string pathStr(animPathBuf);
        auto loadExt = std::filesystem::path(pathStr).extension().string();
        bool isAnimFile = (loadExt == ".anim");
        SkeletonComponent* skeleton = registry.get<SkeletonComponent>(selectedEntity);
        if (!skeleton && !isAnimFile) {
            SkeletonComponent newSkel{};
            registry.emplace<SkeletonComponent>(selectedEntity, std::move(newSkel));
            skeleton = registry.get<SkeletonComponent>(selectedEntity);
        }
        
        bool loaded = false;
        if (isAnimFile) {
            SkeletonComponent dummySkel;
            loaded = renderer.resourceManager->loadBinarySkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator);
        } else {
            SkeletonComponent dummySkel;
            loaded = renderer.resourceManager->loadSkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator);
        }
        
        if (loaded) {
            animator->loadedAnimPath = pathStr;
            if (auto* material = registry.get<Material>(selectedEntity)) {
                bool hasSkin = entityHasSkin(registry, selectedEntity);
                PipelineHandle pipeline = renderer.createPipelineForShaders(
                    hasSkin ? renderer.resolveShaderPath("build/shaders/skinned.vert.spv") : renderer.resolveShaderPath("build/shaders/unlit.vert.spv"),
                    renderer.resolveShaderPath("build/shaders/unlit.frag.spv")
                );
                material->pipeline = pipeline.pipeline;
                material->pipelineLayout = pipeline.layout;
            }
            statusMessage = "Loaded animation successfully.";
        } else {
            statusMessage = "Failed to load animation.";
        }
    }
    
    SameLine();
    if (Button("Save Binary (.anim)")) {
        SkeletonComponent* skeleton = registry.get<SkeletonComponent>(selectedEntity);
        if (skeleton) {
            std::filesystem::create_directories("assets/animations");
            std::string savePath = "assets/animations/model.anim";
            if (auto* nameComp = registry.get<Name>(selectedEntity)) {
                savePath = "assets/animations/" + nameComp->value + ".anim";
            }
            if (renderer.resourceManager->saveBinarySkeletonAndAnimations(savePath, *skeleton, *animator)) {
                statusMessage = "Saved binary animation to " + savePath;
            } else {
                statusMessage = "Failed to save binary animation.";
            }
        } else {
            statusMessage = "No skeleton to save.";
        }
    }
    
    Separator();
    
    if (animator->animations.empty()) {
        TextUnformatted("No animation clips loaded.");
        return;
    }
    
    std::vector<const char*> clipNames;
    for (const auto& anim : animator->animations) {
        clipNames.push_back(anim.name.c_str());
    }
    
    int currentClipIdx = animator->activeAnimationIndex;
    if (Combo("Active Animation", &currentClipIdx, clipNames.data(), static_cast<int>(clipNames.size()))) {
        animator->activeAnimationIndex = currentClipIdx;
        animator->currentTime = 0.0f;
    }
    
    SliderFloat("Playback Speed", &animator->playbackSpeed, 0.0f, 5.0f, "%.2fx");
    Checkbox("Looping", &animator->loop);
    Checkbox("Preview Animation in Editor", &animator->isPreviewing);
    
    if (currentClipIdx >= 0 && currentClipIdx < static_cast<int>(animator->animations.size())) {
        const auto& activeClip = animator->animations[currentClipIdx];
        float progress = activeClip.duration > 0.0f ? (animator->currentTime / activeClip.duration) : 0.0f;
        ProgressBar(progress, ImVec2(-1, 0), (std::to_string(animator->currentTime) + "s / " + std::to_string(activeClip.duration) + "s").c_str());
        
        if (Button("Play")) {
            animator->playbackSpeed = 1.0f;
        }
        SameLine();
        if (Button("Pause")) {
            animator->playbackSpeed = 0.0f;
        }
        SameLine();
        if (Button("Reset")) {
            animator->currentTime = 0.0f;
        }
    }
}

void EditorUI::drawHierarchyEditor() {
    HierarchyComponent* hc = registry.get<HierarchyComponent>(selectedEntity);
    if (!hc) {
        return;
    }

    bool visible = true;
    bool open = CollapsingHeader("Hierarchy Link", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<HierarchyComponent>(selectedEntity);
        statusMessage = "Removed Hierarchy component.";
        markSceneDirty();
        return;
    }

    if (open) {
        std::string parentLabel = "None";
        if (hc->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hc->parent)) {
            if (auto* parentName = registry.get<Name>(hc->parent)) {
                parentLabel = parentName->value;
            } else {
                parentLabel = "Entity " + std::to_string(hc->parent.getId());
            }
        }

        if (BeginCombo("Parent Entity", parentLabel.c_str())) {
            if (Selectable("None", hc->parent.getId() == Entity::INVALID_ENTITY)) {
                hc->parent = Entity();
                statusMessage = "Cleared entity parent.";
            }

            for (auto [entity, name] : registry.view<Name>()) {
                if (entity == selectedEntity) continue;
                if (registry.has<EditorCamera>(entity)) continue;

                // Cycle check
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

                if (isDescendant) continue;

                bool selected = (entity == hc->parent);
                if (Selectable(name.value.c_str(), selected)) {
                    hc->parent = entity;
                    statusMessage = "Set parent entity to " + name.value;
                }
            }
            EndCombo();
        }
    }
}

void EditorUI::drawIKSolverEditor() {
    IKSolverComponent* ik = registry.get<IKSolverComponent>(selectedEntity);
    if (!ik) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<IKSolverComponent>(registry, selectedEntity, "IK Solver", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<IKSolverComponent>(selectedEntity);
        statusMessage = "Removed IK Solver component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }

    Checkbox("Enable IK Solver", &ik->enabled);
    
    const char* solverTypes[] = { "2-Bone (Analytical)", "FABRIK (Iterative)" };
    int currentType = static_cast<int>(ik->solverType);
    if (Combo("Solver Type", &currentType, solverTypes, 2)) {
        ik->solverType = static_cast<IKSolverType>(currentType);
    }

    if (ik->solverType == IKSolverType::TwoBone) {
        char startJointBuf[64]{};
        char midJointBuf[64]{};
        char endJointBuf[64]{};
        strncpy_s(startJointBuf, ik->startJointName.c_str(), sizeof(startJointBuf) - 1);
        strncpy_s(midJointBuf, ik->middleJointName.c_str(), sizeof(midJointBuf) - 1);
        strncpy_s(endJointBuf, ik->endJointName.c_str(), sizeof(endJointBuf) - 1);
        
        if (InputText("Start Joint (e.g. thigh)", startJointBuf, sizeof(startJointBuf))) {
            ik->startJointName = startJointBuf;
        }
        if (InputText("Middle Joint (e.g. shin)", midJointBuf, sizeof(midJointBuf))) {
            ik->middleJointName = midJointBuf;
        }
        if (InputText("End Joint (e.g. foot)", endJointBuf, sizeof(endJointBuf))) {
            ik->endJointName = endJointBuf;
        }
        
        if (Button("Auto Setup Left Leg Joints")) {
            if (auto* skeleton = registry.get<SkeletonComponent>(selectedEntity)) {
                for (const auto& joint : skeleton->joints) {
                    if (joint.name.find("thigh.L") != std::string::npos || joint.name.find("Thigh.L") != std::string::npos || joint.name.find("UpperLeg_L") != std::string::npos) {
                        ik->startJointName = joint.name;
                    }
                    if (joint.name.find("shin.L") != std::string::npos || joint.name.find("Shin.L") != std::string::npos || joint.name.find("LowerLeg_L") != std::string::npos) {
                        ik->middleJointName = joint.name;
                    }
                    if (joint.name.find("foot.L") != std::string::npos || joint.name.find("Foot.L") != std::string::npos || joint.name.find("Foot_L") != std::string::npos) {
                        ik->endJointName = joint.name;
                    }
                }
                ik->polePosition = glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }
    } else {
        SliderInt("Max Iterations", &ik->maxIterations, 1, 50);
        SliderFloat("Tolerance", &ik->tolerance, 0.0001f, 0.01f, "%.4f");
        
        TextUnformatted("Bone Chain Joints (Base to Tip):");
        for (size_t i = 0; i < ik->jointChainNames.size(); ++i) {
            char jointBuf[64]{};
            strncpy_s(jointBuf, ik->jointChainNames[i].c_str(), sizeof(jointBuf) - 1);
            PushID(static_cast<int>(i));
            if (InputText("##joint", jointBuf, sizeof(jointBuf))) {
                ik->jointChainNames[i] = jointBuf;
            }
            SameLine();
            if (Button("Remove")) {
                ik->jointChainNames.erase(ik->jointChainNames.begin() + i);
                PopID();
                break;
            }
            PopID();
        }
        if (Button("Add Bone to Chain")) {
            ik->jointChainNames.push_back("");
        }
        SameLine();
        if (Button("Auto Setup Left Arm Chain")) {
            if (auto* skeleton = registry.get<SkeletonComponent>(selectedEntity)) {
                ik->jointChainNames.clear();
                for (const auto& joint : skeleton->joints) {
                    if (joint.name.find("shoulder.L") != std::string::npos || joint.name.find("Shoulder.L") != std::string::npos || joint.name.find("Clavicle_L") != std::string::npos) {
                        ik->jointChainNames.push_back(joint.name);
                    }
                }
                for (const auto& joint : skeleton->joints) {
                    if (joint.name.find("upper_arm.L") != std::string::npos || joint.name.find("UpperArm.L") != std::string::npos || joint.name.find("UpperArm_L") != std::string::npos) {
                        ik->jointChainNames.push_back(joint.name);
                    }
                }
                for (const auto& joint : skeleton->joints) {
                    if (joint.name.find("forearm.L") != std::string::npos || joint.name.find("Forearm.L") != std::string::npos || joint.name.find("Forearm_L") != std::string::npos) {
                        ik->jointChainNames.push_back(joint.name);
                    }
                }
                for (const auto& joint : skeleton->joints) {
                    if (joint.name.find("hand.L") != std::string::npos || joint.name.find("Hand.L") != std::string::npos || joint.name.find("Hand_L") != std::string::npos) {
                        ik->jointChainNames.push_back(joint.name);
                    }
                }
            }
        }
    }

    DragFloat3("IK Target Position", &ik->targetPosition.x, 0.05f);
    DragFloat3("IK Pole Position", &ik->polePosition.x, 0.05f);
    SliderFloat("IK Target Weight", &ik->targetWeight, 0.0f, 1.0f);
}

void EditorUI::drawAnimationControllerEditor() {
    AnimationControllerComponent* controller = registry.get<AnimationControllerComponent>(selectedEntity);
    if (!controller) {
        return;
    }

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<AnimationControllerComponent>(registry, selectedEntity, "Animation Controller", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<AnimationControllerComponent>(selectedEntity);
        statusMessage = "Removed Animation Controller component.";
        markSceneDirty();
        return;
    }

    if (!open) {
        return;
    }

    Text("Current State: %s", controller->currentState.empty() ? "None" : controller->currentState.c_str());
    if (controller->isCrossfading) {
        Text("Crossfading to %s (%.2f / %.2fs)", controller->targetState.c_str(), controller->crossfadeProgress, controller->crossfadeDuration);
        ProgressBar(controller->crossfadeProgress / controller->crossfadeDuration);
    }

    AnimatorComponent* animator = registry.get<AnimatorComponent>(selectedEntity);
    if (!animator) {
        registry.emplace<AnimatorComponent>(selectedEntity, AnimatorComponent{});
        animator = registry.get<AnimatorComponent>(selectedEntity);
    }

    Checkbox("Preview Mode", &animator->isPreviewing);
    ImGui::Spacing();

    auto ensureClipLoadedInInspector = [&](const std::string& cName) {
        if (cName.empty() || !animator) return;
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
    };

    for (const auto& st : controller->states) {
        if (st.isBlendTree) {
            for (const auto& bn : st.blendTree.nodes) ensureClipLoadedInInspector(bn.clipName);
        } else {
            ensureClipLoadedInInspector(st.clipName);
        }
    }

    std::vector<std::string> clipNames;
    std::set<std::string> seenClips;
    if (animator) {
        for (const auto& anim : animator->animations) {
            if (!anim.name.empty() && seenClips.find(anim.name) == seenClips.end()) {
                seenClips.insert(anim.name);
                clipNames.push_back(anim.name);
            }
            std::string stem = std::filesystem::path(anim.name).stem().string();
            if (!stem.empty() && seenClips.find(stem) == seenClips.end()) {
                seenClips.insert(stem);
                clipNames.push_back(stem);
            }
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

    // 1. Parameters management
    if (TreeNode("Parameters")) {
        static char newParamName[64] = "";
        InputText("New Parameter Name", newParamName, sizeof(newParamName));
        SameLine();
        if (Button("Add Parameter")) {
            if (strlen(newParamName) > 0) {
                controller->parameters[newParamName] = 0.0f;
                newParamName[0] = '\0';
            }
        }

        std::vector<std::string> paramsToDelete;
        for (auto& [paramName, paramVal] : controller->parameters) {
            SliderFloat(paramName.c_str(), &paramVal, -2.0f, 2.0f);
            SameLine();
            std::string btnLabel = "Delete##" + paramName;
            if (Button(btnLabel.c_str())) {
                paramsToDelete.push_back(paramName);
            }
        }

        for (const auto& pName : paramsToDelete) {
            controller->parameters.erase(pName);
        }
        TreePop();
    }

    // 2. States management
    if (TreeNode("States")) {
        static char newStateName[64] = "";
        InputText("New State Name", newStateName, sizeof(newStateName));
        SameLine();
        if (Button("Add State")) {
            if (strlen(newStateName) > 0) {
                AnimationState state;
                state.name = newStateName;
                state.clipName = !clipNames.empty() ? clipNames[0] : "idle";
                state.isBlendTree = false;
                controller->states.push_back(state);
                newStateName[0] = '\0';
            }
        }

        for (size_t sIdx = 0; sIdx < controller->states.size(); ++sIdx) {
            auto& state = controller->states[sIdx];
            std::string stateHeader = state.name + " (" + (state.isBlendTree ? "Blend Tree" : "Single Clip") + ")##state_" + std::to_string(sIdx);
            if (TreeNode(stateHeader.c_str())) {
                char stateName[64];
                strcpy_s(stateName, state.name.c_str());
                if (InputText("State Name", stateName, sizeof(stateName))) {
                    state.name = stateName;
                }

                Checkbox("Is Blend Tree", &state.isBlendTree);

                if (!state.isBlendTree) {
                    if (ImGui::BeginCombo("Animation Clip", state.clipName.empty() ? "(none)" : state.clipName.c_str())) {
                        if (ImGui::Selectable("(none)", state.clipName.empty())) state.clipName.clear();
                        for (const auto& cn : clipNames) {
                            bool sel = (cn == state.clipName);
                            if (ImGui::Selectable(cn.c_str(), sel)) {
                                state.clipName = cn;
                                ensureClipLoadedInInspector(cn);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (BeginDragDropTarget()) {
                        std::string pathStr = acceptDroppedAssetPath();
                        if (!pathStr.empty()) {
                            auto ext = std::filesystem::path(pathStr).extension().string();
                            std::string lowerExt = ext;
                            std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                            if (lowerExt == ".anim" || lowerExt == ".fbx" || lowerExt == ".gltf" || lowerExt == ".glb") {
                                bool isAnimFile = (lowerExt == ".anim");
                                SkeletonComponent* skeleton = registry.get<SkeletonComponent>(selectedEntity);
                                if (!skeleton && !isAnimFile) {
                                    SkeletonComponent newSkel{};
                                    registry.emplace<SkeletonComponent>(selectedEntity, std::move(newSkel));
                                    skeleton = registry.get<SkeletonComponent>(selectedEntity);
                                }
                                if (!animator) {
                                    AnimatorComponent newAnim{};
                                    registry.emplace<AnimatorComponent>(selectedEntity, std::move(newAnim));
                                    animator = registry.get<AnimatorComponent>(selectedEntity);
                                }
                                
                                bool loaded = false;
                                if (isAnimFile) {
                                    SkeletonComponent dummySkel;
                                    loaded = renderer.resourceManager->loadBinarySkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator, true);
                                } else {
                                    SkeletonComponent dummySkel;
                                    loaded = renderer.resourceManager->loadSkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator, true);
                                }
                                
                                if (loaded) {
                                    if (!animator->animations.empty()) {
                                        state.clipName = animator->animations.back().name;
                                    }
                                    statusMessage = "Appended animation clip successfully.";
                                }
                            }
                        }
                        EndDragDropTarget();
                    }

                    Checkbox("Looping", &state.isLooping);
                    SliderFloat("Playback Speed", &state.speed, 0.1f, 5.0f);
                } else {
                    Checkbox("Is 2D", &state.blendTree.is2D);

                    std::vector<const char*> paramNames;
                    int paramXIdx = 0;
                    int paramYIdx = 0;
                    for (const auto& [pName, pVal] : controller->parameters) {
                        paramNames.push_back(pName.c_str());
                    }

                    if (!paramNames.empty()) {
                        for (size_t i = 0; i < paramNames.size(); ++i) {
                            if (paramNames[i] == state.blendTree.parameterName) paramXIdx = static_cast<int>(i);
                            if (paramNames[i] == state.blendTree.parameterYName) paramYIdx = static_cast<int>(i);
                        }
                        if (Combo("Parameter X", &paramXIdx, paramNames.data(), static_cast<int>(paramNames.size()))) {
                            state.blendTree.parameterName = paramNames[paramXIdx];
                        }
                        if (state.blendTree.is2D) {
                            if (Combo("Parameter Y", &paramYIdx, paramNames.data(), static_cast<int>(paramNames.size()))) {
                                state.blendTree.parameterYName = paramNames[paramYIdx];
                            }
                        }
                    } else {
                        TextDisabled("No parameters defined. Add parameters first.");
                    }

                    if (TreeNode("Blend Nodes")) {
                        if (Button("Add Blend Node")) {
                            BlendNode node;
                            node.clipName = !clipNames.empty() ? clipNames[0] : "";
                            node.threshold = 0.0f;
                            node.threshold2D = glm::vec2(0.0f);
                            state.blendTree.nodes.push_back(node);
                        }

                        for (size_t nIdx = 0; nIdx < state.blendTree.nodes.size(); ++nIdx) {
                            auto& node = state.blendTree.nodes[nIdx];
                            std::string cLabel = node.clipName.empty() ? "(none)" : node.clipName;
                            std::string nodeHeader = "Node " + std::to_string(nIdx) + ": " + cLabel + "##node_" + std::to_string(nIdx);
                            if (TreeNode(nodeHeader.c_str())) {
                                if (ImGui::BeginCombo("Clip Name", node.clipName.empty() ? "(none)" : node.clipName.c_str())) {
                                    if (ImGui::Selectable("(none)", node.clipName.empty())) node.clipName.clear();
                                    for (const auto& cn : clipNames) {
                                        bool sel = (cn == node.clipName);
                                        if (ImGui::Selectable(cn.c_str(), sel)) {
                                            node.clipName = cn;
                                            ensureClipLoadedInInspector(cn);
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                                if (BeginDragDropTarget()) {
                                    std::string pathStr = acceptDroppedAssetPath();
                                    if (!pathStr.empty()) {
                                        auto ext = std::filesystem::path(pathStr).extension().string();
                                        std::string lowerExt = ext;
                                        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                                        if (lowerExt == ".anim" || lowerExt == ".fbx" || lowerExt == ".gltf" || lowerExt == ".glb") {
                                            bool isAnimFile = (lowerExt == ".anim");
                                            SkeletonComponent* skeleton = registry.get<SkeletonComponent>(selectedEntity);
                                            if (!skeleton && !isAnimFile) {
                                                SkeletonComponent newSkel{};
                                                registry.emplace<SkeletonComponent>(selectedEntity, std::move(newSkel));
                                                skeleton = registry.get<SkeletonComponent>(selectedEntity);
                                            }
                                            if (!animator) {
                                                AnimatorComponent newAnim{};
                                                registry.emplace<AnimatorComponent>(selectedEntity, std::move(newAnim));
                                                animator = registry.get<AnimatorComponent>(selectedEntity);
                                            }
                                            
                                            bool loaded = false;
                                            if (isAnimFile) {
                                                SkeletonComponent dummySkel;
                                                loaded = renderer.resourceManager->loadBinarySkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator, true);
                                            } else {
                                                SkeletonComponent dummySkel;
                                                loaded = renderer.resourceManager->loadSkeletonAndAnimations(pathStr, skeleton ? *skeleton : dummySkel, *animator, true);
                                            }
                                            
                                            if (loaded) {
                                                if (!animator->animations.empty()) {
                                                    node.clipName = animator->animations.back().name;
                                                }
                                                statusMessage = "Appended animation clip successfully.";
                                            }
                                        }
                                    }
                                    EndDragDropTarget();
                                }

                                if (state.blendTree.is2D) {
                                    DragFloat2("Threshold 2D", &node.threshold2D.x, 0.05f);
                                } else {
                                    DragFloat("Threshold 1D", &node.threshold, 0.05f);
                                }

                                if (Button("Remove Node")) {
                                    state.blendTree.nodes.erase(state.blendTree.nodes.begin() + nIdx);
                                    --nIdx;
                                }
                                TreePop();
                            }
                        }
                        TreePop();
                    }
                }

                if (Button("Delete State")) {
                    controller->states.erase(controller->states.begin() + sIdx);
                    --sIdx;
                }
                TreePop();
            }
        }
        TreePop();
    }

    // 3. Transitions management
    if (TreeNode("Transitions")) {
        if (Button("Add Transition")) {
            AnimationTransition trans;
            if (!controller->states.empty()) {
                trans.fromState = controller->states[0].name;
                trans.toState = controller->states[0].name;
            }
            trans.crossfadeDuration = 0.25f;
            controller->transitions.push_back(trans);
        }

        for (size_t tIdx = 0; tIdx < controller->transitions.size(); ++tIdx) {
            auto& trans = controller->transitions[tIdx];
            std::string transHeader = "Transition: " + trans.fromState + " -> " + trans.toState + "##trans_" + std::to_string(tIdx);
            if (TreeNode(transHeader.c_str())) {
                std::vector<const char*> stateNames;
                int fromStateIdx = 0;
                int toStateIdx = 0;
                for (const auto& st : controller->states) {
                    stateNames.push_back(st.name.c_str());
                }

                if (!stateNames.empty()) {
                    for (size_t i = 0; i < stateNames.size(); ++i) {
                        if (stateNames[i] == trans.fromState) fromStateIdx = static_cast<int>(i);
                        if (stateNames[i] == trans.toState) toStateIdx = static_cast<int>(i);
                    }
                    if (Combo("From State", &fromStateIdx, stateNames.data(), static_cast<int>(stateNames.size()))) {
                        trans.fromState = stateNames[fromStateIdx];
                    }
                    if (Combo("To State", &toStateIdx, stateNames.data(), static_cast<int>(stateNames.size()))) {
                        trans.toState = stateNames[toStateIdx];
                    }
                }

                DragFloat("Crossfade Duration (s)", &trans.crossfadeDuration, 0.01f, 0.0f, 2.0f);

                if (TreeNode("Conditions")) {
                    if (Button("Add Condition")) {
                        TransitionCondition cond;
                        if (!controller->parameters.empty()) {
                            cond.parameterName = controller->parameters.begin()->first;
                        }
                        cond.op = ">";
                        cond.value = 0.0f;
                        trans.conditions.push_back(cond);
                    }

                    for (size_t cIdx = 0; cIdx < trans.conditions.size(); ++cIdx) {
                        auto& cond = trans.conditions[cIdx];
                        std::string condHeader = "Condition " + std::to_string(cIdx) + ": " + cond.parameterName + "##cond_" + std::to_string(cIdx);
                        if (TreeNode(condHeader.c_str())) {
                            std::vector<const char*> paramNames;
                            int paramIdx = 0;
                            for (const auto& [pName, pVal] : controller->parameters) {
                                paramNames.push_back(pName.c_str());
                            }

                            if (!paramNames.empty()) {
                                for (size_t i = 0; i < paramNames.size(); ++i) {
                                    if (paramNames[i] == cond.parameterName) {
                                        paramIdx = static_cast<int>(i);
                                        break;
                                    }
                                }
                                if (Combo("Parameter", &paramIdx, paramNames.data(), static_cast<int>(paramNames.size()))) {
                                    cond.parameterName = paramNames[paramIdx];
                                }
                            }

                            const char* ops[] = { ">", "<", "==" };
                            int opIdx = 0;
                            for (int i = 0; i < 3; ++i) {
                                if (cond.op == ops[i]) opIdx = i;
                            }
                            if (Combo("Operator", &opIdx, ops, 3)) {
                                cond.op = ops[opIdx];
                            }

                            DragFloat("Value", &cond.value, 0.05f);

                            if (Button("Remove Condition")) {
                                trans.conditions.erase(trans.conditions.begin() + cIdx);
                                --cIdx;
                            }
                            TreePop();
                        }
                    }
                    TreePop();
                }

                if (Button("Remove Transition")) {
                    controller->transitions.erase(controller->transitions.begin() + tIdx);
                    --tIdx;
                }
                TreePop();
            }
        }
        TreePop();
    }

    // 4. Quick State Setup Demo buttons
    if (animator) {
        Separator();
        if (Button("Setup Idle/Walk State Machine")) {
            controller->states.clear();
            
            AnimationState idleState;
            idleState.name = "Idle";
            idleState.clipName = "idle";
            idleState.isBlendTree = false;
            if (!animator->animations.empty()) idleState.clipName = animator->animations[0].name;
            
            AnimationState moveState;
            moveState.name = "Movement";
            moveState.isBlendTree = true;
            moveState.blendTree.parameterName = "speed";
            
            if (animator->animations.size() >= 2) {
                BlendNode nodeWalk{ animator->animations[0].name, 0.0f };
                BlendNode nodeRun{ animator->animations[1].name, 1.0f };
                moveState.blendTree.nodes = { nodeWalk, nodeRun };
            } else if (!animator->animations.empty()) {
                BlendNode nodeWalk{ animator->animations[0].name, 0.0f };
                moveState.blendTree.nodes = { nodeWalk };
            }
            
            controller->states = { idleState, moveState };

            controller->transitions.clear();
            
            AnimationTransition toMove;
            toMove.fromState = "Idle";
            toMove.toState = "Movement";
            toMove.crossfadeDuration = 0.3f;
            TransitionCondition condMove{ "speed", ">", 0.1f };
            toMove.conditions = { condMove };
            
            AnimationTransition toIdle;
            toIdle.fromState = "Movement";
            toIdle.toState = "Idle";
            toIdle.crossfadeDuration = 0.3f;
            TransitionCondition condIdle2{ "speed", "<", 0.1f };
            toIdle.conditions = { condIdle2 };
            
            controller->transitions = { toMove, toIdle };
            controller->parameters["speed"] = 0.0f;
            controller->currentState = "Idle";
            controller->currentStateTime = 0.0f;
            controller->isCrossfading = false;
        }

        SameLine();

        if (Button("Setup 2D Locomotion Blend Tree")) {
            controller->states.clear();

            AnimationState idleState;
            idleState.name = "Idle";
            idleState.clipName = "idle";
            idleState.isBlendTree = false;

            for (const auto& clip : animator->animations) {
                std::string lower = clip.name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find("idle") != std::string::npos) {
                    idleState.clipName = clip.name;
                    break;
                }
            }
            if (idleState.clipName == "idle" && !animator->animations.empty()) {
                idleState.clipName = animator->animations[0].name;
            }

            AnimationState moveState;
            moveState.name = "Movement";
            moveState.isBlendTree = true;
            moveState.blendTree.parameterName = "velocityX";
            moveState.blendTree.parameterYName = "velocityY";
            moveState.blendTree.is2D = true;

            std::string defaultClip = !animator->animations.empty() ? animator->animations[0].name : "";

            BlendNode nodeIdle{ defaultClip, 0.0f, glm::vec2(0.0f, 0.0f) };
            BlendNode nodeForward{ defaultClip, 0.0f, glm::vec2(0.0f, 1.0f) };
            BlendNode nodeBackward{ defaultClip, 0.0f, glm::vec2(0.0f, -1.0f) };
            BlendNode nodeLeft{ defaultClip, 0.0f, glm::vec2(-1.0f, 0.0f) };
            BlendNode nodeRight{ defaultClip, 0.0f, glm::vec2(1.0f, 0.0f) };

            for (const auto& clip : animator->animations) {
                std::string lower = clip.name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

                if (lower.find("idle") != std::string::npos) {
                    nodeIdle.clipName = clip.name;
                } else if (lower.find("run") != std::string::npos || lower.find("walk") != std::string::npos || lower.find("walking") != std::string::npos) {
                    if (lower.find("back") != std::string::npos) {
                        nodeBackward.clipName = clip.name;
                    } else {
                        nodeForward.clipName = clip.name;
                    }
                } else if (lower.find("strafe") != std::string::npos) {
                    if (lower.find("left") != std::string::npos) {
                        nodeLeft.clipName = clip.name;
                    } else if (lower.find("right") != std::string::npos) {
                        nodeRight.clipName = clip.name;
                    }
                }
            }

            moveState.blendTree.nodes = { nodeIdle, nodeForward, nodeBackward, nodeLeft, nodeRight };
            controller->states = { idleState, moveState };

            controller->transitions.clear();

            AnimationTransition toMove;
            toMove.fromState = "Idle";
            toMove.toState = "Movement";
            toMove.crossfadeDuration = 0.25f;
            toMove.conditions = { TransitionCondition{ "speed", ">", 0.05f } };

            AnimationTransition toIdle;
            toIdle.fromState = "Movement";
            toIdle.toState = "Idle";
            toIdle.crossfadeDuration = 0.25f;
            toIdle.conditions = { TransitionCondition{ "speed", "<", 0.05f } };

            controller->transitions = { toMove, toIdle };
            controller->parameters["velocityX"] = 0.0f;
            controller->parameters["velocityY"] = 0.0f;
            controller->parameters["speed"] = 0.0f;
            controller->currentState = "Idle";
            controller->currentStateTime = 0.0f;
            controller->isCrossfading = false;
        }
    } else {
        TextDisabled("Add an Animator component to load state machines.");
    }
}

static bool doesEntityHaveRequiredComponent(Registry& registry, Entity entity, const std::string& fieldName) {
    if (!registry.isValid(entity)) return false;

    std::string lowerField = fieldName;
    for (char& c : lowerField) c = std::tolower(static_cast<unsigned char>(c));

    bool hasRequirement = false;
    bool meetsAllRequirements = true;

    auto& reflReg = Engine::ComponentReflectionRegistry::getInstance();
    for (const auto& refl : reflReg.getReflections()) {
        std::string compName = refl.name;
        std::string lowerComp = compName;
        for (char& c : lowerComp) c = std::tolower(static_cast<unsigned char>(c));

        // Check if the field name contains the component name directly (e.g. "targetTransform" contains "transform")
        if (lowerField.find(lowerComp) != std::string::npos) {
            hasRequirement = true;
            if (!refl.has(registry, entity)) {
                meetsAllRequirements = false;
                break;
            }
        }
    }

    if (hasRequirement) {
        return meetsAllRequirements;
    }

    // No specific component requirement matched, accept any entity
    return true;
}

void EditorUI::drawReflectedComponentsEditor() {
    auto& reflReg = Engine::ComponentReflectionRegistry::getInstance();
    for (const auto& refl : reflReg.getReflections()) {
        if (!refl.has(registry, selectedEntity)) continue;
        // Skip components that have dedicated custom inspector panels or structural base components
        if (refl.name == "Transform" || refl.name == "Name" || refl.name == "Hierarchy" ||
            refl.name == "Material" || refl.name == "Collider" || refl.name == "ColliderComponent" ||
            refl.name == "Skeleton" || refl.name == "SkeletonComponent" ||
            refl.name == "Animator" || refl.name == "AnimatorComponent" ||
            refl.name == "AnimationController" || refl.name == "AnimationControllerComponent" ||
            refl.name == "IKSolver" || refl.name == "IKSolverComponent" ||
            refl.name == "Camera" || refl.name == "Light" || refl.name == "LightComponent" ||
            refl.name == "Grid" || refl.name == "Tilemap" || refl.name == "TilemapComponent" ||
            refl.name == "Canvas" || refl.name == "CanvasComponent" ||
            refl.name == "RectTransform" ||
            refl.name == "UIPanel" || refl.name == "UIPanelComponent" ||
            refl.name == "UIImage" || refl.name == "UIImageComponent" ||
            refl.name == "UIText" || refl.name == "UITextComponent" ||
            refl.name == "UIButton" || refl.name == "UIButtonComponent" ||
            refl.name == "UIGridLayoutGroup" || refl.name == "UIGridLayoutGroupComponent" ||
            refl.name == "UILayoutGroup" || refl.name == "UILayoutGroupComponent" ||
            refl.name == "UIScrollRect" || refl.name == "UIScrollRectComponent" ||
            refl.name == "UISlider" || refl.name == "UISliderComponent" ||
            refl.name == "UIToggle" || refl.name == "UIToggleComponent" ||
            refl.name == "SpriteRenderer" || refl.name == "SpriteRendererComponent" ||
            refl.name == "Terrain" || refl.name == "TerrainComponent") continue;




        void* compPtr = refl.get(registry, selectedEntity);
        bool visible = true;
        
        std::string headerName = refl.displayName.empty() ? refl.name : refl.displayName;

        bool open = drawReflectedComponentHeaderWithToggle(registry, selectedEntity, refl, headerName.c_str(), &visible, ImGuiTreeNodeFlags_DefaultOpen);
        if (!visible) {
            refl.remove(registry, selectedEntity);
            statusMessage = "Removed " + refl.name + " component.";
            markSceneDirty();
            continue;
        }

        if (!open) continue;

        std::function<void(const Engine::ComponentField&, char*, const std::string&)> renderReflectedField =
            [&](const Engine::ComponentField& field, char* parentPtr, const std::string& parentId) {
                char* fieldPtr = parentPtr + field.offset;

                // Compute user-friendly label
                std::string label = field.name;
                if (label.rfind("rb", 0) == 0 && label.size() > 2 && std::isupper(label[2])) {
                    label = label.substr(2);
                } else if (label.rfind("player", 0) == 0 && label.size() > 6 && std::isupper(label[6])) {
                    label = label.substr(6);
                }

                // Custom spacing/titles for RigidBody constraints
                if (label == "FreezePX") {
                    Separator();
                    Text("Constraints");
                    Text("Freeze Position");
                    SameLine(130.0f);
                    Checkbox("X##FreezePX", reinterpret_cast<bool*>(fieldPtr));
                    return;
                }
                if (label == "FreezePY") {
                    SameLine(190.0f);
                    Checkbox("Y##FreezePY", reinterpret_cast<bool*>(fieldPtr));
                    return;
                }
                if (label == "FreezePZ") {
                    SameLine(250.0f);
                    Checkbox("Z##FreezePZ", reinterpret_cast<bool*>(fieldPtr));
                    return;
                }
                if (label == "FreezeRX") {
                    Text("Freeze Rotation");
                    SameLine(130.0f);
                    Checkbox("X##FreezeRX", reinterpret_cast<bool*>(fieldPtr));
                    return;
                }
                if (label == "FreezeRY") {
                    SameLine(190.0f);
                    Checkbox("Y##FreezeRY", reinterpret_cast<bool*>(fieldPtr));
                    return;
                }
                if (label == "FreezeRZ") {
                    SameLine(250.0f);
                    Checkbox("Z##FreezeRZ", reinterpret_cast<bool*>(fieldPtr));
                    return;
                }

                // Capitalize camelCase for cleaner drawing
                std::string displayLabel;
                for (size_t i = 0; i < label.size(); ++i) {
                    if (i > 0 && std::isupper(label[i]) && !std::isupper(label[i-1])) {
                        displayLabel += " ";
                    }
                    displayLabel += label[i];
                }
                if (!displayLabel.empty()) displayLabel[0] = std::toupper(displayLabel[0]);

                std::string imguiId = displayLabel + "##" + parentId + "_" + field.name;

                if (field.type == Engine::FieldType::Struct) {
                    if (TreeNode(imguiId.c_str())) {
                        for (const auto& sub : field.subFields) {
                            renderReflectedField(sub, fieldPtr, parentId + "_" + field.name);
                        }
                        TreePop();
                    }
                    return;
                }

                if (field.type == Engine::FieldType::Float) {
                    if (field.name == "rbVelX" || field.name == "rbVelY" || field.name == "rbVelZ") {
                        DragFloat(imguiId.c_str(), reinterpret_cast<float*>(fieldPtr), 0.05f);
                    } else if (field.name == "rbRestitution" || field.name == "rbFriction") {
                        SliderFloat(imguiId.c_str(), reinterpret_cast<float*>(fieldPtr), 0.0f, 1.0f);
                    } else if (field.name.find("Radius") != std::string::npos || field.name.find("Height") != std::string::npos) {
                        DragFloat(imguiId.c_str(), reinterpret_cast<float*>(fieldPtr), 100.0f);
                    } else if (field.name.find("Scattering") != std::string::npos || field.name.find("Absorption") != std::string::npos || field.name.find("Extinction") != std::string::npos || field.name.find("Scale") != std::string::npos) {
                        DragFloat(imguiId.c_str(), reinterpret_cast<float*>(fieldPtr), 0.000001f, 0.0f, 0.0f, "%.8f");
                    } else {
                        DragFloat(imguiId.c_str(), reinterpret_cast<float*>(fieldPtr), 0.05f);
                    }
                } else if (field.type == Engine::FieldType::Int) {
                    DragInt(imguiId.c_str(), reinterpret_cast<int*>(fieldPtr), 1);
                } else if (field.type == Engine::FieldType::Bool) {
                    Checkbox(imguiId.c_str(), reinterpret_cast<bool*>(fieldPtr));
                } else if (field.type == Engine::FieldType::Enum || !field.enumOptions.empty() || field.name == "mode") {
                    auto* enumVal = reinterpret_cast<int*>(fieldPtr);
                    int currentIdx = *enumVal;

                    std::vector<const char*> items;
                    if (!field.enumOptions.empty()) {
                        for (const auto& opt : field.enumOptions) {
                            items.push_back(opt.c_str());
                        }
                    } else if (field.name == "mode" || refl.name == "CinemachineVirtualCamera") {
                        items = { "Third Person Follow", "First Person", "Fixed Look At", "2D Follow" };
                    }

                    if (!items.empty() && Combo(imguiId.c_str(), &currentIdx, items.data(), static_cast<int>(items.size()))) {
                        *enumVal = currentIdx;
                        statusMessage = "Changed " + displayLabel + " to: " + items[currentIdx];
                    }
                } else if (field.type == Engine::FieldType::Vec2) {
                    DragFloat2(imguiId.c_str(), &reinterpret_cast<glm::vec2*>(fieldPtr)->x, 0.05f);
                } else if (field.type == Engine::FieldType::Vec3) {
                    if (field.name.find("Scattering") != std::string::npos || field.name.find("Absorption") != std::string::npos || field.name.find("Extinction") != std::string::npos) {
                        DragFloat3(imguiId.c_str(), &reinterpret_cast<glm::vec3*>(fieldPtr)->x, 0.000001f, 0.0f, 0.0f, "%.8f");
                    } else if (field.name.find("Albedo") != std::string::npos || field.name.find("Color") != std::string::npos) {
                        ColorEdit3(imguiId.c_str(), &reinterpret_cast<glm::vec3*>(fieldPtr)->x);
                    } else {
                        DragFloat3(imguiId.c_str(), &reinterpret_cast<glm::vec3*>(fieldPtr)->x, 0.05f);
                    }
                } else if (field.type == Engine::FieldType::Vec4) {
                    DragFloat4(imguiId.c_str(), &reinterpret_cast<glm::vec4*>(fieldPtr)->x, 0.05f);
                } else if (field.type == Engine::FieldType::RigidBodyType) {
                    const char* types[] = { "Dynamic", "Static" };
                    int currentType = (*reinterpret_cast<RigidBodyType*>(fieldPtr) == RigidBodyType::Static) ? 1 : 0;
                    if (Combo(imguiId.c_str(), &currentType, types, 2)) {
                        *reinterpret_cast<RigidBodyType*>(fieldPtr) = (currentType == 1) ? RigidBodyType::Static : RigidBodyType::Dynamic;
                    }
                } else if (field.type == Engine::FieldType::String) {
                    auto* strVal = reinterpret_cast<std::string*>(fieldPtr);
                    char buf[512];
                    strncpy(buf, strVal->c_str(), sizeof(buf));
                    buf[sizeof(buf) - 1] = '\0';
                    if (InputText(imguiId.c_str(), buf, sizeof(buf))) {
                        *strVal = buf;
                    }
                    if (BeginDragDropTarget()) {
                        const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH");
                        if (!payload) payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS");
                        if (payload && payload->Data) {
                            std::string pathStr((const char*)payload->Data);
                            size_t sep = pathStr.find('|');
                            if (sep != std::string::npos) pathStr = pathStr.substr(0, sep);
                            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
                            *strVal = pathStr;
                            statusMessage = "Assigned asset path: " + pathStr;
                        }
                        EndDragDropTarget();
                    }
                } else if (field.type == Engine::FieldType::Entity) {
                    auto* target = reinterpret_cast<Entity*>(fieldPtr);
                    std::string targetLabel = "None";
                    if (target->getId() != Entity::INVALID_ENTITY && registry.isValid(*target)) {
                        if (auto* nameComp = registry.get<Name>(*target)) {
                            targetLabel = nameComp->value;
                        } else {
                            targetLabel = "Entity " + std::to_string(target->getId());
                        }
                    }
                    if (ImGui::BeginCombo(imguiId.c_str(), targetLabel.c_str())) {
                        if (ImGui::Selectable("None", target->getId() == Entity::INVALID_ENTITY)) {
                            *target = Entity();
                        }
                        for (auto [ent, nameComp] : registry.view<Name>()) {
                            if (ent != selectedEntity) {
                                bool isSelected = (ent == *target);
                                if (ImGui::Selectable(nameComp.value.c_str(), isSelected)) {
                                    *target = ent;
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_HIERARCHY_ENTITY")) {
                            std::uint32_t draggedId = *static_cast<const std::uint32_t*>(payload->Data);
                            Entity draggedEntity(draggedId);
                            if (doesEntityHaveRequiredComponent(registry, draggedEntity, field.name)) {
                                *target = draggedEntity;
                            } else {
                                statusMessage = "Rejected drop: Entity lacks required component for field: " + field.name;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
            };

        for (const auto& field : refl.fields) {
            renderReflectedField(field, static_cast<char*>(compPtr), refl.name);
        }

        // Custom Inspector drawer for Inventory component items list
        if (refl.name == "Inventory") {
            struct TempInventoryItem {
                std::string itemId;
                std::string itemName;
                int count;
            };
            struct TempInventory {
                std::vector<TempInventoryItem> items;
                int maxSlots;
            };
            auto* inv = reinterpret_cast<TempInventory*>(compPtr);
            ImGui::Separator();
            ImGui::Text("Inventory Items (%d / %d):", static_cast<int>(inv->items.size()), inv->maxSlots);
            ImGui::Spacing();

            int toRemoveIdx = -1;
            for (size_t i = 0; i < inv->items.size(); ++i) {
                auto& item = inv->items[i];
                ImGui::PushID(static_cast<int>(i));
                char idBuf[64], nameBuf[64];
                strncpy_s(idBuf, item.itemId.c_str(), sizeof(idBuf) - 1);
                strncpy_s(nameBuf, item.itemName.c_str(), sizeof(nameBuf) - 1);

                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::InputText("ID", idBuf, sizeof(idBuf))) {
                    item.itemId = idBuf;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                    item.itemName = nameBuf;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(55.0f);
                ImGui::DragInt("Qty", &item.count, 1, 1, 999);
                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    toRemoveIdx = static_cast<int>(i);
                }
                ImGui::PopID();
            }

            if (toRemoveIdx >= 0 && toRemoveIdx < static_cast<int>(inv->items.size())) {
                inv->items.erase(inv->items.begin() + toRemoveIdx);
            }

            if (ImGui::Button("+ Add Item", ImVec2(-1, 0))) {
                inv->items.push_back({ "wood", "Wood Plank", 1 });
            }
        }

        // Custom Inspector drawer for GridWorldComponent
        if (refl.name == "GridWorld" || refl.name == "GridWorldComponent") {
            auto* grid = static_cast<Engine::GridWorldComponent*>(compPtr);
            ImGui::Separator();
            ImGui::Text("Grid World Statistics:");
            ImGui::BulletText("Allocated Chunks: %zu", grid->chunks.size());
            size_t modifiedCount = 0;
            size_t blockedCellCount = 0;
            size_t occupiedCellCount = 0;
            for (const auto& [key, chunk] : grid->chunks) {
                if (chunk.hasModifications) modifiedCount++;
                for (const auto& cell : chunk.cells) {
                    if (cell.isBlocked()) blockedCellCount++;
                    if (cell.isOccupied()) occupiedCellCount++;
                }
            }
            ImGui::BulletText("Modified Chunks: %zu", modifiedCount);
            ImGui::BulletText("Blocked Cells: %zu | Occupied Cells: %zu", blockedCellCount, occupiedCellCount);

            glm::ivec3 minCell(0), maxCell(0);
            grid->getBounds(minCell, maxCell);
            ImGui::BulletText("Bounds: (%d, %d, %d) to (%d, %d, %d)",
                minCell.x, minCell.y, minCell.z, maxCell.x, maxCell.y, maxCell.z);

            ImGui::Spacing();
            ImGui::Text("Grid Placement Tool:");
            const char* tools[] = { "Place (Occupy)", "Block (Obstacle)", "Clear (Free)" };
            int currentTool = (gridPlacementTool == GridPlacementTool::Block) ? 1 : ((gridPlacementTool == GridPlacementTool::Clear) ? 2 : 0);
            if (ImGui::Combo("Mode##GridTool", &currentTool, tools, 3)) {
                if (currentTool == 0) gridPlacementTool = GridPlacementTool::Place;
                else if (currentTool == 1) gridPlacementTool = GridPlacementTool::Block;
                else gridPlacementTool = GridPlacementTool::Clear;
            }

            ImGui::DragInt2("Footprint (Cells)##GridFootprint", &gridFootprintSize.x, 1.0f, 1, 16);
            if (gridFootprintSize.x < 1) gridFootprintSize.x = 1;
            if (gridFootprintSize.y < 1) gridFootprintSize.y = 1;

            ImGui::Spacing();
            ImGui::Text("Grid Building Utilities:");
            if (ImGui::Button("Snap Entity Transform to Grid", ImVec2(-1, 24))) {
                if (hasSelection && registry.isValid(selectedEntity)) {
                    if (auto* trans = registry.get<Transform>(selectedEntity)) {
                        trans->position = grid->snapPositionToGrid(trans->position);
                        statusMessage = "Snapped entity position to grid.";
                        markSceneDirty();
                    }
                }
            }

            if (ImGui::Button("Clear All Grid Occupancy", ImVec2(-1, 24))) {
                grid->chunks.clear();
                grid->isDirty = true;
                statusMessage = "Cleared all grid world chunks and occupancy.";
                markSceneDirty();
            }
        }

        // Draw runtime diagnostic section for PlayerController if playing
        if (refl.name == "PlayerController" && editorMode.isPlaying) {
            auto* pc = static_cast<PlayerControllerComponent*>(compPtr);
            ImGui::Separator();
            ImGui::Text("Runtime State:");
            ImGui::Text("Debug Update Count: %d", pc->debugRunningCount);

            // Compute camera direction exactly as in PlayerControllerSystem
            glm::vec3 testForward(0.0f, 0.0f, -1.0f);
            glm::vec3 testRight(1.0f, 0.0f, 0.0f);
            int cameraCount = 0;
            for (auto [camEntity, cam, camTransform] : registry.view<Camera, Transform>()) {
                cameraCount++;
                float yaw = camTransform.rotation.y;
                testForward.x = cos(glm::radians(yaw));
                testForward.y = 0.0f;
                testForward.z = sin(glm::radians(yaw));
                if (glm::length(testForward) > 1e-4f) {
                    testForward = glm::normalize(testForward);
                }
                testRight = glm::normalize(glm::cross(testForward, glm::vec3(0.0f, 1.0f, 0.0f)));
                break;
            }
            ImGui::Text("Cameras found: %d", cameraCount);
            ImGui::Text("Cam Forward: (%.3f, %.3f)", testForward.x, testForward.z);
            ImGui::Text("Cam Right: (%.3f, %.3f)", testRight.x, testRight.z);
            ImGui::Text("MoveDir Length: %.4f", pc->debugMoveDirLength);
            ImGui::Text("PC Set Velocity: (%.3f, %.3f, %.3f)", pc->debugRbVelocity.x, pc->debugRbVelocity.y, pc->debugRbVelocity.z);

            if (auto* rb = registry.get<RigidBodyComponent>(selectedEntity)) {
                ImGui::Text("Sleeping: %s", rb->sleeping ? "Yes" : "No");
                ImGui::Text("Velocity: (%.3f, %.3f, %.3f)", rb->velocity.x, rb->velocity.y, rb->velocity.z);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Warning: No RigidBodyComponent found!");
            }

            if (window) {
                ImGui::Text("W pressed: %s", (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) ? "Yes" : "No");
                ImGui::Text("A pressed: %s", (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) ? "Yes" : "No");
                ImGui::Text("S pressed: %s", (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) ? "Yes" : "No");
                ImGui::Text("D pressed: %s", (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) ? "Yes" : "No");
            }
        }
    }
}

void EditorUI::drawColliderEditor() {
    ColliderComponent* col = registry.get<ColliderComponent>(selectedEntity);
    if (!col) return;

    bool visible = true;
    bool open = drawComponentHeaderWithToggle<ColliderComponent>(registry, selectedEntity, "Collider", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    if (!visible) {
        registry.remove<ColliderComponent>(selectedEntity);
        statusMessage = "Removed Collider component.";
        markSceneDirty();
        return;
    }

    if (!open) return;

    // Shape Selection
    const char* shapes[] = { "Sphere", "AABB", "OBB", "Capsule" };
    int currentShape = 1;
    if (col->shape == ColliderShape::Sphere) currentShape = 0;
    else if (col->shape == ColliderShape::OBB) currentShape = 2;
    else if (col->shape == ColliderShape::Capsule) currentShape = 3;

    if (Combo("Shape", &currentShape, shapes, 4)) {
        if (currentShape == 0) col->shape = ColliderShape::Sphere;
        else if (currentShape == 2) col->shape = ColliderShape::OBB;
        else if (currentShape == 3) col->shape = ColliderShape::Capsule;
        else col->shape = ColliderShape::AABB;
    }

    if (col->shape == ColliderShape::Sphere) {
        if (DragFloat("Radius", &col->radius, 0.05f, 0.001f, 100.0f)) {
            if (col->radius < 0.001f) col->radius = 0.001f;
        }
    } else if (col->shape == ColliderShape::Capsule) {
        if (DragFloat("Radius", &col->radius, 0.05f, 0.001f, 100.0f)) {
            if (col->radius < 0.001f) col->radius = 0.001f;
            if (col->height < col->radius * 2.0f) col->height = col->radius * 2.0f;
        }
        if (DragFloat("Height", &col->height, 0.05f, 0.001f, 100.0f)) {
            if (col->height < col->radius * 2.0f) col->height = col->radius * 2.0f;
        }
    } else {
        if (DragFloat3("Half-Extents", &col->extents[0], 0.05f, 0.001f, 100.0f)) {
            if (col->extents.x < 0.001f) col->extents.x = 0.001f;
            if (col->extents.y < 0.001f) col->extents.y = 0.001f;
            if (col->extents.z < 0.001f) col->extents.z = 0.001f;
        }
    }

    DragFloat3("Center Offset", &col->offset[0], 0.05f);
}


void EditorUI::drawTilemapInspector() {
    if (!hasSelection) return;
    // Terrains are 3D heightfields and must never have a 2D TilemapComponent
    if (registry.has<Engine::TerrainComponent>(selectedEntity)) {
        if (registry.has<Engine::TilemapComponent>(selectedEntity)) {
            registry.remove<Engine::TilemapComponent>(selectedEntity);
        }
        return;
    }
    auto* tm = registry.get<Engine::TilemapComponent>(selectedEntity);
    if (!tm) return;

    // Sync active tilemap brush painting target
    s_brushTilemapEntity = selectedEntity;

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.22f, 0.35f, 0.55f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.45f, 0.70f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.18f, 0.28f, 0.45f, 1.f));
    
    if (drawComponentHeaderWithToggle<Engine::TilemapComponent>(registry, selectedEntity, "Tilemap", nullptr, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginGroup();
        Text("Tileset Path:");
        char pathBuf[512];
        strncpy_s(pathBuf, tm->tilesetPath.c_str(), sizeof(pathBuf) - 1);
        SetNextItemWidth(-1);
        if (InputText("##TilesetPathInput", pathBuf, sizeof(pathBuf))) {
            tm->tilesetPath = pathBuf;
            tm->isDirty = true;
        }
        ImGui::EndGroup();

        if (BeginDragDropTarget()) {
            const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH");
            if (!payload) payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS");
            if (payload && payload->Data) {
                std::string pathStr((const char*)payload->Data);
                size_t sep = pathStr.find('|');
                if (sep != std::string::npos) pathStr = pathStr.substr(0, sep);

                std::filesystem::path dragPath(pathStr);
                std::string ext = dragPath.extension().string();
                for (char& c : ext) c = (char)::tolower((unsigned char)c);
                if (ext == ".tileset") {
                    tm->tilesetPath = dragPath.generic_string();
                    tm->isDirty = true;
                    Engine::invalidateTilesetCache(tm->tilesetPath);
                    if (auto* ts = Engine::loadOrGetTileset(tm->tilesetPath, renderer)) {
                        if (auto* mat = registry.get<Material>(selectedEntity)) {
                            mat->descriptorSet = ts->atlas.descriptorSet;
                        }
                    }
                    statusMessage = "Assigned tileset: " + tm->tilesetPath;
                }
            }
            EndDragDropTarget();
        }
        TextDisabled("Drag & drop a .tileset asset onto the field above");
        Spacing();

        DragFloat("Tile Size##tmTS", &tm->tileSize, 0.01f, 0.01f, 100.f);

        int minX = 0, minY = 0, maxX = 0, maxY = 0;
        tm->getBounds(minX, minY, maxX, maxY);
        size_t totalChunks = 0;
        for (const auto& l : tm->layers) totalChunks += l.chunks.size();

        TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Infinite Tilemap Mode");
        Text("Active 16x16 Chunks: %zu", totalChunks);
        Text("Painted Bounds: (%d, %d) to (%d, %d)", minX, minY, maxX, maxY);

        Spacing();
        Text("Layers (Active paint, Visible, Col, Name, Tag, zOffset, Delete):");

        int layerToDelete = -1;
        for (int i = 0; i < (int)tm->layers.size(); ++i) {
            auto& layer = tm->layers[i];
            PushID(i);
            
            // Active radio button
            bool isActive = (s_tilemapActiveLayer == i);
            if (RadioButton("##active", isActive)) {
                s_tilemapActiveLayer = i;
            }
            SameLine();

            // Visibility checkbox
            if (Checkbox("##visible", &layer.isVisible)) {
                tm->isDirty = true;
            }
            SameLine();

            // Collision layer checkbox
            if (Checkbox("##collision", &layer.isCollision)) {
                tm->isDirty = true;
            }
            SameLine();

            // Name input
            char nameBuf[256];
            strncpy_s(nameBuf, layer.name.c_str(), sizeof(nameBuf) - 1);
            SetNextItemWidth(100.0f);
            if (InputText("##name", nameBuf, sizeof(nameBuf))) {
                layer.name = nameBuf;
            }
            SameLine();

            // Tag input
            char tagBuf[256];
            strncpy_s(tagBuf, layer.tag.c_str(), sizeof(tagBuf) - 1);
            SetNextItemWidth(80.0f);
            if (InputText("##tag", tagBuf, sizeof(tagBuf))) {
                layer.tag = tagBuf;
                tm->isDirty = true;
            }
            SameLine();

            // zOffset drag float
            SetNextItemWidth(60.0f);
            if (DragFloat("##zoffset", &layer.zOffset, 0.01f, -100.0f, 100.0f, "%.2f")) {
                tm->isDirty = true;
            }
            SameLine();

            // Delete button
            if (tm->layers.size() > 1) {
                if (Button("X")) {
                    layerToDelete = i;
                }
            } else {
                TextDisabled("X");
            }

            PopID();
        }

        if (layerToDelete != -1) {
            tm->layers.erase(tm->layers.begin() + layerToDelete);
            if (s_tilemapActiveLayer >= (int)tm->layers.size()) {
                s_tilemapActiveLayer = (int)tm->layers.size() - 1;
            }
            tm->isDirty = true;
            statusMessage = "Deleted layer.";
        }

        if (Button("Add Layer")) {
            Engine::TilemapLayer newLayer;
            newLayer.name = "Layer " + to_string(tm->layers.size());
            newLayer.zOffset = tm->layers.empty() ? 0.0f : (tm->layers.back().zOffset + 0.01f);
            newLayer.tag = "";
            newLayer.isVisible = true;
            tm->layers.push_back(newLayer);
            tm->isDirty = true;
            statusMessage = "Added layer: " + newLayer.name;
        }

        Spacing();
        if (Button("Clear All Tiles")) {
            for (auto& layer : tm->layers) {
                layer.chunks.clear();
            }
            tm->isDirty = true;
            statusMessage = "Cleared all tilemap layers.";
        }
        SameLine();
        if (Button("Open Tileset Editor")) {
            if (!tm->tilesetPath.empty()) {
                s_editingTilesetPath = tm->tilesetPath;
                s_editingTileset = Engine::TilesetAsset::loadFromFile(tm->tilesetPath);
                if (auto* ts = Engine::loadOrGetTileset(tm->tilesetPath, renderer)) {
                    s_editingTileset.atlas = ts->atlas;
                }
                s_tilesetLoaded = true;
            }
            s_openTilesetEditorWindow = true;
        }
    }
    PopStyleColor(3);
}

void EditorUI::drawUIComponentsEditor() {
    if (!hasSelection) return;

    // 0. Prefab Instance Editor
    if (auto* prefab = registry.get<Engine::PrefabComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.10f, 0.45f, 0.65f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.15f, 0.55f, 0.75f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.08f, 0.35f, 0.55f, 1.f));
        bool visible = true;
        if (CollapsingHeader("Prefab Instance", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            Text("Source Asset: %s", prefab->prefabAssetPath.c_str());
            Spacing();

            if (Button("Save / Apply to Prefab")) {
                if (!prefab->prefabAssetPath.empty()) {
                    SceneSerializer serializer(registry, renderer);
                    if (serializer.serializePrefab(prefab->prefabAssetPath, selectedEntity)) {
                        statusMessage = "Saved changes to prefab asset: " + prefab->prefabAssetPath;
                    } else {
                        statusMessage = "Failed to save prefab asset: " + prefab->prefabAssetPath;
                    }
                }
            }
            SameLine();
            if (Button("Revert / Reload")) {
                Scene* currentScene = sceneManager.getCurrentScene();
                if (currentScene && !prefab->prefabAssetPath.empty()) {
                    std::string path = prefab->prefabAssetPath;
                    Entity parent = Entity();
                    if (auto* h = registry.get<HierarchyComponent>(selectedEntity)) {
                        parent = h->parent;
                    }
                    currentScene->deleteEntity(selectedEntity);
                    selectedEntity = currentScene->instantiatePrefab(path, glm::vec3(0.0f), parent);
                    hasSelection = registry.isValid(selectedEntity);
                    statusMessage = "Reverted prefab instance from " + path;
                }
            }
            SameLine();
            if (Button("Unpack")) {
                registry.remove<Engine::PrefabComponent>(selectedEntity);
                statusMessage = "Unpacked prefab instance.";
            }
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::PrefabComponent>(selectedEntity);
            statusMessage = "Removed Prefab component.";
        }
    }

    // 1. Canvas Editor
    if (auto* canvas = registry.get<Engine::CanvasComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.25f, 0.40f, 0.40f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.32f, 0.50f, 0.50f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.20f, 0.32f, 0.32f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::CanvasComponent>(registry, selectedEntity, "UI Canvas", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            Checkbox("Screen Space Overlay##canvas_ss", &canvas->isScreenSpace);

            Spacing();
            static std::string s_exportedCode;
            if (Button("Export UI to C++ Code")) {
                s_exportedCode = Engine::UIBuilder::ExportHierarchyToCode(registry, selectedEntity);
                OpenPopup("##ExportUICodePopup");
            }

            if (BeginPopup("##ExportUICodePopup")) {
                Text("Generated C++ UI Code:");
                Separator();
                InputTextMultiline("##uicodesnip", const_cast<char*>(s_exportedCode.c_str()), s_exportedCode.size() + 1, ImVec2(500, 300), ImGuiInputTextFlags_ReadOnly);
                if (Button("Copy to Clipboard")) {
                    SetClipboardText(s_exportedCode.c_str());
                    CloseCurrentPopup();
                }
                SameLine();
                if (Button("Close")) CloseCurrentPopup();
                EndPopup();
            }
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::CanvasComponent>(selectedEntity);
            statusMessage = "Removed Canvas component.";
            markSceneDirty();
        }
    }

    // Helper for RectTransform preset combo
    auto getPresetName = [](const glm::vec2& amin, const glm::vec2& amax) -> std::string {
        if (amin == glm::vec2(0.5f, 0.5f) && amax == glm::vec2(0.5f, 0.5f)) return "Center";
        if (amin == glm::vec2(0.f, 0.f) && amax == glm::vec2(0.f, 0.f)) return "Top-Left";
        if (amin == glm::vec2(0.5f, 0.f) && amax == glm::vec2(0.5f, 0.f)) return "Top-Center";
        if (amin == glm::vec2(1.f, 0.f) && amax == glm::vec2(1.f, 0.f)) return "Top-Right";
        if (amin == glm::vec2(0.f, 0.5f) && amax == glm::vec2(0.f, 0.5f)) return "Center-Left";
        if (amin == glm::vec2(1.f, 0.5f) && amax == glm::vec2(1.f, 0.5f)) return "Center-Right";
        if (amin == glm::vec2(0.f, 1.f) && amax == glm::vec2(0.f, 1.f)) return "Bottom-Left";
        if (amin == glm::vec2(0.5f, 1.f) && amax == glm::vec2(0.5f, 1.f)) return "Bottom-Center";
        if (amin == glm::vec2(1.f, 1.f) && amax == glm::vec2(1.f, 1.f)) return "Bottom-Right";
        if (amin == glm::vec2(0.f, 0.f) && amax == glm::vec2(1.f, 1.f)) return "Stretch-All";
        return "Custom";
    };

    // 2. RectTransform Component
    if (auto* rect = registry.get<Engine::RectTransform>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.25f, 0.35f, 0.45f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.35f, 0.45f, 0.55f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.20f, 0.30f, 0.40f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::RectTransform>(registry, selectedEntity, "UI RectTransform", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            std::string preset = getPresetName(rect->anchorMin, rect->anchorMax);
            const char* presets[] = { "Center", "Top-Left", "Top-Center", "Top-Right", "Center-Left", "Center-Right", "Bottom-Left", "Bottom-Center", "Bottom-Right", "Stretch-All", "Custom" };
            int curPreset = 10;
            for (int i = 0; i < 10; ++i) if (preset == presets[i]) { curPreset = i; break; }

            if (BeginCombo("Anchor Preset##rect_pre", presets[curPreset])) {
                for (int i = 0; i < 10; ++i) {
                    bool sel = (curPreset == i);
                    if (Selectable(presets[i], sel)) {
                        switch (i) {
                            case 0: rect->anchorMin = {0.5f, 0.5f}; rect->anchorMax = {0.5f, 0.5f}; break;
                            case 1: rect->anchorMin = {0.f, 0.f};   rect->anchorMax = {0.f, 0.f};   break;
                            case 2: rect->anchorMin = {0.5f, 0.f};  rect->anchorMax = {0.5f, 0.f};  break;
                            case 3: rect->anchorMin = {1.f, 0.f};   rect->anchorMax = {1.f, 0.f};   break;
                            case 4: rect->anchorMin = {0.f, 0.5f};  rect->anchorMax = {0.f, 0.5f};  break;
                            case 5: rect->anchorMin = {1.f, 0.5f};  rect->anchorMax = {1.f, 0.5f};  break;
                            case 6: rect->anchorMin = {0.f, 1.f};   rect->anchorMax = {0.f, 1.f};   break;
                            case 7: rect->anchorMin = {0.5f, 1.f};  rect->anchorMax = {0.5f, 1.f};  break;
                            case 8: rect->anchorMin = {1.f, 1.f};   rect->anchorMax = {1.f, 1.f};   break;
                            case 9: rect->anchorMin = {0.f, 0.f};   rect->anchorMax = {1.f, 1.f};   break;
                        }
                    }
                }
                EndCombo();
            }

            DragFloat2("Anchored Position##rect_pos", &rect->anchoredPosition.x, 1.f);
            if (rect->anchorMin == rect->anchorMax) {
                DragFloat2("Size Delta##rect_sd", &rect->sizeDelta.x, 1.f, 0.f, 4096.f);
            } else {
                DragFloat2("Margins (R/B)##rect_sd", &rect->sizeDelta.x, 1.f);
            }
            DragFloat2("Pivot##rect_pivot", &rect->pivot.x, 0.01f, 0.f, 1.f);

            // Allow custom anchor edits if user wants custom values
            if (preset == "Custom" || ImGui::TreeNode("Manual Anchors")) {
                DragFloat2("Anchor Min##rect_amin", &rect->anchorMin.x, 0.01f, 0.f, 1.f);
                DragFloat2("Anchor Max##rect_amax", &rect->anchorMax.x, 0.01f, 0.f, 1.f);
                if (preset != "Custom") ImGui::TreePop();
            }
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::RectTransform>(selectedEntity);
            statusMessage = "Removed RectTransform component.";
            markSceneDirty();
        }
    }

    // 3. Panel Component
    if (auto* panel = registry.get<Engine::UIPanelComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.35f, 0.20f, 0.40f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.45f, 0.25f, 0.50f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.28f, 0.15f, 0.30f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UIPanelComponent>(registry, selectedEntity, "UI Panel", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            ColorEdit4("Background Color##panel_col", &panel->color.x);
            SliderFloat("Corner Radius##panel_br", &panel->borderRadius, 0.f, 100.f);
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UIPanelComponent>(selectedEntity);
            statusMessage = "Removed Panel component.";
            markSceneDirty();
        }
    }

    // 4. Image Component
    if (auto* img = registry.get<Engine::UIImageComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.40f, 0.35f, 0.20f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.50f, 0.45f, 0.25f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.30f, 0.28f, 0.15f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UIImageComponent>(registry, selectedEntity, "UI Image", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            char pathBuf[512];
            strncpy_s(pathBuf, img->texturePath.c_str(), sizeof(pathBuf) - 1);
            if (InputText("Texture Path##img_tex", pathBuf, sizeof(pathBuf))) {
                img->texturePath = pathBuf;
            }
            if (BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
                    img->texturePath = (const char*)payload->Data;
                    statusMessage = "Assigned UI image texture: " + img->texturePath;
                }
                EndDragDropTarget();
            }
            TextDisabled("Drop a texture file here");

            ColorEdit4("Tint Color##img_tint", &img->tintColor.x);
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UIImageComponent>(selectedEntity);
            statusMessage = "Removed Image component.";
            markSceneDirty();
        }
    }

    // 5. Text Component
    if (auto* txt = registry.get<Engine::UITextComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f, 0.40f, 0.30f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.50f, 0.38f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.15f, 0.30f, 0.22f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UITextComponent>(registry, selectedEntity, "UI Text", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            char textBuf[1024];
            strncpy_s(textBuf, txt->text.c_str(), sizeof(textBuf) - 1);
            if (InputTextMultiline("Content##txt_val", textBuf, sizeof(textBuf), ImVec2(-1, 60))) {
                txt->text = textBuf;
            }
            DragFloat("Font Size##txt_sz", &txt->fontSize, 0.5f, 1.f, 256.f);
            ColorEdit4("Text Color##txt_col", &txt->color.x);
            Checkbox("Align Center##txt_ac", &txt->alignCenter);
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UITextComponent>(selectedEntity);
            statusMessage = "Removed Text component.";
            markSceneDirty();
        }
    }

    // 6. Button Component
    if (auto* btn = registry.get<Engine::UIButtonComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.40f, 0.20f, 0.20f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.50f, 0.25f, 0.25f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.30f, 0.15f, 0.15f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UIButtonComponent>(registry, selectedEntity, "UI Button", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            char labelBuf[128];
            strncpy_s(labelBuf, btn->label.c_str(), sizeof(labelBuf) - 1);
            if (InputText("Button Label##btn_lbl", labelBuf, sizeof(labelBuf))) {
                btn->label = labelBuf;
            }

            char eventBuf[128];
            strncpy_s(eventBuf, btn->clickEventName.c_str(), sizeof(eventBuf) - 1);
            if (InputText("Click Event##btn_evt", eventBuf, sizeof(eventBuf))) {
                btn->clickEventName = eventBuf;
            }

            ColorEdit4("Normal Color##btn_col_n", &btn->normalColor.x);
            ColorEdit4("Hover Color##btn_col_h", &btn->hoverColor.x);
            ColorEdit4("Pressed Color##btn_col_p", &btn->pressedColor.x);
            ColorEdit4("Text Color##btn_col_t", &btn->textColor.x);

            if (Button("Trigger Click Preview##btn_trg")) {
                btn->isClicked = true;
                statusMessage = "Clicked button: " + btn->label;
            }
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UIButtonComponent>(selectedEntity);
            statusMessage = "Removed Button component.";
            markSceneDirty();
        }
    }

    // 7. Grid Layout Group Component
    if (auto* grid = registry.get<Engine::UIGridLayoutGroupComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.30f, 0.35f, 0.20f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.40f, 0.45f, 0.25f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.22f, 0.28f, 0.15f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UIGridLayoutGroupComponent>(registry, selectedEntity, "UI Grid Layout Group", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            DragFloat2("Cell Size##grid_cs", &grid->cellSize.x, 1.0f, 1.0f, 1024.0f);
            DragFloat2("Spacing##grid_sp", &grid->spacing.x, 1.0f, 0.0f, 256.0f);
            DragFloat4("Padding (T/R/B/L)##grid_pad", &grid->padding.x, 1.0f, 0.0f, 256.0f);

            int mode = static_cast<int>(grid->constraint);
            const char* modes[] = { "Flexible", "Fixed Column Count", "Fixed Row Count" };
            if (Combo("Constraint Mode##grid_mode", &mode, modes, IM_ARRAYSIZE(modes))) {
                grid->constraint = static_cast<Engine::GridConstraint>(mode);
            }

            if (grid->constraint == Engine::GridConstraint::FixedColumnCount) {
                DragInt("Max Columns##grid_cols", &grid->constraintCount, 0.2f, 1, 64);
            } else if (grid->constraint == Engine::GridConstraint::FixedRowCount) {
                DragInt("Max Rows##grid_rows", &grid->constraintCount, 0.2f, 1, 64);
            }
        }

        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UIGridLayoutGroupComponent>(selectedEntity);
            statusMessage = "Removed Grid Layout Group component.";
            markSceneDirty();
        }
    }

    // 8. Layout Group Component
    if (auto* layout = registry.get<Engine::UILayoutGroupComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f, 0.35f, 0.35f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.45f, 0.45f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.15f, 0.28f, 0.28f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UILayoutGroupComponent>(registry, selectedEntity, "UI Layout Group", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            Checkbox("Vertical Layout##lg_vert", &layout->isVertical);
            DragFloat("Spacing##lg_sp", &layout->spacing, 1.0f, 0.0f, 256.0f);
            DragFloat4("Padding (T/R/B/L)##lg_pad", &layout->padding.x, 1.0f, 0.0f, 256.0f);
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UILayoutGroupComponent>(selectedEntity);
            statusMessage = "Removed Layout Group component.";
            markSceneDirty();
        }
    }

    // 9. Scroll Rect Component
    if (auto* scroll = registry.get<Engine::UIScrollRectComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.40f, 0.25f, 0.20f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.50f, 0.32f, 0.25f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.30f, 0.18f, 0.15f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UIScrollRectComponent>(registry, selectedEntity, "UI Scroll Rect", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            Checkbox("Vertical Scrolling##scroll_v", &scroll->vertical);
            Checkbox("Horizontal Scrolling##scroll_h", &scroll->horizontal);
            DragFloat2("Scroll Position##scroll_pos", &scroll->scrollPosition.x, 1.0f);
            DragFloat("Scroll Speed##scroll_spd", &scroll->scrollSpeed, 0.5f, 1.0f, 200.0f);
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UIScrollRectComponent>(selectedEntity);
            statusMessage = "Removed Scroll Rect component.";
            markSceneDirty();
        }
    }

    // 10. Slider Component
    if (auto* slider = registry.get<Engine::UISliderComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f, 0.30f, 0.50f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.40f, 0.60f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.15f, 0.22f, 0.38f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UISliderComponent>(registry, selectedEntity, "UI Slider", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            SliderFloat("Value##slider_val", &slider->value, slider->minValue, slider->maxValue);
            DragFloat("Min Value##slider_min", &slider->minValue, 0.1f);
            DragFloat("Max Value##slider_max", &slider->maxValue, 0.1f);
            ColorEdit4("Background Color##slider_bg", &slider->backgroundColor.x);
            ColorEdit4("Fill Color##slider_fill", &slider->fillColor.x);
            ColorEdit4("Handle Color##slider_handle", &slider->handleColor.x);
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UISliderComponent>(selectedEntity);
            statusMessage = "Removed Slider component.";
            markSceneDirty();
        }
    }

    // 11. Toggle Component
    if (auto* toggle = registry.get<Engine::UIToggleComponent>(selectedEntity)) {
        PushStyleColor(ImGuiCol_Header,        ImVec4(0.25f, 0.40f, 0.25f, 1.f));
        PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.32f, 0.50f, 0.32f, 1.f));
        PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.18f, 0.30f, 0.18f, 1.f));
        bool visible = true;
        if (drawComponentHeaderWithToggle<Engine::UIToggleComponent>(registry, selectedEntity, "UI Toggle", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
            Checkbox("Is On##toggle_on", &toggle->isOn);
            char labelBuf[128];
            strncpy_s(labelBuf, toggle->label.c_str(), sizeof(labelBuf) - 1);
            if (InputText("Toggle Label##toggle_lbl", labelBuf, sizeof(labelBuf))) {
                toggle->label = labelBuf;
            }
            ColorEdit4("Box Color##toggle_box", &toggle->boxColor.x);
            ColorEdit4("Checkmark Color##toggle_chk", &toggle->checkmarkColor.x);
            ColorEdit4("Text Color##toggle_txt", &toggle->textColor.x);
        }
        PopStyleColor(3);
        if (!visible) {
            registry.remove<Engine::UIToggleComponent>(selectedEntity);
            statusMessage = "Removed Toggle component.";
            markSceneDirty();
        }
    }

}

void EditorUI::drawSpriteRendererInspector() {
    if (!hasSelection) return;
    auto* sprite = registry.get<Engine::SpriteRenderer>(selectedEntity);
    if (!sprite) return;

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.55f, 0.30f, 0.10f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.70f, 0.40f, 0.15f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.40f, 0.22f, 0.08f, 1.f));
    bool visible = true;
    bool open    = drawComponentHeaderWithToggle<Engine::SpriteRenderer>(registry, selectedEntity, "Sprite Renderer", &visible, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);

    if (!visible) {
        registry.remove<Engine::SpriteRenderer>(selectedEntity);
        // Also clean up the managed Mesh and Material so the entity stops rendering
        if (registry.get<Mesh>(selectedEntity)) registry.remove<Mesh>(selectedEntity);
        if (registry.get<Material>(selectedEntity)) registry.remove<Material>(selectedEntity);
        statusMessage = "Removed Sprite Renderer component.";
        markSceneDirty();
        return;
    }

    if (!open) return;

    // Sync fields from active AnimatorComponent property keyframes if available
    if (auto* animator = registry.get<AnimatorComponent>(selectedEntity)) {
        if (animator->activeAnimationIndex >= 0 && animator->activeAnimationIndex < static_cast<int>(animator->animations.size())) {
            auto& clip = animator->animations[animator->activeAnimationIndex];
            for (const auto& chan : clip.propertyChannels) {
                if (chan.keys.empty()) continue;
                std::string fc = chan.fieldName;
                for (char& c : fc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                float t = animator->currentTime;
                if (fc.find("texture") != std::string::npos || fc.find("path") != std::string::npos || fc.find("image") != std::string::npos) {
                    std::string sampledStr = chan.keys.front().stringValue;
                    if (t >= chan.keys.back().time) sampledStr = chan.keys.back().stringValue;
                    else {
                        for (size_t i = 0; i < chan.keys.size() - 1; ++i) {
                            if (t >= chan.keys[i].time && t < chan.keys[i+1].time) {
                                sampledStr = chan.keys[i].stringValue;
                                break;
                            }
                        }
                    }
                    if (!sampledStr.empty()) {
                        sprite->texturePath = sampledStr;
                    }
                } else if (fc.find("flipx") != std::string::npos) {
                    float sampledVal = chan.keys.front().value.x;
                    if (t >= chan.keys.back().time) sampledVal = chan.keys.back().value.x;
                    else {
                        for (size_t i = 0; i < chan.keys.size() - 1; ++i) {
                            if (t >= chan.keys[i].time && t < chan.keys[i+1].time) {
                                float factor = (t - chan.keys[i].time) / (chan.keys[i+1].time - chan.keys[i].time);
                                sampledVal = glm::mix(chan.keys[i].value.x, chan.keys[i+1].value.x, factor);
                                break;
                            }
                        }
                    }
                    sprite->flipX = (sampledVal > 0.5f);
                } else if (fc.find("flipy") != std::string::npos) {
                    float sampledVal = chan.keys.front().value.x;
                    if (t >= chan.keys.back().time) sampledVal = chan.keys.back().value.x;
                    else {
                        for (size_t i = 0; i < chan.keys.size() - 1; ++i) {
                            if (t >= chan.keys[i].time && t < chan.keys[i+1].time) {
                                float factor = (t - chan.keys[i].time) / (chan.keys[i+1].time - chan.keys[i].time);
                                sampledVal = glm::mix(chan.keys[i].value.x, chan.keys[i+1].value.x, factor);
                                break;
                            }
                        }
                    }
                    sprite->flipY = (sampledVal > 0.5f);
                } else if (fc.find("color") != std::string::npos) {
                    glm::vec4 sampledVal = chan.keys.front().value;
                    if (t >= chan.keys.back().time) sampledVal = chan.keys.back().value;
                    else {
                        for (size_t i = 0; i < chan.keys.size() - 1; ++i) {
                            if (t >= chan.keys[i].time && t < chan.keys[i+1].time) {
                                float factor = (t - chan.keys[i].time) / (chan.keys[i+1].time - chan.keys[i].time);
                                sampledVal = glm::mix(chan.keys[i].value, chan.keys[i+1].value, factor);
                                break;
                            }
                        }
                    }
                    sprite->color = sampledVal;
                }
            }
        }
    }

    // ---- Texture Path ----
    char texBuf[512]{};
    strncpy_s(texBuf, sprite->texturePath.c_str(), sizeof(texBuf) - 1);
    if (InputText("Texture##spr_tex", texBuf, sizeof(texBuf))) {
        sprite->texturePath = texBuf;
        sprite->_dirty = true;
    }
    if (BeginDragDropTarget()) {
        const ImGuiPayload* payload = AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH");
        if (!payload) payload = AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS");
        if (payload && payload->Data) {
            std::string dropped((const char*)payload->Data);
            size_t sep = dropped.find('|');
            if (sep != std::string::npos) dropped = dropped.substr(0, sep);
            std::replace(dropped.begin(), dropped.end(), '\\', '/');
            std::string ext = std::filesystem::path(dropped).extension().string();
            for (char& c : ext) c = (char)::tolower((unsigned char)c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".dds" || ext == ".hdr" || ext == ".webp") {
                sprite->texturePath = dropped;
                sprite->_dirty = true;
                statusMessage = "Assigned sprite texture: " + dropped;
            } else {
                statusMessage = "Error: not a valid texture file.";
            }
        }
        EndDragDropTarget();
    }
    TextDisabled("Drop a PNG / JPG / TGA here");

    // ---- Native Texture Size Buttons ----
    if (!sprite->texturePath.empty() && renderer.resourceManager) {
        if (Texture* tex = renderer.resourceManager->loadTexture(sprite->texturePath, renderer)) {
            if (tex->width > 0 && tex->height > 0) {
                Spacing();
                std::string btnLabel = "Use Texture Size (" + std::to_string(tex->width) + "x" + std::to_string(tex->height) + ")";
                if (Button(btnLabel.c_str(), ImVec2(-1, 0))) {
                    if (auto* transform = registry.get<Transform>(selectedEntity)) {
                        transform->scale.x = static_cast<float>(tex->width);
                        transform->scale.y = static_cast<float>(tex->height);
                        statusMessage = "Set sprite scale to texture size: " + std::to_string(tex->width) + "x" + std::to_string(tex->height);
                    }
                }
                if (Button("Set Aspect Ratio Scale", ImVec2(-1, 0))) {
                    if (auto* transform = registry.get<Transform>(selectedEntity)) {
                        float aspect = static_cast<float>(tex->width) / static_cast<float>(tex->height);
                        transform->scale.x = transform->scale.y * aspect;
                        statusMessage = "Adjusted scale X to match texture aspect ratio (" + std::to_string(aspect) + ")";
                    }
                }
            }
        }
    }

    Spacing();

    // ---- Colour ----
    if (ColorEdit4("Color##spr_col", &sprite->color.x)) {
        // Colour synced each frame by SpriteSystem
    }

    Spacing();

    // ---- Flip ----
    bool flipXChanged = Checkbox("Flip X##spr_fx", &sprite->flipX);
    SameLine();
    bool flipYChanged = Checkbox("Flip Y##spr_fy", &sprite->flipY);
    if (flipXChanged || flipYChanged) {
        // Push constants are updated by SpriteSystem on next frame — no _dirty needed
    }

    Spacing();

    // ---- Sort Order ----
    if (DragInt("Sort Order##spr_so", &sprite->sortOrder, 1.f, -9999, 9999)) {
        // SpriteSystem syncs Transform.z on next frame
    }
    TextDisabled("Lower = behind  |  Higher = in front");
}

void EditorUI::drawTerrainInspector() {
    if (!hasSelection) return;
    auto* terrain = registry.get<Engine::TerrainComponent>(selectedEntity);
    if (!terrain) return;

    // Safety: ensure any accidental TilemapComponent is removed from terrain
    if (registry.has<Engine::TilemapComponent>(selectedEntity)) {
        registry.remove<Engine::TilemapComponent>(selectedEntity);
    }

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f, 0.38f, 0.25f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.48f, 0.32f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.18f, 0.32f, 0.20f, 1.f));

    bool visible = true;
    if (drawComponentHeaderWithToggle<Engine::TerrainComponent>(registry, selectedEntity, "Terrain", &visible, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PopStyleColor(3);

        // =====================================================================
        // TOP STATUS & QUICK CONTROLS BAR
        // =====================================================================
        ImGui::Spacing();
        float availW = ImGui::GetContentRegionAvail().x;
        float undoW = 28.0f;
        float gap = 4.0f;
        float modeW = availW - (undoW * 2.0f + gap * 2.0f);

        // Interactive Brush vs Gizmo Toggle
        if (terrain->isSculptingActive) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.52f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.62f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.44f, 0.22f, 1.0f));

            const char* modeLabel = "[Brush: Sculpt Active]";
            if (terrain->toolMode == Engine::TerrainToolMode::PaintTexture) {
                modeLabel = "[Brush: Paint Active]";
            } else if (terrain->toolMode == Engine::TerrainToolMode::PaintFoliage) {
                modeLabel = "[Brush: Foliage Active]";
            }

            if (ImGui::Button(modeLabel, ImVec2(modeW, 26))) {
                terrain->isSculptingActive = false;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Interactive brush is ENABLED in viewport.\nClick to switch to Transform Gizmo (allows moving/rotating terrain entity).");
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.32f, 0.35f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.44f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.26f, 0.28f, 0.34f, 1.0f));

            if (ImGui::Button("[Gizmo Mode / Brush Off]", ImVec2(modeW, 26))) {
                terrain->isSculptingActive = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Transform Gizmo is active in viewport.\nClick to enable interactive terrain brush.");
            }
            ImGui::PopStyleColor(3);
        }

        // Quick Undo / Redo
        ImGui::SameLine(0, gap);
        bool canUndo = terrain->canUndo();
        if (!canUndo) ImGui::BeginDisabled();
        if (ImGui::Button("<##terrain_undo", ImVec2(undoW, 26))) {
            if (terrain->undo()) {
                statusMessage = "Undo terrain action.";
                markSceneDirty();
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Undo (Ctrl+Z)");
        }
        if (!canUndo) ImGui::EndDisabled();

        ImGui::SameLine(0, gap);
        bool canRedo = terrain->canRedo();
        if (!canRedo) ImGui::BeginDisabled();
        if (ImGui::Button(">##terrain_redo", ImVec2(undoW, 26))) {
            if (terrain->redo()) {
                statusMessage = "Redo terrain action.";
                markSceneDirty();
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Redo (Ctrl+Y)");
        }
        if (!canRedo) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // =====================================================================
        // TABBED NAVIGATION WORKSPACE
        // =====================================================================
        if (ImGui::BeginTabBar("TerrainMainTabs", ImGuiTabBarFlags_None)) {

            // -----------------------------------------------------------------
            // TAB 1: SCULPT
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Sculpt")) {
                terrain->toolMode = Engine::TerrainToolMode::Sculpt;

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.95f, 1.0f), "Elevation Sculpting");
                ImGui::TextDisabled("LMB: Deform  |  Shift+LMB: Invert Brush  |  Ctrl+Z: Undo");
                ImGui::Spacing();

                // Brush Mode Toolbar
                struct BrushInfo {
                    Engine::TerrainBrushMode mode;
                    const char* label;
                    const char* tooltip;
                };
                const BrushInfo brushList[] = {
                    { Engine::TerrainBrushMode::Raise,   "Raise",   "Pulls elevation upwards (Shift lowers)" },
                    { Engine::TerrainBrushMode::Lower,   "Lower",   "Pushes elevation downwards (Shift raises)" },
                    { Engine::TerrainBrushMode::Smooth,  "Smooth",  "Smooths out height variations" },
                    { Engine::TerrainBrushMode::Flatten, "Flatten", "Flattens heights toward target height" },
                    { Engine::TerrainBrushMode::Noise,   "Noise",   "Applies natural procedural noise roughness" }
                };

                float sculptBtnW = (ImGui::GetContentRegionAvail().x - 4.0f * 4.0f) / 5.0f;
                for (int i = 0; i < 5; ++i) {
                    if (i > 0) ImGui::SameLine(0, 4.0f);
                    bool isSelected = (terrain->brushMode == brushList[i].mode);
                    if (isSelected) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.48f, 0.72f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.56f, 0.82f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.16f, 0.42f, 0.65f, 1.0f));
                    }

                    if (ImGui::Button(brushList[i].label, ImVec2(sculptBtnW, 26))) {
                        terrain->brushMode = brushList[i].mode;
                        terrain->isSculptingActive = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", brushList[i].tooltip);
                    }

                    if (isSelected) {
                        ImGui::PopStyleColor(3);
                    }
                }

                ImGui::Spacing();

                // Brush Settings
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.40f, 0.80f, 1.0f, 1.0f), "Brush Parameters:");
                ImGui::SliderFloat("Radius", &terrain->brushRadius, 0.5f, 100.0f, "%.1f m");
                ImGui::SliderFloat("Strength", &terrain->brushStrength, 0.5f, 50.0f, "%.1f");

                const char* falloffs[] = { "Smooth (Hermite)", "Linear", "Spherical", "Flat" };
                int currentFalloff = static_cast<int>(terrain->brushFalloff);
                if (ImGui::Combo("Falloff", &currentFalloff, falloffs, IM_ARRAYSIZE(falloffs))) {
                    terrain->brushFalloff = static_cast<Engine::TerrainBrushFalloff>(currentFalloff);
                }

                if (terrain->brushMode == Engine::TerrainBrushMode::Flatten) {
                    ImGui::Spacing();
                    float fltW = ImGui::GetContentRegionAvail().x - 52.0f;
                    ImGui::SetNextItemWidth(fltW);
                    ImGui::DragFloat("Target Y", &terrain->flattenTargetHeight, 0.2f, -100.0f, 500.0f, "%.2f m");
                    ImGui::SameLine(0, 4.0f);
                    if (ImGui::Button("0m##flat0", ImVec2(48.0f, 0))) {
                        terrain->flattenTargetHeight = 0.0f;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set target height to 0.0m");
                }
                ImGui::Spacing();
                ImGui::Separator();

                ImGui::Spacing();

                // Procedural Mountain Generator (FBM)
                if (ImGui::CollapsingHeader("Procedural Mountain Generator (FBM)")) {
                    ImGui::DragInt("Noise Seed", &terrain->noiseSeed, 1.0f, 0, 999999);
                    ImGui::SliderFloat("Frequency", &terrain->noiseFrequency, 0.001f, 0.05f, "%.4f");
                    ImGui::SliderInt("Octaves", &terrain->noiseOctaves, 1, 8);
                    ImGui::SliderFloat("Persistence", &terrain->noisePersistence, 0.1f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Lacunarity", &terrain->noiseLacunarity, 1.0f, 4.0f, "%.2f");

                    ImGui::Spacing();
                    if (ImGui::Button("Generate Mountain Landscape", ImVec2(-1, 28))) {
                        terrain->generateFBM();
                        terrain->isDirty = true;
                        markSceneDirty();
                        statusMessage = "Generated procedural mountain terrain via FBM noise.";
                    }
                }

                // Quick Flatten / Reset
                if (ImGui::CollapsingHeader("Reset Heightmap")) {
                    ImGui::TextDisabled("Resets all vertex elevation data across the entire terrain mesh.");
                    if (ImGui::Button("Flatten All to 0.0m", ImVec2(-1, 24))) {
                        terrain->flattenAll(0.0f);
                        terrain->isDirty = true;
                        markSceneDirty();
                        statusMessage = "Flattened terrain heights to 0.";
                    }
                }

                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // TAB 2: PAINT TEXTURES (SPLATMAP)
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Paint")) {
                terrain->toolMode = Engine::TerrainToolMode::PaintTexture;

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.45f, 0.88f, 0.65f, 1.0f), "Splatmap Texture Painting");
                ImGui::TextDisabled("LMB: Paint layer weights onto terrain  |  Normalized over 4 layers");
                ImGui::Spacing();

                // Brush settings
                ImGui::SliderFloat("Paint Radius", &terrain->paintBrushRadius, 0.5f, 100.0f, "%.1f m");
                ImGui::SliderFloat("Opacity", &terrain->paintBrushOpacity, 0.1f, 10.0f, "%.2f");

                const char* falloffs[] = { "Smooth (Hermite)", "Linear", "Spherical", "Flat" };
                int currentFalloff = static_cast<int>(terrain->paintBrushFalloff);
                if (ImGui::Combo("Paint Falloff", &currentFalloff, falloffs, IM_ARRAYSIZE(falloffs))) {
                    terrain->paintBrushFalloff = static_cast<Engine::TerrainBrushFalloff>(currentFalloff);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // 4-Layer Palette Swatches
                ImGui::TextUnformatted("Texture Palette (4 Layers):");
                float layerTileW = (ImGui::GetContentRegionAvail().x - 3.0f * 6.0f) * 0.25f;
                for (int i = 0; i < 4; ++i) {
                    if (i > 0) ImGui::SameLine(0, 6.0f);
                    bool isSelected = (terrain->activeLayerIndex == i);
                    ImGui::PushID(i);

                    if (isSelected) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.52f, 0.32f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.62f, 0.38f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.50f, 0.95f, 0.60f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.20f, 0.23f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.28f, 0.32f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.30f, 0.32f, 0.36f, 1.0f));
                    }

                    std::string label = "[" + std::to_string(i) + "]\n" + terrain->layers[i].name;
                    if (ImGui::Button(label.c_str(), ImVec2(layerTileW, 42))) {
                        terrain->activeLayerIndex = i;
                        terrain->isSculptingActive = true;
                    }

                    ImGui::PopStyleColor(3);
                    ImGui::PopID();
                }

                // Active Layer Inspector Card
                int activeIdx = std::clamp(terrain->activeLayerIndex, 0, 3);
                auto& activeLayer = terrain->layers[activeIdx];

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.60f, 1.0f), "Layer [%d] - %s Settings:", activeIdx, activeLayer.name.c_str());
                ImGui::Spacing();

                char nameBuf[64];
                strncpy_s(nameBuf, activeLayer.name.c_str(), sizeof(nameBuf) - 1);
                if (ImGui::InputText("Layer Name", nameBuf, sizeof(nameBuf))) {
                    activeLayer.name = nameBuf;
                }

                if (ImGui::ColorEdit4("Tint Color", &activeLayer.tintColor.x)) {
                    terrain->layersDirty = true;
                }

                // Albedo Texture
                char albedoBuf[512];
                strncpy_s(albedoBuf, activeLayer.albedoPath.c_str(), sizeof(albedoBuf) - 1);
                float txtW = ImGui::GetContentRegionAvail().x - 30.0f;
                ImGui::SetNextItemWidth(txtW);
                if (ImGui::InputText("Albedo", albedoBuf, sizeof(albedoBuf))) {
                    activeLayer.albedoPath = albedoBuf;
                    terrain->layersDirty = true;
                }
                if (ImGui::BeginDragDropTarget()) {
                    std::string dropped = acceptDroppedAssetPath();
                    if (!dropped.empty()) {
                        std::replace(dropped.begin(), dropped.end(), '\\', '/');
                        activeLayer.albedoPath = dropped;
                        terrain->layersDirty = true;
                        statusMessage = "Assigned layer albedo: " + dropped;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine(0, 4.0f);
                if (ImGui::Button("X##clralb", ImVec2(24, 0))) {
                    activeLayer.albedoPath.clear();
                    terrain->layersDirty = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear albedo texture");

                // Normal Map
                char normalBuf[512];
                strncpy_s(normalBuf, activeLayer.normalPath.c_str(), sizeof(normalBuf) - 1);
                ImGui::SetNextItemWidth(txtW);
                if (ImGui::InputText("Normal", normalBuf, sizeof(normalBuf))) {
                    activeLayer.normalPath = normalBuf;
                    terrain->layersDirty = true;
                }
                if (ImGui::BeginDragDropTarget()) {
                    std::string dropped = acceptDroppedAssetPath();
                    if (!dropped.empty()) {
                        std::replace(dropped.begin(), dropped.end(), '\\', '/');
                        activeLayer.normalPath = dropped;
                        terrain->layersDirty = true;
                        statusMessage = "Assigned layer normal map: " + dropped;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine(0, 4.0f);
                if (ImGui::Button("X##clrnorm", ImVec2(24, 0))) {
                    activeLayer.normalPath.clear();
                    terrain->layersDirty = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear normal map");

                // Material PBR Properties
                if (ImGui::SliderFloat("Tiling UV Scale", &activeLayer.uvScale, 0.001f, 0.5f, "%.4f")) {
                    terrain->layersDirty = true;
                }
                if (ImGui::SliderFloat("Roughness", &activeLayer.roughness, 0.0f, 1.0f, "%.2f")) {
                    terrain->layersDirty = true;
                }
                if (ImGui::SliderFloat("Metallic", &activeLayer.metallic, 0.0f, 1.0f, "%.2f")) {
                    terrain->layersDirty = true;
                }
                ImGui::Spacing();
                ImGui::Separator();

                // Layer Fill & Reset Actions
                ImGui::Spacing();
                float halfW = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
                if (ImGui::Button("Fill Active Layer", ImVec2(halfW, 26))) {
                    terrain->beginStroke(Engine::TerrainComponent::TerrainUndoType::Splatmap, "Fill Layer");
                    terrain->fillSplatmapWithLayer(terrain->activeLayerIndex);
                    terrain->commitStroke();
                    statusMessage = "Filled terrain with layer: " + activeLayer.name;
                    markSceneDirty();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets splatmap weights to 100%% for current selected layer");

                ImGui::SameLine(0, 6.0f);
                if (ImGui::Button("Reset to Layer 0", ImVec2(halfW, 26))) {
                    terrain->beginStroke(Engine::TerrainComponent::TerrainUndoType::Splatmap, "Reset Splatmap");
                    terrain->resetSplatmap();
                    terrain->commitStroke();
                    statusMessage = "Reset terrain splatmap to Layer 0.";
                    markSceneDirty();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resets entire splatmap back to Layer 0 base");

                ImGui::Spacing();

                // Procedural Biome Auto-Texturing Card
                if (ImGui::CollapsingHeader("Procedural Biome Auto-Texturing")) {
                    static float s_beachHeight = 5.0f;
                    static float s_beachTransition = 3.0f;
                    static float s_dirtSlope = 18.0f;
                    static float s_cliffSlope = 35.0f;
                    static float s_snowHeight = 220.0f;
                    static float s_snowTransition = 30.0f;
                    static bool s_useSnow = true;

                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Elevation & Slope Rules:");
                    ImGui::SliderFloat("Beach Height", &s_beachHeight, -50.0f, 100.0f, "%.1f m");
                    ImGui::SliderFloat("Beach Blend Width", &s_beachTransition, 0.5f, 20.0f, "%.1f m");
                    ImGui::SliderFloat("Dirt Slope Angle", &s_dirtSlope, 5.0f, 45.0f, "%.1f deg");
                    ImGui::SliderFloat("Cliff Rock Angle", &s_cliffSlope, 15.0f, 75.0f, "%.1f deg");
                    ImGui::Checkbox("Snow on High Peaks", &s_useSnow);
                    if (s_useSnow) {
                        ImGui::SliderFloat("Snow Altitude", &s_snowHeight, 50.0f, 600.0f, "%.1f m");
                        ImGui::SliderFloat("Snow Blend Width", &s_snowTransition, 5.0f, 50.0f, "%.1f m");
                    }

                    ImGui::Spacing();
                    if (ImGui::Button("Generate Procedural Biome Splatmap", ImVec2(-1, 28))) {
                        terrain->autoTextureProcedural(
                            s_beachHeight, s_beachTransition,
                            s_dirtSlope, s_cliffSlope,
                            s_snowHeight, s_snowTransition,
                            0, 1, 2, 3, s_useSnow
                        );
                        statusMessage = "Procedurally textured terrain based on elevation and slope!";
                        markSceneDirty();
                    }
                }

                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // TAB 3: FOLIAGE & GRASS
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Foliage")) {
                terrain->toolMode = Engine::TerrainToolMode::PaintFoliage;
                terrain->initDefaultPalette();

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.55f, 1.0f), "Hybrid Foliage & Detail Palette");
                ImGui::TextDisabled("LMB: Paint Active Slots  |  Shift+LMB: Erase  |  Ctrl+Z: Undo");
                ImGui::Spacing();

                if (ImGui::Checkbox("Enable GPU Foliage", &terrain->foliageEnabled)) {
                    terrain->foliageNeedsRebuild = true;
                    markSceneDirty();
                }

                if (terrain->foliageEnabled) {
                    uint32_t totalInstances = 0;
                    for (const auto& c : terrain->chunks) {
                        totalInstances += c.foliageInstanceCount;
                    }
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Active Clumps: %u instances across %zu chunks", totalInstances, terrain->chunks.size());

                    std::string bL0 = terrain->layers[0].name.empty() ? "Layer 0 (Grass)" : terrain->layers[0].name;
                    std::string bL1 = terrain->layers[1].name.empty() ? "Layer 1 (Dirt)" : terrain->layers[1].name;
                    std::string bL2 = terrain->layers[2].name.empty() ? "Layer 2 (Rock)" : terrain->layers[2].name;
                    std::string bL3 = terrain->layers[3].name.empty() ? "Layer 3 (Sand)" : terrain->layers[3].name;
                    const char* allLayerOptions[5] = {
                        "All Layers (Everywhere)",
                        bL0.c_str(),
                        bL1.c_str(),
                        bL2.c_str(),
                        bL3.c_str()
                    };

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.40f, 0.80f, 1.0f, 1.0f), "Interactive Brush Controls:");
                    ImGui::Spacing();

                    bool isErasing = terrain->foliageBrushErase;
                    float fBtnW = (ImGui::GetContentRegionAvail().x - 4.0f) / 2.0f;
                    if (!isErasing) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.52f, 0.28f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.62f, 0.35f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.44f, 0.22f, 1.0f));
                    }
                    if (ImGui::Button("Paint Details", ImVec2(fBtnW, 26))) {
                        terrain->foliageBrushErase = false;
                        terrain->isSculptingActive = true;
                    }
                    if (!isErasing) ImGui::PopStyleColor(3);

                    ImGui::SameLine(0, 4.0f);
                    if (isErasing) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.68f, 0.22f, 0.22f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.28f, 0.28f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.16f, 0.16f, 1.0f));
                    }
                    if (ImGui::Button("Erase Details", ImVec2(fBtnW, 26))) {
                        terrain->foliageBrushErase = true;
                        terrain->isSculptingActive = true;
                    }
                    if (isErasing) ImGui::PopStyleColor(3);

                    ImGui::Spacing();
                    ImGui::SliderFloat("Brush Radius##foliage", &terrain->foliageBrushRadius, 0.5f, 50.0f, "%.1f m");
                    ImGui::SliderFloat("Brush Flow Rate##foliage", &terrain->foliageBrushDensity, 1.0f, 50.0f, "%.1f");

                    int curBrushFilter = terrain->foliageBrushLayerFilter + 1;
                    if (ImGui::Combo("Brush Ground Filter##foliage", &curBrushFilter, allLayerOptions, 5)) {
                        terrain->foliageBrushLayerFilter = curBrushFilter - 1;
                    }

                    float clrW = (ImGui::GetContentRegionAvail().x - 4.0f) / 2.0f;
                    if (ImGui::Button("Clear Grass Clumps", ImVec2(clrW, 24))) {
                        terrain->clearAllFoliage();
                        markSceneDirty();
                        statusMessage = "Cleared all instanced grass clumps.";
                    }
                    ImGui::SameLine(0, 4.0f);
                    if (ImGui::Button("Clear Spawned Props", ImVec2(clrW, 24))) {
                        if (Scene* currentScene = sceneManager.getCurrentScene()) {
                            Entity folFolder{};
                            for (auto [ent, name] : registry.view<Name>()) {
                                if (name.value == "Foliage_Props") { folFolder = ent; break; }
                            }
                            if (registry.isValid(folFolder)) {
                                currentScene->deleteEntity(folFolder);
                                markSceneDirty();
                                statusMessage = "Cleared all spawned foliage props.";
                            }
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "Detail Palette Slots (%zu):", terrain->detailPalette.size());
                    ImGui::Spacing();

                    float addW = (ImGui::GetContentRegionAvail().x - 4.0f) / 2.0f;
                    if (ImGui::Button("+ Add Grass Clump", ImVec2(addW, 24))) {
                        Engine::DetailPrototype grass{};
                        grass.name = "Grass Clump " + std::to_string(terrain->detailPalette.size());
                        grass.type = Engine::DetailType::GrassClump;
                        grass.density = 5.0f;
                        grass.scaleMin = 0.8f;
                        grass.scaleMax = 1.3f;
                        terrain->detailPalette.push_back(grass);
                        markSceneDirty();
                    }
                    ImGui::SameLine(0, 4.0f);
                    if (ImGui::Button("+ Add Prop (Tree/Rock)", ImVec2(addW, 24))) {
                        Engine::DetailPrototype prop{};
                        prop.name = "Prop " + std::to_string(terrain->detailPalette.size());
                        prop.type = Engine::DetailType::PropEntity;
                        prop.density = 0.5f;
                        prop.scaleMin = 0.8f;
                        prop.scaleMax = 1.5f;
                        prop.collisionShape = Engine::DetailCollisionShape::Capsule;
                        terrain->detailPalette.push_back(prop);
                        markSceneDirty();
                    }

                    ImGui::Spacing();

                    int slotToDelete = -1;
                    for (size_t i = 0; i < terrain->detailPalette.size(); ++i) {
                        auto& item = terrain->detailPalette[i];
                        ImGui::PushID(static_cast<int>(i));

                        bool isProp = (item.type == Engine::DetailType::PropEntity);
                        const char* typeBadge = isProp ? "[Prop / Entity]" : "[Instanced Grass]";

                        ImGui::Checkbox("##enabled", &item.enabled);
                        ImGui::SameLine();

                        std::string headerLabel = item.name + " " + typeBadge + "###slot_hdr";
                        bool nodeOpen = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

                        if (nodeOpen) {
                            ImGui::Indent(14.0f);

                            // Slot Name
                            char nameBuf[64];
                            std::strncpy(nameBuf, item.name.c_str(), sizeof(nameBuf) - 1);
                            nameBuf[sizeof(nameBuf) - 1] = '\0';
                            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                                item.name = nameBuf;
                                markSceneDirty();
                            }

                            // Type selection
                            const char* typeNames[] = { "Dense Grass Clump (Instanced)", "Prop / Entity (Tree, Rock, Prefab)" };
                            int curType = static_cast<int>(item.type);
                            if (ImGui::Combo("Type", &curType, typeNames, 2)) {
                                item.type = static_cast<Engine::DetailType>(curType);
                                markSceneDirty();
                            }

                            // Mesh Path (Models / Clumps)
                            char meshBuf[256];
                            std::strncpy(meshBuf, item.meshPath.c_str(), sizeof(meshBuf) - 1);
                            meshBuf[sizeof(meshBuf) - 1] = '\0';
                            if (ImGui::InputText("3D Mesh Path", meshBuf, sizeof(meshBuf))) {
                                item.meshPath = meshBuf;
                                if (item.type == Engine::DetailType::GrassClump) {
                                    terrain->foliageMeshPath = item.meshPath;
                                    terrain->foliageNeedsRebuild = true;
                                    terrain->foliageVertexBuffer = VK_NULL_HANDLE;
                                    terrain->foliageIndexBuffer = VK_NULL_HANDLE;
                                }
                                markSceneDirty();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Custom .obj, .gltf, or .glb 3D model asset path.\n(For grass clumps: leave empty for built-in procedural grass clumps)");
                            }

                            // Texture Path
                            char texBuf[256];
                            std::strncpy(texBuf, item.texturePath.c_str(), sizeof(texBuf) - 1);
                            texBuf[sizeof(texBuf) - 1] = '\0';
                            if (ImGui::InputText("Texture Path", texBuf, sizeof(texBuf))) {
                                item.texturePath = texBuf;
                                if (item.type == Engine::DetailType::GrassClump) {
                                    terrain->foliageTexturePath = item.texturePath;
                                    terrain->foliageNeedsRebuild = true;
                                    terrain->foliageTextureView = VK_NULL_HANDLE;
                                    terrain->foliageDescriptorSet = VK_NULL_HANDLE;
                                }
                                markSceneDirty();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Custom .png, .dds, or .tga texture path.\n(For grass clumps: leave empty for built-in 5-blade grass texture)");
                            }

                            if (isProp) {
                                char prefabBuf[256];
                                std::strncpy(prefabBuf, item.prefabPath.c_str(), sizeof(prefabBuf) - 1);
                                prefabBuf[sizeof(prefabBuf) - 1] = '\0';
                                if (ImGui::InputText("Prefab Path (Optional)", prefabBuf, sizeof(prefabBuf))) {
                                    item.prefabPath = prefabBuf;
                                    markSceneDirty();
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Optional .prefab file to instantiate instead of raw mesh");
                                }
                            }

                            // Placement & Transform Parameters
                            ImGui::SliderFloat("Relative Density", &item.density, 0.05f, 25.0f, "%.2f");
                            ImGui::SliderFloat("Scale Min", &item.scaleMin, 0.1f, 5.0f, "%.2f");
                            ImGui::SliderFloat("Scale Max", &item.scaleMax, 0.1f, 10.0f, "%.2f");
                            ImGui::Checkbox("Uniform Scale", &item.uniformScale);
                            ImGui::SameLine(0, 16.0f);
                            ImGui::Checkbox("Random Yaw (360)", &item.randomYaw);

                            ImGui::Checkbox("Align to Slope Normal", &item.alignToNormal);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Enable for rocks and moss to conform to slope angles.\nDisable for trees, poles, and fences to grow straight upright (Y-up).");
                            }

                            ImGui::SliderFloat("Max Slope Cutoff", &item.maxSlope, 0.05f, 1.0f, "%.2f");
                            ImGui::SliderFloat("Sink Offset Y", &item.sinkOffset, -3.0f, 3.0f, "%.2f m");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Buries roots or rocks into ground to prevent floating on uneven terrain.");
                            }
                            int curTargetLayer = item.targetLayer + 1;
                            if (ImGui::Combo("Target Ground Layer", &curTargetLayer, allLayerOptions, 5)) {
                                item.targetLayer = curTargetLayer - 1;
                                markSceneDirty();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Filter placement to a specific splatmap texture layer, or allow across all surfaces.");
                            }

                            if (ImGui::ColorEdit4("Tint Color", &item.tint.x)) {
                                markSceneDirty();
                            }

                            // Collision for PropEntity
                            if (isProp) {
                                ImGui::Separator();
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Prop Collision & Physics:");
                                const char* colShapes[] = { "None", "Box (OBB)", "Sphere", "Capsule" };
                                int cShape = static_cast<int>(item.collisionShape);
                                if (ImGui::Combo("Collider Shape", &cShape, colShapes, 4)) {
                                    item.collisionShape = static_cast<Engine::DetailCollisionShape>(cShape);
                                    markSceneDirty();
                                }
                                if (item.collisionShape != Engine::DetailCollisionShape::None) {
                                    ImGui::DragFloat3("Collider Extents", &item.colliderExtents.x, 0.05f, 0.1f, 50.0f, "%.2f");
                                    ImGui::Checkbox("Static Rigidbody", &item.isStatic);
                                }
                            }

                            ImGui::Spacing();
                            if (ImGui::Button("Remove Slot", ImVec2(110.0f, 22.0f))) {
                                slotToDelete = static_cast<int>(i);
                            }

                            ImGui::Unindent(14.0f);
                        }
                        ImGui::PopID();
                        ImGui::Spacing();
                    }

                    if (slotToDelete >= 0 && slotToDelete < static_cast<int>(terrain->detailPalette.size())) {
                        terrain->detailPalette.erase(terrain->detailPalette.begin() + slotToDelete);
                        markSceneDirty();
                    }

                    ImGui::Spacing();

                    // Placement rules
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Scattering Rules (Procedural):");

                    int curScatterLayer = terrain->foliageGrassLayer + 1;
                    if (ImGui::Combo("Target Layer", &curScatterLayer, allLayerOptions, 5)) {
                        terrain->foliageGrassLayer = curScatterLayer - 1;
                        terrain->foliageNeedsRebuild = true;
                        markSceneDirty();
                    }

                    if (ImGui::SliderFloat("Density Multiplier", &terrain->foliageDensity, 0.0f, 25.0f, "%.1f")) {
                        terrain->foliageNeedsRebuild = true;
                        markSceneDirty();
                    }
                    if (ImGui::SliderFloat("Min Layer Weight", &terrain->foliageMinWeight, 0.05f, 0.95f, "%.2f")) {
                        terrain->foliageNeedsRebuild = true;
                        markSceneDirty();
                    }
                    if (ImGui::SliderFloat("Max Slope (Cliff cutoff)", &terrain->foliageMaxSlope, 0.1f, 1.0f, "%.2f")) {
                        terrain->foliageNeedsRebuild = true;
                        markSceneDirty();
                    }

                    ImGui::Spacing();

                    // Appearance & Wind
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Appearance & Dynamics:");

                    if (ImGui::SliderFloat("Scale Min", &terrain->foliageScaleMin, 0.2f, 2.0f, "%.2f")) {
                        terrain->foliageNeedsRebuild = true;
                        markSceneDirty();
                    }
                    if (ImGui::SliderFloat("Scale Max", &terrain->foliageScaleMax, 0.5f, 3.5f, "%.2f")) {
                        terrain->foliageNeedsRebuild = true;
                        markSceneDirty();
                    }
                    ImGui::SliderFloat("Wind Strength", &terrain->foliageWindStrength, 0.0f, 3.0f, "%.2f");
                    ImGui::SliderFloat("Draw Distance", &terrain->foliageMaxDistance, 30.0f, 500.0f, "%.0f m");

                    if (ImGui::ColorEdit4("Grass Tint", &terrain->foliageTint.x)) {
                        markSceneDirty();
                    }
                    ImGui::Spacing();
                    ImGui::Separator();

                    ImGui::Spacing();
                    if (ImGui::Button("Rebuild Foliage Instances", ImVec2(-1, 26))) {
                        terrain->foliageNeedsRebuild = true;
                        markSceneDirty();
                        statusMessage = "Rebuilding terrain foliage instances...";
                    }
                }

                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // TAB 4: GRID & SETUP (DIMENSIONS & RESOLUTION)
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Grid & Setup")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.45f, 0.75f, 0.95f, 1.0f), "World Dimensions & Resolution");
                ImGui::Spacing();

                // Dimensions
                bool dimsChanged = false;
                if (ImGui::DragFloat("Size X (Width)", &terrain->sizeX, 1.0f, 1.0f, 5000.0f, "%.1f m")) {
                    dimsChanged = true;
                }
                if (ImGui::DragFloat("Size Z (Depth)", &terrain->sizeZ, 1.0f, 1.0f, 5000.0f, "%.1f m")) {
                    dimsChanged = true;
                }
                if (ImGui::DragFloat("Height Scale", &terrain->heightScale, 0.5f, 1.0f, 1000.0f, "%.1f m")) {
                    dimsChanged = true;
                }
                if (ImGui::DragFloat("Global UV Tiling", &terrain->uvScale, 0.2f, 0.1f, 100.0f, "%.1f")) {
                    dimsChanged = true;
                }

                if (dimsChanged) {
                    terrain->markFullDirty();
                    markSceneDirty();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Chunk Architecture Card
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.75f, 1.0f), "Integrated Chunk Architecture");

                if (ImGui::Checkbox("Show Chunk Wireframes in Viewport", &terrain->showChunkBorders)) {
                    markSceneDirty();
                }
                if (ImGui::Checkbox("Auto-Scale World Dimensions with Chunks", &terrain->autoScaleWithChunks)) {
                    markSceneDirty();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, adding chunks expands the terrain dimensions while preserving chunk size and sculpts.");
                }

                int cCountX = static_cast<int>(terrain->chunkCountX);
                int cCountZ = static_cast<int>(terrain->chunkCountZ);
                int cRes = static_cast<int>(terrain->chunkResolution);
                bool gridChanged = false;

                // Chunks X Stepper
                ImGui::TextUnformatted("Chunks X (Columns):");
                ImGui::SameLine();
                if (ImGui::Button("-##sub_cx") && cCountX > 1) {
                    cCountX--;
                    gridChanged = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::DragInt("##chunk_x_input", &cCountX, 0.1f, 1, 64)) {
                    gridChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("+##add_cx") && cCountX < 64) {
                    cCountX++;
                    gridChanged = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%d cols)", cCountX);

                // Chunks Z Stepper
                ImGui::TextUnformatted("Chunks Z (Rows):   ");
                ImGui::SameLine();
                if (ImGui::Button("-##sub_cz") && cCountZ > 1) {
                    cCountZ--;
                    gridChanged = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::DragInt("##chunk_z_input", &cCountZ, 0.1f, 1, 64)) {
                    gridChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("+##add_cz") && cCountZ < 64) {
                    cCountZ++;
                    gridChanged = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%d rows)", cCountZ);

                // Patch resolution
                const char* chunkResLabels[] = { "33 x 33 (32 quads / Fine)", "65 x 65 (64 quads / Standard)", "129 x 129 (128 quads / Large)" };
                int currentResIdx = 1;
                if (terrain->chunkResolution <= 33) currentResIdx = 0;
                else if (terrain->chunkResolution <= 65) currentResIdx = 1;
                else currentResIdx = 2;

                if (ImGui::Combo("Patch Size", &currentResIdx, chunkResLabels, IM_ARRAYSIZE(chunkResLabels))) {
                    if (currentResIdx == 0) cRes = 33;
                    else if (currentResIdx == 1) cRes = 65;
                    else cRes = 129;
                    gridChanged = true;
                }

                if (gridChanged) {
                    cCountX = std::clamp(cCountX, 1, 64);
                    cCountZ = std::clamp(cCountZ, 1, 64);
                    float deltaWorldX = 0.0f;
                    float deltaWorldZ = 0.0f;
                    terrain->resizeGrid(static_cast<uint32_t>(cCountX), static_cast<uint32_t>(cCountZ), static_cast<uint32_t>(cRes), terrain->autoScaleWithChunks, &deltaWorldX, &deltaWorldZ);
                    if (terrain->autoScaleWithChunks) {
                        if (auto* tr = registry.get<Transform>(selectedEntity)) {
                            tr->position.x += deltaWorldX;
                            tr->position.z += deltaWorldZ;
                        }
                    }
                    markSceneDirty();
                }

                float vertexSpacingX = terrain->sizeX / static_cast<float>(std::max(1u, terrain->resolutionX - 1));
                float vertexSpacingZ = terrain->sizeZ / static_cast<float>(std::max(1u, terrain->resolutionZ - 1));

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f),
                    "Grid Resolution: %u x %u (%zu total vertices)",
                    terrain->resolutionX, terrain->resolutionZ, terrain->heights.size());
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.4f, 1.0f),
                    "Chunks: %u x %u (%u tiles) | Spacing: ~%.2fm",
                    terrain->chunkCountX, terrain->chunkCountZ,
                    terrain->chunkCountX * terrain->chunkCountZ,
                    (vertexSpacingX + vertexSpacingZ) * 0.5f);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Scale Presets
                ImGui::TextUnformatted("Scale & Grid Presets:");
                float presetW = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
                if (ImGui::Button("100m Compact (2x2)", ImVec2(presetW, 24))) {
                    terrain->sizeX = 100.0f;
                    terrain->sizeZ = 100.0f;
                    terrain->resizeGrid(2, 2, 65, false);
                    markSceneDirty();
                }
                ImGui::SameLine(0, 6.0f);
                if (ImGui::Button("250m Standard (4x4)", ImVec2(presetW, 24))) {
                    terrain->sizeX = 250.0f;
                    terrain->sizeZ = 250.0f;
                    terrain->resizeGrid(4, 4, 65, false);
                    markSceneDirty();
                }
                if (ImGui::Button("500m Large (8x8)", ImVec2(presetW, 24))) {
                    terrain->sizeX = 500.0f;
                    terrain->sizeZ = 500.0f;
                    terrain->resizeGrid(8, 8, 65, false);
                    markSceneDirty();
                }
                ImGui::SameLine(0, 6.0f);
                if (ImGui::Button("1000m Default (16x16)", ImVec2(presetW, 24))) {
                    terrain->sizeX = 1000.0f;
                    terrain->sizeZ = 1000.0f;
                    terrain->heightScale = 600.0f;
                    terrain->resizeGrid(16, 16, 65, false);
                    markSceneDirty();
                }

                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // TAB 5: WORLD TILES (MULTI-TILE SEAMLESS STREAMING)
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("World Tiles")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.95f, 1.0f), "Multi-Tile World Streaming");
                ImGui::TextDisabled("Generate and stitch adjacent terrain tiles along world axes");
                ImGui::Spacing();

                auto* currentTransform = registry.get<Transform>(selectedEntity);
                if (currentTransform) {
                    glm::vec3 curPos = currentTransform->position;
                    float sX = terrain->sizeX;
                    float sZ = terrain->sizeZ;

                    auto hasNeighborAt = [&](float offsetX, float offsetZ) -> bool {
                        glm::vec3 testPos = curPos + glm::vec3(offsetX, 0.0f, offsetZ);
                        for (auto [otherE, otherT, otherTr] : registry.view<Engine::TerrainComponent, Transform>()) {
                            if (otherE == selectedEntity) continue;
                            if (glm::distance(otherTr.position, testPos) < 1.0f) return true;
                        }
                        return false;
                    };

                    bool hasEast  = hasNeighborAt(sX, 0.0f);
                    bool hasWest  = hasNeighborAt(-sX, 0.0f);
                    bool hasSouth = hasNeighborAt(0.0f, sZ);
                    bool hasNorth = hasNeighborAt(0.0f, -sZ);

                    auto spawnNeighbor = [&](float offsetX, float offsetZ) {
                        Scene* currentScene = sceneManager.getCurrentScene();
                        if (!currentScene) return;

                        glm::vec3 newPos = curPos + glm::vec3(offsetX, 0.0f, offsetZ);
                        Entity newEnt = registry.create();
                        currentScene->trackEntity(newEnt);

                        int gridX = static_cast<int>(std::round(newPos.x / (sX > 0.0f ? sX : 1.0f)));
                        int gridZ = static_cast<int>(std::round(newPos.z / (sZ > 0.0f ? sZ : 1.0f)));
                        std::string baseName = "Terrain_" + std::to_string(gridX) + "_" + std::to_string(gridZ);
                        registry.emplace<Name>(newEnt, Name{ currentScene->makeUniqueEntityName(baseName) });
                        registry.emplace<Transform>(newEnt, Transform{ newPos, currentTransform->rotation, currentTransform->scale });

                        auto* curMat = registry.get<Material>(selectedEntity);
                        Material newMat = curMat ? *curMat : Material{ glm::vec4(1.0f) };
                        registry.emplace<Material>(newEnt, std::move(newMat));

                        Engine::TerrainComponent newTerrain{};
                        newTerrain.sizeX = terrain->sizeX;
                        newTerrain.sizeZ = terrain->sizeZ;
                        newTerrain.chunkCountX = terrain->chunkCountX;
                        newTerrain.chunkCountZ = terrain->chunkCountZ;
                        newTerrain.chunkResolution = terrain->chunkResolution;
                        newTerrain.heightScale = terrain->heightScale;
                        newTerrain.uvScale = terrain->uvScale;
                        newTerrain.showChunkBorders = terrain->showChunkBorders;
                        newTerrain.autoScaleWithChunks = terrain->autoScaleWithChunks;
                        newTerrain.brushRadius = terrain->brushRadius;
                        newTerrain.brushStrength = terrain->brushStrength;
                        newTerrain.brushMode = terrain->brushMode;
                        newTerrain.brushFalloff = terrain->brushFalloff;
                        newTerrain.ensureAllocated();

                        registry.emplace<Engine::TerrainComponent>(newEnt, std::move(newTerrain));

                        // Synchronize border vertices with all adjacent neighbors
                        Engine::TerrainSystem::synchronizeNeighborBorders(registry, newEnt, true);

                        selectedEntity = newEnt;
                        hasSelection = true;
                        if (auto* n = registry.get<Name>(newEnt)) renameBuffer = n->value;
                        statusMessage = "Created adjacent terrain tile: " + renameBuffer;
                        markSceneDirty();
                    };

                    float availWidth = ImGui::GetContentRegionAvail().x;
                    float btnW = (availWidth - 8.0f) * 0.5f;

                    // North (-Z)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnW * 0.5f + 4.0f);
                    if (hasNorth) {
                        ImGui::BeginDisabled();
                        ImGui::Button("North (-Z) [Exists]##add_nz", ImVec2(btnW, 26));
                        ImGui::EndDisabled();
                    } else {
                        if (ImGui::Button("+ North (-Z)##add_nz", ImVec2(btnW, 26))) {
                            spawnNeighbor(0.0f, -sZ);
                        }
                    }

                    // West (-X) and East (+X)
                    if (hasWest) {
                        ImGui::BeginDisabled();
                        ImGui::Button("West (-X) [Exists]##add_wx", ImVec2(btnW, 26));
                        ImGui::EndDisabled();
                    } else {
                        if (ImGui::Button("+ West (-X)##add_wx", ImVec2(btnW, 26))) {
                            spawnNeighbor(-sX, 0.0f);
                        }
                    }
                    ImGui::SameLine(0, 8.0f);
                    if (hasEast) {
                        ImGui::BeginDisabled();
                        ImGui::Button("East (+X) [Exists]##add_ex", ImVec2(btnW, 26));
                        ImGui::EndDisabled();
                    } else {
                        if (ImGui::Button("+ East (+X)##add_ex", ImVec2(btnW, 26))) {
                            spawnNeighbor(sX, 0.0f);
                        }
                    }

                    // South (+Z)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnW * 0.5f + 4.0f);
                    if (hasSouth) {
                        ImGui::BeginDisabled();
                        ImGui::Button("South (+Z) [Exists]##add_sz", ImVec2(btnW, 26));
                        ImGui::EndDisabled();
                    } else {
                        if (ImGui::Button("+ South (+Z)##add_sz", ImVec2(btnW, 26))) {
                            spawnNeighbor(0.0f, sZ);
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("Synchronize All Adjacent Borders", ImVec2(-1, 26))) {
                        Engine::TerrainSystem::synchronizeNeighborBorders(registry, selectedEntity, false);
                        statusMessage = "Synchronized all adjacent terrain borders.";
                        markSceneDirty();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Averages border heights between this tile and all touching neighbors to eliminate seams.");
                    }
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

    } else {
        ImGui::PopStyleColor(3);
    }

    if (!visible) {
        registry.remove<Engine::TerrainComponent>(selectedEntity);
        statusMessage = "Removed Terrain component.";
        markSceneDirty();
    }
}



