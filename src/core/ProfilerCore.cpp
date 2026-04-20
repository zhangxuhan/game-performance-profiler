#include "ProfilerCore.h"
#include "StatisticsAnalyzer.h"
#include "AlertManager.h"
#include "GPUProfiler.h"
#include "MemoryAnalyzer.h"
#include "NetworkProfiler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace ProfilerCore {

ProfilerCore& ProfilerCore::GetInstance() {
    static ProfilerCore instance;
    return instance;
}

ProfilerCore::ProfilerCore() {
    m_currentFrame = 0;
    m_totalAllocated = 0;
    m_totalFreed = 0;
    m_allocationCount = 0;
    m_analyzer = std::make_unique<StatisticsAnalyzer>();
    m_alertManager = std::make_unique<AlertManager>();
    m_gpuProfiler = std::make_unique<GPUProfiler>();
    m_gpuProfilerEnabled = true;
    m_memoryAnalyzer = std::make_unique<MemoryAnalyzer>();
    m_networkProfiler = std::make_unique<NetworkProfiler>();
    
    // Wire alert manager to analyzer output
    m_alertManager->SetOnAlertGenerated([](const Alert& alert) {
        std::cout << "[Profiler Alert] " << alert.message << std::endl;
    });
    
    // Wire memory analyzer leak detection
    m_memoryAnalyzer->SetLeakCallback([](const MemoryLeak& leak) {
        std::cout << "[Memory Leak] " << leak.size << " bytes in category '"
                  << (leak.tag.empty() ? "untagged" : leak.tag) << "' (age: " 
                  << static_cast<int>(leak.ageMs / 1000.0) << "s)" << std::endl;
    });
    
    // Wire memory pressure callback
    m_memoryAnalyzer->SetPressureCallback([](MemoryPressure pressure) {
        const char* level = "None";
        switch (pressure) {
            case MemoryPressure::Low: level = "Low"; break;
            case MemoryPressure::Medium: level = "Medium"; break;
            case MemoryPressure::High: level = "High"; break;
            case MemoryPressure::Critical: level = "Critical"; break;
            default: break;
        }
        std::cout << "[Memory Pressure] Level: " << level << std::endl;
    });
}

ProfilerCore::~ProfilerCore() {
    StopSampling();
}

void ProfilerCore::StartSampling() {
    if (m_isRunning) return;
    
    m_isRunning = true;
    m_frameHistory.clear();
    m_analyzer->Reset();
    if (m_gpuProfiler && m_gpuProfilerEnabled) {
        m_gpuProfiler->Reset();
    }
    std::cout << "[Profiler] Sampling started" << std::endl;
}

void ProfilerCore::StopSampling() {
    if (!m_isRunning) return;
    
    m_isRunning = false;
    std::cout << "[Profiler] Sampling stopped. Collected " 
              << m_frameHistory.size() << " frames" << std::endl;
}

void ProfilerCore::BeginFrame() {
    m_currentFrame++;
    m_frameStartTime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    m_currentProfiles.clear();
    
    // Notify memory analyzer of frame start
    if (m_memoryAnalyzer) {
        m_memoryAnalyzer->BeginFrame(m_currentFrame);
    }
}

void ProfilerCore::EndFrame() {
    if (!m_isRunning) return;
    
    int64_t frameEndTime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    int64_t frameDuration = frameEndTime - m_frameStartTime;
    float fps = 1000000.0f / static_cast<float>(frameDuration);
    float frameTimeMs = frameDuration / 1000.0f;
    
    FrameData frame;
    frame.frameNumber = m_currentFrame;
    frame.timestamp = m_frameStartTime;
    frame.fps = fps;
    frame.frameTime = frameTimeMs;
    frame.profiles = m_currentProfiles;
    frame.memory = GetMemorySnapshot();
    
    m_frameHistory.push_back(frame);
    
    // Keep only last 1000 frames in memory
    if (m_frameHistory.size() > 1000) {
        m_frameHistory.erase(m_frameHistory.begin());
    }
    
    // Feed data to statistics analyzer
    if (m_analyzer) {
        m_analyzer->RecordFrame(static_cast<double>(fps), static_cast<double>(frameTimeMs),
                               frame.memory.currentUsage);
    }
    
    // Feed data to alert manager for real-time alerts
    if (m_alertManager && m_analyzer) {
        SummaryStats stats = m_analyzer->GetSummary();
        m_alertManager->ProcessFrame(
            static_cast<double>(fps), 
            static_cast<double>(frameTimeMs),
            frame.memory.currentUsage,
            stats.avgFps,
            stats.stabilityScore
        );
    }
    
    // Update GPU profiler with CPU frame time for correlation
    if (m_gpuProfiler && m_gpuProfilerEnabled) {
        m_gpuProfiler->RecordCPUGPUCorrelation(frameTimeMs, frameDuration);
    }
    
    // Feed data to memory analyzer for frame-based tracking
    if (m_memoryAnalyzer) {
        m_memoryAnalyzer->EndFrame();
    }
    
    // Send data to server if callback is set
    if (m_dataCallback) {
        SendDataToServer();
    }
}

void ProfilerCore::BeginFunction(const std::string& name) {
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    m_functionStack.push_back({name, now});
}

void ProfilerCore::EndFunction(const std::string& name) {
    if (m_functionStack.empty()) return;
    
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    // Find matching function
    auto it = std::find_if(m_functionStack.rbegin(), m_functionStack.rend(),
        [&name](const FunctionStackEntry& entry) {
            return entry.name == name;
        });
    
    if (it != m_functionStack.rend()) {
        ProfileData profile;
        profile.functionName = name;
        profile.startTime = it->startTime;
        profile.endTime = now;
        profile.duration = now - it->startTime;
        profile.callCount = 1;
        
        m_currentProfiles.push_back(profile);
    }
}

void ProfilerCore::TrackAllocation(size_t size) {
    m_totalAllocated += size;
    m_allocationCount++;
}

void ProfilerCore::TrackDeallocation(size_t size) {
    m_totalFreed += size;
}

MemorySnapshot ProfilerCore::GetMemorySnapshot() const {
    MemorySnapshot snapshot;
    snapshot.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    snapshot.totalAllocated = m_totalAllocated;
    snapshot.totalFreed = m_totalFreed;
    snapshot.currentUsage = m_totalAllocated - m_totalFreed;
    snapshot.allocationCount = m_allocationCount;
    return snapshot;
}

FrameData ProfilerCore::GetCurrentFrame() const {
    if (m_frameHistory.empty()) {
        return FrameData();
    }
    return m_frameHistory.back();
}

void ProfilerCore::SetDataCallback(DataCallback callback) {
    m_dataCallback = callback;
}

void ProfilerCore::SetAnalyzerWindowSize(size_t windowSize) {
    if (m_analyzer) {
        m_analyzer->SetWindowSize(windowSize);
    }
}

void ProfilerCore::SetAnalyzerThresholds(const struct AlertThresholds& thresholds) {
    if (m_analyzer) {
        m_analyzer->SetThresholds(thresholds);
    }
}

void ProfilerCore::SendDataToServer() {
    if (!m_dataCallback || m_frameHistory.empty()) return;
    
    std::string json = ExportToJSON();
    m_dataCallback(json);
}

std::string ProfilerCore::ExportToJSON() const {
    std::ostringstream ss;
    ss << "{\"frames\":[";
    
    for (size_t i = 0; i < m_frameHistory.size(); i++) {
        const auto& frame = m_frameHistory[i];
        ss << "{\"frame\":" << frame.frameNumber
           << ",\"fps\":" << std::fixed << std::setprecision(2) << frame.fps
           << ",\"frameTime\":" << frame.frameTime
           << ",\"memory\":" << frame.memory.currentUsage
           << ",\"profiles\":[";
        
        for (size_t j = 0; j < frame.profiles.size(); j++) {
            const auto& p = frame.profiles[j];
            ss << "{\"name\":\"" << p.functionName << "\","
               << "\"duration\":" << p.duration << "}";
            if (j < frame.profiles.size() - 1) ss << ",";
        }
        
        ss << "]}";
        if (i < m_frameHistory.size() - 1) ss << ",";
    }
    
    ss << "]";
    
    // Append statistics if analyzer is available
    if (m_analyzer) {
        ss << ",\"statistics\":" << m_analyzer->ExportToJSON();
    }
    
    // Append alerts if alert manager is available
    if (m_alertManager) {
        ss << ",\"alerts\":" << m_alertManager->ExportActiveToJSON();
    }
    
    // Append GPU profiling data if available
    if (m_gpuProfiler && m_gpuProfilerEnabled) {
        ss << ",\"gpu\":" << m_gpuProfiler->ExportToJSON();
    }
    
    // Append memory analysis if available
    if (m_memoryAnalyzer) {
        ss << ",\"memory\":" << m_memoryAnalyzer->ExportToJSON();
    }
    
    ss << "}";
    return ss.str();
}

std::string ProfilerCore::ExportToCSV() const {
    std::ostringstream ss;
    ss << "Frame,FPS,FrameTime(ms),Function,Duration(us)\n";
    
    for (const auto& frame : m_frameHistory) {
        for (const auto& profile : frame.profiles) {
            ss << frame.frameNumber << ","
               << std::fixed << std::setprecision(2) << frame.fps << ","
               << frame.frameTime << ","
               << profile.functionName << ","
               << profile.duration << "\n";
        }
    }
    
    return ss.str();
}

void ProfilerCore::SetAlertConfig(const struct AlertConfig& config) {
    if (m_alertManager) {
        m_alertManager->SetConfig(config);
    }
}

const std::vector<Alert>& ProfilerCore::GetActiveAlerts() const {
    static const std::vector<Alert> empty;
    if (!m_alertManager) return empty;
    // Note: This returns a reference to internal state; in production,
    // you'd return a copy. For performance, we use GetAlertManager() directly.
    return m_alertManager->GetAllAlerts();
}

bool ProfilerCore::AcknowledgeAlert(int alertId) {
    if (m_alertManager) {
        return m_alertManager->AcknowledgeAlert(alertId);
    }
    return false;
}

bool ProfilerCore::AcknowledgeAllAlerts() {
    if (m_alertManager) {
        return m_alertManager->AcknowledgeAllAlerts();
    }
    return false;
}

void ProfilerCore::SetGPUProfilerEnabled(bool enabled) {
    m_gpuProfilerEnabled = enabled;
    if (m_gpuProfiler) {
        if (enabled) {
            // Reset GPU profiler when enabling
            m_gpuProfiler->Reset();
        }
    }
}

void ProfilerCore::SetNetworkProfilerEnabled(bool enabled) {
    if (m_networkProfiler) {
        m_networkProfiler->SetEnabled(enabled);
    }
}

bool ProfilerCore::IsNetworkProfilerEnabled() const {
    if (m_networkProfiler) {
        return m_networkProfiler->IsEnabled();
    }
    return false;
}

void ProfilerCore::RecordCPUGPUFrame(double cpuTimeMs, double gpuTimeUs) {
    if (m_gpuProfiler && m_gpuProfilerEnabled) {
        m_gpuProfiler->RecordCPUGPUCorrelation(cpuTimeMs, gpuTimeUs);
    }
}

std::string ProfilerCore::GetCurrentBottleneck() const {
    if (m_gpuProfiler && m_gpuProfilerEnabled) {
        return m_gpuProfiler->GetCurrentBottleneck();
    }
    return "Unknown";
}

// ─── Memory Analysis Integration ─────────────────────────────────────────────

int64_t ProfilerCore::TrackMemoryAllocation(size_t size, MemoryCategory category,
                                             const std::string& tag,
                                             const char* file, int line) {
    if (m_memoryAnalyzer) {
        return m_memoryAnalyzer->TrackAllocation(size, category, tag, file, line);
    }
    return 0;
}

void ProfilerCore::TrackMemoryDeallocation(int64_t allocationId) {
    if (m_memoryAnalyzer) {
        m_memoryAnalyzer->TrackDeallocation(allocationId);
    }
}

MemoryReport ProfilerCore::GetMemoryReport() const {
    if (m_memoryAnalyzer) {
        return m_memoryAnalyzer->GenerateReport();
    }
    return MemoryReport();
}

std::vector<MemoryLeak> ProfilerCore::GetDetectedLeaks() const {
    if (m_memoryAnalyzer) {
        return m_memoryAnalyzer->DetectLeaks();
    }
    return std::vector<MemoryLeak>();
}

size_t ProfilerCore::GetCurrentMemoryUsage() const {
    if (m_memoryAnalyzer) {
        return m_memoryAnalyzer->GetCurrentUsage();
    }
    return 0;
}

} // namespace ProfilerCore
