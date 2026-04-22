#include "PerformanceScorer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace ProfilerCore {

// ============================================================================
// ComponentScore
// ============================================================================

std::string ComponentScore::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"name\":\"" << name << "\",";
    oss << "\"score\":" << std::fixed << std::setprecision(2) << score << ",";
    oss << "\"weight\":" << weight << ",";
    oss << "\"grade\":\"" << grade << "\",";
    oss << "\"status\":\"" << status << "\",";
    oss << "\"recommendation\":\"" << recommendation << "\",";
    oss << "\"details\":{";
    bool first = true;
    for (const auto& kv : details) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << kv.first << "\":" << kv.second;
    }
    oss << "}";
    oss << "}";
    return oss.str();
}

// ============================================================================
// PerformanceScoreCard
// ============================================================================

std::string PerformanceScoreCard::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"timestamp\":" << timestamp << ",";
    oss << "\"frameNumber\":" << frameNumber << ",";
    oss << "\"sessionId\":\"" << sessionId << "\",";
    oss << "\"overallScore\":" << std::fixed << std::setprecision(2) << overallScore << ",";
    oss << "\"overallGrade\":\"" << overallGrade << "\",";
    oss << "\"overallStatus\":\"" << overallStatus << "\",";
    oss << "\"fpsScore\":" << fpsScore.ToJSON() << ",";
    oss << "\"frameTimeScore\":" << frameTimeScore.ToJSON() << ",";
    oss << "\"memoryScore\":" << memoryScore.ToJSON() << ",";
    oss << "\"gpuScore\":" << gpuScore.ToJSON() << ",";
    oss << "\"stabilityScore\":" << stabilityScore.ToJSON() << ",";
    oss << "\"cpuScore\":" << cpuScore.ToJSON() << ",";
    oss << "\"scoreChange\":" << scoreChange << ",";
    oss << "\"scoreTrend\":" << scoreTrend << ",";
    oss << "\"trendDirection\":\"" << trendDirection << "\",";
    oss << "\"regressionDetected\":" << (regressionDetected ? "true" : "false") << ",";
    oss << "\"regressionComponents\":[";
    for (size_t i = 0; i < regressionComponents.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << regressionComponents[i] << "\"";
    }
    oss << "],";
    oss << "\"regressionSeverity\":" << regressionSeverity << ",";
    oss << "\"summary\":\"" << summary << "\",";
    oss << "\"topIssues\":[";
    for (size_t i = 0; i < topIssues.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << topIssues[i] << "\"";
    }
    oss << "],";
    oss << "\"recommendations\":[";
    for (size_t i = 0; i < recommendations.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << recommendations[i] << "\"";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

// ============================================================================
// TrendAnalysis
// ============================================================================

std::string TrendAnalysis::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"timestamp\":" << timestamp << ",";
    oss << "\"sampleCount\":" << sampleCount << ",";
    oss << "\"overallTrendSlope\":" << std::fixed << std::setprecision(4) << overallTrendSlope << ",";
    oss << "\"overallTrendR2\":" << overallTrendR2 << ",";
    oss << "\"overallTrend\":\"" << overallTrend << "\",";
    oss << "\"overallConfidence\":" << overallConfidence << ",";
    oss << "\"predictedScoreNext100\":" << predictedScoreNext100 << ",";
    oss << "\"predictedScoreNext500\":" << predictedScoreNext500 << ",";
    oss << "\"meanScore\":" << meanScore << ",";
    oss << "\"medianScore\":" << medianScore << ",";
    oss << "\"minScore\":" << minScore << ",";
    oss << "\"maxScore\":" << maxScore << ",";
    oss << "\"stdDevScore\":" << stdDevScore << ",";
    oss << "\"componentTrends\":{";
    bool first = true;
    for (const auto& kv : componentTrends) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << kv.first << "\":" << kv.second;
    }
    oss << "}";
    oss << "}";
    return oss.str();
}

// ============================================================================
// PerformanceScorer
// ============================================================================

PerformanceScorer::PerformanceScorer() {
    Reset();
}

PerformanceScorer::~PerformanceScorer() {
}

void PerformanceScorer::SetConfig(const ScoringConfig& config) {
    m_config = config;
}

void PerformanceScorer::Reset() {
    m_currentData = {};
    m_dataDirty = true;
    m_lastScore = 100.0;
    m_cachedScoreCard = PerformanceScoreCard();
}

void PerformanceScorer::ClearHistory() {
    m_history.clear();
    m_regressions.clear();
    m_lastScore = 100.0;
}

// ============================================================================
// Data Input
// ============================================================================

void PerformanceScorer::SetFPSMetrics(double avgFps, double minFps, double p95Fps, double p99Fps) {
    m_currentData.avgFps = avgFps;
    m_currentData.minFps = minFps;
    m_currentData.p95Fps = p95Fps;
    m_currentData.p99Fps = p99Fps;
    m_dataDirty = true;
}

void PerformanceScorer::SetFrameTimeMetrics(double avgMs, double maxMs, double stdDevMs, double p99Ms) {
    m_currentData.avgFrameTimeMs = avgMs;
    m_currentData.maxFrameTimeMs = maxMs;
    m_currentData.frameTimeStdDev = stdDevMs;
    m_currentData.p99FrameTimeMs = p99Ms;
    m_dataDirty = true;
}

void PerformanceScorer::SetMemoryMetrics(size_t currentMB, size_t peakMB, double growthRateMBps) {
    m_currentData.currentMemoryMB = currentMB;
    m_currentData.peakMemoryMB = peakMB;
    m_currentData.memoryGrowthRate = growthRateMBps;
    m_dataDirty = true;
}

void PerformanceScorer::SetGPUMetrics(double utilization, double frameTimeUs, double driverOverheadMs) {
    m_currentData.gpuUtilization = utilization;
    m_currentData.gpuFrameTimeUs = frameTimeUs;
    m_currentData.gpuDriverOverheadMs = driverOverheadMs;
    m_dataDirty = true;
}

void PerformanceScorer::SetCPUMetrics(double mainThreadMs, double totalCpuMs, double waitTimeMs) {
    m_currentData.cpuMainThreadMs = mainThreadMs;
    m_currentData.cpuTotalMs = totalCpuMs;
    m_currentData.cpuWaitMs = waitTimeMs;
    m_dataDirty = true;
}

void PerformanceScorer::SetStabilityMetrics(double frameTimeStdDev, double jitterMs, int spikeCount) {
    m_currentData.stabilityStdDev = frameTimeStdDev;
    m_currentData.jitterMs = jitterMs;
    m_currentData.spikeCount = spikeCount;
    m_dataDirty = true;
}

// ============================================================================
// Score Computation
// ============================================================================

PerformanceScoreCard PerformanceScorer::ComputeScore() {
    return ComputeScoreWithSession("");
}

PerformanceScoreCard PerformanceScorer::ComputeScoreWithSession(const std::string& sessionId) {
    PerformanceScoreCard card;
    card.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    card.sessionId = sessionId;
    
    // Compute individual component scores
    card.fpsScore = ComputeFPSScore(
        m_currentData.avgFps,
        m_currentData.minFps,
        m_currentData.p95Fps
    );
    card.fpsScore.weight = m_config.fpsWeight;
    
    card.frameTimeScore = ComputeFrameTimeScore(
        m_currentData.avgFrameTimeMs,
        m_currentData.maxFrameTimeMs,
        m_currentData.frameTimeStdDev
    );
    card.frameTimeScore.weight = m_config.frameTimeWeight;
    
    card.memoryScore = ComputeMemoryScore(
        m_currentData.currentMemoryMB,
        m_currentData.peakMemoryMB,
        m_currentData.memoryGrowthRate
    );
    card.memoryScore.weight = m_config.memoryWeight;
    
    card.gpuScore = ComputeGPUScore(
        m_currentData.gpuUtilization,
        m_currentData.gpuFrameTimeUs
    );
    card.gpuScore.weight = m_config.gpuWeight;
    
    card.cpuScore = ComputeCPUScore(
        m_currentData.cpuMainThreadMs,
        m_currentData.cpuWaitMs
    );
    card.cpuScore.weight = m_config.cpuWeight;
    
    card.stabilityScore = ComputeStabilityScore(
        m_currentData.stabilityStdDev,
        m_currentData.jitterMs,
        m_currentData.spikeCount
    );
    card.stabilityScore.weight = m_config.stabilityWeight;
    
    // Compute weighted overall score
    double overall = ComputeWeightedOverall({
        {card.fpsScore.score, m_config.fpsWeight},
        {card.frameTimeScore.score, m_config.frameTimeWeight},
        {card.memoryScore.score, m_config.memoryWeight},
        {card.gpuScore.score, m_config.gpuWeight},
        {card.stabilityScore.score, m_config.stabilityWeight},
        {card.cpuScore.score, m_config.cpuWeight}
    });
    
    card.overallScore = overall;
    card.overallGrade = ScoreToGrade(overall);
    card.overallStatus = ScoreToStatus(overall);
    
    // Compute trend and regression
    double scoreChange = m_history.empty() ? 0.0 : overall - m_history.back().overallScore;
    card.scoreChange = scoreChange;
    
    if (m_config.enableTrendAnalysis && !m_history.empty()) {
        TrendAnalysis trend = AnalyzeTrend();
        card.scoreTrend = trend.overallTrendSlope;
        card.trendDirection = trend.overallTrend;
    } else {
        card.scoreTrend = 0.0;
        card.trendDirection = "Unknown";
    }
    
    // Regression detection
    if (m_config.enableRegressionDetection) {
        DetectRegression(card);
    }
    
    // Generate recommendations
    GenerateRecommendations(card);
    
    // Summary text
    std::ostringstream summary;
    summary << "Overall performance score: " << std::fixed << std::setprecision(1) << overall 
            << " (" << card.overallGrade << "). ";
    if (card.regressionDetected) {
        summary << "Regression detected! ";
    }
    if (!card.topIssues.empty()) {
        summary << "Top issue: " << card.topIssues[0];
    } else {
        summary << "Performance is " << card.overallStatus << ".";
    }
    card.summary = summary.str();
    
    // Update history
    UpdateHistory(card);
    
    // Cache and callback
    m_cachedScoreCard = card;
    m_dataDirty = false;
    m_lastScore = overall;
    
    if (m_scoreCallback) {
        m_scoreCallback(card);
    }
    
    return card;
}

// ============================================================================
// Component Scoring
// ============================================================================

ComponentScore PerformanceScorer::ComputeFPSScore(double avgFps, double minFps, double p95Fps) {
    ComponentScore score;
    score.name = "FPS";
    
    // Base score from average FPS
    double avgScore = NormalizeScore(
        avgFps,
        m_config.fpsExcellentMin,
        m_config.fpsGoodMin,
        m_config.fpsFairMin,
        m_config.fpsPoorMin,
        true  // higher is better
    );
    
    // Penalty for low min FPS (stuttering)
    double minScore = NormalizeScore(
        minFps,
        m_config.fpsExcellentMin * 0.9,
        m_config.fpsGoodMin * 0.9,
        m_config.fpsFairMin * 0.9,
        m_config.fpsPoorMin * 0.9,
        true
    );
    
    // Combined score (weighted average)
    score.score = avgScore * 0.7 + minScore * 0.3;
    score.grade = ScoreToGrade(score.score);
    score.status = ScoreToStatus(score.score);
    
    // Details
    score.details["avgFps"] = avgFps;
    score.details["minFps"] = minFps;
    score.details["p95Fps"] = p95Fps;
    score.details["avgScore"] = avgScore;
    score.details["minScore"] = minScore;
    
    // Recommendation
    if (avgFps < m_config.fpsFairMin) {
        score.recommendation = "FPS is critically low. Reduce render quality or enable VSync.";
    } else if (minFps < m_config.fpsPoorMin) {
        score.recommendation = "Frequent FPS drops detected. Consider reducing shader complexity.";
    } else if (avgFps < m_config.fpsGoodMin) {
        score.recommendation = "FPS could be improved. Check for CPU-bound scenarios.";
    } else {
        score.recommendation = "FPS performance is good.";
    }
    
    return score;
}

ComponentScore PerformanceScorer::ComputeFrameTimeScore(double avgMs, double maxMs, double stdDevMs) {
    ComponentScore score;
    score.name = "FrameTime";
    
    // Score based on average frame time
    double avgScore = NormalizeScore(
        avgMs,
        m_config.frameTimeExcellentMax,
        m_config.frameTimeGoodMax,
        m_config.frameTimeFairMax,
        m_config.frameTimePoorMax,
        false  // lower is better
    );
    
    // Penalty for high max frame time (spikes)
    double maxScore = NormalizeScore(
        maxMs,
        m_config.frameTimeExcellentMax * 1.5,
        m_config.frameTimeGoodMax * 1.5,
        m_config.frameTimeFairMax * 1.5,
        m_config.frameTimePoorMax * 1.5,
        false
    );
    
    // Combined score
    score.score = avgScore * 0.6 + maxScore * 0.4;
    score.grade = ScoreToGrade(score.score);
    score.status = ScoreToStatus(score.score);
    
    score.details["avgMs"] = avgMs;
    score.details["maxMs"] = maxMs;
    score.details["stdDevMs"] = stdDevMs;
    score.details["avgScore"] = avgScore;
    score.details["maxScore"] = maxScore;
    
    if (avgMs > m_config.frameTimePoorMax) {
        score.recommendation = "Frame time is critically high. Significant performance issues present.";
    } else if (maxMs > m_config.frameTimePoorMax * 2) {
        score.recommendation = "Frame time spikes detected. Check for GC pauses or loading hitches.";
    } else {
        score.recommendation = "Frame time performance is acceptable.";
    }
    
    return score;
}

ComponentScore PerformanceScorer::ComputeMemoryScore(size_t currentMB, size_t peakMB, double growthRate) {
    ComponentScore score;
    score.name = "Memory";
    
    // Score based on current memory usage
    double currentScore = NormalizeScore(
        static_cast<double>(currentMB),
        m_config.memoryExcellentMax,
        m_config.memoryGoodMax,
        m_config.memoryFairMax,
        m_config.memoryPoorMax,
        false  // lower is better
    );
    
    // Penalty for high peak
    double peakScore = NormalizeScore(
        static_cast<double>(peakMB),
        m_config.memoryExcellentMax * 1.2,
        m_config.memoryGoodMax * 1.2,
        m_config.memoryFairMax * 1.2,
        m_config.memoryPoorMax * 1.2,
        false
    );
    
    // Penalty for memory growth (potential leak)
    double growthPenalty = 0.0;
    if (growthRate > 1.0) {  // > 1 MB/s
        growthPenalty = std::min(30.0, growthRate * 3.0);
    }
    
    score.score = std::max(0.0, (currentScore * 0.5 + peakScore * 0.5) - growthPenalty);
    score.grade = ScoreToGrade(score.score);
    score.status = ScoreToStatus(score.score);
    
    score.details["currentMB"] = static_cast<double>(currentMB);
    score.details["peakMB"] = static_cast<double>(peakMB);
    score.details["growthRateMBps"] = growthRate;
    score.details["growthPenalty"] = growthPenalty;
    
    if (growthRate > 5.0) {
        score.recommendation = "Potential memory leak detected. Review allocation patterns.";
    } else if (currentMB > m_config.memoryPoorMax) {
        score.recommendation = "Memory usage is very high. Consider asset streaming.";
    } else if (growthRate > 1.0) {
        score.recommendation = "Memory growing slowly. Monitor for leaks.";
    } else {
        score.recommendation = "Memory usage is stable.";
    }
    
    return score;
}

ComponentScore PerformanceScorer::ComputeGPUScore(double utilization, double frameTimeUs) {
    ComponentScore score;
    score.name = "GPU";
    
    // High utilization is good (GPU not waiting), but too high may indicate bottleneck
    double utilScore;
    if (utilization >= m_config.gpuExcellentMin && utilization <= 98.0) {
        utilScore = 100.0;
    } else if (utilization >= m_config.gpuGoodMin) {
        utilScore = NormalizeScore(utilization, m_config.gpuExcellentMin, m_config.gpuGoodMin, 
                                   m_config.gpuFairMin, m_config.gpuPoorMin, true);
    } else {
        // Low GPU utilization = CPU bottleneck or underutilized GPU
        utilScore = NormalizeScore(utilization, m_config.gpuExcellentMin, m_config.gpuGoodMin,
                                   m_config.gpuFairMin, m_config.gpuPoorMin, true) * 0.8;
    }
    
    // Frame time score
    double frameTimeMs = frameTimeUs / 1000.0;
    double ftScore = NormalizeScore(
        frameTimeMs,
        m_config.frameTimeExcellentMax,
        m_config.frameTimeGoodMax,
        m_config.frameTimeFairMax,
        m_config.frameTimePoorMax,
        false
    );
    
    score.score = utilScore * 0.4 + ftScore * 0.6;
    score.grade = ScoreToGrade(score.score);
    score.status = ScoreToStatus(score.score);
    
    score.details["utilization"] = utilization;
    score.details["frameTimeUs"] = frameTimeUs;
    score.details["utilScore"] = utilScore;
    score.details["ftScore"] = ftScore;
    
    if (utilization < m_config.gpuPoorMin) {
        score.recommendation = "GPU underutilized. Likely CPU-bound.";
    } else if (frameTimeMs > m_config.frameTimePoorMax) {
        score.recommendation = "GPU frame time high. Reduce render load.";
    } else if (utilization > 98.0) {
        score.recommendation = "GPU near 100% utilization. Consider reducing quality.";
    } else {
        score.recommendation = "GPU performance is balanced.";
    }
    
    return score;
}

ComponentScore PerformanceScorer::ComputeCPUScore(double mainThreadMs, double waitTimeMs) {
    ComponentScore score;
    score.name = "CPU";
    
    // Score based on main thread time
    double mainScore = NormalizeScore(
        mainThreadMs,
        m_config.frameTimeExcellentMax * 0.8,
        m_config.frameTimeGoodMax * 0.8,
        m_config.frameTimeFairMax * 0.8,
        m_config.frameTimePoorMax * 0.8,
        false
    );
    
    // Penalty for high wait time (inefficiency)
    double waitPenalty = std::min(20.0, waitTimeMs * 2.0);
    
    score.score = std::max(0.0, mainScore - waitPenalty);
    score.grade = ScoreToGrade(score.score);
    score.status = ScoreToStatus(score.score);
    
    score.details["mainThreadMs"] = mainThreadMs;
    score.details["waitTimeMs"] = waitTimeMs;
    score.details["mainScore"] = mainScore;
    score.details["waitPenalty"] = waitPenalty;
    
    if (mainThreadMs > m_config.frameTimePoorMax) {
        score.recommendation = "Main thread overloaded. Optimize game logic.";
    } else if (waitTimeMs > 5.0) {
        score.recommendation = "High CPU wait time. Check for lock contention.";
    } else {
        score.recommendation = "CPU performance is acceptable.";
    }
    
    return score;
}

ComponentScore PerformanceScorer::ComputeStabilityScore(double stdDev, double jitter, int spikeCount) {
    ComponentScore score;
    score.name = "Stability";
    
    // Score based on frame time standard deviation
    double stdScore = NormalizeScore(
        stdDev,
        m_config.stabilityExcellentMax,
        m_config.stabilityGoodMax,
        m_config.stabilityFairMax,
        m_config.stabilityPoorMax,
        false
    );
    
    // Penalty for jitter
    double jitterPenalty = std::min(25.0, jitter * 5.0);
    
    // Penalty for spikes
    double spikePenalty = std::min(25.0, static_cast<double>(spikeCount) * 0.5);
    
    score.score = std::max(0.0, stdScore - jitterPenalty - spikePenalty);
    score.grade = ScoreToGrade(score.score);
    score.status = ScoreToStatus(score.score);
    
    score.details["stdDevMs"] = stdDev;
    score.details["jitterMs"] = jitter;
    score.details["spikeCount"] = static_cast<double>(spikeCount);
    score.details["stdScore"] = stdScore;
    score.details["jitterPenalty"] = jitterPenalty;
    score.details["spikePenalty"] = spikePenalty;
    
    if (spikeCount > 50) {
        score.recommendation = "Excessive frame spikes. Investigate GC or loading pauses.";
    } else if (stdDev > m_config.stabilityPoorMax) {
        score.recommendation = "Frame times unstable. Check thermal throttling.";
    } else if (jitter > 3.0) {
        score.recommendation = "High jitter detected. Reduce background processes.";
    } else {
        score.recommendation = "Frame stability is good.";
    }
    
    return score;
}

// ============================================================================
// Grade Conversion
// ============================================================================

std::string PerformanceScorer::ScoreToGrade(double score) const {
    if (score >= m_config.gradeAPlusThreshold) return "A+";
    if (score >= m_config.gradeAThreshold) return "A";
    if (score >= m_config.gradeBThreshold) return "B";
    if (score >= m_config.gradeCThreshold) return "C";
    if (score >= m_config.gradeDThreshold) return "D";
    return "F";
}

std::string PerformanceScorer::ScoreToStatus(double score) const {
    if (score >= 90.0) return "Excellent";
    if (score >= 80.0) return "Good";
    if (score >= 70.0) return "Fair";
    if (score >= 60.0) return "Poor";
    return "Critical";
}

// ============================================================================
// Normalization Helper
// ============================================================================

double PerformanceScorer::NormalizeScore(double value, double excellent, double good, 
                                          double fair, double poor, bool higherIsBetter) const {
    if (higherIsBetter) {
        // Higher values are better (e.g., FPS)
        if (value >= excellent) return 100.0;
        if (value >= good) return 80.0 + (value - good) / (excellent - good) * 20.0;
        if (value >= fair) return 60.0 + (value - fair) / (good - fair) * 20.0;
        if (value >= poor) return 40.0 + (value - poor) / (fair - poor) * 20.0;
        return std::max(0.0, (value / poor) * 40.0);
    } else {
        // Lower values are better (e.g., frame time)
        if (value <= excellent) return 100.0;
        if (value <= good) return 80.0 + (good - value) / (good - excellent) * 20.0;
        if (value <= fair) return 60.0 + (fair - value) / (fair - good) * 20.0;
        if (value <= poor) return 40.0 + (poor - value) / (poor - fair) * 20.0;
        return std::max(0.0, 40.0 - (value - poor) / poor * 40.0);
    }
}

double PerformanceScorer::ComputeWeightedOverall(
    const std::vector<std::pair<double, double>>& weightedScores) const {
    
    double totalWeight = 0.0;
    double weightedSum = 0.0;
    
    for (const auto& ws : weightedScores) {
        weightedSum += ws.first * ws.second;
        totalWeight += ws.second;
    }
    
    if (totalWeight > 0) {
        return weightedSum / totalWeight;
    }
    return 0.0;
}

// ============================================================================
// History Management
// ============================================================================

void PerformanceScorer::UpdateHistory(const PerformanceScoreCard& card) {
    ScoreHistoryEntry entry;
    entry.timestamp = card.timestamp;
    entry.frameNumber = card.frameNumber;
    entry.overallScore = card.overallScore;
    entry.fpsScore = card.fpsScore.score;
    entry.frameTimeScore = card.frameTimeScore.score;
    entry.memoryScore = card.memoryScore.score;
    entry.gpuScore = card.gpuScore.score;
    entry.stabilityScore = card.stabilityScore.score;
    entry.cpuScore = card.cpuScore.score;
    entry.sessionId = card.sessionId;
    
    m_history.push_back(entry);
    TrimHistory();
}

void PerformanceScorer::TrimHistory() {
    while (m_history.size() > m_config.maxHistorySize) {
        m_history.erase(m_history.begin());
    }
}

std::vector<ScoreHistoryEntry> PerformanceScorer::GetRecentHistory(int count) const {
    std::vector<ScoreHistoryEntry> result;
    int start = static_cast<int>(m_history.size()) - count;
    if (start < 0) start = 0;
    
    for (size_t i = static_cast<size_t>(start); i < m_history.size(); ++i) {
        result.push_back(m_history[i]);
    }
    return result;
}

ScoreHistoryEntry PerformanceScorer::GetLatestEntry() const {
    if (m_history.empty()) {
        return ScoreHistoryEntry();
    }
    return m_history.back();
}

// ============================================================================
// Trend Analysis
// ============================================================================

PerformanceScorer::LinearFit PerformanceScorer::ComputeLinearFit(const std::vector<double>& y) const {
    LinearFit fit;
    fit.valid = false;
    
    if (y.size() < 3) {
        return fit;
    }
    
    size_t n = y.size();
    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
    
    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(i);
        sumX += x;
        sumY += y[i];
        sumXY += x * y[i];
        sumX2 += x * x;
        sumY2 += y[i] * y[i];
    }
    
    double xMean = sumX / n;
    double yMean = sumY / n;
    
    double denominator = n * sumX2 - sumX * sumX;
    if (std::abs(denominator) < 1e-10) {
        return fit;
    }
    
    fit.slope = (n * sumXY - sumX * sumY) / denominator;
    fit.intercept = yMean - fit.slope * xMean;
    
    // R-squared
    double ssTotal = 0, ssResidual = 0;
    for (size_t i = 0; i < n; ++i) {
        double yPred = fit.slope * static_cast<double>(i) + fit.intercept;
        ssTotal += (y[i] - yMean) * (y[i] - yMean);
        ssResidual += (y[i] - yPred) * (y[i] - yPred);
    }
    
    if (ssTotal > 1e-10) {
        fit.r2 = 1.0 - ssResidual / ssTotal;
    } else {
        fit.r2 = 1.0;
    }
    
    fit.valid = true;
    return fit;
}

TrendAnalysis PerformanceScorer::AnalyzeTrend() const {
    TrendAnalysis analysis;
    analysis.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    if (m_history.empty()) {
        return analysis;
    }
    
    analysis.sampleCount = static_cast<int>(m_history.size());
    
    // Overall trend
    std::vector<double> overallScores;
    overallScores.reserve(m_history.size());
    for (const auto& entry : m_history) {
        overallScores.push_back(entry.overallScore);
    }
    
    LinearFit overallFit = ComputeLinearFit(overallScores);
    analysis.overallTrendSlope = overallFit.slope;
    analysis.overallTrendR2 = overallFit.r2;
    
    // Determine trend direction
    if (overallFit.valid && overallFit.r2 > 0.3) {
        if (overallFit.slope > 0.05) {
            analysis.overallTrend = "Improving";
        } else if (overallFit.slope < -0.05) {
            analysis.overallTrend = "Declining";
        } else {
            analysis.overallTrend = "Stable";
        }
        analysis.overallConfidence = overallFit.r2;
    } else {
        analysis.overallTrend = "Unknown";
        analysis.overallConfidence = 0.0;
    }
    
    // Predictions
    if (overallFit.valid) {
        analysis.predictedScoreNext100 = overallFit.slope * (overallScores.size() + 100) + overallFit.intercept;
        analysis.predictedScoreNext500 = overallFit.slope * (overallScores.size() + 500) + overallFit.intercept;
        // Clamp predictions to valid range
        analysis.predictedScoreNext100 = std::max(0.0, std::min(100.0, analysis.predictedScoreNext100));
        analysis.predictedScoreNext500 = std::max(0.0, std::min(100.0, analysis.predictedScoreNext500));
    }
    
    // Statistics
    std::vector<double> sortedScores = overallScores;
    std::sort(sortedScores.begin(), sortedScores.end());
    
    double sum = std::accumulate(overallScores.begin(), overallScores.end(), 0.0);
    analysis.meanScore = sum / overallScores.size();
    analysis.medianScore = sortedScores[sortedScores.size() / 2];
    analysis.minScore = sortedScores.front();
    analysis.maxScore = sortedScores.back();
    
    double variance = 0.0;
    for (double s : overallScores) {
        variance += (s - analysis.meanScore) * (s - analysis.meanScore);
    }
    analysis.stdDevScore = std::sqrt(variance / overallScores.size());
    
    // Component trends
    std::vector<std::string> components = {"fps", "frameTime", "memory", "gpu", "stability", "cpu"};
    std::vector<std::vector<double>> componentScores(components.size());
    
    for (const auto& entry : m_history) {
        componentScores[0].push_back(entry.fpsScore);
        componentScores[1].push_back(entry.frameTimeScore);
        componentScores[2].push_back(entry.memoryScore);
        componentScores[3].push_back(entry.gpuScore);
        componentScores[4].push_back(entry.stabilityScore);
        componentScores[5].push_back(entry.cpuScore);
    }
    
    for (size_t i = 0; i < components.size(); ++i) {
        LinearFit compFit = ComputeLinearFit(componentScores[i]);
        analysis.componentTrends[components[i]] = compFit.slope;
        
        if (compFit.valid && compFit.r2 > 0.3) {
            if (compFit.slope > 0.05) {
                analysis.componentTrendDirections[components[i]] = "Improving";
            } else if (compFit.slope < -0.05) {
                analysis.componentTrendDirections[components[i]] = "Declining";
            } else {
                analysis.componentTrendDirections[components[i]] = "Stable";
            }
        } else {
            analysis.componentTrendDirections[components[i]] = "Unknown";
        }
    }
    
    return analysis;
}

bool PerformanceScorer::IsImproving() const {
    if (m_history.size() < 5) return false;
    TrendAnalysis trend = AnalyzeTrend();
    return trend.overallTrend == "Improving" && trend.overallConfidence > 0.5;
}

bool PerformanceScorer::IsDeclining() const {
    if (m_history.size() < 5) return false;
    TrendAnalysis trend = AnalyzeTrend();
    return trend.overallTrend == "Declining" && trend.overallConfidence > 0.5;
}

// ============================================================================
// Regression Detection
// ============================================================================

void PerformanceScorer::DetectRegression(PerformanceScoreCard& card) {
    if (m_history.size() < 2) {
        card.regressionDetected = false;
        card.regressionSeverity = 0.0;
        return;
    }
    
    double prevScore = m_history.back().overallScore;
    double scoreDrop = prevScore - card.overallScore;
    
    card.regressionDetected = false;
    card.regressionSeverity = 0.0;
    card.regressionComponents.clear();
    
    if (scoreDrop >= m_config.regressionThreshold) {
        // Check which components regressed
        const auto& prev = m_history.back();
        
        if (prev.fpsScore - card.fpsScore.score >= m_config.regressionThreshold) {
            card.regressionComponents.push_back("FPS");
        }
        if (prev.frameTimeScore - card.frameTimeScore.score >= m_config.regressionThreshold) {
            card.regressionComponents.push_back("FrameTime");
        }
        if (prev.memoryScore - card.memoryScore.score >= m_config.regressionThreshold) {
            card.regressionComponents.push_back("Memory");
        }
        if (prev.gpuScore - card.gpuScore.score >= m_config.regressionThreshold) {
            card.regressionComponents.push_back("GPU");
        }
        if (prev.stabilityScore - card.stabilityScore.score >= m_config.regressionThreshold) {
            card.regressionComponents.push_back("Stability");
        }
        if (prev.cpuScore - card.cpuScore.score >= m_config.regressionThreshold) {
            card.regressionComponents.push_back("CPU");
        }
        
        if (!card.regressionComponents.empty()) {
            card.regressionDetected = true;
            card.regressionSeverity = std::min(100.0, scoreDrop * 5.0);
        }
        
        // Record regression event
        RegressionEvent event;
        event.timestamp = card.timestamp;
        event.frameNumber = card.frameNumber;
        event.sessionId = card.sessionId;
        event.previousScore = prevScore;
        event.currentScore = card.overallScore;
        event.scoreDelta = scoreDrop;
        event.severity = card.regressionSeverity;
        event.affectedComponents = card.regressionComponents;
        event.acknowledged = false;
        
        std::ostringstream oss;
        oss << "Performance regression detected: score dropped from " 
            << std::fixed << std::setprecision(1) << prevScore 
            << " to " << card.overallScore;
        event.summary = oss.str();
        
        m_regressions.push_back(event);
        
        if (m_regressionCallback) {
            m_regressionCallback(event);
        }
    }
}

bool PerformanceScorer::HasRegression() const {
    return !m_regressions.empty();
}

std::vector<RegressionEvent> PerformanceScorer::GetRegressions() const {
    return m_regressions;
}

std::vector<RegressionEvent> PerformanceScorer::GetUnacknowledgedRegressions() const {
    std::vector<RegressionEvent> result;
    for (const auto& r : m_regressions) {
        if (!r.acknowledged) {
            result.push_back(r);
        }
    }
    return result;
}

bool PerformanceScorer::AcknowledgeRegression(int64_t regressionTimestamp) {
    for (auto& r : m_regressions) {
        if (r.timestamp == regressionTimestamp && !r.acknowledged) {
            r.acknowledged = true;
            r.acknowledgedAt = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
            ).count();
            return true;
        }
    }
    return false;
}

void PerformanceScorer::ClearAcknowledgedRegressions() {
    m_regressions.erase(
        std::remove_if(m_regressions.begin(), m_regressions.end(),
            [](const RegressionEvent& r) { return r.acknowledged; }),
        m_regressions.end()
    );
}

void PerformanceScorer::ClearAllRegressions() {
    m_regressions.clear();
}

// ============================================================================
// Recommendations Generation
// ============================================================================

void PerformanceScorer::GenerateRecommendations(PerformanceScoreCard& card) {
    card.topIssues.clear();
    card.recommendations.clear();
    
    // Collect issues from worst to best
    std::vector<std::pair<std::string, double>> issues;
    
    if (card.fpsScore.score < 70.0) {
        issues.push_back({"FPS score low", card.fpsScore.score});
    }
    if (card.frameTimeScore.score < 70.0) {
        issues.push_back({"Frame time score low", card.frameTimeScore.score});
    }
    if (card.memoryScore.score < 70.0) {
        issues.push_back({"Memory score low", card.memoryScore.score});
    }
    if (card.gpuScore.score < 70.0) {
        issues.push_back({"GPU score low", card.gpuScore.score});
    }
    if (card.stabilityScore.score < 70.0) {
        issues.push_back({"Stability score low", card.stabilityScore.score});
    }
    if (card.cpuScore.score < 70.0) {
        issues.push_back({"CPU score low", card.cpuScore.score});
    }
    
    // Sort by score ascending (worst first)
    std::sort(issues.begin(), issues.end(), 
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    // Top 3 issues
    for (size_t i = 0; i < std::min(size_t(3), issues.size()); ++i) {
        card.topIssues.push_back(issues[i].first);
    }
    
    // Generate recommendations
    if (card.fpsScore.score < 70.0) {
        card.recommendations.push_back(card.fpsScore.recommendation);
    }
    if (card.frameTimeScore.score < 70.0) {
        card.recommendations.push_back(card.frameTimeScore.recommendation);
    }
    if (card.memoryScore.score < 70.0) {
        card.recommendations.push_back(card.memoryScore.recommendation);
    }
    if (card.gpuScore.score < 70.0) {
        card.recommendations.push_back(card.gpuScore.recommendation);
    }
    if (card.stabilityScore.score < 70.0) {
        card.recommendations.push_back(card.stabilityScore.recommendation);
    }
    if (card.cpuScore.score < 70.0) {
        card.recommendations.push_back(card.cpuScore.recommendation);
    }
    
    // Add general recommendations
    if (card.overallScore >= 90.0) {
        card.recommendations.push_back("Performance is excellent. Maintain current optimization.");
    } else if (card.overallScore >= 80.0) {
        card.recommendations.push_back("Performance is good. Minor optimizations possible.");
    } else if (card.overallScore < 60.0) {
        card.recommendations.push_back("Performance is critical. Immediate action required.");
    }
    
    // Deduplicate
    std::sort(card.recommendations.begin(), card.recommendations.end());
    card.recommendations.erase(
        std::unique(card.recommendations.begin(), card.recommendations.end()),
        card.recommendations.end()
    );
}

// ============================================================================
// Statistical Queries
// ============================================================================

double PerformanceScorer::GetAverageScore(int lastN) const {
    if (m_history.empty()) return 0.0;
    
    std::vector<double> scores = GetRecentHistory(lastN <= 0 ? INT_MAX : lastN);
    scores.clear();
    for (const auto& entry : m_history) {
        scores.push_back(entry.overallScore);
    }
    if (lastN > 0 && scores.size() > static_cast<size_t>(lastN)) {
        scores.erase(scores.begin(), scores.end() - lastN);
    }
    
    if (scores.empty()) return 0.0;
    double sum = std::accumulate(scores.begin(), scores.end(), 0.0);
    return sum / scores.size();
}

double PerformanceScorer::GetMedianScore(int lastN) const {
    std::vector<double> scores;
    for (const auto& entry : m_history) {
        scores.push_back(entry.overallScore);
    }
    if (lastN > 0 && scores.size() > static_cast<size_t>(lastN)) {
        scores.erase(scores.begin(), scores.end() - lastN);
    }
    
    if (scores.empty()) return 0.0;
    std::sort(scores.begin(), scores.end());
    return scores[scores.size() / 2];
}

double PerformanceScorer::GetMinScore(int lastN) const {
    std::vector<double> scores;
    for (const auto& entry : m_history) {
        scores.push_back(entry.overallScore);
    }
    if (lastN > 0 && scores.size() > static_cast<size_t>(lastN)) {
        scores.erase(scores.begin(), scores.end() - lastN);
    }
    
    if (scores.empty()) return 0.0;
    return *std::min_element(scores.begin(), scores.end());
}

double PerformanceScorer::GetMaxScore(int lastN) const {
    std::vector<double> scores;
    for (const auto& entry : m_history) {
        scores.push_back(entry.overallScore);
    }
    if (lastN > 0 && scores.size() > static_cast<size_t>(lastN)) {
        scores.erase(scores.begin(), scores.end() - lastN);
    }
    
    if (scores.empty()) return 0.0;
    return *std::max_element(scores.begin(), scores.end());
}

double PerformanceScorer::GetScorePercentile(double percentile) const {
    if (m_history.empty()) return 0.0;
    
    std::vector<double> scores;
    for (const auto& entry : m_history) {
        scores.push_back(entry.overallScore);
    }
    
    return ComputePercentile(scores, percentile);
}

double PerformanceScorer::ComputePercentile(std::vector<double> values, double percentile) const {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(percentile / 100.0 * (values.size() - 1));
    return values[std::min(index, values.size() - 1)];
}

// ============================================================================
// Callbacks
// ============================================================================

void PerformanceScorer::SetRegressionCallback(RegressionCallback callback) {
    m_regressionCallback = callback;
}

void PerformanceScorer::SetScoreCallback(ScoreCallback callback) {
    m_scoreCallback = callback;
}

// ============================================================================
// Export
// ============================================================================

std::string PerformanceScorer::ExportScoreCardToJSON(const PerformanceScoreCard& card) const {
    return card.ToJSON();
}

std::string PerformanceScorer::ExportHistoryToJSON() const {
    std::ostringstream oss;
    oss << "{\"history\":[";
    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto& entry = m_history[i];
        if (i > 0) oss << ",";
        oss << "{";
        oss << "\"timestamp\":" << entry.timestamp << ",";
        oss << "\"frameNumber\":" << entry.frameNumber << ",";
        oss << "\"overallScore\":" << std::fixed << std::setprecision(2) << entry.overallScore << ",";
        oss << "\"fpsScore\":" << entry.fpsScore << ",";
        oss << "\"frameTimeScore\":" << entry.frameTimeScore << ",";
        oss << "\"memoryScore\":" << entry.memoryScore << ",";
        oss << "\"gpuScore\":" << entry.gpuScore << ",";
        oss << "\"stabilityScore\":" << entry.stabilityScore << ",";
        oss << "\"cpuScore\":" << entry.cpuScore << ",";
        oss << "\"sessionId\":\"" << entry.sessionId << "\"";
        oss << "}";
    }
    oss << "],";
    oss << "\"trend\":" << AnalyzeTrend().ToJSON();
    oss << "}";
    return oss.str();
}

std::string PerformanceScorer::ExportRegressionsToJSON() const {
    std::ostringstream oss;
    oss << "{\"regressions\":[";
    for (size_t i = 0; i < m_regressions.size(); ++i) {
        const auto& r = m_regressions[i];
        if (i > 0) oss << ",";
        oss << "{";
        oss << "\"timestamp\":" << r.timestamp << ",";
        oss << "\"previousScore\":" << std::fixed << std::setprecision(2) << r.previousScore << ",";
        oss << "\"currentScore\":" << r.currentScore << ",";
        oss << "\"scoreDelta\":" << r.scoreDelta << ",";
        oss << "\"severity\":" << r.severity << ",";
        oss << "\"summary\":\"" << r.summary << "\",";
        oss << "\"acknowledged\":" << (r.acknowledged ? "true" : "false") << ",";
        oss << "\"affectedComponents\":[";
        for (size_t j = 0; j < r.affectedComponents.size(); ++j) {
            if (j > 0) oss << ",";
            oss << "\"" << r.affectedComponents[j] << "\"";
        }
        oss << "]";
        oss << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string PerformanceScorer::ExportFullReport() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"latestScoreCard\":" << m_cachedScoreCard.ToJSON() << ",";
    oss << "\"history\":" << ExportHistoryToJSON() << ",";
    oss << "\"regressions\":" << ExportRegressionsToJSON();
    oss << "}";
    return oss.str();
}

} // namespace ProfilerCore
