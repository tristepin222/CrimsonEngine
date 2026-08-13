#include "scenes/SceneSerializer.hpp"
#include "scenes/ComponentSerializerRegistry.hpp"
#include "ecs/EntityFactory.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "renderer/ResourceManager.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/Name.hpp"
#include "ecs/components/PrimitiveType.hpp"
#include "ecs/components/Material.hpp"
#include "ecs/components/Camera.hpp"
#include "ecs/components/Grid.hpp"
#include "ecs/components/inputComponent.hpp"
#include "ecs/components/Primitives.hpp"
#include "ecs/components/Skeleton.hpp"
#include "ecs/components/Animator.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/AnimationController.hpp"
#include "ecs/components/IKSolver.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Collider.hpp"
#include "ecs/components/PlayerControllerComponent.hpp"
#include "meta/ComponentReflection.hpp"
#include "ecs/components/Tilemap.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/SpriteRenderer.hpp"


struct ParentNameComponent {
    std::string name;
};
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

// Static initializer to register core engine components with the registry
/**
 * @brief Static function registering component serializers for built-in camera, grid, and primitive types.
 * @return True when registered.
 */
extern "C" ENGINE_API void registerEngineReflection();

static bool registerBuiltinComponents() {
    registerEngineReflection();
    auto& reg = ComponentSerializerRegistry::getInstance();

    
    // 1. Camera Component
    reg.registerComponent(
        "Camera",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* camera = registry.get<Camera>(entity)) {
                out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("Camera") << ",\n";
                out << JSONUtils::indent(indent) << "\"fov\": " << camera->fov << ",\n";
                out << JSONUtils::indent(indent) << "\"isOrthographic\": " << (camera->isOrthographic ? "true" : "false") << ",\n";
                out << JSONUtils::indent(indent) << "\"orthoSize\": " << camera->orthoSize << ",\n";
                out << JSONUtils::indent(indent) << "\"nearPlane\": " << camera->nearPlane << ",\n";
                out << JSONUtils::indent(indent) << "\"farPlane\": " << camera->farPlane << ",\n";
                out << JSONUtils::indent(indent) << "\"moveSpeed\": " << camera->moveSpeed << ",\n";
                out << JSONUtils::indent(indent) << "\"mouseSensitivity\": " << camera->mouseSensitivity;
            }
        },
        [](Registry& registry, VulkanRenderer& renderer, Entity entity, const std::string& json) {
            float fov = 45.0f;
            bool hasFov = JSONUtils::extractFloatValue(json, "fov", fov);
            std::string type = JSONUtils::extractStringValue(json, "entityType");
            
            if (type == "Camera" || hasFov || json.find("\"isOrthographic\"") != std::string::npos) {
                if (!registry.has<Camera>(entity)) {
                    registry.emplace<Camera>(entity, Camera{});
                }
                if (!registry.has<InputComponent>(entity)) {
                    registry.emplace<InputComponent>(entity, InputComponent{});
                }
                
                if (Camera* camera = registry.get<Camera>(entity)) {
                    camera->fov = fov;

                    if (json.find("\"isOrthographic\": true") != std::string::npos || json.find("\"isOrthographic\":true") != std::string::npos) {
                        camera->isOrthographic = true;
                    } else if (json.find("\"isOrthographic\": false") != std::string::npos || json.find("\"isOrthographic\":false") != std::string::npos) {
                        camera->isOrthographic = false;
                    }

                    JSONUtils::extractFloatValue(json, "orthoSize", camera->orthoSize);
                    JSONUtils::extractFloatValue(json, "nearPlane", camera->nearPlane);
                    JSONUtils::extractFloatValue(json, "farPlane", camera->farPlane);
                    JSONUtils::extractFloatValue(json, "moveSpeed", camera->moveSpeed);
                    JSONUtils::extractFloatValue(json, "mouseSensitivity", camera->mouseSensitivity);

                    int width = 0;
                    int height = 0;
                    glfwGetWindowSize(renderer.getWindow(), &width, &height);
                    if (height > 0) {
                        camera->aspect = static_cast<float>(width) / static_cast<float>(height);
                    }
                    
                    if (Transform* transform = registry.get<Transform>(entity)) {
                        renderer.setActiveCamera(camera->projection(), transform->position, camera->view(*transform));
                    }
                }
            }
        }
    );


    // 2. Grid Component
    reg.registerComponent(
        "Grid",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* grid = registry.get<Grid>(entity)) {
                out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("Grid") << ",\n";
                out << JSONUtils::indent(indent) << "\"gridSpacing\": " << grid->spacing << ",\n";
                out << JSONUtils::indent(indent) << "\"gridSize\": " << grid->size;
            }
        },
        [](Registry& registry, VulkanRenderer& renderer, Entity entity, const std::string& json) {
            std::string type = JSONUtils::extractStringValue(json, "entityType");
            float spacing = 1.0f;
            float size = 100.0f;
            bool hasSpacing = JSONUtils::extractFloatValue(json, "gridSpacing", spacing);
            bool hasSize = JSONUtils::extractFloatValue(json, "gridSize", size);
            
            if (type == "Grid" || hasSpacing || hasSize) {
                glm::vec4 color(0.3f, 0.3f, 0.3f, 1.0f);
                float colorArray[4]{};
                if (JSONUtils::extractFloatArray(json, "color", colorArray, 4)) {
                    color = glm::vec4(colorArray[0], colorArray[1], colorArray[2], colorArray[3]);
                }
                
                registry.emplace<PrimitiveType>(entity, PrimitiveType{ PrimitiveKind::Quad });
                registry.emplace<Material>(entity, Material{ color });
                registry.emplace<Mesh>(entity, Primitives::makeQuad());
                registry.emplace<Grid>(entity, Grid{ spacing, size, color });
                
                PipelineHandle gridPipeline = renderer.createPipelineForShaders(
                    renderer.resolveShaderPath("build/shaders/grid.vert.spv"),
                    renderer.resolveShaderPath("build/shaders/grid.frag.spv")
                );
                
                if (Material* material = registry.get<Material>(entity)) {
                    material->pipeline = gridPipeline.pipeline;
                    material->pipelineLayout = gridPipeline.layout;
                }
                
                EntityFactory::uploadMesh(registry, renderer, entity);
            }
        }
    );

    // 3. Primitive Component (Mesh & Material)
    reg.registerComponent(
        "Primitive",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (registry.has<Engine::TilemapComponent>(entity) || registry.has<Engine::CanvasComponent>(entity) || registry.has<Engine::RectTransform>(entity) || registry.has<Engine::SpriteRenderer>(entity)) {
                return; // Skip primitive details for Tilemap, UI, and Sprite entities
            }

            if (auto* mesh = registry.get<Mesh>(entity)) {
                if (registry.has<Grid>(entity)) {
                    return; // Skip grid mesh details
                }
                if (!mesh->gltfPath.empty()) {
                    out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("Primitive") << ",\n";
                    out << JSONUtils::indent(indent) << "\"gltfPath\": " << JSONUtils::quote(mesh->gltfPath);
                    if (mesh->primitiveIndex != -1) {
                        out << ",\n" << JSONUtils::indent(indent) << "\"primitiveIndex\": " << mesh->primitiveIndex;
                    }
                } else if (auto* primitive = registry.get<PrimitiveType>(entity)) {
                    std::string primStr = "Cube";
                    if (primitive->kind == PrimitiveKind::Triangle) primStr = "Triangle";
                    else if (primitive->kind == PrimitiveKind::Quad) primStr = "Quad";
                    
                    out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("Primitive") << ",\n";
                    out << JSONUtils::indent(indent) << "\"primitive\": " << JSONUtils::quote(primStr);
                }
            }
            if (auto* material = registry.get<Material>(entity)) {
                if (registry.has<Grid>(entity)) {
                    return; // Skip grid color details
                }
                out << ",\n" << JSONUtils::indent(indent) << "\"color\": " << JSONUtils::vec4ToJson(material->color);
                if (!material->texturePath.empty()) {
                    out << ",\n" << JSONUtils::indent(indent) << "\"texturePath\": " << JSONUtils::quote(material->texturePath);
                }
                if (!material->normalMapPath.empty()) {
                    out << ",\n" << JSONUtils::indent(indent) << "\"normalMapPath\": " << JSONUtils::quote(material->normalMapPath);
                }
                if (!material->metallicMapPath.empty()) {
                    out << ",\n" << JSONUtils::indent(indent) << "\"metallicMapPath\": " << JSONUtils::quote(material->metallicMapPath);
                }
                out << ",\n" << JSONUtils::indent(indent) << "\"shaderName\": " << JSONUtils::quote(material->shaderName);
                out << ",\n" << JSONUtils::indent(indent) << "\"roughness\": " << material->roughness;
                out << ",\n" << JSONUtils::indent(indent) << "\"metallic\": " << material->metallic;
                if (material->filterMode != TextureFilterMode::Bilinear) {
                    std::string filterStr = "Bilinear";
                    if (material->filterMode == TextureFilterMode::Nearest) filterStr = "Nearest";
                    else if (material->filterMode == TextureFilterMode::Trilinear) filterStr = "Trilinear";
                    out << ",\n" << JSONUtils::indent(indent) << "\"textureFilter\": " << JSONUtils::quote(filterStr);
                }
            }
        },
        [](Registry& registry, VulkanRenderer& renderer, Entity entity, const std::string& json) {
            std::string type = JSONUtils::extractStringValue(json, "entityType");
            std::string primStr = JSONUtils::extractStringValue(json, "primitive");
            std::string gltfPath = JSONUtils::extractStringValue(json, "gltfPath");
            std::string texturePath = JSONUtils::extractStringValue(json, "texturePath");
            
            if (type == "Camera" || type == "Grid" || type == "Tilemap" || type == "Canvas" || type == "UIElement" ||
                json.find("\"hasRectTransform\":") != std::string::npos ||
                json.find("\"hasRectTransformComponent\":") != std::string::npos ||
                json.find("\"hasCanvas\":") != std::string::npos ||
                json.find("\"hasCanvasComponent\":") != std::string::npos ||
                json.find("\"anchorMin\":") != std::string::npos) {
                return; // Managed by separate component deserializers
            }


            // Skip grids using key detection to avoid double parsing
            float dummySpacing = 0.0f;
            if (JSONUtils::extractFloatValue(json, "gridSpacing", dummySpacing)) {
                return;
            }

            bool isPrimitive = (type == "Primitive" || !primStr.empty() || !gltfPath.empty());
            if (!isPrimitive) {
                float colorArray[4]{};
                if (JSONUtils::extractFloatArray(json, "color", colorArray, 4)) {
                    isPrimitive = true;
                    std::string nameVal = JSONUtils::extractStringValue(json, "name");
                    if (nameVal.find("Triangle") != std::string::npos) {
                        primStr = "Triangle";
                    } else if (nameVal.find("Quad") != std::string::npos) {
                        primStr = "Quad";
                    } else {
                        primStr = "Cube";
                    }
                }
            }

            if (isPrimitive) {
                PrimitiveKind kind = PrimitiveKind::Cube;
                if (primStr == "Triangle") kind = PrimitiveKind::Triangle;
                else if (primStr == "Quad") kind = PrimitiveKind::Quad;
                
                glm::vec4 color(1.0f);
                float colorArray[4]{};
                bool hasColor = JSONUtils::extractFloatArray(json, "color", colorArray, 4);
                if (hasColor) {
                    color = glm::vec4(colorArray[0], colorArray[1], colorArray[2], colorArray[3]);
                } else {
                    if (kind == PrimitiveKind::Triangle) color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    else if (kind == PrimitiveKind::Cube) color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
                }

                float primIndexFloat = -1.0f;
                int primitiveIndex = -1;
                if (JSONUtils::extractFloatValue(json, "primitiveIndex", primIndexFloat)) {
                    primitiveIndex = static_cast<int>(primIndexFloat);
                }
                
                bool isAssetMesh = !gltfPath.empty();
                Mesh meshData;
                
                if (isAssetMesh) {
                    try {
                        meshData = renderer.resourceManager->loadMesh(gltfPath, renderer, primitiveIndex);
                        meshData.primitiveIndex = primitiveIndex;
                        registry.emplace<PrimitiveType>(entity, PrimitiveType{ PrimitiveKind::Cube }); // Default placeholder

                        SkeletonComponent skeleton{};
                        AnimatorComponent animator{};
                        if (renderer.resourceManager->loadSkeletonAndAnimations(gltfPath, skeleton, animator)) {
                            registry.emplace<SkeletonComponent>(entity, std::move(skeleton));
                            registry.emplace<AnimatorComponent>(entity, std::move(animator));
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[SceneSerializer] Error loading mesh " << gltfPath << ": " << e.what() << std::endl;
                        meshData = Primitives::makeCube();
                        registry.emplace<PrimitiveType>(entity, PrimitiveType{ PrimitiveKind::Cube });
                    }
                } else {
                    registry.emplace<PrimitiveType>(entity, PrimitiveType{ kind });
                    switch (kind) {
                    case PrimitiveKind::Triangle: meshData = Primitives::makeTriangle(); break;
                    case PrimitiveKind::Cube: meshData = Primitives::makeCube(); break;
                    case PrimitiveKind::Quad: meshData = Primitives::makeQuad(); break;
                    }
                }
                
                registry.emplace<Mesh>(entity, std::move(meshData));
                registry.emplace<Material>(entity, Material{ color });
                
                if (auto* material = registry.get<Material>(entity)) {
                    material->texturePath = texturePath;
                    material->normalMapPath = JSONUtils::extractStringValue(json, "normalMapPath");
                    material->metallicMapPath = JSONUtils::extractStringValue(json, "metallicMapPath");
                    
                    std::string shaderName = JSONUtils::extractStringValue(json, "shaderName");
                    if (shaderName.empty()) shaderName = "Unlit";
                    material->shaderName = shaderName;
                    
                    float roughnessVal = 0.5f;
                    JSONUtils::extractFloatValue(json, "roughness", roughnessVal);
                    material->roughness = roughnessVal;
                    
                    float metallicVal = 0.0f;
                    JSONUtils::extractFloatValue(json, "metallic", metallicVal);
                    material->metallic = metallicVal;

                    std::string textureFilter = JSONUtils::extractStringValue(json, "textureFilter");
                    TextureFilterMode filterMode = TextureFilterMode::Bilinear;
                    if (textureFilter == "Nearest" || textureFilter == "Point" || textureFilter == "nearest") {
                        filterMode = TextureFilterMode::Nearest;
                    } else if (textureFilter == "Trilinear" || textureFilter == "trilinear") {
                        filterMode = TextureFilterMode::Trilinear;
                    }
                    material->filterMode = filterMode;

                    // Update material descriptors with diffuse, normal, and metallic textures
                    renderer.resourceManager->updateMaterialDescriptorSet(*material, renderer);
                    
                    bool hasSkin = registry.has<SkeletonComponent>(entity);
                    std::string vertShader = "unlit.vert.spv";
                    std::string fragShader = "unlit.frag.spv";
                    if (shaderName == "Lit") {
                        vertShader = hasSkin ? "skinned_lit.vert.spv" : "lit.vert.spv";
                        fragShader = "lit.frag.spv";
                    } else {
                        vertShader = hasSkin ? "skinned.vert.spv" : "unlit.vert.spv";
                        fragShader = "unlit.frag.spv";
                    }

                    PipelineHandle pipeline = renderer.createPipelineForShaders(
                        renderer.resolveShaderPath("build/shaders/" + vertShader),
                        renderer.resolveShaderPath("build/shaders/" + fragShader)
                    );
                    material->pipeline = pipeline.pipeline;
                    material->pipelineLayout = pipeline.layout;
                }
                
                if (!isAssetMesh) {
                    EntityFactory::uploadMesh(registry, renderer, entity);
                }
            }
        }
    );

    // 4. Hierarchy Component (Parent-Child relationship)
    reg.registerComponent(
        "Hierarchy",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* hierarchy = registry.get<HierarchyComponent>(entity)) {
                if (hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
                    if (auto* parentName = registry.get<Name>(hierarchy->parent)) {
                        out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("Hierarchy") << ",\n";
                        out << JSONUtils::indent(indent) << "\"parentName\": " << JSONUtils::quote(parentName->value);
                    }
                }
            }
        },
        [](Registry& registry, VulkanRenderer& renderer, Entity entity, const std::string& json) {
            std::string parentNameStr = JSONUtils::extractStringValue(json, "parentName");
            if (!parentNameStr.empty()) {
                registry.emplace<ParentNameComponent>(entity, ParentNameComponent{ parentNameStr });
            }
        }
    );

    // 5. Animation Controller Component
    reg.registerComponent(
        "AnimationController",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* controller = registry.get<AnimationControllerComponent>(entity)) {
                out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("AnimationController");
                out << ",\n" << JSONUtils::indent(indent) << "\"currentState\": " << JSONUtils::quote(controller->currentState);
                
                bool hasLocomotion = (controller->states.size() == 2 && controller->transitions.size() == 2);
                out << ",\n" << JSONUtils::indent(indent) << "\"hasLocomotionSetup\": " << (hasLocomotion ? "true" : "false");
                
                out << ",\n" << JSONUtils::indent(indent) << "\"parameters\": {";
                bool first = true;
                for (const auto& [name, val] : controller->parameters) {
                    if (!first) out << ", ";
                    out << JSONUtils::quote(name) << ": " << val;
                    first = false;
                }
                out << "}";

                // Serialize custom states
                out << ",\n" << JSONUtils::indent(indent) << "\"states\": [";
                for (size_t i = 0; i < controller->states.size(); ++i) {
                    const auto& state = controller->states[i];
                    if (i > 0) out << ", ";
                    out << "{\n" << JSONUtils::indent(indent + 1) << "\"name\": " << JSONUtils::quote(state.name) << ", ";
                    out << "\"clipName\": " << JSONUtils::quote(state.clipName) << ", ";
                    out << "\"isBlendTree\": " << (state.isBlendTree ? "true" : "false") << ", ";
                    out << "\"isLooping\": " << (state.isLooping ? "true" : "false") << ", ";
                    out << "\"speed\": " << state.speed << ", ";
                    out << "\"blendTree\": {\n" << JSONUtils::indent(indent + 2) << "\"is2D\": " << (state.blendTree.is2D ? "true" : "false") << ", ";
                    out << "\"parameterName\": " << JSONUtils::quote(state.blendTree.parameterName) << ", ";
                    out << "\"parameterYName\": " << JSONUtils::quote(state.blendTree.parameterYName) << ", ";
                    out << "\"nodes\": [";
                    for (size_t n = 0; n < state.blendTree.nodes.size(); ++n) {
                        const auto& node = state.blendTree.nodes[n];
                        if (n > 0) out << ", ";
                        out << "{\"clipName\": " << JSONUtils::quote(node.clipName);
                        out << ", \"threshold\": " << node.threshold;
                        out << ", \"threshold2D_x\": " << node.threshold2D.x;
                        out << ", \"threshold2D_y\": " << node.threshold2D.y;
                        out << "}";
                    }
                    out << "]\n" << JSONUtils::indent(indent + 1) << "}\n" << JSONUtils::indent(indent) << "}";
                }
                out << "]";

                // Serialize custom transitions
                out << ",\n" << JSONUtils::indent(indent) << "\"transitions\": [";
                for (size_t i = 0; i < controller->transitions.size(); ++i) {
                    const auto& trans = controller->transitions[i];
                    if (i > 0) out << ", ";
                    out << "{\n" << JSONUtils::indent(indent + 1) << "\"fromState\": " << JSONUtils::quote(trans.fromState) << ", ";
                    out << "\"toState\": " << JSONUtils::quote(trans.toState) << ", ";
                    out << "\"crossfadeDuration\": " << trans.crossfadeDuration << ", ";
                    out << "\"conditions\": [";
                    for (size_t c = 0; c < trans.conditions.size(); ++c) {
                        const auto& cond = trans.conditions[c];
                        if (c > 0) out << ", ";
                        out << "{\"parameterName\": " << JSONUtils::quote(cond.parameterName);
                        out << ", \"op\": " << JSONUtils::quote(cond.op);
                        out << ", \"value\": " << cond.value;
                        out << "}";
                    }
                    out << "]\n" << JSONUtils::indent(indent) << "}";
                }
                out << "]";
            }
        },
        [](Registry& registry, VulkanRenderer& renderer, Entity entity, const std::string& json) {
            if (json.find("\"entityType\": \"AnimationController\"") != std::string::npos ||
                json.find("\"entityType\":\"AnimationController\"") != std::string::npos) {
                
                AnimationControllerComponent controller{};
                controller.currentState = JSONUtils::extractStringValue(json, "currentState");
                
                bool hasLocomotion = false;
                if (json.find("\"hasLocomotionSetup\": true") != std::string::npos ||
                    json.find("\"hasLocomotionSetup\":true") != std::string::npos) {
                    hasLocomotion = true;
                }
                
                // Helper lambdas for array parsing
                auto extractArrayContent = [](const std::string& source, const std::string& key) -> std::string {
                    size_t keyPos = source.find(key);
                    if (keyPos == std::string::npos) return "";
                    size_t startPos = source.find("[", keyPos);
                    if (startPos == std::string::npos) return "";
                    int bracketCount = 1;
                    size_t endPos = startPos + 1;
                    while (endPos < source.size() && bracketCount > 0) {
                        if (source[endPos] == '[') bracketCount++;
                        else if (source[endPos] == ']') bracketCount--;
                        endPos++;
                    }
                    if (bracketCount == 0) {
                        return source.substr(startPos + 1, endPos - startPos - 2);
                    }
                    return "";
                };

                auto splitObjects = [](const std::string& arrayContent) -> std::vector<std::string> {
                    std::vector<std::string> objects;
                    size_t p = 0;
                    while (p < arrayContent.size()) {
                        size_t objStart = arrayContent.find("{", p);
                        if (objStart == std::string::npos) break;
                        int braceCount = 1;
                        size_t objEnd = objStart + 1;
                        while (objEnd < arrayContent.size() && braceCount > 0) {
                            if (arrayContent[objEnd] == '{') braceCount++;
                            else if (arrayContent[objEnd] == '}') braceCount--;
                            objEnd++;
                        }
                        if (braceCount == 0) {
                            objects.push_back(arrayContent.substr(objStart, objEnd - objStart));
                        }
                        p = objEnd;
                    }
                    return objects;
                };

                // Deserialise parameters map
                size_t paramsPos = json.find("\"parameters\"");
                if (paramsPos != std::string::npos) {
                    size_t startPos = json.find("{", paramsPos);
                    if (startPos != std::string::npos) {
                        size_t endPos = json.find("}", startPos);
                        if (endPos != std::string::npos) {
                            std::string paramsContent = json.substr(startPos + 1, endPos - startPos - 1);
                            size_t p = 0;
                            while (p < paramsContent.size()) {
                                size_t keyStart = paramsContent.find("\"", p);
                                if (keyStart == std::string::npos) break;
                                size_t keyEnd = paramsContent.find("\"", keyStart + 1);
                                if (keyEnd == std::string::npos) break;
                                std::string paramName = paramsContent.substr(keyStart + 1, keyEnd - keyStart - 1);

                                size_t colonPos = paramsContent.find(":", keyEnd);
                                if (colonPos == std::string::npos) break;
                                float val = 0.0f;
                                std::string valStr;
                                for (size_t i = colonPos + 1; i < paramsContent.size(); ++i) {
                                    char c = paramsContent[i];
                                    if (std::isdigit(c) || c == '.' || c == '-') {
                                        valStr += c;
                                    } else if (!valStr.empty() || c == ',') {
                                        break;
                                    }
                                }
                                if (!valStr.empty()) {
                                    val = std::stof(valStr);
                                }
                                controller.parameters[paramName] = val;
                                p = colonPos + valStr.size() + 1;
                            }
                        }
                    }
                }

                // Deserialise custom states
                std::string statesArray = extractArrayContent(json, "\"states\"");
                if (!statesArray.empty()) {
                    for (const auto& stateObj : splitObjects(statesArray)) {
                        AnimationState state;
                        state.name = JSONUtils::extractStringValue(stateObj, "name");
                        state.clipName = JSONUtils::extractStringValue(stateObj, "clipName");
                        
                        bool isBlendTree = false;
                        if (stateObj.find("\"isBlendTree\": true") != std::string::npos ||
                            stateObj.find("\"isBlendTree\":true") != std::string::npos) {
                            isBlendTree = true;
                        }
                        state.isBlendTree = isBlendTree;

                        bool isLooping = true;
                        if (stateObj.find("\"isLooping\": false") != std::string::npos ||
                            stateObj.find("\"isLooping\":false") != std::string::npos) {
                            isLooping = false;
                        }
                        state.isLooping = isLooping;

                        JSONUtils::extractFloatValue(stateObj, "speed", state.speed);

                        // Parse blendTree sub-object
                        size_t btPos = stateObj.find("\"blendTree\"");
                        if (btPos != std::string::npos) {
                            std::string btSub = stateObj.substr(btPos);
                            bool is2D = false;
                            if (btSub.find("\"is2D\": true") != std::string::npos ||
                                btSub.find("\"is2D\":true") != std::string::npos) {
                                is2D = true;
                            }
                            state.blendTree.is2D = is2D;
                            state.blendTree.parameterName = JSONUtils::extractStringValue(btSub, "parameterName");
                            state.blendTree.parameterYName = JSONUtils::extractStringValue(btSub, "parameterYName");

                            // Parse blend tree nodes array
                            std::string nodesArray = extractArrayContent(btSub, "\"nodes\"");
                            if (!nodesArray.empty()) {
                                for (const auto& nodeObj : splitObjects(nodesArray)) {
                                    BlendNode node;
                                    node.clipName = JSONUtils::extractStringValue(nodeObj, "clipName");
                                    JSONUtils::extractFloatValue(nodeObj, "threshold", node.threshold);
                                    JSONUtils::extractFloatValue(nodeObj, "threshold2D_x", node.threshold2D.x);
                                    JSONUtils::extractFloatValue(nodeObj, "threshold2D_y", node.threshold2D.y);
                                    state.blendTree.nodes.push_back(node);
                                }
                            }
                        }
                        controller.states.push_back(state);
                    }
                }

                // Deserialise custom transitions
                std::string transArray = extractArrayContent(json, "\"transitions\"");
                if (!transArray.empty()) {
                    for (const auto& transObj : splitObjects(transArray)) {
                        AnimationTransition trans;
                        trans.fromState = JSONUtils::extractStringValue(transObj, "fromState");
                        trans.toState = JSONUtils::extractStringValue(transObj, "toState");
                        JSONUtils::extractFloatValue(transObj, "crossfadeDuration", trans.crossfadeDuration);

                        std::string condsArray = extractArrayContent(transObj, "\"conditions\"");
                        if (!condsArray.empty()) {
                            for (const auto& condObj : splitObjects(condsArray)) {
                                TransitionCondition cond;
                                cond.parameterName = JSONUtils::extractStringValue(condObj, "parameterName");
                                cond.op = JSONUtils::extractStringValue(condObj, "op");
                                JSONUtils::extractFloatValue(condObj, "value", cond.value);
                                trans.conditions.push_back(cond);
                            }
                        }
                        controller.transitions.push_back(trans);
                    }
                }

                // Backward compatibility fallback for older scene files
                if (controller.states.empty() && hasLocomotion) {
                    AnimationState idleState;
                    idleState.name = "Idle";
                    idleState.clipName = "idle";
                    idleState.isBlendTree = false;
                    
                    AnimationState moveState;
                    moveState.name = "Movement";
                    moveState.isBlendTree = true;
                    moveState.blendTree.parameterName = "speed";
                    
                    if (auto* animator = registry.get<AnimatorComponent>(entity)) {
                        if (!animator->animations.empty()) idleState.clipName = animator->animations[0].name;
                        if (animator->animations.size() >= 2) {
                            BlendNode nodeWalk{ animator->animations[0].name, 0.0f };
                            BlendNode nodeRun{ animator->animations[1].name, 1.0f };
                            moveState.blendTree.nodes = { nodeWalk, nodeRun };
                        } else if (!animator->animations.empty()) {
                            BlendNode nodeWalk{ animator->animations[0].name, 0.0f };
                            moveState.blendTree.nodes = { nodeWalk };
                        }
                    }
                    
                    controller.states = { idleState, moveState };

                    AnimationTransition toMove;
                    toMove.fromState = "Idle";
                    toMove.toState = "Movement";
                    toMove.crossfadeDuration = 0.3f;
                    toMove.conditions = { TransitionCondition{ "speed", ">", 0.1f } };
                    
                    AnimationTransition toIdle;
                    toIdle.fromState = "Movement";
                    toIdle.toState = "Idle";
                    toIdle.crossfadeDuration = 0.3f;
                    toIdle.conditions = { TransitionCondition{ "speed", "<", 0.1f } };
                    
                    controller.transitions = { toMove, toIdle };
                }

                registry.emplace<AnimationControllerComponent>(entity, std::move(controller));
            }
        }
    );

    // 6. IK Solver Component
    reg.registerComponent(
        "IKSolver",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* ik = registry.get<IKSolverComponent>(entity)) {
                out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("IKSolver");
                out << ",\n" << JSONUtils::indent(indent) << "\"enabled\": " << (ik->enabled ? "true" : "false");
                out << ",\n" << JSONUtils::indent(indent) << "\"solverType\": " << static_cast<int>(ik->solverType);
                out << ",\n" << JSONUtils::indent(indent) << "\"startJointName\": " << JSONUtils::quote(ik->startJointName);
                out << ",\n" << JSONUtils::indent(indent) << "\"middleJointName\": " << JSONUtils::quote(ik->middleJointName);
                out << ",\n" << JSONUtils::indent(indent) << "\"endJointName\": " << JSONUtils::quote(ik->endJointName);
                out << ",\n" << JSONUtils::indent(indent) << "\"maxIterations\": " << ik->maxIterations;
                out << ",\n" << JSONUtils::indent(indent) << "\"tolerance\": " << ik->tolerance;
                
                out << ",\n" << JSONUtils::indent(indent) << "\"jointChainNames\": [";
                for (size_t i = 0; i < ik->jointChainNames.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << JSONUtils::quote(ik->jointChainNames[i]);
                }
                out << "]";
                
                out << ",\n" << JSONUtils::indent(indent) << "\"targetPosition\": " << JSONUtils::vec3ToJson(ik->targetPosition);
                out << ",\n" << JSONUtils::indent(indent) << "\"polePosition\": " << JSONUtils::vec3ToJson(ik->polePosition);
                out << ",\n" << JSONUtils::indent(indent) << "\"targetWeight\": " << ik->targetWeight;
            }
        },
        [](Registry& registry, VulkanRenderer& renderer, Entity entity, const std::string& json) {
            if (json.find("\"entityType\": \"IKSolver\"") != std::string::npos ||
                json.find("\"entityType\":\"IKSolver\"") != std::string::npos) {
                
                IKSolverComponent ik{};
                ik.enabled = (json.find("\"enabled\": true") != std::string::npos || json.find("\"enabled\":true") != std::string::npos);
                
                float typeVal = 0.0f;
                if (JSONUtils::extractFloatValue(json, "solverType", typeVal)) {
                    ik.solverType = static_cast<int>(typeVal) == 0 ? IKSolverType::TwoBone : IKSolverType::FABRIK;
                }
                
                ik.startJointName = JSONUtils::extractStringValue(json, "startJointName");
                ik.middleJointName = JSONUtils::extractStringValue(json, "middleJointName");
                ik.endJointName = JSONUtils::extractStringValue(json, "endJointName");
                
                float maxIter = 10.0f;
                if (JSONUtils::extractFloatValue(json, "maxIterations", maxIter)) {
                    ik.maxIterations = static_cast<int>(maxIter);
                }
                float tol = 0.001f;
                JSONUtils::extractFloatValue(json, "tolerance", tol);
                ik.tolerance = tol;
                
                // Parse joint chain names array
                size_t arrayStart = json.find("\"jointChainNames\"");
                if (arrayStart != std::string::npos) {
                    size_t braceStart = json.find("[", arrayStart);
                    size_t braceEnd = json.find("]", arrayStart);
                    if (braceStart != std::string::npos && braceEnd != std::string::npos && braceEnd > braceStart) {
                        std::string arrayContent = json.substr(braceStart + 1, braceEnd - braceStart - 1);
                        std::stringstream ss(arrayContent);
                        std::string item;
                        while (std::getline(ss, item, ',')) {
                            size_t q1 = item.find("\"");
                            size_t q2 = item.rfind("\"");
                            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                                ik.jointChainNames.push_back(item.substr(q1 + 1, q2 - q1 - 1));
                            }
                        }
                    }
                }
                
                float targetPos[3]{};
                if (JSONUtils::extractFloatArray(json, "targetPosition", targetPos, 3)) {
                    ik.targetPosition = glm::vec3(targetPos[0], targetPos[1], targetPos[2]);
                }
                float polePos[3]{};
                if (JSONUtils::extractFloatArray(json, "polePosition", polePos, 3)) {
                    ik.polePosition = glm::vec3(polePos[0], polePos[1], polePos[2]);
                }
                
                float weight = 1.0f;
                if (JSONUtils::extractFloatValue(json, "targetWeight", weight)) {
                    ik.targetWeight = weight;
                }
                
                registry.emplace<IKSolverComponent>(entity, std::move(ik));
            }
        }
    );

    // Reflected Components (RigidBody and PlayerController)
    // Registered dynamically at the end of registerBuiltinComponents() via reflection

    // 9. Collider Component
    reg.registerComponent(
        "Collider",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* col = registry.get<ColliderComponent>(entity)) {
                std::string shapeStr = "AABB";
                if (col->shape == ColliderShape::Sphere) shapeStr = "Sphere";
                else if (col->shape == ColliderShape::OBB) shapeStr = "OBB";
                else if (col->shape == ColliderShape::Capsule) shapeStr = "Capsule";
                out << ",\n" << JSONUtils::indent(indent) << "\"hasCollider\": true";
                out << ",\n" << JSONUtils::indent(indent) << "\"colShape\": " << JSONUtils::quote(shapeStr);
                out << ",\n" << JSONUtils::indent(indent) << "\"colRadius\": " << col->radius;
                out << ",\n" << JSONUtils::indent(indent) << "\"colHeight\": " << col->height;
                out << ",\n" << JSONUtils::indent(indent) << "\"colExtX\": " << col->extents.x;
                out << ",\n" << JSONUtils::indent(indent) << "\"colExtY\": " << col->extents.y;
                out << ",\n" << JSONUtils::indent(indent) << "\"colExtZ\": " << col->extents.z;
                out << ",\n" << JSONUtils::indent(indent) << "\"colOffsetX\": " << col->offset.x;
                out << ",\n" << JSONUtils::indent(indent) << "\"colOffsetY\": " << col->offset.y;
                out << ",\n" << JSONUtils::indent(indent) << "\"colOffsetZ\": " << col->offset.z;
            }
        },
        [](Registry& registry, VulkanRenderer&, Entity entity, const std::string& json) {
            float dummyVal = 0.0f;
            if (JSONUtils::extractFloatValue(json, "colRadius", dummyVal) || json.find("\"hasCollider\": true") != std::string::npos || json.find("\"hasCollider\":true") != std::string::npos) {
                ColliderComponent col{};
                std::string shapeStr = JSONUtils::extractStringValue(json, "colShape");
                if (shapeStr == "Sphere") col.shape = ColliderShape::Sphere;
                else if (shapeStr == "OBB") col.shape = ColliderShape::OBB;
                else if (shapeStr == "Capsule") col.shape = ColliderShape::Capsule;
                else col.shape = ColliderShape::AABB;
                JSONUtils::extractFloatValue(json, "colRadius", col.radius);
                JSONUtils::extractFloatValue(json, "colHeight", col.height);
                JSONUtils::extractFloatValue(json, "colExtX", col.extents.x);
                JSONUtils::extractFloatValue(json, "colExtY", col.extents.y);
                JSONUtils::extractFloatValue(json, "colExtZ", col.extents.z);
                JSONUtils::extractFloatValue(json, "colOffsetX", col.offset.x);
                JSONUtils::extractFloatValue(json, "colOffsetY", col.offset.y);
                JSONUtils::extractFloatValue(json, "colOffsetZ", col.offset.z);
                registry.emplace<ColliderComponent>(entity, std::move(col));
            }
        }
    );

    // PlayerController registered dynamically via reflection

    // 11. Animator Component
    reg.registerComponent(
        "Animator",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* animator = registry.get<AnimatorComponent>(entity)) {
                std::string entityName = "Unknown";
                if (auto* nameComp = registry.get<Name>(entity)) {
                    entityName = nameComp->value;
                }
                std::cout << "[Serializer] Saving Animator for " << entityName 
                          << " with active index: " << animator->activeAnimationIndex << std::endl;
                out << ",\n" << JSONUtils::indent(indent) << "\"hasAnimator\": true,\n";
                out << JSONUtils::indent(indent) << "\"animActiveIndex\": " << animator->activeAnimationIndex << ",\n";
                out << JSONUtils::indent(indent) << "\"animPlaybackSpeed\": " << animator->playbackSpeed << ",\n";
                out << JSONUtils::indent(indent) << "\"animLoop\": " << (animator->loop ? "true" : "false") << ",\n";
                out << JSONUtils::indent(indent) << "\"loadedAnimPath\": " << JSONUtils::quote(animator->loadedAnimPath);
            }
        },
        [](Registry& registry, VulkanRenderer& renderer, Entity entity, const std::string& json) {
            float dummyVal = 0.0f;
            if (JSONUtils::extractFloatValue(json, "animActiveIndex", dummyVal) || json.find("\"hasAnimator\": true") != std::string::npos || json.find("\"hasAnimator\":true") != std::string::npos) {
                float activeIdxFloat = -1.0f;
                float speed = 1.0f;
                bool loop = true;
                
                JSONUtils::extractFloatValue(json, "animActiveIndex", activeIdxFloat);
                JSONUtils::extractFloatValue(json, "animPlaybackSpeed", speed);
                
                std::string loopStr = JSONUtils::extractStringValue(json, "animLoop");
                if (loopStr == "false" || json.find("\"animLoop\": false") != std::string::npos || json.find("\"animLoop\":false") != std::string::npos) {
                    loop = false;
                }

                std::string loadedAnimPath = JSONUtils::extractStringValue(json, "loadedAnimPath");

                std::string entityName = "Unknown";
                if (auto* nameComp = registry.get<Name>(entity)) {
                    entityName = nameComp->value;
                }
                std::cout << "[Deserializer] Restoring Animator for " << entityName 
                          << " with active index: " << static_cast<int>(activeIdxFloat) 
                          << ", loaded path: " << loadedAnimPath << std::endl;

                AnimatorComponent* animator = registry.get<AnimatorComponent>(entity);
                if (!animator) {
                    AnimatorComponent animatorComp{};
                    registry.emplace<AnimatorComponent>(entity, std::move(animatorComp));
                    animator = registry.get<AnimatorComponent>(entity);
                }

                if (!loadedAnimPath.empty()) {
                    bool isAnimFile = (loadedAnimPath.length() >= 5 && loadedAnimPath.substr(loadedAnimPath.length() - 5) == ".anim");
                    SkeletonComponent* skeleton = registry.get<SkeletonComponent>(entity);
                    if (!skeleton && !isAnimFile) {
                        SkeletonComponent newSkel{};
                        registry.emplace<SkeletonComponent>(entity, std::move(newSkel));
                        skeleton = registry.get<SkeletonComponent>(entity);
                    }
                    if (animator) {
                        if (skeleton) {
                            renderer.resourceManager->loadSkeletonAndAnimations(loadedAnimPath, *skeleton, *animator);
                        } else {
                            SkeletonComponent dummySkel;
                            renderer.resourceManager->loadSkeletonAndAnimations(loadedAnimPath, dummySkel, *animator);
                        }
                        animator->loadedAnimPath = loadedAnimPath;
                    }
                }

                if (animator) {
                    animator->activeAnimationIndex = static_cast<int>(activeIdxFloat);
                    animator->playbackSpeed = speed;
                    animator->loop = loop;
                }
            }
        }
    );

    // 5. Tileset — now a disk asset (.tileset file), NOT serialized into the scene.
    //    The Tilemap serializer references it by path string (tilesetPath).

    // 5b. SpriteRenderer Component Serializer
    reg.registerComponent(
        "SpriteRenderer",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* spr = registry.get<Engine::SpriteRenderer>(entity)) {
                out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("SpriteRenderer") << ",\n";
                out << JSONUtils::indent(indent) << "\"sprTexturePath\": " << JSONUtils::quote(spr->texturePath) << ",\n";
                out << JSONUtils::indent(indent) << "\"sprColor\": [" << spr->color.r << "," << spr->color.g << "," << spr->color.b << "," << spr->color.a << "],\n";
                out << JSONUtils::indent(indent) << "\"sprFlipX\": " << (spr->flipX ? "true" : "false") << ",\n";
                out << JSONUtils::indent(indent) << "\"sprFlipY\": " << (spr->flipY ? "true" : "false") << ",\n";
                out << JSONUtils::indent(indent) << "\"sprSortOrder\": " << spr->sortOrder;
            }
        },
        [](Registry& registry, VulkanRenderer&, Entity entity, const std::string& json) {
            if (json.find("\"sprTexturePath\"") == std::string::npos &&
                json.find("\"entityType\": \"SpriteRenderer\"") == std::string::npos &&
                json.find("\"entityType\":\"SpriteRenderer\"") == std::string::npos) return;

            if (!registry.has<Engine::SpriteRenderer>(entity)) {
                registry.emplace<Engine::SpriteRenderer>(entity, Engine::SpriteRenderer{});
            }
            if (auto* spr = registry.get<Engine::SpriteRenderer>(entity)) {
                spr->texturePath = JSONUtils::extractStringValue(json, "sprTexturePath");

                // Colour
                float col[4] = { 1.f, 1.f, 1.f, 1.f };
                if (JSONUtils::extractFloatArray(json, "sprColor", col, 4)) {
                    spr->color = { col[0], col[1], col[2], col[3] };
                }

                // Flip
                spr->flipX = (json.find("\"sprFlipX\": true") != std::string::npos ||
                              json.find("\"sprFlipX\":true")  != std::string::npos);
                spr->flipY = (json.find("\"sprFlipY\": true") != std::string::npos ||
                              json.find("\"sprFlipY\":true")  != std::string::npos);

                // Sort order
                float val = 0.f;
                if (JSONUtils::extractFloatValue(json, "sprSortOrder", val))
                    spr->sortOrder = static_cast<int>(val);

                spr->_dirty = true;
            }
        }
    );

    // 6. Tilemap Component Serializer
    reg.registerComponent(
        "Tilemap",
        [](Registry& registry, Entity entity, std::ostream& out, int indent) {
            if (auto* tm = registry.get<Engine::TilemapComponent>(entity)) {
                out << ",\n" << JSONUtils::indent(indent) << "\"entityType\": " << JSONUtils::quote("Tilemap") << ",\n";
                out << JSONUtils::indent(indent) << "\"width\": " << tm->width << ",\n";
                out << JSONUtils::indent(indent) << "\"height\": " << tm->height << ",\n";
                out << JSONUtils::indent(indent) << "\"tileSize\": " << tm->tileSize << ",\n";
                out << JSONUtils::indent(indent) << "\"tilesetPath\": " << JSONUtils::quote(tm->tilesetPath) << ",\n";

                out << JSONUtils::indent(indent) << "\"layers\": [\n";
                for (size_t l = 0; l < tm->layers.size(); ++l) {
                    const auto& layer = tm->layers[l];
                    out << JSONUtils::indent(indent + 1) << "{\n";
                    out << JSONUtils::indent(indent + 2) << "\"name\": " << JSONUtils::quote(layer.name) << ",\n";
                    out << JSONUtils::indent(indent + 2) << "\"zOffset\": " << layer.zOffset << ",\n";
                    out << JSONUtils::indent(indent + 2) << "\"tag\": " << JSONUtils::quote(layer.tag) << ",\n";
                    out << JSONUtils::indent(indent + 2) << "\"isVisible\": " << (layer.isVisible ? "true" : "false") << ",\n";
                    out << JSONUtils::indent(indent + 2) << "\"isCollision\": " << (layer.isCollision ? "true" : "false") << ",\n";
                    out << JSONUtils::indent(indent + 2) << "\"chunks\": [\n";

                    size_t cIdx = 0;
                    for (const auto& [key, chunk] : layer.chunks) {
                        int cx, cy;
                        Engine::unpackChunkKey(key, cx, cy);
                        out << JSONUtils::indent(indent + 3) << "{\n";
                        out << JSONUtils::indent(indent + 4) << "\"cx\": " << cx << ",\n";
                        out << JSONUtils::indent(indent + 4) << "\"cy\": " << cy << ",\n";
                        out << JSONUtils::indent(indent + 4) << "\"tiles\": [";
                        for (int i = 0; i < Engine::TILE_CHUNK_SIZE * Engine::TILE_CHUNK_SIZE; ++i) {
                            out << chunk.tiles[i];
                            if (i + 1 < Engine::TILE_CHUNK_SIZE * Engine::TILE_CHUNK_SIZE) out << ", ";
                        }
                        out << "],\n";
                        out << JSONUtils::indent(indent + 4) << "\"rotations\": [";
                        for (int i = 0; i < Engine::TILE_CHUNK_SIZE * Engine::TILE_CHUNK_SIZE; ++i) {
                            out << (int)chunk.rotations[i];
                            if (i + 1 < Engine::TILE_CHUNK_SIZE * Engine::TILE_CHUNK_SIZE) out << ", ";
                        }
                        out << "]\n";
                        out << JSONUtils::indent(indent + 3) << "}";
                        if (++cIdx < layer.chunks.size()) out << ",";
                        out << "\n";
                    }
                    out << JSONUtils::indent(indent + 2) << "]\n";
                    out << JSONUtils::indent(indent + 1) << "}";
                    if (l + 1 < tm->layers.size()) out << ",";
                    out << "\n";
                }
                out << JSONUtils::indent(indent) << "]\n";
            }
        },
        [](Registry& registry, VulkanRenderer&, Entity entity, const std::string& json) {
            std::string type = JSONUtils::extractStringValue(json, "entityType");
            if (type == "Tilemap" || json.find("\"tiles\":") != std::string::npos || json.find("\"layers\":") != std::string::npos || json.find("\"chunks\":") != std::string::npos) {
                if (!registry.has<Engine::TilemapComponent>(entity)) {
                    registry.emplace<Engine::TilemapComponent>(entity, Engine::TilemapComponent{});
                }
                if (auto* tm = registry.get<Engine::TilemapComponent>(entity)) {
                    float val = 0.f;
                    if (JSONUtils::extractFloatValue(json, "width",    val)) tm->width    = (int)val;
                    if (JSONUtils::extractFloatValue(json, "height",   val)) tm->height   = (int)val;
                    if (JSONUtils::extractFloatValue(json, "tileSize", val)) tm->tileSize = val;

                    tm->tilesetPath = JSONUtils::extractStringValue(json, "tilesetPath");

                    tm->layers.clear();

                    size_t layersPos = json.find("\"layers\":");
                    if (layersPos != std::string::npos) {
                        size_t startBracket = json.find('[', layersPos);
                        if (startBracket != std::string::npos) {
                            int bracketDepth = 1;
                            size_t endBracket = startBracket + 1;
                            while (endBracket < json.size() && bracketDepth > 0) {
                                if (json[endBracket] == '[') bracketDepth++;
                                else if (json[endBracket] == ']') bracketDepth--;
                                endBracket++;
                            }
                            if (bracketDepth == 0) {
                                std::string layersArrayContent = json.substr(startBracket + 1, (endBracket - 1) - startBracket - 1);
                                size_t searchPos = 0;
                                while (true) {
                                    size_t openBrace = layersArrayContent.find('{', searchPos);
                                    if (openBrace == std::string::npos) break;

                                    int braceDepth = 1;
                                    size_t closeBrace = openBrace + 1;
                                    while (closeBrace < layersArrayContent.size() && braceDepth > 0) {
                                        if (layersArrayContent[closeBrace] == '{') braceDepth++;
                                        else if (layersArrayContent[closeBrace] == '}') braceDepth--;
                                        closeBrace++;
                                    }

                                    if (braceDepth == 0) {
                                        std::string layerJson = layersArrayContent.substr(openBrace, closeBrace - openBrace);
                                        Engine::TilemapLayer layer;
                                        layer.name = JSONUtils::extractStringValue(layerJson, "name");

                                        float zOffsetVal = 0.0f;
                                        JSONUtils::extractFloatValue(layerJson, "zOffset", zOffsetVal);
                                        layer.zOffset = zOffsetVal;

                                        layer.tag = JSONUtils::extractStringValue(layerJson, "tag");

                                        layer.isVisible = true;
                                        size_t visPos = layerJson.find("\"isVisible\":");
                                        if (visPos != std::string::npos) {
                                            size_t colonPos = layerJson.find(':', visPos);
                                            if (colonPos != std::string::npos) {
                                                std::string valSub = layerJson.substr(colonPos + 1, 12);
                                                if (valSub.find("false") != std::string::npos) {
                                                    layer.isVisible = false;
                                                }
                                            }
                                        }

                                        layer.isCollision = false;
                                        size_t colPos = layerJson.find("\"isCollision\":");
                                        if (colPos != std::string::npos) {
                                            size_t colonPos = layerJson.find(':', colPos);
                                            if (colonPos != std::string::npos) {
                                                std::string valSub = layerJson.substr(colonPos + 1, 12);
                                                if (valSub.find("true") != std::string::npos) {
                                                    layer.isCollision = true;
                                                }
                                            }
                                        } else {
                                            std::string lowerTag = layer.tag;
                                            for (char& c : lowerTag) c = (char)::tolower((unsigned char)c);
                                            if (lowerTag.find("obstacle") != std::string::npos) {
                                                layer.isCollision = true;
                                            }
                                        }

                                        // 1. Check for chunked format ("chunks": [...])
                                        size_t chunksPos = layerJson.find("\"chunks\":");
                                        if (chunksPos != std::string::npos) {
                                            size_t cStart = layerJson.find('[', chunksPos);
                                            if (cStart != std::string::npos) {
                                                int cDepth = 1;
                                                size_t cEnd = cStart + 1;
                                                while (cEnd < layerJson.size() && cDepth > 0) {
                                                    if (layerJson[cEnd] == '[') cDepth++;
                                                    else if (layerJson[cEnd] == ']') cDepth--;
                                                    cEnd++;
                                                }
                                                if (cDepth == 0) {
                                                    std::string chunksArrayStr = layerJson.substr(cStart + 1, (cEnd - 1) - cStart - 1);
                                                    size_t cSearch = 0;
                                                    while (true) {
                                                        size_t chunkOpen = chunksArrayStr.find('{', cSearch);
                                                        if (chunkOpen == std::string::npos) break;

                                                        int chDepth = 1;
                                                        size_t chunkClose = chunkOpen + 1;
                                                        while (chunkClose < chunksArrayStr.size() && chDepth > 0) {
                                                            if (chunksArrayStr[chunkClose] == '{') chDepth++;
                                                            else if (chunksArrayStr[chunkClose] == '}') chDepth--;
                                                            chunkClose++;
                                                        }

                                                        if (chDepth == 0) {
                                                            std::string chunkJson = chunksArrayStr.substr(chunkOpen, chunkClose - chunkOpen);
                                                            float cxVal = 0.f, cyVal = 0.f;
                                                            JSONUtils::extractFloatValue(chunkJson, "cx", cxVal);
                                                            JSONUtils::extractFloatValue(chunkJson, "cy", cyVal);
                                                            int cx = static_cast<int>(cxVal);
                                                            int cy = static_cast<int>(cyVal);

                                                            Engine::TileChunk chunk;
                                                            std::vector<int> chunkTiles, chunkRots;
                                                            JSONUtils::extractIntVector(chunkJson, "tiles", chunkTiles);
                                                            JSONUtils::extractIntVector(chunkJson, "rotations", chunkRots);

                                                            for (size_t i = 0; i < chunkTiles.size() && i < Engine::TILE_CHUNK_SIZE * Engine::TILE_CHUNK_SIZE; ++i) {
                                                                chunk.tiles[i] = chunkTiles[i];
                                                            }
                                                            for (size_t i = 0; i < chunkRots.size() && i < Engine::TILE_CHUNK_SIZE * Engine::TILE_CHUNK_SIZE; ++i) {
                                                                chunk.rotations[i] = static_cast<uint8_t>(chunkRots[i]);
                                                            }
                                                            if (!chunk.isEmpty()) {
                                                                layer.chunks[Engine::packChunkKey(cx, cy)] = chunk;
                                                            }
                                                        }
                                                        cSearch = chunkClose;
                                                    }
                                                }
                                            }
                                        } else {
                                            // 2. Migration fallback for legacy flat tiles array ("tiles": [...]) ONLY if "chunks" is not present
                                            std::vector<int> legacyTiles, legacyRotInts;
                                            if (JSONUtils::extractIntVector(layerJson, "tiles", legacyTiles) && !legacyTiles.empty()) {
                                                JSONUtils::extractIntVector(layerJson, "rotations", legacyRotInts);
                                                int w = (tm->width > 0) ? tm->width : 32;
                                                int h = (tm->height > 0) ? tm->height : 32;

                                                for (int y = 0; y < h; ++y) {
                                                    for (int x = 0; x < w; ++x) {
                                                        int cellIdx = y * w + x;
                                                        if (cellIdx < static_cast<int>(legacyTiles.size())) {
                                                            int tileId = legacyTiles[cellIdx];
                                                            if (tileId != -1) {
                                                                uint8_t rot = (cellIdx < static_cast<int>(legacyRotInts.size())) ? static_cast<uint8_t>(legacyRotInts[cellIdx]) : 0;
                                                                int cx = static_cast<int>(std::floor(static_cast<float>(x) / Engine::TILE_CHUNK_SIZE));
                                                                int cy = static_cast<int>(std::floor(static_cast<float>(y) / Engine::TILE_CHUNK_SIZE));
                                                                int lx = (x % Engine::TILE_CHUNK_SIZE + Engine::TILE_CHUNK_SIZE) % Engine::TILE_CHUNK_SIZE;
                                                                int ly = (y % Engine::TILE_CHUNK_SIZE + Engine::TILE_CHUNK_SIZE) % Engine::TILE_CHUNK_SIZE;
                                                                int64_t key = Engine::packChunkKey(cx, cy);

                                                                auto& chunk = layer.chunks[key];
                                                                chunk.tiles[ly * Engine::TILE_CHUNK_SIZE + lx] = tileId;
                                                                chunk.rotations[ly * Engine::TILE_CHUNK_SIZE + lx] = rot;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        tm->layers.push_back(layer);
                                    }
                                    searchPos = closeBrace;
                                }
                            }
                        }
                    }

                    if (tm->layers.empty()) {
                        Engine::TilemapLayer defaultLayer;
                        defaultLayer.name = "Ground";
                        defaultLayer.zOffset = 0.0f;
                        defaultLayer.tag = "ground";
                        defaultLayer.isVisible = true;
                        tm->layers.push_back(defaultLayer);
                    }

                    tm->isDirty = true;
                }
            }
        }
    );
    return true;
}

// Dynamically register all components from ComponentReflectionRegistry into ComponentSerializerRegistry
void syncReflectionSerializers() {
    static bool isSyncing = false;
    if (isSyncing) return;
    isSyncing = true;

    static bool initBuiltin = registerBuiltinComponents();
    (void)initBuiltin;

    auto& reg = ComponentSerializerRegistry::getInstance();
    auto& reflReg = Engine::ComponentReflectionRegistry::getInstance();
    auto currentRegs = reg.getRegistrations();

    for (const auto& refl : reflReg.getReflections()) {
        if (refl.name.empty()) continue;

        bool alreadyRegistered = false;
        for (const auto& regComp : currentRegs) {
            if (regComp.componentName == refl.name) {
                alreadyRegistered = true;
                break;
            }
        }
        if (alreadyRegistered) continue;

        reg.registerComponent(
            refl.name,
            [refl](Registry& registry, Entity entity, std::ostream& out, int indent) {
                if (refl.has(registry, entity)) {
                    void* compPtr = refl.get(registry, entity);
                    if (!compPtr) return;
                    out << JSONUtils::indent(indent) << "\"" << refl.name << "\": {\n";

                    bool firstField = true;
                    for (const auto& field : refl.fields) {
                        char* fieldPtr = static_cast<char*>(compPtr) + field.offset;
                        if (!firstField) out << ",\n";
                        firstField = false;

                        out << JSONUtils::indent(indent + 1) << "\"" << field.name << "\": ";
                        if (field.type == Engine::FieldType::Float) {
                            out << *reinterpret_cast<float*>(fieldPtr);
                        } else if (field.type == Engine::FieldType::Int) {
                            out << *reinterpret_cast<int*>(fieldPtr);
                        } else if (field.type == Engine::FieldType::Bool) {
                            out << (*reinterpret_cast<bool*>(fieldPtr) ? "1.0" : "0.0");
                        } else if (field.type == Engine::FieldType::Vec3) {
                            out << JSONUtils::vec3ToJson(*reinterpret_cast<glm::vec3*>(fieldPtr));
                        } else if (field.type == Engine::FieldType::Enum) {
                            out << *reinterpret_cast<int*>(fieldPtr);
                        } else if (field.type == Engine::FieldType::RigidBodyType) {
                            std::string typeStr = (*reinterpret_cast<RigidBodyType*>(fieldPtr) == RigidBodyType::Static) ? "Static" : "Dynamic";
                            out << JSONUtils::quote(typeStr);
                        } else if (field.type == Engine::FieldType::Entity) {
                            out << static_cast<float>(reinterpret_cast<Entity*>(fieldPtr)->getId());
                        } else if (field.type == Engine::FieldType::String) {
                            out << JSONUtils::quote(*reinterpret_cast<std::string*>(fieldPtr));
                        } else if (field.type == Engine::FieldType::Vec2) {
                            glm::vec2* v = reinterpret_cast<glm::vec2*>(fieldPtr);
                            out << "[" << v->x << ", " << v->y << "]";
                        } else if (field.type == Engine::FieldType::Vec4) {
                            out << JSONUtils::vec4ToJson(*reinterpret_cast<glm::vec4*>(fieldPtr));
                        }
                    }
                    out << "\n" << JSONUtils::indent(indent) << "}";
                }
            },
            [refl](Registry& registry, VulkanRenderer&, Entity entity, const std::string& json) {
                if (json.empty()) return;

                bool hasComp = false;
                std::string compJson;

                // 1. Check if json contains refl.name as a sub-object key
                compJson = JSONUtils::extractSubObject(json, refl.name);
                if (compJson.empty()) {
                    compJson = JSONUtils::extractSubObject(json, refl.name + "Component");
                }

                if (!compJson.empty()) {
                    hasComp = true;
                } else {
                    // 2. Check for flat / legacy entity format: entityType == refl.name or has<ReflName>: true
                    std::string entityTypeStr = JSONUtils::extractStringValue(json, "entityType");
                    if (entityTypeStr == refl.name || entityTypeStr == refl.name + "Component" ||
                        json.find("\"has" + refl.name + "\":") != std::string::npos ||
                        json.find("\"has" + refl.name + "Component\":") != std::string::npos) {
                        hasComp = true;
                        compJson = json;
                    } else if (json.find("\"name\":") == std::string::npos && json.find("\"id\":") == std::string::npos && json.find('{') != std::string::npos) {
                        // 3. Direct inner object passed without wrapper key
                        hasComp = true;
                        compJson = json;
                    }
                }

                if (hasComp) {

                    if (!refl.has(registry, entity)) {
                        refl.add(registry, entity);
                    }
                    void* compPtr = refl.get(registry, entity);
                    if (!compPtr) return;
                    for (const auto& field : refl.fields) {
                        char* fieldPtr = static_cast<char*>(compPtr) + field.offset;
                        if (field.type == Engine::FieldType::Float) {
                            JSONUtils::extractFloatValue(compJson, field.name, *reinterpret_cast<float*>(fieldPtr));
                        } else if (field.type == Engine::FieldType::Int || field.type == Engine::FieldType::Enum) {
                            float fVal = 0.0f;
                            if (JSONUtils::extractFloatValue(compJson, field.name, fVal)) {
                                *reinterpret_cast<int*>(fieldPtr) = static_cast<int>(fVal);
                            }
                        } else if (field.type == Engine::FieldType::Bool) {
                            float fVal = 0.0f;
                            if (JSONUtils::extractFloatValue(compJson, field.name, fVal)) {
                                *reinterpret_cast<bool*>(fieldPtr) = (fVal > 0.5f);
                            } else {
                                if (compJson.find("\"" + field.name + "\": true") != std::string::npos ||
                                    compJson.find("\"" + field.name + "\":true") != std::string::npos) {
                                    *reinterpret_cast<bool*>(fieldPtr) = true;
                                } else if (compJson.find("\"" + field.name + "\": false") != std::string::npos ||
                                           compJson.find("\"" + field.name + "\":false") != std::string::npos) {
                                    *reinterpret_cast<bool*>(fieldPtr) = false;
                                }
                            }
                        } else if (field.type == Engine::FieldType::Vec3) {
                            float vals[3]{};
                            if (JSONUtils::extractFloatArray(compJson, field.name, vals, 3)) {
                                *reinterpret_cast<glm::vec3*>(fieldPtr) = glm::vec3(vals[0], vals[1], vals[2]);
                            } else {
                                auto* vec = reinterpret_cast<glm::vec3*>(fieldPtr);
                                JSONUtils::extractFloatValue(compJson, field.name + "X", vec->x);
                                JSONUtils::extractFloatValue(compJson, field.name + "Y", vec->y);
                                JSONUtils::extractFloatValue(compJson, field.name + "Z", vec->z);
                            }
                        } else if (field.type == Engine::FieldType::RigidBodyType) {
                            std::string typeStr = JSONUtils::extractStringValue(compJson, field.name);
                            if (!typeStr.empty()) {
                                *reinterpret_cast<RigidBodyType*>(fieldPtr) = (typeStr == "Static") ? RigidBodyType::Static : RigidBodyType::Dynamic;
                            }
                        } else if (field.type == Engine::FieldType::Entity) {
                            float idVal = 0.0f;
                            if (JSONUtils::extractFloatValue(compJson, field.name, idVal)) {
                                *reinterpret_cast<Entity*>(fieldPtr) = Entity(static_cast<std::uint32_t>(idVal));
                            }
                        } else if (field.type == Engine::FieldType::String) {
                            if (compJson.find("\"" + field.name + "\":") != std::string::npos) {
                                *reinterpret_cast<std::string*>(fieldPtr) = JSONUtils::extractStringValue(compJson, field.name);
                            }
                        } else if (field.type == Engine::FieldType::Vec2) {
                            float vals[2]{};
                            if (JSONUtils::extractFloatArray(compJson, field.name, vals, 2)) {
                                *reinterpret_cast<glm::vec2*>(fieldPtr) = glm::vec2(vals[0], vals[1]);
                            }
                        } else if (field.type == Engine::FieldType::Vec4) {
                            float vals[4]{};
                            if (JSONUtils::extractFloatArray(compJson, field.name, vals, 4)) {
                                *reinterpret_cast<glm::vec4*>(fieldPtr) = glm::vec4(vals[0], vals[1], vals[2], vals[3]);
                            }
                        }
                    }
                }
            }
        );
    }
    isSyncing = false;
}

/**
 * @brief Construct a new Scene Serializer:: Scene Serializer object.
 * @param registry Reference to ECS registry.
 * @param renderer Reference to Vulkan renderer.
 */
SceneSerializer::SceneSerializer(Registry& registry, VulkanRenderer& renderer)
    : registry(registry), renderer(renderer) {
    // Force built-in components to be registered exactly once
    static bool init = registerBuiltinComponents();
    (void)init;
}

/**
 * @brief Serializes a list of entities to a JSON file.
 * @param path Target file path.
 * @param entities List of entities to save.
 * @return True if successful, false otherwise.
 */
bool SceneSerializer::serialize(const std::string& path, const std::vector<Entity>& entities) {
    syncReflectionSerializers();
    std::filesystem::path outputPath(path);
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "{\n";
    out << JSONUtils::indent(1) << "\"scene\": " << JSONUtils::quote("TestScene") << ",\n";
    out << JSONUtils::indent(1) << "\"entities\": [\n";

    bool first = true;
    for (Entity entity : entities) {
        Name* name = registry.get<Name>(entity);
        Transform* transform = registry.get<Transform>(entity);
        bool isUI = registry.has<Engine::RectTransform>(entity) || registry.has<Engine::CanvasComponent>(entity);

        if (!name || (!transform && !isUI)) {
            continue;
        }

        if (!first) {
            out << ",\n";
        }
        first = false;

        out << JSONUtils::indent(2) << "{\n";
        out << JSONUtils::indent(3) << "\"id\": " << entity.getId() << ",\n";
        out << JSONUtils::indent(3) << "\"name\": " << JSONUtils::quote(name->value);


        // Dynamically invoke all registered component serializers to append component sub-objects
        out << ",\n" << JSONUtils::indent(3) << "\"components\": {\n";
        bool firstComp = true;
        for (const auto& reg : ComponentSerializerRegistry::getInstance().getRegistrations()) {
            std::ostringstream compStream;
            reg.serialize(registry, entity, compStream, 5);
            std::string compStr = compStream.str();
            if (!compStr.empty()) {
                size_t startPos = compStr.find_first_not_of(" \t\r\n,");
                if (startPos != std::string::npos) {
                    compStr = compStr.substr(startPos);
                }
                if (!compStr.empty()) {
                    if (!firstComp) out << ",\n";
                    firstComp = false;

                    // If already formatted as a key object (e.g., "UIPanelComponent": { ... })
                    if (compStr.find("\"" + reg.componentName + "\":") == 0 ||
                        compStr.find("\"" + reg.componentName + "Component\":") == 0) {
                        out << compStr;
                    } else {
                        out << JSONUtils::indent(4) << "\"" << reg.componentName << "\": {\n";
                        out << compStr << "\n";
                        out << JSONUtils::indent(4) << "}";
                    }
                }
            }
        }

        out << "\n" << JSONUtils::indent(3) << "}\n";
        out << JSONUtils::indent(2) << "}";
    }

    out << "\n" << JSONUtils::indent(1) << "]\n";
    out << "}\n";
    return true;
}

/**
 * @brief Deserializes scene entities from a JSON file.
 * @param path Source file path.
 * @param outEntities Vector to store deserialized entities.
 * @return True if successful, false otherwise.
 */
/**
 * @brief Deserializes entities from a pre-loaded JSON string (no file I/O).
 *        Used by the async load pipeline: background thread pre-reads the file,
 *        main thread calls this to spawn entities and upload Vulkan resources.
 */
bool SceneSerializer::deserializeFromString(const std::string& jsonContent, std::vector<Entity>& outEntities) {
    // Write to a uniquely-named temp file so the full deserialize pipeline
    // (component reflection, texture loading, etc.) runs identically.
    std::string tempPath = "_async_scene_" + std::to_string(
        std::hash<std::string>{}(jsonContent.substr(0, 64))) + ".json";
    {
        std::ofstream tempOut(tempPath);
        if (!tempOut.is_open()) return false;
        tempOut << jsonContent;
    }
    bool ok = deserialize(tempPath, outEntities);
    try { std::filesystem::remove(tempPath); } catch (...) {}
    return ok;
}

bool SceneSerializer::deserialize(const std::string& path, std::vector<Entity>& outEntities) {
    syncReflectionSerializers();
    std::unordered_map<std::uint32_t, Entity> oldToNewEntityMap;
    std::ifstream in(path);

    if (!in.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    std::vector<std::string> entityObjects = JSONUtils::extractEntityObjects(source);
    if (entityObjects.empty()) {
        if (source.find("\"entities\"") != std::string::npos) {
            return true;
        }
        return false;
    }

    for (const std::string& entityJson : entityObjects) {
        std::string name = JSONUtils::extractStringValue(entityJson, "name");
        if (name.empty()) {
            continue;
        }

        float oldIdVal = 0.0f;
        std::uint32_t oldId = Entity::INVALID_ENTITY;
        if (JSONUtils::extractFloatValue(entityJson, "id", oldIdVal)) {
            oldId = static_cast<std::uint32_t>(oldIdVal);
        }

        Entity entity = registry.create();
        if (entity.getId() == Entity::INVALID_ENTITY) {
            continue;
        }

        if (oldId != Entity::INVALID_ENTITY) {
            oldToNewEntityMap[oldId] = entity;
        }

        // Initialize core components
        registry.emplace<Name>(entity, Name{ name });

        std::string componentsJson = JSONUtils::extractSubObject(entityJson, "components");
        std::string transformJson = JSONUtils::extractSubObject(entityJson, "transform");

        bool isUI = (entityJson.find("\"hasRectTransform\":") != std::string::npos) ||
                    (!componentsJson.empty() && (
                        componentsJson.find("\"RectTransform\":") != std::string::npos ||
                        componentsJson.find("\"Canvas\":") != std::string::npos ||
                        componentsJson.find("\"CanvasComponent\":") != std::string::npos ||
                        componentsJson.find("\"UIPanel\":") != std::string::npos ||
                        componentsJson.find("\"UIImage\":") != std::string::npos ||
                        componentsJson.find("\"UIText\":") != std::string::npos ||
                        componentsJson.find("\"UIButton\":") != std::string::npos
                    ));


        if (!isUI) {
            registry.emplace<Transform>(entity, Transform{});
            Transform* transform = registry.get<Transform>(entity);
            if (transform) {
                float position[3]{};
                float rotation[3]{};
                float scale[3]{1.0f, 1.0f, 1.0f};

                const std::string& tSource = !transformJson.empty() ? transformJson : entityJson;
                if (JSONUtils::extractFloatArray(tSource, "position", position, 3)) {
                    transform->position = glm::vec3(position[0], position[1], position[2]);
                }
                if (JSONUtils::extractFloatArray(tSource, "rotation", rotation, 3)) {
                    transform->rotation = glm::vec3(rotation[0], rotation[1], rotation[2]);
                }
                if (JSONUtils::extractFloatArray(tSource, "scale", scale, 3)) {
                    transform->scale = glm::vec3(scale[0], scale[1], scale[2]);
                }
            }
        }

        // Invoke all registered component deserializers
        std::vector<ComponentSerializerRegistry::Registration> registrations = ComponentSerializerRegistry::getInstance().getRegistrations();
        for (const auto& reg : registrations) {
            std::string targetJson;
            if (!componentsJson.empty()) {
                // Structured scene format: component dictionary inside componentsJson
                targetJson = JSONUtils::extractSubObject(componentsJson, reg.componentName);
                if (targetJson.empty()) {
                    targetJson = JSONUtils::extractSubObject(componentsJson, reg.componentName + "Component");
                }
            }
            if (targetJson.empty()) {
                targetJson = JSONUtils::extractSubObject(entityJson, reg.componentName);
                if (targetJson.empty()) {
                    targetJson = JSONUtils::extractSubObject(entityJson, reg.componentName + "Component");
                }
            }
            if (targetJson.empty()) {
                targetJson = entityJson;
            }

            if (!targetJson.empty()) {
                reg.deserialize(registry, renderer, entity, targetJson);
            }
        }


        outEntities.push_back(entity);
    }


    // Resolve parent entity links in HierarchyComponent using temporary ParentNameComponent records
    for (auto [entity, parentNameComp] : registry.view<ParentNameComponent>()) {
        Entity parentEntity;
        for (auto [parentCandidate, nameComp] : registry.view<Name>()) {
            if (nameComp.value == parentNameComp.name) {
                parentEntity = parentCandidate;
                break;
            }
        }
        if (parentEntity.getId() != Entity::INVALID_ENTITY) {
            registry.emplace<HierarchyComponent>(entity, HierarchyComponent{ parentEntity });
        }
    }
    
    // Clean up temporary ParentNameComponent storage
    std::vector<Entity> toClean;
    for (auto [entity, parentNameComp] : registry.view<ParentNameComponent>()) {
        toClean.push_back(entity);
    }
    for (Entity e : toClean) {
        registry.remove<ParentNameComponent>(e);
    }

    // Resolve all reflected Entity fields using the oldToNewEntityMap
    auto& reflReg = Engine::ComponentReflectionRegistry::getInstance();
    for (Entity entity : outEntities) {
        for (const auto& refl : reflReg.getReflections()) {
            if (refl.has(registry, entity)) {
                void* compPtr = refl.get(registry, entity);
                if (!compPtr) continue;

                for (const auto& field : refl.fields) {
                    if (field.type == Engine::FieldType::Entity) {
                        Entity* refEntity = reinterpret_cast<Entity*>(static_cast<char*>(compPtr) + field.offset);
                        if (refEntity->getId() != Entity::INVALID_ENTITY) {
                            auto it = oldToNewEntityMap.find(refEntity->getId());
                            if (it != oldToNewEntityMap.end()) {
                                *refEntity = it->second;
                            } else {
                                // If the referenced entity wasn't in this scene, reset it to invalid
                                *refEntity = Entity();
                            }
                        }
                    }
                }
            }
        }
    }

    return !outEntities.empty();
}

bool SceneSerializer::serializePrefab(const std::string& path, Entity rootEntity) {
    syncReflectionSerializers();
    if (rootEntity.getId() == Entity::INVALID_ENTITY || !registry.isValid(rootEntity)) {
        return false;
    }

    std::filesystem::path outputPath(path);
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    // 1. Traverse and gather all entities in the hierarchy recursively
    std::vector<Entity> entities;
    entities.push_back(rootEntity);
    size_t cursor = 0;
    while (cursor < entities.size()) {
        Entity current = entities[cursor];
        for (auto [childEntity, hierarchy] : registry.view<HierarchyComponent>()) {
            if (hierarchy.parent == current) {
                if (std::find(entities.begin(), entities.end(), childEntity) == entities.end()) {
                    entities.push_back(childEntity);
                }
            }
        }
        cursor++;
    }

    out << "{\n";
    out << JSONUtils::indent(1) << "\"prefabType\": " << JSONUtils::quote("EntityPrefab") << ",\n";
    out << JSONUtils::indent(1) << "\"entities\": [\n";

    bool first = true;
    for (Entity entity : entities) {
        Name* name = registry.get<Name>(entity);
        Transform* transform = registry.get<Transform>(entity);
        bool isUI = registry.has<Engine::RectTransform>(entity);
        if (!name || (!transform && !isUI)) {
            continue;
        }

        if (!first) {
            out << ",\n";
        }
        first = false;

        out << JSONUtils::indent(2) << "{\n";
        out << JSONUtils::indent(3) << "\"id\": " << entity.getId() << ",\n";
        
        // Find if this entity has a parent in the prefab list
        auto* hierarchy = registry.get<HierarchyComponent>(entity);
        if (hierarchy && hierarchy->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy->parent)) {
            if (std::find(entities.begin(), entities.end(), hierarchy->parent) != entities.end()) {
                out << JSONUtils::indent(3) << "\"parentId\": " << hierarchy->parent.getId() << ",\n";
            }
        }

        out << JSONUtils::indent(3) << "\"name\": " << JSONUtils::quote(name->value);
        if (transform) {
            out << ",\n" << JSONUtils::indent(3) << "\"position\": " << JSONUtils::vec3ToJson(transform->position) << ",\n";
            out << JSONUtils::indent(3) << "\"rotation\": " << JSONUtils::vec3ToJson(transform->rotation) << ",\n";
            out << JSONUtils::indent(3) << "\"scale\": " << JSONUtils::vec3ToJson(transform->scale);
        }

        // Dynamically invoke all registered component serializers to append custom properties
        for (const auto& reg : ComponentSerializerRegistry::getInstance().getRegistrations()) {
            if (reg.componentName == "Hierarchy") {
                continue; // Skip the default Hierarchy component serialization to avoid parentName collisions
            }
            reg.serialize(registry, entity, out, 3);
        }

        out << "\n" << JSONUtils::indent(2) << "}";
    }

    out << "\n" << JSONUtils::indent(1) << "]\n";
    out << "}\n";
    return true;
}

Entity SceneSerializer::deserializePrefab(const std::string& path, std::vector<Entity>& loadedEntities, Entity parentEntity) {
    syncReflectionSerializers();
    std::ifstream in(path);
    if (!in.is_open()) {
        return Entity();
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    std::vector<std::string> entityObjects = JSONUtils::extractEntityObjects(source);
    if (entityObjects.empty()) {
        return Entity();
    }

    // Temporary mappings to reconstruct the hierarchy relative to newly spawned entities
    std::unordered_map<int, Entity> originalIdToNewEntity;
    std::unordered_map<Entity, int> newEntityToParentId;
    Entity rootNewEntity;

    for (const std::string& entityJson : entityObjects) {
        std::string name = JSONUtils::extractStringValue(entityJson, "name");
        if (name.empty()) {
            continue;
        }

        // Extract the original serialized ID
        int originalId = -1;
        size_t idPos = entityJson.find("\"id\"");
        if (idPos != std::string::npos) {
            size_t colonPos = entityJson.find(":", idPos);
            if (colonPos != std::string::npos) {
                std::string valStr;
                for (size_t i = colonPos + 1; i < entityJson.size(); ++i) {
                    char c = entityJson[i];
                    if (std::isdigit(c) || c == '-') {
                        valStr += c;
                    } else if (!valStr.empty()) {
                        break;
                    }
                }
                if (!valStr.empty()) {
                    originalId = std::stoi(valStr);
                }
            }
        }

        // Extract the parent ID if present
        int parentId = -1;
        size_t parentIdPos = entityJson.find("\"parentId\"");
        if (parentIdPos != std::string::npos) {
            size_t colonPos = entityJson.find(":", parentIdPos);
            if (colonPos != std::string::npos) {
                std::string valStr;
                for (size_t i = colonPos + 1; i < entityJson.size(); ++i) {
                    char c = entityJson[i];
                    if (std::isdigit(c) || c == '-') {
                        valStr += c;
                    } else if (!valStr.empty()) {
                        break;
                    }
                }
                if (!valStr.empty()) {
                    parentId = std::stoi(valStr);
                }
            }
        }

        Entity entity = registry.create();
        if (entity.getId() == Entity::INVALID_ENTITY) {
            continue;
        }

        // Track the root node (first entity created in the prefab that has no parent ID, or parentId is not in the list)
        if (rootNewEntity.getId() == Entity::INVALID_ENTITY && parentId == -1) {
            rootNewEntity = entity;
        }

        // Store mappings
        if (originalId != -1) {
            originalIdToNewEntity[originalId] = entity;
        }
        if (parentId != -1) {
            newEntityToParentId[entity] = parentId;
        }

        // Initialize core components
        registry.emplace<Name>(entity, Name{ name });
        registry.emplace<Transform>(entity, Transform{});
        
        Transform* transform = registry.get<Transform>(entity);
        if (transform) {
            float position[3]{};
            float rotation[3]{};
            float scale[3]{1.0f, 1.0f, 1.0f};

            if (JSONUtils::extractFloatArray(entityJson, "position", position, 3)) {
                transform->position = glm::vec3(position[0], position[1], position[2]);
            }
            if (JSONUtils::extractFloatArray(entityJson, "rotation", rotation, 3)) {
                transform->rotation = glm::vec3(rotation[0], rotation[1], rotation[2]);
            }
            if (JSONUtils::extractFloatArray(entityJson, "scale", scale, 3)) {
                transform->scale = glm::vec3(scale[0], scale[1], scale[2]);
            }
        }

        // Invoke other registered component deserializers
        for (const auto& reg : ComponentSerializerRegistry::getInstance().getRegistrations()) {
            if (reg.componentName == "Hierarchy") {
                continue; // Skip Hierarchy resolving here, we do it in a custom step below
            }
            reg.deserialize(registry, renderer, entity, entityJson);
        }

        loadedEntities.push_back(entity);
    }

    // Fallback: If no entity was identified as root, pick the first one
    if (rootNewEntity.getId() == Entity::INVALID_ENTITY && !loadedEntities.empty()) {
        rootNewEntity = loadedEntities[0];
    }

    // 2. Resolve hierarchical parent links using our ID mappings
    for (auto& [entity, parentId] : newEntityToParentId) {
        auto it = originalIdToNewEntity.find(parentId);
        if (it != originalIdToNewEntity.end()) {
            registry.emplace<HierarchyComponent>(entity, HierarchyComponent{ it->second });
        }
    }

    // 3. If a parentEntity was provided, attach the root of the prefab under it
    if (parentEntity.getId() != Entity::INVALID_ENTITY && registry.isValid(parentEntity)) {
        if (rootNewEntity.getId() != Entity::INVALID_ENTITY) {
            registry.emplace<HierarchyComponent>(rootNewEntity, HierarchyComponent{ parentEntity });
        }
    }

    return rootNewEntity;
}
