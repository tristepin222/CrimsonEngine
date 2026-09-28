#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <iostream>

/**
 * @class VulkanSwapchain
 * @brief Handles creation, extent negotiation, rendering passes, and framebuffer generation for Vulkan Swapchains.
 */
class VulkanSwapchain {
public:
    /**
     * @brief Construct a new Vulkan Swapchain object.
     */
    VulkanSwapchain() = default;
    /**
     * @brief Destroy the Vulkan Swapchain object and release swapchain structures.
     */
    ~VulkanSwapchain();

    /**
     * @brief Sets up parameters, selects formats, builds KHR swapchain, views, render pass and framebuffers.
     * @param device Vulkan logical device context.
     * @param physicalDevice Vulkan physical device.
     * @param surface Presentation surface context.
     * @param graphicsQueueFamily Target graphics family index.
     */
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t graphicsQueueFamily);
    /**
     * @brief Safely destroys framebuffers, render pass, image views, and swapchain handles.
     */
    void cleanup();

    /**
     * @brief Gets selected swapchain format.
     * @return VkFormat.
     */
    VkFormat getImageFormat() const { return imageFormat; }
    /**
     * @brief Gets resolution extent details.
     * @return VkExtent2D.
     */
    VkExtent2D getExtent() const { return extent; }
    /**
     * @brief Gets active framebuffers list.
     * @return Vector of framebuffer handles.
     */
    const std::vector<VkFramebuffer>& getFramebuffers() const { return framebuffers; }
    /** @brief Gets default render pass for swapchain buffers. */
    VkRenderPass getRenderPass() const { return renderPass; }
    /** @brief Gets raw KHR swapchain. */
    VkSwapchainKHR getSwapchain() const { return swapchain; }
    /** @brief Gets depth image view. */
    VkImageView getDepthImageView() const { return depthImageView; }
    /** @brief Gets depth image. */
    VkImage getDepthImage() const { return depthImage; }
    /** @brief Gets depth format. */
    VkFormat getDepthFormat() const { return depthFormat; }

private:
    /** @brief Reference to logical device. */
    VkDevice device = VK_NULL_HANDLE;
    /** @brief Reference to physical device. */
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    /** @brief Reference to presentation surface. */
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    /** @brief Raw swapchain handle. */
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    /** @brief Retrieved swapchain images. */
    std::vector<VkImage> images;
    /** @brief image views wrapping swapchain images. */
    std::vector<VkImageView> imageViews;
    /** @brief framebuffers wrapping image views. */
    std::vector<VkFramebuffer> framebuffers;

    /** @brief image format of swapchain buffers. */
    VkFormat imageFormat{};
    /** @brief image extent (resolution) of swapchain buffers. */
    VkExtent2D extent{};
    /** @brief Render pass used for frame clearing and drawing. */
    VkRenderPass renderPass = VK_NULL_HANDLE;

    /** @brief GPU depth image allocation handle. */
    VkImage depthImage = VK_NULL_HANDLE;
    /** @brief GPU depth memory handle. */
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    /** @brief Depth image view wrapper. */
    VkImageView depthImageView = VK_NULL_HANDLE;
    /** @brief Selected depth format. */
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

private:
    /**
     * @brief Establishes the KHR swapchain handle.
     * @param graphicsQueueFamily Queue family index.
     */
    void createSwapchain(uint32_t graphicsQueueFamily);
    /**
     * @brief Creates image views for each swapchain image.
     */
    void createImageViews();
    /**
     * @brief Configures render pass layouts.
     */
    void createRenderPass();
    /**
     * @brief Creates framebuffers linking image views to the render pass.
     */
    void createFramebuffers();
    /**
     * @brief Creates depth buffers and views.
     */
    void createDepthResources();
    /**
     * @brief Helper to query and select best depth format.
     */
    VkFormat findDepthFormat();
    /**
     * @brief Helper to find supported depth formats.
     */
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    /**
     * @brief Helper to query suitable GPU memory index.
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    /**
     * @brief Selects optimal color format and color space.
     * @param formats List of supported formats.
     * @return Ideal surface format.
     */
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    /**
     * @brief Selects optimal presentation mode (e.g. Mailbox or FIFO).
     * @param modes List of supported modes.
     * @return Ideal present mode.
     */
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes);
    /**
     * @brief Resolves swapchain resolution boundaries against hardware limits.
     * @param capabilities Surface capabilities.
     * @return Optimal swapchain extent.
     */
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};
