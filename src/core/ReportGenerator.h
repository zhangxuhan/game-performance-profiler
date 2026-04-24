#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include "ProfilerCore.h"
#include "StatisticsAnalyzer.h"
#include "MemoryAnalyzer.h"
#include "AlertManager.h"

namespace ProfilerCore {

// Forward declarations
struct FrameData;
struct MemorySnapshot;
struct MemoryLeak;
struct Alert;

enum class ReportFormat {
    HTML,
    JSON,
    Markdown,
    CSV
};

struct ReportConfig {
    ReportFormat format = ReportFormat::HTML;
    bool includeCharts = true;
    bool includeAlerts = true;
    bool includeMemoryLeaks = true;
    bool includeRecommendations = true;
    int maxFrames = 1000;
    std::string title = "Performance Report";
};

struct PerformanceMetrics {
    double avgFps = 0.0;
    double minFps = 0.0;
    double maxFps = 0.0;
    double percentile1 = 0.0;
    double percentile5 = 0.0;
    double percentile95 = 0.0;
    double percentile99 = 0.0;
    double stdDeviation = 0.0;
    
    double avgFrameTime = 0.0;
    double minFrameTime = 0.0;
    double maxFrameTime = 0.0;
    
    int64_t totalAllocations = 0;
    int64_t totalDeallocations = 0;
    int64_t currentMemoryUsage = 0;
    int leakCount = 0;
    
    int alertCount = 0;
    int criticalAlerts = 0;
    int warningAlerts = 0;
    
    double gpuUtilization = 0.0;
    double cpuUtilization = 0.0;
    
    int totalFrames = 0;
    int droppedFrames = 0;
    double droppedFrameRate = 0.0;
};

struct FunctionStats {
    std::string name;
    double totalTime;
    double avgTime;
    double percentage;
    int callCount;
    double minTime;
    double maxTime;
};

class ReportGenerator {
public:
    ReportGenerator();
    ~ReportGenerator();
    
    // Configuration
    void SetConfig(const ReportConfig& config);
    ReportConfig GetConfig() const { return m_config; }
    
    // Report generation
    std::string GenerateReport();
    bool SaveToFile(const std::string& filepath);
    
    // Metrics calculation
    PerformanceMetrics CalculateMetrics(const std::vector<FrameData>& frames);
    std::vector<FunctionStats> CalculateFunctionStats(const std::vector<FrameData>& frames);
    
    // Recommendations
    std::vector<std::string> GenerateRecommendations(const PerformanceMetrics& metrics);
    
    // Chart data generation for HTML reports
    std::string GenerateFPSChartData(const std::vector<FrameData>& frames);
    std::string GenerateFrameTimeChartData(const std::vector<FrameData>& frames);
    std::string GenerateMemoryChartData(const std::vector<FrameData>& frames);
    
private:
    ReportConfig m_config;
    
    std::string GenerateHTMLReport(const PerformanceMetrics& metrics,
                                   const std::vector<FunctionStats>& funcStats,
                                   const std::vector<std::string>& recommendations);
    std::string GenerateJSONReport(const PerformanceMetrics& metrics,
                                    const std::vector<FunctionStats>& funcStats,
                                    const std::vector<std::string>& recommendations);
    std::string GenerateMarkdownReport(const PerformanceMetrics& metrics,
                                        const std::vector<FunctionStats>& funcStats,
                                        const std::vector<std::string>& recommendations);
    std::string GenerateCSVReport(const PerformanceMetrics& metrics,
                                  const std::vector<FunctionStats>& funcStats);
    
    double CalculatePercentile(const std::vector<double>& values, double percentile);
    double CalculateStdDeviation(const std::vector<double>& values, double mean);
    
    std::string EscapeJSON(const std::string& str);
    std::string FormatBytes(int64_t bytes);
    std::string GetCurrentTimestamp();
};

} // namespace ProfilerCore