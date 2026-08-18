#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "ecs/components/Tilemap.hpp"

namespace Engine {

    /**
     * @class TilemapSystem
     * @brief System that processes TilemapComponents to build dynamic meshes and generate static physics colliders.
     */
    class ENGINE_API TilemapSystem : public System {
    public:
        const char* getName() const override { return "TilemapSystem"; }
        /**
         * @brief Construct a new Tilemap System.
         * @param reg Reference to active Registry.
         * @param renderer Reference to active VulkanRenderer.
         */
        TilemapSystem(Registry& reg, VulkanRenderer& renderer);

        /**
         * @brief Updates all dirty tilemaps, rebuilding geometry and colliders.
         * @param dt Delta frame time.
         */
        void update(float dt) override;

    private:
        Registry& registry;
        VulkanRenderer& renderer;

        void rebuildTilemap(Entity entity, TilemapComponent& tilemap);
    };

} // namespace Engine
