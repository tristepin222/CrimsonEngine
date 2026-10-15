#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vulkan/vulkan.h>


class VulkanBuffer;

/**
 * @struct Joint
 * @brief Describes a single bone node in the skeleton joint hierarchy.
 */
struct Joint {
    /** @brief Joint name identifier. */
    std::string name;
    /** @brief Parent joint array index (-1 if root). */
    int parentIndex = -1;
    /** @brief Inverse bind transform matrix mapping geometry to bone local space. */
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f);
    /** @brief Local animation pose transform (Translation * Rotation * Scale). */
    glm::mat4 localTransform = glm::mat4(1.0f);
    
    // Bind pose local TRS components
    glm::vec3 bindTranslation = glm::vec3(0.0f);
    glm::quat bindRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 bindScale = glm::vec3(1.0f);
};

/**
 * @struct SkeletonComponent
 * @brief Component managing the list of joints and generating the offset matrices palette.
 */
// [ReflectClass("Animation/Skeleton")]
struct SkeletonComponent {

    /** @brief List of joints forming the skeleton hierarchy. */
    std::vector<Joint> joints;
    /** @brief Calculated final joint offsets list uploaded to VRAM descriptors. */
    std::vector<glm::mat4> jointMatrices;
    
    std::shared_ptr<VulkanBuffer> gpuBuffer;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};


