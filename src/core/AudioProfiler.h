#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace ProfilerCore {

// ─── Enumerations ─────────────────────────────────────────────────────────────

/**
 * Audio processing quality level
 */
enum class AudioQuality {
    Ultra = 0,        // Highest quality, most CPU
    High = 1,         // High quality
    Medium = 2,       // Balanced
    Low = 3,          // Performance mode
    Disabled = 4      // Audio disabled
};

/**
 * Audio middleware / engine type
 */
enum class AudioEngineType {
    Wwise = 0,        // Audiokinetic Wwise
    FMOD = 1,         // Firelight FMOD
    UnrealAudio = 2,  // Unreal Engine audio
    UnityAudio = 3,   // Unity Audio
    XAudio2 = 4,      // Microsoft XAudio2 (direct)
    OpenAL = 5,        // OpenAL
    Custom = 99       // Unknown/custom
};

/**
 * Audio alert type
 */
enum class AudioAlertType {
    VoiceStarvation,     // No free voices available
    StreamingStarvation, // Streaming buffer underrun
    DSPChainHeavy,       // DSP chain too heavy
    LatencySpike,        // Audio latency spike
    MemoryPressure,      // Audio memory near limit
    CPUOverload,         // Audio thread CPU too high
    SampleRateMismatch,  // Sample rate configuration issue
    BufferUnderrun,      // Audio buffer underrun (glitches)
    LatencyHigh          // Audio latency above threshold
};

// ─── Data Structures ──────────────────────────────────────────────────────────

/**
 * A single audio frame sample
 */
struct AudioSample {
    int64_t timestamp;

    // Voice / instance counts
    int totalVoices;
    int activeVoices;
    int streamingVoices;
    int virtualVoices;      // Voices swapped to "virtual" (culled)
    int freeVoices;          // Available voice slots

    // CPU metrics
    double audioThreadCpuMs;     // Audio thread CPU time (ms/frame)
    double mixerCpuMs;           // Mixer processing time
    double streamingCpuMs;       // Streaming/decompression time
    double dspCpuMs;             // DSP chain processing time
    double totalCpuMs;           // Total audio CPU

    // Memory
    size_t audioMemoryUsed;      // Audio heap used (bytes)
    size_t audioMemoryReserved;  // Audio heap reserved
    size_t streamingBufferSize;  // Streaming buffer total
    size_t streamingBufferUsed;   // Streaming buffer used
    double streamingBufferPercent; // Utilization %

    // Latency
    double audioLatencyMs;       // Round-trip audio latency
    double processingLatencyMs;  // DSP/processing latency
    double outputLatencyMs;      // Output buffer latency

    // Quality indicators
    int activeDSPChains;         // Active DSP effect chains
    int dspEffectCount;          // Total DSP effects running
    int streamingSources;        // Number of streaming audio sources

    // Sample rate info
    uint32_t sampleRate;         // Audio sample rate (e.g., 48000)
    uint32_t bufferSize;         // Audio buffer size (samples)
    int channelCount;            // Output channel count (2=stereo, etc.)

    // State
    bool glitchDetected;         // Audio glitch/click detected this frame
    double glitchSeverity;       // 0-100 severity if glitched
};

/**
 * Voice information
 */
struct VoiceInfo {
    int voiceId;
    std::string name;           // Sound/bank name
    double cpuCost;             // Estimated CPU cost (ms)
    size_t memoryCost;          // Memory footprint (bytes)
    double volume;              // Current volume (0-1)
    bool isStreaming;           // Is a streaming voice
    int dspEffectCount;         // DSP effects on this voice
    double playbackPositionMs;  // Current playback position
};

/**
 * DSP effect information
 */
struct DSPEffectInfo {
    int effectId;
    std::string name;            // e.g., "Reverb", "EQ", "Compressor"
    double cpuCostMs;            // CPU cost per frame
    bool bypassed;               // Is effect bypassed
    int voiceCount;              // How many voices use this effect
};

/**
 * Bandwidth summary for audio streaming
 */
struct AudioBandwidthSummary {
    int64_t startTime;
    int64_t endTime;
    double avgDownloadBps;       // Average streaming bandwidth
    double peakDownloadBps;      // Peak streaming bandwidth
    double totalDownloadedBytes;
    int streamingEvents;         // Number of stream load events
    double avgLatencyMs;        // Average stream load latency
    double maxLatencyMs;        // Max stream load latency
    int starvationEvents;        // Buffer underrun events
    int sampleCount;
};

/**
 * Latency statistics
 */
struct AudioLatencyStats {
    double minLatencyMs;
    double maxLatencyMs;
    double avgLatencyMs;
    double p50LatencyMs;
    double p95LatencyMs;
    double p99LatencyMs;
    double jitterMs;             // Latency variance
    int spikeCount;              // Latency spike events
    int totalSamples;
};

/**
 * Complete audio analysis report
 */
struct AudioReport {
    int64_t timestamp;
    int64_t sessionDurationMs;
    int totalSamples;

    // Voice summary
    int peakVoices;
    int avgVoices;
    int peakActiveVoices;
    int peakFreeVoices;          // Starvation risk indicator
    int voiceStarvationEvents;

    // CPU summary
    double avgAudioThreadCpuMs;
    double peakAudioThreadCpuMs;
    double avgMixerCpuMs;
    double avgDspCpuMs;
    double avgStreamingCpuMs;
    double audioCpuPercent;      // Audio as % of total frame time

    // Memory summary
    size_t peakAudioMemoryUsed;
    size_t avgAudioMemoryUsed;
    size_t audioMemoryLimit;
    double memoryUtilizationPercent;
    int memoryPressureEvents;

    // Latency summary
    AudioLatencyStats latencyStats;

    // Streaming summary
    AudioBandwidthSummary streaming;

    // DSP summary
    int peakDSPEffects;
    double peakDspCpuMs;
    std::vector<DSPEffectInfo> topDspEffects; // Top 5 by CPU

    // Glitch summary
    int totalGlitches;
    double avgGlitchSeverity;
    double worstGlitchSeverity;

    // Quality & Health
    AudioQuality quality;
    double audioHealthScore;     // 0-100 overall health
    double qualityScore;         // 0-100 audio quality
    double efficiencyScore;      // 0-100 CPU efficiency
    double stabilityScore;       // 0-100 glitch stability

    // Alerts
    int alertCount;
    int unacknowledgedAlertCount;
    std::vector<AudioAlert> recentAlerts;

    // Recommendations
    std::vector<std::string> recommendations;

    // Top consuming voices
    std::vector<VoiceInfo> topVoicesByCpu;
};

// ─── Alert Structure ───────────────────────────────────────────────────────────

/**
 * An audio performance alert
 */
struct AudioAlert {
    int id;
    AudioAlertType type;
    int64_t timestamp;
    double value;                // Measured value
    double threshold;            // Threshold that was crossed
    std::string message;         // Human-readable message
    std::string details;         // Detailed explanation
    bool acknowledged;
    int64_t acknowledgedAt;
    int occurrenceCount;
};

// ─── Configuration ────────────────────────────────────────────────────────────

struct AudioProfilerConfig {
    // Sampling
    bool enabled = true;
    size_t windowSize = 300;     // Rolling window samples

    // Voice limits
    int maxVoices = 64;          // Max voice instances
    int warningVoiceCount = 48;  // Warning threshold (75%)
    int criticalVoiceCount = 56; // Critical threshold (87.5%)

    // CPU thresholds (ms per frame at 60 FPS = ~16.67ms budget)
    double maxAudioThreadCpuMs = 2.0;   // 2ms audio thread budget
    double maxMixerCpuMs = 0.5;         // 0.5ms mixer
    double maxDspCpuMs = 1.0;           // 1ms DSP
    double maxStreamingCpuMs = 0.5;     // 0.5ms streaming

    // Memory thresholds
    size_t audioMemoryLimitBytes = 128 * 1024 * 1024; // 128 MB
    double memoryWarningPercent = 75.0;
    double memoryCriticalPercent = 90.0;

    // Latency thresholds (ms)
    double maxAudioLatencyMs = 50.0;     // Max acceptable latency
    double latencyWarningMs = 30.0;      // Warning threshold
    double latencySpikeMs = 20.0;        // What counts as a spike
    double maxLatencyJitterMs = 15.0;    // Max acceptable jitter

    // Streaming thresholds
    double maxStreamingBufferPercent = 90.0; // Buffer fill %
    int maxStreamingSources = 8;            // Max concurrent streams

    // Glitch detection
    bool glitchDetectionEnabled = true;
    double glitchThresholdMs = 5.0;         // Audio delay >5ms = glitch

    // Alert settings
    bool alertsEnabled = true;
    int maxAlerts = 100;
    int64_t alertCooldownMs = 5000;        // Min time between same-type alerts

    // Quality target
    AudioQuality targetQuality = AudioQuality::High;
    int targetSampleRate = 48000;
    int targetChannelCount = 2;
};

// ─── Main Class ───────────────────────────────────────────────────────────────

/**
 * AudioProfiler — monitors audio system performance for games.
 *
 * Games frequently suffer from audio-related performance issues that are
 * hard to detect with general profiling: voice starvation, DSP overload,
 * streaming stalls, and audio glitches. This profiler provides dedicated
 * monitoring for all audio subsystem metrics.
 *
 * Features:
 * - Real-time voice instance tracking and counting
 * - Audio thread CPU profiling (mixer, DSP, streaming)
 * - Memory usage and pressure detection
 * - Audio latency and jitter monitoring
 * - Streaming buffer health analysis
 * - DSP effect chain profiling
 * - Audio glitch/underrun detection
 * - Quality scoring and health monitoring
 * - Alert generation for audio anomalies
 * - Comprehensive analysis reports
 * - JSON export for dashboards
 *
 * Integration:
 *   - ProfilerCore manages lifecycle
 *   - Feed frame data from audio thread callback
 *   - AlertManager receives audio alerts
 *   - ReportGenerator includes audio section
 *
 * Usage:
 *   auto& audio = ProfilerCore::GetInstance().GetAudioProfiler();
 *   audio.RecordFrame(sample);
 *   AudioReport report = audio.GenerateReport();
 */
class AudioProfiler {
public:
    AudioProfiler();
    ~AudioProfiler();

    // ─── Configuration ───────────────────────────────────────────────────────

    void Configure(const AudioProfilerConfig& config);
    AudioProfilerConfig GetConfig() const { return m_config; }
    void Reset();

    // ─── Data Recording ─────────────────────────────────────────────────────

    /**
     * Record a single audio frame sample.
     *
     * Call this every frame (or audio callback interval) with
     * measured audio system metrics.
     *
     * @param sample  Audio frame sample with all measured values
     */
    void RecordFrame(const AudioSample& sample);

    /**
     * Record voice start (voice allocated).
     */
    void RecordVoiceStart(int voiceId, const std::string& name,
                          double cpuCost, size_t memoryCost,
                          int dspEffectCount, bool isStreaming);

    /**
     * Record voice stop (voice released).
     */
    void RecordVoiceStop(int voiceId);

    /**
     * Record DSP effect execution.
     */
    void RecordDSPEffect(int effectId, const std::string& name,
                          double cpuCostMs, bool bypassed);

    /**
     * Simulate a realistic audio frame for testing/demo.
     */
    void RecordSimulatedFrame();

    // ─── Analysis ───────────────────────────────────────────────────────────

    /** Generate full analysis report. */
    AudioReport GenerateReport() const;

    /** Quick accessors */
    double GetCurrentAudioCpuMs() const;
    double GetCurrentLatencyMs() const;
    int GetActiveVoiceCount() const;
    int GetFreeVoiceCount() const;
    double GetAudioHealthScore() const;
    double GetGlitchRate() const;
    AudioQuality GetCurrentQuality() const;
    int GetAlertCount() const;
    ConnectionState GetConnectionState() const;

    /** Get active (unacknowledged) alerts. */
    std::vector<AudioAlert> GetActiveAlerts() const;

    /** Acknowledge a specific alert. */
    bool AcknowledgeAlert(int alertId);

    /** Acknowledge all alerts. */
    void AcknowledgeAllAlerts();

    // ─── Voice Management ───────────────────────────────────────────────────

    /** Get all tracked voices. */
    const std::vector<VoiceInfo>& GetVoices() const { return m_voices; }

    /** Get top N voices by CPU cost. */
    std::vector<VoiceInfo> GetTopVoicesByCpu(int count) const;

    /** Get top N voices by memory cost. */
    std::vector<VoiceInfo> GetTopVoicesByMemory(int count) const;

    // ─── DSP Information ────────────────────────────────────────────────────

    /** Get all tracked DSP effects. */
    const std::vector<DSPEffectInfo>& GetDSPEffects() const { return m_dspEffects; }

    // ─── Streaming Analysis ─────────────────────────────────────────────────

    /** Get streaming bandwidth summary. */
    AudioBandwidthSummary GetStreamingSummary() const;

    /** Get latency statistics. */
    AudioLatencyStats GetLatencyStats() const;

    // ─── Export ─────────────────────────────────────────────────────────────

    /** Export full report as JSON string. */
    std::string ExportToJSON() const;

    /** Export voice data as CSV string. */
    std::string ExportVoicesCSV() const;

    /** Export CPU timeline as CSV string. */
    std::string ExportCPUCSV() const;

    // ─── Lifecycle ──────────────────────────────────────────────────────────

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

private:
    void ComputeVoiceStats(AudioReport& out) const;
    void ComputeCPUStats(AudioReport& out) const;
    void ComputeMemoryStats(AudioReport& out) const;
    void ComputeLatencyStats(AudioReport& out) const;
    void ComputeStreamingStats(AudioReport& out) const;
    void ComputeDSPStats(AudioReport& out) const;
    void ComputeGlitchStats(AudioReport& out) const;
    void ComputeHealthScores(AudioReport& out) const;
    void DetectAlerts() const;
    void GenerateRecommendations(AudioReport& out) const;
    void TrimHistory();

    AudioAlert MakeAlert(AudioAlertType type, double value, double threshold,
                          const std::string& message,
                          const std::string& details) const;

    double QuickSelectPercentile(std::vector<double>& arr, double percentile) const;
    double ComputeStdDev(const std::vector<double>& values, double mean) const;
    double ComputeMean(const std::vector<double>& values) const;
    int GenerateAlertId();
    int64_t GetCurrentTimestamp() const;
    std::string AlertTypeToString(AudioAlertType type) const;
    std::string QualityToString(AudioQuality q) const;

private:
    AudioProfilerConfig m_config;
    bool m_enabled = true;

    // Rolling frame data
    std::vector<AudioSample> m_samples;

    // Voice tracking
    std::vector<VoiceInfo> m_voices;
    std::unordered_map<int, size_t> m_voiceIndex;

    // DSP effect tracking
    std::vector<DSPEffectInfo> m_dspEffects;
    std::unordered_map<int, size_t> m_dspIndex;

    // Alerts
    mutable std::vector<AudioAlert> m_alerts;
    mutable AudioReport m_cachedReport;
    mutable bool m_dirty = true;

    // Running statistics for quick access
    std::vector<double> m_cpuHistory;
    std::vector<double> m_latencyHistory;
    std::vector<double> m_voiceCountHistory;

    // Voice starvation tracking
    int m_voiceStarvationEvents = 0;
    int m_lowFreeVoicesFrames = 0;
    int64_t m_lastVoiceAlert = 0;

    // Glitch tracking
    int m_totalGlitches = 0;
    double m_worstGlitchSeverity = 0.0;
    int64_t m_lastGlitchTime = 0;

    // Memory tracking
    std::vector<double> m_memoryHistory;
    int m_memoryPressureEvents = 0;
    int64_t m_lastMemoryAlert = 0;

    // Latency tracking
    int m_latencySpikeEvents = 0;
    int64_t m_lastLatencyAlert = 0;

    // Streaming tracking
    std::vector<double> m_streamingBandwidth;
    int m_starvationEvents = 0;

    // Session tracking
    int64_t m_sessionStartTime = 0;
    int m_nextVoiceId = 1;
    int m_nextDspId = 1;

    // Alert ID generator
    int m_nextAlertId = 1;
};

} // namespace ProfilerCore
