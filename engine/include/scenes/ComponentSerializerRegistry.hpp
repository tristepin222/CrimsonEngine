#pragma once
#include <string>
#include <vector>
#include <functional>
#include <ostream>
#include "ecs/Entity.hpp"
#include "ecs/Registry.hpp"
#include "scenes/JSONUtils.hpp"

#include "core/EngineAPI.hpp"

class VulkanRenderer;

/**
 * @class ComponentSerializerRegistry
 * @brief Singleton registry managing callbacks for component serialization and deserialization.
 */
class ENGINE_API ComponentSerializerRegistry {
public:
    /** @brief Signature for component serialization callbacks. */
    using SerializerCallback = std::function<void(Registry& registry, Entity entity, std::ostream& out, int indentLevel)>;
    /** @brief Signature for component deserialization callbacks. */
    using DeserializerCallback = std::function<void(Registry& registry, VulkanRenderer& renderer, Entity entity, const std::string& entityJson)>;

    /**
     * @struct Registration
     * @brief Links a component name to its serialization and deserialization functions.
     */
    struct Registration {
        /** @brief String identifier of the component type. */
        std::string componentName;
        /** @brief Function to serialize the component. */
        SerializerCallback serialize;
        /** @brief Function to deserialize the component. */
        DeserializerCallback deserialize;
    };

    /**
     * @brief Retrieves singleton instance of the registry.
     * @return Singleton instance.
     */
    static ComponentSerializerRegistry& getInstance();

    /**
     * @brief Registers serializer callbacks for a component type.
     * @param componentName Name of component.
     * @param serialize Serialization callback.
     * @param deserialize Deserialization callback.
     */
    /**
     * @brief Registers serializer callbacks for a component type.
     * @param serialize Serialization callback.
     * @param deserialize Deserialization callback.
     */
    void registerComponent(const std::string& componentName, SerializerCallback serialize, DeserializerCallback deserialize);


    /**
     * @brief Retrieves all registered components.
     * @return List of registrations.
     */
    const std::vector<Registration>& getRegistrations() const;

private:
    /** @brief Default constructor for singleton. */
    ComponentSerializerRegistry() = default;
    /** @brief Array of registered components. */
    std::vector<Registration> registrations;
};
