#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "editor/EditorModeState.hpp"
#include "AtmosphereComponent.hpp"
#include "AtmosphereRenderFeature.hpp"
#include "WeatherComponent.hpp"

// [ReflectClass]
class AtmosphereSystem : public System {
public:
    const char* getName() const override { return "AtmosphereSystem"; }

    AtmosphereSystem(Registry& reg, class VulkanRenderer& renderer, EditorModeState& editorMode);
    ~AtmosphereSystem() override;

    void update(float dt) override;
    void onEditorUpdate(float dt) override;

    AtmosphereRenderFeature& getRenderFeature() { return m_renderFeature; }

    /**
     * @brief Computes physical atmospheric transmittance T(lambda) from camera position towards the sun.
     * @param camAltitude Altitude in meters above sea level (e.g. 0.0f to 10000.0f).
     * @param sunDir Normalized vector pointing TOWARDS the sun (e.g. normalize(-light.direction)).
     * @return RGB transmittance in [0.0, 1.0].
     */
    glm::vec3 evaluateTransmittanceToSun(float camAltitude, const glm::vec3& sunDir);

private:
    void updateWeather(float dt, WeatherComponent* weather);
    void updateWeatherAudio(const WeatherComponent* weather);
    void triggerThunderAudio(bool distant, float volume, float pitch);
    void cleanupAudio();

    Registry& registry;
    class VulkanRenderer& renderer;
    EditorModeState& editorMode;
    AtmosphereRenderFeature m_renderFeature;
    float m_totalTime = 0.0f;

    struct PendingThunder {
        float delay = 0.0f;
        bool distant = false;
        float volume = 1.0f;
        float pitch = 1.0f;
    };
    std::vector<PendingThunder> m_pendingThunder;

    Entity m_rainAudioEntity;
    Entity m_closeThunderAudioEntity;
    Entity m_distantThunderAudioEntity;
    Entity m_lightningLightEntity;
    float m_activeLightningFlash = 0.0f;
    float m_lightningDistance = 999.0f;   // metres to the current strike; drives flash scaling
    uint32_t m_lightningType = 0;         // 0 = Cloud-to-Ground, 1 = Cloud-to-Cloud Anvil Crawler, 2 = Intra-Cloud Sheet
    glm::vec3 m_lightningGroundPos{0.0f};
    glm::vec3 m_lightningCloudPos{0.0f};

    void updateLightningLight(float flashIntensity);
    void cleanupLightning();
};

