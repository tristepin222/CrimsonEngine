#pragma once

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include "core/EngineAPI.hpp"
#include "ecs/Entity.hpp"
#include "meta/ComponentReflection.hpp"

namespace Engine {

    /**
     * @enum GridCellFlags
     * @brief Bitflags identifying environmental state, physical traits, and interaction rules for a grid cell.
     */
    enum GridCellFlags : uint32_t {
        GridCell_None          = 0,
        GridCell_Occupied      = 1 << 0,  // Contains a placed structure, object, or obstacle
        GridCell_Buildable     = 1 << 1,  // Valid surface for grid building and placement
        GridCell_Walkable      = 1 << 2,  // Traversable surface
        GridCell_Blocked       = 1 << 3,  // Unbuildable obstacle (cliff, deep water, world boundary)
        GridCell_Reserved      = 1 << 4   // Temporarily reserved by a queued placement action
    };

    /**
     * @struct GridCell
     * @brief Complete state of a single cell in the 3D world grid.
     */
    struct GridCell {
        uint32_t flags = GridCell_Buildable | GridCell_Walkable;
        uint16_t surfaceType = 0;   // Surface / terrain material tag
        int16_t  elevationLevel = 0;// Stepped vertical elevation level
        uint32_t occupantId = 0;    // Entity ID occupying this cell (0 = none)
        uint8_t  customData[4]{ 0, 0, 0, 0 }; // Generic 4-byte payload for gameplay logic

        bool isWalkable() const {
            return (flags & GridCell_Walkable) != 0 && (flags & GridCell_Occupied) == 0 && (flags & GridCell_Blocked) == 0;
        }

        bool isBuildable() const {
            return (flags & GridCell_Buildable) != 0 && (flags & GridCell_Occupied) == 0 && (flags & GridCell_Blocked) == 0;
        }

        bool isOccupied() const {
            return (flags & GridCell_Occupied) != 0 || occupantId != 0;
        }

        bool isBlocked() const {
            return (flags & GridCell_Blocked) != 0;
        }
    };

    // Standard chunk dimensions: 16x16 horizontal cells, 8 vertical layers
    constexpr int GRID_CHUNK_WIDTH  = 16; // X dimension
    constexpr int GRID_CHUNK_LENGTH = 16; // Z dimension
    constexpr int GRID_CHUNK_HEIGHT = 8;  // Y dimension
    constexpr int GRID_CHUNK_CELL_COUNT = GRID_CHUNK_WIDTH * GRID_CHUNK_LENGTH * GRID_CHUNK_HEIGHT;

    /**
     * @struct GridChunk
     * @brief 16x16x8 block of cells stored densely inside sparse chunk allocations.
     */
    struct GridChunk {
        std::array<GridCell, GRID_CHUNK_CELL_COUNT> cells{};
        bool isDirty = true;
        bool hasModifications = false;

        static inline int getCellIndex(int lx, int ly, int lz) {
            return (ly * GRID_CHUNK_LENGTH + lz) * GRID_CHUNK_WIDTH + lx;
        }

        GridCell& at(int lx, int ly, int lz) {
            return cells[getCellIndex(lx, ly, lz)];
        }

        const GridCell& at(int lx, int ly, int lz) const {
            return cells[getCellIndex(lx, ly, lz)];
        }
    };

    /**
     * @brief Packs chunk (cx, cz) coordinates into a single 64-bit spatial key.
     */
    inline int64_t packGridChunkKey(int cx, int cz) {
        return (static_cast<int64_t>(cx) << 32) | (static_cast<uint32_t>(cz));
    }

    /**
     * @brief Unpacks a 64-bit spatial key back into chunk (cx, cz) coordinates.
     */
    inline void unpackGridChunkKey(int64_t key, int& cx, int& cz) {
        cx = static_cast<int>(key >> 32);
        cz = static_cast<int>(key & 0xFFFFFFFF);
    }

    /**
     * @struct GridWorldComponent
     * @brief Root ECS component defining an extensible, chunked 3D spatial grid.
     * Handles spatial cell indexing, farming soil states, obstacle occupation,
     * and coordinate conversions for 3D retro simulation games.
     */
    // [ReflectClass("World/Grid World")]
    struct ENGINE_API GridWorldComponent {
        // [ReflectField]
        float cellSize = 1.0f;
        // [ReflectField]
        float cellHeight = 0.5f;
        // [ReflectField]
        glm::vec3 originOffset{ 0.0f, 0.0f, 0.0f };
        // [ReflectField]
        bool showGridOverlay = true;
        // [ReflectField]
        bool showCursorHover = true;
        // [ReflectField]
        glm::vec4 gridLineColor{ 0.35f, 0.7f, 1.0f, 0.3f };
        // [ReflectField]
        glm::vec4 cursorHoverColor{ 0.2f, 1.0f, 0.45f, 0.65f };

        // Sparse chunk storage: chunkKey (cx, cz) -> GridChunk
        std::unordered_map<int64_t, GridChunk> chunks;

        /** @brief Global dirty flag indicating cell or chunk state was modified. */
        bool isDirty = true;

        // -------------------------------------------------------------------
        // Coordinate Transforms
        // -------------------------------------------------------------------

        /**
         * @brief Converts a 3D world space position into discrete grid cell coordinates.
         */
        glm::ivec3 worldToCell(const glm::vec3& worldPos) const {
            glm::vec3 local = worldPos - originOffset;
            int x = static_cast<int>(std::floor(local.x / (cellSize > 0.001f ? cellSize : 1.0f)));
            int y = static_cast<int>(std::floor(local.y / (cellHeight > 0.001f ? cellHeight : 0.5f)));
            int z = static_cast<int>(std::floor(local.z / (cellSize > 0.001f ? cellSize : 1.0f)));
            return glm::ivec3(x, y, z);
        }

        /**
         * @brief Converts discrete grid cell coordinates into minimum world-space corner position.
         */
        glm::vec3 cellToWorld(const glm::ivec3& cellCoord) const {
            return glm::vec3(
                static_cast<float>(cellCoord.x) * cellSize,
                static_cast<float>(cellCoord.y) * cellHeight,
                static_cast<float>(cellCoord.z) * cellSize
            ) + originOffset;
        }

        /**
         * @brief Returns the center point in world space for a given cell coordinate.
         */
        glm::vec3 getCellCenter(const glm::ivec3& cellCoord) const {
            return cellToWorld(cellCoord) + glm::vec3(cellSize * 0.5f, cellHeight * 0.5f, cellSize * 0.5f);
        }

        /**
         * @brief Maps cell coordinates to its parent chunk and intra-chunk local coordinates.
         */
        void cellToChunk(const glm::ivec3& cellCoord, int& cx, int& cz, int& lx, int& ly, int& lz) const {
            cx = static_cast<int>(std::floor(static_cast<float>(cellCoord.x) / static_cast<float>(GRID_CHUNK_WIDTH)));
            cz = static_cast<int>(std::floor(static_cast<float>(cellCoord.z) / static_cast<float>(GRID_CHUNK_LENGTH)));

            lx = (cellCoord.x % GRID_CHUNK_WIDTH + GRID_CHUNK_WIDTH) % GRID_CHUNK_WIDTH;
            lz = (cellCoord.z % GRID_CHUNK_LENGTH + GRID_CHUNK_LENGTH) % GRID_CHUNK_LENGTH;

            // Clamp local elevation layer to chunk vertical range [0, GRID_CHUNK_HEIGHT - 1]
            ly = std::clamp(cellCoord.y, 0, GRID_CHUNK_HEIGHT - 1);
        }

        // -------------------------------------------------------------------
        // Cell Accessors
        // -------------------------------------------------------------------

        /**
         * @brief Looks up a cell by coordinate. Returns nullptr if chunk or cell is unallocated.
         */
        const GridCell* getCell(const glm::ivec3& cellCoord) const {
            int cx, cz, lx, ly, lz;
            cellToChunk(cellCoord, cx, cz, lx, ly, lz);
            int64_t key = packGridChunkKey(cx, cz);
            auto it = chunks.find(key);
            if (it != chunks.end()) {
                return &it->second.at(lx, ly, lz);
            }
            return nullptr;
        }

        /**
         * @brief Looks up a mutable cell by coordinate. Returns nullptr if unallocated.
         */
        GridCell* getCell(const glm::ivec3& cellCoord) {
            int cx, cz, lx, ly, lz;
            cellToChunk(cellCoord, cx, cz, lx, ly, lz);
            int64_t key = packGridChunkKey(cx, cz);
            auto it = chunks.find(key);
            if (it != chunks.end()) {
                return &it->second.at(lx, ly, lz);
            }
            return nullptr;
        }

        /**
         * @brief Retrieves cell, allocating its chunk if it does not yet exist.
         */
        GridCell& getOrCreateCell(const glm::ivec3& cellCoord) {
            int cx, cz, lx, ly, lz;
            cellToChunk(cellCoord, cx, cz, lx, ly, lz);
            int64_t key = packGridChunkKey(cx, cz);
            auto& chunk = chunks[key];
            chunk.hasModifications = true;
            chunk.isDirty = true;
            isDirty = true;
            return chunk.at(lx, ly, lz);
        }

        /**
         * @brief Sets full cell data at the given coordinate.
         */
        bool setCell(const glm::ivec3& cellCoord, const GridCell& cell) {
            GridCell& target = getOrCreateCell(cellCoord);
            target = cell;
            return true;
        }

        /**
         * @brief Modifies bitflags on an existing or new cell.
         */
        bool modifyCellFlags(const glm::ivec3& cellCoord, uint32_t setFlags, uint32_t clearFlags) {
            GridCell& cell = getOrCreateCell(cellCoord);
            cell.flags = (cell.flags & ~clearFlags) | setFlags;
            return true;
        }

        /**
         * @brief Computes the active bounding box across all allocated chunks.
         */
        void getBounds(glm::ivec3& minCell, glm::ivec3& maxCell) const {
            if (chunks.empty()) {
                minCell = glm::ivec3(0);
                maxCell = glm::ivec3(0);
                return;
            }

            int minCX = 1000000, maxCX = -1000000;
            int minCZ = 1000000, maxCZ = -1000000;

            for (const auto& [key, chunk] : chunks) {
                int cx, cz;
                unpackGridChunkKey(key, cx, cz);
                minCX = std::min(minCX, cx);
                maxCX = std::max(maxCX, cx);
                minCZ = std::min(minCZ, cz);
                maxCZ = std::max(maxCZ, cz);
            }

            minCell = glm::ivec3(minCX * GRID_CHUNK_WIDTH, 0, minCZ * GRID_CHUNK_LENGTH);
            maxCell = glm::ivec3((maxCX + 1) * GRID_CHUNK_WIDTH - 1, GRID_CHUNK_HEIGHT - 1, (maxCZ + 1) * GRID_CHUNK_LENGTH - 1);
        }

        /**
         * @brief Snaps a world position to the nearest grid cell horizontal center.
         */
        glm::vec3 snapPositionToGrid(const glm::vec3& worldPos) const {
            glm::ivec3 cell = worldToCell(worldPos);
            glm::vec3 center = getCellCenter(cell);
            center.y = worldPos.y; // Preserve vertical elevation
            return center;
        }

        /**
         * @brief Checks if any vertical cell layer at the given (gx, gz) column is blocked.
         */
        bool isColumnBlocked(int gx, int gz) const {
            int cx, cz, lx, dummyY, lz;
            cellToChunk(glm::ivec3(gx, 0, gz), cx, cz, lx, dummyY, lz);
            int64_t key = packGridChunkKey(cx, cz);
            auto it = chunks.find(key);
            if (it == chunks.end()) return false;
            for (int ly = 0; ly < GRID_CHUNK_HEIGHT; ++ly) {
                if (it->second.at(lx, ly, lz).isBlocked()) {
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Checks if any vertical cell layer at the given (gx, gz) column is occupied by a structure/object.
         */
        bool isColumnOccupied(int gx, int gz) const {
            int cx, cz, lx, dummyY, lz;
            cellToChunk(glm::ivec3(gx, 0, gz), cx, cz, lx, dummyY, lz);
            int64_t key = packGridChunkKey(cx, cz);
            auto it = chunks.find(key);
            if (it == chunks.end()) return false;
            for (int ly = 0; ly < GRID_CHUNK_HEIGHT; ++ly) {
                if (it->second.at(lx, ly, lz).isOccupied()) {
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Checks if the entire column at (gx, gz) is free of obstacles and occupants.
         */
        bool isColumnWalkable(int gx, int gz) const {
            int cx, cz, lx, dummyY, lz;
            cellToChunk(glm::ivec3(gx, 0, gz), cx, cz, lx, dummyY, lz);
            int64_t key = packGridChunkKey(cx, cz);
            auto it = chunks.find(key);
            if (it == chunks.end()) return true;
            for (int ly = 0; ly < GRID_CHUNK_HEIGHT; ++ly) {
                const auto& c = it->second.at(lx, ly, lz);
                if (c.isBlocked() || c.isOccupied()) {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief Clears all blocked flags and occupant data across all vertical layers in a column.
         */
        void clearColumn(int gx, int gz) {
            int cx, cz, lx, dummyY, lz;
            cellToChunk(glm::ivec3(gx, 0, gz), cx, cz, lx, dummyY, lz);
            int64_t key = packGridChunkKey(cx, cz);
            auto it = chunks.find(key);
            if (it != chunks.end()) {
                for (int ly = 0; ly < GRID_CHUNK_HEIGHT; ++ly) {
                    auto& c = it->second.at(lx, ly, lz);
                    c.flags &= ~(GridCell_Blocked | GridCell_Occupied);
                    c.flags |= (GridCell_Buildable | GridCell_Walkable);
                    c.occupantId = 0;
                }
                it->second.hasModifications = true;
                it->second.isDirty = true;
                isDirty = true;
            }
        }

        /**
         * @brief Checks if a rectangular footprint is free and buildable.
         */
        bool isAreaBuildable(const glm::ivec2& originCellXZ, int elevationY, const glm::ivec2& footprintSize) const {
            for (int dx = 0; dx < footprintSize.x; ++dx) {
                for (int dz = 0; dz < footprintSize.y; ++dz) {
                    glm::ivec3 coord(originCellXZ.x + dx, elevationY, originCellXZ.y + dz);
                    const GridCell* cell = getCell(coord);
                    if (cell && !cell->isBuildable()) {
                        return false;
                    }
                }
            }
            return true;
        }

        /**
         * @brief Sets or clears occupancy for a rectangular footprint of cells.
         */
        bool setAreaOccupied(const glm::ivec2& originCellXZ, int elevationY, const glm::ivec2& footprintSize, uint32_t entityId, bool occupied) {
            for (int dx = 0; dx < footprintSize.x; ++dx) {
                for (int dz = 0; dz < footprintSize.y; ++dz) {
                    glm::ivec3 coord(originCellXZ.x + dx, elevationY, originCellXZ.y + dz);
                    GridCell& cell = getOrCreateCell(coord);
                    if (occupied) {
                        cell.flags |= GridCell_Occupied;
                        cell.flags &= ~GridCell_Buildable;
                        cell.occupantId = entityId;
                    } else {
                        cell.flags &= ~GridCell_Occupied;
                        cell.flags |= GridCell_Buildable;
                        if (cell.occupantId == entityId || entityId == 0) {
                            cell.occupantId = 0;
                        }
                    }
                }
            }
            isDirty = true;
            return true;
        }

        /**
         * @brief Sets or clears the blocked flag for a single cell.
         */
        void setCellBlocked(const glm::ivec3& coord, bool blocked) {
            GridCell& cell = getOrCreateCell(coord);
            if (blocked) {
                cell.flags |= GridCell_Blocked;
                cell.flags &= ~(GridCell_Buildable | GridCell_Walkable);
            } else {
                cell.flags &= ~GridCell_Blocked;
                cell.flags |= (GridCell_Buildable | GridCell_Walkable);
            }
            isDirty = true;
        }

        /**
         * @brief Sets or clears the blocked flag for a rectangular footprint of cells.
         */
        bool setAreaBlocked(const glm::ivec2& originCellXZ, int elevationY, const glm::ivec2& footprintSize, bool blocked) {
            for (int dx = 0; dx < footprintSize.x; ++dx) {
                for (int dz = 0; dz < footprintSize.y; ++dz) {
                    glm::ivec3 coord(originCellXZ.x + dx, elevationY, originCellXZ.y + dz);
                    setCellBlocked(coord, blocked);
                }
            }
            isDirty = true;
            return true;
        }
    };

    REGISTER_COMPONENT(GridWorldComponent, "World/Grid World");

} // namespace Engine
