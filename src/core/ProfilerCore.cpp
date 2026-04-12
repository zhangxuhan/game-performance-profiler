#include "ProfilerCore.h"
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
}

ProfilerCore::~ProfilerCore() {
    StopSampling();
}

void ProfilerCore::StartSampling() {
    if (m_isRunning) return;
    
    m_isRunning = true;
    m_frameHistory.clear();
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
    
    ss << "]}";
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

} // namespace ProfilerCore
