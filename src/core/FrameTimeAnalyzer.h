#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace ProfilerCore {

/**
 * Frame time spike severity levels
 */
enum class SpikeSeverity {
    Minor = 0,      // < 2x average frame time
    Moderate = 1,   // 2-3x average frame time
    Severe = 2,     // 3-5x average frame time
    Critical = 3    // > 5x average frame time
};

/**
 * Frame time distribution bucket
 */
struct FrameTimeBucket {
    double lowerBound;      // ms
    double upperBound;      // ms
    int count;              // number of frames in this bucket
    double percentage;      // percentage of total frames
    std::string label;      // human-readable label (e.g., "16.67ms (60 FPS)")
};

/**
 * A single frame time spike event
 */
struct FrameTimeSpike {
    int frameNumber;
    int64_t timestamp;
    double frameTime;           // ms
    double averageFrameTime;    // average before spike
    double spikeRatio;          // frameTime / averageFrameTime
    SpikeSeverity severity;
    std::string possibleCause;  // heuristic-based cause prediction
};

/**
 * Frame time pattern type
 */
enum class FrameTimePattern {
    Stable,             // Consistent frame times
    PeriodicSpikes,     // Regular spikes (GC, fixed timestep)
    BurstSpikes,        // Clustered spikes (loading, scene transition)
    GradualDegradation, // Gradually increasing frame times
    Erratic             // No clear pattern
};

/**
 * Pattern analysis result
 */
struct PatternAnalysis {
    FrameTimePattern pattern;
    double confidence;          // 0-1 confidence score
    double spikeInterval;        // ms between spikes (if periodic)
    int burstCount;              // number of burst events detected
    double degradationRate;      // ms/frame increase rate (if degrading)
    std::string description;     // human-readable analysis
};

/**
 * Comprehensive frame time analysis report
 */
struct FrameTimeReport {
    int64_t timestamp;
    int totalFrames;
    
    // Distribution
    std::vector<FrameTimeBucket> distribution;
    double expectedFrameTime;    // target frame time (16.67ms for 60 FPS)
    double actualFrameTime;      // measured average
    
    // Spike analysis
    int spikeCount;
    int minorSpikes;
    int moderateSpikes;
    int severeSpikes;
    int criticalSpikes;
    std::vector<FrameTimeSpike> topSpikes; // worst 10 spikes
    
    // Pattern analysis
    PatternAnalysis pattern;
    
    // Performance scores
    double smoothnessScore;      // 0-100, based on consistency
    double responsivenessScore;  // 0-100, based on tail latency
    double overallScore;         // weighted average
    
    // Recommendations
    std::vector<std::string> recommendations;
};

/**
 * FrameTimeAnalyzer - deep frame time analysis and spike detection
 * 
 * Features:
 * - Frame time distribution histogram with FPS-equivalent labels
 * - Spike detection with severity classification
 * - Pattern recognition (periodic spikes, burst spikes, degradation)
 * - Cause prediction heuristics (GC, loading, thermal throttling)
 * - Performance scoring (smoothness, responsiveness, overall)
 * - Actionable recommendations based on analysis
 */
class FrameTimeAnalyzer {
public:
    FrameTimeAnalyzer();
    ~FrameTimeAnalyzer();
    
    // Configuration
    void SetTargetFPS(double targetFPS);
    void SetWindowSize(size_t windowSize);
    void SetSpikeThresholds(double minor, double moderate, double severe, double critical);
    void Reset();
    
    // Feed frame data
    void RecordFrame(int frameNumber, double frameTimeMs);
    
    // Generate analysis report
    FrameTimeReport GenerateReport() const;
    
    // Quick accessors
    double GetCurrentAverage() const;
    double GetCurrentStdDev() const;
    int GetSpikeCount() const;
    
    // Export
    std::string ExportToJSON() const;

private:
    void ComputeDistribution() const;
    void DetectSpikes() const;
    void AnalyzePattern() const;
    void ComputeScores() const;
    void GenerateRecommendations() const;
    
    std::string GetSpikeCauseHeuristic(double spikeRatio, double avgFrameTime, double frameTime) const;
    std::string GetPatternDescription(FrameTimePattern pattern, const PatternAnalysis& analysis) const;
    
    // Configuration
    double m_targetFPS;
    double m_targetFrameTime;
    size_t m_windowSize;
    
    // Spike thresholds (multipliers of average)
    double m_spikeMinor;
    double m_spikeModerate;
    double m_spikeSevere;
    double m_spikeCritical;
    
    // Raw data
    struct FrameEntry {
        int frameNumber;
        double frameTime;
        int64_t timestamp;
    };
    std::vector<FrameEntry> m_frames;
    
    // Cached analysis
    mutable FrameTimeReport m_cachedReport;
    mutable bool m_dirty;
    
    // Statistics
    double m_runningSum;
    double m_runningSumSq;
    int m_totalSamples;
};

} // namespace ProfilerCore
