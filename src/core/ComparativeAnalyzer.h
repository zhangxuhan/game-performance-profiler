#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace ProfilerCore {

// Forward declarations
struct TrendDataPoint;
struct SummaryStats;

/**
 * Comparison result for a single metric
 */
struct MetricComparison {
    std::string name;             // Metric name (e.g., "avgFPS", "peakMemory")
    double baselineValue;         // Value from baseline session
    double currentValue;          // Value from current session
    double absoluteDelta;         // currentValue - baselineValue
    double percentChange;         // Percent change (positive = regression for "lower is better" metrics)
    bool isRegression;            // True if change is undesirable
    double severity;              // 0.0 = negligible, 1.0 = critical
    std::string interpretation;   // Human-readable interpretation
    
    std::string ToJSON() const;
};

/**
 * Comparison category for grouping related metrics
 */
enum class ComparisonCategory {
    FPS,           // Frame rate metrics
    FrameTime,     // Frame time metrics
    Memory,        // Memory usage metrics
    Stability,     // Stability and consistency metrics
    GPU,           // GPU-related metrics
    Overall        // Overall score comparison
};

/**
 * Aggregated comparison result for a category
 */
struct CategoryComparison {
    ComparisonCategory category;
    std::string categoryName;
    std::vector<MetricComparison> metrics;
    double overallScore;          // 0-100, higher = better relative to baseline
    bool hasRegressions;
    int regressionCount;
    std::string summary;          // Brief summary text
    
    std::string ToJSON() const;
};

/**
 * Full comparison report between two sessions or time periods
 */
struct ComparisonReport {
    std::string baselineId;       // Session ID or "period_start"
    std::string currentId;        // Session ID or "period_end"
    int64_t baselineTimestamp;
    int64_t currentTimestamp;
    int baselineSampleCount;
    int currentSampleCount;
    
    std::vector<CategoryComparison> categories;
    
    // Overall assessment
    double overallScoreDelta;     // Change in overall performance score
    bool isOverallRegression;
    std::string verdict;          // "IMPROVED", "REGRESSED", "UNCHANGED", "INCONCLUSIVE"
    std::string summary;          // Executive summary
    
    // Recommendations based on comparison
    std::vector<std::string> recommendations;
    
    std::string ToJSON() const;
};

/**
 * Regression detection configuration
 */
struct RegressionThresholds {
    // FPS regression thresholds
    double fpsRegressionWarningPercent = 5.0;     // 5% drop = warning
    double fpsRegressionCriticalPercent = 15.0;   // 15% drop = critical
    
    // Frame time regression thresholds
    double frameTimeRegressionWarningPercent = 10.0;
    double frameTimeRegressionCriticalPercent = 25.0;
    
    // Memory regression thresholds
    double memoryRegressionWarningPercent = 10.0;
    double memoryRegressionCriticalPercent = 30.0;
    
    // Stability regression thresholds
    double stabilityRegressionWarningPoints = 10.0;  // Points dropped
    double stabilityRegressionCriticalPoints = 25.0;
    
    // Minimum samples for valid comparison
    int minSamplesForComparison = 30;
    
    // Statistical significance threshold (p-value)
    double significanceLevel = 0.05;
};

/**
 * Historical comparison data point for tracking changes over time
 */
struct HistoricalComparisonPoint {
    int64_t timestamp;
    std::string sessionId;
    double overallScore;
    double avgFps;
    double avgFrameTime;
    size_t peakMemory;
    double stabilityScore;
    int alertCount;
    
    std::string ToJSON() const;
};

/**
 * Trend analysis for a metric over multiple comparison points
 */
struct MetricTrendOverTime {
    std::string metricName;
    std::vector<std::pair<int64_t, double>> dataPoints;  // timestamp -> value
    double slope;                // Trend direction
    double rSquared;             // Fit quality
    bool isImproving;
    std::string trendDescription;
    
    std::string ToJSON() const;
};

/**
 * ComparativeAnalyzer - Compare performance across sessions and time periods
 * 
 * Features:
 * - Compare two profiling sessions
 * - Compare time periods within a session
 * - Regression detection with configurable thresholds
 * - Historical tracking of performance over time
 * - Statistical significance testing
 * - Automatic recommendation generation
 * - Trend analysis across multiple sessions
 */
class ComparativeAnalyzer {
public:
    ComparativeAnalyzer();
    ~ComparativeAnalyzer();
    
    // Configuration
    void SetRegressionThresholds(const RegressionThresholds& thresholds);
    const RegressionThresholds& GetRegressionThresholds() const { return m_thresholds; }
    
    // === Session Comparison ===
    
    /**
     * Compare two sets of statistics
     * @param baselineStats Statistics from the baseline period/session
     * @param currentStats Statistics from the current period/session
     * @param baselineId Identifier for baseline (session ID or description)
     * @param currentId Identifier for current (session ID or description)
     * @return Full comparison report
     */
    ComparisonReport CompareStats(
        const SummaryStats& baselineStats,
        const SummaryStats& currentStats,
        const std::string& baselineId = "baseline",
        const std::string& currentId = "current"
    );
    
    /**
     * Compare raw frame data
     * @param baselineFrames Vector of (fps, frameTimeMs, memory) tuples
     * @param currentFrames Vector of (fps, frameTimeMs, memory) tuples
     * @return Full comparison report
     */
    ComparisonReport CompareFrameData(
        const std::vector<std::tuple<double, double, size_t>>& baselineFrames,
        const std::vector<std::tuple<double, double, size_t>>& currentFrames,
        const std::string& baselineId = "baseline",
        const std::string& currentId = "current"
    );
    
    // === Historical Tracking ===
    
    /**
     * Record a session for historical comparison
     */
    void RecordHistoricalPoint(const HistoricalComparisonPoint& point);
    
    /**
     * Get all historical comparison points
     */
    const std::vector<HistoricalComparisonPoint>& GetHistoricalData() const { return m_historicalData; }
    
    /**
     * Clear historical data
     */
    void ClearHistory();
    
    /**
     * Get trend analysis for a metric over time
     */
    MetricTrendOverTime AnalyzeMetricTrend(const std::string& metricName) const;
    
    /**
     * Compare current session to historical average
     */
    ComparisonReport CompareToHistoricalAverage(const SummaryStats& currentStats);
    
    /**
     * Compare current session to best historical session
     */
    ComparisonReport CompareToBestSession(const SummaryStats& currentStats);
    
    /**
     * Compare current session to worst historical session
     */
    ComparisonReport CompareToWorstSession(const SummaryStats& currentStats);
    
    // === Statistical Analysis ===
    
    /**
     * Perform two-sample t-test to determine if difference is statistically significant
     * @return p-value (lower = more significant difference)
     */
    double PerformTTest(
        const std::vector<double>& sample1,
        const std::vector<double>& sample2
    ) const;
    
    /**
     * Check if a difference is statistically significant
     */
    bool IsSignificantDifference(
        const std::vector<double>& baseline,
        const std::vector<double>& current
    ) const;
    
    // === Export ===
    
    /**
     * Export comparison report to JSON
     */
    std::string ExportReportToJSON(const ComparisonReport& report) const;
    
    /**
     * Export historical data to JSON
     */
    std::string ExportHistoryToJSON() const;
    
    /**
     * Export metric trends to JSON
     */
    std::string ExportTrendsToJSON() const;
    
    // === Callbacks ===
    
    using RegressionCallback = std::function<void(const ComparisonReport&)>;
    void SetRegressionDetectedCallback(RegressionCallback callback) { m_regressionCallback = callback; }
    
private:
    // Internal comparison methods
    CategoryComparison CompareFPSMetrics(const SummaryStats& baseline, const SummaryStats& current);
    CategoryComparison CompareFrameTimeMetrics(const SummaryStats& baseline, const SummaryStats& current);
    CategoryComparison CompareMemoryMetrics(const SummaryStats& baseline, const SummaryStats& current);
    CategoryComparison CompareStabilityMetrics(const SummaryStats& baseline, const SummaryStats& current);
    
    // Metric comparison helper
    MetricComparison CreateMetricComparison(
        const std::string& name,
        double baselineValue,
        double currentValue,
        bool lowerIsBetter,
        double warningThreshold,
        double criticalThreshold
    );
    
    // Interpretation helpers
    std::string InterpretPercentChange(double percentChange, bool lowerIsBetter);
    std::string DetermineVerdict(const std::vector<CategoryComparison>& categories);
    std::vector<std::string> GenerateRecommendations(const std::vector<CategoryComparison>& categories);
    
    // Statistical helpers
    double CalculateMean(const std::vector<double>& values) const;
    double CalculateVariance(const std::vector<double>& values, double mean) const;
    double CalculateStdDev(const std::vector<double>& values) const;
    
    // Regression detection
    bool IsRegression(double percentChange, bool lowerIsBetter, double warningThreshold, double criticalThreshold);
    double CalculateRegressionSeverity(double percentChange, bool lowerIsBetter, double warningThreshold, double criticalThreshold);
    
    RegressionThresholds m_thresholds;
    std::vector<HistoricalComparisonPoint> m_historicalData;
    size_t m_maxHistorySize = 100;  // Keep last 100 sessions
    
    RegressionCallback m_regressionCallback;
};

} // namespace ProfilerCore
