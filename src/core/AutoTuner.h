#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <chrono>

namespace ProfilerCore {

// Forward declarations — include corresponding .h files in .cpp for definitions
enum class SpikeCause;       // FrameSpikeAnalyzer.h
enum class SpikeSeverity;    // FrameSpikeAnalyzer.h
struct SpikeStatistics;       // FrameSpikeAnalyzer.h
struct SpikePattern;         // FrameSpikeAnalyzer.h
enum class ThrottleType;     // ThermalMonitor.h
struct ThermalSnapshot;       // ThermalMonitor.h
enum class NetworkQuality;   // NetworkProfiler.h
struct NetworkReport;         // NetworkProfiler.h
enum class MemoryPressure;   // MemoryAnalyzer.h
struct MemoryReport;         // MemoryAnalyzer.h
struct MemoryLeak;           // MemoryAnalyzer.h
struct GPUSummaryStats;      // GPUProfiler.h

// Module classes
class FrameSpikeAnalyzer;
class GPUProfiler;
class MemoryAnalyzer;
class NetworkProfiler;
class ThermalMonitor;
class PerformanceScorer;
class StatisticsAnalyzer;

}

namespace ProfilerCore {

// ─── Optimization Categories ────────────────────────────────────────────────

enum class OptimizationCategory {
    Rendering,        // Draw calls, shaders, textures, VRAM
    Memory,           // Allocations, leaks, caching
    CPU,              // Logic, AI, physics, script
    GPU,              // GPU bottlenecks, fill rate, shader complexity
    Network,          // Latency, bandwidth, synchronization
    Threading,        // Parallelism, job scheduling, locks
    Assets,          // Loading, streaming, compression
    General           // Cross-cutting concerns
};

// ─── Priority Levels ─────────────────────────────────────────────────────────

enum class OptimizationPriority {
    Critical = 0,  // Fix immediately (causing crashes/major issues)
    High = 1,     // Address soon (measurable performance impact)
    Medium = 2,    // Consider for next sprint
    Low = 3       // Nice to have
};

// ─── Impact Estimate ─────────────────────────────────────────────────────────

enum class ImpactLevel {
    Unknown = 0,
    Minor = 1,       // < 2 FPS improvement expected
    Moderate = 2,    // 2-5 FPS improvement expected
    Significant = 3,  // 5-10 FPS improvement expected
    Major = 4        // > 10 FPS improvement expected
};

// ─── Target Platform ─────────────────────────────────────────────────────────

enum class TargetPlatform {
    Desktop,     // High-end PC
    Laptop,      // Mobile/constrained PC
    Console,     // Fixed hardware (PS5, Xbox)
    Mobile       // Phone/tablet
};

// ─── Individual Recommendation ────────────────────────────────────────────────

struct OptimizationRecommendation {
    int64_t id;                    // Unique ID

    // Classification
    OptimizationCategory category;
    OptimizationPriority priority;
    ImpactLevel estimatedImpact;
    std::string title;             // Short, actionable title

    // Detailed description
    std::string description;       // What the issue is
    std::string rationale;        // Why this matters
    std::string expectedOutcome;   // What improvement to expect

    // Source of the recommendation
    std::string sourceModule;      // Which analyzer generated this
    std::string sourceMetric;      // Which metric triggered this
    double sourceValue;            // The measured value
    double threshold;              // The threshold that was crossed

    // Timestamps
    int64_t createdAt;             // When recommendation was created
    int64_t lastUpdated;          // When it was last refreshed
    int64_t acknowledgedAt;        // When user acknowledged it

    // Tracking
    bool isAcknowledged;
    bool isImplemented;            // User marked as implemented
    int64_t implementedAt;
    bool isDismissed;               // User explicitly dismissed it

    // Effectiveness tracking
    double preImplementationValue; // Metric value before fix
    double postImplementationValue; // Metric value after fix (if implemented)
    bool effectivenessVerified;

    // Metadata
    int relatedAlertCount;         // How many alerts this addresses
    std::vector<std::string> affectedSystems;
    std::string difficulty;       // "Easy", "Medium", "Hard", "Requires Architecture Change"
    std::string estimatedTime;     // "5 min", "2 hours", "1 day", "1 week"
};

// ─── Recommendation Bundle (grouped by theme) ─────────────────────────────────

struct RecommendationBundle {
    OptimizationCategory category;
    std::string categoryTitle;
    std::string summary;
    std::vector<OptimizationRecommendation> recommendations;
    double totalImpactScore;       // Combined impact of all items in bundle
    int criticalCount;
    int highCount;
    bool isActionable;             // True if all prerequisites are met
};

// ─── Auto-Tuning Session ─────────────────────────────────────────────────────

struct AutoTunerSession {
    int64_t sessionId;
    int64_t startTime;
    int64_t endTime;
    int64_t totalFramesAnalyzed;
    TargetPlatform targetPlatform;

    // Stats
    int totalRecommendations;
    int criticalRecommendations;
    int implementedCount;
    double averageImpactScore;

    // Outcome
    double preSessionScore;
    double postSessionScore;
    double scoreImprovement;
};

// ─── Configuration ───────────────────────────────────────────────────────────

struct AutoTunerConfig {
    TargetPlatform platform = TargetPlatform::Desktop;

    // Thresholds
    double fpsDropThreshold = 5.0;          // Report if FPS drops by this %
    double spikeRateThreshold = 0.1;        // Report if >10% frames spike
    double memoryGrowthThreshold = 1.0;     // MB per second
    double cpuBottleneckThreshold = 0.5;   // 50% CPU bottleneck score
    double gpuBottleneckThreshold = 0.5;   // 50% GPU bottleneck score
    double networkLatencyThreshold = 100.0;// ms RTT
    double minGpuBottleneckThreshold = 0.5; // Below this = underutilization
    double maxVSyncWaitMs = 5.0;          // Above this = problem
    double targetRttMs = 50.0;            // Target round-trip time
    double maxJitterMs = 30.0;            // Max acceptable jitter
    double bandwidthWarningBps = 10.0 * 1024 * 1024;   // 10 MB/s warning
    double bandwidthCriticalBps = 50.0 * 1024 * 1024;  // 50 MB/s critical

    // Thermal thresholds (Celsius)
    double cpuTempWarningC = 85.0;
    double cpuTempCriticalC = 95.0;
    double gpuTempWarningC = 80.0;
    double gpuTempCriticalC = 90.0;

    // Filters
    int minImpactScore = 20;               // Only report if impact >= this
    bool includeImplemented = false;        // Include already-implemented recs
    bool includeDismissed = false;          // Include dismissed recs
    bool sortByPriority = true;            // Sort by priority vs impact

    // Platform-specific FPS targets
    double desktopTargetFps = 60.0;
    double laptopTargetFps = 60.0;
    double consoleTargetFps = 60.0;
    double mobileTargetFps = 30.0;

    size_t maxRecommendations = 50;         // Cap total recommendations
    int analysisWindowFrames = 300;         // Frames to analyze
};

// ─── Callback Types ───────────────────────────────────────────────────────────

using RecommendationCallback = std::function<void(const OptimizationRecommendation&)>;
using BundleCallback = std::function<void(const RecommendationBundle&)>;
using SessionCallback = std::function<void(const AutoTunerSession&)>;

// ─── AutoTuner ───────────────────────────────────────────────────────────────

/**
 * AutoTuner - Automated performance optimization advisor
 *
 * Synthesizes insights from all profiling modules to generate
 * prioritized, actionable optimization recommendations.
 *
 * Features:
 * - Cross-module analysis (FPS, GPU, memory, network, thermal)
 * - Prioritization by impact and urgency
 * - Platform-specific recommendations
 * - Effectiveness tracking (pre/post implementation)
 * - Recommendation bundling by category
 * - Confidence scoring based on data quality
 * - Historical trend analysis
 * - Auto-dismissal of resolved issues
 * - Export to JSON for dashboard consumption
 *
 * Usage:
 *   AutoTuner tuner;
 *   tuner.Configure(config);
 *   tuner.Analyze();
 *   auto recs = tuner.GetRecommendations();
 *   auto bundles = tuner.GetBundles();
 */
class AutoTuner {
public:
    AutoTuner();
    ~AutoTuner();

    // ─── Configuration ───────────────────────────────────────────────────────

    void Configure(const AutoTunerConfig& config);
    AutoTunerConfig GetConfig() const { return m_config; }
    void SetTargetPlatform(TargetPlatform platform);

    // ─── Analysis ───────────────────────────────────────────────────────────

    /** Run full analysis across all available profiler modules */
    void Analyze();

    /** Quick analysis targeting a specific category */
    void AnalyzeCategory(OptimizationCategory category);

    /** Feed a custom metric for consideration */
    void RecordMetric(const std::string& name, double value, double threshold,
                      const std::string& sourceModule);

    // ─── Recommendation Access ──────────────────────────────────────────────

    /** All current recommendations sorted by priority */
    const std::vector<OptimizationRecommendation>& GetRecommendations() const {
        return m_recommendations;
    }

    /** Recommendations filtered by category */
    std::vector<OptimizationRecommendation> GetRecommendationsByCategory(
        OptimizationCategory category) const;

    /** Recommendations filtered by priority */
    std::vector<OptimizationRecommendation> GetRecommendationsByPriority(
        OptimizationPriority priority) const;

    /** Unacknowledged recommendations that need attention */
    std::vector<OptimizationRecommendation> GetPendingRecommendations() const;

    /** Recommendations that have been implemented */
    std::vector<OptimizationRecommendation> GetImplementedRecommendations() const;

    /** Grouped by category with summaries */
    std::vector<RecommendationBundle> GetBundles() const;

    /** Top N recommendations by impact */
    std::vector<OptimizationRecommendation> GetTopRecommendations(int count) const;

    // ─── Recommendation Management ─────────────────────────────────────────

    /** Get a specific recommendation by ID */
    OptimizationRecommendation GetRecommendation(int64_t id) const;

    /** User acknowledges they've seen a recommendation */
    bool AcknowledgeRecommendation(int64_t id);

    /** User marks a recommendation as implemented */
    bool MarkAsImplemented(int64_t id, double postImplementationValue);

    /** User dismisses a recommendation (won't show again) */
    bool DismissRecommendation(int64_t id);

    /** Reset all acknowledgments and dismissals */
    void ResetTracking();

    // ─── Sessions ───────────────────────────────────────────────────────────

    /** Start a tuning session (for tracking before/after) */
    int64_t StartSession();

    /** End current session and record results */
    void EndSession();

    /** Get session history */
    const std::vector<AutoTunerSession>& GetSessions() const { return m_sessions; }

    /** Get current active session */
    AutoTunerSession GetCurrentSession() const;

    // ─── Callbacks ──────────────────────────────────────────────────────────

    void SetRecommendationCallback(RecommendationCallback cb);
    void SetBundleCallback(BundleCallback cb);
    void SetSessionCallback(SessionCallback cb);

    // ─── Export ─────────────────────────────────────────────────────────────

    std::string ExportRecommendationsToJSON() const;
    std::string ExportBundlesToJSON() const;
    std::string ExportSessionsToJSON() const;
    std::string ExportFullReport() const;

    // ─── Reset ──────────────────────────────────────────────────────────────

    void ClearRecommendations();
    void Reset();

private:
    // Analysis helpers
    void AnalyzeFrameTime();
    void AnalyzeGPU();
    void AnalyzeMemory();
    void AnalyzeNetwork();
    void AnalyzeThermal();
    void AnalyzeBottlenecks();
    void AnalyzeStability();

    // Recommendation generation
    void GenerateFPSRecommendations();
    void GenerateGPURecommendations();
    void GenerateMemoryRecommendations();
    void GenerateNetworkRecommendations();
    void GenerateThermalRecommendations();
    void GenerateStabilityRecommendations();
    void GenerateRenderingRecommendations();
    void GenerateThreadingRecommendations();

    void AddRecommendation(const OptimizationRecommendation& rec);
    void UpdateExistingRecommendation(int64_t id, const OptimizationRecommendation& updated);

    // Helper methods
    OptimizationPriority DeterminePriority(double severity, double impact) const;
    ImpactLevel EstimateImpact(double metricDelta, OptimizationCategory category) const;
    std::string EstimateTime(double severity, OptimizationCategory category) const;
    std::string EstimateDifficulty(OptimizationCategory category, double severity) const;
    void UpdateBundles();
    int64_t GenerateRecommendationId();
    int64_t GetCurrentTimestamp() const;
    double GetTargetFPS() const;
    double ComputeOverallScore() const;

    OptimizationRecommendation CreateRecommendation(
        OptimizationCategory category,
        OptimizationPriority priority,
        const std::string& title,
        const std::string& description,
        const std::string& rationale,
        const std::string& expectedOutcome,
        const std::string& sourceModule,
        const std::string& sourceMetric,
        double sourceValue,
        double threshold) const;

    std::string CategoryToString(OptimizationCategory cat) const;
    std::string PriorityToString(OptimizationPriority pri) const;
    std::string ImpactToString(ImpactLevel impact) const;
    std::string SpikeCauseToString(SpikeCause cause) const;

private:
    AutoTunerConfig m_config;

    // Recommendations
    std::vector<OptimizationRecommendation> m_recommendations;
    std::unordered_map<int64_t, size_t> m_recIndex;  // id -> index

    // Bundles (cached)
    std::vector<RecommendationBundle> m_bundles;

    // Sessions
    std::vector<AutoTunerSession> m_sessions;
    int64_t m_activeSessionId = 0;
    double m_sessionStartScore = 0.0;

    // Custom metrics (user-provided)
    struct CustomMetric {
        std::string name;
        double value;
        double threshold;
        std::string sourceModule;
        int64_t timestamp;
    };
    std::vector<CustomMetric> m_customMetrics;

    // Callbacks
    RecommendationCallback m_recCallback;
    BundleCallback m_bundleCallback;
    SessionCallback m_sessionCallback;

    // ID generator
    int64_t m_nextRecId = 1;
};

} // namespace ProfilerCore