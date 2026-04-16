#include "GPUProfiler.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <limits>

namespace ProfilerCore {

GPUProfiler::GPUProfiler() {
    Reset();
}

GPUProfiler::~GPUProfiler() {
}

void GPUProfiler::SetConfig(const GPUProfilerConfig& config) {
    m_config = config;
    m_targetFrameTimeUs = 1000000.0 / config.targetFps;
    m_dirty = true;
}

void GPUProfiler::Reset() {
    m_frameHistory.clear();
    m_metricHistory.clear();
    m_metricValues.clear();
    m_alerts.clear();
    m_lastAlertTime.clear();
    m_cpuTimes.clear();
    m_gpuTimes.clear();
    
    m_cpuBottleneckScore = 0.0;
    m_gpuBottleneckScore = 0.0;
    m_memoryBottleneckScore = 0.0;
    m_currentBottleneck = "Unknown";
    
    m_lowGpuUtilizationFrames = 0;
    m_thermalThrottlingSuspected = false;
    m_memoryGrowthRate = 0.0;
    m_lastMemoryLeakAlert = 0;
    m_nextAlertId = 1;
    m_targetFrameTimeUs = 16666.0;  // 60 FPS default
    
    m_currentStats = GPUSummaryStats();
    m_currentStats.minGpuFrameTime = 0;
    m_currentStats.maxGpuFrameTime = 0;
    m_currentStats.avgGpuFrameTime = 0;
    m_currentStats.p50GpuFrameTime = 0;
    m_currentStats.p90GpuFrameTime = 0;
    m_currentStats.p95GpuFrameTime = 0;
    m_currentStats.p99GpuFrameTime = 0;
    m_currentStats.avgGpuUtilization = 100.0;
    m_currentStats.minGpuUtilization = 100.0;
    m_currentStats.maxGpuUtilization = 100.0;
    m_currentStats.peakTextureMemory = 0;
    m_currentStats.avgTextureMemory = 0;
    m_currentStats.peakVRAMUsed = 0;
    m_currentStats.avgVRAMUsed = 0;
    m_currentStats.memoryGrowthRate = 0;
    m_currentStats.avgDrawCalls = 0;
    m_currentStats.maxDrawCalls = 0;
    m_currentStats.avgTriangles = 0;
    m_currentStats.maxTriangles = 0;
    m_currentStats.avgFillRate = 0;
    m_currentStats.avgShaderComplexity = 0;
    m_currentStats.avgDriverOverhead = 0;
    m_currentStats.avgVSyncWait = 0;
    m_currentStats.avgComputeLoad = 0;
    m_currentStats.overallScore = 100.0;
    m_currentStats.efficiencyScore = 100.0;
    m_currentStats.thermalScore = 100.0;
    m_currentStats.alertCount = 0;
    m_currentStats.sampleCount = 0;
    m_currentStats.timestamp = 0;
    
    m_dirty = false;
}

void GPUProfiler::RecordFrame(const GPUFrameStats& stats) {
    m_frameHistory.push_back(stats);
    
    if (m_frameHistory.size() > m_config.windowSize) {
        m_frameHistory.erase(m_frameHistory.begin());
    }
    
    // Also track in metric history
    RecordMetric(GPUMetricType::FrameTime, stats.gpuFrameTimeUs, "GPU Frame Time");
    RecordMetric(GPUMetricType::ShaderComplexity, stats.shaderComplexity, "Shader Complexity");
    
    m_dirty = true;
}

void GPUProfiler::RecordMetric(GPUMetricType type, double value, const std::string& label) {
    GPUMetricSample sample;
    sample.timestamp = GetCurrentTimestamp();
    sample.type = type;
    sample.value = value;
    sample.label = label;
    
    m_metricHistory[type].push_back(sample);
    m_metricValues[type].push_back(value);
    
    // Trim metric history
    if (m_metricHistory[type].size() > m_config.windowSize) {
        m_metricHistory[type].erase(m_metricHistory[type].begin());
        m_metricValues[type].erase(m_metricValues[type].begin());
    }
    
    m_dirty = true;
}

void GPUProfiler::RecordGPUFrameTime(double gpuTimeUs, double gpuBusyUs, double gpuIdleUs) {
    GPUFrameStats stats;
    stats.timestamp = GetCurrentTimestamp();
    stats.frameNumber = static_cast<int>(m_frameHistory.size()) + 1;
    stats.gpuFrameTimeUs = gpuTimeUs;
    stats.gpuBusyTimeUs = gpuBusyUs;
    stats.gpuIdleTimeUs = gpuIdleUs;
    stats.gpuUtilization = (gpuTimeUs > 0) ? (gpuBusyUs / gpuTimeUs * 100.0) : 0.0;
    
    // Compute overlap
    double totalTime = gpuBusyUs + gpuIdleUs;
    stats.cpuGpuOverlap = (totalTime > 0) ? std::min(gpuBusyUs / totalTime * 100.0, 100.0) : 0.0;
    
    RecordFrame(stats);
}

void GPUProfiler::RecordDrawCalls(int calls, int triangles, int pixels) {
    if (m_frameHistory.empty()) {
        GPUFrameStats stats;
        stats.timestamp = GetCurrentTimestamp();
        stats.frameNumber = 1;
        stats.drawCalls = calls;
        stats.triangles = triangles;
        stats.pixels = pixels;
        RecordFrame(stats);
    } else {
        GPUFrameStats& latest = m_frameHistory.back();
        latest.drawCalls = calls;
        latest.triangles = triangles;
        latest.pixels = pixels;
    }
    
    RecordMetric(GPUMetricType::DrawCalls, static_cast<double>(calls), "Draw Calls");
    RecordMetric(GPUMetricType::Triangles, static_cast<double>(triangles), "Triangles");
    RecordMetric(GPUMetricType::FillRate, static_cast<double>(pixels), "Pixels Rendered");
    
    m_dirty = true;
}

void GPUProfiler::RecordMemoryUsage(size_t textureMem, size_t vramUsed, size_t vramTotal) {
    if (m_frameHistory.empty()) {
        GPUFrameStats stats;
        stats.timestamp = GetCurrentTimestamp();
        stats.frameNumber = 1;
        stats.textureMemoryUsed = textureMem;
        stats.textureMemoryPeak = textureMem;
        stats.vramTotal = vramTotal;
        stats.vramUsed = vramUsed;
        RecordFrame(stats);
    } else {
        GPUFrameStats& latest = m_frameHistory.back();
        latest.textureMemoryUsed = textureMem;
        latest.textureMemoryPeak = std::max(latest.textureMemoryPeak, textureMem);
        latest.vramTotal = vramTotal;
        latest.vramUsed = vramUsed;
    }
    
    RecordMetric(GPUMetricType::TextureMemory, static_cast<double>(textureMem), "Texture Memory");
    
    m_dirty = true;
}

void GPUProfiler::RecordVSyncWait(double waitMs) {
    if (!m_frameHistory.empty()) {
        m_frameHistory.back().vSyncWaitMs = waitMs;
    }
    RecordMetric(GPUMetricType::VSyncWaiting, waitMs, "VSync Wait");
    m_dirty = true;
}

void GPUProfiler::RecordDriverOverhead(double overheadMs) {
    if (!m_frameHistory.empty()) {
        m_frameHistory.back().driverOverheadMs = overheadMs;
    }
    RecordMetric(GPUMetricType::DriverOverhead, overheadMs, "Driver Overhead");
    m_dirty = true;
}

void GPUProfiler::RecordShaderComplexity(double complexity) {
    if (!m_frameHistory.empty()) {
        m_frameHistory.back().shaderComplexity = complexity;
    }
    RecordMetric(GPUMetricType::ShaderComplexity, complexity, "Shader Complexity");
    m_dirty = true;
}

void GPUProfiler::RecordComputeLoad(double loadPercent) {
    if (!m_frameHistory.empty()) {
        m_frameHistory.back().computeLoadPercent = loadPercent;
    }
    RecordMetric(GPUMetricType::ComputeLoad, loadPercent, "Compute Load");
    m_dirty = true;
}

void GPUProfiler::RecordCPUGPUCorrelation(double cpuFrameTimeMs, double gpuFrameTimeUs) {
    m_cpuTimes.push_back(cpuFrameTimeMs);
    m_gpuTimes.push_back(gpuFrameTimeUs);
    
    if (m_cpuTimes.size() > m_config.windowSize) {
        m_cpuTimes.erase(m_cpuTimes.begin());
        m_gpuTimes.erase(m_gpuTimes.begin());
    }
    
    DetermineBottleneck(cpuFrameTimeMs, gpuFrameTimeUs);
}

void GPUProfiler::DetermineBottleneck(double cpuTimeMs, double gpuTimeUs) {
    double cpuTimeUs = cpuTimeMs * 1000.0;
    double totalTime = cpuTimeUs + gpuTimeUs;
    
    if (totalTime <= 0) return;
    
    // Compute bottleneck scores (0-100)
    double cpuContribution = cpuTimeUs / totalTime * 100.0;
    double gpuContribution = gpuTimeUs / totalTime * 100.0;
    
    // Smooth the scores
    m_cpuBottleneckScore = m_cpuBottleneckScore * 0.7 + cpuContribution * 0.3;
    m_gpuBottleneckScore = m_gpuBottleneckScore * 0.7 + gpuContribution * 0.3;
    
    // Determine primary bottleneck
    if (m_gpuBottleneckScore > 70.0) {
        m_currentBottleneck = "GPU";
    } else if (m_cpuBottleneckScore > 70.0) {
        m_currentBottleneck = "CPU";
    } else if (m_gpuBottleneckScore > 50.0 && m_cpuBottleneckScore > 50.0) {
        m_currentBottleneck = "CPU+GPU Balanced";
    } else {
        m_currentBottleneck = "Balanced";
    }
}

const GPUFrameStats& GPUProfiler::GetLatestFrame() const {
    static GPUFrameStats empty;
    if (m_frameHistory.empty()) return empty;
    return m_frameHistory.back();
}

std::vector<GPUFrameStats> GPUProfiler::GetRecentFrames(int count) const {
    int actual = static_cast<int>(std::min(static_cast<size_t>(count), m_frameHistory.size()));
    if (actual == 0) return {};
    return std::vector<GPUFrameStats>(
        m_frameHistory.end() - actual,
        m_frameHistory.end()
    );
}

GPUSummaryStats GPUProfiler::GetSummary() const {
    if (m_dirty) {
        const_cast<GPUProfiler*>(this)->ComputeStatistics();
    }
    return m_currentStats;
}

void GPUProfiler::ComputeStatistics() {
    if (m_frameHistory.empty()) {
        m_dirty = false;
        return;
    }
    
    size_t n = m_frameHistory.size();
    m_currentStats.sampleCount = static_cast<int>(n);
    m_currentStats.timestamp = GetCurrentTimestamp();
    
    // GPU frame time stats
    std::vector<double> gpuTimes;
    gpuTimes.reserve(n);
    for (const auto& f : m_frameHistory) {
        gpuTimes.push_back(f.gpuFrameTimeUs);
    }
    
    m_currentStats.minGpuFrameTime = *std::min_element(gpuTimes.begin(), gpuTimes.end());
    m_currentStats.maxGpuFrameTime = *std::max_element(gpuTimes.begin(), gpuTimes.end());
    m_currentStats.avgGpuFrameTime = std::accumulate(gpuTimes.begin(), gpuTimes.end(), 0.0) / n;
    
    // GPU utilization stats
    std::vector<double> utilizations;
    for (const auto& f : m_frameHistory) {
        utilizations.push_back(f.gpuUtilization);
    }
    m_currentStats.avgGpuUtilization = std::accumulate(utilizations.begin(), utilizations.end(), 0.0) / n;
    m_currentStats.minGpuUtilization = *std::min_element(utilizations.begin(), utilizations.end());
    m_currentStats.maxGpuUtilization = *std::max_element(utilizations.begin(), utilizations.end());
    
    // Memory stats
    std::vector<size_t> texMems, vramUsed;
    for (const auto& f : m_frameHistory) {
        texMems.push_back(f.textureMemoryUsed);
        vramUsed.push_back(f.vramUsed);
    }
    m_currentStats.peakTextureMemory = *std::max_element(texMems.begin(), texMems.end());
    m_currentStats.avgTextureMemory = std::accumulate(texMems.begin(), texMems.end(), 0ULL) / n;
    m_currentStats.peakVRAMUsed = *std::max_element(vramUsed.begin(), vramUsed.end());
    m_currentStats.avgVRAMUsed = std::accumulate(vramUsed.begin(), vramUsed.end(), 0ULL) / n;
    
    // Memory growth rate (linear regression)
    if (n > 1) {
        double xMean = static_cast<double>(n - 1) / 2.0;
        double yMean = static_cast<double>(std::accumulate(texMems.begin(), texMems.end(), 0ULL)) / n;
        double numerator = 0.0;
        double denominator = 0.0;
        for (size_t i = 0; i < n; i++) {
            double xDiff = static_cast<double>(i) - xMean;
            double yDiff = static_cast<double>(texMems[i]) - yMean;
            numerator += xDiff * yDiff;
            denominator += xDiff * xDiff;
        }
        m_memoryGrowthRate = (denominator > 0) ? (numerator / denominator) : 0.0;
        m_currentStats.memoryGrowthRate = m_memoryGrowthRate;
    }
    
    // Draw call stats
    std::vector<int> drawCalls, triangles;
    for (const auto& f : m_frameHistory) {
        drawCalls.push_back(f.drawCalls);
        triangles.push_back(f.triangles);
    }
    m_currentStats.avgDrawCalls = static_cast<int>(std::accumulate(drawCalls.begin(), drawCalls.end(), 0LL) / n);
    m_currentStats.maxDrawCalls = *std::max_element(drawCalls.begin(), drawCalls.end());
    m_currentStats.avgTriangles = static_cast<int>(std::accumulate(triangles.begin(), triangles.end(), 0LL) / n);
    m_currentStats.maxTriangles = *std::max_element(triangles.begin(), triangles.end());
    
    // Other metrics averages
    double sumFillRate = 0, sumShader = 0, sumDriver = 0, sumVSync = 0, sumCompute = 0;
    for (const auto& f : m_frameHistory) {
        sumFillRate += f.fillRateGPps;
        sumShader += f.shaderComplexity;
        sumDriver += f.driverOverheadMs;
        sumVSync += f.vSyncWaitMs;
        sumCompute += f.computeLoadPercent;
    }
    m_currentStats.avgFillRate = sumFillRate / n;
    m_currentStats.avgShaderComplexity = sumShader / n;
    m_currentStats.avgDriverOverhead = sumDriver / n;
    m_currentStats.avgVSyncWait = sumVSync / n;
    m_currentStats.avgComputeLoad = sumCompute / n;
    
    // Frame time consistency
    double meanFt = m_currentStats.avgGpuFrameTime;
    double stdDev = ComputeStdDev(gpuTimes, meanFt);
    m_currentStats.frameTimeStdDev = stdDev;
    // Consistency: if stdDev is low relative to mean, score is high
    double cv = (meanFt > 0) ? (stdDev / meanFt) : 0;
    m_currentStats.frameTimeConsistency = std::max(0.0, std::min(100.0, 100.0 * (1.0 - cv * 2.0)));
    
    // Percentiles
    ComputePercentiles();
    
    // Performance scores
    m_currentStats.overallScore = ComputeOverallScore();
    m_currentStats.efficiencyScore = ComputeEfficiencyScore();
    m_currentStats.thermalScore = ComputeThermalScore();
    
    // Generate alerts
    GenerateAlerts();
    m_currentStats.alertCount = static_cast<int>(m_alerts.size());
    
    m_dirty = false;
}

void GPUProfiler::ComputePercentiles() {
    if (m_frameHistory.empty()) return;
    
    std::vector<double> sortedTimes;
    sortedTimes.reserve(m_frameHistory.size());
    for (const auto& f : m_frameHistory) {
        sortedTimes.push_back(f.gpuFrameTimeUs);
    }
    std::sort(sortedTimes.begin(), sortedTimes.end());
    
    size_t n = sortedTimes.size();
    
    auto getPercentile = [&](double p) -> double {
        if (n == 0) return 0.0;
        double idx = (p / 100.0) * (static_cast<double>(n) - 1.0);
        size_t lower = static_cast<size_t>(std::floor(idx));
        size_t upper = static_cast<size_t>(std::ceil(idx));
        if (lower == upper || upper >= n) return sortedTimes[lower];
        double fraction = idx - static_cast<double>(lower);
        return sortedTimes[lower] * (1.0 - fraction) + sortedTimes[upper] * fraction;
    };
    
    m_currentStats.p50GpuFrameTime = getPercentile(50.0);
    m_currentStats.p90GpuFrameTime = getPercentile(90.0);
    m_currentStats.p95GpuFrameTime = getPercentile(95.0);
    m_currentStats.p99GpuFrameTime = getPercentile(99.0);
}

double GPUProfiler::ComputeStdDev(const std::vector<double>& values, double mean) const {
    if (values.size() < 2) return 0.0;
    double sumSq = 0.0;
    for (double v : values) {
        double diff = v - mean;
        sumSq += diff * diff;
    }
    return std::sqrt(sumSq / static_cast<double>(values.size()));
}

void GPUProfiler::GenerateAlerts() {
    m_alerts.clear();
    
    // High GPU frame time alert
    if (m_currentStats.avgGpuFrameTime > m_config.maxGpuFrameTimeUs) {
        AddAlert(CreateAlert(
            GPUMetricType::FrameTime,
            "High GPU frame time: " + std::to_string(static_cast<int>(m_currentStats.avgGpuFrameTime / 1000.0)) + "ms",
            "Average GPU frame time exceeds target " + std::to_string(static_cast<int>(m_config.maxGpuFrameTimeUs / 1000.0)) + "ms",
            m_currentStats.avgGpuFrameTime,
            m_config.maxGpuFrameTimeUs
        ));
    }
    
    // Low GPU utilization alert
    if (m_config.alertOnUnderutilization && m_currentStats.avgGpuUtilization < m_config.minGpuUtilization) {
        AddAlert(CreateAlert(
            GPUMetricType::FrameTime,
            "Low GPU utilization: " + std::to_string(static_cast<int>(m_currentStats.avgGpuUtilization)) + "%",
            "GPU is underutilized. Likely a CPU or memory bottleneck. Bottleneck: " + m_currentBottleneck,
            m_currentStats.avgGpuUtilization,
            m_config.minGpuUtilization
        ));
    }
    
    // Check individual alert conditions
    CheckUtilization();
    CheckMemoryPressure();
    CheckDriverOverhead();
    CheckVSyncWait();
    CheckThermalThrottling();
}

void GPUProfiler::CheckUtilization() {
    if (m_currentStats.avgGpuUtilization < 50.0) {
        m_lowGpuUtilizationFrames++;
    } else {
        m_lowGpuUtilizationFrames = std::max(0, m_lowGpuUtilizationFrames - 1);
    }
    
    if (m_lowGpuUtilizationFrames >= 60) {  // 1 second at 60 FPS
        AddAlert(CreateAlert(
            GPUMetricType::FrameTime,
            "Prolonged low GPU utilization: " + std::to_string(m_lowGpuUtilizationFrames) + " frames",
            "GPU utilization has been below 50% for extended period. This indicates a non-GPU bottleneck.",
            static_cast<double>(m_lowGpuUtilizationFrames),
            60.0
        ));
        m_lowGpuUtilizationFrames = 0;
    }
}

void GPUProfiler::CheckMemoryPressure() {
    if (!m_config.alertOnMemoryPressure) return;
    
    size_t peakMB = m_currentStats.peakTextureMemory / (1024 * 1024);
    size_t avgMB = m_currentStats.avgTextureMemory / (1024 * 1024);
    
    if (peakMB > m_config.textureMemoryCriticalMB) {
        AddAlert(CreateAlert(
            GPUMetricType::TextureMemory,
            "Critical GPU memory usage: " + std::to_string(static_cast<int>(peakMB)) + " MB",
            "Texture memory usage exceeds critical threshold. Risk of OOM or thrashing.",
            static_cast<double>(peakMB),
            static_cast<double>(m_config.textureMemoryCriticalMB)
        ));
    } else if (peakMB > m_config.textureMemoryWarningMB) {
        AddAlert(CreateAlert(
            GPUMetricType::TextureMemory,
            "High GPU memory usage: " + std::to_string(static_cast<int>(peakMB)) + " MB",
            "Texture memory usage is elevated. Consider reducing texture quality or streaming.",
            static_cast<double>(peakMB),
            static_cast<double>(m_config.textureMemoryWarningMB)
        ));
    }
    
    // VRAM pressure
    if (!m_frameHistory.empty()) {
        const auto& latest = m_frameHistory.back();
        if (latest.vramTotal > 0) {
            double vramPercent = static_cast<double>(latest.vramUsed) / latest.vramTotal * 100.0;
            if (vramPercent > m_config.vramCriticalPercent) {
                AddAlert(CreateAlert(
                    GPUMetricType::TextureMemory,
                    "Critical VRAM pressure: " + std::to_string(static_cast<int>(vramPercent)) + "% used",
                    "VRAM is nearly full. This can cause severe performance degradation and texture swapping.",
                    vramPercent,
                    static_cast<double>(m_config.vramCriticalPercent)
                ));
            }
        }
    }
}

void GPUProfiler::CheckDriverOverhead() {
    if (m_currentStats.avgDriverOverhead > m_config.maxDriverOverheadMs) {
        AddAlert(CreateAlert(
            GPUMetricType::DriverOverhead,
            "High driver overhead: " + std::to_string(m_currentStats.avgDriverOverhead) + "ms",
            "CPU time spent in GPU driver is elevated. Consider reducing draw calls or using instancing.",
            m_currentStats.avgDriverOverhead,
            m_config.maxDriverOverheadMs
        ));
    }
}

void GPUProfiler::CheckVSyncWait() {
    if (m_currentStats.avgVSyncWait > m_config.maxVSyncWaitMs) {
        AddAlert(CreateAlert(
            GPUMetricType::VSyncWaiting,
            "High VSync wait time: " + std::to_string(m_currentStats.avgVSyncWait) + "ms",
            "Excessive time waiting for VSync. Consider enabling adaptive VSync or disabling VSync.",
            m_currentStats.avgVSyncWait,
            m_config.maxVSyncWaitMs
        ));
    }
}

void GPUProfiler::CheckThermalThrottling() {
    if (!m_config.alertOnOverheating) return;
    
    // Detection: sustained low GPU utilization with high GPU frame time
    if (m_currentStats.avgGpuUtilization < 40.0 && 
        m_currentStats.avgGpuFrameTime > m_targetFrameTimeUs * 1.5) {
        if (!m_thermalThrottlingSuspected) {
            m_thermalThrottlingSuspected = true;
        }
    } else {
        m_thermalThrottlingSuspected = false;
    }
    
    if (m_thermalThrottlingSuspected) {
        int64_t now = GetCurrentTimestamp();
        // Check last alert time for deduplication
        auto it = m_lastAlertTime.find(GPUMetricType::FrameTime);
        if (it == m_lastAlertTime.end() || (now - it->second) > 60000) {  // 60s cooldown
            AddAlert(CreateAlert(
                GPUMetricType::FrameTime,
                "Possible thermal throttling detected",
                "Low GPU utilization combined with high frame times may indicate thermal throttling. "
                "GPU temperature management or power limits may be reducing performance.",
                m_currentStats.avgGpuUtilization,
                40.0
            ));
        }
    }
}

GPUAlert GPUProfiler::CreateAlert(GPUMetricType metric, const std::string& message,
                                   const std::string& details, double value, double threshold) {
    int64_t now = GetCurrentTimestamp();
    
    // Deduplication
    auto it = m_lastAlertTime.find(metric);
    if (it != m_lastAlertTime.end() && (now - it->second) < 5000) {
        return GPUAlert{};  // Empty alert = suppressed
    }
    
    GPUAlert alert;
    alert.id = GenerateAlertId();
    alert.metric = metric;
    alert.message = message;
    alert.details = details;
    alert.timestamp = now;
    alert.value = value;
    alert.threshold = threshold;
    alert.acknowledged = false;
    alert.acknowledgedAt = 0;
    alert.occurrenceCount = 1;
    alert.firstOccurrence = now;
    alert.lastOccurrence = now;
    
    return alert;
}

void GPUProfiler::AddAlert(const GPUAlert& alert) {
    if (alert.id == 0) return;  // Empty/skip alert
    m_alerts.push_back(alert);
    m_lastAlertTime[alert.metric] = alert.timestamp;
    
    // Limit history
    while (m_alerts.size() > 100) {
        m_alerts.erase(m_alerts.begin());
    }
}

std::vector<GPUAlert> GPUProfiler::GetActiveAlerts() const {
    std::vector<GPUAlert> active;
    int64_t cutoff = GetCurrentTimestamp() - 30000;
    
    for (const auto& a : m_alerts) {
        if (!a.acknowledged && a.timestamp > cutoff) {
            active.push_back(a);
        }
    }
    return active;
}

std::vector<GPUAlert> GPUProfiler::GetUnacknowledgedAlerts() const {
    std::vector<GPUAlert> result;
    for (const auto& a : m_alerts) {
        if (!a.acknowledged) {
            result.push_back(a);
        }
    }
    return result;
}

bool GPUProfiler::AcknowledgeAlert(int alertId) {
    for (auto& a : m_alerts) {
        if (a.id == alertId && !a.acknowledged) {
            a.acknowledged = true;
            a.acknowledgedAt = GetCurrentTimestamp();
            return true;
        }
    }
    return false;
}

bool GPUProfiler::AcknowledgeAllAlerts() {
    bool any = false;
    for (auto& a : m_alerts) {
        if (!a.acknowledged) {
            a.acknowledged = true;
            a.acknowledgedAt = GetCurrentTimestamp();
            any = true;
        }
    }
    return any;
}

void GPUProfiler::ClearAlerts() {
    m_alerts.clear();
    m_lastAlertTime.clear();
}

std::string GPUProfiler::GetCurrentBottleneck() const {
    return m_currentBottleneck;
}

double GPUProfiler::ComputeOverallScore() const {
    if (m_frameHistory.empty()) return 100.0;
    
    // Score based on GPU frame time vs target
    double targetTime = m_targetFrameTimeUs;
    double avgTime = m_currentStats.avgGpuFrameTime;
    
    double timeScore;
    if (avgTime <= targetTime) {
        timeScore = 100.0;
    } else if (avgTime <= targetTime * 1.5) {
        timeScore = 100.0 - (avgTime / targetTime - 1.0) * 100.0;
    } else if (avgTime <= targetTime * 2.0) {
        timeScore = 50.0 - (avgTime / targetTime - 1.5) * 50.0;
    } else {
        timeScore = std::max(0.0, 25.0 - (avgTime / targetTime - 2.0) * 12.5);
    }
    timeScore = std::max(0.0, std::min(100.0, timeScore));
    
    // Utilization bonus/penalty
    double utilScore = m_currentStats.avgGpuUtilization;
    
    // Consistency bonus
    double consistencyScore = m_currentStats.frameTimeConsistency;
    
    // Weighted combination
    double overall = timeScore * 0.5 + utilScore * 0.3 + consistencyScore * 0.2;
    
    return std::max(0.0, std::min(100.0, overall));
}

double GPUProfiler::ComputeEfficiencyScore() const {
    if (m_frameHistory.empty()) return 100.0;
    
    // Efficiency: how well is the GPU being used relative to available power
    double utilScore = m_currentStats.avgGpuUtilization;
    
    // Driver overhead penalty
    double overheadPenalty = std::min(20.0, m_currentStats.avgDriverOverhead * 5.0);
    
    // VSync wait penalty
    double vsyncPenalty = std::min(15.0, m_currentStats.avgVSyncWait * 3.0);
    
    double efficiency = utilScore - overheadPenalty - vsyncPenalty;
    
    return std::max(0.0, std::min(100.0, efficiency));
}

double GPUProfiler::ComputeThermalScore() const {
    if (m_frameHistory.empty()) return 100.0;
    
    // Thermal score based on GPU utilization pattern
    // Sustained high utilization = good thermal headroom
    // Low utilization + high frame time = thermal throttling suspected
    
    double baseScore = 100.0;
    
    // Penalize thermal throttling suspicion
    if (m_thermalThrottlingSuspected) {
        baseScore -= 30.0;
    }
    
    // Penalize if utilization is inconsistent (potential temp fluctuations)
    double utilConsistency = 100.0 - (m_currentStats.maxGpuUtilization - m_currentStats.minGpuUtilization);
    baseScore *= (utilConsistency / 100.0);
    
    return std::max(0.0, std::min(100.0, baseScore));
}

bool GPUProfiler::IsFrameTimeImproving() const {
    if (m_frameHistory.size() < 30) return false;
    
    size_t mid = m_frameHistory.size() / 2;
    double earlyAvg = 0, lateAvg = 0;
    
    for (size_t i = 0; i < mid; i++) {
        earlyAvg += m_frameHistory[i].gpuFrameTimeUs;
    }
    earlyAvg /= mid;
    
    for (size_t i = mid; i < m_frameHistory.size(); i++) {
        lateAvg += m_frameHistory[i].gpuFrameTimeUs;
    }
    lateAvg /= (m_frameHistory.size() - mid);
    
    // Improving = later frames are faster (lower frame time)
    return lateAvg < earlyAvg * 0.95;
}

bool GPUProfiler::IsMemoryGrowing() const {
    return m_memoryGrowthRate > 0.1 * 1024 * 1024;  // More than 100KB/frame growth
}

double GPUProfiler::GetFrameTimeTrend() const {
    if (m_frameHistory.size() < 10) return 0.0;
    
    // Simple linear regression on frame times
    size_t n = m_frameHistory.size();
    double xMean = static_cast<double>(n - 1) / 2.0;
    double ySum = 0.0;
    for (const auto& f : m_frameHistory) {
        ySum += f.gpuFrameTimeUs;
    }
    double yMean = ySum / n;
    
    double numerator = 0.0, denominator = 0.0;
    for (size_t i = 0; i < n; i++) {
        double xDiff = static_cast<double>(i) - xMean;
        double yDiff = m_frameHistory[i].gpuFrameTimeUs - yMean;
        numerator += xDiff * yDiff;
        denominator += xDiff * xDiff;
    }
    
    // Slope > 0 means degrading (slower), < 0 means improving (faster)
    return (denominator > 0) ? (numerator / denominator) : 0.0;
}

void GPUProfiler::SetFrameTimeTarget(double fps) {
    m_config.targetFps = fps;
    m_targetFrameTimeUs = 1000000.0 / fps;
    m_config.maxGpuFrameTimeUs = m_targetFrameTimeUs;
    m_dirty = true;
}

const std::vector<GPUMetricSample>& GPUProfiler::GetMetricHistory(GPUMetricType type) const {
    static const std::vector<GPUMetricSample> empty;
    auto it = m_metricHistory.find(type);
    if (it == m_metricHistory.end()) return empty;
    return it->second;
}

std::vector<double> GPUProfiler::GetMetricValues(GPUMetricType type) const {
    auto it = m_metricValues.find(type);
    if (it == m_metricValues.end()) return {};
    return it->second;
}

std::string GPUProfiler::MetricTypeToString(GPUMetricType type) const {
    switch (type) {
        case GPUMetricType::FrameTime: return "FrameTime";
        case GPUMetricType::DrawCalls: return "DrawCalls";
        case GPUMetricType::Triangles: return "Triangles";
        case GPUMetricType::TextureMemory: return "TextureMemory";
        case GPUMetricType::ShaderComplexity: return "ShaderComplexity";
        case GPUMetricType::FillRate: return "FillRate";
        case GPUMetricType::VSyncWaiting: return "VSyncWaiting";
        case GPUMetricType::DriverOverhead: return "DriverOverhead";
        case GPUMetricType::ComputeLoad: return "ComputeLoad";
        default: return "Unknown";
    }
}

double GPUProfiler::QuickSelectPercentile(std::vector<double>& arr, double percentile) {
    // Simplified: just sort and pick
    std::sort(arr.begin(), arr.end());
    size_t n = arr.size();
    if (n == 0) return 0.0;
    double idx = (percentile / 100.0) * (n - 1);
    size_t lower = static_cast<size_t>(std::floor(idx));
    size_t upper = static_cast<size_t>(std::ceil(idx));
    if (lower == upper || upper >= n) return arr[lower];
    double fraction = idx - lower;
    return arr[lower] * (1.0 - fraction) + arr[upper] * fraction;
}

int GPUProfiler::GenerateAlertId() {
    return m_nextAlertId++;
}

int64_t GPUProfiler::GetCurrentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

std::string GPUProfiler::ExportToJSON() const {
    std::ostringstream ss;
    ss << "{\"gpuProfiler\":{";
    ss << "\"timestamp\":" << GetCurrentTimestamp() << ",";
    ss << "\"bottleneck\":\"" << m_currentBottleneck << "\",";
    ss << "\"cpuBottleneckScore\":" << std::fixed << std::setprecision(1) << m_cpuBottleneckScore << ",";
    ss << "\"gpuBottleneckScore\":" << m_gpuBottleneckScore << ",";
    ss << "\"memoryBottleneckScore\":" << m_memoryBottleneckScore << ",";
    ss << "\"summary\":" << ExportSummaryToJSON() << ",";
    ss << "\"frames\":" << ExportFramesToJSON(100) << "}}";
    return ss.str();
}

std::string GPUProfiler::ExportSummaryToJSON() const {
    const auto& s = m_currentStats;
    std::ostringstream ss;
    ss << "{";
    ss << "\"timestamp\":" << s.timestamp << ",";
    ss << "\"sampleCount\":" << s.sampleCount << ",";
    ss << "\"gpuFrameTime\":{";
    ss << "\"min\":" << std::fixed << std::setprecision(2) << s.minGpuFrameTime << ",";
    ss << "\"max\":" << s.maxGpuFrameTime << ",";
    ss << "\"avg\":" << s.avgGpuFrameTime << ",";
    ss << "\"p50\":" << s.p50GpuFrameTime << ",";
    ss << "\"p90\":" << s.p90GpuFrameTime << ",";
    ss << "\"p95\":" << s.p95GpuFrameTime << ",";
    ss << "\"p99\":" << s.p99GpuFrameTime << "},";
    ss << "\"gpuUtilization\":{";
    ss << "\"avg\":" << s.avgGpuUtilization << ",";
    ss << "\"min\":" << s.minGpuUtilization << ",";
    ss << "\"max\":" << s.maxGpuUtilization << "},";
    ss << "\"memory\":{";
    ss << "\"peakTextureMB\":" << (s.peakTextureMemory / 1024 / 1024) << ",";
    ss << "\"avgTextureMB\":" << (s.avgTextureMemory / 1024 / 1024) << ",";
    ss << "\"peakVRAMMB\":" << (s.peakVRAMUsed / 1024 / 1024) << ",";
    ss << "\"avgVRAMMB\":" << (s.avgVRAMUsed / 1024 / 1024) << ",";
    ss << "\"growthRateBytesPerFrame\":" << s.memoryGrowthRate << "},";
    ss << "\"drawCalls\":{";
    ss << "\"avg\":" << s.avgDrawCalls << ",";
    ss << "\"max\":" << s.maxDrawCalls << "},";
    ss << "\"triangles\":{";
    ss << "\"avg\":" << s.avgTriangles << ",";
    ss << "\"max\":" << s.maxTriangles << "},";
    ss << "\"performance\":{";
    ss << "\"overallScore\":" << std::setprecision(1) << s.overallScore << ",";
    ss << "\"efficiencyScore\":" << s.efficiencyScore << ",";
    ss << "\"thermalScore\":" << s.thermalScore << ",";
    ss << "\"consistency\":" << s.frameTimeConsistency << "},";
    ss << "\"other\":{";
    ss << "\"avgFillRateGPps\":" << std::setprecision(3) << s.avgFillRate << ",";
    ss << "\"avgShaderComplexity\":" << s.avgShaderComplexity << ",";
    ss << "\"avgDriverOverheadMs\":" << s.avgDriverOverhead << ",";
    ss << "\"avgVSyncWaitMs\":" << s.avgVSyncWait << ",";
    ss << "\"avgComputeLoadPercent\":" << s.avgComputeLoad << "},";
    ss << "\"alertCount\":" << s.alertCount << "}";
    return ss.str();
}

std::string GPUProfiler::ExportFramesToJSON(int limit) const {
    int count = static_cast<int>(std::min(static_cast<size_t>(limit), m_frameHistory.size()));
    if (count == 0) return "[]";
    
    std::ostringstream ss;
    ss << "[";
    
    int start = static_cast<int>(m_frameHistory.size()) - count;
    for (int i = start; i < static_cast<int>(m_frameHistory.size()); i++) {
        const auto& f = m_frameHistory[i];
        ss << "{";
        ss << "\"frame\":" << f.frameNumber << ",";
        ss << "\"gpuFrameTimeUs\":" << std::fixed << std::setprecision(2) << f.gpuFrameTimeUs << ",";
        ss << "\"gpuUtilization\":" << f.gpuUtilization << ",";
        ss << "\"drawCalls\":" << f.drawCalls << ",";
        ss << "\"triangles\":" << f.triangles << ",";
        ss << "\"textureMemoryMB\":" << (f.textureMemoryUsed / 1024 / 1024) << ",";
        ss << "\"vramUsedMB\":" << (f.vramUsed / 1024 / 1024);
        ss << "}";
        if (i < static_cast<int>(m_frameHistory.size()) - 1) ss << ",";
    }
    
    ss << "]";
    return ss.str();
}

} // namespace ProfilerCore
