#include "VulkanDescriptors.hpp"

/**
 * @brief Destroy the Vulkan Descriptors:: Vulkan Descriptors object.
 */
VulkanDescriptors::~VulkanDescriptors() {
    destroy();
}

/**
 * @brief Safely destroys descriptor pools and layouts.
 */
void VulkanDescriptors::destroy() {
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (cameraDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, cameraDescriptorSetLayout, nullptr);
        cameraDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (textureDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout, nullptr);
        textureDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (singleTextureDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, singleTextureDescriptorSetLayout, nullptr);
        singleTextureDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (jointsDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, jointsDescriptorSetLayout, nullptr);
        jointsDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (terrainDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, terrainDescriptorSetLayout, nullptr);
        terrainDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

/**
 * @brief Allocates and initializes the Vulkan descriptor pool.
 * @param dev Logical device context.
 */
void VulkanDescriptors::create(VkDevice dev, uint32_t /*maxFramesInFlight*/) {
    device = dev;

    // Create descriptor pool for Uniform Buffers, Image Samplers, and Bone Palettes
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 500; // Increased UBO capacity for camera, bones, terrain
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1000; // Textures, splatmaps, layers

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 500; // Increased set capacity

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");
}

/**
 * @brief Configures binding requirements and instantiates layout description.
 */
void VulkanDescriptors::createCameraDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[3]{};

    // Binding 0: Camera UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Directional & Spot Shadow Map Array (sampler2DArrayShadow)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Binding 2: Omnidirectional Point Shadow Map Cubemap (samplerCubeShadow)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &cameraDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create camera descriptor set layout");
}

/**
 * @brief Allocates camera descriptor set and updates layout binding.
 * @param uniformBuffer GPU uniform buffer handle.
 * @param bufferSize Size of uniform buffer memory.
 * @param shadowView Directional/Spot shadow map array image view.
 * @param shadowSampler Directional shadow depth comparison sampler.
 * @param pointShadowView Point shadow cubemap image view.
 */
void VulkanDescriptors::allocateCameraDescriptorSets(
    VkBuffer uniformBuffer, VkDeviceSize bufferSize,
    VkImageView shadowView, VkSampler shadowSampler,
    VkImageView pointShadowView
) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &cameraDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &cameraDescriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate camera descriptor set");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = bufferSize;

    VkDescriptorImageInfo shadowImageInfo{};
    shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowImageInfo.imageView = shadowView;
    shadowImageInfo.sampler = shadowSampler;

    VkDescriptorImageInfo pointShadowImageInfo{};
    pointShadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pointShadowImageInfo.imageView = (pointShadowView != VK_NULL_HANDLE) ? pointShadowView : shadowView;
    pointShadowImageInfo.sampler = shadowSampler;

    VkWriteDescriptorSet descriptorWrites[3]{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = cameraDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = cameraDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &shadowImageInfo;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = cameraDescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &pointShadowImageInfo;

    vkUpdateDescriptorSets(device, 3, descriptorWrites, 0, nullptr);
}

void VulkanDescriptors::updateShadowDescriptor(
    VkImageView shadowView, VkSampler shadowSampler,
    VkImageView pointShadowView
) {
    if (cameraDescriptorSet == VK_NULL_HANDLE || device == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo shadowImageInfo{};
    shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowImageInfo.imageView = shadowView;
    shadowImageInfo.sampler = shadowSampler;

    VkDescriptorImageInfo pointShadowImageInfo{};
    pointShadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pointShadowImageInfo.imageView = (pointShadowView != VK_NULL_HANDLE) ? pointShadowView : shadowView;
    pointShadowImageInfo.sampler = shadowSampler;

    VkWriteDescriptorSet descriptorWrites[2]{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = cameraDescriptorSet;
    descriptorWrites[0].dstBinding = 1;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &shadowImageInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = cameraDescriptorSet;
    descriptorWrites[1].dstBinding = 2;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &pointShadowImageInfo;

    vkUpdateDescriptorSets(device, 2, descriptorWrites, 0, nullptr);
}

/**
 * @brief Configures binding requirements for texture samplers and instantiates layout description.
 */
void VulkanDescriptors::createTextureDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[3]{};
    
    // Binding 0: Diffuse sampler
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Normal map sampler
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Binding 2: Metallic map sampler
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &textureDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture descriptor set layout");
}

void VulkanDescriptors::createSingleTextureDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &singleTextureDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create single texture descriptor set layout");
}

void VulkanDescriptors::allocateTextureDescriptorSet(
    VkDescriptorSet& descriptorSet,
    VkImageView diffuseView, VkSampler diffuseSampler,
    VkImageView normalView, VkSampler normalSampler,
    VkImageView metallicView, VkSampler metallicSampler
) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate texture descriptor set");

    updateTextureDescriptorSet(descriptorSet, diffuseView, diffuseSampler, normalView, normalSampler, metallicView, metallicSampler);
}

void VulkanDescriptors::updateTextureDescriptorSet(
    VkDescriptorSet descriptorSet,
    VkImageView diffuseView, VkSampler diffuseSampler,
    VkImageView normalView, VkSampler normalSampler,
    VkImageView metallicView, VkSampler metallicSampler
) {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    VkDescriptorImageInfo imageInfos[3]{};
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = diffuseView;
    imageInfos[0].sampler = diffuseSampler;

    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = normalView;
    imageInfos[1].sampler = normalSampler;

    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].imageView = metallicView;
    imageInfos[2].sampler = metallicSampler;

    VkWriteDescriptorSet descriptorWrites[3]{};
    for (int i = 0; i < 3; ++i) {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = descriptorSet;
        descriptorWrites[i].dstBinding = i;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(device, 3, descriptorWrites, 0, nullptr);
}

void VulkanDescriptors::allocateSingleTextureDescriptorSet(
    VkDescriptorSet& descriptorSet,
    VkImageView view, VkSampler sampler
) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &singleTextureDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate single texture descriptor set");

    updateSingleTextureDescriptorSet(descriptorSet, view, sampler);
}

void VulkanDescriptors::updateSingleTextureDescriptorSet(
    VkDescriptorSet descriptorSet,
    VkImageView view, VkSampler sampler
) {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

/**
 * @brief Configures binding requirements for joint matrices and instantiates layout description.
 */
void VulkanDescriptors::createJointsDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding jointsLayoutBinding{};
    jointsLayoutBinding.binding = 0;
    jointsLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    jointsLayoutBinding.descriptorCount = 1;
    jointsLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    jointsLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &jointsLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &jointsDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create joints descriptor set layout");
}

/**
 * @brief Allocates joints descriptor set and updates binding with joint matrices uniform buffer.
 */
void VulkanDescriptors::allocateJointsDescriptorSet(VkDescriptorSet& descriptorSet, VkBuffer uniformBuffer, VkDeviceSize range) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &jointsDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate joints descriptor set");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = range;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

void VulkanDescriptors::createTerrainDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 10> bindings{};

    // Bindings 0..8: Combined image samplers (0: splatmap, 1..4: albedos, 5..8: normals)
    for (uint32_t i = 0; i < 9; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].pImmutableSamplers = nullptr;
    }

    // Binding 9: Terrain UBO
    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[9].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &terrainDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create terrain descriptor set layout");
}

void VulkanDescriptors::allocateTerrainDescriptorSet(
    VkDescriptorSet& descriptorSet,
    VkImageView splatView, VkSampler splatSampler,
    const std::array<VkImageView, 4>& albedoViews, const std::array<VkSampler, 4>& albedoSamplers,
    const std::array<VkImageView, 4>& normalViews, const std::array<VkSampler, 4>& normalSamplers,
    VkBuffer uboBuffer, VkDeviceSize uboSize
) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &terrainDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate terrain descriptor set");

    updateTerrainDescriptorSet(
        descriptorSet,
        splatView, splatSampler,
        albedoViews, albedoSamplers,
        normalViews, normalSamplers,
        uboBuffer, uboSize
    );
}

void VulkanDescriptors::updateTerrainDescriptorSet(
    VkDescriptorSet descriptorSet,
    VkImageView splatView, VkSampler splatSampler,
    const std::array<VkImageView, 4>& albedoViews, const std::array<VkSampler, 4>& albedoSamplers,
    const std::array<VkImageView, 4>& normalViews, const std::array<VkSampler, 4>& normalSamplers,
    VkBuffer uboBuffer, VkDeviceSize uboSize
) {
    if (descriptorSet == VK_NULL_HANDLE || device == VK_NULL_HANDLE) return;

    std::array<VkDescriptorImageInfo, 9> imageInfos{};
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = splatView;
    imageInfos[0].sampler = splatSampler;

    for (size_t i = 0; i < 4; ++i) {
        imageInfos[1 + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1 + i].imageView = albedoViews[i];
        imageInfos[1 + i].sampler = albedoSamplers[i];
    }

    for (size_t i = 0; i < 4; ++i) {
        imageInfos[5 + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[5 + i].imageView = normalViews[i];
        imageInfos[5 + i].sampler = normalSamplers[i];
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uboBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = uboSize;

    std::array<VkWriteDescriptorSet, 10> descriptorWrites{};
    for (uint32_t i = 0; i < 9; ++i) {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = descriptorSet;
        descriptorWrites[i].dstBinding = i;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pImageInfo = &imageInfos[i];
    }

    descriptorWrites[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[9].dstSet = descriptorSet;
    descriptorWrites[9].dstBinding = 9;
    descriptorWrites[9].dstArrayElement = 0;
    descriptorWrites[9].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[9].descriptorCount = 1;
    descriptorWrites[9].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

