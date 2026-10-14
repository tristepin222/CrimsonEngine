#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "editor/EditorModeState.hpp"
#include "core/EngineAPI.hpp"

#include <unordered_map>

namespace Engine {

    /**
     * @class AudioSystem
     * @brief System responsible for managing 3D spatialized and 2D audio playback.
     */
    class ENGINE_API AudioSystem : public System {
    public:
        const char* getName() const override { return "AudioSystem"; }
        SystemStage getStage() const override { return SystemStage::PostUpdate; }
        bool isParallelSafe() const override { return true; }

        AudioSystem(Registry& reg, EditorModeState& editorMode);
        virtual ~AudioSystem();

        virtual void update(float dt) override;


    private:
        Registry& registry;
        EditorModeState& editorMode;
        void* m_engine = nullptr; // Pointer to ma_engine
        std::unordered_map<std::uint32_t, void*> m_activeSounds; // Entity ID -> ma_sound*
    };

} // namespace Engine
