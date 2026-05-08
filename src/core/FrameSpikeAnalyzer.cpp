#include "FrameSpikeAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace ProfilerCore {

FrameSpikeAnalyzer::FrameSpikeAnalyzer()
    : m_nextSpikeId(1)
    , m_nextPatternId(1)
    , m_avgFrameTime(16.67)
    , m_avgCpuTime(8.0)
    , m_avgGpuTime(8.0)
    , m_currentFrame(0)
    , m_lastFrameTimestamp(0)
    , m_statsDirty(true)
{
}

FrameSpikeAnalyzer::~FrameSpikeAnalyzer() {
}

void FrameSpikeAnalyzer::SetConfig(const SpikeAnalyzerConfig& config) {
    m_config = config;
    m_statsDirty = true;
}

void FrameSpikeAnalyzer::SetTargetFPS(double fps) {
    m_config.targetFrameTimeMs = 1000.0 / fps;
    m_statsDirty = true;
}

void FrameSpikeAnalyzer::Reset() {
    m_frameTimeHistory.clear();
    m_cpuTimeHistory.clear();
    m_gpuTimeHistory.clear();
    m_spikes.clear();
    m_patterns.clear();
    m_recurrenceGroups.clear();
    m_nextSpikeId = 1;
    m_nextPatternId = 1;
    m_currentFrame = 0;
    m_lastFrameTimestamp = 0;
    m_avgFrameTime = m_config.targetFrameTimeMs;
    m_avgCpuTime = m_config.targetFrameTimeMs / 2.0;
    m_avgGpuTime = m_config.targetFrameTimeMs / 2.0;
    m_statsDirty = true;
}

void FrameSpikeAnalyzer::RecordFrame(double frameTimeMs, double cpuTimeMs, double gpuTimeMs,
                                      const SpikeContext& context) {
    int64_t now = GetCurrentTimestamp();
    m_currentFrame++;
    m_lastFrameTimestamp = now;
    
    // Update history
    m_frameTimeHistory.push_back(frameTimeMs);
    m_cpuTimeHistory.push_back(cpuTimeMs);
    m_gpuTimeHistory.push_back(gpuTimeMs);
    
    // Trim history to window size
    while (m_frameTimeHistory.size() > m_config.patternWindowSize) {
        m_frameTimeHistory.pop_front();
        m_cpuTimeHistory.pop_front();
        m_gpuTimeHistory.pop_front();
    }
    
    // Update averages
    if (!m_frameTimeHistory.empty()) {
        double sum = 0.0;
        for (double t : m_frameTimeHistory) sum += t;
        m_avgFrameTime = sum / m_frameTimeHistory.size();
    }
    if (!m_cpuTimeHistory.empty()) {
        double sum = 0.0;
        for (double t : m_cpuTimeHistory) sum += t;
        m_avgCpuTime = sum / m_cpuTimeHistory.size();
    }
    if (!m_gpuTimeHistory.empty()) {
        double sum = 0.0;
        for (double t : m_gpuTimeHistory) sum += t;
        m_avgGpuTime = sum / m_gpuTimeHistory.size();
    }
    
    // Detect spike
    DetectSpike(frameTimeMs, cpuTimeMs, gpuTimeMs, context);
    
    m_statsDirty = true;
}

void FrameSpikeAnalyzer::RecordFrameSimple(double frameTimeMs, double cpuTimeMs, double gpuTimeMs) {
    RecordFrame(frameTimeMs, cpuTimeMs, gpuTimeMs, SpikeContext{});
}

void FrameSpikeAnalyzer::DetectSpike(double frameTimeMs, double cpuTimeMs, double gpuTimeMs,
                                      const SpikeContext& context) {
    double expectedTime = m_config.targetFrameTimeMs;
    double spikeRatio = frameTimeMs / expectedTime;
    
    // Check if this qualifies as a spike (at least minor)
    if (spikeRatio < m_config.minorThreshold) {
        return;  // Not a spike
    }
    
    // Create spike record
    FrameSpike spike;
    spike.id = GenerateSpikeId();
    spike.timestamp = GetCurrentTimestamp();
    spike.frameNumber = m_currentFrame;
    spike.frameTimeMs = frameTimeMs;
    spike.expectedFrameTimeMs = expectedTime;
    spike.spikeRatio = spikeRatio;
    spike.deviationMs = frameTimeMs - expectedTime;
    spike.severity = ClassifySeverity(spikeRatio);
    spike.context = context;
    spike.acknowledged = false;
    spike.acknowledgedAt = 0;
    spike.isRecurring = false;
    spike.recurrenceGroup = 0;
    
    // Determine cause
    if (m_config.enableCauseAnalysis) {
        spike.primaryCause = DetermineCause(frameTimeMs, cpuTimeMs, gpuTimeMs, 
                                            context, spike.causeConfidence);
        spike.secondaryCause = DetermineSecondaryCause(frameTimeMs, cpuTimeMs, gpuTimeMs, context);
    } else {
        spike.primaryCause = SpikeCause::Unknown;
        spike.secondaryCause = SpikeCause::Unknown;
        spike.causeConfidence = 0.0;
    }
    
    // Check for recurring pattern
    if (m_config.enablePatternDetection) {
        DetectRecurringSpike(spike);
    }
    
    // Generate analysis and recommendations
    if (m_config.generateRecommendations) {
        GenerateSpikeAnalysis(spike);
        GenerateRecommendations(spike);
    }
    
    // Add to history
    m_spikes.push_back(spike);
    
    // Update patterns
    if (m_config.enablePatternDetection) {
        UpdatePatterns(spike);
    }
    
    // Trim history
    TrimHistory();
    
    // Notify callback
    if (m_spikeCallback) {
        m_spikeCallback(spike);
    }
}

SpikeSeverity FrameSpikeAnalyzer::ClassifySeverity(double spikeRatio) const {
    if (spikeRatio >= m_config.severeThreshold) {
        return SpikeSeverity::Critical;
    } else if (spikeRatio >= m_config.majorThreshold) {
        return SpikeSeverity::Severe;
    } else if (spikeRatio >= m_config.moderateThreshold) {
        return SpikeSeverity::Major;
    } else if (spikeRatio >= m_config.minorThreshold) {
        return SpikeSeverity::Moderate;
    }
    return SpikeSeverity::Minor;
}

SpikeCause FrameSpikeAnalyzer::DetermineCause(double frameTimeMs, double cpuTimeMs, double gpuTimeMs,
                                               const SpikeContext& context, double& confidence) const {
    confidence = 0.0;
    
    // Check thermal throttling first (high impact)
    if (context.temperatureC > m_config.thermalThresholdC) {
        confidence = 0.85;
        return SpikeCause::ThermalThrottle;
    }
    
    // Check VSync miss
    if (context.vsyncMissed) {
        confidence = 0.9;
        return SpikeCause::VSyncMiss;
    }
    
    // Check GC (garbage collection)
    if (context.gcCollections > 0 || context.waitTimeMs > m_config.gcTimeThresholdMs) {
        confidence = 0.8;
        return SpikeCause::GC;
    }
    
    // Check driver overhead
    if (context.driverTimeMs > m_config.driverOverheadThresholdMs) {
        confidence = 0.75;
        return SpikeCause::DriverOverhead;
    }
    
    // Check disk I/O
    if (context.diskIOBytes > 1024 * 1024) {  // > 1MB
        confidence = 0.7;
        return SpikeCause::DiskIO;
    }
    
    // Check network sync
    if (context.networkBytes > 100 * 1024) {  // > 100KB
        confidence = 0.65;
        return SpikeCause::NetworkSync;
    }
    
    // Check render complexity
    if (context.drawCalls > m_config.renderThresholdDrawCalls) {
        confidence = 0.7;
        return SpikeCause::RenderComplex;
    }
    
    // Check shader compilation (long spike with high GPU time)
    if (frameTimeMs > m_config.shaderCompileThresholdMs && gpuTimeMs > frameTimeMs * 0.7) {
        confidence = 0.6;
        return SpikeCause::ShaderCompile;
    }
    
    // Check asset loading (long spike with disk I/O or memory alloc)
    if (frameTimeMs > m_config.assetLoadThresholdMs && 
        (context.diskIOBytes > 0 || context.memoryDelta > 1024 * 1024)) {
        confidence = 0.65;
        return SpikeCause::AssetLoading;
    }
    
    // Check physics (high CPU time)
    if (cpuTimeMs > m_config.physicsThresholdMs) {
        confidence = 0.6;
        return SpikeCause::PhysicsStep;
    }
    
    // Check memory allocation
    if (context.memoryDelta > 10 * 1024 * 1024) {  // > 10MB allocation
        confidence = 0.55;
        return SpikeCause::MemoryAlloc;
    }
    
    // Check thread contention
    if (context.waitTimeMs > frameTimeMs * 0.3) {
        confidence = 0.5;
        return SpikeCause::ThreadContention;
    }
    
    // GPU vs CPU bound
    if (gpuTimeMs > cpuTimeMs * 1.5) {
        confidence = 0.45;
        return SpikeCause::RenderComplex;
    }
    
    if (cpuTimeMs > gpuTimeMs * 1.5) {
        confidence = 0.45;
        return SpikeCause::AIProcessing;  // Could be AI or general CPU work
    }
    
    // Present wait
    if (gpuTimeMs > frameTimeMs * 0.8) {
        confidence = 0.4;
        return SpikeCause::PresentWait;
    }
    
    // Unknown
    confidence = 0.2;
    return SpikeCause::Unknown;
}

SpikeCause FrameSpikeAnalyzer::DetermineSecondaryCause(double frameTimeMs, double cpuTimeMs,
                                                        double gpuTimeMs, const SpikeContext& context) const {
    // Return a secondary factor that might contribute
    
    if (context.memoryDelta > 1024 * 1024 && context.memoryDelta < 10 * 1024 * 1024) {
        return SpikeCause::MemoryAlloc;
    }
    
    if (context.diskIOBytes > 100 * 1024 && context.diskIOBytes < 1024 * 1024) {
        return SpikeCause::DiskIO;
    }
    
    if (context.driverTimeMs > 1.0 && context.driverTimeMs < m_config.driverOverheadThresholdMs) {
        return SpikeCause::DriverOverhead;
    }
    
    if (context.waitTimeMs > 1.0 && context.waitTimeMs < m_config.gcTimeThresholdMs) {
        return SpikeCause::ThreadContention;
    }
    
    return SpikeCause::Unknown;
}

void FrameSpikeAnalyzer::DetectRecurringSpike(FrameSpike& spike) {
    // Look for similar spikes in recent history
    int64_t cutoffTime = spike.timestamp - m_config.recurrenceIntervalMs;
    
    for (const auto& prevSpike : m_spikes) {
        if (prevSpike.timestamp < cutoffTime) continue;
        if (prevSpike.primaryCause != spike.primaryCause) continue;
        
        // Check if severity is similar
        int severityDiff = std::abs(static_cast<int>(prevSpike.severity) - 
                                    static_cast<int>(spike.severity));
        if (severityDiff > 1) continue;
        
        // This is a recurring spike
        spike.isRecurring = true;
        spike.recurrenceGroup = prevSpike.recurrenceGroup > 0 ? 
                                prevSpike.recurrenceGroup : FindRecurrenceGroup(spike);
        return;
    }
    
    spike.isRecurring = false;
    spike.recurrenceGroup = 0;
}

int FrameSpikeAnalyzer::FindRecurrenceGroup(const FrameSpike& spike) const {
    // Find existing group or suggest new one
    for (const auto& [groupId, spikeIds] : m_recurrenceGroups) {
        // Check if this group matches the spike's cause
        if (!spikeIds.empty()) {
            const FrameSpike& representative = GetSpike(spikeIds.front());
            if (representative.primaryCause == spike.primaryCause) {
                return groupId;
            }
        }
    }
    return static_cast<int>(m_recurrenceGroups.size() + 1);
}

void FrameSpikeAnalyzer::UpdatePatterns(const FrameSpike& spike) {
    // Find or create pattern for this cause
    SpikePattern* matchingPattern = nullptr;
    
    for (auto& pattern : m_patterns) {
        if (pattern.cause == spike.primaryCause) {
            // Check if within reasonable interval
            int64_t interval = spike.timestamp - pattern.lastOccurrence;
            if (interval < m_config.recurrenceIntervalMs * 2) {
                matchingPattern = &pattern;
                break;
            }
        }
    }
    
    if (matchingPattern) {
        // Update existing pattern
        matchingPattern->occurrenceCount++;
        matchingPattern->lastOccurrence = spike.timestamp;
        matchingPattern->spikeIds.push_back(spike.id);
        
        // Update average frame time
        double total = matchingPattern->avgFrameTimeMs * (matchingPattern->occurrenceCount - 1);
        matchingPattern->avgFrameTimeMs = (total + spike.frameTimeMs) / matchingPattern->occurrenceCount;
        
        // Update average interval
        if (matchingPattern->occurrenceCount > 1) {
            int64_t newInterval = spike.timestamp - matchingPattern->lastOccurrence;
            double intervalTotal = matchingPattern->avgIntervalMs * (matchingPattern->occurrenceCount - 2);
            matchingPattern->avgIntervalMs = (intervalTotal + newInterval) / (matchingPattern->occurrenceCount - 1);
        }
    } else {
        // Create new pattern
        SpikePattern newPattern;
        newPattern.patternId = m_nextPatternId++;
        newPattern.cause = spike.primaryCause;
        newPattern.avgFrameTimeMs = spike.frameTimeMs;
        newPattern.avgIntervalMs = 0.0;
        newPattern.occurrenceCount = 1;
        newPattern.firstOccurrence = spike.timestamp;
        newPattern.lastOccurrence = spike.timestamp;
        newPattern.spikeIds.push_back(spike.id);
        newPattern.description = CauseToString(spike.primaryCause) + " spike pattern";
        
        // Generate pattern recommendations
        newPattern.recommendations = GetRecommendations(spike.id);
        
        m_patterns.push_back(newPattern);
        
        // Notify callback
        if (m_patternCallback) {
            m_patternCallback(newPattern);
        }
    }
    
    // Trim patterns
    while (m_patterns.size() > static_cast<size_t>(m_config.maxPatternHistory)) {
        m_patterns.erase(m_patterns.begin());
    }
}

void FrameSpikeAnalyzer::GenerateSpikeAnalysis(FrameSpike& spike) {
    std::ostringstream oss;
    
    oss << "Frame " << spike.frameNumber << " spike detected: "
        << std::fixed << std::setprecision(2) << spike.frameTimeMs << "ms "
        << "(" << std::setprecision(1) << spike.spikeRatio << "x target)";
    
    oss << "\nSeverity: " << SeverityToString(spike.severity);
    oss << "\nPrimary cause: " << CauseToString(spike.primaryCause);
    oss << " (confidence: " << std::setprecision(0) << (spike.causeConfidence * 100) << "%)";
    
    if (spike.secondaryCause != SpikeCause::Unknown) {
        oss << "\nSecondary factor: " << CauseToString(spike.secondaryCause);
    }
    
    if (spike.isRecurring) {
        oss << "\n⚠ This is a recurring spike (group " << spike.recurrenceGroup << ")";
    }
    
    // Add context details
    if (spike.context.cpuTimeMs > 0 || spike.context.gpuTimeMs > 0) {
        oss << "\nCPU: " << std::setprecision(2) << spike.context.cpuTimeMs << "ms, "
            << "GPU: " << spike.context.gpuTimeMs << "ms";
    }
    
    if (spike.context.drawCalls > 0) {
        oss << "\nDraw calls: " << spike.context.drawCalls 
            << ", Triangles: " << spike.context.triangles;
    }
    
    if (spike.context.memoryDelta != 0) {
        oss << "\nMemory delta: " << std::setprecision(2) 
            << (spike.context.memoryDelta / 1024.0 / 1024.0) << " MB";
    }
    
    spike.analysis = oss.str();
}

void FrameSpikeAnalyzer::GenerateRecommendations(FrameSpike& spike) {
    spike.recommendations.clear();
    
    switch (spike.primaryCause) {
        case SpikeCause::GC:
            spike.recommendations.push_back("Consider reducing garbage generation");
            spike.recommendations.push_back("Implement object pooling for frequently allocated objects");
            spike.recommendations.push_back("Schedule GC during non-critical moments (loading screens)");
            break;
            
        case SpikeCause::AssetLoading:
            spike.recommendations.push_back("Implement asynchronous asset loading");
            spike.recommendations.push_back("Use asset streaming for large resources");
            spike.recommendations.push_back("Preload critical assets during loading screens");
            break;
            
        case SpikeCause::ShaderCompile:
            spike.recommendations.push_back("Warm up shaders during loading");
            spike.recommendations.push_back("Use shader caching to avoid recompilation");
            spike.recommendations.push_back("Reduce shader permutations");
            break;
            
        case SpikeCause::PhysicsStep:
            spike.recommendations.push_back("Reduce physics simulation complexity");
            spike.recommendations.push_back("Use fixed timestep for physics");
            spike.recommendations.push_back("Consider physics LOD for distant objects");
            break;
            
        case SpikeCause::AIProcessing:
            spike.recommendations.push_back("Distribute AI processing across frames");
            spike.recommendations.push_back("Use spatial partitioning for pathfinding");
            spike.recommendations.push_back("Implement AI LOD for distant entities");
            break;
            
        case SpikeCause::RenderComplex:
            spike.recommendations.push_back("Reduce draw calls using instancing");
            spike.recommendations.push_back("Implement occlusion culling");
            spike.recommendations.push_back("Use LOD system for geometry");
            spike.recommendations.push_back("Consider deferred rendering for many lights");
            break;
            
        case SpikeCause::VSyncMiss:
            spike.recommendations.push_back("Check for GPU bottleneck");
            spike.recommendations.push_back("Consider adaptive VSync");
            spike.recommendations.push_back("Reduce rendering load to maintain target FPS");
            break;
            
        case SpikeCause::MemoryAlloc:
            spike.recommendations.push_back("Pre-allocate memory pools");
            spike.recommendations.push_back("Use custom allocators for frequent allocations");
            spike.recommendations.push_back("Avoid large allocations during gameplay");
            break;
            
        case SpikeCause::DiskIO:
            spike.recommendations.push_back("Use asynchronous file I/O");
            spike.recommendations.push_back("Implement data streaming");
            spike.recommendations.push_back("Cache frequently accessed data");
            break;
            
        case SpikeCause::NetworkSync:
            spike.recommendations.push_back("Implement client-side prediction");
            spike.recommendations.push_back("Use interpolation for network entities");
            spike.recommendations.push_back("Reduce network message frequency");
            break;
            
        case SpikeCause::ThreadContention:
            spike.recommendations.push_back("Reduce lock contention");
            spike.recommendations.push_back("Use lock-free data structures where possible");
            spike.recommendations.push_back("Rebalance work across threads");
            break;
            
        case SpikeCause::ThermalThrottle:
            spike.recommendations.push_back("Reduce GPU workload to lower temperature");
            spike.recommendations.push_back("Check cooling system");
            spike.recommendations.push_back("Consider dynamic quality adjustment");
            break;
            
        case SpikeCause::DriverOverhead:
            spike.recommendations.push_back("Reduce draw calls");
            spike.recommendations.push_back("Use instancing and batching");
            spike.recommendations.push_back("Update GPU drivers");
            break;
            
        case SpikeCause::PresentWait:
            spike.recommendations.push_back("Check for GPU bottleneck");
            spike.recommendations.push_back("Consider triple buffering");
            spike.recommendations.push_back("Reduce frame complexity");
            break;
            
        default:
            spike.recommendations.push_back("Profile frame to identify bottleneck");
            spike.recommendations.push_back("Check CPU and GPU timing");
            break;
    }
}

std::string FrameSpikeAnalyzer::CauseToString(SpikeCause cause) const {
    switch (cause) {
        case SpikeCause::GC: return "Garbage Collection";
        case SpikeCause::AssetLoading: return "Asset Loading";
        case SpikeCause::ShaderCompile: return "Shader Compilation";
        case SpikeCause::PhysicsStep: return "Physics";
        case SpikeCause::AIProcessing: return "AI Processing";
        case SpikeCause::RenderComplex: return "Render Complexity";
        case SpikeCause::VSyncMiss: return "VSync Miss";
        case SpikeCause::MemoryAlloc: return "Memory Allocation";
        case SpikeCause::DiskIO: return "Disk I/O";
        case SpikeCause::NetworkSync: return "Network Sync";
        case SpikeCause::ThreadContention: return "Thread Contention";
        case SpikeCause::ThermalThrottle: return "Thermal Throttling";
        case SpikeCause::DriverOverhead: return "Driver Overhead";
        case SpikeCause::PresentWait: return "Present Wait";
        default: return "Unknown";
    }
}

std::string FrameSpikeAnalyzer::SeverityToString(SpikeSeverity severity) const {
    switch (severity) {
        case SpikeSeverity::Minor: return "Minor";
        case SpikeSeverity::Moderate: return "Moderate";
        case SpikeSeverity::Major: return "Major";
        case SpikeSeverity::Severe: return "Severe";
        case SpikeSeverity::Critical: return "Critical";
        default: return "Unknown";
    }
}

std::vector<FrameSpike> FrameSpikeAnalyzer::GetRecentSpikes(int count) const {
    int actual = std::min(count, static_cast<int>(m_spikes.size()));
    if (actual == 0) return {};
    
    return std::vector<FrameSpike>(m_spikes.end() - actual, m_spikes.end());
}

std::vector<FrameSpike> FrameSpikeAnalyzer::GetSpikesByCause(SpikeCause cause) const {
    std::vector<FrameSpike> result;
    for (const auto& spike : m_spikes) {
        if (spike.primaryCause == cause) {
            result.push_back(spike);
        }
    }
    return result;
}

std::vector<FrameSpike> FrameSpikeAnalyzer::GetSpikesBySeverity(SpikeSeverity severity) const {
    std::vector<FrameSpike> result;
    for (const auto& spike : m_spikes) {
        if (spike.severity == severity) {
            result.push_back(spike);
        }
    }
    return result;
}

std::vector<FrameSpike> FrameSpikeAnalyzer::GetUnacknowledgedSpikes() const {
    std::vector<FrameSpike> result;
    for (const auto& spike : m_spikes) {
        if (!spike.acknowledged) {
            result.push_back(spike);
        }
    }
    return result;
}

FrameSpike FrameSpikeAnalyzer::GetSpike(int64_t spikeId) const {
    for (const auto& spike : m_spikes) {
        if (spike.id == spikeId) {
            return spike;
        }
    }
    return FrameSpike{};
}

std::vector<SpikePattern> FrameSpikeAnalyzer::GetActivePatterns() const {
    std::vector<SpikePattern> active;
    int64_t cutoff = GetCurrentTimestamp() - m_config.recurrenceIntervalMs * 3;
    
    for (const auto& pattern : m_patterns) {
        if (pattern.lastOccurrence > cutoff && pattern.occurrenceCount >= m_config.minPatternOccurrences) {
            active.push_back(pattern);
        }
    }
    return active;
}

SpikePattern FrameSpikeAnalyzer::GetPattern(int64_t patternId) const {
    for (const auto& pattern : m_patterns) {
        if (pattern.patternId == patternId) {
            return pattern;
        }
    }
    return SpikePattern{};
}

SpikeStatistics FrameSpikeAnalyzer::GetStatistics() const {
    if (m_statsDirty) {
        const_cast<FrameSpikeAnalyzer*>(this)->UpdateStatistics();
    }
    return m_cachedStats;
}

SpikeStatistics FrameSpikeAnalyzer::GetStatisticsForDuration(int64_t durationMs) const {
    SpikeStatistics stats = {};
    int64_t cutoff = GetCurrentTimestamp() - durationMs;
    
    for (const auto& spike : m_spikes) {
        if (spike.timestamp < cutoff) continue;
        
        stats.totalSpikes++;
        switch (spike.severity) {
            case SpikeSeverity::Minor: stats.minorSpikes++; break;
            case SpikeSeverity::Moderate: stats.moderateSpikes++; break;
            case SpikeSeverity::Major: stats.majorSpikes++; break;
            case SpikeSeverity::Severe: stats.severeSpikes++; break;
            case SpikeSeverity::Critical: stats.criticalSpikes++; break;
        }
        
        stats.spikesByCause[spike.primaryCause]++;
        stats.totalSpikeTimeMs += spike.deviationMs;
        stats.maxSpikeFrameTime = std::max(stats.maxSpikeFrameTime, spike.frameTimeMs);
    }
    
    if (stats.totalSpikes > 0) {
        stats.avgSpikeFrameTime = stats.totalSpikeTimeMs / stats.totalSpikes;
    }
    
    return stats;
}

bool FrameSpikeAnalyzer::AcknowledgeSpike(int64_t spikeId) {
    for (auto& spike : m_spikes) {
        if (spike.id == spikeId && !spike.acknowledged) {
            spike.acknowledged = true;
            spike.acknowledgedAt = GetCurrentTimestamp();
            m_statsDirty = true;
            return true;
        }
    }
    return false;
}

bool FrameSpikeAnalyzer::AcknowledgeAllSpikes() {
    bool any = false;
    int64_t now = GetCurrentTimestamp();
    for (auto& spike : m_spikes) {
        if (!spike.acknowledged) {
            spike.acknowledged = true;
            spike.acknowledgedAt = now;
            any = true;
        }
    }
    if (any) m_statsDirty = true;
    return any;
}

void FrameSpikeAnalyzer::ClearHistory() {
    m_spikes.clear();
    m_patterns.clear();
    m_recurrenceGroups.clear();
    m_statsDirty = true;
}

std::string FrameSpikeAnalyzer::AnalyzeSpike(int64_t spikeId) const {
    FrameSpike spike = GetSpike(spikeId);
    if (spike.id == 0) return "Spike not found";
    return spike.analysis;
}

std::vector<std::string> FrameSpikeAnalyzer::GetRecommendations(int64_t spikeId) const {
    FrameSpike spike = GetSpike(spikeId);
    if (spike.id == 0) return {};
    return spike.recommendations;
}

std::vector<std::string> FrameSpikeAnalyzer::GetTopRecommendations(int count) const {
    // Aggregate recommendations from all spikes, weighted by severity
    std::unordered_map<std::string, double> recScores;
    
    for (const auto& spike : m_spikes) {
        double weight = static_cast<int>(spike.severity) + 1;
        for (const auto& rec : spike.recommendations) {
            recScores[rec] += weight;
        }
    }
    
    // Sort by score
    std::vector<std::pair<std::string, double>> sorted(recScores.begin(), recScores.end());
    std::sort(sorted.begin(), sorted.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Return top N
    std::vector<std::string> result;
    for (int i = 0; i < count && i < static_cast<int>(sorted.size()); i++) {
        result.push_back(sorted[i].first);
    }
    return result;
}

double FrameSpikeAnalyzer::ComputeFPSImpact() const {
    if (m_spikes.empty()) return 0.0;
    
    double totalSpikeTime = 0.0;
    int64_t firstTime = m_spikes.front().timestamp;
    int64_t lastTime = m_spikes.back().timestamp;
    
    for (const auto& spike : m_spikes) {
        totalSpikeTime += spike.deviationMs;
    }
    
    double durationMs = static_cast<double>(lastTime - firstTime);
    if (durationMs <= 0) return 0.0;
    
    // Impact = time lost to spikes / total time
    return (totalSpikeTime / durationMs) * 100.0;
}

double FrameSpikeAnalyzer::ComputeUserExperienceScore() const {
    // Base score starts at 100
    double score = 100.0;
    
    // Penalize based on spike frequency and severity
    int64_t durationMs = m_lastFrameTimestamp - (m_spikes.empty() ? m_lastFrameTimestamp : m_spikes.front().timestamp);
    if (durationMs <= 0) return 100.0;
    
    double spikesPerSecond = static_cast<double>(m_spikes.size()) / (durationMs / 1000.0);
    
    // Penalize spike frequency (up to 30 points)
    score -= std::min(30.0, spikesPerSecond * 5.0);
    
    // Penalize severity distribution
    int severeCount = 0;
    int criticalCount = 0;
    for (const auto& spike : m_spikes) {
        if (spike.severity == SpikeSeverity::Severe) severeCount++;
        if (spike.severity == SpikeSeverity::Critical) criticalCount++;
    }
    
    // Penalize severe spikes (up to 20 points)
    score -= std::min(20.0, static_cast<double>(severeCount) * 2.0);
    
    // Penalize critical spikes heavily (up to 30 points)
    score -= std::min(30.0, static_cast<double>(criticalCount) * 5.0);
    
    // Penalize recurring patterns (up to 20 points)
    int recurringPatterns = 0;
    for (const auto& pattern : m_patterns) {
        if (pattern.occurrenceCount >= m_config.minPatternOccurrences) {
            recurringPatterns++;
        }
    }
    score -= std::min(20.0, static_cast<double>(recurringPatterns) * 5.0);
    
    return std::max(0.0, score);
}

int64_t FrameSpikeAnalyzer::EstimateTimeLostToSpikes() const {
    double totalMs = 0.0;
    for (const auto& spike : m_spikes) {
        totalMs += spike.deviationMs;
    }
    return static_cast<int64_t>(totalMs);
}

void FrameSpikeAnalyzer::SetSpikeCallback(SpikeCallback callback) {
    m_spikeCallback = std::move(callback);
}

void FrameSpikeAnalyzer::SetPatternCallback(PatternCallback callback) {
    m_patternCallback = std::move(callback);
}

int64_t FrameSpikeAnalyzer::GenerateSpikeId() {
    return m_nextSpikeId++;
}

int64_t FrameSpikeAnalyzer::GetCurrentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

void FrameSpikeAnalyzer::TrimHistory() {
    while (m_spikes.size() > static_cast<size_t>(m_config.maxSpikeHistory)) {
        m_spikes.erase(m_spikes.begin());
    }
}

void FrameSpikeAnalyzer::UpdateStatistics() {
    m_cachedStats = SpikeStatistics{};
    m_cachedStats.timestamp = GetCurrentTimestamp();
    m_cachedStats.totalSpikes = static_cast<int>(m_spikes.size());
    
    for (const auto& spike : m_spikes) {
        switch (spike.severity) {
            case SpikeSeverity::Minor: m_cachedStats.minorSpikes++; break;
            case SpikeSeverity::Moderate: m_cachedStats.moderateSpikes++; break;
            case SpikeSeverity::Major: m_cachedStats.majorSpikes++; break;
            case SpikeSeverity::Severe: m_cachedStats.severeSpikes++; break;
            case SpikeSeverity::Critical: m_cachedStats.criticalSpikes++; break;
        }
        
        m_cachedStats.spikesByCause[spike.primaryCause]++;
        m_cachedStats.totalSpikeTimeMs += spike.deviationMs;
        m_cachedStats.maxSpikeFrameTime = std::max(m_cachedStats.maxSpikeFrameTime, spike.frameTimeMs);
    }
    
    if (m_cachedStats.totalSpikes > 0) {
        m_cachedStats.avgSpikeFrameTime = m_cachedStats.totalSpikeTimeMs / m_cachedStats.totalSpikes;
    }
    
    // Count patterns
    for (const auto& pattern : m_patterns) {
        if (pattern.occurrenceCount >= m_config.minPatternOccurrences) {
            m_cachedStats.recurringPatterns++;
        }
    }
    
    m_cachedStats.uniqueSpikes = m_cachedStats.totalSpikes;
    for (const auto& spike : m_spikes) {
        if (spike.isRecurring) m_cachedStats.uniqueSpikes--;
    }
    
    m_cachedStats.fpsImpactPercent = ComputeFPSImpact();
    m_cachedStats.userExperienceScore = ComputeUserExperienceScore();
    m_cachedStats.topRecommendations = GetTopRecommendations(5);
    
    m_statsDirty = false;
}

std::string FrameSpikeAnalyzer::ExportToJSON() const {
    std::ostringstream ss;
    ss << "{\"frameSpikeAnalyzer\":{";
    ss << "\"timestamp\":" << GetCurrentTimestamp() << ",";
    ss << "\"config\":{";
    ss << "\"targetFrameTimeMs\":" << std::fixed << std::setprecision(2) << m_config.targetFrameTimeMs << ",";
    ss << "\"minorThreshold\":" << m_config.minorThreshold << ",";
    ss << "\"moderateThreshold\":" << m_config.moderateThreshold << ",";
    ss << "\"majorThreshold\":" << m_config.majorThreshold << ",";
    ss << "\"severeThreshold\":" << m_config.severeThreshold;
    ss << "},";
    ss << "\"statistics\":" << ExportStatisticsToJSON() << ",";
    ss << "\"recentSpikes\":" << ExportSpikesToJSON(50) << ",";
    ss << "\"patterns\":" << ExportPatternsToJSON();
    ss << "}}";
    return ss.str();
}

std::string FrameSpikeAnalyzer::ExportSpikesToJSON(int limit) const {
    int count = std::min(limit, static_cast<int>(m_spikes.size()));
    if (count == 0) return "[]";
    
    std::ostringstream ss;
    ss << "[";
    
    int start = static_cast<int>(m_spikes.size()) - count;
    for (int i = start; i < static_cast<int>(m_spikes.size()); i++) {
        const auto& spike = m_spikes[i];
        ss << "{";
        ss << "\"id\":" << spike.id << ",";
        ss << "\"timestamp\":" << spike.timestamp << ",";
        ss << "\"frameNumber\":" << spike.frameNumber << ",";
        ss << "\"frameTimeMs\":" << std::fixed << std::setprecision(2) << spike.frameTimeMs << ",";
        ss << "\"spikeRatio\":" << std::setprecision(2) << spike.spikeRatio << ",";
        ss << "\"severity\":\"" << SeverityToString(spike.severity) << "\",";
        ss << "\"cause\":\"" << CauseToString(spike.primaryCause) << "\",";
        ss << "\"confidence\":" << std::setprecision(2) << (spike.causeConfidence * 100) << ",";
        ss << "\"isRecurring\":" << (spike.isRecurring ? "true" : "false") << ",";
        ss << "\"acknowledged\":" << (spike.acknowledged ? "true" : "false");
        ss << "}";
        if (i < static_cast<int>(m_spikes.size()) - 1) ss << ",";
    }
    
    ss << "]";
    return ss.str();
}

std::string FrameSpikeAnalyzer::ExportPatternsToJSON() const {
    std::ostringstream ss;
    ss << "[";
    
    for (size_t i = 0; i < m_patterns.size(); i++) {
        const auto& pattern = m_patterns[i];
        ss << "{";
        ss << "\"patternId\":" << pattern.patternId << ",";
        ss << "\"cause\":\"" << CauseToString(pattern.cause) << "\",";
        ss << "\"avgFrameTimeMs\":" << std::fixed << std::setprecision(2) << pattern.avgFrameTimeMs << ",";
        ss << "\"avgIntervalMs\":" << std::setprecision(2) << pattern.avgIntervalMs << ",";
        ss << "\"occurrenceCount\":" << pattern.occurrenceCount << ",";
        ss << "\"description\":\"" << pattern.description << "\"";
        ss << "}";
        if (i < m_patterns.size() - 1) ss << ",";
    }
    
    ss << "]";
    return ss.str();
}

std::string FrameSpikeAnalyzer::ExportStatisticsToJSON() const {
    const auto& stats = GetStatistics();
    std::ostringstream ss;
    ss << "{";
    ss << "\"totalSpikes\":" << stats.totalSpikes << ",";
    ss << "\"minorSpikes\":" << stats.minorSpikes << ",";
    ss << "\"moderateSpikes\":" << stats.moderateSpikes << ",";
    ss << "\"majorSpikes\":" << stats.majorSpikes << ",";
    ss << "\"severeSpikes\":" << stats.severeSpikes << ",";
    ss << "\"criticalSpikes\":" << stats.criticalSpikes << ",";
    ss << "\"avgSpikeFrameTime\":" << std::fixed << std::setprecision(2) << stats.avgSpikeFrameTime << ",";
    ss << "\"maxSpikeFrameTime\":" << stats.maxSpikeFrameTime << ",";
    ss << "\"totalSpikeTimeMs\":" << stats.totalSpikeTimeMs << ",";
    ss << "\"recurringPatterns\":" << stats.recurringPatterns << ",";
    ss << "\"uniqueSpikes\":" << stats.uniqueSpikes << ",";
    ss << "\"fpsImpactPercent\":" << std::setprecision(1) << stats.fpsImpactPercent << ",";
    ss << "\"userExperienceScore\":" << std::setprecision(1) << stats.userExperienceScore;
    ss << "}";
    return ss.str();
}

} // namespace ProfilerCore
