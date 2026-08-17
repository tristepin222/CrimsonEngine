#include "editor/EditorUI.hpp"
#include "editor/EditorUIInternal.hpp"
#include "meta/ComponentReflection.hpp"
#include "scenes/JSONUtils.hpp"
#include "ufbx.h"
#include "cgltf.h"

#include <limits>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <fstream>
#include <cstdlib>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "ecs/Registry.hpp"
#include "ecs/components/Material.hpp"
#include "ecs/components/Mesh.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/Transform.hpp"
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
#include "scenes/TilesetAsset.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/LightComponent.hpp"
#include "editor/AssetBrowserRegistry.hpp"

#include <functional>
#include "renderer/VulkanRenderer.hpp"
#include "scenes/Scene.hpp"
#include "scenes/SceneManager.hpp"
#include "scenes/SceneSerializer.hpp"
#include "renderer/ResourceManager.hpp"
#include <filesystem>
#include "ImGuizmo.h"

using namespace ImGui;
using namespace std;

bool s_openImportSettingsWindow = false;
bool s_triggerLoadImportSettings = false;
ImportSettingsMetadata s_importMetadata;
std::filesystem::path s_importSettingsAssetPath;
bool s_openTilesetEditorWindow = false;
bool s_openAnimationEditorWindow = false;
bool s_openNodeGraphDemoWindow = false;
bool s_openAnimatorControllerWindow = false;
bool s_openSpriteSlicerWindow = false;
std::filesystem::path s_spriteSlicerAssetPath;
int s_sliceCellWidth = 64;
int s_sliceCellHeight = 64;
std::string s_sliceOutputPrefix = "";
bool s_sliceSkipEmptyTiles = true;

std::string s_editingTilesetPath;
Engine::TilesetAsset s_editingTileset;
bool s_tilesetLoaded = false;
TilemapTool s_tilemapTool = TilemapTool::Pencil;
uint8_t s_brushRotation = 0;
bool s_boxDragging = false;
int s_boxStartCol = 0;
int s_boxStartRow = 0;
int s_boxCurrentCol = 0;
int s_boxCurrentRow = 0;
int s_brushTileId = -1;
bool s_brushModeActive = false;
Entity s_brushTilemapEntity;
int s_tilemapActiveLayer = 0;
ImVec2 s_tsPanOffset{ 0.f, 0.f };
float s_tsCellSize = 64.f;
bool s_tsIsPanning = false;
ImVec2 s_tsPanStart{};
ImVec2 s_tsPanOffsetStart{};

void loadImportSettingsMetadata(const std::filesystem::path& path) {
    s_importMetadata = ImportSettingsMetadata{};
    s_importMetadata.assetPath = path.generic_string();
    
    std::string ext = path.extension().string();
    bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga");
    if (isTexture) {
        std::filesystem::path metaPath = path.string() + ".meta";
        if (std::filesystem::exists(metaPath)) {
            try {
                std::ifstream f(metaPath);
                if (f.is_open()) {
                    std::stringstream ss;
                    ss << f.rdbuf();
                    std::string content = ss.str();
                    std::string filterStr = JSONUtils::extractStringValue(content, "filterMode");
                    if (filterStr.empty()) {
                        filterStr = JSONUtils::extractStringValue(content, "filter");
                    }
                    if (filterStr == "Nearest" || filterStr == "Point" || filterStr == "nearest") {
                        s_importMetadata.filterMode = TextureFilterMode::Nearest;
                    } else if (filterStr == "Trilinear" || filterStr == "trilinear") {
                        s_importMetadata.filterMode = TextureFilterMode::Trilinear;
                    } else {
                        s_importMetadata.filterMode = TextureFilterMode::Bilinear;
                    }
                }
            } catch (...) {}
        }
        return; // Skip ufbx model/animation loading for textures
    }

    std::filesystem::path importPath = path.string() + ".import";
    if (std::filesystem::exists(importPath)) {
        try {
            std::ifstream f(importPath);
            if (f.is_open()) {
                std::stringstream ss;
                ss << f.rdbuf();
                std::string content = ss.str();
                float scaleVal = 1.0f;
                if (JSONUtils::extractFloatValue(content, "scale", scaleVal)) {
                    s_importMetadata.scale = scaleVal;
                }
                s_importMetadata.generateNormals = (content.find("\"generateNormals\": true") != std::string::npos || content.find("\"generateNormals\":true") != std::string::npos || content.find("\"generateNormals\": 1") != std::string::npos);
                s_importMetadata.allowMissingPos = (content.find("\"allowMissingPos\": true") != std::string::npos || content.find("\"allowMissingPos\":true") != std::string::npos || content.find("\"allowMissingPos\": 1") != std::string::npos);
                s_importMetadata.forceInPlace = (content.find("\"forceInPlace\": true") != std::string::npos || content.find("\"forceInPlace\":true") != std::string::npos || content.find("\"forceInPlace\": 1") != std::string::npos);
            }
        } catch (...) {}
    }
    
    if (ext == ".fbx" || ext == ".FBX") {
        ufbx_load_opts opts = { 0 };
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(path.string().c_str(), &opts, &error);
        if (scene) {
            for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
                ufbx_anim_stack* stack = scene->anim_stacks.data[i];
                std::string name = stack->name.data ? stack->name.data : "UnnamedAnim";
                double dur = stack->time_end - stack->time_begin;
                s_importMetadata.animations.push_back({ name, dur });
            }
            for (size_t i = 0; i < scene->texture_files.count; ++i) {
                ufbx_texture_file& tf = scene->texture_files.data[i];
                std::string name = std::filesystem::path(tf.filename.data ? tf.filename.data : "").filename().string();
                if (name.empty()) name = "texture_" + std::to_string(i);
                s_importMetadata.textures.push_back({ name, i, tf.content.size > 0, tf.content.size });
            }
            ufbx_free_scene(scene);
        }
    } else if (ext == ".gltf" || ext == ".glb") {
        cgltf_options options = {};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, path.string().c_str(), &data);
        if (result == cgltf_result_success) {
            for (cgltf_size i = 0; i < data->animations_count; ++i) {
                cgltf_animation& anim = data->animations[i];
                std::string name = anim.name ? anim.name : "UnnamedAnim";
                double max_time = 0.0;
                for (cgltf_size j = 0; j < anim.channels_count; ++j) {
                    if (anim.channels[j].sampler && anim.channels[j].sampler->input) {
                        cgltf_accessor* input = anim.channels[j].sampler->input;
                        if (input->count > 0) {
                            cgltf_float outVal = 0.0f;
                            cgltf_accessor_read_float(input, input->count - 1, &outVal, 1);
                            max_time = std::max(max_time, (double)outVal);
                        }
                    }
                }
                s_importMetadata.animations.push_back({ name, max_time });
            }
            cgltf_free(data);
        }
    }
}

void saveImportSettings() {
    std::string ext = std::filesystem::path(s_importMetadata.assetPath).extension().string();
    bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga");
    if (isTexture) {
        std::string relativePath = s_importMetadata.assetPath + ".meta";
        std::string filterStr = "Bilinear";
        if (s_importMetadata.filterMode == TextureFilterMode::Nearest) filterStr = "Nearest";
        else if (s_importMetadata.filterMode == TextureFilterMode::Trilinear) filterStr = "Trilinear";

        auto writeMeta = [&](const std::filesystem::path& destPath) {
            if (!destPath.parent_path().empty()) {
                std::filesystem::create_directories(destPath.parent_path());
            }
            std::ofstream f(destPath);
            if (f.is_open()) {
                f << "{\n"
                  << "  \"filterMode\": \"" << filterStr << "\"\n"
                  << "}\n";
                f.close();
            }
        };

        // Write active copy
        writeMeta(relativePath);
        return;
    }

    std::string relativePath = s_importMetadata.assetPath + ".import";
    try {
        // Write active copy
        std::ofstream f(relativePath);
        if (f.is_open()) {
            f << "{\n"
              << "  \"scale\": " << s_importMetadata.scale << ",\n"
              << "  \"generateNormals\": " << (s_importMetadata.generateNormals ? "true" : "false") << ",\n"
              << "  \"allowMissingPos\": " << (s_importMetadata.allowMissingPos ? "true" : "false") << ",\n"
              << "  \"forceInPlace\": " << (s_importMetadata.forceInPlace ? "true" : "false") << "\n"
              << "}\n";
            f.close();
        }
        
    } catch (...) {}
}

bool writeExtractedFile(const std::string& relativePath, const void* data, size_t size) {
    // 1. Write to active run folder (makes it show up in the editor instantly)
    std::filesystem::path activePath(relativePath);
    if (!activePath.parent_path().empty()) {
        std::filesystem::create_directories(activePath.parent_path());
    }
    std::ofstream activeOut(activePath, std::ios::binary);
    if (!activeOut.is_open()) return false;
    activeOut.write(reinterpret_cast<const char*>(data), size);
    activeOut.close();

    return true;
}

bool entityHasSkin(Registry& registry, Entity entity) {
    if (registry.has<SkeletonComponent>(entity)) return true;
    if (auto* hierarchy = registry.get<HierarchyComponent>(entity)) {
        if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
            return registry.has<SkeletonComponent>(hierarchy->parent);
        }
    }
    return false;
}

EditorUI::EditorUI(Registry& registry, VulkanRenderer& renderer, SceneManager& sceneManager, EditorModeState& editorMode, const std::string& startScenePath, BuildGameCallback buildGameCallback, CompileScriptsCallback compileScriptsCallback)
    : registry(registry), renderer(renderer), sceneManager(sceneManager), editorMode(editorMode), scenePath(startScenePath), buildGameCallback(std::move(buildGameCallback)), compileScriptsCallback(std::move(compileScriptsCallback)) {
}

EditorUI::~EditorUI() {
    shutdown();
}

void EditorUI::initialize(GLFWwindow* window) {
    if (initialized) {
        return;
    }

    this->window = window;

    IMGUI_CHECKVERSION();
    CreateContext();

    // --- Setup Custom Engine Theme (Modern Slate & Cyan Dark Theme) ---
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    
    // Color Palette
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.48f, 0.52f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.11f, 0.12f, 0.15f, 0.60f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.13f, 0.17f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.22f, 0.28f, 0.80f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.25f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.22f, 0.25f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.30f, 0.34f, 0.42f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.38f, 0.42f, 0.52f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.22f, 0.58f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.22f, 0.58f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]        = ImVec4(0.32f, 0.68f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.25f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.15f, 0.42f, 0.72f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.16f, 0.19f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.22f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.25f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.15f, 0.42f, 0.72f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.25f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.15f, 0.42f, 0.72f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.22f, 0.26f, 0.34f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    createDescriptorPool();

    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        throw runtime_error("Failed to initialize ImGui GLFW backend");
    }

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = renderer.device.getInstance();
    initInfo.PhysicalDevice = renderer.device.getPhysicalDevice();
    initInfo.Device = renderer.device.getDevice();
    initInfo.QueueFamily = renderer.device.getGraphicsQueueFamily();
    initInfo.Queue = renderer.device.getGraphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = descriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = 2;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;
    initInfo.PipelineInfoMain.RenderPass = renderer.getRenderPass();
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw runtime_error("Failed to initialize ImGui Vulkan backend");
    }

    // Register Unity-style custom context menu options
    Engine::AssetBrowserRegistry::registerOption("Create/C++ Component", [](const std::filesystem::path& folderPath) {
        std::string filename = "CustomComponent.hpp";
        auto filepath = folderPath / filename;
        std::ofstream out(filepath);
        if (out.is_open()) {
            out << "#pragma once\n#include \"ecs/Registry.hpp\"\n#include \"core/EngineAPI.hpp\"\n\n"
                << "namespace Engine {\n\n"
                << "    struct ENGINE_API CustomComponent {\n"
                << "        float speed = 1.0f;\n"
                << "    };\n\n"
                << "}\n";
            out.close();
        }
    });

    Engine::AssetBrowserRegistry::registerOption("Actions/Print Path", [](const std::filesystem::path& folderPath) {
        std::printf("Selected Asset Folder Path: %s\n", folderPath.generic_string().c_str());
    });

    applyInputMode();
    initialized = true;
}

void EditorUI::shutdown() {
    if (!initialized) {
        return;
    }

    vkDeviceWaitIdle(renderer.device.getDevice());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    DestroyContext();
    destroyDescriptorPool();
    initialized = false;
}

void EditorUI::beginFrame() {
    if (!initialized) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    NewFrame();
}

void EditorUI::render(VkCommandBuffer commandBuffer) {
    if (!initialized) {
        return;
    }

    Render();
    ImGui_ImplVulkan_RenderDrawData(GetDrawData(), commandBuffer);
}

void EditorUI::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(size(poolSizes));
    poolInfo.poolSizeCount = static_cast<uint32_t>(size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(renderer.device.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw runtime_error("Failed to create ImGui descriptor pool");
    }
}

void EditorUI::destroyDescriptorPool() {
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(renderer.device.getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
}

void EditorUI::applyInputMode() {
    if (!window) {
        return;
    }

    if (editorMode.flyMode) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void EditorUI::toggleFlyMode() {
    // Empty implementation for safety to avoid future linker errors
}

void EditorUI::registerComponentInspector(const std::string& name, ComponentInspectorCallback callback) {
    getDynamicInspectors().push_back({name, callback});
}

void EditorUI::registerComponentAddCallback(const std::string& name, ComponentAddCallback callback) {
    getDynamicAddCallbacks().push_back({name, callback});
}

std::vector<std::pair<std::string, EditorUI::ComponentInspectorCallback>>& EditorUI::getDynamicInspectors() {
    static std::vector<std::pair<std::string, ComponentInspectorCallback>> s_inspectors;
    return s_inspectors;
}

std::vector<std::pair<std::string, EditorUI::ComponentAddCallback>>& EditorUI::getDynamicAddCallbacks() {
    static std::vector<std::pair<std::string, ComponentAddCallback>> s_callbacks;
    return s_callbacks;
}
