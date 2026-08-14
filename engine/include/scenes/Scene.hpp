#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "ecs/Entity.hpp"
#include "ecs/Registry.hpp"

class VulkanRenderer;

/**
 * @class Scene
 * @brief Abstract base class representing a game scene, managing entity lifecycle and persistence.
 */
class Scene {
public:
    /**
     * @brief Construct a new Scene object.
     * @param registry Reference to the ECS Registry.
     * @param renderer Reference to the Vulkan Renderer.
     */
    Scene(Registry& registry, VulkanRenderer& renderer);
    /**
     * @brief Destroy the Scene object, unloading remaining entities.
     */
    virtual ~Scene();

    /**
     * @brief Pure virtual method called to load scene assets and entities.
     */
    virtual void load() = 0;
    /**
     * @brief Pure virtual method called every frame to update scene logic.
     * @param dt Delta time in seconds.
     */
    virtual void update(float dt) = 0;
    /**
     * @brief Method called to clean up scene state and entities.
     */
    virtual void unload();

    // Builtin default implementations of editor-called functions
    /**
     * @brief Serializes scene entities to a JSON file.
     * @param path Target filepath.
     * @return True if save was successful.
     */
    virtual bool saveToFile(const std::string& path);
    /**
     * @brief Deserializes scene entities from a JSON file.
     * @param path Source filepath.
     * @return True if load was successful.
     */
    virtual bool loadFromFile(const std::string& path);
    /**
     * @brief Helper to spawn primitive geometric entities.
     * @param primitiveType Kind of primitive (Triangle, Cube, Quad).
     * @return The spawned Entity.
     */
    virtual Entity createPrimitiveEntity(const std::string& primitiveType);
    /**
     * @brief Helper to spawn complex predefined entities (Camera, Grid).
     * @param entityType Kind of entity.
     * @return The spawned Entity.
     */
    virtual Entity createEntityOfType(const std::string& entityType);
    /**
     * @brief Duplicates an existing entity.
     * @param entity Entity to clone.
     * @return Cloned Entity.
     */
    virtual Entity duplicateEntity(Entity entity);
    /**
     * @brief Destroys an entity and removes it from scene tracking.
     * @param entity Entity to delete.
     * @return True if successfully deleted.
     */
    virtual bool deleteEntity(Entity entity);

    /**
     * @brief Instantiates a prefab asset file into the scene at a given world position.
     * @param prefabPath Path to the .prefab asset file.
     * @param position Target 3D position (defaults to origin).
     * @param parentEntity Optional parent entity to attach under.
     * @return The spawned root Entity handle.
     */
    virtual Entity instantiatePrefab(const std::string& prefabPath, const glm::vec3& position = glm::vec3(0.0f), Entity parentEntity = Entity());

    /**
     * @brief Tracks an entity under scene ownership.
     * @param entity Entity to track.
     * @return Tracked entity.
     */
    Entity trackEntity(Entity entity);
    /**
     * @brief Stops tracking an entity.
     * @param entity Entity to untrack.
     */
    void untrackEntity(Entity entity);
    /**
     * @brief Generates a unique name for an entity by appending indices if duplicate.
     * @param baseName Intended name.
     * @return Unique name.
     */
    std::string makeUniqueEntityName(const std::string& baseName) const;

protected:
    
    /**
     * @brief Locates an entity by its Name component.
     * @param name Name string to find.
     * @return Entity handle if found, invalid Entity otherwise.
     */
    Entity findEntityByName(const std::string& name) const;

    /** @brief Reference to registry. */
    Registry& registry;
    /** @brief Reference to Vulkan renderer. */
    VulkanRenderer& renderer;

private:
    /** @brief List of entities owned by this scene. */
    std::vector<Entity> ownedEntities;
};
