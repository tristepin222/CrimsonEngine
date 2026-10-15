#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

/**
 * @struct BlendNode
 * @brief Represents a single animation clip in a blend tree with its target threshold value.
 */
struct BlendNode {
    std::string clipName;
    float threshold = 0.0f; // Blending threshold value (e.g. speed where weight is 1.0)
    glm::vec2 threshold2D = glm::vec2(0.0f); // 2D blending threshold value
};

/**
 * @struct BlendTree
 * @brief Represents a collection of blend nodes driven by a controller parameter.
 */
struct BlendTree {
    std::vector<BlendNode> nodes;
    std::string parameterName; // e.g. "speed" or "velocityX"
    std::string parameterYName; // e.g. "velocityY"
    bool is2D = false; // Is this a 2D blend tree
};

/**
 * @struct AnimationState
 * @brief Represents a state in the animation state machine, referencing a clip or a blend tree.
 */
struct AnimationState {
    std::string name;
    std::string clipName;
    BlendTree blendTree;
    bool isBlendTree = false;
    bool isLooping = true;
    float speed = 1.0f;
};

/**
 * @struct TransitionCondition
 * @brief Represents a condition that must be met to trigger a state transition.
 */
struct TransitionCondition {
    std::string parameterName;
    std::string op; // ">", "<", "=="
    float value = 0.0f;
};

/**
 * @struct AnimationTransition
 * @brief Represents a transition rule from one state to another under certain conditions.
 */
struct AnimationTransition {
    std::string fromState;
    std::string toState;
    std::vector<TransitionCondition> conditions;
    float crossfadeDuration = 0.2f; // crossfade blending duration in seconds
};

/**
 * @struct AnimationControllerComponent
 * @brief Component that manages parameters, state machines, transitions, and blending.
 */
// [ReflectClass("Animation/Animation Controller")]
struct AnimationControllerComponent {
    std::vector<AnimationState> states;
    std::vector<AnimationTransition> transitions;
    // [ReflectField]
    std::string currentState;

    
    // Named parameters for transitions and blend trees
    std::unordered_map<std::string, float> parameters;
    
    // Active crossfade/transition tracking
    std::string fromState;
    std::string targetState;
    float currentStateTime = 0.0f;
    float fromStateTime = 0.0f;
    float crossfadeProgress = 0.0f;
    float crossfadeDuration = 0.0f;
    bool isCrossfading = false;
};


