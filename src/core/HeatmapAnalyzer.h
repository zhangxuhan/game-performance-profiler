#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstdint>
#include <optional>
#include <functional>
#include <array>

namespace ProfilerCore {

// ─── Enumerations ─────────────────────────────────────────────────────────────

/**
 * Type of data to visualize in the heatmap
 */
enum class HeatmapDataType {
    FPS,            // Frames per second distribution
    FrameTime,      // Frame time (ms) distribution
    Memory,         // Memory usage distribution
    Temperature,    // CPU/GPU temperature distribution
    GPU,            // GPU utilization distribution
    CPU,            // CPU utilization distribution
    NetworkLatency, // Network RTT distribution
    DiskIO,         // Disk I/O activity distribution
    Custom          // User-defined metric
};

/**
 * Heatmap color gradient style
 */
enum class HeatmapColorScheme {
    Viridis,        // Purple -> Blue -> Green -> Yellow (scientific)
    Inferno,        // Black -> Red -> Orange -> Yellow (intensity)
    CoolWarm,       // Blue -> White -> Red (diverging)
    Grayscale,      // Black -> White
    Traffic,        // Green -> Yellow -> Red (performance)
    Plasma,         // Purple -> Pink -> Orange -> Yellow
    Turbo           // Blue -> Green -> Yellow -> Red (vivid)
};

/**
 * Heatmap aggregation method for binning data
 */
enum class HeatmapAggregation {
    Average,        // Average value in each bin
    Maximum,        // Maximum value in each bin
    Minimum,        // Minimum value in each bin
    Sum,            // Sum of values in each bin
    Count,          // Number of samples in each bin
    P95,            // 95th percentile in each bin
    P99             // 99th percentile in each bin
};

/**
 * Time bin size for horizontal axis
 */
enum class HeatmapTimeBin {
    PerFrame,       // Each frame is a column
    PerSecond,      // Group by second
    Per5Seconds,    // Group by 5 seconds
    Per10Seconds,   // Group by 10 seconds
    Per30Seconds,   // Group by 30 seconds
    PerMinute,      // Group by minute
    Per5Minutes     // Group by 5 minutes
};

/**
 * Export format for heatmap visualization
 */
enum class HeatmapExportFormat {
    SVG,            // Scalable Vector Graphics
    JSON,           // JSON data for frontend rendering
    CSV,            // CSV matrix format
    HTML            // Self-contained HTML with embedded SVG
};

// ─── Data Structures ──────────────────────────────────────────────────────────

/**
 * A single cell in the heatmap grid
 */
struct HeatmapCell {
    int row;            // Y-axis bin index
    int col;            // X-axis bin index (time)
    double value;       // Aggregated value
    double rawMin;      // Minimum raw value in this cell
    double rawMax;      // Maximum raw value in this cell
    int sampleCount;    // Number of samples contributing to this cell
    std::string color;  // CSS color string (e.g., "#ff5733")
    double normalized;  // Value normalized to 0.0-1.0 for color mapping
};

/**
 * Configuration for heatmap analysis
 */
struct HeatmapConfig {
    HeatmapDataType dataType = HeatmapDataType::FPS;
    HeatmapColorScheme colorScheme = HeatmapColorScheme::Viridis;
    HeatmapAggregation aggregation = HeatmapAggregation::Average;
    HeatmapTimeBin timeBin = HeatmapTimeBin::PerSecond;

    // Value range (auto if min == max)
    double valueMin = 0.0;
    double valueMax = 0.0;
    bool autoRange = true;

    // Grid dimensions
    int maxRows = 20;           // Number of value bins (vertical)
    int maxCols = 60;           // Number of time bins (horizontal)

    // Styling
    int cellWidth = 20;         // Pixels per cell (for SVG)
    int cellHeight = 20;
    int cellPadding = 1;
    bool showAxisLabels = true;
    bool showColorBar = true;
    bool showGridLines = false;
    std::string title;

    // Thresholds for Traffic color scheme
    double goodThreshold = 60.0;    // Green below this
    double warnThreshold = 45.0;    // Yellow below this, red below warn
};

/**
 * Statistics for a heatmap row (value bin)
 */
struct HeatmapRowStats {
    double binStart;       // Start of value range for this row
    double binEnd;         // End of value range
    double avgValue;       // Average value across all time bins
    int sampleCount;       // Total samples in this row
    double frequency;      // Percentage of total samples
};

/**
 * Statistics for a heatmap column (time bin)
 */
struct HeatmapColStats {
    int64_t timeStart;     // Start timestamp (microseconds)
    int64_t timeEnd;       // End timestamp
    double avgValue;       // Average value for this time slice
    double min;            // Minimum value in this time slice
    double max;            // Maximum value in this time slice
    int sampleCount;       // Total samples in this column
};

/**
 * Complete heatmap analysis result
 */
struct HeatmapResult {
    // Grid data
    std::vector<std::vector<HeatmapCell>> grid;  // [row][col]
    std::vector<HeatmapRowStats> rowStats;
    std::vector<HeatmapColStats> colStats;

    // Metadata
    HeatmapConfig config;
    int64_t startTime;        // Session start time (us)
    int64_t endTime;          // Session end time (us)
    int64_t durationMs;       // Total duration
    int totalSamples;
    int totalCells;

    // Summary statistics
    double globalMin;
    double globalMax;
    double globalAvg;
    double globalStdDev;

    // Color bar info
    std::vector<std::pair<double, std::string>> colorBar;  // value -> color

    // Insights
    std::vector<std::string> insights;  // Auto-generated observations
    std::vector<std::string> hotspots;  // Identified problem areas
};

/**
 * Comparison heatmap for two sessions
 */
struct ComparisonHeatmap {
    HeatmapResult baseline;
    HeatmapResult current;
    std::vector<std::vector<double>> deltaGrid;  // Difference values
    std::vector<std::string> significantChanges;
    double correlationScore;  // How similar the patterns are (0-1)
};

/**
 * Call stack heatmap for function profiling
 */
struct CallStackHeatmap {
    std::unordered_map<std::string, std::vector<double>> functionData;  // function -> time series
    std::vector<std::string> functions;  // Ordered function names
    std::vector<std::vector<double>> grid;  // [function][time]
    int64_t startTime;
    int64_t endTime;
};

// ─── Callback Types ───────────────────────────────────────────────────────────

using HeatmapCompleteCallback = std::function<void(const HeatmapResult&)>;

// ─── Main Class ───────────────────────────────────────────────────────────────

/**
 * HeatmapAnalyzer — generates visual heatmaps from performance data.
 *
 * Features:
 * - Multi-metric heatmap generation (FPS, memory, thermal, etc.)
 * - Configurable time binning and value ranges
 * - Multiple color schemes for different visualization needs
 * - SVG export for high-quality visualization
 * - JSON export for frontend chart libraries
 * - Auto-generated insights and hotspot detection
 * - Session comparison heatmaps
 * - Call stack heatmaps for function profiling
 *
 * Usage:
 *   auto& heatmap = ProfilerCore::GetInstance().GetHeatmapAnalyzer();
 *   heatmap.SetConfig(config);
 *   HeatmapResult result = heatmap.GenerateFPSHeatmap();
 *   std::string svg = heatmap.ExportToSVG(result);
 */
class HeatmapAnalyzer {
public:
    HeatmapAnalyzer();
    ~HeatmapAnalyzer();

    // ─── Configuration ───────────────────────────────────────────────────────

    void SetConfig(const HeatmapConfig& config);
    HeatmapConfig GetConfig() const { return m_config; }

    // ─── Data Recording ─────────────────────────────────────────────────────

    /**
     * Record a sample for heatmap generation.
     * Call this for each frame or measurement point.
     */
    void RecordSample(double value, int64_t timestamp = 0);

    /**
     * Record a sample with metric type (for multi-metric analysis).
     */
    void RecordSample(HeatmapDataType type, double value, int64_t timestamp = 0);

    /**
     * Record function profiling data for call stack heatmap.
     */
    void RecordFunctionSample(const std::string& functionName, double durationMs,
                               int64_t timestamp = 0);

    // ─── Pre-built Heatmap Generators ───────────────────────────────────────

    /** Generate FPS distribution heatmap. */
    HeatmapResult GenerateFPSHeatmap();

    /** Generate frame time distribution heatmap. */
    HeatmapResult GenerateFrameTimeHeatmap();

    /** Generate memory usage heatmap. */
    HeatmapResult GenerateMemoryHeatmap();

    /** Generate temperature heatmap (CPU/GPU). */
    HeatmapResult GenerateTemperatureHeatmap();

    /** Generate GPU utilization heatmap. */
    HeatmapResult GenerateGPUHeatmap();

    /** Generate network latency heatmap. */
    HeatmapResult GenerateNetworkLatencyHeatmap();

    /** Generate custom metric heatmap from recorded samples. */
    HeatmapResult GenerateCustomHeatmap(HeatmapDataType type);

    /** Generate call stack heatmap showing function time distribution. */
    CallStackHeatmap GenerateCallStackHeatmap(int topN = 20);

    // ─── Session Comparison ─────────────────────────────────────────────────

    /**
     * Generate comparison heatmap between two sessions.
     */
    ComparisonHeatmap CompareSessions(
        const std::vector<double>& baselineData,
        const std::vector<double>& currentData,
        int64_t baselineStart,
        int64_t currentStart
    );

    // ─── Export ─────────────────────────────────────────────────────────────

    /**
     * Export heatmap as SVG string.
     */
    std::string ExportToSVG(const HeatmapResult& result) const;

    /**
     * Export heatmap as JSON for frontend charting.
     */
    std::string ExportToJSON(const HeatmapResult& result) const;

    /**
     * Export heatmap as CSV matrix.
     */
    std::string ExportToCSV(const HeatmapResult& result) const;

    /**
     * Export as self-contained HTML page with embedded SVG.
     */
    std::string ExportToHTML(const HeatmapResult& result, const std::string& title = "") const;

    /**
     * Export call stack heatmap as SVG.
     */
    std::string ExportCallStackToSVG(const CallStackHeatmap& cs) const;

    // ─── Insights & Analysis ────────────────────────────────────────────────

    /**
     * Get auto-generated insights from the heatmap.
     */
    std::vector<std::string> GenerateInsights(const HeatmapResult& result) const;

    /**
     * Detect hotspots (problem areas) in the heatmap.
     */
    std::vector<std::string> DetectHotspots(const HeatmapResult& result) const;

    // ─── Lifecycle ──────────────────────────────────────────────────────────

    void Reset();
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    // ─── Callbacks ──────────────────────────────────────────────────────────

    void SetCompleteCallback(HeatmapCompleteCallback cb) { m_completeCallback = cb; }

private:
    // Internal helpers
    int64_t NowUs() const;
    std::string ValueToColor(double normalized, HeatmapColorScheme scheme) const;
    double ComputePercentile(std::vector<double>& values, double p) const;
    void ComputeGrid(HeatmapResult& result, const std::vector<double>& values,
                     const std::vector<int64_t>& timestamps);
    void ComputeStatistics(HeatmapResult& result);
    void ComputeColorBar(HeatmapResult& result);
    void NormalizeGrid(HeatmapResult& result);

    // Color scheme implementations
    std::string ColorViridis(double t) const;
    std::string ColorInferno(double t) const;
    std::string ColorCoolWarm(double t) const;
    std::string ColorGrayscale(double t) const;
    std::string ColorTraffic(double t, double goodThreshold, double warnThreshold) const;
    std::string ColorPlasma(double t) const;
    std::string ColorTurbo(double t) const;

    // ─── State ──────────────────────────────────────────────────────────────

    HeatmapConfig m_config;
    bool m_enabled = true;

    // Sample storage
    struct Sample {
        int64_t timestamp;
        double value;
        HeatmapDataType type;
    };
    std::vector<Sample> m_samples;

    // Function samples for call stack heatmap
    struct FunctionSample {
        int64_t timestamp;
        double durationMs;
    };
    std::unordered_map<std::string, std::vector<FunctionSample>> m_functionSamples;

    // Session timing
    int64_t m_sessionStartTime = 0;
    int64_t m_lastSampleTime = 0;

    // Callback
    HeatmapCompleteCallback m_completeCallback;
};

} // namespace ProfilerCore
