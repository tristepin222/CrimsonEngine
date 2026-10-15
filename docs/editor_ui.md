# Editor UI, Viewport Tooling & Raycast Picking

This document describes the design, features, and mathematical foundations of the engine's interactive editor interface, built with ImGui and ImGuizmo ([`EditorUI.hpp`](../engine/include/editor/EditorUI.hpp)). It details multi-entity selection, component drag-and-drop, viewport raycasting, specialized tool windows, user themes, and scene serialization.

![Raycast Picking](diagrams/out/raycast_picking/RaycastPicking.png)

---

## 1. ImGui & ImGuizmo Integration

The editor frontend is managed by [`EditorUI.cpp`](../engine/src/editor/EditorUI.cpp) and modularized across:
* `EditorUIPanels.cpp`: Core dockable panels, menus, and file dialogs.
* `EditorUIComponentEditors.cpp`: Component inspector property drawers and custom editors.
* `EditorUIGizmosAndPicking.cpp`: Viewport raycasting, collider overlays, and ImGuizmo manipulations.

### Fly Mode vs. Edit Mode
The user switches between modes at any time by pressing the **F key**:
* **Edit Mode**: Releases the OS cursor, enabling panel docking, entity selection, gizmo manipulation, terrain sculpting, and grid placement.
* **Fly Mode**: Captures and hides the cursor (`GLFW_CURSOR_DISABLED`), routing mouse deltas and WSADQE keys to camera traversal.

---

## 2. Core Dockable Panels & Workflows

### 1. Scene Hierarchy Panel
* **Entity Tree**: Displays all active entities in the registry with parent-child indentation.
* **Multi-Entity Selection**: Supports `Ctrl + Click` (toggle individual selection) and `Shift + Click` (range selection). Actions like translation, duplication, and deletion apply to all selected entities simultaneously.
* **Inline Renaming & Creation**: Inline buffers to rename entities; right-click context menus to spawn primitives (Cubes, Spheres, Quads), Lights, Cameras, Terrains, or GridWorlds.

### 2. Detail Inspector Panel
* **Component Drawers**: Reflects all attached components with clean collapsible headers.
* **Component Drag & Drop Reordering**: Dragging component header bars allows reordering how components appear in the inspector.
* **Add Component Menu**: Filterable, categorized dropdown driven by static reflection metadata (e.g. `"Rendering & Lights/Sprite Renderer"`, `"AI/AStar Agent"`).

### 3. Asset Browser Panel
* Scans project `assets/` and `scenes/` directories with an interactive folder tree.
* Contextual assignment buttons ("Use Model", "Use Texture", "Open Scene") appear when files match the selected entity's component slots.
* **Right-Click Asset Creation**: Integrated with `AssetBrowserRegistry`, enabling direct creation of `.anim` binary clips, materials, or prefab files.

### 4. Top Toolbar (`drawToolbar`)
* Quick-action buttons for Play / Stop simulation, Gizmo modes (Translate `W`, Rotate `E`, Scale `R`), Coordinate space toggle (World vs. Local), Grid snapping presets, and Camera speed sliders.

---

## 3. Specialized Tool Windows

Under the **Window** menu bar, the editor provides specialized toolsets:

### 1. In-Engine Profiler (`Window -> Profiler`)
* 120-frame rolling history with pause/freeze and timeline scrubber.
* Category percentage breakdown (Rendering, Physics, Systems, ECS, UI).
* Real-time render metrics (draw calls, triangle counts, vertex buffers, entity totals, mesh VRAM).
* *Read more: [Profiler Subsystem Guide](profiler_system.md)*

### 2. UI Builder & Code Generator
* Visually construct game UIs using `CanvasComponent`, `RectTransform`, panels, buttons, text, and layout groups.
* **Export UI to C++ Code**: Generates ready-to-compile C++ `UIBuilder` code with one click.
* *Read more: [UI System Guide](ui_system.md)*

### 3. 2D Sprite Slicer & Spritesheet to Animations
* **Sprite Sheet Slicer (`Window -> Sprite Sheet Slicer`)**: Slices sprite sheets into individual PNG frames, with automatic alpha trimming to discard blank tiles.
* **Spritesheet to Animations (`Window -> Spritesheet to Animations`)**: Bakes multi-frame sequences into engine-native `.anim` binary animation clips.

### 4. Animator Controller & Timeline Animation Editor
* **Animator Controller (`Window -> Animator Controller`)**: Node graph state machine for locomotion states, transitions, parameters, and 1D/2D blend trees.
* **Animation Editor (`Window -> Animation Editor`)**: Keyframe timeline scrubber for recording property curves on reflected entity variables.
* *Read more: [Animator Controller Guide](animator_controller_editor.md)*

### 5. Terrain Sculpting & Chunk Borders Overlay
* Interactive 3D viewport circular brush with live wireframe indicator.
* Sculpting brushes (`Raise`, `Lower`, `Smooth`, `Flatten`, `Noise`) and falloff profiles (`Smooth`, `Linear`, `Spherical`, `Flat`).
* Chunk border visualization toggle to inspect submesh chunks and neighbor seam stitching.
* *Read more: [Terrain System Guide](terrain_system.md)*

### 6. GridWorld 3D Placement Overlay
* 3D voxel grid footprint overlay (1x1, 2x2, 3x3).
* Placement modes: `Place` (structures), `Block` (natural obstacles), `Clear` (bulldoze).
* *Read more: [GridWorld Guide](tilemap_and_gridworld_system.md)*

---

## 4. Live Script Compilation & Standalone Packaging

### Compiling User Scripts Without Engine Exit
The editor integrates live C++ script compilation (`compileScriptsCallback`):
* Click **Compile Scripts** in the toolbar or menu.
* The engine triggers the project compilation script in the background without terminating the editor process.
* The engine safely reloads the resulting script dynamic library, preserving active editor states and scene positions.

### Packaging Standalone Games (`Window -> Build Settings`)
* Opens the **Build Settings** dialog to specify the output target folder.
* Clicking **Build Game** automatically bundles:
  1. Headless standalone runtime (`game.exe`).
  2. Engine runtime binary (`engine.dll`) and active plugin DLLs (`plugins/`).
  3. Precompiled SPIR-V shaders (`shaders/`).
  4. Project assets, scenes, and `project.settings`.

---

## 5. User Preferences & Visual Themes

Under `Edit -> User Preferences`, users can customize editor styles:
* **Themes**: Dark (Default), Crimson (Studio Signature), and Light.
* Preferences are persisted locally in `editor.user.settings` and do not modify shared project repository settings.

---

## 6. Viewport Gizmos (ImGuizmo)

The editor integrates **ImGuizmo** for viewport transformations:
* **Projection Alignment**: Translates camera projection and view matrix coordinates to screen coordinate overlays.
* **Vulkan Clip Correction**: Corrects Vulkan's inverted Y-axis clip coordinate calculations:
  ```cpp
  glm::mat4 proj = renderer.getActiveCameraProjection();
  proj[1][1] *= -1.0f; // Invert projection coordinate for Vulkan space alignment
  ```
* **Decomposition**: When dragging a gizmo, the resulting matrix (`glm::mat4 model`) is decomposed back into position, rotation, and scale components to update the entity's `Transform` component:
  ```cpp
  ImGuizmo::DecomposeMatrixToComponents(&model[0][0], translation, rotation, scale);
  ```

---

## 7. Viewport Raycast Picking Math

When the user clicks in the 3D viewport, screen-space mouse pixels are unprojected into a 3D ray to intersect objects:

### Mathematical Steps

1. **Normalized Device Coordinates (NDC)**:
   \[nX = \frac{2.0 \cdot \text{mouseX}}{\text{width}} - 1.0\]
   \[nY = 1.0 - \frac{2.0 \cdot \text{mouseY}}{\text{height}}\]
2. **Unprojecting Clip Space Points**:
   Multiply NDC near (\(z = -1.0\)) and far (\(z = 1.0\)) coordinates by the inverse View-Projection matrix \(\mathbf{VP}^{-1}\):
   \[\text{nearClip} = \mathbf{VP}^{-1} \cdot \begin{pmatrix} nX \\ nY \\ -1.0 \\ 1.0 \end{pmatrix}, \quad \text{farClip} = \mathbf{VP}^{-1} \cdot \begin{pmatrix} nX \\ nY \\ 1.0 \\ 1.0 \end{pmatrix}\]
3. **Perspective Division**:
   \[\text{nearPoint} = \frac{\text{nearClip}}{\text{nearClip.w}}, \quad \text{farPoint} = \frac{\text{farClip}}{\text{farClip.w}}\]
4. **Ray Construction**:
   \[\text{rayOrigin} = \text{nearPoint}\]
   \[\text{rayDirection} = \text{normalize}(\text{farPoint} - \text{nearPoint})\]
5. **Ray-Sphere Intersection**:
   Computes entity world-space bounding spheres and solves the quadratic equation:
   \[t^2 (\mathbf{d} \cdot \mathbf{d}) + 2t (\mathbf{oc} \cdot \mathbf{d}) + (\mathbf{oc} \cdot \mathbf{oc}) - r^2 = 0\]
   The entity with the smallest positive hit distance \(t\) is selected.

---

## 8. Scene Serialization

The engine implements a decoupled serialization pipeline via [`SceneSerializer.cpp`](../engine/src/scenes/SceneSerializer.cpp) and [`ComponentSerializerRegistry.hpp`](../engine/include/scenes/ComponentSerializerRegistry.hpp):
* **Saving**: Iterates over entities and serializes standard transforms, names, and all registered component descriptors to JSON.
* **Loading**: Reads the JSON file, instantiates entities, and invokes component deserializers to populate state. Custom game components save and load automatically without modifying engine source code.
