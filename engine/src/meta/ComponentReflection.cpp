#include "meta/ComponentReflection.hpp"
#include <cstddef>

extern "C" ENGINE_API void registerEngineReflection();

namespace Engine {

    ComponentReflectionRegistry& ComponentReflectionRegistry::getInstance() {
        static ComponentReflectionRegistry instance;
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            registerEngineReflection();
        }
        return instance;
    }



    void ComponentReflectionRegistry::registerComponent(const ComponentReflection& refl) {
        // Sanity check: If refl or refl.name is memory-corrupted, _Mysize will be garbage (e.g. huge number or non-zero)
        // and _Ptr will point to unallocated memory (<Error reading characters of string.>)
        if (refl.name.empty() || refl.name.size() > 256 || refl.name.capacity() > 1024) {
            return;
        }
        // Verify that the first character is a valid identifier byte
        unsigned char firstChar = static_cast<unsigned char>(refl.name[0]);
        if (!std::isalnum(firstChar) && firstChar != '_') {
            return;
        }
        // Purge any corrupted/empty reflections that might have entered previously
        reflections.erase(
            std::remove_if(reflections.begin(), reflections.end(), [](const ComponentReflection& r) {
                return r.name.empty() || r.name.size() > 256;
            }),
            reflections.end()
        );
        for (auto& existing : reflections) {
            if (existing.name == refl.name) {
                // 1. Merge fields by name
                if (!refl.fields.empty()) {
                    if (existing.fields.empty()) {
                        existing.fields = refl.fields;
                    } else {
                        for (const auto& newField : refl.fields) {
                            bool found = false;
                            for (auto& oldField : existing.fields) {
                                if (oldField.name == newField.name) {
                                    oldField = newField;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                existing.fields.push_back(newField);
                            }
                        }
                    }
                }
                // 2. Merge category and display name
                if (!refl.category.empty() && refl.category != "General") existing.category = refl.category;
                if (!refl.displayName.empty()) {
                    if (existing.displayName.empty() || refl.displayName != refl.name) {
                        existing.displayName = refl.displayName;
                    }
                }
                // 3. Merge lifecycle callbacks
                if (refl.add) existing.add = refl.add;
                if (refl.has) existing.has = refl.has;
                if (refl.remove) existing.remove = refl.remove;
                if (refl.get) existing.get = refl.get;
                return;
            }
        }
        reflections.push_back(refl);
    }


    const std::vector<ComponentReflection>& ComponentReflectionRegistry::getReflections() const {
        return reflections;
    }

} // namespace Engine


