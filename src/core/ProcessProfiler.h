#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <atomic>
#include <mutex>

namespace ProfilerCore {

// ─── Data structures ──────────────────────────────────────────────────────────

struct ProcessInfo {
    DWORD   processId;
    std::wstring name;
    std::wstring executablePath;
    SIZE_T  workingSetSize;    // bytes
    bool    is64Bit;
};

enum class AttachResult {
    Success,
    ProcessNotFound,
    AccessDenied,
    AlreadyAttached,
    InitFailed,
    UnknownError
};

/**
 * Sampling source.
 * Note: Game frame boundary detection via GPU/driver hooks requires DLL injection,
 * which is blocked by security policy. This implementation uses Windows PDH counters
 * as a no-injection alternative — see README for the injection-based approach.
 */
enum class SamplingMethod {
    PDH_Processor,   // Windows PDH: CPU + memory counters (no injection, cross-process)
    PDH_GPU         // Windows PDH: GPU engine time via PDH (no injection)
};

struct RemoteFrameData {
    int64_t  timestamp;       // microseconds since epoch
    int64_t  frameIndex;      // monotonically increasing counter
    float    fps;             // instantaneous FPS (estimated from CPU)
    float    frameTimeMs;     // frame time in milliseconds
    float    cpuTimeMs;       // CPU time delta in this sample interval
    float    cpuPercent;      // process CPU utilization %
    size_t   workingSet;      // process working set in bytes
    bool     valid;
};

struct ProcessStats {
    double   cpuPercent;      // % CPU (sum of all cores)
    size_t   workingSet;      // bytes
    size_t   privateBytes;    // bytes
    size_t   virtualSize;     // bytes
    int64_t  timestampUs;     // microseconds
};

// ─── Main class ───────────────────────────────────────────────────────────────

/**
 * ProcessProfiler — Attach to a running game process and sample performance
 * metrics via Windows PDH (Performance Data Helper), without any DLL injection.
 *
 * How it works:
 *   - PDH exposes Windows performance counter data in real time
 *   - Counters sampled: % Processor Time, Working Set, Private Bytes,
 *     context switch delta, GPU engine time (if available)
 *   - Frame rate is estimated from CPU scheduling intervals (scheduling
 *     frequency + active thread quantum)
 *
 * For higher accuracy (true GPU-synced frame boundaries), integrate
 * a DLL-based Present hook in your game's rendering pipeline. See README.
 */
class ProcessProfiler {
public:
    using FrameCallback  = std::function<void(const RemoteFrameData&)>;
    using StatusCallback  = std::function<void(const std::string&)>;
    using StatsCallback   = std::function<void(const ProcessStats&)>;

    ProcessProfiler();
    ~ProcessProfiler();

    ProcessProfiler(const ProcessProfiler&) = delete;
    ProcessProfiler& operator=(const ProcessProfiler&) = delete;

    // ─── Process Discovery ───────────────────────────────────────────────────

    /** Enumerate all running processes, optionally filtered by name substring.
     *  Results are sorted by working set size (largest first). */
    std::vector<ProcessInfo> EnumerateProcesses(
        const std::wstring& nameFilter = L"", int maxResults = 50) const;

    /** Find exactly one process by name. Returns nullopt if zero or multiple matches. */
    std::optional<ProcessInfo> FindProcess(const std::wstring& name) const;

    /** Get info for a specific PID. */
    std::optional<ProcessInfo> GetProcessInfo(DWORD pid) const;

    // ─── Attach / Detach ─────────────────────────────────────────────────────

    /** Attach to a process by PID. After this, call StartSampling(). */
    AttachResult Attach(DWORD pid, SamplingMethod method = SamplingMethod::PDH_Processor);

    /** Detach from the currently attached process. */
    void Detach();

    [[nodiscard]] bool   IsAttached()    const { return m_isAttached.load(std::memory_order_acquire); }
    [[nodiscard]] DWORD  GetAttachedPid() const { return m_attachedPid; }

    // ─── Sampling Control ────────────────────────────────────────────────────

    /** Start background sampling (separate thread). */
    void StartSampling();

    /** Stop background sampling. */
    void StopSampling();

    [[nodiscard]] bool  IsSampling()   const { return m_isSampling.load(std::memory_order_acquire); }

    // ─── Callbacks ──────────────────────────────────────────────────────────

    /** Called on every new frame sample (approx 60 Hz). */
    void SetFrameCallback(FrameCallback cb) { m_frameCallback = std::move(cb); }

    /** Called when status changes (attach/detach/error). */
    void SetStatusCallback(StatusCallback cb) { m_statusCallback = std::move(cb); }

    /** Called on each PDH polling cycle with raw process stats. */
    void SetStatsCallback(StatsCallback cb) { m_statsCallback = std::move(cb); }

    // ─── Data Access ─────────────────────────────────────────────────────────

    /** Most recent sample. Thread-safe. */
    std::optional<RemoteFrameData> GetLatestFrame() const;

    /** Last N samples in chronological order. Thread-safe. */
    std::vector<RemoteFrameData> GetRecentFrames(size_t count = 60) const;

    /** Target FPS used for frame-time estimation when no real frame boundary is available. */
    void   SetTargetFPS(float fps)   { m_targetFps = fps; }
    [[nodiscard]] float GetTargetFPS() const { return m_targetFps; }

    // ─── One-shot Query (no sampling thread) ───────────────────────────────

    /** Query current stats once without starting a sampling session.
     *  Useful for a quick snapshot. */
    std::optional<ProcessStats> QueryOnce() const;

private:
    // Internal
    static DWORD WINAPI SamplingThreadProc(LPVOID lpParam);
    void SamplingLoop();

    bool IsProcessRunning(DWORD pid) const;
    bool Is64BitProcess(DWORD pid) const;
    bool InitPdhCounters(DWORD pid);
    void ClosePdhCounters();

    std::optional<RemoteFrameData> PollPdhOnce(int64_t nowUs);
    float EstimateFpsFromCpu(double cpuPercent, float elapsedMs) const;

    void ReportStatus(const std::string& msg);

    // ─── State ──────────────────────────────────────────────────────────────

    std::atomic<bool>   m_isAttached{ false };
    std::atomic<bool>   m_isSampling{ false };
    DWORD              m_attachedPid = 0;
    SamplingMethod     m_samplingMethod = SamplingMethod::PDH_Processor;

    HANDLE             m_samplingThread = nullptr;
    HANDLE             m_stopEvent = nullptr;

    FrameCallback      m_frameCallback;
    StatusCallback     m_statusCallback;
    StatsCallback      m_statsCallback;

    float              m_targetFps = 60.0f;

    // PDH
    HQUERY             m_pdhQuery = nullptr;
    bool               m_pdhInit = false;
    bool               m_pdhHasGpuCounters = false;

    struct PdhCounter {
        HCOUNTER h = nullptr;
        std::wstring path;
    };
    PdhCounter         m_counterCpu;
    PdhCounter         m_counterWorkingSet;
    PdhCounter         m_counterPrivateBytes;
    PdhCounter         m_counterCtxSwitches;  // for scheduling analysis
    PdhCounter         m_counterGpuEngine;      // GPU utilization (if available)

    // PDH values for delta calculation
    double             m_prevCpuValue = 0.0;
    int64_t            m_prevTimestampUs = 0;
    uint64_t           m_prevCtxSwitches = 0;

    // Ring buffer
    static constexpr size_t kFrameHistorySize = 600;
    RemoteFrameData    m_frameHistory[kFrameHistorySize];
    std::atomic<size_t> m_frameHead{ 0 };
    std::mutex         m_frameMutex;
};

// ─── Convenience: RAII attach guard ───────────────────────────────────────────

class ScopedProcessAttach {
public:
    explicit ScopedProcessAttach(ProcessProfiler& p, DWORD pid,
                                 SamplingMethod m = SamplingMethod::PDH_Processor)
        : m_profiler(p)
    {
        m_profiler.Attach(pid, m);
    }
    ~ScopedProcessAttach() {
        if (m_profiler.IsAttached()) m_profiler.Detach();
    }
    ScopedProcessAttach(const ScopedProcessAttach&) = delete;
    ScopedProcessAttach& operator=(const ScopedProcessAttach&) = delete;
private:
    ProcessProfiler& m_profiler;
};

} // namespace ProfilerCore
