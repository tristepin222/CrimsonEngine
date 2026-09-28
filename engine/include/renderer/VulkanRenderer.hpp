#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <stdexcept>
#include <glm/glm.hpp>

#include "core/VulkanDevice.hpp"
#include "core/VulkanSwapChain.hpp"
#include "core/VulkanPipeline.hpp"
#include "core/VulkanBuffer.hpp"
#include "core/VulkanDescriptors.hpp"
#include "core/VulkanCommandManager.hpp"
#include "core/VulkanFrameSync.hpp"

#include "../ecs/components/Mesh.hpp"
#include "../ecs/components/Material.hpp"
#include "../ecs/components/Transform.hpp"
#include "../ecs/components/Camera.hpp"
#include "../soa/MeshSoA.hpp"
#include "renderer/RenderSettings.hpp"
#include "renderer/PostProcessPipeline.hpp"

// hook for the sky plugin
using TransmittanceHook = std::function<glm::vec3(float camAltitude, const glm::vec3& sunDir)>;


/**
 * @struct PipelineHandle
 * @brief Holds a compiled graphics pipeline and its layout.
 */
struct PipelineHandle {
    /** @brief Vulkan pipeline object. */
    VkPipeline pipeline = VK_NULL_HANDLE;
    /** @brief Vulkan pipeline layout. */
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

/**
 * @struct GPULight
 * @brief Representation of an individual light source in the GPU uniform buffer.
 */
struct GPULight {
    glm::vec4 position{0.0f};  // xyz: world position, w: range
    glm::vec4 direction{0.0f}; // xyz: direction, w: type (0=Directional, 1=Point, 2=Spot)
    glm::vec4 color{0.0f};     // rgb: color, w: intensity
    glm::vec4 shadowInfo{-1.0f, 0.0015f, 0.0f, 0.0f}; // x: shadowLayerIndex (-1 if unshadowed), y: shadowBias, z: shadowNormalBias, w: unused
};

static constexpr uint32_t MAX_LIGHTS = 16;
static constexpr uint32_t MAX_SHADOW_CASCADES = 4;
static constexpr uint32_t MAX_SPOT_SHADOWS = 4;
static constexpr uint32_t TOTAL_SHADOW_LAYERS = MAX_SHADOW_CASCADES + MAX_SPOT_SHADOWS; // 8 layers

/**
 * @struct CameraUBO
 * @brief Representation of camera and scene lighting data inside Vulkan uniform buffers.
 */
struct CameraUBO {
    static constexpr uint32_t MAX_LIGHTS = 16;
    static constexpr uint32_t MAX_SHADOW_CASCADES = 4;
    static constexpr uint32_t MAX_SPOT_SHADOWS = 4;
    static constexpr uint32_t TOTAL_SHADOW_LAYERS = MAX_SHADOW_CASCADES + MAX_SPOT_SHADOWS;

    /** @brief View-projection matrix. */
    glm::mat4 viewProj{1.0f};
    /** @brief 4 Cascaded light-space view-projection matrices with [0, 1] UV bias for Sun CSM. */
    glm::mat4 cascadeLightSpaceMatrices[MAX_SHADOW_CASCADES]{};
    /** @brief 4 Light-space view-projection matrices with [0, 1] UV bias for Spot lights (layers 4..7). */
    glm::mat4 spotLightSpaceMatrices[MAX_SPOT_SHADOWS]{};
    /** @brief View-space depth splits for cascades (x=split0, y=split1, z=split2, w=split3). */
    glm::vec4 cascadeSplits{0.0f};
    glm::vec4 camPos{0.0f};       // camPos.xyz, w unused
    glm::vec4 ambientLight{1.0f}; // Ambient light color/intensity from the renderer fallback sun
    glm::vec4 shadowParams{0.0f}; // x: bias, y: normalBias, z: shadowMapRes, w: shadowEnabled (1.0 or 0.0)
    glm::vec4 lightParams{0.0f};  // x: numLights, y: primarySunIdx, z: pointShadowLightIdx, w: unused
    glm::vec4 weatherParams{0.0f}; // x: wetness (0..1), y: rainIntensity (0..1), z: puddleLevel (0..1), w: time
    GPULight lights[MAX_LIGHTS]{};
};

/**
 * @struct InstanceData
 * @brief Structure containing transformation and ID parameters for a single renderable instance.
 */
struct InstanceData {
    /** @brief Transformation model matrix. */
    glm::mat4 model;
    /** @brief Identifier of the associated material. */
    uint32_t materialID;
    /** @brief Identifier of the associated mesh. */
    uint32_t meshID;
    /** @brief Instance color tint. */
    glm::vec4 color;
};

/**
 * @struct InstanceDataSoA
 * @brief CPU-side representation of batch-rendering instances organized as a Structure of Arrays.
 */
struct InstanceDataSoA {
    /** @brief Model matrices of all instances. */
    std::vector<glm::mat4> models;
    /** @brief Material IDs of all instances. */
    std::vector<uint32_t> materialIDs;
    /** @brief Mesh IDs of all instances. */
    std::vector<uint32_t> meshIDs;
    /** @brief Colors of all instances. */
    std::vector<glm::vec4> colors;

    /**
     * @brief Clears all array allocations.
     */
    void clear() {
        models.clear();
        materialIDs.clear();
        meshIDs.clear();
        colors.clear();
    }

    /**
     * @brief Appends a new instance.
     * @param inst Instance parameters.
     * @return Index of the new instance.
     */
    size_t push(const InstanceData& inst) {
        models.push_back(inst.model);
        meshIDs.push_back(inst.meshID);
        materialIDs.push_back(inst.materialID);
        colors.push_back(inst.color);
        return models.size() - 1;
    }

    /**
     * @brief Retrieves total active instance count.
     * @return Instance count.
     */
    size_t size() const { return models.size(); }

    /**
     * @brief Retrieves CPU instancing details at index formatted for GPU.
     * @param i Array index.
     * @return GPU format instance data.
     */
    InstanceDataGPU get(size_t i) const {
        return {
            models[i],
            colors[i],
            meshIDs[i],
            materialIDs[i]
        };
    }
};


class ResourceManager;

/**
 * @class VulkanRenderer
 * @brief Manages core Vulkan pipeline states, device interaction, frames, buffers, and command submissions.
 */
class VulkanRenderer {
public:
    VulkanRenderer(GLFWwindow* win, const std::string& exeDir = "", bool enableValidationLayers = true);
    /**
     * @brief Destroy the Vulkan Renderer object and free resources.
     */
    ~VulkanRenderer();

    /**
     * @brief Resolves a shader path relative to the executable or CWD to support packaged/SDK builds.
     * @param originalPath The hardcoded shader path to resolve.
     * @return The resolved absolute or relative path to the shader file.
     */
    std::string resolveShaderPath(const std::string& originalPath) const;

    /**
     * @brief Gets the directory where the active executable resides.
     */
    const std::string& getExeDir() const { return exeDir; }

    /**
     * @brief Starts render pass recording for the current frame.
     * @param prePass Optional callback executed on command buffer before vkCmdBeginRenderPass (e.g. for compute passes).
     */
    void beginFrame(const std::function<void(VkCommandBuffer)>& prePass = {});
    /**
     * @brief Submits command buffers and presents the rendered swapchain image.
     */
    void endFrame();

    /**
     * @brief Submits draw commands for a single mesh.
     * @param mesh The target mesh.
     * @param mat The applied material.
     * @param transform The transformation components.
     */
    void drawMesh(const Mesh& mesh, const Material& mat, const Transform& transform);
    /**
     * @brief Performs drawing of instanced mesh batches.
     */
    void drawInstances();

    /**
     * @brief Creates the GPU buffers for batch instancing.
     * @param maxInstances Allocated size bound.
     */
    void createInstanceBuffer(size_t maxInstances);
    /**
     * @brief Transfers CPU instances down to the GPU instance buffer.
     */
    void updateInstanceBuffer();

    /**
     * @brief Instantiates uniform buffers for camera view and projection matrices.
     */
    void createCameraUBO();
    /**
     * @brief Refreshes active camera matrix attributes in UBO.
     */
    void updateCameraUBO(const CameraUBO& ubo);

    /**
     * @brief Recreates the swapchain when window resizing occurs.
     */
    void recreateSwapchain();
    /**
     * @brief Deallocates all Vulkan rendering state and resource bindings.
     */
    void cleanup();

    /**
     * @brief Creates a graphic pipeline custom-configured with specified shader source files.
     * @param vertPath File path to compiled vertex shader.
     * @param fragPath File path to compiled fragment shader.
     * @return PipelineHandle wrapping VkPipeline and layout.
     */
    PipelineHandle createPipelineForShaders(const std::string& vertPath, const std::string& fragPath);
    PipelineHandle createPipelineForShaders(const std::string& vertPath, const std::string& fragPath, const std::vector<VkDescriptorSetLayout>& customLayouts);
    PipelineHandle createPipelineForShaders(
        const std::string& vertPath,
        const std::string& fragPath,
        const std::vector<VkDescriptorSetLayout>& customLayouts,
        const std::vector<VkVertexInputBindingDescription>& customBindings,
        const std::vector<VkVertexInputAttributeDescription>& customAttributes,
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE
    );
    /**
     * @brief Allocates and uploads GPU buffers for a specific mesh loaded on the CPU.
     * @param id Mesh ID.
     */
    void uploadMesh(size_t id);
    /**
     * @brief Returns window close request status.
     * @return True if close requested.
     */
    bool shouldClose() const;
    /**
     * @brief Calculates delta time since the previous frame.
     * @return Delta time in seconds.
     */
    float getDeltaTime();

    /**
     * @brief Gets GLFW window pointer.
     * @return Pointer to GLFW window.
     */
    GLFWwindow* getWindow() const { return window; }
    /**
     * @brief Checks if a key is currently pressed (executes within engine.dll context).
     * @param key GLFW key code.
     * @return True if pressed.
     */
    bool getKey(int key) const;
    /**
     * @brief Checks if a mouse button is currently pressed (executes within engine.dll context).
     * @param button GLFW mouse button code.
     * @return True if pressed.
     */
    bool getMouseButton(int button) const;
    /**
     * @brief Gets the mouse cursor position (executes within engine.dll context).
     * @param xpos Output X position.
     * @param ypos Output Y position.
     */
    void getMousePosition(double* xpos, double* ypos) const;
    /**
     * @brief Gets the window size (executes within engine.dll context).
     * @param width Output width.
     * @param height Output height.
     */
    void getWindowSize(int* width, int* height) const;
    /**
     * @brief Gets Vulkan Logical Device handle.
     * @return VkDevice handle.
     */
    VkDevice getDevice() const { return device.getDevice(); }
    /**
     * @brief Gets Vulkan Physical Device handle.
     * @return VkPhysicalDevice handle.
     */
    VkPhysicalDevice getPhysicalDevice() const { return device.getPhysicalDevice(); }

    /**
     * @brief Gets Vulkan RenderPass for 3D scene geometry. Returns HDR render pass if active, or swapchain render pass.
     * @return RenderPass handle.
     */
    VkRenderPass getRenderPass() const { return (m_hdrRenderPass != VK_NULL_HANDLE) ? m_hdrRenderPass : swapchain.getRenderPass(); }
    /**
     * @brief Gets Vulkan HDR 16-bit floating point RenderPass.
     * @return HDR RenderPass handle.
     */
    VkRenderPass getHDRRenderPass() const { return m_hdrRenderPass; }
    /**
     * @brief Gets Vulkan Swapchain presentation RenderPass (used by Tone Mapping and ImGui Editor UI).
     * @return Swapchain RenderPass handle.
     */
    VkRenderPass getSwapchainRenderPass() const { return swapchain.getRenderPass(); }
    /**
     * @brief Gets Vulkan Pipeline.
     * @return Pipeline handle.
     */
    VkPipeline getPipeline() const { return pipeline.get(); }
    /**
     * @brief Gets Vulkan Pipeline Layout.
     * @return PipelineLayout handle.
     */
    VkPipelineLayout getPipelineLayout() const { return pipeline.getLayout(); }
    /**
     * @brief Gets Swapchain Extent.
     * @return Extent representation.
     */
    VkExtent2D getSwapchainExtent() const { return swapchain.getExtent(); }

    /**
     * @brief Retrieves active Vulkan command buffer for current frame.
     * @return VkCommandBuffer handle.
     */
    VkCommandBuffer getCurrentCommandBuffer() {
        return cmdManager.getCurrentCommandBuffer();
    }
    /**
     * @brief Begins command recording on a temporary command buffer.
     * @return VkCommandBuffer handle.
     */
    VkCommandBuffer beginSingleUseCommands() {
        return cmdManager.beginOneTimeCommand();
    }
    /**
     * @brief Finalizes and submits a temporary command buffer.
     * @param commandBuffer The temporary command buffer.
     */
    void endSingleUseCommands(VkCommandBuffer commandBuffer) {
        cmdManager.endOneTimeCommand(commandBuffer, device.getGraphicsQueue());
    }
    /**
     * @brief Gets Camera Descriptor Set.
     * @return VkDescriptorSet handle.
     */
    VkDescriptorSet getCameraDescriptorSet() const {
        return cameraDescriptorSet;
    }
    /**
     * @brief Gets Descriptor Set Layout for Textures.
     * @return VkDescriptorSetLayout handle.
     */
    VkDescriptorSetLayout getTextureDescriptorSetLayout() const {
        return descriptors.getTextureDescriptorSetLayout();
    }
    /**
     * @brief Gets Default White Texture Descriptor Set fallback.
     * @return VkDescriptorSet handle.
     */
    VkDescriptorSet getDefaultTextureSet() const;

    /**
     * @brief Configures active camera matrices.
     * @param viewProj View-Projection matrix.
     * @param position 3D position vector.
     */
    void setActiveCamera(const glm::mat4& viewProj, const glm::vec3& position) {
        activeCameraViewProj = viewProj;
        activeCameraPosition = position;
        hasActiveCameraData = true;
    }
    /**
     * @brief Configures active camera view, projection, and position.
     * @param projection Projection matrix.
     * @param position 3D position vector.
     * @param view View matrix.
     */
    void setActiveCamera(const glm::mat4& projection, const glm::vec3& position, const glm::mat4& view)
    {
        activeCameraView = view;
        activeCameraProjection = projection;
        activeCameraViewProj = projection * view;
        activeCameraPosition = position;
        hasActiveCameraData = true;
    }

    /**
     * @brief Checks if a camera is currently active.
     * @return True if active camera data is loaded, false otherwise.
     */
    bool hasActiveCamera() const { return hasActiveCameraData; }
    /**
     * @brief Gets active camera view projection matrix.
     * @return Mat4.
     */
    const glm::mat4& getActiveCameraViewProj() const { return activeCameraViewProj; }
    /**
     * @brief Gets active camera position.
     * @return Vec3.
     */
    const glm::vec3& getActiveCameraPosition() const { return activeCameraPosition; }
    /**
     * @brief Gets active camera view matrix.
     * @return Mat4.
     */
    const glm::mat4& getActiveCameraView() const { return activeCameraView; }
    /**
     * @brief Gets active camera projection matrix.
     * @return Mat4.
     */
    const glm::mat4& getActiveCameraProjection() const { return activeCameraProjection; }

    glm::mat4 gameplayCameraViewProj = glm::mat4(1.0f);
    glm::vec3 gameplayCameraPosition = glm::vec3(0.0f);

    using RenderHook = std::function<void(VkCommandBuffer)>;
    using TransmittanceHook = std::function<glm::vec3(float camAltitude, const glm::vec3& sunDir)>;

    struct CelestialLightInfo {
        bool overrideDirectional = false;
        glm::vec3 direction{0.0f, -1.0f, 0.0f}; // Direction light travels (e.g. -moonDir)
        glm::vec3 color{0.65f, 0.8f, 1.0f};
        float intensity = 0.15f;
    };
    using CelestialLightHook = std::function<CelestialLightInfo(const glm::vec3& sunDir, const glm::vec3& sunColor, float sunIntensity)>;

    struct WeatherParamsInfo {
        float wetness = 0.0f;
        float rainIntensity = 0.0f;
        float puddleLevel = 0.0f;
        float time = 0.0f;
    };
    using WeatherParamsHook = std::function<WeatherParamsInfo()>;

    void setPrePass(RenderHook hook) { m_prePass = std::move(hook); }
    void setSkyPass(RenderHook hook) { m_skyPass = std::move(hook); }
    void setAerialPass(RenderHook hook) { m_aerialPass = std::move(hook); }
    void setTransmittanceHook(TransmittanceHook hook) { m_transmittanceHook = std::move(hook); }
    void setCelestialLightHook(CelestialLightHook hook) { m_celestialLightHook = std::move(hook); }
    void setWeatherParamsHook(WeatherParamsHook hook) { m_weatherParamsHook = std::move(hook); }

    const RenderHook& getPrePass() const { return m_prePass; }
    const RenderHook& getSkyPass() const { return m_skyPass; }
    const RenderHook& getAerialPass() const { return m_aerialPass; }
    const TransmittanceHook& getTransmittanceHook() const { return m_transmittanceHook; }
    const CelestialLightHook& getCelestialLightHook() const { return m_celestialLightHook; }
    const WeatherParamsHook& getWeatherParamsHook() const { return m_weatherParamsHook; }

    void setWeatherParams(const glm::vec4& params) { m_weatherParams = params; }
    glm::vec4 getWeatherParams() const {
        if (m_weatherParamsHook) {
            auto info = m_weatherParamsHook();
            return glm::vec4(info.wetness, info.rainIntensity, info.puddleLevel, info.time);
        }
        return m_weatherParams;
    }

    void executeSkyPass(VkCommandBuffer cmd) { if (m_skyPass) m_skyPass(cmd); }
    void executeAerialPass(VkCommandBuffer cmd) { if (m_aerialPass) m_aerialPass(cmd); }

    VkImageView getDepthImageView() const { return swapchain.getDepthImageView(); }
    VkImage getDepthImage() const { return swapchain.getDepthImage(); }
    VkFormat getDepthFormat() const { return swapchain.getDepthFormat(); }

    glm::vec3 evaluateAtmosphereTransmittance(float altitude, const glm::vec3& sunDir) const {
        if (m_transmittanceHook) return m_transmittanceHook(altitude, sunDir);
        return glm::vec3(1.0f);
    }

    void setGameplayCamera(const glm::mat4& viewProj, const glm::vec3& position) {
        gameplayCameraViewProj = viewProj;
        gameplayCameraPosition = position;
    }
    const glm::mat4& getGameplayCameraViewProj() const { return gameplayCameraViewProj; }
    const glm::vec3& getGameplayCameraPosition() const { return gameplayCameraPosition; }

    // --- Directional Shadow Mapping (Cascaded Shadow Maps) ---
    static constexpr uint32_t MAX_SHADOW_CASCADES = 4;
    static constexpr uint32_t MAX_SPOT_SHADOWS = 4;
    static constexpr uint32_t TOTAL_SHADOW_LAYERS = MAX_SHADOW_CASCADES + MAX_SPOT_SHADOWS;
    static constexpr uint32_t MAX_LIGHTS = 16;

    void createShadowResources();
    void destroyShadowResources();
    void recreateShadowResources(uint32_t newResolution);
    void createShadowPipelines();
    void destroyShadowPipelines();

    void beginShadowPass(VkCommandBuffer cmd, uint32_t layerIndex = 0);
    void endShadowPass(VkCommandBuffer cmd);
    void beginPointShadowPass(VkCommandBuffer cmd, uint32_t faceIndex = 0);
    void endPointShadowPass(VkCommandBuffer cmd);
    void beginMainPass(VkCommandBuffer cmd);

    // HDR and Tone Mapping Frame Lifecycle
    void beginHDRPass(VkCommandBuffer cmd);
    void endHDRPass(VkCommandBuffer cmd);
    void beginSwapchainPass(VkCommandBuffer cmd);
    void renderToneMapping(VkCommandBuffer cmd);

    // Extensible Post-Processing & Retro Resolution Scaling
    void addPostProcessPass(const std::string& name, const std::string& shaderPath, bool enabled = true);
    void removePostProcessPass(size_t index);
    void clearPostProcessPasses();
    bool reloadPostProcessShaders();
    std::vector<Engine::PostProcessPass>& getPostProcessPasses() { return m_postProcessPasses; }
    const std::vector<Engine::PostProcessPass>& getPostProcessPasses() const { return m_postProcessPasses; }

    const Engine::PostProcessSettings& getPostProcessSettings() const { return m_postProcessSettings; }
    void setPostProcessSettings(const Engine::PostProcessSettings& settings);

    void renderPostProcessChain(VkCommandBuffer cmd);
    VkExtent2D getHDRExtent() const { return m_hdrExtent; }

    const Engine::TonemapSettings& getTonemapSettings() const { return m_tonemapSettings; }
    void setTonemapSettings(const Engine::TonemapSettings& settings) { m_tonemapSettings = settings; }

    const Engine::SSAOSettings& getSSAOSettings() const { return m_ssaoSettings; }
    void setSSAOSettings(const Engine::SSAOSettings& settings) { m_ssaoSettings = settings; }

    VkImageView getHDRColorImageView() const { return m_hdrColorImageView; }
    VkFramebuffer getHDRFramebuffer() const { return m_hdrFramebuffer; }

    VkImageView getShadowImageView() const { return shadowDepthView; }
    VkImageView getShadowDepthView() const { return shadowDepthView; }
    VkImageView getShadowCascadeImageView(uint32_t layerIndex = 0) const { return shadowLayerViews[layerIndex]; }
    VkSampler getShadowSampler() const { return shadowSampler; }
    VkRenderPass getShadowRenderPass() const { return shadowRenderPass; }
    VkFramebuffer getShadowFramebuffer(uint32_t layerIndex = 0) const { return shadowLayerFramebuffers[layerIndex]; }
    VkImageView getPointShadowCubeView() const { return pointShadowCubeView; }
    VkFramebuffer getPointShadowFramebuffer(uint32_t faceIndex = 0) const { return pointShadowFaceFramebuffers[faceIndex]; }
    PipelineHandle getShadowPipeline() const { return m_shadowPipeline; }
    PipelineHandle getSkinnedShadowPipeline() const { return m_skinnedShadowPipeline; }

    struct CascadeShadowData {
        std::array<glm::mat4, MAX_SHADOW_CASCADES> cascadeLightSpaceMatrices{};
        glm::vec4 cascadeSplits{0.0f};
        glm::vec4 shadowParams{0.0f};
        bool enabled = false;
    } currentCascadeShadowData{};

    const CascadeShadowData& getCascadeShadowData() const { return currentCascadeShadowData; }
    void setCascadeShadowData(const CascadeShadowData& data) { currentCascadeShadowData = data; }

    const Engine::ShadowSettings& getShadowSettings() const { return m_shadowSettings; }
    void setShadowSettings(const Engine::ShadowSettings& settings) {
        if (settings.resolution != m_shadowSettings.resolution) {
            recreateShadowResources(settings.resolution);
        }
        m_shadowSettings = settings;
    }
    
    /** @brief Storage vector of created custom Vulkan pipelines. */
    std::vector<std::unique_ptr<VulkanPipeline>> pipelines;
    /** @brief CPU-side instancing data in Structure-of-Arrays format. */
    InstanceDataSoA instanceDataCPU;

    /** @brief Mesh database components in SoA layout. */
    MeshSoA meshSoA;
    /** @brief Vulkan GPU buffer for instance matrices. */
    VulkanBuffer instanceBuffer;

    /** @brief Core logical and physical Vulkan device interface. */
    VulkanDevice device{ true };
    /** @brief Swapchain manager module. */
    VulkanSwapchain swapchain;
    /** @brief Global Vulkan descriptor set management module. */
    VulkanDescriptors descriptors;
    /** @brief Global Resource Manager instance. */
    std::unique_ptr<ResourceManager> resourceManager;

private:

    /** @brief Pointer to GLFW window. */
    GLFWwindow* window = nullptr;
    /** @brief Directory where the executable is located. */
    std::string exeDir;


    /** @brief Debug messenger handle for validation layer callbacks. */
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
#ifdef DEBUG
    /** @brief Flag denoting validation checks state. */
    bool enableValidationLayers = true; // turn off for release
#else
    /** @brief Flag denoting validation checks state. */
    bool enableValidationLayers = false;
#endif
    /** @brief Default graphics pipeline state manager. */
    VulkanPipeline pipeline;
    /** @brief Command pool and command buffer allocator. */
    VulkanCommandManager cmdManager;
    /** @brief Synchronization fences and semaphores management. */
    VulkanFrameSync frameSync;
    /** @brief Presentation surface binding window and Vulkan instance. */
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    /** @brief Swapchain index of next image to render. */
    uint32_t currentImageIndex = 0;

    /** @brief GPU buffer for the camera UBO. */
    VulkanBuffer cameraBuffer;

    /** @brief descriptor set representing active camera. */
    VkDescriptorSet cameraDescriptorSet = VK_NULL_HANDLE;

    /** @brief Flag checking if window resized. */
    bool framebufferResized = false;
    /** @brief Previous frame time stamp. */
    double lastTime = 0.0;
    /** @brief Active camera view proj matrix cache. */
    glm::mat4 activeCameraViewProj{ 1.0f };
    /** @brief Active camera world space position cache. */
    glm::vec3 activeCameraPosition{ 0.0f };
    /** @brief Active camera view matrix cache. */
    glm::mat4 activeCameraView{};
    /** @brief Active camera projection matrix cache. */
    glm::mat4 activeCameraProjection{};
    /** @brief Flag checking if active camera is initialized. */
    bool hasActiveCameraData = false;

    /** @brief Cache of pipelines created via createPipelineForShaders keyed by vert|frag path. */
    std::unordered_map<std::string, PipelineHandle> m_pipelineCache;

    RenderHook m_prePass;
    RenderHook m_skyPass;
    RenderHook m_aerialPass;
    TransmittanceHook m_transmittanceHook;
    CelestialLightHook m_celestialLightHook;
    WeatherParamsHook m_weatherParamsHook;
    glm::vec4 m_weatherParams{0.0f};

private:
    Engine::ShadowSettings m_shadowSettings{};
    VkFormat shadowDepthFormat = VK_FORMAT_UNDEFINED;
    VkImage shadowDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowDepthMemory = VK_NULL_HANDLE;
    VkImageView shadowDepthView = VK_NULL_HANDLE; // 2D array view across all layers (cascades + spots) for sampling
    std::array<VkImageView, TOTAL_SHADOW_LAYERS> shadowLayerViews{}; // Per-layer 2D views for render targets
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, TOTAL_SHADOW_LAYERS> shadowLayerFramebuffers{}; // Per-layer framebuffers
    PipelineHandle m_shadowPipeline{};
    PipelineHandle m_skinnedShadowPipeline{};

    // Point Light Shadow Cubemap Resources (6 faces)
    VkImage pointShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory pointShadowMemory = VK_NULL_HANDLE;
    VkImageView pointShadowCubeView = VK_NULL_HANDLE;
    std::array<VkImageView, 6> pointShadowFaceViews{};
    std::array<VkFramebuffer, 6> pointShadowFaceFramebuffers{};

    PipelineHandle createDepthOnlyPipeline(const std::string& vertPath);

    // HDR Offscreen Render Target
    Engine::TonemapSettings m_tonemapSettings{};
    VkRenderPass m_hdrRenderPass = VK_NULL_HANDLE;
    VkImage m_hdrColorImage = VK_NULL_HANDLE;
    VkDeviceMemory m_hdrColorMemory = VK_NULL_HANDLE;
    VkImageView m_hdrColorImageView = VK_NULL_HANDLE;
    VkFramebuffer m_hdrFramebuffer = VK_NULL_HANDLE;
    VkSampler m_hdrSampler = VK_NULL_HANDLE;

    // Post-Process Tone Mapping Pipeline
    VkDescriptorSetLayout m_tonemapDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_tonemapDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_tonemapDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout m_tonemapPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_tonemapPipeline = VK_NULL_HANDLE;

    // Extensible Post-Process Pipeline & Resolution Scaling
    Engine::PostProcessSettings m_postProcessSettings{};
    std::vector<Engine::PostProcessPass> m_postProcessPasses;
    VkExtent2D m_hdrExtent{ 0, 0 };
    VkImageView m_finalPostProcessView = VK_NULL_HANDLE;
    VkImageView m_lastTonemapSrcView = VK_NULL_HANDLE;
    VkSampler m_lastTonemapSampler = VK_NULL_HANDLE;

    // Dedicated HDR Depth Buffer
    VkImage m_hdrDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_hdrDepthMemory = VK_NULL_HANDLE;
    VkImageView m_hdrDepthImageView = VK_NULL_HANDLE;

    // Ping-pong post-process targets
    struct PostProcessTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    std::array<PostProcessTarget, 2> m_postProcessTargets{};
    VkDescriptorSet m_hdrDescriptorSet = VK_NULL_HANDLE;

    VkRenderPass m_postProcessRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_postProcessDescLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_postProcessDescPool = VK_NULL_HANDLE;
    VkPipelineLayout m_postProcessPipelineLayout = VK_NULL_HANDLE;
    VkSampler m_nearestSampler = VK_NULL_HANDLE;

    void createHDRResources();
    void destroyHDRResources();
    void createTonemapPipeline();
    void destroyTonemapPipeline();
    void updateTonemapDescriptor();
    void createPostProcessResources();
    void destroyPostProcessResources();
    VkPipeline createPostProcessPipeline(const std::string& fragPath, bool forceRecompile = false);
    VkExtent2D calculateHDRExtent() const;

    // Screen-Space Ambient Occlusion (SSAO) Pipeline & Resources
    Engine::SSAOSettings m_ssaoSettings{};
    VkRenderPass m_ssaoRenderPass = VK_NULL_HANDLE;
    VkImage m_ssaoImage = VK_NULL_HANDLE;
    VkDeviceMemory m_ssaoMemory = VK_NULL_HANDLE;
    VkImageView m_ssaoImageView = VK_NULL_HANDLE;
    VkFramebuffer m_ssaoFramebuffer = VK_NULL_HANDLE;
    VkSampler m_depthSampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_ssaoDescLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_ssaoDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_ssaoDescSet = VK_NULL_HANDLE;
    VkPipelineLayout m_ssaoPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ssaoPipeline = VK_NULL_HANDLE;
    VulkanBuffer m_ssaoUBOBuffer;

    VkDescriptorSetLayout m_ssaoBlurDescLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_ssaoBlurDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_ssaoBlurDescSet = VK_NULL_HANDLE;
    VkPipelineLayout m_ssaoBlurPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ssaoBlurPipeline = VK_NULL_HANDLE;

    struct SSAOUBOData {
        glm::mat4 proj{1.0f};
        glm::mat4 invProj{1.0f};
        glm::vec4 params{0.5f, 0.025f, 1.5f, 1.5f}; // radius, bias, intensity, power
        glm::vec4 resolution{0.0f}; // width, height, 1/width, 1/height
        glm::ivec4 settings{16, 0, 0, 0}; // sampleCount, debugAO, unused, unused
        glm::vec4 samples[32]{};
    } m_ssaoUBOData;

    void createSSAOResources();
    void destroySSAOResources();
    void createSSAOPipelines();
    void destroySSAOPipelines();
    void updateSSAODescriptors();
    void renderSSAOPass(VkCommandBuffer cmd);
    void renderSSAOBlurPass(VkCommandBuffer cmd);

    /** @brief Initializes main Vulkan instance, debuggers, devices, surfaces, and pipelines. */
    void initVulkan();
    /** @brief Prepares and compiles graphics pipeline configurations. */
    void createPipeline();
    /** @brief Couples window handle with Vulkan instance context. */
    void createWindowSurface();
    /** @brief Populates the Vulkan debug messenger structure parameters. */
    VkDebugUtilsMessengerCreateInfoEXT populateDebugMessengerCreateInfo();
    /** @brief Attaches validation layer logger callbacks. */
    void setupDebugMessenger();
    /** @brief Cleans up messenger validation logs. */
    void destroyDebugMessenger();
    /** @brief Instantiates the root Vulkan API instance. */
    void createInstanceAndDebug();
    /** @brief Creates physical/logical devices and queue interfaces. */
    void createDeviceAndQueues();
    /** @brief Initializes descriptor pools and configurations. */
    void setupDescriptors();
    /** @brief Configures swapchain render targets. */
    void createSwapchain();
    /** @brief Instantiates buffers and pipelines. */
    void createBuffersAndPipelines();
    /** @brief Sets up command buffers, pools, and sync fences. */
    void createCommandsAndSync();
    /** @brief Submits command buffers to graphic queue and schedules screen presentation. */
    void submitAndPresent();
};
