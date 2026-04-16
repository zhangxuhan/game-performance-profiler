#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>

namespace ProfilerCore {

/**
 * Benchmark session metadata
 */
struct BenchmarkMetadata {
    std::string name;           // Session name (e.g., "Level1-Baseline", "After-Optimization")
    std::string description;    // Optional description
    std::string gameVersion;    // Game version string
    std::string buildNumber;    // Build identifier
    std::string platform;       // Platform info (e.g., "Windows 11", "Steam Deck")
    std::string hardwareInfo;   // Hardware summary (GPU, CPU)
    int64_t startTime;          // Session start timestamp
    int64_t endTime;            // Session end timestamp
    int durationSec;            // Session duration in seconds
    bool isBaseline;            // Is this a baseline reference?
};

/**
 * Benchmark session result summary
 */
struct BenchmarkResult {
    std::string sessionId;
    BenchmarkMetadata metadata;
    
    // Core metrics
    double avgFps;
    double minFps;
    double maxFps;
    double p50Fps;
    double p90Fps;
    double p95Fps;
    double p99Fps;
    
    // Frame time metrics
    double avgFrameTimeMs;
    double maxFrameTimeMs;
    double minFrameTimeMs;
    double stdDevFrameTimeMs;
    
    // Memory metrics
    size_t peakMemoryBytes;
    size_t avgMemoryBytes;
    double memoryGrowthRate;    // bytes per frame
    
    // Stability
    double stabilityScore;      // 0-100
    
    // Frame count
    int totalFrames;
    
    // Alert summary
    int criticalAlertCount;
    int warningAlertCount;
    int totalAlertCount;
    
    // Custom metrics (extensible)
    std::unordered_map<std::string, double> customMetrics;
};

/**
 * Comparison result between two benchmarks
 */
struct BenchmarkComparison {
    std::string baselineSessionId;
    std::string comparisonSessionId;
    
    // FPS delta
    double avgFpsDelta;         // Positive = improvement
    double avgFpsDeltaPercent;
    double minFpsDelta;
    double maxFpsDelta;
    double p95FpsDelta;
    double p99FpsDelta;
    
    // Frame time delta
    double avgFrameTimeDeltaMs;
    double avgFrameTimeDeltaPercent;
    double maxFrameTimeDeltaMs;
    
    // Memory delta
    double peakMemoryDeltaBytes;
    double peakMemoryDeltaPercent;
    double avgMemoryDeltaBytes;
    
    // Stability delta
    double stabilityDelta;
    
    // Alert delta
    int criticalAlertDelta;
    int warningAlertDelta;
    
    // Overall assessment
    std::string overallAssessment;  // "Improved", "Degraded", "No Change"
    double improvementScore;        // 0-100, higher = more improvement
    
    // Significant changes detected
    std::vector<std::string> significantChanges;
};

/**
 * Change significance thresholds
 */
struct ComparisonThresholds {
    double fpsSignificantPercent = 5.0;      // 5% FPS change is significant
    double frameTimeSignificantPercent = 5.0;
    double memorySignificantPercent = 10.0;
    double stabilitySignificantPercent = 10.0;
    int alertSignificantDelta = 3;           // 3+ alert change is significant
};

/**
 * BenchmarkRecorder - Record, compare, and analyze benchmark sessions
 * 
 * Features:
 * - Record benchmark sessions with full metadata
 * - Compare sessions to detect performance changes
 * - Identify significant improvements/degradations
 * - Export benchmark reports
 * - Maintain session history
 * - Automatic baseline management
 */
class BenchmarkRecorder {
public:
    BenchmarkRecorder();
    ~BenchmarkRecorder();
    
    // Configuration
    void SetComparisonThresholds(const ComparisonThresholds& thresholds);
    void SetMaxSessionHistory(size_t maxSessions);
    void SetBaselineSession(const std::string& sessionId);
    
    // Session management
    std::string StartSession(const BenchmarkMetadata& metadata);
    void EndSession(const BenchmarkResult& result);
    void CancelSession();
    
    bool IsRecording() const { return m_isRecording; }
    const std::string& GetCurrentSessionId() const { return m_currentSessionId; }
    
    // Session retrieval
    const std::vector<BenchmarkResult>& GetAllSessions() const { return m_sessions; }
    BenchmarkResult GetSession(const std::string& sessionId) const;
    BenchmarkResult GetBaselineSession() const;
    std::vector<BenchmarkResult> GetRecentSessions(int count) const;
    std::vector<BenchmarkResult> GetSessionsByName(const std::string& name) const;
    
    // Comparison
    BenchmarkComparison CompareSessions(
        const std::string& baselineId,
        const std::string& comparisonId
    ) const;
    
    BenchmarkComparison CompareToBaseline(const std::string& sessionId) const;
    
    BenchmarkComparison CompareToPrevious(const std::string& sessionId) const;
    
    std::vector<BenchmarkComparison> CompareAllToBaseline() const;
    
    // Analysis
    std::vector<std::string> DetectSignificantChanges(
        const BenchmarkComparison& comparison
    ) const;
    
    bool HasSignificantImprovement(const BenchmarkComparison& comparison) const;
    bool HasSignificantDegradation(const BenchmarkComparison& comparison) const;
    
    // Statistics across sessions
    double GetAverageFpsAcrossSessions() const;
    double GetBestSessionFps() const;
    double GetWorstSessionFps() const;
    std::string GetBestSessionId() const;
    std::string GetWorstSessionId() const;
    
    // Export
    std::string ExportSessionsToJSON() const;
    std::string ExportComparisonToJSON(const BenchmarkComparison& comparison) const;
    std::string ExportReport(const std::string& sessionId) const;
    std::string ExportFullReport() const;
    
    // Persistence (optional)
    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);
    
    // Clear
    void ClearSessions();
    void RemoveSession(const std::string& sessionId);

private:
    std::string GenerateSessionId() const;
    int64_t GetCurrentTimestamp() const;
    void TrimHistory();
    BenchmarkComparison ComputeComparison(
        const BenchmarkResult& baseline,
        const BenchmarkResult& comparison
    ) const;
    std::string AssessOverall(const BenchmarkComparison& comp) const;
    double ComputeImprovementScore(const BenchmarkComparison& comp) const;

private:
    bool m_isRecording;
    std::string m_currentSessionId;
    BenchmarkMetadata m_currentMetadata;
    
    std::vector<BenchmarkResult> m_sessions;
    std::string m_baselineSessionId;
    
    ComparisonThresholds m_thresholds;
    size_t m_maxSessionHistory = 50;
    
    std::unordered_map<std::string, size_t> m_sessionIndex;
};

} // namespace ProfilerCore