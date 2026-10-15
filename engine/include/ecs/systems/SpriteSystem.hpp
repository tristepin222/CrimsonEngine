#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "ecs/components/SpriteRenderer.hpp"

namespace Engine {

    /**
     * @class SpriteSystem
     * @brief System that drives SpriteRenderer components.
     *
     * For every entity with a SpriteRenderer the system:
     *  1. Creates (once) a shared unit quad Mesh — 1m × 1m, facing +Z.
     *  2. Adds a Mesh component pointing to that quad.
     *  3. Creates a dedicated Material using the "sprite" shader (transparent alpha-blend).
     *  4. Each frame, syncs colour, texture, and sortOrder to the material / transform.
     *
     * Flip is passed through the push constants (scale/fade are repurposed as flipX/flipY)
     * so no mesh rebuild is required when the user toggles flipX or flipY.
     *
     * The existing RenderSystem draws the Mesh+Material pair automatically — no changes
     * to the render pipeline are required.
     */
    class ENGINE_API SpriteSystem : public System {
    public:
        const char* getName() const override { return "SpriteSystem"; }
        /**
         * @brief Construct a new Sprite System.
         * @param reg   Reference to the active ECS Registry.
         * @param rend  Reference to the active VulkanRenderer.
         */
        SpriteSystem(Registry& reg, VulkanRenderer& rend);

        /**
         * @brief Per-frame update: synchronises all SpriteRenderer components with their
         *        Mesh and Material counterparts.
         * @param dt Delta frame time (unused).
         */
        void update(float dt) override;

    private:
        Registry&       registry;
        VulkanRenderer& renderer;

        /** @brief Shared quad mesh ID (reused across all sprites). 0 = not yet created. */
        uint32_t m_quadMeshId = 0;

        /**
         * @brief Ensures the shared unit quad mesh exists and is uploaded to the GPU.
         * @return The mesh ID of the shared quad.
         */
        uint32_t getOrCreateQuadMesh();

        /**
         * @brief Performs initial setup for a newly-seen SpriteRenderer entity.
         * @param entity   The entity to set up.
         * @param sprite   The SpriteRenderer component to initialise.
         */
        void setupSprite(Entity entity, SpriteRenderer& sprite);

        /**
         * @brief Syncs a SpriteRenderer component's colour, texture, and sort order to its Material.
         * @param entity  The entity owning the sprite.
         * @param sprite  The SpriteRenderer component.
         */
        void syncSprite(Entity entity, SpriteRenderer& sprite);
    };

} // namespace Engine
