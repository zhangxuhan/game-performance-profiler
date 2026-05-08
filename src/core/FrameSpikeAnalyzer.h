#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <deque>

namespace ProfilerCore {

/**
 * Spike classification based on potential cause
 */
enum class SpikeCause {
    Unknown = 0,
    GC,              // Garbage collection pause
    AssetLoading,    // Asset/resource loading
    ShaderCompile,   // Shader compilation
    PhysicsStep,     // Heavy physics simulation
    AIProcessing,    // AI/pathfinding computation
    RenderComplex,   // Complex rendering (many draw calls)
    VSyncMiss,       // Missed VSync deadline
    MemoryAlloc,     // Large memory allocation
    DiskIO,          // Disk I/O blocking
    NetworkSync,     // Network synchronization
    ThreadContention,// Thread synchronization/wait
    ThermalThrottle, // Thermal throttling
    DriverOverhead,  // GPU driver overhead
    PresentWait      // Waiting for present/swapchain
};

/**
 * Spike severity classification
 */
enum class SpikeSeverity {
    Minor = 0,       // < 2x average frame time
    Moderate = 1,    // 2-3x average frame time
    Major = 2,       // 3-5x average frame time
    Severe = 3,      // 5-10x average frame time
    Critical = 4     // > 10x average frame time
};

/**
 * Context information for a spike event
 */
struct SpikeContext {
    double cpuTimeMs;           // CPU time during spike
    double gpuTimeMs;           // GPU time during spike
    double memoryDelta;         // Memory change during spike (bytes)
    int drawCalls;              // Draw calls in this frame
    int triangles;              // Triangles rendered
    double diskIOBytes;         // Disk I/O during frame
    double networkBytes;        // Network activity during frame
    int gcCollections;          // GC collections (if applicable)
    double waitTimeMs;          // Time spent waiting (sync/mutex)
    double driverTimeMs;        // Time in GPU driver
    bool vsyncMissed;           // Whether VSync was missed
    double temperatureC;        // GPU/CPU temperature
};

/**
 * A single frame spike event
 */
struct FrameSpike {
    int64_t id;                 // Unique spike ID
    int64_t timestamp;          // When spike occurred
    int frameNumber;            // Frame number
    
    // Timing information
    double frameTimeMs;         // Actual frame time
    double expectedFrameTimeMs; // Expected frame time (target)
    double spikeRatio;          // frameTime / expectedFrameTime
    double deviationMs;         // frameTime - expectedFrameTime
    
    // Classification
    SpikeSeverity severity;
    SpikeCause primaryCause;
    SpikeCause secondaryCause;  // Possible secondary factor
    double causeConfidence;     // 0-1 confidence in cause classification
    
    // Context
    SpikeContext context;
    
    // Analysis
    std::string analysis;       // Human-readable analysis
    std::vector<std::string> recommendations;
    
    // Tracking
    bool acknowledged;
    int64_t acknowledgedAt;
    bool isRecurring;           // Similar spike seen recently
    int recurrenceGroup;        // Group ID for recurring spikes
};

/**
 * Spike pattern for recurring issues
 */
struct SpikePattern {
    int64_t patternId;
    SpikeCause cause;
    double avgFrameTimeMs;
    double avgIntervalMs;       // Average time between occurrences
    int occurrenceCount;
    int64_t firstOccurrence;
    int64_t lastOccurrence;
    std::vector<int64_t> spikeIds;
    std::string description;
    std::vector<std::string> recommendations;
};

/**
 * Spike statistics summary
 */
struct SpikeStatistics {
    int64_t timestamp;
    int totalSpikes;
    int minorSpikes;
    int moderateSpikes;
    int majorSpikes;
    int severeSpikes;
    int criticalSpikes;
    
    // By cause
    std::unordered_map<SpikeCause, int> spikesByCause;
    
    // Timing
    double avgSpikeFrameTime;
    double maxSpikeFrameTime;
    double totalSpikeTimeMs;    // Total time lost to spikes
    
    // Patterns
    int recurringPatterns;
    int uniqueSpikes;
    
    // Impact
    double fpsImpactPercent;    // Estimated FPS impact
    double userExperienceScore; // 0-100, affected by spike frequency/severity
    
    // Recommendations
    std::vector<std::string> topRecommendations;
};

/**
 * Configuration for spike detection
 */
struct SpikeAnalyzerConfig {
    // Detection thresholds
    double targetFrameTimeMs = 16.67;     // 60 FPS target
    double minorThreshold = 2.0;           // 2x target
    double moderateThreshold = 3.0;        // 3x target
    double majorThreshold = 5.0;           // 5x target
    double severeThreshold = 10.0;         // 10x target
    
    // Cause detection thresholds
    double gcTimeThresholdMs = 5.0;        // GC suspected if wait time > 5ms
    double assetLoadThresholdMs = 10.0;    // Asset loading if spike > 10ms
    double shaderCompileThresholdMs = 20.0;// Shader compile if spike > 20ms
    double physicsThresholdMs = 8.0;       // Physics if CPU time > 8ms
    double renderThresholdDrawCalls = 5000;// Render complex if draw calls >
    double driverOverheadThresholdMs = 2.0;// Driver overhead threshold
    double thermalThresholdC = 80.0;       // Thermal throttling temp
    
    // Pattern detection
    int patternWindowSize = 300;           // Frames to analyze for patterns
    int recurrenceIntervalMs = 5000;       // Same cause within 5s = recurring
    int minPatternOccurrences = 3;         // Minimum occurrences for pattern
    
    // History
    int maxSpikeHistory = 500;             // Maximum spikes to keep
    int maxPatternHistory = 50;            // Maximum patterns to track
    
    // Analysis
    bool enableCauseAnalysis = true;
    bool enablePatternDetection = true;
    bool generateRecommendations = true;
};

/**
 * Callback types
 */
using SpikeCallback = std::function<void(const FrameSpike&)>;
using PatternCallback = std::function<void(const SpikePattern&)>;

/**
 * FrameSpikeAnalyzer - Deep analysis of frame time spikes with root cause detection
 * 
 * Features:
 * - Detects and classifies frame time spikes by severity
 * - Analyzes root cause using multiple heuristics (CPU/GPU/IO/GC/etc.)
 * - Detects recurring spike patterns
 * - Generates actionable optimization recommendations
 * - Tracks spike statistics and impact on user experience
 * - Provides context-aware analysis for each spike
 * 
 * Usage:
 *   FrameSpikeAnalyzer analyzer;
 *   analyzer.SetTargetFPS(60.0);
 *   
 *   // Each frame
 *   analyzer.RecordFrame(frameTimeMs, cpuTimeMs, gpuTimeMs, context);
 *   
 *   // Get analysis
 *   auto spikes = analyzer.GetRecentSpikes(10);
 *   auto stats = analyzer.GetStatistics();
 */
class FrameSpikeAnalyzer {
public:
    FrameSpikeAnalyzer();
    ~FrameSpikeAnalyzer();
    
    // Configuration
    void SetConfig(const SpikeAnalyzerConfig& config);
    const SpikeAnalyzerConfig& GetConfig() const { return m_config; }
    void SetTargetFPS(double fps);
    void Reset();
    
    // Data recording
    void RecordFrame(double frameTimeMs, double cpuTimeMs, double gpuTimeMs,
                     const SpikeContext& context = SpikeContext{});
    
    // Convenience method with automatic context extraction
    void RecordFrameSimple(double frameTimeMs, double cpuTimeMs, double gpuTimeMs);
    
    // Spike access
    const std::vector<FrameSpike>& GetAllSpikes() const { return m_spikes; }
    std::vector<FrameSpike> GetRecentSpikes(int count) const;
    std::vector<FrameSpike> GetSpikesByCause(SpikeCause cause) const;
    std::vector<FrameSpike> GetSpikesBySeverity(SpikeSeverity severity) const;
    std::vector<FrameSpike> GetUnacknowledgedSpikes() const;
    
    FrameSpike GetSpike(int64_t spikeId) const;
    
    // Pattern access
    const std::vector<SpikePattern>& GetPatterns() const { return m_patterns; }
    std::vector<SpikePattern> GetActivePatterns() const;
    SpikePattern GetPattern(int64_t patternId) const;
    
    // Statistics
    SpikeStatistics GetStatistics() const;
    SpikeStatistics GetStatisticsForDuration(int64_t durationMs) const;
    
    // Spike management
    bool AcknowledgeSpike(int64_t spikeId);
    bool AcknowledgeAllSpikes();
    void ClearHistory();
    
    // Analysis
    std::string AnalyzeSpike(int64_t spikeId) const;
    std::vector<std::string> GetRecommendations(int64_t spikeId) const;
    std::vector<std::string> GetTopRecommendations(int count = 5) const;
    
    // Impact assessment
    double ComputeFPSImpact() const;
    double ComputeUserExperienceScore() const;
    int64_t EstimateTimeLostToSpikes() const;
    
    // Callbacks
    void SetSpikeCallback(SpikeCallback callback);
    void SetPatternCallback(PatternCallback callback);
    
    // Export
    std::string ExportToJSON() const;
    std::string ExportSpikesToJSON(int limit = 100) const;
    std::string ExportPatternsToJSON() const;
    std::string ExportStatisticsToJSON() const;

private:
    // Detection
    void DetectSpike(double frameTimeMs, double cpuTimeMs, double gpuTimeMs,
                     const SpikeContext& context);
    SpikeSeverity ClassifySeverity(double spikeRatio) const;
    SpikeCause DetermineCause(double frameTimeMs, double cpuTimeMs, double gpuTimeMs,
                              const SpikeContext& context, double& confidence) const;
    SpikeCause DetermineSecondaryCause(double frameTimeMs, double cpuTimeMs,
                                       double gpuTimeMs, const SpikeContext& context) const;
    
    // Pattern detection
    void UpdatePatterns(const FrameSpike& spike);
    void DetectRecurringSpike(FrameSpike& spike);
    int FindRecurrenceGroup(const FrameSpike& spike) const;
    
    // Analysis
    void GenerateSpikeAnalysis(FrameSpike& spike);
    void GenerateRecommendations(FrameSpike& spike);
    std::string CauseToString(SpikeCause cause) const;
    std::string SeverityToString(SpikeSeverity severity) const;
    
    // Helpers
    int64_t GenerateSpikeId();
    int64_t GetCurrentTimestamp() const;
    void TrimHistory();
    void UpdateStatistics();
    
    // Configuration
    SpikeAnalyzerConfig m_config;
    
    // Frame history for baseline calculation
    std::deque<double> m_frameTimeHistory;
    std::deque<double> m_cpuTimeHistory;
    std::deque<double> m_gpuTimeHistory;
    double m_avgFrameTime;
    double m_avgCpuTime;
    double m_avgGpuTime;
    
    // Detected spikes
    std::vector<FrameSpike> m_spikes;
    int64_t m_nextSpikeId;
    
    // Patterns
    std::vector<SpikePattern> m_patterns;
    int64_t m_nextPatternId;
    std::unordered_map<int, std::vector<int64_t>> m_recurrenceGroups;
    
    // Statistics cache
    mutable SpikeStatistics m_cachedStats;
    mutable bool m_statsDirty;
    
    // Callbacks
    SpikeCallback m_spikeCallback;
    PatternCallback m_patternCallback;
    
    // Current frame tracking
    int m_currentFrame;
    int64_t m_lastFrameTimestamp;
};

} // namespace ProfilerCore
