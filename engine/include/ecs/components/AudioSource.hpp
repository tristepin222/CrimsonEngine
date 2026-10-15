#pragma once
#include <string>
#include <glm/glm.hpp>
#include "core/EngineAPI.hpp"
#include "meta/ComponentReflection.hpp"


/**
 * @struct AudioSourceComponent
 * @brief Component to emit 2D or spatialized 3D sounds.
 */
// [ReflectClass("Audio/Audio Source")]
struct ENGINE_API AudioSourceComponent {
    /** @brief Path to the audio sound clip file (e.g. .wav, .mp3, .flac). */
    // [ReflectField]
    std::string clipPath = "";
    /** @brief Playback volume multiplier (0.0 to 1.0+). */
    // [ReflectField]
    float volume = 1.0f;
    /** @brief Playback pitch/frequency multiplier. */
    // [ReflectField]
    float pitch = 1.0f;
    /** @brief Whether sound loops continuously when playback completes. */
    // [ReflectField]
    bool loop = false;
    /** @brief Automatically start audio playback when scene initializes. */
    // [ReflectField]
    bool playOnAwake = true;
    /** @brief Enable 3D spatial attenuation and directional panning. */
    // [ReflectField]
    bool spatialized = true;
    /** @brief Distance at which volume starts attenuating. */
    // [ReflectField]
    float minDistance = 1.0f;
    /** @brief Maximum distance beyond which sound is silent. */
    // [ReflectField]
    float maxDistance = 50.0f;


    /** @brief Active playback state flag. */
    bool isPlaying = false;
    /** @brief Previous frame playback state flag. */
    bool wasPlaying = false;
    /** @brief Opaque handle pointer to miniaudio sound object (ma_sound*). */
    void* internalSound = nullptr;
    /** @brief Currently loaded audio file path. */
    std::string currentLoadedPath = "";
};









