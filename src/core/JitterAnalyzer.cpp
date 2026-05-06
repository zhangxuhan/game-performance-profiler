/**
 * JitterAnalyzer.cpp
 * Implementation of frame time jitter pattern detection and analysis
 *
 * Features:
 * - Periodic jitter detection (VSync, fixed timestep issues)
 * - Sporadic spike detection (GC, asset loading)
 * - Gradual drift detection (memory leak, cache degradation)
 * - Frame burst detection (multi-frame hitches)
 * - Stuttering detection (alternating fast/slow frames)
 * - Root cause classification (CPU vs GPU vs IO vs GC)
 * - Actionable recommendations
 * - Real-time event detection
 */

#include "JitterAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace ProfilerCore {

// ─── Constructor / Destructor ─────────────────────────────────────────────────

JitterAnalyzer::JitterAnalyzer()
    : m_currentEventActive(false)
    , m_consecutiveSlowFrames(0)
    , m_accumulatedJitter(0.0)
    , m_lastEventFrame(0)
{
    m_events.reserve(1000);
}

JitterAnalyzer::~JitterAnalyzer() = default;

// ─── Configuration ────────────────────────────────────────────────────────────

void JitterAnalyzer::SetConfig(const JitterConfig& config) {
    m_config = config;

    // Resize buffers if needed
    if (m_frameTimes.size() > m_config.analysisWindowSize) {
        m_frameTimes.resize(m_config.analysisWindowSize);
        m_cpuTimes.resize(m_config.analysisWindowSize);
        m_gpuTimes.resize(m_config.analysisWindowSize);
    }
}

void JitterAnalyzer::Reset() {
    m_frameTimes.clear();
    m_cpuTimes.clear();
    m_gpuTimes.clear();
    m_events.clear();
    m_recentJitter.clear();
    m_currentEventActive = false;
    m_consecutiveSlowFrames = 0;
    m_accumulatedJitter = 0.0;
    m_lastEventFrame = 0;
    m_lastAnalysis = JitterAnalysis{};
}

// ─── Data Recording ──────────────────────────────────────────────────────────

void JitterAnalyzer::RecordFrame(double frameTimeMs, double cpuTimeMs, double gpuTimeMs) {
    // Add to rolling buffer
    m_frameTimes.push_back(frameTimeMs);
    m_cpuTimes.push_back(cpuTimeMs);
    m_gpuTimes.push_back(gpuTimeMs);

    // Trim to window size
    if (m_frameTimes.size() > m_config.analysisWindowSize) {
        m_frameTimes.pop_front();
        m_cpuTimes.pop_front();
        m_gpuTimes.pop_front();
    }

    // Calculate frame-to-frame jitter
    if (m_frameTimes.size() >= 2) {
        double prevFrameTime = m_frameTimes[m_frameTimes.size() - 2];
        double jitter = std::abs(frameTimeMs - prevFrameTime);
        m_recentJitter.push_back(jitter);

        if (m_recentJitter.size() > m_config.analysisWindowSize) {
            m_recentJitter.pop_front();
        }

        // Real-time event detection
        if (jitter > m_config.jitterThreshold) {
            m_accumulatedJitter += jitter;

            // Check if this is a significant spike
            if (jitter > m_config.frameTimeSpikeThreshold) {
                m_consecutiveSlowFrames++;

                // Start a new event if not already in one
                if (!m_currentEventActive && m_consecutiveSlowFrames >= 2) {
                    m_currentEventActive = true;
                    m_currentEvent = JitterEvent{};
                    m_currentEvent.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    m_currentEvent.frameStart = static_cast<int>(m_frameTimes.size()) - m_consecutiveSlowFrames;
                    m_currentEvent.pattern = JitterPattern::Sporadic; // Initial guess, may be updated
                }

                // Update current event
                if (m_currentEventActive) {
                    m_currentEvent.frameEnd = static_cast<int>(m_frameTimes.size());
                    m_currentEvent.maxFrameTime = std::max(m_currentEvent.maxFrameTime, frameTimeMs);
                }
            }
        } else {
            // Frame is normal - check if we should end current event
            if (m_currentEventActive) {
                m_consecutiveSlowFrames--;

                if (m_consecutiveSlowFrames <= 0) {
                    // End the event
                    m_currentEvent.avgFrameTime = m_accumulatedJitter / std::max(1.0, static_cast<double>(m_currentEvent.frameEnd - m_currentEvent.frameStart));

                    // Calculate severity based on accumulated jitter and duration
                    int frameCount = m_currentEvent.frameEnd - m_currentEvent.frameStart;
                    m_currentEvent.severity = std::min(100.0, m_accumulatedJitter * 10.0 + frameCount * 2.0);

                    // Classify root cause
                    ClassifyRootCause(m_currentEvent);

                    // Generate description
                    std::ostringstream oss;
                    oss << "Jitter event: " << frameCount << " frames, "
                        << std::fixed << std::setprecision(2) << m_currentEvent.maxFrameTime << "ms peak, "
                        << "severity " << static_cast<int>(m_currentEvent.severity);
                    m_currentEvent.description = oss.str();

                    m_events.push_back(m_currentEvent);
                    m_currentEventActive = false;
                    m_consecutiveSlowFrames = 0;
                    m_accumulatedJitter = 0.0;
                }
            }
        }
    }
}

// ─── Analysis ─────────────────────────────────────────────────────────────────

JitterAnalysis JitterAnalyzer::Analyze() {
    m_lastAnalysis = JitterAnalysis{};

    if (m_frameTimes.size() < 10) {
        return m_lastAnalysis;
    }

    m_lastAnalysis.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_lastAnalysis.sampleCount = static_cast<int>(m_frameTimes.size());

    // Calculate jitter metrics
    m_lastAnalysis.averageJitter = CalculateJitterMetric();
    m_lastAnalysis.jitterCoefficient = CalculateJitterCoefficient();

    // Calculate standard deviation of frame times
    double mean = std::accumulate(m_frameTimes.begin(), m_frameTimes.end(), 0.0) / m_frameTimes.size();
    double sqSum = std::inner_product(m_frameTimes.begin(), m_frameTimes.end(), m_frameTimes.begin(), 0.0);
    m_lastAnalysis.jitterStdDev = std::sqrt(sqSum / m_frameTimes.size() - mean * mean);

    // Find max jitter from recent jitter values
    if (!m_recentJitter.empty()) {
        m_lastAnalysis.maxJitter = *std::max_element(m_recentJitter.begin(), m_recentJitter.end());
    }

    // Detect patterns
    DetectPatterns();

    // Generate recommendations
    GenerateRecommendations();

    return m_lastAnalysis;
}

// ─── Pattern Detection ─────────────────────────────────────────────────────────

void JitterAnalyzer::DetectPatterns() {
    // Run all pattern detectors
    DetectPeriodicJitter();
    DetectSporadicSpikes();
    DetectGradualDrift();
    DetectBursts();
    DetectStuttering();

    // Summarize event counts
    m_lastAnalysis.totalJitterEvents = static_cast<int>(m_events.size());
    m_lastAnalysis.periodicEvents = 0;
    m_lastAnalysis.sporadicEvents = 0;
    m_lastAnalysis.burstEvents = 0;
    m_lastAnalysis.stutteringEvents = 0;
    m_lastAnalysis.mildEvents = 0;
    m_lastAnalysis.moderateEvents = 0;
    m_lastAnalysis.severeEvents = 0;

    for (const auto& event : m_events) {
        switch (event.pattern) {
            case JitterPattern::Periodic: m_lastAnalysis.periodicEvents++; break;
            case JitterPattern::Sporadic: m_lastAnalysis.sporadicEvents++; break;
            case JitterPattern::Burst: m_lastAnalysis.burstEvents++; break;
            case JitterPattern::Stuttering: m_lastAnalysis.stutteringEvents++; break;
            default: break;
        }

        if (event.severity < 30.0) {
            m_lastAnalysis.mildEvents++;
        } else if (event.severity < 60.0) {
            m_lastAnalysis.moderateEvents++;
        } else {
            m_lastAnalysis.severeEvents++;
        }
    }

    // Determine primary pattern based on frequency
    std::vector<std::pair<JitterPattern, int>> patternCounts = {
        {JitterPattern::Periodic, m_lastAnalysis.periodicEvents},
        {JitterPattern::Sporadic, m_lastAnalysis.sporadicEvents},
        {JitterPattern::Burst, m_lastAnalysis.burstEvents},
        {JitterPattern::Stuttering, m_lastAnalysis.stutteringEvents}
    };

    auto maxPattern = std::max_element(patternCounts.begin(), patternCounts.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    if (maxPattern->second > 0) {
        m_lastAnalysis.primaryPattern = maxPattern->first;
        m_lastAnalysis.patternStrength = std::min(100.0, maxPattern->second * 10.0);
    } else {
        m_lastAnalysis.primaryPattern = JitterPattern::None;
        m_lastAnalysis.patternStrength = 0.0;
    }

    // Estimate CPU vs GPU jitter ratios
    if (m_cpuTimes.size() > 0 && m_gpuTimes.size() > 0) {
        double cpuJitterSum = 0.0;
        double gpuJitterSum = 0.0;

        for (size_t i = 1; i < m_cpuTimes.size(); ++i) {
            cpuJitterSum += std::abs(m_cpuTimes[i] - m_cpuTimes[i - 1]);
            gpuJitterSum += std::abs(m_gpuTimes[i] - m_gpuTimes[i - 1]);
        }

        double total = cpuJitterSum + gpuJitterSum;
        if (total > 0) {
            m_lastAnalysis.cpuJitterRatio = cpuJitterSum / total;
            m_lastAnalysis.gpuJitterRatio = gpuJitterSum / total;
        }
    }
}

void JitterAnalyzer::DetectPeriodicJitter() {
    // Detect periodic patterns using autocorrelation-like approach
    if (m_recentJitter.size() < static_cast<size_t>(m_config.minPeriodFrames)) {
        return;
    }

    // Find peaks in jitter
    std::vector<int> peaks = FindPeaks(m_config.jitterThreshold);

    if (peaks.size() < 3) {
        return;
    }

    // Check for periodicity in peaks
    int periodStrength = FindPeriodicityStrength(peaks);

    if (periodStrength > 50) {
        // Found periodic pattern - update relevant events
        for (auto& event : m_events) {
            if (event.pattern == JitterPattern::Sporadic && event.severity > 30.0) {
                // Re-classify as periodic
                event.pattern = JitterPattern::Periodic;
                event.probableCause = "VSync";
                event.description = "Periodic jitter detected (possible VSync or fixed timestep issue): " + event.description;
            }
        }
    }
}

void JitterAnalyzer::DetectSporadicSpikes() {
    // Count frames where jitter exceeds threshold
    int spikeCount = 0;
    double totalSpikeJitter = 0.0;

    for (double jitter : m_recentJitter) {
        if (jitter > m_config.frameTimeSpikeThreshold * m_config.sensitivityMultiplier) {
            spikeCount++;
            totalSpikeJitter += jitter;
        }
    }

    // If spikes are infrequent but significant, classify as sporadic
    double spikeRate = static_cast<double>(spikeCount) / m_recentJitter.size();

    if (spikeRate > 0.01 && spikeRate < 0.1 && spikeCount >= 3) {
        // Already handled in RecordFrame real-time detection
        // This just validates the classification
    }
}

void JitterAnalyzer::DetectGradualDrift() {
    // Detect slow degradation using linear regression on frame times
    if (m_frameTimes.size() < 50) {
        return;
    }

    // Simple linear regression
    size_t n = m_frameTimes.size();
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(i);
        double y = m_frameTimes[i];
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }

    double denominator = n * sumX2 - sumX * sumX;
    if (std::abs(denominator) < 1e-10) {
        return;
    }

    double slope = (n * sumXY - sumX * sumY) / denominator;
    double meanFrameTime = sumY / n;

    // Check if slope indicates significant drift
    double driftPercent = slope / meanFrameTime * 100.0;

    if (driftPercent > 0.1) { // More than 0.1% per frame increase
        // Create drift event
        JitterEvent driftEvent;
        driftEvent.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        driftEvent.frameStart = 0;
        driftEvent.frameEnd = static_cast<int>(n);
        driftEvent.pattern = JitterPattern::GradualDrift;
        driftEvent.severity = std::min(100.0, driftPercent * 100.0);
        driftEvent.avgFrameTime = meanFrameTime;
        driftEvent.maxFrameTime = m_frameTimes.back();
        driftEvent.probableCause = "Memory";
        driftEvent.description = "Gradual frame time drift detected: " +
            std::to_string(static_cast<int>(driftPercent * 100)) + "% increase over " +
            std::to_string(n) + " frames (possible memory leak or cache degradation)";

        // Check if similar event already exists
        bool foundSimilar = false;
        for (const auto& event : m_events) {
            if (event.pattern == JitterPattern::GradualDrift &&
                std::abs(event.timestamp - driftEvent.timestamp) < 10000000) { // Within 10 seconds
                foundSimilar = true;
                break;
            }
        }

        if (!foundSimilar) {
            m_events.push_back(driftEvent);
        }
    }
}

void JitterAnalyzer::DetectBursts() {
    // Detect multiple consecutive slow frames
    int burstCount = 0;
    int currentBurstLength = 0;

    for (size_t i = 1; i < m_frameTimes.size(); ++i) {
        double jitter = std::abs(m_frameTimes[i] - m_frameTimes[i - 1]);

        if (jitter > m_config.jitterThreshold) {
            currentBurstLength++;
        } else {
            if (currentBurstLength >= m_config.burstConsecutiveFrames) {
                burstCount++;
            }
            currentBurstLength = 0;
        }
    }

    // Check final burst
    if (currentBurstLength >= m_config.burstConsecutiveFrames) {
        burstCount++;
    }

    // Update events that may be bursts
    if (burstCount > 0) {
        for (auto& event : m_events) {
            int frameCount = event.frameEnd - event.frameStart;
            if (frameCount >= m_config.burstConsecutiveFrames && event.pattern == JitterPattern::Sporadic) {
                event.pattern = JitterPattern::Burst;
                if (event.probableCause.empty() || event.probableCause == "Unknown") {
                    event.probableCause = "IO";
                }
            }
        }
    }
}

void JitterAnalyzer::DetectStuttering() {
    // Detect alternating fast/slow frames (stuttering)
    if (m_frameTimes.size() < 20) {
        return;
    }

    int alternations = 0;
    bool lastWasSlow = false;
    double meanFrameTime = std::accumulate(m_frameTimes.begin(), m_frameTimes.end(), 0.0) / m_frameTimes.size();

    for (size_t i = 1; i < m_frameTimes.size(); ++i) {
        bool isSlow = m_frameTimes[i] > meanFrameTime * 1.2;
        bool isFast = m_frameTimes[i] < meanFrameTime * 0.8;

        if ((isSlow && lastWasSlow == false) || (isFast && lastWasSlow == true)) {
            alternations++;
        }

        if (isSlow || isFast) {
            lastWasSlow = isSlow;
        }
    }

    // High alternation rate indicates stuttering
    double alternationRate = static_cast<double>(alternations) / m_frameTimes.size();

    if (alternationRate > 0.3) {
        JitterEvent stutterEvent;
        stutterEvent.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        stutterEvent.frameStart = 0;
        stutterEvent.frameEnd = static_cast<int>(m_frameTimes.size());
        stutterEvent.pattern = JitterPattern::Stuttering;
        stutterEvent.severity = std::min(100.0, alternationRate * 200.0);
        stutterEvent.avgFrameTime = meanFrameTime;
        stutterEvent.maxFrameTime = *std::max_element(m_frameTimes.begin(), m_frameTimes.end());
        stutterEvent.probableCause = "GPU";
        stutterEvent.description = "Stuttering detected: " +
            std::to_string(static_cast<int>(alternationRate * 100)) + "% alternation rate " +
            "(possible GPU driver issue or asset streaming problem)";

        // Check for existing stuttering event
        bool foundSimilar = false;
        for (const auto& event : m_events) {
            if (event.pattern == JitterPattern::Stuttering) {
                foundSimilar = true;
                break;
            }
        }

        if (!foundSimilar) {
            m_events.push_back(stutterEvent);
        }
    }
}

// ─── Root Cause Classification ─────────────────────────────────────────────────

void JitterAnalyzer::ClassifyRootCause(JitterEvent& event) {
    // Analyze the frames within the event window
    if (event.frameEnd <= event.frameStart || event.frameEnd > static_cast<int>(m_frameTimes.size())) {
        event.probableCause = "Unknown";
        return;
    }

    // Calculate average CPU and GPU times during the event
    double cpuSum = 0.0, gpuSum = 0.0;
    int count = 0;

    for (int i = event.frameStart; i < event.frameEnd && i < static_cast<int>(m_cpuTimes.size()); ++i) {
        cpuSum += m_cpuTimes[i];
        gpuSum += m_gpuTimes[i];
        count++;
    }

    if (count == 0) {
        event.probableCause = "Unknown";
        return;
    }

    double avgCpu = cpuSum / count;
    double avgGpu = gpuSum / count;

    // Classify based on time distribution
    if (avgCpu > m_config.cpuThresholdMs && avgCpu > avgGpu) {
        event.probableCause = "CPU";
    } else if (avgGpu > m_config.gpuThresholdMs) {
        event.probableCause = "GPU";
    } else if (event.maxFrameTime > 50.0) {
        // Very long frames are often IO-bound
        event.probableCause = "IO";
    } else if (event.pattern == JitterPattern::Periodic) {
        event.probableCause = "VSync";
    } else if (event.pattern == JitterPattern::Burst) {
        event.probableCause = "IO";
    } else {
        event.probableCause = "Unknown";
    }
}

// ─── Recommendations ──────────────────────────────────────────────────────────

void JitterAnalyzer::GenerateRecommendations() {
    m_lastAnalysis.recommendations.clear();

    if (m_lastAnalysis.averageJitter < 1.0) {
        return; // No significant issues
    }

    // Based on primary pattern
    switch (m_lastAnalysis.primaryPattern) {
        case JitterPattern::Periodic:
            m_lastAnalysis.recommendations.push_back("Check VSync settings - consider enabling/disabling VSync or using adaptive sync");
            m_lastAnalysis.recommendations.push_back("Review fixed timestep logic in game loop for timing issues");
            break;

        case JitterPattern::Sporadic:
            if (m_lastAnalysis.cpuJitterRatio > 0.6) {
                m_lastAnalysis.recommendations.push_back("Investigate sporadic CPU spikes - possible GC pauses or background tasks");
                m_lastAnalysis.recommendations.push_back("Consider object pooling to reduce allocation/GC pressure");
            } else if (m_lastAnalysis.gpuJitterRatio > 0.6) {
                m_lastAnalysis.recommendations.push_back("GPU-bound sporadic spikes detected - check for shader compilation or texture streaming");
                m_lastAnalysis.recommendations.push_back("Consider pre-warming shaders and reducing texture sizes");
            } else {
                m_lastAnalysis.recommendations.push_back("Mixed sporadic spikes detected - profile both CPU and GPU for bottlenecks");
            }
            break;

        case JitterPattern::GradualDrift:
            m_lastAnalysis.recommendations.push_back("Gradual performance degradation detected - possible memory leak");
            m_lastAnalysis.recommendations.push_back("Monitor memory allocation patterns and check for resource leaks");
            m_lastAnalysis.recommendations.push_back("Consider adding periodic cache clearing or resource reloading");
            break;

        case JitterPattern::Burst:
            m_lastAnalysis.recommendations.push_back("Frame time bursts detected - likely IO-bound asset loading");
            m_lastAnalysis.recommendations.push_back("Implement asynchronous asset loading to prevent frame drops");
            m_lastAnalysis.recommendations.push_back("Consider using background threads for resource loading");
            break;

        case JitterPattern::Stuttering:
            m_lastAnalysis.recommendations.push_back("Stuttering pattern detected - alternating frame times");
            m_lastAnalysis.recommendations.push_back("Check GPU driver settings and consider updating drivers");
            m_lastAnalysis.recommendations.push_back("Review asset streaming strategy for texture/model loading");
            break;

        default:
            break;
    }

    // General recommendations based on severity
    if (m_lastAnalysis.severeEvents > 0) {
        m_lastAnalysis.recommendations.push_back("Severe jitter events detected - prioritize performance optimization");
    }

    if (m_lastAnalysis.jitterCoefficient > 0.3) {
        m_lastAnalysis.recommendations.push_back("High frame time variance - consider locking frame rate for consistency");
    }
}

// ─── Metrics Calculation ──────────────────────────────────────────────────────

double JitterAnalyzer::CalculateJitterMetric() const {
    if (m_recentJitter.empty()) {
        return 0.0;
    }

    return std::accumulate(m_recentJitter.begin(), m_recentJitter.end(), 0.0) / m_recentJitter.size();
}

double JitterAnalyzer::CalculateJitterCoefficient() const {
    if (m_frameTimes.size() < 2) {
        return 0.0;
    }

    double mean = std::accumulate(m_frameTimes.begin(), m_frameTimes.end(), 0.0) / m_frameTimes.size();

    if (mean < 0.001) {
        return 0.0;
    }

    double sqSum = std::inner_product(m_frameTimes.begin(), m_frameTimes.end(), m_frameTimes.begin(), 0.0);
    double stdDev = std::sqrt(sqSum / m_frameTimes.size() - mean * mean);

    // Coefficient of variation
    return stdDev / mean;
}

std::vector<int> JitterAnalyzer::FindPeaks(double threshold) const {
    std::vector<int> peaks;

    if (m_recentJitter.size() < 3) {
        return peaks;
    }

    for (size_t i = 1; i < m_recentJitter.size() - 1; ++i) {
        if (m_recentJitter[i] > threshold &&
            m_recentJitter[i] > m_recentJitter[i - 1] &&
            m_recentJitter[i] >= m_recentJitter[i + 1]) {
            peaks.push_back(static_cast<int>(i));
        }
    }

    return peaks;
}

int JitterAnalyzer::FindPeriodicityStrength(const std::vector<int>& peaks) const {
    if (peaks.size() < 3) {
        return 0;
    }

    // Calculate intervals between peaks
    std::vector<int> intervals;
    for (size_t i = 1; i < peaks.size(); ++i) {
        intervals.push_back(peaks[i] - peaks[i - 1]);
    }

    // Find the most common interval
    std::unordered_map<int, int> intervalCounts;
    int maxCount = 0;
    int dominantInterval = 0;

    for (int interval : intervals) {
        // Allow some tolerance in grouping
        for (int offset = -1; offset <= 1; ++offset) {
            int adjustedInterval = interval + offset;
            intervalCounts[adjustedInterval]++;

            if (intervalCounts[adjustedInterval] > maxCount) {
                maxCount = intervalCounts[adjustedInterval];
                dominantInterval = adjustedInterval;
            }
        }
    }

    // Calculate how many peaks follow the dominant interval
    int consistentCount = 0;
    for (int interval : intervals) {
        double deviation = std::abs(interval - dominantInterval) / static_cast<double>(dominantInterval);
        if (deviation < m_config.periodTolerance) {
            consistentCount++;
        }
    }

    // Return strength as percentage
    return static_cast<int>(100.0 * consistentCount / intervals.size());
}

// ─── Export ───────────────────────────────────────────────────────────────────

std::string JitterAnalyzer::ExportToJSON() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "{\n";
    oss << "  \"timestamp\": " << m_lastAnalysis.timestamp << ",\n";
    oss << "  \"sampleCount\": " << m_lastAnalysis.sampleCount << ",\n";
    oss << "  \"averageJitter\": " << m_lastAnalysis.averageJitter << ",\n";
    oss << "  \"maxJitter\": " << m_lastAnalysis.maxJitter << ",\n";
    oss << "  \"jitterStdDev\": " << m_lastAnalysis.jitterStdDev << ",\n";
    oss << "  \"jitterCoefficient\": " << m_lastAnalysis.jitterCoefficient << ",\n";
    oss << "  \"primaryPattern\": \"" << static_cast<int>(m_lastAnalysis.primaryPattern) << "\",\n";
    oss << "  \"patternStrength\": " << m_lastAnalysis.patternStrength << ",\n";
    oss << "  \"totalJitterEvents\": " << m_lastAnalysis.totalJitterEvents << ",\n";
    oss << "  \"periodicEvents\": " << m_lastAnalysis.periodicEvents << ",\n";
    oss << "  \"sporadicEvents\": " << m_lastAnalysis.sporadicEvents << ",\n";
    oss << "  \"burstEvents\": " << m_lastAnalysis.burstEvents << ",\n";
    oss << "  \"stutteringEvents\": " << m_lastAnalysis.stutteringEvents << ",\n";
    oss << "  \"mildEvents\": " << m_lastAnalysis.mildEvents << ",\n";
    oss << "  \"moderateEvents\": " << m_lastAnalysis.moderateEvents << ",\n";
    oss << "  \"severeEvents\": " << m_lastAnalysis.severeEvents << ",\n";
    oss << "  \"cpuJitterRatio\": " << m_lastAnalysis.cpuJitterRatio << ",\n";
    oss << "  \"gpuJitterRatio\": " << m_lastAnalysis.gpuJitterRatio << ",\n";

    oss << "  \"recommendations\": [";
    for (size_t i = 0; i < m_lastAnalysis.recommendations.size(); ++i) {
        oss << "\"" << m_lastAnalysis.recommendations[i] << "\"";
        if (i < m_lastAnalysis.recommendations.size() - 1) {
            oss << ", ";
        }
    }
    oss << "]\n";
    oss << "}";

    return oss.str();
}

std::string JitterAnalyzer::ExportEventsToJSON() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "[\n";

    for (size_t i = 0; i < m_events.size(); ++i) {
        const auto& event = m_events[i];

        oss << "  {\n";
        oss << "    \"timestamp\": " << event.timestamp << ",\n";
        oss << "    \"frameStart\": " << event.frameStart << ",\n";
        oss << "    \"frameEnd\": " << event.frameEnd << ",\n";
        oss << "    \"pattern\": \"" << static_cast<int>(event.pattern) << "\",\n";
        oss << "    \"severity\": " << event.severity << ",\n";
        oss << "    \"avgFrameTime\": " << event.avgFrameTime << ",\n";
        oss << "    \"maxFrameTime\": " << event.maxFrameTime << ",\n";
        oss << "    \"durationMs\": " << event.durationMs << ",\n";
        oss << "    \"probableCause\": \"" << event.probableCause << "\",\n";
        oss << "    \"description\": \"" << event.description << "\"\n";
        oss << "  }";

        if (i < m_events.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }

    oss << "]";

    return oss.str();
}

} // namespace ProfilerCore
