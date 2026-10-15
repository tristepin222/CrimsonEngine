#pragma once
#include "meta/ComponentReflection.hpp"

/**
 * @struct PropertyKeyframe
 * @brief Keyframe storing a reflected field value (up to 4 floats, e.g. float, bool, vec2, vec3, vec4) at a specific timestamp.
 */
struct PropertyKeyframe {
    float time = 0.0f;
    glm::vec4 value = glm::vec4(0.0f);
    std::string stringValue;
};

/**
 * @struct PropertyChannel
 * @brief Tracks keyframe sequences affecting a specific reflected component field.
 */
struct PropertyChannel {
    std::string componentName;
    std::string fieldName;
    Engine::FieldType type;
    std::vector<PropertyKeyframe> keys;
};

/**
 * @struct Keyframe
 * @brief Keyframe storing a 3D vector (translation or scale) at a specific timestamp.
 */
struct Keyframe {
    /** @brief Time offset in seconds. */
    float time = 0.0f;
    /** @brief Vector value. */
    glm::vec3 value = glm::vec3(0.0f);
};

/**
 * @struct KeyframeRot
 * @brief Keyframe storing a quaternion rotation at a specific timestamp.
 */
struct KeyframeRot {
    /** @brief Time offset in seconds. */
    float time = 0.0f;
    /** @brief Quaternion rotation value. */
    glm::quat value = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

/**
 * @struct AnimationChannel
 * @brief Tracks keyframe sequences (translation, rotation, scale) affecting a specific joint node.
 */
struct AnimationChannel {
    /** @brief Target joint index in the SkeletonComponent. */
    int jointIndex = -1;
    /** @brief Target joint name identifier. */
    std::string jointName;
    /** @brief List of translation keyframes. */
    std::vector<Keyframe> translationKeys;
    /** @brief List of rotation keyframes. */
    std::vector<KeyframeRot> rotationKeys;
    /** @brief List of scale keyframes. */
    std::vector<Keyframe> scaleKeys;
};

/**
 * @struct AnimationClip
 * @brief Collection of channels representing a single skeletal animation state.
 */
struct AnimationClip {
    /** @brief Name of the clip (e.g. "Walk", "Run"). */
    std::string name;
    /** @brief Total duration of the animation in seconds. */
    float duration = 0.0f;
    /** @brief Bone channels mapping keyframe states. */
    std::vector<AnimationChannel> channels;
    /** @brief Reflected component field channels. */
    std::vector<PropertyChannel> propertyChannels;
};

/**
 * @struct AnimatorComponent
 * @brief Animator controller managing active clips and tracking playback timing.
 */
// [ReflectClass("Animation/Animator")]
struct AnimatorComponent {
    /** @brief List of all clips loaded from the model asset. */
    std::vector<AnimationClip> animations;
    /** @brief Index of the active clip in playback. */
    int activeAnimationIndex = -1;
    /** @brief Current playback time offset in seconds. */
    float currentTime = 0.0f;
    // [ReflectField]
    float playbackSpeed = 1.0f;
    // [ReflectField]
    bool loop = true;
    // [ReflectField]
    bool isPreviewing = false;
    // [ReflectField]
    std::string loadedAnimPath;
};


