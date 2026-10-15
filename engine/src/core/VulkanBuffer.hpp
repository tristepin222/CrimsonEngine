#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <cstring>

/**
 * @class VulkanBuffer
 * @brief Manages a Vulkan buffer handle and its allocated GPU memory.
 */
class VulkanBuffer {
public:
    /**
     * @brief Construct a new Vulkan Buffer object.
     */
    VulkanBuffer() = default;
    /**
     * @brief Destroy the Vulkan Buffer object and release resources.
     */
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    /**
     * @brief Move constructor.
     * @param other Buffer to move from.
     */
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    /**
     * @brief Move assignment operator.
     * @param other Buffer to move.
     * @return Reference to self.
     */
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    /**
     * @brief Creates and allocates the buffer.
     * @param device Vulkan logical device context.
     * @param physicalDevice Vulkan physical device.
     * @param size Size in bytes.
     * @param usage Usage bitmask flags.
     * @param properties Memory properties.
     */
    void create(VkDevice device, VkPhysicalDevice physicalDevice,
        VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties);

    /**
     * @brief Frees the buffer and memory.
     */
    void destroy();

    /**
     * @brief Maps buffer memory and uploads data.
     * @param srcData Source pointer.
     * @param dataSize Size of data to transfer.
     */
    void uploadData(const void* srcData, VkDeviceSize dataSize);

    /**
     * @brief Maps buffer memory and uploads partial data.
     * @param srcData Source pointer.
     * @param offset Byte offset into the destination buffer.
     * @param dataSize Size of data to transfer in bytes.
     */
    void uploadSubData(const void* srcData, VkDeviceSize offset, VkDeviceSize dataSize);

    /**
     * @brief Gets raw Vulkan buffer.
     * @return Vulkan buffer.
     */
    VkBuffer get() const { return buffer; }
    /**
     * @brief Gets Vulkan device memory.
     * @return Device memory handle.
     */
    VkDeviceMemory getMemory() const { return memory; }
    /**
     * @brief Gets buffer size.
     * @return Size in bytes.
     */
    VkDeviceSize getSize() const { return bufferSize; }
    /**
     * @brief Gets pointer to persistently mapped memory (if HOST_VISIBLE).
     * @return Pointer to mapped memory, or nullptr if not persistently mapped.
     */
    void* getMappedData() const { return mappedData; }

private:
    /** @brief Reference to logical device. */
    VkDevice device = VK_NULL_HANDLE;
    /** @brief Reference to physical device. */
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    /** @brief Raw buffer handle. */
    VkBuffer buffer = VK_NULL_HANDLE;
    /** @brief Raw memory handle. */
    VkDeviceMemory memory = VK_NULL_HANDLE;
    /** @brief Size allocated in bytes. */
    VkDeviceSize bufferSize = 0;
    /** @brief Persistently mapped memory pointer. */
    void* mappedData = nullptr;

    /**
     * @brief Locates suitable memory type.
     * @param typeFilter Type filter bitmask.
     * @param properties Desired memory properties.
     * @return Memory type index.
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};
