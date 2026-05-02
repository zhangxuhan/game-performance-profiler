#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>

// Forward declare to avoid circular dependency
namespace ProfilerCore {
    class StatisticsAnalyzer;
    class AlertManager;
    class GPUProfiler;
    class MemoryAnalyzer;
    class NetworkProfiler;
    class TrendPredictor;
    class PerformanceScorer;
    class SessionManager;
    struct AlertThresholds;
}

namespace ProfilerCore {

struct ProfileData {
    std::string functionName;
    int64_t startTime;
    int64_t endTime;
    int64_t duration; // microseconds
    int callCount;
    double percentage; // percentage of total time
};

struct MemorySnapshot {
    int64_t timestamp;
    size_t totalAllocated;
    size_t totalFreed;
    size_t currentUsage;
    int allocationCount;
};

struct FrameData {
    int frameNumber;
    int64_t timestamp;
    float fps;
    float frameTime; // milliseconds
    std::vector<ProfileData> profiles;
    MemorySnapshot memory;
};

class ProfilerCore {
public:
    static ProfilerCore& GetInstance();
    
    // Lifecycle
    void StartSampling();
    void StopSampling();
    bool IsRunning() const { return m_isRunning; }
    
    // Frame profiling
    void BeginFrame();
    void EndFrame();
    FrameData GetCurrentFrame() const;
    
    // Function profiling
    void BeginFunction(const std::string& name);
    void EndFunction(const std::string& name);
    
    // Memory profiling  
    void TrackAllocation(size_t size);
    void TrackDeallocation(size_t size);
    MemorySnapshot GetMemorySnapshot() const;
    
    // Statistics analysis
    StatisticsAnalyzer* GetAnalyzer() { return m_analyzer.get(); }
    void SetAnalyzerWindowSize(size_t windowSize);
    void SetAnalyzerThresholds(const struct AlertThresholds& thresholds);
    
    // Alert management
    AlertManager* GetAlertManager() { return m_alertManager.get(); }
    void SetAlertConfig(const struct AlertConfig& config);
    const std::vector<struct Alert>& GetActiveAlerts() const;
    bool AcknowledgeAlert(int alertId);
    bool AcknowledgeAllAlerts();
    
    // GPU Profiling
    GPUProfiler* GetGPUProfiler() { return m_gpuProfiler.get(); }
    void SetGPUProfilerEnabled(bool enabled);
    bool IsGPUProfilerEnabled() const { return m_gpuProfilerEnabled; }

    // Network Profiling
    NetworkProfiler* GetNetworkProfiler() { return m_networkProfiler.get(); }
    void SetNetworkProfilerEnabled(bool enabled);
    bool IsNetworkProfilerEnabled() const;

    // Correlated CPU-GPU analysis
    void RecordCPUGPUFrame(double cpuTimeMs, double gpuTimeUs);
    std::string GetCurrentBottleneck() const;

    // Memory analysis
    MemoryAnalyzer* GetMemoryAnalyzer() { return m_memoryAnalyzer.get(); }
    int64_t TrackMemoryAllocation(size_t size, enum MemoryCategory category = MemoryCategory::General,
                                   const std::string& tag = "",
                                   const char* file = nullptr, int line = 0);
    void TrackMemoryDeallocation(int64_t allocationId);
    struct MemoryReport GetMemoryReport() const;
    std::vector<struct MemoryLeak> GetDetectedLeaks() const;
    size_t GetCurrentMemoryUsage() const;
    
    // Trend prediction
    TrendPredictor* GetTrendPredictor() { return m_trendPredictor.get(); }
    
    // Performance scoring
    PerformanceScorer* GetPerformanceScorer() { return m_performanceScorer.get(); }
    struct PerformanceScoreCard ComputePerformanceScore();
    
    // Thermal monitoring
    ThermalMonitor* GetThermalMonitor() { return m_thermalMonitor.get(); }
    void SetThermalMonitorEnabled(bool enabled);
    bool IsThermalMonitorEnabled() const;
    struct ThermalSnapshot GetThermalSnapshot();
    std::vector<struct CoolingRecommendation> GetCoolingRecommendations();
    
    // Session management
    SessionManager* GetSessionManager() { return m_sessionManager.get(); }
    std::string StartProfilingSession(const std::string& name,
                                       const std::string& description = "",
                                       const std::string& gameVersion = "");
    bool StopProfilingSession(const std::string& sessionId);
    std::string GetActiveSessionId() const;
    
    // Data export
    std::string ExportToJSON() const;
    std::string ExportToCSV() const;
    
    // WebSocket callback
    using DataCallback = std::function<void(const std::string&)>;
    void SetDataCallback(DataCallback callback);
    
private:
    ProfilerCore();
    ~ProfilerCore();
    ProfilerCore(const ProfilerCore&) = delete;
    ProfilerCore& operator=(const ProfilerCore&) = delete;
    
    void ProcessFrame();
    void SendDataToServer();
    
    bool m_isRunning = false;
    int m_currentFrame = 0;
    int64_t m_frameStartTime = 0;
    
    std::vector<ProfileData> m_currentProfiles;
    std::vector<FrameData> m_frameHistory;
    
    size_t m_totalAllocated = 0;
    size_t m_totalFreed = 0;
    int m_allocationCount = 0;
    
    DataCallback m_dataCallback;
    
    std::unique_ptr<StatisticsAnalyzer> m_analyzer;
    std::unique_ptr<AlertManager> m_alertManager;
    std::unique_ptr<GPUProfiler> m_gpuProfiler;
    bool m_gpuProfilerEnabled = true;
    std::unique_ptr<NetworkProfiler> m_networkProfiler;
    std::unique_ptr<MemoryAnalyzer> m_memoryAnalyzer;
    std::unique_ptr<TrendPredictor> m_trendPredictor;
    std::unique_ptr<PerformanceScorer> m_performanceScorer;
    std::unique_ptr<ThermalMonitor> m_thermalMonitor;
    bool m_thermalMonitorEnabled = true;
    std::unique_ptr<SessionManager> m_sessionManager;
    
    struct FunctionStackEntry {
        std::string name;
        int64_t startTime;
    };
    std::vector<FunctionStackEntry> m_functionStack;
};

} // namespace ProfilerCore

#include "MemoryAnalyzer.h"
#include "ThermalMonitor.h"
#include "SessionManager.h"

// Macros for easy profiling
#define PROFILE_FUNCTION() \
    static ProfilerCore::FunctionProfile __profile__##__LINE__(__FUNCTION__)

#define PROFILE_SCOPE(name) \
    static ProfilerCore::ScopeProfile __scope__##__LINE__(name)

// Memory tracking macros
#define TRACK_MEMORY(size, category, tag) \
    ProfilerCore::ProfilerCore::GetInstance().TrackMemoryAllocation(size, category, tag, __FILE__, __LINE__)

#define TRACK_MEMORY_SIMPLE(size) \
    ProfilerCore::ProfilerCore::GetInstance().TrackMemoryAllocation(size, ProfilerCore::MemoryCategory::General, "", __FILE__, __LINE__)

#define FREE_MEMORY(id) \
    ProfilerCore::ProfilerCore::GetInstance().TrackMemoryDeallocation(id)
