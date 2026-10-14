#pragma once
#include "System.hpp"
#include "Registry.hpp"
#include "core/JobSystem.hpp"
#include "profiler/Profiler.hpp"
#include <memory>
#include <vector>
#include <iostream>
#include <typeinfo>

/**
 * @class SystemManager
 * @brief Coordinates registration, staging, deferred commands, and parallel execution of ECS systems.
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
     * @brief Triggers update routine on all registered systems sequentially.
     * @param dt Delta time in seconds.
     */
    void updateAll(float dt) {
        for (auto& system : systems) {
            runSystemUpdate(system, dt);
        }
    }

    /**
     * @brief Triggers update on systems matching a specific execution stage.
     * @param stage Target execution stage.
     * @param dt Delta time in seconds.
     */
    void updateStage(SystemStage stage, float dt) {
        for (auto& system : systems) {
            if (system->getStage() == stage) {
                runSystemUpdate(system, dt);
            }
        }
    }

    /**
     * @brief Evaluates parallel-safe systems within a stage concurrently across JobSystem workers.
     * Non-parallel systems in the stage run sequentially afterwards.
     * @param stage Target stage.
     * @param dt Delta time in seconds.
     * @param jobSystem Job system reference.
     */
    void updateStageParallel(SystemStage stage, float dt, Engine::JobSystem& jobSystem) {
        std::vector<std::function<void()>> parallelJobs;
        std::vector<std::shared_ptr<System>> serialSystems;

        for (auto& system : systems) {
            if (system->getStage() == stage) {
                if (system->isParallelSafe()) {
                    parallelJobs.push_back([system, dt, this]() {
                        runSystemUpdate(system, dt);
                    });
                } else {
                    serialSystems.push_back(system);
                }
            }
        }

        if (!parallelJobs.empty()) {
            auto handle = jobSystem.pushJobs(parallelJobs);
            jobSystem.wait(handle);
        }

        for (auto& system : serialSystems) {
            runSystemUpdate(system, dt);
        }
    }

    /**
     * @brief Triggers the editor update routine on all registered systems during Edit mode.
     * @param dt Delta time in seconds.
     */
    void updateEditorAll(float dt) {
        for (auto& system : systems) {
            std::string name = system->getName();
            if (name.find("Render") != std::string::npos) {
                PROFILE_RENDERING(name);
                system->onEditorUpdate(dt);
            } else if (name.find("Physics") != std::string::npos) {
                PROFILE_PHYSICS(name);
                system->onEditorUpdate(dt);
            } else {
                PROFILE_SYSTEM(name);
                system->onEditorUpdate(dt);
            }
        }
    }

    /**
     * @brief Triggers editor update on systems matching a specific stage.
     */
    void updateStageEditor(SystemStage stage, float dt) {
        for (auto& system : systems) {
            if (system->getStage() == stage) {
                system->onEditorUpdate(dt);
            }
        }
    }

    /**
     * @brief Accesses the deferred entity command buffer.
     */
    EntityCommandBuffer& getCommandBuffer() {
        return commandBuffer;
    }

    /**
     * @brief Flushes deferred commands against the given registry.
     */
    void flushCommandBuffer(Registry& registry) {
        if (!commandBuffer.empty()) {
            commandBuffer.execute(registry);
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
     * @brief Finds and returns a registered system of type T.
     * @tparam T The system type to find.
     * @return std::shared_ptr<T> Pointer to the system if found, nullptr otherwise.
     */
    template<typename T>
    std::shared_ptr<T> getSystem() {
        for (auto& system : systems) {
            if (auto casted = std::dynamic_pointer_cast<T>(system)) {
                return casted;
            }
        }
        return nullptr;
    }

    /**
     * @brief Returns read-only list of all registered systems.
     */
    const std::vector<std::shared_ptr<System>>& getSystems() const {
        return systems;
    }

    /**
     * @brief Removes and releases all registered systems.
     */
    void clear() {
        commandBuffer.clear();
        systems.clear();
    }

private:
    void runSystemUpdate(const std::shared_ptr<System>& system, float dt) {
        std::string name = system->getName();
        if (name.find("Render") != std::string::npos) {
            PROFILE_RENDERING(name);
            system->update(dt);
        } else if (name.find("Physics") != std::string::npos) {
            PROFILE_PHYSICS(name);
            system->update(dt);
        } else {
            PROFILE_SYSTEM(name);
            system->update(dt);
        }
    }

    /** @brief Collection of registered systems. */
    std::vector<std::shared_ptr<System>> systems;
    /** @brief Deferred command buffer for safe ECS mutations. */
    EntityCommandBuffer commandBuffer;
};

