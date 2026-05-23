#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

namespace ProfilerCore {

// ── Thread Role Classification ───────────────────────────────────────────────

enum class ThreadRole : uint8_t {
    Unknown     = 0,
    MainThread,        // Game logic / main loop
    RenderThread,      // Render thread
    WorkerThread,      // Generic worker / job system
    IOThread,          // File/network I/O
    AudioThread,       // Audio processing
    PhysicsThread,     // Physics simulation
    ScriptThread,      // Script/VM execution
    LoaderThread,      // Asset loading
    NetworkThread,     // Network processing
    MaxRole
};

const char* ThreadRoleToString(ThreadRole role);

// ── Contention Types ────────────────────────────────────────────────────────

enum class ContentionType : uint8_t {
    None            = 0,
    LockContention,     // Lock/ mutex contention
    CacheLineBounce,   // False sharing / cache line bouncing
    PriorityInversion,  // Low priority thread blocking high
    CoreMigration,      // Thread moving between CPU cores
    QueueStarvation,    // Worker queue empty / no tasks
    OverSubscription,   // More threads than CPU cores
    MaxContentionType
};

const char* ContentionTypeToString(ContentionType type);

// ── Thread Performance Snapshot ────────────────────────────────────────────

struct ThreadSnapshot {
    int64_t timestampUs;        // Microsecond timestamp
    std::thread::id threadId;
    uint32_t osThreadId;        // OS-level TID
    ThreadRole role;
    
    // CPU metrics
    double cpuUsagePercent;      // 0-100
    uint64_t userTimeUs;         // Time in user mode (μs)
    uint64_t kernelTimeUs;       // Time in kernel mode (μs)
    uint64_t totalCpuTimeUs;    // userTime + kernelTime
    
    // Context switching
    uint64_t contextSwitches;    // Total context switches
    uint64_t voluntarySwitches;   // Voluntary (yield/sleep)
    uint64_t involuntarySwitches; // Involuntary (preempted)
    
    // Scheduling
    int32_t currentCore;         // Current CPU core (-1 = unknown)
    int32_t idealCore;          // Ideal core (affinity)
    uint32_t contextSwitchRate;  // Switches per second
    
    // Wait / block
    uint64_t waitTimeUs;        // Time waiting for locks/IO
    uint32_t waitReason;         // Wait reason code
    
    // Memory
    size_t stackUsageBytes;
    size_t stackCommitBytes;
    
    // Performance indicators
    bool isBlocked;
    bool isRunning;
    bool isYielding;
    double readyQueueLatencyMs;  // Time spent ready to run
};

// ── Contention Event ─────────────────────────────────────────────────────────

struct ContentionEvent {
    int64_t timestampUs;
    std::thread::id threadId;
    uint32_t osThreadId;
    ContentionType type;
    
    // Duration
    uint64_t durationUs;        // How long the contention lasted
    uint64_t waitStartTimeUs;    // When the thread started waiting
    
    // Impact
    double severity;             // 0-100
    uint32_t blockedThreadCount;  // How many threads were blocked
    uint64_t criticalSectionAddr; // Address of the contended lock (if available)
    
    // Classification
    std::string lockName;       // Name of the lock (if instrumented)
    std::string description;
    std::vector<std::string> stackTrace;  // Call stack (if available)
    
    // Recommendations
    std::string suggestion;      // What to do about it
};

// ── Thread Pool Statistics ───────────────────────────────────────────────────

struct ThreadPoolStats {
    std::string poolName;
    uint32_t activeThreads;
    uint32_t idleThreads;
    uint32_t pendingTasks;
    uint32_t completedTasks;
    
    // Throughput
    double tasksPerSecond;
    double avgTaskDurationUs;
    double avgQueueLatencyUs;
    
    // Efficiency
    double cpuEfficiency;       // CPU time / wall time
    double threadUtilization;    // Active time / total time
    uint32_t starvationEvents;   // Times pool had no available threads
    
    // Scaling
    uint32_t optimalThreadCount; // Suggested thread count
    bool isOverSubscribed;
};

// ── Core Migration Event ───────────────────────────────────────────────────

struct CoreMigrationEvent {
    int64_t timestampUs;
    std::thread::id threadId;
    int32_t fromCore;
    int32_t toCore;
    uint64_t durationOnPrevCoreUs;  // How long thread was on previous core
    double cacheMissPenaltyEstimateMs; // Estimated penalty from cache misses
    std::string description;
};

// ── Threading Analysis Result ──────────────────────────────────────────────

struct ThreadingAnalysis {
    int64_t timestampUs;
    int sampleCount;
    uint32_t totalThreadsTracked;
    
    // Overall metrics
    double overallCpuUsage;         // 0-100 across all tracked threads
    double avgContextSwitchRate;     // Per second across all threads
    uint64_t totalContentionTimeUs; // Total time spent in contention
    uint32_t activeContentionEvents; // Currently active
    
    // Contention summary
    uint32_t lockContentionCount;
    uint32_t cacheBounceCount;
    uint32_t priorityInversionCount;
    uint32_t coreMigrationCount;
    uint32_t starvationCount;
    uint32_t overSubscriptionCount;
    
    // Per-thread summary
    struct ThreadSummary {
        std::thread::id threadId;
        ThreadRole role;
        double avgCpuUsage;
        uint64_t totalCpuTimeUs;
        uint32_t contextSwitches;
        uint32_t contentionEvents;
        int32_t coreAffinity;        // Most commonly used core
        bool isBottleneck;          // Is this thread a bottleneck?
        std::string bottleneckReason;
    };
    std::vector<ThreadSummary> threadSummaries;
    
    // Thread pool analysis
    std::vector<ThreadPoolStats> poolStats;
    
    // Hotspots (threads with most contention)
    struct ContentionHotspot {
        std::thread::id threadId;
        ContentionType primaryType;
        uint32_t eventCount;
        uint64_t totalWaitTimeUs;
        double avgSeverity;
    };
    std::vector<ContentionHotspot> hotspots;
    
    // Core migration analysis
    uint32_t totalMigrations;
    double avgMigrationsPerSecond;
    std::vector<CoreMigrationEvent> recentMigrations;
    
    // Optimization suggestions
    std::vector<std::string> recommendations;
    bool needsOptimization() const {
        return !recommendations.empty() || activeContentionEvents > 0;
    }
};

// ── Threading Analyzer Configuration ────────────────────────────────────────

struct ThreadingConfig {
    // Sampling
    uint32_t sampleIntervalMs = 100;      // How often to sample thread states
    size_t maxThreadHistory = 1000;        // Max snapshots to keep per thread
    size_t analysisWindowSize = 300;       // Frames / samples for analysis
    
    // Thresholds
    double contentionThresholdUs = 1000.0;   // 1ms - what counts as contention
    double severeContentionThresholdUs = 5000.0; // 5ms - severe contention
    uint32_t contextSwitchRateWarning = 5000;    // Switches/sec to warn
    uint32_t coreMigrationRateWarning = 10;      // Migrations/sec to warn
    
    // Detection
    bool detectLockContention = true;
    bool detectCacheBouncing = true;
    bool detectPriorityInversion = true;
    bool detectCoreMigration = true;
    bool detectStarvation = true;
    bool detectOverSubscription = true;
    
    // Thread roles (user-provided hints)
    std::unordered_map<std::thread::id, ThreadRole> threadRoleHints;
    
    // Reporting
    bool enableRealTimeAlerts = true;
    double alertCooldownMs = 5000.0;     // Min time between similar alerts
    
    // Advanced
    bool trackCallStacks = false;          // Expensive - enable only when needed
    bool useHardwareCounters = false;      // Use PMCs if available
    uint32_t hardwareCounterSampleRate = 100; // Hz for PMC sampling
};

// ── ThreadingAnalyzer ───────────────────────────────────────────────────────
// Analyzes thread performance, contention, and scheduling efficiency
//
// Features:
// - Track per-thread CPU usage, context switches, and core affinity
// - Detect lock contention and long wait times
// - Identify cache line bouncing (false sharing)
// - Detect priority inversion scenarios
// - Track core migration and cache penalty
// - Monitor thread pool starvation
// - Detect over-subscription (too many threads)
// - Generate optimization recommendations
// - Real-time contention event detection

class ThreadingAnalyzer {
public:
    explicit ThreadingAnalyzer();
    ~ThreadingAnalyzer();

    // Configuration
    void SetConfig(const ThreadingConfig& config);
    ThreadingConfig GetConfig() const { return m_config; }
    void Reset();

    // Thread registration / role assignment
    void RegisterThread(std::thread::id tid, ThreadRole role, const std::string& name = "");
    void UnregisterThread(std::thread::id tid);
    void SetThreadRole(std::thread::id tid, ThreadRole role);
    void SetThreadName(std::thread::id tid, const std::string& name);
    ThreadRole GetThreadRole(std::thread::id tid) const;
    std::string GetThreadName(std::thread::id tid) const;

    // Thread pool registration
    void RegisterThreadPool(const std::string& poolName, uint32_t expectedThreadCount);
    void UpdateThreadPoolStats(const std::string& poolName, const ThreadPoolStats& stats);

    // Sampling - call these from the profiler's sampling loop
    void SampleThread(std::thread::id tid, const ThreadSnapshot& snapshot);
    void SampleAllThreads();  // Auto-sample all registered threads

    // Contention recording
    void RecordContentionStart(std::thread::id tid, ContentionType type,
                              const std::string& lockName = "");
    void RecordContentionEnd(std::thread::id tid, uint64_t durationUs);
    void RecordCoreMigration(std::thread::id tid, int32_t fromCore, int32_t toCore);

    // Analysis
    ThreadingAnalysis Analyze() const;
    ThreadingAnalysis AnalyzeIncremental();  // Fast incremental analysis

    // Accessors
    const std::vector<ThreadSnapshot>& GetThreadHistory(std::thread::id tid) const;
    const std::vector<ContentionEvent>& GetContentionEvents() const { return m_contentionEvents; }
    const std::vector<CoreMigrationEvent>& GetMigrationEvents() const { return m_migrationEvents; }
    
    // Real-time state
    bool HasActiveContention() const { return m_activeContentionCount > 0; }
    uint32_t GetActiveContentionCount() const { return m_activeContentionCount; }
    std::vector<ContentionEvent> GetActiveContentionEvents() const;
    
    // Thread pool
    const std::unordered_map<std::string, ThreadPoolStats>& GetThreadPoolStats() const {
        return m_poolStats;
    }
    
    // Export
    std::string ExportToJSON() const;
    std::string ExportThreadHistoryToJSON(std::thread::id tid) const;
    std::string ExportContentionEventsToJSON() const;

private:
    // Analysis helpers
    void AnalyzeContention(ThreadingAnalysis& result) const;
    void AnalyzeContextSwitching(ThreadingAnalysis& result) const;
    void AnalyzeCoreMigration(ThreadingAnalysis& result) const;
    void AnalyzeThreadPool(ThreadingAnalysis& result) const;
    void AnalyzeOverSubscription(ThreadingAnalysis& result) const;
    void GenerateRecommendations(ThreadingAnalysis& result) const;
    
    // Thread snapshot helpers
    ThreadSnapshot CaptureThreadSnapshot(std::thread::id tid) const;
    double CalculateContextSwitchRate(std::thread::id tid) const;
    uint64_t EstimateCacheMissPenalty(const CoreMigrationEvent& event) const;
    
    // Contention helpers
    ContentionType ClassifyContention(const ContentionEvent& event) const;
    double CalculateContentionSeverity(const ContentionEvent& event) const;
    std::string GenerateContentionSuggestion(const ContentionEvent& event) const;
    
    // Platform-specific helpers
    uint32_t GetOSThreadId(std::thread::id tid) const;
    int32_t GetCurrentCore(std::thread::id tid) const;
    uint64_t GetThreadCpuTime(std::thread::id tid) const;
    uint64_t GetThreadContextSwitches(std::thread::id tid) const;

private:
    ThreadingConfig m_config;
    
    // Thread tracking
    struct ThreadInfo {
        std::thread::id tid;
        uint32_t osThreadId;
        ThreadRole role;
        std::string name;
        std::deque<ThreadSnapshot> history;
        uint64_t lastSampleTimeUs;
        uint64_t lastCpuTimeUs;
        uint64_t lastContextSwitches;
        int32_t lastCore;
        bool isActive;
    };
    std::unordered_map<std::thread::id, ThreadInfo> m_threads;
    mutable std::mutex m_threadsMutex;
    
    // Contention tracking
    std::vector<ContentionEvent> m_contentionEvents;
    std::unordered_map<std::thread::id, ContentionEvent> m_activeContention;
    std::atomic<uint32_t> m_activeContentionCount{0};
    
    // Core migration tracking
    std::vector<CoreMigrationEvent> m_migrationEvents;
    
    // Thread pool tracking
    std::unordered_map<std::string, ThreadPoolStats> m_poolStats;
    
    // Analysis cache (to support incremental analysis)
    mutable ThreadingAnalysis m_cachedAnalysis;
    mutable int64_t m_lastAnalysisTimeUs;
    mutable bool m_analysisValid;
    
    // Statistics
    std::atomic<uint64_t> m_totalSamples{0};
    std::atomic<uint32_t> m_totalContentionEvents{0};
    std::atomic<uint32_t> m_totalMigrations{0};
};

} // namespace ProfilerCore
