#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "ufbx.h"
#include "scenes/JSONUtils.hpp"
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include "renderer/ResourceManager.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "core/VulkanBuffer.hpp"
#include <iostream>
#include <sstream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <stdexcept>
#include <fstream>
#include <algorithm>

// --- Helper Functions for Vulkan Image Operations ---

static void transitionImageLayout(VulkanRenderer& renderer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = renderer.beginSingleUseCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("Unsupported image layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    renderer.endSingleUseCommands(commandBuffer);
}

static void copyBufferToImage(VulkanRenderer& renderer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = renderer.beginSingleUseCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    renderer.endSingleUseCommands(commandBuffer);
}

static void createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory, ResourceManager& resManager) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = resManager.findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

// --- ResourceManager Implementation ---

uint32_t ResourceManager::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

void ResourceManager::createTextureImageView(VkDevice device, Texture& texture) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = (texture.format != VK_FORMAT_UNDEFINED) ? texture.format : VK_FORMAT_R8G8B8A8_SRGB;

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &texture.imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view!");
    }
}

void ResourceManager::createTextureSampler(VkDevice device, Texture& texture, TextureFilterMode filterMode) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    
    if (filterMode == TextureFilterMode::Nearest) {
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    } else if (filterMode == TextureFilterMode::Bilinear) {
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    } else { // Trilinear
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler!");
    }
}

void ResourceManager::createTextureImage(const std::string& path, VulkanRenderer& renderer, Texture& texture) {
    stbi_set_flip_vertically_on_load(false);
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("Failed to load texture file: " + path);
    }

    VkDeviceSize imageSize = texWidth * texHeight * 4;
    texture.width = texWidth;
    texture.height = texHeight;

    VulkanBuffer stagingBuffer;
    stagingBuffer.create(
        renderer.device.getDevice(),
        renderer.device.getPhysicalDevice(),
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.uploadData(pixels, imageSize);
    stbi_image_free(pixels);

    createImage(
        renderer.device.getDevice(),
        renderer.device.getPhysicalDevice(),
        texWidth,
        texHeight,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        texture.image,
        texture.memory,
        *this
    );

    // Single-pass command buffer for layout transition + buffer copy + final layout transition
    VkCommandBuffer cmd = renderer.beginSingleUseCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
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
    region.imageExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

    vkCmdCopyBufferToImage(cmd, stagingBuffer.get(), texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    renderer.endSingleUseCommands(cmd);

    stagingBuffer.destroy();
}

Texture* ResourceManager::loadTexture(const std::string& rawPath, VulkanRenderer& renderer, TextureFilterMode filterMode) {
    if (rawPath.empty()) return &defaultWhiteTexture;

    static std::unordered_set<std::string> s_failedPaths;
    if (s_failedPaths.count(rawPath)) return nullptr;

    static std::unordered_map<std::string, std::string> s_resolvedPaths;
    auto resIt = s_resolvedPaths.find(rawPath);
    if (resIt != s_resolvedPaths.end()) {
        auto it = textureCache.find(resIt->second);
        if (it != textureCache.end()) {
            return it->second.get();
        }
    }

    auto rawIt = textureCache.find(rawPath);
    if (rawIt != textureCache.end()) {
        return rawIt->second.get();
    }

    std::string path = rawPath;
    if (!std::filesystem::exists(path)) {
        std::vector<std::string> prefixes = {
            "assets/",
            "assets/textures/",
            "sandbox_game/",
            "sandbox_game/assets/",
            "sandbox_game/assets/textures/"
        };
        for (const auto& p : prefixes) {
            std::string cand = p + rawPath;
            if (std::filesystem::exists(cand)) {
                path = cand;
                break;
            }
        }
    }

    if (!std::filesystem::exists(path)) {
        std::string filename = std::filesystem::path(rawPath).filename().string();
        if (!filename.empty()) {
            std::vector<std::string> searchDirs = { "assets", "sandbox_game/assets" };
            for (const auto& dir : searchDirs) {
                if (std::filesystem::exists(dir)) {
                    std::error_code ec;
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
                        if (entry.is_regular_file() && entry.path().filename().string() == filename) {
                            path = entry.path().string();
                            break;
                        }
                    }
                }
                if (std::filesystem::exists(path)) break;
            }
        }
    }

    if (!std::filesystem::exists(path)) {
        s_failedPaths.insert(rawPath);
        return nullptr;
    }

    s_resolvedPaths[rawPath] = path;

    auto it = textureCache.find(path);
    if (it != textureCache.end()) {
        return it->second.get();
    }

    // Auto-detect filterMode from .meta file if present
    std::string metaPath = path + ".meta";
    if (std::filesystem::exists(metaPath)) {
        std::ifstream f(metaPath);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            std::string metaJson = ss.str();
            std::string filterStr = JSONUtils::extractStringValue(metaJson, "filterMode");
            if (filterStr.empty()) {
                filterStr = JSONUtils::extractStringValue(metaJson, "filter");
            }
            if (filterStr == "Nearest" || filterStr == "Point" || filterStr == "nearest") {
                filterMode = TextureFilterMode::Nearest;
            } else if (filterStr == "Bilinear" || filterStr == "bilinear") {
                filterMode = TextureFilterMode::Bilinear;
            } else if (filterStr == "Trilinear" || filterStr == "trilinear") {
                filterMode = TextureFilterMode::Trilinear;
            }
        }
    }

    auto texture = std::make_unique<Texture>();
    texture->path = path;
    texture->filterMode = filterMode;

    try {
        createTextureImage(path, renderer, *texture);
        createTextureImageView(renderer.device.getDevice(), *texture);
        createTextureSampler(renderer.device.getDevice(), *texture, filterMode);
        
        // Allocate and write descriptor set
        renderer.descriptors.allocateTextureDescriptorSet(
            texture->descriptorSet,
            texture->imageView, texture->sampler,
            defaultNormalTexture.imageView, defaultNormalTexture.sampler,
            defaultMetallicTexture.imageView, defaultMetallicTexture.sampler
        );

        renderer.descriptors.allocateSingleTextureDescriptorSet(
            texture->singleDescriptorSet,
            texture->imageView, texture->sampler
        );
    } catch (const std::exception& e) {
        std::cerr << "[ResourceManager] Error loading texture " << path << ": " << e.what() << std::endl;
        return &defaultWhiteTexture; // Fallback
    }

    Texture* ptr = texture.get();
    textureCache[path] = std::move(texture);
    return ptr;
}

void ResourceManager::createDefaultTextures(VulkanRenderer& renderer) {
    // 1. Create Default White Texture
    defaultWhiteTexture.path = "";
    defaultWhiteTexture.width = 1;
    defaultWhiteTexture.height = 1;

    uint8_t whitePixel[4] = { 255, 255, 255, 255 }; // RGBA white
    VkDeviceSize imageSize = sizeof(whitePixel);

    VulkanBuffer stagingBuffer;
    stagingBuffer.create(
        renderer.device.getDevice(),
        renderer.device.getPhysicalDevice(),
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer.uploadData(&whitePixel, imageSize);

    createImage(
        renderer.device.getDevice(),
        renderer.device.getPhysicalDevice(),
        1, 1,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        defaultWhiteTexture.image,
        defaultWhiteTexture.memory,
        *this
    );

    transitionImageLayout(renderer, defaultWhiteTexture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(renderer, stagingBuffer.get(), defaultWhiteTexture.image, 1, 1);
    transitionImageLayout(renderer, defaultWhiteTexture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    stagingBuffer.destroy();

    createTextureImageView(renderer.device.getDevice(), defaultWhiteTexture);
    createTextureSampler(renderer.device.getDevice(), defaultWhiteTexture, TextureFilterMode::Bilinear);

    // 2. Create Default Normal Texture (flat tangent-space normal vector: 128, 128, 255, 255)
    defaultNormalTexture.path = "";
    defaultNormalTexture.width = 1;
    defaultNormalTexture.height = 1;
    defaultNormalTexture.format = VK_FORMAT_R8G8B8A8_UNORM;


    uint8_t normalPixel[4] = { 128, 128, 255, 255 };
    stagingBuffer.create(
        renderer.device.getDevice(),
        renderer.device.getPhysicalDevice(),
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer.uploadData(&normalPixel, imageSize);

    createImage(
        renderer.device.getDevice(),
        renderer.device.getPhysicalDevice(),
        1, 1,
        VK_FORMAT_R8G8B8A8_UNORM, // Normals use UNORM so we don't apply gamma correction!
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        defaultNormalTexture.image,
        defaultNormalTexture.memory,
        *this
    );

    transitionImageLayout(renderer, defaultNormalTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(renderer, stagingBuffer.get(), defaultNormalTexture.image, 1, 1);
    transitionImageLayout(renderer, defaultNormalTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    stagingBuffer.destroy();

    createTextureImageView(renderer.device.getDevice(), defaultNormalTexture);
    createTextureSampler(renderer.device.getDevice(), defaultNormalTexture, TextureFilterMode::Bilinear);

    // 3. Create Default Metallic/Roughness Texture (roughness = 0.5, metallic = 0.0 -> 0, 128, 0, 255)
    defaultMetallicTexture.path = "";
    defaultMetallicTexture.width = 1;
    defaultMetallicTexture.height = 1;
    defaultMetallicTexture.format = VK_FORMAT_R8G8B8A8_UNORM;


    uint8_t metallicPixel[4] = { 0, 128, 0, 255 };
    stagingBuffer.create(
        renderer.device.getDevice(),
        renderer.device.getPhysicalDevice(),
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer.uploadData(&metallicPixel, imageSize);

    createImage(
        renderer.device.getDevice(),
        renderer.device.getPhysicalDevice(),
        1, 1,
        VK_FORMAT_R8G8B8A8_UNORM, // metallic/roughness parameters should be raw UNORM values
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        defaultMetallicTexture.image,
        defaultMetallicTexture.memory,
        *this
    );

    transitionImageLayout(renderer, defaultMetallicTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(renderer, stagingBuffer.get(), defaultMetallicTexture.image, 1, 1);
    transitionImageLayout(renderer, defaultMetallicTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    stagingBuffer.destroy();

    createTextureImageView(renderer.device.getDevice(), defaultMetallicTexture);
    createTextureSampler(renderer.device.getDevice(), defaultMetallicTexture, TextureFilterMode::Bilinear);

    // 4. Allocate fallback descriptor sets
    renderer.descriptors.allocateTextureDescriptorSet(
        defaultWhiteTexture.descriptorSet,
        defaultWhiteTexture.imageView, defaultWhiteTexture.sampler,
        defaultNormalTexture.imageView, defaultNormalTexture.sampler,
        defaultMetallicTexture.imageView, defaultMetallicTexture.sampler
    );
    renderer.descriptors.allocateSingleTextureDescriptorSet(
        defaultWhiteTexture.singleDescriptorSet,
        defaultWhiteTexture.imageView, defaultWhiteTexture.sampler
    );

    renderer.descriptors.allocateTextureDescriptorSet(
        defaultNormalTexture.descriptorSet,
        defaultWhiteTexture.imageView, defaultWhiteTexture.sampler,
        defaultNormalTexture.imageView, defaultNormalTexture.sampler,
        defaultMetallicTexture.imageView, defaultMetallicTexture.sampler
    );
    renderer.descriptors.allocateSingleTextureDescriptorSet(
        defaultNormalTexture.singleDescriptorSet,
        defaultNormalTexture.imageView, defaultNormalTexture.sampler
    );

    renderer.descriptors.allocateTextureDescriptorSet(
        defaultMetallicTexture.descriptorSet,
        defaultWhiteTexture.imageView, defaultWhiteTexture.sampler,
        defaultNormalTexture.imageView, defaultNormalTexture.sampler,
        defaultMetallicTexture.imageView, defaultMetallicTexture.sampler
    );
    renderer.descriptors.allocateSingleTextureDescriptorSet(
        defaultMetallicTexture.singleDescriptorSet,
        defaultMetallicTexture.imageView, defaultMetallicTexture.sampler
    );
}

void ResourceManager::updateMaterialDescriptorSet(Material& mat, VulkanRenderer& renderer) {
    Texture* diff = mat.texturePath.empty() ? &defaultWhiteTexture : loadTexture(mat.texturePath, renderer);
    Texture* norm = mat.normalMapPath.empty() ? &defaultNormalTexture : loadTexture(mat.normalMapPath, renderer);
    Texture* met  = mat.metallicMapPath.empty() ? &defaultMetallicTexture : loadTexture(mat.metallicMapPath, renderer);

    if (!diff) diff = &defaultWhiteTexture;
    if (!norm) norm = &defaultNormalTexture;
    if (!met) met = &defaultMetallicTexture;

    // Fast path for 2D Sprites/UI: direct descriptor set pointer sharing
    if (norm == &defaultNormalTexture && met == &defaultMetallicTexture && diff->descriptorSet != VK_NULL_HANDLE) {
        mat.descriptorSet = diff->descriptorSet;
        return;
    }

    if (mat.descriptorSet == VK_NULL_HANDLE || mat.descriptorSet == defaultWhiteTexture.descriptorSet) {
        renderer.descriptors.allocateTextureDescriptorSet(
            mat.descriptorSet,
            diff->imageView, diff->sampler,
            norm->imageView, norm->sampler,
            met->imageView, met->sampler
        );
    } else {
        renderer.descriptors.updateTextureDescriptorSet(
            mat.descriptorSet,
            diff->imageView, diff->sampler,
            norm->imageView, norm->sampler,
            met->imageView, met->sampler
        );
    }
}

static void traverseNodes(cgltf_data* data, cgltf_node* node, const glm::mat4& parentTransform, Mesh& mesh, int targetPrimIndex, int& currentPrimIndex) {
    glm::mat4 local(1.0f);
    cgltf_node_transform_local(node, &local[0][0]);
    glm::mat4 global = parentTransform * local;

    if (node->mesh) {
        cgltf_mesh* gltfMesh = node->mesh;
        for (cgltf_size j = 0; j < gltfMesh->primitives_count; ++j) {
            cgltf_primitive& prim = gltfMesh->primitives[j];
            
            int primIdx = currentPrimIndex++;
            if (targetPrimIndex != -1 && primIdx != targetPrimIndex) {
                continue;
            }

            if (node->name) {
                mesh.nodeName = node->name;
            } else if (node->mesh && node->mesh->name) {
                mesh.nodeName = node->mesh->name;
            } else {
                mesh.nodeName = "MeshPart_" + std::to_string(primIdx);
            }

            // Find parent bone name by traversing up ancestors in glTF
            cgltf_node* ancestor = node->parent;
            while (ancestor) {
                bool isJoint = false;
                for (cgltf_size s = 0; s < data->skins_count; ++s) {
                    cgltf_skin& skin = data->skins[s];
                    for (cgltf_size jk = 0; jk < skin.joints_count; ++jk) {
                        if (skin.joints[jk] == ancestor) {
                            isJoint = true;
                            break;
                        }
                    }
                    if (isJoint) break;
                }
                if (isJoint && ancestor->name) {
                    mesh.parentBoneName = ancestor->name;
                    break;
                }
                ancestor = ancestor->parent;
            }

            size_t vert_start = mesh.vertices.size();

            cgltf_accessor* pos_accessor = nullptr;
            cgltf_accessor* norm_accessor = nullptr;
            cgltf_accessor* uv_accessor = nullptr;
            cgltf_accessor* joints_accessor = nullptr;
            cgltf_accessor* weights_accessor = nullptr;

            for (cgltf_size k = 0; k < prim.attributes_count; ++k) {
                cgltf_attribute& attr = prim.attributes[k];
                if (attr.type == cgltf_attribute_type_position) pos_accessor = attr.data;
                else if (attr.type == cgltf_attribute_type_normal) norm_accessor = attr.data;
                else if (attr.type == cgltf_attribute_type_texcoord) uv_accessor = attr.data;
                else if (attr.type == cgltf_attribute_type_joints) joints_accessor = attr.data;
                else if (attr.type == cgltf_attribute_type_weights) weights_accessor = attr.data;
            }

            if (!pos_accessor) continue;

            bool isSkinned = (joints_accessor != nullptr && weights_accessor != nullptr);
            mesh.isSkinned = isSkinned;

            size_t vert_count = pos_accessor->count;
            for (size_t k = 0; k < vert_count; ++k) {
                Vertex vert{};

                float pos[3]{};
                cgltf_accessor_read_float(pos_accessor, k, pos, 3);
                glm::vec3 position = glm::vec3(pos[0], pos[1], pos[2]);

                glm::vec3 normal(0.0f);
                if (norm_accessor) {
                    float norm[3]{};
                    cgltf_accessor_read_float(norm_accessor, k, norm, 3);
                    normal = glm::vec3(norm[0], norm[1], norm[2]);
                }

                // If not skinned, pre-transform static vertices by global node matrix
                if (!isSkinned) {
                    position = glm::vec3(global * glm::vec4(position, 1.0f));
                    if (norm_accessor) {
                        normal = glm::normalize(glm::vec3(glm::transpose(glm::inverse(global)) * glm::vec4(normal, 0.0f)));
                    }
                }

                vert.position = position;
                vert.normal = normal;

                if (uv_accessor) {
                    float uv[2]{};
                    cgltf_accessor_read_float(uv_accessor, k, uv, 2);
                    vert.uv = glm::vec2(uv[0], uv[1]);
                }

                if (joints_accessor) {
                    cgltf_uint joints[4]{};
                    cgltf_accessor_read_uint(joints_accessor, k, joints, 4);
                    vert.boneIDs = glm::ivec4(
                        static_cast<int>(joints[0]),
                        static_cast<int>(joints[1]),
                        static_cast<int>(joints[2]),
                        static_cast<int>(joints[3])
                    );
                }

                if (weights_accessor) {
                    float weights[4]{};
                    cgltf_accessor_read_float(weights_accessor, k, weights, 4);
                    vert.boneWeights = glm::vec4(weights[0], weights[1], weights[2], weights[3]);
                }



                mesh.vertices.push_back(vert);
            }

            // Read indices
            cgltf_accessor* index_accessor = prim.indices;
            if (index_accessor) {
                for (cgltf_size k = 0; k < index_accessor->count; ++k) {
                    mesh.indices.push_back(static_cast<uint32_t>(cgltf_accessor_read_index(index_accessor, k) + vert_start));
                }
            } else {
                for (size_t k = 0; k < vert_count; ++k) {
                    mesh.indices.push_back(static_cast<uint32_t>(vert_start + k));
                }
            }
        }
    }

    for (cgltf_size i = 0; i < node->children_count; ++i) {
        traverseNodes(data, node->children[i], global, mesh, targetPrimIndex, currentPrimIndex);
    }
}

static glm::mat4 toGlmMatrix(const ufbx_matrix& m) {
    glm::mat4 out(1.0f);
    out[0] = glm::vec4(m.cols[0].x, m.cols[0].y, m.cols[0].z, 0.0f);
    out[1] = glm::vec4(m.cols[1].x, m.cols[1].y, m.cols[1].z, 0.0f);
    out[2] = glm::vec4(m.cols[2].x, m.cols[2].y, m.cols[2].z, 0.0f);
    out[3] = glm::vec4(m.cols[3].x, m.cols[3].y, m.cols[3].z, 1.0f);
    return out;
}

static Mesh loadFBXMesh(const std::string& path, VulkanRenderer& renderer, int primitiveIndex, std::unordered_map<std::string, Mesh>& meshCache) {
    Mesh mesh{};
    mesh.gltfPath = path;
    mesh.primitiveIndex = primitiveIndex;

    // Load import settings if available
    float targetScale = 1.0f;
    bool generateNormals = true;
    bool allowMissingPos = false;
    std::string importPath = path + ".import";
    std::ifstream importFile(importPath);
    if (importFile.is_open()) {
        std::stringstream ss;
        ss << importFile.rdbuf();
        std::string content = ss.str();
        JSONUtils::extractFloatValue(content, "scale", targetScale);
        generateNormals = (content.find("\"generateNormals\": true") != std::string::npos || content.find("\"generateNormals\":true") != std::string::npos || content.find("\"generateNormals\": 1") != std::string::npos);
        allowMissingPos = (content.find("\"allowMissingPos\": true") != std::string::npos || content.find("\"allowMissingPos\":true") != std::string::npos || content.find("\"allowMissingPos\": 1") != std::string::npos);
    }

    ufbx_load_opts opts = { 0 };
    opts.target_unit_meters = targetScale;
    opts.generate_missing_normals = generateNormals;
    opts.allow_missing_vertex_position = allowMissingPos;
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        throw std::runtime_error("Failed to parse FBX file: " + path + " - " + error.description.data);
    }

    std::vector<ufbx_node*> fbxNodes;
    auto collectNodes = [&](auto& self, ufbx_node* node) -> void {
        if (!node) return;
        fbxNodes.push_back(node);
        for (size_t i = 0; i < node->children.count; ++i) {
            self(self, node->children.data[i]);
        }
    };
    collectNodes(collectNodes, scene->root_node);

    ufbx_mesh* targetMesh = nullptr;
    ufbx_node* targetNode = nullptr;
    int currentPrimIndex = 0;

    for (size_t i = 0; i < scene->nodes.count; ++i) {
        ufbx_node* node = scene->nodes.data[i];
        if (node->mesh) {
            if (primitiveIndex == -1 || currentPrimIndex == primitiveIndex) {
                targetMesh = node->mesh;
                targetNode = node;
                break;
            }
            currentPrimIndex++;
        }
    }

    if (!targetMesh) {
        ufbx_free_scene(scene);
        throw std::runtime_error("Failed to find target mesh in FBX: " + path);
    }

    mesh.nodeName = targetNode->name.data ? targetNode->name.data : "MeshPart";
    
    ufbx_node* ancestor = targetNode->parent;
    while (ancestor) {
        if (ancestor->attrib_type == UFBX_ELEMENT_BONE && ancestor->name.data) {
            mesh.parentBoneName = ancestor->name.data;
            break;
        }
        ancestor = ancestor->parent;
    }

    ufbx_skin_deformer* skin = nullptr;
    if (targetMesh->skin_deformers.count > 0) {
        skin = targetMesh->skin_deformers.data[0];
    }
    mesh.isSkinned = (skin != nullptr);

    std::vector<uint32_t> tempIndices;
    tempIndices.reserve(targetMesh->max_face_triangles * 3 * targetMesh->faces.count);

    for (size_t i = 0; i < targetMesh->faces.count; ++i) {
        ufbx_face face = targetMesh->faces.data[i];
        if (face.num_indices < 3) continue;

        size_t old_size = tempIndices.size();
        size_t max_tris = face.num_indices - 2;
        size_t max_indices = max_tris * 3;
        
        tempIndices.resize(old_size + max_indices);
        
        uint32_t num_tris = ufbx_triangulate_face(
            tempIndices.data() + old_size,
            max_indices,
            targetMesh,
            face
        );
        
        tempIndices.resize(old_size + (num_tris * 3));
    }
    std::cout << "[FBX Debug] Path: " << path << "\n"
              << "  num_vertices: " << targetMesh->num_vertices << "\n"
              << "  num_indices (face corners): " << targetMesh->num_indices << "\n"
              << "  num_faces: " << targetMesh->faces.count << "\n"
              << "  triangulated indices count: " << tempIndices.size() << "\n"
              << "  vertex_position.indices.count: " << targetMesh->vertex_position.indices.count << "\n"
              << "  vertex_normal.indices.count: " << targetMesh->vertex_normal.indices.count << "\n"
              << "  vertex_uv.indices.count: " << targetMesh->vertex_uv.indices.count << "\n";
    if (!tempIndices.empty()) {
        std::cout << "  first 5 tempIndices: ";
        for (int i = 0; i < std::min(5, (int)tempIndices.size()); ++i) {
            std::cout << tempIndices[i] << " ";
        }
        std::cout << "\n";
    }

    struct IndexTuple {
        uint32_t pos_idx;
        uint32_t norm_idx;
        uint32_t uv_idx;
        bool operator==(const IndexTuple& o) const {
            return pos_idx == o.pos_idx && norm_idx == o.norm_idx && uv_idx == o.uv_idx;
        }
    };
    struct IndexTupleHash {
        size_t operator()(const IndexTuple& t) const {
            return (t.pos_idx ^ (t.norm_idx << 8)) ^ (t.uv_idx << 16);
        }
    };

    std::unordered_map<IndexTuple, uint32_t, IndexTupleHash> uniqueVertices;
    
    for (uint32_t index_idx : tempIndices) {
        uint32_t pos_idx = (targetMesh->vertex_position.indices.count > 0) ? targetMesh->vertex_position.indices.data[index_idx] : targetMesh->vertex_indices.data[index_idx];
        uint32_t norm_idx = (targetMesh->vertex_normal.indices.count > 0) ? targetMesh->vertex_normal.indices.data[index_idx] : targetMesh->vertex_indices.data[index_idx];
        uint32_t uv_idx = (targetMesh->vertex_uv.indices.count > 0) ? targetMesh->vertex_uv.indices.data[index_idx] : targetMesh->vertex_indices.data[index_idx];

        IndexTuple tuple{ pos_idx, norm_idx, uv_idx };
        auto it = uniqueVertices.find(tuple);
        if (it != uniqueVertices.end()) {
            mesh.indices.push_back(it->second);
        } else {
            uint32_t newVertIdx = static_cast<uint32_t>(mesh.vertices.size());
            uniqueVertices[tuple] = newVertIdx;
            mesh.indices.push_back(newVertIdx);

            Vertex vert{};
            
            ufbx_vec3 pos = targetMesh->vertex_position.values.data[pos_idx];
            glm::vec3 position(pos.x, pos.y, pos.z);

            glm::vec3 normal(0.0f);
            if (targetMesh->vertex_normal.values.count > 0) {
                ufbx_vec3 norm = targetMesh->vertex_normal.values.data[norm_idx];
                normal = glm::vec3(norm.x, norm.y, norm.z);
            }

            if (!mesh.isSkinned) {
                glm::mat4 global = toGlmMatrix(targetNode->node_to_world);
                position = glm::vec3(global * glm::vec4(position, 1.0f));
                normal = glm::normalize(glm::vec3(glm::transpose(glm::inverse(global)) * glm::vec4(normal, 0.0f)));
            }

            vert.position = position;
            vert.normal = normal;

            if (targetMesh->vertex_uv.values.count > 0) {
                ufbx_vec2 uv = targetMesh->vertex_uv.values.data[uv_idx];
                vert.uv = glm::vec2(uv.x, uv.y);
            }

            if (mesh.isSkinned && skin) {
                uint32_t logical_vert_idx = targetMesh->vertex_indices.data[index_idx];
                ufbx_skin_vertex skin_vert = skin->vertices.data[logical_vert_idx];

                int boneIDs[4]{ 0, 0, 0, 0 };
                float boneWeights[4]{ 0.0f, 0.0f, 0.0f, 0.0f };

                uint32_t nWeights = std::min(4u, skin_vert.num_weights);
                float totalWeight = 0.0f;
                for (uint32_t w = 0; w < nWeights; ++w) {
                    ufbx_skin_weight skin_weight = skin->weights.data[skin_vert.weight_begin + w];
                    ufbx_node* boneNode = skin->clusters.data[skin_weight.cluster_index]->bone_node;
                    
                    auto bIt = std::find(fbxNodes.begin(), fbxNodes.end(), boneNode);
                    int jointIndex = 0;
                    if (bIt != fbxNodes.end()) {
                        jointIndex = static_cast<int>(std::distance(fbxNodes.begin(), bIt));
                    }

                    boneIDs[w] = jointIndex;
                    boneWeights[w] = static_cast<float>(skin_weight.weight);
                    totalWeight += boneWeights[w];
                }

                if (totalWeight > 0.0f) {
                    for (int w = 0; w < 4; ++w) {
                        boneWeights[w] /= totalWeight;
                    }
                }

                vert.boneIDs = glm::ivec4(boneIDs[0], boneIDs[1], boneIDs[2], boneIDs[3]);
                vert.boneWeights = glm::vec4(boneWeights[0], boneWeights[1], boneWeights[2], boneWeights[3]);
            }

            mesh.vertices.push_back(vert);
        }
    }

    std::cout << "  final mesh.vertices.size(): " << mesh.vertices.size() << "\n"
              << "  final mesh.indices.size(): " << mesh.indices.size() << "\n";

    ufbx_free_scene(scene);

    const size_t meshID = renderer.meshSoA.push(mesh.vertices, mesh.indices);
    renderer.uploadMesh(meshID);

    mesh.vertexBuffer = renderer.meshSoA.vertexBuffers[meshID].get();
    mesh.indexBuffer = renderer.meshSoA.indexBuffers[meshID].get();
    mesh.id = static_cast<uint32_t>(meshID);

    std::string cacheKey = path;
    if (primitiveIndex >= 0) {
        cacheKey = path + "#" + std::to_string(primitiveIndex);
    }
    meshCache[cacheKey] = mesh;
    return mesh;
}

void ResourceManager::clearMeshCache(const std::string& path) {
    meshCache.erase(path);
    for (auto it = meshCache.begin(); it != meshCache.end();) {
        if (it->first.rfind(path + "#", 0) == 0) {
            it = meshCache.erase(it);
        } else {
            ++it;
        }
    }
}

Mesh ResourceManager::loadMesh(const std::string& path, VulkanRenderer& renderer, int primitiveIndex) {
    std::string cacheKey = path;
    if (primitiveIndex >= 0) {
        cacheKey = path + "#" + std::to_string(primitiveIndex);
    }

    auto it = meshCache.find(cacheKey);
    if (it != meshCache.end()) {
        return it->second;
    }

    // Load import settings if available
    float targetScale = 1.0f;
    std::string importPath = path + ".import";
    std::ifstream importFile(importPath);
    if (importFile.is_open()) {
        std::stringstream ss;
        ss << importFile.rdbuf();
        std::string content = ss.str();
        JSONUtils::extractFloatValue(content, "scale", targetScale);
    }

    if (path.length() >= 4 && (path.substr(path.length() - 4) == ".fbx" || path.substr(path.length() - 4) == ".FBX")) {
        return loadFBXMesh(path, renderer, primitiveIndex, meshCache);
    }

    Mesh mesh{};
    mesh.gltfPath = path;
    mesh.primitiveIndex = primitiveIndex;

    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        throw std::runtime_error("Failed to parse glTF file: " + path);
    }

    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("Failed to load glTF buffers: " + path);
    }

    int currentPrimIndex = 0;

    // Traverse scenes and nodes hierarchically
    if (data->scene) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
            traverseNodes(data, data->scene->nodes[i], glm::mat4(1.0f), mesh, primitiveIndex, currentPrimIndex);
        }
    } else {
        for (cgltf_size i = 0; i < data->scenes_count; ++i) {
            cgltf_scene& scene = data->scenes[i];
            for (cgltf_size j = 0; j < scene.nodes_count; ++j) {
                traverseNodes(data, scene.nodes[j], glm::mat4(1.0f), mesh, primitiveIndex, currentPrimIndex);
            }
        }
    }

    // Scale glTF vertices if custom scale is specified
    if (targetScale != 1.0f) {
        for (auto& vert : mesh.vertices) {
            vert.position *= targetScale;
        }
    }

    cgltf_free(data);

    // Upload loaded geometry to renderer
    const size_t meshID = renderer.meshSoA.push(mesh.vertices, mesh.indices);
    renderer.uploadMesh(meshID);

    mesh.vertexBuffer = renderer.meshSoA.vertexBuffers[meshID].get();
    mesh.indexBuffer = renderer.meshSoA.indexBuffers[meshID].get();
    mesh.id = static_cast<uint32_t>(meshID);

    meshCache[cacheKey] = mesh;
    return mesh;
}

static void countPrimsRecursive(cgltf_node* node, int& count) {
    if (node->mesh) {
        count += static_cast<int>(node->mesh->primitives_count);
    }
    for (cgltf_size c = 0; c < node->children_count; ++c) {
        countPrimsRecursive(node->children[c], count);
    }
}

int ResourceManager::getMeshPrimitiveCount(const std::string& path) {
    if (path.length() >= 4 && (path.substr(path.length() - 4) == ".fbx" || path.substr(path.length() - 4) == ".FBX")) {
        ufbx_load_opts opts = { 0 };
        opts.target_unit_meters = 1.0f;
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
        if (!scene) return 0;
        int count = 0;
        for (size_t i = 0; i < scene->nodes.count; ++i) {
            if (scene->nodes.data[i]->mesh) {
                count++;
            }
        }
        ufbx_free_scene(scene);
        return count;
    }

    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        return 0;
    }
    
    int count = 0;
    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        cgltf_scene& scene = data->scenes[i];
        for (cgltf_size j = 0; j < scene.nodes_count; ++j) {
            countPrimsRecursive(scene.nodes[j], count);
        }
    }
    
    cgltf_free(data);
    return count;
}

void ResourceManager::cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) return;

    // Free default white texture
    if (defaultWhiteTexture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, defaultWhiteTexture.sampler, nullptr);
    }
    if (defaultWhiteTexture.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, defaultWhiteTexture.imageView, nullptr);
    }
    if (defaultWhiteTexture.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, defaultWhiteTexture.image, nullptr);
    }
    if (defaultWhiteTexture.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, defaultWhiteTexture.memory, nullptr);
    }

    // Free cached textures
    for (auto& [_, texture] : textureCache) {
        if (texture->sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, texture->sampler, nullptr);
        }
        if (texture->imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, texture->imageView, nullptr);
        }
        if (texture->image != VK_NULL_HANDLE) {
            vkDestroyImage(device, texture->image, nullptr);
        }
        if (texture->memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, texture->memory, nullptr);
        }
    }
    textureCache.clear();
    meshCache.clear();
}

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cctype>

static bool isRootMotionJoint(const SkeletonComponent& skeleton, int jointIndex) {
    if (jointIndex < 0 || jointIndex >= static_cast<int>(skeleton.joints.size())) return false;
    
    // 1. Direct root (parentIndex == -1)
    if (skeleton.joints[jointIndex].parentIndex == -1) {
        return true;
    }
    
    // 2. Direct child of direct root (parent has parentIndex == -1)
    int parentIdx = skeleton.joints[jointIndex].parentIndex;
    if (parentIdx >= 0 && parentIdx < static_cast<int>(skeleton.joints.size())) {
        if (skeleton.joints[parentIdx].parentIndex == -1) {
            std::string name = skeleton.joints[jointIndex].name;
            for (char& c : name) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            if (name.find("hips") != std::string::npos || name.find("root") != std::string::npos || name.find("pelvis") != std::string::npos) {
                return true;
            }
        }
    }
    
    return false;
}

bool ResourceManager::loadSkeletonAndAnimations(const std::string& path, SkeletonComponent& skeleton, AnimatorComponent& animator, bool append) {
    if (path.length() >= 5 && path.substr(path.length() - 5) == ".anim") {
        return loadBinarySkeletonAndAnimations(path, skeleton, animator, append);
    }
    
    if (path.length() >= 4 && (path.substr(path.length() - 4) == ".fbx" || path.substr(path.length() - 4) == ".FBX")) {
        float targetScale = 1.0f;
        bool forceInPlace = false;
        std::string importPath = path + ".import";
        std::ifstream importFile(importPath);
        if (importFile.is_open()) {
            std::stringstream ss;
            ss << importFile.rdbuf();
            std::string content = ss.str();
            JSONUtils::extractFloatValue(content, "scale", targetScale);
            forceInPlace = (content.find("\"forceInPlace\": true") != std::string::npos || content.find("\"forceInPlace\":true") != std::string::npos || content.find("\"forceInPlace\": 1") != std::string::npos);
        }

        ufbx_load_opts opts = { 0 };
        opts.target_unit_meters = targetScale;
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
        if (!scene) {
            std::cerr << "[ResourceManager] Failed to load FBX: " << error.description.data << std::endl;
            return false;
        }

        std::vector<ufbx_node*> fbxNodes;
        auto collectNodes = [&](auto& self, ufbx_node* node) -> void {
            if (!node) return;
            fbxNodes.push_back(node);
            for (size_t i = 0; i < node->children.count; ++i) {
                self(self, node->children.data[i]);
            }
        };
        collectNodes(collectNodes, scene->root_node);

        int jointCount = static_cast<int>(fbxNodes.size());
        bool targetHasSkeleton = !skeleton.joints.empty();
        std::vector<Joint> loadedJoints;
        if (targetHasSkeleton) {
            loadedJoints = skeleton.joints;
        } else {
            skeleton.joints.resize(jointCount);
        }

        for (size_t i = 0; i < fbxNodes.size(); ++i) {
            ufbx_node* node = fbxNodes[i];
            std::string nodeName = node->name.data ? node->name.data : "Joint_" + std::to_string(i);
            
            Joint& joint = targetHasSkeleton ? loadedJoints[i] : skeleton.joints[i];
            joint.name = nodeName;
            joint.parentIndex = -1;
            if (node->parent) {
                auto it = std::find(fbxNodes.begin(), fbxNodes.end(), node->parent);
                if (it != fbxNodes.end()) {
                    joint.parentIndex = static_cast<int>(std::distance(fbxNodes.begin(), it));
                }
            }

            joint.inverseBindMatrix = glm::inverse(toGlmMatrix(node->node_to_world));
            joint.localTransform = toGlmMatrix(node->node_to_parent);

            joint.bindTranslation = glm::vec3(node->local_transform.translation.x, node->local_transform.translation.y, node->local_transform.translation.z);
            joint.bindRotation = glm::quat(node->local_transform.rotation.w, node->local_transform.rotation.x, node->local_transform.rotation.y, node->local_transform.rotation.z);
            joint.bindScale = glm::vec3(node->local_transform.scale.x, node->local_transform.scale.y, node->local_transform.scale.z);
        }

        for (size_t s = 0; s < scene->skin_deformers.count; ++s) {
            ufbx_skin_deformer* skin = scene->skin_deformers.data[s];
            for (size_t c = 0; c < skin->clusters.count; ++c) {
                ufbx_skin_cluster* cluster = skin->clusters.data[c];
                if (cluster->bone_node) {
                    auto bIt = std::find(fbxNodes.begin(), fbxNodes.end(), cluster->bone_node);
                    if (bIt != fbxNodes.end()) {
                        int idx = static_cast<int>(std::distance(fbxNodes.begin(), bIt));
                        if (!targetHasSkeleton) {
                            skeleton.joints[idx].inverseBindMatrix = toGlmMatrix(cluster->geometry_to_bone);
                        } else {
                            loadedJoints[idx].inverseBindMatrix = toGlmMatrix(cluster->geometry_to_bone);
                        }
                    }
                }
            }
        }

        if (!targetHasSkeleton) {
            skeleton.jointMatrices.assign(jointCount, glm::mat4(1.0f));
        }

        if (!append) {
            animator.animations.clear();
        }
        for (size_t a = 0; a < scene->anim_stacks.count; ++a) {
            ufbx_anim_stack* stack = scene->anim_stacks.data[a];
            AnimationClip clip{};
            clip.name = stack->name.data ? stack->name.data : "AnimStack_" + std::to_string(a);
            clip.duration = static_cast<float>(stack->time_end - stack->time_begin);
            float duration = clip.duration;
            if (duration <= 0.0f) duration = 1.0f;

            int fps = 30;
            int numFrames = std::max(2, static_cast<int>(duration * fps));

            for (size_t n = 0; n < fbxNodes.size(); ++n) {
                ufbx_node* node = fbxNodes[n];
                AnimationChannel channel{};
                channel.jointName = node->name.data ? node->name.data : "";
                
                channel.jointIndex = -1;
                if (targetHasSkeleton) {
                    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                        if (skeleton.joints[i].name == channel.jointName) {
                            channel.jointIndex = static_cast<int>(i);
                            break;
                        }
                    }
                } else {
                    channel.jointIndex = static_cast<int>(n);
                }

                for (int f = 0; f < numFrames; ++f) {
                    float t = static_cast<float>(f) / (numFrames - 1) * duration;
                    double evalTime = stack->time_begin + t;
                    ufbx_transform xform = ufbx_evaluate_transform(stack->anim, node, evalTime);

                    glm::vec3 translation(xform.translation.x, xform.translation.y, xform.translation.z);
                    if (forceInPlace && channel.jointIndex >= 0 && channel.jointIndex < static_cast<int>(skeleton.joints.size())) {
                        if (isRootMotionJoint(skeleton, channel.jointIndex)) {
                            translation.x = skeleton.joints[channel.jointIndex].bindTranslation.x;
                            translation.z = skeleton.joints[channel.jointIndex].bindTranslation.z;
                        }
                    }

                    Keyframe tKey{};
                    tKey.time = t;
                    tKey.value = translation;
                    channel.translationKeys.push_back(tKey);

                    KeyframeRot rKey{};
                    rKey.time = t;
                    rKey.value = glm::quat(xform.rotation.w, xform.rotation.x, xform.rotation.y, xform.rotation.z);
                    channel.rotationKeys.push_back(rKey);

                    Keyframe sKey{};
                    sKey.time = t;
                    sKey.value = glm::vec3(xform.scale.x, xform.scale.y, xform.scale.z);
                    channel.scaleKeys.push_back(sKey);
                }

                clip.channels.push_back(std::move(channel));
            }

            bool duplicateFound = false;
            for (auto& existing : animator.animations) {
                if (existing.name == clip.name) {
                    existing = std::move(clip);
                    duplicateFound = true;
                    break;
                }
            }
            if (!duplicateFound) {
                animator.animations.push_back(std::move(clip));
            }
        }

        if (!animator.animations.empty()) {
            animator.activeAnimationIndex = 0;
        }

        ufbx_free_scene(scene);
        return true;
    }
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        return false;
    }

    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    // Load import settings if available
    float targetScale = 1.0f;
    bool forceInPlace = false;
    std::string importPath = path + ".import";
    std::ifstream importFile(importPath);
    if (importFile.is_open()) {
        std::stringstream ss;
        ss << importFile.rdbuf();
        std::string content = ss.str();
        JSONUtils::extractFloatValue(content, "scale", targetScale);
        forceInPlace = (content.find("\"forceInPlace\": true") != std::string::npos || content.find("\"forceInPlace\":true") != std::string::npos || content.find("\"forceInPlace\": 1") != std::string::npos);
    }

    if (data->skins_count == 0) {
        cgltf_free(data);
        return false;
    }

    cgltf_skin& skin = data->skins[0];
    std::unordered_map<cgltf_node*, int> nodeToJointIndex;

    // 1. Load Joint Hierarchies & IBMs
    bool targetHasSkeleton = !skeleton.joints.empty();
    std::vector<Joint> loadedJoints;
    if (!targetHasSkeleton) {
        skeleton.joints.resize(skin.joints_count);
    } else {
        loadedJoints.resize(skin.joints_count);
    }

    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        cgltf_node* jointNode = skin.joints[i];
        Joint& joint = targetHasSkeleton ? loadedJoints[i] : skeleton.joints[i];
        joint.name = jointNode->name ? jointNode->name : "Joint_" + std::to_string(i);
        joint.parentIndex = -1;

        nodeToJointIndex[jointNode] = static_cast<int>(i);

        // Read Inverse Bind Matrix (IBM)
        if (skin.inverse_bind_matrices) {
            cgltf_accessor_read_float(skin.inverse_bind_matrices, i, &joint.inverseBindMatrix[0][0], 16);
            if (targetScale != 1.0f) {
                joint.inverseBindMatrix[3][0] *= targetScale;
                joint.inverseBindMatrix[3][1] *= targetScale;
                joint.inverseBindMatrix[3][2] *= targetScale;
            }
        } else {
            joint.inverseBindMatrix = glm::mat4(1.0f);
        }

        // Read Local Transform matrix using cgltf helper (handles both matrix and TRS)
        glm::mat4 local(1.0f);
        cgltf_node_transform_local(jointNode, &local[0][0]);

        // Accumulate transforms of any non-joint parent nodes (like "Armature" or "Root")
        // up to the scene root or the first joint parent, and premultiply it.
        glm::mat4 nonJointParentTransform(1.0f);
        cgltf_node* parentNode = jointNode->parent;
        while (parentNode) {
            // Stop if the parent node is actually a joint in our skin
            if (nodeToJointIndex.find(parentNode) != nodeToJointIndex.end()) {
                break;
            }
            glm::mat4 parentLocal(1.0f);
            cgltf_node_transform_local(parentNode, &parentLocal[0][0]);
            nonJointParentTransform = parentLocal * nonJointParentTransform;
            parentNode = parentNode->parent;
        }

        joint.localTransform = nonJointParentTransform * local;

        // Decompose the compiled localTransform matrix to extract bind TRS values
        glm::vec3 skew{};
        glm::vec4 perspective{};
        glm::decompose(joint.localTransform, joint.bindScale, joint.bindRotation, joint.bindTranslation, skew, perspective);

        if (targetScale != 1.0f) {
            joint.bindTranslation *= targetScale;
        }
    }

    // Resolve Parent Indices
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        cgltf_node* jointNode = skin.joints[i];
        if (jointNode->parent) {
            auto it = nodeToJointIndex.find(jointNode->parent);
            if (it != nodeToJointIndex.end()) {
                if (!targetHasSkeleton) {
                    skeleton.joints[i].parentIndex = it->second;
                } else {
                    loadedJoints[i].parentIndex = it->second;
                }
            }
        }
    }



    // 2. Load Animation Clips & Channels
    std::vector<AnimationClip> loadedClips(data->animations_count);
    for (cgltf_size i = 0; i < data->animations_count; ++i) {
        cgltf_animation& gltfAnim = data->animations[i];
        AnimationClip& clip = loadedClips[i];
        clip.name = gltfAnim.name ? gltfAnim.name : "Animation_" + std::to_string(i);
        clip.duration = 0.0f;

        // Temporary map to group channels by jointIndex
        std::unordered_map<int, AnimationChannel> jointChannels;

        for (cgltf_size j = 0; j < gltfAnim.channels_count; ++j) {
            cgltf_animation_channel& channel = gltfAnim.channels[j];
            cgltf_node* targetNode = channel.target_node;
            if (!targetNode) continue;

            auto it = nodeToJointIndex.find(targetNode);
            if (it == nodeToJointIndex.end()) continue; // Channel targets a node outside our joint set

            int jointIdx = it->second;
            AnimationChannel& animChannel = jointChannels[jointIdx];
            animChannel.jointName = targetNode->name ? targetNode->name : "";
            
            animChannel.jointIndex = -1;
            if (targetHasSkeleton) {
                for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                    if (skeleton.joints[i].name == animChannel.jointName) {
                        animChannel.jointIndex = static_cast<int>(i);
                        break;
                    }
                }
            } else {
                animChannel.jointIndex = jointIdx;
            }

            cgltf_animation_sampler* sampler = channel.sampler;
            size_t keyframeCount = sampler->input->count;

            for (size_t k = 0; k < keyframeCount; ++k) {
                float time = 0.0f;
                cgltf_accessor_read_float(sampler->input, k, &time, 1);
                clip.duration = std::max(clip.duration, time);

                float val[4]{};
                if (channel.target_path == cgltf_animation_path_type_translation) {
                    cgltf_accessor_read_float(sampler->output, k, val, 3);
                    glm::vec3 translation(val[0], val[1], val[2]);
                    translation *= targetScale;
                    if (forceInPlace && animChannel.jointIndex >= 0 && animChannel.jointIndex < static_cast<int>(skeleton.joints.size())) {
                        if (isRootMotionJoint(skeleton, animChannel.jointIndex)) {
                            translation.x = skeleton.joints[animChannel.jointIndex].bindTranslation.x;
                            translation.z = skeleton.joints[animChannel.jointIndex].bindTranslation.z;
                        }
                    }
                    animChannel.translationKeys.push_back({ time, translation });
                } else if (channel.target_path == cgltf_animation_path_type_rotation) {
                    animChannel.rotationKeys.push_back({ time, glm::quat(val[3], val[0], val[1], val[2]) });
                } else if (channel.target_path == cgltf_animation_path_type_scale) {
                    animChannel.scaleKeys.push_back({ time, glm::vec3(val[0], val[1], val[2]) });
                }
            }
        }

        // Move the grouped channels to clip.channels
        for (auto& [jointIdx, animChannel] : jointChannels) {
            // Sort keyframes by time
            std::sort(animChannel.translationKeys.begin(), animChannel.translationKeys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
            std::sort(animChannel.rotationKeys.begin(), animChannel.rotationKeys.end(), [](const KeyframeRot& a, const KeyframeRot& b) { return a.time < b.time; });
            std::sort(animChannel.scaleKeys.begin(), animChannel.scaleKeys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

            clip.channels.push_back(std::move(animChannel));
        }
    }

    if (!append) {
        animator.animations.clear();
    }
    for (auto& clip : loadedClips) {
        bool duplicateFound = false;
        for (auto& existing : animator.animations) {
            if (existing.name == clip.name) {
                existing = std::move(clip);
                duplicateFound = true;
                break;
            }
        }
        if (!duplicateFound) {
            animator.animations.push_back(std::move(clip));
        }
    }

    if (!animator.animations.empty()) {
        animator.activeAnimationIndex = 0; // Default active clip
    }

    cgltf_free(data);
    return true;
}

bool ResourceManager::saveBinarySkeletonAndAnimations(const std::string& path, const SkeletonComponent& skeleton, const AnimatorComponent& animator) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[ResourceManager] Failed to open file for writing binary animation: " << path << std::endl;
        return false;
    }

    char magic[4] = {'A', 'N', 'I', 'M'};
    out.write(magic, 4);
    uint32_t version = 3;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));

    uint32_t jointCount = static_cast<uint32_t>(skeleton.joints.size());
    out.write(reinterpret_cast<const char*>(&jointCount), sizeof(jointCount));

    for (const auto& joint : skeleton.joints) {
        uint32_t nameLength = static_cast<uint32_t>(joint.name.size());
        out.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
        if (nameLength > 0) {
            out.write(joint.name.data(), nameLength);
        }
        out.write(reinterpret_cast<const char*>(&joint.parentIndex), sizeof(joint.parentIndex));
        out.write(reinterpret_cast<const char*>(&joint.inverseBindMatrix[0][0]), sizeof(glm::mat4));
        out.write(reinterpret_cast<const char*>(&joint.localTransform[0][0]), sizeof(glm::mat4));
        out.write(reinterpret_cast<const char*>(&joint.bindTranslation[0]), sizeof(glm::vec3));
        out.write(reinterpret_cast<const char*>(&joint.bindRotation[0]), sizeof(glm::quat));
        out.write(reinterpret_cast<const char*>(&joint.bindScale[0]), sizeof(glm::vec3));
    }

    uint32_t animCount = static_cast<uint32_t>(animator.animations.size());
    out.write(reinterpret_cast<const char*>(&animCount), sizeof(animCount));

    for (const auto& clip : animator.animations) {
        uint32_t nameLength = static_cast<uint32_t>(clip.name.size());
        out.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
        if (nameLength > 0) {
            out.write(clip.name.data(), nameLength);
        }
        out.write(reinterpret_cast<const char*>(&clip.duration), sizeof(clip.duration));

        uint32_t channelCount = static_cast<uint32_t>(clip.channels.size());
        out.write(reinterpret_cast<const char*>(&channelCount), sizeof(channelCount));

        for (const auto& channel : clip.channels) {
            out.write(reinterpret_cast<const char*>(&channel.jointIndex), sizeof(channel.jointIndex));

            uint32_t translationKeyCount = static_cast<uint32_t>(channel.translationKeys.size());
            out.write(reinterpret_cast<const char*>(&translationKeyCount), sizeof(translationKeyCount));
            for (const auto& key : channel.translationKeys) {
                out.write(reinterpret_cast<const char*>(&key.time), sizeof(key.time));
                out.write(reinterpret_cast<const char*>(&key.value[0]), sizeof(glm::vec3));
            }

            uint32_t rotationKeyCount = static_cast<uint32_t>(channel.rotationKeys.size());
            out.write(reinterpret_cast<const char*>(&rotationKeyCount), sizeof(rotationKeyCount));
            for (const auto& key : channel.rotationKeys) {
                out.write(reinterpret_cast<const char*>(&key.time), sizeof(key.time));
                out.write(reinterpret_cast<const char*>(&key.value[0]), sizeof(glm::quat));
            }

            uint32_t scaleKeyCount = static_cast<uint32_t>(channel.scaleKeys.size());
            out.write(reinterpret_cast<const char*>(&scaleKeyCount), sizeof(scaleKeyCount));
            for (const auto& key : channel.scaleKeys) {
                out.write(reinterpret_cast<const char*>(&key.time), sizeof(key.time));
                out.write(reinterpret_cast<const char*>(&key.value[0]), sizeof(glm::vec3));
            }
        }

        // Write reflected property channels (version >= 2)
        uint32_t propertyChannelCount = static_cast<uint32_t>(clip.propertyChannels.size());
        out.write(reinterpret_cast<const char*>(&propertyChannelCount), sizeof(propertyChannelCount));
        for (const auto& channel : clip.propertyChannels) {
            uint32_t compNameLen = static_cast<uint32_t>(channel.componentName.size());
            out.write(reinterpret_cast<const char*>(&compNameLen), sizeof(compNameLen));
            if (compNameLen > 0) {
                out.write(channel.componentName.data(), compNameLen);
            }

            uint32_t fieldNameLen = static_cast<uint32_t>(channel.fieldName.size());
            out.write(reinterpret_cast<const char*>(&fieldNameLen), sizeof(fieldNameLen));
            if (fieldNameLen > 0) {
                out.write(channel.fieldName.data(), fieldNameLen);
            }

            uint32_t typeInt = static_cast<uint32_t>(channel.type);
            out.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));

            uint32_t keyCount = static_cast<uint32_t>(channel.keys.size());
            out.write(reinterpret_cast<const char*>(&keyCount), sizeof(keyCount));
            for (const auto& key : channel.keys) {
                out.write(reinterpret_cast<const char*>(&key.time), sizeof(key.time));
                out.write(reinterpret_cast<const char*>(&key.value[0]), sizeof(glm::vec4));
                uint32_t strLen = static_cast<uint32_t>(key.stringValue.size());
                out.write(reinterpret_cast<const char*>(&strLen), sizeof(strLen));
                if (strLen > 0) {
                    out.write(key.stringValue.data(), strLen);
                }
            }
        }
    }

    out.close();
    std::cout << "[ResourceManager] Successfully saved binary animation file: " << path << std::endl;
    return true;
}

bool ResourceManager::loadBinarySkeletonAndAnimations(const std::string& path, SkeletonComponent& skeleton, AnimatorComponent& animator, bool append) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "[ResourceManager] Failed to open binary animation file: " << path << std::endl;
        return false;
    }

    char magic[4];
    in.read(magic, 4);
    if (magic[0] != 'A' || magic[1] != 'N' || magic[2] != 'I' || magic[3] != 'M') {
        std::cerr << "[ResourceManager] Invalid magic header in binary animation file: " << path << std::endl;
        return false;
    }

    uint32_t version = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1 && version != 2 && version != 3) {
        std::cerr << "[ResourceManager] Unsupported version in binary animation file: " << version << std::endl;
        return false;
    }

    uint32_t jointCount = 0;
    in.read(reinterpret_cast<char*>(&jointCount), sizeof(jointCount));
    std::vector<Joint> tempJoints(jointCount);

    for (uint32_t i = 0; i < jointCount; ++i) {
        auto& joint = tempJoints[i];
        uint32_t nameLength = 0;
        in.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));
        if (nameLength > 0) {
            joint.name.resize(nameLength);
            in.read(&joint.name[0], nameLength);
        }
        in.read(reinterpret_cast<char*>(&joint.parentIndex), sizeof(joint.parentIndex));
        in.read(reinterpret_cast<char*>(&joint.inverseBindMatrix[0][0]), sizeof(glm::mat4));
        in.read(reinterpret_cast<char*>(&joint.localTransform[0][0]), sizeof(glm::mat4));
        in.read(reinterpret_cast<char*>(&joint.bindTranslation[0]), sizeof(glm::vec3));
        in.read(reinterpret_cast<char*>(&joint.bindRotation[0]), sizeof(glm::quat));
        in.read(reinterpret_cast<char*>(&joint.bindScale[0]), sizeof(glm::vec3));
    }

    bool targetHasSkeleton = !skeleton.joints.empty();
    if (!targetHasSkeleton) {
        skeleton.joints = tempJoints;
        skeleton.jointMatrices.assign(jointCount, glm::mat4(1.0f));
    }

    uint32_t animCount = 0;
    in.read(reinterpret_cast<char*>(&animCount), sizeof(animCount));
    
    std::vector<AnimationClip> loadedClips(animCount);

    for (uint32_t c = 0; c < animCount; ++c) {
        auto& clip = loadedClips[c];
        uint32_t nameLength = 0;
        in.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));
        if (nameLength > 0) {
            clip.name.resize(nameLength);
            in.read(&clip.name[0], nameLength);
        }
        in.read(reinterpret_cast<char*>(&clip.duration), sizeof(clip.duration));

        uint32_t channelCount = 0;
        in.read(reinterpret_cast<char*>(&channelCount), sizeof(channelCount));
        clip.channels.resize(channelCount);

        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            auto& channel = clip.channels[ch];
            int savedIndex = -1;
            in.read(reinterpret_cast<char*>(&savedIndex), sizeof(savedIndex));

            // Resolve the joint name from tempJoints
            std::string jointName = "";
            if (savedIndex >= 0 && savedIndex < static_cast<int>(tempJoints.size())) {
                jointName = tempJoints[savedIndex].name;
            }
            channel.jointName = jointName;

            // Map the channel's jointIndex to the target skeleton joint index by name
            channel.jointIndex = -1;
            if (!jointName.empty()) {
                for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                    if (skeleton.joints[i].name == jointName) {
                        channel.jointIndex = static_cast<int>(i);
                        break;
                    }
                }
            }

            uint32_t translationKeyCount = 0;
            in.read(reinterpret_cast<char*>(&translationKeyCount), sizeof(translationKeyCount));
            channel.translationKeys.resize(translationKeyCount);
            for (uint32_t k = 0; k < translationKeyCount; ++k) {
                auto& key = channel.translationKeys[k];
                in.read(reinterpret_cast<char*>(&key.time), sizeof(key.time));
                in.read(reinterpret_cast<char*>(&key.value[0]), sizeof(glm::vec3));
            }

            uint32_t rotationKeyCount = 0;
            in.read(reinterpret_cast<char*>(&rotationKeyCount), sizeof(rotationKeyCount));
            channel.rotationKeys.resize(rotationKeyCount);
            for (uint32_t k = 0; k < rotationKeyCount; ++k) {
                auto& key = channel.rotationKeys[k];
                in.read(reinterpret_cast<char*>(&key.time), sizeof(key.time));
                in.read(reinterpret_cast<char*>(&key.value[0]), sizeof(glm::quat));
            }

            uint32_t scaleKeyCount = 0;
            in.read(reinterpret_cast<char*>(&scaleKeyCount), sizeof(scaleKeyCount));
            channel.scaleKeys.resize(scaleKeyCount);
            for (uint32_t k = 0; k < scaleKeyCount; ++k) {
                auto& key = channel.scaleKeys[k];
                in.read(reinterpret_cast<char*>(&key.time), sizeof(key.time));
                in.read(reinterpret_cast<char*>(&key.value[0]), sizeof(glm::vec3));
            }
        }

        if (version >= 2) {
            uint32_t propertyChannelCount = 0;
            in.read(reinterpret_cast<char*>(&propertyChannelCount), sizeof(propertyChannelCount));
            clip.propertyChannels.resize(propertyChannelCount);
            for (uint32_t pch = 0; pch < propertyChannelCount; ++pch) {
                auto& channel = clip.propertyChannels[pch];

                uint32_t compNameLen = 0;
                in.read(reinterpret_cast<char*>(&compNameLen), sizeof(compNameLen));
                channel.componentName.resize(compNameLen);
                if (compNameLen > 0) {
                    in.read(&channel.componentName[0], compNameLen);
                }

                uint32_t fieldNameLen = 0;
                in.read(reinterpret_cast<char*>(&fieldNameLen), sizeof(fieldNameLen));
                channel.fieldName.resize(fieldNameLen);
                if (fieldNameLen > 0) {
                    in.read(&channel.fieldName[0], fieldNameLen);
                }

                uint32_t typeInt = 0;
                in.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
                channel.type = static_cast<Engine::FieldType>(typeInt);

                uint32_t keyCount = 0;
                in.read(reinterpret_cast<char*>(&keyCount), sizeof(keyCount));
                channel.keys.resize(keyCount);
                for (uint32_t k = 0; k < keyCount; ++k) {
                    auto& key = channel.keys[k];
                    in.read(reinterpret_cast<char*>(&key.time), sizeof(key.time));
                    in.read(reinterpret_cast<char*>(&key.value[0]), sizeof(glm::vec4));
                    if (version >= 3) {
                        uint32_t strLen = 0;
                        in.read(reinterpret_cast<char*>(&strLen), sizeof(strLen));
                        if (strLen > 0) {
                            key.stringValue.resize(strLen);
                            in.read(&key.stringValue[0], strLen);
                        }
                    }
                }
            }
        }
    }

    if (!append) {
        animator.animations.clear();
    }
    for (auto& clip : loadedClips) {
        bool duplicateFound = false;
        for (auto& existing : animator.animations) {
            if (existing.name == clip.name) {
                existing = std::move(clip);
                duplicateFound = true;
                break;
            }
        }
        if (!duplicateFound) {
            animator.animations.push_back(std::move(clip));
        }
    }

    if (!animator.animations.empty()) {
        animator.activeAnimationIndex = 0; // Default active clip
    }

    in.close();
    return true;
}

void ResourceManager::updateTextureFilterMode(const std::string& path, VulkanRenderer& renderer, TextureFilterMode filterMode) {
    auto it = textureCache.find(path);
    if (it != textureCache.end()) {
        Texture& texture = *it->second;
        VkDevice device = renderer.device.getDevice();
        
        // Wait for device to be idle to safely destroy the sampler
        vkDeviceWaitIdle(device);
        
        if (texture.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, texture.sampler, nullptr);
            texture.sampler = VK_NULL_HANDLE;
        }
        
        createTextureSampler(device, texture, filterMode);
        
        // Update the descriptor set in-place
        renderer.descriptors.updateTextureDescriptorSet(
            texture.descriptorSet,
            texture.imageView, texture.sampler,
            defaultNormalTexture.imageView, defaultNormalTexture.sampler,
            defaultMetallicTexture.imageView, defaultMetallicTexture.sampler
        );
    }
}

Texture* ResourceManager::createTextureFromPixels(const std::string& cacheKey,
                                                   const uint8_t* pixels, int width, int height,
                                                   VulkanRenderer& renderer,
                                                   TextureFilterMode filterMode) {
    if (!pixels || width <= 0 || height <= 0) return nullptr;

    // Evict any previous entry under this key
    evictTexture(cacheKey, renderer.device.getDevice());

    auto texture = std::make_unique<Texture>();
    texture->path       = cacheKey;
    texture->width      = width;
    texture->height     = height;
    texture->filterMode = filterMode;

    try {
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * 4);

        VulkanBuffer stagingBuffer;
        stagingBuffer.create(
            renderer.device.getDevice(),
            renderer.device.getPhysicalDevice(),
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        stagingBuffer.uploadData(pixels, imageSize);

        createImage(
            renderer.device.getDevice(),
            renderer.device.getPhysicalDevice(),
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            texture->image,
            texture->memory,
            *this
        );

        transitionImageLayout(renderer, texture->image, VK_FORMAT_R8G8B8A8_SRGB,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(renderer, stagingBuffer.get(), texture->image,
                          static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        transitionImageLayout(renderer, texture->image, VK_FORMAT_R8G8B8A8_SRGB,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        stagingBuffer.destroy();

        createTextureImageView(renderer.device.getDevice(), *texture);
        createTextureSampler(renderer.device.getDevice(), *texture, filterMode);
        renderer.descriptors.allocateTextureDescriptorSet(
            texture->descriptorSet,
            texture->imageView, texture->sampler,
            defaultNormalTexture.imageView, defaultNormalTexture.sampler,
            defaultMetallicTexture.imageView, defaultMetallicTexture.sampler
        );
        renderer.descriptors.allocateSingleTextureDescriptorSet(
            texture->singleDescriptorSet,
            texture->imageView, texture->sampler
        );
    } catch (const std::exception& e) {
        std::cerr << "[ResourceManager] createTextureFromPixels failed for key '"
                  << cacheKey << "': " << e.what() << std::endl;
        return nullptr;
    }

    Texture* ptr = texture.get();
    textureCache[cacheKey] = std::move(texture);
    return ptr;
}

void ResourceManager::evictTexture(const std::string& cacheKey, VkDevice device) {
    auto it = textureCache.find(cacheKey);
    if (it == textureCache.end()) return;

    vkDeviceWaitIdle(device);
    Texture& tex = *it->second;
    if (tex.sampler    != VK_NULL_HANDLE) vkDestroySampler(device, tex.sampler, nullptr);
    if (tex.imageView  != VK_NULL_HANDLE) vkDestroyImageView(device, tex.imageView, nullptr);
    if (tex.image      != VK_NULL_HANDLE) vkDestroyImage(device, tex.image, nullptr);
    if (tex.memory     != VK_NULL_HANDLE) vkFreeMemory(device, tex.memory, nullptr);
    textureCache.erase(it);
}
