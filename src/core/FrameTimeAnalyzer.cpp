#include "FrameTimeAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <limits>
#include <unordered_map>

namespace ProfilerCore {

FrameTimeAnalyzer::FrameTimeAnalyzer()
    : m_targetFPS(60.0)
    , m_targetFrameTime(1000.0 / 60.0)
    , m_windowSize(300)
    , m_spikeMinor(1.5)
    , m_spikeModerate(2.0)
    , m_spikeSevere(3.0)
    , m_spikeCritical(5.0)
    , m_runningSum(0.0)
    , m_runningSumSq(0.0)
    , m_totalSamples(0)
    , m_dirty(true)
{
    m_frames.reserve(m_windowSize);
}

FrameTimeAnalyzer::~FrameTimeAnalyzer() {
}

void FrameTimeAnalyzer::SetTargetFPS(double targetFPS) {
    m_targetFPS = targetFPS;
    m_targetFrameTime = 1000.0 / targetFPS;
    m_dirty = true;
}

void FrameTimeAnalyzer::SetWindowSize(size_t windowSize) {
    m_windowSize = windowSize;
    if (m_frames.size() > windowSize) {
        m_frames.erase(m_frames.begin(), m_frames.end() - windowSize);
    }
    m_dirty = true;
}

void FrameTimeAnalyzer::SetSpikeThresholds(double minor, double moderate, double severe, double critical) {
    m_spikeMinor = minor;
    m_spikeModerate = moderate;
    m_spikeSevere = severe;
    m_spikeCritical = critical;
    m_dirty = true;
}

void FrameTimeAnalyzer::Reset() {
    m_frames.clear();
    m_runningSum = 0.0;
    m_runningSumSq = 0.0;
    m_totalSamples = 0;
    m_dirty = true;
    
    m_cachedReport = FrameTimeReport();
    m_cachedReport.totalFrames = 0;
    m_cachedReport.expectedFrameTime = m_targetFrameTime;
    m_cachedReport.actualFrameTime = 0.0;
    m_cachedReport.smoothnessScore = 100.0;
    m_cachedReport.responsivenessScore = 100.0;
    m_cachedReport.overallScore = 100.0;
}

void FrameTimeAnalyzer::RecordFrame(int frameNumber, double frameTimeMs) {
    FrameEntry entry;
    entry.frameNumber = frameNumber;
    entry.frameTime = frameTimeMs;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    m_frames.push_back(entry);
    
    // Update running statistics
    m_runningSum += frameTimeMs;
    m_runningSumSq += frameTimeMs * frameTimeMs;
    m_totalSamples++;
    
    // Trim to window size
    if (m_frames.size() > m_windowSize) {
        m_runningSum -= m_frames.front().frameTime;
        m_runningSumSq -= m_frames.front().frameTime * m_frames.front().frameTime;
        m_frames.erase(m_frames.begin());
    }
    
    m_dirty = true;
}

double FrameTimeAnalyzer::GetCurrentAverage() const {
    if (m_frames.empty()) return 0.0;
    return m_runningSum / static_cast<double>(m_frames.size());
}

double FrameTimeAnalyzer::GetCurrentStdDev() const {
    size_t n = m_frames.size();
    if (n < 2) return 0.0;
    
    double mean = m_runningSum / static_cast<double>(n);
    double variance = (m_runningSumSq / static_cast<double>(n)) - (mean * mean);
    if (variance < 0) variance = 0; // numerical safety
    return std::sqrt(variance);
}

int FrameTimeAnalyzer::GetSpikeCount() const {
    if (m_frames.size() < 5) return 0;
    
    double avg = GetCurrentAverage();
    if (avg <= 0) return 0;
    
    int count = 0;
    for (const auto& f : m_frames) {
        if (f.frameTime > avg * m_spikeMinor) {
            count++;
        }
    }
    return count;
}

FrameTimeReport FrameTimeAnalyzer::GenerateReport() const {
    if (m_dirty) {
        const_cast<FrameTimeAnalyzer*>(this)->RebuildReport();
    }
    return m_cachedReport;
}

void FrameTimeAnalyzer::RebuildReport() {
    m_cachedReport = FrameTimeReport();
    m_cachedReport.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    m_cachedReport.totalFrames = static_cast<int>(m_frames.size());
    m_cachedReport.expectedFrameTime = m_targetFrameTime;
    
    if (m_frames.empty()) {
        m_dirty = false;
        return;
    }
    
    ComputeDistribution();
    DetectSpikes();
    AnalyzePattern();
    ComputeScores();
    GenerateRecommendations();
    
    m_dirty = false;
}

void FrameTimeAnalyzer::ComputeDistribution() const {
    if (m_frames.empty()) return;
    
    // Define histogram buckets based on target FPS
    // Bucket 0: < target/4 (very slow)
    // Bucket 1: target/4 to target/2 (slow)
    // Bucket 2: target/2 to target (below target)
    // Bucket 3: target (ideal)
    // Bucket 4: target to 1.5*target (above target)
    // Bucket 5: > 1.5*target (very fast)
    
    double t = m_targetFrameTime; // ms
    std::vector<double> bounds = {
        0.0,
        t * 2.0,       // < target/2 = below half FPS
        t * 1.5,       // < 2/3 FPS
        t * 1.2,       // < 5/6 FPS
        t,             // target
        t * 0.9,       // > target (faster)
        t * 0.75,      // > 4/3 target
        t * 0.5        // > 2x target (very fast)
    };
    
    std::vector<std::string> labels = {
        "Very Slow (<" + std::to_string(static_cast<int>(1000.0 / (t * 2.0))) + " FPS)",
        "Slow (" + std::to_string(static_cast<int>(1000.0 / (t * 2.0))) + "-" + std::to_string(static_cast<int>(1000.0 / t)) + " FPS)",
        "Below Target (" + std::to_string(static_cast<int>(1000.0 / (t * 1.5))) + "-" + std::to_string(static_cast<int>(1000.0 / (t * 2.0))) + " FPS)",
        "Near Target (" + std::to_string(static_cast<int>(1000.0 / (t * 1.2))) + "-" + std::to_string(static_cast<int>(1000.0 / (t * 1.5))) + " FPS)",
        "At Target (" + std::to_string(static_cast<int>(1000.0 / t)) + " FPS)",
        "Above Target (" + std::to_string(static_cast<int>(1000.0 / (t * 0.9))) + "-" + std::to_string(static_cast<int>(1000.0 / t)) + " FPS)",
        "Fast (" + std::to_string(static_cast<int>(1000.0 / (t * 0.75))) + "-" + std::to_string(static_cast<int>(1000.0 / (t * 0.9))) + " FPS)",
        "Very Fast (>" + std::to_string(static_cast<int>(1000.0 / (t * 0.75))) + " FPS)"
    };
    
    int n = static_cast<int>(bounds.size());
    std::vector<int> bucketCounts(n, 0);
    
    for (const auto& f : m_frames) {
        for (int i = n - 1; i >= 0; i--) {
            if (f.frameTime <= bounds[i]) {
                bucketCounts[i]++;
                break;
            }
        }
    }
    
    double total = static_cast<double>(m_frames.size());
    m_cachedReport.distribution.clear();
    
    for (int i = 0; i < n; i++) {
        FrameTimeBucket bucket;
        bucket.lowerBound = bounds[i];
        bucket.upperBound = (i < n - 1) ? bounds[i + 1] : std::numeric_limits<double>::max();
        bucket.count = bucketCounts[i];
        bucket.percentage = (total > 0) ? (bucketCounts[i] / total * 100.0) : 0.0;
        bucket.label = labels[i];
        m_cachedReport.distribution.push_back(bucket);
    }
    
    // Compute actual average
    double sum = 0.0;
    for (const auto& f : m_frames) {
        sum += f.frameTime;
    }
    m_cachedReport.actualFrameTime = sum / total;
}

void FrameTimeAnalyzer::DetectSpikes() const {
    m_cachedReport.spikeCount = 0;
    m_cachedReport.minorSpikes = 0;
    m_cachedReport.moderateSpikes = 0;
    m_cachedReport.severeSpikes = 0;
    m_cachedReport.criticalSpikes = 0;
    m_cachedReport.topSpikes.clear();
    
    if (m_frames.size() < 5) return;
    
    double avg = GetCurrentAverage();
    if (avg <= 0) return;
    
    std::vector<FrameTimeSpike> allSpikes;
    
    for (const auto& f : m_frames) {
        double ratio = f.frameTime / avg;
        if (ratio < m_spikeMinor) continue;
        
        SpikeSeverity severity;
        if (ratio >= m_spikeCritical) {
            severity = SpikeSeverity::Critical;
        } else if (ratio >= m_spikeSevere) {
            severity = SpikeSeverity::Severe;
        } else if (ratio >= m_spikeModerate) {
            severity = SpikeSeverity::Moderate;
        } else {
            severity = SpikeSeverity::Minor;
        }
        
        // Count by severity
        switch (severity) {
            case SpikeSeverity::Minor:    m_cachedReport.minorSpikes++; break;
            case SpikeSeverity::Moderate:  m_cachedReport.moderateSpikes++; break;
            case SpikeSeverity::Severe:    m_cachedReport.severeSpikes++; break;
            case SpikeSeverity::Critical:  m_cachedReport.criticalSpikes++; break;
        }
        m_cachedReport.spikeCount++;
        
        FrameTimeSpike spike;
        spike.frameNumber = f.frameNumber;
        spike.timestamp = f.timestamp;
        spike.frameTime = f.frameTime;
        spike.averageFrameTime = avg;
        spike.spikeRatio = ratio;
        spike.severity = severity;
        spike.possibleCause = GetSpikeCauseHeuristic(ratio, avg, f.frameTime);
        
        allSpikes.push_back(spike);
    }
    
    // Sort by spike ratio descending and keep top 10
    std::sort(allSpikes.begin(), allSpikes.end(),
        [](const FrameTimeSpike& a, const FrameTimeSpike& b) {
            return a.spikeRatio > b.spikeRatio;
        });
    
    size_t topCount = std::min(static_cast<size_t>(10), allSpikes.size());
    for (size_t i = 0; i < topCount; i++) {
        m_cachedReport.topSpikes.push_back(allSpikes[i]);
    }
}

void FrameTimeAnalyzer::AnalyzePattern() const {
    PatternAnalysis& pattern = m_cachedReport.pattern;
    pattern.pattern = FrameTimePattern::Stable;
    pattern.confidence = 0.0;
    pattern.spikeInterval = 0.0;
    pattern.burstCount = 0;
    pattern.degradationRate = 0.0;
    pattern.description = "No significant pattern detected.";
    
    if (m_frames.size() < 30) return;
    
    // Collect spike positions
    double avg = GetCurrentAverage();
    std::vector<size_t> spikePositions;
    
    for (size_t i = 0; i < m_frames.size(); i++) {
        if (m_frames[i].frameTime > avg * m_spikeModerate) {
            spikePositions.push_back(i);
        }
    }
    
    if (spikePositions.empty()) {
        // Check for gradual degradation
        double trend = GetTrendLineSlope();
        if (trend > 0.05) { // positive slope = increasing frame time = degrading
            pattern.pattern = FrameTimePattern::GradualDegradation;
            pattern.confidence = 0.7;
            pattern.degradationRate = trend;
            pattern.description = "Gradually degrading frame times. Possible memory leak or increasing scene complexity.";
            return;
        }
        
        pattern.pattern = FrameTimePattern::Stable;
        pattern.confidence = 0.8;
        pattern.description = "Frame times are stable and consistent.";
        return;
    }
    
    // Detect periodic vs burst pattern
    if (spikePositions.size() >= 3) {
        // Compute intervals between spikes
        std::vector<double> intervals;
        for (size_t i = 1; i < spikePositions.size(); i++) {
            intervals.push_back(static_cast<double>(spikePositions[i] - spikePositions[i - 1]));
        }
        
        // Check variance of intervals
        double intervalMean = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
        double intervalVar = 0.0;
        for (double iv : intervals) {
            double diff = iv - intervalMean;
            intervalVar += diff * diff;
        }
        intervalVar /= intervals.size();
        double intervalStdDev = std::sqrt(intervalVar);
        
        // Low variance = periodic (CV < 0.3)
        double cv = (intervalMean > 0) ? (intervalStdDev / intervalMean) : 1.0;
        
        if (cv < 0.3) {
            // Periodic pattern
            pattern.pattern = FrameTimePattern::PeriodicSpikes;
            pattern.spikeInterval = intervalMean;
            pattern.confidence = 1.0 - cv;
            
            // Identify likely cause based on interval
            double framesPerSecond = (intervalMean > 0) ? (1.0 / intervalMean * m_frames.size()) : 0;
            
            if (intervalMean > 50 && intervalMean < 80) {
                pattern.description = "Regular spikes every ~" + std::to_string(static_cast<int>(intervalMean)) + 
                    " frames. Likely GC or fixed-timestep operations.";
            } else if (intervalMean < 10) {
                pattern.description = "Frequent periodic spikes (~" + std::to_string(static_cast<int>(intervalMean)) + 
                    " frames apart). May indicate shader compilation or streaming.";
            } else {
                pattern.description = "Periodic spike pattern detected, every ~" + 
                    std::to_string(static_cast<int>(intervalMean)) + " frames.";
            }
            return;
        }
        
        // Burst pattern: spikes clustered together
        int burstCount = 0;
        bool inBurst = false;
        int burstStart = -1;
        
        for (size_t i = 0; i < spikePositions.size(); i++) {
            if (!inBurst) {
                inBurst = true;
                burstStart = static_cast<int>(spikePositions[i]);
            }
            
            if (i + 1 < spikePositions.size()) {
                if (spikePositions[i + 1] - spikePositions[i] > 5) {
                    // Gap detected
                    if (spikePositions[i] - static_cast<size_t>(burstStart) >= 2) {
                        burstCount++;
                    }
                    inBurst = false;
                }
            }
        }
        
        // Check last burst
        if (inBurst && spikePositions.back() - static_cast<size_t>(burstStart) >= 2) {
            burstCount++;
        }
        
        if (burstCount > 0) {
            pattern.pattern = FrameTimePattern::BurstSpikes;
            pattern.burstCount = burstCount;
            pattern.confidence = 0.6;
            pattern.description = "Burst pattern detected: " + std::to_string(burstCount) + 
                " burst events. Likely scene transitions, loading, or batched operations.";
            return;
        }
    }
    
    // Erratic pattern
    pattern.pattern = FrameTimePattern::Erratic;
    pattern.confidence = 0.5;
    pattern.description = "Erratic frame time pattern with irregular spikes. " 
        "Multiple concurrent issues may be affecting performance.";
}

double FrameTimeAnalyzer::GetTrendLineSlope() const {
    if (m_frames.size() < 2) return 0.0;
    
    size_t n = m_frames.size();
    double xMean = static_cast<double>(n - 1) / 2.0;
    double ySum = 0.0;
    for (const auto& f : m_frames) {
        ySum += f.frameTime;
    }
    double yMean = ySum / static_cast<double>(n);
    
    double numerator = 0.0;
    double denominator = 0.0;
    for (size_t i = 0; i < n; i++) {
        double xDiff = static_cast<double>(i) - xMean;
        double yDiff = m_frames[i].frameTime - yMean;
        numerator += xDiff * yDiff;
        denominator += xDiff * xDiff;
    }
    
    return (denominator > 0) ? (numerator / denominator) : 0.0;
}

void FrameTimeAnalyzer::ComputeScores() const {
    if (m_frames.empty()) {
        m_cachedReport.smoothnessScore = 100.0;
        m_cachedReport.responsivenessScore = 100.0;
        m_cachedReport.overallScore = 100.0;
        return;
    }
    
    // --- Smoothness Score ---
    // Based on coefficient of variation and spike count
    double avg = GetCurrentAverage();
    double stdDev = GetCurrentStdDev();
    
    double cv = (avg > 0) ? (stdDev / avg) : 0.0;
    double spikePenalty = static_cast<double>(m_cachedReport.spikeCount) / 
                          static_cast<double>(m_frames.size()) * 30.0; // up to 30% penalty
    double cvPenalty = cv * 40.0; // up to 40% penalty for high variance
    
    double smoothness = 100.0 - std::min(70.0, cvPenalty + spikePenalty);
    m_cachedReport.smoothnessScore = std::max(0.0, std::min(100.0, smoothness));
    
    // --- Responsiveness Score ---
    // Based on tail latency (P95, P99 frame times vs target)
    if (m_frames.size() >= 10) {
        std::vector<double> sorted;
        sorted.reserve(m_frames.size());
        for (const auto& f : m_frames) {
            sorted.push_back(f.frameTime);
        }
        std::sort(sorted.begin(), sorted.end());
        
        size_t n = sorted.size();
        double p95Idx = (95.0 / 100.0) * (n - 1);
        size_t p95Lower = static_cast<size_t>(std::floor(p95Idx));
        size_t p95Upper = static_cast<size_t>(std::ceil(p95Idx));
        double p95 = (p95Lower == p95Upper || p95Upper >= n) ? 
            sorted[p95Lower] : 
            sorted[p95Lower] * (1.0 - (p95Idx - p95Lower)) + sorted[p95Upper] * (p95Idx - p95Lower);
        
        double p99Idx = (99.0 / 100.0) * (n - 1);
        size_t p99Lower = static_cast<size_t>(std::floor(p99Idx));
        size_t p99Upper = static_cast<size_t>(std::ceil(p99Idx));
        double p99 = (p99Lower == p99Upper || p99Upper >= n) ? 
            sorted[p99Lower] : 
            sorted[p99Lower] * (1.0 - (p99Idx - p99Lower)) + sorted[p99Upper] * (p99Idx - p99Lower);
        
        // Score based on how much tail latency exceeds target
        double p95Score = (m_targetFrameTime > 0) ? 
            std::max(0.0, 100.0 - (p95 / m_targetFrameTime - 1.0) * 50.0) : 100.0;
        double p99Score = (m_targetFrameTime > 0) ? 
            std::max(0.0, 100.0 - (p99 / m_targetFrameTime - 1.0) * 50.0) : 100.0;
        
        m_cachedReport.responsivenessScore = std::max(0.0, std::min(100.0, 
            p95Score * 0.5 + p99Score * 0.5));
    } else {
        m_cachedReport.responsivenessScore = 100.0;
    }
    
    // --- Overall Score ---
    m_cachedReport.overallScore = 
        m_cachedReport.smoothnessScore * 0.4 +
        m_cachedReport.responsivenessScore * 0.4 +
        (100.0 - std::min(50.0, static_cast<double>(m_cachedReport.severeSpikes + m_cachedReport.criticalSpikes))) * 0.2;
    
    m_cachedReport.overallScore = std::max(0.0, std::min(100.0, m_cachedReport.overallScore));
}

void FrameTimeAnalyzer::GenerateRecommendations() const {
    m_cachedReport.recommendations.clear();
    
    // Analyze current state and generate actionable advice
    if (m_frames.empty()) return;
    
    const auto& pattern = m_cachedReport.pattern;
    
    // Pattern-based recommendations
    if (pattern.pattern == FrameTimePattern::GradualDegradation) {
        m_cachedReport.recommendations.push_back(
            "Frame times are gradually increasing. Check for memory leaks, "
            "growing object counts, or increasing scene complexity over time."
        );
    }
    
    if (pattern.pattern == FrameTimePattern::PeriodicSpikes) {
        double interval = pattern.spikeInterval;
        if (interval > 50 && interval < 100) {
            m_cachedReport.recommendations.push_back(
                "Periodic spikes detected every ~" + std::to_string(static_cast<int>(interval)) + 
                " frames. This interval is consistent with GC pauses. Consider object pooling "
                "or reducing allocations in hot paths."
            );
        } else {
            m_cachedReport.recommendations.push_back(
                "Regular spike pattern detected. Investigate the source of the periodic "
                "operation (shader compilation, physics sync, etc.)."
            );
        }
    }
    
    if (pattern.pattern == FrameTimePattern::BurstSpikes) {
        m_cachedReport.recommendations.push_back(
            "Burst spikes detected (" + std::to_string(pattern.burstCount) + " events). "
            "Scene transitions or loading operations may be causing frame drops. "
            "Consider async loading or preloading strategies."
        );
    }
    
    if (pattern.pattern == FrameTimePattern::Erratic) {
        m_cachedReport.recommendations.push_back(
            "Erratic frame time pattern detected. Multiple concurrent performance "
            "issues may be present. Isolate and test individual systems."
        );
    }
    
    // Spike severity recommendations
    if (m_cachedReport.criticalSpikes > 0) {
        m_cachedReport.recommendations.push_back(
            "Critical frame time spikes detected (" + std::to_string(m_cachedReport.criticalSpikes) + 
            "). These are likely causing visible freezes. Investigate immediate causes: "
            "asset streaming, shader compilation, or blocking I/O."
        );
    }
    
    if (m_cachedReport.severeSpikes > m_cachedReport.totalFrames / 10) {
        m_cachedReport.recommendations.push_back(
            "Frequent severe spikes detected. More than 10% of frames are severely "
            "affected. Profile with instrumentation to identify hot functions."
        );
    }
    
    // Distribution-based recommendations
    double badPercentage = 0.0;
    for (const auto& bucket : m_cachedReport.distribution) {
        // Find bucket near target
        if (bucket.lowerBound < m_targetFrameTime && 
            bucket.upperBound > m_targetFrameTime * 0.95) {
            // This is the "at target" bucket
            if (bucket.percentage < 80.0) {
                m_cachedReport.recommendations.push_back(
                    "Only " + std::to_string(static_cast<int>(bucket.percentage)) + 
                    "% of frames are at target FPS. " +
                    std::to_string(static_cast<int>(100 - bucket.percentage)) + 
                    "% are below target. Focus optimization efforts on the "
                    "most common slow frames."
                );
            }
            break;
        }
    }
    
    // Score-based recommendations
    if (m_cachedReport.overallScore < 50.0) {
        m_cachedReport.recommendations.push_back(
            "Overall score is poor (<50). Significant performance work needed. "
            "Prioritize fixing critical spikes first, then address variance."
        );
    } else if (m_cachedReport.overallScore < 75.0) {
        m_cachedReport.recommendations.push_back(
            "Overall score is moderate (50-75). Room for improvement. "
            "Address the most impactful bottleneck first."
        );
    }
    
    // StdDev-based
    double stdDev = GetCurrentStdDev();
    double avg = GetCurrentAverage();
    double cv = (avg > 0) ? (stdDev / avg) : 0.0;
    if (cv > 0.3) {
        m_cachedReport.recommendations.push_back(
            "High frame time variance (CV > 30%). This creates inconsistent "
            "gameplay feel. Investigate sources of frame time variability."
        );
    }
    
    // Limit to top 5 recommendations
    while (m_cachedReport.recommendations.size() > 5) {
        m_cachedReport.recommendations.pop_back();
    }
}

std::string FrameTimeAnalyzer::GetSpikeCauseHeuristic(
    double spikeRatio, double avgFrameTime, double frameTime) const 
{
    // Heuristic-based cause prediction
    if (spikeRatio > 10.0) {
        return "Likely: Loading/streaming, shader compilation, or GC major collection";
    } else if (spikeRatio > 5.0) {
        return "Likely: GC minor collection, physics sync, or asset decompression";
    } else if (spikeRatio > 3.0) {
        // Check if it's part of a pattern
        return "Possible: GC, VSync pressure, or thread synchronization";
    } else {
        return "Possible: Small allocation, cache miss, or brief CPU contention";
    }
}

std::string FrameTimeAnalyzer::GetPatternDescription(
    FrameTimePattern pattern, const PatternAnalysis& analysis) const 
{
    switch (pattern) {
        case FrameTimePattern::Stable:
            return "Frame times are consistent with minimal variance.";
        case FrameTimePattern::PeriodicSpikes:
            return "Regular spikes detected every " + 
                   std::to_string(static_cast<int>(analysis.spikeInterval)) + " frames.";
        case FrameTimePattern::BurstSpikes:
            return "Clustered spike bursts (" + std::to_string(analysis.burstCount) + " events).";
        case FrameTimePattern::GradualDegradation:
            return "Slowly degrading performance.";
        case FrameTimePattern::Erratic:
        default:
            return "No consistent pattern — irregular performance.";
    }
}

std::string FrameTimeAnalyzer::ExportToJSON() const {
    FrameTimeReport report = GenerateReport();
    
    std::ostringstream ss;
    ss << "{\"frameTimeAnalyzer\":{";
    ss << "\"timestamp\":" << report.timestamp << ",";
    ss << "\"totalFrames\":" << report.totalFrames << ",";
    ss << "\"expectedFrameTimeMs\":" << std::fixed << std::setprecision(3) << report.expectedFrameTime << ",";
    ss << "\"actualFrameTimeMs\":" << report.actualFrameTime << ",";
    
    // Distribution
    ss << "\"distribution\":[";
    for (size_t i = 0; i < report.distribution.size(); i++) {
        const auto& b = report.distribution[i];
        ss << "{\"lower\":" << b.lowerBound << ",\"upper\":" << b.upperBound;
        ss << ",\"count\":" << b.count << ",\"percentage\":" << b.percentage;
        ss << ",\"label\":\"" << b.label << "\"}";
        if (i < report.distribution.size() - 1) ss << ",";
    }
    ss << "],";
    
    // Spike stats
    ss << "\"spikes\":{";
    ss << "\"total\":" << report.spikeCount << ",";
    ss << "\"minor\":" << report.minorSpikes << ",";
    ss << "\"moderate\":" << report.moderateSpikes << ",";
    ss << "\"severe\":" << report.severeSpikes << ",";
    ss << "\"critical\":" << report.criticalSpikes << "},";
    
    // Top spikes
    ss << "\"topSpikes\":[";
    for (size_t i = 0; i < report.topSpikes.size(); i++) {
        const auto& s = report.topSpikes[i];
        ss << "{\"frame\":" << s.frameNumber << ",\"frameTime\":" << s.frameTime;
        ss << ",\"ratio\":" << std::setprecision(2) << s.spikeRatio;
        ss << ",\"cause\":\"" << s.possibleCause << "\"}";
        if (i < report.topSpikes.size() - 1) ss << ",";
    }
    ss << "],";
    
    // Pattern
    ss << "\"pattern\":{";
    ss << "\"type\":" << static_cast<int>(report.pattern.pattern) << ",";
    ss << "\"confidence\":" << std::setprecision(2) << report.pattern.confidence;
    ss << ",\"interval\":" << report.pattern.spikeInterval;
    ss << ",\"burstCount\":" << report.pattern.burstCount;
    ss << ",\"description\":\"" << report.pattern.description << "\"},";
    
    // Scores
    ss << "\"scores\":{";
    ss << "\"smoothness\":" << std::setprecision(1) << report.smoothnessScore << ",";
    ss << "\"responsiveness\":" << report.responsivenessScore << ",";
    ss << "\"overall\":" << report.overallScore << "},";
    
    // Recommendations
    ss << "\"recommendations\":[";
    for (size_t i = 0; i < report.recommendations.size(); i++) {
        ss << "\"" << report.recommendations[i] << "\"";
        if (i < report.recommendations.size() - 1) ss << ",";
    }
    ss << "]}}";
    
    return ss.str();
}

} // namespace ProfilerCore
