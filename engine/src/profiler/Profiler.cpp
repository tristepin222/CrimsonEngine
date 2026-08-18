#include "profiler/Profiler.hpp"
#include <numeric>
#include <cmath>

namespace Engine {

    Profiler& Profiler::getInstance() {
        static Profiler instance;
        return instance;
    }

    Profiler::Profiler()
        : m_isPaused(false), m_currentFrameIndex(0) {
        m_frameStartTime = std::chrono::high_resolution_clock::now();
    }

    void Profiler::beginFrame() {
        if (m_isPaused) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameStartTime = std::chrono::high_resolution_clock::now();
        m_activeFrame = FrameProfileData{};
        m_activeFrame.frameIndex = ++m_currentFrameIndex;
    }

    void Profiler::endFrame() {
        if (m_isPaused) return;

        auto now = std::chrono::high_resolution_clock::now();
        double frameDurationMs = std::chrono::duration<double, std::milli>(now - m_frameStartTime).count();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_activeFrame.frameTimeMs = frameDurationMs;
        m_activeFrame.fps = (frameDurationMs > 0.0) ? static_cast<float>(1000.0 / frameDurationMs) : 0.0f;

        m_activeFrame.renderingMs = 0.0;
        m_activeFrame.physicsMs = 0.0;
        m_activeFrame.systemsMs = 0.0;
        m_activeFrame.ecsCoreMs = 0.0;
        m_activeFrame.editorUiMs = 0.0;
        m_activeFrame.customMs = 0.0;

        for (const auto& sample : m_activeFrame.systemSamples) {
            m_activeFrame.systemsMs += sample.durationMs;
        }

        for (const auto& sample : m_activeFrame.customSamples) {
            switch (sample.category) {
                case ProfileCategory::Rendering: m_activeFrame.renderingMs += sample.durationMs; break;
                case ProfileCategory::Physics:   m_activeFrame.physicsMs += sample.durationMs; break;
                case ProfileCategory::Systems:   m_activeFrame.systemsMs += sample.durationMs; break;
                case ProfileCategory::ECS_Core:  m_activeFrame.ecsCoreMs += sample.durationMs; break;
                case ProfileCategory::Editor_UI: m_activeFrame.editorUiMs += sample.durationMs; break;
                default:                         m_activeFrame.customMs += sample.durationMs; break;
            }
        }

        m_frameHistory.push_back(m_activeFrame);
        if (m_frameHistory.size() > m_maxHistorySize) {
            m_frameHistory.pop_front();
        }

#if defined(TRACY_ENABLE)
        FrameMark;
#endif

        recalculateAggregates();
    }

    void Profiler::recordSystemSample(const std::string& systemName, double durationMs) {
        if (m_isPaused) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& sample : m_activeFrame.systemSamples) {
            if (sample.name == systemName) {
                sample.durationMs += durationMs;
                sample.calls++;
                return;
            }
        }
        m_activeFrame.systemSamples.push_back({ systemName, durationMs, 1, ProfileCategory::Systems });
    }

    void Profiler::recordCustomSample(const std::string& scopeName, double durationMs, ProfileCategory category) {
        if (m_isPaused) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& sample : m_activeFrame.customSamples) {
            if (sample.name == scopeName) {
                sample.durationMs += durationMs;
                sample.calls++;
                return;
            }
        }
        m_activeFrame.customSamples.push_back({ scopeName, durationMs, 1, category });
    }

    void Profiler::updateRenderStats(const RenderStats& stats) {
        if (m_isPaused) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_activeFrame.renderStats = stats;
    }

    void Profiler::clearHistory() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameHistory.clear();
        m_selectedFrameIdx = -1;
        m_avgFrameTimeMs = 0.0;
        m_minFrameTimeMs = 0.0;
        m_maxFrameTimeMs = 0.0;
        m_avgFPS = 0.0f;
    }

    FrameProfileData Profiler::getLatestFrame() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_frameHistory.empty()) {
            return FrameProfileData{};
        }
        if (m_selectedFrameIdx >= 0 && static_cast<size_t>(m_selectedFrameIdx) < m_frameHistory.size()) {
            return m_frameHistory[m_selectedFrameIdx];
        }
        return m_frameHistory.back();
    }

    std::vector<SystemStats> Profiler::getSystemStats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_frameHistory.empty()) return {};

        std::unordered_map<std::string, std::vector<double>> systemDurations;
        std::unordered_map<std::string, double> latestDurations;

        for (const auto& frame : m_frameHistory) {
            for (const auto& sample : frame.systemSamples) {
                systemDurations[sample.name].push_back(sample.durationMs);
                latestDurations[sample.name] = sample.durationMs;
            }
        }

        std::vector<SystemStats> result;
        result.reserve(systemDurations.size());

        double avgTotalFrameMs = m_avgFrameTimeMs > 0.0 ? m_avgFrameTimeMs : 16.66;

        for (const auto& [name, durations] : systemDurations) {
            if (durations.empty()) continue;

            double sum = std::accumulate(durations.begin(), durations.end(), 0.0);
            double avg = sum / durations.size();
            double minVal = *std::min_element(durations.begin(), durations.end());
            double maxVal = *std::max_element(durations.begin(), durations.end());
            double lastVal = latestDurations[name];
            double percent = (avgTotalFrameMs > 0.0) ? (avg / avgTotalFrameMs * 100.0) : 0.0;

            result.push_back({ name, lastVal, avg, minVal, maxVal, percent });
        }

        std::sort(result.begin(), result.end(), [](const SystemStats& a, const SystemStats& b) {
            return a.avgMs > b.avgMs;
        });

        return result;
    }

    std::vector<FunctionStats> Profiler::getFunctionStats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_frameHistory.empty()) return {};

        std::unordered_map<std::string, std::vector<double>> functionDurations;
        std::unordered_map<std::string, std::vector<uint32_t>> functionCalls;
        std::unordered_map<std::string, double> latestDurations;
        std::unordered_map<std::string, ProfileCategory> categories;

        for (const auto& frame : m_frameHistory) {
            for (const auto& sample : frame.customSamples) {
                functionDurations[sample.name].push_back(sample.durationMs);
                functionCalls[sample.name].push_back(sample.calls);
                latestDurations[sample.name] = sample.durationMs;
                categories[sample.name] = sample.category;
            }
        }

        std::vector<FunctionStats> result;
        result.reserve(functionDurations.size());

        double avgTotalFrameMs = m_avgFrameTimeMs > 0.0 ? m_avgFrameTimeMs : 16.66;

        for (const auto& [name, durations] : functionDurations) {
            if (durations.empty()) continue;

            double sum = std::accumulate(durations.begin(), durations.end(), 0.0);
            double avg = sum / durations.size();
            double minVal = *std::min_element(durations.begin(), durations.end());
            double maxVal = *std::max_element(durations.begin(), durations.end());
            double lastVal = latestDurations[name];
            double percent = (avgTotalFrameMs > 0.0) ? (avg / avgTotalFrameMs * 100.0) : 0.0;

            const auto& callsVec = functionCalls[name];
            uint32_t sumCalls = std::accumulate(callsVec.begin(), callsVec.end(), 0u);
            uint32_t avgCalls = static_cast<uint32_t>(sumCalls / callsVec.size());

            result.push_back({ name, avgCalls, lastVal, avg, minVal, maxVal, percent, categories[name] });
        }

        std::sort(result.begin(), result.end(), [](const FunctionStats& a, const FunctionStats& b) {
            return a.avgMs > b.avgMs;
        });

        return result;
    }

    void Profiler::recalculateAggregates() {
        if (m_frameHistory.empty()) return;

        double sumTime = 0.0;
        m_minFrameTimeMs = m_frameHistory.front().frameTimeMs;
        m_maxFrameTimeMs = m_frameHistory.front().frameTimeMs;

        for (const auto& frame : m_frameHistory) {
            sumTime += frame.frameTimeMs;
            if (frame.frameTimeMs < m_minFrameTimeMs) m_minFrameTimeMs = frame.frameTimeMs;
            if (frame.frameTimeMs > m_maxFrameTimeMs) m_maxFrameTimeMs = frame.frameTimeMs;
        }

        m_avgFrameTimeMs = sumTime / m_frameHistory.size();
        m_avgFPS = (m_avgFrameTimeMs > 0.0) ? static_cast<float>(1000.0 / m_avgFrameTimeMs) : 0.0f;
    }

} // namespace Engine
