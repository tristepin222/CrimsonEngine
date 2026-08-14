#pragma once
#include <string>
#include <glm/glm.hpp>
#include "core/EngineAPI.hpp"

namespace Engine {

    /**
     * @struct SpriteRenderer
     * @brief 2D Sprite Renderer component.
     *
     * Automatically manages a 1x1 unit Quad Mesh and a Sprite Material on the entity.
     * When attached, SpriteSystem creates/updates the Mesh and Material each frame.
     *
     * Sorting: `sortOrder` controls depth order. SpriteSystem offsets
     * Transform.position.z by sortOrder * 0.0001f each frame so overlapping sprites layer correctly.
     * Users should keep Transform.z at 0 and use sortOrder for layering.
     */
    // [ReflectClass("Rendering & Lights/Sprite Renderer")]
    struct ENGINE_API SpriteRenderer {
        /** @brief Path to the sprite texture asset (PNG, JPG, TGA). */
        // [ReflectField]
        std::string texturePath;

        /** @brief RGBA tint multiplied with the sampled texture colour. */
        // [ReflectField]
        glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

        /** @brief Flip UV horizontally (mirrors sprite around the vertical axis). */
        // [ReflectField]
        bool flipX = false;

        /** @brief Flip UV vertically (mirrors sprite around the horizontal axis). */
        // [ReflectField]
        bool flipY = false;

        /** @brief Depth-sorting order relative to other sprites. Lower values render behind higher values. */
        // [ReflectField]
        int sortOrder = 0;

        // ---- Internal runtime state (not reflected, not serialized) ----

        /** @brief GPU mesh ID of the managed unit quad (0 = not yet created). */
        uint32_t _managedMeshId  = 0;

        /** @brief GPU material ID of the managed sprite material (0 = not yet created). */
        uint32_t _managedMatId   = 0;

        /** @brief The texture path that was last uploaded — used to detect dirty texture changes. */
        std::string _loadedTexturePath;

        /** @brief The flip state that was last applied — used to detect changes requiring mesh rebuild. */
        bool _lastFlipX = false;

        /** @brief The flip state that was last applied — used to detect changes requiring mesh rebuild. */
        bool _lastFlipY = false;

        /** @brief sortOrder value that was last applied. */
        int _lastSortOrder = 0;

        /** @brief Whether this sprite needs full setup on the next update. */
        bool _dirty = true;
    };

} // namespace Engine
