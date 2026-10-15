#pragma once
#include "EntityManager.hpp"
#include "ComponentStorage.hpp"
#include "core/JobSystem.hpp"
#include <unordered_map>
#include <typeindex>
#include <functional>
#include <vector>
#include <array>
#include <memory>
#include <mutex>
#include <iostream>

#include "core/EngineAPI.hpp"
#include "profiler/Profiler.hpp"

class Registry;

/**
 * @class EntityCommandBuffer
 * @brief Thread-safe recorder for entity and component mutations for safe deferred playback.
 * Prevents iterator invalidation during View iterations or multithreaded system passes.
 */
class EntityCommandBuffer {
public:
    EntityCommandBuffer() = default;

    /**
     * @brief Defers destruction of an entity.
     * @param e Entity to destroy.
     */
    void destroyEntity(Entity e);

    /**
     * @brief Defers addition or replacement of a component on an entity.
     */
    template<typename T>
    void addComponent(Entity e, T comp);

    /**
     * @brief Defers removal of a component from an entity.
     */
    template<typename T>
    void removeComponent(Entity e);

    /**
     * @brief Defers toggling active state of an entity.
     */
    void setActive(Entity e, bool active);

    /**
     * @brief Plays back all recorded commands against the target registry.
     * Guarantees all recorded commands are evaluated without discarding subsequent commands if an earlier one throws.
     * @param reg Target registry.
     * @param throwOnError If true, rethrows the first captured error after completing all commands.
     * @param outErrors Optional vector pointer to collect any captured exceptions.
     * @return Number of successfully executed commands.
     */
    size_t execute(Registry& reg, bool throwOnError = false, std::vector<std::exception_ptr>* outErrors = nullptr);

    /**
     * @brief Clears pending commands without executing.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        commands.clear();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return commands.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return commands.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::function<void(Registry&)>> commands;
};

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
        if (!entities.isValid(e)) {
            return;
        }

        ComponentMask mask = entities.getMask(e);
        for (std::size_t id = 0; id < MAX_COMPONENTS; ++id) {
            if (!mask.test(id) || !storages[id]) {
                continue;
            }

            storages[id]->removeEntity(e);

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
            destroy(entities.getEntity(id));
        }
        for (size_t i = 0; i < MAX_COMPONENTS; ++i) {
            storages[i].reset();
        }
    }

    /**
     * @brief Checks if an entity is alive and valid.
     * @param e Entity to check.
     * @return True if valid and handle generation matches, false otherwise.
     */
    bool isValid(Entity e) const {
        PROFILE_ECS("Registry::isValid");
        return entities.isValid(e);
    }

    /**
     * @brief Retrieves the active Entity handle for a given ID.
     * @param id Entity ID.
     * @return Entity handle with matching generation.
     */
    Entity getEntity(uint32_t id) const {
        return entities.getEntity(id);
    }

    /**
     * @brief Sets the active state of an entity.
     * @param e Target entity.
     * @param active Whether the entity is active.
     */
    void setActive(Entity e, bool active) {
        entities.setActive(e, active);
    }

    /**
     * @brief Checks if an entity is active.
     * @param e Target entity.
     * @return True if entity is active and valid.
     */
    bool isActive(Entity e) const {
        return entities.isActive(e);
    }

    /**
     * @brief Helper to get compile-time static cached component type ID.
     */
    template<typename T>
    static std::size_t getComponentTypeId() {
        static const std::size_t id = registerComponentType(typeid(T));
        return id;
    }

    /**
     * @brief Enables or disables a component on an entity by toggling its mask bit.
     * When disabled, the component remains in storage but is hidden from views.
     * @tparam T Component type.
     * @param e Target entity.
     * @param enabled Active state flag.
     */
    template<typename T>
    void setComponentEnabled(Entity e, bool enabled) {
        if (!has<T>(e)) return;
        std::size_t id = getComponentTypeId<T>();
        if (id < MAX_COMPONENTS && entities.isValid(e)) {
            entities.getMask(e).set(id, enabled);
        }
    }

    /**
     * @brief Checks if a component is enabled on an entity.
     * @tparam T Component type.
     * @param e Target entity.
     * @return True if entity has the component and its mask bit is set.
     */
    template<typename T>
    bool isComponentEnabled(Entity e) const {
        if (!has<T>(e)) return false;
        std::size_t id = getComponentTypeId<T>();
        if (id >= MAX_COMPONENTS || !entities.isValid(e)) return false;
        return entities.getMask(e).test(id);
    }

    /**
     * @brief Dynamic overload to enable or disable a component by runtime ID.
     */
    void setComponentEnabled(Entity e, std::size_t componentId, bool enabled) {
        if (componentId < MAX_COMPONENTS && entities.isValid(e)) {
            entities.getMask(e).set(componentId, enabled);
        }
    }

    /**
     * @brief Dynamic overload to check if a component is enabled by runtime ID.
     */
    bool isComponentEnabled(Entity e, std::size_t componentId) const {
        if (componentId < MAX_COMPONENTS && entities.isValid(e)) {
            return entities.getMask(e).test(componentId);
        }
        return false;
    }

    /**
     * @brief Exposes the component mask for a given entity.
     */
    /**
     * @brief Exposes the component mask for a given entity. Throws if invalid or stale.
     */
    ComponentMask& getMask(Entity e) {
        return entities.getMask(e);
    }

    /**
     * @brief Exposes the const component mask for a given entity. Throws if invalid or stale.
     */
    const ComponentMask& getMask(Entity e) const {
        return entities.getMask(e);
    }

    /**
     * @brief Non-throwing pointer lookup for component mask. Returns nullptr if stale/invalid.
     */
    ComponentMask* tryGetMask(Entity e) {
        return entities.tryGetMask(e);
    }

    /**
     * @brief Const non-throwing pointer lookup for component mask. Returns nullptr if stale/invalid.
     */
    const ComponentMask* tryGetMask(Entity e) const {
        return entities.tryGetMask(e);
    }

    /**
     * @brief Exposes the vector of currently alive entity IDs.
     */
    const std::vector<uint32_t>& getAlive() const {
        return entities.getAlive();
    }

    /**
     * @brief Emplaces a component on an entity. Throws if entity handle is stale or invalid.
     * @tparam T Component type to emplace.
     * @param e Entity to emplace component on.
     * @param comp Component instance to add.
     * @return Reference to the emplaced component.
     */
    template<typename T>
    T& emplace(Entity e, T&& comp) {
        if (!entities.isValid(e)) {
            throw std::runtime_error("Registry::emplace: Attempted to emplace component on invalid or stale entity handle!");
        }
        ensureStorage<T>();
        getStorage<T>()->add(e, std::forward<T>(comp));

        auto id = getComponentTypeId<T>();
        entities.getMask(e).set(id);

        if (componentAddedCallbacks.find(id) != componentAddedCallbacks.end()) {
            for (auto& cb : componentAddedCallbacks[id]) cb(e);
        }

        return getStorage<T>()->get(e);
    }

    /**
     * @brief Emplaces or replaces a component on an entity. Throws if entity handle is stale or invalid.
     */
    template<typename T>
    T& emplace_or_replace(Entity e, T&& comp) {
        return emplace<T>(e, std::forward<T>(comp));
    }

    /**
     * @brief Removes a component of type T from an entity.
     * @tparam T Component type to remove.
     * @param e Target entity.
     */
    template<typename T>
    void remove(Entity e) {
        if (!entities.isValid(e)) return;
        auto* storage = getStorage<T>();
        if (storage && storage->has(e)) {
            storage->remove(e);
            auto id = getComponentTypeId<T>();
            entities.getMask(e).reset(id);

            if (componentRemovedCallbacks.find(id) != componentRemovedCallbacks.end()) {
                for (auto& cb : componentRemovedCallbacks[id]) cb(e);
            }
        }
    }

    /**
     * @brief Retrieves a pointer to a component of type T on an entity. Returns nullptr if stale or not found.
     * @tparam T Component type.
     * @param e Target entity.
     * @return Pointer to the component, or nullptr if not found.
     */
    template<typename T>
    T* get(Entity e) {
        if (!entities.isValid(e)) return nullptr;
        auto* storage = getStorage<T>();
        if (!storage) return nullptr;
        return storage->tryGet(e);
    }

    /**
     * @brief Const overload: retrieves const pointer to a component of type T on an entity. Returns nullptr if stale or not found.
     * @tparam T Component type.
     * @param e Target entity.
     * @return Const pointer to the component, or nullptr if not found.
     */
    template<typename T>
    const T* get(Entity e) const {
        if (!entities.isValid(e)) return nullptr;
        const auto* storage = getStorage<T>();
        if (!storage) return nullptr;
        return storage->tryGet(e);
    }

    /**
     * @brief Non-throwing pointer lookup for component of type T on an entity. Returns nullptr if stale or missing.
     * @tparam T Component type.
     * @param e Target entity.
     * @return Pointer or nullptr.
     */
    template<typename T>
    T* tryGet(Entity e) {
        return get<T>(e);
    }

    /**
     * @brief Const non-throwing pointer lookup for component of type T on an entity. Returns nullptr if stale or missing.
     * @tparam T Component type.
     * @param e Target entity.
     * @return Const pointer or nullptr.
     */
    template<typename T>
    const T* tryGet(Entity e) const {
        return get<T>(e);
    }

    /**
     * @brief Retrieves a reference to a component of type T on an entity. Throws if stale or missing.
     * @tparam T Component type.
     * @param e Target entity.
     * @return Reference to the component.
     */
    template<typename T>
    T& getRef(Entity e) {
        T* ptr = get<T>(e);
        if (!ptr) throw std::runtime_error("Registry::getRef: Component missing or entity handle is invalid/stale!");
        return *ptr;
    }

    /**
     * @brief Const overload: retrieves const reference to component of type T on an entity. Throws if stale or missing.
     * @tparam T Component type.
     * @param e Target entity.
     * @return Const reference to the component.
     */
    template<typename T>
    const T& getRef(Entity e) const {
        const T* ptr = get<T>(e);
        if (!ptr) throw std::runtime_error("Registry::getRef: Component missing or entity handle is invalid/stale!");
        return *ptr;
    }

    /**
     * @brief Returns the total count of currently alive entities.
     */
    size_t getAliveCount() const {
        return entities.getAliveCount();
    }

    /**
     * @brief Pre-allocates entity pool capacity.
     */
    void reserve(size_t capacity) {
        entities.reserve(capacity);
    }

    /**
     * @brief Checks if an entity possesses a component of type T. Returns false if stale or invalid.
     * @tparam T Component type.
     * @param e Target entity.
     * @return True if component exists and entity is valid, false otherwise.
     */
    template<typename T>
    bool has(Entity e) const {
        if (!entities.isValid(e)) return false;
        std::size_t id = getComponentTypeId<T>();
        if (id >= MAX_COMPONENTS || !storages[id]) return false;
        return static_cast<const ComponentStorage<T>*>(storages[id].get())->has(e);
    }

    /**
     * @brief Accesses the deferred entity command buffer.
     */
    EntityCommandBuffer& getCommandBuffer() {
        return m_commandBuffer;
    }

    /**
     * @brief Flushes deferred commands against this registry.
     * @param throwOnError If true, rethrows first exception encountered after evaluating all commands.
     * @param outErrors Optional output vector collecting captured errors.
     * @return Number of successfully executed commands.
     */
    size_t flushCommandBuffer(bool throwOnError = false, std::vector<std::exception_ptr>* outErrors = nullptr) {
        return m_commandBuffer.execute(*this, throwOnError, outErrors);
    }

    /**
     * @brief Subscribes a callback to when a component of type T is added.
     * @tparam T Component type.
     * @param cb Callback function.
     */
    template<typename T>
    void subscribeToAdded(ComponentAddedCallback cb) {
        auto id = getComponentTypeId<T>();
        componentAddedCallbacks[id].push_back(cb);
    }

    /**
     * @brief Subscribes a callback to when a component of type T is removed.
     * @tparam T Component type.
     * @param cb Callback function.
     */
    template<typename T>
    void subscribeToRemoved(ComponentRemovedCallback cb) {
        auto id = getComponentTypeId<T>();
        componentRemovedCallbacks[id].push_back(cb);
    }

    /**
     * @class View
     * @brief Bitmask-accelerated zero-allocation filtered view over entities.
     * @tparam Components Component filter list.
     */
    template<typename... Components>
    class View {
    public:
        /**
         * @brief Construct a new View object.
         * @param reg Registry creating this view.
         * @param sm Smallest storage containing entities to filter.
         * @param targetMask Bitmask of required components.
         */
        View(Registry& reg, IStorage* sm, ComponentMask targetMask)
            : registry(reg), smallest(sm), mask(targetMask) {}

        /**
         * @struct Iterator
         * @brief Bitmask-accelerated iterator over matching entities.
         */
        struct Iterator {
            Registry& registry;
            const std::vector<Entity>& entities;
            size_t index;
            ComponentMask mask;

            /** @brief Helper to advance iterator to next matching entity using bitwise &. */
            void advance() {
                while (index < entities.size()) {
                    Entity e = entities[index];
                    if (e.getId() != Entity::INVALID_ENTITY && registry.isValid(e) && registry.isActive(e)) {
                        if ((registry.getMask(e) & mask) == mask) {
                            break;
                        }
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
            Iterator it{ registry, smallest->getEntities(), 0, mask };
            it.advance();
            return it;
        }

        /** @brief Returns ending view iterator. */
        Iterator end() {
            static const std::vector<Entity> empty;
            const std::vector<Entity>& ents = smallest ? smallest->getEntities() : empty;
            return Iterator{ registry, ents, ents.size(), mask };
        }

        /**
         * @brief Direct lambda evaluation over all matching entities without iterator construction.
         * @tparam Func Invocable with signature void(Entity, Components&...)
         */
        template<typename Func>
        void each(Func&& func) {
            if (!smallest) return;
            const auto& ents = smallest->getEntities();
            for (size_t i = 0; i < ents.size(); ++i) {
                Entity e = ents[i];
                if (e.getId() != Entity::INVALID_ENTITY && registry.isValid(e) && registry.isActive(e)) {
                    if ((registry.getMask(e) & mask) == mask) {
                        func(e, *registry.get<Components>(e)...);
                    }
                }
            }
        }

        /**
         * @brief Multi-threaded execution across worker threads using JobSystem.
         * @param jobSystem Active JobSystem reference.
         * @param func Invocable with signature void(Entity, Components&...).
         * @param chunkSize Iteration batch size. If <= 0, automatically computed.
         */
        template<typename Func>
        void eachParallel(Engine::JobSystem& jobSystem, Func&& func, int chunkSize = 0) {
            if (!smallest) return;
            const auto& ents = smallest->getEntities();
            int count = static_cast<int>(ents.size());
            if (count == 0) return;

            jobSystem.parallelFor(count, [&](int i) {
                Entity e = ents[i];
                if (e.getId() != Entity::INVALID_ENTITY && registry.isValid(e) && registry.isActive(e)) {
                    if ((registry.getMask(e) & mask) == mask) {
                        func(e, *registry.get<Components>(e)...);
                    }
                }
            }, chunkSize);
        }

    private:
        Registry& registry;
        IStorage* smallest;
        ComponentMask mask;
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
        ComponentMask mask;
        (mask.set(getComponentTypeId<Components>()), ...);
        return View<Components...>(*this, smallest, mask);
    }


private:
    /** @brief Entity lifetime manager. */
    EntityManager entities;
    /** @brief Array of component storage classes indexed by component ID for O(1) access. */
    std::array<std::unique_ptr<IStorage>, MAX_COMPONENTS> storages;
    /** @brief Callbacks for component insertions. */
    std::unordered_map<std::size_t, std::vector<ComponentAddedCallback>> componentAddedCallbacks;
    /** @brief Callbacks for component removals. */
    std::unordered_map<std::size_t, std::vector<ComponentRemovedCallback>> componentRemovedCallbacks;
    /** @brief Built-in thread-safe deferred entity command buffer. */
    EntityCommandBuffer m_commandBuffer;


    /**
     * @brief Ensures a component storage exists.
     * @tparam T Component type.
     */
    template<typename T>
    void ensureStorage() {
        std::size_t id = getComponentTypeId<T>();
        if (id < MAX_COMPONENTS && !storages[id]) {
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
        std::size_t id = getComponentTypeId<T>();
        if (id >= MAX_COMPONENTS) return nullptr;
        return static_cast<ComponentStorage<T>*>(storages[id].get());
    }

    /**
     * @brief Const overload: retrieves storage class for component type.
     * @tparam T Component type.
     * @return Const typed storage pointer, or nullptr if none.
     */
    template<typename T>
    const ComponentStorage<T>* getStorage() const {
        std::size_t id = getComponentTypeId<T>();
        if (id >= MAX_COMPONENTS) return nullptr;
        return static_cast<const ComponentStorage<T>*>(storages[id].get());
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

inline void EntityCommandBuffer::destroyEntity(Entity e) {
    std::lock_guard<std::mutex> lock(m_mutex);
    commands.push_back([e](Registry& reg) {
        reg.destroy(e);
    });
}

template<typename T>
inline void EntityCommandBuffer::addComponent(Entity e, T comp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    commands.push_back([e, c = std::move(comp)](Registry& reg) mutable {
        reg.emplace_or_replace<T>(e, std::move(c));
    });
}

template<typename T>
inline void EntityCommandBuffer::removeComponent(Entity e) {
    std::lock_guard<std::mutex> lock(m_mutex);
    commands.push_back([e](Registry& reg) {
        reg.remove<T>(e);
    });
}

inline void EntityCommandBuffer::setActive(Entity e, bool active) {
    std::lock_guard<std::mutex> lock(m_mutex);
    commands.push_back([e, active](Registry& reg) {
        reg.setActive(e, active);
    });
}

inline size_t EntityCommandBuffer::execute(Registry& reg, bool throwOnError, std::vector<std::exception_ptr>* outErrors) {
    std::vector<std::function<void(Registry&)>> cmds;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cmds = std::move(commands);
        commands.clear();
    }

    size_t successCount = 0;
    std::exception_ptr firstError = nullptr;

    for (auto& cmd : cmds) {
        try {
            cmd(reg);
            ++successCount;
        } catch (...) {
            std::exception_ptr eptr = std::current_exception();
            if (!firstError) {
                firstError = eptr;
            }
            if (outErrors) {
                outErrors->push_back(eptr);
            } else {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& e) {
                    std::cerr << "[EntityCommandBuffer] Command execution warning: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[EntityCommandBuffer] Command execution warning: unknown exception." << std::endl;
                }
            }
        }
    }

    if (throwOnError && firstError) {
        std::rethrow_exception(firstError);
    }

    return successCount;
}


