#include "AudioProfiler.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace ProfilerCore {

// ─── Constructor / Destructor ─────────────────────────────────────────────────

AudioProfiler::AudioProfiler()
    : m_enabled(true)
    , m_sessionStartTime(0)
    , m_nextVoiceId(1)
    , m_nextDspId(1)
    , m_nextAlertId(1)
    , m_voiceStarvationEvents(0)
    , m_lowFreeVoicesFrames(0)
    , m_lastVoiceAlert(0)
    , m_totalGlitches(0)
    , m_worstGlitchSeverity(0.0)
    , m_lastGlitchTime(0)
    , m_memoryPressureEvents(0)
    , m_lastMemoryAlert(0)
    , m_latencySpikeEvents(0)
    , m_lastLatencyAlert(0)
    , m_starvationEvents(0)
    , m_dirty(true)
{
    m_samples.reserve(10000);
    m_voices.reserve(256);
    m_dspEffects.reserve(64);
}

AudioProfiler::~AudioProfiler() {}

// ─── Configuration ─────────────────────────────────────────────────────────────

void AudioProfiler::Configure(const AudioProfilerConfig& config) {
    m_config = config;
    m_dirty = true;
}

void AudioProfiler::Reset() {
    m_samples.clear();
    m_voices.clear();
    m_dspEffects.clear();
    m_alerts.clear();
    m_cpuHistory.clear();
    m_latencyHistory.clear();
    m_voiceCountHistory.clear();
    m_memoryHistory.clear();
    m_streamingBandwidth.clear();
    m_voiceIndex.clear();
    m_dspIndex.clear();
    m_voiceStarvationEvents = 0;
    m_lowFreeVoicesFrames = 0;
    m_lastVoiceAlert = 0;
    m_totalGlitches = 0;
    m_worstGlitchSeverity = 0.0;
    m_lastGlitchTime = 0;
    m_memoryPressureEvents = 0;
    m_lastMemoryAlert = 0;
    m_latencySpikeEvents = 0;
    m_lastLatencyAlert = 0;
    m_starvationEvents = 0;
    m_nextVoiceId = 1;
    m_nextDspId = 1;
    m_nextAlertId = 1;
    m_dirty = true;
}

void AudioProfiler::SetEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        Reset();
    }
}

// ─── Data Recording ───────────────────────────────────────────────────────────

void AudioProfiler::RecordFrame(const AudioSample& sample) {
    if (!m_enabled) return;

    if (m_sessionStartTime == 0) {
        m_sessionStartTime = sample.timestamp;
    }

    // Update voice statistics
    AudioSample mutableSample = sample;
    if (sample.totalVoices > 0 && sample.freeVoices >= 0) {
        m_voiceCountHistory.push_back(static_cast<double>(sample.activeVoices));
    }

    // Track voice starvation
    if (sample.freeVoices < 4) {
        m_lowFreeVoicesFrames++;
        if (m_lowFreeVoicesFrames >= 10) {
            m_voiceStarvationEvents++;
            m_lowFreeVoicesFrames = 0;
        }
    } else {
        m_lowFreeVoicesFrames = 0;
    }

    // Track glitches
    if (sample.glitchDetected) {
        m_totalGlitches++;
        if (sample.glitchSeverity > m_worstGlitchSeverity) {
            m_worstGlitchSeverity = sample.glitchSeverity;
        }
        m_lastGlitchTime = sample.timestamp;
    }

    // Track memory pressure
    if (m_config.audioMemoryLimitBytes > 0) {
        double memPercent = static_cast<double>(sample.audioMemoryUsed) /
                            static_cast<double>(m_config.audioMemoryLimitBytes) * 100.0;
        if (memPercent > m_config.memoryCriticalPercent) {
            m_memoryPressureEvents++;
        }
        m_memoryHistory.push_back(memPercent);
    }

    // Track latency spikes
    if (sample.audioLatencyMs > m_config.latencySpikeMs) {
        m_latencySpikeEvents++;
    }

    // Track streaming
    if (sample.streamingBufferUsed > 0) {
        m_streamingBandwidth.push_back(sample.streamingBufferPercent);
    }

    // Push to history
    m_samples.push_back(sample);
    m_cpuHistory.push_back(sample.totalCpuMs);
    m_latencyHistory.push_back(sample.audioLatencyMs);

    TrimHistory();
    m_dirty = true;
}

void AudioProfiler::RecordVoiceStart(int voiceId, const std::string& name,
                                      double cpuCost, size_t memoryCost,
                                      int dspEffectCount, bool isStreaming) {
    VoiceInfo info;
    info.voiceId = (voiceId >= 0) ? voiceId : m_nextVoiceId++;
    info.name = name;
    info.cpuCost = cpuCost;
    info.memoryCost = memoryCost;
    info.volume = 1.0;
    info.isStreaming = isStreaming;
    info.dspEffectCount = dspEffectCount;
    info.playbackPositionMs = 0.0;

    m_voiceIndex[info.voiceId] = m_voices.size();
    m_voices.push_back(info);
    m_dirty = true;
}

void AudioProfiler::RecordVoiceStop(int voiceId) {
    auto it = m_voiceIndex.find(voiceId);
    if (it != m_voiceIndex.end()) {
        m_voices[it->second] = m_voices.back();
        m_voiceIndex[m_voices.back().voiceId] = it->second;
        m_voices.pop_back();
        m_voiceIndex.erase(it);
        m_dirty = true;
    }
}

void AudioProfiler::RecordDSPEffect(int effectId, const std::string& name,
                                    double cpuCostMs, bool bypassed) {
    int id = (effectId >= 0) ? effectId : m_nextDspId++;
    auto it = m_dspIndex.find(id);
    if (it != m_dspIndex.end()) {
        DSPEffectInfo& info = m_dspEffects[it->second];
        info.cpuCostMs = cpuCostMs;
        info.bypassed = bypassed;
    } else {
        DSPEffectInfo info;
        info.effectId = id;
        info.name = name;
        info.cpuCostMs = cpuCostMs;
        info.bypassed = bypassed;
        info.voiceCount = 0;
        m_dspIndex[id] = m_dspEffects.size();
        m_dspEffects.push_back(info);
    }
    m_dirty = true;
}

void AudioProfiler::RecordSimulatedFrame() {
    AudioSample sample;
    sample.timestamp = GetCurrentTimestamp();

    // Simulate realistic voice counts
    int baseVoices = 24;
    int variation = (rand() % 16) - 8;
    sample.totalVoices = baseVoices + variation;
    sample.activeVoices = static_cast<int>(sample.totalVoices * 0.75);
    sample.streamingVoices = 2 + (rand() % 4);
    sample.virtualVoices = rand() % 3;
    sample.freeVoices = m_config.maxVoices - sample.totalVoices;

    // Simulate CPU load
    sample.mixerCpuMs = 0.1 + (rand() % 100) / 1000.0;
    sample.dspCpuMs = 0.3 + (rand() % 200) / 1000.0;
    sample.streamingCpuMs = 0.05 + (rand() % 50) / 1000.0;
    sample.totalCpuMs = sample.mixerCpuMs + sample.dspCpuMs + sample.streamingCpuMs;
    sample.audioThreadCpuMs = sample.totalCpuMs + 0.05;

    // Memory
    sample.audioMemoryUsed = 40 * 1024 * 1024 + (rand() % 20) * 1024 * 1024;
    sample.audioMemoryReserved = 128 * 1024 * 1024;
    sample.streamingBufferSize = 8 * 1024 * 1024;
    sample.streamingBufferUsed = static_cast<size_t>(
        sample.streamingBufferSize * (0.3 + (rand() % 50) / 100.0));
    sample.streamingBufferPercent = static_cast<double>(sample.streamingBufferUsed) /
                                    static_cast<double>(sample.streamingBufferSize) * 100.0;

    // Latency
    sample.audioLatencyMs = 15.0 + (rand() % 20);
    sample.processingLatencyMs = 3.0 + (rand() % 10);
    sample.outputLatencyMs = 10.0 + (rand() % 15);

    // DSP
    sample.activeDSPChains = 4 + (rand() % 8);
    sample.dspEffectCount = 8 + (rand() % 16);
    sample.streamingSources = 2 + (rand() % 6);

    // Config
    sample.sampleRate = 48000;
    sample.bufferSize = 512;
    sample.channelCount = 2;

    // Rare glitch simulation
    sample.glitchDetected = (rand() % 100) < 2; // 2% chance
    sample.glitchSeverity = sample.glitchDetected ? (20.0 + rand() % 80) : 0.0;

    RecordFrame(sample);
}

// ─── Analysis ─────────────────────────────────────────────────────────────────

AudioReport AudioProfiler::GenerateReport() const {
    AudioReport report;
    report.timestamp = GetCurrentTimestamp();

    if (m_samples.empty()) {
        report.audioHealthScore = 100.0;
        report.qualityScore = 100.0;
        report.efficiencyScore = 100.0;
        report.stabilityScore = 100.0;
        return report;
    }

    report.totalSamples = static_cast<int>(m_samples.size());
    report.sessionDurationMs = m_samples.back().timestamp - m_samples.front().timestamp;

    ComputeVoiceStats(report);
    ComputeCPUStats(report);
    ComputeMemoryStats(report);
    ComputeLatencyStats(report);
    ComputeStreamingStats(report);
    ComputeDSPStats(report);
    ComputeGlitchStats(report);
    ComputeHealthScores(report);
    GenerateRecommendations(report);

    // Cache and return
    m_cachedReport = report;
    m_dirty = false;
    return report;
}

double AudioProfiler::GetCurrentAudioCpuMs() const {
    if (m_cpuHistory.empty()) return 0.0;
    return m_cpuHistory.back();
}

double AudioProfiler::GetCurrentLatencyMs() const {
    if (m_latencyHistory.empty()) return 0.0;
    return m_latencyHistory.back();
}

int AudioProfiler::GetActiveVoiceCount() const {
    if (m_samples.empty()) return 0;
    return m_samples.back().activeVoices;
}

int AudioProfiler::GetFreeVoiceCount() const {
    if (m_samples.empty()) return m_config.maxVoices;
    return m_samples.back().freeVoices;
}

double AudioProfiler::GetAudioHealthScore() const {
    AudioReport r = GenerateReport();
    return r.audioHealthScore;
}

double AudioProfiler::GetGlitchRate() const {
    if (m_samples.empty()) return 0.0;
    return static_cast<double>(m_totalGlitches) / static_cast<double>(m_samples.size()) * 100.0;
}

AudioQuality AudioProfiler::GetCurrentQuality() const {
    if (m_samples.empty()) return m_config.targetQuality;
    return m_samples.back().quality;
}

int AudioProfiler::GetAlertCount() const {
    DetectAlerts();
    return static_cast<int>(m_alerts.size());
}

ConnectionState AudioProfiler::GetConnectionState() const {
    return ConnectionState::Connected;
}

// ─── Voice Management ─────────────────────────────────────────────────────────

std::vector<VoiceInfo> AudioProfiler::GetTopVoicesByCpu(int count) const {
    std::vector<VoiceInfo> sorted = m_voices;
    std::sort(sorted.begin(), sorted.end(),
              [](const VoiceInfo& a, const VoiceInfo& b) {
                  return a.cpuCost > b.cpuCost;
              });
    if (static_cast<int>(sorted.size()) > count) {
        sorted.resize(count);
    }
    return sorted;
}

std::vector<VoiceInfo> AudioProfiler::GetTopVoicesByMemory(int count) const {
    std::vector<VoiceInfo> sorted = m_voices;
    std::sort(sorted.begin(), sorted.end(),
              [](const VoiceInfo& a, const VoiceInfo& b) {
                  return a.memoryCost > b.memoryCost;
              });
    if (static_cast<int>(sorted.size()) > count) {
        sorted.resize(count);
    }
    return sorted;
}

// ─── Streaming Analysis ────────────────────────────────────────────────────────

AudioBandwidthSummary AudioProfiler::GetStreamingSummary() const {
    AudioBandwidthSummary summary = {};
    if (m_samples.empty()) return summary;

    summary.startTime = m_samples.front().timestamp;
    summary.endTime = m_samples.back().timestamp;
    summary.sampleCount = static_cast<int>(m_samples.size());

    double bwSum = 0.0, bwSumSq = 0.0;
    double maxBw = 0.0;
    int streamEvents = 0;
    size_t totalBytes = 0;

    for (const auto& s : m_samples) {
        double bw = s.streamingBufferUsed;
        bwSum += bw;
        bwSumSq += bw * bw;
        if (bw > maxBw) maxBw = bw;
        if (s.streamingVoices > 0) streamEvents++;
        totalBytes += s.streamingBufferUsed;
    }

    summary.avgDownloadBps = bwSum / summary.sampleCount;
    summary.peakDownloadBps = maxBw;
    summary.totalDownloadedBytes = static_cast<double>(totalBytes);
    summary.streamingEvents = streamEvents;
    summary.starvationEvents = m_starvationEvents;

    if (!m_streamingBandwidth.empty()) {
        double mean = bwSum / m_streamingBandwidth.size();
        summary.avgLatencyMs = ComputeMean(m_streamingBandwidth);
        summary.maxLatencyMs = *std::max_element(m_streamingBandwidth.begin(),
                                                  m_streamingBandwidth.end());
    }

    return summary;
}

// ─── Latency Statistics ───────────────────────────────────────────────────────

AudioLatencyStats AudioProfiler::GetLatencyStats() const {
    AudioLatencyStats stats = {};
    if (m_latencyHistory.empty()) return stats;

    stats.totalSamples = static_cast<int>(m_latencyHistory.size());
    stats.minLatencyMs = *std::min_element(m_latencyHistory.begin(), m_latencyHistory.end());
    stats.maxLatencyMs = *std::max_element(m_latencyHistory.begin(), m_latencyHistory.end());
    stats.avgLatencyMs = ComputeMean(m_latencyHistory);

    std::vector<double> sorted = m_latencyHistory;
    stats.p50LatencyMs = QuickSelectPercentile(sorted, 50);
    stats.p95LatencyMs = QuickSelectPercentile(sorted, 95);
    stats.p99LatencyMs = QuickSelectPercentile(sorted, 99);

    // Jitter = average absolute deviation from mean
    double mean = stats.avgLatencyMs;
    double jitterSum = 0.0;
    for (double v : m_latencyHistory) {
        jitterSum += std::abs(v - mean);
    }
    stats.jitterMs = jitterSum / m_latencyHistory.size();
    stats.spikeCount = m_latencySpikeEvents;

    return stats;
}

// ─── Internal Stats Computation ──────────────────────────────────────────────

void AudioProfiler::ComputeVoiceStats(AudioReport& out) const {
    out.peakVoices = 0;
    out.avgVoices = 0;
    out.peakActiveVoices = 0;
    out.peakFreeVoices = 0;
    out.voiceStarvationEvents = m_voiceStarvationEvents;

    if (m_samples.empty()) return;

    int64_t totalVoices = 0;
    int peakActive = 0;
    int peakFree = 0;

    for (const auto& s : m_samples) {
        if (s.totalVoices > out.peakVoices) out.peakVoices = s.totalVoices;
        if (s.activeVoices > peakActive) peakActive = s.activeVoices;
        if (s.freeVoices > peakFree) peakFree = s.freeVoices;
        totalVoices += s.totalVoices;
    }

    out.peakActiveVoices = peakActive;
    out.peakFreeVoices = peakFree;
    out.avgVoices = static_cast<int>(totalVoices / m_samples.size());
}

void AudioProfiler::ComputeCPUStats(AudioReport& out) const {
    if (m_samples.empty()) {
        out.avgAudioThreadCpuMs = 0.0;
        out.peakAudioThreadCpuMs = 0.0;
        out.avgMixerCpuMs = 0.0;
        out.avgDspCpuMs = 0.0;
        out.avgStreamingCpuMs = 0.0;
        out.audioCpuPercent = 0.0;
        return;
    }

    double threadCpuSum = 0.0, threadCpuMax = 0.0;
    double mixerSum = 0.0, dspSum = 0.0, streamSum = 0.0;

    for (const auto& s : m_samples) {
        threadCpuSum += s.audioThreadCpuMs;
        if (s.audioThreadCpuMs > threadCpuMax) threadCpuMax = s.audioThreadCpuMs;
        mixerSum += s.mixerCpuMs;
        dspSum += s.dspCpuMs;
        streamSum += s.streamingCpuMs;
    }

    int n = static_cast<int>(m_samples.size());
    out.avgAudioThreadCpuMs = threadCpuSum / n;
    out.peakAudioThreadCpuMs = threadCpuMax;
    out.avgMixerCpuMs = mixerSum / n;
    out.avgDspCpuMs = dspSum / n;
    out.avgStreamingCpuMs = streamSum / n;

    // Audio as % of frame budget (16.67ms at 60 FPS)
    double frameBudgetMs = 16.67;
    out.audioCpuPercent = (out.avgAudioThreadCpuMs / frameBudgetMs) * 100.0;
}

void AudioProfiler::ComputeMemoryStats(AudioReport& out) const {
    if (m_samples.empty()) {
        out.peakAudioMemoryUsed = 0;
        out.avgAudioMemoryUsed = 0;
        out.audioMemoryLimit = m_config.audioMemoryLimitBytes;
        out.memoryUtilizationPercent = 0.0;
        out.memoryPressureEvents = 0;
        return;
    }

    size_t peakMem = 0;
    int64_t memSum = 0;

    for (const auto& s : m_samples) {
        if (s.audioMemoryUsed > peakMem) peakMem = s.audioMemoryUsed;
        memSum += s.audioMemoryUsed;
    }

    int n = static_cast<int>(m_samples.size());
    out.peakAudioMemoryUsed = peakMem;
    out.avgAudioMemoryUsed = static_cast<size_t>(memSum / n);
    out.audioMemoryLimit = m_config.audioMemoryLimitBytes;
    out.memoryUtilizationPercent = (static_cast<double>(peakMem) /
                                    static_cast<double>(m_config.audioMemoryLimitBytes)) * 100.0;
    out.memoryPressureEvents = m_memoryPressureEvents;
}

void AudioProfiler::ComputeLatencyStats(AudioReport& out) const {
    out.latencyStats = GetLatencyStats();
}

void AudioProfiler::ComputeStreamingStats(AudioReport& out) const {
    out.streaming = GetStreamingSummary();
}

void AudioProfiler::ComputeDSPStats(AudioReport& out) const {
    if (m_samples.empty()) {
        out.peakDSPEffects = 0;
        out.peakDspCpuMs = 0.0;
        return;
    }

    int maxEffects = 0;
    double maxDspCpu = 0.0;

    for (const auto& s : m_samples) {
        if (s.dspEffectCount > maxEffects) maxEffects = s.dspEffectCount;
        if (s.dspCpuMs > maxDspCpu) maxDspCpu = s.dspCpuMs;
    }

    out.peakDSPEffects = maxEffects;
    out.peakDspCpuMs = maxDspCpu;

    // Top DSP effects by CPU
    std::vector<DSPEffectInfo> sorted = m_dspEffects;
    std::sort(sorted.begin(), sorted.end(),
              [](const DSPEffectInfo& a, const DSPEffectInfo& b) {
                  return a.cpuCostMs > b.cpuCostMs;
              });
    for (int i = 0; i < 5 && i < static_cast<int>(sorted.size()); ++i) {
        out.topDspEffects.push_back(sorted[i]);
    }
}

void AudioProfiler::ComputeGlitchStats(AudioReport& out) const {
    out.totalGlitches = m_totalGlitches;
    out.avgGlitchSeverity = (m_samples.empty() || m_totalGlitches == 0)
                                ? 0.0
                                : (m_worstGlitchSeverity / 2.0);
    out.worstGlitchSeverity = m_worstGlitchSeverity;
}

void AudioProfiler::ComputeHealthScores(AudioReport& out) const {
    // ── Overall Health Score (0-100) ───────────────────────────────────────
    double healthScore = 100.0;

    // CPU penalty: over budget = worse health
    double cpuPenalty = 0.0;
    if (out.avgAudioThreadCpuMs > m_config.maxAudioThreadCpuMs) {
        cpuPenalty = ((out.avgAudioThreadCpuMs - m_config.maxAudioThreadCpuMs) /
                       m_config.maxAudioThreadCpuMs) * 40.0;
    }
    healthScore -= cpuPenalty;

    // Memory penalty
    if (out.memoryUtilizationPercent > m_config.memoryWarningPercent) {
        double memPenalty = ((out.memoryUtilizationPercent - m_config.memoryWarningPercent) /
                              (100.0 - m_config.memoryWarningPercent)) * 25.0;
        healthScore -= memPenalty;
    }

    // Latency penalty
    if (out.latencyStats.avgLatencyMs > m_config.latencyWarningMs) {
        double latPenalty = ((out.latencyStats.avgLatencyMs - m_config.latencyWarningMs) /
                               (m_config.maxAudioLatencyMs - m_config.latencyWarningMs)) * 20.0;
        healthScore -= latPenalty;
    }

    // Voice starvation penalty
    double voicePenalty = std::min(static_cast<double>(out.voiceStarvationEvents) * 2.5, 15.0);
    healthScore -= voicePenalty;

    out.audioHealthScore = std::max(0.0, std::min(100.0, healthScore));

    // ── Quality Score ─────────────────────────────────────────────────────
    double qualityScore = 100.0;
    if (out.peakDSPEffects > 20) qualityScore -= 10.0;
    if (out.latencyStats.avgLatencyMs > 30.0) qualityScore -= 15.0;
    if (out.streaming.starvationEvents > 0) qualityScore -= 10.0;
    if (m_config.targetSampleRate != 0) {
        if (!m_samples.empty() && m_samples.back().sampleRate < m_config.targetSampleRate) {
            qualityScore -= 10.0;
        }
    }
    out.qualityScore = std::max(0.0, std::min(100.0, qualityScore));

    // ── Efficiency Score ───────────────────────────────────────────────────
    double efficiencyScore = 100.0;
    // DSP efficiency: more effects per CPU = less efficient
    if (out.peakDspCpuMs > m_config.maxDspCpuMs) {
        double dspPenalty = ((out.peakDspCpuMs - m_config.maxDspCpuMs) /
                               m_config.maxDspCpuMs) * 30.0;
        efficiencyScore -= dspPenalty;
    }
    // Streaming efficiency
    if (out.avgStreamingCpuMs > m_config.maxStreamingCpuMs) {
        double streamPenalty = ((out.avgStreamingCpuMs - m_config.maxStreamingCpuMs) /
                                  m_config.maxStreamingCpuMs) * 20.0;
        efficiencyScore -= streamPenalty;
    }
    out.efficiencyScore = std::max(0.0, std::min(100.0, efficiencyScore));

    // ── Stability Score ────────────────────────────────────────────────────
    double stabilityScore = 100.0;
    // Glitch penalty
    double glitchRate = (m_samples.empty() ? 0.0 :
                          static_cast<double>(m_totalGlitches) / m_samples.size());
    stabilityScore -= glitchRate * 200.0; // 10% glitch rate = -20 points
    // Latency jitter penalty
    if (!m_latencyHistory.empty()) {
        double latencyStdDev = ComputeStdDev(m_latencyHistory, ComputeMean(m_latencyHistory));
        stabilityScore -= std::min(latencyStdDev * 3.0, 20.0);
    }
    out.stabilityScore = std::max(0.0, std::min(100.0, stabilityScore));

    out.quality = m_config.targetQuality;
}

// ─── Alert Detection ──────────────────────────────────────────────────────────

void AudioProfiler::DetectAlerts() const {
    if (!m_config.alertsEnabled || m_samples.empty()) return;

    int64_t now = GetCurrentTimestamp();

    // Voice starvation alert
    if (m_samples.back().freeVoices < 4 && (now - m_lastVoiceAlert) > m_config.alertCooldownMs) {
        m_alerts.push_back(MakeAlert(
            AudioAlertType::VoiceStarvation,
            static_cast<double>(m_samples.back().freeVoices),
            4.0,
            "Voice starvation detected: only " + std::to_string(m_samples.back().freeVoices) +
                " free voices remaining",
            "Consider reducing simultaneous sound instances or increasing max voice count. "
                "Voice starvation causes sounds to cut out abruptly.",
            now));
        m_lastVoiceAlert = now;
    }

    // Memory pressure alert
    if (m_config.audioMemoryLimitBytes > 0) {
        double memPercent = static_cast<double>(m_samples.back().audioMemoryUsed) /
                            static_cast<double>(m_config.audioMemoryLimitBytes) * 100.0;
        if (memPercent > m_config.memoryCriticalPercent &&
            (now - m_lastMemoryAlert) > m_config.alertCooldownMs) {
            m_alerts.push_back(MakeAlert(
                AudioAlertType::MemoryPressure,
                memPercent,
                m_config.memoryCriticalPercent,
                "Audio memory pressure critical: " + std::to_string(static_cast<int>(memPercent)) + "%",
                "Audio memory usage is critically high. Consider streaming more sounds "
                    "instead of loading entirely into memory.",
                now));
            m_lastMemoryAlert = now;
        }
    }

    // Audio CPU overload alert
    if (m_samples.back().totalCpuMs > m_config.maxAudioThreadCpuMs &&
        (now - m_lastVoiceAlert) > m_config.alertCooldownMs) {
        m_alerts.push_back(MakeAlert(
            AudioAlertType::CPUOverload,
            m_samples.back().totalCpuMs,
            m_config.maxAudioThreadCpuMs,
            "Audio thread CPU overload: " + std::to_string(m_samples.back().totalCpuMs) + "ms",
            "Audio thread is consuming excessive CPU. Consider simplifying DSP chains, "
                "reducing active voices, or lowering quality settings.",
            now));
    }

    // Latency spike alert
    if (m_samples.back().audioLatencyMs > m_config.maxAudioLatencyMs &&
        (now - m_lastLatencyAlert) > m_config.alertCooldownMs) {
        m_alerts.push_back(MakeAlert(
            AudioAlertType::LatencySpike,
            m_samples.back().audioLatencyMs,
            m_config.maxAudioLatencyMs,
            "Audio latency spike: " + std::to_string(m_samples.back().audioLatencyMs) + "ms",
            "Audio latency has exceeded the maximum threshold. This can cause noticeable "
                "desync between audio and video. Check streaming buffers and DSP load.",
            now));
        m_lastLatencyAlert = now;
    }

    // Glitch/buffer underrun alert
    if (m_samples.back().glitchDetected &&
        (now - m_lastGlitchTime) > m_config.alertCooldownMs) {
        m_alerts.push_back(MakeAlert(
            AudioAlertType::BufferUnderrun,
            m_samples.back().glitchSeverity,
            m_config.glitchThresholdMs,
            "Audio buffer underrun: glitch severity " +
                std::to_string(static_cast<int>(m_samples.back().glitchSeverity)),
            "Audio system detected a buffer underrun. This causes audible clicks, pops, "
                "or dropouts. Increase buffer size or reduce streaming load.",
            now));
    }

    // Trim old alerts
    while (static_cast<int>(m_alerts.size()) > m_config.maxAlerts) {
        m_alerts.erase(m_alerts.begin());
    }
}

AudioAlert AudioProfiler::MakeAlert(AudioAlertType type, double value, double threshold,
                                    const std::string& message,
                                    const std::string& details) const {
    AudioAlert alert;
    alert.id = m_nextAlertId++;
    alert.type = type;
    alert.timestamp = GetCurrentTimestamp();
    alert.value = value;
    alert.threshold = threshold;
    alert.message = message;
    alert.details = details;
    alert.acknowledged = false;
    alert.acknowledgedAt = 0;
    alert.occurrenceCount = 1;
    return alert;
}

std::vector<AudioAlert> AudioProfiler::GetActiveAlerts() const {
    DetectAlerts();
    std::vector<AudioAlert> active;
    for (const auto& a : m_alerts) {
        if (!a.acknowledged) active.push_back(a);
    }
    return active;
}

bool AudioProfiler::AcknowledgeAlert(int alertId) {
    for (auto& a : m_alerts) {
        if (a.id == alertId) {
            a.acknowledged = true;
            a.acknowledgedAt = GetCurrentTimestamp();
            m_dirty = true;
            return true;
        }
    }
    return false;
}

void AudioProfiler::AcknowledgeAllAlerts() {
    int64_t now = GetCurrentTimestamp();
    for (auto& a : m_alerts) {
        a.acknowledged = true;
        a.acknowledgedAt = now;
    }
    m_dirty = true;
}

// ─── Recommendations ─────────────────────────────────────────────────────────

void AudioProfiler::GenerateRecommendations(AudioReport& out) const {
    out.recommendations.clear();

    // CPU recommendations
    if (out.avgAudioThreadCpuMs > m_config.maxAudioThreadCpuMs) {
        out.recommendations.push_back(
            "Audio thread CPU is " + std::to_string(out.avgAudioThreadCpuMs) +
            "ms (budget: " + std::to_string(m_config.maxAudioThreadCpuMs) +
            "ms). Consider reducing DSP complexity, limiting concurrent voices, "
            "or enabling audio LOD for distant sounds.");
    }

    // Voice recommendations
    if (out.peakActiveVoices > m_config.warningVoiceCount) {
        out.recommendations.push_back(
            "High voice count detected (peak: " + std::to_string(out.peakActiveVoices) +
            "). Implement voice virtualization to automatically cull "
            "inaudible sounds and reduce CPU load.");
    }

    if (out.voiceStarvationEvents > 0) {
        out.recommendations.push_back(
            "Voice starvation events detected: " + std::to_string(out.voiceStarvationEvents) +
            ". Increase max voice limit or implement sound priority "
            "system to gracefully handle voice exhaustion.");
    }

    // Memory recommendations
    if (out.memoryUtilizationPercent > m_config.memoryWarningPercent) {
        out.recommendations.push_back(
            "Audio memory at " + std::to_string(static_cast<int>(out.memoryUtilizationPercent)) +
            "% capacity. Consider switching more sounds to streaming mode, "
            "using procedural audio, or reducing sample quality/bit depth.");
    }

    // Latency recommendations
    if (out.latencyStats.avgLatencyMs > m_config.latencyWarningMs) {
        out.recommendations.push_back(
            "High audio latency (" + std::to_string(out.latencyStats.avgLatencyMs) +
            "ms avg). Reduce DSP chain depth, lower buffer size, "
            "or switch to lower-latency audio engine mode.");
    }

    if (out.latencyStats.jitterMs > m_config.maxLatencyJitterMs) {
        out.recommendations.push_back(
            "High latency jitter (" + std::to_string(out.latencyStats.jitterMs) +
            "ms). Inconsistent audio timing can cause stuttering. "
            "Prioritize audio thread scheduling and reduce streaming stalls.");
    }

    // DSP recommendations
    if (out.peakDSPEffects > 20) {
        out.recommendations.push_back(
            "Many active DSP effects (" + std::to_string(out.peakDSPEffects) +
            "). Consider DSP effect LOD — disable complex effects "
            "when hardware is under load.");
    }

    if (out.avgDspCpuMs > m_config.maxDspCpuMs) {
        out.recommendations.push_back(
            "DSP chain is CPU-heavy (" + std::to_string(out.avgDspCpuMs) +
            "ms). Reverb and EQ are common culprits. Profile individual "
            "effects to identify the heaviest ones.");
    }

    // Glitch recommendations
    if (out.totalGlitches > 0) {
        double glitchRate = static_cast<double>(out.totalGlitches) /
                            static_cast<double>(std::max(1, out.totalSamples)) * 100.0;
        out.recommendations.push_back(
            std::to_string(out.totalGlitches) + " audio glitches detected (" +
            std::to_string(static_cast<int>(glitchRate)) + "% rate). "
                "Increase audio buffer size or reduce streaming workload "
                "to prevent buffer underruns.");
    }

    // Streaming recommendations
    if (out.streaming.starvationEvents > 0) {
        out.recommendations.push_back(
            "Streaming buffer underruns: " + std::to_string(out.streaming.starvationEvents) +
            ". Increase streaming buffer size, reduce concurrent streams, "
            "or pre-stream critical audio assets.");
    }

    // Quality recommendations
    if (out.qualityScore < 70.0) {
        out.recommendations.push_back(
            "Audio quality score is low (" + std::to_string(static_cast<int>(out.qualityScore)) +
            "). Review audio middleware settings. Consider using "
            "compressed audio formats (Vorbis/Opus) instead of PCM to "
            "reduce memory pressure while maintaining quality.");
    }

    // If no issues found
    if (out.recommendations.empty()) {
        out.recommendations.push_back(
            "Audio system is performing within normal parameters. "
            "No critical issues detected.");
    }
}

// ─── Export ────────────────────────────────────────────────────────────────────

std::string AudioProfiler::ExportToJSON() const {
    AudioReport report = GenerateReport();

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"type\": \"audio_report\",\n";
    oss << "  \"timestamp\": " << report.timestamp << ",\n";
    oss << "  \"sessionDurationMs\": " << report.sessionDurationMs << ",\n";
    oss << "  \"totalSamples\": " << report.totalSamples << ",\n";

    // Voice stats
    oss << "  \"voice\": {\n";
    oss << "    \"peakVoices\": " << report.peakVoices << ",\n";
    oss << "    \"avgVoices\": " << report.avgVoices << ",\n";
    oss << "    \"peakActiveVoices\": " << report.peakActiveVoices << ",\n";
    oss << "    \"voiceStarvationEvents\": " << report.voiceStarvationEvents << "\n";
    oss << "  },\n";

    // CPU stats
    oss << "  \"cpu\": {\n";
    oss << "    \"avgAudioThreadCpuMs\": " << std::fixed << std::setprecision(3)
        << report.avgAudioThreadCpuMs << ",\n";
    oss << "    \"peakAudioThreadCpuMs\": " << report.peakAudioThreadCpuMs << ",\n";
    oss << "    \"avgMixerCpuMs\": " << report.avgMixerCpuMs << ",\n";
    oss << "    \"avgDspCpuMs\": " << report.avgDspCpuMs << ",\n";
    oss << "    \"avgStreamingCpuMs\": " << report.avgStreamingCpuMs << ",\n";
    oss << "    \"audioCpuPercent\": " << report.audioCpuPercent << "\n";
    oss << "  },\n";

    // Latency
    oss << "  \"latency\": {\n";
    oss << "    \"avgMs\": " << report.latencyStats.avgLatencyMs << ",\n";
    oss << "    \"minMs\": " << report.latencyStats.minLatencyMs << ",\n";
    oss << "    \"maxMs\": " << report.latencyStats.maxLatencyMs << ",\n";
    oss << "    \"p95Ms\": " << report.latencyStats.p95LatencyMs << ",\n";
    oss << "    \"jitterMs\": " << report.latencyStats.jitterMs << ",\n";
    oss << "    \"spikeCount\": " << report.latencyStats.spikeCount << "\n";
    oss << "  },\n";

    // Scores
    oss << "  \"scores\": {\n";
    oss << "    \"healthScore\": " << report.audioHealthScore << ",\n";
    oss << "    \"qualityScore\": " << report.qualityScore << ",\n";
    oss << "    \"efficiencyScore\": " << report.efficiencyScore << ",\n";
    oss << "    \"stabilityScore\": " << report.stabilityScore << "\n";
    oss << "  },\n";

    // Glitches
    oss << "  \"glitches\": {\n";
    oss << "    \"totalGlitches\": " << report.totalGlitches << ",\n";
    oss << "    \"worstSeverity\": " << report.worstGlitchSeverity << "\n";
    oss << "  },\n";

    // Recommendations
    oss << "  \"recommendations\": [";
    for (size_t i = 0; i < report.recommendations.size(); ++i) {
        oss << "\"" << report.recommendations[i] << "\"";
        if (i + 1 < report.recommendations.size()) oss << ",";
    }
    oss << "]\n";
    oss << "}\n";

    return oss.str();
}

std::string AudioProfiler::ExportVoicesCSV() const {
    std::ostringstream oss;
    oss << "voice_id,name,cpu_cost_ms,memory_bytes,volume,is_streaming,dsp_effect_count\n";
    for (const auto& v : m_voices) {
        oss << v.voiceId << ","
            << v.name << ","
            << std::fixed << std::setprecision(3) << v.cpuCost << ","
            << v.memoryCost << ","
            << v.volume << ","
            << (v.isStreaming ? "true" : "false") << ","
            << v.dspEffectCount << "\n";
    }
    return oss.str();
}

std::string AudioProfiler::ExportCPUCSV() const {
    std::ostringstream oss;
    oss << "timestamp,audio_thread_cpu_ms,mixer_cpu_ms,dsp_cpu_ms,streaming_cpu_ms,"
        << "total_cpu_ms,active_voices,latency_ms,glitch\n";
    for (const auto& s : m_samples) {
        oss << s.timestamp << ","
            << std::fixed << std::setprecision(3) << s.audioThreadCpuMs << ","
            << s.mixerCpuMs << ","
            << s.dspCpuMs << ","
            << s.streamingCpuMs << ","
            << s.totalCpuMs << ","
            << s.activeVoices << ","
            << s.audioLatencyMs << ","
            << (s.glitchDetected ? "1" : "0") << "\n";
    }
    return oss.str();
}

// ─── Internal Helpers ─────────────────────────────────────────────────────────

void AudioProfiler::TrimHistory() {
    size_t maxSize = m_config.windowSize;
    while (m_samples.size() > maxSize) {
        m_samples.erase(m_samples.begin());
    }
    while (m_cpuHistory.size() > maxSize) {
        m_cpuHistory.erase(m_cpuHistory.begin());
    }
    while (m_latencyHistory.size() > maxSize) {
        m_latencyHistory.erase(m_latencyHistory.begin());
    }
    while (m_voiceCountHistory.size() > maxSize) {
        m_voiceCountHistory.erase(m_voiceCountHistory.begin());
    }
    while (m_memoryHistory.size() > maxSize) {
        m_memoryHistory.erase(m_memoryHistory.begin());
    }
    while (m_streamingBandwidth.size() > maxSize) {
        m_streamingBandwidth.erase(m_streamingBandwidth.begin());
    }
}

double AudioProfiler::QuickSelectPercentile(std::vector<double>& arr, double percentile) const {
    if (arr.empty()) return 0.0;
    if (arr.size() == 1) return arr[0];

    size_t n = arr.size();
    size_t k = static_cast<size_t>(std::ceil(n * percentile / 100.0)) - 1;
    k = std::min(k, n - 1);

    // Simple nth_element approach
    std::nth_element(arr.begin(), arr.begin() + k, arr.end());
    return arr[k];
}

double AudioProfiler::ComputeMean(const std::vector<double>& values) const {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double AudioProfiler::ComputeStdDev(const std::vector<double>& values, double mean) const {
    if (values.size() < 2) return 0.0;
    double variance = 0.0;
    for (double v : values) {
        double diff = v - mean;
        variance += diff * diff;
    }
    variance /= values.size();
    return std::sqrt(variance);
}

int AudioProfiler::GenerateAlertId() {
    return m_nextAlertId++;
}

int64_t AudioProfiler::GetCurrentTimestamp() const {
    auto now = std::chrono::steady_clock::now();
    auto epoch = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
}

std::string AudioProfiler::AlertTypeToString(AudioAlertType type) const {
    switch (type) {
        case AudioAlertType::VoiceStarvation:    return "VoiceStarvation";
        case AudioAlertType::StreamingStarvation: return "StreamingStarvation";
        case AudioAlertType::DSPChainHeavy:       return "DSPChainHeavy";
        case AudioAlertType::LatencySpike:        return "LatencySpike";
        case AudioAlertType::MemoryPressure:      return "MemoryPressure";
        case AudioAlertType::CPUOverload:         return "CPUOverload";
        case AudioAlertType::SampleRateMismatch:  return "SampleRateMismatch";
        case AudioAlertType::BufferUnderrun:      return "BufferUnderrun";
        case AudioAlertType::LatencyHigh:         return "LatencyHigh";
        default:                                  return "Unknown";
    }
}

std::string AudioProfiler::QualityToString(AudioQuality q) const {
    switch (q) {
        case AudioQuality::Ultra:      return "Ultra";
        case AudioQuality::High:       return "High";
        case AudioQuality::Medium:     return "Medium";
        case AudioQuality::Low:        return "Low";
        case AudioQuality::Disabled:    return "Disabled";
        default:                       return "Unknown";
    }
}

} // namespace ProfilerCore
