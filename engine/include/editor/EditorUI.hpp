#pragma once

#ifndef _WIN32
#include <cstring>
#include <algorithm>

template <size_t N>
inline int strcpy_s(char (&dest)[N], const char* src) {
    if (!src) {
        if (N > 0) dest[0] = '\0';
        return -1; // EINVAL
    }
    size_t i = 0;
    for (; i < N - 1 && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    if (src[i] != '\0') {
        if (N > 0) dest[0] = '\0';
        return -2; // ERANGE
    }
    return 0;
}

template <size_t N>
inline int strncpy_s(char (&dest)[N], const char* src, size_t count) {
    if (!src) {
        if (N > 0) dest[0] = '\0';
        return -1; // EINVAL
    }
    size_t limit = std::min(N - 1, count);
    size_t i = 0;
    for (; i < limit && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return 0;
}
#endif

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <vulkan/vulkan.h>

#include "../ecs/Entity.hpp"
#include "EditorModeState.hpp"
#include "../ecs/components/Transform.hpp"

struct GLFWwindow;
class Registry;
class SceneManager;
class VulkanRenderer;

/**
 * @class EditorUI
 * @brief Handles rendering of the editor GUI using ImGui, scene hierarchy, inspector, gizmos, and raycast picking.
 */
class EditorUI {
public:
    /**
     * @struct SpritesheetAnimConfig
     * @brief Configuration for a single animation clip extracted from a spritesheet.
     */
    struct SpritesheetAnimConfig {
        std::string name = "Animation";
        int frameCount = 4;
        int startFrameIndex = 0;
    };
    using BuildGameCallback = std::function<int(const std::string& projectPath, const std::string& outPath)>;
    using CompileScriptsCallback = std::function<int(const std::string& projectPath)>;

    /**
     * @brief Construct a new Editor UI object.
     * @param registry Reference to the ECS Registry.
     * @param renderer Reference to the Vulkan Renderer.
     * @param sceneManager Reference to the Scene Manager.
     * @param editorMode Reference to the Editor Mode State.
     */
    EditorUI(Registry& registry, VulkanRenderer& renderer, SceneManager& sceneManager, EditorModeState& editorMode, const std::string& startScenePath = "assets/scenes/test_scene.json", BuildGameCallback buildGameCallback = {}, CompileScriptsCallback compileScriptsCallback = {});
    /**
     * @brief Destroy the Editor UI object.
     */
    ~EditorUI();

    /**
     * @brief Initializes ImGui bindings and structures.
     * @param window Pointer to the GLFW window.
     */
    void initialize(GLFWwindow* window);
    /**
     * @brief Shuts down ImGui and releases descriptor pools.
     */
    void shutdown();
    /**
     * @brief Starts a new ImGui frame.
     */
    void beginFrame();
    /**
     * @brief Processes and populates ImGui panel layout commands.
     */
    void drawPanels();
    /**
     * @brief Submits ImGui draw data to the current Vulkan command buffer.
     * @param commandBuffer Command buffer to write commands to.
     */
    void render(VkCommandBuffer commandBuffer);
    /**
     * @brief Toggles between first-person fly camera controls and editor UI control modes.
     */
    void toggleFlyMode();

    // Dynamic UI Registration for dynamic plugins
    using ComponentInspectorCallback = std::function<void(Registry&, Entity)>;
    using ComponentAddCallback = std::function<void(Registry&, Entity)>;

    static void registerComponentInspector(const std::string& name, ComponentInspectorCallback callback);
    static void registerComponentAddCallback(const std::string& name, ComponentAddCallback callback);

    static std::vector<std::pair<std::string, ComponentInspectorCallback>>& getDynamicInspectors();
    static std::vector<std::pair<std::string, ComponentAddCallback>>& getDynamicAddCallbacks();

private:
    /**
     * @brief Processes viewport clicking rays for entity selection.
     */
    void handleViewportPicking();
    /**
     * @brief Creates Vulkan descriptor pool specifically for ImGui.
     */
    void createDescriptorPool();
    /**
     * @brief Releases ImGui Vulkan descriptor pool resources.
     */
    void destroyDescriptorPool();
    /**
     * @brief Renders panel controls for scene operations (New/Load/Save/Spawn).
     */
    void drawSceneControls();
    /**
     * @brief Renders hierarchical entity list panel.
     */
    void drawHierarchyPanel();
    /**
     * @brief Renders detail inspector panel for the selected entity.
     */
    void drawInspectorPanel();
    /**
     * @brief Renders inline controls for Transform component fields.
     */
    void drawTransformEditor();
    /**
     * @brief Renders inline controls for Material component fields.
     */
    void drawMaterialEditor();
    /**
     * @brief Renders the asset browser panel.
     */
    void drawAssetBrowser();
    /**
     * @brief Renders the floating asset import settings window.
     */
    void drawImportSettingsWindow();
    /**
     * @brief Renders the Tileset Editor window.
     */
    void drawTilesetEditorWindow();
    void drawAnimationEditorWindow();
    void drawNodeGraphDemoWindow();
    void drawAnimatorControllerWindow();
    /**
     * @brief Renders the Sprite Sheet Slicer window.
     */
    void drawSpriteSlicerWindow();
    /**
     * @brief Slices a sprite sheet texture into multiple separate PNG texture files.
     * @return List of output file paths generated.
     */
    std::vector<std::filesystem::path> sliceSpriteSheet(const std::filesystem::path& path, int cellWidth, int cellHeight, const std::string& prefix, bool skipEmptyTiles = true);

    /**
     * @brief Renders the Spritesheet to Animations converter window.
     */
    void drawSpritesheetConverterWindow();

    /**
     * @brief Converts a spritesheet texture into .anim files targeting SpriteRenderer.
     */
    void convertSpritesheetToAnimations(
        const std::filesystem::path& spritesheetPath,
        int cellWidth,
        int cellHeight,
        float frameRate,
        const std::vector<SpritesheetAnimConfig>& animConfigs,
        bool combineIntoSingleFile
    );

    /**
     * @brief Renders the Tilemap component inspector panel.
     */
    void drawTilemapInspector();
    /**
     * @brief Renders the SpriteRenderer component inspector panel.
     */
    void drawSpriteRendererInspector();
    /**

     * @brief Renders custom editor inspectors for all game UI components.
     */
    void drawUIComponentsEditor();
    /**
     * @brief Renders a 3D grid overlay on the tilemap for editor visual feedback.
     */
    void drawTilemapGridOverlay();
    /**
     * @brief Renders inline controls for Mesh component fields.
     */
    void drawMeshEditor();
    /**
     * @brief Renders inline controls for Grid component fields.
     */
    void drawGridEditor();
    /**
     * @brief Renders inline controls for Camera component fields.
     */
    void drawCameraEditor();
    /**
     * @brief Renders inline controls for Skeleton component fields.
     */
    void drawSkeletonEditor();
    /**
     * @brief Renders inline controls for Animator component fields.
     */
    void drawAnimatorEditor();
    /**
     * @brief Renders inline controls for Hierarchy component fields.
     */
    void drawHierarchyEditor();
    /**
     * @brief Renders inline controls for IK Solver component fields.
     */
    void drawIKSolverEditor();
    /**
     * @brief Renders inline controls for Animation Controller component fields.
     */
    void drawAnimationControllerEditor();
    /**
     * @brief Renders inline controls for all reflected component fields dynamically.
     */
    void drawReflectedComponentsEditor();
    /**
     * @brief Renders inline controls for Collider component fields.
     */
    void drawColliderEditor();
    /**
     * @brief Draws a styled banner title inside component panels.
     * @param title Header title text.
     */
    void drawSectionHeader(const std::string& title);
    /**
     * @brief Renders custom controls for editing glm::vec3 variables.
     * @param label The control's label.
     * @param values Pointer to the float array of 3 values.
     * @param speed Interaction speed/step.
     * @return True if any values changed.
     */
    bool drawVec3Control(const char* label, float* values, float speed = 0.05f);
    /**
     * @brief Applies mouse visibility cursor mode according to active editor mode.
     */
    void applyInputMode();
    /**
     * @brief Renders the debug metrics and raycast state panel.
     */
    void drawDebugPanel();
    /**
     * @brief Processes and displays transformation gizmo overlay on the selected entity.
     */
    void drawGizmo();
    /**
     * @brief Renders the Build Settings panel for packaging the game.
     */
    void drawBuildSettingsPanel();
    /**
     * @brief Helper function to extract Euler transform fields from a 4x4 matrix.
     * @param mat Input matrix.
     * @param t Output transform parameters.
     */
    void decomposeMatrixToTransform(const glm::mat4& mat, Transform& t);
    /**
     * @brief Recursively traverses parent references to build the entity's absolute world transform.
     */
    glm::mat4 getEntityWorldMatrix(Entity entity, int depth = 0);
    /**
     * @brief Draws wireframe overlays for all active colliders when enabled.
     */
    void drawColliderDebugOverlay();
    /**
     * @brief Draws physics gun debug rays when activated by the user.
     */
    void drawPhysgunDebugOverlay();

    /** @brief Reference to registry. */
    Registry& registry;
    /** @brief Reference to Vulkan renderer. */
    VulkanRenderer& renderer;
    /** @brief Reference to scene manager. */
    SceneManager& sceneManager;
    /** @brief Reference to editor mode state tracker. */
    EditorModeState& editorMode;
    /** @brief Pointer to GLFW window. */
    GLFWwindow* window = nullptr;
    /** @brief ImGui dedicated descriptor pool. */
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    /** @brief State flag for ImGui initialization status. */
    bool initialized = false;
    /** @brief Target json file path for scene serialization. */
    std::string scenePath = "assets/scenes/test_scene.json";
    /** @brief Status info message printed on UI panel. */
    std::string statusMessage = "Scene not saved yet.";
    /** @brief Cache buffer string for renaming entities. */
    std::string renameBuffer;
    /** @brief The entity currently selected. */
    Entity selectedEntity{};
    /** @brief Selection flag. */
    bool hasSelection = false;
    /** @brief Mouse click state tracking. */
    bool previousLeftMouseDown = false;
    /** @brief Raycast origin caching. */
    glm::vec3 lastPickRayOrigin{ 0.0f };
    /** @brief Raycast direction caching. */
    glm::vec3 lastPickRayDirection{ 0.0f, 0.0f, -1.0f };
    /** @brief Name of the picked entity. */
    std::string lastPickNearestEntityName = "None";
    /** @brief Distance to the picked entity. */
    float lastPickNearestDistance = -1.0f;
    /** @brief Human-readable pick results. */
    std::string lastPickResult = "No pick attempted yet.";
    /** @brief Editor/Fly toggle key state cache. */
    bool previousToggleKeyDown = false;
    /** @brief Flag to toggle drawing collider wireframes in the viewport. */
    bool showColliders = false;
    /** @brief Whether the Build Settings panel is open. */
    bool showBuildSettings = false;
    /** @brief Output path for game builds. */
    std::string buildOutputPath = "build/";
    /** @brief Status message from last build attempt. */
    std::string buildStatusMessage;
    /** @brief Whether a build is currently in progress. */
    bool buildInProgress = false;
    /** @brief Optional callback used to build the game while the editor is running. */
    BuildGameCallback buildGameCallback;
    /** @brief Optional callback used to compile user scripts while the editor is running. */
    CompileScriptsCallback compileScriptsCallback;
};
