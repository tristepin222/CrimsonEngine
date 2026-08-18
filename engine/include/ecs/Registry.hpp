#pragma once
#include "EntityManager.hpp"
#include "ComponentStorage.hpp"
#include <unordered_map>
#include <typeindex>
#include <functional>
#include <vector>
#include <memory>

#include "core/EngineAPI.hpp"
#include "profiler/Profiler.hpp"

// Simple runtime component type id registry
/**
 * @brief Retrieves the global map of component types to unique numeric IDs.
 * @return Reference to the type index map.
 */
ENGINE_API std::unordered_map<std::type_index, std::size_t>& g_componentTypeMap();

/**
 * @brief Registers or retrieves the unique ID for a component type.
 * @param idx The type index.
 * @return The unique ID of the component type.
 */
ENGINE_API std::size_t registerComponentType(std::type_index idx);

/**
 * @class Registry
 * @brief Coordinates entities and components, providing creation, removal, lookup, and views.
 */
class Registry {
public:
    /**
     * @brief Construct a new Registry object.
     */
    Registry() = default;

    /** @brief Callback function type invoked when a component is added. */
    using ComponentAddedCallback = std::function<void(Entity)>;
    /** @brief Callback function type invoked when a component is removed. */
    using ComponentRemovedCallback = std::function<void(Entity)>;

    /**
     * @brief Creates a new entity.
     * @return The created entity.
     */
    Entity create() { return entities.create(); }
    
    /**
     * @brief Destroys an entity, removing all its components.
     * @param e Entity to destroy.
     */
    void destroy(Entity e) {
        if (e.getId() == Entity::INVALID_ENTITY) {
            return;
        }

        ComponentMask mask = entities.getMask(e);
        for (auto& [id, storage] : storages) {
            if (!mask.test(id)) {
                continue;
            }

            storage->removeEntity(e);

            auto callbacksIt = componentRemovedCallbacks.find(id);
            if (callbacksIt != componentRemovedCallbacks.end()) {
                for (auto& cb : callbacksIt->second) {
                    cb(e);
                }
            }
        }

        entities.destroy(e);
    }

    /**
     * @brief Destroys all entities and clears all component storages.
     */
    void clear() {
        auto aliveCopy = entities.getAlive();
        for (auto id : aliveCopy) {
            destroy(Entity(id));
        }
        storages.clear();
    }

    /**
     * @brief Checks if an entity is alive and valid.
     * @param e Entity to check.
     * @return True if valid, false otherwise.
     */
    bool isValid(Entity e) const {
        PROFILE_SCOPE("Registry::isValid");
        if (e.getId() == Entity::INVALID_ENTITY) return false;
        const auto& alive = entities.getAlive();
        return std::find(alive.begin(), alive.end(), e.getId()) != alive.end();
    }

    /**
     * @brief Exposes the vector of currently alive entity IDs.
     */
    const std::vector<uint32_t>& getAlive() const {
        return entities.getAlive();
    }

    /**
     * @brief Emplaces a component on an entity.
     * @tparam T Component type to emplace.
     * @param e Entity to emplace component on.
     * @param comp Component instance to add.
     * @return Reference to the emplaced component.
     */
    template<typename T>
    T& emplace(Entity e, T&& comp) {
        ensureStorage<T>();
        getStorage<T>()->add(e, std::forward<T>(comp));

        auto id = registerComponentType(typeid(T));
        entities.getMask(e).set(id);

        if (componentAddedCallbacks.find(id) != componentAddedCallbacks.end()) {
            for (auto& cb : componentAddedCallbacks[id]) cb(e);
        }

        return getStorage<T>()->get(e);
    }

    /**
     * @brief Emplaces or replaces a component on an entity.
     */
    template<typename T>
    T& emplace_or_replace(Entity e, T&& comp) {
        if (has<T>(e)) {
            remove<T>(e);
        }
        return emplace<T>(e, std::forward<T>(comp));
    }

    /**
     * @brief Removes a component of type T from an entity.
     * @tparam T Component type to remove.
     * @param e Target entity.
     */
    template<typename T>
    void remove(Entity e) {
        auto* storage = getStorage<T>();
        if (storage && storage->has(e)) {
            storage->remove(e);
            auto id = registerComponentType(typeid(T));
            entities.getMask(e).reset(id);

            if (componentRemovedCallbacks.find(id) != componentRemovedCallbacks.end()) {
                for (auto& cb : componentRemovedCallbacks[id]) cb(e);
            }
        }
    }

    /**
     * @brief Retrieves a pointer to a component of type T on an entity.
     * @tparam T Component type.
     * @param e Target entity.
     * @return Pointer to the component, or nullptr if not found.
     */
    template<typename T>
    T* get(Entity e) {
        auto* storage = getStorage<T>();
        if (!storage || !storage->has(e)) return nullptr;
        return &storage->get(e);
    }

    /**
     * @brief Retrieves a reference to a component of type T on an entity. Throws if missing.
     * @tparam T Component type.
     * @param e Target entity.
     * @return Reference to the component.
     */
    template<typename T>
    T& getRef(Entity e) {
        T* ptr = get<T>(e);
        if (!ptr) throw std::runtime_error("Component missing!");
        return *ptr;
    }

    /**
     * @brief Checks if an entity possesses a component of type T.
     * @tparam T Component type.
     * @param e Target entity.
     * @return True if component exists, false otherwise.
     */
    template<typename T>
    bool has(Entity e) {
        auto* storage = getStorage<T>();
        return storage && storage->has(e);
    }

    /**
     * @brief Subscribes a callback to when a component of type T is added.
     * @tparam T Component type.
     * @param cb Callback function.
     */
    template<typename T>
    void subscribeToAdded(ComponentAddedCallback cb) {
        auto id = registerComponentType(typeid(T));
        componentAddedCallbacks[id].push_back(cb);
    }

    /**
     * @brief Subscribes a callback to when a component of type T is removed.
     * @tparam T Component type.
     * @param cb Callback function.
     */
    template<typename T>
    void subscribeToRemoved(ComponentRemovedCallback cb) {
        auto id = registerComponentType(typeid(T));
        componentRemovedCallbacks[id].push_back(cb);
    }

    /**
     * @class View
     * @brief Zero-allocation filtered view over entities containing a specific set of components.
     * @tparam Components Component filter list.
     */
    template<typename... Components>
    class View {
    public:
        /**
         * @brief Construct a new View object.
         * @param reg Registry creating this view.
         * @param sm Smallest storage containing entities to filter.
         */
        View(Registry& reg, IStorage* sm) : registry(reg), smallest(sm) {}

        /**
         * @struct Iterator
         * @brief Iterator over the entities matching the View filters.
         */
        struct Iterator {
            Registry& registry;
            const std::vector<Entity>& entities;
            size_t index;

            /** @brief Helper to advance iterator to next matching entity. */
            void advance() {
                while (index < entities.size()) {
                    Entity e = entities[index];
                    if (e.getId() != Entity::INVALID_ENTITY && (registry.has<Components>(e) && ...)) {
                        break;
                    }
                    index++;
                }
            }

            /** @brief Check iterator inequality. */
            bool operator!=(const Iterator& other) const { return index != other.index; }
            
            /** @brief Move iterator forward. */
            void operator++() {
                index++;
                advance();
            }

            /**
             * @brief Dereferences the iterator to a tuple containing the entity and its component references.
             * @return A tuple of (Entity, T&...).
             */
            auto operator*() const {
                Entity e = entities[index];
                return std::tuple_cat(std::make_tuple(e), std::forward_as_tuple(*registry.get<Components>(e)...));
            }
        };

        /** @brief Returns beginning view iterator. */
        Iterator begin() {
            if (!smallest) return end();
            Iterator it{ registry, smallest->getEntities(), 0 };
            it.advance();
            return it;
        }

        /** @brief Returns ending view iterator. */
        Iterator end() {
            static const std::vector<Entity> empty;
            const std::vector<Entity>& ents = smallest ? smallest->getEntities() : empty;
            return Iterator{ registry, ents, ents.size() };
        }

    private:
        Registry& registry;
        IStorage* smallest;
    };

    /**
     * @brief Generates a filtered view of entities containing the requested components.
     * @tparam Components Components to filter by.
     * @return A View object.
     */
    template<typename... Components>
    auto view() {
        ensureStorages<Components...>();
        IStorage* smallest = getSmallestStorage<Components...>();
        return View<Components...>(*this, smallest);
    }

private:
    /** @brief Entity lifetime manager. */
    EntityManager entities;
    /** @brief Map of component storage classes keyed by type ID. */
    std::unordered_map<std::size_t, std::unique_ptr<IStorage>> storages;
    /** @brief Callbacks for component insertions. */
    std::unordered_map<std::size_t, std::vector<ComponentAddedCallback>> componentAddedCallbacks;
    /** @brief Callbacks for component removals. */
    std::unordered_map<std::size_t, std::vector<ComponentRemovedCallback>> componentRemovedCallbacks;

    /**
     * @brief Ensures a component storage exists.
     * @tparam T Component type.
     */
    template<typename T>
    void ensureStorage() {
        std::size_t id = registerComponentType(typeid(T));
        if (storages.find(id) == storages.end()) {
            storages[id] = std::make_unique<ComponentStorage<T>>();
        }
    }

    /**
     * @brief Retrieves storage class for component type.
     * @tparam T Component type.
     * @return Typed storage pointer, or nullptr if none.
     */
    template<typename T>
    ComponentStorage<T>* getStorage() {
        std::size_t id = registerComponentType(typeid(T));
        auto it = storages.find(id);
        if (it == storages.end()) return nullptr;
        return static_cast<ComponentStorage<T>*>(it->second.get());
    }

    /**
     * @brief Ensures all given component type storages exist.
     * @tparam Components Pack of component types.
     */
    template<typename... Components>
    void ensureStorages() { (ensureStorage<Components>(), ...); }

    /**
     * @brief Base case for retrieving smallest storage from parameter pack.
     * @tparam T Component type.
     * @return Base storage interface.
     */
    template<typename T>
    IStorage* getSmallestStorage() { return getStorage<T>(); }

    /**
     * @brief Recursively determines smallest storage among requested components.
     * @tparam First First type.
     * @tparam Second Second type.
     * @tparam Rest Remaining types.
     * @return Base storage interface pointer of smallest storage.
     */
    template<typename First, typename Second, typename... Rest>
    IStorage* getSmallestStorage() {
        IStorage* a = getStorage<First>();
        IStorage* b = getSmallestStorage<Second, Rest...>();
        if (!a) return b;
        if (!b) return a;
        return (a->size() < b->size()) ? a : b;
    }
};
