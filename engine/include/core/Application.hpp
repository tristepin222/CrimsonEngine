#pragma once
#include <string>
#include <memory>
#include "renderer/VulkanRenderer.hpp"
#include "ecs/Registry.hpp"
#include "ecs/SystemManager.hpp"
#include "scenes/SceneManager.hpp"
#include "editor/EditorModeState.hpp"
#include "editor/EditorUI.hpp"
#include "scenes/ComponentSerializerRegistry.hpp"
#include "core/PluginManager.hpp"

struct GLFWwindow;
class RenderSystem;

#include "renderer/RenderSettings.hpp"

namespace Engine {
    class UISystem;

    /**
     * @struct ApplicationConfig
     * @brief Structure holding start configuration for window size, mode, and start scene paths.
     */
    struct ApplicationConfig {
        std::string title = "Game Engine";
        int width = 1280;
        int height = 720;
        bool enableEditor = true;
        std::string startScenePath = "assets/scenes/test_scene.json";
        /** @brief Path to the game project directory. Scripts in <projectPath>/scripts/ are loaded automatically. */
        std::string projectPath = ".";
        /** @brief Directory where the editor/runtime executable lives. Used to locate engine plugins. */
        std::string exeDir;

        ShadowSettings shadowSettings{};
        bool hasCustomShadowSettings = false;

        TonemapSettings tonemapSettings{};
        bool hasCustomTonemapSettings = false;

        SSAOSettings ssaoSettings{};
        bool hasCustomSSAOSettings = false;
    };

    /**
     * @class Application
     * @brief Encapsulates the core engine lifecycle, main render loop, window, Vulkan renderer, and systems.
     */
    class Application {
    public:
        /**
         * @brief Construct a new Application.
         * @param config The application start configuration.
         */
        Application(const ApplicationConfig& config = ApplicationConfig());

        /**
         * @brief Destroy the Application and release Vulkan/window resources.
         */
        virtual ~Application();

        /**
         * @brief Boots and runs the main loop of the application.
         */
        void run();

        /**
         * @brief Stops the application execution.
         */
        void quit();

        /**
         * @brief Templated method to register game-specific components to the serialization registry.
         * @tparam T Component structure/class type.
         * @param componentName Key name string in JSON files.
         */
        template <typename T>
        void registerComponent(const std::string& componentName) {
            ComponentSerializerRegistry::getInstance().registerComponent(
                componentName,
                &T::serialize,
                &T::deserialize
            );
        }

        /**
         * @brief Called upon initialization. Override to register custom components and load startup scenes.
         */
        virtual void onStart() {}

        /**
         * @brief Called each frame for custom update logic.
         * @param dt Delta time in seconds.
         */
        virtual void onUpdate(float dt) {}

        /**
         * @brief Called during application shutdown.
         */
        virtual void onShutdown() {}

        /**
         * @brief Gets the current EditorModeState (e.g. isPlaying, flyMode).
         */
        EditorModeState& getEditorMode() { return editorMode; }

    protected:
        /** @brief Registry instance for ECS entity-component database. */
        Registry registry;

        /** @brief Scene Manager coordinates scene changes and loading. */
        SceneManager sceneManager;

        /** @brief System Manager driving sequential system update loops. */
        SystemManager systemManager;

        /**
         * @brief Gets reference to the active VulkanRenderer.
         * @return VulkanRenderer& reference.
         */
        VulkanRenderer& getRenderer() { return *renderer; }

    private:
        /** @brief Native window handle. */
        GLFWwindow* window = nullptr;
        /** @brief Reference to VulkanRenderer. */
        std::unique_ptr<VulkanRenderer> renderer;
        /** @brief Editor graphical interface overlay. */
        std::unique_ptr<EditorUI> editorUI;
        /** @brief Shared editor toggle and navigation states. */
        EditorModeState editorMode;

        /** @brief Reference to default rendering system. */
        std::shared_ptr<RenderSystem> renderSystem;
        /** @brief Reference to UI system. */
        std::shared_ptr<UISystem> uiSystem;

        /** @brief Pointer to the dynamic plugin manager. */
        std::unique_ptr<PluginManager> pluginManager;

        /** @brief Active application config values. */
        ApplicationConfig config;

        /** @brief Running status flag. */
        bool running = false;

    private:
        void initEngine();
        void cleanupEngine();
        void loadConfig();
        int buildGame(const std::string& projectPath, const std::string& outPath);
        int compileScripts(const std::string& projectPath);
        int fallbackBuildGame(const std::string& projectPath, const std::string& outPath);
    };

} // namespace Engine
