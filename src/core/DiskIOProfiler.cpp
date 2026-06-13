#include "DiskIOProfiler.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <numeric>
#include <random>
#include <limits>

namespace ProfilerCore {

// ─── Helper: microsecond timestamp ───────────────────────────────────────────

static int64_t NowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

// ─── Helper: IO type name ─────────────────────────────────────────────────────

static const char* OpTypeName(DiskIOOpType type) {
    switch (type) {
        case DiskIOOpType::Read:    return "Read";
        case DiskIOOpType::Write:   return "Write";
        case DiskIOOpType::Flush:   return "Flush";
        case DiskIOOpType::Create:  return "Create";
        case DiskIOOpType::Delete:  return "Delete";
        case DiskIOOpType::Seek:    return "Seek";
        default:                    return "Unknown";
    }
}

static const char* HealthName(DiskHealth health) {
    switch (health) {
        case DiskHealth::Healthy:   return "Healthy";
        case DiskHealth::Moderate:  return "Moderate";
        case DiskHealth::Congested: return "Congested";
        case DiskHealth::Critical:  return "Critical";
        case DiskHealth::Failing:   return "Failing";
        default:                    return "Unknown";
    }
}

static const char* AlertTypeName(DiskAlertType type) {
    switch (type) {
        case DiskAlertType::HighLatency:        return "HighLatency";
        case DiskAlertType::QueueDepthExceeded: return "QueueDepthExceeded";
        case DiskAlertType::BandwidthSaturation:return "BandwidthSaturation";
        case DiskAlertType::StutterPattern:     return "StutterPattern";
        case DiskAlertType::WriteBurst:         return "WriteBurst";
        case DiskAlertType::ReadBurst:          return "ReadBurst";
        case DiskAlertType::IOPSpike:           return "IOPSpike";
        case DiskAlertType::SustainedLoad:      return "SustainedLoad";
        default:                                return "Unknown";
    }
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

DiskIOProfiler::DiskIOProfiler() {
    m_samples.reserve(m_config.windowSize);
}

DiskIOProfiler::~DiskIOProfiler() {
}

// ─── Configuration ────────────────────────────────────────────────────────────

void DiskIOProfiler::Configure(const DiskIOConfig& config) {
    m_config = config;
    TrimWindow();
    m_dirty = true;
    m_fileStatsDirty = true;
}

DiskIOConfig DiskIOProfiler::GetConfig() const {
    return m_config;
}

// ─── Data Recording ──────────────────────────────────────────────────────────

void DiskIOProfiler::RecordOperation(
    DiskIOOpType opType,
    uint64_t     offset,
    uint64_t     sizeBytes,
    double       durationMs,
    const std::string& filePath,
    uint32_t     queueDepth,
    bool         isAsync,
    bool         isPageFault)
{
    if (!m_enabled) return;

    int64_t now = NowUs();

    SampleEntry entry;
    entry.timestamp   = now;
    entry.opType      = opType;
    entry.offset      = offset;
    entry.sizeBytes   = sizeBytes;
    entry.durationMs  = durationMs;
    entry.filePath    = m_config.trackFilePaths ? filePath : "";
    entry.queueDepth  = queueDepth;
    entry.isAsync     = isAsync;
    entry.isPageFault = isPageFault;

    // Update running totals
    m_totalOps++;

    if (opType == DiskIOOpType::Read || opType == DiskIOOpType::Write) {
        // Detect stutter: operation duration exceeded frame budget
        if (durationMs > m_config.stutterThresholdMs) {
            m_stutterEvents++;
        }

        if (opType == DiskIOOpType::Read) {
            m_totalReadBytes += sizeBytes;
            m_readCount++;
            m_readLatencySum += durationMs;
            if (durationMs > m_maxLatencyRead) {
                m_maxLatencyRead = durationMs;
            }
        } else {
            m_totalWriteBytes += sizeBytes;
            m_writeCount++;
            m_writeLatencySum += durationMs;
            if (durationMs > m_maxLatencyWrite) {
                m_maxLatencyWrite = durationMs;
            }
        }
    }

    // Track queue depth
    if (queueDepth > m_maxQueueDepth) {
        m_maxQueueDepth = queueDepth;
    }
    m_queueDepthSum += queueDepth;
    m_queueDepthCount++;

    m_samples.push_back(entry);

    TrimWindow();

    m_dirty = true;
    m_fileStatsDirty = true;

    // Validate any file path argument and identify
    if (m_config.alertsEnabled && durationMs >= m_config.slowOpThresholdMs) {
        DiskAlert alert = MakeAlert(
            DiskAlertType::HighLatency,
            durationMs,
            m_config.slowOpThresholdMs,
            "I/O operation exceeded threshold: " + std::to_string(durationMs) +
            "ms (" + OpTypeName(opType) + " " +
            std::to_string(sizeBytes / 1024) + "KB)",
            filePath
        );
        m_alerts.push_back(alert);

        if (m_alertCallback) {
            m_alertCallback(alert);
        }
    }
}

void DiskIOProfiler::RecordSimulatedSample() {
    using namespace std;

    m_simFrameCount++;
    static mt19937 gen(73); // deterministic seed
    static normal_distribution<> latency_noise(0.0, 2.0);
    static uniform_int_distribution<int> file_selector(0, 5);

    // Simulate file paths for a typical game
    static const char* files[] = {
        "assets/textures/terrain_diffuse.dds",
        "assets/models/character_base.skinned",
        "assets/textures/skybox_cubemap.dds",
        "assets/audio/ambient_loop.ogg",
        "data/savegame/slot_01.sav",
        "shaders/cache/compiled_shaders.bin"
    };

    // Determine operation type: predominantly reads (~80%)
    DiskIOOpType opType = (gen() % 5 == 0) ?
        DiskIOOpType::Write : DiskIOOpType::Read;

    uint64_t sizeBytes = 0;
    double durationMs = 0.0;

    if (opType == DiskIOOpType::Read) {
        // Texture/model loads: 4KB - 4MB
        double logSize = 12.0 + (gen() % 12); // 2^12 to 2^24
        sizeBytes = static_cast<uint64_t>(pow(2.0, logSize));
        // Random offset to simulate scattered reads
        m_simOffset += sizeBytes;
        if (m_simOffset > 1024ULL * 1024ULL * 1024ULL) {
            m_simOffset = 0;
        }

        // Latency scales with size + noise
        durationMs = max(0.1, sizeBytes / (100.0 * 1024.0 * 1024.0) * 1000.0
                        + latency_noise(gen) * 0.5);

        // Simulate page faults (~1% of reads)
        bool isPageFault = (gen() % 100 == 0);
        if (isPageFault) {
            durationMs += 30.0 + abs(latency_noise(gen)) * 5.0;
        }

        // Occasional stutter spike (~every 300 samples)
        if (m_simFrameCount % 300 < 3) {
            durationMs = 80.0 + abs(latency_noise(gen)) * 20.0;
            sizeBytes = 8 * 1024 * 1024; // 8MB burst
        }
    } else {
        // Writes: smaller, more predictable
        sizeBytes = 4 * 1024 + (gen() % (64 * 1024));
        durationMs = max(0.1, sizeBytes / (50.0 * 1024.0 * 1024.0) * 1000.0
                        + latency_noise(gen) * 0.3);

        // Save game burst (~every 600 samples)
        if (m_simFrameCount % 600 < 5) {
            sizeBytes = 512 * 1024; // 512KB
            durationMs = 15.0 + abs(latency_noise(gen)) * 5.0;
        }
    }

    const char* filePath = files[file_selector(gen)];
    uint32_t queueDepth = static_cast<uint32_t>(1 + (gen() % 8));
    if (m_simFrameCount % 200 < 5) {
        queueDepth = 16 + (gen() % 16); // simulate queue buildup
    }

    RecordOperation(opType, m_simOffset, sizeBytes, durationMs,
                    filePath, queueDepth, true, false);
}

// ─── Analysis ─────────────────────────────────────────────────────────────────

DiskIOReport DiskIOProfiler::GenerateReport() const {
    if (m_dirty) {
        m_cachedReport = DiskIOReport();
        m_cachedReport.timestamp = NowUs();
        m_cachedReport.stats = DiskIOStats();
        m_cachedReport.stats.startTime = m_samples.empty() ? 0 : m_samples.front().timestamp;
        m_cachedReport.stats.endTime   = m_samples.empty() ? 0 : m_samples.back().timestamp;

        m_cachedReport.sessionDurationMs =
            (m_cachedReport.stats.endTime - m_cachedReport.stats.startTime) / 1000;

        ComputeStats();
        ProcessPerFileStats();
        DetectAlerts();
        GenerateRecommendations();

        m_dirty = false;
    }

    return m_cachedReport;
}

void DiskIOProfiler::ComputeStats() const {
    auto& stats = m_cachedReport.stats;

    if (m_samples.empty()) return;

    stats.totalReadBytes  = m_totalReadBytes;
    stats.totalWriteBytes = m_totalWriteBytes;
    stats.totalOpCount    = m_totalOps;
    stats.readOpCount     = m_readCount;
    stats.writeOpCount    = m_writeCount;

    double elapsedSec = m_samples.empty() ? 1.0 :
        std::max(1.0, (m_samples.back().timestamp - m_samples.front().timestamp) / 1e6);

    // ─── Latency ────────────────────────────────────────────────────────────
    stats.avgReadLatencyMs  = (m_readCount  > 0) ? m_readLatencySum  / m_readCount  : 0.0;
    stats.avgWriteLatencyMs = (m_writeCount > 0) ? m_writeLatencySum / m_writeCount : 0.0;
    stats.maxReadLatencyMs  = m_maxLatencyRead;
    stats.maxWriteLatencyMs = m_maxLatencyWrite;

    // ─── Throughput ──────────────────────────────────────────────────────────
    stats.avgReadThroughputMBps  = (m_totalReadBytes  / (1024.0 * 1024.0)) / elapsedSec;
    stats.avgWriteThroughputMBps = (m_totalWriteBytes / (1024.0 * 1024.0)) / elapsedSec;

    // Peak throughput: scan samples, find busiest 1-second window
    {
        double peakRead = 0.0, peakWrite = 0.0;
        size_t start = 0;
        uint64_t windowRead = 0, windowWrite = 0;

        for (size_t end = 0; end < m_samples.size(); end++) {
            if (m_samples[end].opType == DiskIOOpType::Read) {
                windowRead += m_samples[end].sizeBytes;
            } else if (m_samples[end].opType == DiskIOOpType::Write) {
                windowWrite += m_samples[end].sizeBytes;
            }

            // Advance start pointer until window is <= 1 second
            while (start < end &&
                   m_samples[end].timestamp - m_samples[start].timestamp > 1000000) {
                if (m_samples[start].opType == DiskIOOpType::Read) {
                    windowRead -= m_samples[start].sizeBytes;
                } else if (m_samples[start].opType == DiskIOOpType::Write) {
                    windowWrite -= m_samples[start].sizeBytes;
                }
                start++;
            }

            double windowSec = (m_samples[end].timestamp - m_samples[start].timestamp) / 1e6;
            if (windowSec > 0.5) {
                peakRead  = std::max(peakRead,  windowRead  / (1024.0 * 1024.0) / windowSec);
                peakWrite = std::max(peakWrite, windowWrite / (1024.0 * 1024.0) / windowSec);
            }
        }

        stats.peakReadThroughputMBps  = peakRead;
        stats.peakWriteThroughputMBps = peakWrite;
    }

    // ─── Queue depth ────────────────────────────────────────────────────────
    stats.avgQueueDepth = (m_queueDepthCount > 0) ?
        m_queueDepthSum / m_queueDepthCount : 0.0;
    stats.maxQueueDepth = m_maxQueueDepth;

    // Count queue stalls
    stats.queueStallCount = 0;
    for (const auto& s : m_samples) {
        if (s.queueDepth >= m_config.maxQueueDepthWarning) {
            stats.queueStallCount++;
        }
    }

    // ─── IOPS ───────────────────────────────────────────────────────────────
    stats.avgIOPS    = m_totalOps / elapsedSec;
    stats.peakIOPS   = 0.0;

    // Peak IOPS: scan 1-second sliding window
    {
        size_t start = 0;
        uint64_t opsInWindow = 0;
        for (size_t end = 0; end < m_samples.size(); end++) {
            opsInWindow++;
            while (start < end &&
                   m_samples[end].timestamp - m_samples[start].timestamp > 1000000) {
                opsInWindow--;
                start++;
            }
            double windowSec = (m_samples[end].timestamp - m_samples[start].timestamp) / 1e6;
            if (windowSec > 0.5) {
                stats.peakIOPS = std::max(stats.peakIOPS, opsInWindow / windowSec);
            }
        }
    }

    // ─── Slow ops ────────────────────────────────────────────────────────────
    stats.slowOpCount = 0;
    for (const auto& s : m_samples) {
        if (s.durationMs >= m_config.slowOpThresholdMs) {
            stats.slowOpCount++;
        }
    }

    // ─── Stutter events ─────────────────────────────────────────────────────
    stats.totalStutterEvents = m_stutterEvents;

    // ─── Health score ───────────────────────────────────────────────────────
    // Composite score based on multiple metrics
    double latencyScore = 100.0;
    if (stats.avgReadLatencyMs > 0) {
        double ratio = stats.avgReadLatencyMs / 10.0; // 10ms = baseline
        latencyScore = std::max(0.0, 100.0 - ratio * 30.0);
    }
    // Penalize max latency more heavily
    if (stats.maxReadLatencyMs > m_config.criticalLatencyMs) {
        latencyScore -= std::min(30.0, (stats.maxReadLatencyMs - m_config.criticalLatencyMs) * 0.3);
    }

    double queueScore = 100.0;
    if (stats.avgQueueDepth > 0) {
        double ratio = stats.avgQueueDepth / 4.0; // depth of 4 = baseline
        queueScore = std::max(0.0, 100.0 - ratio * 20.0);
    }
    if (stats.maxQueueDepth > m_config.maxQueueDepthCritical) {
        queueScore -= std::min(25.0, (stats.maxQueueDepth - m_config.maxQueueDepthCritical) * 0.5);
    }

    double throughputScore = 100.0;
    double totalMB = (m_totalReadBytes + m_totalWriteBytes) / (1024.0 * 1024.0);
    double totalSec = elapsedSec;
    if (totalMB > 0 && totalSec > 0) {
        double avgMBps = totalMB / totalSec;
        double utilPct = (avgMBps / m_config.throughputWarningMBps) * 100.0;
        if (utilPct > 90.0) {
            throughputScore = std::max(0.0, 100.0 - (utilPct - 90.0) * 5.0);
        }
    }

    double stutterPenalty = std::min(20.0, m_stutterEvents * 1.0);
    double slowOpPenalty  = std::min(15.0, stats.slowOpCount * 0.5);

    stats.healthScore = std::max(0.0, std::min(100.0,
        latencyScore * 0.40 + queueScore * 0.25 +
        throughputScore * 0.15 - stutterPenalty - slowOpPenalty
    ));

    stats.health = ComputeHealthFromScore(stats.healthScore);

    m_cachedReport.stats = stats;
}

void DiskIOProfiler::ProcessPerFileStats() const {
    if (!m_fileStatsDirty || !m_config.trackFilePaths) return;

    m_fileStatsCache.clear();

    for (const auto& s : m_samples) {
        if (s.filePath.empty()) continue;

        auto& fs = m_fileStatsCache[s.filePath];

        if (fs.filePath.empty()) {
            fs.filePath = s.filePath;
        }

        if (s.opType == DiskIOOpType::Read) {
            fs.totalReadBytes += s.sizeBytes;
            fs.readCount++;
            fs.totalReadDurationMs += s.durationMs;
            fs.maxReadLatencyMs = std::max(fs.maxReadLatencyMs, s.durationMs);
            if (s.durationMs > m_config.stutterThresholdMs) {
                fs.stutterEvents++;
            }
        } else if (s.opType == DiskIOOpType::Write) {
            fs.totalWriteBytes += s.sizeBytes;
            fs.writeCount++;
            fs.totalWriteDurationMs += s.durationMs;
            fs.maxWriteLatencyMs = std::max(fs.maxWriteLatencyMs, s.durationMs);
        }
    }

    // Compute derived metrics per file
    for (auto& [path, fs] : m_fileStatsCache) {
        double elapsedSec = m_samples.empty() ? 1.0 :
            std::max(1.0, (m_samples.back().timestamp - m_samples.front().timestamp) / 1e6);

        fs.avgReadLatencyMs  = (fs.readCount  > 0) ? fs.totalReadDurationMs  / fs.readCount  : 0.0;
        fs.avgWriteLatencyMs = (fs.writeCount > 0) ? fs.totalWriteDurationMs / fs.writeCount : 0.0;
        fs.readThroughputMBps  = (fs.totalReadBytes  / (1024.0 * 1024.0)) / elapsedSec;
        fs.writeThroughputMBps = (fs.totalWriteBytes / (1024.0 * 1024.0)) / elapsedSec;
    }

    // Sort by total I/O volume and keep top N
    std::vector<std::pair<std::string, FileIOStats>> sorted(
        m_fileStatsCache.begin(), m_fileStatsCache.end()
    );

    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) {
            uint64_t totalA = a.second.totalReadBytes + a.second.totalWriteBytes;
            uint64_t totalB = b.second.totalReadBytes + b.second.totalWriteBytes;
            return totalA > totalB;
        });

    m_cachedReport.stats.topFiles.clear();
    for (size_t i = 0; i < std::min(static_cast<size_t>(m_config.trackedFiles), sorted.size()); i++) {
        m_cachedReport.stats.topFiles.push_back(sorted[i].second);
    }

    m_fileStatsDirty = false;
}

void DiskIOProfiler::DetectAlerts() const {
    if (!m_config.alertsEnabled) return;

    const auto& stats = m_cachedReport.stats;

    // Queue depth exceeded
    if (stats.maxQueueDepth >= m_config.maxQueueDepthCritical) {
        m_alerts.push_back(MakeAlert(
            DiskAlertType::QueueDepthExceeded,
            static_cast<double>(stats.maxQueueDepth),
            static_cast<double>(m_config.maxQueueDepthCritical),
            "I/O queue depth critically high: " +
            std::to_string(stats.maxQueueDepth) +
            " (threshold: " + std::to_string(m_config.maxQueueDepthCritical) + ")"
        ));
    } else if (stats.maxQueueDepth >= m_config.maxQueueDepthWarning) {
        m_alerts.push_back(MakeAlert(
            DiskAlertType::QueueDepthExceeded,
            static_cast<double>(stats.maxQueueDepth),
            static_cast<double>(m_config.maxQueueDepthWarning),
            "I/O queue depth elevated: " +
            std::to_string(stats.maxQueueDepth) +
            " (threshold: " + std::to_string(m_config.maxQueueDepthWarning) + ")"
        ));
    }

    // Bandwidth saturation
    double totalMBps = stats.avgReadThroughputMBps + stats.avgWriteThroughputMBps;
    if (totalMBps > m_config.throughputWarningMBps) {
        m_alerts.push_back(MakeAlert(
            DiskAlertType::BandwidthSaturation,
            totalMBps,
            m_config.throughputWarningMBps,
            "Disk bandwidth saturation: " +
            std::to_string(static_cast<int>(totalMBps)) +
            " MB/s (threshold: " +
            std::to_string(static_cast<int>(m_config.throughputWarningMBps)) + " MB/s)"
        ));
    }

    // Stutter pattern
    if (m_stutterEvents > 5) {
        m_alerts.push_back(MakeAlert(
            DiskAlertType::StutterPattern,
            m_stutterEvents,
            5,
            "Disk-related stutter detected: " +
            std::to_string(m_stutterEvents) +
            " operations exceeded frame budget. " +
            "Consider async asset loading or pre-streaming."
        ));
    }

    // Sustained load
    if (stats.totalOpCount > 100 && stats.healthScore < 40.0) {
        m_alerts.push_back(MakeAlert(
            DiskAlertType::SustainedLoad,
            stats.healthScore,
            40.0,
            "Sustained high disk load detected (health score: " +
            std::to_string(static_cast<int>(stats.healthScore)) + "/100). " +
            "Consider I/O prioritization or background throttling."
        ));
    }

    // Peak IOPS alert (if abnormally high)
    if (stats.peakIOPS > 5000.0) {
        m_alerts.push_back(MakeAlert(
            DiskAlertType::IOPSpike,
            stats.peakIOPS,
            5000.0,
            "Very high I/O operations per second: " +
            std::to_string(static_cast<int>(stats.peakIOPS)) +
            " IOPS. Check for thrashing or excessive file access."
        ));
    }

    // Check top files for read burst patterns
    for (const auto& f : stats.topFiles) {
        if (f.readThroughputMBps > 500.0) {
            m_alerts.push_back(MakeAlert(
                DiskAlertType::ReadBurst,
                f.readThroughputMBps,
                500.0,
                "Heavy read activity on: " +
                TruncatePath(f.filePath) + " (" +
                std::to_string(static_cast<int>(f.readThroughputMBps)) + " MB/s)",
                f.filePath
            ));
            break; // one alert per report for this type
        }

        if (f.writeThroughputMBps > 100.0) {
            m_alerts.push_back(MakeAlert(
                DiskAlertType::WriteBurst,
                f.writeThroughputMBps,
                100.0,
                "Heavy write activity on: " +
                TruncatePath(f.filePath) + " (" +
                std::to_string(static_cast<int>(f.writeThroughputMBps)) + " MB/s)",
                f.filePath
            ));
            break;
        }

        if (f.stutterEvents > 2) {
            m_alerts.push_back(MakeAlert(
                DiskAlertType::StutterPattern,
                f.stutterEvents,
                2,
                "File causing repeated stutter: " +
                TruncatePath(f.filePath) + " (" +
                std::to_string(f.stutterEvents) + " events)",
                f.filePath
            ));
            break;
        }
    }

    m_cachedReport.alertCount = static_cast<int>(m_alerts.size());
    int unack = 0;
    for (const auto& a : m_alerts) {
        if (!a.acknowledged) unack++;
    }
    m_cachedReport.unacknowledgedAlerts = unack;
    m_cachedReport.alerts = m_alerts;
}

DiskAlert DiskIOProfiler::MakeAlert(
    DiskAlertType type, double value, double threshold,
    const std::string& message, const std::string& filePath) const
{
    DiskAlert alert;
    alert.type         = type;
    alert.timestamp    = NowUs();
    alert.value        = value;
    alert.threshold    = threshold;
    alert.message      = message;
    alert.filePath     = filePath;
    alert.acknowledged = false;
    return alert;
}

DiskHealth DiskIOProfiler::ComputeHealthFromScore(double score) {
    if (score >= 80.0) return DiskHealth::Healthy;
    if (score >= 60.0) return DiskHealth::Moderate;
    if (score >= 40.0) return DiskHealth::Congested;
    if (score >= 20.0) return DiskHealth::Critical;
    return DiskHealth::Failing;
}

std::string DiskIOProfiler::TruncatePath(const std::string& path, size_t maxLen) {
    if (path.length() <= maxLen) return path;
    // Keep the filename part, truncate the path
    size_t lastSep = path.find_last_of("/\\");
    if (lastSep == std::string::npos || lastSep <= 3) {
        return "..." + path.substr(path.length() - maxLen + 3);
    }
    std::string filePart = path.substr(lastSep);
    if (filePart.length() + 6 <= maxLen) {
        return "..." + path.substr(lastSep - (maxLen - filePart.length() - 3));
    }
    return ".../" + filePart;
}

void DiskIOProfiler::GenerateRecommendations() const {
    m_cachedReport.recommendations.clear();

    const auto& stats = m_cachedReport.stats;

    if (stats.healthScore >= 80.0) {
        m_cachedReport.recommendations.push_back(
            "Disk I/O is healthy. No action required."
        );
        return;
    }

    // ─── Latency recommendations ────────────────────────────────────────────
    if (stats.avgReadLatencyMs > 20.0) {
        m_cachedReport.recommendations.push_back(
            "High average read latency (" +
            std::to_string(static_cast<int>(stats.avgReadLatencyMs)) +
            "ms). Consider: enabling async file I/O, moving frequently "
            "accessed assets to faster storage (NVMe/SSD vs HDD), "
            "or implementing asset pre-loading during loading screens."
        );
    }

    if (stats.maxReadLatencyMs > m_config.criticalLatencyMs) {
        m_cachedReport.recommendations.push_back(
            "Critical read latency spikes detected (up to " +
            std::to_string(static_cast<int>(stats.maxReadLatencyMs)) +
            "ms). Investigate whether these are caused by shader "
            "compilation, texture streaming, or level-of-detail transitions."
        );
    }

    // ─── Queue depth recommendations ────────────────────────────────────────
    if (stats.avgQueueDepth > 5.0) {
        m_cachedReport.recommendations.push_back(
            "Elevated I/O queue depth (avg: " +
            std::to_string(static_cast<int>(stats.avgQueueDepth)) +
            ", max: " + std::to_string(stats.maxQueueDepth) +
            "). Consider reducing the number of concurrent file operations, "
            "implementing I/O priority scheduling, or using a dedicated "
            "streaming thread pool."
        );
    }

    // ─── Throughput recommendations ─────────────────────────────────────────
    double totalMBps = stats.avgReadThroughputMBps + stats.avgWriteThroughputMBps;
    if (totalMBps > m_config.throughputWarningMBps) {
        m_cachedReport.recommendations.push_back(
            "Disk bandwidth is saturated (" +
            std::to_string(static_cast<int>(totalMBps)) +
            " MB/s). Consider improving texture compression (BC7, ASTC), "
            "reducing asset resolution at distance (virtual texturing), "
            "or enabling on-demand streaming with priority queues."
        );
    }

    // ─── Stutter recommendations ────────────────────────────────────────────
    if (m_stutterEvents > 3) {
        m_cachedReport.recommendations.push_back(
            "Disk-related stuttering detected (" +
            std::to_string(m_stutterEvents) +
            " events). Common mitigations: implement predictive asset "
            "prefetching, use mipmap streaming with priority fallbacks, "
            "convert to chunk-based file format for seek-friendly access, "
            "or increase file cache size."
        );
    }

    // ─── Top file recommendations ───────────────────────────────────────────
    for (const auto& f : stats.topFiles) {
        if (f.stutterEvents > 2) {
            m_cachedReport.recommendations.push_back(
                "Problematic file: " + TruncatePath(f.filePath, 60) +
                " (" + std::to_string(f.stutterEvents) + " stutters, " +
                std::to_string(static_cast<int>(f.readThroughputMBps)) +
                " MB/s). Consider: bundling with other assets, "
                "compressing further, or loading asynchronously."
            );
            break;
        }
    }

    // ─── Health-based recommendations ───────────────────────────────────────
    if (stats.health < DiskHealth::Congested) {
        m_cachedReport.recommendations.push_back(
            "Disk health is degraded (score: " +
            std::to_string(static_cast<int>(stats.healthScore)) +
            "/100). For HDD: defragment the drive. For SSD: check TRIM "
            "status and available space. In-game: reduce texture pool size "
            "or increase streaming buffer."
        );
    }

    if (stats.totalStutterEvents > 10) {
        m_cachedReport.recommendations.push_back(
            "Severe stuttering pattern detected. Consider a fundamental "
            "re-architecture of the asset streaming system: implement "
            "multi-tier memory (RAM -> VRAM -> Disk) with LRU eviction, "
            "and never synchronously load assets on the render thread."
        );
    }

    // Safety limit
    while (m_cachedReport.recommendations.size() > 6) {
        m_cachedReport.recommendations.pop_back();
    }
}

// ─── Quick Accessors ───────────────────────────────────────────────────────────

double DiskIOProfiler::GetCurrentReadLatencyMs() const {
    for (auto it = m_samples.rbegin(); it != m_samples.rend(); ++it) {
        if (it->opType == DiskIOOpType::Read) {
            return it->durationMs;
        }
    }
    return 0.0;
}

double DiskIOProfiler::GetCurrentWriteLatencyMs() const {
    for (auto it = m_samples.rbegin(); it != m_samples.rend(); ++it) {
        if (it->opType == DiskIOOpType::Write) {
            return it->durationMs;
        }
    }
    return 0.0;
}

double DiskIOProfiler::GetAverageReadThroughputMBps() const {
    return GenerateReport().stats.avgReadThroughputMBps;
}

double DiskIOProfiler::GetAverageWriteThroughputMBps() const {
    return GenerateReport().stats.avgWriteThroughputMBps;
}

double DiskIOProfiler::GetCurrentQueueDepth() const {
    if (m_samples.empty()) return 0.0;
    return m_samples.back().queueDepth;
}

double DiskIOProfiler::GetHealthScore() const {
    return GenerateReport().stats.healthScore;
}

DiskHealth DiskIOProfiler::GetDiskHealth() const {
    return GenerateReport().stats.health;
}

int DiskIOProfiler::GetStutterEventCount() const {
    return m_stutterEvents;
}

int DiskIOProfiler::GetAlertCount() const {
    return static_cast<int>(m_alerts.size());
}

std::vector<DiskAlert> DiskIOProfiler::GetActiveAlerts() const {
    GenerateReport();
    std::vector<DiskAlert> active;
    for (const auto& a : m_alerts) {
        if (!a.acknowledged) {
            active.push_back(a);
        }
    }
    return active;
}

bool DiskIOProfiler::AcknowledgeAlert(int64_t timestamp) {
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

void DiskIOProfiler::AcknowledgeAllAlerts() {
    for (auto& a : m_alerts) {
        a.acknowledged = true;
    }
    m_cachedReport.unacknowledgedAlerts = 0;
}

void DiskIOProfiler::SetAlertCallback(DiskAlertCallback callback) {
    m_alertCallback = callback;
}

// ─── Export ───────────────────────────────────────────────────────────────────

std::string DiskIOProfiler::ExportToJSON() const {
    DiskIOReport report = GenerateReport();

    std::ostringstream ss;
    ss << "{\"diskIOProfiler\":{";
    ss << "\"timestamp\":" << report.timestamp << ",";
    ss << "\"sessionDurationMs\":" << report.sessionDurationMs << ",";

    const auto& s = report.stats;
    ss << "\"stats\":{";
    ss << "\"totalReadBytes\":" << s.totalReadBytes << ",";
    ss << "\"totalWriteBytes\":" << s.totalWriteBytes << ",";
    ss << "\"totalOpCount\":" << s.totalOpCount << ",";
    ss << "\"readOpCount\":" << s.readOpCount << ",";
    ss << "\"writeOpCount\":" << s.writeOpCount << ",";

    ss << "\"latency\":{";
    ss << "\"avgReadMs\":" << std::fixed << std::setprecision(3) << s.avgReadLatencyMs << ",";
    ss << "\"avgWriteMs\":" << s.avgWriteLatencyMs << ",";
    ss << "\"maxReadMs\":" << s.maxReadLatencyMs << ",";
    ss << "\"maxWriteMs\":" << s.maxWriteLatencyMs << "},";

    ss << "\"throughput\":{";
    ss << "\"avgReadMBps\":" << std::setprecision(2) << s.avgReadThroughputMBps << ",";
    ss << "\"avgWriteMBps\":" << s.avgWriteThroughputMBps << ",";
    ss << "\"peakReadMBps\":" << s.peakReadThroughputMBps << ",";
    ss << "\"peakWriteMBps\":" << s.peakWriteThroughputMBps << "},";

    ss << "\"queue\":{";
    ss << "\"avgDepth\":" << std::setprecision(2) << s.avgQueueDepth << ",";
    ss << "\"maxDepth\":" << s.maxQueueDepth << ",";
    ss << "\"stallCount\":" << s.queueStallCount << "},";

    ss << "\"iops\":{";
    ss << "\"avg\":" << std::setprecision(1) << s.avgIOPS << ",";
    ss << "\"peak\":" << s.peakIOPS << "},";

    ss << "\"slowOpCount\":" << s.slowOpCount << ",";
    ss << "\"totalStutterEvents\":" << s.totalStutterEvents << ",";

    ss << "\"health\":{";
    ss << "\"score\":" << std::setprecision(1) << s.healthScore << ",";
    ss << "\"status\":\"" << HealthName(s.health) << "\"},";

    // Top files
    ss << "\"topFiles\":[";
    for (size_t i = 0; i < s.topFiles.size(); i++) {
        const auto& f = s.topFiles[i];
        ss << "{\"path\":\"" << f.filePath << "\"";
        ss << ",\"totalReadMB\":" << std::setprecision(2)
           << (f.totalReadBytes / (1024.0 * 1024.0));
        ss << ",\"totalWriteMB\":" << (f.totalWriteBytes / (1024.0 * 1024.0));
        ss << ",\"avgReadLatencyMs\":" << std::setprecision(3) << f.avgReadLatencyMs;
        ss << ",\"avgWriteLatencyMs\":" << f.avgWriteLatencyMs;
        ss << ",\"maxReadLatencyMs\":" << f.maxReadLatencyMs;
        ss << ",\"maxWriteLatencyMs\":" << f.maxWriteLatencyMs;
        ss << ",\"readThroughputMBps\":" << std::setprecision(2) << f.readThroughputMBps;
        ss << ",\"writeThroughputMBps\":" << f.writeThroughputMBps;
        ss << ",\"stutterEvents\":" << f.stutterEvents;
        ss << "}";
        if (i < s.topFiles.size() - 1) ss << ",";
    }
    ss << "]},";

    // Alerts
    ss << "\"alerts\":{";
    ss << "\"total\":" << report.alertCount << ",";
    ss << "\"unacknowledged\":" << report.unacknowledgedAlerts << ",";
    ss << "\"items\":[";
    for (size_t i = 0; i < report.alerts.size(); i++) {
        const auto& a = report.alerts[i];
        ss << "{\"type\":\"" << AlertTypeName(a.type) << "\"";
        ss << ",\"value\":" << a.value;
        ss << ",\"threshold\":" << a.threshold;
        ss << ",\"message\":\"" << a.message << "\"";
        ss << ",\"filePath\":\"" << a.filePath << "\"";
        ss << ",\"acknowledged\":" << (a.acknowledged ? "true" : "false") << "}";
        if (i < report.alerts.size() - 1) ss << ",";
    }
    ss << "]},";

    // Recommendations
    ss << "\"recommendations\":[";
    for (size_t i = 0; i < report.recommendations.size(); i++) {
        ss << "\"" << report.recommendations[i] << "\"";
        if (i < report.recommendations.size() - 1) ss << ",";
    }
    ss << "]}}";

    return ss.str();
}

std::string DiskIOProfiler::ExportIOTimelineCSV() const {
    std::ostringstream ss;
    ss << "timestamp_us,op_type,offset,size_bytes,duration_ms,file_path,queue_depth,is_async,is_page_fault\n";

    for (const auto& s : m_samples) {
        ss << s.timestamp << ","
           << OpTypeName(s.opType) << ","
           << s.offset << ","
           << s.sizeBytes << ","
           << std::fixed << std::setprecision(3) << s.durationMs << ","
           << "\"" << s.filePath << "\","
           << s.queueDepth << ","
           << (s.isAsync ? "1" : "0") << ","
           << (s.isPageFault ? "1" : "0") << "\n";
    }

    return ss.str();
}

std::string DiskIOProfiler::ExportFileStatsCSV() const {
    DiskIOReport report = GenerateReport();

    std::ostringstream ss;
    ss << "file_path,"
        << "total_read_bytes,total_write_bytes,"
        << "read_count,write_count,"
        << "avg_read_latency_ms,avg_write_latency_ms,"
        << "max_read_latency_ms,max_write_latency_ms,"
        << "read_throughput_mbps,write_throughput_mbps,"
        << "stutter_events\n";

    for (const auto& f : report.stats.topFiles) {
        ss << "\"" << f.filePath << "\","
           << f.totalReadBytes << ","
           << f.totalWriteBytes << ","
           << f.readCount << ","
           << f.writeCount << ","
           << std::fixed << std::setprecision(3) << f.avgReadLatencyMs << ","
           << f.avgWriteLatencyMs << ","
           << f.maxReadLatencyMs << ","
           << f.maxWriteLatencyMs << ","
           << std::setprecision(2) << f.readThroughputMBps << ","
           << f.writeThroughputMBps << ","
           << f.stutterEvents << "\n";
    }

    return ss.str();
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void DiskIOProfiler::Reset() {
    m_samples.clear();
    m_alerts.clear();
    m_fileStatsCache.clear();
    m_cachedReport = DiskIOReport();

    m_totalReadBytes = 0;
    m_totalWriteBytes = 0;
    m_readCount = 0;
    m_writeCount = 0;
    m_totalOps = 0;
    m_readLatencySum = 0.0;
    m_writeLatencySum = 0.0;
    m_maxLatencyRead = 0.0;
    m_maxLatencyWrite = 0.0;
    m_maxQueueDepth = 0;
    m_queueDepthSum = 0.0;
    m_queueDepthCount = 0;
    m_stutterEvents = 0;

    m_dirty = true;
    m_fileStatsDirty = true;
}

void DiskIOProfiler::SetEnabled(bool enabled) {
    m_enabled = enabled;
}

void DiskIOProfiler::TrimWindow() {
    size_t excess = m_samples.size() - std::min(m_samples.size(), m_config.windowSize);
    if (excess == 0) return;

    // Subtract old samples from running totals
    for (size_t i = 0; i < excess; i++) {
        const auto& old = m_samples[i];
        m_totalOps--;

        if (old.opType == DiskIOOpType::Read || old.opType == DiskIOOpType::Write) {
            if (old.durationMs > m_config.stutterThresholdMs) {
                m_stutterEvents--;
            }

            if (old.opType == DiskIOOpType::Read) {
                m_totalReadBytes -= old.sizeBytes;
                m_readCount--;
                m_readLatencySum -= old.durationMs;
                // Note: max latency might still reference an evicted entry
                // This is acceptable for approximate statistics
            } else {
                m_totalWriteBytes -= old.sizeBytes;
                m_writeCount--;
                m_writeLatencySum -= old.durationMs;
            }
        }

        m_queueDepthSum -= old.queueDepth;
        m_queueDepthCount--;
    }

    m_samples.erase(m_samples.begin(), m_samples.begin() + static_cast<long>(excess));
}

} // namespace ProfilerCore
