/**
 * ThreadingAnalyzer.cpp
 * Implementation of thread performance, contention, and scheduling analysis
 *
 * Features:
 * - Per-thread CPU usage, context switch, and core affinity tracking
 * - Lock contention and long wait time detection
 * - Cache line bouncing (false sharing) identification
 * - Priority inversion scenario detection
 * - Core migration and cache penalty tracking
 * - Thread pool starvation monitoring
 * - Over-subscription (too many threads) detection
 * - Optimization recommendation generation
 * - Real-time contention event detection
 * - JSON export for dashboard integration
 *
 * Platform: Windows (uses Windows API); stubs provided for other platforms
 */

#include "ThreadingAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <set>
#include <map>
#include <cstring>
#include <limits>

// ─── Windows Platform Support ─────────────────────────────────────────

#if defined(_WIN32) || defined(_WIN64)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <process.h>
    #include <psapi.h>
    #pragma comment(lib, "psapi.lib")
    // Note: For production use, also consider:
    //   - <jthread> C++20 for better thread management
    //   - ETW (Event Tracing for Windows) for deep contention tracing
    //   - Windows Performance Toolkit (WPT) integration
    #define THREADING_ANALYZER_WINDOWS
#elif defined(__linux__) || defined(__APPLE__)
    #include <pthread.h>
    #include <sys/time.h>
    #include <sys/resource.h>
    #include <unistd.h>
    #define THREADING_ANALYZER_POSIX
#endif

// ─── String Conversion Helpers ─────────────────────────────────────────

namespace ProfilerCore {

const char* ThreadRoleToString(ThreadRole role) {
    switch (role) {
        case ThreadRole::Unknown:     return "Unknown";
        case ThreadRole::MainThread:   return "MainThread";
        case ThreadRole::RenderThread: return "RenderThread";
        case ThreadRole::WorkerThread: return "WorkerThread";
        case ThreadRole::IOThread:     return "IOThread";
        case ThreadRole::AudioThread:  return "AudioThread";
        case ThreadRole::PhysicsThread:return "PhysicsThread";
        case ThreadRole::ScriptThread: return "ScriptThread";
        case ThreadRole::LoaderThread: return "LoaderThread";
        case ThreadRole::NetworkThread:return "NetworkThread";
        case ThreadRole::MaxRole:      return "MaxRole";
        default: return "Invalid";
    }
}

const char* ContentionTypeToString(ContentionType type) {
    switch (type) {
        case ContentionType::None:            return "None";
        case ContentionType::LockContention:   return "LockContention";
        case ContentionType::CacheLineBounce: return "CacheLineBounce";
        case ContentionType::PriorityInversion:return "PriorityInversion";
        case ContentionType::CoreMigration:    return "CoreMigration";
        case ContentionType::QueueStarvation:  return "QueueStarvation";
        case ContentionType::OverSubscription:  return "OverSubscription";
        case ContentionType::MaxContentionType:return "MaxContentionType";
        default: return "Invalid";
    }
}

// ─── Constructor / Destructor ─────────────────────────────────────────

ThreadingAnalyzer::ThreadingAnalyzer()
    : m_activeContentionCount(0)
    , m_lastAnalysisTimeUs(0)
    , m_analysisValid(false)
    , m_totalSamples(0)
    , m_totalContentionEvents(0)
    , m_totalMigrations(0)
{
    m_contentionEvents.reserve(1024);
    m_migrationEvents.reserve(256);
}

ThreadingAnalyzer::~ThreadingAnalyzer() = default;

// ─── Configuration ─────────────────────────────────────────────────────

void ThreadingAnalyzer::SetConfig(const ThreadingConfig& config) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    m_config = config;

    // Trim history buffers if analysis window shrank
    if (m_config.maxThreadHistory < 1000) {
        for (auto& pair : m_threads) {
            if (pair.second.history.size() > m_config.maxThreadHistory) {
                auto start = pair.second.history.begin();
                pair.second.history.erase(
                    start,
                    start + (pair.second.history.size() - m_config.maxThreadHistory)
                );
            }
        }
    }
}

void ThreadingAnalyzer::Reset() {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    m_threads.clear();
    m_contentionEvents.clear();
    m_migrationEvents.clear();
    m_activeContention.clear();
    m_poolStats.clear();
    m_activeContentionCount.store(0);
    m_analysisValid = false;
    m_totalSamples.store(0);
    m_totalContentionEvents.store(0);
    m_totalMigrations.store(0);
}

// ─── Thread Registration ──────────────────────────────────────────────

void ThreadingAnalyzer::RegisterThread(std::thread::id tid, ThreadRole role,
                                     const std::string& name) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto& info = m_threads[tid];
    info.tid = tid;
    info.osThreadId = GetOSThreadId(tid);
    info.role = role;
    info.name = name.empty() ? ("Thread_" + std::to_string(info.osThreadId)) : name;
    info.history.reserve(m_config.maxThreadHistory);
    info.lastSampleTimeUs = 0;
    info.lastCpuTimeUs = 0;
    info.lastContextSwitches = 0;
    info.lastCore = GetCurrentCore(tid);
    info.isActive = true;
}

void ThreadingAnalyzer::UnregisterThread(std::thread::id tid) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto it = m_threads.find(tid);
    if (it != m_threads.end()) {
        it->second.isActive = false;
        // Keep history for analysis; can be cleaned up later
    }
}

void ThreadingAnalyzer::SetThreadRole(std::thread::id tid, ThreadRole role) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto it = m_threads.find(tid);
    if (it != m_threads.end()) {
        it->second.role = role;
    }
}

void ThreadingAnalyzer::SetThreadName(std::thread::id tid, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto it = m_threads.find(tid);
    if (it != m_threads.end()) {
        it->second.name = name;
    }
}

ThreadRole ThreadingAnalyzer::GetThreadRole(std::thread::id tid) const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto it = m_threads.find(tid);
    return it != m_threads.end() ? it->second.role : ThreadRole::Unknown;
}

std::string ThreadingAnalyzer::GetThreadName(std::thread::id tid) const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto it = m_threads.find(tid);
    return it != m_threads.end() ? it->second.name : ("Unknown_" + std::to_string(GetOSThreadId(tid)));
}

// ─── Thread Pool Registration ─────────────────────────────────────────

void ThreadingAnalyzer::RegisterThreadPool(const std::string& poolName,
                                          uint32_t expectedThreadCount) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto& stats = m_poolStats[poolName];
    stats.poolName = poolName;
    stats.activeThreads = 0;
    stats.idleThreads = expectedThreadCount;
    stats.pendingTasks = 0;
    stats.completedTasks = 0;
    stats.tasksPerSecond = 0.0;
    stats.avgTaskDurationUs = 0.0;
    stats.avgQueueLatencyUs = 0.0;
    stats.cpuEfficiency = 0.0;
    stats.threadUtilization = 0.0;
    stats.starvationEvents = 0;
    stats.optimalThreadCount = expectedThreadCount;
    stats.isOverSubscribed = false;
}

void ThreadingAnalyzer::UpdateThreadPoolStats(const std::string& poolName,
                                             const ThreadPoolStats& stats) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    m_poolStats[poolName] = stats;
}

// ─── Sampling ────────────────────────────────────────────────────────

void ThreadingAnalyzer::SampleThread(std::thread::id tid, const ThreadSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto it = m_threads.find(tid);
    if (it == m_threads.end()) {
        // Auto-register unknown threads
        RegisterThread(tid, ThreadRole::Unknown);
        it = m_threads.find(tid);
    }

    if (it != m_threads.end()) {
        // Update OS thread ID if not set
        if (it->second.osThreadId == 0) {
            it->second.osThreadId = GetOSThreadId(tid);
        }

        // Calculate derived metrics
        ThreadSnapshot s = snapshot;
        s.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        s.threadId = tid;
        s.osThreadId = it->second.osThreadId;
        s.role = it->second.role;

        // Calculate context switch rate
        if (it->second.lastSampleTimeUs > 0 && it->second.lastContextSwitches > 0) {
            uint64_t timeDeltaUs = s.timestampUs - it->second.lastSampleTimeUs;
            if (timeDeltaUs > 0) {
                uint64_t switchDelta = s.contextSwitches - it->second.lastContextSwitches;
                s.contextSwitchRate = static_cast<uint32_t>(
                    switchDelta * 1'000'000 / timeDeltaUs
                );
            }
        }

        // Detect core migration
        if (it->second.lastCore >= 0 && s.currentCore >= 0 && it->second.lastCore != s.currentCore) {
            RecordCoreMigration(tid, it->second.lastCore, s.currentCore);
        }

        // Detect contention (high context switch rate + blocked state)
        if (s.isBlocked && it->second.lastContextSwitches > 0) {
            uint64_t switchDelta = s.contextSwitches - it->second.lastContextSwitches;
            if (switchDelta > m_config.contentionThresholdUs) {
                // This is a simplified contention detection; real implementation
                // would use lock instrumentation or ETW
                ContentionEvent evt;
                evt.timestampUs = s.timestampUs;
                evt.threadId = tid;
                evt.osThreadId = s.osThreadId;
                evt.type = ContentionType::LockContention;
                evt.durationUs = 0;  // Ongoing
                evt.waitStartTimeUs = s.timestampUs;
                evt.severity = std::min(100.0, static_cast<double>(switchDelta) / 1000.0);
                evt.blockedThreadCount = 1;
                evt.description = "Thread blocked with high context switch rate";
                // Will be updated when unblocked
            }
        }

        it->second.history.push_back(s);
        it->second.lastSampleTimeUs = s.timestampUs;
        it->second.lastCpuTimeUs = s.totalCpuTimeUs;
        it->second.lastContextSwitches = s.contextSwitches;
        it->second.lastCore = s.currentCore;

        // Trim history
        if (it->second.history.size() > m_config.maxThreadHistory) {
            it->second.history.erase(
                it->second.history.begin(),
                it->second.history.begin() + it->second.history.size() - m_config.analysisWindowSize
            );
        }

        m_totalSamples++;
    }

    m_analysisValid = false;
}

void ThreadingAnalyzer::SampleAllThreads() {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    for (auto& pair : m_threads) {
        if (!pair.second.isActive) continue;

        // Skip if sampled too recently
        if (pair.second.lastSampleTimeUs > 0 &&
            (nowUs - pair.second.lastSampleTimeUs) < (m_config.sampleIntervalMs * 1000 / 2)) {
            continue;
        }

        ThreadSnapshot snapshot = CaptureThreadSnapshot(pair.first);
        SampleThread(pair.first, snapshot);
    }
}

// ─── Contention Recording ────────────────────────────────────────────

void ThreadingAnalyzer::RecordContentionStart(std::thread::id tid,
                                              ContentionType type,
                                              const std::string& lockName) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);

    ContentionEvent evt;
    evt.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    evt.threadId = tid;
    evt.osThreadId = GetOSThreadId(tid);
    evt.type = type;
    evt.durationUs = 0;
    evt.waitStartTimeUs = evt.timestampUs;
    evt.severity = 0.0;
    evt.blockedThreadCount = 1;
    evt.lockName = lockName;
    evt.description = "Contention started: " + std::string(ContentionTypeToString(type));

    m_activeContention[tid] = evt;
    m_activeContentionCount++;
    m_totalContentionEvents++;
}

void ThreadingAnalyzer::RecordContentionEnd(std::thread::id tid, uint64_t durationUs) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);

    auto it = m_activeContention.find(tid);
    if (it == m_activeContention.end()) return;

    ContentionEvent& evt = it->second;
    evt.durationUs = durationUs;
    evt.severity = CalculateContentionSeverity(evt);
    evt.suggestion = GenerateContentionSuggestion(evt);

    // Classify the contention type more precisely based on duration
    if (evt.durationUs > m_config.severeContentionThresholdUs) {
        evt.type = ClassifyContention(evt);
    }

    // Generate description
    std::ostringstream oss;
    oss << ContentionTypeToString(evt.type)
        << " on thread " << GetThreadName(tid)
        << " lasted " << (evt.durationUs / 1000.0) << "ms";
    if (!evt.lockName.empty()) {
        oss << " [lock: " << evt.lockName << "]";
    }
    evt.description = oss.str();

    m_contentionEvents.push_back(evt);

    // Trim events list
    if (m_contentionEvents.size() > 5000) {
        m_contentionEvents.erase(
            m_contentionEvents.begin(),
            m_contentionEvents.begin() + m_contentionEvents.size() - 4000
        );
    }

    m_activeContention.erase(it);
    m_activeContentionCount--;
    m_analysisValid = false;
}

void ThreadingAnalyzer::RecordCoreMigration(std::thread::id tid,
                                           int32_t fromCore,
                                           int32_t toCore) {
    std::lock_guard<std::mutex> lock(m_threadsMutex);

    CoreMigrationEvent evt;
    evt.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    evt.threadId = tid;
    evt.fromCore = fromCore;
    evt.toCore = toCore;
    evt.durationOnPrevCoreUs = 0;  // Would need previous timestamp tracking
    evt.cacheMissPenaltyEstimateMs = EstimateCacheMissPenalty(evt);

    std::ostringstream oss;
    oss << "Thread " << GetThreadName(tid)
        << " migrated: core " << fromCore << " -> " << toCore
        << " (est. penalty: " << std::fixed << std::setprecision(2)
        << evt.cacheMissPenaltyEstimateMs << "ms)";
    evt.description = oss.str();

    m_migrationEvents.push_back(evt);

    // Trim
    if (m_migrationEvents.size() > 1000) {
        m_migrationEvents.erase(
            m_migrationEvents.begin(),
            m_migrationEvents.begin() + m_migrationEvents.size() - 800
        );
    }

    m_totalMigrations++;
    m_analysisValid = false;
}

// ─── Analysis ────────────────────────────────────────────────────────

ThreadingAnalysis ThreadingAnalyzer::Analyze() const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);

    // Return cached analysis if still valid
    if (m_analysisValid && m_cachedAnalysis.timestampUs > 0) {
        int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if ((nowUs - m_cachedAnalysis.timestampUs) < (m_config.sampleIntervalMs * 1000)) {
            return m_cachedAnalysis;
        }
    }

    ThreadingAnalysis result;
    result.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    result.sampleCount = static_cast<int>(m_totalSamples.load());
    result.totalThreadsTracked = static_cast<uint32_t>(m_threads.size());
    result.activeContentionEvents = m_activeContentionCount.load();
    result.totalContentionTimeUs = 0;
    result.overallCpuUsage = 0.0;
    result.avgContextSwitchRate = 0.0;
    result.lockContentionCount = 0;
    result.cacheBounceCount = 0;
    result.priorityInversionCount = 0;
    result.coreMigrationCount = m_totalMigrations.load();
    result.starvationCount = 0;
    result.overSubscriptionCount = 0;

    // Run sub-analyses
    AnalyzeContention(result);
    AnalyzeContextSwitching(result);
    AnalyzeCoreMigration(result);
    AnalyzeThreadPool(result);
    AnalyzeOverSubscription(result);
    GenerateRecommendations(result);

    // Build thread summaries
    for (const auto& pair : m_threads) {
        if (!pair.second.isActive && pair.second.history.empty()) continue;

        ThreadingAnalysis::ThreadSummary summary;
        summary.threadId = pair.first;
        summary.role = pair.second.role;
        summary.avgCpuUsage = 0.0;
        summary.totalCpuTimeUs = 0;
        summary.contextSwitches = 0;
        summary.contentionEvents = 0;
        summary.coreAffinity = pair.second.lastCore;
        summary.isBottleneck = false;

        if (!pair.second.history.empty()) {
            const auto& h = pair.second.history;
            double cpuSum = 0.0;
            for (const auto& snap : h) {
                cpuSum += snap.cpuUsagePercent;
                summary.totalCpuTimeUs = std::max(summary.totalCpuTimeUs, snap.totalCpuTimeUs);
                summary.contextSwitches = std::max(summary.contextSwitches, snap.contextSwitches);
            }
            summary.avgCpuUsage = cpuSum / h.size();

            // Check if this thread is a bottleneck
            if (summary.avgCpuUsage > 90.0) {
                summary.isBottleneck = true;
                summary.bottleneckReason = "CPU usage consistently above 90%";
            }
            if (pair.second.lastContextSwitches > m_config.contextSwitchRateWarning) {
                summary.isBottleneck = true;
                summary.bottleneckReason = "Excessive context switches (>" +
                    std::to_string(m_config.contextSwitchRateWarning) + "/s)";
            }
        }

        result.threadSummaries.push_back(summary);
    }

    // Cache the result
    m_cachedAnalysis = result;
    m_analysisValid = true;
    m_lastAnalysisTimeUs = result.timestampUs;

    return result;
}

ThreadingAnalysis ThreadingAnalyzer::AnalyzeIncremental() {
    // For now, just call full analysis; can be optimized later
    return Analyze();
}

// ─── Accessors ──────────────────────────────────────────────────────

const std::vector<ThreadSnapshot>& ThreadingAnalyzer::GetThreadHistory(
    std::thread::id tid) const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    auto it = m_threads.find(tid);
    static const std::vector<ThreadSnapshot> empty;
    return it != m_threads.end() ? it->second.history : empty;
}

std::vector<ContentionEvent> ThreadingAnalyzer::GetActiveContentionEvents() const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    std::vector<ContentionEvent> result;
    result.reserve(m_activeContention.size());
    for (const auto& pair : m_activeContention) {
        result.push_back(pair.second);
    }
    return result;
}

// ─── Analysis Helpers ───────────────────────────────────────────────

void ThreadingAnalyzer::AnalyzeContention(ThreadingAnalysis& result) const {
    for (const auto& evt : m_contentionEvents) {
        result.totalContentionTimeUs += evt.durationUs;

        switch (evt.type) {
            case ContentionType::LockContention:
                result.lockContentionCount++;
                break;
            case ContentionType::CacheLineBounce:
                result.cacheBounceCount++;
                break;
            case ContentionType::PriorityInversion:
                result.priorityInversionCount++;
                break;
            case ContentionType::QueueStarvation:
                result.starvationCount++;
                break;
            case ContentionType::OverSubscription:
                result.overSubscriptionCount++;
                break;
            default: break;
        }
    }

    // Identify hotspots
    std::map<std::thread::id, std::vector<const ContentionEvent*>> byThread;
    for (const auto& evt : m_contentionEvents) {
        byThread[evt.threadId].push_back(&evt);
    }

    for (const auto& pair : byThread) {
        ThreadingAnalysis::ContentionHotspot hotspot;
        hotspot.threadId = pair.first;
        hotspot.eventCount = static_cast<uint32_t>(pair.second.size());
        hotspot.totalWaitTimeUs = 0;
        hotspot.avgSeverity = 0.0;

        // Find primary contention type
        std::map<ContentionType, uint32_t> typeCounts;
        for (const auto* evt : pair.second) {
            typeCounts[evt->type]++;
            hotspot.totalWaitTimeUs += evt->durationUs;
            hotspot.avgSeverity += evt->severity;
        }
        hotspot.avgSeverity /= std::max(1.0, static_cast<double>(pair.second.size()));

        ContentionType primary = ContentionType::None;
        uint32_t maxCount = 0;
        for (const auto& tc : typeCounts) {
            if (tc.second > maxCount) {
                maxCount = tc.second;
                primary = tc.first;
            }
        }
        hotspot.primaryType = primary;

        // Only include significant hotspots
        if (hotspot.eventCount >= 3 || hotspot.totalWaitTimeUs > 10000) {
            result.hotspots.push_back(hotspot);
        }
    }
}

void ThreadingAnalyzer::AnalyzeContextSwitching(ThreadingAnalysis& result) const {
    uint64_t totalSwitches = 0;
    uint32_t threadCount = 0;

    for (const auto& pair : m_threads) {
        if (pair.second.history.empty()) continue;
        const auto& last = pair.second.history.back();
        totalSwitches += last.contextSwitches;
        result.avgContextSwitchRate += static_cast<double>(last.contextSwitchRate);
        threadCount++;
    }

    if (threadCount > 0) {
        result.avgContextSwitchRate /= threadCount;
    }
}

void ThreadingAnalyzer::AnalyzeCoreMigration(ThreadingAnalysis& result) const {
    result.totalMigrations = static_cast<uint32_t>(m_migrationEvents.size());

    if (!m_migrationEvents.empty() && m_totalSamples > 0) {
        // Estimate rate (migrations per second)
        int64_t timeSpanUs = m_migrationEvents.back().timestampUs -
                             m_migrationEvents.front().timestampUs;
        if (timeSpanUs > 0) {
            result.avgMigrationsPerSecond =
                static_cast<double>(result.totalMigrations) * 1'000'000.0 / timeSpanUs;
        }
    }

    // Copy recent migrations to result
    size_t startIdx = m_migrationEvents.size() > 20 ? m_migrationEvents.size() - 20 : 0;
    result.recentMigrations.assign(
        m_migrationEvents.begin() + startIdx,
        m_migrationEvents.end()
    );
}

void ThreadingAnalyzer::AnalyzeThreadPool(ThreadingAnalysis& result) const {
    for (const auto& pair : m_poolStats) {
        result.poolStats.push_back(pair.second);
    }
}

void ThreadingAnalyzer::AnalyzeOverSubscription(ThreadingAnalysis& result) const {
    // Estimate optimal thread count based on CPU core count
    uint32_t hwCores = std::thread::hardware_concurrency();
    uint32_t activeThreads = 0;

    for (const auto& pair : m_threads) {
        if (pair.second.isActive) activeThreads++;
    }

    if (activeThreads > hwCores * 2) {
        result.overSubscriptionCount++;
    }

    // Update pool stats for over-subscription
    for (auto& pair : m_poolStats) {
        if (pair.second.activeThreads + pair.second.idleThreads > hwCores * 2) {
            pair.second.isOverSubscribed = true;
        }
        // Calculate optimal thread count
        pair.second.optimalThreadCount = hwCores + (hwCores / 2);  // Heuristic
    }
}

void ThreadingAnalyzer::GenerateRecommendations(ThreadingAnalysis& result) const {
    result.recommendations.clear();

    // High CPU usage threads
    for (const auto& summary : result.threadSummaries) {
        if (summary.avgCpuUsage > 90.0) {
            std::ostringstream oss;
            oss << "Thread '" << GetThreadName(summary.threadId)
                << "' has very high CPU usage (" << std::fixed << std::setprecision(1)
                << summary.avgCpuUsage << "%). "
                << "Consider breaking work into smaller chunks or reducing frequency.";
            result.recommendations.push_back(oss.str());
        }
    }

    // High contention
    if (result.lockContentionCount > 10) {
        std::ostringstream oss;
        oss << "High lock contention detected (" << result.lockContentionCount
            << " events). Consider using lock-free data structures, "
            << "reducing lock scope, or using reader-writer locks.";
        result.recommendations.push_back(oss.str());
    }

    // Cache line bouncing
    if (result.cacheBounceCount > 5) {
        result.recommendations.push_back(
            "Cache line bouncing detected. Check for false sharing: "
            "ensure frequently-accessed variables on different threads "
            "are separated by at least 64 bytes (cache line size)."
        );
    }

    // Core migration
    if (result.avgMigrationsPerSecond > m_config.coreMigrationRateWarning) {
        std::ostringstream oss;
        oss << "Excessive core migration detected ("
            << std::fixed << std::setprecision(1) << result.avgMigrationsPerSecond
            << " migrations/s). Set thread affinity to reduce cache misses.";
        result.recommendations.push_back(oss.str());
    }

    // Thread pool starvation
    if (result.starvationCount > 0) {
        result.recommendations.push_back(
            "Thread pool starvation detected. Increase worker thread count "
            "or reduce task granularity to keep threads busy."
        );
    }

    // Over-subscription
    uint32_t hwCores = std::thread::hardware_concurrency();
    if (result.totalThreadsTracked > hwCores * 2) {
        std::ostringstream oss;
        oss << "Potential over-subscription: " << result.totalThreadsTracked
            << " threads tracked on " << hwCores << " hardware cores. "
            << "Consider reducing thread count or using a thread pool.";
        result.recommendations.push_back(oss.str());
    }

    // High context switch rate
    if (result.avgContextSwitchRate > m_config.contextSwitchRateWarning) {
        std::ostringstream oss;
        oss << "High context switch rate (" << std::fixed << std::setprecision(0)
            << result.avgContextSwitchRate << "/s). "
            << "Consider reducing thread count, increasing task batch size, "
            << "or using cooperative scheduling.";
        result.recommendations.push_back(oss.str());
    }
}

// ─── Thread Snapshot Capture (Platform-Specific) ───────────────────

ThreadSnapshot ThreadingAnalyzer::CaptureThreadSnapshot(std::thread::id tid) const {
    ThreadSnapshot snapshot{};
    snapshot.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    snapshot.threadId = tid;
    snapshot.osThreadId = GetOSThreadId(tid);
    snapshot.role = GetThreadRole(tid);
    snapshot.isBlocked = false;
    snapshot.isRunning = true;
    snapshot.isYielding = false;
    snapshot.readyQueueLatencyMs = 0.0;
    snapshot.currentCore = GetCurrentCore(tid);
    snapshot.idealCore = snapshot.currentCore;
    snapshot.contextSwitchRate = 0;
    snapshot.waitTimeUs = 0;
    snapshot.stackUsageBytes = 0;
    snapshot.stackCommitBytes = 0;

#if defined(THREADING_ANALYZER_WINDOWS)
    // Windows: Use GetThreadTimes for CPU time
    HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, snapshot.osThreadId);
    if (hThread != NULL) {
        FILETIME createTime, exitTime, kernelTime, userTime;
        if (GetThreadTimes(hThread, &createTime, &exitTime, &kernelTime, &userTime)) {
            // Convert FILETIME to microseconds
            auto filetimeToUs = [](const FILETIME& ft) -> uint64_t {
                ULARGE_INTEGER ul;
                ul.LowPart = ft.dwLowDateTime;
                ul.HighPart = ft.dwHighDateTime;
                return ul.QuadPart / 10;  // 100ns -> μs
            };
            snapshot.userTimeUs = filetimeToUs(userTime);
            snapshot.kernelTimeUs = filetimeToUs(kernelTime);
            snapshot.totalCpuTimeUs = snapshot.userTimeUs + snapshot.kernelTimeUs;
        }

        // Get context switch count (requires more advanced APIs like ETW)
        // For now, use a placeholder
        snapshot.contextSwitches = GetThreadContextSwitches(tid);

        CloseHandle(hThread);
    }

    // Get current core (Windows 10+)
    DWORD_PTR processAffinity, systemAffinity;
    if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity)) {
        // Check which core the thread is running on (approximate)
        snapshot.currentCore = static_cast<int32_t>(snapshot.osThreadId % __builtin_popcountll(systemAffinity));
    }

#elif defined(THREADING_ANALYZER_POSIX)
    // POSIX: Use clock_gettime for CPU time, /proc for context switches
    clockid_t cid;
    if (pthread_getcpuclockid(pthread_t(tid), &cid) == 0) {
        struct timespec ts;
        if (clock_gettime(cid, &ts) == 0) {
            snapshot.totalCpuTimeUs = ts.tv_sec * 1'000'000 + ts.tv_nsec / 1000;
        }
    }
    // Context switches: read from /proc/self/task/<tid>/status
    // (Implementation omitted for brevity)
#endif

    // Calculate CPU usage percentage (compared to last sample)
    auto it = m_threads.find(tid);
    if (it != m_threads.end() && !it->second.history.empty()) {
        const auto& prev = it->second.history.back();
        uint64_t timeDeltaUs = snapshot.timestampUs - prev.timestampUs;
        if (timeDeltaUs > 0 && prev.totalCpuTimeUs > 0) {
            uint64_t cpuDeltaUs = snapshot.totalCpuTimeUs - prev.totalCpuTimeUs;
            snapshot.cpuUsagePercent = std::min(
                100.0,
                cpuDeltaUs * 100.0 / timeDeltaUs
            );
        }
    }

    return snapshot;
}

// ─── Platform Helpers ────────────────────────────────────────────────

uint32_t ThreadingAnalyzer::GetOSThreadId(std::thread::id tid) const {
#if defined(THREADING_ANALYZER_WINDOWS)
    // On Windows, std::thread::id is not directly convertible to DWORD
    // In practice, store the OS thread ID when the thread is created
    return static_cast<uint32_t>(std::hash<std::thread::id>{}(tid));
#elif defined(THREADING_ANALYZER_POSIX)
    return static_cast<uint32_t>(pthread_gettid_np(pthread_t(tid)));
#else
    return static_cast<uint32_t>(std::hash<std::thread::id>{}(tid));
#endif
}

int32_t ThreadingAnalyzer::GetCurrentCore(std::thread::id tid) const {
#if defined(THREADING_ANALYZER_WINDOWS)
    DWORD_PTR coreMask = 0;
    // GetCurrentProcessorNumber is available on Windows XP+
    // For specific threads, use GetThreadIdealProcessor
    HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetOSThreadId(tid));
    if (hThread != NULL) {
        DWORD_PTR processAffinity, systemAffinity;
        if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity)) {
            // Approximate: use thread ID hash to guess current core
            CloseHandle(hThread);
            return static_cast<int32_t>(GetCurrentProcessorNumber());
        }
        CloseHandle(hThread);
    }
    return -1;
#else
    // POSIX: use sched_getcpu()
    return 0;  // Stub
#endif
}

uint64_t ThreadingAnalyzer::GetThreadCpuTime(std::thread::id tid) const {
    auto snapshot = CaptureThreadSnapshot(tid);
    return snapshot.totalCpuTimeUs;
}

uint64_t ThreadingAnalyzer::GetThreadContextSwitches(std::thread::id tid) const {
    // This is a simplified implementation.
    // Real implementation would use:
    //   - Windows: ETW or QueryThreadCycleTime
    //   - Linux: /proc/self/task/<tid>/status (voluntary/nonvoluntary_switches)
    auto it = m_threads.find(tid);
    if (it != m_threads.end() && !it->second.history.empty()) {
        return it->second.history.back().contextSwitches;
    }
    return 0;
}

// ─── Contention Helpers ─────────────────────────────────────────────

ContentionType ThreadingAnalyzer::ClassifyContention(const ContentionEvent& event) const {
    if (event.durationUs < 100) {
        return ContentionType::CacheLineBounce;  // Very short = likely cache
    } else if (event.durationUs > 50000) {
        return ContentionType::PriorityInversion;  // Very long = likely inversion
    } else if (event.blockedThreadCount > 3) {
        return ContentionType::QueueStarvation;  // Many blocked = starvation
    } else {
        return ContentionType::LockContention;  // Default
    }
}

double ThreadingAnalyzer::CalculateContentionSeverity(const ContentionEvent& event) const {
    double severity = 0.0;

    // Base: duration
    severity += std::min(50.0, event.durationUs / 1000.0);  // Up to 50 from duration

    // Blocked thread count
    severity += std::min(30.0, event.blockedThreadCount * 10.0);

    // Type multiplier
    switch (event.type) {
        case ContentionType::PriorityInversion: severity *= 1.5; break;
        case ContentionType::LockContention:     severity *= 1.0; break;
        case ContentionType::CacheLineBounce:   severity *= 0.8; break;
        case ContentionType::QueueStarvation:    severity *= 1.2; break;
        default: break;
    }

    return std::min(100.0, severity);
}

std::string ThreadingAnalyzer::GenerateContentionSuggestion(const ContentionEvent& event) const {
    switch (event.type) {
        case ContentionType::LockContention:
            return "Reduce lock scope; consider lock-free algorithms or finer-grained locking.";
        case ContentionType::CacheLineBounce:
            return "Check for false sharing; pad frequently-accessed variables to 64-byte boundaries.";
        case ContentionType::PriorityInversion:
            return "Review thread priorities; consider priority inheritance or priority ceiling protocol.";
        case ContentionType::CoreMigration:
            return "Set thread affinity to reduce cache miss penalty from core migration.";
        case ContentionType::QueueStarvation:
            return "Increase thread pool size or reduce task granularity to avoid starvation.";
        case ContentionType::OverSubscription:
            return "Reduce thread count; match to hardware core count for optimal performance.";
        default:
            return "Investigate threading performance issue.";
    }
}

uint64_t ThreadingAnalyzer::EstimateCacheMissPenalty(const CoreMigrationEvent& event) const {
    // Rough estimate: each core migration causes L1/L2 cache flush
    // L1 miss: ~4 cycles, L2 miss: ~12 cycles, L3 miss: ~60 cycles
    // At 3GHz, L3 miss ≈ 20ns = 0.02ms
    // But actually, the penalty is in lost cache warmth, not just the miss itself
    // Heuristic: 0.1-0.5ms per migration depending on thread working set
    return 100000ULL;  // 100μs estimate; real calculation would use working set size
}

// ─── Export to JSON ────────────────────────────────────────────────

std::string ThreadingAnalyzer::ExportToJSON() const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    std::ostringstream oss;
    oss << "{\n";

    // Metadata
    oss << "  \"timestampUs\": " << m_cachedAnalysis.timestampUs << ",\n";
    oss << "  \"sampleCount\": " << m_totalSamples.load() << ",\n";
    oss << "  \"totalThreadsTracked\": " << m_threads.size() << ",\n";

    // Overall metrics
    oss << "  \"overallCpuUsage\": " << std::fixed << std::setprecision(2)
        << m_cachedAnalysis.overallCpuUsage << ",\n";
    oss << "  \"avgContextSwitchRate\": " << m_cachedAnalysis.avgContextSwitchRate << ",\n";
    oss << "  \"totalContentionTimeMs\": " << (m_cachedAnalysis.totalContentionTimeUs / 1000.0) << ",\n";
    oss << "  \"activeContentionEvents\": " << m_cachedAnalysis.activeContentionEvents << ",\n";

    // Contention summary
    oss << "  \"contentionSummary\": {\n";
    oss << "    \"lockContention\": " << m_cachedAnalysis.lockContentionCount << ",\n";
    oss << "    \"cacheBounce\": " << m_cachedAnalysis.cacheBounceCount << ",\n";
    oss << "    \"priorityInversion\": " << m_cachedAnalysis.priorityInversionCount << ",\n";
    oss << "    \"coreMigration\": " << m_cachedAnalysis.coreMigrationCount << ",\n";
    oss << "    \"starvation\": " << m_cachedAnalysis.starvationCount << "\n";
    oss << "  },\n";

    // Thread summaries
    oss << "  \"threads\": [";
    bool firstThread = true;
    for (const auto& summary : m_cachedAnalysis.threadSummaries) {
        if (!firstThread) oss << ",";
        firstThread = false;
        oss << "\n    {\n";
        oss << "      \"threadId\": \"" << GetThreadName(summary.threadId) << "\",\n";
        oss << "      \"role\": \"" << ThreadRoleToString(summary.role) << "\",\n";
        oss << "      \"avgCpuUsage\": " << std::fixed << std::setprecision(2) << summary.avgCpuUsage << ",\n";
        oss << "      \"totalCpuTimeMs\": " << (summary.totalCpuTimeUs / 1000.0) << ",\n";
        oss << "      \"contextSwitches\": " << summary.contextSwitches << ",\n";
        oss << "      \"contentionEvents\": " << summary.contentionEvents << ",\n";
        oss << "      \"coreAffinity\": " << summary.coreAffinity << ",\n";
        oss << "      \"isBottleneck\": " << (summary.isBottleneck ? "true" : "false");
        if (!summary.bottleneckReason.empty()) {
            oss << ",\n      \"bottleneckReason\": \"" << summary.bottleneckReason << "\"";
        }
        oss << "\n    }";
    }
    oss << "\n  ],\n";

    // Hotspots
    oss << "  \"hotspots\": [";
    bool firstHot = true;
    for (const auto& hotspot : m_cachedAnalysis.hotspots) {
        if (!firstHot) oss << ",";
        firstHot = false;
        oss << "\n    {\n";
        oss << "      \"threadId\": \"" << GetThreadName(hotspot.threadId) << "\",\n";
        oss << "      \"primaryType\": \"" << ContentionTypeToString(hotspot.primaryType) << "\",\n";
        oss << "      \"eventCount\": " << hotspot.eventCount << ",\n";
        oss << "      \"totalWaitTimeMs\": " << (hotspot.totalWaitTimeUs / 1000.0) << ",\n";
        oss << "      \"avgSeverity\": " << std::fixed << std::setprecision(1) << hotspot.avgSeverity << "\n";
        oss << "    }";
    }
    oss << "\n  ],\n";

    // Recent migrations
    oss << "  \"recentMigrations\": [";
    bool firstMig = true;
    for (const auto& mig : m_cachedAnalysis.recentMigrations) {
        if (!firstMig) oss << ",";
        firstMig = false;
        oss << "\n    {\n";
        oss << "      \"threadId\": \"" << GetThreadName(mig.threadId) << "\",\n";
        oss << "      \"fromCore\": " << mig.fromCore << ",\n";
        oss << "      \"toCore\": " << mig.toCore << ",\n";
        oss << "      \"penaltyEstimateMs\": " << std::fixed << std::setprecision(2)
            << (mig.cacheMissPenaltyEstimateMs) << "\n";
        oss << "    }";
    }
    oss << "\n  ],\n";

    // Recommendations
    oss << "  \"recommendations\": [";
    bool firstRec = true;
    for (const auto& rec : m_cachedAnalysis.recommendations) {
        if (!firstRec) oss << ",";
        firstRec = false;
        oss << "\n    \"" << rec << "\"";
    }
    oss << "\n  ]\n";

    oss << "}\n";
    return oss.str();
}

std::string ThreadingAnalyzer::ExportThreadHistoryToJSON(std::thread::id tid) const {
    const auto& history = GetThreadHistory(tid);
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"threadId\": \"" << GetThreadName(tid) << "\",\n";
    oss << "  \"role\": \"" << ThreadRoleToString(GetThreadRole(tid)) << "\",\n";
    oss << "  \"snapshots\": [";
    bool first = true;
    for (const auto& snap : history) {
        if (!first) oss << ",";
        first = false;
        oss << "\n    {\n";
        oss << "      \"timestampUs\": " << snap.timestampUs << ",\n";
        oss << "      \"cpuUsage\": " << std::fixed << std::setprecision(2) << snap.cpuUsagePercent << ",\n";
        oss << "      \"contextSwitches\": " << snap.contextSwitches << ",\n";
        oss << "      \"contextSwitchRate\": " << snap.contextSwitchRate << ",\n";
        oss << "      \"currentCore\": " << snap.currentCore << ",\n";
        oss << "      \"isBlocked\": " << (snap.isBlocked ? "true" : "false") << "\n";
        oss << "    }";
    }
    oss << "\n  ]\n";
    oss << "}\n";
    return oss.str();
}

std::string ThreadingAnalyzer::ExportContentionEventsToJSON() const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"activeContention\": " << m_activeContentionCount.load() << ",\n";
    oss << "  \"totalContentionEvents\": " << m_contentionEvents.size() << ",\n";
    oss << "  \"events\": [";
    bool first = true;
    for (const auto& evt : m_contentionEvents) {
        if (!first) oss << ",";
        first = false;
        oss << "\n    {\n";
        oss << "      \"timestampUs\": " << evt.timestampUs << ",\n";
        oss << "      \"threadId\": \"" << GetThreadName(evt.threadId) << "\",\n";
        oss << "      \"type\": \"" << ContentionTypeToString(evt.type) << "\",\n";
        oss << "      \"durationMs\": " << (evt.durationUs / 1000.0) << ",\n";
        oss << "      \"severity\": " << std::fixed << std::setprecision(1) << evt.severity << ",\n";
        if (!evt.lockName.empty()) {
            oss << "      \"lockName\": \"" << evt.lockName << "\",\n";
        }
        oss << "      \"description\": \"" << evt.description << "\"\n";
        oss << "    }";
    }
    oss << "\n  ]\n";
    oss << "}\n";
    return oss.str();
}

} // namespace ProfilerCore
