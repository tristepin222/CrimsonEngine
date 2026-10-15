#pragma once
#include "scenes/Scene.hpp"
#include <filesystem>
#include <iostream>

namespace Engine {

    /**
     * @class DefaultScene
     * @brief A generic, data-driven scene implementation that loads its structure from a JSON file.
     */
    class DefaultScene : public Scene {
    public:
        /**
         * @brief Construct a new Default Scene.
         * @param registry Reference to ECS registry.
         * @param renderer Reference to Vulkan renderer.
         * @param jsonPath Path to the scene JSON file.
         */
        DefaultScene(Registry& registry, VulkanRenderer& renderer, const std::string& jsonPath)
            : Scene(registry, renderer), scenePath(jsonPath) {
            currentPath = jsonPath;
        }

        /**
         * @brief Loads the scene entities from JSON path if the file exists.
         */
        void load() override {
            if (std::filesystem::exists(scenePath)) {
                if (loadFromFile(scenePath)) {
                    std::cout << "[DefaultScene] Loaded scene data from: " << scenePath << std::endl;
                } else {
                    std::cerr << "[DefaultScene] Failed to load scene data from: " << scenePath << std::endl;
                }
            } else {
                std::cout << "[DefaultScene] Scene file " << scenePath << " not found, starting with empty scene." << std::endl;
            }
        }

        /**
         * @brief Updates the scene simulation. Left empty as ECS systems handle state updates.
         * @param dt Elapsed frame time.
         */
        void update(float dt) override {
            // Data-driven scenes rely solely on ECS system updates.
        }

    private:
        std::string scenePath;
    };

} // namespace Engine
