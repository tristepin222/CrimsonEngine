#include "ecs/systems/UISystem.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "renderer/ResourceManager.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Engine {

    void UISystem::draw() {
        ImGuiIO& io = ImGui::GetIO();
        glm::vec2 viewportPos(0.0f);
        glm::vec2 viewportSize(io.DisplaySize.x, io.DisplaySize.y);

        if (editorActive) {
            float leftWidth = glm::clamp(io.DisplaySize.x * 0.20f, 260.0f, 400.0f);
            float rightWidth = glm::clamp(io.DisplaySize.x * 0.22f, 320.0f, 460.0f);
            float topY = 22.0f;
            viewportPos = glm::vec2(leftWidth, topY);
            viewportSize = glm::vec2(io.DisplaySize.x - leftWidth - rightWidth, io.DisplaySize.y - topY);
        }

        if (viewportSize.x <= 0.f || viewportSize.y <= 0.f) return;

        // Configure transparent canvas overlay window
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x, viewportPos.y));
        ImGui::SetNextWindowSize(ImVec2(viewportSize.x, viewportSize.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;

        if (!isPlaying) {
            flags |= ImGuiWindowFlags_NoInputs;
        }

        ImGui::Begin("##GameUI_CanvasWindow", nullptr, flags);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Auto-heal: Ensure any UI component entity has a RectTransform component
        for (auto [ent, panel] : registry.view<UIPanelComponent>()) {
            if (!registry.has<RectTransform>(ent)) {
                registry.emplace<RectTransform>(ent, RectTransform{ {0.5f, 0.5f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {160.0f, 36.0f}, {0.5f, 0.5f} });
            }
        }
        for (auto [ent, txt] : registry.view<UITextComponent>()) {
            if (!registry.has<RectTransform>(ent)) {
                registry.emplace<RectTransform>(ent, RectTransform{ {0.5f, 0.5f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {150.0f, 30.0f}, {0.5f, 0.5f} });
            }
        }
        for (auto [ent, img] : registry.view<UIImageComponent>()) {
            if (!registry.has<RectTransform>(ent)) {
                registry.emplace<RectTransform>(ent, RectTransform{ {0.5f, 0.5f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {100.0f, 100.0f}, {0.5f, 0.5f} });
            }
        }
        for (auto [ent, btn] : registry.view<UIButtonComponent>()) {
            if (!registry.has<RectTransform>(ent)) {
                registry.emplace<RectTransform>(ent, RectTransform{ {0.5f, 0.5f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {140.0f, 40.0f}, {0.5f, 0.5f} });
            }
        }

        // 1. Gather all Canvas root elements
        for (auto [canvasEnt, canvas] : registry.view<CanvasComponent>()) {
            if (!canvas.isVisible) continue;

            // Find child elements under this Canvas hierarchy
            for (auto [ent, rect] : registry.view<RectTransform>()) {
                bool isRoot = true;
                if (auto* hierarchy = registry.get<HierarchyComponent>(ent)) {
                    if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
                        // If parent has a RectTransform component, then it's a child widget, not a root widget
                        if (registry.has<RectTransform>(hierarchy->parent)) {
                            isRoot = false;
                        }
                    }
                }

                if (isRoot) {
                    drawWidget(ent, viewportPos, viewportSize, drawList);
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void UISystem::drawWidget(Entity entity, const glm::vec2& viewportPos, const glm::vec2& viewportSize, ImDrawList* drawList) {
        if (!registry.isValid(entity)) return;

        // Calculate widget bounds relative to viewport
        glm::vec4 rect = getRect(entity, viewportSize);
        float absX = viewportPos.x + rect.x;
        float absY = viewportPos.y + rect.y;
        float w = rect.z;
        float h = rect.w;

        // Panel Component
        if (auto* panel = registry.get<UIPanelComponent>(entity)) {
            drawList->AddRectFilled(
                ImVec2(absX, absY),
                ImVec2(absX + w, absY + h),
                ImColor(panel->color.r, panel->color.g, panel->color.b, panel->color.a),
                panel->borderRadius
            );
        }

        // Image Component
        if (auto* img = registry.get<UIImageComponent>(entity)) {
            if (!img->texturePath.empty()) {
                Texture* tex = renderer.resourceManager->loadTexture(img->texturePath, renderer);
                if (tex && tex->singleDescriptorSet != VK_NULL_HANDLE) {
                    drawList->AddImage(
                        (ImTextureID)tex->singleDescriptorSet,
                        ImVec2(absX, absY),
                        ImVec2(absX + w, absY + h),
                        ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f),
                        ImColor(img->tintColor.r, img->tintColor.g, img->tintColor.b, img->tintColor.a)
                    );
                } else {
                    // Fallback grey box
                    drawList->AddRectFilled(
                        ImVec2(absX, absY),
                        ImVec2(absX + w, absY + h),
                        ImColor(1.0f, 1.0f, 1.0f, 0.15f)
                    );
                }
            } else {
                // Color fill overlay
                drawList->AddRectFilled(
                    ImVec2(absX, absY),
                    ImVec2(absX + w, absY + h),
                    ImColor(img->tintColor.r, img->tintColor.g, img->tintColor.b, img->tintColor.a)
                );
            }
        }

        // Text Component
        if (auto* txt = registry.get<UITextComponent>(entity)) {
            if (!txt->text.empty() && txt->color.a > 0.001f) {
                ImFont* font = ImGui::GetFont();
                if (!font && !ImGui::GetIO().Fonts->Fonts.empty()) {
                    font = ImGui::GetIO().Fonts->Fonts[0];
                }
                float fontSize = (txt->fontSize > 0.0f) ? txt->fontSize : ImGui::GetFontSize();
                if (fontSize <= 0.0f) fontSize = 14.0f;

                ImVec2 textSize = ImGui::CalcTextSize(txt->text.c_str());
                float textX = absX;
                float textY = absY;

                if (txt->alignCenter) {
                    textX = absX + (w - textSize.x) * 0.5f;
                    textY = absY + (h - textSize.y) * 0.5f;
                }

                if (font) {
                    drawList->AddText(
                        font,
                        fontSize,
                        ImVec2(textX, textY),
                        ImColor(txt->color.r, txt->color.g, txt->color.b, txt->color.a),
                        txt->text.c_str()
                    );
                } else {
                    drawList->AddText(
                        ImVec2(textX, textY),
                        ImColor(txt->color.r, txt->color.g, txt->color.b, txt->color.a),
                        txt->text.c_str()
                    );
                }
            }
        }

        // Button Component
        if (auto* btn = registry.get<UIButtonComponent>(entity)) {
            ImGui::SetCursorScreenPos(ImVec2(absX, absY));

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(btn->normalColor.r, btn->normalColor.g, btn->normalColor.b, btn->normalColor.a));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(btn->hoverColor.r, btn->hoverColor.g, btn->hoverColor.b, btn->hoverColor.a));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(btn->pressedColor.r, btn->pressedColor.g, btn->pressedColor.b, btn->pressedColor.a));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(btn->textColor.r, btn->textColor.g, btn->textColor.b, btn->textColor.a));

            std::string btnId = btn->label + "##uibtn_" + std::to_string(entity.getId());
            if (ImGui::Button(btnId.c_str(), ImVec2(w, h))) {
                if (isPlaying) {
                    btn->isClicked = true;
                }
            }
            ImGui::PopStyleColor(4);
        }

        // Slider Component
        if (auto* slider = registry.get<UISliderComponent>(entity)) {
            // Track background
            drawList->AddRectFilled(
                ImVec2(absX, absY + h * 0.35f),
                ImVec2(absX + w, absY + h * 0.65f),
                ImColor(slider->backgroundColor.r, slider->backgroundColor.g, slider->backgroundColor.b, slider->backgroundColor.a),
                4.0f
            );
            // Fill bar
            float fillRatio = glm::clamp((slider->value - slider->minValue) / (slider->maxValue - slider->minValue), 0.0f, 1.0f);
            float fillW = w * fillRatio;
            if (fillW > 0.0f) {
                drawList->AddRectFilled(
                    ImVec2(absX, absY + h * 0.35f),
                    ImVec2(absX + fillW, absY + h * 0.65f),
                    ImColor(slider->fillColor.r, slider->fillColor.g, slider->fillColor.b, slider->fillColor.a),
                    4.0f
                );
            }
            // Handle knob
            float handleRadius = std::max(6.0f, h * 0.45f);
            ImVec2 handleCenter(absX + fillW, absY + h * 0.5f);
            drawList->AddCircleFilled(
                handleCenter, handleRadius,
                ImColor(slider->handleColor.r, slider->handleColor.g, slider->handleColor.b, slider->handleColor.a)
            );

            // Interactive dragging
            ImGui::SetCursorScreenPos(ImVec2(absX, absY));
            std::string sliderId = "##uislider_" + std::to_string(entity.getId());
            ImGui::InvisibleButton(sliderId.c_str(), ImVec2(w, h));
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                float mouseX = ImGui::GetMousePos().x - absX;
                float norm = glm::clamp(mouseX / w, 0.0f, 1.0f);
                slider->value = slider->minValue + norm * (slider->maxValue - slider->minValue);
            }
        }

        // Toggle Component
        if (auto* toggle = registry.get<UIToggleComponent>(entity)) {
            float boxSize = glm::min(h, 24.0f);
            float boxY = absY + (h - boxSize) * 0.5f;

            // Box
            drawList->AddRectFilled(
                ImVec2(absX, boxY),
                ImVec2(absX + boxSize, boxY + boxSize),
                ImColor(toggle->boxColor.r, toggle->boxColor.g, toggle->boxColor.b, toggle->boxColor.a),
                4.0f
            );

            // Checkmark
            if (toggle->isOn) {
                drawList->AddRectFilled(
                    ImVec2(absX + 4.0f, boxY + 4.0f),
                    ImVec2(absX + boxSize - 4.0f, boxY + boxSize - 4.0f),
                    ImColor(toggle->checkmarkColor.r, toggle->checkmarkColor.g, toggle->checkmarkColor.b, toggle->checkmarkColor.a),
                    2.0f
                );
            }

            // Label
            if (!toggle->label.empty()) {
                ImGui::SetCursorScreenPos(ImVec2(absX + boxSize + 8.0f, absY + (h - 14.0f) * 0.5f));
                ImGui::TextColored(
                    ImVec4(toggle->textColor.r, toggle->textColor.g, toggle->textColor.b, toggle->textColor.a),
                    "%s", toggle->label.c_str()
                );
            }

            // Click handling
            ImGui::SetCursorScreenPos(ImVec2(absX, absY));
            std::string toggleId = "##uitoggle_" + std::to_string(entity.getId());
            if (ImGui::InvisibleButton(toggleId.c_str(), ImVec2(w, h))) {
                toggle->isOn = !toggle->isOn;
            }
        }

        // Gather children
        std::vector<Entity> children;
        for (auto [child, hierarchy] : registry.view<HierarchyComponent>()) {
            if (hierarchy.parent == entity && registry.has<RectTransform>(child)) {
                children.push_back(child);
            }
        }

        // Apply UIGridLayoutGroupComponent auto-positioning
        if (auto* gridLayout = registry.get<UIGridLayoutGroupComponent>(entity)) {
            int numChildren = (int)children.size();
            int cols = 1;
            int rows = 1;

            if (gridLayout->constraint == GridConstraint::FixedColumnCount) {
                cols = std::max(1, gridLayout->constraintCount);
            } else if (gridLayout->constraint == GridConstraint::FixedRowCount) {
                rows = std::max(1, gridLayout->constraintCount);
                cols = std::max(1, (int)std::ceil((float)numChildren / (float)rows));
            } else {
                // Flexible: calculate columns dynamically based on available container width
                float availW = w - (gridLayout->padding.y + gridLayout->padding.w);
                float itemStepW = gridLayout->cellSize.x + gridLayout->spacing.x;
                cols = std::max(1, (int)std::floor((availW + gridLayout->spacing.x) / std::max(1.0f, itemStepW)));
            }

            for (int i = 0; i < numChildren; ++i) {
                int row = 0;
                int col = 0;
                if (gridLayout->constraint == GridConstraint::FixedRowCount) {
                    col = i / rows;
                    row = i % rows;
                } else {
                    row = i / cols;
                    col = i % cols;
                }

                float cx = gridLayout->padding.w + col * (gridLayout->cellSize.x + gridLayout->spacing.x);
                float cy = gridLayout->padding.x + row * (gridLayout->cellSize.y + gridLayout->spacing.y);
                if (auto* childRect = registry.get<RectTransform>(children[i])) {
                    childRect->anchorMin = glm::vec2(0.0f, 0.0f);
                    childRect->anchorMax = glm::vec2(0.0f, 0.0f);
                    childRect->pivot = glm::vec2(0.0f, 0.0f);
                    childRect->anchoredPosition = glm::vec2(cx, cy);
                    childRect->sizeDelta = gridLayout->cellSize;
                }
            }
        }


        // Apply UILayoutGroupComponent auto-positioning
        if (auto* layoutGroup = registry.get<UILayoutGroupComponent>(entity)) {
            float offset = layoutGroup->isVertical ? layoutGroup->padding.x : layoutGroup->padding.w;
            for (Entity child : children) {
                if (auto* childRect = registry.get<RectTransform>(child)) {
                    childRect->anchorMin = glm::vec2(0.0f, 0.0f);
                    childRect->anchorMax = glm::vec2(0.0f, 0.0f);
                    childRect->pivot = glm::vec2(0.0f, 0.0f);
                    if (layoutGroup->isVertical) {
                        childRect->anchoredPosition = glm::vec2(layoutGroup->padding.w, offset);
                        offset += childRect->sizeDelta.y + layoutGroup->spacing;
                    } else {
                        childRect->anchoredPosition = glm::vec2(offset, layoutGroup->padding.x);
                        offset += childRect->sizeDelta.x + layoutGroup->spacing;
                    }
                }
            }
        }

        // Scroll View Clipping & Scrolling
        auto* scrollRect = registry.get<UIScrollRectComponent>(entity);
        if (scrollRect) {
            // Check mouse wheel scrolling when hovered over container
            ImGui::SetCursorScreenPos(ImVec2(absX, absY));
            std::string scrollId = "##uiscroll_" + std::to_string(entity.getId());
            ImGui::InvisibleButton(scrollId.c_str(), ImVec2(w, h));
            if (ImGui::IsItemHovered()) {
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f && scrollRect->vertical) {
                    scrollRect->scrollPosition.y -= wheel * scrollRect->scrollSpeed;
                    scrollRect->scrollPosition.y = std::max(0.0f, scrollRect->scrollPosition.y);
                }
            }
            drawList->PushClipRect(ImVec2(absX, absY), ImVec2(absX + w, absY + h), true);
        }

        // Render children
        for (Entity child : children) {
            glm::vec2 childViewportPos = viewportPos;
            if (scrollRect) {
                childViewportPos -= scrollRect->scrollPosition;
            }
            drawWidget(child, childViewportPos, viewportSize, drawList);
        }

        if (scrollRect) {
            drawList->PopClipRect();
        }
    }


    glm::vec4 UISystem::getRect(Entity entity, const glm::vec2& viewportSize) {
        auto* rectTransform = registry.get<RectTransform>(entity);
        if (!rectTransform) return glm::vec4(0.0f);

        // Parent container dimensions
        glm::vec4 parentRect(0.0f, 0.0f, viewportSize.x, viewportSize.y);
        if (auto* hierarchy = registry.get<HierarchyComponent>(entity)) {
            if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && hierarchy->parent != entity && registry.isValid(hierarchy->parent)) {
                if (registry.has<RectTransform>(hierarchy->parent)) {
                    parentRect = getRect(hierarchy->parent, viewportSize);
                }
            }
        }


        float px = parentRect.x;
        float py = parentRect.y;
        float pw = parentRect.z;
        float ph = parentRect.w;

        float aminX = rectTransform->anchorMin.x;
        float aminY = rectTransform->anchorMin.y;
        float amaxX = rectTransform->anchorMax.x;
        float amaxY = rectTransform->anchorMax.y;

        float anchorMinX_parent = px + aminX * pw;
        float anchorMinY_parent = py + aminY * ph;
        float anchorMaxX_parent = px + amaxX * pw;
        float anchorMaxY_parent = py + amaxY * ph;

        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

        // Horizontal sizing
        if (std::abs(aminX - amaxX) < 0.0001f) {
            w = rectTransform->sizeDelta.x;
            float anchorX = anchorMinX_parent;
            x = anchorX + rectTransform->anchoredPosition.x - rectTransform->pivot.x * w;
        } else {
            float leftMargin = rectTransform->anchoredPosition.x;
            float rightMargin = rectTransform->sizeDelta.x;
            float x0 = anchorMinX_parent + leftMargin;
            float x1 = anchorMaxX_parent - rightMargin;
            w = std::max(0.0f, x1 - x0);
            x = x0;
        }

        // Vertical sizing
        if (std::abs(aminY - amaxY) < 0.0001f) {
            h = rectTransform->sizeDelta.y;
            float anchorY = anchorMinY_parent;
            y = anchorY + rectTransform->anchoredPosition.y - rectTransform->pivot.y * h;
        } else {
            float topMargin = rectTransform->anchoredPosition.y;
            float bottomMargin = rectTransform->sizeDelta.y;
            float y0 = anchorMinY_parent + topMargin;
            float y1 = anchorMaxY_parent - bottomMargin;
            h = std::max(0.0f, y1 - y0);
            y = y0;
        }

        return glm::vec4(x, y, w, h);
    }

} // namespace Engine
