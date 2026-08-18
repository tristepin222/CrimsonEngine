#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <algorithm>
#include <cstdint>

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

    /**
     * @struct ProfileSample
     * @brief A single execution timing sample for a system or code block.
     */
    struct ProfileSample {
        std::string name;
        double durationMs = 0.0;
        uint32_t calls = 1;
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
        void recordCustomSample(const std::string& scopeName, double durationMs);

        /** @brief Updates the rendering and scene statistics for the active frame. */
        void updateRenderStats(const RenderStats& stats);

        /** @brief Toggles pausing/freezing of profiler sampling. */
        void setPaused(bool paused) { m_isPaused = paused; }

        /** @brief Checks if profiler sampling is paused. */
        bool isPaused() const { return m_isPaused; }

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

        void recalculateAggregates();
    };

    /**
     * @class ProfileScope
     * @brief RAII timer for profiling execution duration of code blocks or systems.
     */
    class ENGINE_API ProfileScope {
    public:
        enum class ScopeType { System, Custom };

        ProfileScope(const std::string& name, ScopeType type = ScopeType::Custom)
            : m_name(name), m_type(type), m_startTime(std::chrono::high_resolution_clock::now()) {}

        ~ProfileScope() {
            auto endTime = std::chrono::high_resolution_clock::now();
            double durationMs = std::chrono::duration<double, std::milli>(endTime - m_startTime).count();
            if (m_type == ScopeType::System) {
                Profiler::getInstance().recordSystemSample(m_name, durationMs);
            } else {
                Profiler::getInstance().recordCustomSample(m_name, durationMs);
            }
        }

    private:
        std::string m_name;
        ScopeType m_type;
        std::chrono::high_resolution_clock::time_point m_startTime;
    };

} // namespace Engine

#define PROFILE_SYSTEM(name) ::Engine::ProfileScope _sysProfileScope(name, ::Engine::ProfileScope::ScopeType::System)
#define PROFILE_SCOPE(name)  ::Engine::ProfileScope _customProfileScope(name, ::Engine::ProfileScope::ScopeType::Custom)

#if defined(_MSC_VER)
    #define PROFILE_FUNCTION() ::Engine::ProfileScope _fnProfileScope(__FUNCSIG__, ::Engine::ProfileScope::ScopeType::Custom)
#else
    #define PROFILE_FUNCTION() ::Engine::ProfileScope _fnProfileScope(__PRETTY_FUNCTION__, ::Engine::ProfileScope::ScopeType::Custom)
#endif
