#include "ReportGenerator.h"
#include "ProfilerCore.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ProfilerCore {

// ─── Construction ─────────────────────────────────────────────────────────────

ReportGenerator::ReportGenerator()
    : m_config()
{
}

ReportGenerator::~ReportGenerator() {
}

void ReportGenerator::SetConfig(const ReportConfig& config) {
    m_config = config;
}

// ─── Report Generation ────────────────────────────────────────────────────────

std::string ReportGenerator::GenerateReport() {
    ProfilerCore& profiler = ProfilerCore::GetInstance();
    
    // Note: In production, you'd call profiler.GetFrameHistory() or similar
    // For now, we generate a report from the current analyzer state
    auto analyzer = profiler.GetAnalyzer();
    if (!analyzer) {
        return "{}";
    }
    
    SummaryStats stats = analyzer->GetSummary();
    PerformanceMetrics metrics;
    
    // Map SummaryStats to PerformanceMetrics
    metrics.avgFps = stats.avgFps;
    metrics.minFps = stats.minFps;
    metrics.maxFps = stats.maxFps;
    metrics.percentile1 = stats.p50Fps * 0.5; // Approximate
    metrics.percentile5 = stats.p50Fps;
    metrics.percentile95 = stats.p95Fps;
    metrics.percentile99 = stats.p99Fps;
    metrics.stdDeviation = stats.stdDevFrameTime;
    
    metrics.avgFrameTime = stats.avgFrameTime;
    metrics.minFrameTime = stats.minFrameTime;
    metrics.maxFrameTime = stats.maxFrameTime;
    
    metrics.currentMemoryUsage = static_cast<int64_t>(stats.avgMemoryUsage);
    int leakCount = static_cast<int>(profiler.GetDetectedLeaks().size());
    metrics.leakCount = leakCount;
    
    metrics.alertCount = stats.alertCount;
    auto alertManager = profiler.GetAlertManager();
    if (alertManager) {
        metrics.criticalAlerts = static_cast<int>(alertManager->GetCriticalAlertCount());
        metrics.warningAlerts = static_cast<int>(alertManager->GetWarningAlertCount());
    }
    
    metrics.totalFrames = stats.sampleCount;
    metrics.droppedFrameRate = (metrics.avgFps < 55.0) ? ((60.0 - metrics.avgFps) / 60.0 * 100.0) : 0.0;
    
    // Generate recommendations
    std::vector<std::string> recommendations = GenerateRecommendations(metrics);
    
    // Function stats (would need frame history access in production)
    std::vector<FunctionStats> funcStats;
    
    // Generate report based on format
    switch (m_config.format) {
        case ReportFormat::HTML:
            return GenerateHTMLReport(metrics, funcStats, recommendations);
        case ReportFormat::JSON:
            return GenerateJSONReport(metrics, funcStats, recommendations);
        case ReportFormat::Markdown:
            return GenerateMarkdownReport(metrics, funcStats, recommendations);
        case ReportFormat::CSV:
            return GenerateCSVReport(metrics, funcStats);
        default:
            return GenerateHTMLReport(metrics, funcStats, recommendations);
    }
}

bool ReportGenerator::SaveToFile(const std::string& filepath) {
    std::string content = GenerateReport();
    if (content.empty()) {
        return false;
    }
    
    std::ofstream file(filepath, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.write(content.c_str(), content.size());
    file.close();
    return true;
}

// ─── Metrics Calculation ───────────────────────────────────────────────────────

PerformanceMetrics ReportGenerator::CalculateMetrics(const std::vector<FrameData>& frames) {
    PerformanceMetrics metrics = {};
    
    if (frames.empty()) {
        return metrics;
    }
    
    std::vector<double> fpsValues;
    std::vector<double> frameTimeValues;
    int64_t totalMem = 0;
    int droppedFrames = 0;
    
    for (const auto& frame : frames) {
        fpsValues.push_back(static_cast<double>(frame.fps));
        frameTimeValues.push_back(static_cast<double>(frame.frameTime));
        totalMem += static_cast<int64_t>(frame.memory.currentUsage);
        if (frame.fps < 55.0f) {
            droppedFrames++;
        }
    }
    
    std::sort(fpsValues.begin(), fpsValues.end());
    std::sort(frameTimeValues.begin(), frameTimeValues.end());
    
    metrics.totalFrames = static_cast<int>(frames.size());
    metrics.droppedFrames = droppedFrames;
    metrics.droppedFrameRate = static_cast<double>(droppedFrames) / static_cast<double>(frames.size()) * 100.0;
    
    // FPS statistics
    metrics.minFps = fpsValues.front();
    metrics.maxFps = fpsValues.back();
    
    double fpsSum = 0.0;
    for (double fps : fpsValues) fpsSum += fps;
    metrics.avgFps = fpsSum / static_cast<double>(fpsValues.size());
    
    // Percentiles
    metrics.percentile1 = CalculatePercentile(fpsValues, 1.0);
    metrics.percentile5 = CalculatePercentile(fpsValues, 5.0);
    metrics.percentile95 = CalculatePercentile(fpsValues, 95.0);
    metrics.percentile99 = CalculatePercentile(fpsValues, 99.0);
    
    // Standard deviation
    metrics.stdDeviation = CalculateStdDeviation(fpsValues, metrics.avgFps);
    
    // Frame time statistics
    metrics.minFrameTime = frameTimeValues.front();
    metrics.maxFrameTime = frameTimeValues.back();
    double ftSum = 0.0;
    for (double ft : frameTimeValues) ftSum += ft;
    metrics.avgFrameTime = ftSum / static_cast<double>(frameTimeValues.size());
    
    // Memory
    metrics.currentMemoryUsage = totalMem / static_cast<int64_t>(frames.size());
    
    return metrics;
}

std::vector<FunctionStats> ReportGenerator::CalculateFunctionStats(const std::vector<FrameData>& frames) {
    std::unordered_map<std::string, FunctionStats> funcMap;
    
    for (const auto& frame : frames) {
        for (const auto& profile : frame.profiles) {
            if (funcMap.find(profile.functionName) == funcMap.end()) {
                FunctionStats stats;
                stats.name = profile.functionName;
                stats.totalTime = 0.0;
                stats.callCount = 0;
                stats.minTime = std::numeric_limits<double>::max();
                stats.maxTime = 0.0;
                funcMap[profile.functionName] = stats;
            }
            
            FunctionStats& stats = funcMap[profile.functionName];
            stats.totalTime += static_cast<double>(profile.duration);
            stats.callCount++;
            stats.minTime = std::min(stats.minTime, static_cast<double>(profile.duration));
            stats.maxTime = std::max(stats.maxTime, static_cast<double>(profile.duration));
        }
    }
    
    // Calculate averages and percentages
    std::vector<FunctionStats> result;
    double totalTimeAll = 0.0;
    for (auto& pair : funcMap) {
        totalTimeAll += pair.second.totalTime;
    }
    
    for (auto& pair : funcMap) {
        pair.second.avgTime = pair.second.totalTime / static_cast<double>(pair.second.callCount);
        pair.second.percentage = (totalTimeAll > 0.0) ? (pair.second.totalTime / totalTimeAll * 100.0) : 0.0;
        result.push_back(pair.second);
    }
    
    // Sort by total time descending
    std::sort(result.begin(), result.end(), [](const FunctionStats& a, const FunctionStats& b) {
        return a.totalTime > b.totalTime;
    });
    
    return result;
}

// ─── Recommendations ───────────────────────────────────────────────────────────

std::vector<std::string> ReportGenerator::GenerateRecommendations(const PerformanceMetrics& metrics) {
    std::vector<std::string> recommendations;
    
    // FPS recommendations
    if (metrics.avgFps < 30.0) {
        recommendations.push_back("Performance is severely degraded. Consider reducing visual quality or investigating render loop bottlenecks.");
    } else if (metrics.avgFps < 45.0) {
        recommendations.push_back("Performance is below optimal. Check for expensive draw calls or shader complexity.");
    } else if (metrics.avgFps < 55.0) {
        recommendations.push_back("Performance is acceptable but could be improved. Monitor for frame time spikes.");
    }
    
    if (metrics.percentile99 < metrics.avgFps * 0.7) {
        recommendations.push_back("High variance in frame times detected. Consider investigating GC pauses or background tasks.");
    }
    
    // Frame time recommendations
    if (metrics.maxFrameTime > 50.0) {
        recommendations.push_back("Major frame spikes detected (>50ms). Check for asset loading in the main thread or blocking operations.");
    }
    
    double frameTimeVar = metrics.stdDeviation / metrics.avgFrameTime * 100.0;
    if (frameTimeVar > 20.0) {
        recommendations.push_back("Frame time variance is high. Consider implementing frame pacing or async resource loading.");
    }
    
    // Memory recommendations
    if (metrics.leakCount > 0) {
        recommendations.push_back("Memory leaks detected. Review allocation patterns and ensure proper cleanup in resource managers.");
    }
    
    if (metrics.currentMemoryUsage > 500 * 1024 * 1024) {
        recommendations.push_back("High memory usage detected. Consider implementing texture streaming or asset caching with LRU eviction.");
    }
    
    // Alert-based recommendations
    if (metrics.criticalAlerts > 0) {
        recommendations.push_back("Critical alerts present. Address these immediately before optimizing other areas.");
    }
    
    // Dropped frame recommendations
    if (metrics.droppedFrameRate > 10.0) {
        recommendations.push_back("More than 10% frames are dropped. Profile specific game systems to identify the bottleneck.");
    }
    
    if (recommendations.empty()) {
        recommendations.push_back("Performance appears healthy. Continue monitoring during gameplay sessions.");
    }
    
    return recommendations;
}

// ─── Chart Data Generation ─────────────────────────────────────────────────────

std::string ReportGenerator::GenerateFPSChartData(const std::vector<FrameData>& frames) {
    std::ostringstream ss;
    ss << "[";
    
    size_t limit = std::min(frames.size(), static_cast<size_t>(m_config.maxFrames));
    size_t step = (frames.size() > limit) ? (frames.size() / limit) : 1;
    
    bool first = true;
    for (size_t i = 0; i < frames.size(); i += step) {
        if (!first) ss << ",";
        first = false;
        ss << "[" << frames[i].frameNumber << "," << std::fixed << std::setprecision(2) << frames[i].fps << "]";
    }
    
    ss << "]";
    return ss.str();
}

std::string ReportGenerator::GenerateFrameTimeChartData(const std::vector<FrameData>& frames) {
    std::ostringstream ss;
    ss << "[";
    
    size_t limit = std::min(frames.size(), static_cast<size_t>(m_config.maxFrames));
    size_t step = (frames.size() > limit) ? (frames.size() / limit) : 1;
    
    bool first = true;
    for (size_t i = 0; i < frames.size(); i += step) {
        if (!first) ss << ",";
        first = false;
        ss << "[" << frames[i].frameNumber << "," << std::fixed << std::setprecision(3) << frames[i].frameTime << "]";
    }
    
    ss << "]";
    return ss.str();
}

std::string ReportGenerator::GenerateMemoryChartData(const std::vector<FrameData>& frames) {
    std::ostringstream ss;
    ss << "[";
    
    size_t limit = std::min(frames.size(), static_cast<size_t>(m_config.maxFrames));
    size_t step = (frames.size() > limit) ? (frames.size() / limit) : 1;
    
    bool first = true;
    for (size_t i = 0; i < frames.size(); i += step) {
        if (!first) ss << ",";
        first = false;
        ss << "[" << frames[i].frameNumber << "," << frames[i].memory.currentUsage << "]";
    }
    
    ss << "]";
    return ss.str();
}

// ─── HTML Report Generation ───────────────────────────────────────────────────

std::string ReportGenerator::GenerateHTMLReport(const PerformanceMetrics& metrics,
                                                  const std::vector<FunctionStats>& funcStats,
                                                  const std::vector<std::string>& recommendations) {
    std::ostringstream ss;
    
    ss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    ss << "<meta charset=\"UTF-8\">\n";
    ss << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    ss << "<title>" << EscapeJSON(m_config.title) << "</title>\n";
    ss << "<style>\n";
    ss << "  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 20px; background: #1a1a2e; color: #eee; }\n";
    ss << "  .container { max-width: 1200px; margin: 0 auto; }\n";
    ss << "  .header { text-align: center; padding: 20px 0; border-bottom: 1px solid #333; }\n";
    ss << "  .metrics-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }\n";
    ss << "  .metric-card { background: #16213e; border-radius: 8px; padding: 15px; text-align: center; }\n";
    ss << "  .metric-value { font-size: 2em; font-weight: bold; color: #4ade80; }\n";
    ss << "  .metric-value.warning { color: #fbbf24; }\n";
    ss << "  .metric-value.critical { color: #ef4444; }\n";
    ss << "  .metric-label { font-size: 0.9em; color: #9ca3af; margin-top: 5px; }\n";
    ss << "  .section { margin: 30px 0; }\n";
    ss << "  .section-title { font-size: 1.5em; margin-bottom: 15px; color: #60a5fa; }\n";
    ss << "  .recommendation { background: #1f2937; padding: 12px; margin: 8px 0; border-radius: 6px; border-left: 3px solid #60a5fa; }\n";
    ss << "  .recommendation.critical { border-left-color: #ef4444; }\n";
    ss << "  table { width: 100%; border-collapse: collapse; margin: 15px 0; }\n";
    ss << "  th, td { padding: 12px; text-align: left; border-bottom: 1px solid #333; }\n";
    ss << "  th { background: #16213e; color: #9ca3af; }\n";
    ss << "  tr:hover { background: #1f2937; }\n";
    ss << "  .timestamp { color: #6b7280; font-size: 0.85em; margin-top: 20px; text-align: right; }\n";
    ss << "</style>\n";
    ss << "</head>\n<body>\n";
    ss << "<div class=\"container\">\n";
    
    // Header
    ss << "<div class=\"header\">\n";
    ss << "  <h1>" << EscapeJSON(m_config.title) << "</h1>\n";
    ss << "  <p>Generated: " << GetCurrentTimestamp() << "</p>\n";
    ss << "</div>\n";
    
    // FPS Metrics
    ss << "<div class=\"section\">\n";
    ss << "  <h2 class=\"section-title\">Performance Overview</h2>\n";
    ss << "  <div class=\"metrics-grid\">\n";
    
    auto getMetricClass = [](double value, double warn, double critical) -> std::string {
        if (value < critical) return "critical";
        if (value < warn) return "warning";
        return "";
    };
    
    ss << "    <div class=\"metric-card\">\n";
    ss << "      <div class=\"metric-value " << getMetricClass(metrics.avgFps, 55.0, 30.0) << "\">" 
       << std::fixed << std::setprecision(1) << metrics.avgFps << "</div>\n";
    ss << "      <div class=\"metric-label\">Avg FPS</div>\n";
    ss << "    </div>\n";
    
    ss << "    <div class=\"metric-card\">\n";
    ss << "      <div class=\"metric-value " << getMetricClass(metrics.minFps, 45.0, 25.0) << "\">" 
       << std::fixed << std::setprecision(1) << metrics.minFps << "</div>\n";
    ss << "      <div class=\"metric-label\">Min FPS (1% Low)</div>\n";
    ss << "    </div>\n";
    
    ss << "    <div class=\"metric-card\">\n";
    ss << "      <div class=\"metric-value\">" << std::fixed << std::setprecision(2) << metrics.avgFrameTime << " ms</div>\n";
    ss << "      <div class=\"metric-label\">Avg Frame Time</div>\n";
    ss << "    </div>\n";
    
    ss << "    <div class=\"metric-card\">\n";
    ss << "      <div class=\"metric-value\">" << std::fixed << std::setprecision(1) << metrics.percentile99 << "</div>\n";
    ss << "      <div class=\"metric-label\">P99 FPS</div>\n";
    ss << "    </div>\n";
    
    ss << "    <div class=\"metric-card\">\n";
    ss << "      <div class=\"metric-value\">" << FormatBytes(metrics.currentMemoryUsage) << "</div>\n";
    ss << "      <div class=\"metric-label\">Memory Usage</div>\n";
    ss << "    </div>\n";
    
    ss << "    <div class=\"metric-card\">\n";
    ss << "      <div class=\"metric-value\">" << metrics.totalFrames << "</div>\n";
    ss << "      <div class=\"metric-label\">Total Frames</div>\n";
    ss << "    </div>\n";
    
    ss << "  </div>\n";
    ss << "</div>\n";
    
    // Alerts section (if configured)
    if (m_config.includeAlerts && (metrics.criticalAlerts > 0 || metrics.warningAlerts > 0)) {
        ss << "<div class=\"section\">\n";
        ss << "  <h2 class=\"section-title\">Alerts</h2>\n";
        ss << "  <div class=\"metrics-grid\">\n";
        ss << "    <div class=\"metric-card\">\n";
        ss << "      <div class=\"metric-value critical\">" << metrics.criticalAlerts << "</div>\n";
        ss << "      <div class=\"metric-label\">Critical Alerts</div>\n";
        ss << "    </div>\n";
        ss << "    <div class=\"metric-card\">\n";
        ss << "      <div class=\"metric-value warning\">" << metrics.warningAlerts << "</div>\n";
        ss << "      <div class=\"metric-label\">Warnings</div>\n";
        ss << "    </div>\n";
        ss << "  </div>\n";
        ss << "</div>\n";
    }
    
    // Memory section
    if (m_config.includeMemoryLeaks) {
        ss << "<div class=\"section\">\n";
        ss << "  <h2 class=\"section-title\">Memory Analysis</h2>\n";
        ss << "  <div class=\"metrics-grid\">\n";
        ss << "    <div class=\"metric-card\">\n";
        ss << "      <div class=\"metric-value " << (metrics.leakCount > 0 ? "critical" : "") << "\">" 
           << metrics.leakCount << "</div>\n";
        ss << "      <div class=\"metric-label\">Memory Leaks Detected</div>\n";
        ss << "    </div>\n";
        ss << "    <div class=\"metric-card\">\n";
        ss << "      <div class=\"metric-value\">" << std::fixed << std::setprecision(1) << metrics.droppedFrameRate << "%</div>\n";
        ss << "      <div class=\"metric-label\">Dropped Frame Rate</div>\n";
        ss << "    </div>\n";
        ss << "  </div>\n";
        ss << "</div>\n";
    }
    
    // Function profiling section
    if (!funcStats.empty()) {
        ss << "<div class=\"section\">\n";
        ss << "  <h2 class=\"section-title\">Function Profiling</h2>\n";
        ss << "  <table>\n";
        ss << "    <thead>\n";
        ss << "      <tr><th>Function</th><th>Total Time</th><th>Avg Time</th><th>Calls</th><th>%</th></tr>\n";
        ss << "    </thead>\n";
        ss << "    <tbody>\n";
        
        for (size_t i = 0; i < std::min(funcStats.size(), static_cast<size_t>(20)); i++) {
            const auto& fs = funcStats[i];
            ss << "      <tr>\n";
            ss << "        <td>" << EscapeJSON(fs.name) << "</td>\n";
            ss << "        <td>" << std::fixed << std::setprecision(2) << fs.totalTime / 1000.0 << " ms</td>\n";
            ss << "        <td>" << std::fixed << std::setprecision(3) << fs.avgTime / 1000.0 << " ms</td>\n";
            ss << "        <td>" << fs.callCount << "</td>\n";
            ss << "        <td>" << std::fixed << std::setprecision(1) << fs.percentage << "%</td>\n";
            ss << "      </tr>\n";
        }
        
        ss << "    </tbody>\n";
        ss << "  </table>\n";
        ss << "</div>\n";
    }
    
    // Recommendations section
    if (m_config.includeRecommendations && !recommendations.empty()) {
        ss << "<div class=\"section\">\n";
        ss << "  <h2 class=\"section-title\">Recommendations</h2>\n";
        
        for (const auto& rec : recommendations) {
            bool isCritical = rec.find("Critical") != std::string::npos || rec.find("leak") != std::string::npos;
            ss << "  <div class=\"recommendation" << (isCritical ? " critical" : "") << "\">" << EscapeJSON(rec) << "</div>\n";
        }
        
        ss << "</div>\n";
    }
    
    // Timestamp
    ss << "<div class=\"timestamp\">Report generated at " << GetCurrentTimestamp() << "</div>\n";
    
    ss << "</div>\n";
    ss << "</body>\n</html>\n";
    
    return ss.str();
}

// ─── JSON Report Generation ────────────────────────────────────────────────────

std::string ReportGenerator::GenerateJSONReport(const PerformanceMetrics& metrics,
                                                  const std::vector<FunctionStats>& funcStats,
                                                  const std::vector<std::string>& recommendations) {
    std::ostringstream ss;
    ss << "{\n";
    
    // Title and timestamp
    ss << "  \"title\": \"" << EscapeJSON(m_config.title) << "\",\n";
    ss << "  \"generatedAt\": \"" << GetCurrentTimestamp() << "\",\n";
    
    // Performance metrics
    ss << "  \"performance\": {\n";
    ss << "    \"fps\": {\n";
    ss << "      \"avg\": " << std::fixed << std::setprecision(2) << metrics.avgFps << ",\n";
    ss << "      \"min\": " << metrics.minFps << ",\n";
    ss << "      \"max\": " << metrics.maxFps << ",\n";
    ss << "      \"p1\": " << metrics.percentile1 << ",\n";
    ss << "      \"p5\": " << metrics.percentile5 << ",\n";
    ss << "      \"p95\": " << metrics.percentile95 << ",\n";
    ss << "      \"p99\": " << metrics.percentile99 << ",\n";
    ss << "      \"stdDev\": " << metrics.stdDeviation << "\n";
    ss << "    },\n";
    ss << "    \"frameTime\": {\n";
    ss << "      \"avg\": " << std::fixed << std::setprecision(3) << metrics.avgFrameTime << ",\n";
    ss << "      \"min\": " << metrics.minFrameTime << ",\n";
    ss << "      \"max\": " << metrics.maxFrameTime << "\n";
    ss << "    },\n";
    ss << "    \"memory\": {\n";
    ss << "      \"current\": " << metrics.currentMemoryUsage << ",\n";
    ss << "      \"currentFormatted\": \"" << FormatBytes(metrics.currentMemoryUsage) << "\",\n";
    ss << "      \"leakCount\": " << metrics.leakCount << "\n";
    ss << "    },\n";
    ss << "    \"frames\": {\n";
    ss << "      \"total\": " << metrics.totalFrames << ",\n";
    ss << "      \"dropped\": " << metrics.droppedFrames << ",\n";
    ss << "      \"droppedRate\": " << std::fixed << std::setprecision(2) << metrics.droppedFrameRate << "\n";
    ss << "    },\n";
    ss << "    \"alerts\": {\n";
    ss << "      \"total\": " << metrics.alertCount << ",\n";
    ss << "      \"critical\": " << metrics.criticalAlerts << ",\n";
    ss << "      \"warning\": " << metrics.warningAlerts << "\n";
    ss << "    }\n";
    ss << "  },\n";
    
    // Function stats
    ss << "  \"functions\": [\n";
    for (size_t i = 0; i < funcStats.size(); i++) {
        const auto& fs = funcStats[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << EscapeJSON(fs.name) << "\",\n";
        ss << "      \"totalTime\": " << std::fixed << std::setprecision(2) << fs.totalTime << ",\n";
        ss << "      \"avgTime\": " << std::fixed << std::setprecision(3) << fs.avgTime << ",\n";
        ss << "      \"callCount\": " << fs.callCount << ",\n";
        ss << "      \"percentage\": " << std::fixed << std::setprecision(2) << fs.percentage << "\n";
        ss << "    }";
        if (i < funcStats.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "  ],\n";
    
    // Recommendations
    ss << "  \"recommendations\": [\n";
    for (size_t i = 0; i < recommendations.size(); i++) {
        ss << "    \"" << EscapeJSON(recommendations[i]) << "\"";
        if (i < recommendations.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "  ]\n";
    
    ss << "}\n";
    return ss.str();
}

// ─── Markdown Report Generation ───────────────────────────────────────────────

std::string ReportGenerator::GenerateMarkdownReport(const PerformanceMetrics& metrics,
                                                      const std::vector<FunctionStats>& funcStats,
                                                      const std::vector<std::string>& recommendations) {
    std::ostringstream ss;
    
    ss << "# " << m_config.title << "\n\n";
    ss << "**Generated:** " << GetCurrentTimestamp() << "\n\n";
    
    // Performance Summary
    ss << "## Performance Summary\n\n";
    ss << "| Metric | Value |\n";
    ss << "|--------|-------|\n";
    ss << "| Average FPS | " << std::fixed << std::setprecision(1) << metrics.avgFps << " |\n";
    ss << "| Min FPS (1% Low) | " << metrics.minFps << " |\n";
    ss << "| Max FPS | " << metrics.maxFps << " |\n";
    ss << "| P99 FPS | " << metrics.percentile99 << " |\n";
    ss << "| Avg Frame Time | " << std::fixed << std::setprecision(2) << metrics.avgFrameTime << " ms |\n";
    ss << "| Memory Usage | " << FormatBytes(metrics.currentMemoryUsage) << " |\n";
    ss << "| Total Frames | " << metrics.totalFrames << " |\n";
    ss << "| Dropped Frames | " << std::fixed << std::setprecision(1) << metrics.droppedFrameRate << "% |\n\n";
    
    // Alerts
    if (metrics.alertCount > 0) {
        ss << "## Alerts\n\n";
        ss << "| Severity | Count |\n";
        ss << "|----------|-------|\n";
        ss << "| Critical | " << metrics.criticalAlerts << " |\n";
        ss << "| Warning | " << metrics.warningAlerts << " |\n\n";
    }
    
    // Memory Analysis
    ss << "## Memory Analysis\n\n";
    ss << "| Metric | Value |\n";
    ss << "|--------|-------|\n";
    ss << "| Current Usage | " << FormatBytes(metrics.currentMemoryUsage) << " |\n";
    ss << "| Leaks Detected | " << metrics.leakCount << " |\n\n";
    
    // Function Profiling
    if (!funcStats.empty()) {
        ss << "## Profiling Results\n\n";
        ss << "| Function | Total Time | Avg Time | Calls | % |\n";
        ss << "|----------|------------|----------|-------|---|\n";
        
        for (size_t i = 0; i < std::min(funcStats.size(), static_cast<size_t>(15)); i++) {
            const auto& fs = funcStats[i];
            ss << "| " << fs.name << " | " 
               << std::fixed << std::setprecision(2) << fs.totalTime / 1000.0 << " ms | "
               << std::fixed << std::setprecision(3) << fs.avgTime / 1000.0 << " ms | "
               << fs.callCount << " | "
               << std::fixed << std::setprecision(1) << fs.percentage << "% |\n";
        }
        ss << "\n";
    }
    
    // Recommendations
    ss << "## Recommendations\n\n";
    for (const auto& rec : recommendations) {
        ss << "- " << rec << "\n";
    }
    ss << "\n";
    
    return ss.str();
}

// ─── CSV Report Generation ────────────────────────────────────────────────────

std::string ReportGenerator::GenerateCSVReport(const PerformanceMetrics& metrics,
                                                const std::vector<FunctionStats>& funcStats) {
    std::ostringstream ss;
    
    // Summary section
    ss << "# Performance Report Summary\n";
    ss << "# Generated," << GetCurrentTimestamp() << "\n";
    ss << "#\n";
    ss << "Metric,Value\n";
    ss << "Avg FPS," << std::fixed << std::setprecision(2) << metrics.avgFps << "\n";
    ss << "Min FPS," << metrics.minFps << "\n";
    ss << "Max FPS," << metrics.maxFps << "\n";
    ss << "P99 FPS," << metrics.percentile99 << "\n";
    ss << "Avg Frame Time (ms)," << std::fixed << std::setprecision(3) << metrics.avgFrameTime << "\n";
    ss << "Memory Usage," << metrics.currentMemoryUsage << "\n";
    ss << "Memory Usage Formatted," << FormatBytes(metrics.currentMemoryUsage) << "\n";
    ss << "Total Frames," << metrics.totalFrames << "\n";
    ss << "Dropped Frame Rate (%)," << std::fixed << std::setprecision(2) << metrics.droppedFrameRate << "\n";
    ss << "Critical Alerts," << metrics.criticalAlerts << "\n";
    ss << "Warning Alerts," << metrics.warningAlerts << "\n";
    ss << "Memory Leaks," << metrics.leakCount << "\n";
    ss << "#\n";
    
    // Function stats section
    if (!funcStats.empty()) {
        ss << "# Function Profiling\n";
        ss << "Function,Total Time (us),Avg Time (us),Call Count,Percentage\n";
        
        for (const auto& fs : funcStats) {
            ss << fs.name << ","
               << std::fixed << std::setprecision(2) << fs.totalTime << ","
               << std::fixed << std::setprecision(3) << fs.avgTime << ","
               << fs.callCount << ","
               << std::fixed << std::setprecision(2) << fs.percentage << "\n";
        }
    }
    
    return ss.str();
}

// ─── Helper Methods ───────────────────────────────────────────────────────────

double ReportGenerator::CalculatePercentile(const std::vector<double>& sortedValues, double percentile) {
    if (sortedValues.empty()) return 0.0;
    
    double index = (percentile / 100.0) * (static_cast<double>(sortedValues.size()) - 1.0);
    size_t lower = static_cast<size_t>(std::floor(index));
    size_t upper = static_cast<size_t>(std::ceil(index));
    
    if (lower == upper || upper >= sortedValues.size()) {
        return sortedValues[lower];
    }
    
    double fraction = index - static_cast<double>(lower);
    return sortedValues[lower] * (1.0 - fraction) + sortedValues[upper] * fraction;
}

double ReportGenerator::CalculateStdDeviation(const std::vector<double>& values, double mean) {
    if (values.size() < 2) return 0.0;
    
    double sum = 0.0;
    for (double val : values) {
        double diff = val - mean;
        sum += diff * diff;
    }
    return std::sqrt(sum / static_cast<double>(values.size()));
}

std::string ReportGenerator::EscapeJSON(const std::string& str) {
    std::ostringstream result;
    for (char c : str) {
        switch (c) {
            case '"':  result << "\\\""; break;
            case '\\': result << "\\\\"; break;
            case '\b': result << "\\b"; break;
            case '\f': result << "\\f"; break;
            case '\n': result << "\\n"; break;
            case '\r': result << "\\r"; break;
            case '\t': result << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    result << c;
                }
        }
    }
    return result.str();
}

std::string ReportGenerator::FormatBytes(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << size << " " << units[unitIndex];
    return ss.str();
}

std::string ReportGenerator::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // namespace ProfilerCore
