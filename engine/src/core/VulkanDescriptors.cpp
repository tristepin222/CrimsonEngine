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
    poolSizes[0].descriptorCount = 100; // Increased UBO capacity for bones
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 100; // Textures

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 200; // Increased set capacity

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");
}

/**
 * @brief Configures binding requirements and instantiates layout description.
 */
void VulkanDescriptors::createCameraDescriptorSetLayout() {
    // Binding for the camera UBO
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &cameraDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create camera descriptor set layout");
}

/**
 * @brief Allocates camera descriptor set and updates layout binding.
 * @param uniformBuffer GPU uniform buffer handle.
 * @param bufferSize Size of uniform buffer memory.
 */
void VulkanDescriptors::allocateCameraDescriptorSets(VkBuffer uniformBuffer, VkDeviceSize bufferSize) {
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

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = cameraDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
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
