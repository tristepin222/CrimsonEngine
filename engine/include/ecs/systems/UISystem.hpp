#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "ecs/components/UIComponents.hpp"
#include <imgui.h>

namespace Engine {

    class UISystem : public System {
    public:
        const char* getName() const override { return "UISystem"; }
        SystemStage getStage() const override { return SystemStage::PostUpdate; }

        UISystem(Registry& reg, VulkanRenderer& rend) 
            : registry(reg), renderer(rend) {}


        void setEditorActive(bool active) { editorActive = active; }
        void setPlaying(bool playing) { isPlaying = playing; }

        void update(float dt) override {
            // Reset click states of all buttons at the start of the frame
            for (auto [ent, btn] : registry.view<UIButtonComponent>()) {
                btn.isClicked = false;
            }
        }

        /**
         * @brief Renders the UI overlay.
         */
        void draw();

    private:
        Registry& registry;
        VulkanRenderer& renderer;
        bool editorActive = false;
        bool isPlaying = false;

        glm::vec4 getRect(Entity entity, const glm::vec2& viewportSize);
        void drawWidget(Entity entity, const glm::vec2& viewportPos, const glm::vec2& viewportSize, ImDrawList* drawList);
    };

} // namespace Engine
