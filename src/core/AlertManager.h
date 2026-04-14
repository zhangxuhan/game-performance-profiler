#pragma once

#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

namespace ProfilerCore {

/**
 * Alert types for different performance issues
 */
enum class AlertType {
    FPS_DROP,           // Sudden FPS drop
    FRAME_TIME_SPIKE,   // Frame time spike
    MEMORY_LEAK,        // Potential memory leak
    HIGH_MEMORY_USAGE,  // Memory usage too high
    STABILITY_ISSUE,    // Frame rate instability
    THERMAL_THROTTLING  // Potential thermal throttling (sustained low perf)
};

/**
 * Alert severity levels
 */
enum class AlertSeverity {
    Info = 0,
    Warning = 1,
    Critical = 2
};

/**
 * A single performance alert
 */
struct Alert {
    int id;
    AlertType type;
    AlertSeverity severity;
    std::string message;
    std::string details;
    int64_t timestamp;
    std::string metric;
    double value;
    double threshold;
    bool acknowledged;
    int64_t acknowledgedAt;
    
    // For grouping similar alerts
    int occurrenceCount;
    int64_t firstOccurrence;
    int64_t lastOccurrence;
};

/**
 * Alert configuration thresholds
 */
struct AlertConfig {
    // FPS thresholds
    double fpsDropThreshold = 10.0;           // FPS drop of 10+ triggers alert
    double fpsCriticalThreshold = 30.0;       // Below 30 FPS is critical
    double fpsWarningThreshold = 45.0;        // Below 45 FPS is warning
    
    // Frame time thresholds (ms)
    double frameTimeSpikeMultiplier = 2.0;    // 2x average = spike
    double frameTimeCriticalMs = 33.33;       // ~30 FPS
    double frameTimeWarningMs = 22.22;        // ~45 FPS
    
    // Memory thresholds
    double memoryGrowthRateThreshold = 1.0 * 1024 * 1024;  // 1MB per frame
    size_t memoryCriticalThreshold = 512 * 1024 * 1024;    // 512MB
    size_t memoryWarningThreshold = 256 * 1024 * 1024;     // 256MB
    
    // Stability thresholds
    double stabilityCriticalThreshold = 50.0;  // Below 50% stability
    double stabilityWarningThreshold = 70.0;   // Below 70% stability
    
    // Thermal throttling detection
    int sustainedLowFpsFrames = 180;           // 3 seconds at 60 FPS
    double sustainedLowFpsThreshold = 35.0;    // Below 35 FPS sustained
    
    // Alert deduplication
    int deduplicationWindowMs = 5000;          // 5 seconds
    int maxAlertsInHistory = 100;              // Keep last 100 alerts
    bool autoAcknowledgeResolved = true;       // Auto-ack when issue resolves
};

/**
 * Alert statistics
 */
struct AlertStats {
    int totalAlerts = 0;
    int infoCount = 0;
    int warningCount = 0;
    int criticalCount = 0;
    int unacknowledgedCount = 0;
    int64_t lastAlertTime = 0;
    std::vector<AlertType> activeAlertTypes;
};

/**
 * AlertManager - Centralized performance alert management
 * 
 * Features:
 * - Real-time alert generation based on configurable thresholds
 * - Alert deduplication to prevent spam
 * - Alert acknowledgment system
 * - Alert history with persistence
 * - Callback system for real-time notifications
 * - Statistical summaries
 */
class AlertManager {
public:
    AlertManager();
    ~AlertManager();

    // Configuration
    void SetConfig(const AlertConfig& config);
    const AlertConfig& GetConfig() const { return m_config; }
    
    // Core alert processing
    void ProcessFrame(double fps, double frameTimeMs, size_t memoryUsage, 
                      double avgFps, double stabilityScore);
    void Reset();
    
    // Alert management
    const std::vector<Alert>& GetAllAlerts() const { return m_alerts; }
    std::vector<Alert> GetActiveAlerts() const;
    std::vector<Alert> GetAlertsBySeverity(AlertSeverity severity) const;
    std::vector<Alert> GetAlertsByType(AlertType type) const;
    std::vector<Alert> GetUnacknowledgedAlerts() const;
    
    bool AcknowledgeAlert(int alertId);
    bool AcknowledgeAllAlerts();
    bool DismissAlert(int alertId);
    void ClearAllAlerts();
    
    // Statistics
    AlertStats GetStats() const;
    int GetActiveAlertCount() const;
    int GetUnacknowledgedCount() const;
    bool HasCriticalAlerts() const;
    bool HasUnacknowledgedAlerts() const;
    
    // Callbacks
    using AlertCallback = std::function<void(const Alert&)>;
    void SetOnAlertGenerated(AlertCallback callback);
    void SetOnAlertAcknowledged(AlertCallback callback);
    void SetOnAlertResolved(AlertCallback callback);
    
    // Export
    std::string ExportToJSON() const;
    std::string ExportActiveToJSON() const;
    std::string ExportStatsToJSON() const;
    
    // Persistence (optional)
    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);

private:
    void CheckFpsDrop(double fps, double avgFps);
    void CheckFrameTimeSpike(double frameTimeMs, double avgFrameTime);
    void CheckMemoryLeak(size_t memoryUsage);
    void CheckHighMemoryUsage(size_t memoryUsage);
    void CheckStability(double stabilityScore);
    void CheckThermalThrottling(double fps);
    
    Alert CreateAlert(AlertType type, AlertSeverity severity, 
                      const std::string& message, const std::string& details,
                      const std::string& metric, double value, double threshold);
    void AddAlert(const Alert& alert);
    bool ShouldDeduplicate(AlertType type, AlertSeverity severity) const;
    void UpdateStats();
    void TrimAlertHistory();
    void NotifyAlertGenerated(const Alert& alert);
    void NotifyAlertResolved(const Alert& alert);
    
    int GenerateAlertId();
    int64_t GetCurrentTimestamp() const;

private:
    AlertConfig m_config;
    std::vector<Alert> m_alerts;
    mutable std::mutex m_alertsMutex;
    
    // Frame history for analysis
    std::vector<double> m_fpsHistory;
    std::vector<double> m_frameTimeHistory;
    std::vector<size_t> m_memoryHistory;
    size_t m_historyWindowSize = 120;  // 2 seconds at 60 FPS
    
    // Alert tracking for deduplication
    std::unordered_map<AlertType, int64_t> m_lastAlertTime;
    
    // Statistics
    AlertStats m_stats;
    mutable std::mutex m_statsMutex;
    
    // Callbacks
    AlertCallback m_onAlertGenerated;
    AlertCallback m_onAlertAcknowledged;
    AlertCallback m_onAlertResolved;
    mutable std::mutex m_callbackMutex;
    
    // ID generation
    std::atomic<int> m_nextAlertId{1};
    
    // Thermal throttling detection state
    int m_sustainedLowFpsCounter = 0;
    bool m_thermalThrottlingActive = false;
    
    // Memory leak detection state
    double m_memoryGrowthRate = 0.0;
    int64_t m_memoryLeakAlertTime = 0;
};

} // namespace ProfilerCore
