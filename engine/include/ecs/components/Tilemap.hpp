#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "core/EngineAPI.hpp"
#include "scenes/TilesetAsset.hpp"

namespace Engine {

    constexpr int TILE_CHUNK_SIZE = 16;

    struct TileChunk {
        int tiles[TILE_CHUNK_SIZE * TILE_CHUNK_SIZE];
        uint8_t rotations[TILE_CHUNK_SIZE * TILE_CHUNK_SIZE];

        TileChunk() {
            std::fill(std::begin(tiles), std::end(tiles), -1);
            std::fill(std::begin(rotations), std::end(rotations), 0);
        }

        bool isEmpty() const {
            for (int t : tiles) {
                if (t != -1) return false;
            }
            return true;
        }
    };

    inline int64_t packChunkKey(int cx, int cy) {
        return (static_cast<int64_t>(cx) << 32) | (static_cast<uint32_t>(cy));
    }

    inline void unpackChunkKey(int64_t key, int& cx, int& cy) {
        cx = static_cast<int>(key >> 32);
        cy = static_cast<int>(key & 0xFFFFFFFF);
    }

    struct TilemapLayer {
        std::string name;
        std::unordered_map<int64_t, TileChunk> chunks;
        float zOffset = 0.0f;
        std::string tag;
        bool isVisible = true;
        bool isCollision = false;

        // Legacy compatibility fields
        std::vector<int> tiles;
        std::vector<uint8_t> rotations;
    };

    /**
     * @struct TilemapComponent
     * @brief Represents an infinite 2D tile grid in the scene.
     *        References a tileset by its disk path (.tileset file).
     *        Tiles are stored sparsely in 16x16 chunks to allow painting anywhere in infinite space.
     */
    // [ReflectClass]
    struct ENGINE_API TilemapComponent {
        // [ReflectField]
        int width = 0;
        // [ReflectField]
        int height = 0;
        // [ReflectField]
        float tileSize = 1.0f;

        // [ReflectField]
        std::string tilesetPath;


        // Dynamic layers (handled manually in serialization and inspector UI)
        std::vector<TilemapLayer> layers;

        // Deprecated compatibility fields
        std::vector<int> tiles;
        std::vector<int> obstacleTiles;

        /** @brief Flag requesting mesh + collision rebuild next frame. */
        bool isDirty = true;

        TilemapComponent() {
            TilemapLayer defaultLayer;
            defaultLayer.name = "Ground";
            defaultLayer.zOffset = 0.0f;
            defaultLayer.tag = "ground";
            defaultLayer.isVisible = true;
            defaultLayer.isCollision = false;
            layers.push_back(defaultLayer);
        }

        int getTileFromLayer(const TilemapLayer& layer, int tileX, int tileY) const {
            int cx = static_cast<int>(std::floor(static_cast<float>(tileX) / TILE_CHUNK_SIZE));
            int cy = static_cast<int>(std::floor(static_cast<float>(tileY) / TILE_CHUNK_SIZE));
            int lx = (tileX % TILE_CHUNK_SIZE + TILE_CHUNK_SIZE) % TILE_CHUNK_SIZE;
            int ly = (tileY % TILE_CHUNK_SIZE + TILE_CHUNK_SIZE) % TILE_CHUNK_SIZE;
            int64_t key = packChunkKey(cx, cy);
            auto it = layer.chunks.find(key);
            if (it != layer.chunks.end()) {
                return it->second.tiles[ly * TILE_CHUNK_SIZE + lx];
            }
            return -1;
        }

        bool isTileSolid(int tileX, int tileY, const TilesetAsset* tileset = nullptr) const {
            for (const auto& layer : layers) {
                int tileId = getTileFromLayer(layer, tileX, tileY);
                if (tileId == -1) continue;

                if (layer.isCollision) return true;

                std::string lowerTag = layer.tag;
                for (char& c : lowerTag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lowerTag.find("obstacle") != std::string::npos || lowerTag.find("solid") != std::string::npos || lowerTag.find("collision") != std::string::npos) {
                    return true;
                }

                if (tileset && tileId >= 0 && tileId < static_cast<int>(tileset->tiles.size())) {
                    if (tileset->tiles[tileId].isSolid) return true;
                }
            }
            return false;
        }

        int getTile(size_t layerIdx, int tileX, int tileY) const {
            if (layerIdx >= layers.size()) return -1;
            return getTileFromLayer(layers[layerIdx], tileX, tileY);
        }

        uint8_t getRotation(size_t layerIdx, int tileX, int tileY) const {
            if (layerIdx >= layers.size()) return 0;
            const auto& layer = layers[layerIdx];
            int cx = static_cast<int>(std::floor(static_cast<float>(tileX) / TILE_CHUNK_SIZE));
            int cy = static_cast<int>(std::floor(static_cast<float>(tileY) / TILE_CHUNK_SIZE));
            int lx = (tileX % TILE_CHUNK_SIZE + TILE_CHUNK_SIZE) % TILE_CHUNK_SIZE;
            int ly = (tileY % TILE_CHUNK_SIZE + TILE_CHUNK_SIZE) % TILE_CHUNK_SIZE;
            int64_t key = packChunkKey(cx, cy);
            auto it = layer.chunks.find(key);
            if (it != layer.chunks.end()) {
                return it->second.rotations[ly * TILE_CHUNK_SIZE + lx];
            }
            return 0;
        }

        void setTile(size_t layerIdx, int tileX, int tileY, int tileId, uint8_t rotation = 0) {
            if (layerIdx >= layers.size()) return;
            auto& layer = layers[layerIdx];
            int cx = static_cast<int>(std::floor(static_cast<float>(tileX) / TILE_CHUNK_SIZE));
            int cy = static_cast<int>(std::floor(static_cast<float>(tileY) / TILE_CHUNK_SIZE));
            int lx = (tileX % TILE_CHUNK_SIZE + TILE_CHUNK_SIZE) % TILE_CHUNK_SIZE;
            int ly = (tileY % TILE_CHUNK_SIZE + TILE_CHUNK_SIZE) % TILE_CHUNK_SIZE;
            int64_t key = packChunkKey(cx, cy);

            if (tileId == -1) {
                auto it = layer.chunks.find(key);
                if (it != layer.chunks.end()) {
                    it->second.tiles[ly * TILE_CHUNK_SIZE + lx] = -1;
                    it->second.rotations[ly * TILE_CHUNK_SIZE + lx] = 0;
                    if (it->second.isEmpty()) {
                        layer.chunks.erase(it);
                    }
                }
            } else {
                auto& chunk = layer.chunks[key];
                chunk.tiles[ly * TILE_CHUNK_SIZE + lx] = tileId;
                chunk.rotations[ly * TILE_CHUNK_SIZE + lx] = rotation;
            }
            isDirty = true;
        }

        void getBounds(int& minX, int& minY, int& maxX, int& maxY) const {
            bool foundAny = false;
            int minTileX = 1000000000;
            int minTileY = 1000000000;
            int maxTileX = -1000000000;
            int maxTileY = -1000000000;

            for (const auto& layer : layers) {
                for (const auto& [key, chunk] : layer.chunks) {
                    int cx, cy;
                    unpackChunkKey(key, cx, cy);
                    for (int ly = 0; ly < TILE_CHUNK_SIZE; ++ly) {
                        for (int lx = 0; lx < TILE_CHUNK_SIZE; ++lx) {
                            if (chunk.tiles[ly * TILE_CHUNK_SIZE + lx] != -1) {
                                int wx = cx * TILE_CHUNK_SIZE + lx;
                                int wy = cy * TILE_CHUNK_SIZE + ly;
                                minTileX = std::min(minTileX, wx);
                                minTileY = std::min(minTileY, wy);
                                maxTileX = std::max(maxTileX, wx);
                                maxTileY = std::max(maxTileY, wy);
                                foundAny = true;
                            }
                        }
                    }
                }
            }

            if (foundAny) {
                minX = minTileX;
                minY = minTileY;
                maxX = maxTileX;
                maxY = maxTileY;
            } else {
                minX = 0;
                minY = 0;
                maxX = 0;
                maxY = 0;
            }
        }
    };

} // namespace Engine

#include "meta/ComponentReflection.hpp"
REGISTER_COMPONENT(Engine::TilemapComponent, "Rendering & Lights/Tilemap");

