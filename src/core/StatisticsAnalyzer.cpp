#include "StatisticsAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <limits>

namespace ProfilerCore {

StatisticsAnalyzer::StatisticsAnalyzer()
    : m_windowSize(300)
    , m_dirty(false)
    , m_totalSamples(0)
{
    Reset();
}

StatisticsAnalyzer::~StatisticsAnalyzer() {
}

void StatisticsAnalyzer::SetWindowSize(size_t windowSize) {
    m_windowSize = windowSize;
    m_dirty = true;
}

void StatisticsAnalyzer::SetThresholds(const AlertThresholds& thresholds) {
    m_thresholds = thresholds;
}

void StatisticsAnalyzer::Reset() {
    m_frameTimes.clear();
    m_fpsHistory.clear();
    m_memoryHistory.clear();
    m_alerts.clear();
    m_totalSamples = 0;
    m_dirty = true;
    
    m_currentStats = SummaryStats();
    m_currentStats.minFrameTime = 0;
    m_currentStats.maxFrameTime = 0;
    m_currentStats.avgFrameTime = 0;
    m_currentStats.stdDevFrameTime = 0;
    m_currentStats.medianFrameTime = 0;
    m_currentStats.minFps = 0;
    m_currentStats.maxFps = 0;
    m_currentStats.avgFps = 0;
    m_currentStats.p50Fps = 0;
    m_currentStats.p90Fps = 0;
    m_currentStats.p95Fps = 0;
    m_currentStats.p99Fps = 0;
    m_currentStats.peakMemoryUsage = 0;
    m_currentStats.avgMemoryUsage = 0;
    m_currentStats.memoryGrowthRate = 0;
    m_currentStats.stabilityScore = 100.0;
    m_currentStats.alertCount = 0;
    m_currentStats.sampleCount = 0;
    m_currentStats.timestamp = 0;
}

void StatisticsAnalyzer::RecordFrame(double fps, double frameTimeMs, size_t memoryUsage) {
    m_frameTimes.push_back(frameTimeMs);
    m_fpsHistory.push_back(fps);
    m_memoryHistory.push_back(memoryUsage);
    m_totalSamples++;
    
    // Trim to window size
    if (m_frameTimes.size() > m_windowSize) {
        m_frameTimes.erase(m_frameTimes.begin());
        m_fpsHistory.erase(m_fpsHistory.begin());
        m_memoryHistory.erase(m_memoryHistory.begin());
    }
    
    m_dirty = true;
}

SummaryStats StatisticsAnalyzer::GetSummary() const {
    if (m_dirty) {
        const_cast<StatisticsAnalyzer*>(this)->ComputeStatistics();
    }
    return m_currentStats;
}

void StatisticsAnalyzer::ComputeStatistics() {
    if (m_frameTimes.empty()) {
        m_dirty = false;
        return;
    }
    
    size_t n = m_frameTimes.size();
    m_currentStats.sampleCount = static_cast<int>(n);
    m_currentStats.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    // --- Frame time stats ---
    double sum = 0.0;
    double minFt = std::numeric_limits<double>::max();
    double maxFt = std::numeric_limits<double>::lowest();
    
    for (size_t i = 0; i < n; i++) {
        double ft = m_frameTimes[i];
        double fps = m_fpsHistory[i];
        sum += ft;
        if (ft < minFt) minFt = ft;
        if (ft > maxFt) maxFt = ft;
    }
    
    m_currentStats.minFrameTime = minFt;
    m_currentStats.maxFrameTime = maxFt;
    m_currentStats.avgFrameTime = sum / static_cast<double>(n);
    
    // Standard deviation
    double mean = m_currentStats.avgFrameTime;
    double sqDiffSum = 0.0;
    for (size_t i = 0; i < n; i++) {
        double diff = m_frameTimes[i] - mean;
        sqDiffSum += diff * diff;
    }
    m_currentStats.stdDevFrameTime = std::sqrt(sqDiffSum / static_cast<double>(n));
    
    // Median
    std::vector<double> sortedFt = m_frameTimes;
    std::sort(sortedFt.begin(), sortedFt.end());
    size_t mid = n / 2;
    if (n % 2 == 0) {
        m_currentStats.medianFrameTime = (sortedFt[mid - 1] + sortedFt[mid]) / 2.0;
    } else {
        m_currentStats.medianFrameTime = sortedFt[mid];
    }
    
    // --- FPS stats ---
    m_currentStats.minFps = *std::min_element(m_fpsHistory.begin(), m_fpsHistory.end());
    m_currentStats.maxFps = *std::max_element(m_fpsHistory.begin(), m_fpsHistory.end());
    
    double fpsSum = 0.0;
    for (double fps : m_fpsHistory) {
        fpsSum += fps;
    }
    m_currentStats.avgFps = fpsSum / static_cast<double>(n);
    
    // Percentiles
    ComputePercentiles();
    
    // --- Memory stats ---
    size_t maxMem = 0;
    size_t memSum = 0;
    for (size_t mem : m_memoryHistory) {
        if (mem > maxMem) maxMem = mem;
        memSum += mem;
    }
    m_currentStats.peakMemoryUsage = maxMem;
    m_currentStats.avgMemoryUsage = memSum / n;
    
    // Memory growth rate: linear regression slope
    if (n > 1) {
        double xMean = static_cast<double>(n - 1) / 2.0;
        double yMean = static_cast<double>(memSum) / static_cast<double>(n);
        double numerator = 0.0;
        double denominator = 0.0;
        for (size_t i = 0; i < n; i++) {
            double xDiff = static_cast<double>(i) - xMean;
            double yDiff = static_cast<double>(m_memoryHistory[i]) - yMean;
            numerator += xDiff * yDiff;
            denominator += xDiff * xDiff;
        }
        m_currentStats.memoryGrowthRate = (denominator > 0) ? (numerator / denominator) : 0.0;
    }
    
    // --- Stability score ---
    m_currentStats.stabilityScore = ComputeStabilityScore();
    
    // --- Alerts ---
    GenerateAlerts();
    m_currentStats.alertCount = static_cast<int>(m_alerts.size());
    
    m_dirty = false;
}

void StatisticsAnalyzer::ComputePercentiles() {
    std::vector<double> sortedFps = m_fpsHistory;
    std::sort(sortedFps.begin(), sortedFps.end());
    size_t n = sortedFps.size();
    
    auto getPercentile = [&](double p) -> double {
        if (n == 0) return 0.0;
        double idx = (p / 100.0) * (static_cast<double>(n) - 1.0);
        size_t lower = static_cast<size_t>(std::floor(idx));
        size_t upper = static_cast<size_t>(std::ceil(idx));
        if (lower == upper || upper >= n) return sortedFps[lower];
        double fraction = idx - static_cast<double>(lower);
        return sortedFps[lower] * (1.0 - fraction) + sortedFps[upper] * fraction;
    };
    
    m_currentStats.p50Fps = getPercentile(50.0);
    m_currentStats.p90Fps = getPercentile(90.0);
    m_currentStats.p95Fps = getPercentile(95.0);
    m_currentStats.p99Fps = getPercentile(99.0);
}

double StatisticsAnalyzer::ComputeStabilityScore() const {
    if (m_fpsHistory.size() < 2) return 100.0;
    
    // Coefficient of variation of FPS (lower = more stable)
    double mean = m_currentStats.avgFps;
    if (mean <= 0) return 0.0;
    
    double variance = 0.0;
    for (double fps : m_fpsHistory) {
        double diff = fps - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(m_fpsHistory.size());
    double stdDev = std::sqrt(variance);
    double cv = stdDev / mean; // coefficient of variation
    
    // Map CV to score: CV=0 -> 100, CV>=0.5 -> 0
    double score = 100.0 * (1.0 - std::min(cv * 2.0, 1.0));
    return std::max(0.0, std::min(100.0, score));
}

void StatisticsAnalyzer::GenerateAlerts() {
    m_alerts.clear();
    
    // FPS alerts
    if (m_currentStats.avgFps < m_thresholds.minFpsCritical) {
        m_alerts.push_back({
            AlertSeverity::Critical,
            "Critical: Average FPS below acceptable threshold",
            m_currentStats.timestamp,
            "avgFps",
            m_currentStats.avgFps,
            m_thresholds.minFpsCritical
        });
    } else if (m_currentStats.avgFps < m_thresholds.minFpsWarning) {
        m_alerts.push_back({
            AlertSeverity::Warning,
            "Warning: Average FPS below recommended threshold",
            m_currentStats.timestamp,
            "avgFps",
            m_currentStats.avgFps,
            m_thresholds.minFpsWarning
        });
    }
    
    // P99 latency alert
    double p99FrameTime = 1000.0 / m_currentStats.p99Fps; // ms
    if (p99FrameTime > m_thresholds.maxFrameTimeCritical / 1000.0) {
        m_alerts.push_back({
            AlertSeverity::Critical,
            "Critical: P99 frame time exceeds critical threshold",
            m_currentStats.timestamp,
            "p99FrameTime",
            p99FrameTime,
            m_thresholds.maxFrameTimeCritical / 1000.0
        });
    } else if (p99FrameTime > m_thresholds.maxFrameTimeWarning / 1000.0) {
        m_alerts.push_back({
            AlertSeverity::Warning,
            "Warning: P99 frame time exceeds recommended threshold",
            m_currentStats.timestamp,
            "p99FrameTime",
            p99FrameTime,
            m_thresholds.maxFrameTimeWarning / 1000.0
        });
    }
    
    // Memory growth rate (memory leak detection)
    double growthPerFrame = m_currentStats.memoryGrowthRate;
    if (growthPerFrame > m_thresholds.memoryGrowthRateCritical) {
        m_alerts.push_back({
            AlertSeverity::Critical,
            "Critical: High memory growth rate detected - possible memory leak",
            m_currentStats.timestamp,
            "memoryGrowthRate",
            growthPerFrame,
            m_thresholds.memoryGrowthRateCritical
        });
    } else if (growthPerFrame > m_thresholds.memoryGrowthRateWarning) {
        m_alerts.push_back({
            AlertSeverity::Warning,
            "Warning: Elevated memory growth rate detected",
            m_currentStats.timestamp,
            "memoryGrowthRate",
            growthPerFrame,
            m_thresholds.memoryGrowthRateWarning
        });
    }
    
    // Peak memory alert
    if (m_currentStats.peakMemoryUsage > m_thresholds.peakMemoryCritical) {
        m_alerts.push_back({
            AlertSeverity::Critical,
            "Critical: Peak memory usage exceeds system limit",
            m_currentStats.timestamp,
            "peakMemoryUsage",
            static_cast<double>(m_currentStats.peakMemoryUsage),
            static_cast<double>(m_thresholds.peakMemoryCritical)
        });
    } else if (m_currentStats.peakMemoryUsage > m_thresholds.peakMemoryWarning) {
        m_alerts.push_back({
            AlertSeverity::Warning,
            "Warning: Peak memory usage is high",
            m_currentStats.timestamp,
            "peakMemoryUsage",
            static_cast<double>(m_currentStats.peakMemoryUsage),
            static_cast<double>(m_thresholds.peakMemoryWarning)
        });
    }
    
    // Stability score alert
    if (m_currentStats.stabilityScore < 50.0) {
        m_alerts.push_back({
            AlertSeverity::Warning,
            "Warning: Frame rate stability is poor - possible GC or threading issues",
            m_currentStats.timestamp,
            "stabilityScore",
            m_currentStats.stabilityScore,
            50.0
        });
    }
}

void StatisticsAnalyzer::ClearAlerts() {
    m_alerts.clear();
    m_currentStats.alertCount = 0;
}

size_t StatisticsAnalyzer::GetAlertCountBySeverity(AlertSeverity severity) const {
    size_t count = 0;
    for (const auto& alert : m_alerts) {
        if (alert.severity == severity) count++;
    }
    return count;
}

std::string StatisticsAnalyzer::ExportToJSON() const {
    std::ostringstream ss;
    ss << "{\"summary\":{";
    ss << "\"timestamp\":" << m_currentStats.timestamp << ",";
    ss << "\"sampleCount\":" << m_currentStats.sampleCount << ",";
    ss << "\"frameTime\":{";
    ss << "\"min\":" << std::fixed << std::setprecision(3) << m_currentStats.minFrameTime << ",";
    ss << "\"max\":" << m_currentStats.maxFrameTime << ",";
    ss << "\"avg\":" << m_currentStats.avgFrameTime << ",";
    ss << "\"stdDev\":" << m_currentStats.stdDevFrameTime << ",";
    ss << "\"median\":" << m_currentStats.medianFrameTime << "},";
    ss << "\"fps\":{";
    ss << "\"min\":" << m_currentStats.minFps << ",";
    ss << "\"max\":" << m_currentStats.maxFps << ",";
    ss << "\"avg\":" << m_currentStats.avgFps << ",";
    ss << "\"p50\":" << m_currentStats.p50Fps << ",";
    ss << "\"p90\":" << m_currentStats.p90Fps << ",";
    ss << "\"p95\":" << m_currentStats.p95Fps << ",";
    ss << "\"p99\":" << m_currentStats.p99Fps << "},";
    ss << "\"memory\":{";
    ss << "\"peak\":" << m_currentStats.peakMemoryUsage << ",";
    ss << "\"avg\":" << m_currentStats.avgMemoryUsage << ",";
    ss << "\"growthRate\":" << m_currentStats.memoryGrowthRate << "},";
    ss << "\"stabilityScore\":" << m_currentStats.stabilityScore << ",";
    ss << "\"alertCount\":" << m_currentStats.alertCount << "},";
    
    // Alerts array
    ss << "\"alerts\":[";
    for (size_t i = 0; i < m_alerts.size(); i++) {
        const auto& a = m_alerts[i];
        ss << "{\"severity\":" << static_cast<int>(a.severity);
        ss << ",\"message\":\"" << a.message << "\"";
        ss << ",\"metric\":\"" << a.metric << "\"";
        ss << ",\"value\":" << a.value;
        ss << ",\"threshold\":" << a.threshold << "}";
        if (i < m_alerts.size() - 1) ss << ",";
    }
    ss << "]}";
    
    return ss.str();
}

} // namespace ProfilerCore
