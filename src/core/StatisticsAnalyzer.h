#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace ProfilerCore {

/**
 * Performance alert severity levels
 */
enum class AlertSeverity {
    Info = 0,
    Warning = 1,
    Critical = 2
};

/**
 * A single performance alert
 */
struct PerformanceAlert {
    AlertSeverity severity;
    std::string message;
    int64_t timestamp;
    std::string metric;
    double value;
    double threshold;
};

/**
 * Summary statistics for a set of samples
 */
struct SummaryStats {
    int64_t timestamp;
    int sampleCount;
    
    // Frame time stats (in microseconds)
    double minFrameTime;
    double maxFrameTime;
    double avgFrameTime;
    double stdDevFrameTime;
    double medianFrameTime;
    
    // FPS stats
    double minFps;
    double maxFps;
    double avgFps;
    double p50Fps;
    double p90Fps;
    double p95Fps;
    double p99Fps;
    
    // Memory stats
    size_t peakMemoryUsage;
    size_t avgMemoryUsage;
    double memoryGrowthRate; // bytes per frame
    
    // Stability score (0-100, higher is better)
    double stabilityScore;
    
    // Alert count
    int alertCount;
};

/**
 * Threshold configuration for alerts
 */
struct AlertThresholds {
    double minFpsWarning = 45.0;
    double minFpsCritical = 30.0;
    double maxFrameTimeWarning = 22222.0; // ~45 FPS in us
    double maxFrameTimeCritical = 33333.0; // ~30 FPS in us
    double memoryGrowthRateWarning = 1024.0 * 1024.0; // 1MB per frame
    double memoryGrowthRateCritical = 5.0 * 1024.0 * 1024.0; // 5MB per frame
    size_t peakMemoryWarning = 256 * 1024 * 1024; // 256MB
    size_t peakMemoryCritical = 512 * 1024 * 1024; // 512MB
};

/**
 * StatisticsAnalyzer - computes aggregate performance statistics and generates alerts
 * 
 * Features:
 * - Rolling window analysis (configurable window size)
 * - Percentile calculation (P50, P90, P95, P99)
 * - FPS stability scoring
 * - Memory leak detection via growth rate analysis
 * - Configurable alert thresholds
 * - JSON export for dashboard consumption
 */
class StatisticsAnalyzer {
public:
    StatisticsAnalyzer();
    ~StatisticsAnalyzer();

    // Configuration
    void SetWindowSize(size_t windowSize);
    void SetThresholds(const AlertThresholds& thresholds);
    void Reset();

    // Feed frame data (called from ProfilerCore after each frame)
    void RecordFrame(double fps, double frameTimeMs, size_t memoryUsage);
    
    // Compute and retrieve current statistics
    SummaryStats GetSummary() const;
    
    // Alert management
    const std::vector<PerformanceAlert>& GetAlerts() const { return m_alerts; }
    void ClearAlerts();
    size_t GetAlertCountBySeverity(AlertSeverity severity) const;
    
    // Export
    std::string ExportToJSON() const;

private:
    void ComputeStatistics();
    void ComputePercentiles();
    void DetectAnomalies();
    void GenerateAlerts();
    double ComputeStabilityScore() const;
    
    // Helper: nth element selection (O(n) quickselect)
    double QuickSelectPercentile(std::vector<double>& arr, double percentile);
    
    size_t m_windowSize;
    AlertThresholds m_thresholds;
    
    std::vector<double> m_frameTimes; // in ms
    std::vector<double> m_fpsHistory;
    std::vector<size_t> m_memoryHistory;
    
    SummaryStats m_currentStats;
    std::vector<PerformanceAlert> m_alerts;
    
    bool m_dirty;
    
    // Internal state for rolling window
    size_t m_totalSamples;
};

} // namespace ProfilerCore
