#pragma once
#include "System.hpp"
#include <memory>
#include <vector>
#include <iostream>
#include <typeinfo>


/**
 * @class SystemManager
 * @brief Manages registration and sequential updating of active ECS systems.
 */
class SystemManager {
public:
    /**
     * @brief Registers a new system with the manager.
     * @param system Shared pointer to the system to add.
     */
    void addSystem(std::shared_ptr<System> system) {
        systems.push_back(system);
    }

    /**
     * @brief Removes a system from the manager (e.g. on script plugin unload).
     * @param system Shared pointer to the system to remove.
     */
    void removeSystem(std::shared_ptr<System> system) {
        if (!system) return;
        for (auto it = systems.begin(); it != systems.end(); ) {
            if (*it == system) {
                it = systems.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Triggers the update routine on all registered systems.
     * @param dt Delta time in seconds.
     */
    void updateAll(float dt) {
        for (auto& system : systems) {
            system->update(dt);
        }
    }

    /**
     * @brief Triggers the editor update routine on all registered systems during Edit mode.
     * @param dt Delta time in seconds.
     */
    void updateEditorAll(float dt) {
        for (auto& system : systems) {
            system->onEditorUpdate(dt);
        }
    }

    /**
     * @brief Notifies all systems that a scene load has completed in the editor.
     */
    void notifySceneLoadedAll() {
        for (auto& system : systems) {
            system->onSceneLoaded();
        }
    }

    /**
     * @brief Triggers debug rendering on all registered systems.
     * Called during the ImGui rendering phase.
     */
    void renderDebugAll() {
        for (auto& system : systems) {
            system->renderDebugUI();
        }
    }



    /**
     * @brief Removes and releases all registered systems.
     * Call this before destroying Vulkan resources to ensure systems
     * (which may own GPU pipelines or descriptors) are shut down first.
     */
    void clear() {
        systems.clear();
    }

private:
    /** @brief Collection of registered systems. */
    std::vector<std::shared_ptr<System>> systems;
};
