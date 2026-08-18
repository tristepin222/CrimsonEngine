#pragma once
#include <vector>
#include <stack>
#include <array>
#include <algorithm>
#include "Entity.hpp"
#include "EntityHash.hpp"

/**
 * @class EntityManager
 * @brief Manages entity allocation, deallocation, component masks, and lifetime tracking.
 */
class EntityManager {
public:
    /** @brief Maximum allowed active entities. */
    static constexpr std::size_t MAX_ENTITIES = 10000;

    /**
     * @brief Construct a new Entity Manager object and initializes the free identifier pool.
     */
    EntityManager() {
        generations.fill(0);
        aliveMask.reset();
        for (Entity::IdType i = 0; i < MAX_ENTITIES; ++i)
            freeIds.push(i);
    }

    /**
     * @brief Checks if an entity handle is valid and currently alive.
     * @param e Entity to check.
     * @return True if alive and handle generation matches, false otherwise.
     */
    bool isValid(Entity e) const {
        if (e.getId() >= MAX_ENTITIES || e.getId() == Entity::INVALID_ENTITY) return false;
        return aliveMask.test(e.getId()) && generations[e.getId()] == e.getGeneration();
    }

    /**
     * @brief Creates a new Entity, populating it from the free ID pool.
     * @return The spawned Entity, or an invalid Entity if limit is reached.
     */
    Entity create() {
        if (freeIds.empty()) return Entity(Entity::INVALID_ENTITY);
        Entity::IdType id = freeIds.top();
        freeIds.pop();
        aliveMask.set(id);
        alive.push_back(id);
        masks[id].reset();
        return Entity(id, generations[id]);
    }

    /**
     * @brief Destroys an entity, recycling its identifier and incrementing generation.
     * @param e Entity to destroy.
     */
    void destroy(Entity e) {
        if (!isValid(e)) return;
        Entity::IdType id = e.getId();
        masks[id].reset();
        aliveMask.reset(id);
        generations[id]++;
        freeIds.push(id);
        alive.erase(std::remove(alive.begin(), alive.end(), id), alive.end());
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
     * @brief Retrieves list of currently active entity identifiers.
     * @return List of active entity IDs.
     */
    const std::vector<Entity::IdType>& getAlive() const {
        return alive;
    }

private:
    /** @brief Tracking vector of active entity IDs. */
    std::vector<Entity::IdType> alive;
    /** @brief Bitset marking active alive entity IDs for O(1) checks. */
    std::bitset<MAX_ENTITIES> aliveMask;
    /** @brief Array of generation counters to invalidate stale handles. */
    std::array<std::uint32_t, MAX_ENTITIES> generations;
    /** @brief Stack of available identifiers. */
    std::stack<Entity::IdType> freeIds;
    /** @brief Array of component masks indexable by entity ID. */
    std::array<ComponentMask, MAX_ENTITIES> masks;
};
