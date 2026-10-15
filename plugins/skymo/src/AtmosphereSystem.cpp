#include "AtmosphereSystem.hpp"
#include "ecs/components/LightComponent.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/AudioSource.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <cstdlib>

namespace {
    bool raySphereIntersect(const glm::vec3& orig, const glm::vec3& dir, float radius, float& t0, float& t1) {
        float b = glm::dot(orig, dir);
        float c = glm::dot(orig, orig) - radius * radius;
        float d = b * b - c;
        if (d < 0.0f) return false;
        float s = std::sqrt(d);
        t0 = -b - s;
        t1 = -b + s;
        return true;
    }

    std::string resolveSoundPath(const std::string& filename, const std::string& exeDir = "") {
        std::vector<std::string> candidates = {
            "plugins/skymo/assets/sounds/" + filename,
            "sandbox_game/plugins/skymo/assets/sounds/" + filename,
            "sandbox_game/build/plugins/skymo/assets/sounds/" + filename,
            "sdk/plugins/skymo/assets/sounds/" + filename,
            "../plugins/skymo/assets/sounds/" + filename,
            "../sdk/plugins/skymo/assets/sounds/" + filename,
            "../sandbox_game/plugins/skymo/assets/sounds/" + filename
        };
        if (!exeDir.empty()) {
            candidates.push_back(exeDir + "/plugins/skymo/assets/sounds/" + filename);
            candidates.push_back(exeDir + "/../plugins/skymo/assets/sounds/" + filename);
            candidates.push_back(exeDir + "/../sandbox_game/plugins/skymo/assets/sounds/" + filename);
        }
        for (const auto& path : candidates) {
            if (std::filesystem::exists(path)) {
                return std::filesystem::absolute(path).lexically_normal().string();
            }
        }
        return "plugins/skymo/assets/sounds/" + filename;
    }
}

AtmosphereSystem::AtmosphereSystem(Registry& reg, class VulkanRenderer& renderer, EditorModeState& editorMode)
    : registry(reg), renderer(renderer), editorMode(editorMode) {
    renderer.setWeatherParamsHook([this]() {
        VulkanRenderer::WeatherParamsInfo info{};
        for (auto [e, weather] : registry.view<WeatherComponent>()) {
            info.wetness = weather.wetness;
            info.rainIntensity = (weather.type == WeatherType::Rain || weather.type == WeatherType::Storm)
                                 ? weather.precipitationIntensity : 0.0f;
            info.puddleLevel = weather.wetness;
            info.time = m_totalTime;
            break;
        }
        return info;
    });
}

AtmosphereSystem::~AtmosphereSystem() {
    renderer.setPrePass(nullptr);
    renderer.setSkyPass(nullptr);
    renderer.setAerialPass(nullptr);
    renderer.setTransmittanceHook(nullptr);
    renderer.setCelestialLightHook(nullptr);
    renderer.setWeatherParamsHook(nullptr);
    cleanupAudio();
    cleanupLightning();
    m_renderFeature.cleanup();
}

void AtmosphereSystem::cleanupAudio() {
    if (registry.isValid(m_rainAudioEntity)) {
        registry.destroy(m_rainAudioEntity);
        m_rainAudioEntity = Entity{};
    }
    if (registry.isValid(m_closeThunderAudioEntity)) {
        registry.destroy(m_closeThunderAudioEntity);
        m_closeThunderAudioEntity = Entity{};
    }
    if (registry.isValid(m_distantThunderAudioEntity)) {
        registry.destroy(m_distantThunderAudioEntity);
        m_distantThunderAudioEntity = Entity{};
    }
    m_pendingThunder.clear();
}

void AtmosphereSystem::cleanupLightning() {
    if (registry.isValid(m_lightningLightEntity)) {
        registry.destroy(m_lightningLightEntity);
        m_lightningLightEntity = Entity{};
    }
}

void AtmosphereSystem::updateLightningLight(float flashIntensity) {
    if (flashIntensity <= 0.001f) {
        if (registry.isValid(m_lightningLightEntity)) {
            if (auto* light = registry.get<Engine::LightComponent>(m_lightningLightEntity)) {
                light->intensity = 0.0f;
            }
        }
        return;
    }

    if (!registry.isValid(m_lightningLightEntity)) {
        m_lightningLightEntity = registry.create();
        registry.emplace<Transform>(m_lightningLightEntity, Transform{});
        auto& light = registry.emplace<Engine::LightComponent>(m_lightningLightEntity, Engine::LightComponent{});
        light.type = Engine::LightType::Point;
        light.range = 450.0f;
        light.color = glm::vec3(0.85f, 0.95f, 1.35f);
        light.castShadows = false;
    }

    if (auto* t = registry.get<Transform>(m_lightningLightEntity)) {
        t->position = m_lightningGroundPos + glm::vec3(0.0f, 25.0f, 0.0f);
    }
    if (auto* light = registry.get<Engine::LightComponent>(m_lightningLightEntity)) {
        light->intensity = flashIntensity * 35.0f;
    }
}

void AtmosphereSystem::onEditorUpdate(float dt) {
    update(dt);
}

void AtmosphereSystem::updateWeather(float dt, WeatherComponent* weather) {
    if (!weather) {
        m_activeLightningFlash = 0.0f;
        return;
    }

    // 1. Detect preset / type change in Inspector
    if (weather->type != weather->lastAppliedType) {
        weather->applyWeatherType(weather->type);
        weather->lastAppliedType = weather->type;
    }

    // 2. Automatic ground wetness accumulation & evaporation
    if (weather->autoWetness) {
        if (weather->type == WeatherType::Rain || weather->type == WeatherType::Storm) {
            weather->wetness = std::min(1.0f, weather->wetness + weather->precipitationIntensity * weather->wetnessAccumSpeed * dt);
        } else {
            weather->wetness = std::max(0.0f, weather->wetness - weather->dryingSpeed * dt);
        }
    }

    // 3. Lightning simulation
    if (weather->type == WeatherType::Storm && weather->enableLightning) {
        weather->lightningTimer -= dt;
        if (weather->lightningTimer <= 0.0f) {
            float baseInterval = std::max(2.0f, 20.0f / std::max(0.2f, weather->lightningFrequency));
            float jitter = (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * (baseInterval * 0.5f);
            weather->lightningTimer = std::max(1.5f, baseInterval + jitter);

            // Procedural strike type:
            // 50% Cloud-to-Ground (CG) stepped leader
            // 35% Cloud-to-Cloud (CC) Anvil Crawler
            // 15% Intra-Cloud (IC) Sheet Discharge
            int typeRoll = rand() % 100;
            if (typeRoll < 50) {
                m_lightningType = 0; // CG
            } else if (typeRoll < 85) {
                m_lightningType = 1; // CC Anvil Crawler
            } else {
                m_lightningType = 2; // IC Sheet
            }

            weather->flashDuration = 0.0f;
            weather->flashBurstsRemaining = (m_lightningType == 1) ? (2 + (rand() % 3)) : (3 + (rand() % 3));
            weather->nextBurstTimer = 0.0f;

            // Realistic open-world scale:
            // 15% Near / dangerous strikes: 800m - 1,800m (blinding camera flash, direct thunderclap)
            // 35% Mid-range strikes: 2,500m - 5,500m (rolling thunder, moderate ambient flash)
            // 50% Distant horizon strikes: 6,000m - 15,000m (NO camera blinding, deep rolling rumble)
            float dist = 0.0f;
            int distRoll = rand() % 100;
            if (distRoll < 15) {
                dist = 800.0f + static_cast<float>(rand() % 1000);
            } else if (distRoll < 50) {
                dist = 2500.0f + static_cast<float>(rand() % 3000);
            } else {
                dist = 6000.0f + static_cast<float>(rand() % 9000);
            }

            bool isDistant = dist > 2000.0f;
            weather->isDistantThunder = isDistant;

            // Physical & acoustic scaling:
            // Close strikes (< 2,000m): immediate sharp violent crack, full volume
            // Distant strikes (2,000m - 15,000m):
            //   - Realistic noticeable delay: 3.5s at 2km up to 12.0s at 15km (replaces previous rushed 0.9-2s)
            //   - Toned-down volume: air distance attenuation cuts volume significantly (0.42 down to 0.18)
            //   - Low-frequency dominance: pitch is lowered to 0.52 - 0.70, which suppresses high frequencies
            //     and preserves the deep rolling sub-bass rumble of distant atmospheric thunder
            float thunderDelay = 0.0f;
            float thunderVol = weather->thunderVolume;
            float thunderPitch = 1.0f;

            if (!isDistant) {
                thunderDelay = 0.35f + (dist / 2000.0f) * 1.25f; // 0.35s - 1.6s
                thunderVol = weather->thunderVolume * (0.85f + (1.0f - dist / 2000.0f) * 0.15f);
                thunderPitch = 0.96f + (static_cast<float>(rand() % 8) / 100.0f);
            } else {
                float tDist = std::clamp((dist - 2000.0f) / 13000.0f, 0.0f, 1.0f);
                thunderDelay = 3.5f + tDist * 8.5f; // 3.5s up to 12.0s delay!
                thunderVol = weather->thunderVolume * (0.42f - tDist * 0.24f); // 0.42 down to 0.18 (toned down!)
                thunderPitch = 0.68f - tDist * 0.16f + (static_cast<float>(rand() % 6) / 100.0f); // 0.52 - 0.70 (deep low-pass roll)
            }

            weather->thunderAudioDelay = thunderDelay;
            m_pendingThunder.push_back({ thunderDelay, isDistant, thunderVol, thunderPitch });
            m_lightningDistance = dist;

            glm::vec3 camPos = renderer.getActiveCameraPosition();

            // Compute camera forward heading on the XZ plane
            glm::mat4 invViewProj = glm::inverse(renderer.getActiveCameraViewProj());
            glm::vec4 centerNdc = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            glm::vec4 centerWorldH = invViewProj * centerNdc;
            glm::vec3 camFwd = (centerWorldH.w != 0.0f)
                ? glm::normalize(glm::vec3(centerWorldH) / centerWorldH.w - camPos)
                : glm::vec3(0.0f, 0.0f, -1.0f);

            glm::vec2 fwd2D = glm::vec2(camFwd.x, camFwd.z);
            float fwdLen = glm::length(fwd2D);
            float baseAngle = (fwdLen > 0.001f) ? std::atan2(fwd2D.y, fwd2D.x) : 0.0f;

            // 80% chance strike spawns in camera forward view cone (+- 38 deg), 20% anywhere around 360 deg
            float angle = 0.0f;
            if ((rand() % 100) < 80) {
                float fovOffset = (static_cast<float>(rand() % 1000) / 1000.0f - 0.5f) * 1.32f; // +- ~38 degrees
                angle = baseAngle + fovOffset;
            } else {
                angle = static_cast<float>(rand() % 6283) / 1000.0f;
            }

            float cloudAlt = 1300.0f + static_cast<float>(rand() % 900); // 1300m - 2200m cloud base

            if (m_lightningType == 0) {
                // Cloud-to-Ground: strikes the terrain
                m_lightningGroundPos = glm::vec3(camPos.x + std::cos(angle) * dist, 0.0f, camPos.z + std::sin(angle) * dist);
                float driftX = static_cast<float>(rand() % 400) - 200.0f;
                float driftZ = static_cast<float>(rand() % 400) - 200.0f;
                m_lightningCloudPos = glm::vec3(m_lightningGroundPos.x + driftX, cloudAlt, m_lightningGroundPos.z + driftZ);
            } else if (m_lightningType == 1) {
                // Cloud-to-Cloud Anvil Crawler: travels horizontally across the cloud deck!
                float crawlerSpan = 3000.0f + static_cast<float>(rand() % 4500);
                float crawlerAngle = angle + 1.5708f + (static_cast<float>(rand() % 1000) / 1000.0f - 0.5f) * 0.8f;
                m_lightningGroundPos = glm::vec3(camPos.x + std::cos(angle) * dist, cloudAlt * 0.95f, camPos.z + std::sin(angle) * dist);
                m_lightningCloudPos = glm::vec3(
                    m_lightningGroundPos.x + std::cos(crawlerAngle) * crawlerSpan,
                    cloudAlt * (1.0f + (static_cast<float>(rand() % 30) - 15.0f) / 100.0f),
                    m_lightningGroundPos.z + std::sin(crawlerAngle) * crawlerSpan
                );
            } else {
                // Intra-Cloud Sheet Discharge: high inside thunderhead
                m_lightningGroundPos = glm::vec3(camPos.x + std::cos(angle) * dist, cloudAlt * 1.10f, camPos.z + std::sin(angle) * dist);
                float spanX = (static_cast<float>(rand() % 200) - 100.0f) * 15.0f;
                float spanZ = (static_cast<float>(rand() % 200) - 100.0f) * 15.0f;
                m_lightningCloudPos = glm::vec3(m_lightningGroundPos.x + spanX, cloudAlt * 1.50f, m_lightningGroundPos.z + spanZ);
            }
        }

        if (weather->flashBurstsRemaining > 0) {
            weather->flashDuration += dt;
            weather->nextBurstTimer -= dt;

            if (weather->nextBurstTimer <= 0.0f) {
                weather->flashIntensity = (0.85f + static_cast<float>(rand() % 35) / 100.0f) * weather->lightningIntensity;
                weather->flashBurstsRemaining--;
                weather->nextBurstTimer = 0.08f + (static_cast<float>(rand() % 60) / 1000.0f);
            } else {
                weather->flashIntensity = std::max(0.0f, weather->flashIntensity - dt * 6.0f);
            }
        } else {
            weather->flashIntensity = std::max(0.0f, weather->flashIntensity - dt * 4.5f);
        }

        // Process queued thunder audio events (prevents strikes from overwriting each other)
        for (auto it = m_pendingThunder.begin(); it != m_pendingThunder.end(); ) {
            it->delay -= dt;
            if (it->delay <= 0.0f) {
                triggerThunderAudio(it->distant, it->volume, it->pitch);
                it = m_pendingThunder.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        weather->flashIntensity = 0.0f;
        weather->flashBurstsRemaining = 0;
        weather->thunderAudioDelay = -1.0f;
        m_pendingThunder.clear();
    }

    m_activeLightningFlash = weather->flashIntensity;
    updateLightningLight(weather->flashIntensity);
}

void AtmosphereSystem::updateWeatherAudio(const WeatherComponent* weather) {
    if (!weather || !weather->enableAudio) {
        if (registry.isValid(m_rainAudioEntity)) {
            if (auto* audio = registry.get<AudioSourceComponent>(m_rainAudioEntity)) {
                audio->isPlaying = false;
            }
        }
        return;
    }

    std::string exeDir = renderer.getExeDir();
    if (!registry.isValid(m_rainAudioEntity)) {
        m_rainAudioEntity = registry.create();
        registry.emplace<Transform>(m_rainAudioEntity, Transform{});
        auto& audio = registry.emplace<AudioSourceComponent>(m_rainAudioEntity, AudioSourceComponent{});
        audio.clipPath = resolveSoundPath("rain_loop.wav", exeDir);
        audio.loop = true;
        audio.spatialized = false;
        audio.playOnAwake = false;
        audio.isPlaying = false;
    }

    std::string closeFile = "thunder_strike.mp3";
    std::string resolvedClose = resolveSoundPath(closeFile, exeDir);
    if (!std::filesystem::exists(resolvedClose)) {
        resolvedClose = resolveSoundPath("thunder_strike.wav", exeDir);
    }
    std::string resolvedDistant = resolveSoundPath("thunder_distant.wav", exeDir);

    if (!registry.isValid(m_closeThunderAudioEntity)) {
        m_closeThunderAudioEntity = registry.create();
        registry.emplace<Transform>(m_closeThunderAudioEntity, Transform{});
        auto& audio = registry.emplace<AudioSourceComponent>(m_closeThunderAudioEntity, AudioSourceComponent{});
        audio.clipPath = resolvedClose;
        audio.loop = false;
        audio.spatialized = false;
        audio.playOnAwake = false;
        audio.isPlaying = false;
    }

    if (!registry.isValid(m_distantThunderAudioEntity)) {
        m_distantThunderAudioEntity = registry.create();
        registry.emplace<Transform>(m_distantThunderAudioEntity, Transform{});
        auto& audio = registry.emplace<AudioSourceComponent>(m_distantThunderAudioEntity, AudioSourceComponent{});
        audio.clipPath = resolvedDistant;
        audio.loop = false;
        audio.spatialized = false;
        audio.playOnAwake = false;
        audio.isPlaying = false;
    }

    glm::vec3 camPos = renderer.getActiveCameraPosition();
    if (auto* t = registry.get<Transform>(m_rainAudioEntity)) {
        t->position = camPos;
    }
    if (auto* t = registry.get<Transform>(m_closeThunderAudioEntity)) {
        t->position = camPos;
    }
    if (auto* t = registry.get<Transform>(m_distantThunderAudioEntity)) {
        t->position = camPos;
    }

    if (auto* audio = registry.get<AudioSourceComponent>(m_rainAudioEntity)) {
        bool isRaining = (weather->type == WeatherType::Rain || weather->type == WeatherType::Storm) && weather->precipitationIntensity > 0.01f;
        if (isRaining) {
            audio->volume = weather->rainVolume * std::clamp(weather->precipitationIntensity, 0.15f, 1.0f);
            if (!audio->isPlaying) {
                audio->isPlaying = true;
                audio->wasPlaying = false;
            }
        } else {
            if (audio->isPlaying) {
                audio->isPlaying = false;
            }
        }
    }
}

void AtmosphereSystem::triggerThunderAudio(bool distant, float volume, float pitch) {
    Entity targetEntity = distant ? m_distantThunderAudioEntity : m_closeThunderAudioEntity;
    if (!registry.isValid(targetEntity)) return;

    if (auto* audio = registry.get<AudioSourceComponent>(targetEntity)) {
        audio->volume = volume;
        audio->pitch = pitch;
        audio->isPlaying = true;
        audio->wasPlaying = false;
    }
}

void AtmosphereSystem::update(float dt) {
    m_totalTime += dt;

    const AtmosphereComponent* atmoComp = nullptr;
    for (auto [e, atmo] : registry.view<AtmosphereComponent>()) {
        atmoComp = &atmo;
        break;
    }

    bool fogComponentExists = false;
    bool fogEnabled = false;
    VolumetricFogComponent fogSettings{};

    for (auto [e, fog] : registry.view<VolumetricFogComponent>()) {
        fogComponentExists = true;
        bool isCompEnabled = registry.isComponentEnabled<VolumetricFogComponent>(e);
        if (isCompEnabled && fog.enabled) {
            fogSettings = fog;
            fogEnabled = true;
            break;
        }
    }

    // Query WeatherComponent
    WeatherComponent* weatherComp = nullptr;
    for (auto [e, weather] : registry.view<WeatherComponent>()) {
        weatherComp = registry.get<WeatherComponent>(e);
        break;
    }
    updateWeather(dt, weatherComp);
    updateWeatherAudio(weatherComp);

    WeatherComponent weatherSettings = weatherComp ? *weatherComp : WeatherComponent{};
    bool weatherEnabled = (weatherComp != nullptr);

    // If neither AtmosphereComponent, active VolumetricFogComponent, nor WeatherComponent exists, unhook everything and return
    if (!atmoComp && !fogEnabled && !weatherEnabled) {
        renderer.setPrePass(nullptr);
        renderer.setSkyPass(nullptr);
        renderer.setAerialPass(nullptr);
        renderer.setTransmittanceHook(nullptr);
        renderer.setCelestialLightHook(nullptr);
        return;
    }

    if (!m_renderFeature.isInitialized()) {
        m_renderFeature.init(renderer);
    }

    static bool s_lastFogEnabled = false;
    static float s_lastMaxDist = -1.0f;
    static float s_lastStartDist = -1.0f;
    static float s_lastDensity = -1.0f;
    static glm::vec3 s_lastTint = glm::vec3(-1.0f);
    static FogMode s_lastMode = FogMode::AtmosphericHaze;
    static bool s_lastAffectSky = false;
    static float s_lastBaseHeight = -1.0f;
    static float s_lastHeightFalloff = -1.0f;
    static float s_lastGroundAttenuation = -1.0f;
    if (fogEnabled != s_lastFogEnabled || 
        (fogEnabled && (fogSettings.maxDistance != s_lastMaxDist || 
                        fogSettings.startDistance != s_lastStartDist ||
                        fogSettings.densityMultiplier != s_lastDensity ||
                        fogSettings.fogColorTint != s_lastTint ||
                        fogSettings.mode != s_lastMode ||
                        fogSettings.affectSky != s_lastAffectSky ||
                        fogSettings.baseHeight != s_lastBaseHeight ||
                        fogSettings.heightFalloff != s_lastHeightFalloff ||
                        fogSettings.groundSunAttenuation != s_lastGroundAttenuation))) {
        s_lastFogEnabled = fogEnabled;
        s_lastMaxDist = fogSettings.maxDistance;
        s_lastStartDist = fogSettings.startDistance;
        s_lastDensity = fogSettings.densityMultiplier;
        s_lastTint = fogSettings.fogColorTint;
        s_lastMode = fogSettings.mode;
        s_lastAffectSky = fogSettings.affectSky;
        s_lastBaseHeight = fogSettings.baseHeight;
        s_lastHeightFalloff = fogSettings.heightFalloff;
        s_lastGroundAttenuation = fogSettings.groundSunAttenuation;
        std::cout << "[AtmosphereSystem] Fog state changed: enabled=" << (fogEnabled ? "true" : "false");
        if (fogEnabled) {
            std::cout << ", mode=" << (fogSettings.mode == FogMode::AtmosphericHaze ? "AtmosphericHaze" : "DenseFog")
                      << ", affectSky=" << (fogSettings.affectSky ? "true" : "false")
                      << ", density=" << fogSettings.densityMultiplier
                      << ", baseHeight=" << fogSettings.baseHeight
                      << ", heightFalloff=" << fogSettings.heightFalloff
                      << ", groundAttenuation=" << fogSettings.groundSunAttenuation
                      << ", maxDist=" << fogSettings.maxDistance
                      << ", tint=[" << fogSettings.fogColorTint.r << ", "
                      << fogSettings.fogColorTint.g << ", " << fogSettings.fogColorTint.b << "]";
        }
        std::cout << std::endl;
    }

    // Default sun direction (pointing up towards the sky)
    glm::vec3 sunDir(0.0f, 1.0f, 0.0f);
    glm::vec3 sunColor(1.0f, 1.0f, 1.0f);
    float sunIntensity = 1.0f;

    for (auto [e, light] : registry.view<Engine::LightComponent>()) {
        if (light.type == Engine::LightType::Directional) {
            sunColor = glm::vec3(light.color);
            sunIntensity = light.intensity;
            // Direction the light shines is light.direction.
            // Sun vector in the sky points towards the sun: -light.direction.
            if (glm::length(light.direction) > 0.0001f) {
                sunDir = glm::normalize(-light.direction);
            } else {
                sunDir = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            break;
        }
    }

    // Register transmittance evaluation hook with the renderer
    renderer.setTransmittanceHook([this](float camAltitude, const glm::vec3& sunDir) {
        return evaluateTransmittanceToSun(camAltitude, sunDir);
    });

    // Register celestial lighting hook:
    // 1. High-intensity directional lightning flash burst during close storm strikes (<1500m).
    //    Distant strikes are still rendered as bolts but don't override the sun/moon.
    // 2. Automatic day/night handover (Moonlight & shadows when sun is below horizon)
    float lightningFlashCopy = m_activeLightningFlash;
    bool isCloseLightning = (lightningFlashCopy > 0.05f && m_lightningDistance < 1500.0f);
    if (isCloseLightning) {
        renderer.setCelestialLightHook([lightningFlashCopy](const glm::vec3& sunDir, const glm::vec3& sunColor, float sunIntensity) -> VulkanRenderer::CelestialLightInfo {
            VulkanRenderer::CelestialLightInfo info{};
            info.overrideDirectional = true;
            info.direction = glm::normalize(glm::vec3(0.08f, -0.99f, 0.06f));
            info.color = glm::vec3(0.92f, 0.96f, 1.05f); // pure cool plasma
            info.intensity = 8.5f * lightningFlashCopy;
            return info;
        });
    } else if (atmoComp && atmoComp->enableMoon) {
        AtmosphereComponent atmoCopy = *atmoComp;
        renderer.setCelestialLightHook([atmoCopy](const glm::vec3& sunDir, const glm::vec3& sunColor, float sunIntensity) -> VulkanRenderer::CelestialLightInfo {
            VulkanRenderer::CelestialLightInfo info{};
            // If the sun is below the horizon, switch primary directional light and shadows to the Moon!
            if (sunDir.y < -0.02f) {
                info.overrideDirectional = true;
                glm::vec3 mDir(0.0f, -1.0f, 0.0f);
                if (atmoCopy.autoMoonDirection) {
                    mDir = -sunDir;
                } else if (glm::length(atmoCopy.moonDirection) > 0.001f) {
                    mDir = glm::normalize(atmoCopy.moonDirection);
                } else {
                    mDir = -sunDir;
                }
                if (glm::length(mDir) > 0.001f) mDir = glm::normalize(mDir);

                // Light shining from the Moon travels along -mDir (pointing downwards towards the earth)
                info.direction = -mDir;
                // Soft cool moonlight (subtle cyan/blue tint) scaled by moonIntensity
                info.color = glm::vec3(0.65f, 0.82f, 1.0f);
                info.intensity = 0.15f * atmoCopy.moonIntensity;
            } else {
                info.overrideDirectional = false;
            }
            return info;
        });
    } else {
        renderer.setCelestialLightHook(nullptr);
    }

    // Query VolumetricCloudComponent
    VolumetricCloudComponent cloudSettings{};
    bool cloudEnabled = false;
    for (auto [e, cloud] : registry.view<VolumetricCloudComponent>()) {
        auto* cloudComp = registry.get<VolumetricCloudComponent>(e);
        if (cloudComp) {
            if (cloudComp->preset != CloudPreset::Custom && cloudComp->preset != cloudComp->lastAppliedPreset) {
                cloudComp->applyPreset(cloudComp->preset);
                cloudComp->lastAppliedPreset = cloudComp->preset;
            }
            cloudSettings = *cloudComp;
            cloudEnabled = cloudComp->enabled;
        }
        break;
    }

    // 1. Hook prePass for compute LUT generation (dispatched outside active render pass)
    AtmosphereComponent compCopy = atmoComp ? *atmoComp : AtmosphereComponent{};
    renderer.setPrePass([this, compCopy, sunDir, sunColor, sunIntensity, fogSettings, fogEnabled](VkCommandBuffer cmd) {
        glm::vec3 camPos = renderer.getActiveCameraPosition();
        glm::mat4 invViewProj = glm::inverse(renderer.getActiveCameraViewProj());
        const auto& shadowData = renderer.getCascadeShadowData();
        m_renderFeature.executeLUTPasses(cmd, compCopy, sunDir, sunColor, sunIntensity, camPos, invViewProj, fogEnabled ? &fogSettings : nullptr, &shadowData);
    });

    // 2. Hook skyPass for fullscreen background composition (rendered inside active render pass after geometry)
    // Only execute physical skyPass if AtmosphereComponent is present in the scene
    if (atmoComp) {
        float timeCopy = m_totalTime;
        // Distant strikes still create subtle sheet lightning in distant clouds without blinding the whole screen
        float cloudFlash = (m_lightningDistance < 1500.0f) ? m_activeLightningFlash
            : (m_activeLightningFlash * std::max(0.12f, 1.0f - (m_lightningDistance - 1500.0f) / 8000.0f));
        renderer.setSkyPass([this, compCopy, timeCopy, sunDir, cloudSettings, cloudEnabled, weatherSettings, weatherEnabled, cloudFlash](VkCommandBuffer cmd) {
            glm::mat4 invViewProj = glm::inverse(renderer.getActiveCameraViewProj());
            glm::vec3 camPos = renderer.getActiveCameraPosition();
            m_renderFeature.executeCompositionPass(cmd, invViewProj, camPos, sunDir, &compCopy, timeCopy, 1.0f,
                cloudEnabled ? &cloudSettings : nullptr,
                weatherEnabled ? &weatherSettings : nullptr,
                cloudFlash);
        });
    } else {
        renderer.setSkyPass(nullptr);
    }

    // 3. Hook aerialPass for aerial perspective / volumetric fog, cloud shadows, 3D precipitation, and 3D lightning
    bool cloudShadowsEnabled = cloudEnabled && cloudSettings.castShadows && cloudSettings.coverage > 0.001f;
    bool weatherActive = weatherEnabled && (weatherSettings.type != WeatherType::Clear || weatherSettings.wetness > 0.001f || m_activeLightningFlash > 0.001f);
    if (fogEnabled || cloudShadowsEnabled || weatherActive) {
        float timeCopy = m_totalTime;

        // Ambient screen flash:
        // Close strikes (< 1500m) illuminate the camera/screen fully (blinding flash).
        // Distant strikes (> 1500m) smoothly fall off to 0 so the camera view is NOT blinded.
        float distAmbientFactor = (m_lightningDistance < 1500.0f) ? 1.0f
            : std::max(0.0f, 1.0f - (m_lightningDistance - 1500.0f) / 2500.0f);
        float ambientFlash = m_activeLightningFlash * distAmbientFactor;

        // The lightning bolt mesh itself stays white-hot and brilliant at any distance!
        float boltIntensity = m_activeLightningFlash;
        uint32_t currentType = m_lightningType;

        glm::vec3 strikeGnd = m_lightningGroundPos;
        glm::vec3 strikeCld = m_lightningCloudPos;
        renderer.setAerialPass([this, fogSettings, fogEnabled, cloudSettings, cloudShadowsEnabled, weatherSettings, weatherActive, sunDir, timeCopy, ambientFlash, boltIntensity, currentType, strikeGnd, strikeCld](VkCommandBuffer cmd) {
            glm::mat4 invViewProj = glm::inverse(renderer.getActiveCameraViewProj());
            glm::mat4 viewProj = renderer.getActiveCameraViewProj();
            glm::vec3 camPos = renderer.getActiveCameraPosition();
            m_renderFeature.executeAerialPass(cmd, invViewProj, camPos, sunDir,
                fogEnabled ? &fogSettings : nullptr,
                cloudShadowsEnabled ? &cloudSettings : nullptr,
                timeCopy,
                weatherActive ? &weatherSettings : nullptr,
                ambientFlash,
                viewProj,
                strikeGnd,
                strikeCld,
                boltIntensity,
                currentType);
        });
    } else {
        renderer.setAerialPass(nullptr);
    }
}


glm::vec3 AtmosphereSystem::evaluateTransmittanceToSun(float camAltitude, const glm::vec3& sunDir) {
    const AtmosphereComponent* atmoComp = nullptr;
    for (auto [e, atmo] : registry.view<AtmosphereComponent>()) {
        atmoComp = &atmo;
        break;
    }

    if (!atmoComp) {
        return glm::vec3(1.0f);
    }

    float bottomR = atmoComp->bottomRadius;
    float topR = atmoComp->topRadius;

    float r = bottomR + std::max(0.0f, camAltitude);
    glm::vec3 pos(0.0f, r, 0.0f);
    float t0, t1;

    // Check if the ray hits the Earth (ground occlusion)
    if (raySphereIntersect(pos, sunDir, bottomR, t0, t1) && t0 > 0.0f) {
        return glm::vec3(0.0f); // Sun is blocked by the planet body
    }

    // Find exit distance to top of atmosphere
    if (!raySphereIntersect(pos, sunDir, topR, t0, t1) || t1 <= 0.0f) {
        return glm::vec3(1.0f); // Outside atmosphere
    }
    float rayLength = t1;

    // 2. Numerical integration (16 steps is plenty of precision on CPU)
    constexpr int kSteps = 16;
    float dt = rayLength / static_cast<float>(kSteps);
    glm::vec3 opticalDepth(0.0f);

    glm::vec3 rayleighExt = atmoComp->rayleighScattering + atmoComp->rayleighAbsorption;
    glm::vec3 mieExt = atmoComp->mieScattering + atmoComp->mieAbsorption;
    glm::vec3 ozoneExt = atmoComp->absorptionExtinction;

    for (int i = 0; i < kSteps; ++i) {
        // Sample at segment midpoint
        float t = (static_cast<float>(i) + 0.5f) * dt;
        glm::vec3 samplePos = pos + sunDir * t;
        float sampleAltitude = glm::length(samplePos) - bottomR;
        if (sampleAltitude < 0.0f) {
            return glm::vec3(0.0f);
        }

        // Density profiles:
        // Rayleigh: exponential decay (scale height ~ 8 km)
        float densityR = std::exp(-sampleAltitude / 8000.0f);
        // Mie: exponential decay (scale height ~ 1.2 km)
        float densityM = std::exp(-sampleAltitude / 1200.0f);
        // Ozone: triangular/tent profile peaked around 25 km
        float densityO = std::max(0.0f, 1.0f - std::abs(sampleAltitude - 25000.0f) / 15000.0f);

        // Accumulate optical thickness: tau += (sigma * density) * dt
        opticalDepth += (rayleighExt * densityR +
                         mieExt * densityM +
                         ozoneExt * densityO) * dt;
    }

    // 3. Atmospheric Beer-Lambert attenuation: T = exp(-tau)
    glm::vec3 atmoTransmittance(
        std::exp(-opticalDepth.r),
        std::exp(-opticalDepth.g),
        std::exp(-opticalDepth.b)
    );

    // 4. Physical ground sunlight attenuation through volumetric fog:
    // If fog is present, direct directional sunlight reaching ground meshes is softly extinguished.
    for (auto [e, fog] : registry.view<VolumetricFogComponent>()) {
        bool isCompEnabled = registry.isComponentEnabled<VolumetricFogComponent>(e);
        if (isCompEnabled && fog.enabled && fog.densityMultiplier > 0.00001f && fog.groundSunAttenuation > 0.001f) {
            float sunAngleCos = std::max(0.05f, sunDir.y);
            float fogScaleHeight = 1.0f / std::max(0.001f, fog.heightFalloff);
            float baseExtinction = (fog.mode == FogMode::DenseFog ? 3.5f : 1.2f) / std::max(10.0f, fog.maxDistance);
            float tauFog = (baseExtinction * fog.densityMultiplier * fogScaleHeight * fog.groundSunAttenuation) / sunAngleCos;
            float fogTransmittance = std::exp(-std::clamp(tauFog, 0.0f, 10.0f));
            atmoTransmittance *= fogTransmittance;
            break;
        }
    }

    return atmoTransmittance;
}