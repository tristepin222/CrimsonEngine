#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <stdexcept>

/**
 * @class VulkanDescriptors
 * @brief Manages the descriptor pool, descriptor layouts, and descriptor set allocations for UBO resources.
 */
class VulkanDescriptors {
public:
    /**
     * @brief Construct a new Vulkan Descriptors object.
     */
    VulkanDescriptors() = default;
    /**
     * @brief Destroy the Vulkan Descriptors object and release descriptor resources.
     */
    ~VulkanDescriptors();

    VulkanDescriptors(const VulkanDescriptors&) = delete;
    VulkanDescriptors& operator=(const VulkanDescriptors&) = delete;

    // Initialize descriptor system
    /**
     * @brief Creates the descriptor pool.
     * @param device Vulkan logical device context.
     * @param maxFramesInFlight Allocated size constraints for double-buffering.
     */
    void create(VkDevice device, uint32_t maxFramesInFlight = 2);

    // Create layout for a uniform buffer (e.g., camera UBO)
    /**
     * @brief Creates descriptor set layouts defining resource bindings for camera.
     */
    void createCameraDescriptorSetLayout();

    /**
     * @brief Creates descriptor set layout for texture samplers.
     */
    void createTextureDescriptorSetLayout();

    /**
     * @brief Creates descriptor set layout for a single texture (diffuse only, for ImGui/UI).
     */
    void createSingleTextureDescriptorSetLayout();

    /**
     * @brief Creates descriptor set layout for joints/bones matrices.
     */
    void createJointsDescriptorSetLayout();

    /**
     * @brief Creates descriptor set layout for terrain 4-layer splatting (1 splatmap + 4 albedos + 4 normals + 1 UBO).
     */
    void createTerrainDescriptorSetLayout();

    /**
     * @brief Allocates and binds descriptor sets for the camera UBO and directional shadow map.
     * @param uniformBuffer The GPU buffer containing camera matrices.
     * @param bufferSize Size of UBO buffer.
     * @param shadowView Directional shadow map image view.
     * @param shadowSampler Directional shadow depth comparison sampler.
     */
    void allocateCameraDescriptorSets(
        VkBuffer uniformBuffer, VkDeviceSize bufferSize,
        VkImageView shadowView, VkSampler shadowSampler,
        VkImageView pointShadowView = VK_NULL_HANDLE
    );

    /**
     * @brief Updates the shadow map samplers in the camera descriptor set.
     */
    void updateShadowDescriptor(
        VkImageView shadowView, VkSampler shadowSampler,
        VkImageView pointShadowView = VK_NULL_HANDLE
    );

    /**
     * @brief Allocates and binds descriptor set for texture samplers (diffuse, normal, metallic).
     * @param descriptorSet Reference to target descriptor set to allocate.
     * @param diffuseView Diffuse image view.
     * @param diffuseSampler Diffuse sampler.
     * @param normalView Normal map image view.
     * @param normalSampler Normal map sampler.
     * @param metallicView Metallic map image view.
     * @param metallicSampler Metallic map sampler.
     */
    void allocateTextureDescriptorSet(
        VkDescriptorSet& descriptorSet,
        VkImageView diffuseView, VkSampler diffuseSampler,
        VkImageView normalView, VkSampler normalSampler,
        VkImageView metallicView, VkSampler metallicSampler
    );

    /**
     * @brief Updates an existing texture descriptor set with new sampler bindings.
     */
    void updateTextureDescriptorSet(
        VkDescriptorSet descriptorSet,
        VkImageView diffuseView, VkSampler diffuseSampler,
        VkImageView normalView, VkSampler normalSampler,
        VkImageView metallicView, VkSampler metallicSampler
    );

    /**
     * @brief Allocates and binds descriptor set for a single texture sampler.
     */
    void allocateSingleTextureDescriptorSet(
        VkDescriptorSet& descriptorSet,
        VkImageView view, VkSampler sampler
    );

    /**
     * @brief Updates an existing single texture descriptor set.
     */
    void updateSingleTextureDescriptorSet(
        VkDescriptorSet descriptorSet,
        VkImageView view, VkSampler sampler
    );

    /**
     * @brief Allocates and binds descriptor set for a skeleton joint matrices buffer.
     * @param descriptorSet Reference to target descriptor set to allocate.
     * @param uniformBuffer GPU buffer.
     * @param range Buffer segment size.
     */
    void allocateJointsDescriptorSet(VkDescriptorSet& descriptorSet, VkBuffer uniformBuffer, VkDeviceSize range);

    /**
     * @brief Allocates and binds descriptor set for terrain 4-layer splatting.
     */
    void allocateTerrainDescriptorSet(
        VkDescriptorSet& descriptorSet,
        VkImageView splatView, VkSampler splatSampler,
        const std::array<VkImageView, 4>& albedoViews, const std::array<VkSampler, 4>& albedoSamplers,
        const std::array<VkImageView, 4>& normalViews, const std::array<VkSampler, 4>& normalSamplers,
        VkBuffer uboBuffer, VkDeviceSize uboSize
    );

    /**
     * @brief Updates an existing terrain descriptor set.
     */
    void updateTerrainDescriptorSet(
        VkDescriptorSet descriptorSet,
        VkImageView splatView, VkSampler splatSampler,
        const std::array<VkImageView, 4>& albedoViews, const std::array<VkSampler, 4>& albedoSamplers,
        const std::array<VkImageView, 4>& normalViews, const std::array<VkSampler, 4>& normalSamplers,
        VkBuffer uboBuffer, VkDeviceSize uboSize
    );

    // Cleanup
    /**
     * @brief Safely destroys layout bindings and descriptor pools.
     */
    void destroy();

    // Getters
    /**
     * @brief Gets raw camera descriptor set layout.
     * @return VkDescriptorSetLayout handle.
     */
    VkDescriptorSetLayout getCameraDescriptorSetLayout() const { return cameraDescriptorSetLayout; }
    /**
     * @brief Gets raw texture descriptor set layout.
     * @return VkDescriptorSetLayout handle.
     */
    VkDescriptorSetLayout getTextureDescriptorSetLayout() const { return textureDescriptorSetLayout; }
    /**
     * @brief Gets raw single-texture descriptor set layout.
     * @return VkDescriptorSetLayout handle.
     */
    VkDescriptorSetLayout getSingleTextureDescriptorSetLayout() const { return singleTextureDescriptorSetLayout; }
    /**
     * @brief Gets raw joints descriptor set layout.
     * @return VkDescriptorSetLayout handle.
     */
    VkDescriptorSetLayout getJointsDescriptorSetLayout() const { return jointsDescriptorSetLayout; }
    /**
     * @brief Gets raw terrain descriptor set layout.
     * @return VkDescriptorSetLayout handle.
     */
    VkDescriptorSetLayout getTerrainDescriptorSetLayout() const { return terrainDescriptorSetLayout; }
    /**
     * @brief Gets raw camera descriptor set.
     * @return VkDescriptorSet handle.
     */
    VkDescriptorSet getCameraDescriptorSet() const { return cameraDescriptorSet; }
    /**
     * @brief Gets raw descriptor pool.
     * @return VkDescriptorPool handle.
     */
    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }

private:
    /** @brief Reference to logical device. */
    VkDevice device = VK_NULL_HANDLE;

    /** @brief Descriptor pool allocator. */
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    /** @brief Descriptor set layout allocated for camera. */
    VkDescriptorSetLayout cameraDescriptorSetLayout = VK_NULL_HANDLE;
    /** @brief Descriptor set layout allocated for textures. */
    VkDescriptorSetLayout textureDescriptorSetLayout = VK_NULL_HANDLE;
    /** @brief Descriptor set layout allocated for single-texture bindings (ImGui/UI). */
    VkDescriptorSetLayout singleTextureDescriptorSetLayout = VK_NULL_HANDLE;
    /** @brief Descriptor set layout allocated for joints. */
    VkDescriptorSetLayout jointsDescriptorSetLayout = VK_NULL_HANDLE;
    /** @brief Descriptor set layout allocated for terrain splatting. */
    VkDescriptorSetLayout terrainDescriptorSetLayout = VK_NULL_HANDLE;
    /** @brief Descriptor set allocated for camera. */
    VkDescriptorSet cameraDescriptorSet = VK_NULL_HANDLE;
};
