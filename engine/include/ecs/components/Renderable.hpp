#pragma once
#include <glm/glm.hpp>

/**
 * @struct Renderable
 * @brief Component that associates an entity with a mesh and material for rendering.
 */
// [ReflectClass("Rendering & Lights/Renderable")]
struct Renderable {
    /** @brief GPU mesh handle ID for rendering geometry. */
    // [ReflectField]
    uint32_t meshID;
    /** @brief Material asset ID for surface shading and texturing. */
    // [ReflectField]
    uint32_t materialID;
};