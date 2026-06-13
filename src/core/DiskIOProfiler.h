#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <chrono>
#include <cstdint>
#include <functional>

namespace ProfilerCore {

// ─── Enumerations ─────────────────────────────────────────────────────────────

/**
 * Disk I/O operation type
 */
enum class DiskIOOpType {
    Read = 0,
    Write = 1,
    Flush = 2,
    Create = 3,
    Delete = 4,
    Seek = 5,
    Unknown = 6
};

/**
 * Disk health / congestion level
 */
enum class DiskHealth {
    Healthy = 0,        // Normal operation
    Moderate = 1,       // Elevated latency, occasional queuing
    Congested = 2,      // Significant queuing, frame time impact likely
    Critical = 3,       // Disk is severely bottlenecked, game stuttering
    Failing = 4         // Potential hardware failure symptoms
};

/**
 * Alert types specific to disk I/O
 */
enum class DiskAlertType {
    HighLatency,        // Single operation took too long
    QueueDepthExceeded, // I/O queue backing up
    BandwidthSaturation,// Throughput maxed out
    StutterPattern,     // Recurring disk-caused frame stutter
    WriteBurst,         // Unusually large write burst
    ReadBurst,          // Unusually large read burst
    IOPSpike,           // Sudden operations-per-second spike
    SustainedLoad       // Long-duration high I/O load
};

// ─── Data Structures ──────────────────────────────────────────────────────────

/**
 * A single disk I/O sample
 */
struct DiskIOSample {
    int64_t     timestamp;       // microseconds since epoch
    DiskIOOpType opType;          // read/write/flush/etc
    uint64_t    offset;           // file offset in bytes
    uint64_t    sizeBytes;        // transfer size in bytes
    double      durationMs;       // operation duration in ms
    std::string filePath;         // path of the target file (truncated)
    uint32_t    queueDepth;       // I/O queue depth at time of operation
    bool        isAsync;          // was this an async operation?
    bool        isPageFault;      // caused by page fault / swap?
};

/**
 * Per-file I/O statistics
 */
struct FileIOStats {
    std::string filePath;
    uint64_t    totalReadBytes;
    uint64_t    totalWriteBytes;
    uint64_t    readCount;
    uint64_t    writeCount;
    double      totalReadDurationMs;
    double      totalWriteDurationMs;
    double      avgReadLatencyMs;
    double      avgWriteLatencyMs;
    double      maxReadLatencyMs;
    double      maxWriteLatencyMs;
    double      readThroughputMBps;   // average read MB/s
    double      writeThroughputMBps;  // average write MB/s
    int         stutterEvents;        // times this file caused frame drops
};

/**
 * Aggregate disk I/O statistics for a time window
 */
struct DiskIOStats {
    int64_t     startTime;
    int64_t     endTime;
    uint64_t    totalReadBytes;
    uint64_t    totalWriteBytes;
    uint64_t    totalOpCount;
    uint64_t    readOpCount;
    uint64_t    writeOpCount;
    
    double      avgReadLatencyMs;
    double      avgWriteLatencyMs;
    double      maxReadLatencyMs;
    double      maxWriteLatencyMs;
    double      avgReadThroughputMBps;
    double      avgWriteThroughputMBps;
    double      peakReadThroughputMBps;
    double      peakWriteThroughputMBps;
    
    double      avgQueueDepth;
    double      maxQueueDepth;
    uint32_t    queueStallCount;      // times queue depth > threshold
    double      avgIOPS;              // average I/O operations per second
    double      peakIOPS;
    
    DiskHealth  health;
    double      healthScore;          // 0-100, higher = healthier
    int         slowOpCount;          // operations exceeding threshold
    int         totalStutterEvents;   // disk-caused stutters detected
    
    std::vector<FileIOStats> topFiles; // top files by I/O volume
};

/**
 * Disk I/O alert
 */
struct DiskAlert {
    DiskAlertType type;
    int64_t     timestamp;
    double      value;
    double      threshold;
    std::string message;
    std::string filePath;      // file involved (if applicable)
    bool        acknowledged;
};

/**
 * Disk I/O configuration
 */
struct DiskIOConfig {
    size_t      windowSize = 300;          // sample window
    double      slowOpThresholdMs = 50.0;  // > 50ms = slow operation
    double      criticalLatencyMs = 100.0; // > 100ms = critical latency
    double      stutterThresholdMs = 16.67;// > 1 frame at 60fps = stutter
    uint32_t    maxQueueDepthWarning = 8;  // queue depth warning threshold
    uint32_t    maxQueueDepthCritical = 32;// queue depth critical threshold
    double      throughputWarningMBps = 100.0; // sequential throughput warning
    uint32_t    trackedFiles = 10;         // top files to track individually
    bool        alertsEnabled = true;
    bool        trackFilePaths = true;     // expensive, disable if not needed
};

/**
 * Complete disk I/O report
 */
struct DiskIOReport {
    int64_t     timestamp;
    int64_t     sessionDurationMs;
    DiskIOStats stats;
    int         alertCount;
    int         unacknowledgedAlerts;
    std::vector<DiskAlert> alerts;
    std::vector<std::string> recommendations;
};

// ─── Callbacks ─────────────────────────────────────────────────────────────────

using DiskAlertCallback = std::function<void(const DiskAlert&)>;

// ─── Main Class ───────────────────────────────────────────────────────────────

/**
 * DiskIOProfiler — monitors disk I/O performance for games.
 *
 * Disk I/O bottlenecks are a common cause of frame stuttering in modern games,
 * especially open-world titles with heavy asset streaming. This module tracks
 * read/write operations, identifies problematic files, and generates actionable
 * optimization recommendations.
 *
 * Features:
 * - Per-operation latency tracking (read/write/flush)
 * - Per-file I/O statistics to identify hot files
 * - I/O queue depth monitoring for congestion detection
 * - Throughput analysis (MB/s, IOPS)
 * - Stutter event detection (disk-caused frame drops)
 * - Disk health scoring (0-100)
 * - Automatic alert generation
 * - JSON/CSV export
 * - Simulated test data for development
 *
 * Usage:
 *   auto& disk = ProfilerCore::GetInstance().GetDiskIOProfiler();
 *   // After each disk operation:
 *   disk.RecordOperation(DiskIOOpType::Read, offset, sizeBytes,
 *                         durationMs, filePath, queueDepth);
 *   // Generate report:
 *   DiskIOReport report = disk.GenerateReport();
 */
class DiskIOProfiler {
public:
    DiskIOProfiler();
    ~DiskIOProfiler();

    // ─── Configuration ───────────────────────────────────────────────────────

    void Configure(const DiskIOConfig& config);
    DiskIOConfig GetConfig() const;

    // ─── Data Recording ─────────────────────────────────────────────────────

    /**
     * Record a single disk I/O operation.
     *
     * @param opType      Type of I/O operation (read/write/flush etc.)
     * @param offset      Byte offset in the file
     * @param sizeBytes   Transfer size in bytes
     * @param durationMs  Time the operation took in milliseconds
     * @param filePath    Path of the file being accessed
     * @param queueDepth  I/O queue depth at operation time
     * @param isAsync     Whether this was an asynchronous operation
     * @param isPageFault Whether this was caused by a page fault
     */
    void RecordOperation(
        DiskIOOpType opType,
        uint64_t     offset,
        uint64_t     sizeBytes,
        double       durationMs,
        const std::string& filePath,
        uint32_t     queueDepth = 0,
        bool         isAsync = false,
        bool         isPageFault = false
    );

    /**
     * Simulate realistic disk I/O for testing/demo.
     * Generates plausible read/write patterns with occasional spikes.
     */
    void RecordSimulatedSample();

    // ─── Analysis ───────────────────────────────────────────────────────────

    /** Generate full analysis report. */
    DiskIOReport GenerateReport() const;

    /** Quick accessors */
    double   GetCurrentReadLatencyMs() const;
    double   GetCurrentWriteLatencyMs() const;
    double   GetAverageReadThroughputMBps() const;
    double   GetAverageWriteThroughputMBps() const;
    double   GetCurrentQueueDepth() const;
    double   GetHealthScore() const;
    DiskHealth GetDiskHealth() const;
    int      GetStutterEventCount() const;
    int      GetAlertCount() const;

    /** Get active alerts that need attention. */
    std::vector<DiskAlert> GetActiveAlerts() const;

    /** Acknowledge a specific alert by timestamp. */
    bool AcknowledgeAlert(int64_t timestamp);

    /** Acknowledge all current alerts. */
    void AcknowledgeAllAlerts();

    // ─── Callbacks ──────────────────────────────────────────────────────────

    void SetAlertCallback(DiskAlertCallback callback);

    // ─── Export ─────────────────────────────────────────────────────────────

    /** Export full report as JSON string. */
    std::string ExportToJSON() const;

    /** Export I/O timeline as CSV string. */
    std::string ExportIOTimelineCSV() const;

    /** Export per-file stats as CSV string. */
    std::string ExportFileStatsCSV() const;

    // ─── Lifecycle ──────────────────────────────────────────────────────────

    void Reset();
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

private:
    // Internal helpers
    void ComputeStats() const;
    void ProcessPerFileStats() const;
    void DetectAlerts() const;
    void GenerateRecommendations() const;
    void TrimWindow();

    DiskAlert MakeAlert(DiskAlertType type, double value, double threshold,
                        const std::string& message,
                        const std::string& filePath = "") const;

    static DiskHealth ComputeHealthFromScore(double score);
    static std::string TruncatePath(const std::string& path, size_t maxLen = 80);

    // ─── State ──────────────────────────────────────────────────────────────

    DiskIOConfig               m_config;
    bool                       m_enabled = true;
    DiskAlertCallback          m_alertCallback;

    struct SampleEntry {
        int64_t     timestamp;
        DiskIOOpType opType;
        uint64_t    offset;
        uint64_t    sizeBytes;
        double      durationMs;
        std::string filePath;
        uint32_t    queueDepth;
        bool        isAsync;
        bool        isPageFault;
    };

    std::deque<SampleEntry>    m_samples;
    mutable DiskIOReport       m_cachedReport;
    mutable bool               m_dirty = true;

    // Running totals for efficient re-computation
    uint64_t  m_totalReadBytes = 0;
    uint64_t  m_totalWriteBytes = 0;
    uint64_t  m_readCount = 0;
    uint64_t  m_writeCount = 0;
    uint64_t  m_totalOps = 0;
    double    m_readLatencySum = 0.0;
    double    m_writeLatencySum = 0.0;
    double    m_maxLatencyRead = 0.0;
    double    m_maxLatencyWrite = 0.0;
    uint32_t  m_maxQueueDepth = 0;
    double    m_queueDepthSum = 0.0;
    uint32_t  m_queueDepthCount = 0;
    int       m_stutterEvents = 0;

    // Per-file aggregation (lazy)
    mutable std::unordered_map<std::string, FileIOStats> m_fileStatsCache;
    mutable bool m_fileStatsDirty = true;

    // Alerts
    mutable std::vector<DiskAlert> m_alerts;

    // Simulation state
    int       m_simFrameCount = 0;
    uint64_t  m_simOffset = 0;
};

} // namespace ProfilerCore
