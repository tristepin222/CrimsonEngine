#include "ecs/systems/SpriteSystem.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Mesh.hpp"
#include "ecs/components/Material.hpp"
#include "renderer/ResourceManager.hpp"
#include <iostream>

namespace Engine {

    // =========================================================================
    // Constructor
    // =========================================================================

    SpriteSystem::SpriteSystem(Registry& reg, VulkanRenderer& rend)
        : registry(reg), renderer(rend) {

        // Mark every newly-added SpriteRenderer as dirty so setupSprite() runs on the next frame
        registry.subscribeToAdded<SpriteRenderer>([this](Entity e) {
            if (auto* sprite = registry.get<SpriteRenderer>(e)) {
                sprite->_dirty = true;
            }
        });
    }

    // =========================================================================
    // update
    // =========================================================================

    void SpriteSystem::update(float /*dt*/) {
        for (auto [entity, sprite] : registry.view<SpriteRenderer>()) {
            if (sprite._dirty) {
                setupSprite(entity, sprite);
                sprite._dirty = false;
            } else {
                syncSprite(entity, sprite);
            }
        }
    }

    // =========================================================================
    // getOrCreateQuadMesh
    // =========================================================================

    uint32_t SpriteSystem::getOrCreateQuadMesh() {
        if (m_quadMeshId != 0) return m_quadMeshId;

        // Unit quad centred at origin, facing +Z (for 2D / orthographic use).
        // Vertex layout: position(xyz), normal(xyz), uv(xy), boneIDs(ivec4=0), boneWeights(vec4=0)
        // Winding: counter-clockwise when viewed from +Z.
        glm::vec3 normal(0.0f, 0.0f, 1.0f);

        std::vector<Vertex> verts = {
            Vertex(glm::vec3(-0.5f, -0.5f, 0.0f), normal, glm::vec2(0.0f, 1.0f)), // BL
            Vertex(glm::vec3( 0.5f, -0.5f, 0.0f), normal, glm::vec2(1.0f, 1.0f)), // BR
            Vertex(glm::vec3( 0.5f,  0.5f, 0.0f), normal, glm::vec2(1.0f, 0.0f)), // TR
            Vertex(glm::vec3(-0.5f,  0.5f, 0.0f), normal, glm::vec2(0.0f, 0.0f)), // TL
        };

        std::vector<uint32_t> indices = { 0, 1, 2,  2, 3, 0 };

        const size_t meshID = renderer.meshSoA.push(verts, indices);
        renderer.uploadMesh(meshID);

        m_quadMeshId = static_cast<uint32_t>(meshID);
        std::cout << "[SpriteSystem] Shared quad mesh created (id=" << m_quadMeshId << ")" << std::endl;
        return m_quadMeshId;
    }

    // =========================================================================
    // setupSprite
    // =========================================================================

    void SpriteSystem::setupSprite(Entity entity, SpriteRenderer& sprite) {
        // 1. Ensure Transform exists
        if (!registry.get<Transform>(entity)) {
            registry.emplace<Transform>(entity, Transform{});
        }

        // 2. Attach / update Mesh component → shared unit quad
        uint32_t qid = getOrCreateQuadMesh();
        auto* mesh = registry.get<Mesh>(entity);
        if (!mesh) {
            registry.emplace<Mesh>(entity, Mesh{});
            mesh = registry.get<Mesh>(entity);
        }
        mesh->id           = qid;
        mesh->gltfPath     = "";   // Procedural — no file path
        mesh->primitiveIndex = -1;
        mesh->vertices     = renderer.meshSoA.vertices[qid];
        mesh->indices      = renderer.meshSoA.indices[qid];
        mesh->vertexBuffer = renderer.meshSoA.vertexBuffers[qid].get();
        mesh->indexBuffer  = renderer.meshSoA.indexBuffers[qid].get();

        // 3. Attach / update Material component → sprite shader pipeline
        auto* mat = registry.get<Material>(entity);
        if (!mat) {
            registry.emplace<Material>(entity, Material{});
            mat = registry.get<Material>(entity);
        }
        mat->shaderName    = "Sprite";
        mat->texturePath   = sprite.texturePath;
        mat->color         = sprite.color;
        // Repurpose roughness/metallic as flipX/flipY sign (-1 or +1)
        mat->roughness     = sprite.flipX ? -1.0f : 1.0f;
        mat->metallic      = sprite.flipY ? -1.0f : 1.0f;

        // Build pipeline for sprite shaders
        if (mat->pipeline == VK_NULL_HANDLE) {
            PipelineHandle pipeline = renderer.createPipelineForShaders(
                renderer.resolveShaderPath("build/shaders/sprite.vert.spv"),
                renderer.resolveShaderPath("build/shaders/sprite.frag.spv")
            );
            mat->pipeline       = pipeline.pipeline;
            mat->pipelineLayout = pipeline.layout;
        }

        // Upload texture & update descriptor set
        renderer.resourceManager->updateMaterialDescriptorSet(*mat, renderer);

        // Track applied state
        sprite._loadedTexturePath = sprite.texturePath;
        sprite._lastFlipX         = sprite.flipX;
        sprite._lastFlipY         = sprite.flipY;
        sprite._lastSortOrder     = sprite.sortOrder;
    }

    // =========================================================================
    // syncSprite
    // =========================================================================

    void SpriteSystem::syncSprite(Entity entity, SpriteRenderer& sprite) {
        auto* mat = registry.get<Material>(entity);
        if (!mat) {
            // Material was somehow removed — mark dirty to recreate
            sprite._dirty = true;
            return;
        }

        bool changed = false;

        // Sync colour (cheap, done every frame)
        mat->color = sprite.color;

        // Sync flip signs (cheap float writes)
        float newFlipX = sprite.flipX ? -1.0f : 1.0f;
        float newFlipY = sprite.flipY ? -1.0f : 1.0f;
        if (mat->roughness != newFlipX || mat->metallic != newFlipY) {
            mat->roughness = newFlipX;
            mat->metallic  = newFlipY;
            changed = true;
        }

        // Sync texture if changed (triggers GPU upload)
        if (sprite.texturePath != sprite._loadedTexturePath) {
            mat->texturePath = sprite.texturePath;
            renderer.resourceManager->updateMaterialDescriptorSet(*mat, renderer);
            sprite._loadedTexturePath = sprite.texturePath;
            changed = true;
        }

        // Sync sort order
        if (sprite.sortOrder != sprite._lastSortOrder) {
            sprite._lastSortOrder = sprite.sortOrder;
            changed = true;
        }

        (void)changed;
    }

} // namespace Engine
