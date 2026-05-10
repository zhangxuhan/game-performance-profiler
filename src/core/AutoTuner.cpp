#include "AutoTuner.h"
#include "ProfilerCore.h"
#include "StatisticsAnalyzer.h"
#include "GPUProfiler.h"
#include "MemoryAnalyzer.h"
#include "NetworkProfiler.h"
#include "ThermalMonitor.h"
#include "JitterAnalyzer.h"
#include "FrameSpikeAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace ProfilerCore {

// ─── Construction ────────────────────────────────────────────────────────────

AutoTuner::AutoTuner()
    : m_nextRecId(1)
{
}

AutoTuner::~AutoTuner() {
}

// ─── Configuration ────────────────────────────────────────────────────────────

void AutoTuner::Configure(const AutoTunerConfig& config) {
    m_config = config;
    UpdateBundles();
}

void AutoTuner::SetTargetPlatform(TargetPlatform platform) {
    m_config.platform = platform;
}

// ─── Main Analysis ────────────────────────────────────────────────────────────

void AutoTuner::Analyze() {
    // Clear previous non-implemented/dismissed recommendations
    std::vector<OptimizationRecommendation> kept;
    for (const auto& r : m_recommendations) {
        if (!r.isImplemented && !r.isDismissed) {
            // Keep for potential update
        } else if (r.isImplemented && m_config.includeImplemented) {
            kept.push_back(r);
        } else if (r.isDismissed && m_config.includeDismissed) {
            kept.push_back(r);
        }
    }
    m_recommendations = kept;
    m_recIndex.clear();
    for (size_t i = 0; i < m_recommendations.size(); i++) {
        m_recIndex[m_recommendations[i].id] = i;
    }

    // Analyze all categories
    AnalyzeFrameTime();
    AnalyzeGPU();
    AnalyzeMemory();
    AnalyzeNetwork();
    AnalyzeThermal();
    AnalyzeStability();
    AnalyzeBottlenecks();
    AnalyzeRendering();
    AnalyzeThreading();

    // Sort by priority
    if (m_config.sortByPriority) {
        std::sort(m_recommendations.begin(), m_recommendations.end(),
            [](const OptimizationRecommendation& a, const OptimizationRecommendation& b) {
                if (static_cast<int>(a.priority) != static_cast<int>(b.priority))
                    return static_cast<int>(a.priority) < static_cast<int>(b.priority);
                return static_cast<int>(a.estimatedImpact) > static_cast<int>(b.estimatedImpact);
            });
    }

    // Cap recommendations
    if (m_recommendations.size() > m_config.maxRecommendations) {
        m_recommendations.resize(m_config.maxRecommendations);
    }

    // Update bundle index
    for (size_t i = 0; i < m_recommendations.size(); i++) {
        m_recIndex[m_recommendations[i].id] = i;
    }

    UpdateBundles();

    // Fire callbacks
    for (const auto& rec : m_recommendations) {
        if (m_recCallback && !rec.isAcknowledged) {
            m_recCallback(rec);
        }
    }

    // Session tracking
    if (m_activeSessionId > 0 && !m_sessions.empty()) {
        m_sessions.back().totalRecommendations = static_cast<int>(m_recommendations.size());
        m_sessions.back().criticalRecommendations = CountCritical();
    }
}

void AutoTuner::AnalyzeCategory(OptimizationCategory category) {
    switch (category) {
        case OptimizationCategory::Rendering: AnalyzeRendering(); break;
        case OptimizationCategory::Memory:    AnalyzeMemory();    break;
        case OptimizationCategory::CPU:       AnalyzeFrameTime(); break;
        case OptimizationCategory::GPU:        AnalyzeGPU();        break;
        case OptimizationCategory::Network:    AnalyzeNetwork();    break;
        case OptimizationCategory::Threading:  AnalyzeThreading();  break;
        case OptimizationCategory::Assets:    AnalyzeStability();  break;
        case OptimizationCategory::General:   Analyze();           break;
    }
    UpdateBundles();
}

void AutoTuner::RecordMetric(const std::string& name, double value,
                              double threshold, const std::string& sourceModule) {
    CustomMetric m;
    m.name = name;
    m.value = value;
    m.threshold = threshold;
    m.sourceModule = sourceModule;
    m.timestamp = GetCurrentTimestamp();
    m_customMetrics.push_back(m);

    // Auto-generate recommendation if threshold crossed
    if (value > threshold) {
        double severity = (threshold > 0) ? (value - threshold) / threshold : 1.0;
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::General,
            severity > 0.5 ? OptimizationPriority::High : OptimizationPriority::Medium,
            "Custom metric threshold exceeded: " + name,
            name + " = " + std::to_string(value) + " (threshold: " + std::to_string(threshold) + ")",
            "Metric " + name + " exceeded configured threshold",
            "Fix or adjust threshold for " + name,
            sourceModule,
            name,
            value,
            threshold
        );
        AddRecommendation(rec);
    }
}

// ─── Analysis Helpers ─────────────────────────────────────────────────────────

void AutoTuner::AnalyzeFrameTime() {
    auto& core = ProfilerCore::GetInstance();
    auto analyzer = core.GetAnalyzer();
    if (!analyzer) return;

    SummaryStats stats = analyzer->GetSummary();
    double targetFps = GetTargetFPS();
    double targetFrameTime = 1000.0 / targetFps;
    double severity;

    // Low FPS recommendation
    if (stats.avgFps < targetFps * 0.85) {
        severity = (targetFps - stats.avgFps) / targetFps;
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::CPU,
            stats.avgFps < targetFps * 0.6 ? OptimizationPriority::Critical : OptimizationPriority::High,
            "Average FPS below target",
            "Average FPS is " + std::to_string(stats.avgFps) + " (target: " + std::to_string(targetFps) + ")",
            "Frame time averages " + std::to_string(stats.avgFrameTime) + "ms vs target " + std::to_string(targetFrameTime) + "ms",
            "Expect " + std::to_string(targetFps - stats.avgFps) + " FPS improvement by addressing CPU bottlenecks",
            "StatisticsAnalyzer",
            "avgFps",
            stats.avgFps,
            targetFps * 0.85
        );
        rec.estimatedImpact = EstimateImpact(targetFps - stats.avgFps, OptimizationCategory::CPU);
        AddRecommendation(rec);
    }

    // P99 FPS (1% low) analysis
    if (stats.p99Fps < targetFps * 0.5) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::CPU,
            OptimizationPriority::High,
            "Severe 1% low FPS degradation",
            "P99 FPS is " + std::to_string(stats.p99Fps) + " (target: " + std::to_string(targetFps) + ")",
            "10% of frames are slower than " + std::to_string(1000.0 / stats.p99Fps) + "ms",
            "Target P99 improvement to " + std::to_string(targetFps * 0.7) + "+ for smoother experience",
            "StatisticsAnalyzer",
            "p99Fps",
            stats.p99Fps,
            targetFps * 0.5
        );
        rec.estimatedImpact = EstimateImpact(targetFps - stats.p99Fps, OptimizationCategory::CPU);
        AddRecommendation(rec);
    }

    // High frame time variance
    if (stats.stdDevFrameTime > targetFrameTime * 0.5) {
        double cv = stats.stdDevFrameTime / stats.avgFrameTime;
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::CPU,
            cv > 1.0 ? OptimizationPriority::High : OptimizationPriority::Medium,
            "High frame time variance",
            "Frame time std dev is " + std::to_string(stats.stdDevFrameTime) + "ms (CV: " + std::to_string(cv) + ")",
            "Inconsistent frame times affect perceived smoothness",
            "Reduce variance for more consistent frame pacing",
            "StatisticsAnalyzer",
            "stdDevFrameTime",
            stats.stdDevFrameTime,
            targetFrameTime * 0.5
        );
        rec.estimatedImpact = ImpactLevel::Moderate;
        AddRecommendation(rec);
    }

    // Stability score
    if (stats.stabilityScore < 60.0) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::CPU,
            stats.stabilityScore < 40.0 ? OptimizationPriority::High : OptimizationPriority::Medium,
            "Low frame rate stability score",
            "Stability score: " + std::to_string(stats.stabilityScore) + "/100",
            "Frame rate varies significantly during gameplay",
            "Target stability > 80 for consistent experience",
            "StatisticsAnalyzer",
            "stabilityScore",
            stats.stabilityScore,
            60.0
        );
        rec.estimatedImpact = stats.stabilityScore < 40.0 ? ImpactLevel::Significant : ImpactLevel::Moderate;
        AddRecommendation(rec);
    }
}

void AutoTuner::AnalyzeGPU() {
    auto& core = ProfilerCore::GetInstance();
    auto gpu = core.GetGPUProfiler();
    if (!gpu) return;

    GPUSummaryStats stats = gpu->GetSummary();
    double targetFrameTimeUs = 1000.0 / GetTargetFPS() * 1000.0; // ms -> us

    // High GPU frame time
    if (stats.avgGpuFrameTime > targetFrameTimeUs * 1.2) {
        double overrun = (stats.avgGpuFrameTime - targetFrameTimeUs) / targetFrameTimeUs;
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::GPU,
            overrun > 0.5 ? OptimizationPriority::Critical : OptimizationPriority::High,
            "GPU frame time exceeds budget",
            "Average GPU time: " + std::to_string(stats.avgGpuFrameTime / 1000.0) + "ms (target: " + std::to_string(targetFrameTimeUs / 1000.0) + "ms)",
            "GPU is the primary bottleneck",
            "Reduce shader complexity, draw calls, or texture resolution",
            "GPUProfiler",
            "avgGpuFrameTime",
            stats.avgGpuFrameTime,
            targetFrameTimeUs * 1.2
        );
        rec.estimatedImpact = overrun > 0.5 ? ImpactLevel::Major : ImpactLevel::Significant;
        AddRecommendation(rec);
    }

    // GPU underutilization
    if (stats.avgGpuUtilization < m_config.minGpuBottleneckThreshold * 100.0) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::GPU,
            OptimizationPriority::Medium,
            "GPU underutilized - CPU bottleneck suspected",
            "GPU utilization: " + std::to_string(stats.avgGpuUtilization) + "%",
            "GPU has spare capacity; CPU or data delivery is limiting performance",
            "Investigate CPU-side bottlenecks, draw call overhead, or asset streaming",
            "GPUProfiler",
            "avgGpuUtilization",
            stats.avgGpuUtilization,
            m_config.minGpuBottleneckThreshold * 100.0
        );
        rec.estimatedImpact = ImpactLevel::Moderate;
        AddRecommendation(rec);
    }

    // High driver overhead
    if (stats.avgDriverOverhead > 2.0) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::GPU,
            OptimizationPriority::Medium,
            "High GPU driver overhead detected",
            "Driver overhead: " + std::to_string(stats.avgDriverOverhead) + "ms",
            "CPU is spending significant time in GPU driver calls",
            "Reduce draw call count, use instancing, or batch state changes",
            "GPUProfiler",
            "avgDriverOverhead",
            stats.avgDriverOverhead,
            2.0
        );
        rec.estimatedImpact = ImpactLevel::Moderate;
        AddRecommendation(rec);
    }

    // VSync waiting
    if (stats.avgVSyncWait > m_config.maxVSyncWaitMs) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::GPU,
            OptimizationPriority::Medium,
            "Excessive VSync wait time",
            "VSync wait: " + std::to_string(stats.avgVSyncWait) + "ms (threshold: " + std::to_string(m_config.maxVSyncWaitMs) + "ms)",
            "Frame arrives early but waits for display refresh",
            "Consider adaptive VSync, reduce frame rate target, or optimize render order",
            "GPUProfiler",
            "avgVSyncWait",
            stats.avgVSyncWait,
            m_config.maxVSyncWaitMs
        );
        rec.estimatedImpact = ImpactLevel::Minor;
        AddRecommendation(rec);
    }

    // GPU memory pressure
    if (stats.peakVRAMUsed > 0 && stats.avgVRAMUsed > stats.peakVRAMUsed * 0.9) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::GPU,
            OptimizationPriority::High,
            "High GPU memory utilization",
            "VRAM usage: " + std::to_string(stats.avgVRAMUsed / (1024 * 1024)) + "MB / " + std::to_string(stats.peakVRAMUsed / (1024 * 1024)) + "MB",
            "GPU memory is nearly saturated",
            "Reduce texture quality, use mipmaps, or implement texture streaming",
            "GPUProfiler",
            "avgVRAMUsed",
            static_cast<double>(stats.avgVRAMUsed),
            static_cast<double>(stats.peakVRAMUsed * 0.9)
        );
        rec.estimatedImpact = ImpactLevel::Moderate;
        AddRecommendation(rec);
    }
}

void AutoTuner::AnalyzeMemory() {
    auto& core = ProfilerCore::GetInstance();
    auto mem = core.GetMemoryAnalyzer();
    if (!mem) return;

    MemoryReport report = mem->GenerateReport();

    // Memory leak
    if (report.trend.hasLeak) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Memory,
            report.trend.leakConfidence > 70 ? OptimizationPriority::Critical : OptimizationPriority::High,
            "Memory leak detected",
            "Estimated leak: " + std::to_string(report.trend.estimatedLeakSize / (1024 * 1024)) + "MB",
            "Memory usage growing continuously without corresponding deallocation",
            "Identify and fix leaking allocations; use memory profiling to pinpoint source",
            "MemoryAnalyzer",
            "trend.hasLeak",
            static_cast<double>(report.trend.estimatedLeakSize),
            0.0
        );
        rec.estimatedImpact = ImpactLevel::Significant;
        AddRecommendation(rec);
    }

    // Memory pressure
    if (report.pressure == MemoryPressure::High || report.pressure == MemoryPressure::Critical) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Memory,
            report.pressure == MemoryPressure::Critical ? OptimizationPriority::Critical : OptimizationPriority::High,
            "High system memory pressure",
            "Memory pressure level: " + std::to_string(static_cast<int>(report.pressure)),
            "System is running low on available memory",
            "Reduce memory footprint, implement object pooling, or optimize asset loading",
            "MemoryAnalyzer",
            "pressure",
            static_cast<double>(static_cast<int>(report.pressure)),
            static_cast<double>(static_cast<int>(MemoryPressure::Medium))
        );
        rec.estimatedImpact = report.pressure == MemoryPressure::Critical ? ImpactLevel::Major : ImpactLevel::Significant;
        AddRecommendation(rec);
    }

    // High watermark
    if (report.trend.hasHighWatermark) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Memory,
            OptimizationPriority::Medium,
            "Memory usage approaching budget limit",
            "Peak usage: " + std::to_string(report.peakMemory / (1024 * 1024)) + "MB",
            "Memory usage is near the configured limit",
            "Monitor memory growth, implement LRU cache for assets",
            "MemoryAnalyzer",
            "peakMemory",
            static_cast<double>(report.peakMemory),
            0.0
        );
        rec.estimatedImpact = ImpactLevel::Moderate;
        AddRecommendation(rec);
    }

    // OOM prediction
    if (report.trend.projectedOOMTime > 0 && report.trend.projectedOOMTime < 300000) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Memory,
            OptimizationPriority::Critical,
            "Imminent out-of-memory condition predicted",
            "OOM predicted in " + std::to_string(static_cast<int>(report.trend.projectedOOMTime / 60)) + " minutes",
            "Memory leak rate will exhaust available memory soon",
            "Find and fix memory leak immediately; add emergency memory cleanup",
            "MemoryAnalyzer",
            "projectedOOMTime",
            report.trend.projectedOOMTime,
            300000.0
        );
        rec.estimatedImpact = ImpactLevel::Major;
        AddRecommendation(rec);
    }

    // Fragmentation
    if (report.trend.fragmentationScore > 50) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Memory,
            report.trend.fragmentationScore > 75 ? OptimizationPriority::High : OptimizationPriority::Medium,
            "Memory fragmentation detected",
            "Fragmentation score: " + std::to_string(report.trend.fragmentationScore) + "/100",
            "Memory is fragmented, reducing effective capacity",
            "Implement memory defragmentation, use slab allocators, or adjust allocation strategy",
            "MemoryAnalyzer",
            "fragmentationScore",
            static_cast<double>(report.trend.fragmentationScore),
            50.0
        );
        rec.estimatedImpact = ImpactLevel::Minor;
        AddRecommendation(rec);
    }
}

void AutoTuner::AnalyzeNetwork() {
    auto& core = ProfilerCore::GetInstance();
    auto net = core.GetNetworkProfiler();
    if (!net) return;

    NetworkReport report = net->GenerateReport();

    // High latency
    if (report.latency.avgRttMs > m_config.targetRttMs * 1.5) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Network,
            report.latency.avgRttMs > m_config.targetRttMs * 3.0 ? OptimizationPriority::High : OptimizationPriority::Medium,
            "High network round-trip latency",
            "Average RTT: " + std::to_string(report.latency.avgRttMs) + "ms (target: " + std::to_string(m_config.targetRttMs) + "ms)",
            "Network latency is affecting game responsiveness",
            "Optimize network protocol, use prediction/interpolation, or reduce sync frequency",
            "NetworkProfiler",
            "avgRttMs",
            report.latency.avgRttMs,
            m_config.targetRttMs * 1.5
        );
        rec.estimatedImpact = EstimateImpact(50.0 - report.latency.avgRttMs, OptimizationCategory::Network);
        AddRecommendation(rec);
    }

    // High jitter
    if (report.latency.maxJitterMs > m_config.maxJitterMs * 1.5) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Network,
            OptimizationPriority::Medium,
            "High network latency variance",
            "Max jitter: " + std::to_string(report.latency.maxJitterMs) + "ms",
            "Inconsistent latency causes rubber-banding and stuttering",
            "Implement client-side prediction, use larger interpolation buffers",
            "NetworkProfiler",
            "maxJitterMs",
            report.latency.maxJitterMs,
            m_config.maxJitterMs * 1.5
        );
        rec.estimatedImpact = ImpactLevel::Minor;
        AddRecommendation(rec);
    }

    // Bandwidth
    if (report.bandwidth.peakDownloadBps > m_config.bandwidthWarningBps) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Network,
            report.bandwidth.peakDownloadBps > m_config.bandwidthCriticalBps ? OptimizationPriority::High : OptimizationPriority::Medium,
            "High bandwidth consumption detected",
            "Peak download: " + std::to_string(report.bandwidth.peakDownloadBps / (1024.0 * 1024.0)) + " MB/s",
            "Excessive data being downloaded, may cause lag on slower connections",
            "Compress data, reduce asset download size, or implement adaptive quality",
            "NetworkProfiler",
            "peakDownloadBps",
            report.bandwidth.peakDownloadBps,
            m_config.bandwidthWarningBps
        );
        rec.estimatedImpact = ImpactLevel::Minor;
        AddRecommendation(rec);
    }

    // Quality
    if (report.quality == NetworkQuality::Poor || report.quality == NetworkQuality::Critical) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Network,
            OptimizationPriority::High,
            "Poor network quality",
            "Quality rating: " + std::to_string(static_cast<int>(report.quality)) + "/4",
            "Network conditions are degrading player experience",
            "Prioritize network optimization, consider server region selection",
            "NetworkProfiler",
            "quality",
            static_cast<double>(static_cast<int>(report.quality)),
            static_cast<double>(static_cast<int>(NetworkQuality::Fair))
        );
        rec.estimatedImpact = ImpactLevel::Significant;
        AddRecommendation(rec);
    }
}

void AutoTuner::AnalyzeThermal() {
    auto& core = ProfilerCore::GetInstance();
    auto thermal = core.GetThermalMonitor();
    if (!thermal) return;

    auto snapshot = thermal->GetSnapshot();

    // High CPU temperature
    if (snapshot.cpuPackage > m_config.cpuTempWarningC) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::CPU,
            snapshot.cpuPackage > m_config.cpuTempCriticalC ? OptimizationPriority::Critical : OptimizationPriority::High,
            "High CPU temperature detected",
            "CPU package temperature: " + std::to_string(snapshot.cpuPackage) + "C",
            "CPU is overheating; thermal throttling may be active",
            "Reduce CPU workload, improve cooling, or cap frame rate",
            "ThermalMonitor",
            "cpuPackage",
            snapshot.cpuPackage,
            m_config.cpuTempWarningC
        );
        rec.estimatedImpact = snapshot.cpuPackage > m_config.cpuTempCriticalC ? ImpactLevel::Significant : ImpactLevel::Moderate;
        AddRecommendation(rec);
    }

    // High GPU temperature
    if (snapshot.gpuCore > m_config.gpuTempWarningC) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::GPU,
            snapshot.gpuCore > m_config.gpuTempCriticalC ? OptimizationPriority::Critical : OptimizationPriority::High,
            "High GPU temperature detected",
            "GPU core temperature: " + std::to_string(snapshot.gpuCore) + "C",
            "GPU is overheating; thermal throttling will reduce performance",
            "Reduce render quality, improve case airflow, or lower frame rate target",
            "ThermalMonitor",
            "gpuCore",
            snapshot.gpuCore,
            m_config.gpuTempWarningC
        );
        rec.estimatedImpact = snapshot.gpuCore > m_config.gpuTempCriticalC ? ImpactLevel::Significant : ImpactLevel::Moderate;
        AddRecommendation(rec);
    }

    // Thermal throttling detected
    if (snapshot.throttleType != ThrottleType::None) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::CPU,
            OptimizationPriority::Critical,
            "Thermal throttling is active",
            "Throttle type: " + std::to_string(static_cast<int>(snapshot.throttleType)),
            "CPU/GPU performance is being reduced to manage heat",
            "Immediate action needed: reduce workload or improve cooling",
            "ThermalMonitor",
            "throttleType",
            static_cast<double>(snapshot.throttleType),
            0.0
        );
        rec.estimatedImpact = ImpactLevel::Major;
        AddRecommendation(rec);
    }
}

void AutoTuner::AnalyzeStability() {
    auto& core = ProfilerCore::GetInstance();

    // Frame spike analysis
    auto spike = core.GetFrameSpikeAnalyzer();
    if (spike) {
        auto stats = spike->GetStatistics();

        if (stats.totalSpikes > 0) {
            if (stats.severeSpikes > 20 || stats.majorSpikes > 30) {
                OptimizationRecommendation rec = CreateRecommendation(
                    OptimizationCategory::CPU,
                    OptimizationPriority::Critical,
                    "Frequent severe frame spikes detected",
                    std::to_string(stats.severeSpikes) + " severe, " + std::to_string(stats.majorSpikes) + " major spikes",
                    "Multiple frames experienced severe hitches",
                    "Profile individual systems; common causes are GC, asset loading, shader compile",
                    "FrameSpikeAnalyzer",
                    "severeSpikes",
                    static_cast<double>(stats.severeSpikes),
                    20.0
                );
                rec.estimatedImpact = ImpactLevel::Significant;
                AddRecommendation(rec);
            }

            // Pattern-based recommendations
            auto patterns = spike->GetPatterns();
            for (const auto& pattern : patterns) {
                if (pattern.occurrenceCount >= 3) {
                    OptimizationRecommendation rec = CreateRecommendation(
                        OptimizationCategory::CPU,
                        pattern.occurrenceCount >= 10 ? OptimizationPriority::High : OptimizationPriority::Medium,
                        "Recurring frame spike pattern: " + SpikeCauseToString(pattern.cause),
                        "Pattern occurs every ~" + std::to_string(static_cast<int>(pattern.avgIntervalMs / 16.67)) + " frames",
                        std::to_string(pattern.occurrenceCount) + " occurrences detected, each ~" + std::to_string(pattern.avgFrameTimeMs) + "ms",
                        pattern.recommendations.empty() ? "Profile and fix the recurring bottleneck" : pattern.recommendations[0],
                        "FrameSpikeAnalyzer",
                        "recurringPattern",
                        static_cast<double>(pattern.occurrenceCount),
                        3.0
                    );
                    rec.estimatedImpact = pattern.occurrenceCount >= 10 ? ImpactLevel::Significant : ImpactLevel::Moderate;
                    AddRecommendation(rec);
                }
            }
        }
    }

    // Jitter analysis
    // (Could add jitter-based recommendations similarly)
}

void AutoTuner::AnalyzeBottlenecks() {
    auto& core = ProfilerCore::GetInstance();
    auto gpu = core.GetGPUProfiler();
    if (!gpu) return;

    double cpuScore = gpu->GetCPUBottleneckScore();
    double gpuScore = gpu->GetGPUBottleneckScore();
    double memScore = gpu->GetMemoryBottleneckScore();

    std::string bottleneck = gpu->GetCurrentBottleneck();

    if (bottleneck == "CPU" && cpuScore > m_config.cpuBottleneckThreshold) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::CPU,
            OptimizationPriority::High,
            "CPU is the primary performance bottleneck",
            "Bottleneck score: " + std::to_string(cpuScore * 100) + "%",
            "CPU-side work is limiting frame rate",
            "Optimize game logic, use job system, reduce draw call overhead",
            "GPUProfiler",
            "cpuBottleneckScore",
            cpuScore,
            m_config.cpuBottleneckThreshold
        );
        rec.estimatedImpact = cpuScore > 0.7 ? ImpactLevel::Major : ImpactLevel::Significant;
        AddRecommendation(rec);
    }

    if (bottleneck == "GPU" && gpuScore > m_config.gpuBottleneckThreshold) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::GPU,
            OptimizationPriority::High,
            "GPU is the primary performance bottleneck",
            "Bottleneck score: " + std::to_string(gpuScore * 100) + "%",
            "GPU rendering is the limiting factor",
            "Reduce shader complexity, draw calls, texture resolution, or enable FSR/DLSS",
            "GPUProfiler",
            "gpuBottleneckScore",
            gpuScore,
            m_config.gpuBottleneckThreshold
        );
        rec.estimatedImpact = gpuScore > 0.7 ? ImpactLevel::Major : ImpactLevel::Significant;
        AddRecommendation(rec);
    }

    if (bottleneck == "Memory" && memScore > 0.5) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Memory,
            OptimizationPriority::Medium,
            "Memory bandwidth is a bottleneck",
            "Memory bottleneck score: " + std::to_string(memScore * 100) + "%",
            "Memory bandwidth is limiting data transfer to GPU",
            "Use simpler texture formats, reduce texture overfetch, or enable texture compression",
            "GPUProfiler",
            "memoryBottleneckScore",
            memScore,
            0.5
        );
        rec.estimatedImpact = ImpactLevel::Moderate;
        AddRecommendation(rec);
    }
}

void AutoTuner::AnalyzeRendering() {
    auto& core = ProfilerCore::GetInstance();
    auto gpu = core.GetGPUProfiler();
    if (!gpu) return;

    GPUSummaryStats stats = gpu->GetSummary();

    // High draw calls
    if (stats.avgDrawCalls > 2000) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Rendering,
            stats.avgDrawCalls > 5000 ? OptimizationPriority::High : OptimizationPriority::Medium,
            "High draw call count detected",
            "Average draw calls: " + std::to_string(stats.avgDrawCalls),
            "Too many draw calls cause CPU bottleneck in driver",
            "Use instancing, combine meshes, implement draw call batching",
            "GPUProfiler",
            "avgDrawCalls",
            static_cast<double>(stats.avgDrawCalls),
            2000.0
        );
        rec.estimatedImpact = stats.avgDrawCalls > 5000 ? ImpactLevel::Significant : ImpactLevel::Moderate;
        AddRecommendation(rec);
    }

    // High triangle count
    if (stats.maxTriangles > 10000000) { // 10M
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Rendering,
            OptimizationPriority::Medium,
            "High triangle count",
            "Max triangles per frame: " + std::to_string(stats.maxTriangles / 1000000) + "M",
            "Excessive geometry is overloading the GPU",
            "Implement LOD system, cull off-screen geometry, or reduce mesh complexity",
            "GPUProfiler",
            "maxTriangles",
            static_cast<double>(stats.maxTriangles),
            10000000.0
        );
        rec.estimatedImpact = ImpactLevel::Moderate;
        AddRecommendation(rec);
    }

    // High shader complexity
    if (stats.avgShaderComplexity > 70.0) {
        OptimizationRecommendation rec = CreateRecommendation(
            OptimizationCategory::Rendering,
            stats.avgShaderComplexity > 85.0 ? OptimizationPriority::High : OptimizationPriority::Medium,
            "High shader complexity score",
            "Shader complexity: " + std::to_string(stats.avgShaderComplexity) + "/100",
            "Complex shaders are limiting GPU throughput",
            "Simplify shaders, use cheaper approximations, reduce instruction count",
            "GPUProfiler",
            "avgShaderComplexity",
            stats.avgShaderComplexity,
            70.0
        );
        rec.estimatedImpact = stats.avgShaderComplexity > 85.0 ? ImpactLevel::Significant : ImpactLevel::Moderate;
        AddRecommendation(rec);
    }
}

void AutoTuner::AnalyzeThreading() {
    auto& core = ProfilerCore::GetInstance();

    // Could analyze thread utilization patterns
    // For now, rely on the other analyzers to surface threading issues
    // through their recommendations (e.g., driver overhead -> draw call batching)

    // Placeholder for future thread analysis:
    // - Thread load imbalance detection
    // - Job queue contention analysis
    // - Synchronization overhead measurement
}

// ─── Recommendation Management ────────────────────────────────────────────────

void AutoTuner::AddRecommendation(const OptimizationRecommendation& rec) {
    // Check if this is an update to an existing recommendation (same sourceMetric)
    for (auto& existing : m_recommendations) {
        if (existing.sourceModule == rec.sourceModule &&
            existing.sourceMetric == rec.sourceMetric &&
            !existing.isImplemented && !existing.isDismissed) {
            // Update existing
            if (rec.priority < existing.priority ||
                rec.sourceValue > existing.sourceValue) {
                existing.title = rec.title;
                existing.description = rec.description;
                existing.rationale = rec.rationale;
                existing.expectedOutcome = rec.expectedOutcome;
                existing.sourceValue = rec.sourceValue;
                existing.threshold = rec.threshold;
                existing.lastUpdated = GetCurrentTimestamp();
                existing.estimatedImpact = rec.estimatedImpact;
                if (rec.priority < existing.priority) {
                    existing.priority = rec.priority;
                }
            }
            return;
        }
    }

    // New recommendation
    OptimizationRecommendation newRec = rec;
    newRec.id = GenerateRecommendationId();
    newRec.createdAt = GetCurrentTimestamp();
    newRec.lastUpdated = newRec.createdAt;
    newRec.acknowledgedAt = 0;
    newRec.isAcknowledged = false;
    newRec.isImplemented = false;
    newRec.implementedAt = 0;
    newRec.isDismissed = false;
    newRec.preImplementationValue = rec.sourceValue;
    newRec.postImplementationValue = 0.0;
    newRec.effectivenessVerified = false;
    newRec.relatedAlertCount = 0;

    m_recommendations.push_back(newRec);
    m_recIndex[newRec.id] = m_recommendations.size() - 1;

    if (m_recCallback) {
        m_recCallback(newRec);
    }
}

std::vector<OptimizationRecommendation> AutoTuner::GetRecommendationsByCategory(
    OptimizationCategory category) const
{
    std::vector<OptimizationRecommendation> result;
    for (const auto& r : m_recommendations) {
        if (r.category == category && !r.isDismissed) {
            result.push_back(r);
        }
    }
    return result;
}

std::vector<OptimizationRecommendation> AutoTuner::GetRecommendationsByPriority(
    OptimizationPriority priority) const
{
    std::vector<OptimizationRecommendation> result;
    for (const auto& r : m_recommendations) {
        if (r.priority == priority && !r.isDismissed) {
            result.push_back(r);
        }
    }
    return result;
}

std::vector<OptimizationRecommendation> AutoTuner::GetPendingRecommendations() const {
    std::vector<OptimizationRecommendation> result;
    for (const auto& r : m_recommendations) {
        if (!r.isAcknowledged && !r.isImplemented && !r.isDismissed) {
            result.push_back(r);
        }
    }
    return result;
}

std::vector<OptimizationRecommendation> AutoTuner::GetImplementedRecommendations() const {
    std::vector<OptimizationRecommendation> result;
    for (const auto& r : m_recommendations) {
        if (r.isImplemented) {
            result.push_back(r);
        }
    }
    return result;
}

std::vector<RecommendationBundle> AutoTuner::GetBundles() const {
    return m_bundles;
}

std::vector<OptimizationRecommendation> AutoTuner::GetTopRecommendations(int count) const {
    std::vector<OptimizationRecommendation> result = m_recommendations;
    std::sort(result.begin(), result.end(),
        [](const OptimizationRecommendation& a, const OptimizationRecommendation& b) {
            if (static_cast<int>(a.priority) != static_cast<int>(b.priority))
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            return static_cast<int>(a.estimatedImpact) > static_cast<int>(b.estimatedImpact);
        });
    if (static_cast<int>(result.size()) > count) {
        result.resize(count);
    }
    return result;
}

OptimizationRecommendation AutoTuner::GetRecommendation(int64_t id) const {
    auto it = m_recIndex.find(id);
    if (it != m_recIndex.end() && it->second < m_recommendations.size()) {
        return m_recommendations[it->second];
    }
    return OptimizationRecommendation{};
}

bool AutoTuner::AcknowledgeRecommendation(int64_t id) {
    auto it = m_recIndex.find(id);
    if (it != m_recIndex.end() && it->second < m_recommendations.size()) {
        m_recommendations[it->second].isAcknowledged = true;
        m_recommendations[it->second].acknowledgedAt = GetCurrentTimestamp();
        return true;
    }
    return false;
}

bool AutoTuner::MarkAsImplemented(int64_t id, double postImplementationValue) {
    auto it = m_recIndex.find(id);
    if (it != m_recIndex.end() && it->second < m_recommendations.size()) {
        auto& rec = m_recommendations[it->second];
        rec.isImplemented = true;
        rec.implementedAt = GetCurrentTimestamp();
        rec.postImplementationValue = postImplementationValue;
        rec.effectivenessVerified = (postImplementationValue < rec.preImplementationValue);
        return true;
    }
    return false;
}

bool AutoTuner::DismissRecommendation(int64_t id) {
    auto it = m_recIndex.find(id);
    if (it != m_recIndex.end() && it->second < m_recommendations.size()) {
        m_recommendations[it->second].isDismissed = true;
        return true;
    }
    return false;
}

void AutoTuner::ResetTracking() {
    for (auto& rec : m_recommendations) {
        rec.isAcknowledged = false;
        rec.acknowledgedAt = 0;
        rec.isDismissed = false;
    }
}

// ─── Sessions ────────────────────────────────────────────────────────────────

int64_t AutoTuner::StartSession() {
    AutoTunerSession session;
    session.sessionId = GetCurrentTimestamp();
    session.startTime = GetCurrentTimestamp();
    session.endTime = 0;
    session.totalFramesAnalyzed = 0;
    session.targetPlatform = m_config.platform;
    session.totalRecommendations = 0;
    session.criticalRecommendations = 0;
    session.implementedCount = 0;
    session.averageImpactScore = 0.0;
    session.preSessionScore = ComputeOverallScore();
    session.postSessionScore = session.preSessionScore;
    session.scoreImprovement = 0.0;

    m_sessions.push_back(session);
    m_activeSessionId = session.sessionId;
    m_sessionStartScore = session.preSessionScore;

    if (m_sessionCallback) {
        m_sessionCallback(session);
    }

    return session.sessionId;
}

void AutoTuner::EndSession() {
    if (m_activeSessionId == 0 || m_sessions.empty()) return;

    auto& session = m_sessions.back();
    session.endTime = GetCurrentTimestamp();
    session.postSessionScore = ComputeOverallScore();
    session.scoreImprovement = session.postSessionScore - session.preSessionScore;
    session.totalRecommendations = static_cast<int>(m_recommendations.size());
    session.criticalRecommendations = CountCritical();

    double totalImpact = 0.0;
    int implemented = 0;
    for (const auto& rec : m_recommendations) {
        if (rec.isImplemented) {
            implemented++;
        }
        totalImpact += static_cast<double>(rec.estimatedImpact);
    }
    session.implementedCount = implemented;
    session.averageImpactScore = m_recommendations.empty() ? 0.0 : totalImpact / m_recommendations.size();

    m_activeSessionId = 0;
}

AutoTunerSession AutoTuner::GetCurrentSession() const {
    if (m_activeSessionId > 0 && !m_sessions.empty()) {
        return m_sessions.back();
    }
    return AutoTunerSession{};
}

// ─── Sessions - Helper ────────────────────────────────────────────────────────

int AutoTuner::CountCritical() const {
    int count = 0;
    for (const auto& rec : m_recommendations) {
        if (rec.priority == OptimizationPriority::Critical && !rec.isDismissed) {
            count++;
        }
    }
    return count;
}

// ─── Bundle Update ────────────────────────────────────────────────────────────

void AutoTuner::UpdateBundles() {
    m_bundles.clear();

    // Group by category
    std::unordered_map<OptimizationCategory, std::vector<const OptimizationRecommendation*>> groups;
    for (const auto& rec : m_recommendations) {
        if (!rec.isDismissed) {
            groups[rec.category].push_back(&rec);
        }
    }

    // Build bundles
    for (auto& pair : groups) {
        RecommendationBundle bundle;
        bundle.category = pair.first;
        bundle.categoryTitle = CategoryToString(pair.first);

        int critical = 0, high = 0;
        double impactSum = 0.0;

        for (const auto* ptr : pair.second) {
            bundle.recommendations.push_back(*ptr);
            if (ptr->priority == OptimizationPriority::Critical) critical++;
            if (ptr->priority == OptimizationPriority::High) high++;
            impactSum += static_cast<double>(ptr->estimatedImpact);
        }

        bundle.criticalCount = critical;
        bundle.highCount = high;
        bundle.totalImpactScore = impactSum;

        // Summary
        std::ostringstream ss;
        ss << critical << " critical, " << high << " high priority items; "
           << "estimated impact: " << static_cast<int>(impactSum);
        bundle.summary = ss.str();

        bundle.isActionable = true; // Simplified

        m_bundles.push_back(bundle);

        if (m_bundleCallback) {
            m_bundleCallback(bundle);
        }
    }

    // Sort bundles by total impact
    std::sort(m_bundles.begin(), m_bundles.end(),
        [](const RecommendationBundle& a, const RecommendationBundle& b) {
            return a.totalImpactScore > b.totalImpactScore;
        });
}

// ─── Export ────────────────────────────────────────────────────────────────────

std::string AutoTuner::ExportRecommendationsToJSON() const {
    std::ostringstream ss;
    ss << "{\"recommendations\":[";
    for (size_t i = 0; i < m_recommendations.size(); i++) {
        const auto& r = m_recommendations[i];
        ss << "{\"id\":" << r.id;
        ss << ",\"category\":" << static_cast<int>(r.category);
        ss << ",\"priority\":" << static_cast<int>(r.priority);
        ss << ",\"impact\":" << static_cast<int>(r.estimatedImpact);
        ss << ",\"title\":\"" << r.title << "\"";
        ss << ",\"description\":\"" << r.description << "\"";
        ss << ",\"rationale\":\"" << r.rationale << "\"";
        ss << ",\"expectedOutcome\":\"" << r.expectedOutcome << "\"";
        ss << ",\"sourceModule\":\"" << r.sourceModule << "\"";
        ss << ",\"sourceMetric\":\"" << r.sourceMetric << "\"";
        ss << ",\"sourceValue\":" << r.sourceValue;
        ss << ",\"threshold\":" << r.threshold;
        ss << ",\"createdAt\":" << r.createdAt;
        ss << ",\"isAcknowledged\":" << (r.isAcknowledged ? "true" : "false");
        ss << ",\"isImplemented\":" << (r.isImplemented ? "true" : "false");
        ss << ",\"isDismissed\":" << (r.isDismissed ? "true" : "false");
        ss << ",\"difficulty\":\"" << r.difficulty << "\"";
        ss << ",\"estimatedTime\":\"" << r.estimatedTime << "\"";
        ss << "}";
        if (i < m_recommendations.size() - 1) ss << ",";
    }
    ss << "],\"totalCount\":" << m_recommendations.size() << "}";
    return ss.str();
}

std::string AutoTuner::ExportBundlesToJSON() const {
    std::ostringstream ss;
    ss << "{\"bundles\":[";
    for (size_t i = 0; i < m_bundles.size(); i++) {
        const auto& b = m_bundles[i];
        ss << "{\"category\":" << static_cast<int>(b.category);
        ss << ",\"title\":\"" << b.categoryTitle << "\"";
        ss << ",\"summary\":\"" << b.summary << "\"";
        ss << ",\"recCount\":" << b.recommendations.size();
        ss << ",\"criticalCount\":" << b.criticalCount;
        ss << ",\"highCount\":" << b.highCount;
        ss << ",\"totalImpactScore\":" << b.totalImpactScore;
        ss << ",\"isActionable\":" << (b.isActionable ? "true" : "false");
        ss << "}";
        if (i < m_bundles.size() - 1) ss << ",";
    }
    ss << "]}";
    return ss.str();
}

std::string AutoTuner::ExportSessionsToJSON() const {
    std::ostringstream ss;
    ss << "{\"sessions\":[";
    for (size_t i = 0; i < m_sessions.size(); i++) {
        const auto& s = m_sessions[i];
        ss << "{\"sessionId\":" << s.sessionId;
        ss << ",\"startTime\":" << s.startTime;
        ss << ",\"endTime\":" << s.endTime;
        ss << ",\"totalFramesAnalyzed\":" << s.totalFramesAnalyzed;
        ss << ",\"platform\":" << static_cast<int>(s.targetPlatform);
        ss << ",\"totalRecommendations\":" << s.totalRecommendations;
        ss << ",\"criticalRecommendations\":" << s.criticalRecommendations;
        ss << ",\"implementedCount\":" << s.implementedCount;
        ss << ",\"preSessionScore\":" << std::fixed << std::setprecision(1) << s.preSessionScore;
        ss << ",\"postSessionScore\":" << s.postSessionScore;
        ss << ",\"scoreImprovement\":" << s.scoreImprovement;
        ss << "}";
        if (i < m_sessions.size() - 1) ss << ",";
    }
    ss << "]}";
    return ss.str();
}

std::string AutoTuner::ExportFullReport() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"generatedAt\":" << GetCurrentTimestamp() << ",\n";
    ss << "  \"overallScore\":" << std::fixed << std::setprecision(1) << ComputeOverallScore() << ",\n";
    ss << "  \"config\":{\n";
    ss << "    \"targetPlatform\":" << static_cast<int>(m_config.platform) << ",\n";
    ss << "    \"targetFps\":" << GetTargetFPS() << "\n";
    ss << "  },\n";

    // Summary
    ss << "  \"summary\":{\n";
    int critical = 0, high = 0, medium = 0, low = 0;
    for (const auto& r : m_recommendations) {
        if (r.isDismissed) continue;
        switch (r.priority) {
            case OptimizationPriority::Critical: critical++; break;
            case OptimizationPriority::High:     high++;     break;
            case OptimizationPriority::Medium:   medium++;   break;
            case OptimizationPriority::Low:     low++;      break;
        }
    }
    ss << "    \"total\":" << m_recommendations.size() << ",\n";
    ss << "    \"critical\":" << critical << ",\n";
    ss << "    \"high\":" << high << ",\n";
    ss << "    \"medium\":" << medium << ",\n";
    ss << "    \"low\":" << low << "\n";
    ss << "  },\n";

    // Bundles
    ss << "  \"bundles\":";
    ss << ExportBundlesToJSON() << ",\n";

    // Recommendations
    ss << "  \"recommendations\":";
    ss << ExportRecommendationsToJSON() << "\n";

    ss << "}\n";
    return ss.str();
}

// ─── Reset ───────────────────────────────────────────────────────────────────

void AutoTuner::ClearRecommendations() {
    m_recommendations.clear();
    m_recIndex.clear();
    m_bundles.clear();
}

void AutoTuner::Reset() {
    m_recommendations.clear();
    m_recIndex.clear();
    m_bundles.clear();
    m_sessions.clear();
    m_activeSessionId = 0;
    m_customMetrics.clear();
    m_nextRecId = 1;
}

// ─── Private Helpers ──────────────────────────────────────────────────────────

OptimizationRecommendation AutoTuner::CreateRecommendation(
    OptimizationCategory category,
    OptimizationPriority priority,
    const std::string& title,
    const std::string& description,
    const std::string& rationale,
    const std::string& expectedOutcome,
    const std::string& sourceModule,
    const std::string& sourceMetric,
    double sourceValue,
    double threshold) const
{
    OptimizationRecommendation rec;
    rec.id = 0; // Will be assigned by AddRecommendation
    rec.category = category;
    rec.priority = priority;
    rec.estimatedImpact = ImpactLevel::Unknown;
    rec.title = title;
    rec.description = description;
    rec.rationale = rationale;
    rec.expectedOutcome = expectedOutcome;
    rec.sourceModule = sourceModule;
    rec.sourceMetric = sourceMetric;
    rec.sourceValue = sourceValue;
    rec.threshold = threshold;
    rec.createdAt = 0;
    rec.lastUpdated = 0;
    rec.acknowledgedAt = 0;
    rec.isAcknowledged = false;
    rec.isImplemented = false;
    rec.implementedAt = 0;
    rec.isDismissed = false;
    rec.preImplementationValue = sourceValue;
    rec.postImplementationValue = 0.0;
    rec.effectivenessVerified = false;
    rec.relatedAlertCount = 0;
    rec.difficulty = EstimateDifficulty(category, sourceValue / std::max(threshold, 1.0));
    rec.estimatedTime = EstimateTime(sourceValue, category);
    return rec;
}

OptimizationPriority AutoTuner::DeterminePriority(double severity, double impact) const {
    if (severity > 1.0 || impact > 0.8) return OptimizationPriority::Critical;
    if (severity > 0.5 || impact > 0.5) return OptimizationPriority::High;
    if (severity > 0.2 || impact > 0.3) return OptimizationPriority::Medium;
    return OptimizationPriority::Low;
}

ImpactLevel AutoTuner::EstimateImpact(double fpsDelta, OptimizationCategory category) const {
    // FPS delta is positive when there's room for improvement
    if (fpsDelta <= 0) return ImpactLevel::Unknown;

    switch (category) {
        case OptimizationCategory::GPU:
            return fpsDelta > 10.0 ? ImpactLevel::Major
                 : fpsDelta > 5.0 ? ImpactLevel::Significant
                 : fpsDelta > 2.0 ? ImpactLevel::Moderate
                 : ImpactLevel::Minor;
        case OptimizationCategory::CPU:
            return fpsDelta > 8.0 ? ImpactLevel::Major
                 : fpsDelta > 4.0 ? ImpactLevel::Significant
                 : fpsDelta > 1.5 ? ImpactLevel::Moderate
                 : ImpactLevel::Minor;
        case OptimizationCategory::Memory:
            return fpsDelta > 5.0 ? ImpactLevel::Significant
                 : fpsDelta > 2.0 ? ImpactLevel::Moderate
                 : ImpactLevel::Minor;
        default:
            return fpsDelta > 3.0 ? ImpactLevel::Significant
                 : fpsDelta > 1.0 ? ImpactLevel::Moderate
                 : ImpactLevel::Minor;
    }
}

std::string AutoTuner::EstimateTime(double severity, OptimizationCategory category) const {
    if (severity > 1.0) return "1 week+";
    if (severity > 0.5) {
        return category == OptimizationCategory::Rendering ? "1-3 days" : "2-4 hours";
    }
    return category == OptimizationCategory::Rendering ? "2-4 hours" : "30 min";
}

std::string AutoTuner::EstimateDifficulty(OptimizationCategory category, double severity) const {
    switch (category) {
        case OptimizationCategory::Rendering:
            return severity > 0.8 ? "Hard" : "Medium";
        case OptimizationCategory::Memory:
            return severity > 0.5 ? "Hard" : "Medium";
        case OptimizationCategory::Threading:
            return "Hard";
        case OptimizationCategory::GPU:
            return severity > 0.3 ? "Medium" : "Easy";
        default:
            return "Medium";
    }
}

int64_t AutoTuner::GenerateRecommendationId() {
    return m_nextRecId++;
}

int64_t AutoTuner::GetCurrentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

double AutoTuner::GetTargetFPS() const {
    switch (m_config.platform) {
        case TargetPlatform::Desktop: return m_config.desktopTargetFps;
        case TargetPlatform::Laptop:   return m_config.laptopTargetFps;
        case TargetPlatform::Console:  return m_config.consoleTargetFps;
        case TargetPlatform::Mobile:   return m_config.mobileTargetFps;
        default: return 60.0;
    }
}

double AutoTuner::ComputeOverallScore() const {
    auto& core = ProfilerCore::GetInstance();
    auto scorer = core.GetPerformanceScorer();
    if (scorer) {
        auto score = scorer->ComputeScore();
        return score.overallScore;
    }

    // Fallback: compute from recommendations
    if (m_recommendations.empty()) return 100.0;

    double score = 100.0;
    int critical = 0, high = 0, medium = 0;
    for (const auto& r : m_recommendations) {
        if (r.isImplemented || r.isDismissed) continue;
        switch (r.priority) {
            case OptimizationPriority::Critical: critical++; break;
            case OptimizationPriority::High:     high++;     break;
            case OptimizationPriority::Medium:   medium++;   break;
        }
    }
    score -= critical * 10;
    score -= high * 5;
    score -= medium * 2;
    return std::max(0.0, score);
}

std::string AutoTuner::CategoryToString(OptimizationCategory cat) const {
    switch (cat) {
        case OptimizationCategory::Rendering:  return "Rendering";
        case OptimizationCategory::Memory:     return "Memory";
        case OptimizationCategory::CPU:         return "CPU / Game Logic";
        case OptimizationCategory::GPU:        return "GPU / Graphics";
        case OptimizationCategory::Network:     return "Network";
        case OptimizationCategory::Threading:   return "Threading";
        case OptimizationCategory::Assets:      return "Asset Loading";
        case OptimizationCategory::General:     return "General";
        default: return "Unknown";
    }
}

std::string AutoTuner::PriorityToString(OptimizationPriority pri) const {
    switch (pri) {
        case OptimizationPriority::Critical: return "Critical";
        case OptimizationPriority::High:     return "High";
        case OptimizationPriority::Medium:   return "Medium";
        case OptimizationPriority::Low:      return "Low";
        default: return "Unknown";
    }
}

std::string AutoTuner::ImpactToString(ImpactLevel impact) const {
    switch (impact) {
        case ImpactLevel::Major:        return "Major (>10 FPS)";
        case ImpactLevel::Significant:  return "Significant (5-10 FPS)";
        case ImpactLevel::Moderate:    return "Moderate (2-5 FPS)";
        case ImpactLevel::Minor:       return "Minor (<2 FPS)";
        default:                        return "Unknown";
    }
}

std::string AutoTuner::SpikeCauseToString(SpikeCause cause) const {
    switch (cause) {
        case SpikeCause::GC:             return "Garbage Collection";
        case SpikeCause::AssetLoading:   return "Asset Loading";
        case SpikeCause::ShaderCompile:  return "Shader Compilation";
        case SpikeCause::PhysicsStep:    return "Physics Simulation";
        case SpikeCause::AIProcessing:  return "AI Processing";
        case SpikeCause::RenderComplex:  return "Complex Rendering";
        case SpikeCause::VSyncMiss:     return "VSync Miss";
        case SpikeCause::MemoryAlloc:    return "Memory Allocation";
        case SpikeCause::DiskIO:        return "Disk I/O";
        case SpikeCause::NetworkSync:    return "Network Sync";
        case SpikeCause::ThreadContention: return "Thread Contention";
        case SpikeCause::ThermalThrottle: return "Thermal Throttling";
        case SpikeCause::DriverOverhead: return "GPU Driver Overhead";
        case SpikeCause::PresentWait:   return "Present Wait";
        default:                         return "Unknown";
    }
}

} // namespace ProfilerCore