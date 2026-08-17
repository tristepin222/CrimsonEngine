#pragma once
#include <string>
#include <glm/glm.hpp>
#include "meta/ComponentReflection.hpp"

namespace Engine {

    // [ReflectClass("UI/Canvas")]
    struct CanvasComponent {
        // [ReflectField]
        bool isScreenSpace = true;
        // [ReflectField]
        bool isVisible = true;
    };

    // [ReflectClass("UI/Rect Transform")]
    struct RectTransform {
        // [ReflectField]
        glm::vec2 anchorMin{0.5f, 0.5f};
        // [ReflectField]
        glm::vec2 anchorMax{0.5f, 0.5f};
        // [ReflectField]
        glm::vec2 anchoredPosition{0.0f, 0.0f};
        // [ReflectField]
        glm::vec2 sizeDelta{100.0f, 100.0f};
        // [ReflectField]
        glm::vec2 pivot{0.5f, 0.5f};
    };

    // [ReflectClass("UI/UI Panel")]
    struct UIPanelComponent {
        // [ReflectField]
        glm::vec4 color{0.15f, 0.15f, 0.15f, 0.8f};
        // [ReflectField]
        float borderRadius = 4.0f;
    };

    // [ReflectClass("UI/UI Image")]
    struct UIImageComponent {
        // [ReflectField]
        std::string texturePath;
        // [ReflectField]
        glm::vec4 tintColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

    // [ReflectClass("UI/UI Text")]
    struct UITextComponent {
        // [ReflectField]
        std::string text = "New Text";
        // [ReflectField]
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        // [ReflectField]
        float fontSize = 14.0f;
        // [ReflectField]
        bool alignCenter = false;
    };

    // [ReflectClass("UI/UI Button")]
    struct UIButtonComponent {
        // [ReflectField]
        std::string label = "Button";
        // [ReflectField]
        glm::vec4 normalColor{0.15f, 0.40f, 0.70f, 1.0f};
        // [ReflectField]
        glm::vec4 hoverColor{0.20f, 0.50f, 0.80f, 1.0f};
        // [ReflectField]
        glm::vec4 pressedColor{0.10f, 0.30f, 0.60f, 1.0f};
        // [ReflectField]
        glm::vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
        bool isClicked = false;
        // [ReflectField]
        std::string clickEventName;
    };

    enum class GridConstraint {
        Flexible = 0,
        FixedColumnCount = 1,
        FixedRowCount = 2
    };

    // [ReflectClass("UI/UI Grid Layout Group")]
    struct UIGridLayoutGroupComponent {
        // [ReflectField]
        glm::vec2 cellSize{100.0f, 100.0f};
        // [ReflectField]
        glm::vec2 spacing{10.0f, 10.0f};
        // [ReflectField]
        glm::vec4 padding{5.0f, 5.0f, 5.0f, 5.0f}; // Top, Right, Bottom, Left
        GridConstraint constraint = GridConstraint::FixedColumnCount;
        // [ReflectField]
        int constraintCount = 3; // Max columns or max rows depending on constraint
    };

    // [ReflectClass("UI/UI Layout Group")]
    struct UILayoutGroupComponent {
        // [ReflectField]
        bool isVertical = true;
        // [ReflectField]
        float spacing = 8.0f;
        // [ReflectField]
        glm::vec4 padding{4.0f, 4.0f, 4.0f, 4.0f}; // Top, Right, Bottom, Left
        bool childForceExpand = false;
    };

    // [ReflectClass("UI/UI Scroll Rect")]
    struct UIScrollRectComponent {
        // [ReflectField]
        bool horizontal = false;
        // [ReflectField]
        bool vertical = true;
        // [ReflectField]
        glm::vec2 scrollPosition{0.0f, 0.0f};
        // [ReflectField]
        float scrollSpeed = 25.0f;
    };

    // [ReflectClass("UI/UI Slider")]
    struct UISliderComponent {
        // [ReflectField]
        float value = 0.5f;
        // [ReflectField]
        float minValue = 0.0f;
        // [ReflectField]
        float maxValue = 1.0f;
        // [ReflectField]
        glm::vec4 backgroundColor{0.2f, 0.2f, 0.25f, 1.0f};
        // [ReflectField]
        glm::vec4 fillColor{0.2f, 0.6f, 0.9f, 1.0f};
        // [ReflectField]
        glm::vec4 handleColor{0.95f, 0.95f, 0.98f, 1.0f};
    };

    // [ReflectClass("UI/UI Toggle")]
    struct UIToggleComponent {
        // [ReflectField]
        bool isOn = true;
        // [ReflectField]
        std::string label = "Toggle Option";
        // [ReflectField]
        glm::vec4 boxColor{0.2f, 0.2f, 0.25f, 1.0f};
        // [ReflectField]
        glm::vec4 checkmarkColor{0.2f, 0.8f, 0.4f, 1.0f};
        // [ReflectField]
        glm::vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

} // namespace Engine
