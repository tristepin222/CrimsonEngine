#include "scenes/ComponentSerializerRegistry.hpp"
#include "scenes/SceneSerializer.hpp"

ComponentSerializerRegistry& ComponentSerializerRegistry::getInstance() {
    static ComponentSerializerRegistry instance;
    return instance;
}

void ComponentSerializerRegistry::registerComponent(const std::string& componentName, SerializerCallback serialize, DeserializerCallback deserialize) {
    if (componentName.empty()) {
        return;
    }
    for (auto& existing : registrations) {
        if (existing.componentName == componentName) {
            existing.serialize = serialize;
            existing.deserialize = deserialize;
            return;
        }
    }
    registrations.push_back({ componentName, serialize, deserialize });
}

const std::vector<ComponentSerializerRegistry::Registration>& ComponentSerializerRegistry::getRegistrations() const {
    syncReflectionSerializers();
    return registrations;
}
