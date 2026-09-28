#include "../include/renderer/VulkanRenderer.hpp"
#include "renderer/ResourceManager.hpp"
#include <iostream>
#include <algorithm>
#include <random>

/**
 * @brief Construct a new Vulkan Renderer:: Vulkan Renderer object.
 * @param win Reference window.
 * @param enableValidation True to configure validation layers, false otherwise.
 */
VulkanRenderer::VulkanRenderer(GLFWwindow* win, const std::string& exeDirectory, bool enableValidation)
    : window(win), exeDir(exeDirectory), enableValidationLayers(enableValidation)
{
    resourceManager = std::make_unique<ResourceManager>();

    // Set up window resize callback
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
        auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
        if (renderer) {
            renderer->framebufferResized = true;
        }
    });
    glfwSetWindowUserPointer(window, this);

    initVulkan();
    lastTime = glfwGetTime();
}

/**
 * @brief Destroy the Vulkan Renderer:: Vulkan Renderer object.
 */
VulkanRenderer::~VulkanRenderer() {
    cleanup();
}

#include <filesystem>
#include <fstream>
#include "renderer/ShaderCompiler.hpp"

std::string VulkanRenderer::resolveShaderPath(const std::string& originalPath) const {
    if (originalPath.empty()) return "";

    // 0. If it's a source shader (.vert, .frag, .comp) or project shader, compile or resolve it
    std::string compiled = Engine::ShaderCompiler::resolveOrCompile(originalPath);
    if (!compiled.empty() && std::filesystem::exists(compiled)) {
        return compiled;
    }

    // 1. Extract shader filename (e.g. unlit.vert.spv)
    std::filesystem::path p(originalPath);
    std::string filename = p.filename().string();

    // 2. Try sibling "shaders" folder next to exe (packaged app)
    if (!exeDir.empty()) {
        std::filesystem::path localShader = std::filesystem::path(exeDir) / "shaders" / filename;
        if (std::filesystem::exists(localShader)) {
            return localShader.string();
        }
        
        // 3. Try parent "shaders" folder (SDK/editor structure: bin/ is sibling to shaders/)
        std::filesystem::path parentShader = std::filesystem::path(exeDir) / ".." / "shaders" / filename;
        if (std::filesystem::exists(parentShader)) {
            return parentShader.string();
        }

        // 4. Try parent-parent "shaders" folder (build/engine/Release/../../shaders)
        std::filesystem::path ppShader = std::filesystem::path(exeDir) / ".." / ".." / "shaders" / filename;
        if (std::filesystem::exists(ppShader)) {
            return ppShader.string();
        }
    }

    // 5. If path exists directly in CWD, return it
    if (std::filesystem::exists(originalPath)) {
        return originalPath;
    }

    // 6. Try relative "shaders/" and "build/shaders/" in CWD
    std::filesystem::path cwdShader1 = std::filesystem::path("shaders") / filename;
    if (std::filesystem::exists(cwdShader1)) return cwdShader1.string();

    std::filesystem::path cwdShader2 = std::filesystem::path("build/shaders") / filename;
    if (std::filesystem::exists(cwdShader2)) return cwdShader2.string();

    // 7. Try parent sibling "build/shaders" (if CWD is sandbox_game)
    std::filesystem::path siblingShader = std::filesystem::path("..") / "build" / "shaders" / filename;
    if (std::filesystem::exists(siblingShader)) return siblingShader.string();

    // Fallback to original
    return originalPath;
}

//
// ─── Initialization ─────────────────────────────────────────────────────────────
//
// initVulkan() — orchestration (call this from ctor)
/**
 * @brief Initializes the Vulkan API contexts.
 */
void VulkanRenderer::initVulkan() {
    std::cout << "[VulkanRenderer] createInstanceAndDebug..." << std::endl;
    createInstanceAndDebug();       // create instance & debug messenger
    std::cout << "[VulkanRenderer] createWindowSurface..." << std::endl;
    createWindowSurface();          // must create surface before picking physical device
    std::cout << "[VulkanRenderer] createDeviceAndQueues..." << std::endl;
    createDeviceAndQueues();        // pick physical device using surface, create logical device
    std::cout << "[VulkanRenderer] createSwapchain..." << std::endl;
    createSwapchain();              // create swapchain (depends on device + surface)
    std::cout << "[VulkanRenderer] createHDRResources..." << std::endl;
    createHDRResources();           // create HDR render pass, images, framebuffer, sampler
    std::cout << "[VulkanRenderer] setupDescriptors..." << std::endl;
    setupDescriptors();
    std::cout << "[VulkanRenderer] createCommandsAndSync..." << std::endl;
    createCommandsAndSync();
    std::cout << "[VulkanRenderer] createBuffersAndPipelines..." << std::endl;
    createBuffersAndPipelines();
    std::cout << "[VulkanRenderer] createTonemapPipeline..." << std::endl;
    createTonemapPipeline();        // create tone mapping pipeline & descriptor set
    std::cout << "[VulkanRenderer] createPostProcessResources..." << std::endl;
    createPostProcessResources();   // create post-process render pass, ping-pong targets, and samplers
    std::cout << "[VulkanRenderer] createSSAOPipelines..." << std::endl;
    createSSAOPipelines();          // create SSAO generation and blur pipelines
    std::cout << "[VulkanRenderer] initVulkan done!" << std::endl;
}



// -----------------------------
// Instance & Debug
// -----------------------------
/**
 * @brief Creates Vulkan instance and hooks validation callback logs.
 */
void VulkanRenderer::createInstanceAndDebug() {
    // This should create the VkInstance inside device.initialize()
    device.initialize();           // creates VkInstance
    setupDebugMessenger();         // debug messenger requires a valid instance
}

// -----------------------------
// Surface, Device & Swapchain
// -----------------------------
/**
 * @brief Creates the presentation surface coupling Vulkan and window context.
 */
void VulkanRenderer::createWindowSurface() {
    if (surface != VK_NULL_HANDLE) return; // already created

    if (glfwCreateWindowSurface(device.getInstance(), window, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface");
}

/**
 * @brief Selects physical GPU and configures device queues.
 */
void VulkanRenderer::createDeviceAndQueues() {
    // surface must be valid here
    if (surface == VK_NULL_HANDLE) {
        throw std::runtime_error("createDeviceAndQueues called with null surface");
    }

    device.pickPhysicalDevice(surface);      // pick GPU that supports presentation to surface
    device.createLogicalDevice();            // create device + queues
}

/**
 * @brief Initializes swapchain presentation structures.
 */
void VulkanRenderer::createSwapchain() {
    swapchain.initialize(
        device.getDevice(),
        device.getPhysicalDevice(),
        surface,
        device.getGraphicsQueueFamily()
    );
}

// -----------------------------
// Remaining setup
// -----------------------------
/**
 * @brief Setup descriptor layouts and allocations.
 */
void VulkanRenderer::setupDescriptors() {
    descriptors.create(device.getDevice());
    descriptors.createCameraDescriptorSetLayout();
    descriptors.createTextureDescriptorSetLayout();
    descriptors.createSingleTextureDescriptorSetLayout();
    descriptors.createJointsDescriptorSetLayout();
    descriptors.createTerrainDescriptorSetLayout();
}

/**
 * @brief Creates uniform buffers and compiles base pipeline.
 */
void VulkanRenderer::createBuffersAndPipelines() {
    createCameraUBO();
    createShadowResources();
    descriptors.allocateCameraDescriptorSets(cameraBuffer.get(), cameraBuffer.getSize(), shadowDepthView, shadowSampler, pointShadowCubeView);
    cameraDescriptorSet = descriptors.getCameraDescriptorSet();

    // Create the default fallback textures (white, normal, metallic)
    resourceManager->createDefaultTextures(*this);

    createInstanceBuffer(10000);
    createPipeline();
    createShadowPipelines();
}

/**
 * @brief Allocates command pools and sync structures.
 */
void VulkanRenderer::createCommandsAndSync() {
    cmdManager.create(device.getDevice(), device.getGraphicsQueueFamily(), 1);
    frameSync.create(device.getDevice(), 1);
}


/**
 * @brief Builds the default graphics pipeline configuration.
 */
void VulkanRenderer::createPipeline() {
    std::string vert = resolveShaderPath("build/shaders/grid.vert.spv");
    std::string frag = resolveShaderPath("build/shaders/grid.frag.spv");
    std::vector<VkDescriptorSetLayout> layouts = { descriptors.getCameraDescriptorSetLayout() };

    pipeline.create(device.getDevice(), swapchain.getExtent(), getRenderPass(), vert, frag, layouts);
}

/**
 * @brief Prepares Vulkan contexts for command recording.
 */
void VulkanRenderer::beginFrame(const std::function<void(VkCommandBuffer)>& prePass) {
    frameSync.waitForCurrentFrame();

    VkResult res = vkAcquireNextImageKHR(
        device.getDevice(),
        swapchain.getSwapchain(),
        UINT64_MAX,
        frameSync.getImageAvailableSemaphore(),
        VK_NULL_HANDLE,
        &currentImageIndex
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }

    cmdManager.beginFrame();
    VkCommandBuffer cmd = cmdManager.getCurrentCommandBuffer();

    if (prePass) {
        prePass(cmd);
    } else if (m_prePass) {
        m_prePass(cmd);
    }
}

void VulkanRenderer::beginShadowPass(VkCommandBuffer cmd, uint32_t layerIndex) {
    if (shadowRenderPass == VK_NULL_HANDLE || layerIndex >= TOTAL_SHADOW_LAYERS || shadowLayerFramebuffers[layerIndex] == VK_NULL_HANDLE) return;

    VkClearValue clearValue{};
    clearValue.depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = shadowRenderPass;
    rpInfo.framebuffer = shadowLayerFramebuffers[layerIndex];
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = { m_shadowSettings.resolution, m_shadowSettings.resolution };
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_shadowSettings.resolution);
    viewport.height = static_cast<float>(m_shadowSettings.resolution);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { m_shadowSettings.resolution, m_shadowSettings.resolution };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanRenderer::endShadowPass(VkCommandBuffer cmd) {
    if (shadowRenderPass == VK_NULL_HANDLE) return;
    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::beginPointShadowPass(VkCommandBuffer cmd, uint32_t faceIndex) {
    if (shadowRenderPass == VK_NULL_HANDLE || faceIndex >= 6 || pointShadowFaceFramebuffers[faceIndex] == VK_NULL_HANDLE) return;

    VkClearValue clearValue{};
    clearValue.depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = shadowRenderPass;
    rpInfo.framebuffer = pointShadowFaceFramebuffers[faceIndex];
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = { m_shadowSettings.resolution, m_shadowSettings.resolution };
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_shadowSettings.resolution);
    viewport.height = static_cast<float>(m_shadowSettings.resolution);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { m_shadowSettings.resolution, m_shadowSettings.resolution };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanRenderer::endPointShadowPass(VkCommandBuffer cmd) {
    if (shadowRenderPass == VK_NULL_HANDLE) return;
    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::beginHDRPass(VkCommandBuffer cmd) {
    if (m_hdrRenderPass == VK_NULL_HANDLE || m_hdrFramebuffer == VK_NULL_HANDLE) {
        beginMainPass(cmd);
        return;
    }

    VkClearValue clearValues[2]{};
    clearValues[0].color = { {0.05f, 0.05f, 0.05f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_hdrRenderPass;
    rpInfo.framebuffer = m_hdrFramebuffer;
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = m_hdrExtent;
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_hdrExtent.width);
    viewport.height = static_cast<float>(m_hdrExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_hdrExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.getLayout(), 0, 1, &cameraDescriptorSet, 0, nullptr);
}

void VulkanRenderer::endHDRPass(VkCommandBuffer cmd) {
    if (m_hdrRenderPass != VK_NULL_HANDLE) {
        vkCmdEndRenderPass(cmd);
    }
}

void VulkanRenderer::beginSwapchainPass(VkCommandBuffer cmd) {
    VkClearValue clearValues[2]{};
    clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = swapchain.getRenderPass();
    rpInfo.framebuffer = swapchain.getFramebuffers().at(currentImageIndex);
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = swapchain.getExtent();
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain.getExtent().width);
    viewport.height = static_cast<float>(swapchain.getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain.getExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanRenderer::renderToneMapping(VkCommandBuffer cmd) {
    if (m_tonemapPipeline == VK_NULL_HANDLE || m_tonemapPipelineLayout == VK_NULL_HANDLE || m_tonemapDescriptorSet == VK_NULL_HANDLE) return;

    updateTonemapDescriptor();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipelineLayout, 0, 1, &m_tonemapDescriptorSet, 0, nullptr);

    struct TonemapPushConstants {
        int tonemapperMode;
        float exposure;
        float gamma;
        float contrast;
        float saturation;
        float padding[3];
    };

    TonemapPushConstants pc{};
    pc.tonemapperMode = static_cast<int>(m_tonemapSettings.mode);
    pc.exposure = m_tonemapSettings.exposure;
    pc.gamma = m_tonemapSettings.gamma;
    pc.contrast = m_tonemapSettings.contrast;
    pc.saturation = m_tonemapSettings.saturation;
    pc.padding[0] = 0.0f;
    pc.padding[1] = 0.0f;
    pc.padding[2] = 0.0f;

    vkCmdPushConstants(cmd, m_tonemapPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TonemapPushConstants), &pc);

    // Draw procedural fullscreen triangle: 3 vertices, 1 instance
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void VulkanRenderer::beginMainPass(VkCommandBuffer cmd) {
    if (m_hdrRenderPass != VK_NULL_HANDLE) {
        beginHDRPass(cmd);
        return;
    }

    VkClearValue clearValues[2]{};
    clearValues[0].color = { {0.1f, 0.1f, 0.1f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = swapchain.getRenderPass();
    rpInfo.framebuffer = swapchain.getFramebuffers().at(currentImageIndex);
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = swapchain.getExtent();
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain.getExtent().width);
    viewport.height = static_cast<float>(swapchain.getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain.getExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.getLayout(), 0, 1, &cameraDescriptorSet, 0, nullptr);
}

/**
 * @brief Stops active render pass recording.
 */
void VulkanRenderer::endFrame() {
    VkCommandBuffer cmd = cmdManager.getCurrentCommandBuffer();
    vkCmdEndRenderPass(cmd);
    cmdManager.endFrame();

    submitAndPresent();
}

/**
 * @brief Submits command buffers to graphics queue.
 */
void VulkanRenderer::submitAndPresent() {
    VkCommandBuffer cmdBuf = cmdManager.getCurrentCommandBuffer();

    VkSemaphore waitSemaphores[] = { frameSync.getImageAvailableSemaphore() };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { frameSync.getRenderFinishedSemaphore() };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device.getGraphicsQueue(), 1, &submitInfo, frameSync.getInFlightFence()) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR sc = swapchain.getSwapchain();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &sc;
    presentInfo.pImageIndices = &currentImageIndex;

    VkResult res = vkQueuePresentKHR(device.getGraphicsQueue(), &presentInfo);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapchain();
    }
    else if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }

    frameSync.nextFrame();
}

/**
 * @brief Allocates memory for model transformation instance buffer.
 * @param maxInstances Number of maximum instances.
 */
void VulkanRenderer::createInstanceBuffer(size_t maxInstances) {
    VkDeviceSize size = maxInstances * sizeof(glm::mat4);
    instanceBuffer.create(device.getDevice(), device.getPhysicalDevice(), size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

/**
 * @brief Uploads instanced structures to GPU buffers.
 */
void VulkanRenderer::updateInstanceBuffer() {
    if (instanceDataCPU.size() == 0) return;

    std::vector<InstanceDataGPU> gpuData(instanceDataCPU.size());
    for (size_t i = 0; i < instanceDataCPU.size(); i++) {
        gpuData[i].model = instanceDataCPU.models[i];
        gpuData[i].color = instanceDataCPU.colors[i];
        // optionally include meshID/materialID if your shader needs it
    }

    instanceBuffer.uploadData(gpuData.data(), gpuData.size() * sizeof(InstanceDataGPU));
}

/**
 * @brief Allocates memory for Camera Uniform Buffer.
 */
void VulkanRenderer::createCameraUBO() {
    cameraBuffer.create(device.getDevice(), device.getPhysicalDevice(), sizeof(CameraUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

/**
 * @brief Maps and uploads camera projection matrices to GPU.
 */
void VulkanRenderer::updateCameraUBO(const CameraUBO& ubo) {
    if (!hasActiveCameraData) return;
    cameraBuffer.uploadData(&ubo, sizeof(CameraUBO));
}

/**
 * @brief Deallocates current swapchain and rebuilds it matching window dimensions.
 */
void VulkanRenderer::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device.getDevice());

    // Clean up old swapchain-dependent objects
    destroyPostProcessResources();
    destroyHDRResources();
    pipeline.destroy();
    swapchain.cleanup();

    // Recreate swapchain
    swapchain.initialize(
        device.getDevice(),
        device.getPhysicalDevice(),
        surface,
        device.getGraphicsQueueFamily()
    );

    // Recreate HDR color image, views, framebuffer matching new extent
    createHDRResources();
    createPostProcessResources();
    updateTonemapDescriptor();

    // Recreate pipeline using new render pass & extent
    createPipeline();
}

/**
 * @brief releases allocated Vulkan resources.
 */
void VulkanRenderer::cleanup() {
    if (device.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.getDevice());
    }

    // 0. Destroy Post-Process, Tone Mapping and HDR resources
    destroySSAOPipelines();
    destroyPostProcessResources();
    for (auto& pass : m_postProcessPasses) {
        if (pass.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device.getDevice(), pass.pipeline, nullptr);
            pass.pipeline = VK_NULL_HANDLE;
        }
    }
    m_postProcessPasses.clear();

    if (m_postProcessPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), m_postProcessPipelineLayout, nullptr);
        m_postProcessPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_postProcessDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), m_postProcessDescLayout, nullptr);
        m_postProcessDescLayout = VK_NULL_HANDLE;
    }
    if (m_postProcessRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.getDevice(), m_postProcessRenderPass, nullptr);
        m_postProcessRenderPass = VK_NULL_HANDLE;
    }
    if (m_nearestSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), m_nearestSampler, nullptr);
        m_nearestSampler = VK_NULL_HANDLE;
    }

    destroyTonemapPipeline();
    destroyHDRResources();
    if (m_hdrSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), m_hdrSampler, nullptr);
        m_hdrSampler = VK_NULL_HANDLE;
    }
    if (m_hdrRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.getDevice(), m_hdrRenderPass, nullptr);
        m_hdrRenderPass = VK_NULL_HANDLE;
    }

    // 1. Clear custom pipelines vector and cache to destroy them using the device
    m_pipelineCache.clear();
    pipelines.clear();

    // 2. Clear mesh database to destroy vertex/index buffers using the device
    meshSoA.clear();

    // 3. Cleanup default texture and cached textures in resource manager
    if (resourceManager) {
        resourceManager->cleanup(device.getDevice());
    }

    // 4. Destroy standard render buffers, pipelines, commands, and sync fences
    destroyShadowPipelines();
    destroyShadowResources();
    pipeline.destroy();
    descriptors.destroy();
    instanceBuffer.destroy();
    cameraBuffer.destroy();
    frameSync.destroy();
    cmdManager.destroy();

    // 5. Cleanup swapchain (contains framebuffers, renderpass, depth views/images)
    swapchain.cleanup();

    // 6. Destroy validation debug messenger
    destroyDebugMessenger();

    // 7. Destroy window surface (must be destroyed before instance is destroyed)
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(device.getInstance(), surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    // 8. Cleanup device and instance
    device.cleanup();
}

/**
 * @brief Helper utility creating a separate graphics pipeline using target shaders.
 * @param vert path to vertex bytecode.
 * @param frag path to fragment bytecode.
 * @return Pipeline handle.
 */
PipelineHandle VulkanRenderer::createPipelineForShaders(const std::string& vert, const std::string& frag) {
    std::vector<VkDescriptorSetLayout> layouts = {
        descriptors.getCameraDescriptorSetLayout(),
        descriptors.getTextureDescriptorSetLayout(),
        descriptors.getJointsDescriptorSetLayout()
    };
    return createPipelineForShaders(vert, frag, layouts);
}

PipelineHandle VulkanRenderer::createPipelineForShaders(const std::string& vert, const std::string& frag, const std::vector<VkDescriptorSetLayout>& customLayouts) {
    return createPipelineForShaders(vert, frag, customLayouts, {}, {}, VK_CULL_MODE_NONE);
}

PipelineHandle VulkanRenderer::createPipelineForShaders(
    const std::string& vert,
    const std::string& frag,
    const std::vector<VkDescriptorSetLayout>& customLayouts,
    const std::vector<VkVertexInputBindingDescription>& customBindings,
    const std::vector<VkVertexInputAttributeDescription>& customAttributes,
    VkCullModeFlags cullMode
) {
    std::string actualVert = resolveShaderPath(vert);
    std::string actualFrag = resolveShaderPath(frag);

    std::string key = actualVert + "|" + actualFrag;
    for (const auto& l : customLayouts) {
        key += "|" + std::to_string(reinterpret_cast<uintptr_t>(l));
    }
    key += "|bindings:" + std::to_string(customBindings.size()) + "|attrs:" + std::to_string(customAttributes.size()) + "|cull:" + std::to_string(cullMode);

    auto it = m_pipelineCache.find(key);
    if (it != m_pipelineCache.end()) {
        return it->second;
    }

    auto pipe = std::make_unique<VulkanPipeline>();
    pipe->create(device.getDevice(), swapchain.getExtent(), getRenderPass(),
        actualVert, actualFrag, customLayouts, customBindings, customAttributes, cullMode);

    VkPipeline p = pipe->get();
    VkPipelineLayout l = pipe->getLayout();
    pipelines.push_back(std::move(pipe)); // keep alive

    PipelineHandle handle = { p, l };
    m_pipelineCache[key] = handle;
    return handle;
}

/**
 * @brief Computes frame delta time since last invocation.
 * @return Delta time value.
 */
float VulkanRenderer::getDeltaTime() {
    double currentTime = glfwGetTime();
    float dt = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;
    return dt;
}

/**
 * @brief Verification if window close signals are active.
 * @return True if close requested.
 */
bool VulkanRenderer::shouldClose() const {
    return glfwWindowShouldClose(window);
}

bool VulkanRenderer::getKey(int key) const {
    if (!window) return false;
    return glfwGetKey(window, key) == GLFW_PRESS;
}

bool VulkanRenderer::getMouseButton(int button) const {
    if (!window) return false;
    return glfwGetMouseButton(window, button) == GLFW_PRESS;
}

void VulkanRenderer::getMousePosition(double* xpos, double* ypos) const {
    if (window) {
        glfwGetCursorPos(window, xpos, ypos);
    } else {
        if (xpos) *xpos = 0.0;
        if (ypos) *ypos = 0.0;
    }
}

void VulkanRenderer::getWindowSize(int* width, int* height) const {
    if (window) {
        glfwGetWindowSize(window, width, height);
    } else {
        if (width) *width = 0;
        if (height) *height = 0;
    }
}

/**
 * @brief Uploads mesh data to vertex and index buffers.
 * @param meshID ID of target mesh.
 */
void VulkanRenderer::uploadMesh(size_t meshID) {
    auto& vertices = meshSoA.vertices[meshID];
    auto& indices = meshSoA.indices[meshID];

    // Vertex buffer
    if (!vertices.empty()) {
        VkDeviceSize size = vertices.size() * sizeof(Vertex);
        meshSoA.vertexBuffers[meshID].create(
            device.getDevice(),
            device.getPhysicalDevice(),
            size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        meshSoA.vertexBuffers[meshID].uploadData(vertices.data(), size);
    }

    // Index buffer
    if (!indices.empty()) {
        VkDeviceSize size = indices.size() * sizeof(uint32_t);
        meshSoA.indexBuffers[meshID].create(
            device.getDevice(),
            device.getPhysicalDevice(),
            size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        meshSoA.indexBuffers[meshID].uploadData(indices.data(), size);
    }
}
/**
 * @brief Populates validation debug create info parameters.
 * @return debug create info.
 */
VkDebugUtilsMessengerCreateInfoEXT VulkanRenderer::populateDebugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* userData) -> VkBool32 {
            std::cerr << "[Vulkan Validation] " << pCallbackData->pMessage << std::endl;
            return VK_FALSE;
        };
    createInfo.pUserData = nullptr;
    return createInfo;
}
/**
 * @brief Attaches validation logger callbacks.
 */
void VulkanRenderer::setupDebugMessenger() {
    if (!enableValidationLayers) return;

    auto createInfo = populateDebugMessengerCreateInfo();

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        device.getInstance(), "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        if (func(device.getInstance(), &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create debug messenger!");
        }
    }
}
/**
 * @brief Deallocates debug messenger log handle.
 */
void VulkanRenderer::destroyDebugMessenger() {
    if (!enableValidationLayers || debugMessenger == VK_NULL_HANDLE) return;

    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        device.getInstance(), "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(device.getInstance(), debugMessenger, nullptr);
    }
}

/**
 * @brief Retrieves the default white texture descriptor set fallback.
 * @return VkDescriptorSet handle.
 */
VkDescriptorSet VulkanRenderer::getDefaultTextureSet() const {
    if (resourceManager) {
        return resourceManager->getDefaultWhiteTexture()->descriptorSet;
    }
    return VK_NULL_HANDLE;
}

static std::vector<char> loadShaderBytecode(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

PipelineHandle VulkanRenderer::createDepthOnlyPipeline(const std::string& vertPath) {
    VkDevice dev = device.getDevice();
    auto vertCode = loadShaderBytecode(vertPath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = vertCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());

    VkShaderModule vertModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &createInfo, nullptr, &vertModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow vertex shader module for: " + vertPath);
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Dual-sided shadow casting ensures leaves, billboards, and thin geometry cast shadows
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    rasterizer.depthBiasClamp = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0; // Depth-only: 0 color attachments
    colorBlending.pAttachments = nullptr;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(PushConstants);

    std::vector<VkDescriptorSetLayout> setLayouts = {
        descriptors.getCameraDescriptorSetLayout(),
        descriptors.getTextureDescriptorSetLayout(),
        descriptors.getJointsDescriptorSetLayout()
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(dev, vertModule, nullptr);
        throw std::runtime_error("Failed to create depth-only pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &vertStage;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
        vkDestroyShaderModule(dev, vertModule, nullptr);
        throw std::runtime_error("Failed to create depth-only graphics pipeline!");
    }

    vkDestroyShaderModule(dev, vertModule, nullptr);
    return { pipeline, pipelineLayout };
}

void VulkanRenderer::createShadowResources() {
    VkDevice dev = device.getDevice();

    shadowDepthFormat = swapchain.getDepthFormat();
    if (shadowDepthFormat == VK_FORMAT_UNDEFINED) {
        shadowDepthFormat = VK_FORMAT_D32_SFLOAT;
    }

    uint32_t res = m_shadowSettings.resolution;
    if (res != 1024 && res != 2048 && res != 4096) res = 2048;
    m_shadowSettings.resolution = res;

    // 1. Create depth image with TOTAL_SHADOW_LAYERS array layers (4 Sun CSM + 4 Spot lights)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = res;
    imageInfo.extent.height = res;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = TOTAL_SHADOW_LAYERS;
    imageInfo.format = shadowDepthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(dev, &imageInfo, nullptr, &shadowDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow depth image!");
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(dev, shadowDepthImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(dev, &allocInfo, nullptr, &shadowDepthMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate shadow depth memory!");
    }
    vkBindImageMemory(dev, shadowDepthImage, shadowDepthMemory, 0);

    // 2. Create 2D Array depth image view across all layers (cascades + spots) for sampling in shaders
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowDepthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = shadowDepthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = TOTAL_SHADOW_LAYERS;

    if (vkCreateImageView(dev, &viewInfo, nullptr, &shadowDepthView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow depth 2D array image view!");
    }

    // 2b. Create per-layer 2D image views for render targets (0..3 for cascades, 4..7 for spots)
    for (uint32_t i = 0; i < TOTAL_SHADOW_LAYERS; ++i) {
        VkImageViewCreateInfo layerViewInfo{};
        layerViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        layerViewInfo.image = shadowDepthImage;
        layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerViewInfo.format = shadowDepthFormat;
        layerViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        layerViewInfo.subresourceRange.baseMipLevel = 0;
        layerViewInfo.subresourceRange.levelCount = 1;
        layerViewInfo.subresourceRange.baseArrayLayer = i;
        layerViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(dev, &layerViewInfo, nullptr, &shadowLayerViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shadow layer image view!");
        }
    }

    // 3. Create comparison sampler (linear filter + clamp to border with border color 1.0f depth)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // 1.0 depth = unshadowed outside frustum
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.maxAnisotropy = 1.0f;

    if (vkCreateSampler(dev, &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow sampler!");
    }

    // 4. Create shadow render pass
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = shadowDepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pColorAttachments = nullptr;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &depthAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(dev, &rpInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow render pass!");
    }

    // 5. Create per-layer shadow framebuffers
    for (uint32_t i = 0; i < TOTAL_SHADOW_LAYERS; ++i) {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = shadowRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &shadowLayerViews[i];
        fbInfo.width = res;
        fbInfo.height = res;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &shadowLayerFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shadow layer framebuffer!");
        }
    }

    // 6. Create Point Light Omnidirectional Shadow Cubemap (6 faces)
    VkImageCreateInfo cubeImgInfo{};
    cubeImgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    cubeImgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cubeImgInfo.imageType = VK_IMAGE_TYPE_2D;
    cubeImgInfo.extent.width = res;
    cubeImgInfo.extent.height = res;
    cubeImgInfo.extent.depth = 1;
    cubeImgInfo.mipLevels = 1;
    cubeImgInfo.arrayLayers = 6;
    cubeImgInfo.format = shadowDepthFormat;
    cubeImgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    cubeImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    cubeImgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    cubeImgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    cubeImgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(dev, &cubeImgInfo, nullptr, &pointShadowImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point shadow cubemap image!");
    }

    VkMemoryRequirements cubeMemReqs;
    vkGetImageMemoryRequirements(dev, pointShadowImage, &cubeMemReqs);

    VkMemoryAllocateInfo cubeAllocInfo{};
    cubeAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    cubeAllocInfo.allocationSize = cubeMemReqs.size;
    cubeAllocInfo.memoryTypeIndex = device.findMemoryType(cubeMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(dev, &cubeAllocInfo, nullptr, &pointShadowMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate point shadow memory!");
    }
    vkBindImageMemory(dev, pointShadowImage, pointShadowMemory, 0);

    // Cubemap view across all 6 faces for sampling
    VkImageViewCreateInfo cubeViewInfo{};
    cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cubeViewInfo.image = pointShadowImage;
    cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cubeViewInfo.format = shadowDepthFormat;
    cubeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    cubeViewInfo.subresourceRange.baseMipLevel = 0;
    cubeViewInfo.subresourceRange.levelCount = 1;
    cubeViewInfo.subresourceRange.baseArrayLayer = 0;
    cubeViewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(dev, &cubeViewInfo, nullptr, &pointShadowCubeView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point shadow cubemap image view!");
    }

    // Per-face 2D views and framebuffers for rendering each cube face
    for (uint32_t f = 0; f < 6; ++f) {
        VkImageViewCreateInfo faceViewInfo{};
        faceViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        faceViewInfo.image = pointShadowImage;
        faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        faceViewInfo.format = shadowDepthFormat;
        faceViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        faceViewInfo.subresourceRange.baseMipLevel = 0;
        faceViewInfo.subresourceRange.levelCount = 1;
        faceViewInfo.subresourceRange.baseArrayLayer = f;
        faceViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(dev, &faceViewInfo, nullptr, &pointShadowFaceViews[f]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create point shadow face view!");
        }

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = shadowRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &pointShadowFaceViews[f];
        fbInfo.width = res;
        fbInfo.height = res;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &pointShadowFaceFramebuffers[f]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create point shadow face framebuffer!");
        }
    }

    // Transition all shadow images (cascades + spots 2D array and point light cubemap)
    // from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL initially.
    // This ensures any unrendered layers/faces are in a valid layout when sampled by main pass shaders.
    VkCommandBuffer initCmd = beginSingleUseCommands();

    VkImageMemoryBarrier initBarriers[2]{};
    initBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    initBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    initBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    initBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    initBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    initBarriers[0].image = shadowDepthImage;
    initBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    initBarriers[0].subresourceRange.baseMipLevel = 0;
    initBarriers[0].subresourceRange.levelCount = 1;
    initBarriers[0].subresourceRange.baseArrayLayer = 0;
    initBarriers[0].subresourceRange.layerCount = TOTAL_SHADOW_LAYERS;
    initBarriers[0].srcAccessMask = 0;
    initBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    initBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    initBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    initBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    initBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    initBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    initBarriers[1].image = pointShadowImage;
    initBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    initBarriers[1].subresourceRange.baseMipLevel = 0;
    initBarriers[1].subresourceRange.levelCount = 1;
    initBarriers[1].subresourceRange.baseArrayLayer = 0;
    initBarriers[1].subresourceRange.layerCount = 6;
    initBarriers[1].srcAccessMask = 0;
    initBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        initCmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        2, initBarriers
    );

    endSingleUseCommands(initCmd);
}

void VulkanRenderer::destroyShadowResources() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < TOTAL_SHADOW_LAYERS; ++i) {
        if (shadowLayerFramebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(dev, shadowLayerFramebuffers[i], nullptr);
            shadowLayerFramebuffers[i] = VK_NULL_HANDLE;
        }
        if (shadowLayerViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, shadowLayerViews[i], nullptr);
            shadowLayerViews[i] = VK_NULL_HANDLE;
        }
    }
    for (uint32_t f = 0; f < 6; ++f) {
        if (pointShadowFaceFramebuffers[f] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(dev, pointShadowFaceFramebuffers[f], nullptr);
            pointShadowFaceFramebuffers[f] = VK_NULL_HANDLE;
        }
        if (pointShadowFaceViews[f] != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, pointShadowFaceViews[f], nullptr);
            pointShadowFaceViews[f] = VK_NULL_HANDLE;
        }
    }
    if (pointShadowCubeView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, pointShadowCubeView, nullptr);
        pointShadowCubeView = VK_NULL_HANDLE;
    }
    if (pointShadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(dev, pointShadowImage, nullptr);
        pointShadowImage = VK_NULL_HANDLE;
    }
    if (pointShadowMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, pointShadowMemory, nullptr);
        pointShadowMemory = VK_NULL_HANDLE;
    }
    if (shadowRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, shadowRenderPass, nullptr);
        shadowRenderPass = VK_NULL_HANDLE;
    }
    if (shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(dev, shadowSampler, nullptr);
        shadowSampler = VK_NULL_HANDLE;
    }
    if (shadowDepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, shadowDepthView, nullptr);
        shadowDepthView = VK_NULL_HANDLE;
    }
    if (shadowDepthImage != VK_NULL_HANDLE) {
        vkDestroyImage(dev, shadowDepthImage, nullptr);
        shadowDepthImage = VK_NULL_HANDLE;
    }
    if (shadowDepthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, shadowDepthMemory, nullptr);
        shadowDepthMemory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::recreateShadowResources(uint32_t newResolution) {
    if (newResolution != 1024 && newResolution != 2048 && newResolution != 4096) {
        newResolution = 2048;
    }
    if (newResolution == m_shadowSettings.resolution && shadowDepthImage != VK_NULL_HANDLE) {
        return;
    }
    if (device.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.getDevice());
    }

    destroyShadowPipelines();
    destroyShadowResources();

    m_shadowSettings.resolution = newResolution;
    createShadowResources();
    createShadowPipelines();

    descriptors.updateShadowDescriptor(shadowDepthView, shadowSampler, pointShadowCubeView);
}

void VulkanRenderer::createShadowPipelines() {
    std::string shadowVert = resolveShaderPath("build/shaders/shadow.vert.spv");
    m_shadowPipeline = createDepthOnlyPipeline(shadowVert);

    std::string skinnedShadowVert = resolveShaderPath("build/shaders/skinned_shadow.vert.spv");
    m_skinnedShadowPipeline = createDepthOnlyPipeline(skinnedShadowVert);
}

void VulkanRenderer::destroyShadowPipelines() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    if (m_shadowPipeline.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_shadowPipeline.pipeline, nullptr);
        vkDestroyPipelineLayout(dev, m_shadowPipeline.layout, nullptr);
        m_shadowPipeline = {};
    }
    if (m_skinnedShadowPipeline.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_skinnedShadowPipeline.pipeline, nullptr);
        vkDestroyPipelineLayout(dev, m_skinnedShadowPipeline.layout, nullptr);
        m_skinnedShadowPipeline = {};
    }
}

void VulkanRenderer::createHDRResources() {
    VkDevice dev = device.getDevice();
    m_hdrExtent = calculateHDRExtent();

    // 1. Create HDR Render Pass (reusable across resizes)
    if (m_hdrRenderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkFormat depthFmt = swapchain.getDepthFormat();
        if (depthFmt == VK_FORMAT_UNDEFINED) depthFmt = VK_FORMAT_D32_SFLOAT;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFmt;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_GENERAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        rpInfo.pAttachments = attachments.data();
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        rpInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(dev, &rpInfo, nullptr, &m_hdrRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create HDR render pass!");
        }
    }

    // 2. Create HDR Sampler (reusable across resizes)
    if (m_hdrSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        if (vkCreateSampler(dev, &samplerInfo, nullptr, &m_hdrSampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create HDR sampler!");
        }
    }

    // 3. Create HDR color image at m_hdrExtent
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent.width = m_hdrExtent.width;
    imgInfo.extent.height = m_hdrExtent.height;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(dev, &imgInfo, nullptr, &m_hdrColorImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HDR color image!");
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(dev, m_hdrColorImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(dev, &allocInfo, nullptr, &m_hdrColorMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate HDR color image memory!");
    }
    vkBindImageMemory(dev, m_hdrColorImage, m_hdrColorMemory, 0);

    // 4. Create HDR color image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_hdrColorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(dev, &viewInfo, nullptr, &m_hdrColorImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HDR color image view!");
    }

    // 5. Create HDR Depth Image at m_hdrExtent (independent of swapchain resolution)
    VkFormat depthFmt = swapchain.getDepthFormat();
    if (depthFmt == VK_FORMAT_UNDEFINED) depthFmt = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo depthImgInfo{};
    depthImgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImgInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImgInfo.extent.width = m_hdrExtent.width;
    depthImgInfo.extent.height = m_hdrExtent.height;
    depthImgInfo.extent.depth = 1;
    depthImgInfo.mipLevels = 1;
    depthImgInfo.arrayLayers = 1;
    depthImgInfo.format = depthFmt;
    depthImgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthImgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(dev, &depthImgInfo, nullptr, &m_hdrDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HDR depth image!");
    }

    VkMemoryRequirements dMemReqs;
    vkGetImageMemoryRequirements(dev, m_hdrDepthImage, &dMemReqs);

    VkMemoryAllocateInfo dAllocInfo{};
    dAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    dAllocInfo.allocationSize = dMemReqs.size;
    dAllocInfo.memoryTypeIndex = device.findMemoryType(dMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(dev, &dAllocInfo, nullptr, &m_hdrDepthMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate HDR depth image memory!");
    }
    vkBindImageMemory(dev, m_hdrDepthImage, m_hdrDepthMemory, 0);

    VkImageViewCreateInfo dViewInfo{};
    dViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dViewInfo.image = m_hdrDepthImage;
    dViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dViewInfo.format = depthFmt;
    dViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dViewInfo.subresourceRange.baseMipLevel = 0;
    dViewInfo.subresourceRange.levelCount = 1;
    dViewInfo.subresourceRange.baseArrayLayer = 0;
    dViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(dev, &dViewInfo, nullptr, &m_hdrDepthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HDR depth image view!");
    }

    // 6. Create HDR framebuffer
    std::array<VkImageView, 2> attachments = {
        m_hdrColorImageView,
        m_hdrDepthImageView
    };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_hdrRenderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = m_hdrExtent.width;
    fbInfo.height = m_hdrExtent.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &m_hdrFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HDR framebuffer!");
    }

    m_finalPostProcessView = m_hdrColorImageView;
}

void VulkanRenderer::destroyHDRResources() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    if (m_hdrFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(dev, m_hdrFramebuffer, nullptr);
        m_hdrFramebuffer = VK_NULL_HANDLE;
    }
    if (m_hdrDepthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, m_hdrDepthImageView, nullptr);
        m_hdrDepthImageView = VK_NULL_HANDLE;
    }
    if (m_hdrDepthImage != VK_NULL_HANDLE) {
        vkDestroyImage(dev, m_hdrDepthImage, nullptr);
        m_hdrDepthImage = VK_NULL_HANDLE;
    }
    if (m_hdrDepthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_hdrDepthMemory, nullptr);
        m_hdrDepthMemory = VK_NULL_HANDLE;
    }
    if (m_hdrColorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, m_hdrColorImageView, nullptr);
        m_hdrColorImageView = VK_NULL_HANDLE;
    }
    if (m_hdrColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(dev, m_hdrColorImage, nullptr);
        m_hdrColorImage = VK_NULL_HANDLE;
    }
    if (m_hdrColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_hdrColorMemory, nullptr);
        m_hdrColorMemory = VK_NULL_HANDLE;
    }
    m_lastTonemapSrcView = VK_NULL_HANDLE;
    m_lastTonemapSampler = VK_NULL_HANDLE;
}

void VulkanRenderer::createTonemapPipeline() {
    VkDevice dev = device.getDevice();

    // 1. Descriptor Set Layout (Binding 0: Combined Image Sampler)
    if (m_tonemapDescriptorLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_tonemapDescriptorLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create tonemap descriptor set layout!");
        }
    }

    // 2. Descriptor Pool & Set
    if (m_tonemapDescriptorPool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_tonemapDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create tonemap descriptor pool!");
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_tonemapDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_tonemapDescriptorLayout;

        if (vkAllocateDescriptorSets(dev, &allocInfo, &m_tonemapDescriptorSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate tonemap descriptor set!");
        }
    }

    // 3. Update descriptor set with HDR image view & sampler
    updateTonemapDescriptor();

    // 4. Create Pipeline Layout with Push Constant
    if (m_tonemapPipelineLayout == VK_NULL_HANDLE) {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = 32; // sizeof(TonemapPushConstants)

        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_tonemapDescriptorLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;

        if (vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_tonemapPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create tonemap pipeline layout!");
        }
    }

    // 5. Create Graphics Pipeline
    std::string vertPath = resolveShaderPath("build/shaders/tonemap.vert.spv");
    std::string fragPath = resolveShaderPath("build/shaders/tonemap.frag.spv");

    auto vertCode = loadShaderBytecode(vertPath);
    auto fragCode = loadShaderBytecode(fragPath);

    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo modInfo{};
    modInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    modInfo.codeSize = vertCode.size();
    modInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    if (vkCreateShaderModule(dev, &modInfo, nullptr, &vertModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tonemap vertex shader module!");
    }

    modInfo.codeSize = fragCode.size();
    modInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    if (vkCreateShaderModule(dev, &modInfo, nullptr, &fragModule) != VK_SUCCESS) {
        vkDestroyShaderModule(dev, vertModule, nullptr);
        throw std::runtime_error("Failed to create tonemap fragment shader module!");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &rasterizer;
    pipeInfo.pMultisampleState = &multisampling;
    pipeInfo.pDepthStencilState = &depthStencil;
    pipeInfo.pColorBlendState = &colorBlending;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = m_tonemapPipelineLayout;
    pipeInfo.renderPass = swapchain.getRenderPass();
    pipeInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_tonemapPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(dev, vertModule, nullptr);
        vkDestroyShaderModule(dev, fragModule, nullptr);
        throw std::runtime_error("Failed to create tonemap graphics pipeline!");
    }

    vkDestroyShaderModule(dev, vertModule, nullptr);
    vkDestroyShaderModule(dev, fragModule, nullptr);
}

void VulkanRenderer::destroyTonemapPipeline() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    if (m_tonemapPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_tonemapPipeline, nullptr);
        m_tonemapPipeline = VK_NULL_HANDLE;
    }
    if (m_tonemapPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_tonemapPipelineLayout, nullptr);
        m_tonemapPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_tonemapDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, m_tonemapDescriptorPool, nullptr);
        m_tonemapDescriptorPool = VK_NULL_HANDLE;
        m_tonemapDescriptorSet = VK_NULL_HANDLE;
    }
    if (m_tonemapDescriptorLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, m_tonemapDescriptorLayout, nullptr);
        m_tonemapDescriptorLayout = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::updateTonemapDescriptor() {
    if (m_tonemapDescriptorSet == VK_NULL_HANDLE) {
        return;
    }
    VkImageView srcView = (m_finalPostProcessView != VK_NULL_HANDLE) ? m_finalPostProcessView : m_hdrColorImageView;
    if (srcView == VK_NULL_HANDLE) return;

    VkSampler samplerToUse = (m_postProcessSettings.nearestNeighborUpscale && m_nearestSampler != VK_NULL_HANDLE)
        ? m_nearestSampler
        : m_hdrSampler;
    if (samplerToUse == VK_NULL_HANDLE) return;

    if (m_lastTonemapSrcView == srcView && m_lastTonemapSampler == samplerToUse) {
        return;
    }
    m_lastTonemapSrcView = srcView;
    m_lastTonemapSampler = samplerToUse;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = samplerToUse;
    imageInfo.imageView = srcView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_tonemapDescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device.getDevice(), 1, &write, 0, nullptr);
}

VkExtent2D VulkanRenderer::calculateHDRExtent() const {
    VkExtent2D swapExtent = swapchain.getExtent();
    if (m_postProcessSettings.useFixedResolution && m_postProcessSettings.targetWidth > 0 && m_postProcessSettings.targetHeight > 0) {
        return VkExtent2D{
            static_cast<uint32_t>(m_postProcessSettings.targetWidth),
            static_cast<uint32_t>(m_postProcessSettings.targetHeight)
        };
    }
    float scale = std::clamp(m_postProcessSettings.renderScale, 0.1f, 4.0f);
    return VkExtent2D{
        std::max(1u, static_cast<uint32_t>(swapExtent.width * scale)),
        std::max(1u, static_cast<uint32_t>(swapExtent.height * scale))
    };
}

void VulkanRenderer::createPostProcessResources() {
    VkDevice dev = device.getDevice();

    // 1. Create Nearest Sampler (for sharp pixelated upscaling)
    if (m_nearestSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo nearestInfo{};
        nearestInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        nearestInfo.magFilter = VK_FILTER_NEAREST;
        nearestInfo.minFilter = VK_FILTER_NEAREST;
        nearestInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        nearestInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        nearestInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        nearestInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        nearestInfo.maxAnisotropy = 1.0f;
        nearestInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        if (vkCreateSampler(dev, &nearestInfo, nullptr, &m_nearestSampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create nearest sampler for post-processing!");
        }
    }

    // 2. Create Post-Process Render Pass (single color attachment R16G16B16A16_SFLOAT)
    if (m_postProcessRenderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        rpInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(dev, &rpInfo, nullptr, &m_postProcessRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process render pass!");
        }
    }

    // 3. Create Post-Process Descriptor Set Layout (Binding 0: Combined Image Sampler)
    if (m_postProcessDescLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_postProcessDescLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process descriptor set layout!");
        }
    }

    // 4. Create Post-Process Pipeline Layout (with push constants)
    if (m_postProcessPipelineLayout == VK_NULL_HANDLE) {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(Engine::PostProcessPushConstants);

        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_postProcessDescLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;

        if (vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_postProcessPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process pipeline layout!");
        }
    }

    // 5. Create Descriptor Pool (for HDR view and 2 ping-pong targets)
    if (m_postProcessDescPool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 8;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 8;

        if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_postProcessDescPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process descriptor pool!");
        }

        std::array<VkDescriptorSetLayout, 3> layouts = {
            m_postProcessDescLayout,
            m_postProcessDescLayout,
            m_postProcessDescLayout
        };
        VkDescriptorSet sets[3];
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_postProcessDescPool;
        allocInfo.descriptorSetCount = 3;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(dev, &allocInfo, sets) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate post-process descriptor sets!");
        }
        m_hdrDescriptorSet = sets[0];
        m_postProcessTargets[0].descriptorSet = sets[1];
        m_postProcessTargets[1].descriptorSet = sets[2];
    }

    // 6. Create Ping-Pong Targets at m_hdrExtent
    for (uint32_t i = 0; i < 2; ++i) {
        auto& target = m_postProcessTargets[i];

        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.extent.width = m_hdrExtent.width;
        imgInfo.extent.height = m_hdrExtent.height;
        imgInfo.extent.depth = 1;
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(dev, &imgInfo, nullptr, &target.image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process target image!");
        }

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(dev, target.image, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = device.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(dev, &allocInfo, nullptr, &target.memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate post-process target memory!");
        }
        vkBindImageMemory(dev, target.image, target.memory, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = target.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(dev, &viewInfo, nullptr, &target.view) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process target image view!");
        }

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_postProcessRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &target.view;
        fbInfo.width = m_hdrExtent.width;
        fbInfo.height = m_hdrExtent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &target.framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process framebuffer!");
        }
    }

    // 7. Update Descriptor Sets for sampling HDR image and ping-pong targets
    std::array<VkImageView, 3> views = {
        m_hdrColorImageView,
        m_postProcessTargets[0].view,
        m_postProcessTargets[1].view
    };
    std::array<VkDescriptorSet, 3> sets = {
        m_hdrDescriptorSet,
        m_postProcessTargets[0].descriptorSet,
        m_postProcessTargets[1].descriptorSet
    };

    for (size_t i = 0; i < 3; ++i) {
        if (views[i] == VK_NULL_HANDLE || sets[i] == VK_NULL_HANDLE) continue;

        VkDescriptorImageInfo dImgInfo{};
        dImgInfo.sampler = m_hdrSampler;
        dImgInfo.imageView = views[i];
        dImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &dImgInfo;

        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
    }

    // 8. Pre-populate built-in passes if empty
    if (m_postProcessPasses.empty()) {
        Engine::PostProcessPass pixPass{};
        pixPass.name = "Pixelation";
        pixPass.shaderPath = "assets/shaders/pixelate.frag";
        pixPass.enabled = false;
        pixPass.pushConstants.params0.x = 4.0f; // default 4px pixel block
        pixPass.pipeline = createPostProcessPipeline(pixPass.shaderPath);
        m_postProcessPasses.push_back(pixPass);

        Engine::PostProcessPass grayPass{};
        grayPass.name = "Grayscale";
        grayPass.shaderPath = "assets/shaders/grayscale.frag";
        grayPass.enabled = false;
        grayPass.pushConstants.params0.x = 1.0f; // 100% grayscale factor
        grayPass.pipeline = createPostProcessPipeline(grayPass.shaderPath);
        m_postProcessPasses.push_back(grayPass);
    }

    // 9. Screen-Space Ambient Occlusion (SSAO) target & descriptors matching m_hdrExtent
    createSSAOResources();
}

void VulkanRenderer::destroyPostProcessResources() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    destroySSAOResources();

    for (auto& target : m_postProcessTargets) {
        if (target.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(dev, target.framebuffer, nullptr);
            target.framebuffer = VK_NULL_HANDLE;
        }
        if (target.view != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, target.view, nullptr);
            target.view = VK_NULL_HANDLE;
        }
        if (target.image != VK_NULL_HANDLE) {
            vkDestroyImage(dev, target.image, nullptr);
            target.image = VK_NULL_HANDLE;
        }
        if (target.memory != VK_NULL_HANDLE) {
            vkFreeMemory(dev, target.memory, nullptr);
            target.memory = VK_NULL_HANDLE;
        }
    }

    if (m_postProcessDescPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, m_postProcessDescPool, nullptr);
        m_postProcessDescPool = VK_NULL_HANDLE;
        m_hdrDescriptorSet = VK_NULL_HANDLE;
        m_postProcessTargets[0].descriptorSet = VK_NULL_HANDLE;
        m_postProcessTargets[1].descriptorSet = VK_NULL_HANDLE;
    }
    m_lastTonemapSrcView = VK_NULL_HANDLE;
    m_lastTonemapSampler = VK_NULL_HANDLE;
}

VkPipeline VulkanRenderer::createPostProcessPipeline(const std::string& fragPath, bool forceRecompile) {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE || m_postProcessPipelineLayout == VK_NULL_HANDLE || m_postProcessRenderPass == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    std::string vertPath = resolveShaderPath("build/shaders/tonemap.vert.spv");
    std::string resolvedFrag = Engine::ShaderCompiler::resolveOrCompile(fragPath, forceRecompile);
    if (resolvedFrag.empty() || (resolvedFrag.size() >= 5 && resolvedFrag.substr(resolvedFrag.size() - 5) == ".frag")) {
        std::filesystem::path p(fragPath);
        std::string fname = p.filename().string();
        std::string testSpv = resolveShaderPath("build/shaders/" + fname + ".spv");
        if (testSpv.size() >= 4 && testSpv.substr(testSpv.size() - 4) == ".spv" && std::filesystem::exists(testSpv)) {
            resolvedFrag = testSpv;
        } else {
            testSpv = resolveShaderPath(fragPath + ".spv");
            if (std::filesystem::exists(testSpv)) {
                resolvedFrag = testSpv;
            }
        }
    }

    if (resolvedFrag.empty() || (resolvedFrag.size() >= 5 && resolvedFrag.substr(resolvedFrag.size() - 5) == ".frag")) {
        std::cerr << "[VulkanRenderer] Error: Could not resolve or compile post-process SPIR-V for: " << fragPath << std::endl;
        return VK_NULL_HANDLE;
    }

    std::vector<char> vertCode;
    std::vector<char> fragCode;
    try {
        vertCode = loadShaderBytecode(vertPath);
        fragCode = loadShaderBytecode(resolvedFrag);
    } catch (const std::exception& e) {
        std::cerr << "[VulkanRenderer] Failed to load bytecode for post-process pass (" << fragPath << "): " << e.what() << std::endl;
        return VK_NULL_HANDLE;
    }

    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo modInfo{};
    modInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    modInfo.codeSize = vertCode.size();
    modInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    if (vkCreateShaderModule(dev, &modInfo, nullptr, &vertModule) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] Failed to create vertex shader module for post-process!" << std::endl;
        return VK_NULL_HANDLE;
    }

    modInfo.codeSize = fragCode.size();
    modInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    if (vkCreateShaderModule(dev, &modInfo, nullptr, &fragModule) != VK_SUCCESS) {
        vkDestroyShaderModule(dev, vertModule, nullptr);
        std::cerr << "[VulkanRenderer] Failed to create fragment shader module for: " << fragPath << std::endl;
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &rasterizer;
    pipeInfo.pMultisampleState = &multisampling;
    pipeInfo.pDepthStencilState = &depthStencil;
    pipeInfo.pColorBlendState = &colorBlending;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = m_postProcessPipelineLayout;
    pipeInfo.renderPass = m_postProcessRenderPass;
    pipeInfo.subpass = 0;

    VkPipeline pipelineHandle = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipelineHandle) != VK_SUCCESS) {
        vkDestroyShaderModule(dev, vertModule, nullptr);
        vkDestroyShaderModule(dev, fragModule, nullptr);
        std::cerr << "[VulkanRenderer] Failed to build post-process graphics pipeline!" << std::endl;
        return VK_NULL_HANDLE;
    }

    vkDestroyShaderModule(dev, vertModule, nullptr);
    vkDestroyShaderModule(dev, fragModule, nullptr);

    return pipelineHandle;
}

void VulkanRenderer::addPostProcessPass(const std::string& name, const std::string& shaderPath, bool enabled) {
    Engine::PostProcessPass pass{};
    pass.name = name;
    pass.shaderPath = shaderPath;
    pass.enabled = enabled;
    pass.pipeline = createPostProcessPipeline(shaderPath);
    m_postProcessPasses.push_back(pass);
}

void VulkanRenderer::removePostProcessPass(size_t index) {
    if (index >= m_postProcessPasses.size()) return;
    if (m_postProcessPasses[index].pipeline != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.getDevice());
        vkDestroyPipeline(device.getDevice(), m_postProcessPasses[index].pipeline, nullptr);
    }
    m_postProcessPasses.erase(m_postProcessPasses.begin() + index);
}

void VulkanRenderer::clearPostProcessPasses() {
    if (device.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.getDevice());
    }
    for (auto& pass : m_postProcessPasses) {
        if (pass.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device.getDevice(), pass.pipeline, nullptr);
            pass.pipeline = VK_NULL_HANDLE;
        }
    }
    m_postProcessPasses.clear();
}

bool VulkanRenderer::reloadPostProcessShaders() {
    if (device.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.getDevice());
    }
    bool allSuccess = true;
    for (auto& pass : m_postProcessPasses) {
        VkPipeline newPipe = createPostProcessPipeline(pass.shaderPath, true);
        if (newPipe != VK_NULL_HANDLE) {
            if (pass.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device.getDevice(), pass.pipeline, nullptr);
            }
            pass.pipeline = newPipe;
            std::cout << "[VulkanRenderer] Successfully reloaded post-process pass: " << pass.name << std::endl;
        } else {
            std::cerr << "[VulkanRenderer] Failed to recompile post-process pass: " << pass.name << std::endl;
            allSuccess = false;
        }
    }
    return allSuccess;
}

void VulkanRenderer::setPostProcessSettings(const Engine::PostProcessSettings& settings) {
    bool extentChanged = false;
    if (settings.renderScale != m_postProcessSettings.renderScale ||
        settings.useFixedResolution != m_postProcessSettings.useFixedResolution ||
        settings.targetWidth != m_postProcessSettings.targetWidth ||
        settings.targetHeight != m_postProcessSettings.targetHeight) {
        extentChanged = true;
    }
    bool filterChanged = (settings.nearestNeighborUpscale != m_postProcessSettings.nearestNeighborUpscale);

    m_postProcessSettings = settings;

    if (extentChanged && device.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.getDevice());
        destroyPostProcessResources();
        destroyHDRResources();
        createHDRResources();
        createPostProcessResources();
        updateTonemapDescriptor();
    } else if (filterChanged) {
        updateTonemapDescriptor();
    }
}

void VulkanRenderer::renderPostProcessChain(VkCommandBuffer cmd) {
    m_finalPostProcessView = m_hdrColorImageView;

    VkDescriptorSet currentSourceSet = m_hdrDescriptorSet;
    uint32_t targetIndex = 0;

    // --- Screen-Space Ambient Occlusion (SSAO) ---
    if (m_ssaoSettings.enabled && m_ssaoPipeline != VK_NULL_HANDLE && m_ssaoBlurPipeline != VK_NULL_HANDLE) {
        renderSSAOPass(cmd);
        renderSSAOBlurPass(cmd);

        currentSourceSet = m_postProcessTargets[0].descriptorSet;
        m_finalPostProcessView = m_postProcessTargets[0].view;
        targetIndex = 1;
    }

    if (!m_postProcessSettings.enabled || m_postProcessPasses.empty()) {
        return;
    }

    std::vector<size_t> activePassIndices;
    for (size_t i = 0; i < m_postProcessPasses.size(); ++i) {
        if (m_postProcessPasses[i].enabled) {
            if (m_postProcessPasses[i].pipeline == VK_NULL_HANDLE) {
                m_postProcessPasses[i].pipeline = createPostProcessPipeline(m_postProcessPasses[i].shaderPath);
            }
            if (m_postProcessPasses[i].pipeline != VK_NULL_HANDLE) {
                activePassIndices.push_back(i);
            }
        }
    }

    if (activePassIndices.empty()) {
        return;
    }

    for (size_t passIdx : activePassIndices) {
        auto& pass = m_postProcessPasses[passIdx];
        auto& target = m_postProcessTargets[targetIndex];

        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = m_postProcessRenderPass;
        rpInfo.framebuffer = target.framebuffer;
        rpInfo.renderArea.offset = { 0, 0 };
        rpInfo.renderArea.extent = m_hdrExtent;
        rpInfo.clearValueCount = 0;
        rpInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_hdrExtent.width);
        viewport.height = static_cast<float>(m_hdrExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_hdrExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcessPipelineLayout, 0, 1, &currentSourceSet, 0, nullptr);

        Engine::PostProcessPushConstants pc = pass.pushConstants;
        pc.resolution = glm::vec4(
            static_cast<float>(m_hdrExtent.width),
            static_cast<float>(m_hdrExtent.height),
            1.0f / static_cast<float>(m_hdrExtent.width),
            1.0f / static_cast<float>(m_hdrExtent.height)
        );

        vkCmdPushConstants(cmd, m_postProcessPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Engine::PostProcessPushConstants), &pc);

        // Procedural fullscreen triangle: 3 vertices, 1 instance
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);

        // Advance ping-pong state
        currentSourceSet = target.descriptorSet;
        m_finalPostProcessView = target.view;
        targetIndex = 1 - targetIndex;
    }
}

// -------------------------------------------------------------
// Screen-Space Ambient Occlusion (SSAO) Implementation
// -------------------------------------------------------------

void VulkanRenderer::createSSAOResources() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    // 1. Dedicated Clamp-to-Edge Nearest Sampler for Depth Sampling
    if (m_depthSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo sampInfo{};
        sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampInfo.magFilter = VK_FILTER_NEAREST;
        sampInfo.minFilter = VK_FILTER_NEAREST;
        sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampInfo.maxAnisotropy = 1.0f;
        sampInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        if (vkCreateSampler(dev, &sampInfo, nullptr, &m_depthSampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth sampler for SSAO!");
        }
    }

    // 2. SSAO Single-Channel (R8_UNORM) Render Pass
    if (m_ssaoRenderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R8_UNORM;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        rpInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(dev, &rpInfo, nullptr, &m_ssaoRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSAO render pass!");
        }
    }

    // 3. SSAO Target Image (R8_UNORM)
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent.width = m_hdrExtent.width;
    imgInfo.extent.height = m_hdrExtent.height;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_R8_UNORM;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(dev, &imgInfo, nullptr, &m_ssaoImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO target image!");
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(dev, m_ssaoImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(dev, &allocInfo, nullptr, &m_ssaoMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO target image memory!");
    }
    vkBindImageMemory(dev, m_ssaoImage, m_ssaoMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_ssaoImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(dev, &viewInfo, nullptr, &m_ssaoImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO target image view!");
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_ssaoRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &m_ssaoImageView;
    fbInfo.width = m_hdrExtent.width;
    fbInfo.height = m_hdrExtent.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &m_ssaoFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO framebuffer!");
    }

    // 4. Create SSAO UBO buffer and precompute hemisphere kernel
    if (m_ssaoUBOBuffer.get() == VK_NULL_HANDLE) {
        m_ssaoUBOBuffer.create(
            device.getDevice(),
            device.getPhysicalDevice(),
            sizeof(SSAOUBOData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
        std::default_random_engine generator(1337);

        for (int i = 0; i < 32; ++i) {
            glm::vec3 sample(
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 0.95f + 0.05f
            );
            sample = glm::normalize(sample);
            sample *= randomFloats(generator);

            float scale = float(i) / 32.0f;
            scale = glm::mix(0.1f, 1.0f, scale * scale);
            sample *= scale;

            m_ssaoUBOData.samples[i] = glm::vec4(sample, 0.0f);
        }
    }

    updateSSAODescriptors();
}

void VulkanRenderer::destroySSAOResources() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    if (m_ssaoFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(dev, m_ssaoFramebuffer, nullptr);
        m_ssaoFramebuffer = VK_NULL_HANDLE;
    }
    if (m_ssaoImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, m_ssaoImageView, nullptr);
        m_ssaoImageView = VK_NULL_HANDLE;
    }
    if (m_ssaoImage != VK_NULL_HANDLE) {
        vkDestroyImage(dev, m_ssaoImage, nullptr);
        m_ssaoImage = VK_NULL_HANDLE;
    }
    if (m_ssaoMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_ssaoMemory, nullptr);
        m_ssaoMemory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::createSSAOPipelines() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE || m_ssaoPipeline != VK_NULL_HANDLE) return;

    // --- 1. SSAO Generation Pipeline Setup ---
    if (m_ssaoDescLayout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_ssaoDescLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSAO descriptor set layout!");
        }
    }

    if (m_ssaoDescPool == VK_NULL_HANDLE) {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 4;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = 4;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 4;

        if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_ssaoDescPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSAO descriptor pool!");
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_ssaoDescPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_ssaoDescLayout;

        if (vkAllocateDescriptorSets(dev, &allocInfo, &m_ssaoDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate SSAO descriptor set!");
        }
    }

    if (m_ssaoPipelineLayout == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_ssaoDescLayout;
        plInfo.pushConstantRangeCount = 0;

        if (vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_ssaoPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSAO pipeline layout!");
        }
    }

    // --- 2. SSAO Blur & Composite Pipeline Setup ---
    if (m_ssaoBlurDescLayout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        for (uint32_t b = 0; b < 3; ++b) {
            bindings[b].binding = b;
            bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[b].descriptorCount = 1;
            bindings[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_ssaoBlurDescLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSAO blur descriptor set layout!");
        }
    }

    if (m_ssaoBlurDescPool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 8;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 4;

        if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_ssaoBlurDescPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSAO blur descriptor pool!");
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_ssaoBlurDescPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_ssaoBlurDescLayout;

        if (vkAllocateDescriptorSets(dev, &allocInfo, &m_ssaoBlurDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate SSAO blur descriptor set!");
        }
    }

    if (m_ssaoBlurPipelineLayout == VK_NULL_HANDLE) {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(glm::vec4) * 2; // 32 bytes

        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_ssaoBlurDescLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;

        if (vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_ssaoBlurPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSAO blur pipeline layout!");
        }
    }

    // --- 3. Build Graphics Pipelines ---
    std::string vertPath = resolveShaderPath("build/shaders/tonemap.vert.spv");
    std::string ssaoFragPath = resolveShaderPath("build/shaders/ssao.frag.spv");
    std::string blurFragPath = resolveShaderPath("build/shaders/ssao_blur.frag.spv");

    auto vertCode = loadShaderBytecode(vertPath);
    auto ssaoFragCode = loadShaderBytecode(ssaoFragPath);
    auto blurFragCode = loadShaderBytecode(blurFragPath);

    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule ssaoFragModule = VK_NULL_HANDLE;
    VkShaderModule blurFragModule = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo modInfo{};
    modInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    modInfo.codeSize = vertCode.size();
    modInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    vkCreateShaderModule(dev, &modInfo, nullptr, &vertModule);

    modInfo.codeSize = ssaoFragCode.size();
    modInfo.pCode = reinterpret_cast<const uint32_t*>(ssaoFragCode.data());
    vkCreateShaderModule(dev, &modInfo, nullptr, &ssaoFragModule);

    modInfo.codeSize = blurFragCode.size();
    modInfo.pCode = reinterpret_cast<const uint32_t*>(blurFragCode.data());
    vkCreateShaderModule(dev, &modInfo, nullptr, &blurFragModule);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // SSAO Generation Pipeline
    VkPipelineShaderStageCreateInfo ssaoStages[2]{};
    ssaoStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ssaoStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    ssaoStages[0].module = vertModule;
    ssaoStages[0].pName = "main";
    ssaoStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ssaoStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    ssaoStages[1].module = ssaoFragModule;
    ssaoStages[1].pName = "main";

    VkGraphicsPipelineCreateInfo ssaoPipeInfo{};
    ssaoPipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ssaoPipeInfo.stageCount = 2;
    ssaoPipeInfo.pStages = ssaoStages;
    ssaoPipeInfo.pVertexInputState = &vertexInput;
    ssaoPipeInfo.pInputAssemblyState = &inputAssembly;
    ssaoPipeInfo.pViewportState = &viewportState;
    ssaoPipeInfo.pRasterizationState = &rasterizer;
    ssaoPipeInfo.pMultisampleState = &multisampling;
    ssaoPipeInfo.pDepthStencilState = &depthStencil;
    ssaoPipeInfo.pColorBlendState = &colorBlending;
    ssaoPipeInfo.pDynamicState = &dynamicState;
    ssaoPipeInfo.layout = m_ssaoPipelineLayout;
    ssaoPipeInfo.renderPass = m_ssaoRenderPass;
    ssaoPipeInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ssaoPipeInfo, nullptr, &m_ssaoPipeline) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] Failed to build SSAO generation pipeline!" << std::endl;
    }

    // SSAO Blur & Composite Pipeline
    VkPipelineShaderStageCreateInfo blurStages[2]{};
    blurStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    blurStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    blurStages[0].module = vertModule;
    blurStages[0].pName = "main";
    blurStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    blurStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    blurStages[1].module = blurFragModule;
    blurStages[1].pName = "main";

    VkGraphicsPipelineCreateInfo blurPipeInfo = ssaoPipeInfo;
    blurPipeInfo.pStages = blurStages;
    blurPipeInfo.layout = m_ssaoBlurPipelineLayout;
    blurPipeInfo.renderPass = m_postProcessRenderPass;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &blurPipeInfo, nullptr, &m_ssaoBlurPipeline) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] Failed to build SSAO blur pipeline!" << std::endl;
    }

    vkDestroyShaderModule(dev, vertModule, nullptr);
    vkDestroyShaderModule(dev, ssaoFragModule, nullptr);
    vkDestroyShaderModule(dev, blurFragModule, nullptr);

    updateSSAODescriptors();
}

void VulkanRenderer::destroySSAOPipelines() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    if (m_ssaoPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_ssaoPipeline, nullptr);
        m_ssaoPipeline = VK_NULL_HANDLE;
    }
    if (m_ssaoPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_ssaoPipelineLayout, nullptr);
        m_ssaoPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_ssaoDescPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, m_ssaoDescPool, nullptr);
        m_ssaoDescPool = VK_NULL_HANDLE;
        m_ssaoDescSet = VK_NULL_HANDLE;
    }
    if (m_ssaoDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, m_ssaoDescLayout, nullptr);
        m_ssaoDescLayout = VK_NULL_HANDLE;
    }

    if (m_ssaoBlurPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_ssaoBlurPipeline, nullptr);
        m_ssaoBlurPipeline = VK_NULL_HANDLE;
    }
    if (m_ssaoBlurPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_ssaoBlurPipelineLayout, nullptr);
        m_ssaoBlurPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_ssaoBlurDescPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, m_ssaoBlurDescPool, nullptr);
        m_ssaoBlurDescPool = VK_NULL_HANDLE;
        m_ssaoBlurDescSet = VK_NULL_HANDLE;
    }
    if (m_ssaoBlurDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, m_ssaoBlurDescLayout, nullptr);
        m_ssaoBlurDescLayout = VK_NULL_HANDLE;
    }

    if (m_ssaoRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, m_ssaoRenderPass, nullptr);
        m_ssaoRenderPass = VK_NULL_HANDLE;
    }
    if (m_depthSampler != VK_NULL_HANDLE) {
        vkDestroySampler(dev, m_depthSampler, nullptr);
        m_depthSampler = VK_NULL_HANDLE;
    }

    m_ssaoUBOBuffer.destroy();
}

void VulkanRenderer::updateSSAODescriptors() {
    VkDevice dev = device.getDevice();
    if (dev == VK_NULL_HANDLE) return;

    if (m_ssaoDescSet != VK_NULL_HANDLE && m_hdrDepthImageView != VK_NULL_HANDLE && m_depthSampler != VK_NULL_HANDLE && m_ssaoUBOBuffer.get() != VK_NULL_HANDLE) {
        VkDescriptorImageInfo dImgInfo{};
        dImgInfo.sampler = m_depthSampler;
        dImgInfo.imageView = m_hdrDepthImageView;
        dImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorBufferInfo bInfo{};
        bInfo.buffer = m_ssaoUBOBuffer.get();
        bInfo.offset = 0;
        bInfo.range = sizeof(SSAOUBOData);

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_ssaoDescSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &dImgInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_ssaoDescSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &bInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    if (m_ssaoBlurDescSet != VK_NULL_HANDLE && m_ssaoImageView != VK_NULL_HANDLE && m_hdrDepthImageView != VK_NULL_HANDLE && m_hdrColorImageView != VK_NULL_HANDLE) {
        VkDescriptorImageInfo ssaoInfo{};
        ssaoInfo.sampler = m_hdrSampler;
        ssaoInfo.imageView = m_ssaoImageView;
        ssaoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = m_depthSampler;
        depthInfo.imageView = m_hdrDepthImageView;
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo colorInfo{};
        colorInfo.sampler = m_hdrSampler;
        colorInfo.imageView = m_hdrColorImageView;
        colorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_ssaoBlurDescSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &ssaoInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_ssaoBlurDescSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &depthInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_ssaoBlurDescSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &colorInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void VulkanRenderer::renderSSAOPass(VkCommandBuffer cmd) {
    if (m_ssaoPipeline == VK_NULL_HANDLE || m_ssaoFramebuffer == VK_NULL_HANDLE) return;

    // 1. Upload updated UBO data
    glm::mat4 proj = activeCameraProjection;
    glm::mat4 invProj = glm::inverse(proj);

    m_ssaoUBOData.proj = proj;
    m_ssaoUBOData.invProj = invProj;
    m_ssaoUBOData.params = glm::vec4(
        std::max(0.01f, m_ssaoSettings.radius),
        std::max(0.0001f, m_ssaoSettings.bias),
        m_ssaoSettings.intensity,
        m_ssaoSettings.power
    );
    m_ssaoUBOData.resolution = glm::vec4(
        static_cast<float>(m_hdrExtent.width),
        static_cast<float>(m_hdrExtent.height),
        1.0f / static_cast<float>(m_hdrExtent.width),
        1.0f / static_cast<float>(m_hdrExtent.height)
    );
    m_ssaoUBOData.settings = glm::ivec4(
        m_ssaoSettings.sampleCount,
        m_ssaoSettings.debugAO ? 1 : 0,
        0, 0
    );

    m_ssaoUBOBuffer.uploadData(&m_ssaoUBOData, sizeof(SSAOUBOData));

    // 2. Begin SSAO render pass
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_ssaoRenderPass;
    rpInfo.framebuffer = m_ssaoFramebuffer;
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = m_hdrExtent;
    rpInfo.clearValueCount = 0;
    rpInfo.pClearValues = nullptr;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_hdrExtent.width), static_cast<float>(m_hdrExtent.height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ {0, 0}, m_hdrExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipelineLayout, 0, 1, &m_ssaoDescSet, 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::renderSSAOBlurPass(VkCommandBuffer cmd) {
    if (m_ssaoBlurPipeline == VK_NULL_HANDLE || m_postProcessTargets[0].framebuffer == VK_NULL_HANDLE) return;

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_postProcessRenderPass;
    rpInfo.framebuffer = m_postProcessTargets[0].framebuffer;
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = m_hdrExtent;
    rpInfo.clearValueCount = 0;
    rpInfo.pClearValues = nullptr;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_hdrExtent.width), static_cast<float>(m_hdrExtent.height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ {0, 0}, m_hdrExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoBlurPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoBlurPipelineLayout, 0, 1, &m_ssaoBlurDescSet, 0, nullptr);

    struct BlurPC {
        glm::vec4 resolution;
        glm::vec4 params;
    } pc;
    pc.resolution = glm::vec4(
        static_cast<float>(m_hdrExtent.width),
        static_cast<float>(m_hdrExtent.height),
        1.0f / static_cast<float>(m_hdrExtent.width),
        1.0f / static_cast<float>(m_hdrExtent.height)
    );
    pc.params = glm::vec4(
        m_ssaoSettings.debugAO ? 1.0f : 0.0f,
        m_ssaoSettings.intensity,
        0.0f, 0.0f
    );

    vkCmdPushConstants(cmd, m_ssaoBlurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}




