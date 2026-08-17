#include "core/Application.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include "ecs/systems/RenderSystem.hpp"
#include "ecs/systems/CameraSystem.hpp"
#include "ecs/systems/InputSystem.hpp"
#include "ecs/systems/AnimationSystem.hpp"
#include "ecs/systems/PhysicsSystem.hpp"
#include "ecs/systems/PlayerControllerSystem.hpp"
#include "ecs/systems/AudioSystem.hpp"
#include "ecs/systems/TilemapSystem.hpp"
#include "ecs/systems/UISystem.hpp"
#include "ecs/systems/SpriteSystem.hpp"
#include "scenes/Scene.hpp"
#include "scenes/JSONUtils.hpp"
#include "scenes/DefaultScene.hpp"
#include "scenes/SceneManagement.hpp"
#include "core/JobSystem.hpp"
#include "ecs/components/EditorCamera.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Camera.hpp"
#include "ecs/components/inputComponent.hpp"


namespace Engine {

    Application::Application(const ApplicationConfig& cfg) : config(cfg) {
        // Resolve the project path and change CWD to it so that all relative asset
        // paths (assets/, scenes/, shaders/) work regardless of where the exe lives.
        std::filesystem::path projectPath = std::filesystem::absolute(config.projectPath);
        if (std::filesystem::is_directory(projectPath)) {
            std::filesystem::current_path(projectPath);
            config.projectPath = projectPath.string();
            std::cout << "[Application] Working directory set to project: " << projectPath.string() << std::endl;
        } else {
            std::cerr << "[Application] WARNING: projectPath does not exist: " << projectPath.string() << std::endl;
        }
        loadConfig();
        initEngine();
    }

    Application::~Application() {
        cleanupEngine();
    }

    void Application::loadConfig() {
        std::ifstream file("project.settings");
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();

            std::string titleVal = JSONUtils::extractStringValue(content, "title");
            if (!titleVal.empty()) config.title = titleVal;

            float w = 0.0f, h = 0.0f;
            if (JSONUtils::extractFloatValue(content, "width", w)) config.width = static_cast<int>(w);
            if (JSONUtils::extractFloatValue(content, "height", h)) config.height = static_cast<int>(h);

            // The editor state (enableEditor) is strictly determined by the entry point binary (editor.exe or game_runtime.exe) and shouldn't be overridden by project settings.

            std::string sceneVal = JSONUtils::extractStringValue(content, "startScenePath");
            if (!sceneVal.empty()) config.startScenePath = sceneVal;

            std::cout << "[Application] Config loaded from project.settings" << std::endl;
        } else {
            std::cout << "[Application] project.settings not found, using configurations from code" << std::endl;
        }
    }

    void Application::initEngine() {
        JobSystem::getInstance().initialize(); // Initialize Job System thread pool

        std::cout << "[Application] Job System initialized successfully" << std::endl;

        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("Failed to create GLFW window");
        }

        std::cout << "[Application] GLFW window created successfully" << std::endl;

        renderer = std::make_unique<VulkanRenderer>(window, config.exeDir);

        std::cout << "[Application] VulkanRenderer initialized successfully" << std::endl;

        // Instantiate standard engine systems
        renderSystem = std::make_shared<RenderSystem>(registry, *renderer);
        auto cameraSystem = std::make_shared<CameraSystem>(registry, *renderer, editorMode);
        auto inputSystem = std::make_shared<InputSystem>(registry, *renderer, editorMode);
        auto animationSystem = std::make_shared<AnimationSystem>(registry, *renderer, editorMode);
        auto physicsSystem = std::make_shared<PhysicsSystem>(registry, editorMode);
        auto playerControllerSystem = std::make_shared<PlayerControllerSystem>(registry, *renderer, editorMode);
        auto audioSystem = std::make_shared<AudioSystem>(registry, editorMode);
        auto tilemapSystem = std::make_shared<TilemapSystem>(registry, *renderer);
        auto spriteSystem  = std::make_shared<SpriteSystem>(registry, *renderer);
        uiSystem = std::make_shared<UISystem>(registry, *renderer);

        systemManager.addSystem(inputSystem);
        systemManager.addSystem(cameraSystem);
        systemManager.addSystem(tilemapSystem);
        systemManager.addSystem(spriteSystem);
        systemManager.addSystem(physicsSystem);
        systemManager.addSystem(animationSystem);
        systemManager.addSystem(playerControllerSystem);
        systemManager.addSystem(audioSystem);
        systemManager.addSystem(renderSystem);
        systemManager.addSystem(uiSystem);

        // Spawn persistent Editor Camera
        Entity editorCam = registry.create();
        registry.emplace<Name>(editorCam, Name{"EditorCamera"});
        registry.emplace<Transform>(editorCam, Transform{ glm::vec3(0.0f, 2.0f, 5.0f) });
        registry.emplace<Camera>(editorCam, Camera{});
        registry.emplace<InputComponent>(editorCam, InputComponent{});
        registry.emplace<EditorCamera>(editorCam, EditorCamera{});

        // Initialize editor UI overlay (always initialized to support ImGui Game UI rendering)
        editorUI = std::make_unique<EditorUI>(registry, *renderer, sceneManager, editorMode, config.startScenePath,
            [this](const std::string& projectPath, const std::string& outPath) {
                return buildGame(projectPath, outPath);
            },
            [this](const std::string& projectPath) {
                return compileScripts(projectPath);
            });
        editorUI->initialize(window);

        // Setup initial editor fly mode based on whether editor UI is present
        if (!config.enableEditor) {
            editorMode.isPlaying = true;
            editorMode.flyMode = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else {
            editorMode.flyMode = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        // Load engine-level plugins, then project-level scripts
        pluginManager = std::make_unique<PluginManager>(registry, systemManager, *renderer, editorMode);
        pluginManager->setExeDirectory(config.exeDir);
        pluginManager->loadPlugins();
        pluginManager->loadScripts(config.projectPath);

        sceneManager.setContext(&registry, renderer.get());
        SceneManagement::setSceneManager(&sceneManager);
        sceneManager.onSceneLoadedCallback = [this](const SceneInfo& info) {
            systemManager.notifySceneLoadedAll();
        };
        sceneManager.changeScene(std::make_unique<DefaultScene>(registry, *renderer, config.startScenePath));

        running = true;

        std::cout << "[Application] Engine initialized successfully" << std::endl;
    }

    void Application::cleanupEngine() {
        // 1. Stop the game loop / flush pending work first
        JobSystem::getInstance().shutdown();

        // 2. Unload scripts/plugins (they may reference Vulkan objects via systems)
        if (pluginManager) {
            pluginManager->unloadPlugins();
            pluginManager.reset();
        }

        // 3. Shut down all ECS systems (they hold Vulkan pipelines, descriptors, etc.)
        systemManager.clear();
        renderSystem.reset();

        // 4. Unload current scene and destroy all entities/components (releases any Vulkan-backed resources)
        sceneManager.changeScene(nullptr);
        registry.clear();

        // 5. Shut down the editor UI (destroys ImGui Vulkan backend, descriptor sets)
        if (editorUI) {
            editorUI->shutdown();
            editorUI.reset();
        }

        // 6. Destroy the Vulkan renderer (device, swapchain, instance)
        if (renderer) {
            renderer->cleanup();
            renderer.reset();
        }

        // 7. Destroy the window and GLFW
        if (window) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
    }

    int Application::buildGame(const std::string& projectPath, const std::string& outPath) {
        // pluginManager->unloadPlugins(); // Disabled to keep engine plugins loaded during build

        std::filesystem::path outPathFs = std::filesystem::absolute(outPath);
        outPathFs = outPathFs.lexically_normal();
        std::string outPathArg = outPathFs.string();
        if (!outPathArg.empty() && (outPathArg.back() == '\\' || outPathArg.back() == '/')) {
            outPathArg.pop_back();
        }

        std::string batchPath = "build_game_package.bat";
        bool found = false;

        // 1. Try project root
        std::filesystem::path projBatch = std::filesystem::path(projectPath) / "build_game_package.bat";
        if (std::filesystem::exists(projBatch)) {
            batchPath = std::filesystem::absolute(projBatch).string();
            found = true;
        }

        // 2. Try config.exeDir (engine root)
        if (!found && !config.exeDir.empty()) {
            std::filesystem::path exeBatch = std::filesystem::path(config.exeDir) / "build_game_package.bat";
            if (std::filesystem::exists(exeBatch)) {
                batchPath = std::filesystem::absolute(exeBatch).string();
                found = true;
            }
        }

        // 3. Try config.exeDir/.. (fallback if started from sdk/bin/ or subdir)
        if (!found && !config.exeDir.empty()) {
            std::filesystem::path exeBatchParent = std::filesystem::path(config.exeDir) / ".." / "build_game_package.bat";
            if (std::filesystem::exists(exeBatchParent)) {
                batchPath = std::filesystem::absolute(exeBatchParent).string();
                found = true;
            }
        }

        // 4. Try CWD
        if (!found) {
            std::filesystem::path cwdBatch = std::filesystem::current_path() / "build_game_package.bat";
            if (std::filesystem::exists(cwdBatch)) {
                batchPath = std::filesystem::absolute(cwdBatch).string();
                found = true;
            } else {
                // Fallback to absolute resolving of CWD filename
                batchPath = std::filesystem::absolute(batchPath).string();
            }
        }

        int result = -1;
        if (found) {
            std::string cmd = "\"\"" + batchPath + "\" \"" + projectPath + "\" \"" + outPathArg + "\"\"";
            std::cout << "[BuildSystem] Running: " << cmd << std::endl;
            result = std::system(cmd.c_str());
        } else {
            result = fallbackBuildGame(projectPath, outPath);
        }

        if (pluginManager) {
            pluginManager->loadPlugins();
            pluginManager->loadScripts(config.projectPath);
        }

        return result;
    }

    int Application::compileScripts(const std::string& projectPath) {
        std::filesystem::path absProjectPath = std::filesystem::absolute(projectPath).lexically_normal();
        std::filesystem::path scriptsDir = absProjectPath / "scripts";
        
        // If scripts folder doesn't exist, we don't compile anything
        if (!std::filesystem::exists(scriptsDir)) {
            std::cout << "[BuildSystem] No scripts/ directory found. Skipping script compilation." << std::endl;
            return 0;
        }

        std::filesystem::path buildDir = absProjectPath / ".script_build";
        std::filesystem::create_directories(buildDir);

        std::filesystem::path scriptsCMake = scriptsDir / "CMakeLists.txt";
        std::filesystem::path sourceDir = scriptsDir;

        if (!std::filesystem::exists(scriptsCMake)) {
            std::cout << "[BuildSystem] scripts/CMakeLists.txt not found. Generating temporary CMake file in build directory..." << std::endl;
            
            // 1. Create standard folder structure if missing
            std::filesystem::create_directories(scriptsDir / "public");
            std::filesystem::create_directories(scriptsDir / "private");
            std::filesystem::create_directories(scriptsDir / "src");

            // 2. Add placeholder.cpp if private/ is completely empty
            bool hasSources = false;
            if (std::filesystem::exists(scriptsDir / "private")) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsDir / "private")) {
                    if (entry.is_regular_file() && (entry.path().extension() == ".cpp" || entry.path().extension() == ".cc")) {
                        hasSources = true;
                        break;
                    }
                }
            }
            if (!hasSources) {
                std::filesystem::path placeholder = scriptsDir / "private" / "placeholder.cpp";
                std::ofstream out(placeholder);
                out << "// Place your game script files in public/ (headers) and private/ (sources)\n";
                out.close();
            }

            std::filesystem::path tempCMake = buildDir / "CMakeLists.txt";
            
            std::string sdkPath = config.exeDir;
            std::replace(sdkPath.begin(), sdkPath.end(), '\\', '/');

            std::string normProjectPath = absProjectPath.string();
            std::replace(normProjectPath.begin(), normProjectPath.end(), '\\', '/');

            std::ofstream cmakeOut(tempCMake);
            cmakeOut << "cmake_minimum_required(VERSION 3.20)\n"
                     << "project(GameScripts LANGUAGES CXX)\n\n"
                     << "set(CMAKE_CXX_STANDARD 20)\n"
                     << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n"
                     << "# Find the Engine SDK\n"
                     << "find_package(Engine REQUIRED PATHS\n"
                     << "    \"" << sdkPath << "/cmake\"\n"
                     << ")\n\n"
                     << "# Setup directories\n"
                     << "set(SCRIPT_PUBLIC_DIR \"" << normProjectPath << "/scripts/public\")\n"
                     << "set(SCRIPT_PRIVATE_DIR \"" << normProjectPath << "/scripts/private\")\n"
                     << "set(SCRIPT_SRC_DIR \"" << normProjectPath << "/scripts/src\")\n\n"
                     << "# Glob sources\n"
                     << "file(GLOB_RECURSE SCRIPT_SOURCES \n"
                     << "    \"${SCRIPT_PRIVATE_DIR}/*.cpp\"\n"
                     << "    \"${SCRIPT_PRIVATE_DIR}/*.cc\"\n"
                     << ")\n"
                     << "list(REMOVE_ITEM SCRIPT_SOURCES \"${SCRIPT_SRC_DIR}/generated_reflection.cpp\")\n\n"
                     << "file(GLOB_RECURSE SCRIPT_HEADERS \n"
                     << "    \"${SCRIPT_PUBLIC_DIR}/*.hpp\"\n"
                     << "    \"${SCRIPT_PUBLIC_DIR}/*.h\"\n"
                     << ")\n\n"
                     << "# Make sure we have sources\n"
                     << "if(NOT SCRIPT_SOURCES)\n"
                     << "    message(FATAL_ERROR \"No script source files found in scripts/private!\")\n"
                     << "endif()\n\n"
                     << "# Find the reflection generator\n"
                     << "if(EXISTS \"${ENGINE_SDK_ROOT}/bin/reflection_generator.exe\")\n"
                     << "    set(REFLECTION_GENERATOR_EXE \"${ENGINE_SDK_ROOT}/bin/reflection_generator.exe\")\n"
                     << "else()\n"
                     << "    set(REFLECTION_GENERATOR_EXE \"${ENGINE_SDK_ROOT}/reflection_generator.exe\")\n"
                     << "endif()\n\n"
                     << "# Output generated reflection file\n"
                     << "set(GENERATED_REF_CPP \"${SCRIPT_SRC_DIR}/generated_reflection.cpp\")\n"
                     << "file(MAKE_DIRECTORY \"${SCRIPT_SRC_DIR}\")\n\n"
                     << "if(NOT CMAKE_GENERATOR_PLATFORM MATCHES \"ARM\" AND NOT CMAKE_CROSSCOMPILING)\n"
                     << "    add_custom_command( \n"
                     << "        OUTPUT \"${GENERATED_REF_CPP}\"\n"
                     << "        COMMAND \"${REFLECTION_GENERATOR_EXE}\" \"${SCRIPT_PUBLIC_DIR}\" \"${GENERATED_REF_CPP}\"\n"
                     << "        DEPENDS ${SCRIPT_SOURCES} ${SCRIPT_HEADERS}\n"
                     << "        COMMENT \"[Reflection Generator] Parsing annotations...\"\n"
                     << "    )\n"
                     << "else()\n"
                     << "    add_custom_command(\n"
                     << "        OUTPUT \"${GENERATED_REF_CPP}\"\n"
                     << "        COMMAND ${CMAKE_COMMAND} -E echo \"[Reflection Generator] Skipping...\"\n"
                     << "        DEPENDS ${SCRIPT_SOURCES} ${SCRIPT_HEADERS}\n"
                     << "    )\n"
                     << "endif()\n\n"
                     << "list(APPEND SCRIPT_SOURCES \"${GENERATED_REF_CPP}\")\n\n"
                     << "add_library(game_scripts SHARED ${SCRIPT_SOURCES})\n\n"
                     << "target_include_directories(game_scripts PRIVATE\n"
                     << "    \"${SCRIPT_PUBLIC_DIR}\"\n"
                     << "    \"${SCRIPT_PRIVATE_DIR}\"\n"
                     << "    \"${ENGINE_SDK_ROOT}/include\"\n"
                     << "    \"${ENGINE_SDK_ROOT}/include/plugins/cinemachine\"\n"
                     << "    \"${ENGINE_SDK_ROOT}/include/plugins/AStar\"\n"
                     << ")\n\n"
                     << "target_link_libraries(game_scripts PRIVATE Engine::engine)\n\n"
                     << "# Output DLL to project's bin folder\n"
                     << "set(OUTPUT_DIR \"" << normProjectPath << "/bin\")\n"
                     << "set_target_properties(game_scripts PROPERTIES\n"
                     << "    LIBRARY_OUTPUT_DIRECTORY_RELEASE \"${OUTPUT_DIR}\"\n"
                     << "    RUNTIME_OUTPUT_DIRECTORY_RELEASE \"${OUTPUT_DIR}\"\n"
                     << "    ARCHIVE_OUTPUT_DIRECTORY_RELEASE \"${OUTPUT_DIR}\"\n"
                     << "    LIBRARY_OUTPUT_DIRECTORY_DEBUG \"${OUTPUT_DIR}\"\n"
                     << "    RUNTIME_OUTPUT_DIRECTORY_DEBUG \"${OUTPUT_DIR}\"\n"
                     << "    ARCHIVE_OUTPUT_DIRECTORY_DEBUG \"${OUTPUT_DIR}\"\n"
                     << ")\n";
            cmakeOut.close();
            
            sourceDir = buildDir;
        }

        // Previously we unloaded all plugins before building, which could corrupt engine plugin state.
        // Instead, we only need to configure and build the scripts, then reload the compiled script DLLs.
        // Run CMake config and build dynamically (Release build)
        std::string configCmd = "cmake -S \"" + sourceDir.string() + "\" -B \"" + buildDir.string() + "\" -G \"Visual Studio 17 2022\" -A x64 -T v143 -DCMAKE_BUILD_TYPE=Release";
        std::string buildCmd = "cmake --build \"" + buildDir.string() + "\" --config Release";

        std::cout << "[BuildSystem] Configuring scripts: " << configCmd << std::endl;
        int result = std::system(configCmd.c_str());
        if (result == 0) {
            if (pluginManager) {
                // Unload active script DLLs so the linker can overwrite them without LNK1104 file locks
                pluginManager->unloadScripts();
            }
            std::cout << "[BuildSystem] Building scripts: " << buildCmd << std::endl;
            result = std::system(buildCmd.c_str());
        }

        if (pluginManager) {
            // Reload only the newly built user script plugins.
            pluginManager->loadScripts(config.projectPath);
        }

        return result;
    }

    static void copyDir(const std::filesystem::path& source, const std::filesystem::path& destination) {
        std::filesystem::create_directories(destination);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
            const auto& path = entry.path();
            auto relative = std::filesystem::relative(path, source);
            if (std::filesystem::is_directory(path)) {
                std::filesystem::create_directories(destination / relative);
            } else if (std::filesystem::is_regular_file(path)) {
                std::filesystem::copy_file(path, destination / relative, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    int Application::fallbackBuildGame(const std::string& projectPath, const std::string& outPath) {
        std::cout << "[BuildSystem] Performing fallback game packaging in C++..." << std::endl;

        std::filesystem::path projPathFs = std::filesystem::absolute(projectPath).lexically_normal();
        std::filesystem::path outPathFs = std::filesystem::absolute(outPath).lexically_normal();

        // 1. Recreate output directory
        try {
            if (std::filesystem::exists(outPathFs)) {
                std::filesystem::remove_all(outPathFs);
            }
            std::filesystem::create_directories(outPathFs);
        } catch (const std::exception& e) {
            std::cerr << "[BuildSystem] Failed to recreate output directory: " << e.what() << std::endl;
            return 1;
        }

        // Determine SDK directory from config.exeDir
        std::filesystem::path sdkDir = config.exeDir.empty() ? std::filesystem::current_path() : std::filesystem::path(config.exeDir);

        // 2. Copy game_runtime.exe as game.exe
        std::filesystem::path runtimeSrc = sdkDir / "bin" / "game_runtime.exe";
        if (!std::filesystem::exists(runtimeSrc)) {
            runtimeSrc = sdkDir / "game_runtime.exe";
        }
        if (!std::filesystem::exists(runtimeSrc)) {
            std::cerr << "[BuildSystem] [ERROR] game_runtime.exe not found in SDK path: " << sdkDir.string() << std::endl;
            return 1;
        }
        try {
            std::filesystem::copy_file(runtimeSrc, outPathFs / "game.exe", std::filesystem::copy_options::overwrite_existing);
        } catch (const std::exception& e) {
            std::cerr << "[BuildSystem] Failed to copy game_runtime.exe: " << e.what() << std::endl;
            return 1;
        }

        // 3. Copy engine.dll
        std::filesystem::path engineDll = sdkDir / "engine.dll";
        if (std::filesystem::exists(engineDll)) {
            try {
                std::filesystem::copy_file(engineDll, outPathFs / "engine.dll", std::filesystem::copy_options::overwrite_existing);
            } catch (const std::exception& e) {
                std::cerr << "[BuildSystem] Failed to copy engine.dll: " << e.what() << std::endl;
                return 1;
            }
        }

        // 4. Copy engine plugins folder
        std::filesystem::path pluginsSrc = sdkDir / "plugins";
        if (std::filesystem::exists(pluginsSrc)) {
            try {
                copyDir(pluginsSrc, outPathFs / "plugins");
            } catch (const std::exception& e) {
                std::cerr << "[BuildSystem] Warning: Failed to copy plugins: " << e.what() << std::endl;
            }
        }

        // 5. Copy shaders folder
        std::filesystem::path shadersSrc = sdkDir / "shaders";
        if (!std::filesystem::exists(shadersSrc)) {
            shadersSrc = sdkDir / ".." / "shaders";
        }
        if (std::filesystem::exists(shadersSrc)) {
            try {
                copyDir(shadersSrc, outPathFs / "shaders");
            } catch (const std::exception& e) {
                std::cerr << "[BuildSystem] Warning: Failed to copy shaders: " << e.what() << std::endl;
            }
        }

        // 6. Copy assets folder
        std::filesystem::path assetsSrc = projPathFs / "assets";
        if (std::filesystem::exists(assetsSrc)) {
            try {
                copyDir(assetsSrc, outPathFs / "assets");
            } catch (const std::exception& e) {
                std::cerr << "[BuildSystem] Warning: Failed to copy assets: " << e.what() << std::endl;
            }
        }

        // 7. Copy scenes folder
        std::filesystem::path scenesSrc = projPathFs / "scenes";
        if (std::filesystem::exists(scenesSrc)) {
            try {
                copyDir(scenesSrc, outPathFs / "scenes");
            } catch (const std::exception& e) {
                std::cerr << "[BuildSystem] Warning: Failed to copy scenes: " << e.what() << std::endl;
            }
        }

        // 8. Copy project.settings
        std::filesystem::path settingsSrc = projPathFs / "project.settings";
        if (std::filesystem::exists(settingsSrc)) {
            try {
                std::filesystem::copy_file(settingsSrc, outPathFs / "project.settings", std::filesystem::copy_options::overwrite_existing);
            } catch (const std::exception& e) {
                std::cerr << "[BuildSystem] Warning: Failed to copy project.settings: " << e.what() << std::endl;
            }
        }

        // 9. Compile scripts
        std::filesystem::path scriptsCMake = projPathFs / "scripts" / "CMakeLists.txt";
        if (std::filesystem::exists(scriptsCMake)) {
            int compileResult = compileScripts(projectPath);
            if (compileResult != 0) {
                std::cerr << "[BuildSystem] [ERROR] Script compilation failed during game build." << std::endl;
                return compileResult;
            }
        }

        // 10. Copy compiled DLLs from project bin/
        std::filesystem::path binSrc = projPathFs / "bin";
        if (std::filesystem::exists(binSrc)) {
            try {
                std::filesystem::create_directories(outPathFs / "bin");
                for (const auto& entry : std::filesystem::directory_iterator(binSrc)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                        std::filesystem::copy_file(entry.path(), outPathFs / "bin" / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[BuildSystem] Warning: Failed to copy dlls: " << e.what() << std::endl;
            }
        }

        std::cout << "[BuildSystem] Game packaged successfully in C++." << std::endl;
        return 0;
    }

    void Application::run() {
        onStart();

        while (running && !renderer->shouldClose()) {
            glfwPollEvents();

            if (editorMode.pendingPlay) {
                editorMode.pendingPlay = false;
                if (Scene* currentScene = sceneManager.getCurrentScene()) {
                    currentScene->saveToFile("assets/scenes/.play_temp.json");
                }
                editorMode.isPlaying = true;
                editorMode.flyMode = true;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            if (editorMode.pendingStop) {
                editorMode.pendingStop = false;
                editorMode.isPlaying = false;
                editorMode.flyMode = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                if (Scene* currentScene = sceneManager.getCurrentScene()) {
                    currentScene->loadFromFile("assets/scenes/.play_temp.json");
                    try {
                        std::filesystem::remove("assets/scenes/.play_temp.json");
                    } catch (...) {}
                }
            }

            float dt = renderer->getDeltaTime();

            // Run user update callback
            onUpdate(dt);

            // Tick scenes and ECS systems
            sceneManager.update(dt);
            systemManager.updateAll(dt);
            if (!editorMode.isPlaying && config.enableEditor) {
                systemManager.updateEditorAll(dt);
            }

            if (uiSystem) {
                uiSystem->setEditorActive(config.enableEditor);
                uiSystem->setPlaying(editorMode.isPlaying);
            }

            if (editorUI) {
                if (config.enableEditor) {
                    editorUI->beginFrame();

                    // Draw Game UI canvas first so it renders beneath editor UI panels and windows
                    if (uiSystem) {
                        uiSystem->draw();
                    }

                    // Draw ImGui editor panels second (menu bar, inspector, floating editor windows)
                    editorUI->drawPanels();

                    // Draw active system debug overlays (e.g. AStar path debug lines)
                    systemManager.renderDebugAll();

                    renderSystem->drawFrame([this](VkCommandBuffer cmd) {
                        editorUI->render(cmd);
                    });
                } else {
                    // Standalone mode: draw viewport and Game UI fullscreen
                    editorUI->beginFrame();
                    if (uiSystem) {
                        uiSystem->draw();
                    }

                    renderSystem->drawFrame([this](VkCommandBuffer cmd) {
                        editorUI->render(cmd);
                    });
                }
            } else {
                // Fallback: draw viewport fullscreen with no UI overlay if editorUI is somehow null
                renderSystem->drawFrame();
            }
        }

        onShutdown();
    }

    void Application::quit() {
        running = false;
    }

} // namespace Engine
