#include "scenes/Scene.hpp"
#include "scenes/SceneSerializer.hpp"
#include "ecs/EntityFactory.hpp"
#include "ecs/EntityCloner.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/PrefabComponent.hpp"
#include "ecs/components/SpriteRenderer.hpp"
#include "renderer/VulkanRenderer.hpp"
#include <algorithm>
#include <filesystem>

/**
 * @brief Construct a new Scene:: Scene object.
 * @param registry Reference to ECS registry.
 * @param renderer Reference to renderer.
 */
Scene::Scene(Registry& registry, VulkanRenderer& renderer)
    : registry(registry), renderer(renderer) {
}

/**
 * @brief Destroy the Scene:: Scene object.
 */
Scene::~Scene() {
}

#include "ecs/components/EditorCamera.hpp"

/**
 * @brief Destroys all entities tracked by this scene.
 */
void Scene::unload() {
    std::vector<Entity> toDestroy;
    for (uint32_t id : registry.getAlive()) {
        Entity e(id);
        if (!registry.has<EditorCamera>(e)) {
            toDestroy.push_back(e);
        }
    }
    for (Entity e : toDestroy) {
        registry.destroy(e);
    }
    ownedEntities.clear();
}

/**
 * @brief Serializes scene entities to file.
 * @param path Output path.
 * @return True if successful, false otherwise.
 */
bool Scene::saveToFile(const std::string& path) {
    // 1. Purge invalid or EditorCamera entities from ownedEntities
    ownedEntities.erase(
        std::remove_if(ownedEntities.begin(), ownedEntities.end(),
            [this](Entity e) {
                return !registry.isValid(e) || registry.has<EditorCamera>(e);
            }),
        ownedEntities.end()
    );

    // 2. Discover any untracked valid scene entities in the registry (excluding EditorCamera)
    for (auto [e, name] : registry.view<Name>()) {
        if (registry.isValid(e) && !registry.has<EditorCamera>(e) && std::find(ownedEntities.begin(), ownedEntities.end(), e) == ownedEntities.end()) {
            ownedEntities.push_back(e);
        }
    }
    for (auto [e, transform] : registry.view<Transform>()) {
        if (registry.isValid(e) && !registry.has<EditorCamera>(e) && std::find(ownedEntities.begin(), ownedEntities.end(), e) == ownedEntities.end()) {
            ownedEntities.push_back(e);
        }
    }

    // 3. Filter list sent to serializer to guarantee EditorCamera is never written
    std::vector<Entity> entitiesToSerialize;
    for (Entity e : ownedEntities) {
        if (registry.isValid(e) && !registry.has<EditorCamera>(e)) {
            entitiesToSerialize.push_back(e);
        }
    }

    SceneSerializer serializer(registry, renderer);
    return serializer.serialize(path, entitiesToSerialize);
}

/**
 * @brief Deserializes and loads scene entities from file.
 * @param path Input path.
 * @return True if successful, false otherwise.
 */
bool Scene::loadFromFile(const std::string& path) {
    SceneSerializer serializer(registry, renderer);
    unload();
    
    std::vector<Entity> newEntities;
    if (serializer.deserialize(path, newEntities)) {
        for (Entity entity : newEntities) {
            trackEntity(entity);
        }
        return true;
    }
    return false;
}

/**
 * @brief Spawns a primitive shape entity.
 * @param primitiveType Shape type string ("Triangle", "Cube", "Quad").
 * @return Spawned entity handle.
 */
Entity Scene::createPrimitiveEntity(const std::string& primitiveType) {
    PrimitiveKind kind = PrimitiveKind::Cube;
    std::string baseName = "Cube";

    if (primitiveType == "Triangle") {
        kind = PrimitiveKind::Triangle;
        baseName = "Triangle";
    } else if (primitiveType == "Cube") {
        kind = PrimitiveKind::Cube;
        baseName = "Cube";
    } else if (primitiveType == "Quad") {
        kind = PrimitiveKind::Quad;
        baseName = "Quad";
    } else {
        return Entity();
    }

    Entity entity = EntityFactory::spawnPrimitive(
        registry,
        renderer,
        kind,
        makeUniqueEntityName(baseName),
        glm::vec3(0.0f)
    );
    return trackEntity(entity);
}

/**
 * @brief Spawns a component helper entity ("Camera", "Grid").
 * @param entityType Type string.
 * @return Spawned entity handle.
 */
Entity Scene::createEntityOfType(const std::string& entityType) {
    Entity entity;
    if (entityType == "Camera") {
        entity = EntityFactory::spawnCamera(
            registry,
            renderer,
            makeUniqueEntityName("Camera"),
            glm::vec3(0.0f, 5.0f, 5.0f),
            glm::vec3(-90.0f, 0.0f, 0.0f),
            45.0f
        );
    } else if (entityType == "Grid") {
        entity = EntityFactory::spawnGrid(
            registry,
            renderer,
            makeUniqueEntityName("Grid"),
            glm::vec3(0.0f),
            glm::vec3(-90.0f, 0.0f, 0.0f),
            glm::vec4(0.3f, 0.3f, 0.3f, 1.0f),
            1.0f,
            100.0f
        );
    } else if (entityType == "Empty") {
        entity = registry.create();
        registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Empty GameObject") });
        registry.emplace<Transform>(entity, Transform{ glm::vec3(0.0f) });
    } else if (entityType == "Sprite Renderer") {
        entity = registry.create();
        registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Sprite Renderer") });
        registry.emplace<Transform>(entity, Transform{ glm::vec3(0.0f) });
        registry.emplace<Engine::SpriteRenderer>(entity, Engine::SpriteRenderer{});
    } else if (entityType == "Canvas") {
        entity = registry.create();
        registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Canvas") });
        registry.emplace<Transform>(entity, Transform{ glm::vec3(0.0f) });
        registry.emplace<Engine::CanvasComponent>(entity, Engine::CanvasComponent{});
    } else if (entityType == "UI Panel" || entityType == "UI Image" || entityType == "UI Text" ||
               entityType == "UI Button" || entityType == "UI Slider" || entityType == "UI Toggle" ||
               entityType == "UI Grid Layout" || entityType == "UI Scroll View") {
        // Find existing Canvas or create one
        Entity canvasEnt;
        bool found = false;
        for (auto [e, c] : registry.view<Engine::CanvasComponent>()) {
            canvasEnt = e;
            found = true;
            break;
        }
        if (!found) {
            canvasEnt = registry.create();
            registry.emplace<Name>(canvasEnt, Name{ makeUniqueEntityName("Canvas") });
            registry.emplace<Transform>(canvasEnt, Transform{ glm::vec3(0.0f) });
            registry.emplace<Engine::CanvasComponent>(canvasEnt, Engine::CanvasComponent{});
            trackEntity(canvasEnt);
        }

        entity = registry.create();
        registry.emplace<HierarchyComponent>(entity, HierarchyComponent{ canvasEnt });
        registry.emplace<Engine::RectTransform>(entity, Engine::RectTransform{});

        if (entityType == "UI Panel") {
            registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Panel") });
            registry.emplace<Engine::UIPanelComponent>(entity, Engine::UIPanelComponent{});
        } else if (entityType == "UI Image") {
            registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Image") });
            registry.emplace<Engine::UIImageComponent>(entity, Engine::UIImageComponent{});
        } else if (entityType == "UI Text") {
            registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Text") });
            registry.emplace<Engine::UITextComponent>(entity, Engine::UITextComponent{});
        } else if (entityType == "UI Button") {
            registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Button") });
            registry.emplace<Engine::UIButtonComponent>(entity, Engine::UIButtonComponent{});
        } else if (entityType == "UI Slider") {
            registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Slider") });
            if (auto* rt = registry.get<Engine::RectTransform>(entity)) rt->sizeDelta = glm::vec2(160.0f, 20.0f);
            registry.emplace<Engine::UISliderComponent>(entity, Engine::UISliderComponent{});
        } else if (entityType == "UI Toggle") {
            registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Toggle") });
            if (auto* rt = registry.get<Engine::RectTransform>(entity)) rt->sizeDelta = glm::vec2(140.0f, 24.0f);
            registry.emplace<Engine::UIToggleComponent>(entity, Engine::UIToggleComponent{});
        } else if (entityType == "UI Grid Layout") {
            registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Grid Layout") });
            if (auto* rt = registry.get<Engine::RectTransform>(entity)) rt->sizeDelta = glm::vec2(340.0f, 220.0f);
            registry.emplace<Engine::UIPanelComponent>(entity, Engine::UIPanelComponent{ glm::vec4(0.1f, 0.1f, 0.12f, 0.6f), 6.0f });
            registry.emplace<Engine::UIGridLayoutGroupComponent>(entity, Engine::UIGridLayoutGroupComponent{ glm::vec2(90.0f, 90.0f), glm::vec2(10.0f, 10.0f), glm::vec4(10.0f), Engine::GridConstraint::FixedColumnCount, 3 });
        } else if (entityType == "UI Scroll View") {

            registry.emplace<Name>(entity, Name{ makeUniqueEntityName("Scroll View") });
            if (auto* rt = registry.get<Engine::RectTransform>(entity)) rt->sizeDelta = glm::vec2(220.0f, 240.0f);
            registry.emplace<Engine::UIPanelComponent>(entity, Engine::UIPanelComponent{ glm::vec4(0.12f, 0.12f, 0.15f, 0.8f), 6.0f });
            registry.emplace<Engine::UIScrollRectComponent>(entity, Engine::UIScrollRectComponent{});
            // Create vertical layout content inside scroll view
            Entity content = registry.create();
            registry.emplace<Name>(content, Name{ "ScrollContent" });
            registry.emplace<HierarchyComponent>(content, HierarchyComponent{ entity });
            registry.emplace<Engine::RectTransform>(content, Engine::RectTransform{});
            registry.emplace<Engine::UILayoutGroupComponent>(content, Engine::UILayoutGroupComponent{ true, 8.0f, glm::vec4(8.0f), false });
            trackEntity(content);
            for (int i = 1; i <= 8; ++i) {
                Entity item = registry.create();
                registry.emplace<Name>(item, Name{ "ScrollItem_" + std::to_string(i) });
                registry.emplace<HierarchyComponent>(item, HierarchyComponent{ content });
                auto& itemRt = registry.emplace<Engine::RectTransform>(item, Engine::RectTransform{});
                itemRt.sizeDelta = glm::vec2(180.0f, 36.0f);
                registry.emplace<Engine::UIPanelComponent>(item, Engine::UIPanelComponent{ glm::vec4(0.25f, 0.25f, 0.3f, 0.9f), 4.0f });
                trackEntity(item);
            }
        }
    } else {

        return Entity();
    }

    return trackEntity(entity);
}

/**
 * @brief Clones the target entity.
 * @param entity Entity to clone.
 * @return Duplicated entity handle.
 */
Entity Scene::duplicateEntity(Entity entity) {
    Entity duplicated = EntityCloner::clone(registry, renderer, entity);
    if (duplicated.getId() != Entity::INVALID_ENTITY) {
        trackEntity(duplicated);
        
        // Ensure name is unique
        if (Name* name = registry.get<Name>(duplicated)) {
            name->value = makeUniqueEntityName(name->value);
        }
    }
    return duplicated;
}

/**
 * @brief Deletes and destroys the target entity.
 * @param entity Entity to delete.
 * @return True if successful, false otherwise.
 */
bool Scene::deleteEntity(Entity entity) {
    if (entity.getId() == Entity::INVALID_ENTITY) {
        return false;
    }
    
    // Find all children recursively
    std::vector<Entity> toDelete;
    toDelete.push_back(entity);
    
    size_t cursor = 0;
    while (cursor < toDelete.size()) {
        Entity current = toDelete[cursor];
        for (auto [childEntity, hierarchy] : registry.view<HierarchyComponent>()) {
            if (hierarchy.parent == current) {
                if (std::find(toDelete.begin(), toDelete.end(), childEntity) == toDelete.end()) {
                    toDelete.push_back(childEntity);
                }
            }
        }
        cursor++;
    }
    
    // Destroy all of them
    for (Entity e : toDelete) {
        untrackEntity(e);
        registry.destroy(e);
    }
    
    return true;
}

Entity Scene::instantiatePrefab(const std::string& prefabPath, const glm::vec3& position, Entity parentEntity) {
    if (!std::filesystem::exists(prefabPath)) {
        return Entity();
    }

    SceneSerializer serializer(registry, renderer);
    std::vector<Entity> loadedEntities;
    Entity rootEntity = serializer.deserializePrefab(prefabPath, loadedEntities, parentEntity);

    if (registry.isValid(rootEntity)) {
        for (Entity e : loadedEntities) {
            trackEntity(e);
            bool isRoot = (e == rootEntity);
            registry.emplace_or_replace<Engine::PrefabComponent>(e, Engine::PrefabComponent{ prefabPath, isRoot });
        }

        if (position != glm::vec3(0.0f)) {
            if (auto* trans = registry.get<Transform>(rootEntity)) {
                trans->position = position;
            } else if (auto* rect = registry.get<Engine::RectTransform>(rootEntity)) {
                rect->anchoredPosition = glm::vec2(position.x, position.y);
            }
        }
    }

    return rootEntity;
}

/**
 * @brief Registers entity under scene tracking list.
 * @param entity Target entity.
 * @return Tracked entity.
 */
Entity Scene::trackEntity(Entity entity) {
    if (registry.isValid(entity) && !registry.has<EditorCamera>(entity)) {
        if (std::find(ownedEntities.begin(), ownedEntities.end(), entity) == ownedEntities.end()) {
            ownedEntities.push_back(entity);
        }
    }
    return entity;
}

/**
 * @brief Removes entity from scene tracking list.
 * @param entity Target entity.
 */
void Scene::untrackEntity(Entity entity) {
    ownedEntities.erase(
        std::remove(ownedEntities.begin(), ownedEntities.end(), entity),
        ownedEntities.end()
    );
}

/**
 * @brief Resolves active entity matching name property.
 * @param name Search name query.
 * @return Entity handle.
 */
Entity Scene::findEntityByName(const std::string& name) const {
    for (auto [entity, entityName] : registry.view<Name>()) {
        if (entityName.value == name) {
            return entity;
        }
    }
    return Entity();
}

/**
 * @brief Creates unique suffix indices if name conflicts.
 * @param baseName Desired name.
 * @return Unique name.
 */
std::string Scene::makeUniqueEntityName(const std::string& baseName) const {
    if (findEntityByName(baseName).getId() == Entity::INVALID_ENTITY) {
        return baseName;
    }

    int suffix = 1;
    while (true) {
        std::string candidate = baseName + " " + std::to_string(suffix);
        if (findEntityByName(candidate).getId() == Entity::INVALID_ENTITY) {
            return candidate;
        }
        ++suffix;
    }
}
