#include "ecs/EntityCloner.hpp"
#include "ecs/EntityFactory.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/PrimitiveType.hpp"
#include "ecs/components/Mesh.hpp"
#include "ecs/components/Material.hpp"
#include "ecs/components/Camera.hpp"
#include "ecs/components/Grid.hpp"
#include "ecs/components/Skeleton.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/SpriteRenderer.hpp"
#include "ecs/components/Tilemap.hpp"
#include "meta/ComponentReflection.hpp"
#include "renderer/ResourceManager.hpp"
#include <cstring>
#include <unordered_map>
#include <vector>
#include <string>

namespace EntityCloner {
    using namespace Engine;

    static void copyReflectedFields(const ComponentReflection& refl, void* srcPtr, void* dstPtr) {
        if (!srcPtr || !dstPtr) return;
        for (const auto& field : refl.fields) {
            char* srcField = static_cast<char*>(srcPtr) + field.offset;
            char* dstField = static_cast<char*>(dstPtr) + field.offset;

            switch (field.type) {
            case FieldType::Float:
                *reinterpret_cast<float*>(dstField) = *reinterpret_cast<const float*>(srcField);
                break;
            case FieldType::Int:
            case FieldType::Enum:
                *reinterpret_cast<int*>(dstField) = *reinterpret_cast<const int*>(srcField);
                break;
            case FieldType::Bool:
                *reinterpret_cast<bool*>(dstField) = *reinterpret_cast<const bool*>(srcField);
                break;
            case FieldType::Vec2:
                *reinterpret_cast<glm::vec2*>(dstField) = *reinterpret_cast<const glm::vec2*>(srcField);
                break;
            case FieldType::Vec3:
                *reinterpret_cast<glm::vec3*>(dstField) = *reinterpret_cast<const glm::vec3*>(srcField);
                break;
            case FieldType::Vec4:
                *reinterpret_cast<glm::vec4*>(dstField) = *reinterpret_cast<const glm::vec4*>(srcField);
                break;
            case FieldType::String:
                *reinterpret_cast<std::string*>(dstField) = *reinterpret_cast<const std::string*>(srcField);
                break;
            default:
                break;
            }
        }
    }

    static void remapEntityReferences(Registry& registry, const std::unordered_map<Entity, Entity>& oldToNewMap) {
        const auto& reflections = ComponentReflectionRegistry::getInstance().getReflections();

        for (const auto& [oldEnt, newEnt] : oldToNewMap) {
            if (!registry.isValid(newEnt)) continue;

            for (const auto& refl : reflections) {
                if (refl.has && refl.has(registry, newEnt) && refl.get) {
                    void* compPtr = refl.get(registry, newEnt);
                    if (!compPtr) continue;

                    for (const auto& field : refl.fields) {
                        if (field.type == FieldType::Entity) {
                            char* fieldPtr = static_cast<char*>(compPtr) + field.offset;
                            Entity* entRef = reinterpret_cast<Entity*>(fieldPtr);
                            if (entRef && entRef->getId() != Entity::INVALID_ENTITY) {
                                auto it = oldToNewMap.find(*entRef);
                                if (it != oldToNewMap.end()) {
                                    *entRef = it->second;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    static Entity cloneRecursive(Registry& registry, VulkanRenderer& renderer, Entity source, Entity parent, bool isRoot, std::unordered_map<Entity, Entity>& oldToNewMap) {
        if (source.getId() == Entity::INVALID_ENTITY || !registry.isValid(source)) {
            return Entity();
        }

        Entity duplicated = registry.create();
        oldToNewMap[source] = duplicated;

        // 1. Copy Name
        if (auto* name = registry.get<Name>(source)) {
            std::string newName = isRoot ? (name->value + " Copy") : name->value;
            registry.emplace<Name>(duplicated, Name{ newName });
        } else {
            registry.emplace<Name>(duplicated, Name{ isRoot ? "Entity Copy" : "Entity" });
        }

        // 2. Copy Transform or RectTransform
        if (auto* srcTransform = registry.get<Transform>(source)) {
            auto& dupTransform = registry.emplace<Transform>(duplicated, Transform{ *srcTransform });
            if (isRoot) {
                dupTransform.position += glm::vec3(1.0f, 0.0f, 1.0f);
            }
        }

        if (auto* srcRect = registry.get<Engine::RectTransform>(source)) {
            auto& dupRect = registry.emplace<Engine::RectTransform>(duplicated, Engine::RectTransform{ *srcRect });
            if (isRoot) {
                dupRect.anchoredPosition += glm::vec2(15.0f, -15.0f);
            }
        }

        // 3. Copy Mesh if present
        if (auto* srcMesh = registry.get<Mesh>(source)) {
            auto& dupMesh = registry.emplace<Mesh>(duplicated, Mesh{ *srcMesh });
            dupMesh.vertexBuffer = VK_NULL_HANDLE;
            dupMesh.indexBuffer = VK_NULL_HANDLE;
            dupMesh.vertexBufferMemory = VK_NULL_HANDLE;
            dupMesh.indexBufferMemory = VK_NULL_HANDLE;
            if (renderer.resourceManager) {
                EntityFactory::uploadMesh(registry, renderer, duplicated);
            }
        }

        // 4. Copy Material if present
        if (auto* srcMat = registry.get<Material>(source)) {
            auto& dupMat = registry.emplace<Material>(duplicated, Material{ *srcMat });
            dupMat.descriptorSet = VK_NULL_HANDLE;
            dupMat.pipeline = VK_NULL_HANDLE;
            dupMat.pipelineLayout = VK_NULL_HANDLE;
            if (renderer.resourceManager) {
                renderer.resourceManager->updateMaterialDescriptorSet(dupMat, renderer);

                bool hasSkin = registry.has<SkeletonComponent>(duplicated);
                std::string vertShader = "unlit.vert.spv";
                std::string fragShader = "unlit.frag.spv";
                if (dupMat.shaderName == "Lit") {
                    vertShader = hasSkin ? "skinned_lit.vert.spv" : "lit.vert.spv";
                    fragShader = "lit.frag.spv";
                } else if (dupMat.shaderName == "Sprite") {
                    vertShader = "sprite.vert.spv";
                    fragShader = "sprite.frag.spv";
                } else {
                    vertShader = hasSkin ? "skinned.vert.spv" : "unlit.vert.spv";
                    fragShader = "unlit.frag.spv";
                }

                PipelineHandle pipeline = renderer.createPipelineForShaders(
                    renderer.resolveShaderPath("build/shaders/" + vertShader),
                    renderer.resolveShaderPath("build/shaders/" + fragShader)
                );
                dupMat.pipeline = pipeline.pipeline;
                dupMat.pipelineLayout = pipeline.layout;
            }
        }

        // 5. Copy PrimitiveType if present
        if (auto* srcPrim = registry.get<PrimitiveType>(source)) {
            registry.emplace<PrimitiveType>(duplicated, PrimitiveType{ *srcPrim });
        }

        // 6. Copy Camera if present
        if (auto* srcCam = registry.get<Camera>(source)) {
            registry.emplace<Camera>(duplicated, Camera{ *srcCam });
        }

        // 7. Copy Grid if present
        if (auto* srcGrid = registry.get<Grid>(source)) {
            registry.emplace<Grid>(duplicated, Grid{ *srcGrid });
        }

        // 8. Copy UI Components
        if (auto* c = registry.get<Engine::CanvasComponent>(source)) registry.emplace<Engine::CanvasComponent>(duplicated, Engine::CanvasComponent{ *c });
        if (auto* c = registry.get<Engine::UIPanelComponent>(source)) registry.emplace<Engine::UIPanelComponent>(duplicated, Engine::UIPanelComponent{ *c });
        if (auto* c = registry.get<Engine::UIImageComponent>(source)) registry.emplace<Engine::UIImageComponent>(duplicated, Engine::UIImageComponent{ *c });
        if (auto* c = registry.get<Engine::UITextComponent>(source)) registry.emplace<Engine::UITextComponent>(duplicated, Engine::UITextComponent{ *c });
        if (auto* c = registry.get<Engine::UIButtonComponent>(source)) registry.emplace<Engine::UIButtonComponent>(duplicated, Engine::UIButtonComponent{ *c });
        if (auto* c = registry.get<Engine::UIGridLayoutGroupComponent>(source)) registry.emplace<Engine::UIGridLayoutGroupComponent>(duplicated, Engine::UIGridLayoutGroupComponent{ *c });
        if (auto* c = registry.get<Engine::UILayoutGroupComponent>(source)) registry.emplace<Engine::UILayoutGroupComponent>(duplicated, Engine::UILayoutGroupComponent{ *c });
        if (auto* c = registry.get<Engine::UIScrollRectComponent>(source)) registry.emplace<Engine::UIScrollRectComponent>(duplicated, Engine::UIScrollRectComponent{ *c });
        if (auto* c = registry.get<Engine::UISliderComponent>(source)) registry.emplace<Engine::UISliderComponent>(duplicated, Engine::UISliderComponent{ *c });
        if (auto* c = registry.get<Engine::UIToggleComponent>(source)) registry.emplace<Engine::UIToggleComponent>(duplicated, Engine::UIToggleComponent{ *c });

        // 9. Dynamic Reflected Components Copy (includes custom C++ gameplay scripts and reflect components)
        const auto& reflections = ComponentReflectionRegistry::getInstance().getReflections();
        for (const auto& refl : reflections) {
            if (refl.has && refl.has(registry, source) && refl.add && refl.get) {
                if (!refl.has(registry, duplicated)) {
                    refl.add(registry, duplicated);
                }
                void* srcPtr = refl.get(registry, source);
                void* dstPtr = refl.get(registry, duplicated);
                copyReflectedFields(refl, srcPtr, dstPtr);
            }
        }

        // 10. Flag dirty components for system re-initialization
        if (auto* spr = registry.get<Engine::SpriteRenderer>(duplicated)) {
            spr->_dirty = true;
        }
        if (auto* tm = registry.get<Engine::TilemapComponent>(duplicated)) {
            tm->isDirty = true;
        }

        // 11. Establish Parent Link
        if (parent.getId() != Entity::INVALID_ENTITY && registry.isValid(parent)) {
            registry.emplace<HierarchyComponent>(duplicated, HierarchyComponent{ parent });
        } else if (registry.has<HierarchyComponent>(duplicated)) {
            registry.remove<HierarchyComponent>(duplicated);
        }

        // 12. Recursively Clone Children
        std::vector<Entity> childrenToClone;
        for (auto [e, hierarchy] : registry.view<HierarchyComponent>()) {
            if (hierarchy.parent == source && e != source && registry.isValid(e)) {
                childrenToClone.push_back(e);
            }
        }

        for (Entity childSource : childrenToClone) {
            cloneRecursive(registry, renderer, childSource, duplicated, false, oldToNewMap);
        }

        return duplicated;
    }

    Entity clone(Registry& registry, VulkanRenderer& renderer, Entity source) {
        std::unordered_map<Entity, Entity> oldToNewMap;
        Entity rootCopy = cloneRecursive(registry, renderer, source, Entity(), true, oldToNewMap);
        remapEntityReferences(registry, oldToNewMap);
        return rootCopy;
    }
}
