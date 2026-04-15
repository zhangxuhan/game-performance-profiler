#include "ProcessProfiler.h"
#include <TlHelp32.h>
#include <psapi.h>
#include <Pdh.h>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "Pdh.lib")
#pragma comment(lib, "psapi.lib")

namespace {

constexpr int kPollIntervalMs = 16; // ~60 Hz

// Build a PDH counter path for the given process ID
std::wstring BuildProcessCounterPath(DWORD pid, const wchar_t* counter) {
    wchar_t buf[512];
    swprintf_s(buf, L"\\Process(%lu)\\%s", pid, counter);
    return std::wstring(buf);
}

// Try to build a GPU counter path (may not exist on all systems)
std::wstring BuildGpuCounterPath() {
    // Try common GPU counters — most capable first
    const wchar_t* candidates[] = {
        L"\\GPU Engine(pid_XXXX_engtype_3D)\\Utilization Percentage",
        L"\\GPU Engine(*)\\Utilization Percentage",
    };
    // We'll just use the wildcard form and filter in PollPdhOnce
    return std::wstring(L"\\GPU Engine(*)\\Utilization Percentage");
}

bool AddCounter(HQUERY query, const std::wstring& path, HCOUNTER* outHandle) {
    HCOUNTER h;
    PDH_STATUS st = PdhAddCounterW(query, path.c_str(), 0, &h);
    if (st == ERROR_SUCCESS) {
        *outHandle = h;
        return true;
    }
    // Counter may not exist on this system — not a fatal error
    return false;
}

double ReadCounterDouble(HCOUNTER h) {
    if (!h) return 0.0;
    PDH_FMT_COUNTERVALUE val;
    DWORD type;
    if (PdhGetFormattedCounterValue(h, PDH_FMT_DOUBLE, &type, &val) == ERROR_SUCCESS) {
        return val.doubleValue;
    }
    return 0.0;
}

uint64_t ReadCounterUInt64(HCOUNTER h) {
    if (!h) return 0;
    PDH_FMT_COUNTERVALUE val;
    DWORD type;
    if (PdhGetFormattedCounterValue(h, PDH_FMT_LARGE, &type, &val) == ERROR_SUCCESS) {
        return static_cast<uint64_t>(val.longlongValue);
    }
    return 0;
}

bool Is64BitWindows() {
#ifdef _WIN64
    return true;
#else
    BOOL b = FALSE;
    IsWow64Process(GetCurrentProcess(), &b);
    return !b;
#endif
}

bool Is64BitProcess(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    BOOL b = FALSE;
    IsWow64Process(h, &b);
    CloseHandle(h);
    return !b;
}

} // anonymous namespace

namespace ProfilerCore {

// ─── ProcessProfiler ──────────────────────────────────────────────────────────

ProcessProfiler::ProcessProfiler() {
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

ProcessProfiler::~ProcessProfiler() {
    StopSampling();
    Detach();
    if (m_stopEvent) { CloseHandle(m_stopEvent); m_stopEvent = nullptr; }
}

// ─── Process Discovery ──────────────────────────────────────────────────────

std::vector<ProcessInfo> ProcessProfiler::EnumerateProcesses(
    const std::wstring& filter, int maxResults) const
{
    std::vector<ProcessInfo> result;
    result.reserve(128);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap, &pe)) { CloseHandle(snap); return result; }

    do {
        std::wstring name(pe.szExeFile);

        // Case-insensitive substring filter
        if (!filter.empty()) {
            std::wstring nl = name, fl = filter;
            std::transform(nl.begin(), nl.end(), nl.begin(), ::towlower);
            std::transform(fl.begin(), fl.end(), fl.begin(), ::towlower);
            if (nl.find(fl) == std::wstring::npos) continue;
        }

        ProcessInfo info;
        info.processId = pe.th32ProcessID;
        info.name = name;

        // Get executable path
        wchar_t pathBuf[MAX_PATH];
        pathBuf[0] = L'\0';
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (hProc) {
            DWORD len = MAX_PATH;
            QueryFullProcessImageNameW(hProc, 0, pathBuf, &len);
            CloseHandle(hProc);
        }
        info.executablePath = pathBuf;

        info.is64Bit = Is64BitProcess(pe.th32ProcessID);
        info.workingSetSize = 0;

        // Get working set
        hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                info.workingSetSize = pmc.WorkingSetSize;
            }
            CloseHandle(hProc);
        }

        result.push_back(std::move(info));

    } while (Process32NextW(snap, &pe));

    CloseHandle(snap);

    // Sort by working set descending (game processes tend to use more memory)
    std::sort(result.begin(), result.end(),
        [](const ProcessInfo& a, const ProcessInfo& b) {
            return a.workingSetSize > b.workingSetSize;
        });

    if (static_cast<int>(result.size()) > maxResults) {
        result.resize(maxResults);
    }
    return result;
}

std::optional<ProcessInfo> ProcessProfiler::FindProcess(const std::wstring& name) const {
    auto all = EnumerateProcesses(name);
    if (all.size() == 1) return std::make_optional(std::move(all[0]));
    return std::nullopt;
}

std::optional<ProcessInfo> ProcessProfiler::GetProcessInfo(DWORD pid) const {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return std::nullopt;

    std::optional<ProcessInfo> result = std::nullopt;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID != pid) continue;
            ProcessInfo info;
            info.processId = pid;
            info.name = pe.szExeFile;
            info.executablePath = L"";
            info.is64Bit = Is64BitProcess(pid);
            info.workingSetSize = 0;

            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProc) {
                PROCESS_MEMORY_COUNTERS pmc{};
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                    info.workingSetSize = pmc.WorkingSetSize;
                }
                CloseHandle(hProc);
            }
            result = std::make_optional(std::move(info));
            break;
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return result;
}

// ─── Attach / Detach ────────────────────────────────────────────────────────

bool ProcessProfiler::IsProcessRunning(DWORD pid) const {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    DWORD ret = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return ret == WAIT_TIMEOUT;
}

AttachResult ProcessProfiler::Attach(DWORD pid, SamplingMethod method) {
    if (m_isAttached.load(std::memory_order_acquire)) {
        if (m_attachedPid == pid) return AttachResult::AlreadyAttached;
        Detach();
    }

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        DWORD err = GetLastError();
        return (err == ERROR_ACCESS_DENIED) ? AttachResult::AccessDenied
                                            : AttachResult::ProcessNotFound;
    }
    CloseHandle(h);

    m_attachedPid = pid;
    m_samplingMethod = method;

    if (!InitPdhCounters(pid)) {
        m_attachedPid = 0;
        return AttachResult::InitFailed;
    }

    m_isAttached.store(true, std::memory_order_release);
    ReportStatus("Attached to PID " + std::to_string(pid) +
        " (PDH-based sampling, no injection)");
    return AttachResult::Success;
}

void ProcessProfiler::Detach() {
    if (!m_isAttached.load(std::memory_order_acquire)) return;

    StopSampling();
    ClosePdhCounters();

    m_isAttached.store(false, std::memory_order_release);
    m_attachedPid = 0;
    m_pdhInit = false;
    ReportStatus("Detached");
}

// ─── PDH Setup ──────────────────────────────────────────────────────────────

bool ProcessProfiler::InitPdhCounters(DWORD pid) {
    ClosePdhCounters();

    PDH_STATUS st = PdhOpenQueryW(nullptr, 0, &m_pdhQuery);
    if (st != ERROR_SUCCESS) {
        ReportStatus("PdhOpenQuery failed: " + std::to_string(st));
        return false;
    }

    // % Processor Time (per-process)
    AddCounter(m_pdhQuery, BuildProcessCounterPath(pid, L"% Processor Time"),
               &m_counterCpu.h);

    // Working Set
    AddCounter(m_pdhQuery, BuildProcessCounterPath(pid, L"Working Set"),
               &m_counterWorkingSet.h);

    // Private Bytes
    AddCounter(m_pdhQuery, BuildProcessCounterPath(pid, L"Private Bytes"),
               &m_counterPrivateBytes.h);

    // Context Switches/sec (for scheduling analysis)
    AddCounter(m_pdhQuery, BuildProcessCounterPath(pid, L"Context Switches/sec"),
               &m_counterCtxSwitches.h);

    // GPU utilization (optional — may not exist on all systems)
    if (m_samplingMethod == SamplingMethod::PDH_GPU) {
        std::wstring gpuPath = BuildGpuCounterPath();
        AddCounter(m_pdhQuery, gpuPath, &m_counterGpuEngine.h);
        m_pdhHasGpuCounters = (m_counterGpuEngine.h != nullptr);
    }

    // Collect initial sample so PDH internal state is primed
    PdhCollectQueryData(m_pdhQuery);
    Sleep(50);
    PdhCollectQueryData(m_pdhQuery);

    m_pdhInit = true;
    ReportStatus("PDH counters initialized for PID " + std::to_string(pid));
    return true;
}

void ProcessProfiler::ClosePdhCounters() {
    if (m_pdhQuery) {
        PdhCloseQuery(m_pdhQuery);
        m_pdhQuery = nullptr;
    }
    memset(&m_counterCpu, 0, sizeof(m_counterCpu));
    memset(&m_counterWorkingSet, 0, sizeof(m_counterWorkingSet));
    memset(&m_counterPrivateBytes, 0, sizeof(m_counterPrivateBytes));
    memset(&m_counterCtxSwitches, 0, sizeof(m_counterCtxSwitches));
    memset(&m_counterGpuEngine, 0, sizeof(m_counterGpuEngine));
    m_pdhInit = false;
}

// ─── One-shot PDH Poll ───────────────────────────────────────────────────────

std::optional<ProcessStats> ProcessProfiler::QueryOnce() const {
    if (!m_pdhInit || !m_pdhQuery) return std::nullopt;

    PdhCollectQueryData(m_pdhQuery);

    ProcessStats stats;
    stats.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    stats.cpuPercent    = ReadCounterDouble(m_counterCpu.h);
    stats.workingSet    = ReadCounterUInt64(m_counterWorkingSet.h);
    stats.privateBytes  = ReadCounterUInt64(m_counterPrivateBytes.h);

    // Virtual Size
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_attachedPid);
    if (hProc) {
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
            stats.virtualSize = pmc.PagefileUsage;
        }
        CloseHandle(hProc);
    } else {
        stats.virtualSize = 0;
    }

    return std::make_optional(stats);
}

std::optional<RemoteFrameData> ProcessProfiler::PollPdhOnce(int64_t nowUs) {
    if (!m_pdhInit || !m_pdhQuery) return std::nullopt;

    PdhCollectQueryData(m_pdhQuery);

    double   cpuPercent   = ReadCounterDouble(m_counterCpu.h);
    size_t   workingSet   = static_cast<size_t>(ReadCounterUInt64(m_counterWorkingSet.h));
    size_t   privateBytes = static_cast<size_t>(ReadCounterUInt64(m_counterPrivateBytes.h));
    uint64_t ctxSwitches  = ReadCounterUInt64(m_counterCtxSwitches.h);
    double   gpuPercent   = ReadCounterDouble(m_counterGpuEngine.h);

    // Delta timing
    float elapsedMs = 0.0f;
    if (m_prevTimestampUs > 0) {
        elapsedMs = static_cast<float>(nowUs - m_prevTimestampUs) / 1000.0f;
    }
    double elapsedSec = elapsedMs / 1000.0;

    // Delta CPU value (PDH % is normalized to 0–100 per core)
    double cpuDelta = 0.0;
    if (m_prevCpuValue >= 0 && elapsedSec > 0) {
        cpuDelta = cpuPercent - m_prevCpuValue;
        // CPU% can spike above 100 on multi-core, cap at 800 (8 cores fully used)
        cpuDelta = std::clamp(cpuDelta, -100.0, 800.0);
    }
    m_prevCpuValue = cpuPercent;

    // Delta context switches
    uint64_t ctxDelta = 0;
    if (m_prevCtxSwitches > 0 && elapsedMs > 0) {
        ctxDelta = (ctxSwitches > m_prevCtxSwitches) ? (ctxSwitches - m_prevCtxSwitches) : 0;
    }
    m_prevCtxSwitches = ctxSwitches;

    // Estimate frame time
    // Methodology:
    //   When a game's main thread runs on one core, it will consume ~100% of that core.
    //   If it runs on multiple threads, %CPU ~ N * 100%.
    //   On a 4-core system, full single-thread utilization = 25% total.
    //   A 60 FPS game at 16.67ms/frame means the thread is active for most of each frame interval.
    //   We use the ratio of actual CPU% to "full thread" CPU% to estimate busy time.
    //
    //   A process using 100% of 1 core continuously → ~25% total CPU (4-core system)
    //   Frame time estimate = elapsedMs * (cpuPercent / cpuForOneCore)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    double cpuCount = static_cast<double>(si.dwNumberOfProcessors);
    double cpuPerCore = 100.0 / cpuCount;  // 1 core = 100%/N
    double cpuFraction = cpuPercent / cpuPerCore;  // fraction of a full core
    cpuFraction = std::clamp(cpuFraction, 0.0, cpuCount);

    float frameTimeMs = 0.0f;
    float fps = 0.0f;

    if (elapsedMs > 0) {
        if (cpuFraction > 0.5) {
            // Estimate busy time within the sample interval
            // busyTime = elapsedMs * (activeCpuFraction / 1.0)
            float busyMs = static_cast<float>(elapsedMs * cpuFraction);
            // Number of frames that could have fit in busy time
            float framesInBusy = busyMs / (1000.0f / m_targetFps);
            float framesInTotal = elapsedMs / (1000.0f / m_targetFps);

            if (framesInTotal > 0.5f) {
                fps = framesInTotal;  // Normalized to target FPS per interval
                // Better estimate: fps = framesInBusy when actively rendering
                // Use CPU as activity signal
                fps = static_cast<float>(m_targetFps * cpuFraction / 1.0);
                fps = std::clamp(fps, 1.0f, 500.0f);
                frameTimeMs = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
            }
        } else {
            // Low CPU: likely idle or waiting on GPU — assume target FPS
            frameTimeMs = 1000.0f / m_targetFps;
            fps = m_targetFps;
        }
    }

    m_prevTimestampUs = nowUs;

    RemoteFrameData data;
    data.timestamp    = nowUs;
    data.frameIndex   = 0; // assigned below
    data.fps          = fps;
    data.frameTimeMs = frameTimeMs;
    data.cpuTimeMs    = static_cast<float>(cpuDelta * elapsedSec);
    data.cpuPercent   = static_cast<float>(cpuPercent);
    data.workingSet   = workingSet;
    data.valid        = (elapsedMs > 0);

    return std::make_optional(data);
}

float ProcessProfiler::EstimateFpsFromCpu(double cpuPercent, float elapsedMs) const {
    // See methodology in PollPdhOnce
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    double cpuPerCore = 100.0 / si.dwNumberOfProcessors;
    double cpuFraction = cpuPercent / cpuPerCore;
    cpuFraction = std::clamp(cpuFraction, 0.0, si.dwNumberOfProcessors);

    if (cpuFraction < 0.1) {
        // Idle — no frames rendered this interval
        return 0.0f;
    }

    // Estimate active frames per interval
    // Active frames = how many frame intervals fit in the CPU-active portion
    float frameInterval = 1000.0f / m_targetFps;
    float busyMs = static_cast<float>(elapsedMs * cpuFraction / 1.0);
    float activeFrames = busyMs / frameInterval;
    return std::clamp(activeFrames, 0.0f, 500.0f);
}

// ─── Sampling Loop ───────────────────────────────────────────────────────────

DWORD WINAPI ProcessProfiler::SamplingThreadProc(LPVOID lpParam) {
    auto* self = static_cast<ProcessProfiler*>(lpParam);
    self->SamplingLoop();
    return 0;
}

void ProcessProfiler::StartSampling() {
    if (m_isSampling.load(std::memory_order_acquire)) return;
    if (!m_isAttached.load(std::memory_order_acquire)) {
        ReportStatus("Cannot start sampling: not attached to any process");
        return;
    }

    // Reset ring buffer
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_frameHead.store(0);
    }

    ResetEvent(m_stopEvent);
    m_isSampling.store(true, std::memory_order_release);
    m_prevTimestampUs = 0;
    m_prevCpuValue = 0;
    m_prevCtxSwitches = 0;

    m_samplingThread = CreateThread(nullptr, 0, &SamplingThreadProc, this, 0, nullptr);
    if (!m_samplingThread) {
        m_isSampling.store(false, std::memory_order_release);
        ReportStatus("Failed to create sampling thread");
        return;
    }

    ReportStatus("PDH sampling started (~60 Hz)");
}

void ProcessProfiler::StopSampling() {
    if (!m_isSampling.load(std::memory_order_acquire)) return;

    SetEvent(m_stopEvent);
    if (m_samplingThread) {
        WaitForSingleObject(m_samplingThread, 2000);
        CloseHandle(m_samplingThread);
        m_samplingThread = nullptr;
    }
    m_isSampling.store(false, std::memory_order_release);
    ReportStatus("Sampling stopped");
}

void ProcessProfiler::SamplingLoop() {
    int64_t frameIndex = 0;

    while (true) {
        DWORD wait = WaitForSingleObject(m_stopEvent, kPollIntervalMs);
        if (wait == WAIT_OBJECT_0) break;

        if (!m_isAttached.load(std::memory_order_acquire)) break;

        // Check process still alive
        if (!IsProcessRunning(m_attachedPid)) {
            ReportStatus("Target process exited");
            break;
        }

        auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();

        auto frameOpt = PollPdhOnce(nowUs);
        if (!frameOpt || !frameOpt->valid) continue;

        RemoteFrameData frame = *frameOpt;
        frame.frameIndex = frameIndex++;

        // Stats callback
        if (m_statsCallback) {
            ProcessStats st;
            st.cpuPercent = frame.cpuPercent;
            st.workingSet = frame.workingSet;
            st.privateBytes = 0;
            st.virtualSize = 0;
            st.timestampUs = nowUs;
            m_statsCallback(st);
        }

        // Ring buffer
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            size_t idx = m_frameHead.load() % kFrameHistorySize;
            m_frameHistory[idx] = frame;
            m_frameHead.store(idx + 1);
        }

        // User callback
        if (m_frameCallback) {
            m_frameCallback(frame);
        }
    }
}

// ─── Data Access ─────────────────────────────────────────────────────────────

std::optional<RemoteFrameData> ProcessProfiler::GetLatestFrame() const {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    size_t head = m_frameHead.load();
    if (head == 0) return std::nullopt;
    return m_frameHistory[(head - 1) % kFrameHistorySize];
}

std::vector<RemoteFrameData> ProcessProfiler::GetRecentFrames(size_t count) const {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    std::vector<RemoteFrameData> out;
    size_t head = m_frameHead.load();
    if (head == 0) return out;

    size_t actual = std::min(count, head);
    out.reserve(actual);
    for (size_t i = 0; i < actual; ++i) {
        out.push_back(m_frameHistory[(head - 1 - i) % kFrameHistorySize]);
    }
    return out;
}

// ─── Status ─────────────────────────────────────────────────────────────────

void ProcessProfiler::ReportStatus(const std::string& msg) {
    if (m_statusCallback) {
        m_statusCallback(msg);
    }
    OutputDebugStringA(("[ProcessProfiler] " + msg + "\n").c_str());
}

} // namespace ProfilerCore
