#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <fstream>
#include "ecs/components/pushconstants.hpp"
#include "ecs/components/Mesh.hpp"

/**
 * @class VulkanPipeline
 * @brief Manages the compilation, configuration, and execution layout of a graphics pipeline.
 */
class VulkanPipeline {
public:
    /**
     * @brief Construct a new Vulkan Pipeline object.
     */
    VulkanPipeline() = default;
    /**
     * @brief Destroy the Vulkan Pipeline object and release pipeline structures.
     */
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    /**
     * @brief Configures vertex inputs, layouts, rasterization, color blending, and creates graphics pipeline.
     * @param device Vulkan logical device context.
     * @param swapchainExtent Framebuffer resolution dimensions.
     * @param renderPass Render pass target context.
     * @param vertPath Path to vertex shader bytecode.
     * @param fragPath Path to fragment shader bytecode.
     * @param descriptorSetLayouts Set layouts bound to pipeline layout.
     */
    void create(VkDevice device,
        VkExtent2D swapchainExtent,
        VkRenderPass renderPass,
        const std::string& vertPath,
        const std::string& fragPath,
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
        const std::vector<VkVertexInputBindingDescription>& customBindings = {},
        const std::vector<VkVertexInputAttributeDescription>& customAttributes = {},
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE);

    /**
     * @brief Safely destroys graphics pipeline and layouts.
     */
    void destroy();

    /**
     * @brief Gets compiled graphics pipeline.
     * @return VkPipeline handle.
     */
    VkPipeline get() const { return graphicsPipeline; }
    /**
     * @brief Gets layout representing pipeline descriptors and push constants.
     * @return VkPipelineLayout handle.
     */
    VkPipelineLayout getLayout() const { return pipelineLayout; }

private:
    /** @brief Reference to logical device. */
    VkDevice device = VK_NULL_HANDLE;
    /** @brief Raw Vulkan pipeline object. */
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    /** @brief Raw Vulkan pipeline layout object. */
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

private:
    /**
     * @brief Wraps raw shader bytecode into a Vulkan module handle.
     * @param code Byte array.
     * @return Shader module handle.
     */
    VkShaderModule createShaderModule(const std::vector<char>& code);
    /**
     * @brief Helper to load binary files (SPIR-V shaders).
     * @param filename Shader path.
     * @return Vector of bytes.
     */
    std::vector<char> loadFile(const std::string& filename);
};
