#pragma once
#include <string>
#include <vector>
#include "ecs/Entity.hpp"
#include "ecs/Registry.hpp"

class VulkanRenderer;

/**
 * @class SceneSerializer
 * @brief Serializes and deserializes ECS entities and their registered components to/from JSON files.
 */
class SceneSerializer {
public:
    /**
     * @brief Construct a new Scene Serializer object.
     * @param registry Reference to the ECS Registry.
     * @param renderer Reference to the Vulkan Renderer.
     */
    SceneSerializer(Registry& registry, VulkanRenderer& renderer);
    
    /**
     * @brief Serializes a list of entities and their components to a JSON file.
     * @param path Target filepath.
     * @param entities List of entities to save.
     * @return True if saving was successful, false otherwise.
     */
    bool serialize(const std::string& path, const std::vector<Entity>& entities);
    /**
     * @brief Deserializes entities and components from a JSON file.
     * @param path Source filepath.
     * @param outEntities Target list to fill with loaded entities.
     * @return True if loading was successful, false otherwise.
     */
    bool deserialize(const std::string& path, std::vector<Entity>& outEntities);
    /**
     * @brief Deserializes entities from a pre-loaded JSON string.
     *        Used by async loading: background thread reads the file, main thread calls this.
     * @param jsonContent Raw JSON content string.
     * @param outEntities Target list to fill with loaded entities.
     * @return True if loading was successful, false otherwise.
     */
    bool deserializeFromString(const std::string& jsonContent, std::vector<Entity>& outEntities);
    
    /**
     * @brief Serializes a single entity and all its hierarchy children to a JSON prefab file.
     * @param path Target filepath.
     * @param rootEntity The root entity of the prefab hierarchy.
     * @return True if successful, false otherwise.
     */
    bool serializePrefab(const std::string& path, Entity rootEntity);

    /**
     * @brief Deserializes a prefab file, instantiates entities, and remaps hierarchical parent-child references.
     * @param path Source prefab filepath.
     * @param loadedEntities Target list to fill with the newly spawned entities.
     * @param parentEntity Optional parent entity to attach the newly created root under.
     * @return The newly spawned root entity, or invalid entity if failed.
     */
    Entity deserializePrefab(const std::string& path, std::vector<Entity>& loadedEntities, Entity parentEntity = Entity());

private:
    /** @brief Reference to registry. */
    Registry& registry;
    /** @brief Reference to Vulkan renderer. */
    VulkanRenderer& renderer;
};

/** @brief Synchronizes all component reflections into ComponentSerializerRegistry. */
ENGINE_API void syncReflectionSerializers();
