#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <cstdint>

namespace ProfilerCore {

/**
 * GPU metric types
 */
enum class GPUMetricType {
    FrameTime,         // GPU frame rendering time
    DrawCalls,         // Number of draw calls per frame
    Triangles,         // Number of triangles rendered
    TextureMemory,     // GPU texture memory usage
    ShaderComplexity,   // Estimated shader complexity score
    FillRate,          // Pixel fill rate
    VSyncWaiting,       // Time spent waiting for VSync
    DriverOverhead,     // CPU time spent in GPU driver
    ComputeLoad         // GPU compute shader load
};

/**
 * GPU metric sample
 */
struct GPUMetricSample {
    int64_t timestamp;
    GPUMetricType type;
    double value;          // Primary value
    std::string label;     // Human-readable label
};

/**
 * GPU frame statistics
 */
struct GPUFrameStats {
    int64_t timestamp;
    int frameNumber;
    
    // Core GPU timing (microseconds)
    double gpuFrameTimeUs;      // Total GPU frame time
    double gpuBusyTimeUs;       // GPU active time
    double gpuIdleTimeUs;       // GPU idle/waiting time
    
    // Derived metrics
    double gpuUtilization;      // 0-100%, GPU busy / total
    double cpuGpuOverlap;        // 0-100%, how much CPU/GPU work overlapped
    
    // Draw metrics
    int drawCalls;
    int triangles;
    int pixels;                 // Total pixels rendered
    
    // Memory metrics
    size_t textureMemoryUsed;
    size_t textureMemoryPeak;
    size_t vramTotal;
    size_t vramUsed;
    
    // Performance indicators
    double fillRateGPps;        // Gigapixels per second
    double shaderComplexity;    // Score 0-100
    double vSyncWaitMs;         // Milliseconds waiting for VSync
    double driverOverheadMs;    // CPU time in driver
    
    // Compute
    double computeLoadPercent;   // 0-100%
    
    // Stability
    double frameTimeStdDev;     // Standard deviation of recent GPU frame times
    double frameTimeConsistency; // 0-100, consistency score
};

/**
 * GPU profiling configuration
 */
struct GPUProfilerConfig {
    // Sampling
    bool enabled = true;
    size_t windowSize = 300;         // Rolling window size for stats
    
    // Thresholds
    double maxGpuFrameTimeUs = 16666.0;  // 60 FPS = 16666us
    double minGpuUtilization = 70.0;    // Below 70% = underutilized
    double maxDriverOverheadMs = 2.0;   // Above 2ms driver overhead
    double maxVSyncWaitMs = 5.0;        // Above 5ms waiting = problem
    
    // Memory thresholds (MB)
    size_t textureMemoryWarningMB = 512;
    size_t textureMemoryCriticalMB = 1024;
    size_t vramWarningPercent = 80;
    size_t vramCriticalPercent = 95;
    
    // Alert configuration
    bool alertOnUnderutilization = true;
    bool alertOnOverheating = true;
    bool alertOnMemoryPressure = true;
    int overheatingFrameCount = 300;    // 5 seconds at 60 FPS
    
    // VSync settings
    bool adaptiveVSync = true;
    double targetFps = 60.0;
};

/**
 * GPU performance alert
 */
struct GPUAlert {
    int id;
    GPUMetricType metric;
    std::string message;
    std::string details;
    int64_t timestamp;
    double value;
    double threshold;
    bool acknowledged;
    int64_t acknowledgedAt;
    int occurrenceCount;
    int64_t firstOccurrence;
    int64_t lastOccurrence;
};

/**
 * GPU alert types
 */
enum class GPUAlertType {
    HIGH_FRAME_TIME,       // GPU frame time too high
    LOW_UTILIZATION,       // GPU underutilized (bottleneck elsewhere)
    HIGH_MEMORY_PRESSURE,  // GPU memory nearly full
    DRIVER_OVERHEAD,       // Driver overhead too high
    VSYNC_WAIT,            // Excessive VSync waiting
    SHADER_COMPLEXITY,     // Shader too complex
    THERMAL_THROTTLING,    // GPU throttling detected
    DRAW_CALL_SPIKE,       // Abnormal number of draw calls
    MEMORY_LEAK           // GPU memory growing continuously
};

/**
 * Summary statistics for GPU metrics
 */
struct GPUSummaryStats {
    int64_t timestamp;
    int sampleCount;
    
    // Frame time stats (us)
    double minGpuFrameTime;
    double maxGpuFrameTime;
    double avgGpuFrameTime;
    double p50GpuFrameTime;
    double p90GpuFrameTime;
    double p95GpuFrameTime;
    double p99GpuFrameTime;
    
    // Utilization stats
    double avgGpuUtilization;
    double minGpuUtilization;
    double maxGpuUtilization;
    
    // Memory stats
    size_t peakTextureMemory;
    size_t avgTextureMemory;
    size_t peakVRAMUsed;
    size_t avgVRAMUsed;
    double memoryGrowthRate;   // bytes per frame
    
    // Draw stats
    int avgDrawCalls;
    int maxDrawCalls;
    int avgTriangles;
    int maxTriangles;
    
    // Derived metrics
    double avgFillRate;
    double avgShaderComplexity;
    double avgDriverOverhead;
    double avgVSyncWait;
    double avgComputeLoad;
    
    // Performance scores (0-100)
    double overallScore;        // Combined GPU performance score
    double efficiencyScore;     // How well GPU is utilized
    double thermalScore;        // Thermal headroom score
    
    // Alert count
    int alertCount;
};

/**
 * GPU Profiler - Tracks GPU performance metrics alongside CPU metrics
 * 
 * Features:
 * - Multi-metric GPU tracking (frame time, memory, draw calls, etc.)
 * - Rolling window analysis with percentile computation
 * - GPU utilization analysis (detect CPU/GPU bottlenecks)
 * - GPU memory tracking and leak detection
 * - Thermal throttling detection
 * - Configurable alert thresholds
 * - JSON export for dashboard integration
 * - Integration with ProfilerCore for correlated CPU/GPU analysis
 */
class GPUProfiler {
public:
    GPUProfiler();
    ~GPUProfiler();

    // Configuration
    void SetConfig(const GPUProfilerConfig& config);
    const GPUProfilerConfig& GetConfig() const { return m_config; }
    void Reset();

    // Core profiling
    void RecordFrame(const GPUFrameStats& stats);
    void RecordMetric(GPUMetricType type, double value, const std::string& label = "");
    
    // Convenience methods for common metrics
    void RecordGPUFrameTime(double gpuTimeUs, double gpuBusyUs, double gpuIdleUs);
    void RecordDrawCalls(int calls, int triangles, int pixels);
    void RecordMemoryUsage(size_t textureMem, size_t vramUsed, size_t vramTotal);
    void RecordVSyncWait(double waitMs);
    void RecordDriverOverhead(double overheadMs);
    void RecordShaderComplexity(double complexity);
    void RecordComputeLoad(double loadPercent);

    // Correlated CPU-GPU analysis
    void RecordCPUGPUCorrelation(double cpuFrameTimeMs, double gpuFrameTimeUs);
    void DetermineBottleneck(double cpuFrameTimeMs, double gpuFrameTimeUs);
    
    // Statistics
    GPUSummaryStats GetSummary() const;
    const GPUFrameStats& GetLatestFrame() const;
    std::vector<GPUFrameStats> GetRecentFrames(int count) const;
    
    // Alert management
    const std::vector<GPUAlert>& GetAlerts() const { return m_alerts; }
    std::vector<GPUAlert> GetActiveAlerts() const;
    std::vector<GPUAlert> GetUnacknowledgedAlerts() const;
    bool AcknowledgeAlert(int alertId);
    bool AcknowledgeAllAlerts();
    void ClearAlerts();
    
    // Bottleneck analysis
    std::string GetCurrentBottleneck() const;
    double GetCPUBottleneckScore() const { return m_cpuBottleneckScore; }
    double GetGPUBottleneckScore() const { return m_gpuBottleneckScore; }
    double GetMemoryBottleneckScore() const { return m_memoryBottleneckScore; }
    
    // Performance scores
    double ComputeOverallScore() const;
    double ComputeEfficiencyScore() const;
    double ComputeThermalScore() const;
    
    // Trend analysis
    bool IsFrameTimeImproving() const;
    bool IsMemoryGrowing() const;
    double GetFrameTimeTrend() const;  // positive = improving, negative = degrading
    
    // Thresholds
    void SetFrameTimeTarget(double fps);
    double GetTargetFPS() const { return m_config.targetFps; }
    
    // Export
    std::string ExportToJSON() const;
    std::string ExportSummaryToJSON() const;
    std::string ExportFramesToJSON(int limit = 100) const;
    
    // Metrics access
    const std::vector<GPUMetricSample>& GetMetricHistory(GPUMetricType type) const;
    std::vector<double> GetMetricValues(GPUMetricType type) const;

private:
    void ComputeStatistics();
    void ComputePercentiles();
    void GenerateAlerts();
    void UpdateBottleneckScores(double cpuTimeMs, double gpuTimeUs);
    void CheckThermalThrottling();
    void CheckMemoryPressure();
    void CheckUtilization();
    void CheckDriverOverhead();
    void CheckVSyncWait();
    void TrimHistory();
    
    GPUAlert CreateAlert(GPUMetricType metric, const std::string& message,
                         const std::string& details, double value, double threshold);
    void AddAlert(const GPUAlert& alert);
    
    double QuickSelectPercentile(std::vector<double>& arr, double percentile);
    double ComputeStdDev(const std::vector<double>& values, double mean) const;
    std::string MetricTypeToString(GPUMetricType type) const;
    
    int GenerateAlertId();
    int64_t GetCurrentTimestamp() const;

private:
    GPUProfilerConfig m_config;
    
    // Rolling frame data
    std::vector<GPUFrameStats> m_frameHistory;
    
    // Per-metric history
    std::unordered_map<GPUMetricType, std::vector<GPUMetricSample>> m_metricHistory;
    std::unordered_map<GPUMetricType, std::vector<double>> m_metricValues;
    
    // Computed statistics
    GPUSummaryStats m_currentStats;
    bool m_dirty = true;
    
    // Alerts
    std::vector<GPUAlert> m_alerts;
    std::unordered_map<GPUMetricType, int64_t> m_lastAlertTime;
    
    // Bottleneck analysis
    double m_cpuBottleneckScore = 0.0;     // 0-100, how much CPU limits performance
    double m_gpuBottleneckScore = 0.0;     // 0-100, how much GPU limits performance
    double m_memoryBottleneckScore = 0.0;  // 0-100, how much memory limits performance
    std::string m_currentBottleneck = "Unknown";
    
    // Thermal throttling detection
    int m_lowGpuUtilizationFrames = 0;
    bool m_thermalThrottlingSuspected = false;
    
    // Memory leak detection
    double m_memoryGrowthRate = 0.0;
    int64_t m_lastMemoryLeakAlert = 0;
    
    // Correlation data for CPU-GPU analysis
    std::vector<double> m_cpuTimes;
    std::vector<double> m_gpuTimes;
    
    // Alert ID generator
    int m_nextAlertId = 1;
    
    // Target frame time (derived from target FPS)
    double m_targetFrameTimeUs = 16666.0;  // 60 FPS default
};

} // namespace ProfilerCore
