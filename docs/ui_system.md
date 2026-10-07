# UI Engine & Fluent UI Builder

This document describes the engine's retained-mode **UI Engine** and the fluent **UI Builder API**, implemented across [`UIComponents.hpp`](../engine/include/ecs/components/UIComponents.hpp), [`UISystem.hpp`](../engine/include/ecs/systems/UISystem.hpp), and [`UIBuilder.hpp`](../engine/include/ui/UIBuilder.hpp). The UI architecture enables developers to build complex, responsive in-game interfaces (HUDs, dialogue boxes, inventory screens, menus) both visually inside the editor and programmatically via fluent C++ code.

---

## 1. System Overview

The engine's UI architecture follows modern retained-mode UI paradigms (similar to Unity's UGUI):
* **ECS Data Driven**: Every UI element is an entity in the ECS registry possessing a [`RectTransform`](../engine/include/ecs/components/UIComponents.hpp) and one or more visual/interactive components.
* **Hierarchical Anchoring**: RectTransforms calculate pixel bounds relative to parent container anchors and pivots, supporting responsive layouts across varying window resolutions.
* **Dual Construction**: UIs can be designed visually in the editor hierarchy or constructed programmatically using the fluent [`UIBuilder`](../engine/include/ui/UIBuilder.hpp) class.
* **C++ Code Generator**: Visual layouts constructed in the editor can be exported into clean C++ `UIBuilder` code with a single click.

```mermaid
graph TD
    Canvas[CanvasComponent (Root Screen / World Space)] --> Panel[UIPanelComponent + RectTransform]
    Panel --> Layout[UILayoutGroupComponent (Vertical/Horizontal/Grid)]
    Layout --> W1[UIButtonComponent]
    Layout --> W2[UITextComponent]
    Layout --> W3[UISliderComponent]
    Layout --> W4[UIImageComponent]
    
    UISystem[UISystem] --> LayoutCalc[Calculate Rect Bounds from Anchors]
    LayoutCalc --> Render[Vulkan UI Render Pass]
```

---

## 2. Core UI Components

### 1. `CanvasComponent`
The root container for all child UI elements:
* `isScreenSpace`: If true, the canvas stretches across the viewport as a screen-space 2D overlay. If false, it renders as a 3D world-space surface.
* `isVisible`: Master visibility switch for the entire hierarchy.

### 2. `RectTransform`
Defines 2D placement, sizing, and responsive anchoring:
* `anchorMin` & `anchorMax` (\(0.0\) to \(1.0\)): Defines parent-relative anchor points (e.g., `{0, 0}` is bottom-left, `{1, 1}` is top-right, `{0.5, 0.5}` is center).
* `anchoredPosition`: 2D offset from the anchor position.
* `sizeDelta`: Explicit width and height in pixels.
* `pivot`: Point within the element around which rotation and positioning occur (default `{0.5, 0.5}`).

### 3. Visual & Interactive Widgets
* **`UIPanelComponent`**: Background quad with customizable `color` and `borderRadius`.
* **`UIImageComponent`**: Textured sprite rectangle supporting `tintColor` and transparency.
* **`UITextComponent`**: Text string, `fontSize`, font color, and alignment toggles.
* **`UIButtonComponent`**: Clickable button with state colors (`normalColor`, `hoverColor`, `pressedColor`), `label`, and `clickEventName`.
* **`UISliderComponent`**: Numeric range slider (`value`, `minValue`, `maxValue`) with background, fill, and handle colors.
* **`UIToggleComponent`**: Boolean checkbox/toggle (`isOn`, `label`, checkmark tint).

### 4. Layout Groups
* **`UILayoutGroupComponent`**: Automatic vertical or horizontal stacking with configurable `spacing` and `padding`.
* **`UIGridLayoutGroupComponent`**: Grid arrangement with fixed column or row constraints (`FixedColumnCount`, `FixedRowCount`).
* **`UIScrollRectComponent`**: Scrollable viewport supporting horizontal and vertical panning.

---

## 3. Fluent UI Builder API (`UIBuilder.hpp`)

The [`UIBuilder`](../engine/include/ui/UIBuilder.hpp) class provides a chained, zero-boilerplate API to construct interfaces in C++ code:

### Example: Programmatic Menu Creation

```cpp
#include "ui/UIBuilder.hpp"

void createMainMenu(Registry& registry) {
    Engine::UIBuilder builder(registry);

    builder.BeginCanvas("MainMenuCanvas", true)
        .BeginPanel("BackgroundPanel", {0.0f, 0.0f}, {400.0f, 500.0f}, {0.1f, 0.1f, 0.12f, 0.9f}, 8.0f)
            .BeginVerticalLayout("MenuLayout", 12.0f, {16.0f, 16.0f, 16.0f, 16.0f})
                .AddText("TitleText", "CRIMSON ENGINE", 24.0f, {1.0f, 0.2f, 0.2f, 1.0f}, true);

    // Add buttons
    Entity playBtn = builder.AddButton("PlayButton", "Start Game", "OnPlayClicked");
    Entity settingsBtn = builder.AddButton("SettingsButton", "Settings", "OnSettingsClicked");
    Entity exitBtn = builder.AddButton("ExitButton", "Quit", "OnExitClicked");

    builder.EndContainer() // End VerticalLayout
        .EndContainer();   // End BackgroundPanel
}
```

---

## 4. Pre-Built UI Templates

For common game interfaces, `UIBuilder` includes static one-call generators:

```cpp
// 1. Health Bar
Entity hpBar = Engine::UIBuilder::CreateHealthBar(
    registry, canvasEntity,
    glm::vec2(20.0f, 20.0f), glm::vec2(240.0f, 28.0f),
    100.0f, 100.0f
);

// 2. RPG Dialogue Box
Entity dialog = Engine::UIBuilder::CreateDialogueBox(
    registry, canvasEntity,
    "Elder Sage",
    "The ancient seals have weakened. Crimson beasts walk the valley once more."
);

// 3. Grid Inventory
Entity inventory = Engine::UIBuilder::CreateInventoryGrid(
    registry, canvasEntity,
    4, 6, // 4 rows, 6 columns
    glm::vec2(52.0f, 52.0f) // Slot size
);
```

---

## 5. Reactive Runtime State Mutators

During gameplay, UI states can be updated via static helper functions without searching or querying manually:

```cpp
Engine::UIBuilder::SetText(registry, scoreTextEntity, "Score: 12500");
Engine::UIBuilder::SetSliderValue(registry, staminaSliderEntity, 0.75f);
Engine::UIBuilder::SetVisible(registry, pauseMenuEntity, isGamePaused);
```

---

## 6. One-Click C++ Code Exporter

When designing an interface visually in the editor, developers can avoid re-typing code manually:
1. Select any UI hierarchy root in the scene hierarchy.
2. In the Canvas Inspector, click **Export UI to C++ Code**.
3. A modal dialog presents generated, human-readable C++ code that calls `UIBuilder` methods matching the visual layout.
4. Click **Copy to Clipboard** and paste it directly into your game script or state machine.
