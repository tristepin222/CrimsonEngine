#pragma once
#include "Entity.hpp"
#include <vector>
#include <array>
#include <cstdint>
#include <stdexcept>

// Base interface
/**
 * @struct IStorage
 * @brief Polymorphic base interface for all component storage types, allowing untyped manipulation of components.
 */
struct IStorage {
    /**
     * @brief Virtual destructor.
     */
    virtual ~IStorage() = default;
    /**
     * @brief Returns the number of component instances stored.
     * @return Size of the storage.
     */
    virtual size_t size() const = 0;
    /**
     * @brief Removes the component associated with the given entity.
     * @param e The entity to remove.
     */
    virtual void removeEntity(Entity e) = 0;

    /**
     * @brief Returns list of entities mapped to components.
     * @return Reference to vector of entities.
     */
    virtual const std::vector<Entity>& getEntities() const = 0;
};

/**
 * @class ComponentStorage
 * @brief High-performance Sparse Set component storage with contiguous memory layout and O(1) lookups.
 * @tparam T Type of component being stored.
 */
template<typename T>
class ComponentStorage : public IStorage {
public:
    static constexpr std::uint32_t MAX_ENTITIES = 10000;
    static constexpr std::uint32_t INVALID_INDEX = 0xFFFFFFFF;

    ComponentStorage() {
        sparse.fill(INVALID_INDEX);
    }

    /**
     * @brief Adds or replaces a component on an entity.
     * @param e The target entity.
     * @param comp The component instance.
     */
    void add(Entity e, T comp) {
        std::uint32_t id = e.getId();
        if (id >= MAX_ENTITIES) return;

        std::uint32_t denseIdx = sparse[id];
        if (denseIdx != INVALID_INDEX) {
            data[denseIdx] = std::move(comp);
            entities[denseIdx] = e;
            return;
        }

        denseIdx = static_cast<std::uint32_t>(data.size());
        sparse[id] = denseIdx;
        entities.push_back(e);
        data.push_back(std::move(comp));
    }

    /**
     * @brief Direct O(1) array lookup checking if an entity possesses this component.
     * @param e The entity to check.
     * @return True if component exists, false otherwise.
     */
    bool has(Entity e) const {
        std::uint32_t id = e.getId();
        return id < MAX_ENTITIES && sparse[id] != INVALID_INDEX && entities[sparse[id]] == e;
    }

    /**
     * @brief Removes the component from an entity using swap-removal.
     * @param e The entity to remove.
     */
    void remove(Entity e) {
        std::uint32_t id = e.getId();
        if (id >= MAX_ENTITIES || sparse[id] == INVALID_INDEX) return;

        std::uint32_t index = sparse[id];
        std::uint32_t last = static_cast<std::uint32_t>(data.size() - 1);

        if (index != last) {
            // Swap-remove to preserve contiguous dense layout
            data[index] = std::move(data[last]);
            entities[index] = entities[last];
            sparse[entities[index].getId()] = index;
        }

        data.pop_back();
        entities.pop_back();
        sparse[id] = INVALID_INDEX;
    }

    /**
     * @brief Polymorphic interface to remove entity component.
     * @param e The entity to remove.
     */
    void removeEntity(Entity e) override {
        remove(e);
    }

    /**
     * @brief Retrieves reference to component associated with an entity.
     * @param e The target entity.
     * @return Reference to component instance.
     */
    T& get(Entity e) {
        std::uint32_t id = e.getId();
        if (id >= MAX_ENTITIES || sparse[id] == INVALID_INDEX) {
            throw std::runtime_error("Component missing for target entity!");
        }
        return data[sparse[id]];
    }

    /**
     * @brief Retrieves all entities with this component.
     * @return Vector of entities.
     */
    const std::vector<Entity>& getEntities() const override {
        return entities;
    }

    /**
     * @brief Returns the size of the storage.
     * @return Element count.
     */
    size_t size() const override { return data.size(); }

    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto entityBegin() { return entities.begin(); }
    auto entityEnd() { return entities.end(); }

private:
    /** @brief Contiguous array of component instances. */
    std::vector<T> data;
    /** @brief Dense array of entities matching data vector order. */
    std::vector<Entity> entities;
    /** @brief Fixed-size sparse array mapping Entity ID -> Dense index for O(1) access. */
    std::array<std::uint32_t, MAX_ENTITIES> sparse;
};
