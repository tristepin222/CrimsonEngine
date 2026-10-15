# Post-Processing Pipeline & Runtime Shader Compiler

This document covers the **Post-Processing Stack**, **HDR Photographic Tonemapping**, **Ambient Occlusion (SSAO)**, **Cascaded Shadow Mapping (CSM)**, and the **Runtime Shader Compiler** (`ShaderCompiler`), implemented in [`PostProcessPipeline.hpp`](../engine/include/renderer/PostProcessPipeline.hpp), [`RenderSettings.hpp`](../engine/include/renderer/RenderSettings.hpp), and [`ShaderCompiler.hpp`](../engine/include/renderer/ShaderCompiler.hpp).

---

## 1. Post-Processing Architecture

The engine uses a modular fullscreen pass pipeline. Once the 3D scene rasterization completes into an offscreen HDR framebuffer, the result is routed through a chain of configurable post-process passes before presentation:

```mermaid
graph LR
    Scene3D[3D Forward Pass / HDR Buffer] --> SSAO[SSAO + Bilateral Blur]
    SSAO --> Tonemap[HDR Tonemapping (ACES / AgX / Filmic)]
    Tonemap --> Custom[Custom Passes (Grayscale / Pixelate)]
    Custom --> Scale[Resolution Scaling & Upsampling]
    Scale --> Swapchain[Vulkan Swapchain Presentation]
```

### Post-Process Pass Structure (`PostProcessPass`)
Each post-process step encapsulates:
* **Shader**: GLSL source (`.frag`) or precompiled SPIR-V (`.spv`).
* **Push Constants (`PostProcessPushConstants`)**:
  * `resolution`: Screen dimensions \((W, H, 1/W, 1/H)\).
  * `params0` through `params3`: User parameters (intensity, thresholds, kernel filters).
* **Pipeline**: Managed Vulkan graphics pipeline bound with a fullscreen quad or triangle strip.

---

## 2. Photographic Tone Mapping (`TonemapSettings`)

High-Dynamic-Range (HDR) rendering requires mapping high-luminance light values into displayable standard dynamic range \([0, 1]\). The engine provides four photographic operators:

| Operator | Mode Enum | Visual Characteristics | Best Used For |
|---|---|---|---|
| **ACES** | `TonemapperMode::ACES` | High-contrast filmic S-curve with natural desaturation at high highlights. | Realistic, cinematic games (Industry standard). |
| **AgX** | `TonemapperMode::AgX` | Modern curve (Blender 4.0 default) preventing color shifts and harsh clip boundaries in bright zones. | Vibrant lighting with intense saturated lights. |
| **Filmic** | `TonemapperMode::Filmic` | Unreal/Jim Hejl curve with gentle shoulder rolloff. | Stylized or softer contrast scenes. |
| **Reinhard** | `TonemapperMode::Reinhard` | Luminance-preserving gentle rolloff (\(\frac{L}{1 + L}\)). | Neutral, low-contrast rendering. |

### Color Grading Controls
* `exposure`: Global exposure EV multiplier.
* `gamma`: Gamma correction exponent (default \(2.2\)).
* `contrast`: S-curve contrast expander.
* `saturation`: Color vibrancy adjustment.

---

## 3. Screen-Space Ambient Occlusion (SSAO)

SSAO approximates contact shadows and crevices where ambient light fails to penetrate:

1. **Depth & Normal Sampling (`ssao.frag`)**:
   * Uses hemispherical sampling kernels (16, 32, or 64 samples) oriented along surface normals.
   * Compares sample depths against the scene depth buffer.
   * Rotates sample vectors using a tiled \(4 \times 4\) random noise texture to eliminate directional banding.
2. **Bilateral Depth-Aware Blur (`ssao_blur.frag`)**:
   * Blurs the noisy occlusion buffer while respecting sharp depth discontinuities, preventing shadow bleeding across foreground and background edges.

---

## 4. Cascaded Shadow Mapping (CSM)

Directional sunlight shadows use Cascaded Shadow Maps (CSM) to maintain sharp shadows across near and far viewing distances:

* **1 to 4 Cascades**: Slices the camera frustum into multiple depth zones.
* **Logarithmic Split Lambda (`cascadeSplitLambda = 0.85`)**: Blends practical uniform and logarithmic splits to allocate higher shadow texel resolution close to the player.
* **16-Tap Poisson Disk Filtering**: High-quality soft shadow penumbras with randomized sampling disks.
* **Geometric Normal Bias (`normalBias`)**: Texel-space normal offsetting that completely eliminates shadow acne without leaving detached "peter-panning" artifacts.

---

## 5. Retro Resolution Scaling & Pixelation

Under [`PostProcessSettings`](../engine/include/renderer/PostProcessPipeline.hpp), the engine supports native retro-style pixelation:
* **`renderScale`**: Downsamples rendering resolution (e.g., \(0.5\times\) or \(0.25\times\)).
* **Fixed Retro Resolution (`useFixedResolution = true`)**: Locks rendering to fixed retro resolutions (e.g. \(480 \times 270\) or \(320 \times 240\)).
* **Nearest-Neighbor Upscaling (`nearestNeighborUpscale = true`)**: Uses point sampling during blit presentation to maintain crisp, non-blurred pixels for retro 3D aesthetics.

---

## 6. Runtime Shader Compiler (`ShaderCompiler`)

The engine integrates [`ShaderCompiler`](../engine/include/renderer/ShaderCompiler.hpp) to compile GLSL shaders to Vulkan SPIR-V dynamically without requiring external manual compile scripts:

```mermaid
graph LR
    GLSL[Shader Source (.vert / .frag / .comp)] --> Check{Timestamp Changed?}
    Check -->|No| Cache[Serve Cached .spv from Disk]
    Check -->|Yes| Glslc[Invoke glslc from Vulkan SDK]
    Glslc --> Validate{Compilation Success?}
    Validate -->|Success| Save[Update .spv & Timestamp Cache]
    Validate -->|Error| Log[Report Diagnostics / Keep Previous Pipeline]
```

### Features & Workflow
* **Automatic `glslc` Detection**: Locates the Google shader compiler using the `VULKAN_SDK` environment variable or system `PATH`.
* **Smart Timestamp Caching**: Compares file modification times of `.vert`/`.frag`/`.comp` against output `.spv` binaries. If unchanged, compilation is skipped entirely, ensuring zero frame hit on startup.
* **Live Hot-Reloading Diagnostics**: Captures compiler errors, line numbers, and warnings, displaying them directly in the editor console without crashing or terminating the engine.
