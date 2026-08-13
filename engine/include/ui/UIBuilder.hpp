#pragma once
#include "ecs/Registry.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/Name.hpp"
#include "core/EngineAPI.hpp"
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

namespace Engine {

    /**
     * @class ENGINE_API UIBuilder
     * @brief Fluent, code-driven UI builder for programmatically creating, styling,
     *        layouting, and binding game UIs (HUDs, health bars, dialogue boxes, menus).
     */
    class ENGINE_API UIBuilder {
    public:
        explicit UIBuilder(Registry& registry);
        ~UIBuilder() = default;

        // --- Hierarchy Stack Management ---
        UIBuilder& BeginCanvas(const std::string& canvasName = "Canvas", bool isScreenSpace = true);
        UIBuilder& BeginPanel(const std::string& name, const glm::vec2& anchoredPos = {0.0f, 0.0f}, const glm::vec2& size = {200.0f, 150.0f}, const glm::vec4& color = {0.15f, 0.15f, 0.15f, 0.8f}, float borderRadius = 4.0f);
        UIBuilder& BeginVerticalLayout(const std::string& name, float spacing = 8.0f, const glm::vec4& padding = {4.0f, 4.0f, 4.0f, 4.0f});
        UIBuilder& BeginHorizontalLayout(const std::string& name, float spacing = 8.0f, const glm::vec4& padding = {4.0f, 4.0f, 4.0f, 4.0f});
        UIBuilder& BeginGridLayout(const std::string& name, const glm::vec2& cellSize = {80.0f, 80.0f}, const glm::vec2& spacing = {8.0f, 8.0f}, int columns = 4);
        UIBuilder& EndContainer();

        // --- Widget Creation ---
        Entity AddText(const std::string& name, const std::string& content, float fontSize = 14.0f, const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f}, bool alignCenter = false);
        Entity AddImage(const std::string& name, const std::string& texturePath = "", const glm::vec4& tintColor = {1.0f, 1.0f, 1.0f, 1.0f});
        Entity AddButton(const std::string& name, const std::string& label, const std::string& clickEventName = "", const glm::vec4& normalColor = {0.15f, 0.40f, 0.70f, 1.0f});
        Entity AddSlider(const std::string& name, float minVal = 0.0f, float maxVal = 1.0f, float currentVal = 0.5f);
        Entity AddToggle(const std::string& name, const std::string& label = "Toggle", bool isOn = true);

        // --- Positioning & RectTransform Styling Helpers ---
        UIBuilder& SetAnchors(const glm::vec2& minAnchor, const glm::vec2& maxAnchor);
        UIBuilder& SetAnchoredPosition(const glm::vec2& pos);
        UIBuilder& SetSizeDelta(const glm::vec2& size);
        UIBuilder& SetPivot(const glm::vec2& pivot);

        // --- Entity Retrievers ---
        Entity GetCurrentContainer() const;
        Entity GetLastCreatedWidget() const;
        Entity GetCanvas() const;

        // --- Static Reactive State & Mutator Helpers ---
        static void SetText(Registry& registry, Entity textEntity, const std::string& newText);
        static void SetTextColor(Registry& registry, Entity textEntity, const glm::vec4& color);
        static void SetPanelColor(Registry& registry, Entity panelEntity, const glm::vec4& color);
        static void SetImageTexture(Registry& registry, Entity imgEntity, const std::string& texturePath);
        static void SetSliderValue(Registry& registry, Entity sliderEntity, float val);
        static void SetToggleIsOn(Registry& registry, Entity toggleEntity, bool isOn);
        static void SetVisible(Registry& registry, Entity widgetEntity, bool visible);

        // --- Pre-Built UI Templates ---
        static Entity CreateHealthBar(Registry& registry, Entity parentCanvas, const glm::vec2& pos = {20.0f, 20.0f}, const glm::vec2& size = {220.0f, 30.0f}, float initialHp = 100.0f, float maxHp = 100.0f);
        static Entity CreateDialogueBox(Registry& registry, Entity parentCanvas, const std::string& speakerName, const std::string& dialogueText);
        static Entity CreateInventoryGrid(Registry& registry, Entity parentCanvas, int rows = 4, int cols = 5, const glm::vec2& slotSize = {48.0f, 48.0f});

        // --- Code Generator Helper ---
        static std::string ExportHierarchyToCode(Registry& registry, Entity rootEntity);

    private:
        Entity createBaseWidget(const std::string& name, const glm::vec2& pos = {0.0f, 0.0f}, const glm::vec2& size = {100.0f, 30.0f});

        Registry& m_registry;
        Entity m_canvas = Entity();
        Entity m_lastWidget = Entity();
        std::vector<Entity> m_parentStack;
    };

} // namespace Engine
