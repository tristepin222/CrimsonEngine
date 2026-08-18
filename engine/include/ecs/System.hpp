#pragma once
#include "Entity.hpp"
#include "EntityManager.hpp"
#include <functional>
#include <vector>
#include <algorithm>

/**
 * @class System
 * @brief Base class for ECS systems that process sets of entities.
 */
class System {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~System() = default;

    /**
     * @brief Human-readable name for profiling and debugging.
     */
    virtual const char* getName() const { return "System"; }
    
    /**
     * @brief Pure virtual update function to implement system behaviors.
     * @param dt Delta time in seconds.
     */
    virtual void update(float dt) = 0;
    
    /**
     * @brief Optional rendering/debug drawing function.
     * Called during the ImGui rendering phase.
     */
    virtual void renderDebugUI() {}

    /**
     * @brief Called once when system initializes in editor mode.
     */
    virtual void onEditorStart() {}

    /**
     * @brief Called every frame when the editor is in Edit mode (!editorMode.isPlaying).
     * @param dt Delta time in seconds.
     */
    virtual void onEditorUpdate(float dt) {}

    /**
     * @brief Called when a scene finishes loading/deserializing in the editor.
     */
    virtual void onSceneLoaded() {}
    
    /**
     * @brief Adds an entity to the system tracking list.
     * @param e Entity to track.
     */
    void addEntity(Entity e) { entities.push_back(e); }
    
    /**
     * @brief Removes an entity from system tracking.
     * @param e Entity to remove.
     */
    void removeEntity(Entity e) {
        entities.erase(std::remove(entities.begin(), entities.end(), e), entities.end());
    }
protected:
    /** @brief List of entities tracked by this system. */
    std::vector<Entity> entities;
};
