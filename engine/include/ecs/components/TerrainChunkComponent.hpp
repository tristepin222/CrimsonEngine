#pragma once
#include <glm/glm.hpp>
#include "core/EngineAPI.hpp"
#include "ecs/Entity.hpp"

namespace Engine {

    /**
     * @struct TerrainChunkComponent
     * @brief Tag and spatial metadata for an individual chunk/tile in an integrated terrain grid.
     */
    struct ENGINE_API TerrainChunkComponent {
        Entity parentTerrain{};
        uint32_t chunkX = 0;
        uint32_t chunkZ = 0;
        uint32_t startVertexX = 0;
        uint32_t startVertexZ = 0;
        uint32_t resolution = 65;
        glm::vec3 aabbMin{ 0.0f };
        glm::vec3 aabbMax{ 0.0f };
        bool isDirty = true;
    };

} // namespace Engine
