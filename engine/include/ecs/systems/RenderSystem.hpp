#pragma once
#include "../System.hpp"
#include "../Registry.hpp"
#include "../components/Transform.hpp"
#include "../components/Mesh.hpp"
#include "../components/Material.hpp"
#include "../components/Skeleton.hpp"
#include "../components/LightComponent.hpp"
#include "../../renderer/VulkanRenderer.hpp"
#include "../components/Renderable.hpp"
#include "glm/gtc/matrix_access.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include "../components/Grid.hpp"
#include "../components/pushconstants.hpp"
#include "../components/Hierarchy.hpp"
#include "../components/SpriteRenderer.hpp"
#include "../components/Tilemap.hpp"
#include "../components/TerrainComponent.hpp"
#include "ecs/systems/TerrainSystem.hpp"
#include <functional>
#include <fstream>
#include <algorithm>
#include <limits>
#include <cmath>
#include "core/JobSystem.hpp"

/**
 * @class RenderSystem
 * @brief System that handles rendering of mesh and grid components, managing batches and Vulkan draw commands.
 */
class RenderSystem : public System {
public:
    const char* getName() const override { return "RenderSystem"; }
    /**
     * @brief Construct a new Render System object and subscribes to component add/remove events.
     * @param reg Reference to the ECS Registry.
     * @param renderer Reference to the Vulkan Renderer.
     */
    RenderSystem(Registry& reg, VulkanRenderer& renderer)
        : registry(reg), renderer(renderer) {
        // Auto-track entities with Mesh + Transform + Material
        registry.subscribeToAdded<Mesh>([this](Entity e) { checkAndAdd(e); });
        registry.subscribeToAdded<Transform>([this](Entity e) { checkAndAdd(e); });
        registry.subscribeToAdded<Material>([this](Entity e) { checkAndAdd(e); });
        registry.subscribeToRemoved<Mesh>([this](Entity e) { removeEntity(e); });
        registry.subscribeToRemoved<Transform>([this](Entity e) { removeEntity(e); });
        registry.subscribeToRemoved<Material>([this](Entity e) { removeEntity(e); });

        registry.subscribeToAdded<Grid>([this](Entity e) { checkAndAdd(e); });
        registry.subscribeToRemoved<Grid>([this](Entity e) { removeEntity(e); });
    }

    /**
     * @brief System update called each frame to rebuild instance data.
     * @param dt Delta time in seconds.
     */
    void update(float dt) override {
        buildInstanceData();
        renderer.updateInstanceBuffer();  // bulk upload to GPU
    }

    using RenderHook = std::function<void(VkCommandBuffer)>;

    void setPrePass(RenderHook hook) { m_prePass = std::move(hook); }
    void setSkyPass(RenderHook hook) { m_skyPass = std::move(hook); }
    void clearPrePass() { m_prePass = nullptr; }
    void clearSkyPass() { m_skyPass = nullptr; }

    VulkanRenderer& getRenderer() { return renderer; }
    const VulkanRenderer& getRenderer() const { return renderer; }

    /**
     * @brief Executes the rendering pass, drawing geometry, grid, and optional GUI/overlay passes.
     * @param overlayPass Optional callback function to execute additional rendering (e.g. ImGui) during the frame.
     */
    void drawFrame(const std::function<void(VkCommandBuffer)>& overlayPass = {}) {
        renderer.beginFrame(nullptr);

        VkCommandBuffer cmd = renderer.getCurrentCommandBuffer();

        // --- Collect active light info ---
        glm::vec4 ambientLight = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // Renderer fallback sun as ambient fill
        
        bool hasDirectionalLight = false;
        glm::vec3 dirLightDir(0.0f, -1.0f, 0.0f);
        float shadowDist = 80.0f;
        float shadowBias = 0.0015f;
        float shadowNormalBias = 0.004f;
        bool lightCastsShadows = true;

        // Find primary directional light (preferring one that casts shadows)
        Entity primarySunEnt;
        for (auto [e, light] : registry.view<Engine::LightComponent>()) {
            if (!registry.isComponentEnabled<Engine::LightComponent>(e)) continue;
            if (light.type == Engine::LightType::Directional) {
                if (primarySunEnt.getId() == Entity::INVALID_ENTITY) {
                    primarySunEnt = e;
                } else if (light.castShadows) {
                    auto* cur = registry.get<Engine::LightComponent>(primarySunEnt);
                    if (cur && !cur->castShadows) {
                        primarySunEnt = e;
                    }
                }
            }
        }

        uint32_t numLights = 0;
        float primaryShadowIndex = -1.0f;
        std::array<GPULight, CameraUBO::MAX_LIGHTS> gpuLights{};

        // Track spot and point lights for shadow casting
        struct PendingSpotShadow {
            uint32_t lightIdx;
            glm::vec3 pos;
            glm::vec3 dir;
            float range;
            float bias;
            float normalBias;
        };
        std::vector<PendingSpotShadow> pendingSpots;

        struct PendingPointShadow {
            uint32_t lightIdx;
            glm::vec3 pos;
            float range;
            float bias;
            float normalBias;
            float distToCam;
        };
        std::vector<PendingPointShadow> pendingPoints;

        glm::vec3 activeCamPos = renderer.getActiveCameraPosition();

        // If primary directional sun exists, put it in slot 0
        if (primarySunEnt.getId() != Entity::INVALID_ENTITY && registry.isValid(primarySunEnt)) {
            auto* light = registry.get<Engine::LightComponent>(primarySunEnt);
            auto* transform = registry.get<Transform>(primarySunEnt);
            if (light) {
                glm::vec3 pos(0.0f, 10.0f, 0.0f);
                if (transform) {
                    pos = transform->position;
                }
                glm::vec3 dir = (glm::length(light->direction) > 0.0001f) 
                    ? glm::normalize(light->direction) 
                    : glm::vec3(0.0f, -1.0f, 0.0f);
                float rangeVal = (light->range > 0.001f) ? light->range : 10.0f;
                glm::vec3 finalLightColor = light->color;

                hasDirectionalLight = true;
                dirLightDir = dir;
                lightCastsShadows = light->castShadows;
                shadowDist = (light->shadowDistance > 0.1f) ? light->shadowDistance : renderer.getShadowSettings().maxDistance;
                shadowBias = light->shadowBias;
                shadowNormalBias = (light->shadowNormalBias >= 0.05f) ? light->shadowNormalBias : renderer.getShadowSettings().normalBias;

                glm::vec3 sunDir = -dir; // Vector pointing towards the sun
                float camAlt = activeCamPos.y;
                glm::vec3 transmittance = renderer.evaluateAtmosphereTransmittance(camAlt, sunDir);
                finalLightColor *= transmittance;
                float finalIntensity = light->intensity;

                // Check for Celestial Light handover (e.g. Moonlight taking over when Sun is below horizon)
                if (renderer.getCelestialLightHook()) {
                    auto celest = renderer.getCelestialLightHook()(sunDir, light->color, light->intensity);
                    if (celest.overrideDirectional) {
                        dir = celest.direction;
                        dirLightDir = dir;
                        finalLightColor = celest.color;
                        finalIntensity = celest.intensity;
                    }
                }

                gpuLights[0].position = glm::vec4(pos, rangeVal);
                gpuLights[0].direction = glm::vec4(dir, static_cast<float>(Engine::LightType::Directional));
                gpuLights[0].color = glm::vec4(finalLightColor, finalIntensity);
                gpuLights[0].shadowInfo = glm::vec4(-1.0f, shadowBias, shadowNormalBias, 0.0f);
                numLights = 1;
            }
        }

        // Add all remaining active lights (point lights, spot lights, secondary directional lights)
        for (auto [e, light] : registry.view<Engine::LightComponent>()) {
            if (!registry.isComponentEnabled<Engine::LightComponent>(e)) continue;
            if (e == primarySunEnt) continue;
            if (numLights >= CameraUBO::MAX_LIGHTS) break;

            auto* transform = registry.get<Transform>(e);
            glm::vec3 pos(0.0f);
            if (transform) {
                pos = transform->position;
            }
            glm::vec3 dir = (glm::length(light.direction) > 0.0001f) 
                ? glm::normalize(light.direction) 
                : glm::vec3(0.0f, -1.0f, 0.0f);
            float rangeVal = (light.range > 0.001f) ? light.range : 10.0f;
            float typeVal = static_cast<float>(light.type);

            uint32_t currentLightIdx = numLights;
            gpuLights[currentLightIdx].position = glm::vec4(pos, rangeVal);
            gpuLights[currentLightIdx].direction = glm::vec4(dir, typeVal);
            gpuLights[currentLightIdx].color = glm::vec4(light.color, light.intensity);
            gpuLights[currentLightIdx].shadowInfo = glm::vec4(-1.0f, light.shadowBias, light.shadowNormalBias, 0.0f);

            if (light.castShadows) {
                if (light.type == Engine::LightType::Spot) {
                    pendingSpots.push_back({ currentLightIdx, pos, dir, rangeVal, light.shadowBias, light.shadowNormalBias });
                } else if (light.type == Engine::LightType::Point) {
                    float dist = glm::length(pos - activeCamPos);
                    pendingPoints.push_back({ currentLightIdx, pos, rangeVal, light.shadowBias, light.shadowNormalBias, dist });
                }
            }

            numLights++;
        }

        // If no lights at all exist in the scene, add default fallback sun
        if (numLights == 0) {
            gpuLights[0].position = glm::vec4(0.0f, 10.0f, 0.0f, 10.0f);
            gpuLights[0].direction = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
            gpuLights[0].color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            gpuLights[0].shadowInfo = glm::vec4(-1.0f, 0.0015f, 1.5f, 0.0f);
            numLights = 1;
            primaryShadowIndex = -1.0f;
        }

        // --- 1. Directional Shadow Pass ---
        const auto& globalShadows = renderer.getShadowSettings();
        bool shadowsEnabled = globalShadows.enabled && hasDirectionalLight && lightCastsShadows;

        std::array<glm::mat4, VulkanRenderer::MAX_SHADOW_CASCADES> cascadeLightViewProjs{};
        std::array<glm::mat4, VulkanRenderer::MAX_SHADOW_CASCADES> cascadeLightSpaceMatrices{};
        glm::vec4 cascadeSplits(0.0f);
        float worldUnitsPerTexel = 0.05f;
        if (shadowsEnabled) {
            computeDirectionalLightCascades(dirLightDir, shadowDist, cascadeLightViewProjs, cascadeLightSpaceMatrices, cascadeSplits, worldUnitsPerTexel);
            renderShadowPass(cmd, cascadeLightViewProjs);
            primaryShadowIndex = 0.0f;
            gpuLights[0].shadowInfo.x = 0.0f; // Sun CSM enabled on layer 0
        } else {
            primaryShadowIndex = -1.0f;
        }

        // Convert normal bias from texels to world units
        float effectiveNormalBiasTexels = (shadowNormalBias >= 0.05f) ? shadowNormalBias : 1.5f;
        float normalBiasWorld = effectiveNormalBiasTexels * worldUnitsPerTexel;

        // --- 1b. Spot Light Shadow Passes (up to 4 spot lights on layers 4..7) ---
        std::array<glm::mat4, VulkanRenderer::MAX_SPOT_SHADOWS> spotLightSpaceMatrices{};
        glm::mat4 biasMatrix(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f
        );

        if (globalShadows.enabled) {
            uint32_t activeSpots = std::min(static_cast<uint32_t>(pendingSpots.size()), VulkanRenderer::MAX_SPOT_SHADOWS);
            for (uint32_t s = 0; s < activeSpots; ++s) {
                const auto& spot = pendingSpots[s];
                uint32_t shadowLayer = VulkanRenderer::MAX_SHADOW_CASCADES + s;

                float nearPlane = 0.1f;
                float farPlane = std::max(spot.range, 1.0f);
                glm::mat4 spotProj = glm::perspectiveRH_ZO(glm::radians(80.0f), 1.0f, nearPlane, farPlane);
                spotProj[1][1] *= -1.0f; // Invert Y for Vulkan NDC

                glm::vec3 up = (std::abs(spot.dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                glm::mat4 spotView = glm::lookAt(spot.pos, spot.pos + spot.dir, up);
                glm::mat4 spotViewProj = spotProj * spotView;

                spotLightSpaceMatrices[s] = biasMatrix * spotViewProj;
                gpuLights[spot.lightIdx].shadowInfo.x = static_cast<float>(shadowLayer);

                renderSpotShadowPass(cmd, s, spotViewProj);
            }
        }

        // --- 1c. Hero Point Light Shadow Pass (closest shadow-casting point light, 6 faces) ---
        float pointShadowIndex = -1.0f;
        if (globalShadows.enabled && !pendingPoints.empty()) {
            std::sort(pendingPoints.begin(), pendingPoints.end(), [](const auto& a, const auto& b) {
                return a.distToCam < b.distToCam;
            });

            const auto& heroPoint = pendingPoints[0];
            pointShadowIndex = static_cast<float>(heroPoint.lightIdx);
            gpuLights[heroPoint.lightIdx].shadowInfo.x = 0.0f;

            float nearPlane = 0.1f;
            float farPlane = std::max(heroPoint.range, 1.0f);
            glm::mat4 pointProj = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

            glm::vec3 pPos = heroPoint.pos;
            std::array<glm::mat4, 6> pointFaceViewProjs = {
                pointProj * glm::lookAt(pPos, pPos + glm::vec3( 1,  0,  0), glm::vec3(0, -1,  0)), // +X
                pointProj * glm::lookAt(pPos, pPos + glm::vec3(-1,  0,  0), glm::vec3(0, -1,  0)), // -X
                pointProj * glm::lookAt(pPos, pPos + glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1)), // +Y
                pointProj * glm::lookAt(pPos, pPos + glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1)), // -Y
                pointProj * glm::lookAt(pPos, pPos + glm::vec3( 0,  0,  1), glm::vec3(0, -1,  0)), // +Z
                pointProj * glm::lookAt(pPos, pPos + glm::vec3( 0,  0, -1), glm::vec3(0, -1,  0))  // -Z
            };

            renderPointShadowPass(cmd, pointFaceViewProjs);
        }

        // Store active cascade shadow data on renderer so compute pre-passes (volumetric fog) have access
        VulkanRenderer::CascadeShadowData currentShadowData{};
        for (uint32_t i = 0; i < VulkanRenderer::MAX_SHADOW_CASCADES; ++i) {
            currentShadowData.cascadeLightSpaceMatrices[i] = cascadeLightSpaceMatrices[i];
        }
        currentShadowData.cascadeSplits = cascadeSplits;
        currentShadowData.shadowParams = glm::vec4(
            shadowBias,
            normalBiasWorld,
            static_cast<float>(globalShadows.resolution),
            shadowsEnabled ? 1.0f : 0.0f
        );
        currentShadowData.enabled = shadowsEnabled;
        renderer.setCascadeShadowData(currentShadowData);

        // --- 2. Compute Pre-Pass (Transmittance, Sky-View, & Shadowed 3D Camera Volume LUT) ---
        if (m_prePass) {
            m_prePass(cmd);
        }

        // --- 3. Begin HDR Scene Pass (16-bit float offscreen target) ---
        renderer.beginHDRPass(cmd);

        // --- 4. Update CameraUBO ---
        CameraUBO ubo{};
        ubo.viewProj = renderer.getActiveCameraViewProj();
        for (uint32_t i = 0; i < VulkanRenderer::MAX_SHADOW_CASCADES; ++i) {
            ubo.cascadeLightSpaceMatrices[i] = cascadeLightSpaceMatrices[i];
        }
        for (uint32_t i = 0; i < VulkanRenderer::MAX_SPOT_SHADOWS; ++i) {
            ubo.spotLightSpaceMatrices[i] = spotLightSpaceMatrices[i];
        }
        ubo.cascadeSplits = cascadeSplits;
        ubo.camPos = glm::vec4(renderer.getActiveCameraPosition(), 1.0f);
        ubo.ambientLight = ambientLight;
        ubo.shadowParams = glm::vec4(
            shadowBias,
            normalBiasWorld,
            static_cast<float>(globalShadows.resolution),
            globalShadows.enabled ? 1.0f : 0.0f
        );
        ubo.lightParams = glm::vec4(static_cast<float>(numLights), primaryShadowIndex, pointShadowIndex, 0.0f);
        ubo.weatherParams = renderer.getWeatherParams();
        for (uint32_t i = 0; i < CameraUBO::MAX_LIGHTS; ++i) {
            ubo.lights[i] = gpuLights[i];
        }
        renderer.updateCameraUBO(ubo);

        std::vector<InstanceDataGPU> gpuData(renderer.instanceDataCPU.size());
        for (size_t i = 0; i < renderer.instanceDataCPU.size(); i++) {
            gpuData[i].model = renderer.instanceDataCPU.models[i];
            gpuData[i].color = renderer.instanceDataCPU.colors[i];
        }
        renderer.instanceBuffer.uploadData(gpuData.data(), gpuData.size() * sizeof(InstanceDataGPU));

        // --- Draw instances (meshes, tilemaps, sprites) ---
        drawBatches();

        // --- Draw sky background pass ---
        renderer.executeSkyPass(cmd);
        if (m_skyPass) {
            m_skyPass(cmd);
        }

        // --- Draw aerial perspective (fog over meshes) ---
        renderer.executeAerialPass(cmd);

        // --- Draw grid overlay ---
        drawGrids();

        // --- 5. End HDR Scene Pass (Transitions HDR color image to SHADER_READ_ONLY_OPTIMAL) ---
        renderer.endHDRPass(cmd);

        // --- 5b. Extensible Post-Processing Stack (Ping-Pong blits on HDR textures) ---
        renderer.renderPostProcessChain(cmd);

        // --- 6. Begin Swapchain Presentation Pass ---
        renderer.beginSwapchainPass(cmd);

        // --- 7. Fullscreen ACES / Filmic Tone Mapping & Final Presentation Pass ---
        renderer.renderToneMapping(cmd);

        // --- 8. Render UI Overlay (ImGui) directly onto Swapchain ---
        if (overlayPass) {
            overlayPass(cmd);
        }

        // --- 9. End Frame (Submits command buffer and presents) ---
        renderer.endFrame();
    }

private:
    /** @brief Reference to the entity registry. */
    Registry& registry;
    /** @brief Reference to the renderer. */
    VulkanRenderer& renderer;
    /** @brief Pre-pass hook before render pass (e.g. compute LUTs). */
    RenderHook m_prePass;
    /** @brief Sky background pass hook (inside main render pass after geometry). */
    RenderHook m_skyPass;
    /** @brief List of tracked entities that have renderable components. */
    std::vector<Entity> entities;
    /** @brief Mapping from entity to its instance index in CPU buffers. */
    std::unordered_map<Entity, size_t> entityToInstanceIndex;
    /** @brief Vector to track renderable entities to avoid allocation. */
    std::vector<Entity> renderableEntities;

    /**
     * @brief Verifies if an entity meets requirements for rendering and adds it to the tracked list.
     * @param e The entity to check.
     */
    void checkAndAdd(Entity e) {
        if ((registry.get<Mesh>(e) && registry.get<Transform>(e) && registry.get<Material>(e))
            || registry.get<Grid>(e)) {
            if (std::find(entities.begin(), entities.end(), e) == entities.end())
                entities.push_back(e);
        }
    }

    /**
     * @brief Removes an entity from tracking when components are removed.
     * @param e The entity to remove.
     */
    void removeEntity(Entity e) {
        entities.erase(std::remove(entities.begin(), entities.end(), e), entities.end());
    }

    /**
     * @brief Computes the absolute world matrix of an entity by traversing the hierarchy.
     */
    glm::mat4 getWorldMatrix(Entity entity, int depth = 0) {
        if (depth > 100) return glm::mat4(1.0f); // Safety depth limit to prevent infinite recursion
        glm::mat4 model = glm::mat4(1.0f);
        if (auto* transform = registry.get<Transform>(entity)) {
            model = transform->matrix();
        }
        if (auto* hierarchy = registry.get<HierarchyComponent>(entity)) {
            if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
                // If this is a child mesh entity that is rigid/non-skinned, attach it to its parent bone if matching joint exists
                if (auto* mesh = registry.get<Mesh>(entity)) {
                    if (!mesh->isSkinned) {
                        std::string targetBone = mesh->parentBoneName.empty() ? mesh->nodeName : mesh->parentBoneName;
                        if (!targetBone.empty()) {
                            if (auto* parentSkeleton = registry.get<SkeletonComponent>(hierarchy->parent)) {
                                int jointIdx = -1;
                                
                                // 1. Exact match
                                for (size_t i = 0; i < parentSkeleton->joints.size(); ++i) {
                                    if (parentSkeleton->joints[i].name == targetBone) {
                                        jointIdx = static_cast<int>(i);
                                        break;
                                    }
                                }
                                
                                // 2. Case-insensitive match fallback
                                if (jointIdx == -1) {
                                    auto toLower = [](std::string s) {
                                        for (char& c : s) c = static_cast<char>(std::tolower(c));
                                        return s;
                                    };
                                    std::string lowerTarget = toLower(targetBone);
                                    for (size_t i = 0; i < parentSkeleton->joints.size(); ++i) {
                                        if (toLower(parentSkeleton->joints[i].name) == lowerTarget) {
                                            jointIdx = static_cast<int>(i);
                                            break;
                                        }
                                    }
                                }
                                
                                // 3. Substring match fallback (handles prefixes/suffixes)
                                if (jointIdx == -1) {
                                    for (size_t i = 0; i < parentSkeleton->joints.size(); ++i) {
                                        if (parentSkeleton->joints[i].name.find(targetBone) != std::string::npos ||
                                            targetBone.find(parentSkeleton->joints[i].name) != std::string::npos) {
                                            jointIdx = static_cast<int>(i);
                                            break;
                                        }
                                    }
                                }
                                
                                if (jointIdx != -1 && jointIdx < static_cast<int>(parentSkeleton->jointMatrices.size())) {
                                    return getWorldMatrix(hierarchy->parent, depth + 1) * parentSkeleton->jointMatrices[jointIdx];
                                }
                            }
                        }
                    }
                }
                model = getWorldMatrix(hierarchy->parent, depth + 1) * model;
            }
        }
        return model;
    }

    /**
     * @brief Rebuilds instance data by iterating over entities.
     */
    void buildInstanceData() {
        renderer.instanceDataCPU.clear();
        entityToInstanceIndex.clear();

        renderableEntities.clear();
        for (auto [entity, mesh, transform, material] :
            registry.view<Mesh, Transform, Material>()) {
            if (registry.get<Grid>(entity)) continue;
            if (!mesh.visible) continue;
            renderableEntities.push_back(entity);
        }

        std::vector<InstanceData> tempInstances(renderableEntities.size());

        Engine::JobSystem::getInstance().parallelFor(static_cast<int>(renderableEntities.size()), [&](int idx) {
            Entity entity = renderableEntities[idx];
            auto* mesh = registry.get<Mesh>(entity);
            auto* material = registry.get<Material>(entity);

            InstanceData inst{};
            inst.model = getWorldMatrix(entity);
            inst.materialID = material ? material->id : 0;
            inst.meshID = mesh ? mesh->id : 0;
            inst.color = material ? material->color : glm::vec4(1.0f);

            tempInstances[idx] = inst;
        });

        for (size_t i = 0; i < renderableEntities.size(); ++i) {
            size_t instanceIdx = renderer.instanceDataCPU.push(tempInstances[i]);
            entityToInstanceIndex[renderableEntities[i]] = instanceIdx;
        }
    }

    /**
     * @brief Retrieves the active camera's world position.
     * @return Camera position, or zero vector if none is active.
     */
    glm::vec3 getCameraPosition() {
        if (renderer.hasActiveCamera()) {
            return renderer.getActiveCameraPosition();
        }
        return glm::vec3(0.0f);
    }

    /**
     * @brief Computes 4-cascade light-space matrices with practical log-linear split scheme, rotation-invariant bounding spheres, and sub-texel snapping.
     */
    void computeDirectionalLightCascades(
        const glm::vec3& lightDir,
        float shadowDistance,
        std::array<glm::mat4, VulkanRenderer::MAX_SHADOW_CASCADES>& outLightViewProjs,
        std::array<glm::mat4, VulkanRenderer::MAX_SHADOW_CASCADES>& outLightSpaceMatrices,
        glm::vec4& outCascadeSplits,
        float& outBaseWorldUnitsPerTexel
    ) {
        glm::vec3 lightDirNorm = (glm::length(lightDir) > 0.0001f)
            ? glm::normalize(lightDir)
            : glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 sunDir = -lightDirNorm;

        // Extract camera view & projection
        const glm::mat4& activeProj = renderer.getActiveCameraProjection();
        const glm::mat4& activeView = renderer.getActiveCameraView();
        glm::vec3 camPos = renderer.getActiveCameraPosition();

        // Extract camera basis vectors from activeView (rows of view matrix)
        glm::vec3 camRight = glm::normalize(glm::vec3(activeView[0][0], activeView[1][0], activeView[2][0]));
        glm::vec3 camUp    = glm::normalize(glm::vec3(activeView[0][1], activeView[1][1], activeView[2][1]));
        glm::vec3 camForward = -glm::normalize(glm::vec3(activeView[0][2], activeView[1][2], activeView[2][2]));

        // Extract FOV and aspect ratio
        float tanHalfFov = 1.0f / std::max(std::abs(activeProj[1][1]), 0.001f);
        float aspect = std::abs(activeProj[1][1]) / std::max(std::abs(activeProj[0][0]), 0.001f);

        float nearZ = 0.1f;
        float farZ = std::max(shadowDistance, 10.0f);
        float lambda = renderer.getShadowSettings().cascadeSplitLambda;
        uint32_t cascadeCount = VulkanRenderer::MAX_SHADOW_CASCADES;

        // 1. Calculate cascade split depths using practical split scheme (blend of log and linear)
        float splits[VulkanRenderer::MAX_SHADOW_CASCADES];
        for (uint32_t i = 0; i < cascadeCount; ++i) {
            float p = static_cast<float>(i + 1) / static_cast<float>(cascadeCount);
            float logSplit = nearZ * std::pow(farZ / nearZ, p);
            float uniformSplit = nearZ + (farZ - nearZ) * p;
            splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        }

        outCascadeSplits = glm::vec4(splits[0], splits[1], splits[2], splits[3]);

        float shadowResolution = static_cast<float>(renderer.getShadowSettings().resolution);
        glm::vec3 up = (std::abs(lightDirNorm.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

        // UV bias matrix to convert NDC [-1, 1] to UV [0, 1]
        glm::mat4 biasMatrix(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f
        );

        // 2. Compute orthographic projection and light view matrix per cascade
        for (uint32_t i = 0; i < cascadeCount; ++i) {
            float cNear = (i == 0) ? nearZ : splits[i - 1];
            float cFar = splits[i];

            float nearHalfH = cNear * tanHalfFov;
            float nearHalfW = nearHalfH * aspect;
            float farHalfH = cFar * tanHalfFov;
            float farHalfW = farHalfH * aspect;

            glm::vec3 nearCenter = camPos + camForward * cNear;
            glm::vec3 farCenter  = camPos + camForward * cFar;

            std::array<glm::vec3, 8> frustumCorners = {
                nearCenter - camRight * nearHalfW - camUp * nearHalfH,
                nearCenter + camRight * nearHalfW - camUp * nearHalfH,
                nearCenter - camRight * nearHalfW + camUp * nearHalfH,
                nearCenter + camRight * nearHalfW + camUp * nearHalfH,
                farCenter  - camRight * farHalfW  - camUp * farHalfH,
                farCenter  + camRight * farHalfW  - camUp * farHalfH,
                farCenter  - camRight * farHalfW  + camUp * farHalfH,
                farCenter  + camRight * farHalfW  + camUp * farHalfH
            };

            // Frustum center
            glm::vec3 frustumCenter(0.0f);
            for (const auto& pt : frustumCorners) {
                frustumCenter += pt;
            }
            frustumCenter /= 8.0f;

            // Bounding sphere radius: rotation-invariant to eliminate camera rotation shimmer
            float sphereRadius = 0.0f;
            for (const auto& pt : frustumCorners) {
                sphereRadius = std::max(sphereRadius, glm::length(pt - frustumCenter));
            }
            sphereRadius = std::ceil(sphereRadius * 16.0f) / 16.0f;

            if (i == 0) {
                outBaseWorldUnitsPerTexel = (2.0f * sphereRadius) / shadowResolution;
            }

            // Light position & view matrix
            float lightDistance = sphereRadius + 150.0f;
            glm::vec3 lightPos = frustumCenter + sunDir * lightDistance;
            glm::mat4 lightView = glm::lookAt(lightPos, frustumCenter, up);

            // Symmetrical orthographic projection matching the rotation-invariant bounding sphere
            float nearPlane = 1.0f;
            float farPlane = lightDistance + sphereRadius + 50.0f;
            glm::mat4 lightProj = glm::orthoRH_ZO(-sphereRadius, sphereRadius, -sphereRadius, sphereRadius, nearPlane, farPlane);
            lightProj[1][1] *= -1.0f; // Invert Y for Vulkan NDC

            // Texel snapping: snap projection translation so shadow texels lock to world texel grid
            glm::vec4 shadowOrigin = lightProj * lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            shadowOrigin *= (shadowResolution / 2.0f);

            glm::vec4 roundedOrigin = glm::round(shadowOrigin);
            glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
            roundOffset = roundOffset * (2.0f / shadowResolution);
            roundOffset.z = 0.0f;
            roundOffset.w = 0.0f;

            lightProj[3] += roundOffset;

            glm::mat4 lightViewProj = lightProj * lightView;
            outLightViewProjs[i] = lightViewProj;
            outLightSpaceMatrices[i] = biasMatrix * lightViewProj;
        }
    }

    /**
     * @brief Helper that records depth-only draw calls for all shadow-casting meshes using a given light viewProj.
     */
    void recordShadowDrawCalls(VkCommandBuffer cmd, const glm::mat4& lightViewProj, bool includeTerrain = true) {
        PipelineHandle shadowPipe = renderer.getShadowPipeline();
        PipelineHandle skinnedShadowPipe = renderer.getSkinnedShadowPipeline();
        if (shadowPipe.pipeline == VK_NULL_HANDLE) return;

        Mesh* currentMesh = nullptr;
        VkPipeline currentBoundPipeline = VK_NULL_HANDLE;

        for (Entity e : entities) {
            auto* mesh = registry.get<Mesh>(e);
            auto* mat = registry.get<Material>(e);
            if (!mesh || !mat || mesh->vertexBuffer == VK_NULL_HANDLE || mesh->indexBuffer == VK_NULL_HANDLE) continue;

            // Skip 2D sprites, tilemaps, and terrains (drawn separately) from regular entity shadow casting
            if (registry.has<Engine::SpriteRenderer>(e) || registry.has<Engine::TilemapComponent>(e) || registry.has<Engine::TerrainComponent>(e)) continue;

            auto it = entityToInstanceIndex.find(e);
            if (it == entityToInstanceIndex.end()) continue;

            // Check if skinned
            SkeletonComponent* skeleton = registry.get<SkeletonComponent>(e);
            if (!skeleton) {
                if (auto* hierarchy = registry.get<HierarchyComponent>(e)) {
                    if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
                        skeleton = registry.get<SkeletonComponent>(hierarchy->parent);
                    }
                }
            }

            bool isSkinned = (skeleton && skeleton->descriptorSet != VK_NULL_HANDLE && skinnedShadowPipe.pipeline != VK_NULL_HANDLE);
            VkPipeline targetPipe = isSkinned ? skinnedShadowPipe.pipeline : shadowPipe.pipeline;
            VkPipelineLayout targetLayout = isSkinned ? skinnedShadowPipe.layout : shadowPipe.layout;

            if (targetPipe != currentBoundPipeline) {
                currentBoundPipeline = targetPipe;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentBoundPipeline);
                currentMesh = nullptr;
            }

            if (isSkinned) {
                vkCmdBindDescriptorSets(cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    targetLayout,
                    2, // Joint descriptor set
                    1,
                    &skeleton->descriptorSet,
                    0,
                    nullptr);
            }

            if (mesh != currentMesh) {
                currentMesh = mesh;
                VkBuffer vertexBuffers[] = { mesh->vertexBuffer };
                VkDeviceSize vertexOffsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, vertexOffsets);
                vkCmdBindIndexBuffer(cmd, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            }

            const InstanceDataGPU& inst = renderer.instanceDataCPU.get(it->second);

            PushConstants pc{};
            pc.model = inst.model;
            pc.color = inst.color;
            pc.viewProj = lightViewProj;
            pc.camPos = glm::vec4(getCameraPosition(), 1.0f);
            pc.scale = 0.0f;
            pc.fade = 0.0f;

            vkCmdPushConstants(cmd,
                targetLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &pc);

            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(mesh->indices.size()), 1, 0, 0, 0);
        }

        // Draw terrain chunks into shadow map (if requested)
        if (includeTerrain) {
            recordTerrainShadowDrawCalls(cmd, lightViewProj);
        }
    }

    /**
     * @brief Records shadow draw calls for all visible terrain sub-mesh chunks using cascade frustum culling.
     */
    void recordTerrainShadowDrawCalls(VkCommandBuffer cmd, const glm::mat4& lightViewProj) {
        PipelineHandle shadowPipe = renderer.getShadowPipeline();
        if (shadowPipe.pipeline == VK_NULL_HANDLE) return;

        // 1. Extract shadow cascade frustum planes for precision orthographic/perspective culling
        std::array<glm::vec4, 6> cascadePlanes = Engine::TerrainSystem::extractFrustumPlanes(lightViewProj);

        for (auto [e, terrain, transform] : registry.view<Engine::TerrainComponent, Transform>()) {
            if (terrain.chunks.empty()) continue;
            glm::mat4 model = getWorldMatrix(e);

            // Hierarchical rejection: if entire terrain world AABB is outside this shadow cascade frustum, skip all chunks!
            glm::vec3 terrainLocalMin(0.0f, 0.0f, 0.0f);
            glm::vec3 terrainLocalMax(terrain.sizeX, terrain.heightScale, terrain.sizeZ);
            glm::vec3 terrainWorldMin, terrainWorldMax;
            Engine::TerrainSystem::transformAABB(model, terrainLocalMin, terrainLocalMax, terrainWorldMin, terrainWorldMax);

            if (!Engine::TerrainSystem::isAabbInFrustum(terrainWorldMin, terrainWorldMax, cascadePlanes)) {
                continue; // Skip the entire terrain entity for this shadow cascade
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipe.pipeline);

            PushConstants pc{};
            pc.model = model;
            pc.color = glm::vec4(1.0f);
            pc.viewProj = lightViewProj;
            pc.camPos = glm::vec4(getCameraPosition(), 1.0f);
            pc.scale = 0.0f;
            pc.fade = 0.0f;

            vkCmdPushConstants(cmd,
                shadowPipe.layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &pc);

            // Bind shared index buffer ONCE for this terrain entity
            VkBuffer sharedIB = terrain.chunks[0].mesh.indexBuffer;
            if (sharedIB == VK_NULL_HANDLE) {
                for (const auto& chunk : terrain.chunks) {
                    if (chunk.mesh.indexBuffer != VK_NULL_HANDLE) {
                        sharedIB = chunk.mesh.indexBuffer;
                        break;
                    }
                }
            }
            if (sharedIB == VK_NULL_HANDLE) continue;
            vkCmdBindIndexBuffer(cmd, sharedIB, 0, VK_INDEX_TYPE_UINT32);

            uint32_t indexCount = (terrain.chunkResolution > 1)
                ? (terrain.chunkResolution - 1) * (terrain.chunkResolution - 1) * 6
                : 0;
            if (indexCount == 0) continue;

            for (auto& chunk : terrain.chunks) {
                if (chunk.mesh.vertexBuffer == VK_NULL_HANDLE) continue;

                // Precision culling against the shadow cascade frustum:
                // Only chunks physically intersecting the cascade volume are drawn!
                if (!Engine::TerrainSystem::isAabbInFrustum(chunk.worldAabbMin, chunk.worldAabbMax, cascadePlanes)) {
                    continue;
                }

                VkBuffer vertexBuffers[] = { chunk.mesh.vertexBuffer };
                VkDeviceSize vertexOffsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, vertexOffsets);
                vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
            }
        }
    }

    /**
     * @brief Records draw calls for all shadow-casting meshes into each cascade of the directional shadow depth pass.
     */
    void renderShadowPass(VkCommandBuffer cmd, const std::array<glm::mat4, VulkanRenderer::MAX_SHADOW_CASCADES>& cascadeLightViewProjs) {
        for (uint32_t c = 0; c < VulkanRenderer::MAX_SHADOW_CASCADES; ++c) {
            renderer.beginShadowPass(cmd, c);
            recordShadowDrawCalls(cmd, cascadeLightViewProjs[c], true);
            renderer.endShadowPass(cmd);
        }
    }

    /**
     * @brief Records draw calls for all shadow-casting meshes into a spot light's shadow layer.
     */
    void renderSpotShadowPass(VkCommandBuffer cmd, uint32_t spotIndex, const glm::mat4& spotLightViewProj) {
        renderer.beginShadowPass(cmd, VulkanRenderer::MAX_SHADOW_CASCADES + spotIndex);
        recordShadowDrawCalls(cmd, spotLightViewProj, true);
        renderer.endShadowPass(cmd);
    }

    /**
     * @brief Records draw calls for all shadow-casting meshes into all 6 faces of the omnidirectional point shadow cubemap.
     */
    void renderPointShadowPass(VkCommandBuffer cmd, const std::array<glm::mat4, 6>& faceViewProjs) {
        for (uint32_t face = 0; face < 6; ++face) {
            renderer.beginPointShadowPass(cmd, face);
            recordShadowDrawCalls(cmd, faceViewProjs[face], false);
            renderer.endPointShadowPass(cmd);
        }
    }

    /**
     * @brief Binds pipelines, descriptor sets, and draw buffers for geometry batches.
     */
    struct RenderDrawCall {
        Entity entity;
        Mesh* mesh = nullptr;
        Material* mat = nullptr;
        size_t instanceIdx = 0;
        int sortOrder = 0;
        float zPos = 0.0f;
    };

    /**
     * @brief Binds pipelines, descriptor sets, and draw buffers for geometry batches.
     */
    void drawBatches() {
        PROFILE_FUNCTION();
        VkCommandBuffer cmd = renderer.getCurrentCommandBuffer();
        VkDescriptorSet cameraSet = renderer.getCameraDescriptorSet();

        std::vector<RenderDrawCall> drawCalls;
        drawCalls.reserve(entities.size());

        for (Entity e : entities) {
            auto* mesh = registry.get<Mesh>(e);
            auto* mat = registry.get<Material>(e);
            auto* transform = registry.get<Transform>(e);
            if (!mesh || !mat || !transform) continue;

            auto it = entityToInstanceIndex.find(e);
            if (it == entityToInstanceIndex.end()) continue;
            size_t idx = it->second;

            int sortOrder = 0;
            if (auto* spr = registry.get<Engine::SpriteRenderer>(e)) {
                sortOrder = spr->sortOrder;
            } else if (registry.has<Engine::TilemapComponent>(e)) {
                sortOrder = -1000; // Tilemaps draw behind standard 2D sprites by default
            } else if (registry.has<Engine::TerrainComponent>(e)) {
                continue; // Terrains are drawn via drawTerrains() using internal chunks
            }

            float zPos = transform->position.z;

            drawCalls.push_back({ e, mesh, mat, idx, sortOrder, zPos });
        }

        // Sort back-to-front (lowest sortOrder first, then zPos, then mat, then mesh)
        std::sort(drawCalls.begin(), drawCalls.end(), [](const RenderDrawCall& a, const RenderDrawCall& b) {
            if (a.sortOrder != b.sortOrder) return a.sortOrder < b.sortOrder;
            if (a.zPos != b.zPos) return a.zPos < b.zPos;
            if (a.mat != b.mat) return a.mat < b.mat;
            return a.mesh < b.mesh;
        });

        Material* currentMat = nullptr;
        Mesh* currentMesh = nullptr;

        for (const auto& dc : drawCalls) {
            Mesh* mesh = dc.mesh;
            Material* mat = dc.mat;
            if (!mesh || !mat || mesh->vertexBuffer == VK_NULL_HANDLE || mesh->indexBuffer == VK_NULL_HANDLE || mat->pipeline == VK_NULL_HANDLE) continue;

            if (mat != currentMat) {
                currentMat = mat;
                currentMesh = nullptr; // force vertex buffer rebind when material changes

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mat->pipeline);

                VkDescriptorSet descriptorSets[2] = {
                    cameraSet,
                    mat->descriptorSet != VK_NULL_HANDLE ? mat->descriptorSet : renderer.getDefaultTextureSet()
                };

                vkCmdBindDescriptorSets(cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    mat->pipelineLayout,
                    0,
                    2,
                    descriptorSets,
                    0,
                    nullptr);
            }

            if (mesh != currentMesh) {
                currentMesh = mesh;

                VkBuffer vertexBuffers[] = { mesh->vertexBuffer };
                VkDeviceSize vertexOffsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, vertexOffsets);
                vkCmdBindIndexBuffer(cmd, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            }

            // Bind Joint Descriptor Set (Set 2) if entity (or parent) has a Skeleton component
            SkeletonComponent* skeleton = registry.get<SkeletonComponent>(dc.entity);
            if (!skeleton) {
                if (auto* hierarchy = registry.get<HierarchyComponent>(dc.entity)) {
                    if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
                        skeleton = registry.get<SkeletonComponent>(hierarchy->parent);
                    }
                }
            }

            if (skeleton && skeleton->descriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    mat->pipelineLayout,
                    2, // Set 2
                    1,
                    &skeleton->descriptorSet,
                    0,
                    nullptr);
            }

            // Read instance data from renderer.instanceDataCPU
            const InstanceDataGPU& inst = renderer.instanceDataCPU.get(dc.instanceIdx);

            PushConstants pc{};
            pc.model = inst.model;
            pc.color = inst.color;
            pc.camPos = glm::vec4(getCameraPosition(), 1.0f);
            if (renderer.hasActiveCamera()) {
                pc.viewProj = renderer.getActiveCameraViewProj();
            } else {
                pc.viewProj = glm::mat4(1.0f);
            }
            pc.scale = mat ? mat->roughness : 0.5f;
            pc.fade = mat ? mat->metallic : 0.0f;

            vkCmdPushConstants(cmd,
                mat->pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &pc);

            // Draw instance
            vkCmdDrawIndexed(cmd,
                static_cast<uint32_t>(mesh->indices.size()),
                1,
                0,
                0,
                0);
        }

        // Update Profiler RenderStats
        Engine::RenderStats rStats{};
        rStats.drawCalls = static_cast<uint32_t>(drawCalls.size());
        rStats.entityCount = static_cast<uint32_t>(registry.getAlive().size());
        rStats.meshMemoryBytes = renderer.meshSoA.vertices.size() * sizeof(Vertex);
        for (const auto& dc : drawCalls) {
            if (dc.mesh) {
                rStats.vertexCount += static_cast<uint32_t>(dc.mesh->vertices.size());
                rStats.triangleCount += static_cast<uint32_t>(dc.mesh->indices.size() / 3);
            }
        }

        // --- Draw Terrains (sub-mesh chunks without ECS entities) ---
        drawTerrains(cmd, cameraSet, rStats);

        // --- Draw GPU Instanced Foliage & Grass ---
        drawFoliage(cmd, cameraSet, rStats);

        Engine::Profiler::getInstance().updateRenderStats(rStats);
    }

    /**
     * @brief Draws all visible terrain sub-mesh chunks directly without creating ECS child entities.
     */
    void drawTerrains(VkCommandBuffer cmd, VkDescriptorSet cameraSet, Engine::RenderStats& rStats) {
        for (auto [e, terrain, transform, mat] : registry.view<Engine::TerrainComponent, Transform, Material>()) {
            VkDescriptorSet terrainSet = (terrain.terrainDescriptorSet != VK_NULL_HANDLE)
                ? terrain.terrainDescriptorSet
                : mat.descriptorSet;
            if (mat.pipeline == VK_NULL_HANDLE || terrain.chunks.empty() || terrainSet == VK_NULL_HANDLE) continue;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mat.pipeline);

            VkDescriptorSet descriptorSets[2] = {
                cameraSet,
                terrainSet
            };

            vkCmdBindDescriptorSets(cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                mat.pipelineLayout,
                0,
                2,
                descriptorSets,
                0,
                nullptr);

            glm::mat4 model = getWorldMatrix(e);

            PushConstants pc{};
            pc.model = model;
            pc.color = mat.color;
            pc.camPos = glm::vec4(getCameraPosition(), 1.0f);
            pc.viewProj = renderer.hasActiveCamera() ? renderer.getActiveCameraViewProj() : glm::mat4(1.0f);
            pc.scale = mat.roughness;
            pc.fade = mat.metallic;

            vkCmdPushConstants(cmd,
                mat.pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &pc);

            // Bind shared index buffer ONCE for all chunks in this terrain
            VkBuffer sharedIB = terrain.chunks[0].mesh.indexBuffer;
            if (sharedIB == VK_NULL_HANDLE) {
                for (const auto& chunk : terrain.chunks) {
                    if (chunk.mesh.indexBuffer != VK_NULL_HANDLE) {
                        sharedIB = chunk.mesh.indexBuffer;
                        break;
                    }
                }
            }
            if (sharedIB == VK_NULL_HANDLE) continue;
            vkCmdBindIndexBuffer(cmd, sharedIB, 0, VK_INDEX_TYPE_UINT32);

            uint32_t indexCount = (terrain.chunkResolution > 1)
                ? (terrain.chunkResolution - 1) * (terrain.chunkResolution - 1) * 6
                : 0;
            if (indexCount == 0) continue;
            uint32_t vertCount = terrain.chunkResolution * terrain.chunkResolution;

            for (auto& chunk : terrain.chunks) {
                if (!chunk.isVisible) continue;
                if (chunk.mesh.vertexBuffer == VK_NULL_HANDLE) continue;

                VkBuffer vertexBuffers[] = { chunk.mesh.vertexBuffer };
                VkDeviceSize vertexOffsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, vertexOffsets);
                vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);

                rStats.drawCalls += 1;
                rStats.vertexCount += vertCount;
                rStats.triangleCount += indexCount / 3;
            }
        }
    }

    /**
     * @brief Draws instanced foliage (grass clumps) for visible terrain chunks.
     */
    void drawFoliage(VkCommandBuffer cmd, VkDescriptorSet cameraSet, Engine::RenderStats& rStats) {
        for (auto [e, terrain, transform] : registry.view<Engine::TerrainComponent, Transform>()) {
            if (!terrain.foliageEnabled || terrain.foliagePipeline == VK_NULL_HANDLE ||
                terrain.foliageVertexBuffer == VK_NULL_HANDLE || terrain.foliageIndexBuffer == VK_NULL_HANDLE ||
                terrain.foliageDescriptorSet == VK_NULL_HANDLE || terrain.foliageIndexCount == 0) {
                continue;
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain.foliagePipeline);

            VkDescriptorSet descriptorSets[2] = {
                cameraSet,
                terrain.foliageDescriptorSet
            };

            vkCmdBindDescriptorSets(cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                terrain.foliagePipelineLayout,
                0,
                2,
                descriptorSets,
                0,
                nullptr);

            glm::mat4 model = getWorldMatrix(e);

            PushConstants pc{};
            pc.model = model;
            pc.color = terrain.foliageTint;
            pc.camPos = glm::vec4(getCameraPosition(), 1.0f);
            pc.viewProj = renderer.hasActiveCamera() ? renderer.getActiveCameraViewProj() : glm::mat4(1.0f);
            pc.scale = terrain.foliageMaxDistance;   // scale holds max distance for smooth distance culling
            pc.fade = terrain.foliageWindStrength;   // fade holds wind animation strength

            vkCmdPushConstants(cmd,
                terrain.foliagePipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &pc);

            // Shared clump mesh index buffer
            vkCmdBindIndexBuffer(cmd, terrain.foliageIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

            for (auto& chunk : terrain.chunks) {
                if (!chunk.isVisible) continue;
                if (chunk.foliageBuffer == VK_NULL_HANDLE || chunk.foliageInstanceCount == 0) continue;

                // Binding 0: Per-vertex clump mesh geometry
                // Binding 1: Per-instance chunk foliage instance data
                VkBuffer vbs[2] = { terrain.foliageVertexBuffer, chunk.foliageBuffer };
                VkDeviceSize offsets[2] = { 0, 0 };
                vkCmdBindVertexBuffers(cmd, 0, 2, vbs, offsets);

                vkCmdDrawIndexed(cmd, terrain.foliageIndexCount, chunk.foliageInstanceCount, 0, 0, 0);

                rStats.drawCalls += 1;
                rStats.vertexCount += chunk.foliageInstanceCount * 12; // 12 vertices per 3-quad clump
                rStats.triangleCount += chunk.foliageInstanceCount * 6; // 6 triangles per 3-quad clump
            }
        }
    }

    /**
     * @brief Draws all active grid overlays.
     */
    void drawGrids() {
        VkCommandBuffer cmd = renderer.getCurrentCommandBuffer();
        VkDescriptorSet cameraSet = renderer.getCameraDescriptorSet();

        for (Entity e : entities) {
            auto* grid = registry.get<Grid>(e);
            auto* mat = registry.get<Material>(e);
            if (!grid || !mat || mat->pipeline == VK_NULL_HANDLE) continue;


            // Bind grid pipeline
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mat->pipeline);

            // Bind grid quad vertex buffer to satisfy pipeline input requirements
            if (auto* mesh = registry.get<Mesh>(e)) {
                if (mesh->vertexBuffer != VK_NULL_HANDLE) {
                    VkBuffer vertexBuffers[] = { mesh->vertexBuffer };
                    VkDeviceSize vertexOffsets[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, vertexOffsets);
                }
            }

            // Bind descriptor sets (camera)
            vkCmdBindDescriptorSets(cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                mat->pipelineLayout,
                0,
                1,
                &cameraSet,
                0,
                nullptr);

            // Get camera position for grid positioning
            glm::vec3 camPos = getCameraPosition();

            // Push constants for grid shader
            struct GridPushConstants {
                glm::mat4 viewProj;
                glm::vec4 color;
                glm::vec4 camPos;
                float scale;
                float fade;
            } pc;

            // Use the camera's view-projection matrix
            if (renderer.hasActiveCamera()) {
                pc.viewProj = renderer.getActiveCameraViewProj();
            } else {
                pc.viewProj = glm::mat4(1.0f);
            }

            pc.color = grid->color;
            pc.camPos = glm::vec4(camPos, 1.0f);
            pc.scale = grid->spacing;
            pc.fade = grid->size;

            vkCmdPushConstants(cmd,
                mat->pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(GridPushConstants),
                &pc);

            // Draw 6 vertices so the shader can emit a full-screen quad as two triangles.
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }

    /**
     * @struct pair_hash
     * @brief Custom hash function for std::pair<Mesh*, Material*> used in batch rendering maps.
     */
    struct pair_hash {
        /**
         * @brief Computes hash for pair of Mesh and Material pointers.
         * @param p The pointer pair.
         * @return Combined hash value.
         */
        std::size_t operator()(const std::pair<Mesh*, Material*>& p) const {
            return std::hash<Mesh*>()(p.first) ^ (std::hash<Material*>()(p.second) << 1);
        }
    };
};
