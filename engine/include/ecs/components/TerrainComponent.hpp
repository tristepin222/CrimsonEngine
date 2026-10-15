#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <cstring>
#include <glm/glm.hpp>
#include "core/EngineAPI.hpp"
#include "ecs/Entity.hpp"
#include "ecs/components/Mesh.hpp"

namespace Engine {

    /**
     * @enum TerrainBrushMode
     * @brief Sculpting mode for terrain deformation.
     */
    enum class TerrainBrushMode {
        Raise = 0,
        Lower = 1,
        Smooth = 2,
        Flatten = 3,
        Noise = 4
    };

    /**
     * @enum TerrainBrushFalloff
     * @brief Falloff curve applied across the brush radius.
     */
    enum class TerrainBrushFalloff {
        Smooth = 0, // Hermite smoothstep (C1 continuous)
        Linear = 1,
        Spherical = 2,
        Flat = 3    // Constant / sharp plateau
    };

    /**
     * @enum TerrainToolMode
     * @brief Active editor viewport tool for terrain interaction.
     */
    enum class TerrainToolMode {
        Sculpt = 0,
        PaintTexture = 1,
        PaintFoliage = 2
    };

    /**
     * @struct TerrainLayer
     * @brief Ground texture layer specification for terrain splatting.
     */
    struct TerrainLayer {
        std::string name = "Layer";
        std::string albedoPath;
        std::string normalPath;
        glm::vec4 tintColor{ 1.0f };
        float uvScale = 0.05f;       // UV frequency multiplier in world space (e.g. 0.05 = repeat every 20m)
        float roughness = 0.85f;
        float metallic = 0.0f;
    };

    /**
     * @enum DetailType
     * @brief Hybrid palette detail item type (GPU instanced grass vs spawned scene prop entity).
     */
    enum class DetailType : uint32_t {
        GrassClump = 0,    // High-performance GPU instanced vegetation (swaying grass clumps)
        PropEntity = 1     // Scene entity (Trees, Rocks, Bushes, Prefabs, Colliders)
    };

    /**
     * @enum DetailCollisionShape
     * @brief Primitive collision shape for spawned prop entities.
     */
    enum class DetailCollisionShape : uint32_t {
        None = 0,
        Box = 1,
        Sphere = 2,
        Capsule = 3
    };

    /**
     * @struct DetailPrototype
     * @brief A configurable prototype entry in the Hybrid Detail & Foliage Palette.
     */
    struct DetailPrototype {
        std::string name = "Detail Item";
        bool enabled = true;
        DetailType type = DetailType::GrassClump;

        // Assets
        std::string meshPath;        // Custom .obj / .gltf / .fbx model (or empty for procedural grass)
        std::string texturePath;     // Custom .png / .dds / .tga texture (or empty for procedural grass)
        std::string prefabPath;      // Optional .prefab / .json path for PropEntity

        // Placement rules
        float density = 5.0f;        // Relative spawn density / chance
        float scaleMin = 0.8f;
        float scaleMax = 1.3f;
        bool uniformScale = true;
        bool randomYaw = true;
        bool alignToNormal = false;  // False: upright Y-up (Trees). True: aligns to slope normal (Rocks)
        float maxSlope = 0.70f;      // Slope cutoff (0.0 flat to 1.0 vertical)
        float sinkOffset = 0.0f;     // Y offset to sink roots or rocks into ground
        glm::vec4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        int targetLayer = -1;        // -1 = All Layers (Any surface), 0 = Grass, 1 = Dirt, 2 = Rock, 3 = Sand

        // Physics & Collision for PropEntity
        DetailCollisionShape collisionShape = DetailCollisionShape::Capsule;
        glm::vec3 colliderExtents{ 0.5f, 2.0f, 0.5f }; // Box half-extents or (radius, height, radius)
        bool isStatic = true;
    };

    /**
     * @struct FoliageInstanceGPU
     * @brief GPU vertex buffer layout for instanced foliage rendering.
     * Binding 1 (VK_VERTEX_INPUT_RATE_INSTANCE).
     */
    struct FoliageInstanceGPU {
        glm::vec4 positionAndScale; // location 5: xyz = chunk-local / world position, w = uniform scale
        glm::vec4 rotationAndWind;  // location 6: x = yaw angle (radians), y = wind phase offset, z = tilt/sway factor, w = unused
        glm::vec4 color;            // location 7: rgba tint
    };

    /**
     * @struct TerrainChunk
     * @brief Internal submesh chunk data for frustum culling and fast partial GPU uploads.
     * Note: Chunks are pure internal data and NOT separate ECS entities.
     */
    struct TerrainChunk {
        uint32_t chunkX = 0;
        uint32_t chunkZ = 0;
        uint32_t startVertexX = 0;
        uint32_t startVertexZ = 0;
        glm::vec3 aabbMin{ 0.0f };
        glm::vec3 aabbMax{ 0.0f };
        glm::vec3 worldAabbMin{ 0.0f };
        glm::vec3 worldAabbMax{ 0.0f };
        Mesh mesh;
        bool isVisible = true;

        // Foliage GPU resources per chunk
        std::vector<FoliageInstanceGPU> foliageInstances;
        VkBuffer foliageBuffer = VK_NULL_HANDLE;
        VkDeviceMemory foliageBufferMemory = VK_NULL_HANDLE;
        uint32_t foliageInstanceCount = 0;
        bool foliageDirty = true;
        VkDeviceSize foliageBufferCapacity = 0;
    };

    /**
     * @struct TerrainComponent
     * @brief Represents a deformable 3D heightmap terrain entity using an integrated chunk grid architecture.
     */
    struct ENGINE_API TerrainComponent {
        // --- Terrain Dimensions & Chunk Grid Architecture ---
        float sizeX = 1000.0f;          // World width along X (meters)
        float sizeZ = 1000.0f;          // World depth along Z (meters)
        uint32_t chunkCountX = 16;      // Number of chunk tiles along X
        uint32_t chunkCountZ = 16;      // Number of chunk tiles along Z
        uint32_t chunkResolution = 65;  // Vertices per chunk edge (standard: 33, 65, 129, 2^k + 1)
        uint32_t resolutionX = 1025;    // Total grid vertices along X: chunkCountX * (chunkResolution - 1) + 1
        uint32_t resolutionZ = 1025;    // Total grid vertices along Z: chunkCountZ * (chunkResolution - 1) + 1
        float heightScale = 600.0f;     // Max vertical range / default hill height (600m like Unity)
        float uvScale = 32.0f;          // UV repeat tiling factor for textures
        bool showChunkBorders = false;  // Visual chunk boundary wireframe overlay in editor viewport
        bool autoScaleWithChunks = true;// Automatically scale sizeX/sizeZ when chunk count changes

        // 1D height array (size: resolutionX * resolutionZ)
        // Stored in local meters relative to entity transform Y
        std::vector<float> heights;

        // Internal sub-mesh chunks (NOT separate ECS entities!)
        std::vector<TerrainChunk> chunks;
        // Dirty flag per chunk tile
        std::vector<bool> chunkDirty;

        // --- Active Tool Mode ---
        TerrainToolMode toolMode = TerrainToolMode::Sculpt;

        // --- Sculpting & Brush Settings ---
        TerrainBrushMode brushMode = TerrainBrushMode::Raise;
        TerrainBrushFalloff brushFalloff = TerrainBrushFalloff::Smooth;
        float brushRadius = 10.0f;       // World-space brush radius in meters
        float brushStrength = 10.0f;     // Deformation speed (meters per second)
        float flattenTargetHeight = 0.0f;// Target elevation for Flatten brush
        bool isSculptingActive = true;   // Toggle for interactive viewport sculpting (active by default)

        // --- Texture Layer Painting & Splatmap ---
        std::array<TerrainLayer, 4> layers = {
            TerrainLayer{ "Grass", "", "", glm::vec4(0.35f, 0.65f, 0.25f, 1.0f), 0.05f, 0.80f, 0.0f },
            TerrainLayer{ "Dirt",  "", "", glm::vec4(0.55f, 0.40f, 0.25f, 1.0f), 0.05f, 0.90f, 0.0f },
            TerrainLayer{ "Rock",  "", "", glm::vec4(0.50f, 0.50f, 0.52f, 1.0f), 0.04f, 0.70f, 0.0f },
            TerrainLayer{ "Sand",  "", "", glm::vec4(0.85f, 0.75f, 0.50f, 1.0f), 0.06f, 0.95f, 0.0f }
        };
        int activeLayerIndex = 0;
        float paintBrushRadius = 15.0f;
        float paintBrushOpacity = 2.0f;
        TerrainBrushFalloff paintBrushFalloff = TerrainBrushFalloff::Smooth;
        uint32_t splatmapResolution = 512;
        std::vector<uint8_t> splatmapData; // RGBA8 (size: splatmapResolution * splatmapResolution * 4)
        bool splatmapDirty = true;
        bool layersDirty = true;           // Signals GPU UBO/textures update

        // GPU handles (managed by TerrainSystem)
        VkImage splatmapImage = VK_NULL_HANDLE;
        VkDeviceMemory splatmapMemory = VK_NULL_HANDLE;
        VkImageView splatmapView = VK_NULL_HANDLE;
        VkSampler splatmapSampler = VK_NULL_HANDLE;
        VkBuffer terrainUboBuffer = VK_NULL_HANDLE;
        VkDeviceMemory terrainUboMemory = VK_NULL_HANDLE;
        VkDescriptorSet terrainDescriptorSet = VK_NULL_HANDLE;
        bool terrainGpuInitialized = false;

        // --- GPU Foliage & Grass Instancing ---
        bool foliageEnabled = true;
        float foliageDensity = 8.0f;           // Clump density multiplier (1.0 - 20.0)
        float foliageMaxDistance = 150.0f;     // Camera distance culling limit (meters)
        float foliageScaleMin = 0.8f;          // Minimum random clump scale
        float foliageScaleMax = 1.3f;          // Maximum random clump scale
        float foliageWindStrength = 1.0f;      // Wind sway magnitude
        int foliageGrassLayer = -1;            // Layer index for grass placement (-1 = All Layers, 0 = Grass, 1 = Dirt, 2 = Rock, 3 = Sand)
        float foliageMinWeight = 0.20f;        // Minimum splatmap weight required to spawn grass (when targetLayer >= 0)
        float foliageMaxSlope = 0.70f;         // Maximum slope (0.0 flat to 1.0 vertical) allowed for grass
        glm::vec4 foliageTint{ 0.90f, 1.05f, 0.80f, 1.0f }; // Clump color tint
        std::string foliageTexturePath;        // Optional custom grass albedo texture
        std::string foliageMeshPath;           // Optional custom grass clump 3D mesh (.obj / .gltf)
        bool foliageNeedsRebuild = true;       // Flag to trigger foliage regeneration
        bool foliageDirty = false;             // Flag set when foliage is modified by brush or cleared

        // Hybrid Detail & Foliage Palette
        std::vector<DetailPrototype> detailPalette;

        void initDefaultPalette() {
            if (!detailPalette.empty()) return;
            DetailPrototype grass{};
            grass.name = "Grass Clump";
            grass.type = DetailType::GrassClump;
            grass.density = foliageDensity;
            grass.scaleMin = foliageScaleMin;
            grass.scaleMax = foliageScaleMax;
            grass.meshPath = foliageMeshPath;
            grass.texturePath = foliageTexturePath;
            grass.tint = foliageTint;
            grass.targetLayer = -1;
            detailPalette.push_back(grass);

            DetailPrototype rock{};
            rock.name = "Rock / Boulder";
            rock.type = DetailType::PropEntity;
            rock.enabled = false;
            rock.density = 0.6f;
            rock.scaleMin = 0.6f;
            rock.scaleMax = 1.8f;
            rock.alignToNormal = true;
            rock.collisionShape = DetailCollisionShape::Capsule;
            rock.colliderExtents = glm::vec3(0.8f, 1.2f, 0.8f);
            rock.sinkOffset = -0.15f;
            rock.targetLayer = -1;
            detailPalette.push_back(rock);

            DetailPrototype tree{};
            tree.name = "Tree / Prop";
            tree.type = DetailType::PropEntity;
            tree.enabled = false;
            tree.density = 0.3f;
            tree.scaleMin = 0.8f;
            tree.scaleMax = 1.4f;
            tree.alignToNormal = false;
            tree.collisionShape = DetailCollisionShape::Capsule;
            tree.colliderExtents = glm::vec3(0.5f, 4.0f, 0.5f);
            tree.sinkOffset = 0.0f;
            tree.targetLayer = -1;
            detailPalette.push_back(tree);
        }

        // Foliage Interactive Brush Parameters
        float foliageBrushRadius = 12.0f;      // Viewport brush radius in meters
        float foliageBrushDensity = 12.0f;     // Clumps flow rate / spawn density
        bool foliageBrushErase = false;        // Toggle erase mode in brush (Shift also erases)
        int foliageBrushLayerFilter = -1;      // -1 = All Layers (No restriction), 0..3 = Specific layer

        // Foliage GPU resources (managed by TerrainSystem)
        VkBuffer foliageVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory foliageVertexMemory = VK_NULL_HANDLE;
        VkBuffer foliageIndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory foliageIndexMemory = VK_NULL_HANDLE;
        uint32_t foliageIndexCount = 0;
        VkPipeline foliagePipeline = VK_NULL_HANDLE;
        VkPipelineLayout foliagePipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSet foliageDescriptorSet = VK_NULL_HANDLE;
        VkImage foliageTextureImage = VK_NULL_HANDLE;
        VkDeviceMemory foliageTextureMemory = VK_NULL_HANDLE;
        VkImageView foliageTextureView = VK_NULL_HANDLE;
        VkSampler foliageTextureSampler = VK_NULL_HANDLE;

        // --- Procedural FBM Noise Parameters ---
        int noiseSeed = 1337;
        float noiseFrequency = 0.015f;
        int noiseOctaves = 4;
        float noisePersistence = 0.5f;
        float noiseLacunarity = 2.0f;

        // --- Dirty / State flags ---
        bool isDirty = true;
        bool isMeshInitialized = false;

        // Dirty region tracking for partial uploads & normal recalculation
        bool hasPartialDirty = false;
        uint32_t dirtyMinX = 0;
        uint32_t dirtyMaxX = 0;
        uint32_t dirtyMinZ = 0;
        uint32_t dirtyMaxZ = 0;

        // Cached height min/max bounds for O(1) raycasting envelope
        float boundsMinY = 0.0f;
        float boundsMaxY = 0.0f;

        inline uint32_t getChunkStride() const {
            return chunkResolution > 1 ? (chunkResolution - 1) : 1;
        }

        void ensureGridSize() {
            if (chunkResolution < 2) chunkResolution = 2;
            if (chunkCountX < 1) chunkCountX = 1;
            if (chunkCountZ < 1) chunkCountZ = 1;
            resolutionX = chunkCountX * (chunkResolution - 1) + 1;
            resolutionZ = chunkCountZ * (chunkResolution - 1) + 1;
        }

        void markChunkDirty(uint32_t cx, uint32_t cz) {
            if (cx < chunkCountX && cz < chunkCountZ) {
                size_t idx = static_cast<size_t>(cz) * chunkCountX + cx;
                if (idx < chunkDirty.size()) {
                    chunkDirty[idx] = true;
                }
            }
            isDirty = true;
        }

        void markAllChunksDirty() {
            ensureGridSize();
            size_t totalChunks = static_cast<size_t>(chunkCountX) * chunkCountZ;
            if (chunkDirty.size() != totalChunks) {
                chunkDirty.assign(totalChunks, true);
            } else {
                std::fill(chunkDirty.begin(), chunkDirty.end(), true);
            }
            isDirty = true;
        }

        void markDirtyRegion(uint32_t minX, uint32_t maxX, uint32_t minZ, uint32_t maxZ) {
            if (!hasPartialDirty) {
                dirtyMinX = minX;
                dirtyMaxX = maxX;
                dirtyMinZ = minZ;
                dirtyMaxZ = maxZ;
                hasPartialDirty = true;
            } else {
                dirtyMinX = std::min(dirtyMinX, minX);
                dirtyMaxX = std::max(dirtyMaxX, maxX);
                dirtyMinZ = std::min(dirtyMinZ, minZ);
                dirtyMaxZ = std::max(dirtyMaxZ, maxZ);
            }
            isDirty = true;
        }

        void markFullDirty() {
            isDirty = true;
            hasPartialDirty = false;
            markAllChunksDirty();
        }

        void recalculateBounds() {
            if (heights.empty()) {
                boundsMinY = 0.0f;
                boundsMaxY = 0.0f;
                return;
            }
            boundsMinY = heights[0];
            boundsMaxY = heights[0];
            for (float h : heights) {
                if (h < boundsMinY) boundsMinY = h;
                if (h > boundsMaxY) boundsMaxY = h;
            }
        }

        void resizeGrid(uint32_t newCountX, uint32_t newCountZ, uint32_t newRes, bool autoScaleSize = true, float* outDeltaWorldX = nullptr, float* outDeltaWorldZ = nullptr) {
            newCountX = std::max(1u, newCountX);
            newCountZ = std::max(1u, newCountZ);
            newRes = std::max(2u, newRes);

            uint32_t newResX = newCountX * (newRes - 1) + 1;
            uint32_t newResZ = newCountZ * (newRes - 1) + 1;
            size_t newTotal = static_cast<size_t>(newResX) * newResZ;

            if (newCountX == chunkCountX && newCountZ == chunkCountZ && newRes == chunkResolution && heights.size() == newTotal) {
                if (outDeltaWorldX) *outDeltaWorldX = 0.0f;
                if (outDeltaWorldZ) *outDeltaWorldZ = 0.0f;
                return;
            }

            if (autoScaleSize) {
                float oldChunkSizeX = sizeX / static_cast<float>(chunkCountX > 0 ? chunkCountX : 1);
                float oldChunkSizeZ = sizeZ / static_cast<float>(chunkCountZ > 0 ? chunkCountZ : 1);
                float newSizeX = oldChunkSizeX * static_cast<float>(newCountX);
                float newSizeZ = oldChunkSizeZ * static_cast<float>(newCountZ);
                if (outDeltaWorldX) *outDeltaWorldX = 0.5f * (newSizeX - sizeX);
                if (outDeltaWorldZ) *outDeltaWorldZ = 0.5f * (newSizeZ - sizeZ);
                sizeX = newSizeX;
                sizeZ = newSizeZ;
            } else {
                if (outDeltaWorldX) *outDeltaWorldX = 0.0f;
                if (outDeltaWorldZ) *outDeltaWorldZ = 0.0f;
            }

            uint32_t oldResX = resolutionX;
            uint32_t oldResZ = resolutionZ;
            uint32_t oldChunkRes = chunkResolution;
            uint32_t oldCountX = chunkCountX;
            uint32_t oldCountZ = chunkCountZ;

            std::vector<float> newHeights(newTotal, 0.0f);

            if (!heights.empty() && heights.size() == static_cast<size_t>(oldResX) * oldResZ) {
                if (newRes == oldChunkRes && autoScaleSize) {
                    // Exact 1:1 vertex preservation along shared chunk rows/columns
                    uint32_t copyX = std::min(oldResX, newResX);
                    uint32_t copyZ = std::min(oldResZ, newResZ);
                    for (uint32_t z = 0; z < copyZ; ++z) {
                        const float* srcRow = &heights[static_cast<size_t>(z) * oldResX];
                        float* dstRow = &newHeights[static_cast<size_t>(z) * newResX];
                        std::memcpy(dstRow, srcRow, copyX * sizeof(float));
                    }
                } else if (autoScaleSize) {
                    // Resolution changed but auto-scaling chunks
                    uint32_t copyCX = std::min(oldCountX, newCountX);
                    uint32_t copyCZ = std::min(oldCountZ, newCountZ);
                    for (uint32_t cz = 0; cz < copyCZ; ++cz) {
                        for (uint32_t lz = 0; lz < newRes; ++lz) {
                            float v = static_cast<float>(lz) / static_cast<float>(newRes - 1);
                            float oldLz = v * static_cast<float>(oldChunkRes - 1);
                            uint32_t gz = cz * (newRes - 1) + lz;

                            for (uint32_t cx = 0; cx < copyCX; ++cx) {
                                for (uint32_t lx = 0; lx < newRes; ++lx) {
                                    float u = static_cast<float>(lx) / static_cast<float>(newRes - 1);
                                    float oldLx = u * static_cast<float>(oldChunkRes - 1);
                                    uint32_t gx = cx * (newRes - 1) + lx;

                                    float oldGx = static_cast<float>(cx * (oldChunkRes - 1)) + oldLx;
                                    float oldGz = static_cast<float>(cz * (oldChunkRes - 1)) + oldLz;

                                    int x0 = std::clamp(static_cast<int>(oldGx), 0, static_cast<int>(oldResX - 1));
                                    int z0 = std::clamp(static_cast<int>(oldGz), 0, static_cast<int>(oldResZ - 1));
                                    int x1 = std::min(x0 + 1, static_cast<int>(oldResX - 1));
                                    int z1 = std::min(z0 + 1, static_cast<int>(oldResZ - 1));
                                    float fx = oldGx - static_cast<float>(x0);
                                    float fz = oldGz - static_cast<float>(z0);

                                    float h00 = heights[static_cast<size_t>(z0) * oldResX + x0];
                                    float h10 = heights[static_cast<size_t>(z0) * oldResX + x1];
                                    float h01 = heights[static_cast<size_t>(z1) * oldResX + x0];
                                    float h11 = heights[static_cast<size_t>(z1) * oldResX + x1];
                                    float h0 = h00 + (h10 - h00) * fx;
                                    float h1 = h01 + (h11 - h01) * fx;
                                    newHeights[static_cast<size_t>(gz) * newResX + gx] = h0 + (h1 - h0) * fz;
                                }
                            }
                        }
                    }
                } else {
                    // Global normalized resampling across fixed world boundaries
                    for (uint32_t gz = 0; gz < newResZ; ++gz) {
                        float v = (newResZ > 1) ? static_cast<float>(gz) / static_cast<float>(newResZ - 1) : 0.0f;
                        float oldGz = v * static_cast<float>(oldResZ - 1);
                        int z0 = std::clamp(static_cast<int>(oldGz), 0, static_cast<int>(oldResZ - 1));
                        int z1 = std::min(z0 + 1, static_cast<int>(oldResZ - 1));
                        float fz = oldGz - static_cast<float>(z0);

                        for (uint32_t gx = 0; gx < newResX; ++gx) {
                            float u = (newResX > 1) ? static_cast<float>(gx) / static_cast<float>(newResX - 1) : 0.0f;
                            float oldGx = u * static_cast<float>(oldResX - 1);
                            int x0 = std::clamp(static_cast<int>(oldGx), 0, static_cast<int>(oldResX - 1));
                            int x1 = std::min(x0 + 1, static_cast<int>(oldResX - 1));
                            float fx = oldGx - static_cast<float>(x0);

                            float h00 = heights[static_cast<size_t>(z0) * oldResX + x0];
                            float h10 = heights[static_cast<size_t>(z0) * oldResX + x1];
                            float h01 = heights[static_cast<size_t>(z1) * oldResX + x0];
                            float h11 = heights[static_cast<size_t>(z1) * oldResX + x1];
                            float h0 = h00 + (h10 - h00) * fx;
                            float h1 = h01 + (h11 - h01) * fx;
                            newHeights[static_cast<size_t>(gz) * newResX + gx] = h0 + (h1 - h0) * fz;
                        }
                    }
                }
            }

            heights = std::move(newHeights);
            chunkCountX = newCountX;
            chunkCountZ = newCountZ;
            chunkResolution = newRes;
            resolutionX = newResX;
            resolutionZ = newResZ;

            recalculateBounds();
            markAllChunksDirty();
            isMeshInitialized = false;
        }

        void setChunkConfiguration(uint32_t countX, uint32_t countZ, uint32_t res) {
            resizeGrid(countX, countZ, res, autoScaleWithChunks);
        }

        // ---------------------------------------------------------------------
        // Coordinate & Height Accessors
        // ---------------------------------------------------------------------

        inline size_t getIndex(uint32_t ix, uint32_t iz) const {
            return static_cast<size_t>(iz) * resolutionX + ix;
        }

        inline float getHeight(int ix, int iz) const {
            if (heights.empty() || ix < 0 || ix >= static_cast<int>(resolutionX) ||
                iz < 0 || iz >= static_cast<int>(resolutionZ)) {
                return 0.0f;
            }
            return heights[static_cast<size_t>(iz) * resolutionX + static_cast<size_t>(ix)];
        }

        inline void setHeight(int ix, int iz, float h) {
            if (ix >= 0 && ix < static_cast<int>(resolutionX) &&
                iz >= 0 && iz < static_cast<int>(resolutionZ)) {
                heights[static_cast<size_t>(iz) * resolutionX + static_cast<size_t>(ix)] = h;
                if (h < boundsMinY) boundsMinY = h;
                if (h > boundsMaxY) boundsMaxY = h;

                uint32_t stride = getChunkStride();
                int minCX = std::clamp(std::max(0, ix - 1) / static_cast<int>(stride), 0, static_cast<int>(chunkCountX - 1));
                int maxCX = std::clamp(std::min(static_cast<int>(resolutionX - 1), ix + 1) / static_cast<int>(stride), 0, static_cast<int>(chunkCountX - 1));
                int minCZ = std::clamp(std::max(0, iz - 1) / static_cast<int>(stride), 0, static_cast<int>(chunkCountZ - 1));
                int maxCZ = std::clamp(std::min(static_cast<int>(resolutionZ - 1), iz + 1) / static_cast<int>(stride), 0, static_cast<int>(chunkCountZ - 1));

                for (int cz = minCZ; cz <= maxCZ; ++cz) {
                    for (int cx = minCX; cx <= maxCX; ++cx) {
                        markChunkDirty(static_cast<uint32_t>(cx), static_cast<uint32_t>(cz));
                    }
                }
            }
        }

        /**
         * @brief Bilinearly interpolates height at local position (localX, localZ).
         * Local origin (0, 0) is at the center of the terrain:
         * localX in [-sizeX * 0.5, sizeX * 0.5], localZ in [-sizeZ * 0.5, sizeZ * 0.5].
         */
        float getInterpolatedHeight(float localX, float localZ) const {
            if (heights.empty() || resolutionX < 2 || resolutionZ < 2) return 0.0f;

            float invSizeX = 1.0f / sizeX;
            float invSizeZ = 1.0f / sizeZ;
            float normX = (localX * invSizeX + 0.5f) * static_cast<float>(resolutionX - 1);
            float normZ = (localZ * invSizeZ + 0.5f) * static_cast<float>(resolutionZ - 1);

            int maxIX = static_cast<int>(resolutionX - 1);
            int maxIZ = static_cast<int>(resolutionZ - 1);
            int x0 = std::clamp(static_cast<int>(normX), 0, maxIX);
            int z0 = std::clamp(static_cast<int>(normZ), 0, maxIZ);
            int x1 = std::min(x0 + 1, maxIX);
            int z1 = std::min(z0 + 1, maxIZ);

            float fx = normX - static_cast<float>(x0);
            float fz = normZ - static_cast<float>(z0);

            const float* data = heights.data();
            size_t row0 = static_cast<size_t>(z0) * resolutionX;
            size_t row1 = static_cast<size_t>(z1) * resolutionX;

            float h00 = data[row0 + x0];
            float h10 = data[row0 + x1];
            float h01 = data[row1 + x0];
            float h11 = data[row1 + x1];

            float h0 = h00 + (h10 - h00) * fx;
            float h1 = h01 + (h11 - h01) * fx;
            return h0 + (h1 - h0) * fz;
        }

        /**
         * @brief Computes normalized surface normal at arbitrary local position (localX, localZ)
         * using central differences on the interpolated height field.
         */
        glm::vec3 getInterpolatedNormal(float localX, float localZ) const {
            if (heights.empty() || resolutionX < 2 || resolutionZ < 2) return glm::vec3(0.0f, 1.0f, 0.0f);
            float dx = sizeX / static_cast<float>(resolutionX - 1);
            float dz = sizeZ / static_cast<float>(resolutionZ - 1);
            float epsX = std::max(0.1f, dx * 0.5f);
            float epsZ = std::max(0.1f, dz * 0.5f);

            float hL = getInterpolatedHeight(localX - epsX, localZ);
            float hR = getInterpolatedHeight(localX + epsX, localZ);
            float hD = getInterpolatedHeight(localX, localZ - epsZ);
            float hU = getInterpolatedHeight(localX, localZ + epsZ);

            float nx = (hL - hR) / (2.0f * epsX);
            float nz = (hD - hU) / (2.0f * epsZ);
            float len = std::sqrt(nx * nx + 1.0f + nz * nz);
            if (len < 1e-6f) return glm::vec3(0.0f, 1.0f, 0.0f);
            return glm::vec3(nx / len, 1.0f / len, nz / len);
        }

        void ensureAllocated() {
            ensureGridSize();
            size_t total = static_cast<size_t>(resolutionX) * resolutionZ;
            if (heights.size() != total) {
                if (!heights.empty()) {
                    resizeGrid(chunkCountX, chunkCountZ, chunkResolution, false);
                } else {
                    heights.assign(total, 0.0f);
                    boundsMinY = 0.0f;
                    boundsMaxY = 0.0f;
                    markAllChunksDirty();
                    isMeshInitialized = false;
                }
            }
            size_t totalChunks = static_cast<size_t>(chunkCountX) * chunkCountZ;
            if (chunkDirty.size() != totalChunks) {
                chunkDirty.assign(totalChunks, true);
            }
        }

        // ---------------------------------------------------------------------
        // Mesh Building & Normal Calculation
        // ---------------------------------------------------------------------

        inline glm::vec3 computeNormalFast(uint32_t ix, uint32_t iz, float inv2dx, float inv2dz, float invDx, float invDz) const {
            const float* data = heights.data();
            float hL, hR, hD, hU;
            float stepXInv, stepZInv;

            if (ix > 0 && ix + 1 < resolutionX) {
                hL = data[iz * resolutionX + (ix - 1)];
                hR = data[iz * resolutionX + (ix + 1)];
                stepXInv = inv2dx;
            } else if (ix == 0) {
                hL = data[iz * resolutionX];
                hR = (resolutionX > 1) ? data[iz * resolutionX + 1] : hL;
                stepXInv = invDx;
            } else {
                hL = data[iz * resolutionX + (ix - 1)];
                hR = data[iz * resolutionX + ix];
                stepXInv = invDx;
            }

            if (iz > 0 && iz + 1 < resolutionZ) {
                hD = data[(iz - 1) * resolutionX + ix];
                hU = data[(iz + 1) * resolutionX + ix];
                stepZInv = inv2dz;
            } else if (iz == 0) {
                hD = data[ix];
                hU = (resolutionZ > 1) ? data[resolutionX + ix] : hD;
                stepZInv = invDz;
            } else {
                hD = data[(iz - 1) * resolutionX + ix];
                hU = data[iz * resolutionX + ix];
                stepZInv = invDz;
            }

            float nx = (hL - hR) * stepXInv;
            float nz = (hD - hU) * stepZInv;
            float invLen = 1.0f / std::sqrt(nx * nx + 1.0f + nz * nz);
            return glm::vec3(nx * invLen, invLen, nz * invLen);
        }

        glm::vec3 computeNormal(uint32_t ix, uint32_t iz) const {
            float dx = sizeX / static_cast<float>(resolutionX - 1);
            float dz = sizeZ / static_cast<float>(resolutionZ - 1);
            return computeNormalFast(ix, iz, 1.0f / (2.0f * dx), 1.0f / (2.0f * dz), 1.0f / dx, 1.0f / dz);
        }

        // Shared index buffer builder for any chunk of resolution chunkResolution
        void buildChunkIndices(std::vector<uint32_t>& outIndices) const {
            outIndices.clear();
            outIndices.reserve(static_cast<size_t>(chunkResolution - 1) * (chunkResolution - 1) * 6);

            for (uint32_t lz = 0; lz < chunkResolution - 1; ++lz) {
                for (uint32_t lx = 0; lx < chunkResolution - 1; ++lx) {
                    uint32_t i0 = lz * chunkResolution + lx;
                    uint32_t i1 = lz * chunkResolution + (lx + 1);
                    uint32_t i2 = (lz + 1) * chunkResolution + lx;
                    uint32_t i3 = (lz + 1) * chunkResolution + (lx + 1);

                    // First triangle: i0 -> i1 -> i2
                    outIndices.push_back(i0);
                    outIndices.push_back(i1);
                    outIndices.push_back(i2);

                    // Second triangle: i1 -> i3 -> i2
                    outIndices.push_back(i1);
                    outIndices.push_back(i3);
                    outIndices.push_back(i2);
                }
            }
        }

        void buildChunkVertices(
            uint32_t cx, uint32_t cz,
            std::vector<Vertex>& outVertices,
            glm::vec3& outAabbMin,
            glm::vec3& outAabbMax
        ) const {
            outVertices.resize(static_cast<size_t>(chunkResolution) * chunkResolution);
            uint32_t stride = getChunkStride();
            uint32_t startX = cx * stride;
            uint32_t startZ = cz * stride;

            float halfX = sizeX * 0.5f;
            float halfZ = sizeZ * 0.5f;
            float dx = sizeX / static_cast<float>(resolutionX - 1);
            float dz = sizeZ / static_cast<float>(resolutionZ - 1);
            float invDx = 1.0f / dx;
            float invDz = 1.0f / dz;
            float inv2dx = 1.0f / (2.0f * dx);
            float inv2dz = 1.0f / (2.0f * dz);

            float minY = 1e9f;
            float maxY = -1e9f;

            const float* hData = heights.data();

            for (uint32_t lz = 0; lz < chunkResolution; ++lz) {
                uint32_t gz = startZ + lz;
                float posZ = -halfZ + gz * dz;
                float v = static_cast<float>(gz) / static_cast<float>(resolutionZ - 1) * uvScale;
                size_t gridRowOffset = static_cast<size_t>(gz) * resolutionX;
                size_t chunkRowOffset = static_cast<size_t>(lz) * chunkResolution;

                for (uint32_t lx = 0; lx < chunkResolution; ++lx) {
                    uint32_t gx = startX + lx;
                    float posX = -halfX + gx * dx;
                    float u = static_cast<float>(gx) / static_cast<float>(resolutionX - 1) * uvScale;
                    float posY = hData[gridRowOffset + gx];

                    if (posY < minY) minY = posY;
                    if (posY > maxY) maxY = posY;

                    size_t cIdx = chunkRowOffset + lx;
                    outVertices[cIdx].position = glm::vec3(posX, posY, posZ);
                    outVertices[cIdx].normal = computeNormalFast(gx, gz, inv2dx, inv2dz, invDx, invDz);
                    outVertices[cIdx].uv = glm::vec2(u, v);
                    outVertices[cIdx].boneIDs = glm::ivec4(0);
                    outVertices[cIdx].boneWeights = glm::vec4(0.0f);
                }
            }

            float chunkMinX = -halfX + startX * dx;
            float chunkMaxX = -halfX + (startX + stride) * dx;
            float chunkMinZ = -halfZ + startZ * dz;
            float chunkMaxZ = -halfZ + (startZ + stride) * dz;

            outAabbMin = glm::vec3(chunkMinX, minY, chunkMinZ);
            outAabbMax = glm::vec3(chunkMaxX, maxY, chunkMaxZ);
        }

        void updateChunkVertices(
            uint32_t cx, uint32_t cz,
            std::vector<Vertex>& vertices,
            glm::vec3& outAabbMin,
            glm::vec3& outAabbMax
        ) const {
            if (vertices.size() != static_cast<size_t>(chunkResolution) * chunkResolution) {
                buildChunkVertices(cx, cz, vertices, outAabbMin, outAabbMax);
                return;
            }

            uint32_t stride = getChunkStride();
            uint32_t startX = cx * stride;
            uint32_t startZ = cz * stride;

            float halfX = sizeX * 0.5f;
            float halfZ = sizeZ * 0.5f;
            float dx = sizeX / static_cast<float>(resolutionX - 1);
            float dz = sizeZ / static_cast<float>(resolutionZ - 1);
            float invDx = 1.0f / dx;
            float invDz = 1.0f / dz;
            float inv2dx = 1.0f / (2.0f * dx);
            float inv2dz = 1.0f / (2.0f * dz);

            float minY = 1e9f;
            float maxY = -1e9f;

            const float* hData = heights.data();
            Vertex* vData = vertices.data();

            for (uint32_t lz = 0; lz < chunkResolution; ++lz) {
                uint32_t gz = startZ + lz;
                float posZ = -halfZ + gz * dz;
                float v = static_cast<float>(gz) / static_cast<float>(resolutionZ - 1) * uvScale;
                size_t gridRowOffset = static_cast<size_t>(gz) * resolutionX;
                size_t chunkRowOffset = static_cast<size_t>(lz) * chunkResolution;

                for (uint32_t lx = 0; lx < chunkResolution; ++lx) {
                    uint32_t gx = startX + lx;
                    float posX = -halfX + gx * dx;
                    float u = static_cast<float>(gx) / static_cast<float>(resolutionX - 1) * uvScale;
                    float posY = hData[gridRowOffset + gx];

                    if (posY < minY) minY = posY;
                    if (posY > maxY) maxY = posY;

                    size_t cIdx = chunkRowOffset + lx;
                    vData[cIdx].position = glm::vec3(posX, posY, posZ);
                    vData[cIdx].uv = glm::vec2(u, v);
                    vData[cIdx].normal = computeNormalFast(gx, gz, inv2dx, inv2dz, invDx, invDz);
                }
            }

            float chunkMinX = -halfX + startX * dx;
            float chunkMaxX = -halfX + (startX + stride) * dx;
            float chunkMinZ = -halfZ + startZ * dz;
            float chunkMaxZ = -halfZ + (startZ + stride) * dz;

            outAabbMin = glm::vec3(chunkMinX, minY, chunkMinZ);
            outAabbMax = glm::vec3(chunkMaxX, maxY, chunkMaxZ);
        }

        // Full-mesh building kept for backward compatibility and tools
        void buildMesh(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
            ensureAllocated();

            outVertices.resize(static_cast<size_t>(resolutionX) * resolutionZ);
            float halfX = sizeX * 0.5f;
            float halfZ = sizeZ * 0.5f;
            float dx = sizeX / static_cast<float>(resolutionX - 1);
            float dz = sizeZ / static_cast<float>(resolutionZ - 1);
            float invDx = 1.0f / dx;
            float invDz = 1.0f / dz;
            float inv2dx = 1.0f / (2.0f * dx);
            float inv2dz = 1.0f / (2.0f * dz);

            for (uint32_t iz = 0; iz < resolutionZ; ++iz) {
                float posZ = -halfZ + iz * dz;
                float v = static_cast<float>(iz) / (resolutionZ - 1) * uvScale;
                size_t rowOffset = static_cast<size_t>(iz) * resolutionX;

                for (uint32_t ix = 0; ix < resolutionX; ++ix) {
                    float posX = -halfX + ix * dx;
                    float u = static_cast<float>(ix) / (resolutionX - 1) * uvScale;
                    float posY = heights[rowOffset + ix];

                    size_t idx = rowOffset + ix;
                    outVertices[idx].position = glm::vec3(posX, posY, posZ);
                    outVertices[idx].normal = computeNormalFast(ix, iz, inv2dx, inv2dz, invDx, invDz);
                    outVertices[idx].uv = glm::vec2(u, v);
                    outVertices[idx].boneIDs = glm::ivec4(0);
                    outVertices[idx].boneWeights = glm::vec4(0.0f);
                }
            }

            outIndices.clear();
            outIndices.reserve(static_cast<size_t>(resolutionX - 1) * (resolutionZ - 1) * 6);

            for (uint32_t iz = 0; iz < resolutionZ - 1; ++iz) {
                for (uint32_t ix = 0; ix < resolutionX - 1; ++ix) {
                    uint32_t i0 = static_cast<uint32_t>(getIndex(ix, iz));
                    uint32_t i1 = static_cast<uint32_t>(getIndex(ix + 1, iz));
                    uint32_t i2 = static_cast<uint32_t>(getIndex(ix, iz + 1));
                    uint32_t i3 = static_cast<uint32_t>(getIndex(ix + 1, iz + 1));

                    outIndices.push_back(i0);
                    outIndices.push_back(i1);
                    outIndices.push_back(i2);

                    outIndices.push_back(i1);
                    outIndices.push_back(i3);
                    outIndices.push_back(i2);
                }
            }

            isMeshInitialized = true;
            isDirty = false;
            hasPartialDirty = false;
        }

        void updateMeshVertices(std::vector<Vertex>& vertices) {
            ensureAllocated();
            size_t total = static_cast<size_t>(resolutionX) * resolutionZ;
            if (vertices.size() != total) {
                isMeshInitialized = false;
                return;
            }

            float halfX = sizeX * 0.5f;
            float halfZ = sizeZ * 0.5f;
            float dx = sizeX / static_cast<float>(resolutionX - 1);
            float dz = sizeZ / static_cast<float>(resolutionZ - 1);
            float invDx = 1.0f / dx;
            float invDz = 1.0f / dz;
            float inv2dx = 1.0f / (2.0f * dx);
            float inv2dz = 1.0f / (2.0f * dz);

            Vertex* vData = vertices.data();
            const float* hData = heights.data();
            uint32_t resX = resolutionX;
            uint32_t resZ = resolutionZ;

            for (uint32_t iz = 0; iz < resZ; ++iz) {
                float posZ = -halfZ + iz * dz;
                float v = static_cast<float>(iz) / static_cast<float>(resZ - 1) * uvScale;
                size_t rowOffset = static_cast<size_t>(iz) * resX;

                for (uint32_t ix = 0; ix < resX; ++ix) {
                    float posX = -halfX + ix * dx;
                    float u = static_cast<float>(ix) / static_cast<float>(resX - 1) * uvScale;
                    size_t idx = rowOffset + ix;

                    vData[idx].position = glm::vec3(posX, hData[idx], posZ);
                    vData[idx].uv = glm::vec2(u, v);
                    vData[idx].normal = computeNormalFast(ix, iz, inv2dx, inv2dz, invDx, invDz);
                }
            }
            isDirty = false;
            hasPartialDirty = false;
        }

        // ---------------------------------------------------------------------
        // Sculpting Brush Application
        // ---------------------------------------------------------------------

        float calculateFalloff(float dist, float radius) const {
            if (dist >= radius || radius <= 0.0001f) return 0.0f;
            float u = dist / radius;
            switch (brushFalloff) {
            case TerrainBrushFalloff::Smooth:
                return (1.0f - u * u) * (1.0f - u * u);
            case TerrainBrushFalloff::Linear:
                return 1.0f - u;
            case TerrainBrushFalloff::Spherical:
                return std::sqrt(std::max(0.0f, 1.0f - u * u));
            case TerrainBrushFalloff::Flat:
                return 1.0f;
            }
            return 1.0f - u;
        }

#if defined(_MSC_VER)
#pragma optimize("gt", on)
#endif
        void applyBrush(float localHitX, float localHitZ, float dt, bool invert = false) {
            ensureAllocated();
            if (brushRadius <= 0.01f || brushStrength <= 0.001f) return;

            float halfX = sizeX * 0.5f;
            float halfZ = sizeZ * 0.5f;
            float dx = sizeX / static_cast<float>(resolutionX - 1);
            float dz = sizeZ / static_cast<float>(resolutionZ - 1);
            float invDx = 1.0f / dx;
            float invDz = 1.0f / dz;

            // Bounding box of the brush in grid indices
            int minIX = std::clamp(static_cast<int>(std::floor((localHitX - brushRadius + halfX) * invDx)), 0, static_cast<int>(resolutionX - 1));
            int maxIX = std::clamp(static_cast<int>(std::ceil((localHitX + brushRadius + halfX) * invDx)), 0, static_cast<int>(resolutionX - 1));
            int minIZ = std::clamp(static_cast<int>(std::floor((localHitZ - brushRadius + halfZ) * invDz)), 0, static_cast<int>(resolutionZ - 1));
            int maxIZ = std::clamp(static_cast<int>(std::ceil((localHitZ + brushRadius + halfZ) * invDz)), 0, static_cast<int>(resolutionZ - 1));

            TerrainBrushMode activeMode = brushMode;
            if (invert) {
                if (activeMode == TerrainBrushMode::Raise) activeMode = TerrainBrushMode::Lower;
                else if (activeMode == TerrainBrushMode::Lower) activeMode = TerrainBrushMode::Raise;
            }

            float deltaH = brushStrength * dt;
            float radiusSq = brushRadius * brushRadius;
            float invRadiusSq = 1.0f / radiusSq;
            bool anyChanged = false;

            float* hData = heights.data();
            uint32_t resX = resolutionX;
            uint32_t resZ = resolutionZ;

            for (int iz = minIZ; iz <= maxIZ; ++iz) {
                float posZ = -halfZ + iz * dz;
                float dZ = posZ - localHitZ;
                float dZSq = dZ * dZ;
                if (dZSq >= radiusSq) continue;

                // Restrict X span to within the circle on this row
                float maxDX = std::sqrt(radiusSq - dZSq);
                int rowMinIX = std::clamp(static_cast<int>(std::floor((localHitX - maxDX + halfX) * invDx)), minIX, maxIX);
                int rowMaxIX = std::clamp(static_cast<int>(std::ceil((localHitX + maxDX + halfX) * invDx)), minIX, maxIX);

                size_t rowOffset = static_cast<size_t>(iz) * resX;

                for (int ix = rowMinIX; ix <= rowMaxIX; ++ix) {
                    float posX = -halfX + ix * dx;
                    float dX = posX - localHitX;
                    float distSq = dX * dX + dZSq;
                    if (distSq >= radiusSq) continue;

                    float weight = 0.0f;
                    if (brushFalloff == TerrainBrushFalloff::Smooth) {
                        float t = distSq * invRadiusSq;
                        float oneMinusT = 1.0f - t;
                        weight = oneMinusT * oneMinusT;
                    } else {
                        float dist = std::sqrt(distSq);
                        weight = calculateFalloff(dist, brushRadius);
                    }
                    if (weight <= 0.0001f) continue;

                    size_t idx = rowOffset + static_cast<size_t>(ix);
                    float currentH = hData[idx];
                    float newH = currentH;

                    switch (activeMode) {
                    case TerrainBrushMode::Raise:
                        newH = currentH + deltaH * weight;
                        break;
                    case TerrainBrushMode::Lower:
                        newH = currentH - deltaH * weight;
                        break;
                    case TerrainBrushMode::Smooth: {
                        float hL = hData[rowOffset + (ix > 0 ? ix - 1 : 0)];
                        float hR = hData[rowOffset + (ix + 1 < (int)resX ? ix + 1 : ix)];
                        float hD = hData[(iz > 0 ? iz - 1 : 0) * resX + ix];
                        float hU = hData[(iz + 1 < (int)resZ ? iz + 1 : iz) * resX + ix];
                        float avgH = (hL + hR + hD + hU) * 0.25f;
                        float blend = std::clamp(brushStrength * 0.8f * weight * dt, 0.0f, 1.0f);
                        newH = currentH + (avgH - currentH) * blend;
                        break;
                    }
                    case TerrainBrushMode::Flatten: {
                        float blend = std::clamp(brushStrength * 1.2f * weight * dt, 0.0f, 1.0f);
                        newH = currentH + (flattenTargetHeight - currentH) * blend;
                        break;
                    }
                    case TerrainBrushMode::Noise: {
                        float n = (std::sin(posX * 12.9898f + posZ * 78.233f) * 43758.5453f);
                        n = (n - std::floor(n)) * 2.0f - 1.0f;
                        newH = currentH + n * deltaH * weight * 0.5f;
                        break;
                    }
                    }

                    hData[idx] = newH;
                    if (newH < boundsMinY) boundsMinY = newH;
                    if (newH > boundsMaxY) boundsMaxY = newH;
                    anyChanged = true;
                }
            }

            if (anyChanged) {
                // Determine overlapping chunks (including 1-ring margin for seamless normals)
                uint32_t stride = getChunkStride();
                int normMinIX = std::max(0, minIX - 1);
                int normMaxIX = std::min(static_cast<int>(resX - 1), maxIX + 1);
                int normMinIZ = std::max(0, minIZ - 1);
                int normMaxIZ = std::min(static_cast<int>(resZ - 1), maxIZ + 1);

                int minCX = std::clamp(normMinIX / static_cast<int>(stride), 0, static_cast<int>(chunkCountX - 1));
                int maxCX = std::clamp(normMaxIX / static_cast<int>(stride), 0, static_cast<int>(chunkCountX - 1));
                int minCZ = std::clamp(normMinIZ / static_cast<int>(stride), 0, static_cast<int>(chunkCountZ - 1));
                int maxCZ = std::clamp(normMaxIZ / static_cast<int>(stride), 0, static_cast<int>(chunkCountZ - 1));

                for (int cz = minCZ; cz <= maxCZ; ++cz) {
                    for (int cx = minCX; cx <= maxCX; ++cx) {
                        markChunkDirty(static_cast<uint32_t>(cx), static_cast<uint32_t>(cz));
                    }
                }
                markDirtyRegion(static_cast<uint32_t>(normMinIX), static_cast<uint32_t>(normMaxIX),
                                static_cast<uint32_t>(normMinIZ), static_cast<uint32_t>(normMaxIZ));
            }
        }
#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

        // ---------------------------------------------------------------------
        // Procedural FBM Generation & Reset
        // ---------------------------------------------------------------------

        void flattenAll(float height = 0.0f) {
            ensureAllocated();
            std::fill(heights.begin(), heights.end(), height);
            flattenTargetHeight = height;
            boundsMinY = height;
            boundsMaxY = height;
            markFullDirty();
        }

        void generateFBM() {
            ensureAllocated();
            float halfX = sizeX * 0.5f;
            float halfZ = sizeZ * 0.5f;
            float dx = sizeX / static_cast<float>(resolutionX - 1);
            float dz = sizeZ / static_cast<float>(resolutionZ - 1);

            boundsMinY = 0.0f;
            boundsMaxY = 0.0f;

            for (uint32_t iz = 0; iz < resolutionZ; ++iz) {
                float posZ = (-halfZ + iz * dz) * noiseFrequency;
                size_t rowOffset = static_cast<size_t>(iz) * resolutionX;

                for (uint32_t ix = 0; ix < resolutionX; ++ix) {
                    float posX = (-halfX + ix * dx) * noiseFrequency;

                    float amplitude = 1.0f;
                    float frequency = 1.0f;
                    float total = 0.0f;
                    float maxAmplitude = 0.0f;

                    for (int o = 0; o < noiseOctaves; ++o) {
                        float sampleX = posX * frequency + static_cast<float>(noiseSeed) * 0.137f;
                        float sampleZ = posZ * frequency + static_cast<float>(noiseSeed) * 0.281f;

                        // Smooth continuous sine-based gradient noise approximation
                        float n = std::sin(sampleX) * std::cos(sampleZ) +
                                  0.5f * std::sin(sampleX * 1.71f + sampleZ * 0.93f) * std::cos(sampleX * 0.85f - sampleZ * 1.62f);

                        total += n * amplitude;
                        maxAmplitude += amplitude;
                        amplitude *= noisePersistence;
                        frequency *= noiseLacunarity;
                    }

                    float normalized = (maxAmplitude > 0.0f) ? (total / maxAmplitude) : 0.0f;
                    float h = normalized * heightScale;
                    heights[rowOffset + ix] = h;
                    if (h < boundsMinY) boundsMinY = h;
                    if (h > boundsMaxY) boundsMaxY = h;
                }
            }
            markFullDirty();
        }

        // ---------------------------------------------------------------------
        // Splatmap Texture Layer Painting & Procedural Texturing
        // ---------------------------------------------------------------------

        void ensureSplatmapAllocated() {
            if (splatmapResolution < 16) splatmapResolution = 16;
            size_t requiredBytes = static_cast<size_t>(splatmapResolution) * splatmapResolution * 4;
            if (splatmapData.size() != requiredBytes) {
                splatmapData.assign(requiredBytes, 0);
                // Initialize default: 100% Layer 0 (Grass)
                for (size_t i = 0; i < requiredBytes; i += 4) {
                    splatmapData[i + 0] = 255; // R = Layer 0
                    splatmapData[i + 1] = 0;   // G = Layer 1
                    splatmapData[i + 2] = 0;   // B = Layer 2
                    splatmapData[i + 3] = 0;   // A = Layer 3
                }
                splatmapDirty = true;
            }
        }

        void resetSplatmap() {
            fillSplatmapWithLayer(0);
        }

        void fillSplatmapWithLayer(int layerIdx) {
            ensureSplatmapAllocated();
            layerIdx = std::clamp(layerIdx, 0, 3);
            uint8_t r = (layerIdx == 0) ? 255 : 0;
            uint8_t g = (layerIdx == 1) ? 255 : 0;
            uint8_t b = (layerIdx == 2) ? 255 : 0;
            uint8_t a = (layerIdx == 3) ? 255 : 0;
            size_t totalPixels = static_cast<size_t>(splatmapResolution) * splatmapResolution;
            for (size_t i = 0; i < totalPixels; ++i) {
                size_t idx = i * 4;
                splatmapData[idx + 0] = r;
                splatmapData[idx + 1] = g;
                splatmapData[idx + 2] = b;
                splatmapData[idx + 3] = a;
            }
            splatmapDirty = true;
        }

        void paintSplatmap(
            float localX, float localZ,
            int targetLayer,
            float radius, float opacity, float dt,
            TerrainBrushFalloff falloff = TerrainBrushFalloff::Smooth
        ) {
            ensureSplatmapAllocated();
            if (radius <= 0.001f || sizeX <= 0.001f || sizeZ <= 0.001f) return;
            targetLayer = std::clamp(targetLayer, 0, 3);

            float invSizeX = 1.0f / sizeX;
            float invSizeZ = 1.0f / sizeZ;
            float normU = (localX * invSizeX + 0.5f) * static_cast<float>(splatmapResolution);
            float normV = (localZ * invSizeZ + 0.5f) * static_cast<float>(splatmapResolution);

            float pixelRadiusX = (radius * invSizeX) * static_cast<float>(splatmapResolution);
            float pixelRadiusZ = (radius * invSizeZ) * static_cast<float>(splatmapResolution);

            int minPX = std::clamp(static_cast<int>(std::floor(normU - pixelRadiusX)), 0, static_cast<int>(splatmapResolution - 1));
            int maxPX = std::clamp(static_cast<int>(std::ceil(normU + pixelRadiusX)), 0, static_cast<int>(splatmapResolution - 1));
            int minPZ = std::clamp(static_cast<int>(std::floor(normV - pixelRadiusZ)), 0, static_cast<int>(splatmapResolution - 1));
            int maxPZ = std::clamp(static_cast<int>(std::ceil(normV + pixelRadiusZ)), 0, static_cast<int>(splatmapResolution - 1));

            float rate = opacity * dt * 255.0f;
            float invRadius = 1.0f / radius;

            for (int pz = minPZ; pz <= maxPZ; ++pz) {
                float pWorldZ = (-0.5f + (static_cast<float>(pz) + 0.5f) / static_cast<float>(splatmapResolution)) * sizeZ;
                float dz = pWorldZ - localZ;
                size_t rowOffset = static_cast<size_t>(pz) * splatmapResolution * 4;

                for (int px = minPX; px <= maxPX; ++px) {
                    float pWorldX = (-0.5f + (static_cast<float>(px) + 0.5f) / static_cast<float>(splatmapResolution)) * sizeX;
                    float dx = pWorldX - localX;
                    float dist = std::hypot(dx, dz);
                    if (dist > radius) continue;

                    float normD = dist * invRadius;
                    float factor = 1.0f;
                    switch (falloff) {
                    case TerrainBrushFalloff::Smooth:
                        factor = 1.0f - normD * normD * (3.0f - 2.0f * normD);
                        break;
                    case TerrainBrushFalloff::Linear:
                        factor = 1.0f - normD;
                        break;
                    case TerrainBrushFalloff::Spherical:
                        factor = std::sqrt(std::max(0.0f, 1.0f - normD * normD));
                        break;
                    case TerrainBrushFalloff::Flat:
                        factor = 1.0f;
                        break;
                    }

                    float delta = rate * factor;
                    size_t pIdx = rowOffset + static_cast<size_t>(px) * 4;

                    float w[4] = {
                        static_cast<float>(splatmapData[pIdx + 0]),
                        static_cast<float>(splatmapData[pIdx + 1]),
                        static_cast<float>(splatmapData[pIdx + 2]),
                        static_cast<float>(splatmapData[pIdx + 3])
                    };

                    w[targetLayer] += delta;

                    float sum = w[0] + w[1] + w[2] + w[3];
                    if (sum > 0.0001f) {
                        float s = 255.0f / sum;
                        splatmapData[pIdx + 0] = static_cast<uint8_t>(std::clamp(std::round(w[0] * s), 0.0f, 255.0f));
                        splatmapData[pIdx + 1] = static_cast<uint8_t>(std::clamp(std::round(w[1] * s), 0.0f, 255.0f));
                        splatmapData[pIdx + 2] = static_cast<uint8_t>(std::clamp(std::round(w[2] * s), 0.0f, 255.0f));
                        splatmapData[pIdx + 3] = static_cast<uint8_t>(std::clamp(std::round(w[3] * s), 0.0f, 255.0f));
                    }
                }
            }
            splatmapDirty = true;
        }

        void paintFoliage(float localX, float localZ, float radius, float density, float dt, bool erase, int layerFilter = -1) {
            if (!foliageEnabled || chunks.empty()) return;

            float r2 = radius * radius;
            float chunkSizeX = sizeX / static_cast<float>(chunkCountX > 0 ? chunkCountX : 1);
            float chunkSizeZ = sizeZ / static_cast<float>(chunkCountZ > 0 ? chunkCountZ : 1);
            float halfX = sizeX * 0.5f;
            float halfZ = sizeZ * 0.5f;

            int minCX = std::clamp(static_cast<int>(std::floor((localX - radius + halfX) / chunkSizeX)), 0, static_cast<int>(chunkCountX - 1));
            int maxCX = std::clamp(static_cast<int>(std::floor((localX + radius + halfX) / chunkSizeX)), 0, static_cast<int>(chunkCountX - 1));
            int minCZ = std::clamp(static_cast<int>(std::floor((localZ - radius + halfZ) / chunkSizeZ)), 0, static_cast<int>(chunkCountZ - 1));
            int maxCZ = std::clamp(static_cast<int>(std::floor((localZ + radius + halfZ) / chunkSizeZ)), 0, static_cast<int>(chunkCountZ - 1));

            if (erase) {
                for (int cz = minCZ; cz <= maxCZ; ++cz) {
                    for (int cx = minCX; cx <= maxCX; ++cx) {
                        size_t cIdx = static_cast<size_t>(cz) * chunkCountX + cx;
                        if (cIdx >= chunks.size()) continue;
                        auto& chunk = chunks[cIdx];
                        if (chunk.foliageInstances.empty()) continue;

                        size_t initialCount = chunk.foliageInstances.size();
                        chunk.foliageInstances.erase(
                            std::remove_if(chunk.foliageInstances.begin(), chunk.foliageInstances.end(),
                                [&](const FoliageInstanceGPU& inst) {
                                    float dx = inst.positionAndScale.x - localX;
                                    float dz = inst.positionAndScale.z - localZ;
                                    return (dx * dx + dz * dz) <= r2;
                                }),
                            chunk.foliageInstances.end()
                        );

                        if (chunk.foliageInstances.size() != initialCount) {
                            chunk.foliageInstanceCount = static_cast<uint32_t>(chunk.foliageInstances.size());
                            chunk.foliageDirty = true;
                            foliageDirty = true;
                        }
                    }
                }
            } else {
                float clumpsToSpawnFloat = density * (radius * 0.5f) * (dt * 12.0f);
                int clumpsToSpawn = static_cast<int>(clumpsToSpawnFloat);
                static float accumFraction = 0.0f;
                accumFraction += (clumpsToSpawnFloat - static_cast<float>(clumpsToSpawn));
                if (accumFraction >= 1.0f) {
                    clumpsToSpawn += 1;
                    accumFraction -= 1.0f;
                }
                if (clumpsToSpawn <= 0) return;

                static uint32_t paintRng = 771239u;
                auto nextRand = []() -> float {
                    paintRng = paintRng * 1664525u + 1013904223u;
                    return static_cast<float>(paintRng & 0x00FFFFFFu) / 16777215.0f;
                };

                bool hasSplatmap = !splatmapData.empty() && (splatmapData.size() == (static_cast<size_t>(splatmapResolution) * splatmapResolution * 4));

                std::vector<const DetailPrototype*> activeGrass;
                for (const auto& proto : detailPalette) {
                    if (proto.enabled && proto.type == DetailType::GrassClump) {
                        activeGrass.push_back(&proto);
                    }
                }

                for (int s = 0; s < clumpsToSpawn; ++s) {
                    float r = radius * std::sqrt(nextRand());
                    float theta = nextRand() * 6.2831853f;
                    float px = localX + r * std::cos(theta);
                    float pz = localZ + r * std::sin(theta);

                    if (px < -halfX || px > halfX || pz < -halfZ || pz > halfZ) continue;

                    const DetailPrototype* chosenProto = nullptr;
                    if (!activeGrass.empty()) {
                        size_t pIdx = static_cast<size_t>(nextRand() * static_cast<float>(activeGrass.size())) % activeGrass.size();
                        chosenProto = activeGrass[pIdx];
                    }

                    int effectiveLayer = layerFilter >= 0 ? layerFilter : (chosenProto ? chosenProto->targetLayer : -1);

                    // Layer filter check (if effectiveLayer is specified 0..3)
                    if (effectiveLayer >= 0 && effectiveLayer <= 3 && hasSplatmap) {
                        float u = (px + halfX) / sizeX;
                        float v = (pz + halfZ) / sizeZ;
                        int sx = std::clamp(static_cast<int>(u * static_cast<float>(splatmapResolution)), 0, static_cast<int>(splatmapResolution - 1));
                        int sz = std::clamp(static_cast<int>(v * static_cast<float>(splatmapResolution)), 0, static_cast<int>(splatmapResolution - 1));
                        size_t sIdx = (static_cast<size_t>(sz) * splatmapResolution + sx) * 4;
                        float w = static_cast<float>(splatmapData[sIdx + effectiveLayer]) / 255.0f;
                        if (w < 0.15f) continue;
                    }

                    int cx = std::clamp(static_cast<int>(std::floor((px + halfX) / chunkSizeX)), 0, static_cast<int>(chunkCountX - 1));
                    int cz = std::clamp(static_cast<int>(std::floor((pz + halfZ) / chunkSizeZ)), 0, static_cast<int>(chunkCountZ - 1));
                    size_t cIdx = static_cast<size_t>(cz) * chunkCountX + cx;
                    if (cIdx >= chunks.size()) continue;

                    auto& chunk = chunks[cIdx];
                    if (chunk.foliageInstances.size() >= 4000) continue;

                    float minS = chosenProto ? chosenProto->scaleMin : foliageScaleMin;
                    float maxS = chosenProto ? chosenProto->scaleMax : foliageScaleMax;
                    glm::vec4 tintCol = chosenProto ? chosenProto->tint : foliageTint;
                    float sink = chosenProto ? chosenProto->sinkOffset : 0.0f;

                    float py = getInterpolatedHeight(px, pz) + sink;
                    float baseScale = minS + nextRand() * (maxS - minS);
                    float yaw = nextRand() * 6.2831853f;
                    float windPhase = nextRand() * 6.2831853f;
                    float tilt = (nextRand() - 0.5f) * 0.4f;

                    float tintVar = 0.90f + nextRand() * 0.20f;
                    glm::vec4 color = tintCol * glm::vec4(tintVar, tintVar * 1.04f, tintVar * 0.96f, 1.0f);

                    FoliageInstanceGPU inst{};
                    inst.positionAndScale = glm::vec4(px, py, pz, baseScale);
                    inst.rotationAndWind = glm::vec4(yaw, windPhase, tilt, 0.0f);
                    inst.color = color;

                    chunk.foliageInstances.push_back(inst);
                    chunk.foliageInstanceCount = static_cast<uint32_t>(chunk.foliageInstances.size());
                    chunk.foliageDirty = true;
                    foliageDirty = true;
                }
            }
        }

        void clearAllFoliage() {
            for (auto& chunk : chunks) {
                chunk.foliageInstances.clear();
                chunk.foliageInstanceCount = 0;
                chunk.foliageDirty = true;
            }
            foliageDirty = true;
            foliageNeedsRebuild = false;
        }

        // --- Terrain Editing Undo / Redo System ---
        enum class TerrainUndoType {
            None,
            Heights,
            Splatmap,
            Both,
            Foliage
        };

        struct TerrainUndoRecord {
            TerrainUndoType type = TerrainUndoType::None;
            std::vector<float> heights;
            std::vector<uint8_t> splatmapData;
            std::vector<std::vector<FoliageInstanceGPU>> chunkFoliageInstances;
            std::string description;
        };

        std::vector<TerrainUndoRecord> undoStack;
        std::vector<TerrainUndoRecord> redoStack;
        TerrainUndoRecord pendingStroke;
        bool isStrokeInProgress = false;
        static constexpr size_t MAX_UNDO_STEPS = 12;

        void beginStroke(TerrainUndoType type, const std::string& desc = "") {
            if (isStrokeInProgress) return;
            pendingStroke.type = type;
            pendingStroke.description = desc;
            if (type == TerrainUndoType::Heights || type == TerrainUndoType::Both) {
                pendingStroke.heights = heights;
            } else {
                pendingStroke.heights.clear();
            }
            if (type == TerrainUndoType::Splatmap || type == TerrainUndoType::Both) {
                ensureSplatmapAllocated();
                pendingStroke.splatmapData = splatmapData;
            } else {
                pendingStroke.splatmapData.clear();
            }
            if (type == TerrainUndoType::Foliage) {
                pendingStroke.chunkFoliageInstances.resize(chunks.size());
                for (size_t i = 0; i < chunks.size(); ++i) {
                    pendingStroke.chunkFoliageInstances[i] = chunks[i].foliageInstances;
                }
            } else {
                pendingStroke.chunkFoliageInstances.clear();
            }
            isStrokeInProgress = true;
        }

        void commitStroke() {
            if (!isStrokeInProgress) return;
            isStrokeInProgress = false;

            bool changed = false;
            if (pendingStroke.type == TerrainUndoType::Heights || pendingStroke.type == TerrainUndoType::Both) {
                if (pendingStroke.heights != heights) changed = true;
            }
            if (pendingStroke.type == TerrainUndoType::Splatmap || pendingStroke.type == TerrainUndoType::Both) {
                if (pendingStroke.splatmapData != splatmapData) changed = true;
            }
            if (pendingStroke.type == TerrainUndoType::Foliage) {
                if (pendingStroke.chunkFoliageInstances.size() == chunks.size()) {
                    for (size_t i = 0; i < chunks.size(); ++i) {
                        if (chunks[i].foliageInstances.size() != pendingStroke.chunkFoliageInstances[i].size()) {
                            changed = true;
                            break;
                        }
                    }
                } else {
                    changed = true;
                }
            }

            if (changed) {
                undoStack.push_back(std::move(pendingStroke));
                if (undoStack.size() > MAX_UNDO_STEPS) {
                    undoStack.erase(undoStack.begin());
                }
                redoStack.clear();
            }
            pendingStroke = TerrainUndoRecord{};
        }

        void cancelStroke() {
            if (!isStrokeInProgress) return;
            if (pendingStroke.type == TerrainUndoType::Heights || pendingStroke.type == TerrainUndoType::Both) {
                heights = std::move(pendingStroke.heights);
                markFullDirty();
                recalculateBounds();
            }
            if (pendingStroke.type == TerrainUndoType::Splatmap || pendingStroke.type == TerrainUndoType::Both) {
                splatmapData = std::move(pendingStroke.splatmapData);
                splatmapDirty = true;
            }
            if (pendingStroke.type == TerrainUndoType::Foliage) {
                for (size_t i = 0; i < std::min(chunks.size(), pendingStroke.chunkFoliageInstances.size()); ++i) {
                    chunks[i].foliageInstances = std::move(pendingStroke.chunkFoliageInstances[i]);
                    chunks[i].foliageInstanceCount = static_cast<uint32_t>(chunks[i].foliageInstances.size());
                    chunks[i].foliageDirty = true;
                }
            }
            isStrokeInProgress = false;
            pendingStroke = TerrainUndoRecord{};
        }

        bool canUndo() const { return !undoStack.empty(); }
        bool canRedo() const { return !redoStack.empty(); }

        bool undo() {
            if (undoStack.empty()) return false;
            TerrainUndoRecord rec = std::move(undoStack.back());
            undoStack.pop_back();

            TerrainUndoRecord current;
            current.type = rec.type;
            current.description = rec.description;
            if (rec.type == TerrainUndoType::Heights || rec.type == TerrainUndoType::Both) {
                current.heights = heights;
                heights = std::move(rec.heights);
                markFullDirty();
                recalculateBounds();
            }
            if (rec.type == TerrainUndoType::Splatmap || rec.type == TerrainUndoType::Both) {
                current.splatmapData = splatmapData;
                splatmapData = std::move(rec.splatmapData);
                splatmapDirty = true;
            }
            if (rec.type == TerrainUndoType::Foliage) {
                current.chunkFoliageInstances.resize(chunks.size());
                for (size_t i = 0; i < chunks.size(); ++i) {
                    current.chunkFoliageInstances[i] = chunks[i].foliageInstances;
                }
                for (size_t i = 0; i < std::min(chunks.size(), rec.chunkFoliageInstances.size()); ++i) {
                    chunks[i].foliageInstances = std::move(rec.chunkFoliageInstances[i]);
                    chunks[i].foliageInstanceCount = static_cast<uint32_t>(chunks[i].foliageInstances.size());
                    chunks[i].foliageDirty = true;
                }
            }
            redoStack.push_back(std::move(current));
            if (redoStack.size() > MAX_UNDO_STEPS) {
                redoStack.erase(redoStack.begin());
            }
            return true;
        }

        bool redo() {
            if (redoStack.empty()) return false;
            TerrainUndoRecord rec = std::move(redoStack.back());
            redoStack.pop_back();

            TerrainUndoRecord current;
            current.type = rec.type;
            current.description = rec.description;
            if (rec.type == TerrainUndoType::Heights || rec.type == TerrainUndoType::Both) {
                current.heights = heights;
                heights = std::move(rec.heights);
                markFullDirty();
                recalculateBounds();
            }
            if (rec.type == TerrainUndoType::Splatmap || rec.type == TerrainUndoType::Both) {
                current.splatmapData = splatmapData;
                splatmapData = std::move(rec.splatmapData);
                splatmapDirty = true;
            }
            if (rec.type == TerrainUndoType::Foliage) {
                current.chunkFoliageInstances.resize(chunks.size());
                for (size_t i = 0; i < chunks.size(); ++i) {
                    current.chunkFoliageInstances[i] = chunks[i].foliageInstances;
                }
                for (size_t i = 0; i < std::min(chunks.size(), rec.chunkFoliageInstances.size()); ++i) {
                    chunks[i].foliageInstances = std::move(rec.chunkFoliageInstances[i]);
                    chunks[i].foliageInstanceCount = static_cast<uint32_t>(chunks[i].foliageInstances.size());
                    chunks[i].foliageDirty = true;
                }
            }
            undoStack.push_back(std::move(current));
            if (undoStack.size() > MAX_UNDO_STEPS) {
                undoStack.erase(undoStack.begin());
            }
            return true;
        }

        void autoTextureBySlope(float cliffSlopeAngle = 40.0f, float dirtSlopeAngle = 20.0f) {
            beginStroke(TerrainUndoType::Splatmap, "Auto Texture by Slope");
            ensureSplatmapAllocated();
            float invRes = 1.0f / static_cast<float>(splatmapResolution);
            float rad2deg = 180.0f / 3.1415926535f;

            for (uint32_t pz = 0; pz < splatmapResolution; ++pz) {
                float localZ = (-0.5f + (static_cast<float>(pz) + 0.5f) * invRes) * sizeZ;
                size_t rowOffset = static_cast<size_t>(pz) * splatmapResolution * 4;

                for (uint32_t px = 0; px < splatmapResolution; ++px) {
                    float localX = (-0.5f + (static_cast<float>(px) + 0.5f) * invRes) * sizeX;
                    glm::vec3 normal = getInterpolatedNormal(localX, localZ);
                    float slopeDeg = std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * rad2deg;

                    float wGrass = 0.0f;
                    float wDirt = 0.0f;
                    float wRock = 0.0f;
                    float wSand = 0.0f;

                    if (slopeDeg >= cliffSlopeAngle) {
                        // Steep cliff -> 100% Rock
                        wRock = 255.0f;
                    } else if (slopeDeg >= dirtSlopeAngle) {
                        // Intermediate slope -> blend between Dirt and Rock
                        float t = (slopeDeg - dirtSlopeAngle) / std::max(0.1f, (cliffSlopeAngle - dirtSlopeAngle));
                        wRock = t * 255.0f;
                        wDirt = (1.0f - t) * 255.0f;
                    } else {
                        // Flat / gentle slope -> blend between Grass and Dirt
                        float t = slopeDeg / std::max(0.1f, dirtSlopeAngle);
                        wDirt = t * 128.0f;
                        wGrass = 255.0f - wDirt;
                    }

                    size_t pIdx = rowOffset + static_cast<size_t>(px) * 4;
                    splatmapData[pIdx + 0] = static_cast<uint8_t>(std::clamp(std::round(wGrass), 0.0f, 255.0f));
                    splatmapData[pIdx + 1] = static_cast<uint8_t>(std::clamp(std::round(wDirt), 0.0f, 255.0f));
                    splatmapData[pIdx + 2] = static_cast<uint8_t>(std::clamp(std::round(wRock), 0.0f, 255.0f));
                    splatmapData[pIdx + 3] = static_cast<uint8_t>(std::clamp(std::round(wSand), 0.0f, 255.0f));
                }
            }
            splatmapDirty = true;
            commitStroke();
        }

        void autoTextureProcedural(
            float beachHeight = 5.0f,
            float beachTransition = 3.0f,
            float dirtSlopeAngle = 18.0f,
            float cliffSlopeAngle = 35.0f,
            float snowHeight = 220.0f,
            float snowTransition = 30.0f,
            int layerGrass = 0,
            int layerDirt = 1,
            int layerRock = 2,
            int layerSandOrSnow = 3,
            bool useSnow = true
        ) {
            beginStroke(TerrainUndoType::Splatmap, "Procedural Splatmap");
            ensureSplatmapAllocated();
            float invRes = 1.0f / static_cast<float>(splatmapResolution);
            float rad2deg = 180.0f / 3.1415926535f;

            for (uint32_t pz = 0; pz < splatmapResolution; ++pz) {
                float localZ = (-0.5f + (static_cast<float>(pz) + 0.5f) * invRes) * sizeZ;
                size_t rowOffset = static_cast<size_t>(pz) * splatmapResolution * 4;

                for (uint32_t px = 0; px < splatmapResolution; ++px) {
                    float localX = (-0.5f + (static_cast<float>(px) + 0.5f) * invRes) * sizeX;
                    float h = getInterpolatedHeight(localX, localZ);
                    glm::vec3 normal = getInterpolatedNormal(localX, localZ);
                    float slopeDeg = std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * rad2deg;

                    float w[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

                    // 1. Slope-based base weights (Grass vs Dirt vs Rock)
                    float rockW = 0.0f;
                    float dirtW = 0.0f;
                    float grassW = 0.0f;

                    if (slopeDeg >= cliffSlopeAngle) {
                        rockW = 1.0f;
                    } else if (slopeDeg >= dirtSlopeAngle) {
                        float t = (slopeDeg - dirtSlopeAngle) / std::max(0.1f, (cliffSlopeAngle - dirtSlopeAngle));
                        rockW = t;
                        dirtW = 1.0f - t;
                    } else {
                        float t = slopeDeg / std::max(0.1f, dirtSlopeAngle);
                        dirtW = t * 0.45f;
                        grassW = 1.0f - dirtW;
                    }

                    w[std::clamp(layerGrass, 0, 3)] = grassW;
                    w[std::clamp(layerDirt, 0, 3)]  += dirtW;
                    w[std::clamp(layerRock, 0, 3)]  += rockW;

                    // 2. Beach / Water Shore (low altitude)
                    if (h <= beachHeight + beachTransition) {
                        float beachT = std::clamp((beachHeight + beachTransition - h) / std::max(0.1f, beachTransition), 0.0f, 1.0f);
                        float cliffPreserve = std::clamp((slopeDeg - dirtSlopeAngle) / std::max(0.1f, cliffSlopeAngle - dirtSlopeAngle), 0.0f, 0.8f);
                        float effectiveBeach = beachT * (1.0f - cliffPreserve);

                        int shoreLayer = (!useSnow) ? layerSandOrSnow : layerDirt;
                        for (int i = 0; i < 4; ++i) {
                            if (i == shoreLayer) {
                                w[i] = glm::mix(w[i], 1.0f, effectiveBeach);
                            } else {
                                w[i] = glm::mix(w[i], 0.0f, effectiveBeach);
                            }
                        }
                    }

                    // 3. Snow on high mountain peaks
                    if (useSnow && h >= snowHeight - snowTransition) {
                        float snowT = std::clamp((h - (snowHeight - snowTransition)) / std::max(0.1f, snowTransition * 2.0f), 0.0f, 1.0f);
                        float cliffShed = std::clamp((slopeDeg - 45.0f) / 25.0f, 0.0f, 0.85f);
                        float effectiveSnow = snowT * (1.0f - cliffShed);

                        int snowLayer = layerSandOrSnow;
                        for (int i = 0; i < 4; ++i) {
                            if (i == snowLayer) {
                                w[i] = glm::mix(w[i], 1.0f, effectiveSnow);
                            } else {
                                w[i] = glm::mix(w[i], 0.0f, effectiveSnow);
                            }
                        }
                    }

                    // Normalize to sum of 255
                    float sum = w[0] + w[1] + w[2] + w[3];
                    if (sum > 0.0001f) {
                        float s = 255.0f / sum;
                        w[0] *= s;
                        w[1] *= s;
                        w[2] *= s;
                        w[3] *= s;
                    } else {
                        w[0] = 255.0f;
                    }

                    size_t pIdx = rowOffset + static_cast<size_t>(px) * 4;
                    splatmapData[pIdx + 0] = static_cast<uint8_t>(std::clamp(std::round(w[0]), 0.0f, 255.0f));
                    splatmapData[pIdx + 1] = static_cast<uint8_t>(std::clamp(std::round(w[1]), 0.0f, 255.0f));
                    splatmapData[pIdx + 2] = static_cast<uint8_t>(std::clamp(std::round(w[2]), 0.0f, 255.0f));
                    splatmapData[pIdx + 3] = static_cast<uint8_t>(std::clamp(std::round(w[3]), 0.0f, 255.0f));
                }
            }
            splatmapDirty = true;
            commitStroke();
        }
    };

} // namespace Engine
