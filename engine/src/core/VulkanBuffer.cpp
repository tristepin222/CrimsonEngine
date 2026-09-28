#include "VulkanBuffer.hpp"

/**
 * @brief Destroy the Vulkan Buffer:: Vulkan Buffer object and release Vulkan handles.
 */
VulkanBuffer::~VulkanBuffer() {
    destroy();
}

/**
 * @brief Move constructor.
 * @param other Buffer to move from.
 */
VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept {
    *this = std::move(other);
}

/**
 * @brief Move assignment operator.
 * @param other Buffer to move.
 * @return Reference to self.
 */
VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        device = other.device;
        physicalDevice = other.physicalDevice;
        buffer = other.buffer;
        memory = other.memory;
        bufferSize = other.bufferSize;
        mappedData = other.mappedData;

        other.device = VK_NULL_HANDLE;
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.bufferSize = 0;
        other.mappedData = nullptr;
    }
    return *this;
}

/**
 * @brief Creates and allocates the buffer.
 * @param dev Vulkan logical device context.
 * @param phys Vulkan physical device.
 * @param size Buffer size.
 * @param usage Usage flags.
 * @param properties Memory properties.
 */
void VulkanBuffer::create(VkDevice dev, VkPhysicalDevice phys,
    VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties) {
    destroy();
    device = dev;
    physicalDevice = phys;
    bufferSize = size;
    mappedData = nullptr;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");

    vkBindBufferMemory(device, buffer, memory, 0);

    // Persistently map host-visible coherent buffers for maximum update performance
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
        (properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        if (vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mappedData) != VK_SUCCESS) {
            mappedData = nullptr;
        }
    }
}

/**
 * @brief Safely destroys the buffer and memory.
 */
void VulkanBuffer::destroy() {
    if (mappedData != nullptr) {
        if (device != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) {
            vkUnmapMemory(device, memory);
        }
        mappedData = nullptr;
    }
    if (buffer != VK_NULL_HANDLE) {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
    bufferSize = 0;
}

/**
 * @brief Uploads data to the buffer.
 * @param srcData Source pointer.
 * @param dataSize Transfer size.
 */
void VulkanBuffer::uploadData(const void* srcData, VkDeviceSize dataSize) {
    if (dataSize == 0) return;

    if (dataSize > bufferSize)
        throw std::runtime_error("uploadData() size exceeds buffer capacity");

    if (mappedData != nullptr) {
        std::memcpy(mappedData, srcData, static_cast<size_t>(dataSize));
    } else {
        void* mapped = nullptr;
        vkMapMemory(device, memory, 0, dataSize, 0, &mapped);
        std::memcpy(mapped, srcData, static_cast<size_t>(dataSize));
        vkUnmapMemory(device, memory);
    }
}

/**
 * @brief Maps buffer memory and uploads partial data.
 * @param srcData Source pointer.
 * @param offset Byte offset into the destination buffer.
 * @param dataSize Size of data to transfer.
 */
void VulkanBuffer::uploadSubData(const void* srcData, VkDeviceSize offset, VkDeviceSize dataSize) {
    if (dataSize == 0) return;

    if (offset + dataSize > bufferSize)
        throw std::runtime_error("uploadSubData() range exceeds buffer capacity");

    if (mappedData != nullptr) {
        std::memcpy(static_cast<char*>(mappedData) + offset, srcData, static_cast<size_t>(dataSize));
    } else {
        void* mapped = nullptr;
        // Map starting at 0 up to (offset + dataSize) to strictly satisfy
        // VkPhysicalDeviceLimits::minMemoryMapAlignment requirements across all GPU architectures.
        if (vkMapMemory(device, memory, 0, offset + dataSize, 0, &mapped) != VK_SUCCESS)
            throw std::runtime_error("uploadSubData() failed to map memory");

        std::memcpy(static_cast<char*>(mapped) + offset, srcData, static_cast<size_t>(dataSize));
        vkUnmapMemory(device, memory);
    }
}

/**
 * @brief Selects the correct memory type on physical device.
 * @param typeFilter Bitmask filtering memory types.
 * @param properties Desired memory properties.
 * @return Memory type index.
 */
uint32_t VulkanBuffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}
