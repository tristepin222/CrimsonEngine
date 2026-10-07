# Engine Architecture

This document describes the high-level architecture and system layout of the C++ Game Engine Prototype.

## System Design Overview

The engine is structured as a modular desktop application in C++20, utilizing GLFW for OS windowing/events and Vulkan for rendering. The application maintains a clear separation between core systems, resource managers, systems logic, editor GUI, dynamic plugins, and the application level.

![System Architecture](diagrams/out/architecture/Architecture.png)

### Architectural Layers

1.  **Application Layer (`Application.cpp`, `SceneManager`, `DefaultScene`)**: 
    The bootstrap and lifecycle entry point. It reads runtime configurations (`project.settings`), initializes GLFW and Vulkan, instantiates the ECS Registry, binds standard systems, dynamically loads startup scenes, and orchestrates the frame loop.
2.  **Plugin Layer (`PluginManager`, `Plugin.hpp`)**:
    Dynamic shared library host. Discovers and loads `.dll` modules at runtime (e.g. `skymo_plugin.dll`, `cinemachine_plugin.dll`, `astar_plugin.dll`), registering custom ECS components, reflection metadata, systems, and editor UI inspectors via a clean C-ABI handshake.
    *   *Read more: [Dynamic Plugin Architecture](plugin_system.md)*
3.  **Entity-Component System (ECS) Layer (`Registry`, `EntityManager`, `ComponentStorage`, `EntityCloner`)**:
    A lightweight, custom ECS backend that manages entities, component allocations, deep entity cloning, and subscriptions. Component data is stored contiguously in memory pools (`ComponentStorage`) using the Swap-Remove pattern to maintain maximum CPU cache locality.
    *   *Read more: [ECS Subsystem Guide](ecs_system.md)*
4.  **Systems Layer (`System`, `SystemManager`)**:
    Encapsulates simulation behaviours:
    *   **[PhysicsSystem](physics_system.md)**: Rigid body dynamics, broadphase bounding volumes, SAT collision detection, and impulse resolution.
    *   **[AnimationSystem](animation_system.md)**: Forward Kinematics, blend trees, and IK solvers.
    *   **[TerrainSystem](terrain_system.md)**: Chunked procedural terrain sculpting, multi-tile border stitching, and GPU-instanced foliage.
    *   **[GridWorldSystem](tilemap_and_gridworld_system.md)**: 3D spatial voxel grid, DDA raycasting, and 2D multi-layer tilemaps.
    *   **[UISystem](ui_system.md)**: Responsive UI layout calculations and widget state updates.
5.  **Rendering & Post-Processing Layer (`VulkanRenderer`, `PostProcessPipeline`, `ShaderCompiler`)**:
    Wraps Vulkan handles (devices, swapchains, buffers, descriptor pools, command managers, pipelines) in custom RAII classes. Manages double-buffered frame synchronization, instanced draw calls, Cascaded Shadow Maps (CSM), Screen-Space Ambient Occlusion (SSAO), HDR tone mapping, and runtime shader compilation via `glslc`.
    *   *Read more: [Vulkan Renderer Guide](vulkan_renderer.md) & [Post-Processing Guide](post_process_and_shaders.md)*
6.  **Profiling Layer (`Profiler`, Tracy Integration)**:
    Provides frame history tracking, system execution samples, render stats (draw calls, triangles, VRAM), and deep microsecond flame graph integration with Tracy Profiler.
    *   *Read more: [Profiler Subsystem Guide](profiler_system.md)*
7.  **Editor Layer (`EditorUI`, `EditorModeState`, `UIBuilder`)**:
    An interactive WYSIWYG editor powered by ImGui and ImGuizmo. Enables multi-entity selection, component drag-and-drop, 3D viewport gizmos, raycast picking, node graph state machine editing, spritesheet slicing, and runtime C++ UI export. Can be disabled for standalone play.
    *   *Read more: [Editor UI Guide](editor_ui.md)*

---

## Frame Lifecycle & Execution Flow

The engine operates on a single-threaded game loop inside `Engine::Application::run()`. The lifecycle of a single frame comprises polling OS events, profiling frame start, updating active scenes, updating ECS systems, recording post-processing and UI panel commands, and submitting Vulkan command buffers for drawing.

### Core Loop Structure (`Application::run()`)

Below is the C++ structural skeleton of the game loop:

```cpp
void Application::run() {
    onStart();

    while (running && !renderer->shouldClose()) {
        // 1. Profiler frame begin
        Engine::Profiler::getInstance().beginFrame();

        // 2. Poll OS windowing and input events
        glfwPollEvents();
        float dt = renderer->getDeltaTime();

        // 3. Custom game override ticks
        onUpdate(dt);

        // 4. Update scene transitions and logic
        sceneManager.update(dt);
        
        // 5. Process ECS systems (Physics -> Input -> Animation -> Camera -> Logic -> Render)
        systemManager.updateAll(dt);
        
        if (config.enableEditor && editorUI) {
            // 6a. Record editor panel layouts and overlays
            editorUI->beginFrame();
            editorUI->drawPanels();
            
            // 7a. Render frame with post-processing & ImGui overlay
            renderSystem->drawFrame([this](VkCommandBuffer cmd) {
                editorUI->render(cmd);
            });
        } else {
            // 6b. Standalone mode: Render clean viewport fullscreen (no UI)
            renderSystem->drawFrame();
        }

        // 8. Finalize frame profiling metrics
        Engine::Profiler::getInstance().endFrame();
    }

    onShutdown();
}
```

The sequence below illustrates the frame execution flow:

![Frame Lifecycle Sequence](diagrams/out/frame_lifecycle/FrameLifecycle.png)

### Detailed Loop Phases

*   **OS Event Polling**: Calls `glfwPollEvents()` to update mouse/keyboard and window resizing data.
*   **Scene Update**: Updates active scene transitions and scripting logic via `SceneManager::update(dt)`.
*   **ECS Systems Update**: Walks through registered systems in `SystemManager` and calls `update(dt)`:
    *   **Input System**: Reads mouse delta and WSADQE keys. In `Fly Mode`, hides the cursor and updates `InputComponent`. In `Edit Mode`, releases cursor to ImGui.
    *   **Physics System**: Evaluates bounding volumes, detects collisions via SAT, resolves contact impulses, and integrates velocities.
    *   **Cinemachine System**: Evaluates active virtual cameras, smooths tracking targets, and drives physical camera transforms.
    *   **Terrain & GridWorld Systems**: Updates LODs, chunk streams, foliage swaying, and cell voxel interactions.
    *   **Render System (Update)**: Compiles active entity transforms, materials, and mesh configurations into cache buffers (`InstanceDataSoA`) for instanced draws.
*   **Editor Panel Draw**: Records ImGui layouts, inspects properties of the selected entity (or multi-selection), handles raycast picking, and applies 3D transformations via ImGuizmo.
*   **Render Draw Frame**: The `RenderSystem::drawFrame` coordinates Vulkan-specific recording:
    1.  Acquires an image from the swapchain using double-buffering semaphores.
    2.  Pushes camera uniform buffer data (View-Projection matrix).
    3.  Executes shadow map pass (Cascaded Shadow Maps).
    4.  Draws 3D forward geometry batches using push constants and dynamic instance buffers.
    5.  Executes compute shader atmospheric passes (Skymo skyview, clouds, aerial perspective).
    6.  Executes post-processing pipeline (SSAO, Tone Mapping, custom fullscreen passes).
    7.  Executes the overlay callback, triggering ImGui Vulkan backend render commands.
    8.  Submits the recorded command buffer to the graphics queue and presents the swapchain image.

---

## Data-Driven Scenes & Standalone Configuration

Instead of compiling hardcoded C++ scene classes, the engine parses level data dynamically at runtime:

1. **`DefaultScene`**: Reads a serializable JSON file on launch and invokes `SceneSerializer` to instantiate ECS entities and upload assets automatically.
2. **`project.settings`**: A settings configuration file adjacent to the executable that sets the startup window title, dimensions, starting scene path, and the `enableEditor` toggle flag.
3. **Standalone Mode**: Setting `"enableEditor": false` locks mouse cursor control by default, enables gameplay camera traversal, and disables the ImGui overlay entirely for maximum rendering throughput.

---

## Asset Pipeline & Resource Caching

The engine implements a lightweight asset manager (`ResourceManager`) to handle external model geometries and image file decoding:

* **glTF Parser (`cgltf`)**: Recursively reads nodes, meshes, triangles, and textures, creating standard ECS entities with vertex buffers uploaded directly to GPU memory.
* **Texture Loader (`stb_image`)**: Decodes image formats into pixels, staging memory copies onto device-local Vulkan images. Applies a vertical flip on load to match Vulkan's downward Y-coordinate space.
* **Resource Caching**: Maintains mapping tables of loaded meshes/textures. If multiple entities share the same asset path, the manager serves cached descriptor sets, preventing duplicate VRAM allocations.
* **Fallback 1x1 Texture**: A default 1x1 white texture binds to materials without designated textures to maintain visual compatibility inside shaders.

---

## Architectural Trade-Offs

### Single-Threaded Main Loop & Job System
* **Why**: The frame execution loop operates on the main thread to simplify Vulkan command buffer submission and swapchain synchronization. However, parallelizable loops (such as physics collision broadphase and skinning calculations) are offloaded to our multi-threaded **[Job System](job_system.md)** via parallel-for chunking.
* **Trade-off**: The Job System uses a lock-free yielding design to synchronize threads without locking the main game loop, keeping performance high and preventing deadlocks.

### Dynamic Plugins vs. Monolithic Static Compilation
* **Why**: Building systems like Skymo, Cinemachine, and A* as shared dynamic libraries (`.dll`) drastically accelerates iteration times, eliminates engine re-linking bottlenecks, and allows developers to distribute modular features cleanly.
* **Trade-off**: Requires maintaining a stable C ABI boundary (`PluginContext`) and managing cross-DLL singleton states (such as `ImGui::SetCurrentContext`).
