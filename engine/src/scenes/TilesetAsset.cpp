#include "scenes/TilesetAsset.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "renderer/ResourceManager.hpp"
#include "scenes/JSONUtils.hpp"

#include "stb_image.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <cstring>

namespace Engine {

// ---------------------------------------------------------------------------
// Process-wide cache
// ---------------------------------------------------------------------------
std::unordered_map<std::string, TilesetAsset>& getTilesetCache() {
    static std::unordered_map<std::string, TilesetAsset> s_cache;
    return s_cache;
}

void invalidateTilesetCache(const std::string& path) {
    (void)path;
    auto& cache = getTilesetCache();
    cache.clear();
}

TilesetAsset* loadOrGetTileset(const std::string& rawPath, VulkanRenderer& renderer) {
    if (rawPath.empty()) return nullptr;

    std::string normPath = rawPath;
    if (std::filesystem::exists(normPath)) {
        try {
            normPath = std::filesystem::absolute(normPath).generic_string();
        } catch (...) {}
    } else {
        std::vector<std::string> candidates = {
            "sandbox_game/" + rawPath,
            "assets/tilesets/" + std::filesystem::path(rawPath).filename().string(),
            "sandbox_game/assets/tilesets/" + std::filesystem::path(rawPath).filename().string()
        };
        for (const auto& cand : candidates) {
            if (std::filesystem::exists(cand)) {
                try {
                    normPath = std::filesystem::absolute(cand).generic_string();
                } catch (...) {
                    normPath = cand;
                }
                break;
            }
        }
    }

    auto& cache = getTilesetCache();

    auto it = cache.find(normPath);
    if (it != cache.end()) {
        if (!it->second.atlas.valid) {
            it->second.buildAtlas(renderer);
        }
        return &it->second;
    }

    auto itRaw = cache.find(rawPath);
    if (itRaw != cache.end()) {
        if (!itRaw->second.atlas.valid) {
            itRaw->second.buildAtlas(renderer);
        }
        return &itRaw->second;
    }

    TilesetAsset ts = TilesetAsset::loadFromFile(normPath);
    if (ts.filePath.empty()) {
        ts = TilesetAsset::loadFromFile(rawPath);
    }
    if (ts.filePath.empty()) {
        std::cerr << "[TilesetAsset] Failed to load tileset from: " << rawPath << std::endl;
        return nullptr;
    }

    ts.buildAtlas(renderer);
    cache[normPath] = ts;
    cache[rawPath]  = std::move(ts);
    return &cache[normPath];
}

// ---------------------------------------------------------------------------
// Disk I/O — .tile files
// ---------------------------------------------------------------------------

TileAsset TilesetAsset::loadTileFile(const std::string& path) {
    TileAsset tile;
    if (!std::filesystem::exists(path)) return tile;

    std::ifstream f(path);
    if (!f.is_open()) return tile;

    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    float idVal = -1.f;
    JSONUtils::extractFloatValue(json, "id", idVal);
    tile.id          = static_cast<int>(idVal);
    tile.name        = JSONUtils::extractStringValue(json, "name");
    tile.texturePath = JSONUtils::extractStringValue(json, "texturePath");
    if (!tile.texturePath.empty()) {
        tile.texturePath = resolveTileTexturePath(tile.texturePath, path);
    }

    float solidVal = 0.f;
    JSONUtils::extractFloatValue(json, "isSolid", solidVal);
    tile.isSolid = solidVal != 0.f;

    float gx = 0.f, gy = 0.f;
    JSONUtils::extractFloatValue(json, "gridX", gx);
    JSONUtils::extractFloatValue(json, "gridY", gy);
    tile.gridX = static_cast<int>(gx);
    tile.gridY = static_cast<int>(gy);

    float tr = 1.f, tg = 1.f, tb = 1.f, ta = 1.f;
    JSONUtils::extractFloatValue(json, "colorTintR", tr);
    JSONUtils::extractFloatValue(json, "colorTintG", tg);
    JSONUtils::extractFloatValue(json, "colorTintB", tb);
    JSONUtils::extractFloatValue(json, "colorTintA", ta);
    tile.colorTint = glm::vec4(tr, tg, tb, ta);

    return tile;
}

void TilesetAsset::saveTileFile(const TileAsset& tile, const std::string& path) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[TilesetAsset] Cannot write .tile file: " << path << std::endl;
        return;
    }
    f << "{\n";
    f << "  \"id\": " << tile.id << ",\n";
    f << "  \"name\": \"" << tile.name << "\",\n";
    f << "  \"texturePath\": \"" << tile.texturePath << "\",\n";
    f << "  \"isSolid\": " << (tile.isSolid ? 1 : 0) << ",\n";
    f << "  \"gridX\": " << tile.gridX << ",\n";
    f << "  \"gridY\": " << tile.gridY << ",\n";
    f << "  \"colorTintR\": " << tile.colorTint.r << ",\n";
    f << "  \"colorTintG\": " << tile.colorTint.g << ",\n";
    f << "  \"colorTintB\": " << tile.colorTint.b << ",\n";
    f << "  \"colorTintA\": " << tile.colorTint.a << "\n";
    f << "}\n";
}

// ---------------------------------------------------------------------------
// Disk I/O — .tileset files
// ---------------------------------------------------------------------------

TilesetAsset TilesetAsset::loadFromFile(const std::string& rawPath) {
    TilesetAsset ts;
    std::string path = rawPath;
    if (!std::filesystem::exists(path)) {
        std::vector<std::string> candidates = {
            "sandbox_game/" + rawPath,
            "assets/tilesets/" + std::filesystem::path(rawPath).filename().string(),
            "sandbox_game/assets/tilesets/" + std::filesystem::path(rawPath).filename().string()
        };
        for (const auto& cand : candidates) {
            if (std::filesystem::exists(cand)) {
                path = cand;
                break;
            }
        }
    }
    if (!std::filesystem::exists(path)) return ts;

    std::ifstream f(path);
    if (!f.is_open()) return ts;

    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    ts.name     = JSONUtils::extractStringValue(json, "name");
    ts.filePath = path;

    float tw = 16.f, th = 16.f;
    JSONUtils::extractFloatValue(json, "tileWidth",  tw);
    JSONUtils::extractFloatValue(json, "tileHeight", th);
    ts.tileWidth  = static_cast<int>(tw);
    ts.tileHeight = static_cast<int>(th);

    // Parse the "tiles" array — array of .tile file paths
    std::vector<std::string> tilePaths;
    JSONUtils::extractStringVector(json, "tiles", tilePaths);

    // Resolve relative paths to be relative to the .tileset file's parent directory
    std::filesystem::path tsDir = std::filesystem::path(path).parent_path();
    int idx = 0;
    for (const auto& tp : tilePaths) {
        std::string resolvedPath = tp;
        if (!std::filesystem::exists(resolvedPath)) {
            // Fallback 1: Try relative to tileset parent folder
            std::string tempPath = (tsDir / tp).generic_string();
            if (std::filesystem::exists(tempPath)) {
                resolvedPath = tempPath;
            } else {
                // Fallback 2: Try inside the subdirectory named after the tileset (e.g. tsDir/ts.name/filename.tile)
                std::string fname = std::filesystem::path(tp).filename().string();
                std::string subDirPath = (tsDir / ts.name / fname).generic_string();
                if (std::filesystem::exists(subDirPath)) {
                    resolvedPath = subDirPath;
                } else {
                    // Default back to relative path if not found
                    resolvedPath = tempPath;
                }
            }
        }
        TileAsset tile = loadTileFile(resolvedPath);
        tile.id = idx++;
        ts.tiles.push_back(std::move(tile));
    }

    return ts;
}

void TilesetAsset::saveToFile(const TilesetAsset& ts) {
    if (ts.filePath.empty()) {
        std::cerr << "[TilesetAsset] Cannot save tileset: filePath is empty." << std::endl;
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(ts.filePath).parent_path());
    std::ofstream f(ts.filePath);
    if (!f.is_open()) {
        std::cerr << "[TilesetAsset] Cannot write .tileset file: " << ts.filePath << std::endl;
        return;
    }

    f << "{\n";
    f << "  \"name\": \"" << ts.name << "\",\n";
    f << "  \"tileWidth\": " << ts.tileWidth << ",\n";
    f << "  \"tileHeight\": " << ts.tileHeight << ",\n";
    f << "  \"tiles\": [\n";

    std::filesystem::path tsDir = std::filesystem::path(ts.filePath).parent_path();
    for (size_t i = 0; i < ts.tiles.size(); ++i) {
        // Store paths relative to the .tileset file's directory (inside the subdirectory named after the tileset)
        std::string tilePath = (std::filesystem::path(ts.name) / (ts.tiles[i].name + ".tile")).generic_string();
        std::filesystem::path absTilePath = tsDir / tilePath;
        std::string storedPath = absTilePath.generic_string();
        f << "    \"" << storedPath << "\"";
        if (i + 1 < ts.tiles.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n";
    f << "}\n";
}

// ---------------------------------------------------------------------------
// Atlas building
// ---------------------------------------------------------------------------

std::string resolveTileTexturePath(const std::string& rawPath, const std::string& tilesetPath) {
    if (rawPath.empty()) return "";
    if (std::filesystem::exists(rawPath)) return rawPath;

    static std::unordered_map<std::string, std::string> s_pathCache;
    auto it = s_pathCache.find(rawPath);
    if (it != s_pathCache.end() && std::filesystem::exists(it->second)) {
        return it->second;
    }

    std::vector<std::string> searchPaths;
    if (!tilesetPath.empty()) {
        std::filesystem::path tsDir = std::filesystem::path(tilesetPath).parent_path();
        searchPaths.push_back((tsDir / rawPath).generic_string());
        searchPaths.push_back((tsDir / "GroundTextures" / rawPath).generic_string());
        searchPaths.push_back((tsDir / std::filesystem::path(rawPath).filename()).generic_string());
    }
    searchPaths.push_back("assets/" + rawPath);
    searchPaths.push_back("assets/textures/" + rawPath);
    searchPaths.push_back("assets/textures/GroundTextures/" + rawPath);
    searchPaths.push_back("sandbox_game/assets/" + rawPath);
    searchPaths.push_back("sandbox_game/assets/textures/" + rawPath);
    searchPaths.push_back("sandbox_game/assets/textures/GroundTextures/" + rawPath);

    for (const auto& p : searchPaths) {
        if (std::filesystem::exists(p)) {
            s_pathCache[rawPath] = p;
            return p;
        }
    }

    s_pathCache[rawPath] = rawPath;
    return rawPath;
}

void TilesetAsset::buildAtlas(VulkanRenderer& renderer) {
    atlas = AtlasCache{};

    if (tiles.empty()) {
        atlas.valid = true;
        atlas.atlasBuilt = true;
        return;
    }

    int numTiles = static_cast<int>(tiles.size());

    struct LoadedImage {
        stbi_uc* pixels = nullptr;
        int width = 0;
        int height = 0;
    };
    std::vector<LoadedImage> loadedImages(numTiles);

    int targetTileW = (tileWidth  > 0) ? tileWidth  : 16;
    int targetTileH = (tileHeight > 0) ? tileHeight : 16;
    int rawMaxW = targetTileW;
    int rawMaxH = targetTileH;

    stbi_set_flip_vertically_on_load(false);
    for (int i = 0; i < numTiles; ++i) {
        const auto& tile = tiles[i];
        std::string resolvedPath = resolveTileTexturePath(tile.texturePath, filePath);
        if (!resolvedPath.empty() && std::filesystem::exists(resolvedPath)) {
            int tw = 0, th = 0, tc = 0;
            stbi_uc* srcPx = stbi_load(resolvedPath.c_str(), &tw, &th, &tc, STBI_rgb_alpha);
            if (srcPx) {
                loadedImages[i] = LoadedImage{ srcPx, tw, th };
                if (tw <= targetTileW * 2 && tw > rawMaxW) rawMaxW = tw;
                if (th <= targetTileH * 2 && th > rawMaxH) rawMaxH = th;
            }
        }
    }

    int gridCols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(numTiles))));
    int gridRows = (numTiles + gridCols - 1) / gridCols;

    // Hardware limit protection: cap total atlas dimensions to 4096 (or physical GPU limit)
    uint32_t maxGpuDim = 4096;
    if (renderer.device.getPhysicalDevice() != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(renderer.device.getPhysicalDevice(), &props);
        if (props.limits.maxImageDimension2D > 0) {
            maxGpuDim = props.limits.maxImageDimension2D;
        }
    }

    int maxAllowedAtlasDim = static_cast<int>(std::min(maxGpuDim, 4096u));

    int maxAllowedCellW = maxAllowedAtlasDim / std::max(gridCols, 1);
    int maxAllowedCellH = maxAllowedAtlasDim / std::max(gridRows, 1);
    if (maxAllowedCellW < 8) maxAllowedCellW = 8;
    if (maxAllowedCellH < 8) maxAllowedCellH = 8;

    int cellW = std::min(rawMaxW, maxAllowedCellW);
    int cellH = std::min(rawMaxH, maxAllowedCellH);
    if (cellW < 1) cellW = 1;
    if (cellH < 1) cellH = 1;

    int atlasW = gridCols * cellW;
    int atlasH = gridRows * cellH;

    std::vector<uint8_t> atlasPx;
    try {
        atlasPx.resize(static_cast<size_t>(atlasW) * static_cast<size_t>(atlasH) * 4, 0);
    } catch (const std::exception& e) {
        std::cerr << "[TilesetAsset] Failed to allocate atlas memory: " << e.what() << std::endl;
        atlas.atlasBuilt = true;
        for (auto& img : loadedImages) {
            if (img.pixels) stbi_image_free(img.pixels);
        }
        return;
    }

    constexpr uint8_t MAGENTA[4] = { 255, 0, 255, 255 };

    std::unordered_map<std::string, int> pathCounts;
    for (const auto& t : tiles) {
        if (!t.texturePath.empty()) {
            pathCounts[t.texturePath]++;
        }
    }

    for (int ti = 0; ti < numTiles; ++ti) {
        TileAsset& tile = tiles[ti];
        stbi_uc* srcPx = loadedImages[ti].pixels;
        int tw = loadedImages[ti].width;
        int th = loadedImages[ti].height;

        int col = ti % gridCols;
        int row = ti / gridCols;

        // Compute UV rect for this tile
        float u0 = static_cast<float>(col * cellW) / static_cast<float>(atlasW);
        float v0 = static_cast<float>(row * cellH) / static_cast<float>(atlasH);
        float u1 = static_cast<float>((col + 1) * cellW) / static_cast<float>(atlasW);
        float v1 = static_cast<float>((row + 1) * cellH) / static_cast<float>(atlasH);
        tile.atlasUV = glm::vec4(u0, v0, u1, v1);

        // Fast row-by-row blitting with sub-rect cropping for shared sheet textures
        int dstX = col * cellW;
        int dstY = row * cellH;

        bool isDefaultTint = (tile.colorTint == glm::vec4(1.f, 1.f, 1.f, 1.f));

        if (srcPx && tw > 0 && th > 0) {
            int cropX = 0;
            int cropY = 0;
            int cropW = tw;
            int cropH = th;

            // Only sub-crop if multiple tiles share the exact same texture atlas sheet
            if (pathCounts[tile.texturePath] > 1 && (tw > targetTileW || th > targetTileH)) {
                int colsInSrc = tw / targetTileW;
                int rowsInSrc = th / targetTileH;

                if (colsInSrc > 0 && rowsInSrc > 0) {
                    cropX = (tile.gridX % colsInSrc) * targetTileW;
                    cropY = (tile.gridY % rowsInSrc) * targetTileH;
                    cropW = std::min(targetTileW, tw - cropX);
                    cropH = std::min(targetTileH, th - cropY);
                }
            }

            if (cellW == cropW && cellH == cropH && isDefaultTint && cropX + cropW <= tw && cropY + cropH <= th) {
                // Direct fast memcpy for unscaled, untinted sub-rect tiles
                for (int py = 0; py < cellH; ++py) {
                    int dstIdx = ((dstY + py) * atlasW + dstX) * 4;
                    int srcIdx = ((cropY + py) * tw + cropX) * 4;
                    std::memcpy(&atlasPx[dstIdx], &srcPx[srcIdx], static_cast<size_t>(cellW) * 4);
                }
            } else {
                // Fast row-strided sampling from crop region
                for (int py = 0; py < cellH; ++py) {
                    int srcPy = cropY + (py * cropH) / cellH;
                    if (srcPy >= th) srcPy = th - 1;

                    int dstRowIdx = ((dstY + py) * atlasW + dstX) * 4;
                    int srcRowIdx = (srcPy * tw) * 4;

                    for (int px = 0; px < cellW; ++px) {
                        int srcPxX = cropX + (px * cropW) / cellW;
                        if (srcPxX >= tw) srcPxX = tw - 1;

                        int dstIdx = dstRowIdx + px * 4;
                        int srcIdx = srcRowIdx + srcPxX * 4;

                        if (isDefaultTint) {
                            atlasPx[dstIdx + 0] = srcPx[srcIdx + 0];
                            atlasPx[dstIdx + 1] = srcPx[srcIdx + 1];
                            atlasPx[dstIdx + 2] = srcPx[srcIdx + 2];
                            atlasPx[dstIdx + 3] = srcPx[srcIdx + 3];
                        } else {
                            atlasPx[dstIdx + 0] = static_cast<uint8_t>(std::clamp(srcPx[srcIdx + 0] * tile.colorTint.r, 0.f, 255.f));
                            atlasPx[dstIdx + 1] = static_cast<uint8_t>(std::clamp(srcPx[srcIdx + 1] * tile.colorTint.g, 0.f, 255.f));
                            atlasPx[dstIdx + 2] = static_cast<uint8_t>(std::clamp(srcPx[srcIdx + 2] * tile.colorTint.b, 0.f, 255.f));
                            atlasPx[dstIdx + 3] = static_cast<uint8_t>(std::clamp(srcPx[srcIdx + 3] * tile.colorTint.a, 0.f, 255.f));
                        }
                    }
                }
            }
            stbi_image_free(srcPx);
        } else {
            // Fill placeholder cell
            for (int py = 0; py < cellH; ++py) {
                int dstRowIdx = ((dstY + py) * atlasW + dstX) * 4;
                for (int px = 0; px < cellW; ++px) {
                    int dstIdx = dstRowIdx + px * 4;
                    if (isDefaultTint) {
                        atlasPx[dstIdx + 0] = MAGENTA[0];
                        atlasPx[dstIdx + 1] = MAGENTA[1];
                        atlasPx[dstIdx + 2] = MAGENTA[2];
                        atlasPx[dstIdx + 3] = MAGENTA[3];
                    } else {
                        atlasPx[dstIdx + 0] = static_cast<uint8_t>(std::clamp(MAGENTA[0] * tile.colorTint.r, 0.f, 255.f));
                        atlasPx[dstIdx + 1] = static_cast<uint8_t>(std::clamp(MAGENTA[1] * tile.colorTint.g, 0.f, 255.f));
                        atlasPx[dstIdx + 2] = static_cast<uint8_t>(std::clamp(MAGENTA[2] * tile.colorTint.b, 0.f, 255.f));
                        atlasPx[dstIdx + 3] = static_cast<uint8_t>(std::clamp(MAGENTA[3] * tile.colorTint.a, 0.f, 255.f));
                    }
                }
            }
        }
    }

    // Upload atlas to GPU via ResourceManager
    std::string cacheKey = "tileset_atlas:" + filePath;
    Texture* tex = renderer.resourceManager->createTextureFromPixels(
        cacheKey, atlasPx.data(), atlasW, atlasH, renderer, TextureFilterMode::Nearest
    );

    if (tex) {
        atlas.pixels      = std::move(atlasPx);
        atlas.atlasWidth  = atlasW;
        atlas.atlasHeight = atlasH;
        atlas.descriptorSet = tex->descriptorSet;
        atlas.singleDescriptorSet = tex->singleDescriptorSet;
        atlas.valid       = true;
        atlas.atlasBuilt  = true;
    } else {
        std::cerr << "[TilesetAsset] Atlas GPU upload failed for: " << filePath << std::endl;
        atlas.atlasBuilt  = true; // marked built to prevent per-frame retry loops
    }
}

void TilesetAsset::destroyAtlas(VulkanRenderer& renderer) {
    if (!atlas.valid) return;
    std::string cacheKey = "tileset_atlas:" + filePath;
    renderer.resourceManager->evictTexture(cacheKey, renderer.device.getDevice());
    atlas = AtlasCache{};
}

} // namespace Engine
