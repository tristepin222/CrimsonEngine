#pragma once
#include "core/EngineAPI.hpp"

/**
 * @struct AudioListenerComponent
 * @brief Component representing a 3D audio listener.
 */
// [ReflectClass("Audio/Audio Listener")]
struct ENGINE_API AudioListenerComponent {
    /** @brief Active status flag for this audio listener. */
    // [ReflectField]
    bool active = true;
};
