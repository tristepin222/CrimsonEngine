#pragma once
#include <string>
#include "ecs/Entity.hpp"
#include "meta/ComponentReflection.hpp"

namespace Engine {

    /**
     * @struct PrefabComponent
     * @brief Identifies an entity instantiated from a prefab asset file.
     */
    // [ReflectClass("General/Prefab")]
    struct PrefabComponent {
        // [ReflectField]
        std::string prefabAssetPath = "";
        // [ReflectField]
        bool isRootInstance = true;
    };

} // namespace Engine
