#include "ecs/systems/TerrainSystem.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/Tilemap.hpp"
#include "renderer/ResourceManager.hpp"
#include "core/JobSystem.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace Engine {

    TerrainSystem::TerrainSystem(Registry& reg, VulkanRenderer& rend)
        : registry(reg), renderer(rend) {
        registry.subscribeToAdded<TerrainComponent>([this](Entity e) {
            if (auto* terrain = registry.get<TerrainComponent>(e)) {
                terrain->ensureAllocated();
                terrain->markFullDirty();
                terrain->isMeshInitialized = false;
            }
        });

        registry.subscribeToRemoved<TerrainComponent>([this](Entity e) {
            if (auto* terrain = registry.get<TerrainComponent>(e)) {
                destroyChunks(*terrain);
                destroyTerrainGPUResources(*terrain);
            }
        });
    }

    TerrainSystem::~TerrainSystem() {
        VkDevice dev = renderer.getDevice();
        if (dev != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(dev);
            for (auto& ib : sharedIndexBuffers) {
                if (ib.buffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(dev, ib.buffer, nullptr);
                    ib.buffer = VK_NULL_HANDLE;
                }
                if (ib.memory != VK_NULL_HANDLE) {
                    vkFreeMemory(dev, ib.memory, nullptr);
                    ib.memory = VK_NULL_HANDLE;
                }
            }
        }
        sharedIndexBuffers.clear();

        for (auto [entity, terrain] : registry.view<TerrainComponent>()) {
            destroyChunks(terrain);
            destroyTerrainGPUResources(terrain);
        }
    }

    void TerrainSystem::destroyTerrainGPUResources(TerrainComponent& terrain) {
        VkDevice dev = renderer.getDevice();
        if (dev != VK_NULL_HANDLE) {
            if (terrain.splatmapSampler != VK_NULL_HANDLE) {
                vkDestroySampler(dev, terrain.splatmapSampler, nullptr);
                terrain.splatmapSampler = VK_NULL_HANDLE;
            }
            if (terrain.splatmapView != VK_NULL_HANDLE) {
                vkDestroyImageView(dev, terrain.splatmapView, nullptr);
                terrain.splatmapView = VK_NULL_HANDLE;
            }
            if (terrain.splatmapImage != VK_NULL_HANDLE) {
                vkDestroyImage(dev, terrain.splatmapImage, nullptr);
                terrain.splatmapImage = VK_NULL_HANDLE;
            }
            if (terrain.splatmapMemory != VK_NULL_HANDLE) {
                vkFreeMemory(dev, terrain.splatmapMemory, nullptr);
                terrain.splatmapMemory = VK_NULL_HANDLE;
            }
            if (terrain.terrainUboBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(dev, terrain.terrainUboBuffer, nullptr);
                terrain.terrainUboBuffer = VK_NULL_HANDLE;
            }
            if (terrain.terrainUboMemory != VK_NULL_HANDLE) {
                vkFreeMemory(dev, terrain.terrainUboMemory, nullptr);
                terrain.terrainUboMemory = VK_NULL_HANDLE;
            }
            if (terrain.foliageVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(dev, terrain.foliageVertexBuffer, nullptr);
                terrain.foliageVertexBuffer = VK_NULL_HANDLE;
            }
            if (terrain.foliageVertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(dev, terrain.foliageVertexMemory, nullptr);
                terrain.foliageVertexMemory = VK_NULL_HANDLE;
            }
            if (terrain.foliageIndexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(dev, terrain.foliageIndexBuffer, nullptr);
                terrain.foliageIndexBuffer = VK_NULL_HANDLE;
            }
            if (terrain.foliageIndexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(dev, terrain.foliageIndexMemory, nullptr);
                terrain.foliageIndexMemory = VK_NULL_HANDLE;
            }
            if (terrain.foliageTextureSampler != VK_NULL_HANDLE) {
                vkDestroySampler(dev, terrain.foliageTextureSampler, nullptr);
                terrain.foliageTextureSampler = VK_NULL_HANDLE;
            }
            if (terrain.foliageTextureView != VK_NULL_HANDLE) {
                vkDestroyImageView(dev, terrain.foliageTextureView, nullptr);
                terrain.foliageTextureView = VK_NULL_HANDLE;
            }
            if (terrain.foliageTextureImage != VK_NULL_HANDLE) {
                vkDestroyImage(dev, terrain.foliageTextureImage, nullptr);
                terrain.foliageTextureImage = VK_NULL_HANDLE;
            }
            if (terrain.foliageTextureMemory != VK_NULL_HANDLE) {
                vkFreeMemory(dev, terrain.foliageTextureMemory, nullptr);
                terrain.foliageTextureMemory = VK_NULL_HANDLE;
            }
            terrain.foliageDescriptorSet = VK_NULL_HANDLE;
            terrain.foliagePipeline = VK_NULL_HANDLE;
            terrain.foliagePipelineLayout = VK_NULL_HANDLE;
            terrain.foliageIndexCount = 0;
            terrain.terrainDescriptorSet = VK_NULL_HANDLE;
            terrain.terrainGpuInitialized = false;
        }
    }

    void TerrainSystem::destroyChunks(TerrainComponent& terrain) {
        VkDevice dev = renderer.getDevice();
        if (dev != VK_NULL_HANDLE && !terrain.chunks.empty()) {
            vkDeviceWaitIdle(dev);
            for (auto& chunk : terrain.chunks) {
                if (chunk.mesh.vertexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(dev, chunk.mesh.vertexBuffer, nullptr);
                    chunk.mesh.vertexBuffer = VK_NULL_HANDLE;
                }
                if (chunk.mesh.vertexBufferMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(dev, chunk.mesh.vertexBufferMemory, nullptr);
                    chunk.mesh.vertexBufferMemory = VK_NULL_HANDLE;
                }
                if (chunk.foliageBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(dev, chunk.foliageBuffer, nullptr);
                    chunk.foliageBuffer = VK_NULL_HANDLE;
                }
                if (chunk.foliageBufferMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(dev, chunk.foliageBufferMemory, nullptr);
                    chunk.foliageBufferMemory = VK_NULL_HANDLE;
                }
                chunk.foliageBufferCapacity = 0;
                chunk.foliageInstanceCount = 0;
                chunk.foliageInstances.clear();
                chunk.mesh.indexBuffer = VK_NULL_HANDLE;
                chunk.mesh.indexBufferMemory = VK_NULL_HANDLE;
            }
        }
        terrain.chunks.clear();
    }

    static void uploadChunkVertexBuffer(VkDevice dev, VkPhysicalDevice phys, TerrainChunk& chunk) {
        auto& verts = chunk.mesh.vertices;
        if (verts.empty()) return;

        VkDeviceSize size = verts.size() * sizeof(Vertex);

        // If buffer does not exist, create it
        if (chunk.mesh.vertexBuffer == VK_NULL_HANDLE) {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(dev, &bufferInfo, nullptr, &chunk.mesh.vertexBuffer) != VK_SUCCESS) {
                std::cerr << "[TerrainSystem] Failed to create chunk vertex buffer!" << std::endl;
                return;
            }

            VkMemoryRequirements memReq;
            vkGetBufferMemoryRequirements(dev, chunk.mesh.vertexBuffer, &memReq);

            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

            uint32_t memoryTypeIndex = UINT32_MAX;
            VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                if ((memReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                    memoryTypeIndex = i;
                    break;
                }
            }

            if (memoryTypeIndex == UINT32_MAX) {
                std::cerr << "[TerrainSystem] Failed to find suitable memory type for chunk vertex buffer!" << std::endl;
                return;
            }

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = memoryTypeIndex;

            if (vkAllocateMemory(dev, &allocInfo, nullptr, &chunk.mesh.vertexBufferMemory) != VK_SUCCESS) {
                std::cerr << "[TerrainSystem] Failed to allocate chunk vertex buffer memory!" << std::endl;
                return;
            }

            vkBindBufferMemory(dev, chunk.mesh.vertexBuffer, chunk.mesh.vertexBufferMemory, 0);
        }

        // Direct upload to host-visible memory
        void* dst = nullptr;
        if (vkMapMemory(dev, chunk.mesh.vertexBufferMemory, 0, size, 0, &dst) == VK_SUCCESS) {
            std::memcpy(dst, verts.data(), static_cast<size_t>(size));
            vkUnmapMemory(dev, chunk.mesh.vertexBufferMemory);
        }
    }

    void TerrainSystem::update(float dt) {
        for (auto [entity, terrain] : registry.view<TerrainComponent>()) {
            if (!terrain.isMeshInitialized || terrain.isDirty || terrain.splatmapDirty || terrain.layersDirty || terrain.foliageNeedsRebuild) {
                syncTerrainEntity(entity, terrain);
            } else if (terrain.foliageEnabled && terrain.foliageDirty) {
                // Instantly upload any chunks whose foliage was modified by manual painting or erasing
                syncTerrainFoliage(terrain);
                terrain.foliageDirty = false;
            }
        }

        // Perform frustum culling and world bounds update on all terrain chunk tiles
        performFrustumCulling(renderer.getActiveCameraViewProj());
    }

    std::array<glm::vec4, 6> TerrainSystem::extractFrustumPlanes(const glm::mat4& viewProj) {
        std::array<glm::vec4, 6> planes;

        // GLM matrices are column-major: viewProj[col][row]
        glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
        glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
        glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
        glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

        // Vulkan clip space: x in [-w, w], y in [-w, w], z in [0, w]
        planes[0] = row3 + row0; // Left:   w + x >= 0
        planes[1] = row3 - row0; // Right:  w - x >= 0
        planes[2] = row3 + row1; // Bottom: w + y >= 0
        planes[3] = row3 - row1; // Top:    w - y >= 0
        planes[4] = row2;        // Near:   z >= 0
        planes[5] = row3 - row2; // Far:    w - z >= 0

        // Normalize plane equations (Ax + By + Cz + D = 0)
        for (int i = 0; i < 6; ++i) {
            float len = glm::length(glm::vec3(planes[i]));
            if (len > 1e-6f) {
                planes[i] /= len;
            }
        }
        return planes;
    }

    bool TerrainSystem::isAabbInFrustum(const glm::vec3& aabbMin, const glm::vec3& aabbMax, const std::array<glm::vec4, 6>& planes) {
        if (aabbMin.x > aabbMax.x) return true; // Safety check for uninitialized boxes

        for (const auto& plane : planes) {
            glm::vec3 pVertex(
                plane.x > 0.0f ? aabbMax.x : aabbMin.x,
                plane.y > 0.0f ? aabbMax.y : aabbMin.y,
                plane.z > 0.0f ? aabbMax.z : aabbMin.z
            );
            if (glm::dot(glm::vec3(plane), pVertex) + plane.w < 0.0f) {
                return false; // Box is entirely on negative side of this frustum plane
            }
        }
        return true;
    }

    bool TerrainSystem::isAabbFullyInFrustum(const glm::vec3& aabbMin, const glm::vec3& aabbMax, const std::array<glm::vec4, 6>& planes) {
        if (aabbMin.x > aabbMax.x) return true;

        for (const auto& plane : planes) {
            glm::vec3 nVertex(
                plane.x > 0.0f ? aabbMin.x : aabbMax.x,
                plane.y > 0.0f ? aabbMin.y : aabbMax.y,
                plane.z > 0.0f ? aabbMin.z : aabbMax.z
            );
            if (glm::dot(glm::vec3(plane), nVertex) + plane.w < 0.0f) {
                return false; // Box is not completely on positive side of this plane
            }
        }
        return true;
    }

    void TerrainSystem::transformAABB(const glm::mat4& m, const glm::vec3& minIn, const glm::vec3& maxIn, glm::vec3& minOut, glm::vec3& maxOut) {
        minOut = glm::vec3(m[3]);
        maxOut = minOut;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                float a = m[j][i] * minIn[j];
                float b = m[j][i] * maxIn[j];
                if (a < b) {
                    minOut[i] += a;
                    maxOut[i] += b;
                } else {
                    minOut[i] += b;
                    maxOut[i] += a;
                }
            }
        }
    }

    void TerrainSystem::performFrustumCulling(const glm::mat4& viewProj) {
        float det = glm::determinant(viewProj);
        bool validProj = !std::isnan(det) && std::abs(det) >= 1e-6f;
        std::array<glm::vec4, 6> planes{};
        if (validProj) {
            planes = extractFrustumPlanes(viewProj);
        }

        for (auto [entity, terrain, transform] : registry.view<TerrainComponent, Transform>()) {
            if (terrain.chunks.empty()) continue;

            glm::mat4 worldM = transform.matrix();

            if (!validProj) {
                for (auto& chunk : terrain.chunks) {
                    chunk.isVisible = true;
                }
                continue;
            }

            // Hierarchical test on overall terrain world AABB
            glm::vec3 terrainLocalMin(0.0f, 0.0f, 0.0f);
            glm::vec3 terrainLocalMax(terrain.sizeX, terrain.heightScale, terrain.sizeZ);
            glm::vec3 terrainWorldMin, terrainWorldMax;
            transformAABB(worldM, terrainLocalMin, terrainLocalMax, terrainWorldMin, terrainWorldMax);

            // 1. If entire terrain is outside the frustum: cull all chunks in 6 operations
            if (!isAabbInFrustum(terrainWorldMin, terrainWorldMax, planes)) {
                for (auto& chunk : terrain.chunks) {
                    chunk.isVisible = false;
                }
                continue;
            }

            // 2. If entire terrain is completely inside the frustum: make all chunks visible in 6 operations
            if (isAabbFullyInFrustum(terrainWorldMin, terrainWorldMax, planes)) {
                for (auto& chunk : terrain.chunks) {
                    chunk.isVisible = true;
                }
                continue;
            }

            // 3. Terrain boundary straddles the frustum planes: precision test individual chunks
            for (auto& chunk : terrain.chunks) {
                transformAABB(worldM, chunk.aabbMin, chunk.aabbMax, chunk.worldAabbMin, chunk.worldAabbMax);
                chunk.isVisible = isAabbInFrustum(chunk.worldAabbMin, chunk.worldAabbMax, planes);
            }
        }
    }

    struct TerrainShaderUBO {
        glm::vec4 layerTiling{ 0.05f };
        glm::vec4 layerRoughness{ 0.8f, 0.9f, 0.7f, 0.95f };
        glm::vec4 layerMetallic{ 0.0f };
        glm::vec4 tintColor0{ 0.35f, 0.65f, 0.25f, 1.0f };
        glm::vec4 tintColor1{ 0.55f, 0.40f, 0.25f, 1.0f };
        glm::vec4 tintColor2{ 0.50f, 0.50f, 0.52f, 1.0f };
        glm::vec4 tintColor3{ 0.85f, 0.75f, 0.50f, 1.0f };
        glm::vec4 terrainParams{ 1000.0f, 1000.0f, 600.0f, 0.0f };
    };

    static void uploadSplatmapTexture(VulkanRenderer& renderer, TerrainComponent& terrain) {
        terrain.ensureSplatmapAllocated();
        VkDevice dev = renderer.getDevice();
        VkPhysicalDevice phys = renderer.device.getPhysicalDevice();
        uint32_t res = terrain.splatmapResolution;
        VkDeviceSize imgSize = static_cast<VkDeviceSize>(res) * res * 4;

        if (terrain.splatmapImage == VK_NULL_HANDLE) {
            VkImageCreateInfo imgInfo{};
            imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imgInfo.imageType = VK_IMAGE_TYPE_2D;
            imgInfo.extent.width = res;
            imgInfo.extent.height = res;
            imgInfo.extent.depth = 1;
            imgInfo.mipLevels = 1;
            imgInfo.arrayLayers = 1;
            imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            if (vkCreateImage(dev, &imgInfo, nullptr, &terrain.splatmapImage) != VK_SUCCESS) {
                std::cerr << "[TerrainSystem] Failed to create splatmap image!" << std::endl;
                return;
            }

            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(dev, terrain.splatmapImage, &memReq);

            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
            uint32_t memoryTypeIndex = UINT32_MAX;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                if ((memReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                    memoryTypeIndex = i;
                    break;
                }
            }
            if (memoryTypeIndex == UINT32_MAX) {
                for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                    if (memReq.memoryTypeBits & (1 << i)) {
                        memoryTypeIndex = i;
                        break;
                    }
                }
            }

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = memoryTypeIndex;
            vkAllocateMemory(dev, &allocInfo, nullptr, &terrain.splatmapMemory);
            vkBindImageMemory(dev, terrain.splatmapImage, terrain.splatmapMemory, 0);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = terrain.splatmapImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(dev, &viewInfo, nullptr, &terrain.splatmapView);

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            vkCreateSampler(dev, &samplerInfo, nullptr, &terrain.splatmapSampler);
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = imgSize;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev, &bufInfo, nullptr, &stagingBuffer);

        VkMemoryRequirements bMemReq;
        vkGetBufferMemoryRequirements(dev, stagingBuffer, &bMemReq);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
        uint32_t bMemType = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((bMemReq.memoryTypeBits & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                bMemType = i;
                break;
            }
        }

        VkMemoryAllocateInfo bAllocInfo{};
        bAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        bAllocInfo.allocationSize = bMemReq.size;
        bAllocInfo.memoryTypeIndex = bMemType;
        vkAllocateMemory(dev, &bAllocInfo, nullptr, &stagingMemory);
        vkBindBufferMemory(dev, stagingBuffer, stagingMemory, 0);

        void* data = nullptr;
        if (vkMapMemory(dev, stagingMemory, 0, imgSize, 0, &data) == VK_SUCCESS) {
            std::memcpy(data, terrain.splatmapData.data(), static_cast<size_t>(imgSize));
            vkUnmapMemory(dev, stagingMemory);
        }

        VkCommandBuffer cmd = renderer.beginSingleUseCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = terrain.splatmapImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { res, res, 1 };

        vkCmdCopyBufferToImage(cmd, stagingBuffer, terrain.splatmapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        renderer.endSingleUseCommands(cmd);

        vkDestroyBuffer(dev, stagingBuffer, nullptr);
        vkFreeMemory(dev, stagingMemory, nullptr);

        terrain.splatmapDirty = false;
    }

    static void syncTerrainGPUResources(VulkanRenderer& renderer, TerrainComponent& terrain, Material* mat) {
        VkDevice dev = renderer.getDevice();
        VkPhysicalDevice phys = renderer.device.getPhysicalDevice();

        if (terrain.splatmapImage == VK_NULL_HANDLE || terrain.splatmapDirty) {
            uploadSplatmapTexture(renderer, terrain);
            terrain.foliageNeedsRebuild = true;
        }

        VkDeviceSize uboSize = sizeof(TerrainShaderUBO);
        if (terrain.terrainUboBuffer == VK_NULL_HANDLE) {
            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = uboSize;
            bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(dev, &bufInfo, nullptr, &terrain.terrainUboBuffer);

            VkMemoryRequirements bMemReq;
            vkGetBufferMemoryRequirements(dev, terrain.terrainUboBuffer, &bMemReq);

            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
            uint32_t bMemType = UINT32_MAX;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                if ((bMemReq.memoryTypeBits & (1 << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                    bMemType = i;
                    break;
                }
            }

            VkMemoryAllocateInfo bAllocInfo{};
            bAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            bAllocInfo.allocationSize = bMemReq.size;
            bAllocInfo.memoryTypeIndex = bMemType;
            vkAllocateMemory(dev, &bAllocInfo, nullptr, &terrain.terrainUboMemory);
            vkBindBufferMemory(dev, terrain.terrainUboBuffer, terrain.terrainUboMemory, 0);
        }

        if (terrain.layersDirty || !terrain.terrainGpuInitialized || terrain.terrainDescriptorSet == VK_NULL_HANDLE) {
            TerrainShaderUBO ubo{};
            ubo.layerTiling = glm::vec4(
                terrain.layers[0].uvScale,
                terrain.layers[1].uvScale,
                terrain.layers[2].uvScale,
                terrain.layers[3].uvScale
            );
            ubo.layerRoughness = glm::vec4(
                terrain.layers[0].roughness,
                terrain.layers[1].roughness,
                terrain.layers[2].roughness,
                terrain.layers[3].roughness
            );
            ubo.layerMetallic = glm::vec4(
                terrain.layers[0].metallic,
                terrain.layers[1].metallic,
                terrain.layers[2].metallic,
                terrain.layers[3].metallic
            );
            ubo.tintColor0 = terrain.layers[0].tintColor;
            ubo.tintColor1 = terrain.layers[1].tintColor;
            ubo.tintColor2 = terrain.layers[2].tintColor;
            ubo.tintColor3 = terrain.layers[3].tintColor;
            ubo.terrainParams = glm::vec4(terrain.sizeX, terrain.sizeZ, terrain.heightScale, 0.0f);

            void* dst = nullptr;
            if (vkMapMemory(dev, terrain.terrainUboMemory, 0, uboSize, 0, &dst) == VK_SUCCESS) {
                std::memcpy(dst, &ubo, sizeof(TerrainShaderUBO));
                vkUnmapMemory(dev, terrain.terrainUboMemory);
            }

            std::array<VkImageView, 4> albedoViews{};
            std::array<VkSampler, 4> albedoSamplers{};
            std::array<VkImageView, 4> normalViews{};
            std::array<VkSampler, 4> normalSamplers{};

            Texture* defWhite = renderer.resourceManager ? renderer.resourceManager->getDefaultWhiteTexture() : nullptr;
            Texture* defNorm = renderer.resourceManager ? renderer.resourceManager->getDefaultNormalTexture() : nullptr;

            for (size_t i = 0; i < 4; ++i) {
                albedoViews[i] = defWhite ? defWhite->imageView : VK_NULL_HANDLE;
                albedoSamplers[i] = defWhite ? defWhite->sampler : VK_NULL_HANDLE;
                normalViews[i] = defNorm ? defNorm->imageView : VK_NULL_HANDLE;
                normalSamplers[i] = defNorm ? defNorm->sampler : VK_NULL_HANDLE;

                if (renderer.resourceManager) {
                    if (!terrain.layers[i].albedoPath.empty()) {
                        if (Texture* tex = renderer.resourceManager->loadTexture(terrain.layers[i].albedoPath, renderer)) {
                            albedoViews[i] = tex->imageView;
                            albedoSamplers[i] = tex->sampler;
                        }
                    }
                    if (!terrain.layers[i].normalPath.empty()) {
                        if (Texture* tex = renderer.resourceManager->loadTexture(terrain.layers[i].normalPath, renderer)) {
                            normalViews[i] = tex->imageView;
                            normalSamplers[i] = tex->sampler;
                        }
                    }
                }
            }

            if (terrain.terrainDescriptorSet == VK_NULL_HANDLE) {
                renderer.descriptors.allocateTerrainDescriptorSet(
                    terrain.terrainDescriptorSet,
                    terrain.splatmapView, terrain.splatmapSampler,
                    albedoViews, albedoSamplers,
                    normalViews, normalSamplers,
                    terrain.terrainUboBuffer, uboSize
                );
            } else {
                renderer.descriptors.updateTerrainDescriptorSet(
                    terrain.terrainDescriptorSet,
                    terrain.splatmapView, terrain.splatmapSampler,
                    albedoViews, albedoSamplers,
                    normalViews, normalSamplers,
                    terrain.terrainUboBuffer, uboSize
                );
            }

            terrain.layersDirty = false;
            terrain.terrainGpuInitialized = true;
        }

        if (mat) {
            if (mat->pipeline == VK_NULL_HANDLE || mat->shaderName != "Terrain") {
                PipelineHandle pipeline = renderer.createPipelineForShaders(
                    renderer.resolveShaderPath("build/shaders/terrain.vert.spv"),
                    renderer.resolveShaderPath("build/shaders/terrain.frag.spv"),
                    {
                        renderer.descriptors.getCameraDescriptorSetLayout(),
                        renderer.descriptors.getTerrainDescriptorSetLayout()
                    }
                );
                mat->pipeline = pipeline.pipeline;
                mat->pipelineLayout = pipeline.layout;
                mat->shaderName = "Terrain";
                mat->roughness = 0.85f;
                mat->metallic = 0.0f;
            }
            mat->descriptorSet = terrain.terrainDescriptorSet;
        }
    }

    void TerrainSystem::syncTerrainEntity(Entity entity, TerrainComponent& terrain) {
        // 1. Ensure Transform component
        if (!registry.has<Transform>(entity)) {
            registry.emplace<Transform>(entity, Transform{ glm::vec3(0.0f) });
        }

        // 2. Ensure Material component configured for PBR Terrain shading
        auto* mat = registry.get<Material>(entity);
        if (!mat) {
            registry.emplace<Material>(entity, Material{ glm::vec4(1.0f) });
            mat = registry.get<Material>(entity);
        }

        // Synchronize GPU splatmap, UBO, textures, and pipeline
        syncTerrainGPUResources(renderer, terrain, mat);

        // 0. Quick dirty check: if mesh is already initialized and heightfield is not dirty, nothing to rebuild
        if (terrain.isMeshInitialized && !terrain.isDirty) {
            return;
        }

        // 3. If parent has Mesh component, hide it so only chunk sub-meshes render
        if (auto* parentMesh = registry.get<Mesh>(entity)) {
            parentMesh->visible = false;
        }

        // 4. Ensure memory and grid dimensions are valid
        terrain.ensureAllocated();

        // 5. Ensure shared index buffer for this chunk resolution
        uint32_t chunkRes = terrain.chunkResolution;
        VkBuffer sharedIbHandle = VK_NULL_HANDLE;
        for (const auto& ib : sharedIndexBuffers) {
            if (ib.resolution == chunkRes) {
                sharedIbHandle = ib.buffer;
                break;
            }
        }

        if (sharedIbHandle == VK_NULL_HANDLE) {
            std::vector<uint32_t> indices;
            terrain.buildChunkIndices(indices);

            VkDevice dev = renderer.getDevice();
            VkPhysicalDevice phys = renderer.device.getPhysicalDevice();

            SharedIndexBuffer newIb{};
            newIb.resolution = chunkRes;
            newIb.count = static_cast<uint32_t>(indices.size());

            VkDeviceSize size = indices.size() * sizeof(uint32_t);

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(dev, &bufferInfo, nullptr, &newIb.buffer) == VK_SUCCESS) {
                VkMemoryRequirements memReq;
                vkGetBufferMemoryRequirements(dev, newIb.buffer, &memReq);

                VkPhysicalDeviceMemoryProperties memProps;
                vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

                uint32_t memoryTypeIndex = UINT32_MAX;
                VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                    if ((memReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                        memoryTypeIndex = i;
                        break;
                    }
                }

                if (memoryTypeIndex != UINT32_MAX) {
                    VkMemoryAllocateInfo allocInfo{};
                    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                    allocInfo.allocationSize = memReq.size;
                    allocInfo.memoryTypeIndex = memoryTypeIndex;

                    if (vkAllocateMemory(dev, &allocInfo, nullptr, &newIb.memory) == VK_SUCCESS) {
                        vkBindBufferMemory(dev, newIb.buffer, newIb.memory, 0);

                        void* dst = nullptr;
                        if (vkMapMemory(dev, newIb.memory, 0, size, 0, &dst) == VK_SUCCESS) {
                            std::memcpy(dst, indices.data(), static_cast<size_t>(size));
                            vkUnmapMemory(dev, newIb.memory);
                        }
                    }
                }
            }

            sharedIbHandle = newIb.buffer;
            sharedIndexBuffers.push_back(newIb);
        }

        // 6. Ensure internal chunks vector matches chunk counts
        size_t targetChunkCount = static_cast<size_t>(terrain.chunkCountX) * terrain.chunkCountZ;
        bool needRecreate = (terrain.chunks.size() != targetChunkCount);
        if (!needRecreate) {
            size_t expectedVertsPerChunk = static_cast<size_t>(terrain.chunkResolution) * terrain.chunkResolution;
            for (const auto& chunk : terrain.chunks) {
                if (chunk.mesh.vertices.size() != expectedVertsPerChunk || chunk.mesh.vertexBuffer == VK_NULL_HANDLE) {
                    needRecreate = true;
                    break;
                }
            }
        }

        if (needRecreate) {
            destroyChunks(terrain);
            terrain.chunks.resize(targetChunkCount);
            terrain.chunkDirty.assign(targetChunkCount, true);

            uint32_t stride = terrain.getChunkStride();

            for (uint32_t cz = 0; cz < terrain.chunkCountZ; ++cz) {
                for (uint32_t cx = 0; cx < terrain.chunkCountX; ++cx) {
                    size_t cIdx = static_cast<size_t>(cz) * terrain.chunkCountX + cx;
                    auto& chunk = terrain.chunks[cIdx];
                    chunk.chunkX = cx;
                    chunk.chunkZ = cz;
                    chunk.startVertexX = cx * stride;
                    chunk.startVertexZ = cz * stride;
                    chunk.isVisible = true;
                    chunk.mesh = Mesh{};
                    chunk.mesh.indexBuffer = sharedIbHandle;
                }
            }
        }

        // 7. Collect dirty chunks
        std::vector<uint32_t> dirtyChunkIndices;
        dirtyChunkIndices.reserve(terrain.chunks.size());
        for (size_t i = 0; i < terrain.chunks.size(); ++i) {
            if (terrain.chunkDirty[i] || !terrain.isMeshInitialized || terrain.chunks[i].mesh.vertices.empty()) {
                dirtyChunkIndices.push_back(static_cast<uint32_t>(i));
            }
        }

        // 8. Generate vertices & normals in parallel across all worker threads!
        if (!dirtyChunkIndices.empty()) {
            size_t expectedVerts = static_cast<size_t>(terrain.chunkResolution) * terrain.chunkResolution;

            JobSystem::getInstance().parallelFor(static_cast<int>(dirtyChunkIndices.size()), [&](int idx) {
                uint32_t cIdx = dirtyChunkIndices[idx];
                auto& chunk = terrain.chunks[cIdx];
                uint32_t cx = chunk.chunkX;
                uint32_t cz = chunk.chunkZ;

                if (chunk.mesh.vertices.size() != expectedVerts) {
                    terrain.buildChunkVertices(cx, cz, chunk.mesh.vertices, chunk.aabbMin, chunk.aabbMax);
                } else {
                    terrain.updateChunkVertices(cx, cz, chunk.mesh.vertices, chunk.aabbMin, chunk.aabbMax);
                }
                chunk.mesh.indexBuffer = sharedIbHandle;
            });

            // 9. Upload dirty vertex buffers to GPU
            VkDevice dev = renderer.getDevice();
            VkPhysicalDevice phys = renderer.device.getPhysicalDevice();

            for (uint32_t cIdx : dirtyChunkIndices) {
                auto& chunk = terrain.chunks[cIdx];
                uploadChunkVertexBuffer(dev, phys, chunk);
                terrain.chunkDirty[cIdx] = false;
            }
        }

        terrain.isMeshInitialized = true;
        terrain.isDirty = false;
        terrain.hasPartialDirty = false;

        if (mat) {
            mat->descriptorSet = terrain.terrainDescriptorSet;
        }

        // 10. Clean up any legacy or rogue ColliderComponent or TilemapComponent on terrain entity.
        // Terrains use accurate O(1) surface heightfield collision in PhysicsSystem.
        // Tilemaps are strictly 2D and should never exist on a 3D terrain entity.
        if (registry.has<ColliderComponent>(entity)) {
            registry.remove<ColliderComponent>(entity);
        }
        if (registry.has<Engine::TilemapComponent>(entity)) {
            registry.remove<Engine::TilemapComponent>(entity);
        }

        // 11. Synchronize GPU instanced foliage & grass
        syncTerrainFoliage(terrain);
    }

    void TerrainSystem::syncTerrainFoliage(TerrainComponent& terrain) {
        if (!terrain.foliageEnabled) {
            return;
        }

        VkDevice dev = renderer.getDevice();
        if (dev == VK_NULL_HANDLE) return;

        // 1. Ensure Foliage Pipeline
        if (terrain.foliagePipeline == VK_NULL_HANDLE) {
            std::vector<VkVertexInputBindingDescription> bindings(2);
            bindings[0].binding = 0;
            bindings[0].stride = sizeof(Vertex);
            bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            bindings[1].binding = 1;
            bindings[1].stride = sizeof(FoliageInstanceGPU);
            bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

            std::vector<VkVertexInputAttributeDescription> attrs(8);
            // Vertex attributes (locations 0..4)
            attrs[0].binding = 0;
            attrs[0].location = 0;
            attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrs[0].offset = offsetof(Vertex, position);

            attrs[1].binding = 0;
            attrs[1].location = 1;
            attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrs[1].offset = offsetof(Vertex, normal);

            attrs[2].binding = 0;
            attrs[2].location = 2;
            attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
            attrs[2].offset = offsetof(Vertex, uv);

            attrs[3].binding = 0;
            attrs[3].location = 3;
            attrs[3].format = VK_FORMAT_R32G32B32A32_SINT;
            attrs[3].offset = offsetof(Vertex, boneIDs);

            attrs[4].binding = 0;
            attrs[4].location = 4;
            attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrs[4].offset = offsetof(Vertex, boneWeights);

            // FoliageInstanceGPU attributes (locations 5..7)
            attrs[5].binding = 1;
            attrs[5].location = 5;
            attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrs[5].offset = offsetof(FoliageInstanceGPU, positionAndScale);

            attrs[6].binding = 1;
            attrs[6].location = 6;
            attrs[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrs[6].offset = offsetof(FoliageInstanceGPU, rotationAndWind);

            attrs[7].binding = 1;
            attrs[7].location = 7;
            attrs[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrs[7].offset = offsetof(FoliageInstanceGPU, color);

            PipelineHandle handle = renderer.createPipelineForShaders(
                renderer.resolveShaderPath("build/shaders/foliage.vert.spv"),
                renderer.resolveShaderPath("build/shaders/foliage.frag.spv"),
                {
                    renderer.descriptors.getCameraDescriptorSetLayout(),
                    renderer.descriptors.getSingleTextureDescriptorSetLayout()
                },
                bindings,
                attrs,
                VK_CULL_MODE_NONE
            );

            terrain.foliagePipeline = handle.pipeline;
            terrain.foliagePipelineLayout = handle.layout;
        }

        // 2. Build procedural mesh and texture
        buildProceduralGrassMesh(terrain);
        buildProceduralGrassTexture(terrain);

        // 3. Generate and upload foliage instances
        if (terrain.foliageNeedsRebuild) {
            JobSystem::getInstance().parallelFor(static_cast<int>(terrain.chunks.size()), [&](int i) {
                generateChunkFoliage(terrain, static_cast<uint32_t>(i));
            });

            for (size_t i = 0; i < terrain.chunks.size(); ++i) {
                uploadChunkFoliageBuffer(terrain, static_cast<uint32_t>(i));
            }
            terrain.foliageNeedsRebuild = false;
        } else {
            for (size_t i = 0; i < terrain.chunks.size(); ++i) {
                if (terrain.chunks[i].foliageDirty || 
                    (terrain.chunks[i].foliageBuffer == VK_NULL_HANDLE && !terrain.chunks[i].foliageInstances.empty())) {
                    uploadChunkFoliageBuffer(terrain, static_cast<uint32_t>(i));
                }
            }
        }
    }

    void TerrainSystem::buildProceduralGrassMesh(TerrainComponent& terrain) {
        if (terrain.foliageVertexBuffer != VK_NULL_HANDLE && terrain.foliageIndexBuffer != VK_NULL_HANDLE) {
            return;
        }

        VkDevice dev = renderer.getDevice();
        VkPhysicalDevice phys = renderer.device.getPhysicalDevice();
        if (dev == VK_NULL_HANDLE || phys == VK_NULL_HANDLE) return;

        // Custom mesh support (loaded from obj, gltf, glb, etc.)
        std::string customMeshPath = terrain.foliageMeshPath;
        if (customMeshPath.empty()) {
            for (const auto& proto : terrain.detailPalette) {
                if (proto.type == DetailType::GrassClump && !proto.meshPath.empty()) {
                    customMeshPath = proto.meshPath;
                    break;
                }
            }
        }

        if (renderer.resourceManager && !customMeshPath.empty()) {
            try {
                Mesh loaded = renderer.resourceManager->loadMesh(customMeshPath, renderer);
                if (loaded.vertexBuffer != VK_NULL_HANDLE && loaded.indexBuffer != VK_NULL_HANDLE && !loaded.indices.empty()) {
                    terrain.foliageVertexBuffer = loaded.vertexBuffer;
                    terrain.foliageVertexMemory = loaded.vertexBufferMemory;
                    terrain.foliageIndexBuffer = loaded.indexBuffer;
                    terrain.foliageIndexMemory = loaded.indexBufferMemory;
                    terrain.foliageIndexCount = static_cast<uint32_t>(loaded.indices.size());
                    return;
                }
            } catch (...) {}
        }

        // 3 crossed quads rotated by 0, 60, 120 degrees around local Y
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        const float quadWidth = 0.9f;
        const float quadHeight = 1.0f;
        const float halfW = quadWidth * 0.5f;
        const float angles[3] = { 0.0f, 1.04719755f, 2.0943951f };

        for (int q = 0; q < 3; ++q) {
            float angle = angles[q];
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            glm::vec3 tangent(cosA, 0.0f, sinA);
            glm::vec3 normal(-sinA, 0.0f, cosA);

            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

            glm::vec3 p0 = -tangent * halfW;
            glm::vec3 p1 = tangent * halfW;
            glm::vec3 p2 = tangent * halfW + glm::vec3(0.0f, quadHeight, 0.0f);
            glm::vec3 p3 = -tangent * halfW + glm::vec3(0.0f, quadHeight, 0.0f);

            // UVs: v=1.0 at base, v=0.0 at tip
            vertices.emplace_back(p0, normal, glm::vec2(0.0f, 1.0f));
            vertices.emplace_back(p1, normal, glm::vec2(1.0f, 1.0f));
            vertices.emplace_back(p2, normal, glm::vec2(1.0f, 0.0f));
            vertices.emplace_back(p3, normal, glm::vec2(0.0f, 0.0f));

            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);

            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);
        }

        terrain.foliageIndexCount = static_cast<uint32_t>(indices.size());

        auto createHostBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& outBuffer, VkDeviceMemory& outMemory, const void* data) {
            VkBufferCreateInfo bInfo{};
            bInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bInfo.size = size;
            bInfo.usage = usage;
            bInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(dev, &bInfo, nullptr, &outBuffer);

            VkMemoryRequirements req;
            vkGetBufferMemoryRequirements(dev, outBuffer, &req);

            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
            uint32_t memType = UINT32_MAX;
            VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((req.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                    memType = i;
                    break;
                }
            }

            VkMemoryAllocateInfo aInfo{};
            aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            aInfo.allocationSize = req.size;
            aInfo.memoryTypeIndex = memType;
            vkAllocateMemory(dev, &aInfo, nullptr, &outMemory);
            vkBindBufferMemory(dev, outBuffer, outMemory, 0);

            void* dst = nullptr;
            if (vkMapMemory(dev, outMemory, 0, size, 0, &dst) == VK_SUCCESS) {
                std::memcpy(dst, data, static_cast<size_t>(size));
                vkUnmapMemory(dev, outMemory);
            }
        };

        createHostBuffer(vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, terrain.foliageVertexBuffer, terrain.foliageVertexMemory, vertices.data());
        createHostBuffer(indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, terrain.foliageIndexBuffer, terrain.foliageIndexMemory, indices.data());
    }

    void TerrainSystem::buildProceduralGrassTexture(TerrainComponent& terrain) {
        if (terrain.foliageTextureView != VK_NULL_HANDLE && terrain.foliageDescriptorSet != VK_NULL_HANDLE) {
            return;
        }

        VkDevice dev = renderer.getDevice();
        VkPhysicalDevice phys = renderer.device.getPhysicalDevice();
        if (dev == VK_NULL_HANDLE || phys == VK_NULL_HANDLE) return;

        // Custom texture support
        std::string customTexPath = terrain.foliageTexturePath;
        if (customTexPath.empty()) {
            for (const auto& proto : terrain.detailPalette) {
                if (proto.type == DetailType::GrassClump && !proto.texturePath.empty()) {
                    customTexPath = proto.texturePath;
                    break;
                }
            }
        }

        if (renderer.resourceManager && !customTexPath.empty()) {
            if (Texture* tex = renderer.resourceManager->loadTexture(customTexPath, renderer)) {
                terrain.foliageTextureView = tex->imageView;
                terrain.foliageTextureSampler = tex->sampler;
                if (terrain.foliageDescriptorSet == VK_NULL_HANDLE) {
                    renderer.descriptors.allocateSingleTextureDescriptorSet(terrain.foliageDescriptorSet, tex->imageView, tex->sampler);
                } else {
                    renderer.descriptors.updateSingleTextureDescriptorSet(terrain.foliageDescriptorSet, tex->imageView, tex->sampler);
                }
                return;
            }
        }

        // Procedural grass blade texture (128x128 RGBA8)
        const uint32_t texW = 128;
        const uint32_t texH = 128;
        std::vector<uint8_t> pixels(texW * texH * 4, 0);

        struct Blade {
            float xCenter;
            float height;
            float widthBase;
            float curve;
        };
        Blade blades[5] = {
            { 0.18f, 0.85f, 0.08f, -0.04f },
            { 0.35f, 1.00f, 0.09f,  0.02f },
            { 0.50f, 0.94f, 0.10f, -0.01f },
            { 0.67f, 0.98f, 0.08f,  0.03f },
            { 0.82f, 0.88f, 0.07f, -0.03f }
        };

        for (uint32_t y = 0; y < texH; ++y) {
            float v = static_cast<float>(y) / static_cast<float>(texH - 1);
            float heightFromBottom = 1.0f - v;

            for (uint32_t x = 0; x < texW; ++x) {
                float u = static_cast<float>(x) / static_cast<float>(texW - 1);

                float bladeAlpha = 0.0f;
                glm::vec3 bladeColor(0.20f, 0.50f, 0.15f);

                for (const auto& b : blades) {
                    if (heightFromBottom > b.height) continue;

                    float prog = heightFromBottom / b.height;
                    float currentCenter = b.xCenter + b.curve * prog * prog;
                    float currentHalfWidth = (b.widthBase * 0.5f) * (1.0f - prog * 0.85f);

                    float distToCenter = std::abs(u - currentCenter);
                    if (distToCenter < currentHalfWidth) {
                        float edgeSoft = 1.0f - (distToCenter / currentHalfWidth);
                        float a = std::min(1.0f, edgeSoft * 3.0f);
                        if (a > bladeAlpha) {
                            bladeAlpha = a;
                            bladeColor = glm::mix(glm::vec3(0.18f, 0.42f, 0.12f), glm::vec3(0.55f, 0.82f, 0.22f), prog);
                        }
                    }
                }

                size_t pIdx = (static_cast<size_t>(y) * texW + x) * 4;
                pixels[pIdx + 0] = static_cast<uint8_t>(std::clamp(bladeColor.r * 255.0f, 0.0f, 255.0f));
                pixels[pIdx + 1] = static_cast<uint8_t>(std::clamp(bladeColor.g * 255.0f, 0.0f, 255.0f));
                pixels[pIdx + 2] = static_cast<uint8_t>(std::clamp(bladeColor.b * 255.0f, 0.0f, 255.0f));
                pixels[pIdx + 3] = static_cast<uint8_t>(std::clamp(bladeAlpha * 255.0f, 0.0f, 255.0f));
            }
        }

        VkDeviceSize imgSize = texW * texH * 4;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        VkBufferCreateInfo sInfo{};
        sInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sInfo.size = imgSize;
        sInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        sInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev, &sInfo, nullptr, &stagingBuffer);

        VkMemoryRequirements sReq;
        vkGetBufferMemoryRequirements(dev, stagingBuffer, &sReq);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
        uint32_t sMemType = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((sReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                sMemType = i;
                break;
            }
        }

        VkMemoryAllocateInfo saInfo{};
        saInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        saInfo.allocationSize = sReq.size;
        saInfo.memoryTypeIndex = sMemType;
        vkAllocateMemory(dev, &saInfo, nullptr, &stagingMemory);
        vkBindBufferMemory(dev, stagingBuffer, stagingMemory, 0);

        void* sDst = nullptr;
        if (vkMapMemory(dev, stagingMemory, 0, imgSize, 0, &sDst) == VK_SUCCESS) {
            std::memcpy(sDst, pixels.data(), imgSize);
            vkUnmapMemory(dev, stagingMemory);
        }

        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.extent.width = texW;
        imgInfo.extent.height = texH;
        imgInfo.extent.depth = 1;
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        vkCreateImage(dev, &imgInfo, nullptr, &terrain.foliageTextureImage);

        VkMemoryRequirements iReq;
        vkGetImageMemoryRequirements(dev, terrain.foliageTextureImage, &iReq);
        uint32_t iMemType = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((iReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                iMemType = i;
                break;
            }
        }

        VkMemoryAllocateInfo iaInfo{};
        iaInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        iaInfo.allocationSize = iReq.size;
        iaInfo.memoryTypeIndex = iMemType;
        vkAllocateMemory(dev, &iaInfo, nullptr, &terrain.foliageTextureMemory);
        vkBindImageMemory(dev, terrain.foliageTextureImage, terrain.foliageTextureMemory, 0);

        VkCommandBuffer cmd = renderer.beginSingleUseCommands();

        VkImageMemoryBarrier b1{};
        b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.image = terrain.foliageTextureImage;
        b1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b1.subresourceRange.baseMipLevel = 0;
        b1.subresourceRange.levelCount = 1;
        b1.subresourceRange.baseArrayLayer = 0;
        b1.subresourceRange.layerCount = 1;
        b1.srcAccessMask = 0;
        b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { texW, texH, 1 };
        vkCmdCopyBufferToImage(cmd, stagingBuffer, terrain.foliageTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier b2 = b1;
        b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b2);

        renderer.endSingleUseCommands(cmd);

        vkDestroyBuffer(dev, stagingBuffer, nullptr);
        vkFreeMemory(dev, stagingMemory, nullptr);

        VkImageViewCreateInfo vInfo{};
        vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vInfo.image = terrain.foliageTextureImage;
        vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        vInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vInfo.subresourceRange.baseMipLevel = 0;
        vInfo.subresourceRange.levelCount = 1;
        vInfo.subresourceRange.baseArrayLayer = 0;
        vInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(dev, &vInfo, nullptr, &terrain.foliageTextureView);

        VkSamplerCreateInfo smpInfo{};
        smpInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        smpInfo.magFilter = VK_FILTER_LINEAR;
        smpInfo.minFilter = VK_FILTER_LINEAR;
        smpInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        smpInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        smpInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        smpInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        smpInfo.maxAnisotropy = 1.0f;
        vkCreateSampler(dev, &smpInfo, nullptr, &terrain.foliageTextureSampler);

        renderer.descriptors.allocateSingleTextureDescriptorSet(
            terrain.foliageDescriptorSet,
            terrain.foliageTextureView,
            terrain.foliageTextureSampler
        );
    }

    void TerrainSystem::generateChunkFoliage(TerrainComponent& terrain, uint32_t chunkIdx) {
        if (chunkIdx >= terrain.chunks.size()) return;
        auto& chunk = terrain.chunks[chunkIdx];
        chunk.foliageInstances.clear();

        if (!terrain.foliageEnabled || terrain.foliageDensity <= 0.001f) {
            chunk.foliageInstanceCount = 0;
            chunk.foliageDirty = false;
            return;
        }

        float halfX = terrain.sizeX * 0.5f;
        float halfZ = terrain.sizeZ * 0.5f;
        float chunkSizeX = terrain.sizeX / static_cast<float>(terrain.chunkCountX);
        float chunkSizeZ = terrain.sizeZ / static_cast<float>(terrain.chunkCountZ);

        float chunkMinX = -halfX + static_cast<float>(chunk.chunkX) * chunkSizeX;
        float chunkMinZ = -halfZ + static_cast<float>(chunk.chunkZ) * chunkSizeZ;

        float chunkArea = chunkSizeX * chunkSizeZ;
        uint32_t candidateCount = static_cast<uint32_t>(chunkArea * (terrain.foliageDensity * 0.04f));
        candidateCount = std::clamp(candidateCount, 16u, 2500u);

        uint32_t seed = (chunk.chunkX * 73856093u) ^ (chunk.chunkZ * 19349663u) ^ static_cast<uint32_t>(terrain.noiseSeed);
        auto nextRand = [&seed]() -> float {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(seed & 0x00FFFFFFu) / 16777215.0f;
        };

        uint32_t smRes = terrain.splatmapResolution;
        bool hasSplatmap = !terrain.splatmapData.empty() && terrain.splatmapData.size() == (static_cast<size_t>(smRes) * smRes * 4);
        int targetLayer = terrain.foliageGrassLayer; // -1 = All Layers, 0..3 = Specific Layer

        for (uint32_t i = 0; i < candidateCount; ++i) {
            float lx = chunkMinX + nextRand() * chunkSizeX;
            float lz = chunkMinZ + nextRand() * chunkSizeZ;

            // Splatmap weight check (if targetLayer >= 0, filter by that layer; if -1, allow on all layers)
            float grassWeight = 1.0f;
            if (hasSplatmap && targetLayer >= 0 && targetLayer <= 3) {
                float u = (lx + halfX) / terrain.sizeX;
                float v = (lz + halfZ) / terrain.sizeZ;
                int px = std::clamp(static_cast<int>(u * static_cast<float>(smRes)), 0, static_cast<int>(smRes - 1));
                int py = std::clamp(static_cast<int>(v * static_cast<float>(smRes)), 0, static_cast<int>(smRes - 1));
                size_t pIdx = (static_cast<size_t>(py) * smRes + px) * 4;
                grassWeight = static_cast<float>(terrain.splatmapData[pIdx + targetLayer]) / 255.0f;
                if (grassWeight < terrain.foliageMinWeight) {
                    continue;
                }
            }

            // Slope check: steep cliffs should not spawn dense grass
            glm::vec3 N = terrain.getInterpolatedNormal(lx, lz);
            float slope = 1.0f - std::max(0.0f, N.y);
            if (slope > terrain.foliageMaxSlope) {
                continue;
            }

            float ly = terrain.getInterpolatedHeight(lx, lz);

            // Random clump scale modulated by layer weight
            float baseScale = terrain.foliageScaleMin + nextRand() * (terrain.foliageScaleMax - terrain.foliageScaleMin);
            float scale = baseScale * (0.6f + 0.4f * grassWeight);

            float yaw = nextRand() * 6.2831853f;
            float windPhase = nextRand() * 6.2831853f;
            float tiltFactor = (nextRand() - 0.5f) * 0.4f;

            float tintVar = 0.90f + nextRand() * 0.20f;
            glm::vec4 color = terrain.foliageTint * glm::vec4(tintVar, tintVar * 1.04f, tintVar * 0.96f, 1.0f);

            FoliageInstanceGPU inst{};
            inst.positionAndScale = glm::vec4(lx, ly, lz, scale);
            inst.rotationAndWind = glm::vec4(yaw, windPhase, tiltFactor, 0.0f);
            inst.color = color;

            chunk.foliageInstances.push_back(inst);
        }

        chunk.foliageInstanceCount = static_cast<uint32_t>(chunk.foliageInstances.size());
        chunk.foliageDirty = false;
    }

    void TerrainSystem::uploadChunkFoliageBuffer(TerrainComponent& terrain, uint32_t chunkIdx) {
        if (chunkIdx >= terrain.chunks.size()) return;
        auto& chunk = terrain.chunks[chunkIdx];

        VkDevice dev = renderer.getDevice();
        VkPhysicalDevice phys = renderer.device.getPhysicalDevice();
        if (dev == VK_NULL_HANDLE || phys == VK_NULL_HANDLE) return;

        if (chunk.foliageInstances.empty()) {
            chunk.foliageInstanceCount = 0;
            chunk.foliageDirty = false;
            return;
        }

        VkDeviceSize requiredSize = chunk.foliageInstances.size() * sizeof(FoliageInstanceGPU);

        // If buffer already exists and has enough capacity, simply copy data without destroying or allocating
        if (chunk.foliageBuffer != VK_NULL_HANDLE && chunk.foliageBufferMemory != VK_NULL_HANDLE && requiredSize <= chunk.foliageBufferCapacity) {
            void* dst = nullptr;
            if (vkMapMemory(dev, chunk.foliageBufferMemory, 0, requiredSize, 0, &dst) == VK_SUCCESS) {
                std::memcpy(dst, chunk.foliageInstances.data(), static_cast<size_t>(requiredSize));
                vkUnmapMemory(dev, chunk.foliageBufferMemory);
            }
            chunk.foliageInstanceCount = static_cast<uint32_t>(chunk.foliageInstances.size());
            chunk.foliageDirty = false;
            return;
        }

        // Buffer needs (re)allocation: wait for GPU to be idle to ensure no in-flight commands use old buffer
        vkDeviceWaitIdle(dev);

        if (chunk.foliageBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(dev, chunk.foliageBuffer, nullptr);
            chunk.foliageBuffer = VK_NULL_HANDLE;
        }
        if (chunk.foliageBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(dev, chunk.foliageBufferMemory, nullptr);
            chunk.foliageBufferMemory = VK_NULL_HANDLE;
        }
        chunk.foliageBufferCapacity = 0;

        // Allocate with headroom (at least 256 instances, or 1.5x required) to minimize future reallocations
        VkDeviceSize allocSize = std::max(static_cast<VkDeviceSize>(requiredSize * 1.5f), static_cast<VkDeviceSize>(256 * sizeof(FoliageInstanceGPU)));

        VkBufferCreateInfo bInfo{};
        bInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bInfo.size = allocSize;
        bInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev, &bInfo, nullptr, &chunk.foliageBuffer) != VK_SUCCESS) {
            chunk.foliageDirty = false;
            return;
        }

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, chunk.foliageBuffer, &req);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
        uint32_t memType = UINT32_MAX;
        VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((req.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                memType = i;
                break;
            }
        }

        if (memType == UINT32_MAX) {
            vkDestroyBuffer(dev, chunk.foliageBuffer, nullptr);
            chunk.foliageBuffer = VK_NULL_HANDLE;
            chunk.foliageDirty = false;
            return;
        }

        VkMemoryAllocateInfo aInfo{};
        aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        aInfo.allocationSize = req.size;
        aInfo.memoryTypeIndex = memType;
        if (vkAllocateMemory(dev, &aInfo, nullptr, &chunk.foliageBufferMemory) != VK_SUCCESS) {
            vkDestroyBuffer(dev, chunk.foliageBuffer, nullptr);
            chunk.foliageBuffer = VK_NULL_HANDLE;
            chunk.foliageDirty = false;
            return;
        }
        vkBindBufferMemory(dev, chunk.foliageBuffer, chunk.foliageBufferMemory, 0);
        chunk.foliageBufferCapacity = allocSize;

        void* dst = nullptr;
        if (vkMapMemory(dev, chunk.foliageBufferMemory, 0, requiredSize, 0, &dst) == VK_SUCCESS) {
            std::memcpy(dst, chunk.foliageInstances.data(), static_cast<size_t>(requiredSize));
            vkUnmapMemory(dev, chunk.foliageBufferMemory);
        }

        chunk.foliageInstanceCount = static_cast<uint32_t>(chunk.foliageInstances.size());
        chunk.foliageDirty = false;
    }

    bool TerrainSystem::raycast(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        const glm::mat4& worldMatrix,
        const TerrainComponent& terrain,
        glm::vec3& outHitWorldPos,
        glm::vec2& outHitLocalXZ
    ) {
        if (terrain.heights.empty() || terrain.resolutionX < 2 || terrain.resolutionZ < 2) {
            return false;
        }

        glm::mat4 invModel = glm::inverse(worldMatrix);
        glm::vec4 localOrigin4 = invModel * glm::vec4(rayOrigin, 1.0f);
        if (std::abs(localOrigin4.w) < 0.0001f) return false;
        glm::vec3 rOrig = glm::vec3(localOrigin4) / localOrigin4.w;
        glm::vec3 rDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));

        float halfX = terrain.sizeX * 0.5f;
        float halfZ = terrain.sizeZ * 0.5f;

        // 1. Overall bounding envelope test
        float minH = std::min(terrain.boundsMinY - 20.0f, rOrig.y - 20.0f);
        float maxH = std::max(terrain.boundsMaxY + 20.0f, rOrig.y + 20.0f);

        glm::vec3 globalBoxMin(-halfX, minH, -halfZ);
        glm::vec3 globalBoxMax( halfX, maxH,  halfZ);

        float gtmin = 0.0f;
        float gtmax = 1e6f;

        for (int i = 0; i < 3; ++i) {
            float d = rDir[i];
            float invD = 1.0f / (std::abs(d) > 1e-7f ? d : (d >= 0.0f ? 1e-7f : -1e-7f));
            float t0 = (globalBoxMin[i] - rOrig[i]) * invD;
            float t1 = (globalBoxMax[i] - rOrig[i]) * invD;
            if (invD < 0.0f) std::swap(t0, t1);
            gtmin = std::max(gtmin, t0);
            gtmax = std::min(gtmax, t1);
            if (gtmax < gtmin) return false;
        }
        if (gtmax <= 0.0f) return false;

        // 2. Chunk broadphase: test each chunk's AABB
        struct ChunkCandidate {
            uint32_t cx;
            uint32_t cz;
            float tEnter;
            float tExit;
            glm::vec3 boxMin;
            glm::vec3 boxMax;
        };

        std::vector<ChunkCandidate> hitChunks;
        hitChunks.reserve(16);

        uint32_t stride = terrain.getChunkStride();
        float dx = terrain.sizeX / static_cast<float>(terrain.resolutionX - 1);
        float dz = terrain.sizeZ / static_cast<float>(terrain.resolutionZ - 1);

        for (uint32_t cz = 0; cz < terrain.chunkCountZ; ++cz) {
            float chunkMinZ = -halfZ + (cz * stride) * dz;
            float chunkMaxZ = -halfZ + (cz * stride + terrain.chunkResolution - 1) * dz;

            for (uint32_t cx = 0; cx < terrain.chunkCountX; ++cx) {
                float chunkMinX = -halfX + (cx * stride) * dx;
                float chunkMaxX = -halfX + (cx * stride + terrain.chunkResolution - 1) * dx;

                // Chunk vertical range with safety margin
                glm::vec3 cBoxMin(chunkMinX, terrain.boundsMinY - 5.0f, chunkMinZ);
                glm::vec3 cBoxMax(chunkMaxX, terrain.boundsMaxY + 5.0f, chunkMaxZ);

                float ctmin = 0.0f;
                float ctmax = 1e6f;

                for (int i = 0; i < 3; ++i) {
                    float d = rDir[i];
                    float invD = 1.0f / (std::abs(d) > 1e-7f ? d : (d >= 0.0f ? 1e-7f : -1e-7f));
                    float t0 = (cBoxMin[i] - rOrig[i]) * invD;
                    float t1 = (cBoxMax[i] - rOrig[i]) * invD;
                    if (invD < 0.0f) std::swap(t0, t1);
                    ctmin = std::max(ctmin, t0);
                    ctmax = std::min(ctmax, t1);
                    if (ctmax < ctmin) break;
                }

                if (ctmax >= ctmin && ctmax > 0.0f) {
                    hitChunks.push_back({ cx, cz, std::max(0.0f, ctmin), ctmax, cBoxMin, cBoxMax });
                }
            }
        }

        if (hitChunks.empty()) return false;

        // Sort chunks front-to-back along the ray
        std::sort(hitChunks.begin(), hitChunks.end(), [](const ChunkCandidate& a, const ChunkCandidate& b) {
            return a.tEnter < b.tEnter;
        });

        // 3. Narrowphase: raymarch only through intersecting chunks
        float cellDx = dx;
        float cellDz = dz;
        float minStep = std::max(0.04f, std::min(cellDx, cellDz) * 0.65f);

        for (const auto& chunk : hitChunks) {
            float startT = chunk.tEnter;
            float endT = chunk.tExit;

            glm::vec3 p0 = rOrig + startT * rDir;
            float clampedX0 = std::clamp(p0.x, chunk.boxMin.x, chunk.boxMax.x);
            float clampedZ0 = std::clamp(p0.z, chunk.boxMin.z, chunk.boxMax.z);
            float h0 = terrain.getInterpolatedHeight(clampedX0, clampedZ0);
            if (p0.y <= h0 && startT > 0.0f) {
                outHitLocalXZ = glm::vec2(clampedX0, clampedZ0);
                outHitWorldPos = glm::vec3(worldMatrix * glm::vec4(clampedX0, h0, clampedZ0, 1.0f));
                return true;
            }

            float curT = startT;
            float prevT = curT;
            bool found = false;

            while (curT <= endT) {
                glm::vec3 pos = rOrig + curT * rDir;
                float clampedX = std::clamp(pos.x, chunk.boxMin.x, chunk.boxMax.x);
                float clampedZ = std::clamp(pos.z, chunk.boxMin.z, chunk.boxMax.z);
                float h = terrain.getInterpolatedHeight(clampedX, clampedZ);

                if (pos.y <= h) {
                    found = true;
                    break;
                }
                prevT = curT;

                float vertDist = pos.y - h;
                float step = (vertDist > 1.0f) ? std::max(minStep, vertDist * 0.45f) : minStep;
                curT += step;
                if (curT > endT) {
                    pos = rOrig + endT * rDir;
                    clampedX = std::clamp(pos.x, chunk.boxMin.x, chunk.boxMax.x);
                    clampedZ = std::clamp(pos.z, chunk.boxMin.z, chunk.boxMax.z);
                    h = terrain.getInterpolatedHeight(clampedX, clampedZ);
                    if (pos.y <= h) {
                        prevT = curT - step;
                        curT = endT;
                        found = true;
                    }
                    break;
                }
            }

            if (found) {
                // Bisection refinement for sub-millimeter precision
                float lo = prevT;
                float hi = curT;
                for (int iter = 0; iter < 12; ++iter) {
                    float mid = (lo + hi) * 0.5f;
                    glm::vec3 p = rOrig + mid * rDir;
                    float clampedX = std::clamp(p.x, chunk.boxMin.x, chunk.boxMax.x);
                    float clampedZ = std::clamp(p.z, chunk.boxMin.z, chunk.boxMax.z);
                    float h = terrain.getInterpolatedHeight(clampedX, clampedZ);
                    if (p.y <= h) {
                        hi = mid;
                    } else {
                        lo = mid;
                    }
                }

                float hitT = (lo + hi) * 0.5f;
                glm::vec3 localHit = rOrig + hitT * rDir;
                localHit.x = std::clamp(localHit.x, -halfX, halfX);
                localHit.z = std::clamp(localHit.z, -halfZ, halfZ);
                localHit.y = terrain.getInterpolatedHeight(localHit.x, localHit.z);

                outHitLocalXZ = glm::vec2(localHit.x, localHit.z);
                outHitWorldPos = glm::vec3(worldMatrix * glm::vec4(localHit, 1.0f));
                return true;
            }
        }

        return false;
    }

    void TerrainSystem::synchronizeNeighborBorders(
        Registry& registry,
        Entity targetEntity,
        bool copyFromNeighborsToTarget
    ) {
        if (!registry.isValid(targetEntity)) return;
        auto* targetTerrain = registry.get<TerrainComponent>(targetEntity);
        auto* targetTransform = registry.get<Transform>(targetEntity);
        if (!targetTerrain || !targetTransform) return;

        glm::vec3 targetPos = targetTransform->position;
        float targetSizeX = targetTerrain->sizeX;
        float targetSizeZ = targetTerrain->sizeZ;
        uint32_t targetResX = targetTerrain->resolutionX;
        uint32_t targetResZ = targetTerrain->resolutionZ;

        bool targetModified = false;

        for (auto [neighborEnt, neighborTerrain, neighborTransform] : registry.view<TerrainComponent, Transform>()) {
            if (neighborEnt == targetEntity) continue;

            glm::vec3 nPos = neighborTransform.position;
            float dx = nPos.x - targetPos.x;
            float dz = nPos.z - targetPos.z;
            float dy = std::abs(nPos.y - targetPos.y);
            if (dy > 5.0f) continue; // Not on the same elevation level

            bool neighborModified = false;

            // 1. Neighbor is to the East (+X): target East border touches neighbor West border
            if (std::abs(dx - targetSizeX) < 1.0f && std::abs(dz) < 1.0f) {
                uint32_t zCount = std::min(targetResZ, neighborTerrain.resolutionZ);
                for (uint32_t z = 0; z < zCount; ++z) {
                    if (copyFromNeighborsToTarget) {
                        float h = neighborTerrain.getHeight(0, z);
                        targetTerrain->setHeight(targetResX - 1, z, h);
                    } else {
                        float hTarget = targetTerrain->getHeight(targetResX - 1, z);
                        float hNeighbor = neighborTerrain.getHeight(0, z);
                        float avgH = 0.5f * (hTarget + hNeighbor);
                        targetTerrain->setHeight(targetResX - 1, z, avgH);
                        neighborTerrain.setHeight(0, z, avgH);
                        neighborModified = true;
                    }
                    targetModified = true;
                }
            }
            // 2. Neighbor is to the West (-X): target West border touches neighbor East border
            else if (std::abs(dx + targetSizeX) < 1.0f && std::abs(dz) < 1.0f) {
                uint32_t zCount = std::min(targetResZ, neighborTerrain.resolutionZ);
                for (uint32_t z = 0; z < zCount; ++z) {
                    if (copyFromNeighborsToTarget) {
                        float h = neighborTerrain.getHeight(neighborTerrain.resolutionX - 1, z);
                        targetTerrain->setHeight(0, z, h);
                    } else {
                        float hTarget = targetTerrain->getHeight(0, z);
                        float hNeighbor = neighborTerrain.getHeight(neighborTerrain.resolutionX - 1, z);
                        float avgH = 0.5f * (hTarget + hNeighbor);
                        targetTerrain->setHeight(0, z, avgH);
                        neighborTerrain.setHeight(neighborTerrain.resolutionX - 1, z, avgH);
                        neighborModified = true;
                    }
                    targetModified = true;
                }
            }
            // 3. Neighbor is to the South (+Z): target South border touches neighbor North border
            else if (std::abs(dz - targetSizeZ) < 1.0f && std::abs(dx) < 1.0f) {
                uint32_t xCount = std::min(targetResX, neighborTerrain.resolutionX);
                for (uint32_t x = 0; x < xCount; ++x) {
                    if (copyFromNeighborsToTarget) {
                        float h = neighborTerrain.getHeight(x, 0);
                        targetTerrain->setHeight(x, targetResZ - 1, h);
                    } else {
                        float hTarget = targetTerrain->getHeight(x, targetResZ - 1);
                        float hNeighbor = neighborTerrain.getHeight(x, 0);
                        float avgH = 0.5f * (hTarget + hNeighbor);
                        targetTerrain->setHeight(x, targetResZ - 1, avgH);
                        neighborTerrain.setHeight(x, 0, avgH);
                        neighborModified = true;
                    }
                    targetModified = true;
                }
            }
            // 4. Neighbor is to the North (-Z): target North border touches neighbor South border
            else if (std::abs(dz + targetSizeZ) < 1.0f && std::abs(dx) < 1.0f) {
                uint32_t xCount = std::min(targetResX, neighborTerrain.resolutionX);
                for (uint32_t x = 0; x < xCount; ++x) {
                    if (copyFromNeighborsToTarget) {
                        float h = neighborTerrain.getHeight(x, neighborTerrain.resolutionZ - 1);
                        targetTerrain->setHeight(x, 0, h);
                    } else {
                        float hTarget = targetTerrain->getHeight(x, 0);
                        float hNeighbor = neighborTerrain.getHeight(x, 0);
                        float avgH = 0.5f * (hTarget + hNeighbor);
                        targetTerrain->setHeight(x, 0, avgH);
                        neighborTerrain.setHeight(x, neighborTerrain.resolutionZ - 1, avgH);
                        neighborModified = true;
                    }
                    targetModified = true;
                }
            }

            if (neighborModified) {
                neighborTerrain.recalculateBounds();
                neighborTerrain.isDirty = true;
            }
        }

        if (targetModified) {
            targetTerrain->recalculateBounds();
            targetTerrain->isDirty = true;
        }
    }

} // namespace Engine
