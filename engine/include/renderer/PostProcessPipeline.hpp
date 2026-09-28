#pragma once

#include "core/EngineAPI.hpp"
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Engine {

    /**
     * @struct PostProcessPushConstants
     * @brief Standard push constant layout supplied to all fullscreen post-process fragment shaders.
     */
    struct alignas(16) PostProcessPushConstants {
        glm::vec4 resolution{0.0f}; // x: width, y: height, z: 1.0/width, w: 1.0/height
        glm::vec4 params0{0.0f};    // custom user param (e.g. intensity, pixel size, factor)
        glm::vec4 params1{0.0f};    // custom user param
        glm::vec4 params2{0.0f};    // custom user param
        glm::vec4 params3{0.0f};    // custom user param
    };

    /**
     * @struct PostProcessPass
     * @brief Represents a single blit step in the post-processing stack.
     */
    struct PostProcessPass {
        std::string name;
        std::string shaderPath; // GLSL source (.frag) or compiled SPIR-V (.spv)
        bool enabled = true;
        PostProcessPushConstants pushConstants{};

        // Internal Vulkan pipeline objects managed by VulkanRenderer
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    /**
     * @struct PostProcessSettings
     * @brief Global configuration for rendering resolution scaling and final presentation filtering.
     */
    struct PostProcessSettings {
        bool enabled = true;
        float renderScale = 1.0f;             // 1.0 = native, 0.5 = half, 0.25 = quarter
        bool useFixedResolution = false;      // If true, uses targetWidth and targetHeight
        int targetWidth = 480;                // Fixed width (e.g. 480 for 480x270 retro style)
        int targetHeight = 270;               // Fixed height (e.g. 270 for 16:9 pixelated retro)
        bool nearestNeighborUpscale = false;  // Point/Nearest sampling for sharp pixelated 3D aesthetic
    };

} // namespace Engine
