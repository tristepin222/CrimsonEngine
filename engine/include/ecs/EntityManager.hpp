#pragma once
#include <vector>
#include <stack>
#include <cstdint>
#include <algorithm>
#include "Entity.hpp"
#include "EntityHash.hpp"

/**
 * @class EntityManager
 * @brief Manages dynamic entity allocation, deallocation, component masks, and lifetime tracking.
 * Provides O(1) allocation, destruction, and recycling with dynamically scalable entity limits.
 */
class EntityManager {
public:
    /** @brief Initial capacity for pre-allocated entity pools. */
    static constexpr std::size_t INITIAL_CAPACITY = 4096;
    /** @brief Preserved for backward compatibility. Dynamic scaling removes hard ceilings. */
    static constexpr std::size_t MAX_ENTITIES = INITIAL_CAPACITY;

    /**
     * @brief Construct a new Entity Manager object and initializes pre-allocated buffers.
     */
    EntityManager() {
        reserve(INITIAL_CAPACITY);
    }

    /**
     * @brief Pre-allocates capacity for entity pools.
     * @param capacity Target entity capacity.
     */
    void reserve(std::size_t capacity) {
        if (capacity <= generations.size()) return;
        generations.resize(capacity, 0);
        masks.resize(capacity);
        flags.resize(capacity, 0);
        entityToAliveIndex.resize(capacity, INVALID_INDEX);
        alive.reserve(capacity);
    }

    /**
     * @brief Checks if an entity handle is valid and currently alive.
     * @param e Entity to check.
     * @return True if alive and handle generation matches, false otherwise.
     */
    bool isValid(Entity e) const {
        Entity::IdType id = e.getId();
        if (id == Entity::INVALID_ENTITY || id >= generations.size()) return false;
        return (flags[id] & FLAG_ALIVE) && (generations[id] == e.getGeneration());
    }

    /**
     * @brief Creates a new Entity, recycling free identifiers or dynamically scaling the pool.
     * @return The spawned Entity handle.
     */
    Entity create() {
        Entity::IdType id;
        if (!freeIds.empty()) {
            id = freeIds.top();
            freeIds.pop();
        } else {
            id = static_cast<Entity::IdType>(generations.size());
            growCapacity(id + 1);
        }

        flags[id] = FLAG_ALIVE | FLAG_ACTIVE;
        masks[id].reset();

        entityToAliveIndex[id] = static_cast<uint32_t>(alive.size());
        alive.push_back(id);

        return Entity(id, generations[id]);
    }

    /**
     * @brief Destroys an entity, recycling its identifier and incrementing generation in O(1).
     * @param e Entity to destroy.
     */
    void destroy(Entity e) {
        if (!isValid(e)) return;
        Entity::IdType id = e.getId();

        masks[id].reset();
        flags[id] = 0;
        generations[id]++;
        freeIds.push(id);

        // O(1) swap-remove from dense alive vector
        uint32_t idx = entityToAliveIndex[id];
        if (idx < alive.size()) {
            Entity::IdType lastId = alive.back();
            alive[idx] = lastId;
            entityToAliveIndex[lastId] = idx;
            alive.pop_back();
            entityToAliveIndex[id] = INVALID_INDEX;
        }
    }

    /**
     * @brief Retrieves the component mask of an entity.
     * @param e Entity to check.
     * @return Reference to component mask.
     */
    ComponentMask& getMask(Entity e) {
        return masks[e.getId()];
    }

    /**
     * @brief Retrieves the const component mask of an entity.
     * @param e Entity to check.
     * @return Const reference to component mask.
     */
    const ComponentMask& getMask(Entity e) const {
        return masks[e.getId()];
    }

    /**
     * @brief Retrieves list of currently active entity identifiers.
     * @return Dense list of active entity IDs.
     */
    const std::vector<Entity::IdType>& getAlive() const {
        return alive;
    }

    /**
     * @brief Returns the total count of currently alive entities.
     * @return Alive entity count.
     */
    size_t getAliveCount() const {
        return alive.size();
    }

    /**
     * @brief Retrieves the valid Entity handle for an active ID.
     * @param id The entity identifier.
     * @return Entity handle with the current generation, or INVALID_ENTITY if not alive.
     */
    Entity getEntity(Entity::IdType id) const {
        if (id >= generations.size() || !(flags[id] & FLAG_ALIVE)) return Entity(Entity::INVALID_ENTITY);
        return Entity(id, generations[id]);
    }

    /**
     * @brief Retrieves the generation counter of an entity slot.
     * @param id The entity identifier.
     * @return Current generation counter.
     */
    std::uint32_t getGeneration(Entity::IdType id) const {
        if (id >= generations.size()) return 0;
        return generations[id];
    }

    /**
     * @brief Sets the active state of an entity.
     * @param e The entity to set the active state for.
     * @param active The active state to set.
     */
    void setActive(Entity e, bool active) {
        if (isValid(e)) {
            if (active) {
                flags[e.getId()] |= FLAG_ACTIVE;
            } else {
                flags[e.getId()] &= ~FLAG_ACTIVE;
            }
        }
    }

    /**
     * @brief Checks if an entity is active.
     * @param e The entity to check.
     * @return True if the entity is alive and active, false otherwise.
     */
    bool isActive(Entity e) const {
        return isValid(e) && (flags[e.getId()] & FLAG_ACTIVE);
    }

    /**
     * @brief Returns total capacity allocated for entity slots.
     */
    size_t getCapacity() const {
        return generations.size();
    }

private:
    static constexpr uint8_t FLAG_ALIVE  = 1 << 0;
    static constexpr uint8_t FLAG_ACTIVE = 1 << 1;
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;

    void growCapacity(size_t minCap) {
        size_t newCap = std::max(minCap, generations.size() * 2);
        if (newCap < INITIAL_CAPACITY) newCap = INITIAL_CAPACITY;
        generations.resize(newCap, 0);
        masks.resize(newCap);
        flags.resize(newCap, 0);
        entityToAliveIndex.resize(newCap, INVALID_INDEX);
    }

    /** @brief Dense tracking vector of active entity IDs. */
    std::vector<Entity::IdType> alive;
    /** @brief Maps entity ID to its index in the dense alive vector for O(1) swap-remove. */
    std::vector<uint32_t> entityToAliveIndex;
    /** @brief Dynamic generation counters to invalidate stale handles. */
    std::vector<std::uint32_t> generations;
    /** @brief Bit flags (alive, active) per entity slot. */
    std::vector<uint8_t> flags;
    /** @brief Stack of available recycled identifiers. */
    std::stack<Entity::IdType> freeIds;
    /** @brief Dynamic component masks indexable by entity ID. */
    std::vector<ComponentMask> masks;
};

