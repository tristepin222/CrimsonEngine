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
#include <functional>
#include <fstream>
#include <algorithm>
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

    /**
     * @brief Executes the rendering pass, drawing geometry, grid, and optional GUI/overlay passes.
     * @param overlayPass Optional callback function to execute additional rendering (e.g. ImGui) during the frame.
     */
    void drawFrame(const std::function<void(VkCommandBuffer)>& overlayPass = {}) {
        renderer.beginFrame();

        VkCommandBuffer cmd = renderer.getCurrentCommandBuffer();

        // --- Collect active light info ---
        glm::vec4 ambientLight = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // Renderer fallback sun as ambient fill
        glm::vec4 lightDir = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);      // Default key light direction, type 0 (directional)
        glm::vec4 lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);    // Default key light color/intensity
        
        Entity lightEnt;
        for (auto [e, light] : registry.view<Engine::LightComponent>()) {
            lightEnt = e;
            break;
        }

        if (lightEnt.getId() != Entity::INVALID_ENTITY && registry.isValid(lightEnt)) {
            auto* light = registry.get<Engine::LightComponent>(lightEnt);
            auto* transform = registry.get<Transform>(lightEnt);
            if (light) {
                glm::vec3 dir(0.0f, 1.0f, 0.0f);
                if (transform) {
                    if (glm::dot(transform->position, transform->position) > 0.0001f) {
                        dir = glm::normalize(-transform->position);
                    } else {
                        dir = transform->forward();
                    }
                }
                lightDir = glm::vec4(dir.x, dir.y, dir.z, static_cast<float>(light->type));
                lightColor = glm::vec4(light->color.x, light->color.y, light->color.z, light->intensity);
            }
        }

        CameraUBO ubo{};
        ubo.viewProj = renderer.getActiveCameraViewProj();
        ubo.camPos = glm::vec4(renderer.getActiveCameraPosition(), 1.0f);
        ubo.ambientLight = ambientLight;
        ubo.lightDir = lightDir;
        ubo.lightColor = lightColor;
        renderer.updateCameraUBO(ubo);


        std::vector<InstanceDataGPU> gpuData(renderer.instanceDataCPU.size());
        for (size_t i = 0; i < renderer.instanceDataCPU.size(); i++) {
            gpuData[i].model = renderer.instanceDataCPU.models[i];
            gpuData[i].color = renderer.instanceDataCPU.colors[i];
        }
        renderer.instanceBuffer.uploadData(gpuData.data(), gpuData.size() * sizeof(InstanceDataGPU));


        // --- Draw instances (meshes, tilemaps, sprites) ---
        drawBatches();

        // --- Draw grid overlay ---
        drawGrids();

        if (overlayPass) {
            overlayPass(cmd);
        }

        renderer.endFrame();
    }

private:
    /** @brief Reference to the entity registry. */
    Registry& registry;
    /** @brief Reference to the renderer. */
    VulkanRenderer& renderer;
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
        Engine::Profiler::getInstance().updateRenderStats(rStats);
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
