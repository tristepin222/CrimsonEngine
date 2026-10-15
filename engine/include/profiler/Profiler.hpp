#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <algorithm>
#include <cstdint>
#include <cstring>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#endif

#ifndef ENGINE_API
#ifdef _WIN32
    #ifdef ENGINE_EXPORTS
        #define ENGINE_API __declspec(dllexport)
    #else
        #define ENGINE_API __declspec(dllimport)
    #endif
#else
    #define ENGINE_API
#endif
#endif

namespace Engine {

    inline const char* toCString(const char* s) { return s; }
    inline const char* toCString(const std::string& s) { return s.c_str(); }

    /**
     * @enum ProfileCategory
     * @brief Unity-style categories for color-coded timeline tracking and comparative analysis.
     */
    enum class ProfileCategory {
        Rendering,
        Physics,
        Systems,
        ECS_Core,
        Editor_UI,
        Custom
    };

    /**
     * @struct ProfileSample
     * @brief A single execution timing sample for a system or code block.
     */
    struct ProfileSample {
        std::string name;
        double durationMs = 0.0;
        uint32_t calls = 1;
        ProfileCategory category = ProfileCategory::Custom;
    };

    /**
     * @struct SystemStats
     * @brief Accumulated statistical metrics for a system across frame history.
     */
    struct SystemStats {
        std::string name;
        double lastMs = 0.0;
        double avgMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
        double percentShare = 0.0;
        ProfileCategory category = ProfileCategory::Systems;
    };

    /**
     * @struct FunctionStats
     * @brief Accumulated statistical metrics for a specific function/scope across frame history.
     */
    struct FunctionStats {
        std::string name;
        uint32_t avgCallsPerFrame = 0;
        double lastMs = 0.0;
        double avgMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
        double percentShare = 0.0;
        ProfileCategory category = ProfileCategory::Custom;
    };

    /**
     * @struct RenderStats
     * @brief Real-time rendering and scene metrics for a frame.
     */
    struct RenderStats {
        uint32_t drawCalls = 0;
        uint32_t triangleCount = 0;
        uint32_t vertexCount = 0;
        uint32_t entityCount = 0;
        uint32_t componentCount = 0;
        size_t meshMemoryBytes = 0;
    };

    /**
     * @struct FrameProfileData
     * @brief Complete profiling snapshot for a single engine frame.
     */
    struct FrameProfileData {
        uint64_t frameIndex = 0;
        double frameTimeMs = 0.0;
        float fps = 0.0f;
        double renderingMs = 0.0;
        double physicsMs = 0.0;
        double systemsMs = 0.0;
        double ecsCoreMs = 0.0;
        double editorUiMs = 0.0;
        double customMs = 0.0;
        std::vector<ProfileSample> systemSamples;
        std::vector<ProfileSample> customSamples;
        RenderStats renderStats;
    };

    /**
     * @class Profiler
     * @brief Central high-precision profiling manager tracking frame timings, ECS systems, and render stats.
     */
    class ENGINE_API Profiler {
    public:
        static Profiler& getInstance();

        Profiler(const Profiler&) = delete;
        Profiler& operator=(const Profiler&) = delete;

        /** @brief Begins timing a new frame. */
        void beginFrame();

        /** @brief Finalizes timing for the current frame and pushes to history ring buffer. */
        void endFrame();

        /** @brief Records a system timing sample for the current frame. */
        void recordSystemSample(const std::string& systemName, double durationMs);

        /** @brief Records a custom code scope timing sample for the current frame. */
        void recordCustomSample(const std::string& scopeName, double durationMs, ProfileCategory category = ProfileCategory::Custom);

        /** @brief Updates the rendering and scene statistics for the active frame. */
        void updateRenderStats(const RenderStats& stats);

        /** @brief Toggles pausing/freezing of profiler sampling. */
        void setPaused(bool paused) { m_isPaused = paused; }

        /** @brief Checks if profiler sampling is paused. */
        bool isPaused() const { return m_isPaused; }

        /** @brief Sets selected frame index for timeline scrubber inspection. */
        void setSelectedFrameIndex(int idx) { m_selectedFrameIdx = idx; }

        /** @brief Gets selected frame index (-1 for live latest frame). */
        int getSelectedFrameIndex() const { return m_selectedFrameIdx; }

        /** @brief Clears accumulated frame history and statistics. */
        void clearHistory();

        /** @brief Retrieves the latest completed frame profile snapshot. */
        FrameProfileData getLatestFrame() const;

        /** @brief Retrieves the entire frame history ring buffer. */
        const std::deque<FrameProfileData>& getFrameHistory() const { return m_frameHistory; }

        /** @brief Retrieves computed aggregate statistics for all registered ECS systems. */
        std::vector<SystemStats> getSystemStats() const;

        /** @brief Retrieves computed aggregate statistics for all profiled functions/custom scopes. */
        std::vector<FunctionStats> getFunctionStats() const;

        /** @brief Retrieves overall performance summary metrics. */
        double getAverageFrameTimeMs() const { return m_avgFrameTimeMs; }
        double getMinFrameTimeMs() const { return m_minFrameTimeMs; }
        double getMaxFrameTimeMs() const { return m_maxFrameTimeMs; }
        float getAverageFPS() const { return m_avgFPS; }

    private:
        Profiler();
        ~Profiler() = default;

        bool m_isPaused = false;
        uint64_t m_currentFrameIndex = 0;
        std::chrono::high_resolution_clock::time_point m_frameStartTime;

        FrameProfileData m_activeFrame;
        std::deque<FrameProfileData> m_frameHistory;
        size_t m_maxHistorySize = 120;

        mutable std::mutex m_mutex;

        double m_avgFrameTimeMs = 0.0;
        double m_minFrameTimeMs = 0.0;
        double m_maxFrameTimeMs = 0.0;
        float m_avgFPS = 0.0f;

        int m_selectedFrameIdx = -1;

        void recalculateAggregates();
    };

    /**
     * @class ProfileScope
     * @brief RAII timer for profiling execution duration of code blocks or systems.
     */
    class ENGINE_API ProfileScope {
    public:
        enum class ScopeType { System, Custom };

        ProfileScope(const std::string& name, ScopeType type = ScopeType::Custom, ProfileCategory cat = ProfileCategory::Custom)
            : m_name(name), m_type(type), m_category(cat), m_startTime(std::chrono::high_resolution_clock::now()) {}

        ProfileScope(const std::string& name, ProfileCategory cat)
            : m_name(name), m_type(ScopeType::Custom), m_category(cat), m_startTime(std::chrono::high_resolution_clock::now()) {}

        ~ProfileScope() {
            auto endTime = std::chrono::high_resolution_clock::now();
            double durationMs = std::chrono::duration<double, std::milli>(endTime - m_startTime).count();
            if (m_type == ScopeType::System) {
                Profiler::getInstance().recordSystemSample(m_name, durationMs);
            } else {
                Profiler::getInstance().recordCustomSample(m_name, durationMs, m_category);
            }
        }

    private:
        std::string m_name;
        ScopeType m_type;
        ProfileCategory m_category;
        std::chrono::high_resolution_clock::time_point m_startTime;
    };

} // namespace Engine

#if defined(TRACY_ENABLE)
    #define PROFILE_SYSTEM(name) ZoneTransientN(_tracyZoneSystem, ::Engine::toCString(name), true); ::Engine::ProfileScope _sysProfileScope(name, ::Engine::ProfileScope::ScopeType::System, ::Engine::ProfileCategory::Systems)
    #define PROFILE_SCOPE(name)  ZoneTransientN(_tracyZoneCustom, ::Engine::toCString(name), true); ::Engine::ProfileScope _customProfileScope(name, ::Engine::ProfileScope::ScopeType::Custom, ::Engine::ProfileCategory::Custom)
    #define PROFILE_CATEGORY(name, cat) ZoneTransientN(_tracyZoneCategory, ::Engine::toCString(name), true); ::Engine::ProfileScope _catProfileScope(name, cat)
    #define PROFILE_RENDERING(name) ZoneTransientN(_tracyZoneRendering, ::Engine::toCString(name), true); ::Engine::ProfileScope _rendProfileScope(name, ::Engine::ProfileCategory::Rendering)
    #define PROFILE_PHYSICS(name) ZoneTransientN(_tracyZonePhysics, ::Engine::toCString(name), true); ::Engine::ProfileScope _physProfileScope(name, ::Engine::ProfileCategory::Physics)
    #define PROFILE_ECS(name) ZoneTransientN(_tracyZoneECS, ::Engine::toCString(name), true); ::Engine::ProfileScope _ecsProfileScope(name, ::Engine::ProfileCategory::ECS_Core)
    #define PROFILE_EDITOR_UI(name) ZoneTransientN(_tracyZoneEditorUI, ::Engine::toCString(name), true); ::Engine::ProfileScope _uiProfileScope(name, ::Engine::ProfileCategory::Editor_UI)

    #if defined(_MSC_VER)
        #define PROFILE_FUNCTION() ZoneScoped; ::Engine::ProfileScope _fnProfileScope(__FUNCSIG__, ::Engine::ProfileCategory::Custom)
    #else
        #define PROFILE_FUNCTION() ZoneScoped; ::Engine::ProfileScope _fnProfileScope(__PRETTY_FUNCTION__, ::Engine::ProfileCategory::Custom)
    #endif
#else
    #define PROFILE_SYSTEM(name) ::Engine::ProfileScope _sysProfileScope(name, ::Engine::ProfileScope::ScopeType::System, ::Engine::ProfileCategory::Systems)
    #define PROFILE_SCOPE(name)  ::Engine::ProfileScope _customProfileScope(name, ::Engine::ProfileScope::ScopeType::Custom, ::Engine::ProfileCategory::Custom)
    #define PROFILE_CATEGORY(name, cat) ::Engine::ProfileScope _catProfileScope(name, cat)
    #define PROFILE_RENDERING(name) ::Engine::ProfileScope _rendProfileScope(name, ::Engine::ProfileCategory::Rendering)
    #define PROFILE_PHYSICS(name) ::Engine::ProfileScope _physProfileScope(name, ::Engine::ProfileCategory::Physics)
    #define PROFILE_ECS(name) ::Engine::ProfileScope _ecsProfileScope(name, ::Engine::ProfileCategory::ECS_Core)
    #define PROFILE_EDITOR_UI(name) ::Engine::ProfileScope _uiProfileScope(name, ::Engine::ProfileCategory::Editor_UI)

    #if defined(_MSC_VER)
        #define PROFILE_FUNCTION() ::Engine::ProfileScope _fnProfileScope(__FUNCSIG__, ::Engine::ProfileCategory::Custom)
    #else
        #define PROFILE_FUNCTION() ::Engine::ProfileScope _fnProfileScope(__PRETTY_FUNCTION__, ::Engine::ProfileCategory::Custom)
    #endif
#endif
