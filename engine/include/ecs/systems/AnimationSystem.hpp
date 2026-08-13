#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "ecs/components/Skeleton.hpp"
#include "ecs/components/Animator.hpp"
#include "ecs/components/AnimationController.hpp"
#include "ecs/components/IKSolver.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/SpriteRenderer.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "renderer/ResourceManager.hpp"
#include "core/JobSystem.hpp"
#include "core/VulkanBuffer.hpp"
#include "editor/EditorModeState.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

/**
 * @class AnimationSystem
 * @brief System that processes active skeletal animations, interpolating poses and resolving joint trees.
 */
class AnimationSystem : public System {
public:
    AnimationSystem(Registry& reg, VulkanRenderer& renderer, EditorModeState& editorMode)
        : registry(reg), renderer(renderer), editorMode(editorMode) {}

        /**
         * @brief Updates skeletal poses for all animated entities.
         * @param dt Delta time in seconds.
         */
        struct JointPose {
            glm::vec3 translation;
            glm::quat rotation;
            glm::vec3 scale;
        };

        /**
         * @brief Updates skeletal poses for all animated entities.
         * @param dt Delta time in seconds.
         */
        void update(float dt) override {
            // 0. Synchronize child animators & controllers with parent animators
            for (auto [entity, hierarchy, animator] : registry.view<HierarchyComponent, AnimatorComponent>()) {
                if (hierarchy.parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy.parent)) {
                    // Skip synchronization if child has its own controller or independent animations
                    if (registry.has<AnimationControllerComponent>(entity) || !animator.animations.empty()) {
                        continue;
                    }
                    if (auto* parentAnimator = registry.get<AnimatorComponent>(hierarchy.parent)) {
                        animator.activeAnimationIndex = parentAnimator->activeAnimationIndex;
                        animator.currentTime = parentAnimator->currentTime;
                        animator.playbackSpeed = parentAnimator->playbackSpeed;
                        animator.loop = parentAnimator->loop;
                        animator.isPreviewing = parentAnimator->isPreviewing;
                    }
                }
            }

            for (auto [entity, hierarchy, controller] : registry.view<HierarchyComponent, AnimationControllerComponent>()) {
                if (hierarchy.parent.getId() != Entity::INVALID_ENTITY && registry.isValid(hierarchy.parent)) {
                    // Skip synchronization if child has its own independent animations
                    if (auto* animator = registry.get<AnimatorComponent>(entity)) {
                        if (!animator->animations.empty()) {
                            continue;
                        }
                    }
                    if (auto* parentController = registry.get<AnimationControllerComponent>(hierarchy.parent)) {
                        controller.currentState = parentController->currentState;
                        controller.currentStateTime = parentController->currentStateTime;
                        controller.fromState = parentController->fromState;
                        controller.fromStateTime = parentController->fromStateTime;
                        controller.crossfadeProgress = parentController->crossfadeProgress;
                        controller.crossfadeDuration = parentController->crossfadeDuration;
                        controller.isCrossfading = parentController->isCrossfading;
                        controller.parameters = parentController->parameters;
                    }
                }
            }

            // 1. First Pass: Update all active Animation Controllers in parallel
            controllerEntities.clear();
            for (auto [entity, controller, animator] : registry.view<AnimationControllerComponent, AnimatorComponent>()) {
                controllerEntities.push_back(entity);
            }

            Engine::JobSystem::getInstance().parallelFor(static_cast<int>(controllerEntities.size()), [&](int idx) {
                Entity entity = controllerEntities[idx];
                auto* controller = registry.get<AnimationControllerComponent>(entity);
                auto* animator = registry.get<AnimatorComponent>(entity);
                if (controller && animator) {
                    float entityDt = (editorMode.isPlaying || animator->isPreviewing || animator->playbackSpeed > 0.0f) ? dt : 0.0f;
                    updateController(entity, *controller, *animator, entityDt);
                }
            });

            // 2. Second Pass: Process skeletal transforms, blending, and IK adjustments in parallel
            animatedEntities.clear();
            for (auto [entity, skeleton, animator] : registry.view<SkeletonComponent, AnimatorComponent>()) {
                if (!skeleton.joints.empty()) {
                    animatedEntities.push_back(entity);
                }
            }

            Engine::JobSystem::getInstance().parallelFor(static_cast<int>(animatedEntities.size()), [&](int idx) {
                Entity entity = animatedEntities[idx];
                auto* skeleton = registry.get<SkeletonComponent>(entity);
                auto* animator = registry.get<AnimatorComponent>(entity);
                if (skeleton && animator) {
                    float entityDt = (editorMode.isPlaying || animator->isPreviewing || animator->playbackSpeed > 0.0f) ? dt : 0.0f;
                    updateEntityAnimation(entity, *skeleton, *animator, entityDt);
                }
            });

            // 3. Third Pass: Process generic property-only animations for all entities (Main Thread)
            for (auto [entity, animator] : registry.view<AnimatorComponent>()) {
                float entityDt = (editorMode.isPlaying || animator.isPreviewing || animator.playbackSpeed > 0.0f) ? dt : 0.0f;
                updateGenericAnimation(entity, animator, entityDt);
            }
        }

    private:
        /**
         * @brief Updates high-level state machine progression and checks transitions.
         */
        void updateController(Entity entity, AnimationControllerComponent& controller, AnimatorComponent& animator, float dt) {
            auto isPseudoNode = [](const std::string& name) {
                std::string lower = name;
                for (char& c : lower) c = (char)std::tolower((unsigned char)c);
                return (lower == "entry" || lower == "__entry__" || lower == "start" || lower == "any state" || lower == "anystate");
            };

            auto getParamVal = [&](const std::string& name, float defaultVal = 0.0f) -> float {
                if (controller.parameters.empty()) return defaultVal;
                if (!name.empty() && name != "(select param)") {
                    auto it = controller.parameters.find(name);
                    if (it != controller.parameters.end()) return it->second;

                    // Case-insensitive and space-stripping match
                    std::string cleanName = name;
                    cleanName.erase(std::remove_if(cleanName.begin(), cleanName.end(), ::isspace), cleanName.end());
                    for (char& c : cleanName) c = (char)std::tolower((unsigned char)c);

                    for (const auto& [pKey, pVal] : controller.parameters) {
                        std::string cleanKey = pKey;
                        cleanKey.erase(std::remove_if(cleanKey.begin(), cleanKey.end(), ::isspace), cleanKey.end());
                        for (char& c : cleanKey) c = (char)std::tolower((unsigned char)c);
                        if (cleanKey == cleanName) return pVal;
                    }
                }
                return defaultVal;
            };

            auto loadClipOnDemand = [&](const std::string& clipName) {
                if (clipName.empty()) return;
                for (const auto& cl : animator.animations) {
                    if (cl.name == clipName || std::filesystem::path(cl.name).stem().string() == clipName) return;
                }

                std::string foundPath = "";
                std::vector<std::string> searchDirs = { "", "assets/", "assets/animations/", "assets/sprites/", "assets/textures/", "sandbox_game/assets/", "sandbox_game/assets/animations/", "sandbox_game/assets/sprites/" };
                std::vector<std::string> searchExts = { "", ".anim", ".fbx", ".gltf", ".glb", ".png", ".jpg" };

                for (const auto& dir : searchDirs) {
                    for (const auto& ext : searchExts) {
                        std::string cand = dir + clipName + ext;
                        if (std::filesystem::exists(cand)) {
                            foundPath = cand;
                            break;
                        }
                    }
                    if (!foundPath.empty()) break;
                }

                if (foundPath.empty()) {
                    std::vector<std::string> rootDirs = { "assets", "sandbox_game/assets" };
                    std::string targetStem = std::filesystem::path(clipName).stem().string();
                    for (char& c : targetStem) c = (char)std::tolower((unsigned char)c);

                    for (const auto& rDir : rootDirs) {
                        try {
                            if (std::filesystem::exists(rDir)) {
                                for (const auto& entry : std::filesystem::recursive_directory_iterator(rDir)) {
                                    if (entry.is_regular_file()) {
                                        std::string entryStem = entry.path().stem().string();
                                        for (char& c : entryStem) c = (char)std::tolower((unsigned char)c);

                                        if (entryStem == targetStem) {
                                            foundPath = entry.path().generic_string();
                                            break;
                                        }
                                    }
                                }
                            }
                        } catch (...) {}
                        if (!foundPath.empty()) break;
                    }
                }

                if (!foundPath.empty()) {
                    std::string ext = std::filesystem::path(foundPath).extension().string();
                    for (char& c : ext) c = (char)std::tolower((unsigned char)c);

                    if (ext == ".anim" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
                        SkeletonComponent* skel = registry.get<SkeletonComponent>(entity);
                        SkeletonComponent dummySkel;
                        renderer.resourceManager->loadBinarySkeletonAndAnimations(foundPath, skel ? *skel : dummySkel, animator, true);
                    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                        AnimationClip texClip;
                        texClip.name = clipName;
                        texClip.duration = 1.0f;
                        PropertyChannel pChan;
                        pChan.componentName = "SpriteRenderer";
                        pChan.fieldName = "texturePath";
                        PropertyKeyframe pKey;
                        pKey.time = 0.0f;
                        pKey.stringValue = foundPath;
                        pChan.keys.push_back(pKey);
                        texClip.propertyChannels.push_back(pChan);
                        animator.animations.push_back(texClip);
                    }
                }
            };

            // Pre-load all motion clips for all states in controller
            for (const auto& st : controller.states) {
                if (st.isBlendTree) {
                    for (const auto& bn : st.blendTree.nodes) {
                        loadClipOnDemand(bn.clipName);
                    }
                } else {
                    loadClipOnDemand(st.clipName);
                }
            }

            if ((controller.currentState.empty() || isPseudoNode(controller.currentState)) && !controller.states.empty()) {
                // First check if there is an outgoing transition from Entry/Start
                std::string targetStateName = "";
                for (const auto& trans : controller.transitions) {
                    if (isPseudoNode(trans.fromState) && !trans.toState.empty() && !isPseudoNode(trans.toState)) {
                        targetStateName = trans.toState;
                        break;
                    }
                }
                // Fallback: pick first real state that is not a pseudo node
                if (targetStateName.empty()) {
                    for (const auto& st : controller.states) {
                        if (!isPseudoNode(st.name)) {
                            targetStateName = st.name;
                            break;
                        }
                    }
                }
                if (targetStateName.empty() && !controller.states.empty()) {
                    targetStateName = controller.states[0].name;
                }
                controller.currentState = targetStateName;
                controller.currentStateTime = 0.0f;
            }

            if (controller.currentState.empty()) return;

            // Advance play time for current state
            const AnimationState* currState = nullptr;
            for (const auto& state : controller.states) {
                if (state.name == controller.currentState) {
                    currState = &state;
                    break;
                }
            }
            if (!currState && !controller.currentState.empty()) {
                std::string cleanCurr = controller.currentState;
                cleanCurr.erase(std::remove_if(cleanCurr.begin(), cleanCurr.end(), ::isspace), cleanCurr.end());
                for (char& c : cleanCurr) c = (char)std::tolower((unsigned char)c);
                for (const auto& state : controller.states) {
                    std::string cleanSt = state.name;
                    cleanSt.erase(std::remove_if(cleanSt.begin(), cleanSt.end(), ::isspace), cleanSt.end());
                    for (char& c : cleanSt) c = (char)std::tolower((unsigned char)c);
                    if (cleanSt == cleanCurr) {
                        currState = &state;
                        break;
                    }
                }
            }
            if (!currState && !controller.states.empty()) {
                for (const auto& state : controller.states) {
                    if (!isPseudoNode(state.name)) {
                        currState = &state;
                        controller.currentState = state.name;
                        break;
                    }
                }
            }

            if (currState) {
                controller.currentStateTime += dt * currState->speed;

                static float s_debugTimer = 0.0f;
                s_debugTimer += dt;
                bool doLog = false;
                if (s_debugTimer >= 1.0f) {
                    s_debugTimer = 0.0f;
                    doLog = true;
                }

                if (doLog) {
                    std::cout << "[AnimDebug] Entity state: '" << controller.currentState 
                              << "' | stateTime: " << controller.currentStateTime 
                              << " | isBlendTree: " << (currState->isBlendTree ? "YES" : "NO")
                              << " | dt: " << dt 
                              << " | isPlaying: " << (editorMode.isPlaying ? 1 : 0)
                              << " | isPreviewing: " << (animator.isPreviewing ? 1 : 0)
                              << std::endl;
                }

                // Sync active clip & time in AnimatorComponent from current state
                if (!currState->isBlendTree) {
                    int clipIdx = -1;
                    if (!currState->clipName.empty()) {
                        for (size_t i = 0; i < animator.animations.size(); ++i) {
                            if (animator.animations[i].name == currState->clipName ||
                                std::filesystem::path(animator.animations[i].name).stem().string() == currState->clipName) {
                                clipIdx = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                    if (clipIdx == -1 && !animator.animations.empty()) {
                        clipIdx = 0;
                    }
                    if (clipIdx != -1) {
                        animator.activeAnimationIndex = clipIdx;
                        auto& clip = animator.animations[clipIdx];
                        
                        float effectiveDur = clip.duration;
                        for (const auto& chan : clip.propertyChannels) {
                            if (!chan.keys.empty()) {
                                effectiveDur = std::max(effectiveDur, chan.keys.back().time);
                            }
                        }
                        if (effectiveDur <= 0.0f) effectiveDur = 1.0f;
                        clip.duration = effectiveDur;

                        if (currState->isLooping) {
                            animator.currentTime = std::fmod(controller.currentStateTime, effectiveDur);
                        } else {
                            animator.currentTime = std::min(controller.currentStateTime, effectiveDur);
                        }
                        animator.loop = currState->isLooping;
                    }
                } else {
                    // For Blend Trees: determine dominant motion clip for property-based / 2D sprite animators
                    BlendTree treeCopy = currState->blendTree;
                    if (treeCopy.nodes.empty()) {
                        if (doLog) {
                            std::cout << "[BlendTreeDebug] WARNING: BlendTree state '" << currState->name 
                                      << "' has 0 nodes in blendTree.nodes! Auto-populating from animator.animations (" << animator.animations.size() << " clips)..." << std::endl;
                        }
                        for (const auto& clip : animator.animations) {
                            if (!clip.name.empty()) {
                                BlendNode bn;
                                bn.clipName = clip.name;
                                treeCopy.nodes.push_back(bn);
                            }
                        }
                    }
                    const auto& tree = treeCopy;
                    if (!tree.nodes.empty()) {
                        std::string dominantClipName;

                        std::string paramXName = tree.parameterName;
                        std::string paramYName = tree.parameterYName;

                        // Smart fallback for parameter names if unassigned
                        if (paramXName.empty() || paramXName == "(select param)") {
                            for (const auto& [pk, pv] : controller.parameters) {
                                std::string lk = pk; for (char& c : lk) c = (char)std::tolower((unsigned char)c);
                                if (lk.find("x") != std::string::npos || lk.find("horiz") != std::string::npos) {
                                    paramXName = pk; break;
                                }
                            }
                            if ((paramXName.empty() || paramXName == "(select param)") && !controller.parameters.empty()) {
                                paramXName = controller.parameters.begin()->first;
                            }
                        }

                        if (paramYName.empty() || paramYName == "(select param)") {
                            for (const auto& [pk, pv] : controller.parameters) {
                                std::string lk = pk; for (char& c : lk) c = (char)std::tolower((unsigned char)c);
                                if (lk.find("y") != std::string::npos || lk.find("vert") != std::string::npos) {
                                    paramYName = pk; break;
                                }
                            }
                            if ((paramYName.empty() || paramYName == "(select param)") && controller.parameters.size() >= 2) {
                                auto it = controller.parameters.begin();
                                std::advance(it, 1);
                                paramYName = it->first;
                            }
                        }

                        size_t bestIdx = 0;

                        if (tree.is2D) {
                            float px = getParamVal(paramXName, 0.0f);
                            float py = getParamVal(paramYName, 0.0f);
                            glm::vec2 p(px, py);

                            // Check if all node thresholds are zero
                            bool allZero = true;
                            for (const auto& bn : tree.nodes) {
                                if (glm::length(bn.threshold2D) > 0.001f) {
                                    allZero = false;
                                    break;
                                }
                            }

                            float minDistSq = 1e30f;
                            for (size_t i = 0; i < tree.nodes.size(); ++i) {
                                glm::vec2 thresh = tree.nodes[i].threshold2D;
                                std::string cLower = tree.nodes[i].clipName;
                                for (char& c : cLower) c = (char)std::tolower((unsigned char)c);

                                if (glm::length(thresh) <= 0.001f) {
                                    if (cLower.find("down") != std::string::npos || cLower.find("south") != std::string::npos) {
                                        thresh = glm::vec2(0.0f, -1.0f);
                                    } else if (cLower.find("up") != std::string::npos || cLower.find("north") != std::string::npos) {
                                        thresh = glm::vec2(0.0f, 1.0f);
                                    } else if (cLower.find("left") != std::string::npos || cLower.find("west") != std::string::npos) {
                                        thresh = glm::vec2(-1.0f, 0.0f);
                                    } else if (cLower.find("right") != std::string::npos || cLower.find("east") != std::string::npos) {
                                        thresh = glm::vec2(1.0f, 0.0f);
                                    } else if (cLower.find("idle") != std::string::npos || cLower.find("stand") != std::string::npos) {
                                        thresh = glm::vec2(0.0f, 0.0f);
                                    } else if (allZero && tree.nodes.size() == 5) {
                                        // Standard 5-point 2D blend tree fallback: Idle, Down, Up, Left, Right
                                        static const glm::vec2 defaults[5] = {
                                            glm::vec2(0.0f, 0.0f),  // Node 0: Idle
                                            glm::vec2(0.0f, -1.0f), // Node 1: Down
                                            glm::vec2(0.0f, 1.0f),  // Node 2: Up
                                            glm::vec2(-1.0f, 0.0f), // Node 3: Left
                                            glm::vec2(1.0f, 0.0f)   // Node 4: Right
                                        };
                                        if (i < 5) thresh = defaults[i];
                                    }
                                }

                                glm::vec2 diff = p - thresh;
                                float dSq = glm::dot(diff, diff);
                                if (dSq < minDistSq) {
                                    minDistSq = dSq;
                                    bestIdx = i;
                                }
                            }
                            dominantClipName = tree.nodes[bestIdx].clipName;
                        } else {
                            float pVal = getParamVal(paramXName, 0.0f);
                            float minDiff = 1e30f;
                            for (size_t i = 0; i < tree.nodes.size(); ++i) {
                                float diff = std::abs(pVal - tree.nodes[i].threshold);
                                if (diff < minDiff) {
                                    minDiff = diff;
                                    bestIdx = i;
                                }
                            }
                            dominantClipName = tree.nodes[bestIdx].clipName;
                        }

                        if (dominantClipName.empty() && !animator.animations.empty()) {
                            dominantClipName = animator.animations[bestIdx % animator.animations.size()].name;
                        }

                        if (doLog) {
                            std::string loadedList = "";
                            for (size_t i = 0; i < animator.animations.size(); ++i) {
                                if (i > 0) loadedList += ", ";
                                loadedList += "'" + animator.animations[i].name + "'";
                            }
                            std::cout << "[BlendTreeDebug] state: '" << currState->name
                                      << "' | is2D: " << (tree.is2D ? "YES" : "NO")
                                      << " | paramX: '" << paramXName << "' (" << getParamVal(paramXName, 0.0f) << ")"
                                      << " | paramY: '" << paramYName << "' (" << getParamVal(paramYName, 0.0f) << ")"
                                      << " | bestIdx: " << bestIdx
                                      << " | dominantClip: '" << dominantClipName << "'"
                                      << " | loadedClips: [" << loadedList << "]"
                                      << std::endl;
                        }

                        if (!dominantClipName.empty()) {
                            int clipIdx = -1;
                            for (size_t i = 0; i < animator.animations.size(); ++i) {
                                std::string aStem = std::filesystem::path(animator.animations[i].name).stem().string();
                                std::string dStem = std::filesystem::path(dominantClipName).stem().string();
                                for (char& c : aStem) c = (char)std::tolower((unsigned char)c);
                                for (char& c : dStem) c = (char)std::tolower((unsigned char)c);

                                if (animator.animations[i].name == dominantClipName || aStem == dStem ||
                                    aStem.find(dStem) != std::string::npos || dStem.find(aStem) != std::string::npos) {
                                    clipIdx = static_cast<int>(i);
                                    break;
                                }
                            }
                            if (clipIdx == -1 && !dominantClipName.empty()) {
                                std::string foundPath = "";
                                std::vector<std::string> cands = {
                                    dominantClipName, dominantClipName + ".anim",
                                    "assets/" + dominantClipName, "assets/" + dominantClipName + ".anim",
                                    "assets/animations/" + dominantClipName, "assets/animations/" + dominantClipName + ".anim",
                                    "assets/sprites/" + dominantClipName, "assets/sprites/" + dominantClipName + ".anim",
                                    "assets/textures/" + dominantClipName, "assets/textures/" + dominantClipName + ".anim"
                                };
                                for (const auto& cand : cands) {
                                    if (std::filesystem::exists(cand)) {
                                        foundPath = cand;
                                        break;
                                    }
                                }

                                if (foundPath.empty()) {
                                    try {
                                        if (std::filesystem::exists("assets")) {
                                            std::string targetStem = std::filesystem::path(dominantClipName).stem().string();
                                            for (char& c : targetStem) c = (char)std::tolower((unsigned char)c);

                                            for (const auto& entry : std::filesystem::recursive_directory_iterator("assets")) {
                                                if (entry.is_regular_file()) {
                                                    std::string entryStem = entry.path().stem().string();
                                                    for (char& c : entryStem) c = (char)std::tolower((unsigned char)c);

                                                    if (entryStem == targetStem) {
                                                        foundPath = entry.path().generic_string();
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    } catch (...) {}
                                }

                                if (!foundPath.empty()) {
                                    std::string ext = std::filesystem::path(foundPath).extension().string();
                                    for (char& c : ext) c = (char)std::tolower((unsigned char)c);

                                    if (ext == ".anim" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
                                        SkeletonComponent* skel = registry.get<SkeletonComponent>(entity);
                                        SkeletonComponent dummySkel;
                                        renderer.resourceManager->loadBinarySkeletonAndAnimations(foundPath, skel ? *skel : dummySkel, animator, true);
                                    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                                        AnimationClip texClip;
                                        texClip.name = dominantClipName;
                                        texClip.duration = 1.0f;
                                        PropertyChannel pChan;
                                        pChan.componentName = "SpriteRenderer";
                                        pChan.fieldName = "texturePath";
                                        PropertyKeyframe pKey;
                                        pKey.time = 0.0f;
                                        pKey.stringValue = foundPath;
                                        pChan.keys.push_back(pKey);
                                        texClip.propertyChannels.push_back(pChan);
                                        animator.animations.push_back(texClip);
                                    }
                                }

                                for (size_t i = 0; i < animator.animations.size(); ++i) {
                                    std::string aStem = std::filesystem::path(animator.animations[i].name).stem().string();
                                    std::string dStem = std::filesystem::path(dominantClipName).stem().string();
                                    for (char& c : aStem) c = (char)std::tolower((unsigned char)c);
                                    for (char& c : dStem) c = (char)std::tolower((unsigned char)c);

                                    if (animator.animations[i].name == dominantClipName || aStem == dStem ||
                                        aStem.find(dStem) != std::string::npos || dStem.find(aStem) != std::string::npos) {
                                        clipIdx = static_cast<int>(i);
                                        break;
                                    }
                                }
                            }
                            if (clipIdx == -1 && !animator.animations.empty()) {
                                clipIdx = 0;
                            }
                            if (clipIdx != -1) {
                                animator.activeAnimationIndex = clipIdx;
                                auto& clip = animator.animations[clipIdx];
                                float effectiveDur = clip.duration;
                                for (const auto& chan : clip.propertyChannels) {
                                    if (!chan.keys.empty()) {
                                        effectiveDur = std::max(effectiveDur, chan.keys.back().time);
                                    }
                                }
                                for (const auto& chan : clip.channels) {
                                    if (!chan.translationKeys.empty()) effectiveDur = std::max(effectiveDur, chan.translationKeys.back().time);
                                    if (!chan.rotationKeys.empty()) effectiveDur = std::max(effectiveDur, chan.rotationKeys.back().time);
                                    if (!chan.scaleKeys.empty()) effectiveDur = std::max(effectiveDur, chan.scaleKeys.back().time);
                                }
                                if (effectiveDur <= 0.0f) effectiveDur = 1.0f;
                                clip.duration = effectiveDur;

                                if (currState->isLooping) {
                                    animator.currentTime = std::fmod(controller.currentStateTime, effectiveDur);
                                } else {
                                    animator.currentTime = std::min(controller.currentStateTime, effectiveDur);
                                }
                                if (doLog) {
                                    std::cout << "[BlendTreeDebug] is2D: " << (tree.is2D ? "YES" : "NO")
                                              << " | paramX: '" << tree.parameterName << "'"
                                              << " | paramY: '" << tree.parameterYName << "'"
                                              << " | dominantClip: '" << dominantClipName << "'"
                                              << " | clipIdx: " << clipIdx
                                              << " | animTime: " << animator.currentTime
                                              << " | loadedAnims: " << animator.animations.size()
                                              << std::endl;
                                }
                            }
                        }
                    }
                }
            }

            // Advance crossfade timing if active
            if (controller.isCrossfading) {
                const AnimationState* fState = nullptr;
                for (const auto& state : controller.states) {
                    if (state.name == controller.fromState) {
                        fState = &state;
                        break;
                    }
                }
                if (fState) {
                    controller.fromStateTime += dt * fState->speed;
                }
                
                controller.crossfadeProgress += dt;
                if (controller.crossfadeProgress >= controller.crossfadeDuration) {
                    controller.isCrossfading = false;
                    controller.fromState.clear();
                }
            }

            // Check transition rules (only if not currently crossfading)
            if (!controller.isCrossfading) {
                for (const auto& trans : controller.transitions) {
                    bool isAnyStateTrans = (trans.fromState == "Any State" || trans.fromState == "any state" || trans.fromState == "AnyState");
                    if (isAnyStateTrans && trans.conditions.empty()) continue;

                    if ((trans.fromState == controller.currentState || isAnyStateTrans) && trans.toState != controller.currentState) {
                        bool allConditionsMet = true;
                        for (const auto& cond : trans.conditions) {
                            float paramVal = getParamVal(cond.parameterName, 0.0f);

                            if (cond.op == ">") {
                                if (!(paramVal > cond.value)) allConditionsMet = false;
                            } else if (cond.op == "<") {
                                if (!(paramVal < cond.value)) allConditionsMet = false;
                            } else if (cond.op == "==") {
                                if (!(std::abs(paramVal - cond.value) < 1e-4f)) allConditionsMet = false;
                            } else {
                                allConditionsMet = false;
                            }
                        }

                        bool shouldTransition = false;
                        if (!trans.conditions.empty()) {
                            shouldTransition = allConditionsMet;
                        } else {
                            float clipDur = 0.5f;
                            if (currState) {
                                for (const auto& c : animator.animations) {
                                    if (c.name == currState->clipName) {
                                        clipDur = c.duration;
                                        break;
                                    }
                                }
                            }
                            if (clipDur <= 0.0f || controller.currentStateTime >= clipDur) {
                                shouldTransition = true;
                            }
                        }

                        if (shouldTransition) {
                            // Trigger transition!
                            controller.fromState = controller.currentState;
                            controller.fromStateTime = controller.currentStateTime;
                            controller.currentState = trans.toState;
                            controller.currentStateTime = 0.0f;
                            
                            controller.targetState = trans.toState;
                            controller.crossfadeProgress = 0.0f;
                            controller.crossfadeDuration = trans.crossfadeDuration;
                            controller.isCrossfading = true;
                            break; // only transition once per tick
                        }
                    }
                }
            }
        }

        const AnimationClip* findClip(const AnimatorComponent& animator, const std::string& name) {
            if (name.empty()) return nullptr;
            for (const auto& clip : animator.animations) {
                if (clip.name == name) return &clip;
            }
            // Stem or case-insensitive fallback
            for (const auto& clip : animator.animations) {
                std::filesystem::path p(clip.name);
                if (p.stem().string() == name) return &clip;
            }
            // Case insensitive exact or stem match
            std::string lowerName = name;
            for (char& c : lowerName) c = (char)std::tolower((unsigned char)c);
            for (const auto& clip : animator.animations) {
                std::string lowerClip = clip.name;
                for (char& c : lowerClip) c = (char)std::tolower((unsigned char)c);
                if (lowerClip == lowerName) return &clip;
                std::filesystem::path p(lowerClip);
                if (p.stem().string() == lowerName) return &clip;
            }
            return nullptr;
        }

        void sampleClip(const AnimationClip& clip, float time, const SkeletonComponent& skeleton, std::vector<JointPose>& outPose) {
            for (const auto& channel : clip.channels) {
                if (channel.jointIndex < 0 || channel.jointIndex >= static_cast<int>(skeleton.joints.size())) {
                    continue;
                }
                auto& joint = skeleton.joints[channel.jointIndex];
                outPose[channel.jointIndex].translation = interpolateTranslation(channel.translationKeys, time, joint.bindTranslation);
                outPose[channel.jointIndex].rotation = interpolateRotation(channel.rotationKeys, time, joint.bindRotation);
                outPose[channel.jointIndex].scale = interpolateScale(channel.scaleKeys, time, joint.bindScale);
            }
        }

        void blendPoses(const std::vector<JointPose>& poseA, const std::vector<JointPose>& poseB, float weight, std::vector<JointPose>& outPose) {
            for (size_t i = 0; i < outPose.size(); ++i) {
                outPose[i].translation = glm::mix(poseA[i].translation, poseB[i].translation, weight);
                outPose[i].rotation = glm::slerp(poseA[i].rotation, poseB[i].rotation, weight);
                outPose[i].scale = glm::mix(poseA[i].scale, poseB[i].scale, weight);
            }
        }

        void evaluateStatePose(const AnimationControllerComponent& controller, const AnimationState& state, float stateTime, const AnimatorComponent& animator, const SkeletonComponent& skeleton, std::vector<JointPose>& outPose) {
            if (state.isBlendTree) {
                const auto& tree = state.blendTree;
                if (tree.nodes.empty()) return;

                float paramVal = 0.0f;
                auto it = controller.parameters.find(tree.parameterName);
                if (it != controller.parameters.end()) {
                    paramVal = it->second;
                }

                if (tree.is2D) {
                    float paramX = 0.0f;
                    auto itX = controller.parameters.find(tree.parameterName);
                    if (itX != controller.parameters.end()) {
                        paramX = itX->second;
                    }

                    float paramY = 0.0f;
                    auto itY = controller.parameters.find(tree.parameterYName);
                    if (itY != controller.parameters.end()) {
                        paramY = itY->second;
                    }

                    glm::vec2 p(paramX, paramY);
                    size_t numNodes = tree.nodes.size();
                    std::vector<float> weights(numNodes, 0.0f);
                    float totalWeight = 0.0f;

                    // 2D Freeform Cartesian / Gradient Band Blending algorithm
                    for (size_t i = 0; i < numNodes; ++i) {
                        glm::vec2 pi = tree.nodes[i].threshold2D;
                        float minVal = 1.0f;

                        for (size_t j = 0; j < numNodes; ++j) {
                            if (i == j) continue;
                            glm::vec2 pj = tree.nodes[j].threshold2D;
                            glm::vec2 v = pj - pi;
                            float lenSq = glm::dot(v, v);
                            if (lenSq < 1e-5f) continue;

                            glm::vec2 u = p - pi;
                            float t = glm::dot(u, v) / lenSq;
                            float val = 1.0f - t;
                            if (val < minVal) {
                                minVal = val;
                            }
                        }

                        weights[i] = std::max(0.0f, minVal);
                        totalWeight += weights[i];
                    }

                    // Fallback to nearest neighbor node if totalWeight is zero (e.g. all nodes coincide or param outside gradient)
                    if (totalWeight <= 1e-5f && numNodes > 0) {
                        size_t nearestIdx = 0;
                        float minDistSq = 1e30f;
                        for (size_t i = 0; i < numNodes; ++i) {
                            glm::vec2 diff = p - tree.nodes[i].threshold2D;
                            float dSq = glm::dot(diff, diff);
                            if (dSq < minDistSq) {
                                minDistSq = dSq;
                                nearestIdx = i;
                            }
                        }
                        weights[nearestIdx] = 1.0f;
                        totalWeight = 1.0f;
                    }

                    // Normalize weights and sample/blend poses
                    if (totalWeight > 1e-5f) {
                        for (size_t i = 0; i < numNodes; ++i) {
                            weights[i] /= totalWeight;
                        }

                        std::vector<JointPose> blendedPose(skeleton.joints.size());
                        for (size_t j = 0; j < skeleton.joints.size(); ++j) {
                            blendedPose[j] = { skeleton.joints[j].bindTranslation, skeleton.joints[j].bindRotation, skeleton.joints[j].bindScale };
                        }

                        float cumulativeWeight = 0.0f;
                        bool firstPoseSet = false;

                        for (size_t i = 0; i < numNodes; ++i) {
                            float w = weights[i];
                            if (w < 1e-4f) continue;

                            if (const AnimationClip* clip = findClip(animator, tree.nodes[i].clipName)) {
                                std::vector<JointPose> tempPose(skeleton.joints.size());
                                for (size_t j = 0; j < skeleton.joints.size(); ++j) {
                                    tempPose[j] = { skeleton.joints[j].bindTranslation, skeleton.joints[j].bindRotation, skeleton.joints[j].bindScale };
                                }
                                sampleClip(*clip, stateTime, skeleton, tempPose);

                                if (!firstPoseSet) {
                                    blendedPose = tempPose;
                                    cumulativeWeight = w;
                                    firstPoseSet = true;
                                } else {
                                    float blendFactor = w / (cumulativeWeight + w);
                                    blendPoses(blendedPose, tempPose, blendFactor, blendedPose);
                                    cumulativeWeight += w;
                                }
                            }
                        }
                        outPose = blendedPose;
                    }
                } else {
                    // Sort nodes by threshold for robust linear 1D interpolation
                    std::vector<BlendNode> sortedNodes = tree.nodes;
                    std::sort(sortedNodes.begin(), sortedNodes.end(), [](const BlendNode& a, const BlendNode& b) {
                        return a.threshold < b.threshold;
                    });

                    if (paramVal <= sortedNodes.front().threshold) {
                        if (const AnimationClip* clip = findClip(animator, sortedNodes.front().clipName)) {
                            sampleClip(*clip, stateTime, skeleton, outPose);
                        }
                    } else if (paramVal >= sortedNodes.back().threshold) {
                        if (const AnimationClip* clip = findClip(animator, sortedNodes.back().clipName)) {
                            sampleClip(*clip, stateTime, skeleton, outPose);
                        }
                    } else {
                        for (size_t i = 0; i < sortedNodes.size() - 1; ++i) {
                            if (paramVal >= sortedNodes[i].threshold && paramVal <= sortedNodes[i+1].threshold) {
                                float t = (paramVal - sortedNodes[i].threshold) / (sortedNodes[i+1].threshold - sortedNodes[i].threshold);
                                
                                std::vector<JointPose> poseA(skeleton.joints.size());
                                std::vector<JointPose> poseB(skeleton.joints.size());
                                for (size_t j = 0; j < skeleton.joints.size(); ++j) {
                                    poseA[j] = { skeleton.joints[j].bindTranslation, skeleton.joints[j].bindRotation, skeleton.joints[j].bindScale };
                                    poseB[j] = poseA[j];
                                }

                                if (const AnimationClip* clipA = findClip(animator, sortedNodes[i].clipName)) {
                                    sampleClip(*clipA, stateTime, skeleton, poseA);
                                }
                                if (const AnimationClip* clipB = findClip(animator, sortedNodes[i+1].clipName)) {
                                    sampleClip(*clipB, stateTime, skeleton, poseB);
                                }

                                blendPoses(poseA, poseB, t, outPose);
                                break;
                            }
                        }
                    }
                }
            } else {
                if (const AnimationClip* clip = findClip(animator, state.clipName)) {
                    sampleClip(*clip, stateTime, skeleton, outPose);
                }
            }
        }

        int findJointIndex(const SkeletonComponent& skeleton, const std::string& name) {
            for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                if (skeleton.joints[i].name == name) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        glm::quat rotationBetweenVectors(glm::vec3 u, glm::vec3 v) {
            u = glm::normalize(u);
            v = glm::normalize(v);
            float cosTheta = glm::dot(u, v);
            glm::vec3 rotationAxis;

            if (cosTheta < -1.0f + 1e-6f) {
                rotationAxis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), u);
                if (glm::dot(rotationAxis, rotationAxis) < 0.01f) {
                    rotationAxis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), u);
                }
                rotationAxis = glm::normalize(rotationAxis);
                return glm::angleAxis(glm::radians(180.0f), rotationAxis);
            }

            rotationAxis = glm::cross(u, v);
            float s = std::sqrt((1.0f + cosTheta) * 2.0f);
            float invs = 1.0f / s;

            return glm::quat(
                s * 0.5f,
                rotationAxis.x * invs,
                rotationAxis.y * invs,
                rotationAxis.z * invs
            );
        }

        void solve2BoneIK(SkeletonComponent& skeleton, const IKSolverComponent& ik, std::vector<glm::mat4>& globalTransforms) {
            int idxA = findJointIndex(skeleton, ik.startJointName);
            int idxB = findJointIndex(skeleton, ik.middleJointName);
            int idxC = findJointIndex(skeleton, ik.endJointName);

            if (idxA == -1 || idxB == -1 || idxC == -1) return;

            glm::mat4 origGlobalA = globalTransforms[idxA];
            glm::mat4 origGlobalB = globalTransforms[idxB];
            glm::mat4 origGlobalC = globalTransforms[idxC];

            glm::vec3 P_A = glm::vec3(origGlobalA[3]);
            glm::vec3 P_B = glm::vec3(origGlobalB[3]);
            glm::vec3 P_C = glm::vec3(origGlobalC[3]);
            glm::vec3 P_T = ik.targetPosition;
            glm::vec3 P_P = ik.polePosition;

            float L1 = glm::distance(P_B, P_A);
            float L2 = glm::distance(P_C, P_B);
            float D = glm::distance(P_T, P_A);

            if (L1 < 1e-4f || L2 < 1e-4f) return;

            if (D > L1 + L2 - 0.001f) {
                P_T = P_A + glm::normalize(P_T - P_A) * (L1 + L2 - 0.001f);
                D = L1 + L2 - 0.001f;
            }
            if (D < 0.001f) {
                P_T = P_A + glm::vec3(0.0f, 0.0f, 0.001f);
                D = 0.001f;
            }

            float cosAlpha = (L1 * L1 + D * D - L2 * L2) / (2.0f * L1 * D);
            cosAlpha = glm::clamp(cosAlpha, -1.0f, 1.0f);
            float alpha = std::acos(cosAlpha);

            glm::vec3 d_AT = glm::normalize(P_T - P_A);
            glm::vec3 v_AP = P_P - P_A;
            glm::vec3 N = glm::cross(d_AT, v_AP);
            if (glm::dot(N, N) < 1e-6f) {
                N = glm::cross(d_AT, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::dot(N, N) < 1e-6f) {
                    N = glm::cross(d_AT, glm::vec3(1.0f, 0.0f, 0.0f));
                }
            }
            N = glm::normalize(N);
            glm::vec3 B_dir = glm::normalize(glm::cross(N, d_AT));

            glm::vec3 P_B_new = P_A + (d_AT * std::cos(alpha) + B_dir * std::sin(alpha)) * L1;
            glm::vec3 P_C_new = P_T;

            glm::vec3 origDirA = P_B - P_A;
            glm::vec3 targetDirA = P_B_new - P_A;
            glm::quat rotA = rotationBetweenVectors(origDirA, targetDirA);
            glm::mat3 rotPartA = glm::mat3(glm::toMat4(rotA)) * glm::mat3(origGlobalA);
            glm::mat4 newGlobalA = glm::mat4(rotPartA);
            newGlobalA[3] = glm::vec4(P_A, 1.0f);

            glm::vec3 origDirB = P_C - P_B;
            glm::vec3 targetDirB = P_C_new - P_B_new;
            glm::quat rotB = rotationBetweenVectors(origDirB, targetDirB);
            glm::mat3 rotPartB = glm::mat3(glm::toMat4(rotB)) * glm::mat3(origGlobalB);
            glm::mat4 newGlobalB = glm::mat4(rotPartB);
            newGlobalB[3] = glm::vec4(P_B_new, 1.0f);

            glm::mat4 newGlobalC = origGlobalC;
            newGlobalC[3] = glm::vec4(P_C_new, 1.0f);

            float w = glm::clamp(ik.targetWeight, 0.0f, 1.0f);
            globalTransforms[idxA] = origGlobalA * (1.0f - w) + newGlobalA * w;
            globalTransforms[idxB] = origGlobalB * (1.0f - w) + newGlobalB * w;
            globalTransforms[idxC] = origGlobalC * (1.0f - w) + newGlobalC * w;

            auto updateLocal = [&](int idx) {
                int parentIdx = skeleton.joints[idx].parentIndex;
                if (parentIdx != -1) {
                    skeleton.joints[idx].localTransform = glm::inverse(globalTransforms[parentIdx]) * globalTransforms[idx];
                } else {
                    skeleton.joints[idx].localTransform = globalTransforms[idx];
                }
            };

            updateLocal(idxA);
            updateLocal(idxB);
            updateLocal(idxC);

            std::vector<bool> resolved(skeleton.joints.size(), false);
            resolved[idxA] = true;
            resolved[idxB] = true;
            resolved[idxC] = true;

            std::function<void(int)> propagate = [&](int jointIdx) {
                int parentIdx = skeleton.joints[jointIdx].parentIndex;
                if (parentIdx != -1 && resolved[parentIdx] && !resolved[jointIdx]) {
                    globalTransforms[jointIdx] = globalTransforms[parentIdx] * skeleton.joints[jointIdx].localTransform;
                    resolved[jointIdx] = true;
                }
                for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                    if (skeleton.joints[i].parentIndex == jointIdx) {
                        propagate(static_cast<int>(i));
                    }
                }
            };

            propagate(idxA);
            propagate(idxB);
            propagate(idxC);
        }

        void solveFABRIK(SkeletonComponent& skeleton, const IKSolverComponent& ik, std::vector<glm::mat4>& globalTransforms) {
            if (ik.jointChainNames.size() < 2) return;

            std::vector<int> chainIndices;
            for (const auto& name : ik.jointChainNames) {
                int idx = findJointIndex(skeleton, name);
                if (idx != -1) {
                    chainIndices.push_back(idx);
                }
            }

            if (chainIndices.size() < 2) return;

            int n = static_cast<int>(chainIndices.size()) - 1;
            std::vector<glm::vec3> positions(chainIndices.size());
            std::vector<float> boneLengths(n);
            float totalLength = 0.0f;

            for (size_t i = 0; i < chainIndices.size(); ++i) {
                positions[i] = glm::vec3(globalTransforms[chainIndices[i]][3]);
                if (i > 0) {
                    boneLengths[i - 1] = glm::distance(positions[i], positions[i - 1]);
                    totalLength += boneLengths[i - 1];
                }
            }

            glm::vec3 target = ik.targetPosition;
            glm::vec3 origin = positions[0];

            float distToTarget = glm::distance(origin, target);
            if (distToTarget > totalLength) {
                for (int i = 0; i < n; ++i) {
                    glm::vec3 dir = glm::normalize(target - positions[i]);
                    positions[i + 1] = positions[i] + dir * boneLengths[i];
                }
            } else {
                for (int iter = 0; iter < ik.maxIterations; ++iter) {
                    float err = glm::distance(positions[n], target);
                    if (err < ik.tolerance) break;

                    positions[n] = target;
                    for (int i = n - 1; i >= 0; --i) {
                        glm::vec3 dir = glm::normalize(positions[i] - positions[i + 1]);
                        positions[i] = positions[i + 1] + dir * boneLengths[i];
                    }

                    positions[0] = origin;
                    for (int i = 0; i < n; ++i) {
                        glm::vec3 dir = glm::normalize(positions[i + 1] - positions[i]);
                        positions[i + 1] = positions[i] + dir * boneLengths[i];
                    }
                }
            }

            for (int i = 0; i < n; ++i) {
                int idxCurrent = chainIndices[i];
                int idxNext = chainIndices[i + 1];

                glm::mat4 origGlobalCurrent = globalTransforms[idxCurrent];
                // Compute current position of idxNext by evaluating relative to current idxCurrent transform
                glm::mat4 currentGlobalNext = origGlobalCurrent * skeleton.joints[idxNext].localTransform;

                glm::vec3 P_current = glm::vec3(origGlobalCurrent[3]);
                glm::vec3 P_next = glm::vec3(currentGlobalNext[3]);

                glm::vec3 origDir = P_next - P_current;
                glm::vec3 solvedDir = positions[i + 1] - positions[i];

                if (glm::dot(origDir, origDir) < 1e-6f || glm::dot(solvedDir, solvedDir) < 1e-6f) continue;

                glm::quat rot = rotationBetweenVectors(origDir, solvedDir);
                float w = glm::clamp(ik.targetWeight, 0.0f, 1.0f);
                glm::quat blendedRot = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), rot, w);

                // Rotate the current joint
                glm::mat3 rotPart = glm::mat3(glm::toMat4(blendedRot)) * glm::mat3(origGlobalCurrent);
                glm::mat4 newGlobal = glm::mat4(rotPart);
                newGlobal[3] = glm::vec4(glm::mix(P_current, positions[i], w), 1.0f);

                globalTransforms[idxCurrent] = newGlobal;

                // Update local transform of current joint relative to parent
                int parentIdx = skeleton.joints[idxCurrent].parentIndex;
                if (parentIdx != -1) {
                    skeleton.joints[idxCurrent].localTransform = glm::inverse(globalTransforms[parentIdx]) * globalTransforms[idxCurrent];
                } else {
                    skeleton.joints[idxCurrent].localTransform = globalTransforms[idxCurrent];
                }

                // Recalculate next joint global transform based on the newly rotated current joint
                globalTransforms[idxNext] = globalTransforms[idxCurrent] * skeleton.joints[idxNext].localTransform;
            }

            std::vector<bool> resolved(skeleton.joints.size(), false);
            for (int idx : chainIndices) {
                resolved[idx] = true;
            }

            std::function<void(int)> propagate = [&](int jointIdx) {
                int parentIdx = skeleton.joints[jointIdx].parentIndex;
                if (parentIdx != -1 && resolved[parentIdx] && !resolved[jointIdx]) {
                    globalTransforms[jointIdx] = globalTransforms[parentIdx] * skeleton.joints[jointIdx].localTransform;
                    resolved[jointIdx] = true;
                }
                for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                    if (skeleton.joints[i].parentIndex == jointIdx) {
                        propagate(static_cast<int>(i));
                    }
                }
            };

            for (int idx : chainIndices) {
                propagate(idx);
            }
        }

        /**
         * @brief Updates the animation timing and calculates local/global joint matrices for an entity.
         */
        void updateEntityAnimation(Entity entity, SkeletonComponent& skeleton, AnimatorComponent& animator, float dt) {
            if (skeleton.joints.empty()) return;

            // 1. Initialize temporary pose with bind-pose default TRS values
            std::vector<JointPose> finalPose(skeleton.joints.size());
            for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                finalPose[i] = { skeleton.joints[i].bindTranslation, skeleton.joints[i].bindRotation, skeleton.joints[i].bindScale };
            }

            // 2. Sample poses according to State Machine or fallback to Single Clip Animator
            if (auto* controller = registry.get<AnimationControllerComponent>(entity)) {
                if (!controller->currentState.empty()) {
                    // Sample current state
                    const AnimationState* currState = nullptr;
                    for (const auto& state : controller->states) {
                        if (state.name == controller->currentState) {
                            currState = &state;
                            break;
                        }
                    }

                    if (currState) {
                        evaluateStatePose(*controller, *currState, controller->currentStateTime, animator, skeleton, finalPose);
                    }

                    // Handle crossfading transition
                    if (controller->isCrossfading && !controller->fromState.empty()) {
                        const AnimationState* fState = nullptr;
                        for (const auto& state : controller->states) {
                            if (state.name == controller->fromState) {
                                fState = &state;
                                break;
                            }
                        }

                        if (fState) {
                            std::vector<JointPose> fromPose(skeleton.joints.size());
                            for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                                fromPose[i] = { skeleton.joints[i].bindTranslation, skeleton.joints[i].bindRotation, skeleton.joints[i].bindScale };
                            }
                            evaluateStatePose(*controller, *fState, controller->fromStateTime, animator, skeleton, fromPose);
                            
                            float weight = glm::clamp(controller->crossfadeProgress / controller->crossfadeDuration, 0.0f, 1.0f);
                            blendPoses(fromPose, finalPose, weight, finalPose);
                        }
                    }
                }
            } else {
                // Fallback to single clip playback
                AnimationClip* clip = nullptr;
                if (!animator.animations.empty() && 
                    animator.activeAnimationIndex >= 0 && 
                    animator.activeAnimationIndex < static_cast<int>(animator.animations.size())) {
                    clip = &animator.animations[animator.activeAnimationIndex];
                }

                if (clip) {
                    if (editorMode.isPlaying) {
                        static int lastPrintedIndex = -999;
                        if (animator.activeAnimationIndex != lastPrintedIndex) {
                            std::cout << "[AnimationSystem] Entity plays animation index: " << animator.activeAnimationIndex 
                                      << " (Name: " << clip->name << ")" << std::endl;
                            lastPrintedIndex = animator.activeAnimationIndex;
                        }
                    }
                    float speed = editorMode.isPlaying ? (animator.playbackSpeed > 0.0f ? animator.playbackSpeed : 1.0f) : animator.playbackSpeed;
                    animator.currentTime += dt * speed;
                    if (animator.loop) {
                        if (clip->duration > 0.0f) {
                            animator.currentTime = std::fmod(animator.currentTime, clip->duration);
                        } else {
                            animator.currentTime = 0.0f;
                        }
                    } else {
                        animator.currentTime = std::min(animator.currentTime, clip->duration);
                    }

                    sampleClip(*clip, animator.currentTime, skeleton, finalPose);
                }
            }

            // 3. Write blended pose back to localTransforms
            for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                glm::mat4 tMat = glm::translate(glm::mat4(1.0f), finalPose[i].translation);
                glm::mat4 rMat = glm::toMat4(finalPose[i].rotation);
                glm::mat4 sMat = glm::scale(glm::mat4(1.0f), finalPose[i].scale);
                skeleton.joints[i].localTransform = tMat * rMat * sMat;
            }

            // 4. Resolve Hierarchical Global Transforms (Forward Kinematics)
            std::vector<glm::mat4> globalTransforms(skeleton.joints.size(), glm::mat4(1.0f));
            std::vector<bool> resolved(skeleton.joints.size(), false);
            
            auto resolveJointGlobal = [&](auto& self, int jointIdx) -> glm::mat4 {
                if (jointIdx == -1) return glm::mat4(1.0f);
                if (resolved[jointIdx]) return globalTransforms[jointIdx];

                glm::mat4 parentGlobal = self(self, skeleton.joints[jointIdx].parentIndex);
                globalTransforms[jointIdx] = parentGlobal * skeleton.joints[jointIdx].localTransform;
                resolved[jointIdx] = true;
                return globalTransforms[jointIdx];
            };

            for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                resolveJointGlobal(resolveJointGlobal, static_cast<int>(i));
            }

            // 5. Post-FK: Apply IK solver if present and enabled
            if (auto* ik = registry.get<IKSolverComponent>(entity)) {
                if (ik->enabled) {
                    if (ik->solverType == IKSolverType::TwoBone) {
                        solve2BoneIK(skeleton, *ik, globalTransforms);
                    } else if (ik->solverType == IKSolverType::FABRIK) {
                        solveFABRIK(skeleton, *ik, globalTransforms);
                    }
                }
            }

            // 6. Generate final joint offset matrix palette
            skeleton.jointMatrices.assign(256, glm::mat4(1.0f));
            for (size_t i = 0; i < std::min(skeleton.joints.size(), size_t(256)); ++i) {
                skeleton.jointMatrices[i] = globalTransforms[i] * skeleton.joints[i].inverseBindMatrix;
            }

            // 7. Upload Joint Matrices Palette to GPU Buffer
            VkDeviceSize bufferSize = 256 * sizeof(glm::mat4);

            if (!skeleton.gpuBuffer) {
                skeleton.gpuBuffer = std::make_shared<VulkanBuffer>();
                skeleton.gpuBuffer->create(
                    renderer.device.getDevice(),
                    renderer.device.getPhysicalDevice(),
                    bufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );

                renderer.descriptors.allocateJointsDescriptorSet(
                    skeleton.descriptorSet,
                    skeleton.gpuBuffer->get(),
                    bufferSize
                );
            }

            skeleton.gpuBuffer->uploadData(skeleton.jointMatrices.data(), bufferSize);
            sampleAndApplyProperties(entity, animator);
        }

        // --- Keyframe Interpolation Helpers ---

        glm::vec3 interpolateTranslation(const std::vector<Keyframe>& keys, float time, const glm::vec3& defaultValue) {
            if (keys.empty()) return defaultValue;
            if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
            if (time >= keys.back().time) return keys.back().value;

            size_t index = 0;
            for (size_t i = 0; i < keys.size() - 1; ++i) {
                if (time >= keys[i].time && time <= keys[i+1].time) {
                    index = i;
                    break;
                }
            }

            const auto& k1 = keys[index];
            const auto& k2 = keys[index+1];
            float factor = (time - k1.time) / (k2.time - k1.time);
            return glm::mix(k1.value, k2.value, factor);
        }

        glm::quat interpolateRotation(const std::vector<KeyframeRot>& keys, float time, const glm::quat& defaultValue) {
            if (keys.empty()) return defaultValue;
            if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
            if (time >= keys.back().time) return keys.back().value;

            size_t index = 0;
            for (size_t i = 0; i < keys.size() - 1; ++i) {
                if (time >= keys[i].time && time <= keys[i+1].time) {
                    index = i;
                    break;
                }
            }

            const auto& k1 = keys[index];
            const auto& k2 = keys[index+1];
            float factor = (time - k1.time) / (k2.time - k1.time);
            return glm::slerp(k1.value, k2.value, factor);
        }

        glm::vec3 interpolateScale(const std::vector<Keyframe>& keys, float time, const glm::vec3& defaultValue) {
            if (keys.empty()) return defaultValue;
            if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
            if (time >= keys.back().time) return keys.back().value;

            size_t index = 0;
            for (size_t i = 0; i < keys.size() - 1; ++i) {
                if (time >= keys[i].time && time <= keys[i+1].time) {
                    index = i;
                    break;
                }
            }

            const auto& k1 = keys[index];
            const auto& k2 = keys[index+1];
            float factor = (time - k1.time) / (k2.time - k1.time);
            return glm::mix(k1.value, k2.value, factor);
        }

        glm::vec4 interpolateProperty(const std::vector<PropertyKeyframe>& keys, float time, const glm::vec4& defaultValue) {
            if (keys.empty()) return defaultValue;
            if (keys.size() == 1) return keys.front().value;

            std::vector<PropertyKeyframe> spacedKeys;
            const std::vector<PropertyKeyframe>* effectiveKeys = &keys;

            if (keys.back().time <= keys.front().time + 0.001f) {
                spacedKeys = keys;
                for (size_t i = 0; i < spacedKeys.size(); ++i) {
                    spacedKeys[i].time = static_cast<float>(i) * 0.1f;
                }
                effectiveKeys = &spacedKeys;
                float dur = spacedKeys.back().time + 0.1f;
                time = std::fmod(time, dur);
            }

            const auto& kList = *effectiveKeys;
            if (time <= kList.front().time) return kList.front().value;
            if (time >= kList.back().time) return kList.back().value;

            size_t index = 0;
            for (size_t i = 0; i < kList.size() - 1; ++i) {
                if (time >= kList[i].time && time <= kList[i+1].time) {
                    index = i;
                    break;
                }
            }

            const auto& k1 = kList[index];
            const auto& k2 = kList[index+1];
            float interval = k2.time - k1.time;
            float factor = (interval > 0.0001f) ? (time - k1.time) / interval : 0.0f;
            return glm::mix(k1.value, k2.value, factor);
        }

        std::string sampleStringProperty(const std::vector<PropertyKeyframe>& keys, float time) {
            if (keys.empty()) return "";
            if (keys.size() == 1) return keys.front().stringValue;

            std::vector<PropertyKeyframe> spacedKeys;
            const std::vector<PropertyKeyframe>* effectiveKeys = &keys;

            if (keys.back().time <= keys.front().time + 0.001f) {
                spacedKeys = keys;
                for (size_t i = 0; i < spacedKeys.size(); ++i) {
                    spacedKeys[i].time = static_cast<float>(i) * 0.1f;
                }
                effectiveKeys = &spacedKeys;
                float dur = spacedKeys.back().time + 0.1f;
                time = std::fmod(time, dur);
            }

            const auto& kList = *effectiveKeys;
            if (time <= kList.front().time) return kList.front().stringValue;
            if (time >= kList.back().time) return kList.back().stringValue;
            for (size_t i = 0; i < kList.size() - 1; ++i) {
                if (time >= kList[i].time && time < kList[i + 1].time) {
                    return kList[i].stringValue;
                }
            }
            return kList.back().stringValue;
        }

        void sampleAndApplyProperties(Entity entity, AnimatorComponent& animator) {
            AnimationClip* clip = nullptr;
            if (!animator.animations.empty()) {
                if (animator.activeAnimationIndex >= 0 && animator.activeAnimationIndex < static_cast<int>(animator.animations.size())) {
                    clip = &animator.animations[animator.activeAnimationIndex];
                } else {
                    animator.activeAnimationIndex = 0;
                    clip = &animator.animations[0];
                }
            }

            if (!clip || clip->propertyChannels.empty()) return;

            // Helper to strip spaces/special chars/Component suffix for flexible matching
            auto normalizeName = [](const std::string& s) {
                std::string res;
                for (char c : s) {
                    if (c != ' ' && c != '_' && c != '&' && c != '/') {
                        res += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                }
                if (res.size() > 9 && res.substr(res.size() - 9) == "component") {
                    res = res.substr(0, res.size() - 9);
                }
                return res;
            };

            // Collect target entity and all child entities in hierarchy
            std::vector<Entity> targetEntities;
            targetEntities.push_back(entity);

            for (size_t readIdx = 0; readIdx < targetEntities.size(); ++readIdx) {
                Entity curr = targetEntities[readIdx];
                for (auto [childEntity, hierarchy] : registry.view<HierarchyComponent>()) {
                    if (hierarchy.parent.getId() == curr.getId()) {
                        targetEntities.push_back(childEntity);
                    }
                }
            }

            auto& reflReg = Engine::ComponentReflectionRegistry::getInstance();

            for (const auto& channel : clip->propertyChannels) {
                if (channel.keys.empty()) continue;

                std::string normChanComp = normalizeName(channel.componentName);
                std::string normChanField = normalizeName(channel.fieldName);

                // Direct Type-Safe Access for SpriteRenderer
                bool handledDirectly = false;
                for (Entity targetEntity : targetEntities) {
                    if (auto* spr = registry.get<Engine::SpriteRenderer>(targetEntity)) {
                        bool isTex = (normChanField.find("texture") != std::string::npos || normChanField.find("image") != std::string::npos || normChanField.find("path") != std::string::npos);
                        bool isCol = (normChanField.find("color") != std::string::npos);
                        bool isFlipX = (normChanField.find("flipx") != std::string::npos);
                        bool isFlipY = (normChanField.find("flipy") != std::string::npos);
                        bool isSort = (normChanField.find("sort") != std::string::npos || normChanField.find("order") != std::string::npos);

                        if (isTex) {
                            std::string strVal = sampleStringProperty(channel.keys, animator.currentTime);
                            spr->texturePath = strVal;
                            spr->_dirty = true;
                            if (auto* mat = registry.get<Material>(targetEntity)) {
                                mat->texturePath = strVal;
                                renderer.resourceManager->updateMaterialDescriptorSet(*mat, renderer);
                                spr->_loadedTexturePath = strVal;
                            }
                            handledDirectly = true;
                        } else if (isCol) {
                            glm::vec4 val = interpolateProperty(channel.keys, animator.currentTime, spr->color);
                            spr->color = val;
                            if (auto* mat = registry.get<Material>(targetEntity)) {
                                mat->color = spr->color;
                            }
                            handledDirectly = true;
                        } else if (isFlipX) {
                            glm::vec4 val = interpolateProperty(channel.keys, animator.currentTime, glm::vec4(spr->flipX ? 1.0f : 0.0f));
                            spr->flipX = (val.x > 0.5f);
                            if (auto* mat = registry.get<Material>(targetEntity)) {
                                mat->roughness = spr->flipX ? -1.0f : 1.0f;
                            }
                            handledDirectly = true;
                        } else if (isFlipY) {
                            glm::vec4 val = interpolateProperty(channel.keys, animator.currentTime, glm::vec4(spr->flipY ? 1.0f : 0.0f));
                            spr->flipY = (val.x > 0.5f);
                            if (auto* mat = registry.get<Material>(targetEntity)) {
                                mat->metallic = spr->flipY ? -1.0f : 1.0f;
                            }
                            handledDirectly = true;
                        } else if (isSort) {
                            glm::vec4 val = interpolateProperty(channel.keys, animator.currentTime, glm::vec4(static_cast<float>(spr->sortOrder)));
                            spr->sortOrder = static_cast<int>(val.x);
                            if (auto* t = registry.get<Transform>(targetEntity)) {
                                t->position.z = spr->sortOrder * 0.0001f;
                            }
                            handledDirectly = true;
                        }
                    }
                }
                if (handledDirectly) continue;

                const Engine::ComponentReflection* targetRefl = nullptr;
                for (const auto& refl : reflReg.getReflections()) {
                    if (refl.name == channel.componentName ||
                        normalizeName(refl.name) == normChanComp ||
                        normalizeName(refl.displayName) == normChanComp) {
                        targetRefl = &refl;
                        break;
                    }
                }

                if (!targetRefl) continue;

                for (Entity targetEntity : targetEntities) {
                    if (!targetRefl->has(registry, targetEntity)) continue;

                    void* compPtr = targetRefl->get(registry, targetEntity);
                    if (!compPtr) continue;

                    auto stripPrefix = [](const std::string& s) {
                        if (s.rfind("spr", 0) == 0 && s.size() > 3) return s.substr(3);
                        if (s.rfind("m_", 0) == 0 && s.size() > 2) return s.substr(2);
                        if (s.rfind("s_", 0) == 0 && s.size() > 2) return s.substr(2);
                        return s;
                    };
                    std::string sfn = stripPrefix(normChanField);

                    const Engine::ComponentField* targetField = nullptr;
                    for (const auto& f : targetRefl->fields) {
                        std::string fn = normalizeName(f.name);
                        std::string sfn2 = stripPrefix(fn);
                        if (f.name == channel.fieldName || fn == normChanField || sfn == sfn2 ||
                            (sfn.find("texture") != std::string::npos && sfn2.find("texture") != std::string::npos) ||
                            (sfn.find("image") != std::string::npos && sfn2.find("texture") != std::string::npos) ||
                            (sfn.find("texture") != std::string::npos && sfn2.find("image") != std::string::npos)) {
                            targetField = &f;
                            break;
                        }
                    }

                    if (!targetField) continue;

                    char* fieldPtr = static_cast<char*>(compPtr) + targetField->offset;

                    glm::vec4 val = interpolateProperty(channel.keys, animator.currentTime, glm::vec4(0.0f));

                    if (channel.type == Engine::FieldType::Float) {
                        *reinterpret_cast<float*>(fieldPtr) = val.x;
                    } else if (channel.type == Engine::FieldType::Int) {
                        *reinterpret_cast<int*>(fieldPtr) = static_cast<int>(val.x);
                    } else if (channel.type == Engine::FieldType::Bool) {
                        *reinterpret_cast<bool*>(fieldPtr) = (val.x > 0.5f);
                    } else if (channel.type == Engine::FieldType::Vec2) {
                        *reinterpret_cast<glm::vec2*>(fieldPtr) = glm::vec2(val.x, val.y);
                    } else if (channel.type == Engine::FieldType::Vec3) {
                        *reinterpret_cast<glm::vec3*>(fieldPtr) = glm::vec3(val.x, val.y, val.z);
                    } else if (channel.type == Engine::FieldType::Vec4) {
                        *reinterpret_cast<glm::vec4*>(fieldPtr) = val;
                    } else if (channel.type == Engine::FieldType::String) {
                        std::string strVal = sampleStringProperty(channel.keys, animator.currentTime);
                        *reinterpret_cast<std::string*>(fieldPtr) = strVal;
                    }

                    if (auto* spr = registry.get<Engine::SpriteRenderer>(targetEntity)) {
                        spr->_dirty = true;
                        if (auto* mat = registry.get<Material>(targetEntity)) {
                            mat->texturePath = spr->texturePath;
                            renderer.resourceManager->updateMaterialDescriptorSet(*mat, renderer);
                            spr->_loadedTexturePath = spr->texturePath;
                        }
                    }
                }
            }
        }

        void updateGenericAnimation(Entity entity, AnimatorComponent& animator, float dt) {
            AnimationClip* clip = nullptr;
            if (!animator.animations.empty()) {
                if (animator.activeAnimationIndex >= 0 && animator.activeAnimationIndex < static_cast<int>(animator.animations.size())) {
                    clip = &animator.animations[animator.activeAnimationIndex];
                } else {
                    animator.activeAnimationIndex = 0;
                    clip = &animator.animations[0];
                }
            }

            if (clip) {
                float effectiveDur = clip->duration;
                for (const auto& chan : clip->propertyChannels) {
                    if (!chan.keys.empty()) {
                        effectiveDur = std::max(effectiveDur, chan.keys.back().time);
                    }
                }
                if (effectiveDur <= 0.0f) effectiveDur = 1.0f;
                clip->duration = effectiveDur;

                // If there is no AnimationControllerComponent, drive animator time directly
                if (!registry.has<AnimationControllerComponent>(entity)) {
                    float speed = editorMode.isPlaying ? (animator.playbackSpeed > 0.0f ? animator.playbackSpeed : 1.0f) : animator.playbackSpeed;
                    animator.currentTime += dt * speed;
                    if (animator.loop) {
                        animator.currentTime = std::fmod(animator.currentTime, effectiveDur);
                    } else {
                        animator.currentTime = std::min(animator.currentTime, effectiveDur);
                    }
                }
            }

            sampleAndApplyProperties(entity, animator);
        }

        Registry& registry;
        VulkanRenderer& renderer;
        EditorModeState& editorMode;
        std::vector<Entity> controllerEntities;
        std::vector<Entity> animatedEntities;
        std::vector<Entity> genericAnimatedEntities;
    };

