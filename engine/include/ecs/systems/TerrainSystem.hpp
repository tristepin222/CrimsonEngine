#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "ecs/components/TerrainComponent.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Mesh.hpp"
#include "ecs/components/Material.hpp"
#include "ecs/components/Collider.hpp"
#include <array>
#include <vector>

namespace Engine {

    /**
     * @class TerrainSystem
     * @brief Manages procedural generation, integrated chunk grid tile synchronization, frustum culling, and raycasting.
     */
    class ENGINE_API TerrainSystem : public System {
    public:
        const char* getName() const override { return "TerrainSystem"; }

        TerrainSystem(Registry& reg, VulkanRenderer& renderer);
        ~TerrainSystem() override;

        void update(float dt) override;

        /**
         * @brief Raycasts against a terrain entity surface using chunk AABB broadphase + precision refinement.
         */
        static bool raycast(
            const glm::vec3& rayOrigin,
            const glm::vec3& rayDir,
            const glm::mat4& worldMatrix,
            const TerrainComponent& terrain,
            glm::vec3& outHitWorldPos,
            glm::vec2& outHitLocalXZ
        );

        /**
         * @brief Destroys all GPU resources belonging to a terrain's chunk grid.
         */
        void destroyChunks(TerrainComponent& terrain);

        /**
         * @brief Destroys splatmap, UBO, and descriptor GPU resources for a terrain.
         */
        void destroyTerrainGPUResources(TerrainComponent& terrain);

        /**
         * @brief Synchronizes border vertices between a terrain entity and its adjacent neighbors.
         * @param registry Reference to ECS registry.
         * @param targetEntity The central terrain entity to synchronize with neighbors.
         * @param copyFromNeighborsToTarget If true, initializes target borders from existing neighbors (used when creating a new tile). If false, averages and stitches borders (used during sculpting).
         */
        static void synchronizeNeighborBorders(
            Registry& registry,
            Entity targetEntity,
            bool copyFromNeighborsToTarget = false
        );

        /**
         * @brief Extracts 6 normalized frustum planes from any view-projection matrix (perspective or orthographic).
         */
        static std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& viewProj);

        /**
         * @brief Fast frustum intersection test using p-vertex against 6 normalized planes.
         */
        static bool isAabbInFrustum(const glm::vec3& aabbMin, const glm::vec3& aabbMax, const std::array<glm::vec4, 6>& planes);

        /**
         * @brief Tests if an AABB is completely inside all 6 frustum planes (using n-vertex).
         */
        static bool isAabbFullyInFrustum(const glm::vec3& aabbMin, const glm::vec3& aabbMax, const std::array<glm::vec4, 6>& planes);

        /**
         * @brief Transforms an AABB by an affine transformation matrix using Arvo's algorithm.
         */
        static void transformAABB(const glm::mat4& m, const glm::vec3& minIn, const glm::vec3& maxIn, glm::vec3& minOut, glm::vec3& maxOut);

    private:
        Registry& registry;
        VulkanRenderer& renderer;

        struct SharedIndexBuffer {
            uint32_t resolution = 0;
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            uint32_t count = 0;
        };

        // Shared GPU index buffers cached by chunk resolution
        std::vector<SharedIndexBuffer> sharedIndexBuffers;

        void syncTerrainEntity(Entity entity, TerrainComponent& terrain);
        void syncTerrainFoliage(TerrainComponent& terrain);
        void buildProceduralGrassMesh(TerrainComponent& terrain);
        void buildProceduralGrassTexture(TerrainComponent& terrain);
        void generateChunkFoliage(TerrainComponent& terrain, uint32_t chunkIdx);
        void uploadChunkFoliageBuffer(TerrainComponent& terrain, uint32_t chunkIdx);
        void performFrustumCulling(const glm::mat4& viewProj);
    };

} // namespace Engine

