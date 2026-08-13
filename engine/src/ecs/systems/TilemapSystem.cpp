#include "ecs/systems/TilemapSystem.hpp"
#include "scenes/TilesetAsset.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Collider.hpp"
#include "ecs/components/Material.hpp"
#include "ecs/components/Mesh.hpp"
#include "renderer/ResourceManager.hpp"
#include <iostream>
#include <algorithm>

namespace Engine {

    TilemapSystem::TilemapSystem(Registry& reg, VulkanRenderer& rend)
        : registry(reg), renderer(rend) {
        registry.subscribeToAdded<TilemapComponent>([this](Entity e) {
            if (auto* tilemap = registry.get<TilemapComponent>(e)) {
                tilemap->isDirty = true;
            }
        });
    }

    void TilemapSystem::update(float dt) {
        for (auto [entity, tilemap] : registry.view<TilemapComponent>()) {
            if (tilemap.isDirty) {
                rebuildTilemap(entity, tilemap);
            }
        }
    }

    void TilemapSystem::rebuildTilemap(Entity entity, TilemapComponent& tilemap) {
        // 1. Load the TilesetAsset from disk (cached after first load)
        if (tilemap.tilesetPath.empty()) {
            tilemap.isDirty = false;
            return;
        }

        TilesetAsset* tileset = loadOrGetTileset(tilemap.tilesetPath, renderer);
        if (!tileset || !tileset->atlas.valid || tileset->tiles.empty()) {
            tilemap.isDirty = false;
            return;
        }

        // 2. Build single-mesh geometry — iterate over all populated chunks in each layer
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        for (const auto& layer : tilemap.layers) {
            if (!layer.isVisible) continue;

            for (const auto& [key, chunk] : layer.chunks) {
                int cx, cy;
                unpackChunkKey(key, cx, cy);

                for (int ly = 0; ly < TILE_CHUNK_SIZE; ++ly) {
                    for (int lx = 0; lx < TILE_CHUNK_SIZE; ++lx) {
                        int cellIdx = ly * TILE_CHUNK_SIZE + lx;
                        int tileIdx = chunk.tiles[cellIdx];
                        if (tileIdx < 0 || tileIdx >= static_cast<int>(tileset->tiles.size())) continue;

                        int wx = cx * TILE_CHUNK_SIZE + lx;
                        int wy = cy * TILE_CHUNK_SIZE + ly;

                        // UV rectangle for this tile inside the atlas
                        const glm::vec4& uv = tileset->tiles[tileIdx].atlasUV;
                        float u0 = uv.x, v0 = uv.y, u1 = uv.z, v1 = uv.w;

                        uint8_t rot = chunk.rotations[cellIdx];
                        glm::vec2 uvBL(u0, v1);
                        glm::vec2 uvBR(u1, v1);
                        glm::vec2 uvTR(u1, v0);
                        glm::vec2 uvTL(u0, v0);

                        if (rot == 1) {      // 90 deg clockwise
                            uvBL = glm::vec2(u0, v0);
                            uvBR = glm::vec2(u0, v1);
                            uvTR = glm::vec2(u1, v1);
                            uvTL = glm::vec2(u1, v0);
                        } else if (rot == 2) { // 180 deg
                            uvBL = glm::vec2(u1, v0);
                            uvBR = glm::vec2(u0, v0);
                            uvTR = glm::vec2(u0, v1);
                            uvTL = glm::vec2(u1, v1);
                        } else if (rot == 3) { // 270 deg clockwise
                            uvBL = glm::vec2(u1, v1);
                            uvBR = glm::vec2(u1, v0);
                            uvTR = glm::vec2(u0, v0);
                            uvTL = glm::vec2(u0, v1);
                        }

                        // World-space quad corners for this cell (applying layer's zOffset)
                        float tx0 = wx * tilemap.tileSize;
                        float ty0 = wy * tilemap.tileSize;
                        float tx1 = (wx + 1) * tilemap.tileSize;
                        float ty1 = (wy + 1) * tilemap.tileSize;

                        uint32_t vOff = static_cast<uint32_t>(vertices.size());
                        glm::vec3 normal(0.0f, 0.0f, 1.0f);

                        vertices.push_back(Vertex(glm::vec3(tx0, ty0, layer.zOffset), normal, uvBL)); // BL
                        vertices.push_back(Vertex(glm::vec3(tx1, ty0, layer.zOffset), normal, uvBR)); // BR
                        vertices.push_back(Vertex(glm::vec3(tx1, ty1, layer.zOffset), normal, uvTR)); // TR
                        vertices.push_back(Vertex(glm::vec3(tx0, ty1, layer.zOffset), normal, uvTL)); // TL

                        indices.push_back(vOff + 0); indices.push_back(vOff + 1); indices.push_back(vOff + 2);
                        indices.push_back(vOff + 2); indices.push_back(vOff + 3); indices.push_back(vOff + 0);
                    }
                }
            }
        }

        if (!vertices.empty() && !indices.empty()) {
            // 3. Upload geometry to the GPU
            auto* mesh = registry.get<Mesh>(entity);
            if (!mesh) {
                registry.emplace<Mesh>(entity, Mesh{});
                mesh = registry.get<Mesh>(entity);
            }

            mesh->vertices = std::move(vertices);
            mesh->indices  = std::move(indices);

            bool alreadyUploaded = (mesh->vertexBuffer != VK_NULL_HANDLE)
                                 && (mesh->id < static_cast<uint32_t>(renderer.meshSoA.ids.size()));
            if (alreadyUploaded) {
                // Overwrite existing SoA entry and re-upload
                renderer.meshSoA.vertices[mesh->id] = mesh->vertices;
                renderer.meshSoA.indices[mesh->id]  = mesh->indices;
                renderer.uploadMesh(mesh->id);
                mesh->vertexBuffer = renderer.meshSoA.vertexBuffers[mesh->id].get();
                mesh->indexBuffer  = renderer.meshSoA.indexBuffers[mesh->id].get();
            } else {
                const size_t meshID = renderer.meshSoA.push(mesh->vertices, mesh->indices);
                renderer.uploadMesh(meshID);
                mesh->id           = static_cast<uint32_t>(meshID);
                mesh->vertexBuffer = renderer.meshSoA.vertexBuffers[meshID].get();
                mesh->indexBuffer  = renderer.meshSoA.indexBuffers[meshID].get();
            }
        }

        // 4. Configure material — bind the atlas descriptor set
        auto* mat = registry.get<Material>(entity);
        if (!mat) {
            registry.emplace<Material>(entity, Material{});
            mat = registry.get<Material>(entity);
        }
        mat->color         = glm::vec4(1.f);
        mat->texturePath   = "tileset_atlas:" + tilemap.tilesetPath;
        mat->descriptorSet = tileset->atlas.descriptorSet;
        mat->filterMode    = TextureFilterMode::Nearest;

        if (mat->pipeline == VK_NULL_HANDLE) {
            PipelineHandle pipeline = renderer.createPipelineForShaders(
                renderer.resolveShaderPath("build/shaders/unlit.vert.spv"),
                renderer.resolveShaderPath("build/shaders/unlit.frag.spv")
            );
            mat->pipeline       = pipeline.pipeline;
            mat->pipelineLayout = pipeline.layout;
        }

        if (!registry.get<Transform>(entity)) {
            registry.emplace<Transform>(entity, Transform{});
        }

        // 5. Clean up any legacy child colliders (physics is now solved directly in O(1) by PhysicsSystem)
        std::vector<Entity> toDestroy;
        for (auto [child, hierarchy] : registry.view<HierarchyComponent>()) {
            if (hierarchy.parent == entity) {
                if (auto* nameComp = registry.get<Name>(child)) {
                    if (nameComp->value.rfind("TileCollider_", 0) == 0) {
                        toDestroy.push_back(child);
                    }
                }
            }
        }
        for (Entity child : toDestroy) registry.destroy(child);

        tilemap.isDirty = false;

        tilemap.isDirty = false;
    }

} // namespace Engine
