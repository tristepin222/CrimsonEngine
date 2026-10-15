# Vulkan ECS Game Engine Prototype

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat-square)
![Vulkan API](https://img.shields.io/badge/Vulkan-1.3-red.svg?style=flat-square)
![GLFW](https://img.shields.io/badge/GLFW-3.3-green.svg?style=flat-square)
![ImGui](https://img.shields.io/badge/Dear_ImGui-1.9-orange.svg?style=flat-square)
![ImGuizmo](https://img.shields.io/badge/ImGuizmo-3D_Manipulator-purple.svg?style=flat-square)
![Tracy Profiler](https://img.shields.io/badge/Tracy-Profiler-yellowgreen.svg?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20(x86%2FARM)-lightgrey.svg?style=flat-square)

A high-performance C++20 game engine prototype showcasing a **custom Entity-Component System (ECS)**, a modular **Vulkan rendering pipeline**, an **interactive editor interface**, a **dynamic runtime plugin architecture**, and production-grade environment simulations (physical atmospheric scattering, volumetric clouds, dynamic weather, sculptable terrains, 3D voxel grid worlds, and a fluent UI builder).

---

## Architecture Overview

The engine maintains a clean architectural separation between system layers. Bootstrap and lifecycle events are driven by a single-threaded main loop that coordinates multithreaded jobs, updates dynamic plugins and ECS systems, and dispatches multipass GPU rendering pipelines.

![System Architecture](docs/diagrams/out/architecture/Architecture.png)

For a comprehensive explanation of execution flow, frame lifecycle, and layer boundaries, see the [Architecture Guide](docs/architecture.md).

---

## Key Features

1.  **Custom ECS Backend & Deep Cloning**: Built from scratch using template queries, generational entity recycling, dense contiguous memory pools (`ComponentStorage`), the cache-friendly **Swap-Remove** pattern, and complete deep entity cloning (`EntityCloner`).
    *   *Read more: [ECS Subsystem Guide](docs/ecs_system.md)*
2.  **Modular Vulkan Wrapper**: Abstracted RAII handles encapsulating Vulkan instances, physical/logical devices, swapchains, buffers, compute pipelines, and descriptor layouts, reducing API verbosity without sacrificing control.
    *   *Read more: [Vulkan Renderer Guide](docs/vulkan_renderer.md)*
3.  **Dynamic Runtime Plugin Architecture**: Modular C-ABI plugin system (`PluginManager`) allowing subsystems (Cinemachine, Skymo atmosphere, A* navigation) to be compiled into shared libraries (`.dll`) and loaded/unloaded at runtime.
    *   *Read more: [Dynamic Plugin Architecture Guide](docs/plugin_system.md)*
4.  **Skymo: Physical Atmosphere, Clouds & Dynamic Weather**: Realistic planetary atmospheric scattering (Bruneton/Hillaire compute LUTs: Transmittance, Multi-Scattering, SkyView, 3D Aerial Perspective), raymarched volumetric clouds with 3D noise modeling, volumetric height fog, and dynamic weather (rain, snow, ground wetness/puddles, procedural lightning strikes, and 3D spatialized audio).
    *   *Read more: [Skymo Atmosphere & Weather Guide](docs/skymo_system.md)*
5.  **Cinemachine Virtual Camera Engine**: Procedural virtual camera system supporting 3rd-Person Orbit Follow, 1st-Person Bone-Locking (attaching camera to animated skeleton head bones), Fixed Look-At, 2D Orthographic tracking, position/rotation damping, dead zones, and priority blending.
    *   *Read more: [Cinemachine Virtual Camera Guide](docs/cinemachine_system.md)*
6.  **Sculptable Terrain & Hybrid Foliage Engine**: Chunked submesh terrain with real-time 3D viewport sculpting brushes (`Raise`, `Lower`, `Smooth`, `Flatten`, `Noise`) and falloff curves, multi-layer texture splatting, automated multi-tile border seam stitching, and a dual foliage system (GPU-instanced swaying grass + physics prop entity spawner).
    *   *Read more: [Terrain & Foliage Guide](docs/terrain_system.md)*
7.  **Infinite 3D GridWorld & 2D Tilemap Engine**: 3D spatial voxel grid with infinite chunk hashing, Amanatides & Woo Fast Voxel Traversal (3D DDA raycasting), cell occupation flags, multi-layer 2D tilemaps, automated sprite sheet slicer, and 2D/3D A* pathfinding.
    *   *Read more: [GridWorld & Tilemap Guide](docs/tilemap_and_gridworld_system.md)*
8.  **UI Engine & Fluent UI Builder**: Retained-mode responsive UI framework with `CanvasComponent`, `RectTransform`, layout groups (Vertical, Horizontal, Grid), interactive widgets (Buttons, Sliders, Toggles, Text, Images), pre-built templates (Health Bar, Dialogue Box, Inventory), and a real-time C++ Code Exporter.
    *   *Read more: [UI System & UI Builder Guide](docs/ui_system.md)*
9.  **Post-Processing & Runtime Shader Compiler**: Modular fullscreen post-processing pipeline featuring Cascaded Shadow Maps (CSM), Screen-Space Ambient Occlusion (SSAO) with bilateral blur, photographic HDR tonemapping (ACES, AgX, Filmic, Reinhard), retro downsampling with nearest-neighbor upscaling, and runtime shader compilation via `glslc` with timestamp caching.
    *   *Read more: [Post-Processing & Shader Compiler Guide](docs/post_process_and_shaders.md)*
10. **Unified Profiler & Tracy Integration**: Dual-profiling model featuring an in-engine 120-frame rolling profiler with scrubber and category breakdown, coupled with deep nanosecond flame graph profiling via Tracy Profiler (`TRACY_ENABLE`).
    *   *Read more: [Engine Profiler & Tracy Guide](docs/profiler_system.md)*
11. **WYSIWYG Editor with Multi-Entity Selection**: ImGui/ImGuizmo editor interface featuring multi-entity selection (`Ctrl`/`Shift`), batch gizmo transformations, component drag-and-drop reordering, raycast picking, user preferences themes (Dark, Crimson, Light), and live script compilation without terminating the editor.
    *   *Read more: [Editor UI Guide](docs/editor_ui.md)*
12. **Static Reflection & Generic Scene Serialization**: Compile-time pre-processor code generator parsing source annotations (`// [ReflectClass]`, `// [ReflectField]`, `// [ReflectStruct]`) to generate static type descriptors for automatic JSON serialization and dynamic inspector drawers.
    *   *Read more: [Static Reflection Guide](docs/reflection_system.md)*
13. **Skeletal Animation, 2D Blend Trees & IK Solvers**: Linear Blend Skinning (LBS) executed on the GPU, Forward Kinematics, analytical 2-bone and FABRIK IK solvers, 1D/2D animation blend trees, and custom binary `.anim` baking pipeline.
    *   *Read more: [Animation System Guide](docs/animation_system.md)*
14. **Generic Node Graph Framework & Animator Controller**: Visual node graph system featuring customizable node cards, Bezier curve links, infinite panning canvas, right-side detail panels, and an Animator Controller state machine editor.
    *   *Read more: [Node Graph Framework Guide](docs/node_graph_framework.md) & [Animator Controller Guide](docs/animator_controller_editor.md)*
15. **Rigid Body Physics & Collision Resolution**: Multithreaded physics simulation utilizing bounding volumes, Separating Axis Theorem (SAT), contact point manifold generation, impulse resolution, and a gravity gun script.
    *   *Read more: [Physics System Guide](docs/physics_system.md)*

---

## Detailed Subsystem Guides

| Subsystem | Documentation File | Description |
|---|---|---|
| **System Architecture** | [docs/architecture.md](docs/architecture.md) | High-level architecture, layer boundaries, and frame lifecycle. |
| **Custom ECS Backend** | [docs/ecs_system.md](docs/ecs_system.md) | Registry, EntityManager, dense storage, swap-remove, and cloning. |
| **Dynamic Plugin System** | [docs/plugin_system.md](docs/plugin_system.md) | Shared library loading, C ABI context handshake, and plugin lifecycle. |
| **Skymo Atmosphere & Weather** | [docs/skymo_system.md](docs/skymo_system.md) | Atmospheric scattering, volumetric clouds, fog, rain, snow, lightning. |
| **Cinemachine Virtual Cameras** | [docs/cinemachine_system.md](docs/cinemachine_system.md) | 3rd-person follow, 1st-person bone lock, damping, priority blending. |
| **Terrain & Foliage Engine** | [docs/terrain_system.md](docs/terrain_system.md) | Viewport sculpting, texture splatting, seam stitching, instanced foliage. |
| **GridWorld & Tilemap System** | [docs/tilemap_and_gridworld_system.md](docs/tilemap_and_gridworld_system.md) | 3D voxel DDA raycasting, infinite chunks, multi-layer tilemaps, A*. |
| **UI Engine & Fluent Builder** | [docs/ui_system.md](docs/ui_system.md) | RectTransform layouts, widgets, templates, and runtime C++ code export. |
| **Post-Processing & Shaders** | [docs/post_process_and_shaders.md](docs/post_process_and_shaders.md) | CSM, SSAO, HDR tonemapping, retro scaling, and runtime `glslc` compiler. |
| **Profiler & Tracy Integration** | [docs/profiler_system.md](docs/profiler_system.md) | In-engine 120-frame timeline scrubber and Tracy Profiler instrumentation. |
| **Vulkan Graphics Pipeline** | [docs/vulkan_renderer.md](docs/vulkan_renderer.md) | Hardware device abstraction, double-buffering, and instancing. |
| **Editor UI & Viewport Tooling** | [docs/editor_ui.md](docs/editor_ui.md) | ImGui/ImGuizmo, multi-selection, component dragging, raycast picking. |
| **Static Reflection Engine** | [docs/reflection_system.md](docs/reflection_system.md) | Pre-processor parser, static type descriptors, and scene serialization. |
| **Skeletal Animation & Skinning** | [docs/animation_system.md](docs/animation_system.md) | GPU skinning, blend trees, forward kinematics, and IK solvers. |
| **Node Graph Framework** | [docs/node_graph_framework.md](docs/node_graph_framework.md) | Visual node canvas, Bezier links, detail panels, and serialization. |
| **Animator Controller Editor** | [docs/animator_controller_editor.md](docs/animator_controller_editor.md) | Visual state machine editor, blend tree thresholds, and parameters. |
| **Physics Subsystem** | [docs/physics_system.md](docs/physics_system.md) | Rigid body simulation, SAT collision resolution, and contact manifolds. |
| **Multithreaded Job System** | [docs/job_system.md](docs/job_system.md) | Thread pool job system, lock-free yielding, and parallel-for chunking. |

---

## Getting Started

### Hardware & System Requirements
*   **Operating System**: Windows 10/11 or Ubuntu Linux 22.04+ (x86_64 or ARM64).
*   **Vulkan SDK**: Vulkan SDK 1.3+ installed on the host system.
*   **Compiler**: C++20 compliant compiler (MSVC 2022, GCC 11+, or Clang 13+).
*   **Build System**: CMake 3.20+.

### Dependency Management
Dependencies are managed automatically through the CMake configuration:
*   **Vulkan SDK**: Located dynamically on the host system using `find_package(Vulkan)` (relies on `VULKAN_SDK`).
*   **GLFW & GLM**: Fetched and configured automatically during CMake configure via `FetchContent`.
*   **ImGui & ImGuizmo**: Bundled in `third_party/` and compiled automatically.
*   **Tracy Profiler**: Managed as an optional dependency via `build_engine/_deps/tracy-src`.

### Quick Setup & Compilation

You can build the entire project (SDK, core engine, plugins, and sandbox game) using the main utility script:

```bash
# Automatically builds the Engine SDK and compiles the game
build.bat

# Fresh clean configuration and rebuild
build.bat --clean

# Compile everything and launch the editor immediately
build.bat --run
```

---

## Controls Reference

| Hotkey / Input | Mode | Description |
|---|---|---|
| **F Key** | All | Toggles between **Edit Mode** and **Fly Mode** |
| **W, A, S, D** | Fly Mode | Moves the camera Forward, Left, Backward, and Right |
| **Q, E** | Fly Mode | Moves the camera vertically Down and Up |
| **Mouse Drag** | Fly Mode | Rotates camera orientation (look around) |
| **Mouse Hover** | Edit Mode | Interact with ImGui hierarchy, inspector panels, and menus |
| **Mouse Left-Click** | Edit Mode | Viewport entity raycast picking & selection |
| **Ctrl + Click** | Edit Mode | Toggle multi-entity selection in Scene Hierarchy |
| **Shift + Click** | Edit Mode | Range multi-entity selection in Scene Hierarchy |
| **Gizmo Handles** | Edit Mode | Grab and drag ImGuizmo axes to translate, rotate, or scale objects |
| **W / E / R** | Edit Mode | Switch ImGuizmo mode: Translate (`W`), Rotate (`E`), Scale (`R`) |
| **Left Mouse Drag** | Terrain Tool | Viewport terrain sculpting brush / foliage paint stroke |
| **Left Mouse Click** | Grid Tool | Viewport 3D voxel grid block placement / removal |
