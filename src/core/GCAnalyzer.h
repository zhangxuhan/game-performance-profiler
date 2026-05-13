#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstdint>

namespace ProfilerCore {

// ─── Enumerations ─────────────────────────────────────────────────────────────

/**
 * GC generation level
 */
enum class GCGeneration {
    Generation0 = 0,   // Short-lived object collection
    Generation1 = 1,    // Medium-lived object collection
    Generation2 = 2,    // Full heap collection
    Unknown = -1
};

/**
 * GC trigger cause
 */
enum class GCTrigger {
    Allocation,         // Normal allocation-triggered
    Forced,             // GC.Collect() called
    MemoryPressure,     // Low memory triggered
    AppDomainUnload,     // AppDomain unload
    CodePitch,          // Code deallocation
    ShuttingDown,       // Application shutdown
    Finalizer,          // Finalizer triggered
    Blocking,           // Blocking GC (synchronous)
    Background,         // Background GC
    Emptying,           // Emptying collections
    LowMemory,          // Low memory notification
    Unknown
};

/**
 * GC severity level
 */
enum class GCSeverity {
    Negligible = 0,  // < 1ms pause
    Minor = 1,       // 1-5ms pause
    Moderate = 2,    // 5-15ms pause
    Severe = 3,      // 15-50ms pause
    Critical = 4,    // > 50ms pause (will cause visible hitch)
    Unknown = -1
};

// ─── Data Structures ──────────────────────────────────────────────────────────

/**
 * A single garbage collection event
 */
struct GCEvent {
    int64_t timestamp;         // Microseconds since session start
    int64_t endTimestamp;      // When GC finished

    // Generation info
    GCGeneration generation;
    int generationIndex;        // Which collection number this is for this gen

    // Pause timing
    double pauseDurationMs;     // Total pause duration
    double pausePercent;        // % of frame time consumed by this GC
    double concurrentDurationMs; // Concurrent phase duration (if background GC)

    // Memory state before GC
    size_t heapSizeBeforeBytes;
    size_t highMemoryLoadBeforeBytes;
    size_t fragmentedBytes;

    // Memory state after GC
    size_t heapSizeAfterBytes;
    size_t promotedBytes;       // Bytes promoted to older generation

    // Fragmentation
    double fragmentationPercent;

    // Trigger
    GCTrigger trigger;

    // Severity
    GCSeverity severity;

    // Frame impact
    int affectedFrameCount;     // Number of frames this GC impacted
    double frameImpactMs;       // Total frame time impact in ms
    bool causedSpike;           // True if this GC caused a frame spike (>16.67ms)

    // Metadata
    int threadId;               // Thread that triggered GC
    bool isCompacting;          // Was this a compacting GC?
    std::string details;        // Additional info string

    bool isValid;
};

/**
 * GC statistics for a time window
 */
struct GCStatistics {
    int64_t timestamp;          // Analysis timestamp

    // Event counts
    int totalEvents;
    int gen0Events;
    int gen1Events;
    int gen2Events;

    // Pause duration statistics
    double totalPauseTimeMs;
    double avgPauseMs;
    double maxPauseMs;
    double minPauseMs;
    double medianPauseMs;
    double stdDevPauseMs;
    double p90PauseMs;
    double p95PauseMs;
    double p99PauseMs;

    // Frequency statistics
    double eventsPerMinute;
    double eventsPerFrame;      // GC events per game frame (averaged)
    double pauseTimePerMinuteMs;
    double pauseTimePerFrameMs;

    // Memory impact
    double totalPromotedBytes;
    double avgFragmentationPercent;
    double peakFragmentation;
    size_t peakHeapSizeBytes;
    size_t avgHeapSizeBytes;

    // Severity distribution
    int negligibleCount;
    int minorCount;
    int moderateCount;
    int severeCount;
    int criticalCount;

    // Frame impact
    double frameHitchCount;     // Number of frames with > 16ms pause
    double totalFrameImpactMs;  // Sum of frame time lost to GC
    double avgFrameImpactMs;    // Average frame impact per GC event

    // Trend
    double pauseTrend;          // Pause time trend (positive = worsening)
    double frequencyTrend;      // GC frequency trend
    int consecutiveHighGcFrames; // Frames with consecutive high GC

    // Memory pressure
    double heapGrowthRateBytesPerFrame;
    double gen2PromotionRateBytesPerMinute;
};

/**
 * Allocation hotspot (location with high allocation rate)
 */
struct AllocationHotspot {
    std::string name;           // Function/area name
    std::string category;       // "Rendering", "Physics", "Logic", "UI", etc.

    // Allocation stats
    int64_t totalAllocations;   // Count
    size_t totalBytes;          // Total bytes allocated
    double avgObjectSizeBytes;
    int allocationCount;

    // GC impact
    int gcTriggers;             // How many GCs this allocation triggered
    double pauseTimeMs;         // Total pause time from these allocations
    double pauseTimePercent;    // % of total GC pause time

    // Frequency
    double allocationRatePerFrame;
    double allocationRateBytesPerSec;

    // Recommendations
    std::string recommendation;

    // Severity
    GCSeverity severity;
};

/**
 * GC analysis report
 */
struct GCReport {
    int64_t timestamp;
    int64_t sessionDurationMs;

    // Overall assessment
    double healthScore;         // 0-100, higher = better
    std::string assessment;     // "Healthy", "Warning", "Critical"
    GCSeverity overallSeverity;

    // Statistics
    GCStatistics stats;

    // Recent events
    std::vector<GCEvent> recentEvents;
    std::vector<GCEvent> worstEvents;  // Top 10 worst GC events

    // Allocation hotspots
    std::vector<AllocationHotspot> hotspots;

    // Common patterns
    std::vector<std::string> detectedPatterns;

    // Recommendations
    std::vector<std::string> recommendations;
    std::vector<std::string> urgentRecommendations;

    // Comparison to baseline
    double improvementVsBaseline;  // % improvement vs baseline
    bool hasRegressed;
    std::string regressionNote;
};

/**
 * GC configuration
 */
struct GCAnalyzerConfig {
    bool enabled = true;

    // Detection thresholds
    double minPauseThresholdMs = 0.5;     // Min pause to count as GC event
    double spikeThresholdMs = 16.67;       // Pause that causes a frame spike
    double severePauseMs = 10.0;          // Pause considered "severe"

    // Analysis window
    size_t maxEventHistory = 1000;         // Max events to keep
    size_t analysisWindowFrames = 600;     // Frames to analyze for statistics

    // Allocation tracking
    bool trackAllocations = true;
    size_t maxHotspots = 20;
    int minAllocationsToTrack = 100;       // Min allocations to report hotspot

    // Severity thresholds
    double negligibleThresholdMs = 1.0;
    double minorThresholdMs = 5.0;
    double moderateThresholdMs = 15.0;
    double severeThresholdMs = 50.0;

    // Alerts
    bool generateAlerts = true;
    int alertThresholdCount = 3;           // Alert after N high-GC frames in a row
    double alertPauseThresholdMs = 10.0;   // "High GC" = above this

    // Recommendations
    bool generateRecommendations = true;
    double hotspotMinPausePercent = 10.0;  // Only report if >10% of total pause time
};

// ─── Main Class ───────────────────────────────────────────────────────────────

/**
 * GCAnalyzer - Dedicated garbage collection performance analysis
 *
 * Features:
 * - Detect and classify GC events (gen0/gen1/gen2)
 * - Measure pause durations with frame impact
 * - Identify allocation hotspots driving GC pressure
 * - Track GC frequency and trends over time
 * - Generate actionable recommendations
 * - Health scoring for GC performance
 * - Integration with ProfilerCore for unified analysis
 *
 * Usage:
 *   auto* gc = profilerCore->GetGCAnalyzer();
 *   gc->RecordAllocation("RenderSystem", "Texture", 1024);
 *   gc->RecordGCEvent(gen, pauseMs, trigger);
 *   auto report = gc->GenerateReport();
 */
class GCAnalyzer {
public:
    GCAnalyzer();
    ~GCAnalyzer();

    // Configuration
    void SetConfig(const GCAnalyzerConfig& config);
    const GCAnalyzerConfig& GetConfig() const { return m_config; }
    void Reset();

    // ─── GC Event Recording ──────────────────────────────────────────────────

    /**
     * Record a GC event
     * @param generation  Which GC generation (0, 1, or 2)
     * @param pauseMs     Total pause duration in milliseconds
     * @param trigger     What triggered this GC
     * @param heapBefore  Heap size before GC (bytes)
     * @param heapAfter   Heap size after GC (bytes)
     * @param promoted    Bytes promoted to older generation
     */
    void RecordGCEvent(
        GCGeneration generation,
        double pauseMs,
        GCTrigger trigger,
        size_t heapBefore,
        size_t heapAfter,
        size_t promoted,
        bool isCompacting = false,
        int threadId = 0
    );

    /**
     * Record GC pause from external profiler (e.g., .NET ETW events)
     */
    void RecordExternalGCEvent(
        int64_t startTimestamp,
        int64_t endTimestamp,
        int generation,
        double pauseMs,
        const std::string& reason
    );

    // ─── Allocation Tracking ─────────────────────────────────────────────────

    /**
     * Record an allocation at a named location
     * @param category  Broad category (Rendering, Physics, Logic, etc.)
     * @param name       Specific location name (function/area)
     * @param sizeBytes  Size of the allocation
     */
    void RecordAllocation(
        const std::string& category,
        const std::string& name,
        size_t sizeBytes
    );

    /**
     * Record a batch of allocations from the same location
     */
    void RecordAllocationBatch(
        const std::string& category,
        const std::string& name,
        size_t totalBytes,
        int count
    );

    // ─── Analysis ─────────────────────────────────────────────────────────────

    /**
     * Get current GC statistics
     */
    GCStatistics GetStatistics();

    /**
     * Generate a full GC analysis report
     */
    GCReport GenerateReport();

    /**
     * Get events within a time range
     */
    std::vector<GCEvent> GetEventsInRange(int64_t startUs, int64_t endUs) const;

    /**
     * Get the most recent events
     */
    std::vector<GCEvent> GetRecentEvents(int count = 20) const;

    /**
     * Get worst GC events (by pause duration)
     */
    std::vector<GCEvent> GetWorstEvents(int count = 10) const;

    // ─── Allocation Hotspots ─────────────────────────────────────────────────

    /**
     * Get identified allocation hotspots
     */
    std::vector<AllocationHotspot> GetAllocationHotspots() const;

    /**
     * Clear allocation tracking data
     */
    void ClearAllocationData();

    // ─── Health & Assessment ──────────────────────────────────────────────────

    /**
     * Get overall GC health score (0-100)
     */
    double GetHealthScore() const;

    /**
     * Get current overall severity
     */
    GCSeverity GetOverallSeverity() const;

    /**
     * Check if GC is causing frame spikes
     */
    bool IsCausingFrameSpikes() const;

    /**
     * Get the primary GC bottleneck category
     */
    std::string GetPrimaryBottleneck() const;

    // ─── Recommendations ─────────────────────────────────────────────────────

    /**
     * Get GC optimization recommendations
     */
    std::vector<std::string> GetRecommendations() const;

    /**
     * Get urgent recommendations (things that need immediate attention)
     */
    std::vector<std::string> GetUrgentRecommendations() const;

    // ─── Pattern Detection ───────────────────────────────────────────────────

    /**
     * Detect recurring GC patterns
     */
    std::vector<std::string> DetectPatterns() const;

    /**
     * Check if there's a memory leak pattern
     */
    bool HasMemoryLeakPattern() const;

    /**
     * Check if fragmentation is building up
     */
    bool IsFragmentationBuilding() const;

    // ─── Export ──────────────────────────────────────────────────────────────

    /**
     * Export analysis to JSON
     */
    std::string ExportToJSON() const;

    /**
     * Export GC events to JSON
     */
    std::string ExportEventsToJSON() const;

    /**
     * Export statistics to JSON
     */
    std::string ExportStatisticsToJSON() const;

    // ─── Integration Helpers ─────────────────────────────────────────────────

    /**
     * Feed frame data for correlation analysis
     * Call this each frame to let GC analyzer track frame impact
     */
    void OnFrameEnd(int frameNumber, double frameTimeMs, int64_t timestampUs);

    /**
     * Set the baseline for comparison
     */
    void SetBaseline(const GCStatistics& baseline);

    /**
     * Compare current stats to baseline
     */
    double CompareToBaseline() const;

    // ─── Callbacks ───────────────────────────────────────────────────────────

    using GCAlertCallback = std::function<void(
        const std::string& alertType,
        const std::string& message,
        const GCEvent& event
    )>;

    void SetAlertCallback(GCAlertCallback cb) { m_alertCallback = std::move(cb); }

private:
    // Internal methods
    void UpdateStatistics();
    void AnalyzeAllocations();
    void DetectHotspots();
    void GenerateRecommendationsInternal();
    void CheckAlerts();

    GCSeverity ClassifySeverity(double pauseMs) const;
    GCGeneration ParseGeneration(int gen) const;
    std::string SeverityToString(GCSeverity severity) const;
    std::string TriggerToString(GCTrigger trigger) const;
    std::string GenerationToString(GCGeneration gen) const;

    int64_t GetCurrentTimestamp() const;
    double CalculateTrend(const std::vector<double>& values) const;
    double CalculateMedian(std::vector<double> values) const;

private:
    GCAnalyzerConfig m_config;

    // GC events
    std::vector<GCEvent> m_events;
    size_t m_eventHead = 0;

    // Statistics
    GCStatistics m_stats;

    // Allocation tracking
    struct AllocationBucket {
        std::string category;
        std::string name;
        int64_t totalAllocations = 0;
        size_t totalBytes = 0;
        int gcTriggers = 0;
        double totalPauseMs = 0.0;
    };
    std::unordered_map<std::string, AllocationBucket> m_allocationMap;

    // Frame tracking for correlation
    int m_currentFrame = 0;
    double m_currentFrameTime = 0.0;
    int m_consecutiveHighGcFrames = 0;
    double m_accumulatedPauseMs = 0.0;

    // Alerts
    std::vector<std::string> m_recommendations;
    std::vector<std::string> m_urgentRecommendations;
    GCAlertCallback m_alertCallback;

    // Baseline for comparison
    bool m_hasBaseline = false;
    GCStatistics m_baseline;

    // Mutex for thread safety
    mutable std::mutex m_mutex;
};

// ─── Inline Helper Functions ───────────────────────────────────────────────────

inline GCSeverity GCAnalyzer::ClassifySeverity(double pauseMs) const {
    if (pauseMs >= m_config.severeThresholdMs) return GCSeverity::Critical;
    if (pauseMs >= m_config.moderateThresholdMs) return GCSeverity::Severe;
    if (pauseMs >= m_config.minorThresholdMs) return GCSeverity::Moderate;
    if (pauseMs >= m_config.minPauseThresholdMs) return GCSeverity::Minor;
    return GCSeverity::Negligible;
}

} // namespace ProfilerCore