#include "ui/UIBuilder.hpp"
#include <sstream>
#include <iomanip>

namespace Engine {

    UIBuilder::UIBuilder(Registry& registry) : m_registry(registry) {}

    UIBuilder& UIBuilder::BeginCanvas(const std::string& canvasName, bool isScreenSpace) {
        Entity canvas = m_registry.create();
        m_registry.emplace<Name>(canvas, Name{ canvasName });
        m_registry.emplace<Transform>(canvas, Transform{});
        m_registry.emplace<CanvasComponent>(canvas, CanvasComponent{ isScreenSpace });
        m_registry.emplace<HierarchyComponent>(canvas, HierarchyComponent{});

        m_canvas = canvas;
        m_lastWidget = canvas;
        m_parentStack.clear();
        m_parentStack.push_back(canvas);
        return *this;
    }

    Entity UIBuilder::createBaseWidget(const std::string& name, const glm::vec2& pos, const glm::vec2& size) {
        Entity widget = m_registry.create();
        m_registry.emplace<Name>(widget, Name{ name });
        m_registry.emplace<Transform>(widget, Transform{});
        m_registry.emplace<RectTransform>(widget, RectTransform{
            glm::vec2(0.5f, 0.5f),
            glm::vec2(0.5f, 0.5f),
            pos,
            size,
            glm::vec2(0.5f, 0.5f)
        });

        Entity parent = m_parentStack.empty() ? m_canvas : m_parentStack.back();
        if (m_registry.isValid(parent)) {
            m_registry.emplace<HierarchyComponent>(widget, HierarchyComponent{ parent });
        } else {
            m_registry.emplace<HierarchyComponent>(widget, HierarchyComponent{});
        }

        m_lastWidget = widget;
        return widget;
    }

    UIBuilder& UIBuilder::BeginPanel(const std::string& name, const glm::vec2& anchoredPos, const glm::vec2& size, const glm::vec4& color, float borderRadius) {
        Entity panel = createBaseWidget(name, anchoredPos, size);
        m_registry.emplace<UIPanelComponent>(panel, UIPanelComponent{ color, borderRadius });
        m_parentStack.push_back(panel);
        return *this;
    }

    UIBuilder& UIBuilder::BeginVerticalLayout(const std::string& name, float spacing, const glm::vec4& padding) {
        Entity layout = createBaseWidget(name, {0.0f, 0.0f}, {200.0f, 200.0f});
        m_registry.emplace<UIPanelComponent>(layout, UIPanelComponent{ glm::vec4(0.0f), 0.0f });
        m_registry.emplace<UILayoutGroupComponent>(layout, UILayoutGroupComponent{ true, spacing, padding, false });
        m_parentStack.push_back(layout);
        return *this;
    }

    UIBuilder& UIBuilder::BeginHorizontalLayout(const std::string& name, float spacing, const glm::vec4& padding) {
        Entity layout = createBaseWidget(name, {0.0f, 0.0f}, {200.0f, 200.0f});
        m_registry.emplace<UIPanelComponent>(layout, UIPanelComponent{ glm::vec4(0.0f), 0.0f });
        m_registry.emplace<UILayoutGroupComponent>(layout, UILayoutGroupComponent{ false, spacing, padding, false });
        m_parentStack.push_back(layout);
        return *this;
    }

    UIBuilder& UIBuilder::BeginGridLayout(const std::string& name, const glm::vec2& cellSize, const glm::vec2& spacing, int columns) {
        Entity grid = createBaseWidget(name, {0.0f, 0.0f}, {200.0f, 200.0f});
        m_registry.emplace<UIPanelComponent>(grid, UIPanelComponent{ glm::vec4(0.0f), 0.0f });
        m_registry.emplace<UIGridLayoutGroupComponent>(grid, UIGridLayoutGroupComponent{ cellSize, spacing, glm::vec4(4.0f), GridConstraint::FixedColumnCount, columns });
        m_parentStack.push_back(grid);
        return *this;
    }

    UIBuilder& UIBuilder::EndContainer() {
        if (!m_parentStack.empty()) {
            m_parentStack.pop_back();
        }
        return *this;
    }

    Entity UIBuilder::AddText(const std::string& name, const std::string& content, float fontSize, const glm::vec4& color, bool alignCenter) {
        Entity txtEnt = createBaseWidget(name, {0.0f, 0.0f}, {150.0f, 30.0f});
        m_registry.emplace<UITextComponent>(txtEnt, UITextComponent{ content, color, fontSize, alignCenter });
        return txtEnt;
    }

    Entity UIBuilder::AddImage(const std::string& name, const std::string& texturePath, const glm::vec4& tintColor) {
        Entity imgEnt = createBaseWidget(name, {0.0f, 0.0f}, {100.0f, 100.0f});
        m_registry.emplace<UIImageComponent>(imgEnt, UIImageComponent{ texturePath, tintColor });
        return imgEnt;
    }

    Entity UIBuilder::AddButton(const std::string& name, const std::string& label, const std::string& clickEventName, const glm::vec4& normalColor) {
        Entity btnEnt = createBaseWidget(name, {0.0f, 0.0f}, {140.0f, 40.0f});
        UIButtonComponent btn;
        btn.label = label;
        btn.clickEventName = clickEventName;
        btn.normalColor = normalColor;
        btn.hoverColor = normalColor * 1.25f;
        btn.pressedColor = normalColor * 0.85f;
        m_registry.emplace<UIButtonComponent>(btnEnt, std::move(btn));
        m_registry.emplace<UITextComponent>(btnEnt, UITextComponent{ label, glm::vec4(1.0f), 14.0f, true });
        return btnEnt;
    }

    Entity UIBuilder::AddSlider(const std::string& name, float minVal, float maxVal, float currentVal) {
        Entity sliderEnt = createBaseWidget(name, {0.0f, 0.0f}, {180.0f, 24.0f});
        UISliderComponent slider;
        slider.minValue = minVal;
        slider.maxValue = maxVal;
        slider.value = currentVal;
        m_registry.emplace<UISliderComponent>(sliderEnt, std::move(slider));
        return sliderEnt;
    }

    Entity UIBuilder::AddToggle(const std::string& name, const std::string& label, bool isOn) {
        Entity toggleEnt = createBaseWidget(name, {0.0f, 0.0f}, {160.0f, 28.0f});
        UIToggleComponent toggle;
        toggle.label = label;
        toggle.isOn = isOn;
        m_registry.emplace<UIToggleComponent>(toggleEnt, std::move(toggle));
        return toggleEnt;
    }

    UIBuilder& UIBuilder::SetAnchors(const glm::vec2& minAnchor, const glm::vec2& maxAnchor) {
        if (m_registry.isValid(m_lastWidget)) {
            if (auto* rect = m_registry.get<RectTransform>(m_lastWidget)) {
                rect->anchorMin = minAnchor;
                rect->anchorMax = maxAnchor;
            }
        }
        return *this;
    }

    UIBuilder& UIBuilder::SetAnchoredPosition(const glm::vec2& pos) {
        if (m_registry.isValid(m_lastWidget)) {
            if (auto* rect = m_registry.get<RectTransform>(m_lastWidget)) {
                rect->anchoredPosition = pos;
            }
        }
        return *this;
    }

    UIBuilder& UIBuilder::SetSizeDelta(const glm::vec2& size) {
        if (m_registry.isValid(m_lastWidget)) {
            if (auto* rect = m_registry.get<RectTransform>(m_lastWidget)) {
                rect->sizeDelta = size;
            }
        }
        return *this;
    }

    UIBuilder& UIBuilder::SetPivot(const glm::vec2& pivot) {
        if (m_registry.isValid(m_lastWidget)) {
            if (auto* rect = m_registry.get<RectTransform>(m_lastWidget)) {
                rect->pivot = pivot;
            }
        }
        return *this;
    }

    Entity UIBuilder::GetCurrentContainer() const {
        return m_parentStack.empty() ? m_canvas : m_parentStack.back();
    }

    Entity UIBuilder::GetLastCreatedWidget() const {
        return m_lastWidget;
    }

    Entity UIBuilder::GetCanvas() const {
        return m_canvas;
    }

    // --- Static Mutator Helpers ---
    void UIBuilder::SetText(Registry& registry, Entity textEntity, const std::string& newText) {
        if (registry.isValid(textEntity)) {
            if (auto* txt = registry.get<UITextComponent>(textEntity)) {
                txt->text = newText;
            }
            if (auto* btn = registry.get<UIButtonComponent>(textEntity)) {
                btn->label = newText;
            }
        }
    }

    void UIBuilder::SetTextColor(Registry& registry, Entity textEntity, const glm::vec4& color) {
        if (registry.isValid(textEntity)) {
            if (auto* txt = registry.get<UITextComponent>(textEntity)) {
                txt->color = color;
            }
        }
    }

    void UIBuilder::SetPanelColor(Registry& registry, Entity panelEntity, const glm::vec4& color) {
        if (registry.isValid(panelEntity)) {
            if (auto* panel = registry.get<UIPanelComponent>(panelEntity)) {
                panel->color = color;
            }
        }
    }

    void UIBuilder::SetImageTexture(Registry& registry, Entity imgEntity, const std::string& texturePath) {
        if (registry.isValid(imgEntity)) {
            if (auto* img = registry.get<UIImageComponent>(imgEntity)) {
                img->texturePath = texturePath;
            }
        }
    }

    void UIBuilder::SetSliderValue(Registry& registry, Entity sliderEntity, float val) {
        if (registry.isValid(sliderEntity)) {
            if (auto* slider = registry.get<UISliderComponent>(sliderEntity)) {
                slider->value = glm::clamp(val, slider->minValue, slider->maxValue);
            }
        }
    }

    void UIBuilder::SetToggleIsOn(Registry& registry, Entity toggleEntity, bool isOn) {
        if (registry.isValid(toggleEntity)) {
            if (auto* toggle = registry.get<UIToggleComponent>(toggleEntity)) {
                toggle->isOn = isOn;
            }
        }
    }

    void UIBuilder::SetVisible(Registry& registry, Entity widgetEntity, bool visible) {
        if (!registry.isValid(widgetEntity)) return;

        if (auto* canvas = registry.get<CanvasComponent>(widgetEntity)) {
            canvas->isVisible = visible;
        }
        if (auto* panel = registry.get<UIPanelComponent>(widgetEntity)) {
            panel->color.a = visible ? 0.85f : 0.0f;
        }
        if (auto* txt = registry.get<UITextComponent>(widgetEntity)) {
            txt->color.a = visible ? 1.0f : 0.0f;
        }
        if (auto* img = registry.get<UIImageComponent>(widgetEntity)) {
            img->tintColor.a = visible ? 1.0f : 0.0f;
        }

        for (auto [child, h] : registry.view<HierarchyComponent>()) {
            if (h.parent == widgetEntity) {
                SetVisible(registry, child, visible);
            }
        }
    }

    // --- Templates ---
    Entity UIBuilder::CreateHealthBar(Registry& registry, Entity parentCanvas, const glm::vec2& pos, const glm::vec2& size, float initialHp, float maxHp) {
        UIBuilder builder(registry);
        builder.m_canvas = parentCanvas;
        builder.m_parentStack.push_back(parentCanvas);

        builder.BeginPanel("HealthBar_BG", pos, size, glm::vec4(0.2f, 0.2f, 0.2f, 0.8f), 6.0f);
        Entity fill = builder.AddImage("HealthBar_Fill", "", glm::vec4(0.85f, 0.15f, 0.15f, 1.0f));
        builder.SetAnchors({0.0f, 0.0f}, {initialHp / maxHp, 1.0f});
        builder.SetAnchoredPosition({0.0f, 0.0f});
        builder.AddText("HealthText", "HP: " + std::to_string((int)initialHp), 14.0f, glm::vec4(1.0f), true);
        builder.EndContainer();

        return fill;
    }

    Entity UIBuilder::CreateDialogueBox(Registry& registry, Entity parentCanvas, const std::string& speakerName, const std::string& dialogueText) {
        UIBuilder builder(registry);
        builder.m_canvas = parentCanvas;
        builder.m_parentStack.push_back(parentCanvas);

        builder.BeginPanel("DialogueWindow", {0.0f, -120.0f}, {600.0f, 140.0f}, glm::vec4(0.1f, 0.1f, 0.15f, 0.9f), 8.0f);
        builder.SetAnchors({0.5f, 1.0f}, {0.5f, 1.0f}); // Bottom center
        builder.AddText("SpeakerName", speakerName, 16.0f, glm::vec4(0.9f, 0.7f, 0.2f, 1.0f));
        builder.AddText("DialogueText", dialogueText, 14.0f, glm::vec4(1.0f), false);
        builder.EndContainer();

        return builder.GetLastCreatedWidget();
    }

    Entity UIBuilder::CreateInventoryGrid(Registry& registry, Entity parentCanvas, int rows, int cols, const glm::vec2& slotSize) {
        UIBuilder builder(registry);
        builder.m_canvas = parentCanvas;
        builder.m_parentStack.push_back(parentCanvas);

        glm::vec2 totalSize(cols * (slotSize.x + 8.0f) + 16.0f, rows * (slotSize.y + 8.0f) + 16.0f);
        builder.BeginPanel("InventoryPanel", {0.0f, 0.0f}, totalSize, glm::vec4(0.12f, 0.12f, 0.16f, 0.95f), 10.0f);
        builder.BeginGridLayout("SlotGrid", slotSize, {8.0f, 8.0f}, cols);

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                std::string slotName = "Slot_" + std::to_string(r) + "_" + std::to_string(c);
                builder.AddImage(slotName, "", glm::vec4(0.25f, 0.25f, 0.30f, 0.8f));
            }
        }

        builder.EndContainer(); // Grid
        builder.EndContainer(); // Panel

        return builder.GetLastCreatedWidget();
    }

    Entity UIBuilder::FindChildByName(Registry& registry, Entity rootEntity, const std::string& name) {
        if (!registry.isValid(rootEntity)) return Entity();

        for (auto [e, h, n] : registry.view<HierarchyComponent, Name>()) {
            if (h.parent == rootEntity) {
                if (n.value == name) return e;
                Entity res = FindChildByName(registry, e, name);
                if (registry.isValid(res)) return res;
            }
        }
        return Entity();
    }

    Entity UIBuilder::GetOrCreateCanvas(Registry& registry, const std::string& canvasName, bool isScreenSpace) {
        for (auto [e, n, c] : registry.view<Name, CanvasComponent>()) {
            if (n.value == canvasName) return e;
        }
        UIBuilder builder(registry);
        builder.BeginCanvas(canvasName, isScreenSpace);
        return builder.GetCanvas();
    }

    std::string UIBuilder::ExportHierarchyToCode(Registry& registry, Entity rootEntity) {
        if (!registry.isValid(rootEntity)) return "// Invalid entity";

        std::ostringstream ss;
        ss << "// --- Auto-Generated C++ UI Code ---\n";
        ss << "Engine::UIBuilder builder(registry);\n";

        auto getEntityName = [&](Entity e) -> std::string {
            if (auto* n = registry.get<Name>(e)) return n->value;
            return "Widget_" + std::to_string(e.getId());
        };

        std::function<void(Entity, int)> dumpWidget = [&](Entity e, int indent) {
            std::string ind(indent * 4, ' ');
            std::string name = getEntityName(e);

            if (registry.has<CanvasComponent>(e)) {
                ss << ind << "builder.BeginCanvas(\"" << name << "\");\n";
            } else if (registry.has<UIPanelComponent>(e)) {
                auto* p = registry.get<UIPanelComponent>(e);
                auto* r = registry.get<RectTransform>(e);
                glm::vec2 pos = r ? r->anchoredPosition : glm::vec2(0.0f);
                glm::vec2 sz = r ? r->sizeDelta : glm::vec2(100.0f);
                ss << ind << "builder.BeginPanel(\"" << name << "\", {" << pos.x << "f, " << pos.y << "f}, {" << sz.x << "f, " << sz.y << "f}, glm::vec4(" << p->color.r << "f, " << p->color.g << "f, " << p->color.b << "f, " << p->color.a << "f), " << p->borderRadius << "f);\n";
            } else if (registry.has<UITextComponent>(e)) {
                auto* t = registry.get<UITextComponent>(e);
                ss << ind << "builder.AddText(\"" << name << "\", \"" << t->text << "\", " << t->fontSize << "f, glm::vec4(" << t->color.r << "f, " << t->color.g << "f, " << t->color.b << "f, " << t->color.a << "f), " << (t->alignCenter ? "true" : "false") << ");\n";
            } else if (registry.has<UIButtonComponent>(e)) {
                auto* b = registry.get<UIButtonComponent>(e);
                ss << ind << "builder.AddButton(\"" << name << "\", \"" << b->label << "\", \"" << b->clickEventName << "\");\n";
            } else if (registry.has<UIImageComponent>(e)) {
                auto* img = registry.get<UIImageComponent>(e);
                ss << ind << "builder.AddImage(\"" << name << "\", \"" << img->texturePath << "\");\n";
            }

            for (auto [child, h] : registry.view<HierarchyComponent>()) {
                if (h.parent == e) {
                    dumpWidget(child, indent + 1);
                }
            }

            if (registry.has<UIPanelComponent>(e) || registry.has<CanvasComponent>(e)) {
                ss << ind << "builder.EndContainer();\n";
            }
        };

        dumpWidget(rootEntity, 0);
        return ss.str();
    }

} // namespace Engine
