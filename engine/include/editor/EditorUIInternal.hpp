#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <imgui.h>
#include "../ecs/Entity.hpp"
#include "scenes/TilesetAsset.hpp"
#include "ecs/components/Material.hpp"

struct ImportSettingsMetadata {
    std::string assetPath;
    float scale = 1.0f;
    bool generateNormals = true;
    bool allowMissingPos = false;
    bool forceInPlace = false;
    TextureFilterMode filterMode = TextureFilterMode::Bilinear;
    
    struct AnimMetadata {
        std::string name;
        double duration = 0.0;
    };
    std::vector<AnimMetadata> animations;
    
    struct TexMetadata {
        std::string name;
        size_t index = 0;
        bool hasEmbeddedContent = false;
        size_t contentSize = 0;
    };
    std::vector<TexMetadata> textures;
};

extern bool s_openImportSettingsWindow;
extern bool s_triggerLoadImportSettings;
extern ImportSettingsMetadata s_importMetadata;
extern std::filesystem::path s_importSettingsAssetPath;
extern bool s_openTilesetEditorWindow;
extern bool s_openAnimationEditorWindow;
extern bool s_openNodeGraphDemoWindow;
extern bool s_openAnimatorControllerWindow;
extern bool s_openSpriteSlicerWindow;
extern std::filesystem::path s_spriteSlicerAssetPath;
extern int s_sliceCellWidth;
extern int s_sliceCellHeight;
extern std::string s_sliceOutputPrefix;
extern bool s_sliceSkipEmptyTiles;
extern std::string s_editingTilesetPath;
extern Engine::TilesetAsset s_editingTileset;
extern bool s_tilesetLoaded;
enum class TilemapTool { Pencil, BoxOutline, BoxFill, Eraser };
extern TilemapTool s_tilemapTool;
extern uint8_t s_brushRotation;
extern bool s_boxDragging;
extern int s_boxStartCol;
extern int s_boxStartRow;
extern int s_boxCurrentCol;
extern int s_boxCurrentRow;
extern int s_brushTileId;
extern bool s_brushModeActive;
extern Entity s_brushTilemapEntity;
extern int s_tilemapActiveLayer;
extern ImVec2 s_tsPanOffset;
extern float s_tsCellSize;
extern bool s_tsIsPanning;
extern ImVec2 s_tsPanStart;
extern ImVec2 s_tsPanOffsetStart;

void loadImportSettingsMetadata(const std::filesystem::path& path);
void saveImportSettings();
bool writeExtractedFile(const std::string& relativePath, const void* data, size_t size);
bool entityHasSkin(Registry& registry, Entity entity);
