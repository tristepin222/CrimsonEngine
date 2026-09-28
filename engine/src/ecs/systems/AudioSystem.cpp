#include "ecs/systems/AudioSystem.hpp"
#include "ecs/components/AudioSource.hpp"
#include "ecs/components/AudioListener.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Camera.hpp"
#include "miniaudio.h"
#include <algorithm>
#include <iostream>

namespace Engine {

    AudioSystem::AudioSystem(Registry& reg, EditorModeState& mode)
        : registry(reg), editorMode(mode) {
        
        m_engine = new ma_engine();
        ma_result result = ma_engine_init(nullptr, static_cast<ma_engine*>(m_engine));
        if (result != MA_SUCCESS) {
            std::cerr << "[AudioSystem] Failed to initialize miniaudio engine." << std::endl;
        } else {
            std::cout << "[AudioSystem] Miniaudio engine initialized successfully." << std::endl;
        }
    }

    AudioSystem::~AudioSystem() {
        // Unload all active sounds to prevent leaks
        for (auto& pair : m_activeSounds) {
            auto* pSound = static_cast<ma_sound*>(pair.second);
            if (pSound) {
                ma_sound_uninit(pSound);
                delete pSound;
            }
        }
        m_activeSounds.clear();

        if (m_engine) {
            ma_engine_uninit(static_cast<ma_engine*>(m_engine));
            delete static_cast<ma_engine*>(m_engine);
            m_engine = nullptr;
        }
    }

    void AudioSystem::update(float dt) {
        if (!m_engine) return;
        auto* pEngine = static_cast<ma_engine*>(m_engine);

        // If not in play mode, stop all active playing sounds and return
        if (!editorMode.isPlaying) {
            for (auto [entity, source] : registry.view<AudioSourceComponent>()) {
                if (source.internalSound && source.isPlaying) {
                    ma_sound_stop(static_cast<ma_sound*>(source.internalSound));
                    source.isPlaying = false;
                    source.wasPlaying = false;
                }
            }
            return;
        }

        // 1. Update 3D Audio Listener
        glm::vec3 listenerPos(0.0f);
        glm::vec3 listenerForward(0.0f, 0.0f, -1.0f);
        glm::vec3 listenerUp(0.0f, 1.0f, 0.0f);
        bool listenerFound = false;

        // Search for explicit active AudioListenerComponent
        for (auto [entity, listener, transform] : registry.view<AudioListenerComponent, Transform>()) {
            if (listener.active) {
                listenerPos = transform.position;
                listenerForward = transform.forward();
                listenerUp = transform.up();
                listenerFound = true;
                break;
            }
        }

        // Fallback to active Camera
        if (!listenerFound) {
            for (auto [entity, camera, transform] : registry.view<Camera, Transform>()) {
                listenerPos = transform.position;
                listenerForward = transform.forward();
                listenerUp = transform.up();
                listenerFound = true;
                break;
            }
        }

        if (listenerFound) {
            ma_engine_listener_set_position(pEngine, 0, listenerPos.x, listenerPos.y, listenerPos.z);
            ma_engine_listener_set_direction(pEngine, 0, listenerForward.x, listenerForward.y, listenerForward.z);
            ma_engine_listener_set_world_up(pEngine, 0, listenerUp.x, listenerUp.y, listenerUp.z);
        }

        // 2. Update and sync all active AudioSourceComponents
        std::vector<std::uint32_t> visitedEntities;

        for (auto [entity, source, transform] : registry.view<AudioSourceComponent, Transform>()) {
            visitedEntities.push_back(entity.getId());

            // Check if we need to load or reload the audio clip
            if (source.clipPath != source.currentLoadedPath) {
                // Unload old sound if present
                if (source.internalSound) {
                    auto* oldSound = static_cast<ma_sound*>(source.internalSound);
                    ma_sound_uninit(oldSound);
                    delete oldSound;
                    source.internalSound = nullptr;
                    m_activeSounds.erase(entity.getId());
                }

                if (!source.clipPath.empty()) {
                    auto* newSound = new ma_sound();
                    // Load sound file dynamically
                    ma_result result = ma_sound_init_from_file(
                        pEngine,
                        source.clipPath.c_str(),
                        0,
                        nullptr,
                        nullptr,
                        newSound
                    );

                    if (result == MA_SUCCESS) {
                        source.internalSound = newSound;
                        source.currentLoadedPath = source.clipPath;
                        m_activeSounds[entity.getId()] = newSound;

                        // Trigger playback if playOnAwake is enabled
                        if (source.playOnAwake) {
                            source.isPlaying = true;
                        }
                    } else {
                        std::cerr << "[AudioSystem] Failed to load audio clip: " << source.clipPath << std::endl;
                        delete newSound;
                        source.currentLoadedPath = "";
                    }
                } else {
                    source.currentLoadedPath = "";
                }
            }

            // Sync property changes and position updates if sound is initialized
            if (source.internalSound) {
                auto* pSound = static_cast<ma_sound*>(source.internalSound);

                // Set 3D Position
                ma_sound_set_position(pSound, transform.position.x, transform.position.y, transform.position.z);

                // Set Doppler velocity if RigidBody exists
                if (auto* rb = registry.get<RigidBodyComponent>(entity)) {
                    ma_sound_set_velocity(pSound, rb->velocity.x, rb->velocity.y, rb->velocity.z);
                } else {
                    ma_sound_set_velocity(pSound, 0.0f, 0.0f, 0.0f);
                }

                // Sync core parameters
                ma_sound_set_volume(pSound, source.volume);
                ma_sound_set_pitch(pSound, source.pitch);
                ma_sound_set_looping(pSound, source.loop ? MA_TRUE : MA_FALSE);
                ma_sound_set_spatialization_enabled(pSound, source.spatialized ? MA_TRUE : MA_FALSE);
                ma_sound_set_min_distance(pSound, source.minDistance);
                ma_sound_set_max_distance(pSound, source.maxDistance);

                // Sync playback play/stop transitions
                if (source.isPlaying && !source.wasPlaying) {
                    ma_sound_seek_to_pcm_frame(pSound, 0);
                    ma_sound_start(pSound);
                    source.wasPlaying = true;
                } else if (!source.isPlaying && source.wasPlaying) {
                    ma_sound_stop(pSound);
                    source.wasPlaying = false;
                }

                // Sync natural completion back to component variables for non-looping clips
                if (source.isPlaying && !source.loop && ma_sound_at_end(pSound)) {
                    source.isPlaying = false;
                    source.wasPlaying = false;
                }
            }
        }

        // 3. Clean up orphaned sounds (entities deleted or components removed)
        for (auto it = m_activeSounds.begin(); it != m_activeSounds.end(); ) {
            std::uint32_t entityId = it->first;
            if (std::find(visitedEntities.begin(), visitedEntities.end(), entityId) == visitedEntities.end()) {
                auto* pSound = static_cast<ma_sound*>(it->second);
                if (pSound) {
                    ma_sound_uninit(pSound);
                    delete pSound;
                }
                it = m_activeSounds.erase(it);
            } else {
                ++it;
            }
        }
    }

} // namespace Engine
