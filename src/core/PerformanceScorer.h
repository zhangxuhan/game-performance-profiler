#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace ProfilerCore {

/**
 * Individual component score with details
 */
struct ComponentScore {
    std::string name;           // Component name (CPU, GPU, Memory, etc.)
    double score;               // 0-100, higher is better
    double weight;              // Weight in overall score calculation
    std::string grade;          // Letter grade (A+, A, B, C, D, F)
    std::string status;         // "Excellent", "Good", "Fair", "Poor", "Critical"
    std::string recommendation; // Actionable recommendation
    std::unordered_map<std::string, double> details; // Sub-metrics
    
    std::string ToJSON() const;
};

/**
 * Overall performance score card
 */
struct PerformanceScoreCard {
    int64_t timestamp;
    int frameNumber;
    std::string sessionId;
    
    // Overall score
    double overallScore;        // 0-100, weighted combination
    std::string overallGrade;   // Letter grade
    std::string overallStatus;  // Status text
    
    // Component scores
    ComponentScore fpsScore;
    ComponentScore frameTimeScore;
    ComponentScore memoryScore;
    ComponentScore gpuScore;
    ComponentScore stabilityScore;
    ComponentScore cpuScore;
    
    // Trend indicators
    double scoreChange;         // Change from previous measurement
    double scoreTrend;          // Trend over last N measurements (positive = improving)
    std::string trendDirection; // "Improving", "Stable", "Declining"
    
    // Regression detection
    bool regressionDetected;
    std::vector<std::string> regressionComponents;
    double regressionSeverity;  // 0-100, higher = more severe
    
    // Summary
    std::string summary;
    std::vector<std::string> topIssues;
    std::vector<std::string> recommendations;
    
    std::string ToJSON() const;
};

/**
 * Historical score entry for trend analysis
 */
struct ScoreHistoryEntry {
    int64_t timestamp;
    int frameNumber;
    double overallScore;
    double fpsScore;
    double frameTimeScore;
    double memoryScore;
    double gpuScore;
    double stabilityScore;
    double cpuScore;
    std::string sessionId;
};

/**
 * Score thresholds and grading configuration
 */
struct ScoringConfig {
    // Grade thresholds (percentage)
    double gradeAPlusThreshold = 95.0;
    double gradeAThreshold = 90.0;
    double gradeBThreshold = 80.0;
    double gradeCThreshold = 70.0;
    double gradeDThreshold = 60.0;
    // Below D is F
    
    // Component weights (should sum to 1.0)
    double fpsWeight = 0.20;
    double frameTimeWeight = 0.20;
    double memoryWeight = 0.15;
    double gpuWeight = 0.15;
    double stabilityWeight = 0.15;
    double cpuWeight = 0.15;
    
    // Regression detection
    double regressionThreshold = 5.0;      // Score drop >= 5 points triggers regression
    double significantDropThreshold = 10.0; // Score drop >= 10 is significant regression
    int regressionHistorySize = 10;         // Number of samples for trend detection
    
    // FPS scoring thresholds
    double fpsExcellentMin = 60.0;          // 100% score at this or higher
    double fpsGoodMin = 50.0;
    double fpsFairMin = 40.0;
    double fpsPoorMin = 30.0;
    
    // Frame time scoring thresholds (ms)
    double frameTimeExcellentMax = 16.67;   // 60 FPS
    double frameTimeGoodMax = 20.0;         // 50 FPS
    double frameTimeFairMax = 25.0;         // 40 FPS
    double frameTimePoorMax = 33.33;        // 30 FPS
    
    // Memory scoring thresholds (MB)
    double memoryExcellentMax = 1024;
    double memoryGoodMax = 2048;
    double memoryFairMax = 3072;
    double memoryPoorMax = 4096;
    
    // Stability scoring thresholds (std dev in ms)
    double stabilityExcellentMax = 1.0;
    double stabilityGoodMax = 3.0;
    double stabilityFairMax = 5.0;
    double stabilityPoorMax = 10.0;
    
    // GPU utilization thresholds (percentage)
    double gpuExcellentMin = 90.0;          // High utilization is good (not bottleneck)
    double gpuGoodMin = 75.0;
    double gpuFairMin = 50.0;
    double gpuPoorMin = 25.0;
    
    // History configuration
    size_t maxHistorySize = 1000;
    
    bool enableRegressionDetection = true;
    bool enableTrendAnalysis = true;
};

/**
 * Regression event for tracking
 */
struct RegressionEvent {
    int64_t timestamp;
    int frameNumber;
    std::string sessionId;
    double previousScore;
    double currentScore;
    double scoreDelta;
    double severity;
    std::vector<std::string> affectedComponents;
    std::string summary;
    bool acknowledged;
    int64_t acknowledgedAt;
};

/**
 * Performance trend analysis result
 */
struct TrendAnalysis {
    int64_t timestamp;
    int sampleCount;
    
    // Overall trend
    double overallTrendSlope;      // Score points per sample
    double overallTrendR2;         // Goodness of fit
    std::string overallTrend;      // "Improving", "Stable", "Declining"
    double overallConfidence;      // 0-1 confidence in trend
    
    // Component trends
    std::unordered_map<std::string, double> componentTrends;
    std::unordered_map<std::string, std::string> componentTrendDirections;
    
    // Predictions
    double predictedScoreNext100;  // Predicted score in 100 samples
    double predictedScoreNext500;
    
    // Statistical summary
    double meanScore;
    double medianScore;
    double minScore;
    double maxScore;
    double stdDevScore;
    
    std::string ToJSON() const;
};

/**
 * PerformanceScorer - Unified performance scoring and regression detection
 * 
 * Features:
 * - Comprehensive scoring from 0-100 across multiple components
 * - Letter grading system (A+ to F)
 * - Historical score tracking for trend analysis
 * - Automated regression detection with severity assessment
 * - Trend prediction using linear regression
 * - Actionable recommendations based on weak components
 * - Integration with all existing analyzers
 * - CI/CD friendly score thresholds
 * 
 * Usage:
 *   PerformanceScorer scorer;
 *   scorer.SetData(fps, frameTime, memory, gpuUtil, cpuTime, stability);
 *   PerformanceScoreCard card = scorer.ComputeScore();
 *   if (card.regressionDetected) {
 *       // Alert team, fail CI build, etc.
 *   }
 */
class PerformanceScorer {
public:
    PerformanceScorer();
    ~PerformanceScorer();
    
    // Configuration
    void SetConfig(const ScoringConfig& config);
    const ScoringConfig& GetConfig() const { return m_config; }
    void Reset();
    void ClearHistory();
    
    // Data input - raw metrics
    void SetFPSMetrics(double avgFps, double minFps, double p95Fps, double p99Fps);
    void SetFrameTimeMetrics(double avgMs, double maxMs, double stdDevMs, double p99Ms);
    void SetMemoryMetrics(size_t currentMB, size_t peakMB, double growthRateMBps);
    void SetGPUMetrics(double utilization, double frameTimeUs, double driverOverheadMs);
    void SetCPUMetrics(double mainThreadMs, double totalCpuMs, double waitTimeMs);
    void SetStabilityMetrics(double frameTimeStdDev, double jitterMs, double spikeCount);
    
    // Data input - from existing analyzers (convenience methods)
    void SetDataFromStatistics(class StatisticsAnalyzer* analyzer);
    void SetDataFromGPUProfiler(class GPUProfiler* profiler);
    void SetDataFromMemoryAnalyzer(class MemoryAnalyzer* analyzer);
    void SetDataFromFrameTimeAnalyzer(class FrameTimeAnalyzer* analyzer);
    void SetDataFromTrendPredictor(class TrendPredictor* predictor);
    
    // Compute scores
    PerformanceScoreCard ComputeScore();
    PerformanceScoreCard ComputeScoreWithSession(const std::string& sessionId);
    
    // Individual component scoring
    ComponentScore ComputeFPSScore(double avgFps, double minFps, double p95Fps);
    ComponentScore ComputeFrameTimeScore(double avgMs, double maxMs, double stdDevMs);
    ComponentScore ComputeMemoryScore(size_t currentMB, size_t peakMB, double growthRate);
    ComponentScore ComputeGPUScore(double utilization, double frameTimeUs);
    ComponentScore ComputeCPUScore(double mainThreadMs, double waitTimeMs);
    ComponentScore ComputeStabilityScore(double stdDev, double jitter, int spikeCount);
    
    // Grade conversion
    std::string ScoreToGrade(double score) const;
    std::string ScoreToStatus(double score) const;
    
    // History management
    const std::vector<ScoreHistoryEntry>& GetHistory() const { return m_history; }
    std::vector<ScoreHistoryEntry> GetRecentHistory(int count) const;
    ScoreHistoryEntry GetLatestEntry() const;
    size_t GetHistorySize() const { return m_history.size(); }
    
    // Trend analysis
    TrendAnalysis AnalyzeTrend() const;
    TrendAnalysis AnalyzeTrendForComponent(const std::string& componentName) const;
    bool IsImproving() const;
    bool IsDeclining() const;
    
    // Regression detection
    bool HasRegression() const;
    std::vector<RegressionEvent> GetRegressions() const;
    std::vector<RegressionEvent> GetUnacknowledgedRegressions() const;
    bool AcknowledgeRegression(int64_t regressionTimestamp);
    void ClearAcknowledgedRegressions();
    void ClearAllRegressions();
    
    // Statistical queries
    double GetAverageScore(int lastN = 0) const;
    double GetMedianScore(int lastN = 0) const;
    double GetMinScore(int lastN = 0) const;
    double GetMaxScore(int lastN = 0) const;
    double GetScorePercentile(double percentile) const;
    
    // Comparison utilities
    double CompareToBaseline(double baselineScore) const;
    double CompareToPrevious() const;
    PerformanceScoreCard GetBestScoreCard() const;
    PerformanceScoreCard GetWorstScoreCard() const;
    
    // Export
    std::string ExportScoreCardToJSON(const PerformanceScoreCard& card) const;
    std::string ExportHistoryToJSON() const;
    std::string ExportRegressionsToJSON() const;
    std::string ExportFullReport() const;
    
    // Callbacks
    using RegressionCallback = std::function<void(const RegressionEvent&)>;
    using ScoreCallback = std::function<void(const PerformanceScoreCard&)>;
    void SetRegressionCallback(RegressionCallback callback);
    void SetScoreCallback(ScoreCallback callback);
    
private:
    // Internal scoring helpers
    double NormalizeScore(double value, double excellent, double good, double fair, double poor, bool higherIsBetter) const;
    double ComputeWeightedOverall(const std::vector<std::pair<double, double>>& weightedScores) const;
    void GenerateRecommendations(PerformanceScoreCard& card);
    void DetectRegression(const PerformanceScoreCard& card);
    void UpdateHistory(const PerformanceScoreCard& card);
    void TrimHistory();
    
    // Linear regression for trend analysis
    struct LinearFit {
        double slope;
        double intercept;
        double r2;
        bool valid;
    };
    LinearFit ComputeLinearFit(const std::vector<double>& y) const;
    
    // Percentile calculation
    double ComputePercentile(std::vector<double> values, double percentile) const;
    
    ScoringConfig m_config;
    
    // Current metric values
    struct {
        double avgFps = 0, minFps = 0, p95Fps = 0, p99Fps = 0;
        double avgFrameTimeMs = 0, maxFrameTimeMs = 0, frameTimeStdDev = 0, p99FrameTimeMs = 0;
        size_t currentMemoryMB = 0, peakMemoryMB = 0;
        double memoryGrowthRate = 0;
        double gpuUtilization = 0, gpuFrameTimeUs = 0, gpuDriverOverheadMs = 0;
        double cpuMainThreadMs = 0, cpuTotalMs = 0, cpuWaitMs = 0;
        double stabilityStdDev = 0, jitterMs = 0;
        int spikeCount = 0;
    } m_currentData;
    
    bool m_dataDirty = true;
    
    // History tracking
    std::vector<ScoreHistoryEntry> m_history;
    
    // Regression tracking
    std::vector<RegressionEvent> m_regressions;
    double m_lastScore = 100.0;
    
    // Cached score card
    PerformanceScoreCard m_cachedScoreCard;
    
    // Callbacks
    RegressionCallback m_regressionCallback;
    ScoreCallback m_scoreCallback;
};

} // namespace ProfilerCore
