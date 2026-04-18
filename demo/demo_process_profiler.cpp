// ProcessProfiler Demo
// Build: cl /EHsc /std:c++17 /I src src\core\ProcessProfiler.cpp demo\demo_process_profiler.cpp /link Pdh.lib psapi.lib

#include "ProcessProfiler.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

using namespace ProfilerCore;

static int gSampleCount = 0;

// ─── Helpers ──────────────────────────────────────────────────────────────────

void PrintProcessInfo(const ProcessInfo& pi) {
    printf("  PID %-6lu  %-30ls  %s  %6.1f MB\n",
        pi.processId,
        pi.name.c_str(),
        pi.is64Bit ? "x64" : "x86",
        static_cast<double>(pi.workingSetSize) / (1024.0 * 1024.0));
}

void PrintSeparator() {
    printf("─────────────────────────────────────────────────────────────────\n");
}

// ─── Callbacks ────────────────────────────────────────────────────────────────

void OnFrame(const RemoteFrameData& frame) {
    gSampleCount++;
    // Only print every 30th sample (~every 0.5s at 60Hz) to keep output readable
    if (gSampleCount % 30 == 1) {
        printf("  [#%-5lld] FPS: %6.1f  Frame: %6.2f ms  CPU: %5.1f%%  WS: %5.1f MB\n",
            frame.frameIndex,
            frame.fps,
            frame.frameTimeMs,
            frame.cpuPercent,
            static_cast<double>(frame.workingSet) / (1024.0 * 1024.0));
    }
}

void OnStatus(const std::string& msg) {
    printf("  [STATUS] %s\n", msg.c_str());
}

void OnStats(const ProcessStats& st) {
    // No-op for demo; frame callback covers what we need
    (void)st;
}

// ─── Demo Sections ────────────────────────────────────────────────────────────

void DemoEnumerate(ProcessProfiler& pp) {
    printf("\n▸ 1. EnumerateProcesses() — Top 15 by memory:\n");
    PrintSeparator();

    auto processes = pp.EnumerateProcesses(L"", 15);
    for (const auto& p : processes) {
        PrintProcessInfo(p);
    }
    printf("  (%zu total shown)\n", processes.size());
}

void DemoFind(ProcessProfiler& pp) {
    printf("\n▸ 2. FindProcess() — search common game processes:\n");
    PrintSeparator();

    const wchar_t* candidates[] = { L"steam", L"epic", L"chrome", L"explorer", L"code" };
    for (auto name : candidates) {
        auto result = pp.FindProcess(name);
        if (result) {
            printf("  Found \"%ls\": ", name);
            PrintProcessInfo(*result);
        } else {
            printf("  \"%ls\" — no match or multiple matches\n", name);
        }
    }
}

void DemoFilter(ProcessProfiler& pp) {
    printf("\n▸ 3. EnumerateProcesses(filter) — processes matching \"svchost\":\n");
    PrintSeparator();

    auto svchost = pp.EnumerateProcesses(L"svchost", 8);
    for (const auto& p : svchost) {
        PrintProcessInfo(p);
    }
}

void DemoAttachAndSample(ProcessProfiler& pp, DWORD pid) {
    printf("\n▸ 4. Attach & Sample — PID %lu for 5 seconds:\n", pid);
    PrintSeparator();

    pp.SetFrameCallback(OnFrame);
    pp.SetStatusCallback(OnStatus);
    pp.SetStatsCallback(OnStats);

    AttachResult ar = pp.Attach(pid, SamplingMethod::PDH_Processor);
    if (ar != AttachResult::Success) {
        printf("  [FAIL] Attach returned %d\n", static_cast<int>(ar));
        return;
    }

    pp.StartSampling();

    // Sample for 5 seconds
    printf("  Sampling for 5 seconds... (printing every ~0.5s)\n\n");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    pp.StopSampling();

    // Pull recent data
    printf("\n▸ 5. GetRecentFrames() — last 10 samples:\n");
    PrintSeparator();

    auto recent = pp.GetRecentFrames(10);
    printf("  Retrieved %zu samples (newest first):\n", recent.size());
    for (const auto& f : recent) {
        printf("  [#%-5lld] FPS: %6.1f  Frame: %6.2f ms  CPU: %5.1f%%\n",
            f.frameIndex, f.fps, f.frameTimeMs, f.cpuPercent);
    }

    pp.Detach();
    printf("\n  Detached. Total samples collected: %d\n", gSampleCount);
}

void DemoGetProcessInfo(ProcessProfiler& pp, DWORD pid) {
    printf("\n▸ 6. GetProcessInfo(%lu):\n", pid);
    PrintSeparator();

    auto info = pp.GetProcessInfo(pid);
    if (info) {
        printf("  Name:           %ls\n", info->name.c_str());
        printf("  PID:            %lu\n", info->processId);
        printf("  Path:           %ls\n", info->executablePath.c_str());
        printf("  Architecture:   %s\n", info->is64Bit ? "64-bit" : "32-bit");
        printf("  Working Set:    %.1f MB\n",
            static_cast<double>(info->workingSetSize) / (1024.0 * 1024.0));
    } else {
        printf("  Process not found\n");
    }
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║         ProcessProfiler — Interactive Demo                ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    ProcessProfiler pp;

    // ── Section 1-3: Process discovery (no attach needed) ──
    DemoEnumerate(pp);
    DemoFind(pp);
    DemoFilter(pp);

    // ── Section 4-6: Attach + sample ──
    DWORD targetPid = 0;

    if (argc > 1) {
        targetPid = static_cast<DWORD>(atoi(argv[1]));
    } else {
        // Auto-pick: find the process with the largest working set (likely a game)
        auto procs = pp.EnumerateProcesses(L"", 1);
        if (!procs.empty()) {
            targetPid = procs[0].processId;
            printf("\n  Auto-selected highest-memory process: \"%ls\" (PID %lu)\n",
                procs[0].name.c_str(), procs[0].processId);
        }
    }

    if (targetPid == 0) {
        printf("\n  No target process. Pass PID as argument: demo.exe <PID>\n");
        return 1;
    }

    DemoGetProcessInfo(pp, targetPid);
    DemoAttachAndSample(pp, targetPid);

    printf("\n✓ Demo complete.\n");
    return 0;
}
