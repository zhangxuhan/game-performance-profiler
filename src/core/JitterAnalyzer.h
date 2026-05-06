#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <deque>

namespace ProfilerCore {

/**
 * Jitter pattern types for classification
 */
enum class JitterPattern {
    None = 0,
    Periodic,        // Regular pattern (e.g., VSync issues)
    Sporadic,        // Random occasional spikes
    GradualDrift,    // Slow degradation over time
    Burst,           // Multiple consecutive frame drops
    Stuttering       // Alternating fast/slow frames
};

/**
 * A detected jitter event
 */
struct JitterEvent {
    int64_t timestamp;
    int frameStart;
    int frameEnd;
    JitterPattern pattern;
    double severity;      // 0-100, how severe this jitter is
    double avgFrameTime;  // ms
    double maxFrameTime;  // ms
    double durationMs;   // How long this event lasted
    std::string probableCause;  // "CPU", "GPU", "IO", "GC", "Memory"
    std::string description;
};

/**
 * Frame time jitter analysis result
 */
struct JitterAnalysis {
    int64_t timestamp;
    int sampleCount;
    
    // Overall jitter metrics
    double averageJitter;     // Average frame time variation (ms)
    double maxJitter;        // Maximum frame time variation (ms)
    double jitterStdDev;    // Standard deviation of frame time
    double jitterCoefficient; // CV of frame time (higher = more unstable)
    
    // Classification
    JitterPattern primaryPattern;
    double patternStrength;  // How strongly the pattern is expressed (0-100)
    
    // Event summary
    int totalJitterEvents;
    int periodicEvents;
    int sporadicEvents;
    int burstEvents;
    int stutteringEvents;
    
    // Severity distribution
    int mildEvents;     // severity < 30
    int moderateEvents;  // severity 30-60
    int severeEvents;   // severity > 60
    
    // Root cause estimates
    double cpuJitterRatio;   // Estimated CPU-driven jitter ratio
    double gpuJitterRatio;  // Estimated GPU-driven jitter ratio
    
    // Recommendations
    std::vector<std::string> recommendations;
};

/**
 * Configuration for jitter detection
 */
struct JitterConfig {
    // Thresholds
    double jitterThreshold = 2.0;         // ms, what counts as "jitter"
    double severeJitterThreshold = 5.0; // ms, severe jitter
    double frameTimeSpikeThreshold = 3.0; // ms, single frame spike
    
    // Pattern detection
    int minPeriodFrames = 8;           // Minimum frames to detect periodic
    int burstConsecutiveFrames = 3;       // Frames in a row to count as burst
    double periodTolerance = 0.2;         // 20% tolerance for periodic detection
    
    // Analysis window
    size_t analysisWindowSize = 300;    // frames to analyze
    
    // Sensitivity
    double sensitivityMultiplier = 1.0; // Adjust detection sensitivity
    
    // Classification thresholds
    double cpuThresholdMs = 2.0;      // Frame time above this may be CPU-bound
    double gpuThresholdMs = 8.0;         // Frame time above this is likely GPU-bound
};

/**
 * JitterAnalyzer - Analyze frame time jitter patterns and detect performance issues
 * 
 * Features:
 * - Detect periodic jitter (VSync, fixed timestep issues)
 * - Detect sporadic spikes (GC, asset loading)
 * - Detect gradual drift (memory leak, cache degradation)
 * - Detect frame bursts (multi-frame hitches)
 * - Detect stuttering (alternating fast/slow)
 * - Classify root cause (CPU vs GPU vs IO vs GC)
 * - Generate actionable recommendations
 * - Real-time event detection
 */
class JitterAnalyzer {
public:
    JitterAnalyzer();
    ~JitterAnalyzer();

    // Configuration
    void SetConfig(const JitterConfig& config);
    JitterConfig GetConfig() const { return m_config; }
    void Reset();

    // Feed frame data
    void RecordFrame(double frameTimeMs, double cpuTimeMs, double gpuTimeMs);
    
    // Analysis
    JitterAnalysis Analyze();
    const std::vector<JitterEvent>& GetEvents() const { return m_events; }
    
    // Real-time event detection
    bool IsInJitterEvent() const { return m_currentEventActive; }
    JitterEvent GetCurrentEvent() const { return m_currentEvent; }
    
    // Export
    std::string ExportToJSON() const;
    std::string ExportEventsToJSON() const;

private:
    void DetectPatterns();
    void DetectPeriodicJitter();
    void DetectSporadicSpikes();
    void DetectGradualDrift();
    void DetectBursts();
    void DetectStuttering();
    
    void ClassifyRootCause(JitterEvent& event);
    void GenerateRecommendations();
    
    double CalculateJitterMetric() const;
    double CalculateJitterCoefficient() const;
    std::vector<int> FindPeaks(double threshold) const;
    int FindPeriodicityStrength(const std::vector<int>& peaks) const;

private:
    JitterConfig m_config;
    
    // Rolling buffer for frame times
    std::deque<double> m_frameTimes;
    std::deque<double> m_cpuTimes;
    std::deque<double> m_gpuTimes;
    
    // Detected events
    std::vector<JitterEvent> m_events;
    JitterAnalysis m_lastAnalysis;
    
    // Real-time detection state
    bool m_currentEventActive;
    JitterEvent m_currentEvent;
    int m_consecutiveSlowFrames;
    double m_accumulatedJitter;
    
    // Pattern detection state
    std::deque<double> m_recentJitter;
    int m_lastEventFrame;
};

} // namespace ProfilerCore
