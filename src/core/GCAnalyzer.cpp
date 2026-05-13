/**
 * GCAnalyzer.cpp - Garbage collection performance analysis implementation
 *
 * Monitors GC events, measures pause durations, identifies allocation
 * hotspots, and generates optimization recommendations for managed code.
 */

#include "GCAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <unordered_map>

namespace ProfilerCore {

// ─── Constructor / Destructor ────────────────────────────────────────────────

GCAnalyzer::GCAnalyzer()
{
    m_stats = {};
    m_recommendations.clear();
    m_urgentRecommendations.clear();
}

GCAnalyzer::~GCAnalyzer() {
    // Nothing to clean up
}

// ─── Configuration ────────────────────────────────────────────────────────────

void GCAnalyzer::SetConfig(const GCAnalyzerConfig& config)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void GCAnalyzer::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_events.clear();
    m_eventHead = 0;
    m_stats = {};
    m_allocationMap.clear();
    m_currentFrame = 0;
    m_currentFrameTime = 0.0;
    m_consecutiveHighGcFrames = 0;
    m_accumulatedPauseMs = 0.0;
    m_recommendations.clear();
    m_urgentRecommendations.clear();
    m_hasBaseline = false;
}

// ─── GC Event Recording ───────────────────────────────────────────────────────

void GCAnalyzer::RecordGCEvent(
    GCGeneration generation,
    double pauseMs,
    GCTrigger trigger,
    size_t heapBefore,
    size_t heapAfter,
    size_t promoted,
    bool isCompacting,
    int threadId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    GCEvent event = {};
    event.timestamp = GetCurrentTimestamp();
    event.endTimestamp = event.timestamp + static_cast<int64_t>(pauseMs * 1000.0);
    event.generation = generation;
    event.generationIndex = static_cast<int>(generation);

    event.pauseDurationMs = pauseMs;
    event.heapSizeBeforeBytes = heapBefore;
    event.heapSizeAfterBytes = heapAfter;
    event.promotedBytes = promoted;
    event.trigger = trigger;
    event.isCompacting = isCompacting;
    event.threadId = threadId;
    event.isValid = true;

    // Calculate impact
    event.causedSpike = pauseMs > m_config.spikeThresholdMs;
    event.severity = ClassifySeverity(pauseMs);

    // Frame impact estimation
    event.frameImpactMs = pauseMs;
    event.affectedFrameCount = static_cast<int>(std::ceil(pauseMs / 16.67));

    // Memory metrics
    if (heapBefore > 0) {
        event.pausePercent = (pauseMs / 16.67) * 100.0;  // % of 60fps frame
    }

    if (heapBefore > heapAfter) {
        event.fragmentationPercent = ((heapBefore - heapAfter) / static_cast<double>(heapBefore)) * 100.0;
    }

    // Store event
    m_events.push_back(event);

    // Update consecutive counter if needed
    if (pauseMs > m_config.alertPauseThresholdMs) {
        m_consecutiveHighGcFrames++;
        m_accumulatedPauseMs += pauseMs;
    } else {
        m_consecutiveHighGcFrames = std::max(0, m_consecutiveHighGcFrames - 1);
    }

    // Check for alerts
    if (m_config.generateAlerts && m_consecutiveHighGcFrames >= m_config.alertThresholdCount) {
        if (m_alertCallback) {
            m_alertCallback("HighGC", "High GC detected for " +
                std::to_string(m_consecutiveHighGcFrames) + " consecutive frames",
                event);
        }
        m_consecutiveHighGcFrames = 0;  // Reset after alert
    }

    // Update running statistics
    UpdateStatistics();
}

void GCAnalyzer::RecordExternalGCEvent(
    int64_t startTimestamp,
    int64_t endTimestamp,
    int generation,
    double pauseMs,
    const std::string& reason)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    GCEvent event = {};
    event.timestamp = startTimestamp;
    event.endTimestamp = endTimestamp;
    event.generation = ParseGeneration(generation);
    event.generationIndex = generation;
    event.pauseDurationMs = pauseMs;
    event.trigger = GCTrigger::Allocation;  // Default, could parse from reason
    event.isValid = true;
    event.severity = ClassifySeverity(pauseMs);
    event.details = reason;
    event.causedSpike = pauseMs > m_config.spikeThresholdMs;

    if (pauseMs > 0) {
        event.frameImpactMs = pauseMs;
        event.affectedFrameCount = static_cast<int>(std::ceil(pauseMs / 16.67));
    }

    m_events.push_back(event);
    UpdateStatistics();
}

// ─── Allocation Tracking ──────────────────────────────────────────────────────

void GCAnalyzer::RecordAllocation(
    const std::string& category,
    const std::string& name,
    size_t sizeBytes)
{
    if (!m_config.trackAllocations) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string key = category + "::" + name;
    auto& bucket = m_allocationMap[key];

    bucket.category = category;
    bucket.name = name;
    bucket.totalAllocations++;
    bucket.totalBytes += sizeBytes;

    // Estimate per-allocation GC impact (very rough)
    // This would be more accurate with actual GC pause data
    bucket.totalPauseMs += 0.001;  // Add 1us per allocation as estimate
}

void GCAnalyzer::RecordAllocationBatch(
    const std::string& category,
    const std::string& name,
    size_t totalBytes,
    int count)
{
    if (!m_config.trackAllocations) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string key = category + "::" + name;
    auto& bucket = m_allocationMap[key];

    bucket.category = category;
    bucket.name = name;
    bucket.totalAllocations += count;
    bucket.totalBytes += totalBytes;

    // Rough GC cost estimate
    double estimatedPause = (count > 0) ? (count * 0.0001) : 0.0;
    bucket.totalPauseMs += estimatedPause;
}

void GCAnalyzer::ClearAllocationData()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocationMap.clear();
}

// ─── Analysis ────────────────────────────────────────────────────────────────

GCStatistics GCAnalyzer::GetStatistics()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    UpdateStatistics();
    return m_stats;
}

void GCAnalyzer::UpdateStatistics()
{
    if (m_events.empty()) {
        m_stats = {};
        m_stats.timestamp = GetCurrentTimestamp();
        return;
    }

    m_stats.timestamp = GetCurrentTimestamp();
    m_stats.totalEvents = static_cast<int>(m_events.size());

    // Count by generation
    m_stats.gen0Events = 0;
    m_stats.gen1Events = 0;
    m_stats.gen2Events = 0;

    // Pause time collection
    std::vector<double> pauseTimes;
    pauseTimes.reserve(m_events.size());

    for (const auto& event : m_events) {
        switch (event.generation) {
            case GCGeneration::Generation0: m_stats.gen0Events++; break;
            case GCGeneration::Generation1: m_stats.gen1Events++; break;
            case GCGeneration::Generation2: m_stats.gen2Events++; break;
            default: break;
        }
        pauseTimes.push_back(event.pauseDurationMs);
        m_stats.totalPauseTimeMs += event.pauseDurationMs;

        if (event.causedSpike) {
            m_stats.frameHitchCount++;
            m_stats.totalFrameImpactMs += event.frameImpactMs;
        }
    }

    // Calculate pause statistics
    if (!pauseTimes.empty()) {
        std::sort(pauseTimes.begin(), pauseTimes.end());

        m_stats.maxPauseMs = pauseTimes.back();
        m_stats.minPauseMs = pauseTimes.front();
        m_stats.avgPauseMs = m_stats.totalPauseTimeMs / pauseTimes.size();
        m_stats.medianPauseMs = pauseTimes[pauseTimes.size() / 2];

        // Standard deviation
        double sumSq = 0.0;
        for (double t : pauseTimes) {
            double d = t - m_stats.avgPauseMs;
            sumSq += d * d;
        }
        m_stats.stdDevPauseMs = std::sqrt(sumSq / pauseTimes.size());

        // Percentiles
        int p90idx = static_cast<int>(pauseTimes.size() * 0.9);
        int p95idx = static_cast<int>(pauseTimes.size() * 0.95);
        int p99idx = static_cast<int>(pauseTimes.size() * 0.99);
        m_stats.p90PauseMs = pauseTimes[std::min(p90idx, static_cast<int>(pauseTimes.size() - 1))];
        m_stats.p95PauseMs = pauseTimes[std::min(p95idx, static_cast<int>(pauseTimes.size() - 1))];
        m_stats.p99PauseMs = pauseTimes[std::min(p99idx, static_cast<int>(pauseTimes.size() - 1))];
    }

    // Severity distribution
    m_stats.negligibleCount = 0;
    m_stats.minorCount = 0;
    m_stats.moderateCount = 0;
    m_stats.severeCount = 0;
    m_stats.criticalCount = 0;
    for (const auto& event : m_events) {
        switch (event.severity) {
            case GCSeverity::Negligible: m_stats.negligibleCount++; break;
            case GCSeverity::Minor: m_stats.minorCount++; break;
            case GCSeverity::Moderate: m_stats.moderateCount++; break;
            case GCSeverity::Severe: m_stats.severeCount++; break;
            case GCSeverity::Critical: m_stats.criticalCount++; break;
            default: break;
        }
    }

    // Frequency
    if (m_stats.totalEvents > 1 && m_events.size() >= 2) {
        int64_t durationUs = m_events.back().timestamp - m_events.front().timestamp;
        if (durationUs > 0) {
            double durationMin = durationUs / (1000.0 * 1000.0 * 60.0);
            m_stats.eventsPerMinute = m_stats.totalEvents / std::max(0.001, durationMin);
        }
    }

    // Average frame impact
    if (m_stats.totalEvents > 0) {
        m_stats.avgFrameImpactMs = m_stats.totalFrameImpactMs / m_stats.totalEvents;
    }

    // Calculate trends using recent window
    if (m_events.size() >= 60) {
        size_t recentCount = std::min(static_cast<size_t>(300), m_events.size());
        std::vector<double> recentPauses;
        for (size_t i = m_events.size() - recentCount; i < m_events.size(); ++i) {
            recentPauses.push_back(m_events[i].pauseDurationMs);
        }
        m_stats.pauseTrend = CalculateTrend(recentPauses);
    }

    // Fragmentation tracking (from recent events)
    double maxFrag = 0.0;
    double sumFrag = 0.0;
    size_t fragCount = 0;
    for (const auto& event : m_events) {
        if (event.fragmentationPercent > 0) {
            maxFrag = std::max(maxFrag, event.fragmentationPercent);
            sumFrag += event.fragmentationPercent;
            fragCount++;
        }
    }
    m_stats.peakFragmentation = maxFrag;
    m_stats.avgFragmentationPercent = fragCount > 0 ? sumFrag / fragCount : 0.0;

    // Heap size tracking
    size_t maxHeap = 0;
    size_t sumHeap = 0;
    for (const auto& event : m_events) {
        maxHeap = std::max(maxHeap, event.heapSizeBeforeBytes);
        sumHeap += event.heapSizeBeforeBytes;
    }
    m_stats.peakHeapSizeBytes = maxHeap;
    m_stats.avgHeapSizeBytes = m_events.empty() ? 0 : sumHeap / m_events.size();

    // Promotion rate
    double totalPromoted = 0.0;
    for (const auto& event : m_events) {
        totalPromoted += static_cast<double>(event.promotedBytes);
    }
    m_stats.totalPromotedBytes = static_cast<size_t>(totalPromoted);
}

GCReport GCAnalyzer::GenerateReport()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    GCReport report = {};
    report.timestamp = GetCurrentTimestamp();
    report.stats = m_stats;

    // Health score
    report.healthScore = GetHealthScore();

    // Assessment
    if (report.healthScore >= 80) {
        report.assessment = "Healthy";
        report.overallSeverity = GCSeverity::Negligible;
    } else if (report.healthScore >= 60) {
        report.assessment = "Good";
        report.overallSeverity = GCSeverity::Minor;
    } else if (report.healthScore >= 40) {
        report.assessment = "Warning";
        report.overallSeverity = GCSeverity::Moderate;
    } else if (report.healthScore >= 20) {
        report.assessment = "Poor";
        report.overallSeverity = GCSeverity::Severe;
    } else {
        report.assessment = "Critical";
        report.overallSeverity = GCSeverity::Critical;
    }

    // Recent and worst events
    size_t recentCount = std::min(static_cast<size_t>(50), m_events.size());
    report.recentEvents.assign(
        m_events.end() - recentCount,
        m_events.end()
    );
    report.worstEvents = GetWorstEventsInternal();

    // Allocation hotspots
    DetectHotspots();
    report.hotspots = m_hotspots;

    // Patterns
    report.detectedPatterns = DetectPatterns();

    // Recommendations
    GenerateRecommendationsInternal();
    report.recommendations = m_recommendations;
    report.urgentRecommendations = m_urgentRecommendations;

    // Baseline comparison
    if (m_hasBaseline) {
        report.improvementVsBaseline = CompareToBaseline();
        report.hasRegressed = report.improvementVsBaseline < -5.0;
    }

    return report;
}

std::vector<GCEvent> GCAnalyzer::GetEventsInRange(int64_t startUs, int64_t endUs) const
{
    std::vector<GCEvent> result;
    for (const auto& event : m_events) {
        if (event.timestamp >= startUs && event.timestamp <= endUs) {
            result.push_back(event);
        }
    }
    return result;
}

std::vector<GCEvent> GCAnalyzer::GetRecentEvents(int count) const
{
    size_t n = std::min(static_cast<size_t>(count), m_events.size());
    return std::vector<GCEvent>(
        m_events.end() - n,
        m_events.end()
    );
}

std::vector<GCEvent> GCAnalyzer::GetWorstEvents(int count) const
{
    // Clone events and sort by pause duration
    std::vector<GCEvent> sorted = m_events;
    std::sort(sorted.begin(), sorted.end(),
        [](const GCEvent& a, const GCEvent& b) {
            return a.pauseDurationMs > b.pauseDurationMs;
        });

    size_t n = std::min(static_cast<size_t>(count), sorted.size());
    return std::vector<GCEvent>(sorted.begin(), sorted.begin() + n);
}

std::vector<GCEvent> GCAnalyzer::GetWorstEventsInternal() const
{
    return GetWorstEvents(10);
}

// ─── Allocation Hotspots ─────────────────────────────────────────────────────

std::vector<AllocationHotspot> GCAnalyzer::GetAllocationHotspots() const
{
    return m_hotspots;
}

void GCAnalyzer::DetectHotspots()
{
    m_hotspots.clear();

    if (m_allocationMap.empty()) return;

    // Build list from map
    std::vector<std::pair<std::string, AllocationBucket>> list;
    for (const auto& kv : m_allocationMap) {
        list.push_back(kv);
    }

    // Sort by total pause time
    std::sort(list.begin(), list.end(),
        [](const auto& a, const auto& b) {
            return a.second.totalPauseMs > b.second.totalPauseMs;
        });

    // Calculate total pause time for percentage
    double totalPause = 0.0;
    for (const auto& kv : list) {
        totalPause += kv.second.totalPauseMs;
    }

    // Create hotspots
    size_t count = std::min(m_config.maxHotspots, list.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& kv = list[i];
        const auto& bucket = kv.second;

        if (bucket.totalAllocations < m_config.minAllocationsToTrack) continue;

        AllocationHotspot hotspot = {};
        hotspot.category = bucket.category;
        hotspot.name = bucket.name;
        hotspot.totalAllocations = bucket.totalAllocations;
        hotspot.totalBytes = bucket.totalBytes;
        hotspot.allocationCount = bucket.allocationCount;
        hotspot.gcTriggers = bucket.gcTriggers;
        hotspot.pauseTimeMs = bucket.totalPauseMs;
        hotspot.pauseTimePercent = totalPause > 0 ? (bucket.totalPauseMs / totalPause) * 100.0 : 0.0;
        hotspot.allocationRatePerFrame = bucket.totalAllocations / std::max(1, m_currentFrame);
        hotspot.allocationRateBytesPerSec = bucket.totalBytes / std::max(1.0, m_stats.timestamp / 1000000.0);
        hotspot.avgObjectSizeBytes = bucket.totalAllocations > 0 ?
            bucket.totalBytes / bucket.totalAllocations : 0;

        // Generate recommendation
        if (hotspot.pauseTimePercent > 30.0) {
            hotspot.recommendation = "High impact - consider object pooling or cached allocations";
            hotspot.severity = GCSeverity::Severe;
        } else if (hotspot.pauseTimePercent > 10.0) {
            hotspot.recommendation = "Moderate impact - review allocation patterns";
            hotspot.severity = GCSeverity::Moderate;
        } else {
            hotspot.severity = GCSeverity::Minor;
        }

        m_hotspots.push_back(hotspot);
    }
}

// ─── Health & Assessment ────────────────────────────────────────────────────

double GCAnalyzer::GetHealthScore() const
{
    if (m_events.empty()) return 100.0;

    double score = 100.0;

    // Deduct for critical/severe events
    double criticalDeduction = m_stats.criticalCount * 15.0;
    double severeDeduction = m_stats.severeCount * 8.0;
    double moderateDeduction = m_stats.moderateCount * 3.0;

    score -= criticalDeduction + severeDeduction + moderateDeduction;

    // Deduct for high frequency
    if (m_stats.eventsPerMinute > 100) {
        score -= 20.0;
    } else if (m_stats.eventsPerMinute > 50) {
        score -= 10.0;
    } else if (m_stats.eventsPerMinute > 20) {
        score -= 5.0;
    }

    // Deduct for long pauses
    if (m_stats.p99PauseMs > 50.0) {
        score -= 15.0;
    } else if (m_stats.p99PauseMs > 20.0) {
        score -= 8.0;
    } else if (m_stats.p99PauseMs > 10.0) {
        score -= 3.0;
    }

    // Deduct for frame hitches
    double hitchRate = m_stats.frameHitchCount / static_cast<double>(m_stats.totalEvents);
    if (hitchRate > 0.1) {
        score -= 20.0;
    } else if (hitchRate > 0.05) {
        score -= 10.0;
    } else if (hitchRate > 0.02) {
        score -= 5.0;
    }

    return std::max(0.0, std::min(100.0, score));
}

GCSeverity GCAnalyzer::GetOverallSeverity() const
{
    if (m_stats.criticalCount > 0) return GCSeverity::Critical;
    if (m_stats.severeCount > 0) return GCSeverity::Severe;
    if (m_stats.moderateCount > 0) return GCSeverity::Moderate;
    if (m_stats.minorCount > 0) return GCSeverity::Minor;
    return GCSeverity::Negligible;
}

bool GCAnalyzer::IsCausingFrameSpikes() const
{
    return m_stats.frameHitchCount > 0 &&
           (m_stats.frameHitchCount / static_cast<double>(m_stats.totalEvents)) > 0.02;
}

std::string GCAnalyzer::GetPrimaryBottleneck() const
{
    if (m_stats.gen2Events > m_stats.gen1Events && m_stats.gen2Events > m_stats.gen0Events) {
        return "Generation2";
    }
    if (m_stats.gen0Events > m_stats.gen2Events && m_stats.gen0Events > m_stats.gen1Events) {
        return "AllocationRate";
    }
    if (m_stats.avgPauseMs > 10.0) {
        return "LargeObjectHeap";
    }
    return "Mixed";
}

// ─── Recommendations ─────────────────────────────────────────────────────────

std::vector<std::string> GCAnalyzer::GetRecommendations() const
{
    return m_recommendations;
}

std::vector<std::string> GCAnalyzer::GetUrgentRecommendations() const
{
    return m_urgentRecommendations;
}

void GCAnalyzer::GenerateRecommendationsInternal()
{
    m_recommendations.clear();
    m_urgentRecommendations.clear();

    // Critical issues - urgent
    if (m_stats.criticalCount > 0) {
        m_urgentRecommendations.push_back(
            "Critical GC pauses detected (" + std::to_string(m_stats.criticalCount) +
            "). Investigate Gen2 collection frequency."
        );
    }

    if (m_stats.maxPauseMs > 100.0) {
        m_urgentRecommendations.push_back(
            "Extreme pause (>100ms) detected. Check for large object allocations."
        );
    }

    // High frequency
    if (m_stats.eventsPerMinute > 100) {
        m_urgentRecommendations.push_back(
            "Very high GC frequency (" + std::to_string(static_cast<int>(m_stats.eventsPerMinute)) +
            "/min). Review allocation patterns."
        );
    } else if (m_stats.eventsPerMinute > 50) {
        m_recommendations.push_back(
            "High GC frequency (" + std::to_string(static_cast<int>(m_stats.eventsPerMinute)) +
            "/min). Consider object pooling for frequently allocated types."
        );
    }

    // Gen2 heavy
    if (m_stats.gen2Events > 5) {
        m_recommendations.push_back(
            "Frequent Gen2 collections (" + std::to_string(m_stats.gen2Events) +
            "). Monitor promotion rate and optimize short-lived objects."
        );
    }

    // Fragmentation
    if (m_stats.peakFragmentation > 20.0) {
        m_recommendations.push_back(
            "High heap fragmentation (" + std::to_string(static_cast<int>(m_stats.peakFragmentation)) +
            "%). Consider compacting or reducing allocation churn."
        );
    }

    // Frame hitches
    if (m_stats.frameHitchCount > 0) {
        double hitchRate = m_stats.frameHitchCount / static_cast<double>(m_stats.totalEvents);
        if (hitchRate > 0.1) {
            m_urgentRecommendations.push_back(
                "High frame hitch rate (" + std::to_string(static_cast<int>(hitchRate * 100)) +
                "% of frames affected by GC). Prioritize allocation optimization."
            );
        } else {
            m_recommendations.push_back(
                std::to_string(m_stats.frameHitchCount) + " frames affected by GC pauses. " +
                "Consider incremental GC mode or reducing allocations."
            );
        }
    }

    // Pause trend
    if (m_stats.pauseTrend > 5.0) {
        m_urgentRecommendations.push_back(
            "GC pause time is increasing over time. Memory usage may be growing."
        );
    }

    // Hotspot recommendations
    for (const auto& hotspot : m_hotspots) {
        if (hotspot.severity >= GCSeverity::Severe && !hotspot.recommendation.empty()) {
            m_recommendations.push_back(
                hotspot.category + "::" + hotspot.name + ": " + hotspot.recommendation
            );
        }
    }
}

// ─── Pattern Detection ───────────────────────────────────────────────────────

std::vector<std::string> GCAnalyzer::DetectPatterns() const
{
    std::vector<std::string> patterns;

    if (m_events.size() < 10) return patterns;

    // Pattern: Frequent small GCs (likely allocation pressure)
    if (m_stats.gen0Events > m_stats.totalEvents * 0.8) {
        patterns.push_back("Allocation Pressure - Frequent Gen0 collections");
    }

    // Pattern: Gen2 dominance (likely memory leak or large working set)
    if (m_stats.gen2Events > m_stats.gen0Events * 0.1) {
        patterns.push_back("Gen2 Dominance - May indicate memory pressure or fragmentation");
    }

    // Pattern: Increasing pause trend
    if (m_stats.pauseTrend > 3.0) {
        patterns.push_back("Growing Pause Times - Memory may be accumulating");
    }

    // Pattern: Cyclic pattern (GC every N frames)
    if (m_stats.eventsPerMinute > 0) {
        double framesPerGc = 60000.0 / m_stats.eventsPerMinute / 60.0;  // rough estimate
        if (framesPerGc > 30 && framesPerGc < 300 && m_stats.gen0Events > 10) {
            patterns.push_back(
                "Cyclic GC Pattern - GC every ~" + std::to_string(static_cast<int>(framesPerGc)) + " frames"
            );
        }
    }

    // Pattern: Large object heap issues
    bool hasLargePauses = false;
    for (const auto& event : m_events) {
        if (event.pauseDurationMs > 20.0) {
            hasLargePauses = true;
            break;
        }
    }
    if (hasLargePauses) {
        patterns.push_back("Large Pause Events - Possibly Large Object Heap allocations");
    }

    // Pattern: High Gen1 transition rate
    if (m_stats.gen1Events > m_stats.gen0Events * 0.2 && m_stats.gen1Events > 3) {
        patterns.push_back("Elevated Gen1 Collections - Mid-life objects accumulating");
    }

    return patterns;
}

bool GCAnalyzer::HasMemoryLeakPattern() const
{
    if (m_stats.pauseTrend > 5.0 && m_stats.gen2Events > 3) {
        return true;
    }

    // Check for monotonically increasing heap size
    if (m_events.size() >= 10) {
        size_t firstHalf = m_events.size() / 2;
        double earlyAvg = 0.0, lateAvg = 0.0;
        for (size_t i = 0; i < firstHalf; ++i) {
            earlyAvg += m_events[i].heapSizeBeforeBytes;
        }
        for (size_t i = firstHalf; i < m_events.size(); ++i) {
            lateAvg += m_events[i].heapSizeBeforeBytes;
        }
        earlyAvg /= firstHalf;
        lateAvg /= (m_events.size() - firstHalf);

        if (lateAvg > earlyAvg * 1.5) {
            return true;
        }
    }

    return false;
}

bool GCAnalyzer::IsFragmentationBuilding() const
{
    return m_stats.avgFragmentationPercent > 10.0 || m_stats.gen2Events > 5;
}

// ─── Export ──────────────────────────────────────────────────────────────────

std::string GCAnalyzer::ExportToJSON() const
{
    auto report = const_cast<GCAnalyzer*>(this)->GenerateReport();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "{\n";
    ss << "  \"timestamp\": " << report.timestamp << ",\n";
    ss << "  \"healthScore\": " << report.healthScore << ",\n";
    ss << "  \"assessment\": \"" << report.assessment << "\",\n";
    ss << "  \"stats\": {\n";
    ss << "    \"totalEvents\": " << report.stats.totalEvents << ",\n";
    ss << "    \"gen0Events\": " << report.stats.gen0Events << ",\n";
    ss << "    \"gen1Events\": " << report.stats.gen1Events << ",\n";
    ss << "    \"gen2Events\": " << report.stats.gen2Events << ",\n";
    ss << "    \"totalPauseMs\": " << report.stats.totalPauseMs << ",\n";
    ss << "    \"avgPauseMs\": " << report.stats.avgPauseMs << ",\n";
    ss << "    \"maxPauseMs\": " << report.stats.maxPauseMs << ",\n";
    ss << "    \"p99PauseMs\": " << report.stats.p99PauseMs << ",\n";
    ss << "    \"eventsPerMinute\": " << report.stats.eventsPerMinute << ",\n";
    ss << "    \"frameHitchCount\": " << report.stats.frameHitchCount << ",\n";
    ss << "    \"eventsPerFrame\": " << report.stats.eventsPerFrame << "\n";
    ss << "  },\n";

    // Events
    ss << "  \"recentEvents\": [\n";
    for (size_t i = 0; i < report.recentEvents.size(); ++i) {
        const auto& e = report.recentEvents[i];
        ss << "    {\"gen\":" << static_cast<int>(e.generation)
           << ",\"pauseMs\":" << e.pauseDurationMs
           << ",\"trigger\":" << static_cast<int>(e.trigger)
           << ",\"severity\":" << static_cast<int>(e.severity) << "}";
        if (i < report.recentEvents.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "  ]\n";

    ss << "}\n";

    return ss.str();
}

std::string GCAnalyzer::ExportEventsToJSON() const
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "{\"events\":[\n";
    for (size_t i = 0; i < m_events.size(); ++i) {
        const auto& e = m_events[i];
        ss << "    {\"timestamp\":" << e.timestamp
           << ",\"gen\":" << static_cast<int>(e.generation)
           << ",\"pauseMs\":" << e.pauseDurationMs
           << ",\"trigger\":" << static_cast<int>(e.trigger)
           << ",\"heapBefore\":" << e.heapSizeBeforeBytes
           << ",\"heapAfter\":" << e.heapSizeAfterBytes
           << ",\"severity\":" << static_cast<int>(e.severity)
           << ",\"causedSpike\":" << (e.causedSpike ? "true" : "false") << "}";
        if (i < m_events.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "]}\n";

    return ss.str();
}

std::string GCAnalyzer::ExportStatisticsToJSON() const
{
    auto stats = GetStatistics();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "{\n";
    ss << "  \"timestamp\": " << stats.timestamp << ",\n";
    ss << "  \"totalEvents\": " << stats.totalEvents << ",\n";
    ss << "  \"pauseStats\": {\n";
    ss << "    \"total\": " << stats.totalPauseTimeMs << ",\n";
    ss << "    \"avg\": " << stats.avgPauseMs << ",\n";
    ss << "    \"max\": " << stats.maxPauseMs << ",\n";
    ss << "    \"p90\": " << stats.p90PauseMs << ",\n";
    ss << "    \"p95\": " << stats.p95PauseMs << ",\n";
    ss << "    \"p99\": " << stats.p99PauseMs << "\n";
    ss << "  },\n";
    ss << "  \"severity\": {\n";
    ss << "    \"critical\": " << stats.criticalCount << ",\n";
    ss << "    \"severe\": " << stats.severeCount << ",\n";
    ss << "    \"moderate\": " << stats.moderateCount << ",\n";
    ss << "    \"minor\": " << stats.minorCount << "\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

// ─── Integration Helpers ─────────────────────────────────────────────────────

void GCAnalyzer::OnFrameEnd(int frameNumber, double frameTimeMs, int64_t timestampUs)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_currentFrame = frameNumber;
    m_currentFrameTime = frameTimeMs;
}

void GCAnalyzer::SetBaseline(const GCStatistics& baseline)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_baseline = baseline;
    m_hasBaseline = true;
}

double GCAnalyzer::CompareToBaseline() const
{
    if (!m_hasBaseline) return 0.0;

    // Compare pause times
    double baselineAvg = m_baseline.avgPauseMs;
    double currentAvg = m_stats.avgPauseMs;

    if (baselineAvg <= 0) return 0.0;

    // Negative = improvement (lower pause is better)
    return ((baselineAvg - currentAvg) / baselineAvg) * 100.0;
}

// ─── Utility Functions ────────────────────────────────────────────────────────

int64_t GCAnalyzer::GetCurrentTimestamp() const
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

double GCAnalyzer::CalculateTrend(const std::vector<double>& values) const
{
    if (values.size() < 2) return 0.0;

    // Simple linear regression slope
    size_t n = values.size();
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;

    for (size_t i = 0; i < n; ++i) {
        sumX += static_cast<double>(i);
        sumY += values[i];
        sumXY += static_cast<double>(i) * values[i];
        sumX2 += static_cast<double>(i) * i;
    }

    double denom = n * sumX2 - sumX * sumX;
    if (std::abs(denom) < 0.0001) return 0.0;

    double slope = (n * sumXY - sumX * sumY) / denom;
    return slope;
}

double GCAnalyzer::CalculateMedian(std::vector<double> values) const
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t mid = values.size() / 2;
    if (values.size() % 2 == 0) {
        return (values[mid - 1] + values[mid]) / 2.0;
    }
    return values[mid];
}

GCGeneration GCAnalyzer::ParseGeneration(int gen) const
{
    switch (gen) {
        case 0: return GCGeneration::Generation0;
        case 1: return GCGeneration::Generation1;
        case 2: return GCGeneration::Generation2;
        default: return GCGeneration::Unknown;
    }
}

std::string GCAnalyzer::SeverityToString(GCSeverity severity) const
{
    switch (severity) {
        case GCSeverity::Negligible: return "Negligible";
        case GCSeverity::Minor: return "Minor";
        case GCSeverity::Moderate: return "Moderate";
        case GCSeverity::Severe: return "Severe";
        case GCSeverity::Critical: return "Critical";
        default: return "Unknown";
    }
}

std::string GCAnalyzer::TriggerToString(GCTrigger trigger) const
{
    switch (trigger) {
        case GCTrigger::Allocation: return "Allocation";
        case GCTrigger::Forced: return "Forced";
        case GCTrigger::MemoryPressure: return "MemoryPressure";
        case GCTrigger::LowMemory: return "LowMemory";
        case GCTrigger::Background: return "Background";
        case GCTrigger::Finalizer: return "Finalizer";
        default: return "Unknown";
    }
}

std::string GCAnalyzer::GenerationToString(GCGeneration gen) const
{
    switch (gen) {
        case GCGeneration::Generation0: return "Gen0";
        case GCGeneration::Generation1: return "Gen1";
        case GCGeneration::Generation2: return "Gen2";
        default: return "Unknown";
    }
}

} // namespace ProfilerCore