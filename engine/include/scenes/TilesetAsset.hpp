#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "core/EngineAPI.hpp"
#include <vulkan/vulkan.h>

class VulkanRenderer;
class ResourceManager;

namespace Engine {

    /**
     * @struct TileAsset
     * @brief Represents one tile slot in a tileset.
     *        Saved to disk as a small <name>.tile JSON file.
     */
    struct ENGINE_API TileAsset {
        /** @brief Slot index in the parent tileset (== index in TilesetAsset::tiles). */
        int id = -1;
        /** @brief Display name shown in the palette. */
        std::string name;
        /** @brief Project-relative path to the tile's texture image. */
        std::string texturePath;
        /** @brief True = solid collider spawned for cells containing this tile. */
        bool isSolid = false;
        /** @brief RGBA tint color multiplier for this tile. */
        glm::vec4 colorTint{ 1.f, 1.f, 1.f, 1.f };

        // --- Infinite palette grid position (persisted) ---
        /** @brief Column on the infinite palette grid (can be negative). */
        int gridX = 0;
        /** @brief Row on the infinite palette grid (can be negative). */
        int gridY = 0;

        // --- Runtime atlas data (not persisted) ---
        /** @brief Normalised UV rect in the runtime atlas: (u0, v0, u1, v1). */
        glm::vec4 atlasUV{ 0.f, 0.f, 1.f, 1.f };
    };

    /**
     * @struct TilesetAsset
     * @brief A disk asset (.tileset JSON) that owns an ordered list of TileAssets.
     *        At runtime, a CPU-side atlas is built by packing every tile texture
     *        into a single RGBA image, which is uploaded to Vulkan once.
     *        This gives the TilemapSystem a single draw call per tilemap.
     */
    struct ENGINE_API TilesetAsset {
        /** @brief Display name of the tileset. */
        std::string name;
        /** @brief Absolute (or project-relative) path to the .tileset file. */
        std::string filePath;
        /** @brief Width of each tile cell in the packed atlas (pixels). */
        int tileWidth  = 16;
        /** @brief Height of each tile cell in the packed atlas (pixels). */
        int tileHeight = 16;

        /**
         * @brief Ordered list of tile slots.
         *        tiles[i].id == i. Index is the value stored in TilemapComponent::tiles[].
         */
        std::vector<TileAsset> tiles;

        // --- Runtime atlas (rebuilt when tileset changes) ---
        struct ENGINE_API AtlasCache {
            /** @brief Packed atlas pixel data (RGBA8, CPU side). */
            std::vector<uint8_t> pixels;
            int atlasWidth  = 0;
            int atlasHeight = 0;
            /** @brief Vulkan descriptor set for the atlas texture (owned by ResourceManager). */
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            /** @brief Vulkan descriptor set with single-texture binding (for ImGui/UI). */
            VkDescriptorSet singleDescriptorSet = VK_NULL_HANDLE;
            bool valid = false;
            bool atlasBuilt = false;
        };
        AtlasCache atlas;

        // ------------------------------------------------------------------
        // Disk I/O helpers
        // ------------------------------------------------------------------

        /**
         * @brief Loads a TilesetAsset from a .tileset JSON file.
         *        Individual tile paths listed in the file are loaded as TileAssets.
         * @param path Path to the .tileset file.
         * @return Loaded TilesetAsset (name and filePath set; tiles populated).
         *         Returns a default-constructed asset on error.
         */
        static TilesetAsset loadFromFile(const std::string& path);

        /**
         * @brief Saves the TilesetAsset to its filePath on disk (.tileset JSON).
         *        Does NOT write individual .tile files; use saveTileFile() for that.
         * @param ts The tileset to persist.
         */
        static void saveToFile(const TilesetAsset& ts);

        /**
         * @brief Loads a single TileAsset from a .tile JSON file.
         * @param path Path to the .tile file.
         * @return Loaded TileAsset.
         */
        static TileAsset loadTileFile(const std::string& path);

        /**
         * @brief Saves a single TileAsset to a .tile JSON file.
         * @param tile  The tile to persist.
         * @param path  Destination .tile file path.
         */
        static void saveTileFile(const TileAsset& tile, const std::string& path);

        // ------------------------------------------------------------------
        // Atlas building
        // ------------------------------------------------------------------

        /**
         * @brief Rebuilds the CPU-side atlas from tiles[].texturePath entries,
         *        uploads it as a Vulkan texture, and fills tiles[].atlasUV.
         *        If a tile's texture cannot be loaded, a solid magenta 1×1 tile is used.
         * @param renderer Active VulkanRenderer (used for GPU upload).
         */
        void buildAtlas(VulkanRenderer& renderer);

        /**
         * @brief Frees the GPU-side atlas resources if they exist.
         * @param renderer Active VulkanRenderer.
         */
        void destroyAtlas(VulkanRenderer& renderer);
    };

    // ------------------------------------------------------------------
    // Global in-memory cache (keyed by canonical file path)
    // ------------------------------------------------------------------

    /**
     * @brief Returns a reference to the process-wide TilesetAsset cache.
     *        The TilemapSystem uses this to avoid reloading/repacking every frame.
     */
    ENGINE_API std::unordered_map<std::string, TilesetAsset>& getTilesetCache();

    /**
     * @brief Resolves a tile texture path against common asset locations and tileset relative directories.
     */
    ENGINE_API std::string resolveTileTexturePath(const std::string& rawPath, const std::string& tilesetPath = "");

    /**
     * @brief Loads a TilesetAsset from disk (or returns a cached copy).
     *        If the tileset file changes on disk, call invalidateTilesetCache() first.
     * @param path Project-relative path to the .tileset file.
     * @return Pointer to the cached TilesetAsset, or nullptr if the file cannot be read.
     */
    ENGINE_API TilesetAsset* loadOrGetTileset(const std::string& path, VulkanRenderer& renderer);

    /**
     * @brief Evicts the specified entry (or all entries if path is empty) from the cache.
     */
    ENGINE_API void invalidateTilesetCache(const std::string& path = "");

} // namespace Engine
