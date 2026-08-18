#pragma once
#include <cstdint>
#include <bitset>
#include <limits>
#include <functional>

/**
 * @class Entity
 * @brief Lightweight identifier representing a game object in the Entity Component System.
 */
class Entity {
public:
    /** @brief Numeric type of the entity identifier. */
    using IdType = std::uint32_t;
    /** @brief Constant representing a null or invalid entity. */
    static constexpr IdType INVALID_ENTITY = std::numeric_limits<IdType>::max();

    /**
     * @brief Construct a new, invalid Entity object.
     */
    Entity() : id(INVALID_ENTITY), generation(0) {}
    /**
     * @brief Construct a new Entity object with a given ID and generation.
     * @param i The identifier for the entity.
     * @param gen The generation counter.
     */
    explicit Entity(IdType i, std::uint32_t gen = 0) : id(i), generation(gen) {}

    /**
     * @brief Retrieve the raw identifier of the entity.
     * @return The raw ID.
     */
    IdType getId() const { return id; }

    /**
     * @brief Retrieve the generation counter of the entity handle.
     * @return The generation.
     */
    std::uint32_t getGeneration() const { return generation; }

    /**
     * @brief Check equality with another entity.
     * @param other The entity to compare with.
     * @return True if IDs and generations are identical, false otherwise.
     */
    bool operator==(const Entity& other) const { return id == other.id && generation == other.generation; }
    /**
     * @brief Check inequality with another entity.
     * @param other The entity to compare with.
     * @return True if IDs or generations differ, false otherwise.
     */
    bool operator!=(const Entity& other) const { return !(*this == other); }

    /**
     * @brief Strict weak ordering for map keys or sorting.
     */
    bool operator<(const Entity& other) const {
        if (id != other.id) return id < other.id;
        return generation < other.generation;
    }

private:
    /** @brief Unique numeric identifier. */
    IdType id;
    /** @brief Generation version to prevent dangling handle usage. */
    std::uint32_t generation = 0;
};

/** @brief Maximum number of supported component types in the ECS. */
constexpr std::size_t MAX_COMPONENTS = 64;
/** @brief Bitmask representing component registration status. */
using ComponentMask = std::bitset<MAX_COMPONENTS>;
