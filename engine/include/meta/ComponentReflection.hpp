#pragma once
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include "ecs/Entity.hpp"
#include "ecs/Registry.hpp"
#include "core/EngineAPI.hpp"

namespace Engine {

    /**
     * @enum FieldType
     * @brief Supported field types for component reflection.
     */
    enum class FieldType {
        Float,
        Int,
        Bool,
        Vec3,
        RigidBodyType,
        Entity,
        String,
        Vec2,
        Vec4,
        Enum
    };


    /**
     * @struct ComponentField
     * @brief Reflection metadata for a single field inside a component.
     */
    struct ComponentField {
        std::string name;
        FieldType type;
        size_t offset;
        std::vector<std::string> enumOptions = {};
    };

    /**
     * @struct ComponentReflection
     * @brief Reflection metadata and ECS callbacks for a component.
     *
     * The `menuPath` field drives the "Add Component" popup hierarchy.
     * It is in the form "Category/Display Name" — for example:
     *   "Rendering & Lights/Sprite Renderer"
     * If `displayName` is empty the editor derives one from `name` at render time.
     */
    struct ComponentReflection {
        /** @brief Internal component name (derived from type, e.g. "SpriteRenderer"). */
        std::string name;
        /** @brief Top-level menu group shown in "Add Component" (e.g. "Rendering & Lights"). */
        std::string category = "General";
        /** @brief Human-readable label inside the category submenu (e.g. "Sprite Renderer").
         *         If empty the editor derives it from `name`. */
        std::string displayName;
        std::vector<ComponentField> fields;

        // Lifecycle callbacks
        std::function<void(Registry&, Entity)> add;
        std::function<bool(Registry&, Entity)> has;
        std::function<void(Registry&, Entity)> remove;
        std::function<void*(Registry&, Entity)> get;
    };

    /**
     * @class ComponentReflectionRegistry
     * @brief Central singleton managing the registered component reflections.
     */
    class ENGINE_API ComponentReflectionRegistry {
    public:
        static ComponentReflectionRegistry& getInstance();

        void registerComponent(const ComponentReflection& refl);
        const std::vector<ComponentReflection>& getReflections() const;

    private:
        ComponentReflectionRegistry() = default;
        std::vector<ComponentReflection> reflections;
    };

} // namespace Engine

// Forward declarations for type deduction specializations
enum class RigidBodyType;
struct RotationField;

namespace Engine {

    template<typename T> struct DeduceFieldType;
    template<> struct DeduceFieldType<float> { static constexpr FieldType value = FieldType::Float; };
    template<> struct DeduceFieldType<double> { static constexpr FieldType value = FieldType::Float; };
    template<> struct DeduceFieldType<int> { static constexpr FieldType value = FieldType::Float; };
    template<> struct DeduceFieldType<bool> { static constexpr FieldType value = FieldType::Bool; };
    template<> struct DeduceFieldType<glm::vec2> { static constexpr FieldType value = FieldType::Vec2; };
    template<> struct DeduceFieldType<glm::vec3> { static constexpr FieldType value = FieldType::Vec3; };
    template<> struct DeduceFieldType<RotationField> { static constexpr FieldType value = FieldType::Vec3; };
    template<> struct DeduceFieldType<glm::vec4> { static constexpr FieldType value = FieldType::Vec4; };
    template<> struct DeduceFieldType<std::string> { static constexpr FieldType value = FieldType::String; };
    template<> struct DeduceFieldType<Entity> { static constexpr FieldType value = FieldType::Entity; };
    template<> struct DeduceFieldType<RigidBodyType> { static constexpr FieldType value = FieldType::RigidBodyType; };


} // namespace Engine

#define REFLECT_FIELD(Type, FieldName) \
    refl.fields.push_back({ \
        #FieldName, \
        Engine::DeduceFieldType<std::decay_t<decltype(std::declval<Type>().FieldName)>>::value, \
        offsetof(Type, FieldName) \
    });

/**
 * @brief Unity-style 1-line macro for auto-registering custom components into the engine.
 *
 * The second argument is a **menu path** of the form "Category/Display Name".
 * Both category and display name are used to build the hierarchical "Add Component" popup
 * automatically — no manual editor code is required.
 *
 * Examples:
 *   REGISTER_COMPONENT(HealthComponent, "Gameplay/Health");
 *   REGISTER_COMPONENT(AudioSourceComponent, "Audio/Audio Source");
 *   REGISTER_COMPONENT(SomeComp, "Rendering & Lights");  // plain category, display name auto-derived
 */
// REGISTER_COMPONENT macro is deprecated. All components now register uniformly via // [ReflectClass("Category/Name")]
#define REGISTER_COMPONENT(Type, MenuPath)

#define REFLECT_COMPONENT_EXPAND(Type, MenuPath, Counter, InitFunc) \
    namespace { \
        struct AutoRegisterComp_##Counter { \
            AutoRegisterComp_##Counter() { \
                Engine::ComponentReflection refl; \
                refl.name = #Type; \
                size_t scopePos = refl.name.rfind("::"); \
                if (scopePos != std::string::npos) refl.name = refl.name.substr(scopePos + 2); \
                if (refl.name.size() > 9 && refl.name.rfind("Component") == refl.name.size() - 9) { \
                    refl.name = refl.name.substr(0, refl.name.size() - 9); \
                } \
                std::string _path = MenuPath; \
                size_t _slash = _path.rfind('/'); \
                if (_slash != std::string::npos) { \
                    refl.category    = _path.substr(0, _slash); \
                    refl.displayName = _path.substr(_slash + 1); \
                } else { \
                    refl.category    = _path; \
                    refl.displayName = ""; \
                } \
                refl.add = [](Registry& reg, Entity e) { reg.emplace<Type>(e, Type{}); }; \
                refl.has = [](Registry& reg, Entity e) { return reg.has<Type>(e); }; \
                refl.remove = [](Registry& reg, Entity e) { reg.remove<Type>(e); }; \
                refl.get = [](Registry& reg, Entity e) { return static_cast<void*>(reg.get<Type>(e)); }; \
                auto init = InitFunc; \
                init(refl); \
                Engine::ComponentReflectionRegistry::getInstance().registerComponent(refl); \
            } \
        }; \
        static AutoRegisterComp_##Counter global_autoRegComp_##Counter; \
    }

#define REFLECT_COMPONENT_CONCAT(Type, MenuPath, Counter, InitFunc) REFLECT_COMPONENT_EXPAND(Type, MenuPath, Counter, InitFunc)
#define REFLECT_COMPONENT(Type, MenuPath, InitFunc) REFLECT_COMPONENT_CONCAT(Type, MenuPath, __COUNTER__, InitFunc)










