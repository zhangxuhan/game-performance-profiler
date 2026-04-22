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

namespace ProfilerCore {

// ─── Enumerations ─────────────────────────────────────────────────────────────

/**
 * Network quality rating
 */
enum class NetworkQuality {
    Excellent = 0,  // < 5ms jitter, > 95 Mbps
    Good = 1,      // < 15ms jitter, > 50 Mbps
    Fair = 2,       // < 30ms jitter, > 20 Mbps
    Poor = 3,       // < 50ms jitter, > 5 Mbps
    Critical = 4   // >= 50ms jitter or < 5 Mbps
};

/**
 * Network alert type
 */
enum class NetworkAlertType {
    BandwidthSpike,      // Sudden bandwidth surge
    LatencySpike,        // Round-trip time spike
    PacketLossDetected,  // Possible packet loss pattern
    ConnectionDrop,      // Connection interrupted
    JitterHigh,          // High latency variance
    LowBandwidth,        // Sustained low throughput
    UploadBurst,         // Unusually high upload
    DownloadBurst       // Unusually high download
};

/**
 * Connection state
 */
enum class ConnectionState {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Degraded = 3,
    Reconnecting = 4
};

// ─── Data Structures ──────────────────────────────────────────────────────────

/**
 * A single network sample
 */
struct NetworkSample {
    int64_t  timestamp;       // microseconds since epoch
    double   rttMs;           // round-trip time in ms
    double   jitterMs;        // latency variance in ms
    double   downloadBps;     // download bandwidth (bytes/sec)
    double   uploadBps;       // upload bandwidth (bytes/sec)
    uint32_t packetsIn;       // packets received in interval
    uint32_t packetsOut;      // packets sent in interval
    uint32_t packetsLost;     // lost packets (estimated)
    double   packetRateIn;   // packets/sec received
    double   packetRateOut;  // packets/sec sent
    double   bandwidthUtilization; // % of available bandwidth used
    bool     valid;
};

/**
 * A network alert event
 */
struct NetworkAlert {
    NetworkAlertType type;
    int64_t  timestamp;
    double   value;           // measured value
    double   threshold;      // threshold that was crossed
    std::string message;
    bool     acknowledged;
};

/**
 * Bandwidth summary for a time window
 */
struct BandwidthSummary {
    int64_t  startTime;
    int64_t  endTime;
    double   avgDownloadBps;
    double   avgUploadBps;
    double   peakDownloadBps;
    double   peakUploadBps;
    double   totalDownloadBytes;
    double   totalUploadBytes;
    double   downloadStdDev;
    double   uploadStdDev;
    int      sampleCount;
};

/**
 * Latency statistics for a time window
 */
struct LatencyStats {
    int64_t  startTime;
    int64_t  endTime;
    double   minRttMs;
    double   maxRttMs;
    double   avgRttMs;
    double   medianRttMs;
    double   p95RttMs;
    double   p99RttMs;
    double   jitterMs;         // average absolute deviation
    double   maxJitterMs;     // worst jitter observed
    int      spikeCount;
    double   packetLossRate;  // estimated loss percentage
    int      sampleCount;
};

/**
 * Complete network analysis report
 */
struct NetworkReport {
    int64_t  timestamp;
    int64_t  sessionDurationMs;
    int      totalSamples;

    // Bandwidth
    BandwidthSummary bandwidth;
    LatencyStats     latency;

    // Quality
    NetworkQuality   quality;
    double           qualityScore;       // 0-100
    double           stabilityScore;    // 0-100

    // Alerts
    int              alertCount;
    int              unacknowledgedAlerts;
    std::vector<NetworkAlert> recentAlerts;

    // Connection
    ConnectionState  connectionState;
    int64_t          lastPacketTime;     // microseconds since epoch

    // Recommendations
    std::vector<std::string> recommendations;
};

// ─── Configuration ────────────────────────────────────────────────────────────

struct NetworkProfilerConfig {
    size_t   windowSize = 300;           // number of samples to keep
    double   targetRttMs = 50.0;         // expected RTT
    double   maxRttSpikeMs = 200.0;     // RTT spike threshold
    double   maxJitterMs = 30.0;        // acceptable jitter
    double   bandwidthWarningBps = 1e6; // 1 MB/s warning threshold
    double   bandwidthCriticalBps = 5e6; // 5 MB/s critical threshold
    double   packetLossThreshold = 0.05; // 5% loss threshold
    bool     alertsEnabled = true;
};

// ─── Main Class ───────────────────────────────────────────────────────────────

/**
 * NetworkProfiler — monitors network performance metrics for games.
 *
 * Features:
 * - Real-time bandwidth tracking (upload/download)
 * - RTT and jitter analysis
 * - Network quality scoring (0-100)
 * - Latency spike and burst detection
 * - Integration with AlertManager for network alerts
 * - Comprehensive analysis reports
 * - JSON export
 *
 * Usage:
 *   auto& net = ProfilerCore::ProfilerCore::GetInstance().GetNetworkProfiler();
 *   net.RecordSample(rttMs, downloadBps, uploadBps);
 *   NetworkReport report = net.GenerateReport();
 */
class NetworkProfiler {
public:
    NetworkProfiler();
    ~NetworkProfiler();

    // ─── Configuration ───────────────────────────────────────────────────────

    void Configure(const NetworkProfilerConfig& config);
    NetworkProfilerConfig GetConfig() const;

    // ─── Data Recording ─────────────────────────────────────────────────────

    /**
     * Record a single network sample.
     * Call this every frame or polling interval with measured values.
     *
     * @param rttMs         Round-trip time in milliseconds (0 if unknown)
     * @param downloadBps   Download throughput in bytes/sec (0 if unknown)
     * @param uploadBps     Upload throughput in bytes/sec (0 if unknown)
     * @param packetsIn     Packets received (optional)
     * @param packetsOut    Packets sent (optional)
     * @param packetsLost   Packets lost (optional)
     */
    void RecordSample(
        double  rttMs,
        double  downloadBps,
        double  uploadBps,
        uint32_t packetsIn = 0,
        uint32_t packetsOut = 0,
        uint32_t packetsLost = 0
    );

    /**
     * Simulate a realistic network sample for testing/demo.
     * Generates plausible RTT, bandwidth and jitter values with occasional spikes.
     */
    void RecordSimulatedSample();

    // ─── Analysis ───────────────────────────────────────────────────────────

    /** Generate full analysis report. */
    NetworkReport GenerateReport() const;

    /** Quick accessors */
    double   GetCurrentDownloadBps() const;
    double   GetCurrentUploadBps() const;
    double   GetCurrentRttMs() const;
    double   GetCurrentJitterMs() const;
    NetworkQuality GetCurrentQuality() const;
    double   GetQualityScore() const;
    double   GetStabilityScore() const;
    int      GetAlertCount() const;
    ConnectionState GetConnectionState() const;

    /** Get alerts that need attention. */
    std::vector<NetworkAlert> GetActiveAlerts() const;

    /** Acknowledge a specific alert. */
    bool AcknowledgeAlert(int64_t timestamp);

    /** Acknowledge all current alerts. */
    void AcknowledgeAllAlerts();

    // ─── Export ─────────────────────────────────────────────────────────────

    /** Export full report as JSON string. */
    std::string ExportToJSON() const;

    /** Export bandwidth data as CSV string. */
    std::string ExportBandwidthCSV() const;

    /** Export latency data as CSV string. */
    std::string ExportLatencyCSV() const;

    // ─── Lifecycle ──────────────────────────────────────────────────────────

    void Reset();
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

private:
    void ComputeBandwidthStats(BandwidthSummary& out) const;
    void ComputeLatencyStats(LatencyStats& out) const;
    void ComputeQualityScore() const;
    void DetectAlerts() const;
    void GenerateRecommendations() const;

    NetworkAlert MakeAlert(NetworkAlertType type, double value, double threshold,
                           const std::string& message) const;

    NetworkQuality JitterToQuality(double jitterMs) const;
    NetworkQuality BandwidthToQuality(double downloadBps) const;

    // ─── State ──────────────────────────────────────────────────────────────

    NetworkProfilerConfig  m_config;
    bool                    m_enabled = true;

    struct SampleEntry {
        int64_t  timestamp;
        double   rttMs;
        double   jitterMs;
        double   downloadBps;
        double   uploadBps;
        uint32_t packetsIn;
        uint32_t packetsOut;
        uint32_t packetsLost;
        double   packetRateIn;
        double   packetRateOut;
        bool     valid;
    };

    std::vector<SampleEntry>     m_samples;
    mutable std::vector<NetworkAlert> m_alerts;
    mutable NetworkReport         m_cachedReport;
    mutable bool                  m_dirty = true;

    // Connection state
    ConnectionState               m_connectionState = ConnectionState::Connected;
    int64_t                       m_lastPacketTime = 0;
    int64_t                       m_sessionStartTime = 0;
    int64_t                       m_lastDisconnectTime = 0;

    // Running statistics for jitter computation
    double                        m_rttSum = 0.0;
    double                        m_rttSumSq = 0.0;
    double                        m_prevRttMs = 0.0;
    double                        m_jitterSum = 0.0;
    int                           m_jitterCount = 0;

    // Bandwidth stats
    double                        m_downloadSum = 0.0;
    double                        m_downloadSumSq = 0.0;
    double                        m_uploadSum = 0.0;
    double                        m_uploadSumSq = 0.0;

    // Simulation state
    double                        m_simBaseRtt = 25.0;
    double                        m_simBaseDownload = 5.0 * 1024.0 * 1024.0; // 5 MB/s
    double                        m_simBaseUpload = 512.0 * 1024.0;          // 512 KB/s
    int                           m_simFrameCount = 0;
};

} // namespace ProfilerCore
