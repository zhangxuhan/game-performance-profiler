#include "ComparativeAnalyzer.h"
#include "StatisticsAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace ProfilerCore {

// ============================================================================
// JSON Serialization Helpers
// ============================================================================

std::string MetricComparison::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"name\":\"" << name << "\",";
    oss << "\"baselineValue\":" << std::fixed << std::setprecision(3) << baselineValue << ",";
    oss << "\"currentValue\":" << currentValue << ",";
    oss << "\"absoluteDelta\":" << absoluteDelta << ",";
    oss << "\"percentChange\":" << percentChange << ",";
    oss << "\"isRegression\":" << (isRegression ? "true" : "false") << ",";
    oss << "\"severity\":" << severity << ",";
    oss << "\"interpretation\":\"" << interpretation << "\"";
    oss << "}";
    return oss.str();
}

std::string CategoryComparison::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"category\":\"" << categoryName << "\",";
    oss << "\"overallScore\":" << std::fixed << std::setprecision(2) << overallScore << ",";
    oss << "\"hasRegressions\":" << (hasRegressions ? "true" : "false") << ",";
    oss << "\"regressionCount\":" << regressionCount << ",";
    oss << "\"summary\":\"" << summary << "\",";
    oss << "\"metrics\":[";
    for (size_t i = 0; i < metrics.size(); ++i) {
        oss << metrics[i].ToJSON();
        if (i < metrics.size() - 1) oss << ",";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

std::string ComparisonReport::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"baselineId\":\"" << baselineId << "\",";
    oss << "\"currentId\":\"" << currentId << "\",";
    oss << "\"baselineTimestamp\":" << baselineTimestamp << ",";
    oss << "\"currentTimestamp\":" << currentTimestamp << ",";
    oss << "\"baselineSampleCount\":" << baselineSampleCount << ",";
    oss << "\"currentSampleCount\":" << currentSampleCount << ",";
    oss << "\"overallScoreDelta\":" << std::fixed << std::setprecision(2) << overallScoreDelta << ",";
    oss << "\"isOverallRegression\":" << (isOverallRegression ? "true" : "false") << ",";
    oss << "\"verdict\":\"" << verdict << "\",";
    oss << "\"summary\":\"" << summary << "\",";
    oss << "\"categories\":[";
    for (size_t i = 0; i < categories.size(); ++i) {
        oss << categories[i].ToJSON();
        if (i < categories.size() - 1) oss << ",";
    }
    oss << "],";
    oss << "\"recommendations\":[";
    for (size_t i = 0; i < recommendations.size(); ++i) {
        oss << "\"" << recommendations[i] << "\"";
        if (i < recommendations.size() - 1) oss << ",";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

std::string HistoricalComparisonPoint::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"timestamp\":" << timestamp << ",";
    oss << "\"sessionId\":\"" << sessionId << "\",";
    oss << "\"overallScore\":" << std::fixed << std::setprecision(2) << overallScore << ",";
    oss << "\"avgFps\":" << std::setprecision(2) << avgFps << ",";
    oss << "\"avgFrameTime\":" << std::setprecision(3) << avgFrameTime << ",";
    oss << "\"peakMemory\":" << peakMemory << ",";
    oss << "\"stabilityScore\":" << std::setprecision(2) << stabilityScore << ",";
    oss << "\"alertCount\":" << alertCount;
    oss << "}";
    return oss.str();
}

std::string MetricTrendOverTime::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"metricName\":\"" << metricName << "\",";
    oss << "\"slope\":" << std::fixed << std::setprecision(6) << slope << ",";
    oss << "\"rSquared\":" << std::setprecision(4) << rSquared << ",";
    oss << "\"isImproving\":" << (isImproving ? "true" : "false") << ",";
    oss << "\"trendDescription\":\"" << trendDescription << "\",";
    oss << "\"dataPoints\":[";
    for (size_t i = 0; i < dataPoints.size(); ++i) {
        oss << "[" << dataPoints[i].first << "," << dataPoints[i].second << "]";
        if (i < dataPoints.size() - 1) oss << ",";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

// ============================================================================
// ComparativeAnalyzer Implementation
// ============================================================================

ComparativeAnalyzer::ComparativeAnalyzer() {
    // Default thresholds are set in struct definition
}

ComparativeAnalyzer::~ComparativeAnalyzer() {
}

void ComparativeAnalyzer::SetRegressionThresholds(const RegressionThresholds& thresholds) {
    m_thresholds = thresholds;
}

// ============================================================================
// Main Comparison Methods
// ============================================================================

ComparisonReport ComparativeAnalyzer::CompareStats(
    const SummaryStats& baselineStats,
    const SummaryStats& currentStats,
    const std::string& baselineId,
    const std::string& currentId)
{
    ComparisonReport report;
    report.baselineId = baselineId;
    report.currentId = currentId;
    report.baselineTimestamp = baselineStats.timestamp;
    report.currentTimestamp = currentStats.timestamp;
    report.baselineSampleCount = baselineStats.sampleCount;
    report.currentSampleCount = currentStats.sampleCount;
    
    // Compare each category
    report.categories.push_back(CompareFPSMetrics(baselineStats, currentStats));
    report.categories.push_back(CompareFrameTimeMetrics(baselineStats, currentStats));
    report.categories.push_back(CompareMemoryMetrics(baselineStats, currentStats));
    report.categories.push_back(CompareStabilityMetrics(baselineStats, currentStats));
    
    // Overall assessment
    double baselineOverall = baselineStats.stabilityScore;
    double currentOverall = currentStats.stabilityScore;
    report.overallScoreDelta = currentOverall - baselineOverall;
    report.isOverallRegression = report.overallScoreDelta < -10.0;
    
    report.verdict = DetermineVerdict(report.categories);
    report.recommendations = GenerateRecommendations(report.categories);
    
    // Generate summary
    int totalRegressions = 0;
    for (const auto& cat : report.categories) {
        totalRegressions += cat.regressionCount;
    }
    
    std::ostringstream summaryStream;
    if (report.verdict == "IMPROVED") {
        summaryStream << "Performance improved compared to baseline. ";
        summaryStream << "Overall score increased by " << std::fixed << std::setprecision(1) 
                      << report.overallScoreDelta << " points.";
    } else if (report.verdict == "REGRESSED") {
        summaryStream << "Performance regression detected! ";
        summaryStream << totalRegressions << " metric(s) showing degradation. ";
        summaryStream << "Overall score decreased by " << std::fixed << std::setprecision(1)
                      << std::abs(report.overallScoreDelta) << " points.";
    } else if (report.verdict == "UNCHANGED") {
        summaryStream << "Performance is stable compared to baseline. ";
        summaryStream << "No significant changes detected.";
    } else {
        summaryStream << "Comparison inconclusive. Insufficient data for reliable analysis.";
    }
    report.summary = summaryStream.str();
    
    // Notify callback if regression detected
    if (report.isOverallRegression && m_regressionCallback) {
        m_regressionCallback(report);
    }
    
    return report;
}

ComparisonReport ComparativeAnalyzer::CompareFrameData(
    const std::vector<std::tuple<double, double, size_t>>& baselineFrames,
    const std::vector<std::tuple<double, double, size_t>>& currentFrames,
    const std::string& baselineId,
    const std::string& currentId)
{
    // Convert frame data to SummaryStats
    auto computeStats = [](const std::vector<std::tuple<double, double, size_t>>& frames) -> SummaryStats {
        SummaryStats stats;
        if (frames.empty()) return stats;
        
        stats.sampleCount = static_cast<int>(frames.size());
        stats.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        
        double fpsSum = 0, ftSum = 0;
        double minFps = std::numeric_limits<double>::max();
        double maxFps = std::numeric_limits<double>::lowest();
        double minFt = std::numeric_limits<double>::max();
        double maxFt = std::numeric_limits<double>::lowest();
        size_t peakMem = 0;
        size_t memSum = 0;
        
        std::vector<double> fpsValues, ftValues;
        
        for (const auto& frame : frames) {
            double fps = std::get<0>(frame);
            double ft = std::get<1>(frame);
            size_t mem = std::get<2>(frame);
            
            fpsSum += fps;
            ftSum += ft;
            minFps = std::min(minFps, fps);
            maxFps = std::max(maxFps, fps);
            minFt = std::min(minFt, ft);
            maxFt = std::max(maxFt, ft);
            peakMem = std::max(peakMem, mem);
            memSum += mem;
            
            fpsValues.push_back(fps);
            ftValues.push_back(ft);
        }
        
        stats.avgFps = fpsSum / frames.size();
        stats.minFps = minFps;
        stats.maxFps = maxFps;
        stats.avgFrameTime = ftSum / frames.size();
        stats.minFrameTime = minFt;
        stats.maxFrameTime = maxFt;
        stats.peakMemoryUsage = peakMem;
        stats.avgMemoryUsage = memSum / frames.size();
        
        // Calculate percentiles
        std::sort(fpsValues.begin(), fpsValues.end());
        size_t n = fpsValues.size();
        auto getPercentile = [&](double p) -> double {
            double idx = (p / 100.0) * (static_cast<double>(n) - 1.0);
            size_t lower = static_cast<size_t>(std::floor(idx));
            size_t upper = static_cast<size_t>(std::ceil(idx));
            if (lower == upper || upper >= n) return fpsValues[lower];
            double fraction = idx - static_cast<double>(lower);
            return fpsValues[lower] * (1.0 - fraction) + fpsValues[upper] * fraction;
        };
        
        stats.p50Fps = getPercentile(50.0);
        stats.p90Fps = getPercentile(90.0);
        stats.p95Fps = getPercentile(95.0);
        stats.p99Fps = getPercentile(99.0);
        
        // Calculate stability score
        double mean = stats.avgFps;
        double variance = 0;
        for (double fps : fpsValues) {
            double diff = fps - mean;
            variance += diff * diff;
        }
        variance /= frames.size();
        double stdDev = std::sqrt(variance);
        double cv = (mean > 0) ? stdDev / mean : 1.0;
        stats.stabilityScore = 100.0 * (1.0 - std::min(cv * 2.0, 1.0));
        
        return stats;
    };
    
    SummaryStats baselineStats = computeStats(baselineFrames);
    SummaryStats currentStats = computeStats(currentFrames);
    
    return CompareStats(baselineStats, currentStats, baselineId, currentId);
}

// ============================================================================
// Category Comparison Methods
// ============================================================================

CategoryComparison ComparativeAnalyzer::CompareFPSMetrics(
    const SummaryStats& baseline, 
    const SummaryStats& current)
{
    CategoryComparison category;
    category.category = ComparisonCategory::FPS;
    category.categoryName = "Frame Rate";
    category.hasRegressions = false;
    category.regressionCount = 0;
    
    // Compare average FPS (higher is better)
    MetricComparison avgFps = CreateMetricComparison(
        "Average FPS",
        baseline.avgFps,
        current.avgFps,
        false,  // higher is better
        m_thresholds.fpsRegressionWarningPercent,
        m_thresholds.fpsRegressionCriticalPercent
    );
    category.metrics.push_back(avgFps);
    
    // Compare minimum FPS (higher is better)
    MetricComparison minFps = CreateMetricComparison(
        "Minimum FPS",
        baseline.minFps,
        current.minFps,
        false,
        m_thresholds.fpsRegressionWarningPercent,
        m_thresholds.fpsRegressionCriticalPercent
    );
    category.metrics.push_back(minFps);
    
    // Compare P99 FPS (higher is better)
    MetricComparison p99Fps = CreateMetricComparison(
        "P99 FPS",
        baseline.p99Fps,
        current.p99Fps,
        false,
        m_thresholds.fpsRegressionWarningPercent,
        m_thresholds.fpsRegressionCriticalPercent
    );
    category.metrics.push_back(p99Fps);
    
    // Compare P95 FPS (higher is better)
    MetricComparison p95Fps = CreateMetricComparison(
        "P95 FPS",
        baseline.p95Fps,
        current.p95Fps,
        false,
        m_thresholds.fpsRegressionWarningPercent,
        m_thresholds.fpsRegressionCriticalPercent
    );
    category.metrics.push_back(p95Fps);
    
    // Calculate category score (0-100, 50 = no change, >50 = improved, <50 = regressed)
    double totalSeverity = 0;
    int metricCount = 0;
    for (const auto& m : category.metrics) {
        if (m.isRegression) {
            category.hasRegressions = true;
            category.regressionCount++;
            totalSeverity += m.severity;
        }
        metricCount++;
    }
    
    category.overallScore = 50.0 - (totalSeverity / metricCount * 50.0) + 
                            (category.hasRegressions ? 0 : 
                             (avgFps.percentChange > 0 ? std::min(avgFps.percentChange, 20.0) : 0));
    category.overallScore = std::max(0.0, std::min(100.0, category.overallScore));
    
    // Generate summary
    std::ostringstream summary;
    if (category.hasRegressions) {
        summary << "FPS regression detected: " << category.regressionCount << " metric(s) degraded. ";
        summary << "Avg FPS: " << std::fixed << std::setprecision(1) << baseline.avgFps 
                << " -> " << current.avgFps;
    } else if (avgFps.percentChange > 2.0) {
        summary << "FPS improved by " << std::fixed << std::setprecision(1) 
                << avgFps.percentChange << "%. ";
        summary << "Avg FPS: " << baseline.avgFps << " -> " << current.avgFps;
    } else {
        summary << "FPS stable. Avg: " << std::fixed << std::setprecision(1) << current.avgFps << " FPS";
    }
    category.summary = summary.str();
    
    return category;
}

CategoryComparison ComparativeAnalyzer::CompareFrameTimeMetrics(
    const SummaryStats& baseline,
    const SummaryStats& current)
{
    CategoryComparison category;
    category.category = ComparisonCategory::FrameTime;
    category.categoryName = "Frame Time";
    category.hasRegressions = false;
    category.regressionCount = 0;
    
    // Compare average frame time (lower is better)
    MetricComparison avgFt = CreateMetricComparison(
        "Average Frame Time",
        baseline.avgFrameTime,
        current.avgFrameTime,
        true,  // lower is better
        m_thresholds.frameTimeRegressionWarningPercent,
        m_thresholds.frameTimeRegressionCriticalPercent
    );
    category.metrics.push_back(avgFt);
    
    // Compare maximum frame time (lower is better)
    MetricComparison maxFt = CreateMetricComparison(
        "Maximum Frame Time",
        baseline.maxFrameTime,
        current.maxFrameTime,
        true,
        m_thresholds.frameTimeRegressionWarningPercent,
        m_thresholds.frameTimeRegressionCriticalPercent
    );
    category.metrics.push_back(maxFt);
    
    // Compare standard deviation (lower is better)
    MetricComparison stdDev = CreateMetricComparison(
        "Frame Time Std Dev",
        baseline.stdDevFrameTime,
        current.stdDevFrameTime,
        true,
        m_thresholds.frameTimeRegressionWarningPercent,
        m_thresholds.frameTimeRegressionCriticalPercent
    );
    category.metrics.push_back(stdDev);
    
    // Compare median frame time (lower is better)
    MetricComparison medianFt = CreateMetricComparison(
        "Median Frame Time",
        baseline.medianFrameTime,
        current.medianFrameTime,
        true,
        m_thresholds.frameTimeRegressionWarningPercent,
        m_thresholds.frameTimeRegressionCriticalPercent
    );
    category.metrics.push_back(medianFt);
    
    // Calculate category score
    double totalSeverity = 0;
    for (const auto& m : category.metrics) {
        if (m.isRegression) {
            category.hasRegressions = true;
            category.regressionCount++;
            totalSeverity += m.severity;
        }
    }
    
    category.overallScore = 50.0 - (totalSeverity / category.metrics.size() * 50.0);
    category.overallScore = std::max(0.0, std::min(100.0, category.overallScore));
    
    // Generate summary
    std::ostringstream summary;
    if (category.hasRegressions) {
        summary << "Frame time regression: " << category.regressionCount << " metric(s) degraded. ";
        summary << "Avg: " << std::fixed << std::setprecision(2) << baseline.avgFrameTime 
                << "ms -> " << current.avgFrameTime << "ms";
    } else {
        summary << "Frame time stable. Avg: " << std::fixed << std::setprecision(2) 
                << current.avgFrameTime << "ms";
    }
    category.summary = summary.str();
    
    return category;
}

CategoryComparison ComparativeAnalyzer::CompareMemoryMetrics(
    const SummaryStats& baseline,
    const SummaryStats& current)
{
    CategoryComparison category;
    category.category = ComparisonCategory::Memory;
    category.categoryName = "Memory";
    category.hasRegressions = false;
    category.regressionCount = 0;
    
    // Compare peak memory (lower is better)
    double baselinePeakMB = static_cast<double>(baseline.peakMemoryUsage) / (1024.0 * 1024.0);
    double currentPeakMB = static_cast<double>(current.peakMemoryUsage) / (1024.0 * 1024.0);
    
    MetricComparison peakMem = CreateMetricComparison(
        "Peak Memory (MB)",
        baselinePeakMB,
        currentPeakMB,
        true,  // lower is better
        m_thresholds.memoryRegressionWarningPercent,
        m_thresholds.memoryRegressionCriticalPercent
    );
    category.metrics.push_back(peakMem);
    
    // Compare average memory (lower is better)
    double baselineAvgMB = static_cast<double>(baseline.avgMemoryUsage) / (1024.0 * 1024.0);
    double currentAvgMB = static_cast<double>(current.avgMemoryUsage) / (1024.0 * 1024.0);
    
    MetricComparison avgMem = CreateMetricComparison(
        "Average Memory (MB)",
        baselineAvgMB,
        currentAvgMB,
        true,
        m_thresholds.memoryRegressionWarningPercent,
        m_thresholds.memoryRegressionCriticalPercent
    );
    category.metrics.push_back(avgMem);
    
    // Compare memory growth rate (lower is better)
    double baselineGrowthKB = baseline.memoryGrowthRate / 1024.0;
    double currentGrowthKB = current.memoryGrowthRate / 1024.0;
    
    MetricComparison growthRate = CreateMetricComparison(
        "Memory Growth Rate (KB/frame)",
        baselineGrowthKB,
        currentGrowthKB,
        true,
        m_thresholds.memoryRegressionWarningPercent,
        m_thresholds.memoryRegressionCriticalPercent
    );
    category.metrics.push_back(growthRate);
    
    // Calculate category score
    double totalSeverity = 0;
    for (const auto& m : category.metrics) {
        if (m.isRegression) {
            category.hasRegressions = true;
            category.regressionCount++;
            totalSeverity += m.severity;
        }
    }
    
    category.overallScore = 50.0 - (totalSeverity / category.metrics.size() * 50.0);
    category.overallScore = std::max(0.0, std::min(100.0, category.overallScore));
    
    // Generate summary
    std::ostringstream summary;
    if (category.hasRegressions) {
        summary << "Memory regression: " << category.regressionCount << " metric(s) degraded. ";
        summary << "Peak: " << std::fixed << std::setprecision(1) << baselinePeakMB 
                << "MB -> " << currentPeakMB << "MB";
    } else {
        summary << "Memory stable. Peak: " << std::fixed << std::setprecision(1) 
                << currentPeakMB << "MB";
    }
    category.summary = summary.str();
    
    return category;
}

CategoryComparison ComparativeAnalyzer::CompareStabilityMetrics(
    const SummaryStats& baseline,
    const SummaryStats& current)
{
    CategoryComparison category;
    category.category = ComparisonCategory::Stability;
    category.categoryName = "Stability";
    category.hasRegressions = false;
    category.regressionCount = 0;
    
    // Compare stability score (higher is better)
    MetricComparison stabilityScore = CreateMetricComparison(
        "Stability Score",
        baseline.stabilityScore,
        current.stabilityScore,
        false,  // higher is better
        m_thresholds.stabilityRegressionWarningPoints,
        m_thresholds.stabilityRegressionCriticalPoints
    );
    // For stability, we use absolute point change, not percent
    stabilityScore.percentChange = current.stabilityScore - baseline.stabilityScore;
    stabilityScore.isRegression = stabilityScore.percentChange < -m_thresholds.stabilityRegressionWarningPoints;
    stabilityScore.severity = std::abs(stabilityScore.percentChange) / 100.0;
    stabilityScore.interpretation = InterpretPercentChange(stabilityScore.percentChange, false);
    category.metrics.push_back(stabilityScore);
    
    // Compare alert count (lower is better)
    MetricComparison alertCount = CreateMetricComparison(
        "Alert Count",
        static_cast<double>(baseline.alertCount),
        static_cast<double>(current.alertCount),
        true,  // lower is better
        20.0,  // 20% more alerts = warning
        50.0   // 50% more alerts = critical
    );
    category.metrics.push_back(alertCount);
    
    // Calculate category score
    double totalSeverity = 0;
    for (const auto& m : category.metrics) {
        if (m.isRegression) {
            category.hasRegressions = true;
            category.regressionCount++;
            totalSeverity += m.severity;
        }
    }
    
    category.overallScore = current.stabilityScore;
    
    // Generate summary
    std::ostringstream summary;
    if (category.hasRegressions) {
        summary << "Stability regression: Score dropped by " 
                << std::fixed << std::setprecision(1) 
                << std::abs(stabilityScore.percentChange) << " points. ";
        summary << "Alerts: " << baseline.alertCount << " -> " << current.alertCount;
    } else {
        summary << "Stability good. Score: " << std::fixed << std::setprecision(1) 
                << current.stabilityScore;
    }
    category.summary = summary.str();
    
    return category;
}

// ============================================================================
// Metric Comparison Helper
// ============================================================================

MetricComparison ComparativeAnalyzer::CreateMetricComparison(
    const std::string& name,
    double baselineValue,
    double currentValue,
    bool lowerIsBetter,
    double warningThreshold,
    double criticalThreshold)
{
    MetricComparison comp;
    comp.name = name;
    comp.baselineValue = baselineValue;
    comp.currentValue = currentValue;
    
    // Calculate delta
    comp.absoluteDelta = currentValue - baselineValue;
    
    // Calculate percent change (handle zero baseline)
    if (std::abs(baselineValue) > 1e-10) {
        comp.percentChange = (comp.absoluteDelta / baselineValue) * 100.0;
    } else {
        comp.percentChange = (std::abs(currentValue) > 1e-10) ? 100.0 : 0.0;
    }
    
    // Determine if this is a regression
    comp.isRegression = IsRegression(
        comp.percentChange, 
        lowerIsBetter, 
        warningThreshold, 
        criticalThreshold
    );
    
    // Calculate severity
    comp.severity = CalculateRegressionSeverity(
        comp.percentChange,
        lowerIsBetter,
        warningThreshold,
        criticalThreshold
    );
    
    // Generate interpretation
    comp.interpretation = InterpretPercentChange(comp.percentChange, lowerIsBetter);
    
    return comp;
}

// ============================================================================
// Historical Tracking
// ============================================================================

void ComparativeAnalyzer::RecordHistoricalPoint(const HistoricalComparisonPoint& point) {
    m_historicalData.push_back(point);
    
    // Trim to max size
    if (m_historicalData.size() > m_maxHistorySize) {
        m_historicalData.erase(m_historicalData.begin());
    }
}

void ComparativeAnalyzer::ClearHistory() {
    m_historicalData.clear();
}

MetricTrendOverTime ComparativeAnalyzer::AnalyzeMetricTrend(const std::string& metricName) const {
    MetricTrendOverTime trend;
    trend.metricName = metricName;
    trend.slope = 0.0;
    trend.rSquared = 0.0;
    trend.isImproving = false;
    
    if (m_historicalData.size() < 3) {
        trend.trendDescription = "Insufficient data for trend analysis";
        return trend;
    }
    
    // Extract data points for the requested metric
    for (const auto& point : m_historicalData) {
        double value = 0.0;
        if (metricName == "overallScore") value = point.overallScore;
        else if (metricName == "avgFps") value = point.avgFps;
        else if (metricName == "avgFrameTime") value = point.avgFrameTime;
        else if (metricName == "peakMemory") value = static_cast<double>(point.peakMemory);
        else if (metricName == "stabilityScore") value = point.stabilityScore;
        else continue;
        
        trend.dataPoints.push_back({point.timestamp, value});
    }
    
    if (trend.dataPoints.size() < 3) {
        trend.trendDescription = "Insufficient data points for metric: " + metricName;
        return trend;
    }
    
    // Linear regression on the data points
    size_t n = trend.dataPoints.size();
    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
    
    for (const auto& dp : trend.dataPoints) {
        double x = static_cast<double>(dp.first);
        double y = dp.second;
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
        sumY2 += y * y;
    }
    
    double xMean = sumX / n;
    double yMean = sumY / n;
    
    double denominator = n * sumX2 - sumX * sumX;
    if (std::abs(denominator) < 1e-10) {
        trend.trendDescription = "Cannot compute trend - constant values";
        return trend;
    }
    
    trend.slope = (n * sumXY - sumX * sumY) / denominator;
    double intercept = yMean - trend.slope * xMean;
    
    // Calculate R-squared
    double ssTotal = 0, ssResidual = 0;
    for (const auto& dp : trend.dataPoints) {
        double x = static_cast<double>(dp.first);
        double y = dp.second;
        double yPred = trend.slope * x + intercept;
        ssTotal += (y - yMean) * (y - yMean);
        ssResidual += (y - yPred) * (y - yPred);
    }
    
    trend.rSquared = (ssTotal > 1e-10) ? (1.0 - ssResidual / ssTotal) : 1.0;
    
    // Determine if improving
    bool lowerIsBetter = (metricName == "avgFrameTime" || metricName == "peakMemory");
    trend.isImproving = lowerIsBetter ? (trend.slope < 0) : (trend.slope > 0);
    
    // Generate description
    std::ostringstream desc;
    desc << std::fixed << std::setprecision(2);
    if (trend.rSquared < 0.3) {
        desc << "No clear trend (R²=" << trend.rSquared << ")";
    } else if (trend.isImproving) {
        desc << metricName << " is improving over time";
    } else {
        desc << metricName << " is degrading over time";
    }
    trend.trendDescription = desc.str();
    
    return trend;
}

ComparisonReport ComparativeAnalyzer::CompareToHistoricalAverage(const SummaryStats& currentStats) {
    if (m_historicalData.empty()) {
        ComparisonReport report;
        report.verdict = "INCONCLUSIVE";
        report.summary = "No historical data available for comparison";
        return report;
    }
    
    // Calculate historical averages
    SummaryStats avgStats;
    avgStats.timestamp = currentStats.timestamp;
    avgStats.sampleCount = currentStats.sampleCount;
    
    double fpsSum = 0, ftSum = 0, memSum = 0, stabSum = 0;
    for (const auto& point : m_historicalData) {
        fpsSum += point.avgFps;
        ftSum += point.avgFrameTime;
        memSum += point.peakMemory;
        stabSum += point.stabilityScore;
    }
    
    size_t n = m_historicalData.size();
    avgStats.avgFps = fpsSum / n;
    avgStats.avgFrameTime = ftSum / n;
    avgStats.peakMemoryUsage = static_cast<size_t>(memSum / n);
    avgStats.stabilityScore = stabSum / n;
    
    return CompareStats(avgStats, currentStats, "historical_average", "current");
}

ComparisonReport ComparativeAnalyzer::CompareToBestSession(const SummaryStats& currentStats) {
    if (m_historicalData.empty()) {
        ComparisonReport report;
        report.verdict = "INCONCLUSIVE";
        report.summary = "No historical data available for comparison";
        return report;
    }
    
    // Find best session by overall score
    auto bestIt = std::max_element(m_historicalData.begin(), m_historicalData.end(),
        [](const HistoricalComparisonPoint& a, const HistoricalComparisonPoint& b) {
            return a.overallScore < b.overallScore;
        });
    
    SummaryStats bestStats;
    bestStats.timestamp = bestIt->timestamp;
    bestStats.avgFps = bestIt->avgFps;
    bestStats.avgFrameTime = bestIt->avgFrameTime;
    bestStats.peakMemoryUsage = bestIt->peakMemory;
    bestStats.stabilityScore = bestIt->stabilityScore;
    
    return CompareStats(bestStats, currentStats, "best_session_" + bestIt->sessionId, "current");
}

ComparisonReport ComparativeAnalyzer::CompareToWorstSession(const SummaryStats& currentStats) {
    if (m_historicalData.empty()) {
        ComparisonReport report;
        report.verdict = "INCONCLUSIVE";
        report.summary = "No historical data available for comparison";
        return report;
    }
    
    // Find worst session by overall score
    auto worstIt = std::min_element(m_historicalData.begin(), m_historicalData.end(),
        [](const HistoricalComparisonPoint& a, const HistoricalComparisonPoint& b) {
            return a.overallScore < b.overallScore;
        });
    
    SummaryStats worstStats;
    worstStats.timestamp = worstIt->timestamp;
    worstStats.avgFps = worstIt->avgFps;
    worstStats.avgFrameTime = worstIt->avgFrameTime;
    worstStats.peakMemoryUsage = worstIt->peakMemory;
    worstStats.stabilityScore = worstIt->stabilityScore;
    
    return CompareStats(worstStats, currentStats, "worst_session_" + worstIt->sessionId, "current");
}

// ============================================================================
// Statistical Analysis
// ============================================================================

double ComparativeAnalyzer::PerformTTest(
    const std::vector<double>& sample1,
    const std::vector<double>& sample2) const
{
    if (sample1.size() < 2 || sample2.size() < 2) {
        return 1.0;  // Cannot perform test
    }
    
    double mean1 = CalculateMean(sample1);
    double mean2 = CalculateMean(sample2);
    double var1 = CalculateVariance(sample1, mean1);
    double var2 = CalculateVariance(sample2, mean2);
    
    size_t n1 = sample1.size();
    size_t n2 = sample2.size();
    
    // Welch's t-test (unequal variances)
    double se = std::sqrt(var1 / n1 + var2 / n2);
    if (se < 1e-10) return 1.0;
    
    double t = (mean1 - mean2) / se;
    
    // Approximate degrees of freedom (Welch-Satterthwaite)
    double df_num = std::pow(var1 / n1 + var2 / n2, 2);
    double df_denom = std::pow(var1 / n1, 2) / (n1 - 1) + std::pow(var2 / n2, 2) / (n2 - 1);
    double df = df_num / df_denom;
    
    // Approximate p-value using normal distribution for large samples
    // For small samples, this is an approximation
    double z = std::abs(t);
    double p = 2.0 * (1.0 - std::erf(z / std::sqrt(2.0)));
    
    return p;
}

bool ComparativeAnalyzer::IsSignificantDifference(
    const std::vector<double>& baseline,
    const std::vector<double>& current) const
{
    double pValue = PerformTTest(baseline, current);
    return pValue < m_thresholds.significanceLevel;
}

// ============================================================================
// Export Methods
// ============================================================================

std::string ComparativeAnalyzer::ExportReportToJSON(const ComparisonReport& report) const {
    return report.ToJSON();
}

std::string ComparativeAnalyzer::ExportHistoryToJSON() const {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < m_historicalData.size(); ++i) {
        oss << m_historicalData[i].ToJSON();
        if (i < m_historicalData.size() - 1) oss << ",";
    }
    oss << "]";
    return oss.str();
}

std::string ComparativeAnalyzer::ExportTrendsToJSON() const {
    std::vector<std::string> metrics = {"overallScore", "avgFps", "avgFrameTime", "peakMemory", "stabilityScore"};
    
    std::ostringstream oss;
    oss << "{";
    for (size_t i = 0; i < metrics.size(); ++i) {
        MetricTrendOverTime trend = AnalyzeMetricTrend(metrics[i]);
        oss << "\"" << metrics[i] << "\":" << trend.ToJSON();
        if (i < metrics.size() - 1) oss << ",";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// Helper Methods
// ============================================================================

std::string ComparativeAnalyzer::InterpretPercentChange(double percentChange, bool lowerIsBetter) {
    double absChange = std::abs(percentChange);
    
    if (absChange < 2.0) {
        return "Negligible change";
    } else if (absChange < 5.0) {
        return "Minor change";
    } else if (absChange < 10.0) {
        return "Moderate change";
    } else if (absChange < 20.0) {
        return "Significant change";
    } else {
        return "Major change";
    }
    
    // Add direction
    std::string direction = (percentChange > 0) ? " increase" : " decrease";
    std::string quality = lowerIsBetter ? 
        ((percentChange > 0) ? " (worse)" : " (better)") :
        ((percentChange > 0) ? " (better)" : " (worse)");
    
    return InterpretPercentChange(percentChange, lowerIsBetter) + direction + quality;
}

std::string ComparativeAnalyzer::DetermineVerdict(const std::vector<CategoryComparison>& categories) {
    int totalRegressions = 0;
    int totalImprovements = 0;
    
    for (const auto& cat : categories) {
        if (cat.hasRegressions) {
            totalRegressions += cat.regressionCount;
        }
        // Check for improvements (score > 55 means notable improvement)
        if (cat.overallScore > 55.0) {
            totalImprovements++;
        }
    }
    
    if (totalRegressions >= 3) {
        return "REGRESSED";
    } else if (totalRegressions > 0 && totalImprovements == 0) {
        return "REGRESSED";
    } else if (totalImprovements >= 2 && totalRegressions == 0) {
        return "IMPROVED";
    } else if (totalRegressions == 0 && totalImprovements == 0) {
        return "UNCHANGED";
    } else {
        return "INCONCLUSIVE";
    }
}

std::vector<std::string> ComparativeAnalyzer::GenerateRecommendations(
    const std::vector<CategoryComparison>& categories)
{
    std::vector<std::string> recommendations;
    
    for (const auto& cat : categories) {
        if (cat.hasRegressions) {
            if (cat.category == ComparisonCategory::FPS) {
                recommendations.push_back("FPS regression detected. Review recent changes to render pipeline or shader complexity.");
                recommendations.push_back("Check for increased draw calls or texture uploads.");
            } else if (cat.category == ComparisonCategory::FrameTime) {
                recommendations.push_back("Frame time increased. Profile the main game loop for new bottlenecks.");
                recommendations.push_back("Consider reducing physics complexity or AI update frequency.");
            } else if (cat.category == ComparisonCategory::Memory) {
                recommendations.push_back("Memory usage increased. Check for unreleased resources or asset caching issues.");
                recommendations.push_back("Review recent asset loading changes and texture compression settings.");
            } else if (cat.category == ComparisonCategory::Stability) {
                recommendations.push_back("Frame rate stability decreased. Check for GC pauses or background thread contention.");
                recommendations.push_back("Review recent threading changes and lock contention patterns.");
            }
        }
    }
    
    if (recommendations.empty()) {
        recommendations.push_back("Performance is stable or improved. Continue monitoring.");
    }
    
    return recommendations;
}

bool ComparativeAnalyzer::IsRegression(
    double percentChange,
    bool lowerIsBetter,
    double warningThreshold,
    double criticalThreshold)
{
    // For "lower is better" metrics, positive change is regression
    // For "higher is better" metrics, negative change is regression
    double regressionChange = lowerIsBetter ? percentChange : -percentChange;
    
    return regressionChange > warningThreshold;
}

double ComparativeAnalyzer::CalculateRegressionSeverity(
    double percentChange,
    bool lowerIsBetter,
    double warningThreshold,
    double criticalThreshold)
{
    double regressionChange = lowerIsBetter ? percentChange : -percentChange;
    
    if (regressionChange <= warningThreshold) {
        return 0.0;
    } else if (regressionChange >= criticalThreshold) {
        return 1.0;
    } else {
        // Linear interpolation between warning and critical
        return (regressionChange - warningThreshold) / (criticalThreshold - warningThreshold);
    }
}

double ComparativeAnalyzer::CalculateMean(const std::vector<double>& values) const {
    if (values.empty()) return 0.0;
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / values.size();
}

double ComparativeAnalyzer::CalculateVariance(const std::vector<double>& values, double mean) const {
    if (values.size() < 2) return 0.0;
    double sum = 0.0;
    for (double v : values) {
        double diff = v - mean;
        sum += diff * diff;
    }
    return sum / (values.size() - 1);  // Sample variance
}

double ComparativeAnalyzer::CalculateStdDev(const std::vector<double>& values) const {
    return std::sqrt(CalculateVariance(values, CalculateMean(values)));
}

} // namespace ProfilerCore
