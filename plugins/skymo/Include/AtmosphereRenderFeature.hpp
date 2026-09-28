#pragma once

#include "AtmosphereComponent.hpp"
#include "renderer/VulkanRenderer.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>

struct DensityLayerGPU {
    float width = 0.0f;
    float expTerm = 0.0f;
    float expScale = 0.0f;
    float linearTerm = 0.0f;
    float constantTerm = 0.0f;
    float pad[3] = {0.0f, 0.0f, 0.0f};
};

struct AtmosphereParametersGPU {
    glm::vec4 solarIrradiance;      // xyz: solarIrradiance, w: sunAngularRadius
    glm::vec4 rayleighScattering;   // xyz: rayleighScattering, w: bottomRadius
    glm::vec4 rayleighAbsorption;   // xyz: rayleighAbsorption, w: topRadius
    glm::vec4 mieScattering;        // xyz: mieScattering, w: mieAnisotropy
    glm::vec4 mieAbsorption;        // xyz: mieAbsorption, w: pad
    glm::vec4 absorptionExtinction; // xyz: absorptionExtinction, w: pad
    glm::vec4 groundAlbedo;         // xyz: groundAlbedo, w: pad
    DensityLayerGPU rayleighDensity[2];
    DensityLayerGPU mieDensity[2];
    DensityLayerGPU absorptionDensity[2];
};

struct SkyViewPushConstants {
    glm::vec3 cameraPosition{0.0f};
    float cameraAltitude = 0.0f;
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};
    float sunIntensity = 1.0f;
};

struct SkyCompositionPushConstants {
    glm::mat4 invViewProj{1.0f};
    glm::vec4 cameraPos{0.0f, 0.0f, 0.0f, 1.0f};     // xyz: camPos, w: exposure
    glm::vec4 sunDir{0.0f, 1.0f, 0.0f, 0.0f};        // xyz: sunDir, w: unused
    glm::vec4 moonDir{0.0f, -1.0f, 0.0f, -1.0f};     // xyz: moonDir, w: moonPhase (<0 auto, >=0 manual)
    glm::vec4 nightParams{1.0f, 1.5f, 0.0f, 0.025f}; // x: starIntensity, y: moonIntensity, z: time, w: moonAngularRadius
};

#include "VolumetricFogComponent.hpp"
#include "VolumetricCloudComponent.hpp"
#include "WeatherComponent.hpp"

struct CloudParametersGPU {
    glm::vec4 cloudAltitudes;      // x: bottomAltitude, y: topAltitude, z: coverage, w: density
    glm::vec4 cloudModeling;       // x: cloudScale, y: detailScale, z: detailErosion, w: enabled (1.0 or 0.0)
    glm::vec4 windParams;          // xy: windDirection, z: windSpeed, w: time
    glm::vec4 lightingParams;      // x: silverLining, y: powderEffect, z: marchSteps, w: shadowIntensity
    glm::vec4 cloudColor;          // xyz: cloudColor, w: lightningFlash
    glm::vec4 ambientColor;        // xyz: ambientColor, w: unused
    glm::vec4 weatherParams;       // x: weatherType (0=Clear, 1=Rain, 2=Snow, 3=Storm), y: precipIntensity, z: wetness, w: lightningFlash
};

struct WeatherPrecipPushConstants {
    glm::mat4 viewProj{1.0f};
    glm::vec4 cameraPos{0.0f};     // xyz: camPos, w: time
    glm::vec4 windParams{0.0f};    // xy: windDir, z: windSpeed, w: unused
    glm::vec4 weatherParams{0.0f}; // x: weatherType, y: precipIntensity, z: unused, w: lightningFlash
    glm::vec4 lightTint{1.0f};     // rgb: ambient/sun tint, w: exposure
};

struct WeatherLightningPushConstants {
    glm::mat4 viewProj{1.0f};
    glm::vec4 strikePos{0.0f};     // xyz: ground strike point, w: strike width
    glm::vec4 cloudPos{0.0f};      // xyz: cloud base strike point, w: flashIntensity
    glm::vec4 cameraPos{0.0f};     // xyz: cameraPos, w: branchSeed
};

struct WeatherParticleVertex {
    glm::vec3 seedPos{0.0f};
    glm::vec2 corner{0.0f};
    glm::vec2 sizePhase{0.0f};
};

struct AerialPerspectivePushConstants {
    glm::mat4 invViewProj{1.0f};
    glm::vec4 cameraPos{0.0f, 0.0f, 0.0f, 96.0f};    // xyz: camPos, w: maxFogDistance
    glm::vec4 sunDirection{0.0f, 1.0f, 0.0f, 1.0f};   // xyz: sunDir, w: sunIntensity
    glm::vec4 fogParams{1.0f, 0.8f, 0.2f, 0.0f};     // x: densityMultiplier, y: anisotropy, z: nearDistance, w: mode (0=atmo, 1=dense)
    glm::vec4 fogColorTint{1.0f, 1.0f, 1.0f, 0.0f};  // rgb: tint, w: affectSky (0.0 or 1.0)
    glm::vec4 heightParams{0.0f, 0.01f, 1.0f, 0.0f}; // x: baseHeight, y: heightFalloff, z: shadowIntensity, w: unused
};

struct AerialCompPushConstants {
    glm::mat4 invViewProj{1.0f};
    glm::vec4 cameraPos{0.0f, 0.0f, 0.0f, 0.0f};     // xyz: camPos, w: mode (0=atmo, 1=dense)
    glm::vec4 sunDir{0.0f, 1.0f, 0.0f, 1.0f};        // xyz: sunDir, w: sunIntensity
    glm::vec4 fogParams{0.2f, 96.0f, 1.0f, 0.0f};    // x: nearDist, y: maxDist, z: densityMultiplier, w: affectSky (0.0 or 1.0)
    glm::vec4 fogColorTint{1.0f, 1.0f, 1.0f, 1.0f};  // rgb: tint, w: unused
    glm::vec4 sunScreen{0.5f, 0.5f, 0.0f, 1.0f};    // xy: sunUV, z: sunInFront (1.0 or 0.0), w: shaftIntensity
};

struct LUTTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    uint32_t width = 0;
    uint32_t height = 0;
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct LUTTexture3D {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct VolumetricShadowUBO {
    glm::mat4 cascadeLightSpaceMatrices[4];
    glm::vec4 cascadeSplits;
    glm::vec4 shadowParams;
};

/**
 * @class AtmosphereRenderFeature
 * @brief Encapsulates Sébastien Hillaire's physical atmosphere rendering pipeline.
 *
 * Manages compute passes for Transmittance LUT, Multi-Scattering LUT, Sky-View LUT,
 * and 3D Camera Volume LUT (Aerial Perspective), as well as fullscreen composition passes.
 */
class AtmosphereRenderFeature {
public:
    AtmosphereRenderFeature() = default;
    ~AtmosphereRenderFeature();

    void init(VulkanRenderer& renderer);
    void cleanup();

    // 1. Called before scene geometry rendering (dispatches compute LUTs)
    void executeLUTPasses(VkCommandBuffer cmd, const AtmosphereComponent& comp, 
                          const glm::vec3& sunDir, const glm::vec3& sunColor, 
                          float sunIntensity, const glm::vec3& camPos,
                          const glm::mat4& invViewProj = glm::mat4(1.0f),
                          const VolumetricFogComponent* fog = nullptr,
                          const VulkanRenderer::CascadeShadowData* shadowData = nullptr);

    // 2. Called after geometry pass to composite physical sky, stars, moon, and clouds into scene
    void executeCompositionPass(VkCommandBuffer cmd, const glm::mat4& invViewProj,
                                const glm::vec3& camPos, const glm::vec3& sunDir,
                                const AtmosphereComponent* atmo,
                                float totalTime,
                                float exposure = 1.0f,
                                const VolumetricCloudComponent* cloud = nullptr,
                                const WeatherComponent* weather = nullptr,
                                float lightningFlash = 0.0f);

    // 3. Called after sky background to apply aerial perspective fog, cloud shadows, 3D precipitation and lightning over scene geometry
    void executeAerialPass(VkCommandBuffer cmd, const glm::mat4& invViewProj, const glm::vec3& camPos, const glm::vec3& sunDir,
                           const VolumetricFogComponent* fog = nullptr,
                           const VolumetricCloudComponent* cloud = nullptr,
                           float totalTime = 0.0f,
                           const WeatherComponent* weather = nullptr,
                           float lightningFlash = 0.0f,
                           const glm::mat4& viewProj = glm::mat4(1.0f),
                           const glm::vec3& lightningGroundPos = glm::vec3(0.0f),
                           const glm::vec3& lightningCloudPos = glm::vec3(0.0f),
                           float boltIntensity = -1.0f,
                           uint32_t strikeType = 0);

    bool isInitialized() const { return m_initialized; }

private:
    void createUBO();
    void updateUBO(const AtmosphereComponent& comp);
    void updateCloudUBO(const VolumetricCloudComponent* cloud, float totalTime, const WeatherComponent* weather = nullptr, float lightningFlash = 0.0f);
    void generateCloudNoise();
    void createLUTTextures();
    void createDescriptorPool();
    void createDescriptorSetLayouts();
    void allocateAndWriteDescriptorSets();
    void createPipelines();

    void createTexture(LUTTexture& tex, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage);
    void destroyTexture(LUTTexture& tex);

    void createTexture3D(LUTTexture3D& tex, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, VkImageUsageFlags usage, bool repeat = false);
    void destroyTexture3D(LUTTexture3D& tex);

    VkShaderModule loadShaderModule(const std::string& path);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void transitionImageLayout(VkCommandBuffer cmd, LUTTexture& tex,
                               VkImageLayout newLayout,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

    void transitionImageLayout3D(VkCommandBuffer cmd, LUTTexture3D& tex,
                                 VkImageLayout newLayout,
                                 VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                 VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

    VulkanRenderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    bool m_initialized = false;

    // GPU Buffer for atmosphere parameters
    VkBuffer m_paramBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_paramMemory = VK_NULL_HANDLE;

    static constexpr uint32_t FROXEL_GRID_X = 160;
    static constexpr uint32_t FROXEL_GRID_Y = 90;
    static constexpr uint32_t FROXEL_GRID_Z = 64;

    // LUT textures
    LUTTexture m_transmittanceLUT; // 256 x 64
    LUTTexture m_multiScatterLUT;   // 32 x 32
    LUTTexture m_skyViewLUT;        // 192 x 108
    LUTTexture3D m_cameraVolumeLUT; // 160 x 90 x 64 (Screen-aligned Froxel grid)
    LUTTexture m_moonAlbedoTex;     // 1024 x 512 NASA LROC Photographic Albedo Map
    LUTTexture3D m_cloudNoiseTex;   // 64 x 64 x 64 Perlin-Worley 3D Noise Volume
    VkBuffer m_cloudBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_cloudMemory = VK_NULL_HANDLE;

    void loadMoonTexture();

    // Descriptor Pool
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    // Pass 1: Transmittance
    VkDescriptorSetLayout m_transmittanceLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_transmittanceSet = VK_NULL_HANDLE;
    VkPipelineLayout m_transmittancePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_transmittancePipeline = VK_NULL_HANDLE;

    // Pass 2: Multi-Scattering
    VkDescriptorSetLayout m_multiScatterLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_multiScatterSet = VK_NULL_HANDLE;
    VkPipelineLayout m_multiScatterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_multiScatterPipeline = VK_NULL_HANDLE;

    // Pass 3: Sky-View
    VkDescriptorSetLayout m_skyViewLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_skyViewSet = VK_NULL_HANDLE;
    VkPipelineLayout m_skyViewPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_skyViewPipeline = VK_NULL_HANDLE;

    // Pass 4: Sky Composition
    VkDescriptorSetLayout m_compositionLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_compositionSet = VK_NULL_HANDLE;
    VkPipelineLayout m_compositionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_compositionPipeline = VK_NULL_HANDLE;

    // Pass 5: Aerial Perspective Compute
    VkDescriptorSetLayout m_aerialPerspectiveLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_aerialPerspectiveSet = VK_NULL_HANDLE;
    VkPipelineLayout m_aerialPerspectivePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_aerialPerspectivePipeline = VK_NULL_HANDLE;

    // Pass 6: Aerial Composition Graphics (Fullscreen Fog Blend)
    VkDescriptorSetLayout m_aerialCompLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_aerialCompSet = VK_NULL_HANDLE;
    VkPipelineLayout m_aerialCompPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_aerialCompPipeline = VK_NULL_HANDLE;
    VkSampler m_depthSampler = VK_NULL_HANDLE;
    VkImageView m_lastDepthView = VK_NULL_HANDLE;

    // Shadow cascade data for volumetric God Rays
    VkBuffer m_shadowBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_shadowMemory = VK_NULL_HANDLE;
    VkImageView m_lastShadowDepthView = VK_NULL_HANDLE;

    void updateShadowUBO(const VulkanRenderer::CascadeShadowData* shadowData);

    // --- Dynamic Weather Rendering (Approach 2) ---
    LUTTexture m_weatherFxTex;
    LUTTexture m_lightningBoltTex;
    void loadWeatherTextures();

    static constexpr uint32_t PRECIP_PARTICLE_COUNT = 2048;
    VkBuffer m_particleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_particleMemory = VK_NULL_HANDLE;
    void createParticleBuffer();

    // Pass 7: Precipitation Particles (Alpha Blend)
    VkDescriptorSetLayout m_precipLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_precipSet = VK_NULL_HANDLE;
    VkPipelineLayout m_precipPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_precipPipeline = VK_NULL_HANDLE;

    // Pass 8: 3D Branching Lightning Bolt (Additive Blend)
    VkDescriptorSetLayout m_lightningLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_lightningSet = VK_NULL_HANDLE;
    VkPipelineLayout m_lightningPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_lightningPipeline = VK_NULL_HANDLE;

    void createWeatherPipelines();
};