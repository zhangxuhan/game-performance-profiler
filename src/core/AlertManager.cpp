#include "AlertManager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cmath>

namespace ProfilerCore {

AlertManager::AlertManager() {
    Reset();
}

AlertManager::~AlertManager() {
}

void AlertManager::SetConfig(const AlertConfig& config) {
    m_config = config;
}

void AlertManager::Reset() {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    
    m_alerts.clear();
    m_fpsHistory.clear();
    m_frameTimeHistory.clear();
    m_memoryHistory.clear();
    m_lastAlertTime.clear();
    m_sustainedLowFpsCounter = 0;
    m_thermalThrottlingActive = false;
    m_memoryGrowthRate = 0.0;
    m_memoryLeakAlertTime = 0;
    m_nextAlertId = 1;
    
    m_stats = AlertStats();
}

void AlertManager::ProcessFrame(double fps, double frameTimeMs, size_t memoryUsage,
                                double avgFps, double stabilityScore) {
    // Update history
    m_fpsHistory.push_back(fps);
    m_frameTimeHistory.push_back(frameTimeMs);
    m_memoryHistory.push_back(memoryUsage);
    
    if (m_fpsHistory.size() > m_historyWindowSize) {
        m_fpsHistory.erase(m_fpsHistory.begin());
        m_frameTimeHistory.erase(m_frameTimeHistory.begin());
        m_memoryHistory.erase(m_memoryHistory.begin());
    }
    
    // Calculate average frame time from history
    double avgFrameTime = 0.0;
    if (!m_frameTimeHistory.empty()) {
        avgFrameTime = std::accumulate(m_frameTimeHistory.begin(), 
                                       m_frameTimeHistory.end(), 0.0) / 
                       m_frameTimeHistory.size();
    }
    
    // Run all checks
    CheckFpsDrop(fps, avgFps);
    CheckFrameTimeSpike(frameTimeMs, avgFrameTime);
    CheckMemoryLeak(memoryUsage);
    CheckHighMemoryUsage(memoryUsage);
    CheckStability(stabilityScore);
    CheckThermalThrottling(fps);
}

void AlertManager::CheckFpsDrop(double fps, double avgFps) {
    if (m_fpsHistory.size() < 10) return;
    
    double drop = avgFps - fps;
    if (drop < m_config.fpsDropThreshold) return;
    
    AlertSeverity severity = AlertSeverity::Warning;
    if (fps < m_config.fpsCriticalThreshold) {
        severity = AlertSeverity::Critical;
    } else if (fps < m_config.fpsWarningThreshold) {
        severity = AlertSeverity::Warning;
    } else {
        severity = AlertSeverity::Info;
    }
    
    if (ShouldDeduplicate(AlertType::FPS_DROP, severity)) return;
    
    std::ostringstream msg;
    msg << "FPS dropped by " << std::fixed << std::setprecision(1) 
        << drop << " to " << fps;
    
    std::ostringstream details;
    details << "Average FPS: " << avgFps << ", Current FPS: " << fps 
            << ", Drop: " << drop;
    
    Alert alert = CreateAlert(AlertType::FPS_DROP, severity, msg.str(), 
                              details.str(), "fps", fps, avgFps);
    AddAlert(alert);
}

void AlertManager::CheckFrameTimeSpike(double frameTimeMs, double avgFrameTime) {
    if (avgFrameTime <= 0 || m_frameTimeHistory.size() < 10) return;
    
    double multiplier = frameTimeMs / avgFrameTime;
    if (multiplier < m_config.frameTimeSpikeMultiplier) return;
    
    AlertSeverity severity = AlertSeverity::Warning;
    if (frameTimeMs > m_config.frameTimeCriticalMs) {
        severity = AlertSeverity::Critical;
    }
    
    if (ShouldDeduplicate(AlertType::FRAME_TIME_SPIKE, severity)) return;
    
    std::ostringstream msg;
    msg << "Frame time spike detected: " << std::fixed << std::setprecision(2) 
        << frameTimeMs << "ms (" << std::setprecision(1) << multiplier 
        << "x average)";
    
    std::ostringstream details;
    details << "Frame time increased from average " << avgFrameTime 
            << "ms to " << frameTimeMs << "ms";
    
    Alert alert = CreateAlert(AlertType::FRAME_TIME_SPIKE, severity, msg.str(),
                              details.str(), "frameTimeMs", frameTimeMs, avgFrameTime);
    AddAlert(alert);
}

void AlertManager::CheckMemoryLeak(size_t memoryUsage) {
    if (m_memoryHistory.size() < 60) return;  // Need at least 1 second of data
    
    // Calculate memory growth rate using linear regression
    size_t n = m_memoryHistory.size();
    double xMean = static_cast<double>(n - 1) / 2.0;
    double yMean = std::accumulate(m_memoryHistory.begin(), m_memoryHistory.end(), 0.0) / n;
    
    double numerator = 0.0;
    double denominator = 0.0;
    for (size_t i = 0; i < n; i++) {
        double xDiff = static_cast<double>(i) - xMean;
        double yDiff = static_cast<double>(m_memoryHistory[i]) - yMean;
        numerator += xDiff * yDiff;
        denominator += xDiff * xDiff;
    }
    
    m_memoryGrowthRate = (denominator > 0) ? (numerator / denominator) : 0.0;
    
    if (m_memoryGrowthRate < m_config.memoryGrowthRateThreshold) return;
    
    // Check if we already have an active memory leak alert
    int64_t now = GetCurrentTimestamp();
    if (now - m_memoryLeakAlertTime < m_config.deduplicationWindowMs * 6) return;  // 30s cooldown
    
    AlertSeverity severity = AlertSeverity::Warning;
    if (m_memoryGrowthRate > m_config.memoryGrowthRateThreshold * 5) {
        severity = AlertSeverity::Critical;
    }
    
    std::ostringstream msg;
    msg << "Potential memory leak detected: +" << std::fixed << std::setprecision(2)
        << (m_memoryGrowthRate / 1024.0 / 1024.0) << " MB/frame growth";
    
    std::ostringstream details;
    details << "Memory usage growing at " << m_memoryGrowthRate 
            << " bytes/frame. Current usage: " << (memoryUsage / 1024 / 1024) << " MB";
    
    Alert alert = CreateAlert(AlertType::MEMORY_LEAK, severity, msg.str(),
                              details.str(), "memoryGrowthRate", m_memoryGrowthRate, 
                              m_config.memoryGrowthRateThreshold);
    AddAlert(alert);
    m_memoryLeakAlertTime = now;
}

void AlertManager::CheckHighMemoryUsage(size_t memoryUsage) {
    if (memoryUsage < m_config.memoryWarningThreshold) return;
    
    AlertSeverity severity = AlertSeverity::Warning;
    if (memoryUsage > m_config.memoryCriticalThreshold) {
        severity = AlertSeverity::Critical;
    }
    
    if (ShouldDeduplicate(AlertType::HIGH_MEMORY_USAGE, severity)) return;
    
    double usageMB = memoryUsage / 1024.0 / 1024.0;
    double thresholdMB = (severity == AlertSeverity::Critical) ? 
                         (m_config.memoryCriticalThreshold / 1024.0 / 1024.0) :
                         (m_config.memoryWarningThreshold / 1024.0 / 1024.0);
    
    std::ostringstream msg;
    msg << "High memory usage: " << std::fixed << std::setprecision(1) 
        << usageMB << " MB";
    
    std::ostringstream details;
    details << "Memory usage (" << usageMB << " MB) exceeds threshold (" 
            << thresholdMB << " MB)";
    
    Alert alert = CreateAlert(AlertType::HIGH_MEMORY_USAGE, severity, msg.str(),
                              details.str(), "memoryUsage", usageMB, thresholdMB);
    AddAlert(alert);
}

void AlertManager::CheckStability(double stabilityScore) {
    if (stabilityScore > m_config.stabilityWarningThreshold) return;
    
    AlertSeverity severity = AlertSeverity::Warning;
    if (stabilityScore < m_config.stabilityCriticalThreshold) {
        severity = AlertSeverity::Critical;
    }
    
    if (ShouldDeduplicate(AlertType::STABILITY_ISSUE, severity)) return;
    
    std::ostringstream msg;
    msg << "Frame rate instability detected: " << std::fixed << std::setprecision(1) 
        << stabilityScore << "% stability";
    
    std::ostringstream details;
    details << "Stability score " << stabilityScore << "% is below threshold. "
            << "This may indicate GC pressure, threading issues, or background processes.";
    
    Alert alert = CreateAlert(AlertType::STABILITY_ISSUE, severity, msg.str(),
                              details.str(), "stabilityScore", stabilityScore,
                              m_config.stabilityWarningThreshold);
    AddAlert(alert);
}

void AlertManager::CheckThermalThrottling(double fps) {
    if (fps < m_config.sustainedLowFpsThreshold) {
        m_sustainedLowFpsCounter++;
    } else {
        if (m_thermalThrottlingActive && m_config.autoAcknowledgeResolved) {
            // Thermal throttling resolved
            m_thermalThrottlingActive = false;
        }
        m_sustainedLowFpsCounter = std::max(0, m_sustainedLowFpsCounter - 1);
    }
    
    if (m_sustainedLowFpsCounter >= m_config.sustainedLowFpsFrames && !m_thermalThrottlingActive) {
        m_thermalThrottlingActive = true;
        
        if (ShouldDeduplicate(AlertType::THERMAL_THROTTLING, AlertSeverity::Warning)) return;
        
        double durationSec = m_config.sustainedLowFpsFrames / 60.0;
        
        std::ostringstream msg;
        msg << "Potential thermal throttling detected: sustained low FPS for " 
            << std::fixed << std::setprecision(1) << durationSec << "s";
        
        std::ostringstream details;
        details << "FPS has been below " << m_config.sustainedLowFpsThreshold 
                << " for " << m_sustainedLowFpsCounter << " frames. "
                << "This pattern suggests thermal throttling or power management.";
        
        Alert alert = CreateAlert(AlertType::THERMAL_THROTTLING, AlertSeverity::Warning, 
                                  msg.str(), details.str(), "sustainedLowFps", 
                                  fps, m_config.sustainedLowFpsThreshold);
        AddAlert(alert);
    }
}

Alert AlertManager::CreateAlert(AlertType type, AlertSeverity severity,
                                const std::string& message, const std::string& details,
                                const std::string& metric, double value, double threshold) {
    Alert alert;
    alert.id = GenerateAlertId();
    alert.type = type;
    alert.severity = severity;
    alert.message = message;
    alert.details = details;
    alert.timestamp = GetCurrentTimestamp();
    alert.metric = metric;
    alert.value = value;
    alert.threshold = threshold;
    alert.acknowledged = false;
    alert.acknowledgedAt = 0;
    alert.occurrenceCount = 1;
    alert.firstOccurrence = alert.timestamp;
    alert.lastOccurrence = alert.timestamp;
    return alert;
}

void AlertManager::AddAlert(const Alert& alert) {
    {
        std::lock_guard<std::mutex> lock(m_alertsMutex);
        m_alerts.push_back(alert);
        m_lastAlertTime[alert.type] = alert.timestamp;
        TrimAlertHistory();
    }
    
    UpdateStats();
    NotifyAlertGenerated(alert);
}

bool AlertManager::ShouldDeduplicate(AlertType type, AlertSeverity severity) const {
    auto it = m_lastAlertTime.find(type);
    if (it == m_lastAlertTime.end()) return false;
    
    int64_t now = GetCurrentTimestamp();
    return (now - it->second) < m_config.deduplicationWindowMs;
}

std::vector<Alert> AlertManager::GetActiveAlerts() const {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    std::vector<Alert> active;
    
    int64_t cutoff = GetCurrentTimestamp() - 30000;  // Active within last 30 seconds
    
    for (const auto& alert : m_alerts) {
        if (!alert.acknowledged && alert.timestamp > cutoff) {
            active.push_back(alert);
        }
    }
    
    return active;
}

std::vector<Alert> AlertManager::GetAlertsBySeverity(AlertSeverity severity) const {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    std::vector<Alert> result;
    
    for (const auto& alert : m_alerts) {
        if (alert.severity == severity) {
            result.push_back(alert);
        }
    }
    
    return result;
}

std::vector<Alert> AlertManager::GetAlertsByType(AlertType type) const {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    std::vector<Alert> result;
    
    for (const auto& alert : m_alerts) {
        if (alert.type == type) {
            result.push_back(alert);
        }
    }
    
    return result;
}

std::vector<Alert> AlertManager::GetUnacknowledgedAlerts() const {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    std::vector<Alert> result;
    
    for (const auto& alert : m_alerts) {
        if (!alert.acknowledged) {
            result.push_back(alert);
        }
    }
    
    return result;
}

bool AlertManager::AcknowledgeAlert(int alertId) {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    
    for (auto& alert : m_alerts) {
        if (alert.id == alertId && !alert.acknowledged) {
            alert.acknowledged = true;
            alert.acknowledgedAt = GetCurrentTimestamp();
            
            UpdateStats();
            
            std::lock_guard<std::mutex> cbLock(m_callbackMutex);
            if (m_onAlertAcknowledged) {
                m_onAlertAcknowledged(alert);
            }
            return true;
        }
    }
    
    return false;
}

bool AlertManager::AcknowledgeAllAlerts() {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    bool anyAcknowledged = false;
    
    for (auto& alert : m_alerts) {
        if (!alert.acknowledged) {
            alert.acknowledged = true;
            alert.acknowledgedAt = GetCurrentTimestamp();
            anyAcknowledged = true;
            
            std::lock_guard<std::mutex> cbLock(m_callbackMutex);
            if (m_onAlertAcknowledged) {
                m_onAlertAcknowledged(alert);
            }
        }
    }
    
    if (anyAcknowledged) {
        UpdateStats();
    }
    
    return anyAcknowledged;
}

bool AlertManager::DismissAlert(int alertId) {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    
    auto it = std::remove_if(m_alerts.begin(), m_alerts.end(),
        [alertId](const Alert& alert) { return alert.id == alertId; });
    
    if (it != m_alerts.end()) {
        m_alerts.erase(it, m_alerts.end());
        UpdateStats();
        return true;
    }
    
    return false;
}

void AlertManager::ClearAllAlerts() {
    {
        std::lock_guard<std::mutex> lock(m_alertsMutex);
        m_alerts.clear();
        m_lastAlertTime.clear();
    }
    
    UpdateStats();
}

AlertStats AlertManager::GetStats() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}

void AlertManager::UpdateStats() {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    
    m_stats.totalAlerts = static_cast<int>(m_alerts.size());
    m_stats.infoCount = 0;
    m_stats.warningCount = 0;
    m_stats.criticalCount = 0;
    m_stats.unacknowledgedCount = 0;
    m_stats.activeAlertTypes.clear();
    
    for (const auto& alert : m_alerts) {
        switch (alert.severity) {
            case AlertSeverity::Info: m_stats.infoCount++; break;
            case AlertSeverity::Warning: m_stats.warningCount++; break;
            case AlertSeverity::Critical: m_stats.criticalCount++; break;
        }
        
        if (!alert.acknowledged) {
            m_stats.unacknowledgedCount++;
            m_stats.activeAlertTypes.push_back(alert.type);
        }
        
        if (alert.timestamp > m_stats.lastAlertTime) {
            m_stats.lastAlertTime = alert.timestamp;
        }
    }
}

int AlertManager::GetActiveAlertCount() const {
    return static_cast<int>(GetActiveAlerts().size());
}

int AlertManager::GetUnacknowledgedCount() const {
    return GetStats().unacknowledgedCount;
}

bool AlertManager::HasCriticalAlerts() const {
    return !GetAlertsBySeverity(AlertSeverity::Critical).empty();
}

bool AlertManager::HasUnacknowledgedAlerts() const {
    return GetUnacknowledgedCount() > 0;
}

void AlertManager::SetOnAlertGenerated(AlertCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onAlertGenerated = callback;
}

void AlertManager::SetOnAlertAcknowledged(AlertCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onAlertAcknowledged = callback;
}

void AlertManager::SetOnAlertResolved(AlertCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onAlertResolved = callback;
}

void AlertManager::NotifyAlertGenerated(const Alert& alert) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_onAlertGenerated) {
        m_onAlertGenerated(alert);
    }
}

void AlertManager::NotifyAlertResolved(const Alert& alert) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_onAlertResolved) {
        m_onAlertResolved(alert);
    }
}

void AlertManager::TrimAlertHistory() {
    while (m_alerts.size() > static_cast<size_t>(m_config.maxAlertsInHistory)) {
        m_alerts.erase(m_alerts.begin());
    }
}

int AlertManager::GenerateAlertId() {
    return m_nextAlertId++;
}

int64_t AlertManager::GetCurrentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

std::string AlertManager::ExportToJSON() const {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    std::ostringstream ss;
    
    ss << "{\"alerts\":[";
    for (size_t i = 0; i < m_alerts.size(); i++) {
        const auto& a = m_alerts[i];
        ss << "{";
        ss << "\"id\":" << a.id << ",";
        ss << "\"type\":" << static_cast<int>(a.type) << ",";
        ss << "\"severity\":" << static_cast<int>(a.severity) << ",";
        ss << "\"message\":\"" << a.message << "\",";
        ss << "\"details\":\"" << a.details << "\",";
        ss << "\"timestamp\":" << a.timestamp << ",";
        ss << "\"metric\":\"" << a.metric << "\",";
        ss << "\"value\":" << a.value << ",";
        ss << "\"threshold\":" << a.threshold << ",";
        ss << "\"acknowledged\":" << (a.acknowledged ? "true" : "false") << ",";
        ss << "\"acknowledgedAt\":" << a.acknowledgedAt << ",";
        ss << "\"occurrenceCount\":" << a.occurrenceCount;
        ss << "}";
        if (i < m_alerts.size() - 1) ss << ",";
    }
    ss << "]}";
    
    return ss.str();
}

std::string AlertManager::ExportActiveToJSON() const {
    auto active = GetActiveAlerts();
    std::ostringstream ss;
    
    ss << "{\"activeAlerts\":[";
    for (size_t i = 0; i < active.size(); i++) {
        const auto& a = active[i];
        ss << "{";
        ss << "\"id\":" << a.id << ",";
        ss << "\"type\":" << static_cast<int>(a.type) << ",";
        ss << "\"severity\":" << static_cast<int>(a.severity) << ",";
        ss << "\"message\":\"" << a.message << "\",";
        ss << "\"timestamp\":" << a.timestamp << ",";
        ss << "\"metric\":\"" << a.metric << "\",";
        ss << "\"value\":" << a.value;
        ss << "}";
        if (i < active.size() - 1) ss << ",";
    }
    ss << "]}";
    
    return ss.str();
}

std::string AlertManager::ExportStatsToJSON() const {
    AlertStats stats = GetStats();
    std::ostringstream ss;
    
    ss << "{";
    ss << "\"totalAlerts\":" << stats.totalAlerts << ",";
    ss << "\"infoCount\":" << stats.infoCount << ",";
    ss << "\"warningCount\":" << stats.warningCount << ",";
    ss << "\"criticalCount\":" << stats.criticalCount << ",";
    ss << "\"unacknowledgedCount\":" << stats.unacknowledgedCount << ",";
    ss << "\"lastAlertTime\":" << stats.lastAlertTime << ",";
    ss << "\"hasCritical\":" << (HasCriticalAlerts() ? "true" : "false") << ",";
    ss << "\"hasUnacknowledged\":" << (HasUnacknowledgedAlerts() ? "true" : "false");
    ss << "}";
    
    return ss.str();
}

bool AlertManager::SaveToFile(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << ExportToJSON();
    return true;
}

bool AlertManager::LoadFromFile(const std::string& filepath) {
    // Implementation would parse JSON and restore alerts
    // For now, just return false as this is optional
    return false;
}

} // namespace ProfilerCore
