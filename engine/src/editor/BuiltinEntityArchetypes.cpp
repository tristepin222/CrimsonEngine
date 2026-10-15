/**
 * @file BuiltinEntityArchetypes.cpp
 * @brief Registers all built-in entity archetypes into the EntityArchetypeRegistry.
 */

#include "editor/EntityArchetypeRegistry.hpp"

namespace Engine {

    void registerBuiltinEntityArchetypes() {
        auto& reg = EntityArchetypeRegistry::getInstance();
        if (!reg.getArchetypes().empty()) return;

        // ---- 3D Primitives ----
        reg.registerArchetype({ "3D Objects/Cube",     "Cube",     true,  false });
        reg.registerArchetype({ "3D Objects/Triangle", "Triangle", true,  false });
        reg.registerArchetype({ "3D Objects/Quad",     "Quad",     true,  false });
        reg.registerArchetype({ "3D Objects/Terrain",  "Terrain",  false, false });
        reg.registerArchetype({ "3D Objects/Grid World", "Grid World", false, false });

        // ---- Rendering & Lights ----
        reg.registerArchetype({ "Rendering & Lights/Directional Light", "Directional Light", false, false });
        reg.registerArchetype({ "Rendering & Lights/Point Light",       "Point Light",       false, false });
        reg.registerArchetype({ "Rendering & Lights/Camera",          "Camera",          false, false });
        reg.registerArchetype({ "Rendering & Lights/Grid",            "Grid",            false, false });
        reg.registerArchetype({ "Rendering & Lights/Sprite Renderer", "Sprite Renderer", false, false });

        // ---- UI ----
        reg.registerArchetype({ "UI/Canvas",          "Canvas",          false, false });
        reg.registerArchetype({ "UI/Panel",           "UI Panel",        false, false });
        reg.registerArchetype({ "UI/Image",           "UI Image",        false, false });
        reg.registerArchetype({ "UI/Text",            "UI Text",         false, false });
        reg.registerArchetype({ "UI/Button",          "UI Button",       false, false });
        reg.registerArchetype({ "UI/Slider",          "UI Slider",       false, false });
        reg.registerArchetype({ "UI/Toggle",          "UI Toggle",       false, false });
        reg.registerArchetype({ "UI/Grid Layout",     "UI Grid Layout",  false, true  }); // separator before
        reg.registerArchetype({ "UI/Scroll View",     "UI Scroll View",  false, false });

        // ---- Top-level items (no submenu) ----
        reg.registerArchetype({ "Empty GameObject",   "Empty",           false, false });
    }

} // namespace Engine
