#include "NetworkProfiler.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <limits>
#include <random>

namespace ProfilerCore {

// ─── Helper: microsecond timestamp ───────────────────────────────────────────

static int64_t NowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

NetworkProfiler::NetworkProfiler()
    : m_sessionStartTime(NowUs())
{
    m_samples.reserve(m_config.windowSize);
}

NetworkProfiler::~NetworkProfiler() {
}

// ─── Configuration ────────────────────────────────────────────────────────────

void NetworkProfiler::Configure(const NetworkProfilerConfig& config) {
    m_config = config;
    if (m_samples.size() > config.windowSize) {
        // Keep the newest samples
        size_t keep = m_samples.size() - config.windowSize;
        m_samples.erase(m_samples.begin(), m_samples.begin() + keep);
    }
    m_dirty = true;
}

NetworkProfilerConfig NetworkProfiler::GetConfig() const {
    return m_config;
}

// ─── Data Recording ──────────────────────────────────────────────────────────

void NetworkProfiler::RecordSample(
    double   rttMs,
    double   downloadBps,
    double   uploadBps,
    uint32_t packetsIn,
    uint32_t packetsOut,
    uint32_t packetsLost)
{
    if (!m_enabled) return;

    int64_t now = NowUs();

    SampleEntry entry;
    entry.timestamp   = now;
    entry.rttMs       = rttMs;
    entry.downloadBps = downloadBps;
    entry.uploadBps   = uploadBps;
    entry.packetsIn   = packetsIn;
    entry.packetsOut  = packetsOut;
    entry.packetsLost = packetsLost;
    entry.valid       = true;

    // Compute jitter from RTT delta
    if (m_prevRttMs > 0.0 && rttMs > 0.0) {
        double delta = std::abs(rttMs - m_prevRttMs);
        entry.jitterMs = delta;
        m_jitterSum += delta;
        m_jitterCount++;
    } else {
        entry.jitterMs = 0.0;
    }
    m_prevRttMs = rttMs;

    // Compute packet rates (per second)
    if (!m_samples.empty()) {
        double elapsedSec = (now - m_samples.back().timestamp) / 1e6;
        if (elapsedSec > 0.0) {
            entry.packetRateIn  = packetsIn  / elapsedSec;
            entry.packetRateOut = packetsOut / elapsedSec;
        }
    }

    // Update running sums for quick statistics
    m_rttSum       += rttMs;
    m_rttSumSq     += rttMs * rttMs;
    m_downloadSum  += downloadBps;
    m_downloadSumSq += downloadBps * downloadBps;
    m_uploadSum    += uploadBps;
    m_uploadSumSq  += uploadBps * uploadBps;

    m_samples.push_back(entry);

    // Trim to window size
    if (m_samples.size() > m_config.windowSize) {
        const auto& old = m_samples.front();
        m_rttSum       -= old.rttMs;
        m_rttSumSq     -= old.rttMs * old.rttMs;
        m_downloadSum  -= old.downloadBps;
        m_downloadSumSq -= old.downloadBps * old.downloadBps;
        m_uploadSum    -= old.uploadBps;
        m_uploadSumSq  -= old.uploadBps * old.uploadBps;
        m_samples.erase(m_samples.begin());
    }

    m_lastPacketTime = now;
    m_dirty = true;
    m_connectionState = ConnectionState::Connected;
}

void NetworkProfiler::RecordSimulatedSample() {
    using namespace std;

    m_simFrameCount++;

    // Base values with smooth random walk
    static std::mt19937 gen(42); // deterministic seed for consistency
    static std::normal_distribution<> rtt_noise(0.0, 3.0);
    static std::normal_distribution<> bw_noise(0.0, 0.1);

    double rtt = m_simBaseRtt + rtt_noise(gen);
    rtt = max(5.0, rtt); // minimum 5ms

    // Occasional RTT spikes (~every 180 frames = ~3 sec at 60fps)
    bool spike = (m_simFrameCount % 180 < 5);
    if (spike) {
        rtt += 80.0 + abs(rtt_noise(gen) * 20.0);
    }

    double dl = m_simBaseDownload * (1.0 + bw_noise(gen));
    dl = max(1024.0, dl); // minimum 1 KB/s

    double ul = m_simBaseUpload * (1.0 + bw_noise(gen) * 0.5);
    ul = max(512.0, ul); // minimum 512 B/s

    // Occasional download burst
    if (m_simFrameCount % 240 < 3) {
        dl *= 3.0;
    }

    uint32_t pktIn  = static_cast<uint32_t>(dl / 1400.0);
    uint32_t pktOut = static_cast<uint32_t>(ul / 512.0);
    uint32_t lost   = (gen() % 100 < 2) ? 1 : 0; // ~2% packet loss simulation

    RecordSample(rtt, dl, ul, pktIn, pktOut, lost);
}

// ─── Analysis ─────────────────────────────────────────────────────────────────

NetworkReport NetworkProfiler::GenerateReport() const {
    if (m_dirty) {
        m_cachedReport = NetworkReport();
        m_cachedReport.timestamp = NowUs();

        if (!m_samples.empty()) {
            m_cachedReport.sessionDurationMs =
                (m_samples.back().timestamp - m_samples.front().timestamp) / 1000;
        }
        m_cachedReport.totalSamples = static_cast<int>(m_samples.size());

        ComputeBandwidthStats(m_cachedReport.bandwidth);
        ComputeLatencyStats(m_cachedReport.latency);
        ComputeQualityScore();
        DetectAlerts();
        GenerateRecommendations();

        // Connection state
        m_cachedReport.connectionState = m_connectionState;
        m_cachedReport.lastPacketTime  = m_lastPacketTime;

        m_dirty = false;
    }

    return m_cachedReport;
}

void NetworkProfiler::ComputeBandwidthStats(BandwidthSummary& out) const {
    if (m_samples.empty()) {
        out = BandwidthSummary();
        return;
    }

    out.startTime  = m_samples.front().timestamp;
    out.endTime    = m_samples.back().timestamp;
    out.sampleCount = static_cast<int>(m_samples.size());

    double dlSum = 0.0, ulSum = 0.0;
    double dlMax = 0.0, ulMax = 0.0;
    double totalDl = 0.0, totalUl = 0.0;
    double dlSqSum = 0.0, ulSqSum = 0.0;

    double elapsedSec = (out.endTime - out.startTime) / 1e6;
    if (elapsedSec <= 0) elapsedSec = 1.0;

    for (const auto& s : m_samples) {
        dlSum += s.downloadBps;
        ulSum += s.uploadBps;
        dlMax = std::max(dlMax, s.downloadBps);
        ulMax = std::max(ulMax, s.uploadBps);
        dlSqSum += s.downloadBps * s.downloadBps;
        ulSqSum += s.uploadBps * s.uploadBps;

        // Estimate total bytes transferred
        double sampleDurSec = 1.0 / 60.0; // assume 60 Hz sampling
        totalDl += s.downloadBps * sampleDurSec;
        totalUl += s.uploadBps   * sampleDurSec;
    }

    double n = static_cast<double>(m_samples.size());
    out.avgDownloadBps   = dlSum / n;
    out.avgUploadBps      = ulSum / n;
    out.peakDownloadBps  = dlMax;
    out.peakUploadBps    = ulMax;
    out.totalDownloadBytes = totalDl;
    out.totalUploadBytes   = totalUl;

    // Standard deviation
    double dlMean = dlSum / n;
    double ulMean = ulSum / n;
    out.downloadStdDev = std::sqrt(std::max(0.0, dlSqSum / n - dlMean * dlMean));
    out.uploadStdDev   = std::sqrt(std::max(0.0, ulSqSum / n - ulMean * ulMean));
}

void NetworkProfiler::ComputeLatencyStats(LatencyStats& out) const {
    if (m_samples.empty()) {
        out = LatencyStats();
        return;
    }

    out.startTime  = m_samples.front().timestamp;
    out.endTime    = m_samples.back().timestamp;
    out.sampleCount = static_cast<int>(m_samples.size());

    // Collect valid RTT samples
    std::vector<double> rttVals;
    std::vector<double> jitterVals;
    rttVals.reserve(m_samples.size());
    jitterVals.reserve(m_samples.size());

    double rttMin = std::numeric_limits<double>::max();
    double rttMax = 0.0;
    double rttSum = 0.0;
    uint64_t lostSum = 0;

    int spikeCount = 0;
    double prevRtt = 0.0;

    for (const auto& s : m_samples) {
        if (s.rttMs <= 0) continue;

        rttVals.push_back(s.rttMs);
        jitterVals.push_back(s.jitterMs);
        rttSum += s.rttMs;

        rttMin = std::min(rttMin, s.rttMs);
        rttMax = std::max(rttMax, s.rttMs);

        // Count spikes
        if (prevRtt > 0) {
            double delta = std::abs(s.rttMs - prevRtt);
            if (delta > m_config.maxJitterMs) {
                spikeCount++;
            }
        }
        prevRtt = s.rttMs;

        lostSum += s.packetsLost;
    }

    if (rttVals.empty()) {
        out = LatencyStats();
        return;
    }

    size_t n = rttVals.size();
    out.minRttMs = (rttMin == std::numeric_limits<double>::max()) ? 0.0 : rttMin;
    out.maxRttMs = rttMax;
    out.avgRttMs = rttSum / static_cast<double>(n);

    // Sort for percentiles
    std::vector<double> sorted = rttVals;
    std::sort(sorted.begin(), sorted.end());

    auto percentile = [&](double p) -> double {
        if (sorted.empty()) return 0.0;
        double idx = (p / 100.0) * (sorted.size() - 1);
        size_t lo = static_cast<size_t>(std::floor(idx));
        size_t hi = static_cast<size_t>(std::ceil(idx));
        if (lo == hi || hi >= sorted.size()) return sorted[lo];
        return sorted[lo] * (1.0 - (idx - lo)) + sorted[hi] * (idx - lo);
    };

    out.medianRttMs = percentile(50.0);
    out.p95RttMs    = percentile(95.0);
    out.p99RttMs    = percentile(99.0);

    // Jitter statistics
    if (!jitterVals.empty()) {
        out.jitterMs = std::accumulate(jitterVals.begin(), jitterVals.end(), 0.0)
                      / static_cast<double>(jitterVals.size());
        out.maxJitterMs = *std::max_element(jitterVals.begin(), jitterVals.end());
    }

    out.spikeCount = spikeCount;

    // Estimate packet loss rate
    uint64_t totalPkts = 0;
    for (const auto& s : m_samples) {
        totalPkts += s.packetsIn + s.packetsLost;
    }
    out.packetLossRate = (totalPkts > 0) ?
        static_cast<double>(lostSum) / static_cast<double>(totalPkts) : 0.0;
}

void NetworkProfiler::ComputeQualityScore() const {
    const auto& latency = m_cachedReport.latency;
    const auto& bw = m_cachedReport.bandwidth;

    double rttScore = 100.0;
    if (latency.avgRttMs > 0) {
        // Score based on RTT vs target
        double ratio = latency.avgRttMs / m_config.targetRttMs;
        rttScore = std::max(0.0, 100.0 - (ratio - 1.0) * 60.0);
    }

    double jitterScore = 100.0;
    if (latency.jitterMs > 0) {
        jitterScore = std::max(0.0, 100.0 - latency.jitterMs / m_config.maxJitterMs * 50.0);
    }

    double bwScore = 100.0;
    if (bw.avgDownloadBps > 0) {
        // Score based on bandwidth utilization
        double util = bw.avgDownloadBps / m_config.bandwidthWarningBps;
        if (util > 1.0) {
            // Very high bandwidth might indicate an issue (DoS, burst)
            bwScore = std::max(0.0, 100.0 - (util - 1.0) * 30.0);
        } else {
            bwScore = util * 100.0;
        }
    }

    double lossScore = 100.0;
    if (latency.packetLossRate > 0) {
        lossScore = std::max(0.0, 100.0 - latency.packetLossRate * 1000.0);
    }

    m_cachedReport.qualityScore = rttScore * 0.35 + jitterScore * 0.25 +
                                  bwScore * 0.20 + lossScore * 0.20;
    m_cachedReport.qualityScore = std::max(0.0, std::min(100.0, m_cachedReport.qualityScore));

    // Stability score based on variance
    double cvDownload = (bw.avgDownloadBps > 0) ?
        bw.downloadStdDev / bw.avgDownloadBps : 0.0;
    double cvUpload = (bw.avgUploadBps > 0) ?
        m_uploadSumSq / static_cast<double>(std::max(size_t(1), m_samples.size())) / bw.avgUploadBps - bw.avgUploadBps : 0.0;

    double stability = 100.0 - (cvDownload + cvUpload) * 30.0;
    m_cachedReport.stabilityScore = std::max(0.0, std::min(100.0, stability));

    // Determine quality rating
    m_cachedReport.quality = JitterToQuality(latency.jitterMs);
}

NetworkQuality NetworkProfiler::JitterToQuality(double jitterMs) const {
    if (jitterMs < 5.0)  return NetworkQuality::Excellent;
    if (jitterMs < 15.0) return NetworkQuality::Good;
    if (jitterMs < 30.0) return NetworkQuality::Fair;
    if (jitterMs < 50.0) return NetworkQuality::Poor;
    return NetworkQuality::Critical;
}

NetworkQuality NetworkProfiler::BandwidthToQuality(double downloadBps) const {
    if (downloadBps > 95.0 * 1024.0 * 1024.0)  return NetworkQuality::Excellent;
    if (downloadBps > 50.0 * 1024.0 * 1024.0) return NetworkQuality::Good;
    if (downloadBps > 20.0 * 1024.0 * 1024.0) return NetworkQuality::Fair;
    if (downloadBps > 5.0  * 1024.0 * 1024.0)  return NetworkQuality::Poor;
    return NetworkQuality::Critical;
}

void NetworkProfiler::DetectAlerts() const {
    if (!m_config.alertsEnabled || m_samples.empty()) return;

    m_alerts.clear();

    const auto& bw = m_cachedReport.bandwidth;
    const auto& latency = m_cachedReport.latency;

    // Bandwidth spike: peak >> average
    if (bw.avgDownloadBps > 0 && bw.peakDownloadBps > bw.avgDownloadBps * 5.0) {
        m_alerts.push_back(MakeAlert(
            NetworkAlertType::DownloadBurst,
            bw.peakDownloadBps,
            bw.avgDownloadBps * 5.0,
            "Download bandwidth spike detected: peak " +
            std::to_string(static_cast<int>(bw.peakDownloadBps / 1024.0 / 1024.0)) +
            " MB/s (avg " +
            std::to_string(static_cast<int>(bw.avgDownloadBps / 1024.0 / 1024.0)) + " MB/s)"
        ));
    }

    // High RTT spike
    if (latency.maxRttMs > m_config.maxRttSpikeMs) {
        m_alerts.push_back(MakeAlert(
            NetworkAlertType::LatencySpike,
            latency.maxRttMs,
            m_config.maxRttSpikeMs,
            "High RTT spike: " + std::to_string(static_cast<int>(latency.maxRttMs)) +
            "ms (threshold: " + std::to_string(static_cast<int>(m_config.maxRttSpikeMs)) + "ms)"
        ));
    }

    // High jitter
    if (latency.maxJitterMs > m_config.maxJitterMs) {
        m_alerts.push_back(MakeAlert(
            NetworkAlertType::JitterHigh,
            latency.maxJitterMs,
            m_config.maxJitterMs,
            "High jitter detected: " + std::to_string(static_cast<int>(latency.maxJitterMs)) +
            "ms (threshold: " + std::to_string(static_cast<int>(m_config.maxJitterMs)) + "ms)"
        ));
    }

    // High bandwidth
    if (bw.peakDownloadBps > m_config.bandwidthCriticalBps) {
        m_alerts.push_back(MakeAlert(
            NetworkAlertType::BandwidthSpike,
            bw.peakDownloadBps,
            m_config.bandwidthCriticalBps,
            "Critical bandwidth usage: " +
            std::to_string(static_cast<int>(bw.peakDownloadBps / 1024.0 / 1024.0)) +
            " MB/s. Possible asset streaming burst or anomaly."
        ));
    }

    // Packet loss
    if (latency.packetLossRate > m_config.packetLossThreshold) {
        m_alerts.push_back(MakeAlert(
            NetworkAlertType::PacketLossDetected,
            latency.packetLossRate * 100.0,
            m_config.packetLossThreshold * 100.0,
            "Packet loss detected: " + std::to_string(
                static_cast<int>(latency.packetLossRate * 100.0)) +
            "% (threshold: " +
            std::to_string(static_cast<int>(m_config.packetLossThreshold * 100.0)) + "%)"
        ));
    }

    // Connection state alert
    if (m_connectionState == ConnectionState::Degraded) {
        m_alerts.push_back(MakeAlert(
            NetworkAlertType::ConnectionDrop,
            0, 0,
            "Connection degraded: increased latency or intermittent connectivity."
        ));
    }

    // Update report
    m_cachedReport.alertCount = static_cast<int>(m_alerts.size());

    int unack = 0;
    for (const auto& a : m_alerts) {
        if (!a.acknowledged) unack++;
    }
    m_cachedReport.unacknowledgedAlerts = unack;
    m_cachedReport.recentAlerts = m_alerts;
}

NetworkAlert NetworkProfiler::MakeAlert(
    NetworkAlertType type, double value, double threshold,
    const std::string& message) const
{
    NetworkAlert alert;
    alert.type         = type;
    alert.timestamp    = NowUs();
    alert.value        = value;
    alert.threshold    = threshold;
    alert.message      = message;
    alert.acknowledged = false;
    return alert;
}

void NetworkProfiler::GenerateRecommendations() const {
    m_cachedReport.recommendations.clear();

    const auto& latency = m_cachedReport.latency;
    const auto& bw = m_cachedReport.bandwidth;

    if (m_cachedReport.qualityScore < 50.0) {
        m_cachedReport.recommendations.push_back(
            "Network quality is poor. Consider optimizing payload sizes, "
            "using delta compression, or implementing client-side prediction "
            "to mask latency."
        );
    }

    if (latency.spikeCount > 5) {
        m_cachedReport.recommendations.push_back(
            "Frequent latency spikes detected (" + std::to_string(latency.spikeCount) +
            "). Investigate network congestion, server tick rate, or "
            "implement jitter buffering in your netcode."
        );
    }

    if (bw.avgDownloadBps > m_config.bandwidthWarningBps * 2) {
        m_cachedReport.recommendations.push_back(
            "High average bandwidth consumption (" +
            std::to_string(static_cast<int>(bw.avgDownloadBps / 1024.0 / 1024.0)) +
            " MB/s). Consider asset streaming optimization, level-of-detail "
            "streaming, or reducing update frequency for non-critical data."
        );
    }

    if (latency.packetLossRate > 0.01) {
        m_cachedReport.recommendations.push_back(
            "Packet loss detected. Implement UDP retry logic or switch to a "
            "more reliable protocol for critical game state updates."
        );
    }

    if (m_cachedReport.stabilityScore < 60.0) {
        m_cachedReport.recommendations.push_back(
            "Unstable network performance. Consider implementing adaptive "
            "quality, reducing update rate during congestion, or migrating "
            "to a closer server region."
        );
    }

    if (latency.avgRttMs > 100.0) {
        m_cachedReport.recommendations.push_back(
            "High average RTT (" + std::to_string(static_cast<int>(latency.avgRttMs)) +
            "ms). This is noticeable in competitive games. Consider server "
            "migration, better routing, or client-side prediction/rollback."
        );
    }

    // Limit to 5 recommendations
    while (m_cachedReport.recommendations.size() > 5) {
        m_cachedReport.recommendations.pop_back();
    }
}

// ─── Quick Accessors ───────────────────────────────────────────────────────────

double NetworkProfiler::GetCurrentDownloadBps() const {
    if (m_samples.empty()) return 0.0;
    return m_samples.back().downloadBps;
}

double NetworkProfiler::GetCurrentUploadBps() const {
    if (m_samples.empty()) return 0.0;
    return m_samples.back().uploadBps;
}

double NetworkProfiler::GetCurrentRttMs() const {
    if (m_samples.empty()) return 0.0;
    return m_samples.back().rttMs;
}

double NetworkProfiler::GetCurrentJitterMs() const {
    if (m_samples.empty()) return 0.0;
    return m_samples.back().jitterMs;
}

NetworkQuality NetworkProfiler::GetCurrentQuality() const {
    if (m_samples.empty()) return NetworkQuality::Good;
    return JitterToQuality(m_samples.back().jitterMs);
}

double NetworkProfiler::GetQualityScore() const {
    return GenerateReport().qualityScore;
}

double NetworkProfiler::GetStabilityScore() const {
    return GenerateReport().stabilityScore;
}

int NetworkProfiler::GetAlertCount() const {
    return GenerateReport().alertCount;
}

ConnectionState NetworkProfiler::GetConnectionState() const {
    return m_connectionState;
}

std::vector<NetworkAlert> NetworkProfiler::GetActiveAlerts() const {
    GenerateReport(); // ensure alerts are computed
    return m_alerts;
}

bool NetworkProfiler::AcknowledgeAlert(int64_t timestamp) {
    for (auto& a : m_alerts) {
        if (a.timestamp == timestamp) {
            a.acknowledged = true;
            m_cachedReport.unacknowledgedAlerts =
                std::max(0, m_cachedReport.unacknowledgedAlerts - 1);
            return true;
        }
    }
    return false;
}

void NetworkProfiler::AcknowledgeAllAlerts() {
    for (auto& a : m_alerts) {
        a.acknowledged = true;
    }
    m_cachedReport.unacknowledgedAlerts = 0;
}

// ─── Export ───────────────────────────────────────────────────────────────────

std::string NetworkProfiler::ExportToJSON() const {
    NetworkReport report = GenerateReport();

    std::ostringstream ss;
    ss << "{\"networkProfiler\":{";
    ss << "\"timestamp\":" << report.timestamp << ",";
    ss << "\"sessionDurationMs\":" << report.sessionDurationMs << ",";
    ss << "\"totalSamples\":" << report.totalSamples << ",";

    // Bandwidth
    const auto& bw = report.bandwidth;
    ss << "\"bandwidth\":{";
    ss << "\"avgDownloadBps\":" << std::fixed << std::setprecision(2) << bw.avgDownloadBps << ",";
    ss << "\"avgUploadBps\":" << bw.avgUploadBps << ",";
    ss << "\"peakDownloadBps\":" << bw.peakDownloadBps << ",";
    ss << "\"peakUploadBps\":" << bw.peakUploadBps << ",";
    ss << "\"totalDownloadMB\":" << std::setprecision(2) << bw.totalDownloadBytes / (1024.0*1024.0) << ",";
    ss << "\"totalUploadMB\":" << bw.totalUploadBytes / (1024.0*1024.0) << ",";
    ss << "\"downloadStdDev\":" << bw.downloadStdDev << ",";
    ss << "\"uploadStdDev\":" << bw.uploadStdDev << "},";

    // Latency
    const auto& lat = report.latency;
    ss << "\"latency\":{";
    ss << "\"minRttMs\":" << std::fixed << std::setprecision(2) << lat.minRttMs << ",";
    ss << "\"maxRttMs\":" << lat.maxRttMs << ",";
    ss << "\"avgRttMs\":" << lat.avgRttMs << ",";
    ss << "\"medianRttMs\":" << lat.medianRttMs << ",";
    ss << "\"p95RttMs\":" << lat.p95RttMs << ",";
    ss << "\"p99RttMs\":" << lat.p99RttMs << ",";
    ss << "\"jitterMs\":" << lat.jitterMs << ",";
    ss << "\"maxJitterMs\":" << lat.maxJitterMs << ",";
    ss << "\"spikeCount\":" << lat.spikeCount << ",";
    ss << "\"packetLossRate\":" << std::setprecision(4) << lat.packetLossRate << "},";

    // Quality
    ss << "\"quality\":{";
    ss << "\"score\":" << std::fixed << std::setprecision(1) << report.qualityScore << ",";
    ss << "\"stability\":" << report.stabilityScore << ",";
    ss << "\"rating\":" << static_cast<int>(report.quality) << "},";

    // Connection
    ss << "\"connection\":{";
    ss << "\"state\":" << static_cast<int>(report.connectionState) << ",";
    ss << "\"lastPacketTime\":" << report.lastPacketTime << "},";

    // Alerts
    ss << "\"alerts\":{";
    ss << "\"total\":" << report.alertCount << ",";
    ss << "\"unacknowledged\":" << report.unacknowledgedAlerts << ",";
    ss << "\"items\":[";
    for (size_t i = 0; i < report.recentAlerts.size(); i++) {
        const auto& a = report.recentAlerts[i];
        ss << "{\"type\":" << static_cast<int>(a.type)
           << ",\"value\":" << a.value
           << ",\"threshold\":" << a.threshold
           << ",\"message\":\"" << a.message << "\""
           << ",\"acknowledged\":" << (a.acknowledged ? "true" : "false") << "}";
        if (i < report.recentAlerts.size() - 1) ss << ",";
    }
    ss << "]}},";

    // Recommendations
    ss << "\"recommendations\":[";
    for (size_t i = 0; i < report.recommendations.size(); i++) {
        ss << "\"" << report.recommendations[i] << "\"";
        if (i < report.recommendations.size() - 1) ss << ",";
    }
    ss << "]}}";

    return ss.str();
}

std::string NetworkProfiler::ExportBandwidthCSV() const {
    std::ostringstream ss;
    ss << "timestamp_us,rtt_ms,jitter_ms,download_bps,upload_bps,pkts_in,pkts_out,pkts_lost,pkt_rate_in,pkt_rate_out\n";

    for (const auto& s : m_samples) {
        ss << s.timestamp << ","
           << std::fixed << std::setprecision(3) << s.rttMs << ","
           << s.jitterMs << ","
           << std::fixed << std::setprecision(2) << s.downloadBps << ","
           << s.uploadBps << ","
           << s.packetsIn << ","
           << s.packetsOut << ","
           << s.packetsLost << ","
           << std::fixed << std::setprecision(2) << s.packetRateIn << ","
           << s.packetRateOut << "\n";
    }

    return ss.str();
}

std::string NetworkProfiler::ExportLatencyCSV() const {
    std::ostringstream ss;
    ss << "timestamp_us,rtt_ms,jitter_ms\n";

    for (const auto& s : m_samples) {
        if (s.rttMs <= 0) continue;
        ss << s.timestamp << ","
           << std::fixed << std::setprecision(3) << s.rttMs << ","
           << s.jitterMs << "\n";
    }

    return ss.str();
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void NetworkProfiler::Reset() {
    m_samples.clear();
    m_alerts.clear();
    m_rttSum = m_rttSumSq = 0.0;
    m_downloadSum = m_downloadSumSq = 0.0;
    m_uploadSum = m_uploadSumSq = 0.0;
    m_jitterSum = 0.0;
    m_jitterCount = 0;
    m_prevRttMs = 0.0;
    m_dirty = true;
    m_sessionStartTime = NowUs();
    m_cachedReport = NetworkReport();
}

void NetworkProfiler::SetEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        m_connectionState = ConnectionState::Disconnected;
    } else {
        m_connectionState = ConnectionState::Connected;
    }
}

} // namespace ProfilerCore
