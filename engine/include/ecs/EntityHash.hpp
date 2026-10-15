// EntityHash.hpp
#pragma once
#include "Entity.hpp"
#include <functional>

/**
 * @namespace std
 * @brief Standard library namespace.
 */
namespace std {
    /**
     * @struct hash<Entity>
     * @brief Specialization of std::hash for Entity so it can be used in std::unordered_map.
     */
    template<>
    struct hash<Entity> {
        /**
         * @brief Hash functor operator for Entity.
         * @param e The entity to hash.
         * @return The computed size_t hash.
         */
        inline std::size_t operator()(const Entity& e) const noexcept {
            std::uint64_t combined = (static_cast<std::uint64_t>(e.getId()) << 32) | e.getGeneration();
            return std::hash<std::uint64_t>{}(combined);
        }
    };
}