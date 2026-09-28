#include "AtmosphereRenderFeature.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <array>
#include <filesystem>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

AtmosphereRenderFeature::~AtmosphereRenderFeature() {
    cleanup();
}

void AtmosphereRenderFeature::init(VulkanRenderer& renderer) {
    if (m_initialized) return;

    m_renderer = &renderer;
    m_device = renderer.getDevice();
    m_physicalDevice = renderer.getPhysicalDevice();

    createDescriptorPool();
    createUBO();
    createLUTTextures();
    loadWeatherTextures();
    createParticleBuffer();
    generateCloudNoise();
    createDescriptorSetLayouts();
    allocateAndWriteDescriptorSets();
    createPipelines();
    createWeatherPipelines();

    m_initialized = true;
}

void AtmosphereRenderFeature::cleanup() {
    if (!m_initialized) return;

    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        // Weather Pipelines
        if (m_lightningPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_lightningPipeline, nullptr);
            m_lightningPipeline = VK_NULL_HANDLE;
        }
        if (m_lightningPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_lightningPipelineLayout, nullptr);
            m_lightningPipelineLayout = VK_NULL_HANDLE;
        }
        if (m_lightningLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_lightningLayout, nullptr);
            m_lightningLayout = VK_NULL_HANDLE;
        }

        if (m_precipPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_precipPipeline, nullptr);
            m_precipPipeline = VK_NULL_HANDLE;
        }
        if (m_precipPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_precipPipelineLayout, nullptr);
            m_precipPipelineLayout = VK_NULL_HANDLE;
        }
        if (m_precipLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_precipLayout, nullptr);
            m_precipLayout = VK_NULL_HANDLE;
        }

        if (m_particleBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_particleBuffer, nullptr);
            m_particleBuffer = VK_NULL_HANDLE;
        }
        if (m_particleMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_particleMemory, nullptr);
            m_particleMemory = VK_NULL_HANDLE;
        }

        destroyTexture(m_weatherFxTex);
        destroyTexture(m_lightningBoltTex);

        // Graphics Pipeline
        if (m_aerialCompPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_aerialCompPipeline, nullptr);
            m_aerialCompPipeline = VK_NULL_HANDLE;
        }
        if (m_aerialCompPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_aerialCompPipelineLayout, nullptr);
            m_aerialCompPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_compositionPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_compositionPipeline, nullptr);
            m_compositionPipeline = VK_NULL_HANDLE;
        }
        if (m_compositionPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_compositionPipelineLayout, nullptr);
            m_compositionPipelineLayout = VK_NULL_HANDLE;
        }

        // Compute Pipelines
        if (m_aerialPerspectivePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_aerialPerspectivePipeline, nullptr);
            m_aerialPerspectivePipeline = VK_NULL_HANDLE;
        }
        if (m_aerialPerspectivePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_aerialPerspectivePipelineLayout, nullptr);
            m_aerialPerspectivePipelineLayout = VK_NULL_HANDLE;
        }

        if (m_skyViewPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_skyViewPipeline, nullptr);
            m_skyViewPipeline = VK_NULL_HANDLE;
        }
        if (m_skyViewPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_skyViewPipelineLayout, nullptr);
            m_skyViewPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_multiScatterPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_multiScatterPipeline, nullptr);
            m_multiScatterPipeline = VK_NULL_HANDLE;
        }
        if (m_multiScatterPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_multiScatterPipelineLayout, nullptr);
            m_multiScatterPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_transmittancePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_transmittancePipeline, nullptr);
            m_transmittancePipeline = VK_NULL_HANDLE;
        }
        if (m_transmittancePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_transmittancePipelineLayout, nullptr);
            m_transmittancePipelineLayout = VK_NULL_HANDLE;
        }

        // Descriptor Set Layouts
        if (m_aerialCompLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_aerialCompLayout, nullptr);
            m_aerialCompLayout = VK_NULL_HANDLE;
        }
        if (m_aerialPerspectiveLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_aerialPerspectiveLayout, nullptr);
            m_aerialPerspectiveLayout = VK_NULL_HANDLE;
        }
        if (m_compositionLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_compositionLayout, nullptr);
            m_compositionLayout = VK_NULL_HANDLE;
        }
        if (m_skyViewLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_skyViewLayout, nullptr);
            m_skyViewLayout = VK_NULL_HANDLE;
        }
        if (m_multiScatterLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_multiScatterLayout, nullptr);
            m_multiScatterLayout = VK_NULL_HANDLE;
        }
        if (m_transmittanceLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_transmittanceLayout, nullptr);
            m_transmittanceLayout = VK_NULL_HANDLE;
        }

        // Descriptor Pool
        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }

        // LUT Textures
        destroyTexture(m_transmittanceLUT);
        destroyTexture(m_multiScatterLUT);
        destroyTexture(m_skyViewLUT);
        destroyTexture3D(m_cameraVolumeLUT);
        destroyTexture(m_moonAlbedoTex);
        destroyTexture3D(m_cloudNoiseTex);

        // Depth sampler
        if (m_depthSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, m_depthSampler, nullptr);
            m_depthSampler = VK_NULL_HANDLE;
        }
        m_lastDepthView = VK_NULL_HANDLE;

        // UBO
        if (m_paramBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_paramBuffer, nullptr);
            m_paramBuffer = VK_NULL_HANDLE;
        }
        if (m_paramMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_paramMemory, nullptr);
            m_paramMemory = VK_NULL_HANDLE;
        }

        // Cloud UBO
        if (m_cloudBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_cloudBuffer, nullptr);
            m_cloudBuffer = VK_NULL_HANDLE;
        }
        if (m_cloudMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_cloudMemory, nullptr);
            m_cloudMemory = VK_NULL_HANDLE;
        }

        // Shadow UBO
        if (m_shadowBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_shadowBuffer, nullptr);
            m_shadowBuffer = VK_NULL_HANDLE;
        }
        if (m_shadowMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_shadowMemory, nullptr);
            m_shadowMemory = VK_NULL_HANDLE;
        }
        m_lastShadowDepthView = VK_NULL_HANDLE;
    }

    m_initialized = false;
}

uint32_t AtmosphereRenderFeature::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("AtmosphereRenderFeature: Failed to find suitable memory type!");
}

void AtmosphereRenderFeature::createUBO() {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(AtmosphereParametersGPU);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_paramBuffer) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create parameter buffer!");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, m_paramBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_paramMemory) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to allocate parameter buffer memory!");
    }

    vkBindBufferMemory(m_device, m_paramBuffer, m_paramMemory, 0);

    // Create Shadow UBO for volumetric God Rays
    VkBufferCreateInfo shadowBufferInfo{};
    shadowBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    shadowBufferInfo.size = sizeof(VolumetricShadowUBO);
    shadowBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    shadowBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &shadowBufferInfo, nullptr, &m_shadowBuffer) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create shadow parameter buffer!");
    }

    VkMemoryRequirements shadowMemReq;
    vkGetBufferMemoryRequirements(m_device, m_shadowBuffer, &shadowMemReq);

    VkMemoryAllocateInfo shadowAllocInfo{};
    shadowAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    shadowAllocInfo.allocationSize = shadowMemReq.size;
    shadowAllocInfo.memoryTypeIndex = findMemoryType(shadowMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_device, &shadowAllocInfo, nullptr, &m_shadowMemory) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to allocate shadow buffer memory!");
    }

    vkBindBufferMemory(m_device, m_shadowBuffer, m_shadowMemory, 0);

    // Create Cloud UBO for volumetric raymarched clouds
    VkBufferCreateInfo cloudBufferInfo{};
    cloudBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    cloudBufferInfo.size = sizeof(CloudParametersGPU);
    cloudBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    cloudBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &cloudBufferInfo, nullptr, &m_cloudBuffer) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create cloud parameter buffer!");
    }

    VkMemoryRequirements cloudMemReq;
    vkGetBufferMemoryRequirements(m_device, m_cloudBuffer, &cloudMemReq);

    VkMemoryAllocateInfo cloudAllocInfo{};
    cloudAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    cloudAllocInfo.allocationSize = cloudMemReq.size;
    cloudAllocInfo.memoryTypeIndex = findMemoryType(cloudMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_device, &cloudAllocInfo, nullptr, &m_cloudMemory) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to allocate cloud buffer memory!");
    }

    vkBindBufferMemory(m_device, m_cloudBuffer, m_cloudMemory, 0);
}

void AtmosphereRenderFeature::updateCloudUBO(const VolumetricCloudComponent* cloud, float totalTime, const WeatherComponent* weather, float lightningFlash) {
    CloudParametersGPU ubo{};
    if (cloud && cloud->enabled && cloud->coverage > 0.0001f) {
        ubo.cloudAltitudes = glm::vec4(
            cloud->bottomAltitude,
            cloud->topAltitude,
            std::clamp(cloud->coverage, 0.0f, 1.0f),
            std::max(0.0001f, cloud->density)
        );
        ubo.cloudModeling = glm::vec4(
            cloud->cloudScale,
            cloud->detailScale,
            cloud->detailErosion,
            1.0f // enabled
        );
        glm::vec2 wDir = (glm::length(cloud->windDirection) > 0.001f) ? glm::normalize(cloud->windDirection) : glm::vec2(1.0f, 0.0f);
        ubo.windParams = glm::vec4(
            wDir.x,
            wDir.y,
            cloud->windSpeed,
            totalTime
        );
        ubo.lightingParams = glm::vec4(
            cloud->silverLining,
            cloud->powderEffect,
            static_cast<float>(std::clamp(cloud->marchSteps, 8, 96)),
            (cloud->castShadows ? std::clamp(cloud->shadowIntensity, 0.0f, 1.0f) : 0.0f)
        );
        ubo.cloudColor = glm::vec4(cloud->cloudColor, lightningFlash);
        ubo.ambientColor = glm::vec4(cloud->ambientColor, 1.0f);
    } else {
        ubo.cloudModeling.w = 0.0f; // disabled (early skip in shader)
        ubo.windParams = glm::vec4(1.0f, 0.0f, 0.0f, totalTime);
        ubo.cloudColor = glm::vec4(1.0f, 1.0f, 1.0f, lightningFlash);
    }

    if (weather) {
        ubo.weatherParams = glm::vec4(
            static_cast<float>(weather->type),
            std::clamp(weather->precipitationIntensity, 0.0f, 1.0f),
            std::clamp(weather->wetness, 0.0f, 1.0f),
            lightningFlash
        );
    } else {
        ubo.weatherParams = glm::vec4(0.0f);
    }

    void* data = nullptr;
    vkMapMemory(m_device, m_cloudMemory, 0, sizeof(CloudParametersGPU), 0, &data);
    memcpy(data, &ubo, sizeof(CloudParametersGPU));
    vkUnmapMemory(m_device, m_cloudMemory);
}

void AtmosphereRenderFeature::updateShadowUBO(const VulkanRenderer::CascadeShadowData* shadowData) {
    VolumetricShadowUBO ubo{};
    if (shadowData && shadowData->enabled) {
        for (int i = 0; i < 4; ++i) {
            ubo.cascadeLightSpaceMatrices[i] = shadowData->cascadeLightSpaceMatrices[i];
        }
        ubo.cascadeSplits = shadowData->cascadeSplits;
        ubo.shadowParams = shadowData->shadowParams;
    } else {
        ubo.shadowParams = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // Shadows disabled for volumetric fog
    }

    void* data = nullptr;
    vkMapMemory(m_device, m_shadowMemory, 0, sizeof(VolumetricShadowUBO), 0, &data);
    memcpy(data, &ubo, sizeof(VolumetricShadowUBO));
    vkUnmapMemory(m_device, m_shadowMemory);
}

void AtmosphereRenderFeature::updateUBO(const AtmosphereComponent& comp) {
    AtmosphereParametersGPU params{};

    params.solarIrradiance = glm::vec4(comp.solarIrradiance, comp.sunAngularRadius);
    params.rayleighScattering = glm::vec4(comp.rayleighScattering, comp.bottomRadius);
    params.rayleighAbsorption = glm::vec4(comp.rayleighAbsorption, comp.topRadius);
    params.mieScattering = glm::vec4(comp.mieScattering, comp.mieAnisotropy);
    params.mieAbsorption = glm::vec4(comp.mieAbsorption, 0.0f);
    params.absorptionExtinction = glm::vec4(comp.absorptionExtinction, 0.0f);
    params.groundAlbedo = glm::vec4(comp.groundAlbedo, 0.0f);

    for (int i = 0; i < 2; ++i) {
        params.rayleighDensity[i].width = comp.rayleighDensity.layers[i].width;
        params.rayleighDensity[i].expTerm = comp.rayleighDensity.layers[i].expTerm;
        params.rayleighDensity[i].expScale = comp.rayleighDensity.layers[i].expScale;
        params.rayleighDensity[i].linearTerm = comp.rayleighDensity.layers[i].linearTerm;
        params.rayleighDensity[i].constantTerm = comp.rayleighDensity.layers[i].constantTerm;

        params.mieDensity[i].width = comp.mieDensity.layers[i].width;
        params.mieDensity[i].expTerm = comp.mieDensity.layers[i].expTerm;
        params.mieDensity[i].expScale = comp.mieDensity.layers[i].expScale;
        params.mieDensity[i].linearTerm = comp.mieDensity.layers[i].linearTerm;
        params.mieDensity[i].constantTerm = comp.mieDensity.layers[i].constantTerm;

        params.absorptionDensity[i].width = comp.absorptionDensity.layers[i].width;
        params.absorptionDensity[i].expTerm = comp.absorptionDensity.layers[i].expTerm;
        params.absorptionDensity[i].expScale = comp.absorptionDensity.layers[i].expScale;
        params.absorptionDensity[i].linearTerm = comp.absorptionDensity.layers[i].linearTerm;
        params.absorptionDensity[i].constantTerm = comp.absorptionDensity.layers[i].constantTerm;
    }

    void* data = nullptr;
    vkMapMemory(m_device, m_paramMemory, 0, sizeof(AtmosphereParametersGPU), 0, &data);
    memcpy(data, &params, sizeof(AtmosphereParametersGPU));
    vkUnmapMemory(m_device, m_paramMemory);
}

void AtmosphereRenderFeature::createTexture(LUTTexture& tex, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage) {
    tex.width = width;
    tex.height = height;
    tex.format = format;
    tex.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create LUT image!");
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, tex.image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to allocate LUT image memory!");
    }

    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create LUT image view!");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create LUT sampler!");
    }
}

void AtmosphereRenderFeature::destroyTexture(LUTTexture& tex) {
    if (tex.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        tex.sampler = VK_NULL_HANDLE;
    }
    if (tex.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, tex.view, nullptr);
        tex.view = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, tex.image, nullptr);
        tex.image = VK_NULL_HANDLE;
    }
    if (tex.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, tex.memory, nullptr);
        tex.memory = VK_NULL_HANDLE;
    }
}

void AtmosphereRenderFeature::loadMoonTexture() {
    std::vector<std::string> candidatePaths = {
        "plugins/skymo/assets/textures/moon_albedo.jpg",
        "sandbox_game/plugins/skymo/assets/textures/moon_albedo.jpg",
        "sandbox_game/build/plugins/skymo/assets/textures/moon_albedo.jpg",
        "sdk/plugins/skymo/assets/textures/moon_albedo.jpg",
        "sdk/plugins/assets/textures/moon_albedo.jpg",
        "../plugins/skymo/assets/textures/moon_albedo.jpg",
        "../sdk/plugins/skymo/assets/textures/moon_albedo.jpg",
        "../sdk/plugins/assets/textures/moon_albedo.jpg",
        "../sandbox_game/plugins/skymo/assets/textures/moon_albedo.jpg",
        "plugins/assets/textures/moon_albedo.jpg",
        "../plugins/assets/textures/moon_albedo.jpg"
    };

    if (m_renderer) {
        std::string exeDir = m_renderer->getExeDir();
        candidatePaths.push_back(exeDir + "/plugins/skymo/assets/textures/moon_albedo.jpg");
        candidatePaths.push_back(exeDir + "/plugins/assets/textures/moon_albedo.jpg");
        candidatePaths.push_back(exeDir + "/../plugins/skymo/assets/textures/moon_albedo.jpg");
    }

    std::string path;
    for (const auto& candidate : candidatePaths) {
        if (std::filesystem::exists(candidate)) {
            path = candidate;
            break;
        }
    }

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = nullptr;
    if (std::filesystem::exists(path)) {
        stbi_set_flip_vertically_on_load(false);
        pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    }

    std::vector<uint8_t> fallbackData;
    if (!pixels) {
        width = 512;
        height = 256;
        fallbackData.resize(width * height * 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 4;
                fallbackData[idx + 0] = 180;
                fallbackData[idx + 1] = 175;
                fallbackData[idx + 2] = 170;
                fallbackData[idx + 3] = 255;
            }
        }
        pixels = fallbackData.data();
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;
    createTexture(m_moonAlbedoTex, width, height, VK_FORMAT_R8G8B8A8_UNORM, 
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_device, &bufferInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0);

    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped);
    memcpy(mapped, pixels, imageSize);
    vkUnmapMemory(m_device, stagingMemory);

    if (pixels != fallbackData.data()) {
        stbi_image_free(pixels);
    }

    VkCommandBuffer cmd = m_renderer->beginSingleUseCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_moonAlbedoTex.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_moonAlbedoTex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_moonAlbedoTex.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    m_renderer->endSingleUseCommands(cmd);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);
}

void AtmosphereRenderFeature::loadWeatherTextures() {
    auto loadTex = [this](LUTTexture& targetTex, const std::string& filename, uint8_t fbR, uint8_t fbG, uint8_t fbB, uint8_t fbA) {
        std::vector<std::string> candidatePaths = {
            "plugins/skymo/assets/textures/" + filename,
            "sandbox_game/plugins/skymo/assets/textures/" + filename,
            "sandbox_game/build/plugins/skymo/assets/textures/" + filename,
            "sdk/plugins/skymo/assets/textures/" + filename,
            "sdk/plugins/assets/textures/" + filename,
            "../plugins/skymo/assets/textures/" + filename,
            "../sdk/plugins/skymo/assets/textures/" + filename,
            "../sdk/plugins/assets/textures/" + filename,
            "../sandbox_game/plugins/skymo/assets/textures/" + filename,
            "plugins/assets/textures/" + filename,
            "../plugins/assets/textures/" + filename
        };

        if (m_renderer) {
            std::string exeDir = m_renderer->getExeDir();
            candidatePaths.push_back(exeDir + "/plugins/skymo/assets/textures/" + filename);
            candidatePaths.push_back(exeDir + "/plugins/assets/textures/" + filename);
            candidatePaths.push_back(exeDir + "/../plugins/skymo/assets/textures/" + filename);
        }

        std::string path;
        for (const auto& candidate : candidatePaths) {
            if (std::filesystem::exists(candidate)) {
                path = candidate;
                break;
            }
        }

        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = nullptr;
        if (!path.empty()) {
            stbi_set_flip_vertically_on_load(false);
            pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        }

        std::vector<uint8_t> fallbackData;
        if (!pixels) {
            width = 64;
            height = 64;
            fallbackData.resize(width * height * 4);
            for (size_t i = 0; i < fallbackData.size(); i += 4) {
                fallbackData[i + 0] = fbR;
                fallbackData[i + 1] = fbG;
                fallbackData[i + 2] = fbB;
                fallbackData[i + 3] = fbA;
            }
            pixels = fallbackData.data();
        }

        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;
        createTexture(targetTex, width, height, VK_FORMAT_R8G8B8A8_UNORM, 
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(m_device, &bufferInfo, nullptr, &stagingBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingMemory);
        vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0);

        void* mapped = nullptr;
        vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped);
        memcpy(mapped, pixels, imageSize);
        vkUnmapMemory(m_device, stagingMemory);

        if (pixels != fallbackData.data()) {
            stbi_image_free(pixels);
        }

        VkCommandBuffer cmd = m_renderer->beginSingleUseCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = targetTex.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

        vkCmdCopyBufferToImage(cmd, stagingBuffer, targetTex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        targetTex.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        m_renderer->endSingleUseCommands(cmd);

        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    };

    loadTex(m_weatherFxTex, "weather_fx.png", 255, 255, 255, 255);
    loadTex(m_lightningBoltTex, "lightning_bolt.png", 255, 255, 255, 255);
}

void AtmosphereRenderFeature::createParticleBuffer() {
    std::vector<WeatherParticleVertex> vertices;
    vertices.reserve(PRECIP_PARTICLE_COUNT * 6);

    srand(1337);

    for (uint32_t i = 0; i < PRECIP_PARTICLE_COUNT; ++i) {
        float rx = static_cast<float>(rand() % 10000) / 10000.0f;
        float ry = static_cast<float>(rand() % 10000) / 10000.0f;
        float rz = static_cast<float>(rand() % 10000) / 10000.0f;
        glm::vec3 seedPos(rx, ry, rz);

        float size = 0.75f + (static_cast<float>(rand() % 1000) / 1000.0f) * 0.75f;
        float phase = static_cast<float>(rand() % 10000) / 10000.0f;
        glm::vec2 sizePhase(size, phase);

        vertices.push_back({ seedPos, glm::vec2(-0.5f, -0.5f), sizePhase });
        vertices.push_back({ seedPos, glm::vec2( 0.5f, -0.5f), sizePhase });
        vertices.push_back({ seedPos, glm::vec2( 0.5f,  0.5f), sizePhase });
        vertices.push_back({ seedPos, glm::vec2( 0.5f,  0.5f), sizePhase });
        vertices.push_back({ seedPos, glm::vec2(-0.5f,  0.5f), sizePhase });
        vertices.push_back({ seedPos, glm::vec2(-0.5f, -0.5f), sizePhase });
    }

    VkDeviceSize bufferSize = vertices.size() * sizeof(WeatherParticleVertex);

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = bufferSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_device, &stagingInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0);

    void* data = nullptr;
    vkMapMemory(m_device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), bufferSize);
    vkUnmapMemory(m_device, stagingMemory);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_particleBuffer);

    vkGetBufferMemoryRequirements(m_device, m_particleBuffer, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_device, &allocInfo, nullptr, &m_particleMemory);
    vkBindBufferMemory(m_device, m_particleBuffer, m_particleMemory, 0);

    VkCommandBuffer cmd = m_renderer->beginSingleUseCommands();
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, m_particleBuffer, 1, &copyRegion);
    m_renderer->endSingleUseCommands(cmd);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);
}

void AtmosphereRenderFeature::createWeatherPipelines() {
    // 1. Descriptor Set Layouts (Binding 0: Combined Image Sampler)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_precipLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create weather precipitation descriptor set layout!");
        }
        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_lightningLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create weather lightning descriptor set layout!");
        }
    }

    // 2. Allocate and Write Descriptor Sets
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_precipLayout;
        if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_precipSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate precipitation descriptor set!");
        }

        allocInfo.pSetLayouts = &m_lightningLayout;
        if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_lightningSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate lightning descriptor set!");
        }

        VkDescriptorImageInfo precipImageInfo{};
        precipImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        precipImageInfo.imageView = m_weatherFxTex.view;
        precipImageInfo.sampler = m_weatherFxTex.sampler;

        VkWriteDescriptorSet precipWrite{};
        precipWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        precipWrite.dstSet = m_precipSet;
        precipWrite.dstBinding = 0;
        precipWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        precipWrite.descriptorCount = 1;
        precipWrite.pImageInfo = &precipImageInfo;
        vkUpdateDescriptorSets(m_device, 1, &precipWrite, 0, nullptr);

        VkDescriptorImageInfo lightningImageInfo{};
        lightningImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lightningImageInfo.imageView = m_lightningBoltTex.view;
        lightningImageInfo.sampler = m_lightningBoltTex.sampler;

        VkWriteDescriptorSet lightningWrite{};
        lightningWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lightningWrite.dstSet = m_lightningSet;
        lightningWrite.dstBinding = 0;
        lightningWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        lightningWrite.descriptorCount = 1;
        lightningWrite.pImageInfo = &lightningImageInfo;
        vkUpdateDescriptorSets(m_device, 1, &lightningWrite, 0, nullptr);
    }

    // Common Pipeline States
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    VkRect2D scissor{};
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // 3. Precipitation Pipeline (Alpha Blended, Depth Tested)
    {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(WeatherPrecipPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_precipLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_precipPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create weather precipitation pipeline layout!");
        }

        VkShaderModule vertModule = loadShaderModule("weather_particles.vert.spv");
        VkShaderModule fragModule = loadShaderModule("weather_particles.frag.spv");

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

        VkVertexInputBindingDescription bindDesc{};
        bindDesc.binding = 0;
        bindDesc.stride = sizeof(WeatherParticleVertex);
        bindDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrDescs{};
        attrDescs[0].location = 0;
        attrDescs[0].binding = 0;
        attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrDescs[0].offset = offsetof(WeatherParticleVertex, seedPos);

        attrDescs[1].location = 1;
        attrDescs[1].binding = 0;
        attrDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrDescs[1].offset = offsetof(WeatherParticleVertex, corner);

        attrDescs[2].location = 2;
        attrDescs[2].binding = 0;
        attrDescs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrDescs[2].offset = offsetof(WeatherParticleVertex, sizePhase);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
        vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_precipPipelineLayout;
        pipelineInfo.renderPass = m_renderer->getRenderPass();
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_precipPipeline) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create weather precipitation pipeline!");
        }

        vkDestroyShaderModule(m_device, vertModule, nullptr);
        vkDestroyShaderModule(m_device, fragModule, nullptr);
    }

    // 4. 3D Branching Lightning Pipeline (Additive Blended, Depth Tested)
    {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(WeatherLightningPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_lightningLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_lightningPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create weather lightning pipeline layout!");
        }

        VkShaderModule vertModule = loadShaderModule("weather_lightning.vert.spv");
        VkShaderModule fragModule = loadShaderModule("weather_lightning.frag.spv");

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_lightningPipelineLayout;
        pipelineInfo.renderPass = m_renderer->getRenderPass();
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_lightningPipeline) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create weather lightning pipeline!");
        }

        vkDestroyShaderModule(m_device, vertModule, nullptr);
        vkDestroyShaderModule(m_device, fragModule, nullptr);
    }
}

void AtmosphereRenderFeature::createTexture3D(LUTTexture3D& tex, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, VkImageUsageFlags usage, bool repeat) {
    tex.width = width;
    tex.height = height;
    tex.depth = depth;
    tex.format = format;
    tex.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = depth;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create 3D LUT image!");
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, tex.image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to allocate 3D LUT image memory!");
    }

    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create 3D LUT image view!");
    }

    VkSamplerAddressMode addrMode = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = addrMode;
    samplerInfo.addressModeV = addrMode;
    samplerInfo.addressModeW = addrMode;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create 3D LUT sampler!");
    }
}

void AtmosphereRenderFeature::destroyTexture3D(LUTTexture3D& tex) {
    if (tex.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        tex.sampler = VK_NULL_HANDLE;
    }
    if (tex.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, tex.view, nullptr);
        tex.view = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, tex.image, nullptr);
        tex.image = VK_NULL_HANDLE;
    }
    if (tex.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, tex.memory, nullptr);
        tex.memory = VK_NULL_HANDLE;
    }
}

void AtmosphereRenderFeature::generateCloudNoise() {
    createTexture3D(m_cloudNoiseTex, 64, 64, 64, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true);

    VkShaderModule compModule = loadShaderModule("cloud_noise.comp.spv");

    // 1. Temporary Descriptor Set Layout
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VkDescriptorSetLayout compLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &compLayout) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create cloud noise descriptor layout!");
    }

    // 2. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compLayout;

    VkPipelineLayout compPipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &compPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create cloud noise pipeline layout!");
    }

    // 3. Compute Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = compModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = compPipelineLayout;

    VkPipeline compPipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compPipeline) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create cloud noise compute pipeline!");
    }

    // 4. Temporary Descriptor Set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &compLayout;

    VkDescriptorSet compSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &compSet) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to allocate cloud noise descriptor set!");
    }

    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageView = m_cloudNoiseTex.view;
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = compSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &storageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    // 5. Execute Command Buffer
    VkCommandBuffer cmd = m_renderer->beginSingleUseCommands();

    transitionImageLayout3D(cmd, m_cloudNoiseTex, VK_IMAGE_LAYOUT_GENERAL,
                            0, VK_ACCESS_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compPipelineLayout, 0, 1, &compSet, 0, nullptr);
    vkCmdDispatch(cmd, 16, 16, 16); // 64 / 4 = 16

    transitionImageLayout3D(cmd, m_cloudNoiseTex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    m_renderer->endSingleUseCommands(cmd);

    // 6. Cleanup temporary resources
    vkDestroyPipeline(m_device, compPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, compPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, compLayout, nullptr);
    vkDestroyShaderModule(m_device, compModule, nullptr);
}

void AtmosphereRenderFeature::createLUTTextures() {
    VkImageUsageFlags computeLUTUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    createTexture(m_transmittanceLUT, 256, 64, VK_FORMAT_R16G16B16A16_SFLOAT, computeLUTUsage);
    createTexture(m_multiScatterLUT, 32, 32, VK_FORMAT_R16G16B16A16_SFLOAT, computeLUTUsage);
    createTexture(m_skyViewLUT, 192, 108, VK_FORMAT_R16G16B16A16_SFLOAT, computeLUTUsage);
    createTexture3D(m_cameraVolumeLUT, FROXEL_GRID_X, FROXEL_GRID_Y, FROXEL_GRID_Z, VK_FORMAT_R16G16B16A16_SFLOAT, computeLUTUsage);
    loadMoonTexture();
}

void AtmosphereRenderFeature::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 32;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 32;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = 48;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 32;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create descriptor pool!");
    }
}

void AtmosphereRenderFeature::createDescriptorSetLayouts() {
    // 1. Transmittance Pass Layout (Binding 0: UBO, Binding 1: Storage Image)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_transmittanceLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create transmittance descriptor set layout!");
        }
    }

    // 2. Multi-Scatter Pass Layout (Binding 0: UBO, Binding 1: Sampler Transmittance, Binding 2: Storage Image MultiScatter)
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_multiScatterLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create multi-scatter descriptor set layout!");
        }
    }

    // 3. Sky-View Pass Layout (Binding 0: UBO, Binding 1: Transmittance, Binding 2: MultiScatter, Binding 3: Storage SkyView)
    {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_skyViewLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create sky-view descriptor set layout!");
        }
    }

    // 4. Composition Pass Layout
    // Binding 0: Sampler SkyView (2D)
    // Binding 1: Sampler MoonAlbedo (2D)
    // Binding 2: Sampler CloudNoise (3D)
    // Binding 3: Sampler TransmittanceLUT (2D)
    // Binding 4: Cloud UBO
    {
        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_compositionLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create composition descriptor set layout!");
        }
    }

    // 5. Aerial Perspective Pass Layout (Binding 0: UBO, Binding 1: Transmittance, Binding 2: MultiScatter, Binding 3: 3D Storage, Binding 4: ShadowMap, Binding 5: ShadowUBO)
    {
        std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[5].binding = 5;
        bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_aerialPerspectiveLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create aerial perspective descriptor set layout!");
        }
    }

    // 6. Aerial Composition Pass Layout (Binding 0: 3D Sampler, Binding 1: Depth 2D Sampler, Binding 2: SkyView 2D Sampler, Binding 3: CloudNoise 3D Sampler, Binding 4: Cloud UBO)
    {
        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_aerialCompLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create aerial composition descriptor set layout!");
        }
    }
}

void AtmosphereRenderFeature::allocateAndWriteDescriptorSets() {
    // 1. Allocate sets
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;

    allocInfo.pSetLayouts = &m_transmittanceLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_transmittanceSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate transmittance descriptor set!");

    allocInfo.pSetLayouts = &m_multiScatterLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_multiScatterSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate multi-scatter descriptor set!");

    allocInfo.pSetLayouts = &m_skyViewLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_skyViewSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate sky-view descriptor set!");

    allocInfo.pSetLayouts = &m_compositionLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_compositionSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate composition descriptor set!");

    allocInfo.pSetLayouts = &m_aerialPerspectiveLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_aerialPerspectiveSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate aerial perspective descriptor set!");

    allocInfo.pSetLayouts = &m_aerialCompLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_aerialCompSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate aerial composition descriptor set!");

    // Common UBO Descriptor Info
    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = m_paramBuffer;
    uboInfo.offset = 0;
    uboInfo.range = sizeof(AtmosphereParametersGPU);

    // Image infos
    VkDescriptorImageInfo transStorageInfo{};
    transStorageInfo.imageView = m_transmittanceLUT.view;
    transStorageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo transSamplerInfo{};
    transSamplerInfo.sampler = m_transmittanceLUT.sampler;
    transSamplerInfo.imageView = m_transmittanceLUT.view;
    transSamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo multiStorageInfo{};
    multiStorageInfo.imageView = m_multiScatterLUT.view;
    multiStorageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo multiSamplerInfo{};
    multiSamplerInfo.sampler = m_multiScatterLUT.sampler;
    multiSamplerInfo.imageView = m_multiScatterLUT.view;
    multiSamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo skyStorageInfo{};
    skyStorageInfo.imageView = m_skyViewLUT.view;
    skyStorageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo skySamplerInfo{};
    skySamplerInfo.sampler = m_skyViewLUT.sampler;
    skySamplerInfo.imageView = m_skyViewLUT.view;
    skySamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo aerialStorageInfo{};
    aerialStorageInfo.imageView = m_cameraVolumeLUT.view;
    aerialStorageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo aerialSamplerInfo{};
    aerialSamplerInfo.sampler = m_cameraVolumeLUT.sampler;
    aerialSamplerInfo.imageView = m_cameraVolumeLUT.view;
    aerialSamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::vector<VkWriteDescriptorSet> writes;

    // Writes for Transmittance Set
    {
        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = m_transmittanceSet;
        w0.dstBinding = 0;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w0.descriptorCount = 1;
        w0.pBufferInfo = &uboInfo;
        writes.push_back(w0);

        VkWriteDescriptorSet w1{};
        w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = m_transmittanceSet;
        w1.dstBinding = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w1.descriptorCount = 1;
        w1.pImageInfo = &transStorageInfo;
        writes.push_back(w1);
    }

    // Writes for MultiScatter Set
    {
        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = m_multiScatterSet;
        w0.dstBinding = 0;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w0.descriptorCount = 1;
        w0.pBufferInfo = &uboInfo;
        writes.push_back(w0);

        VkWriteDescriptorSet w1{};
        w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = m_multiScatterSet;
        w1.dstBinding = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w1.descriptorCount = 1;
        w1.pImageInfo = &transSamplerInfo;
        writes.push_back(w1);

        VkWriteDescriptorSet w2{};
        w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w2.dstSet = m_multiScatterSet;
        w2.dstBinding = 2;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w2.descriptorCount = 1;
        w2.pImageInfo = &multiStorageInfo;
        writes.push_back(w2);
    }

    // Writes for SkyView Set
    {
        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = m_skyViewSet;
        w0.dstBinding = 0;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w0.descriptorCount = 1;
        w0.pBufferInfo = &uboInfo;
        writes.push_back(w0);

        VkWriteDescriptorSet w1{};
        w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = m_skyViewSet;
        w1.dstBinding = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w1.descriptorCount = 1;
        w1.pImageInfo = &transSamplerInfo;
        writes.push_back(w1);

        VkWriteDescriptorSet w2{};
        w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w2.dstSet = m_skyViewSet;
        w2.dstBinding = 2;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w2.descriptorCount = 1;
        w2.pImageInfo = &multiSamplerInfo;
        writes.push_back(w2);

        VkWriteDescriptorSet w3{};
        w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w3.dstSet = m_skyViewSet;
        w3.dstBinding = 3;
        w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w3.descriptorCount = 1;
        w3.pImageInfo = &skyStorageInfo;
        writes.push_back(w3);
    }

    VkDescriptorImageInfo cloudNoiseSamplerInfo{};
    cloudNoiseSamplerInfo.sampler = m_cloudNoiseTex.sampler;
    cloudNoiseSamplerInfo.imageView = m_cloudNoiseTex.view;
    cloudNoiseSamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo cloudUboInfo{};
    cloudUboInfo.buffer = m_cloudBuffer;
    cloudUboInfo.offset = 0;
    cloudUboInfo.range = sizeof(CloudParametersGPU);

    // Writes for Composition Set
    {
        VkDescriptorImageInfo moonSamplerInfo{};
        moonSamplerInfo.sampler = m_moonAlbedoTex.sampler;
        moonSamplerInfo.imageView = m_moonAlbedoTex.view;
        moonSamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = m_compositionSet;
        w0.dstBinding = 0;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w0.descriptorCount = 1;
        w0.pImageInfo = &skySamplerInfo;
        writes.push_back(w0);

        VkWriteDescriptorSet w1{};
        w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = m_compositionSet;
        w1.dstBinding = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w1.descriptorCount = 1;
        w1.pImageInfo = &moonSamplerInfo;
        writes.push_back(w1);

        VkWriteDescriptorSet w2{};
        w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w2.dstSet = m_compositionSet;
        w2.dstBinding = 2;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w2.descriptorCount = 1;
        w2.pImageInfo = &cloudNoiseSamplerInfo;
        writes.push_back(w2);

        VkWriteDescriptorSet w3{};
        w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w3.dstSet = m_compositionSet;
        w3.dstBinding = 3;
        w3.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w3.descriptorCount = 1;
        w3.pImageInfo = &transSamplerInfo;
        writes.push_back(w3);

        VkWriteDescriptorSet w4{};
        w4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w4.dstSet = m_compositionSet;
        w4.dstBinding = 4;
        w4.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w4.descriptorCount = 1;
        w4.pBufferInfo = &cloudUboInfo;
        writes.push_back(w4);
    }

    // Writes for Aerial Perspective Set
    {
        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = m_aerialPerspectiveSet;
        w0.dstBinding = 0;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w0.descriptorCount = 1;
        w0.pBufferInfo = &uboInfo;
        writes.push_back(w0);

        VkWriteDescriptorSet w1{};
        w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = m_aerialPerspectiveSet;
        w1.dstBinding = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w1.descriptorCount = 1;
        w1.pImageInfo = &transSamplerInfo;
        writes.push_back(w1);

        VkWriteDescriptorSet w2{};
        w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w2.dstSet = m_aerialPerspectiveSet;
        w2.dstBinding = 2;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w2.descriptorCount = 1;
        w2.pImageInfo = &multiSamplerInfo;
        writes.push_back(w2);

        VkWriteDescriptorSet w3{};
        w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w3.dstSet = m_aerialPerspectiveSet;
        w3.dstBinding = 3;
        w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w3.descriptorCount = 1;
        w3.pImageInfo = &aerialStorageInfo;
        writes.push_back(w3);

        m_lastShadowDepthView = m_renderer->getShadowDepthView();
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.sampler = m_renderer->getShadowSampler();
        shadowInfo.imageView = m_lastShadowDepthView;
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo shadowUboInfo{};
        shadowUboInfo.buffer = m_shadowBuffer;
        shadowUboInfo.offset = 0;
        shadowUboInfo.range = sizeof(VolumetricShadowUBO);

        VkWriteDescriptorSet w4{};
        w4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w4.dstSet = m_aerialPerspectiveSet;
        w4.dstBinding = 4;
        w4.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w4.descriptorCount = 1;
        w4.pImageInfo = &shadowInfo;
        writes.push_back(w4);

        VkWriteDescriptorSet w5{};
        w5.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w5.dstSet = m_aerialPerspectiveSet;
        w5.dstBinding = 5;
        w5.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w5.descriptorCount = 1;
        w5.pBufferInfo = &shadowUboInfo;
        writes.push_back(w5);
    }

    if (m_depthSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_depthSampler) != VK_SUCCESS) {
            throw std::runtime_error("AtmosphereRenderFeature: Failed to create depth sampler!");
        }
    }

    m_lastDepthView = m_renderer->getDepthImageView();
    VkDescriptorImageInfo depthInfo{};
    depthInfo.sampler = m_depthSampler;
    depthInfo.imageView = m_lastDepthView;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    // Writes for Aerial Composition Set
    {
        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = m_aerialCompSet;
        w0.dstBinding = 0;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w0.descriptorCount = 1;
        w0.pImageInfo = &aerialSamplerInfo;
        writes.push_back(w0);

        VkWriteDescriptorSet w1{};
        w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = m_aerialCompSet;
        w1.dstBinding = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w1.descriptorCount = 1;
        w1.pImageInfo = &depthInfo;
        writes.push_back(w1);

        VkWriteDescriptorSet w2{};
        w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w2.dstSet = m_aerialCompSet;
        w2.dstBinding = 2;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w2.descriptorCount = 1;
        w2.pImageInfo = &skySamplerInfo;
        writes.push_back(w2);

        VkWriteDescriptorSet w3{};
        w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w3.dstSet = m_aerialCompSet;
        w3.dstBinding = 3;
        w3.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w3.descriptorCount = 1;
        w3.pImageInfo = &cloudNoiseSamplerInfo;
        writes.push_back(w3);

        VkWriteDescriptorSet w4{};
        w4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w4.dstSet = m_aerialCompSet;
        w4.dstBinding = 4;
        w4.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w4.descriptorCount = 1;
        w4.pBufferInfo = &cloudUboInfo;
        writes.push_back(w4);
    }

    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

VkShaderModule AtmosphereRenderFeature::loadShaderModule(const std::string& path) {
    std::string resolved = m_renderer->resolveShaderPath(path);
    std::ifstream file(resolved, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to open shader file: " + resolved);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("AtmosphereRenderFeature: Failed to create shader module for: " + resolved);
    }

    return shaderModule;
}

void AtmosphereRenderFeature::createPipelines() {
    // 1. Transmittance Compute Pipeline
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_transmittanceLayout;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_transmittancePipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create transmittance pipeline layout!");

        VkShaderModule compModule = loadShaderModule("transmittance.comp.spv");

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = compModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = m_transmittancePipelineLayout;

        if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_transmittancePipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create transmittance compute pipeline!");

        vkDestroyShaderModule(m_device, compModule, nullptr);
    }

    // 2. Multi-Scatter Compute Pipeline
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_multiScatterLayout;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_multiScatterPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create multi-scatter pipeline layout!");

        VkShaderModule compModule = loadShaderModule("multiscatter.comp.spv");

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = compModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = m_multiScatterPipelineLayout;

        if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_multiScatterPipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create multi-scatter compute pipeline!");

        vkDestroyShaderModule(m_device, compModule, nullptr);
    }

    // 3. Sky-View Compute Pipeline (with push constant)
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(SkyViewPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_skyViewLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_skyViewPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create sky-view pipeline layout!");

        VkShaderModule compModule = loadShaderModule("skyview.comp.spv");

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = compModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = m_skyViewPipelineLayout;

        if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_skyViewPipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create sky-view compute pipeline!");

        vkDestroyShaderModule(m_device, compModule, nullptr);
    }

    // 4. Sky Composition Graphics Pipeline
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(SkyCompositionPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_compositionLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_compositionPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create composition pipeline layout!");

        VkShaderModule vertModule = loadShaderModule("sky_composition.vert.spv");
        VkShaderModule fragModule = loadShaderModule("sky_composition.frag.spv");

        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragModule;
        shaderStages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        // In Vulkan clip space, background clear depth is 1.0. EQUAL or LESS_OR_EQUAL renders behind all scene geometry
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_compositionPipelineLayout;
        pipelineInfo.renderPass = m_renderer->getRenderPass();
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_compositionPipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create sky composition graphics pipeline!");

        vkDestroyShaderModule(m_device, vertModule, nullptr);
        vkDestroyShaderModule(m_device, fragModule, nullptr);
    }

    // 5. Aerial Perspective Compute Pipeline (with push constant)
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(AerialPerspectivePushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_aerialPerspectiveLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_aerialPerspectivePipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create aerial perspective pipeline layout!");

        VkShaderModule compModule = loadShaderModule("aerial_perspective.comp.spv");

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = compModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = m_aerialPerspectivePipelineLayout;

        if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_aerialPerspectivePipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create aerial perspective compute pipeline!");

        vkDestroyShaderModule(m_device, compModule, nullptr);
    }

    // 6. Aerial Composition Graphics Pipeline
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(AerialCompPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_aerialCompLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_aerialCompPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create aerial composition pipeline layout!");

        VkShaderModule vertModule = loadShaderModule("aerial_composition.vert.spv");
        VkShaderModule fragModule = loadShaderModule("aerial_composition.frag.spv");

        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragModule;
        shaderStages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_aerialCompPipelineLayout;
        pipelineInfo.renderPass = m_renderer->getRenderPass();
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_aerialCompPipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create aerial composition graphics pipeline!");

        vkDestroyShaderModule(m_device, vertModule, nullptr);
        vkDestroyShaderModule(m_device, fragModule, nullptr);
    }
}

void AtmosphereRenderFeature::transitionImageLayout(VkCommandBuffer cmd, LUTTexture& tex,
                                                   VkImageLayout newLayout,
                                                   VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                                   VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    if (tex.currentLayout == newLayout) return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = tex.currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    tex.currentLayout = newLayout;
}

void AtmosphereRenderFeature::transitionImageLayout3D(VkCommandBuffer cmd, LUTTexture3D& tex,
                                                     VkImageLayout newLayout,
                                                     VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                                     VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    if (tex.currentLayout == newLayout) return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = tex.currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    tex.currentLayout = newLayout;
}

void AtmosphereRenderFeature::executeLUTPasses(VkCommandBuffer cmd, const AtmosphereComponent& comp,
                                               const glm::vec3& sunDir, const glm::vec3& sunColor,
                                               float sunIntensity, const glm::vec3& camPos,
                                               const glm::mat4& invViewProj,
                                               const VolumetricFogComponent* fog,
                                               const VulkanRenderer::CascadeShadowData* shadowData) {
    if (!m_initialized) return;

    // Update GPU UBO
    updateUBO(comp);
    updateShadowUBO(shadowData);

    // Refresh shadow descriptor if shadow map view was recreated (e.g. resolution change)
    VkImageView currentShadowView = m_renderer->getShadowDepthView();
    if (currentShadowView != VK_NULL_HANDLE && currentShadowView != m_lastShadowDepthView) {
        m_lastShadowDepthView = currentShadowView;
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.sampler = m_renderer->getShadowSampler();
        shadowInfo.imageView = m_lastShadowDepthView;
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_aerialPerspectiveSet;
        w.dstBinding = 4;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo = &shadowInfo;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }

    // 1. Transmittance LUT Pass (256x64)
    {
        transitionImageLayout(cmd, m_transmittanceLUT, VK_IMAGE_LAYOUT_GENERAL,
                              0, VK_ACCESS_SHADER_WRITE_BIT,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_transmittancePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_transmittancePipelineLayout, 0, 1, &m_transmittanceSet, 0, nullptr);
        vkCmdDispatch(cmd, 256 / 8, 64 / 8, 1);

        transitionImageLayout(cmd, m_transmittanceLUT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    // 2. Multi-Scattering LUT Pass (32x32)
    {
        transitionImageLayout(cmd, m_multiScatterLUT, VK_IMAGE_LAYOUT_GENERAL,
                              0, VK_ACCESS_SHADER_WRITE_BIT,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_multiScatterPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_multiScatterPipelineLayout, 0, 1, &m_multiScatterSet, 0, nullptr);
        vkCmdDispatch(cmd, 32 / 8, 32 / 8, 1);

        transitionImageLayout(cmd, m_multiScatterLUT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    // 3. Sky-View LUT Pass (192x108)
    {
        transitionImageLayout(cmd, m_skyViewLUT, VK_IMAGE_LAYOUT_GENERAL,
                              0, VK_ACCESS_SHADER_WRITE_BIT,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_skyViewPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_skyViewPipelineLayout, 0, 1, &m_skyViewSet, 0, nullptr);

        SkyViewPushConstants pc{};
        pc.cameraPosition = camPos;
        pc.cameraAltitude = std::max(0.0f, camPos.y);
        pc.sunDirection = sunDir;
        pc.sunIntensity = sunIntensity;

        vkCmdPushConstants(cmd, m_skyViewPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SkyViewPushConstants), &pc);
        vkCmdDispatch(cmd, (192 + 7) / 8, (108 + 7) / 8, 1);

        transitionImageLayout(cmd, m_skyViewLUT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    // 4. Aerial Perspective 3D Camera Volume LUT Pass (32x32x32)
    {
        transitionImageLayout3D(cmd, m_cameraVolumeLUT, VK_IMAGE_LAYOUT_GENERAL,
                                0, VK_ACCESS_SHADER_WRITE_BIT,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_aerialPerspectivePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_aerialPerspectivePipelineLayout, 0, 1, &m_aerialPerspectiveSet, 0, nullptr);

        float maxDist = fog ? fog->volumetricFogDistance : 96.0f;
        if (shadowData && shadowData->enabled) {
            float shadowMax = shadowData->cascadeSplits[3];
            if (shadowMax > 1.0f) {
                maxDist = std::min(maxDist, shadowMax);
            }
        }
        float startDist = fog ? std::max(0.1f, fog->startDistance) : 0.2f;
        float densityMul = fog ? fog->densityMultiplier : 1.0f;
        float anisotropy = fog ? fog->anisotropy : comp.mieAnisotropy;
        glm::vec3 tint = fog ? fog->fogColorTint : glm::vec3(1.0f);
        float mode = fog ? static_cast<float>(fog->mode) : 0.0f;
        float affectSky = (fog && fog->affectSky) ? 1.0f : 0.0f;
        float baseHeight = fog ? fog->baseHeight : 0.0f;
        float heightFalloff = fog ? fog->heightFalloff : 0.01f;
        float shadowIntensity = (fog && fog->castShadows) ? fog->shadowIntensity : 0.0f;
        float marchSteps = static_cast<float>(fog ? fog->marchSteps : 16);

        glm::vec3 dominantDir = sunDir;
        float dominantIntensity = sunIntensity;
        if (sunDir.y < -0.02f) {
            glm::vec3 mDir(0.0f, -1.0f, 0.0f);
            if (comp.autoMoonDirection) {
                mDir = -sunDir;
            } else if (glm::length(comp.moonDirection) > 0.001f) {
                mDir = glm::normalize(comp.moonDirection);
            } else {
                mDir = -sunDir;
            }
            if (glm::length(mDir) > 0.001f) mDir = glm::normalize(mDir);
            dominantDir = mDir;
            dominantIntensity = 0.15f * comp.moonIntensity;
        }

        AerialPerspectivePushConstants pc{};
        pc.invViewProj = invViewProj;
        pc.cameraPos = glm::vec4(camPos, maxDist);
        pc.sunDirection = glm::vec4(dominantDir, dominantIntensity);
        pc.fogParams = glm::vec4(densityMul, anisotropy, startDist, mode);
        pc.fogColorTint = glm::vec4(tint, affectSky);
        pc.heightParams = glm::vec4(baseHeight, heightFalloff, shadowIntensity, marchSteps);

        vkCmdPushConstants(cmd, m_aerialPerspectivePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(AerialPerspectivePushConstants), &pc);
        vkCmdDispatch(cmd, (FROXEL_GRID_X + 7) / 8, (FROXEL_GRID_Y + 7) / 8, 1);

        transitionImageLayout3D(cmd, m_cameraVolumeLUT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
}

void AtmosphereRenderFeature::executeCompositionPass(VkCommandBuffer cmd, const glm::mat4& invViewProj,
                                                     const glm::vec3& camPos, const glm::vec3& sunDir,
                                                     const AtmosphereComponent* atmo,
                                                     float totalTime,
                                                     float exposure,
                                                     const VolumetricCloudComponent* cloud,
                                                     const WeatherComponent* weather,
                                                     float lightningFlash) {
    if (!m_initialized) return;

    updateCloudUBO(cloud, totalTime, weather, lightningFlash);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositionPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositionPipelineLayout, 0, 1, &m_compositionSet, 0, nullptr);

    SkyCompositionPushConstants pc{};
    pc.invViewProj = invViewProj;
    pc.cameraPos = glm::vec4(camPos, exposure);
    pc.sunDir = glm::vec4(sunDir, 0.0f);

    glm::vec3 mDir(0.0f, -1.0f, 0.0f);
    float mPhase = -1.0f;
    float starInt = 1.0f;
    float moonInt = 1.5f;
    float moonRadius = 0.025f;
    float twinkleSpeed = 1.0f;

    if (atmo) {
        starInt = atmo->enableStars ? atmo->starIntensity : 0.0f;
        moonInt = atmo->enableMoon ? atmo->moonIntensity : 0.0f;
        moonRadius = atmo->moonAngularRadius;
        mPhase = atmo->moonPhase;
        twinkleSpeed = atmo->starTwinkleSpeed;

        if (atmo->autoMoonDirection) {
            mDir = -sunDir;
            if (glm::length(mDir) > 0.001f) {
                mDir = glm::normalize(mDir);
            } else {
                mDir = glm::vec3(0.0f, -1.0f, 0.0f);
            }
        } else {
            if (glm::length(atmo->moonDirection) > 0.001f) {
                mDir = glm::normalize(atmo->moonDirection);
            } else {
                mDir = -sunDir;
            }
        }
    } else {
        mDir = -sunDir;
    }

    pc.moonDir = glm::vec4(mDir, mPhase);
    pc.nightParams = glm::vec4(starInt, moonInt, totalTime * twinkleSpeed, moonRadius);

    vkCmdPushConstants(cmd, m_compositionPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyCompositionPushConstants), &pc);

    // Dynamic viewport and scissor from swapchain extent
    VkExtent2D extent = m_renderer->getSwapchainExtent();
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Draw full-screen triangle (3 vertices)
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void AtmosphereRenderFeature::executeAerialPass(VkCommandBuffer cmd, const glm::mat4& invViewProj, const glm::vec3& camPos, const glm::vec3& sunDir,
                                               const VolumetricFogComponent* fog,
                                               const VolumetricCloudComponent* cloud,
                                               float totalTime,
                                               const WeatherComponent* weather,
                                               float lightningFlash,
                                               const glm::mat4& viewProj,
                                               const glm::vec3& lightningGroundPos,
                                               const glm::vec3& lightningCloudPos,
                                               float boltIntensity,
                                               uint32_t strikeType) {
    if (!m_initialized) return;

    if (cloud || weather || lightningFlash > 0.0001f) {
        updateCloudUBO(cloud, totalTime, weather, lightningFlash);
    }

    VkImageView currentDepthView = m_renderer->getDepthImageView();
    if (currentDepthView != m_lastDepthView && currentDepthView != VK_NULL_HANDLE) {
        m_lastDepthView = currentDepthView;
        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = m_depthSampler;
        depthInfo.imageView = m_lastDepthView;
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_aerialCompSet;
        w.dstBinding = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo = &depthInfo;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_aerialCompPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_aerialCompPipelineLayout, 0, 1, &m_aerialCompSet, 0, nullptr);

    float maxDist = fog ? fog->volumetricFogDistance : 96.0f;
    if (m_renderer && m_renderer->getCascadeShadowData().enabled) {
        float shadowMax = m_renderer->getCascadeShadowData().cascadeSplits[3];
        if (shadowMax > 1.0f) {
            maxDist = std::min(maxDist, shadowMax);
        }
    }
    float startDist = fog ? std::max(0.1f, fog->startDistance) : 0.2f;
    float densityMul = fog ? fog->densityMultiplier : 1.0f;
    glm::vec3 tint = fog ? fog->fogColorTint : glm::vec3(1.0f);
    float mode = fog ? static_cast<float>(fog->mode) : 0.0f;
    float affectSky = (fog && fog->affectSky) ? 1.0f : 0.0f;

    glm::mat4 activeViewProj = (viewProj != glm::mat4(1.0f)) ? viewProj : m_renderer->getActiveCameraViewProj();
    glm::vec4 centerNdc = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    glm::vec4 centerWorldH = invViewProj * centerNdc;
    glm::vec3 camForward = glm::normalize(glm::vec3(centerWorldH) / centerWorldH.w - camPos);

    glm::vec3 dominantDir = sunDir;
    float dominantIntensity = 1.0f;
    if (sunDir.y < -0.02f) {
        dominantDir = -sunDir;
        if (glm::length(dominantDir) > 0.001f) dominantDir = glm::normalize(dominantDir);
        dominantIntensity = 0.35f;
    }

    glm::vec3 normLight = glm::normalize(dominantDir);
    float lightDotCam = glm::dot(normLight, camForward);
    glm::vec4 lightClip = activeViewProj * glm::vec4(camPos + normLight * 1000.0f, 1.0f);
    glm::vec2 lightScreenUV(0.5f);
    float lightInFront = (lightClip.w > 0.001f && lightDotCam > 0.05f) ? 1.0f : 0.0f;
    if (lightClip.w > 0.001f) {
        glm::vec2 ndc = glm::vec2(lightClip.x, lightClip.y) / lightClip.w;
        lightScreenUV = ndc * 0.5f + 0.5f;
    }
    float shaftIntensity = ((fog && fog->castShadows) ? fog->shadowIntensity : 1.0f) * dominantIntensity;

    AerialCompPushConstants pc{};
    pc.invViewProj = invViewProj;
    pc.cameraPos = glm::vec4(camPos, mode);
    pc.sunDir = glm::vec4(dominantDir, dominantIntensity);
    pc.fogParams = glm::vec4(startDist, maxDist, densityMul, affectSky);
    pc.fogColorTint = glm::vec4(tint, 1.0f);
    pc.sunScreen = glm::vec4(lightScreenUV, lightInFront, shaftIntensity);

    vkCmdPushConstants(cmd, m_aerialCompPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(AerialCompPushConstants), &pc);

    VkExtent2D extent = m_renderer->getSwapchainExtent();
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 1. Draw full-screen aerial fog / cloud shadow triangle (3 vertices)
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // 2. 3D Camera-Relative Precipitation Particles (Rain & Snow)
    if (weather && weather->precipitationIntensity > 0.01f &&
        (weather->type == WeatherType::Rain || weather->type == WeatherType::Snow || weather->type == WeatherType::Storm) &&
        m_precipPipeline != VK_NULL_HANDLE && m_particleBuffer != VK_NULL_HANDLE) {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_precipPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_precipPipelineLayout, 0, 1, &m_precipSet, 0, nullptr);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_particleBuffer, &offset);

        WeatherPrecipPushConstants precipPC{};
        precipPC.viewProj = activeViewProj;
        precipPC.cameraPos = glm::vec4(camPos, totalTime);

        // Prefer WeatherComponent wind (user-controllable); fall back to cloud wind if available
        glm::vec2 windDir  = (glm::length(weather->windDirection) > 0.001f)
            ? glm::normalize(weather->windDirection)
            : (cloud ? cloud->windDirection : glm::vec2(1.0f, 0.0f));
        float     windSpd  = (weather->windSpeed > 0.0f) ? weather->windSpeed
            : (cloud ? cloud->windSpeed : 8.0f);
        precipPC.windParams = glm::vec4(windDir, windSpd, 0.0f);

        precipPC.weatherParams = glm::vec4(static_cast<float>(weather->type), weather->precipitationIntensity, weather->wetness, lightningFlash);

        glm::vec3 sunLightCol = (sunDir.y > 0.0f) ? glm::vec3(1.0f, 0.98f, 0.92f) : glm::vec3(0.55f, 0.70f, 0.95f);
        precipPC.lightTint = glm::vec4(sunLightCol, 1.0f);

        vkCmdPushConstants(cmd, m_precipPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WeatherPrecipPushConstants), &precipPC);

        vkCmdDraw(cmd, PRECIP_PARTICLE_COUNT * 6, 1, 0, 0);
    }

    // 3. 3D Branching Lightning Strike Mesh (Additive Cloud-to-Ground Bolt)
    // The vertex shader draws NUM_SEGMENTS=5 quads (3 trunk + 2 branches) via
    // gl_VertexIndex, so we need 5 * 6 = 30 vertices in one draw call.
    float effectiveBoltIntensity = (boltIntensity >= 0.0f) ? boltIntensity : lightningFlash;
    if (effectiveBoltIntensity > 0.001f && weather && weather->type == WeatherType::Storm &&
        m_lightningPipeline != VK_NULL_HANDLE) {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightningPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightningPipelineLayout, 0, 1, &m_lightningSet, 0, nullptr);

        // Derive a per-strike seed from the ground strike position so every bolt
        // looks different. Simple hash: fract(sin(dot(pos.xz, magic)) * large)
        float branchSeed = std::fmod(
            std::abs(std::sin(lightningGroundPos.x * 127.1f + lightningGroundPos.z * 311.7f) * 43758.5453f),
            1.0f);

        // Scale trunk width with distance to camera so distant horizon bolts (up to 15km)
        // maintain majestic angular presence on screen and never turn into subpixel noise.
        float strikeDist = glm::length(lightningGroundPos - camPos);
        float trunkWidth = std::clamp(strikeDist * 0.00065f + 0.35f, 0.40f, 4.5f);

        WeatherLightningPushConstants lightningPC{};
        lightningPC.viewProj  = activeViewProj;
        lightningPC.strikePos = glm::vec4(lightningGroundPos, trunkWidth);  // w = trunk half-width
        lightningPC.cloudPos  = glm::vec4(lightningCloudPos,  effectiveBoltIntensity);
        lightningPC.cameraPos = glm::vec4(camPos, static_cast<float>(strikeType) * 10.0f + branchSeed);

        vkCmdPushConstants(cmd, m_lightningPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WeatherLightningPushConstants), &lightningPC);

        // 64 segments × 6 vertices = 384 vertices
        vkCmdDraw(cmd, 64 * 6, 1, 0, 0);
    }
}