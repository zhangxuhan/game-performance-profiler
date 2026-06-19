#include "HeatmapAnalyzer.h"
#include "ProfilerCore.h"
#include "StatisticsAnalyzer.h"
#include "GPUProfiler.h"
#include "MemoryAnalyzer.h"
#include "ThermalMonitor.h"
#include "NetworkProfiler.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <set>

namespace ProfilerCore {

// ─── Helper: microsecond timestamp ───────────────────────────────────────────

static int64_t NowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

HeatmapAnalyzer::HeatmapAnalyzer()
    : m_sessionStartTime(NowUs())
{
    m_samples.reserve(10000);
}

HeatmapAnalyzer::~HeatmapAnalyzer() {
}

// ─── Configuration ────────────────────────────────────────────────────────────

void HeatmapAnalyzer::SetConfig(const HeatmapConfig& config) {
    m_config = config;
}

// ─── Data Recording ──────────────────────────────────────────────────────────

void HeatmapAnalyzer::RecordSample(double value, int64_t timestamp) {
    if (!m_enabled) return;

    Sample s;
    s.timestamp = (timestamp > 0) ? timestamp : NowUs();
    s.value = value;
    s.type = m_config.dataType;

    m_samples.push_back(s);
    m_lastSampleTime = s.timestamp;

    // Track session start
    if (m_samples.size() == 1) {
        m_sessionStartTime = s.timestamp;
    }
}

void HeatmapAnalyzer::RecordSample(HeatmapDataType type, double value, int64_t timestamp) {
    if (!m_enabled) return;

    Sample s;
    s.timestamp = (timestamp > 0) ? timestamp : NowUs();
    s.value = value;
    s.type = type;

    m_samples.push_back(s);
    m_lastSampleTime = s.timestamp;

    if (m_samples.size() == 1) {
        m_sessionStartTime = s.timestamp;
    }
}

void HeatmapAnalyzer::RecordFunctionSample(const std::string& functionName,
                                             double durationMs, int64_t timestamp) {
    if (!m_enabled) return;

    FunctionSample fs;
    fs.timestamp = (timestamp > 0) ? timestamp : NowUs();
    fs.durationMs = durationMs;

    m_functionSamples[functionName].push_back(fs);

    if (m_functionSamples.size() == 1 && m_samples.empty()) {
        m_sessionStartTime = fs.timestamp;
    }
}

// ─── Pre-built Heatmap Generators ─────────────────────────────────────────────

HeatmapResult HeatmapAnalyzer::GenerateFPSHeatmap() {
    HeatmapResult result;
    result.config = m_config;
    result.config.dataType = HeatmapDataType::FPS;

    // Get FPS data from statistics analyzer
    auto& core = ProfilerCore::GetInstance();
    auto analyzer = core.GetAnalyzer();
    if (!analyzer) {
        return result;
    }

    // Collect frame history
    std::vector<double> fpsValues;
    std::vector<int64_t> timestamps;

    // Use session data if available
    auto session = core.GetSessionManager();
    if (session) {
        auto frames = session->GetSessionFrames(core.GetActiveSessionId());
        for (const auto& f : frames) {
            fpsValues.push_back(static_cast<double>(f.fps));
            timestamps.push_back(f.timestamp);
        }
    }

    // Fallback: generate from current samples
    if (fpsValues.empty()) {
        auto stats = analyzer->GetSummary();
        // Simulate distribution based on stats
        for (const auto& sample : m_samples) {
            if (sample.type == HeatmapDataType::FPS) {
                fpsValues.push_back(sample.value);
                timestamps.push_back(sample.timestamp);
            }
        }
    }

    if (fpsValues.empty()) {
        return result;
    }

    result.startTime = timestamps.empty() ? m_sessionStartTime : timestamps.front();
    result.endTime = timestamps.empty() ? m_lastSampleTime : timestamps.back();
    result.durationMs = (result.endTime - result.startTime) / 1000;
    result.totalSamples = static_cast<int>(fpsValues.size());

    // Compute grid
    ComputeGrid(result, fpsValues, timestamps);
    ComputeStatistics(result);
    ComputeColorBar(result);
    GenerateInsights(result);

    return result;
}

HeatmapResult HeatmapAnalyzer::GenerateFrameTimeHeatmap() {
    HeatmapResult result;
    result.config = m_config;
    result.config.dataType = HeatmapDataType::FrameTime;

    auto& core = ProfilerCore::GetInstance();
    auto session = core.GetSessionManager();

    std::vector<double> frameTimes;
    std::vector<int64_t> timestamps;

    if (session) {
        auto frames = session->GetSessionFrames(core.GetActiveSessionId());
        for (const auto& f : frames) {
            frameTimes.push_back(static_cast<double>(f.frameTime));
            timestamps.push_back(f.timestamp);
        }
    }

    if (frameTimes.empty()) {
        for (const auto& sample : m_samples) {
            if (sample.type == HeatmapDataType::FrameTime) {
                frameTimes.push_back(sample.value);
                timestamps.push_back(sample.timestamp);
            }
        }
    }

    if (frameTimes.empty()) {
        return result;
    }

    result.startTime = timestamps.empty() ? m_sessionStartTime : timestamps.front();
    result.endTime = timestamps.empty() ? m_lastSampleTime : timestamps.back();
    result.durationMs = (result.endTime - result.startTime) / 1000;
    result.totalSamples = static_cast<int>(frameTimes.size());

    ComputeGrid(result, frameTimes, timestamps);
    ComputeStatistics(result);
    ComputeColorBar(result);

    return result;
}

HeatmapResult HeatmapAnalyzer::GenerateMemoryHeatmap() {
    HeatmapResult result;
    result.config = m_config;
    result.config.dataType = HeatmapDataType::Memory;

    std::vector<double> memoryValues;
    std::vector<int64_t> timestamps;

    for (const auto& sample : m_samples) {
        if (sample.type == HeatmapDataType::Memory) {
            memoryValues.push_back(sample.value / (1024.0 * 1024.0)); // Convert to MB
            timestamps.push_back(sample.timestamp);
        }
    }

    // Fallback: get from memory analyzer
    if (memoryValues.empty()) {
        auto& core = ProfilerCore::GetInstance();
        auto mem = core.GetMemoryAnalyzer();
        if (mem) {
            auto report = mem->GenerateReport();
            // Use trend data if available
            for (const auto& sample : m_samples) {
                memoryValues.push_back(sample.value / (1024.0 * 1024.0));
                timestamps.push_back(sample.timestamp);
            }
        }
    }

    if (memoryValues.empty()) {
        return result;
    }

    result.startTime = timestamps.empty() ? m_sessionStartTime : timestamps.front();
    result.endTime = timestamps.empty() ? m_lastSampleTime : timestamps.back();
    result.durationMs = (result.endTime - result.startTime) / 1000;
    result.totalSamples = static_cast<int>(memoryValues.size());

    ComputeGrid(result, memoryValues, timestamps);
    ComputeStatistics(result);
    ComputeColorBar(result);

    return result;
}

HeatmapResult HeatmapAnalyzer::GenerateTemperatureHeatmap() {
    HeatmapResult result;
    result.config = m_config;
    result.config.dataType = HeatmapDataType::Temperature;

    std::vector<double> tempValues;
    std::vector<int64_t> timestamps;

    for (const auto& sample : m_samples) {
        if (sample.type == HeatmapDataType::Temperature) {
            tempValues.push_back(sample.value);
            timestamps.push_back(sample.timestamp);
        }
    }

    // Fallback: get from thermal monitor
    if (tempValues.empty()) {
        auto& core = ProfilerCore::GetInstance();
        auto thermal = core.GetThermalMonitor();
        if (thermal) {
            auto snapshot = thermal->GetSnapshot();
            tempValues.push_back(snapshot.cpuPackage);
            timestamps.push_back(NowUs());
        }
    }

    if (tempValues.empty()) {
        return result;
    }

    result.startTime = timestamps.empty() ? m_sessionStartTime : timestamps.front();
    result.endTime = timestamps.empty() ? m_lastSampleTime : timestamps.back();
    result.durationMs = (result.endTime - result.startTime) / 1000;
    result.totalSamples = static_cast<int>(tempValues.size());

    ComputeGrid(result, tempValues, timestamps);
    ComputeStatistics(result);
    ComputeColorBar(result);

    return result;
}

HeatmapResult HeatmapAnalyzer::GenerateGPUHeatmap() {
    HeatmapResult result;
    result.config = m_config;
    result.config.dataType = HeatmapDataType::GPU;

    std::vector<double> gpuValues;
    std::vector<int64_t> timestamps;

    for (const auto& sample : m_samples) {
        if (sample.type == HeatmapDataType::GPU) {
            gpuValues.push_back(sample.value);
            timestamps.push_back(sample.timestamp);
        }
    }

    // Fallback: get from GPU profiler
    if (gpuValues.empty()) {
        auto& core = ProfilerCore::GetInstance();
        auto gpu = core.GetGPUProfiler();
        if (gpu) {
            auto stats = gpu->GetSummary();
            gpuValues.push_back(stats.avgGpuUtilization);
            timestamps.push_back(NowUs());
        }
    }

    if (gpuValues.empty()) {
        return result;
    }

    result.startTime = timestamps.empty() ? m_sessionStartTime : timestamps.front();
    result.endTime = timestamps.empty() ? m_lastSampleTime : timestamps.back();
    result.durationMs = (result.endTime - result.startTime) / 1000;
    result.totalSamples = static_cast<int>(gpuValues.size());

    ComputeGrid(result, gpuValues, timestamps);
    ComputeStatistics(result);
    ComputeColorBar(result);

    return result;
}

HeatmapResult HeatmapAnalyzer::GenerateNetworkLatencyHeatmap() {
    HeatmapResult result;
    result.config = m_config;
    result.config.dataType = HeatmapDataType::NetworkLatency;

    std::vector<double> rttValues;
    std::vector<int64_t> timestamps;

    for (const auto& sample : m_samples) {
        if (sample.type == HeatmapDataType::NetworkLatency) {
            rttValues.push_back(sample.value);
            timestamps.push_back(sample.timestamp);
        }
    }

    // Fallback: get from network profiler
    if (rttValues.empty()) {
        auto& core = ProfilerCore::GetInstance();
        auto net = core.GetNetworkProfiler();
        if (net) {
            auto report = net->GenerateReport();
            // Use recorded samples
            for (const auto& sample : m_samples) {
                rttValues.push_back(sample.value);
                timestamps.push_back(sample.timestamp);
            }
        }
    }

    if (rttValues.empty()) {
        return result;
    }

    result.startTime = timestamps.empty() ? m_sessionStartTime : timestamps.front();
    result.endTime = timestamps.empty() ? m_lastSampleTime : timestamps.back();
    result.durationMs = (result.endTime - result.startTime) / 1000;
    result.totalSamples = static_cast<int>(rttValues.size());

    ComputeGrid(result, rttValues, timestamps);
    ComputeStatistics(result);
    ComputeColorBar(result);

    return result;
}

HeatmapResult HeatmapAnalyzer::GenerateCustomHeatmap(HeatmapDataType type) {
    HeatmapResult result;
    result.config = m_config;
    result.config.dataType = type;

    std::vector<double> values;
    std::vector<int64_t> timestamps;

    for (const auto& sample : m_samples) {
        if (sample.type == type) {
            values.push_back(sample.value);
            timestamps.push_back(sample.timestamp);
        }
    }

    if (values.empty()) {
        return result;
    }

    result.startTime = timestamps.empty() ? m_sessionStartTime : timestamps.front();
    result.endTime = timestamps.empty() ? m_lastSampleTime : timestamps.back();
    result.durationMs = (result.endTime - result.startTime) / 1000;
    result.totalSamples = static_cast<int>(values.size());

    ComputeGrid(result, values, timestamps);
    ComputeStatistics(result);
    ComputeColorBar(result);

    return result;
}

CallStackHeatmap HeatmapAnalyzer::GenerateCallStackHeatmap(int topN) {
    CallStackHeatmap result;
    result.startTime = m_sessionStartTime;
    result.endTime = m_lastSampleTime;

    if (m_functionSamples.empty()) {
        return result;
    }

    // Sort functions by total time
    std::vector<std::pair<std::string, double>> functionTotals;
    for (const auto& pair : m_functionSamples) {
        double total = 0.0;
        for (const auto& s : pair.second) {
            total += s.durationMs;
        }
        functionTotals.push_back({pair.first, total});
    }

    std::sort(functionTotals.begin(), functionTotals.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    // Take top N
    int count = std::min(topN, static_cast<int>(functionTotals.size()));
    for (int i = 0; i < count; i++) {
        result.functions.push_back(functionTotals[i].first);
    }

    // Build grid (simplified: average per function per time bin)
    int timeBins = 30;
    result.grid.resize(result.functions.size());
    for (size_t i = 0; i < result.functions.size(); i++) {
        result.grid[i].resize(timeBins, 0.0);

        const auto& samples = m_functionSamples[result.functions[i]];
        if (samples.empty()) continue;

        int64_t duration = result.endTime - result.startTime;
        if (duration <= 0) continue;

        double binDuration = static_cast<double>(duration) / timeBins;

        std::vector<int> counts(timeBins, 0);
        for (const auto& s : samples) {
            int bin = static_cast<int>((s.timestamp - result.startTime) / binDuration);
            if (bin >= 0 && bin < timeBins) {
                result.grid[i][bin] += s.durationMs;
                counts[bin]++;
            }
        }

        // Average
        for (int j = 0; j < timeBins; j++) {
            if (counts[j] > 0) {
                result.grid[i][j] /= counts[j];
            }
        }
    }

    return result;
}

// ─── Session Comparison ──────────────────────────────────────────────────────

ComparisonHeatmap HeatmapAnalyzer::CompareSessions(
    const std::vector<double>& baselineData,
    const std::vector<double>& currentData,
    int64_t baselineStart,
    int64_t currentStart)
{
    ComparisonHeatmap result;

    // Generate baseline heatmap
    std::vector<int64_t> baselineTs;
    for (size_t i = 0; i < baselineData.size(); i++) {
        baselineTs.push_back(baselineStart + i * 16667); // ~60fps
    }
    result.baseline.config = m_config;
    ComputeGrid(result.baseline, baselineData, baselineTs);
    ComputeStatistics(result.baseline);

    // Generate current heatmap
    std::vector<int64_t> currentTs;
    for (size_t i = 0; i < currentData.size(); i++) {
        currentTs.push_back(currentStart + i * 16667);
    }
    result.current.config = m_config;
    ComputeGrid(result.current, currentData, currentTs);
    ComputeStatistics(result.current);

    // Compute delta
    int rows = std::min(result.baseline.grid.size(), result.current.grid.size());
    int cols = std::min(
        result.baseline.grid.empty() ? 0 : result.baseline.grid[0].size(),
        result.current.grid.empty() ? 0 : result.current.grid[0].size()
    );

    result.deltaGrid.resize(rows);
    for (int r = 0; r < rows; r++) {
        result.deltaGrid[r].resize(cols, 0.0);
        for (int c = 0; c < cols; c++) {
            result.deltaGrid[r][c] = result.current.grid[r][c].value - result.baseline.grid[r][c].value;

            // Detect significant changes (>20% or >10 units)
            double baseline = result.baseline.grid[r][c].value;
            double current = result.current.grid[r][c].value;
            if (baseline > 0) {
                double change = std::abs(current - baseline) / baseline;
                if (change > 0.2 || std::abs(current - baseline) > 10) {
                    std::ostringstream ss;
                    ss << "Cell (" << r << "," << c << "): " << baseline << " -> " << current
                       << " (" << std::fixed << std::setprecision(1) << (change * 100) << "% change)";
                    result.significantChanges.push_back(ss.str());
                }
            }
        }
    }

    // Correlation score
    if (baselineData.size() == currentData.size() && !baselineData.empty()) {
        double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
        size_t n = baselineData.size();

        for (size_t i = 0; i < n; i++) {
            sumX += baselineData[i];
            sumY += currentData[i];
            sumXY += baselineData[i] * currentData[i];
            sumX2 += baselineData[i] * baselineData[i];
            sumY2 += currentData[i] * currentData[i];
        }

        double num = n * sumXY - sumX * sumY;
        double den = std::sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));
        result.correlationScore = (den > 0) ? num / den : 0.0;
        result.correlationScore = std::max(0.0, std::min(1.0, result.correlationScore));
    } else {
        result.correlationScore = 0.0;
    }

    return result;
}

// ─── Export Methods ───────────────────────────────────────────────────────────

std::string HeatmapAnalyzer::ExportToSVG(const HeatmapResult& result) const {
    std::ostringstream ss;

    int cellW = m_config.cellWidth;
    int cellH = m_config.cellHeight;
    int padding = m_config.cellPadding;
    int rows = static_cast<int>(result.grid.size());
    int cols = rows > 0 ? static_cast<int>(result.grid[0].size()) : 0;

    int width = cols * (cellW + padding) + 100;  // Extra for labels
    int height = rows * (cellH + padding) + 80;

    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
       << "width=\"" << width << "\" height=\"" << height << "\" "
       << "viewBox=\"0 0 " << width << " " << height << "\">\n";

    // Background
    ss << "  <rect width=\"" << width << "\" height=\"" << height << "\" fill=\"#1a1a2e\"/>\n";

    // Title
    if (!result.config.title.empty()) {
        ss << "  <text x=\"" << width / 2 << "\" y=\"25\" fill=\"white\" "
           << "font-family=\"Arial, sans-serif\" font-size=\"16\" text-anchor=\"middle\" "
           << "font-weight=\"bold\">" << result.config.title << "</text>\n";
    }

    // Grid cells
    int offsetX = 80;
    int offsetY = 50;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const auto& cell = result.grid[r][c];
            int x = offsetX + c * (cellW + padding);
            int y = offsetY + r * (cellH + padding);

            ss << "  <rect x=\"" << x << "\" y=\"" << y << "\" "
               << "width=\"" << cellW << "\" height=\"" << cellH << "\" "
               << "fill=\"" << cell.color << "\"";

            if (cell.sampleCount > 0) {
                ss << ">\n";
                ss << "    <title>Value: " << std::fixed << std::setprecision(2) << cell.value
                   << "\nSamples: " << cell.sampleCount << "</title>\n";
                ss << "  </rect>\n";
            } else {
                ss << "/>\n";
            }
        }
    }

    // Y-axis labels (value bins)
    if (m_config.showAxisLabels && !result.rowStats.empty()) {
        for (int r = 0; r < rows; r++) {
            int y = offsetY + r * (cellH + padding) + cellH / 2;
            ss << "  <text x=\"75\" y=\"" << (y + 4) << "\" fill=\"#aaa\" "
               << "font-family=\"monospace\" font-size=\"10\" text-anchor=\"end\">"
               << std::fixed << std::setprecision(1) << result.rowStats[r].binStart
               << "</text>\n";
        }
    }

    // Color bar
    if (m_config.showColorBar && !result.colorBar.empty()) {
        int barX = width - 50;
        int barY = offsetY;
        int barW = 20;
        int barH = rows * (cellH + padding);

        ss << "  <defs>\n";
        ss << "    <linearGradient id=\"colorBar\" x1=\"0%\" y1=\"100%\" x2=\"0%\" y2=\"0%\">\n";
        for (size_t i = 0; i < result.colorBar.size(); i++) {
            double pct = static_cast<double>(i) / (result.colorBar.size() - 1) * 100;
            ss << "      <stop offset=\"" << pct << "%\" stop-color=\""
               << result.colorBar[i].second << "\"/>\n";
        }
        ss << "    </linearGradient>\n";
        ss << "  </defs>\n";

        ss << "  <rect x=\"" << barX << "\" y=\"" << barY << "\" "
           << "width=\"" << barW << "\" height=\"" << barH << "\" "
           << "fill=\"url(#colorBar)\" stroke=\"#444\"/>\n";

        // Min/max labels
        ss << "  <text x=\"" << (barX + barW + 5) << "\" y=\"" << (barY + 10) << "\" "
           << "fill=\"#aaa\" font-size=\"9\">" << std::fixed << std::setprecision(1)
           << result.globalMax << "</text>\n";
        ss << "  <text x=\"" << (barX + barW + 5) << "\" y=\"" << (barY + barH) << "\" "
           << "fill=\"#aaa\" font-size=\"9\">" << result.globalMin << "</text>\n";
    }

    // Summary stats
    ss << "  <text x=\"" << offsetX << "\" y=\"" << (height - 10) << "\" fill=\"#888\" "
       << "font-size=\"11\">Samples: " << result.totalSamples
       << " | Range: " << std::fixed << std::setprecision(1) << result.globalMin
       << " - " << result.globalMax
       << " | Avg: " << result.globalAvg << "</text>\n";

    ss << "</svg>\n";

    return ss.str();
}

std::string HeatmapAnalyzer::ExportToJSON(const HeatmapResult& result) const {
    std::ostringstream ss;

    ss << "{\n";
    ss << "  \"config\": {\n";
    ss << "    \"dataType\": " << static_cast<int>(result.config.dataType) << ",\n";
    ss << "    \"colorScheme\": " << static_cast<int>(result.config.colorScheme) << ",\n";
    ss << "    \"aggregation\": " << static_cast<int>(result.config.aggregation) << "\n";
    ss << "  },\n";

    // Metadata
    ss << "  \"metadata\": {\n";
    ss << "    \"startTime\": " << result.startTime << ",\n";
    ss << "    \"endTime\": " << result.endTime << ",\n";
    ss << "    \"durationMs\": " << result.durationMs << ",\n";
    ss << "    \"totalSamples\": " << result.totalSamples << ",\n";
    ss << "    \"globalMin\": " << std::fixed << std::setprecision(2) << result.globalMin << ",\n";
    ss << "    \"globalMax\": " << result.globalMax << ",\n";
    ss << "    \"globalAvg\": " << result.globalAvg << "\n";
    ss << "  },\n";

    // Grid data
    ss << "  \"grid\": [\n";
    for (size_t r = 0; r < result.grid.size(); r++) {
        ss << "    [";
        for (size_t c = 0; c < result.grid[r].size(); c++) {
            const auto& cell = result.grid[r][c];
            ss << "{\"value\":" << std::fixed << std::setprecision(2) << cell.value
               << ",\"color\":\"" << cell.color << "\""
               << ",\"samples\":" << cell.sampleCount << "}";
            if (c < result.grid[r].size() - 1) ss << ",";
        }
        ss << "]";
        if (r < result.grid.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "  ],\n";

    // Row stats
    ss << "  \"rowStats\": [\n";
    for (size_t i = 0; i < result.rowStats.size(); i++) {
        const auto& rs = result.rowStats[i];
        ss << "    {\"binStart\":" << rs.binStart << ",\"binEnd\":" << rs.binEnd
           << ",\"frequency\":" << std::fixed << std::setprecision(3) << rs.frequency << "}";
        if (i < result.rowStats.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "  ],\n";

    // Insights
    ss << "  \"insights\": [\n";
    for (size_t i = 0; i < result.insights.size(); i++) {
        ss << "    \"" << result.insights[i] << "\"";
        if (i < result.insights.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "  ],\n";

    // Hotspots
    ss << "  \"hotspots\": [\n";
    for (size_t i = 0; i < result.hotspots.size(); i++) {
        ss << "    \"" << result.hotspots[i] << "\"";
        if (i < result.hotspots.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "  ]\n";

    ss << "}\n";

    return ss.str();
}

std::string HeatmapAnalyzer::ExportToCSV(const HeatmapResult& result) const {
    std::ostringstream ss;

    // Header row
    ss << "row_bin,value_start";
    for (size_t c = 0; c < result.grid[0].size(); c++) {
        ss << ",col_" << c;
    }
    ss << "\n";

    // Data rows
    for (size_t r = 0; r < result.grid.size(); r++) {
        ss << r << "," << std::fixed << std::setprecision(2) << result.rowStats[r].binStart;
        for (size_t c = 0; c < result.grid[r].size(); c++) {
            ss << "," << result.grid[r][c].value;
        }
        ss << "\n";
    }

    return ss.str();
}

std::string HeatmapAnalyzer::ExportToHTML(const HeatmapResult& result,
                                           const std::string& title) const {
    std::ostringstream ss;

    std::string pageTitle = title.empty() ? result.config.title : title;
    if (pageTitle.empty()) pageTitle = "Performance Heatmap";

    ss << "<!DOCTYPE html>\n";
    ss << "<html lang=\"en\">\n";
    ss << "<head>\n";
    ss << "  <meta charset=\"UTF-8\">\n";
    ss << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    ss << "  <title>" << pageTitle << "</title>\n";
    ss << "  <style>\n";
    ss << "    body { background: #0a0a14; color: #fff; font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; }\n";
    ss << "    .container { max-width: 1200px; margin: 0 auto; }\n";
    ss << "    h1 { color: #4fc3f7; margin-bottom: 20px; }\n";
    ss << "    .stats { background: #1a1a2e; padding: 15px; border-radius: 8px; margin-bottom: 20px; }\n";
    ss << "    .stats span { margin-right: 30px; color: #aaa; }\n";
    ss << "    .insights { background: #1a1a2e; padding: 15px; border-radius: 8px; margin-top: 20px; }\n";
    ss << "    .insights h3 { color: #4fc3f7; margin-top: 0; }\n";
    ss << "    .insights ul { padding-left: 20px; }\n";
    ss << "    .insights li { color: #ccc; margin-bottom: 8px; }\n";
    ss << "    .hotspot { color: #ff7043; }\n";
    ss << "  </style>\n";
    ss << "</head>\n";
    ss << "<body>\n";
    ss << "  <div class=\"container\">\n";
    ss << "    <h1>" << pageTitle << "</h1>\n";

    // Stats
    ss << "    <div class=\"stats\">\n";
    ss << "      <span><strong>Samples:</strong> " << result.totalSamples << "</span>\n";
    ss << "      <span><strong>Range:</strong> " << std::fixed << std::setprecision(2)
       << result.globalMin << " - " << result.globalMax << "</span>\n";
    ss << "      <span><strong>Average:</strong> " << result.globalAvg << "</span>\n";
    ss << "      <span><strong>Duration:</strong> " << (result.durationMs / 1000.0) << "s</span>\n";
    ss << "    </div>\n";

    // SVG
    ss << "    <div class=\"heatmap\">\n";
    ss << ExportToSVG(result);
    ss << "    </div>\n";

    // Insights
    if (!result.insights.empty()) {
        ss << "    <div class=\"insights\">\n";
        ss << "      <h3>Insights</h3>\n";
        ss << "      <ul>\n";
        for (const auto& insight : result.insights) {
            ss << "        <li>" << insight << "</li>\n";
        }
        ss << "      </ul>\n";
        ss << "    </div>\n";
    }

    // Hotspots
    if (!result.hotspots.empty()) {
        ss << "    <div class=\"insights\">\n";
        ss << "      <h3 class=\"hotspot\">Hotspots Detected</h3>\n";
        ss << "      <ul>\n";
        for (const auto& hs : result.hotspots) {
            ss << "        <li class=\"hotspot\">" << hs << "</li>\n";
        }
        ss << "      </ul>\n";
        ss << "    </div>\n";
    }

    ss << "  </div>\n";
    ss << "</body>\n";
    ss << "</html>\n";

    return ss.str();
}

std::string HeatmapAnalyzer::ExportCallStackToSVG(const CallStackHeatmap& cs) const {
    std::ostringstream ss;

    if (cs.functions.empty() || cs.grid.empty()) {
        return "<svg></svg>";
    }

    int cellW = 30;
    int cellH = 25;
    int padding = 2;
    int rows = static_cast<int>(cs.functions.size());
    int cols = cs.grid.empty() ? 0 : static_cast<int>(cs.grid[0].size());
    int maxLabelWidth = 0;

    for (const auto& f : cs.functions) {
        maxLabelWidth = std::max(maxLabelWidth, static_cast<int>(f.length() * 7));
    }

    int width = maxLabelWidth + cols * (cellW + padding) + 50;
    int height = rows * (cellH + padding) + 60;

    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
       << "width=\"" << width << "\" height=\"" << height << "\">\n";

    ss << "  <rect width=\"" << width << "\" height=\"" << height << "\" fill=\"#1a1a2e\"/>\n";
    ss << "  <text x=\"" << width / 2 << "\" y=\"25\" fill=\"white\" "
       << "font-family=\"Arial\" font-size=\"14\" text-anchor=\"middle\">"
       << "Function Call Heatmap</text>\n";

    // Find max value for normalization
    double maxVal = 0.0;
    for (const auto& row : cs.grid) {
        for (double v : row) {
            maxVal = std::max(maxVal, v);
        }
    }

    int offsetX = maxLabelWidth + 20;
    int offsetY = 40;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            double val = cs.grid[r][c];
            double norm = maxVal > 0 ? val / maxVal : 0.0;
            std::string color = ValueToColor(norm, HeatmapColorScheme::Viridis);

            int x = offsetX + c * (cellW + padding);
            int y = offsetY + r * (cellH + padding);

            ss << "  <rect x=\"" << x << "\" y=\"" << y << "\" "
               << "width=\"" << cellW << "\" height=\"" << cellH << "\" "
               << "fill=\"" << color << "\">\n";
            ss << "    <title>" << cs.functions[r] << ": " << std::fixed
               << std::setprecision(2) << val << "ms</title>\n";
            ss << "  </rect>\n";
        }

        // Function name label
        ss << "  <text x=\"" << (offsetX - 5) << "\" y=\"" << (offsetY + r * (cellH + padding) + cellH - 7) << "\" "
           << "fill=\"#aaa\" font-family=\"monospace\" font-size=\"9\" text-anchor=\"end\">"
           << cs.functions[r] << "</text>\n";
    }

    ss << "</svg>\n";

    return ss.str();
}

// ─── Insights & Analysis ──────────────────────────────────────────────────────

std::vector<std::string> HeatmapAnalyzer::GenerateInsights(const HeatmapResult& result) const {
    std::vector<std::string> insights;

    if (result.totalSamples == 0) {
        insights.push_back("No data available for analysis.");
        return insights;
    }

    // Overall assessment
    if (result.globalAvg > 0) {
        std::ostringstream ss;
        ss << "Average " << (result.config.dataType == HeatmapDataType::FPS ? "FPS" : "value")
           << ": " << std::fixed << std::setprecision(1) << result.globalAvg;
        insights.push_back(ss.str());
    }

    // Variance analysis
    if (result.globalStdDev > 0) {
        double cv = result.globalStdDev / std::max(result.globalAvg, 1.0);
        if (cv > 0.3) {
            insights.push_back("High variance detected (" + std::to_string(static_cast<int>(cv * 100)) +
                               "% coefficient of variation). Performance is inconsistent.");
        } else if (cv < 0.1) {
            insights.push_back("Low variance (" + std::to_string(static_cast<int>(cv * 100)) +
                               "% CV). Performance is stable.");
        }
    }

    // Distribution analysis
    if (!result.rowStats.empty()) {
        // Find the most common value range
        int maxFreqRow = 0;
        double maxFreq = 0.0;
        for (size_t i = 0; i < result.rowStats.size(); i++) {
            if (result.rowStats[i].frequency > maxFreq) {
                maxFreq = result.rowStats[i].frequency;
                maxFreqRow = static_cast<int>(i);
            }
        }

        if (maxFreq > 0.3) {
            std::ostringstream ss;
            ss << "Most values (" << std::fixed << std::setprecision(0) << (maxFreq * 100)
               << "%) fall in range " << std::setprecision(1) << result.rowStats[maxFreqRow].binStart
               << " - " << result.rowStats[maxFreqRow].binEnd;
            insights.push_back(ss.str());
        }
    }

    // Time-based patterns
    if (!result.colStats.empty()) {
        // Find best and worst time periods
        int bestCol = 0, worstCol = 0;
        double bestVal = result.colStats[0].avgValue;
        double worstVal = result.colStats[0].avgValue;

        for (size_t i = 1; i < result.colStats.size(); i++) {
            if (result.colStats[i].avgValue > bestVal) {
                bestVal = result.colStats[i].avgValue;
                bestCol = static_cast<int>(i);
            }
            if (result.colStats[i].avgValue < worstVal) {
                worstVal = result.colStats[i].avgValue;
                worstCol = static_cast<int>(i);
            }
        }

        double bestTime = (result.colStats[bestCol].timeStart - result.startTime) / 1000000.0;
        double worstTime = (result.colStats[worstCol].timeStart - result.startTime) / 1000000.0;

        if (result.config.dataType == HeatmapDataType::FPS) {
            insights.push_back("Best performance at " + std::to_string(static_cast<int>(bestTime)) +
                               "s (" + std::to_string(static_cast<int>(bestVal)) + " FPS)");
            insights.push_back("Worst performance at " + std::to_string(static_cast<int>(worstTime)) +
                               "s (" + std::to_string(static_cast<int>(worstVal)) + " FPS)");
        }
    }

    // Limit insights
    while (insights.size() > 6) {
        insights.pop_back();
    }

    return insights;
}

std::vector<std::string> HeatmapAnalyzer::DetectHotspots(const HeatmapResult& result) const {
    std::vector<std::string> hotspots;

    if (result.grid.empty()) return hotspots;

    // Detect cells significantly below average
    double threshold = result.globalAvg * 0.7;  // 30% below average
    if (result.config.dataType == HeatmapDataType::FPS) {
        threshold = result.globalAvg * 0.6;  // 40% below average for FPS
    }

    int hotspotCount = 0;
    for (size_t r = 0; r < result.grid.size(); r++) {
        for (size_t c = 0; c < result.grid[r].size(); c++) {
            const auto& cell = result.grid[r][c];
            if (cell.sampleCount > 0 && cell.value < threshold) {
                if (hotspotCount < 5) {  // Limit to 5 hotspots
                    double timeSec = 0.0;
                    if (!result.colStats.empty() && c < result.colStats.size()) {
                        timeSec = (result.colStats[c].timeStart - result.startTime) / 1000000.0;
                    }

                    std::ostringstream ss;
                    ss << "At " << std::fixed << std::setprecision(1) << timeSec << "s: "
                       << std::setprecision(1) << cell.value << " ("
                       << std::setprecision(0) << ((cell.value / result.globalAvg - 1.0) * 100) << "% vs avg)";
                    hotspots.push_back(ss.str());
                    hotspotCount++;
                }
            }
        }
    }

    return hotspots;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void HeatmapAnalyzer::Reset() {
    m_samples.clear();
    m_functionSamples.clear();
    m_sessionStartTime = NowUs();
    m_lastSampleTime = m_sessionStartTime;
}

// ─── Private Helpers ──────────────────────────────────────────────────────────

void HeatmapAnalyzer::ComputeGrid(HeatmapResult& result,
                                    const std::vector<double>& values,
                                    const std::vector<int64_t>& timestamps) {
    if (values.empty()) return;

    int rows = m_config.maxRows;
    int cols = m_config.maxCols;

    // Determine value range
    result.globalMin = m_config.autoRange ?
        *std::min_element(values.begin(), values.end()) : m_config.valueMin;
    result.globalMax = m_config.autoRange ?
        *std::max_element(values.begin(), values.end()) : m_config.valueMax;

    if (result.globalMax <= result.globalMin) {
        result.globalMax = result.globalMin + 1.0;
    }

    double valueRange = result.globalMax - result.globalMin;
    double valueBinSize = valueRange / rows;

    // Determine time range
    int64_t timeStart = timestamps.empty() ? result.startTime : timestamps.front();
    int64_t timeEnd = timestamps.empty() ? result.endTime : timestamps.back();
    int64_t timeRange = timeEnd - timeStart;
    if (timeRange <= 0) timeRange = 1;
    double timeBinSize = static_cast<double>(timeRange) / cols;

    // Initialize grid
    result.grid.resize(rows);
    for (int r = 0; r < rows; r++) {
        result.grid[r].resize(cols);
        for (int c = 0; c < cols; c++) {
            result.grid[r][c].row = r;
            result.grid[r][c].col = c;
            result.grid[r][c].value = 0.0;
            result.grid[r][c].rawMin = result.globalMax;
            result.grid[r][c].rawMax = result.globalMin;
            result.grid[r][c].sampleCount = 0;
            result.grid[r][c].normalized = 0.0;
        }
    }

    // Initialize row stats
    result.rowStats.resize(rows);
    for (int r = 0; r < rows; r++) {
        result.rowStats[r].binStart = result.globalMin + r * valueBinSize;
        result.rowStats[r].binEnd = result.rowStats[r].binStart + valueBinSize;
        result.rowStats[r].sampleCount = 0;
        result.rowStats[r].frequency = 0.0;
    }

    // Initialize col stats
    result.colStats.resize(cols);
    for (int c = 0; c < cols; c++) {
        result.colStats[c].timeStart = timeStart + static_cast<int64_t>(c * timeBinSize);
        result.colStats[c].timeEnd = result.colStats[c].timeStart + static_cast<int64_t>(timeBinSize);
        result.colStats[c].avgValue = 0.0;
        result.colStats[c].min = result.globalMax;
        result.colStats[c].max = result.globalMin;
        result.colStats[c].sampleCount = 0;
    }

    // Bin samples
    std::vector<std::vector<std::vector<double>>> cellValues(rows,
        std::vector<std::vector<double>>(cols));

    for (size_t i = 0; i < values.size(); i++) {
        double v = values[i];
        int64_t t = timestamps.empty() ? timeStart + i * 16667 : timestamps[i];

        int r = static_cast<int>((v - result.globalMin) / valueBinSize);
        int c = static_cast<int>((t - timeStart) / timeBinSize);

        r = std::max(0, std::min(rows - 1, r));
        c = std::max(0, std::min(cols - 1, c));

        cellValues[r][c].push_back(v);
        result.grid[r][c].sampleCount++;
        result.grid[r][c].rawMin = std::min(result.grid[r][c].rawMin, v);
        result.grid[r][c].rawMax = std::max(result.grid[r][c].rawMax, v);

        result.rowStats[r].sampleCount++;
        result.colStats[c].sampleCount++;
        result.colStats[c].min = std::min(result.colStats[c].min, v);
        result.colStats[c].max = std::max(result.colStats[c].max, v);
    }

    // Aggregate values per cell
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            auto& cell = result.grid[r][c];
            auto& samples = cellValues[r][c];

            if (samples.empty()) continue;

            switch (m_config.aggregation) {
                case HeatmapAggregation::Average:
                    cell.value = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
                    break;
                case HeatmapAggregation::Maximum:
                    cell.value = *std::max_element(samples.begin(), samples.end());
                    break;
                case HeatmapAggregation::Minimum:
                    cell.value = *std::min_element(samples.begin(), samples.end());
                    break;
                case HeatmapAggregation::Sum:
                    cell.value = std::accumulate(samples.begin(), samples.end(), 0.0);
                    break;
                case HeatmapAggregation::Count:
                    cell.value = static_cast<double>(samples.size());
                    break;
                case HeatmapAggregation::P95:
                    cell.value = ComputePercentile(samples, 0.95);
                    break;
                case HeatmapAggregation::P99:
                    cell.value = ComputePercentile(samples, 0.99);
                    break;
            }
        }
    }

    // Compute row frequencies
    int totalSamples = static_cast<int>(values.size());
    for (int r = 0; r < rows; r++) {
        result.rowStats[r].frequency =
            static_cast<double>(result.rowStats[r].sampleCount) / std::max(1, totalSamples);
    }

    // Compute column averages
    for (int c = 0; c < cols; c++) {
        double sum = 0.0;
        int count = 0;
        for (int r = 0; r < rows; r++) {
            if (result.grid[r][c].sampleCount > 0) {
                sum += result.grid[r][c].value * result.grid[r][c].sampleCount;
                count += result.grid[r][c].sampleCount;
            }
        }
        result.colStats[c].avgValue = count > 0 ? sum / count : 0.0;
    }

    result.totalCells = rows * cols;

    // Normalize and assign colors
    NormalizeGrid(result);
}

void HeatmapAnalyzer::ComputeStatistics(HeatmapResult& result) {
    if (result.grid.empty()) return;

    // Compute average and std dev
    double sum = 0.0;
    double sumSq = 0.0;
    int count = 0;

    for (const auto& row : result.grid) {
        for (const auto& cell : row) {
            if (cell.sampleCount > 0) {
                sum += cell.value * cell.sampleCount;
                sumSq += cell.value * cell.value * cell.sampleCount;
                count += cell.sampleCount;
            }
        }
    }

    if (count > 0) {
        result.globalAvg = sum / count;
        double variance = (sumSq / count) - (result.globalAvg * result.globalAvg);
        result.globalStdDev = std::sqrt(std::max(0.0, variance));
    }
}

void HeatmapAnalyzer::ComputeColorBar(HeatmapResult& result) {
    result.colorBar.clear();

    int steps = 10;
    double range = result.globalMax - result.globalMin;
    double step = range / (steps - 1);

    for (int i = 0; i < steps; i++) {
        double value = result.globalMin + i * step;
        double normalized = static_cast<double>(i) / (steps - 1);
        std::string color = ValueToColor(normalized, m_config.colorScheme);
        result.colorBar.push_back({value, color});
    }
}

void HeatmapAnalyzer::NormalizeGrid(HeatmapResult& result) {
    if (result.grid.empty()) return;

    double range = result.globalMax - result.globalMin;
    if (range <= 0) range = 1.0;

    for (auto& row : result.grid) {
        for (auto& cell : row) {
            cell.normalized = (cell.value - result.globalMin) / range;
            cell.normalized = std::max(0.0, std::min(1.0, cell.normalized));

            // Compute color
            if (m_config.colorScheme == HeatmapColorScheme::Traffic) {
                cell.color = ColorTraffic(cell.normalized, m_config.goodThreshold, m_config.warnThreshold);
            } else {
                cell.color = ValueToColor(cell.normalized, m_config.colorScheme);
            }
        }
    }
}

double HeatmapAnalyzer::ComputePercentile(std::vector<double>& values, double p) const {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    double idx = p * (values.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(idx));
    size_t hi = static_cast<size_t>(std::ceil(idx));
    if (lo == hi || hi >= values.size()) return values[lo];
    return values[lo] * (1.0 - (idx - lo)) + values[hi] * (idx - lo);
}

std::string HeatmapAnalyzer::ValueToColor(double normalized, HeatmapColorScheme scheme) const {
    switch (scheme) {
        case HeatmapColorScheme::Viridis:   return ColorViridis(normalized);
        case HeatmapColorScheme::Inferno:   return ColorInferno(normalized);
        case HeatmapColorScheme::CoolWarm:  return ColorCoolWarm(normalized);
        case HeatmapColorScheme::Grayscale: return ColorGrayscale(normalized);
        case HeatmapColorScheme::Plasma:    return ColorPlasma(normalized);
        case HeatmapColorScheme::Turbo:     return ColorTurbo(normalized);
        case HeatmapColorScheme::Traffic:   return ColorTraffic(normalized, 0.7, 0.5);
        default: return ColorViridis(normalized);
    }
}

// ─── Color Scheme Implementations ─────────────────────────────────────────────

std::string HeatmapAnalyzer::ColorViridis(double t) const {
    t = std::max(0.0, std::min(1.0, t));
    // Simplified Viridis approximation
    double r = 0.267 + 0.329 * t - 2.93 * t * t + 1.85 * t * t * t;
    double g = 0.004 + 2.15 * t - 3.46 * t * t + 1.67 * t * t * t;
    double b = 0.329 + 1.17 * t - 1.06 * t * t + 0.33 * t * t * t;

    auto toHex = [](double v) -> std::string {
        int val = static_cast<int>(std::max(0.0, std::min(255.0, v * 255.0)));
        std::ostringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << val;
        return ss.str();
    };

    return "#" + toHex(r) + toHex(g) + toHex(b);
}

std::string HeatmapAnalyzer::ColorInferno(double t) const {
    t = std::max(0.0, std::min(1.0, t));
    double r = 0.0 + 0.28 * t + 1.75 * t * t;
    double g = 0.0 + 2.0 * t - 1.5 * t * t;
    double b = 0.0 + 0.3 * t + 0.5 * t * t;

    auto toHex = [](double v) -> std::string {
        int val = static_cast<int>(std::max(0.0, std::min(255.0, v * 255.0)));
        std::ostringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << val;
        return ss.str();
    };

    return "#" + toHex(r) + toHex(g) + toHex(b);
}

std::string HeatmapAnalyzer::ColorCoolWarm(double t) const {
    t = std::max(0.0, std::min(1.0, t));
    // Blue -> White -> Red
    double r, g, b;
    if (t < 0.5) {
        r = t * 2.0;
        g = t * 2.0;
        b = 1.0;
    } else {
        r = 1.0;
        g = 2.0 - t * 2.0;
        b = 2.0 - t * 2.0;
    }

    auto toHex = [](double v) -> std::string {
        int val = static_cast<int>(std::max(0.0, std::min(255.0, v * 255.0)));
        std::ostringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << val;
        return ss.str();
    };

    return "#" + toHex(r) + toHex(g) + toHex(b);
}

std::string HeatmapAnalyzer::ColorGrayscale(double t) const {
    t = std::max(0.0, std::min(1.0, t));
    int val = static_cast<int>(t * 255);
    std::ostringstream ss;
    ss << "#" << std::hex << std::setw(2) << std::setfill('0') << val
       << std::setw(2) << std::setfill('0') << val
       << std::setw(2) << std::setfill('0') << val;
    return ss.str();
}

std::string HeatmapAnalyzer::ColorTraffic(double t, double goodThreshold, double warnThreshold) const {
    // Normalize against thresholds
    // goodThreshold (high normalized) -> green
    // warnThreshold (mid normalized) -> yellow
    // low -> red
    double adjusted = t;
    if (goodThreshold != warnThreshold) {
        // Remap thresholds to 0-1 range
        adjusted = (t - (1.0 - goodThreshold)) / (goodThreshold - warnThreshold);
        adjusted = std::max(0.0, std::min(1.0, adjusted));
    }

    double r, g, b;
    if (adjusted > 0.6) {
        // Green zone
        r = 0.2;
        g = 0.8;
        b = 0.2;
    } else if (adjusted > 0.3) {
        // Yellow zone
        r = 1.0;
        g = 0.8;
        b = 0.0;
    } else {
        // Red zone
        r = 0.9;
        g = 0.2;
        b = 0.2;
    }

    auto toHex = [](double v) -> std::string {
        int val = static_cast<int>(std::max(0.0, std::min(255.0, v * 255.0)));
        std::ostringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << val;
        return ss.str();
    };

    return "#" + toHex(r) + toHex(g) + toHex(b);
}

std::string HeatmapAnalyzer::ColorPlasma(double t) const {
    t = std::max(0.0, std::min(1.0, t));
    double r = 0.05 + 0.39 * t + 0.53 * t * t;
    double g = 0.03 + 0.72 * t - 0.33 * t * t;
    double b = 0.53 - 0.83 * t + 0.64 * t * t;

    auto toHex = [](double v) -> std::string {
        int val = static_cast<int>(std::max(0.0, std::min(255.0, v * 255.0)));
        std::ostringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << val;
        return ss.str();
    };

    return "#" + toHex(r) + toHex(g) + toHex(b);
}

std::string HeatmapAnalyzer::ColorTurbo(double t) const {
    t = std::max(0.0, std::min(1.0, t));
    // Simplified Turbo colormap
    double r = 0.0 + 0.9 * t + 0.1 * t * t;
    double g = 0.0 + 0.7 * t + 0.3 * t * t - 0.5 * t * t * t;
    double b = 0.3 + 0.5 * t - 0.8 * t * t + 0.3 * t * t * t;

    auto toHex = [](double v) -> std::string {
        int val = static_cast<int>(std::max(0.0, std::min(255.0, v * 255.0)));
        std::ostringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << val;
        return ss.str();
    };

    return "#" + toHex(r) + toHex(g) + toHex(b);
}

} // namespace ProfilerCore
