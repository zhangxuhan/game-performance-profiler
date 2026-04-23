#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace ProfilerCore {

/**
 * Thermal alert severity levels
 */
enum class ThermalAlertLevel {
    Normal = 0,      // Temperature is within safe range
    Warning = 1,     // Approaching thermal limits
    Critical = 2,    // At or above thermal limits, throttling likely
    Emergency = 3    // Danger zone, immediate action required
};

/**
 * Thermal throttling type
 */
enum class ThrottleType {
    None = 0,
    CPU = 1,
    GPU = 2,
    Both = 3
};

/**
 * Temperature source/component
 */
enum class ThermalComponent {
    CPU = 0,
    GPU = 1,
    Memory = 2,
    VRM = 3,          // Voltage Regulator Module
    SOC = 4,          // System on Chip (for mobile/embedded)
    Ambient = 5,      // Ambient/case temperature
    Unknown = 99
};

/**
 * Single temperature reading
 */
struct TemperatureReading {
    int64_t timestamp;
    ThermalComponent component;
    double celsius;         // Temperature in Celsius
    double fahrenheit;     // Temperature in Fahrenheit
    bool isValid;          // Whether reading is valid
    std::string source;    // Source identifier (e.g., "CPU Core 0")
};

/**
 * Thermal state snapshot
 */
struct ThermalSnapshot {
    int64_t timestamp;
    int frameNumber;
    
    // CPU temperatures
    double cpuPackage;         // Package temperature
    double cpuCoreMax;         // Max core temperature
    double cpuCoreAvg;         // Average core temperature
    std::vector<double> cpuCoreTemps;  // Per-core temperatures
    
    // GPU temperatures
    double gpuCore;
    double gpuMemory;
    double gpuHotspot;
    double gpuJunction;       // For AMD cards
    
    // Other components
    double memoryTemp;
    double vrmTemp;
    double socTemp;
    double ambientTemp;
    
    // Throttling status
    ThrottleType throttleType;
    double cpuThrottlePercent;  // Estimated throttle percentage
    double gpuThrottlePercent;
    
    // Alert level
    ThermalAlertLevel alertLevel;
    bool isThermalEvent;        // True if significant thermal event
    
    // Fan speed (if available)
    int cpuFanRPM;
    int gpuFanRPM;
    int cpuFanPercent;
    int gpuFanPercent;
    
    std::string ToJSON() const;
};

/**
 * Thermal threshold configuration
 */
struct ThermalThresholds {
    // CPU thresholds (Celsius)
    double cpuWarningTemp = 80.0;
    double cpuCriticalTemp = 95.0;
    double cpuEmergencyTemp = 100.0;
    
    // GPU thresholds (Celsius)
    double gpuWarningTemp = 75.0;
    double gpuCriticalTemp = 85.0;
    double gpuEmergencyTemp = 95.0;
    
    // Memory thresholds
    double memoryWarningTemp = 85.0;
    double memoryCriticalTemp = 95.0;
    
    // VRM thresholds
    double vrmWarningTemp = 90.0;
    double vrmCriticalTemp = 105.0;
    
    // Ambient thresholds
    double ambientWarningTemp = 40.0;
    double ambientCriticalTemp = 50.0;
    
    // Throttle detection
    double cpuThrottleThreshold = 85.0;
    double gpuThrottleThreshold = 80.0;
    
    // Hysteresis to prevent alert flapping
    double hysteresisOffset = 2.0;
};

/**
 * Thermal history entry for trend analysis
 */
struct ThermalHistoryEntry {
    int64_t timestamp;
    int frameNumber;
    double cpuTemp;
    double gpuTemp;
    ThermalAlertLevel alertLevel;
    ThrottleType throttleType;
};

/**
 * Thermal statistics summary
 */
struct ThermalStatistics {
    int64_t timestamp;
    int sampleCount;
    int64_t durationMs;
    
    // CPU stats
    double cpuMin, cpuMax, cpuAvg, cpuStdDev;
    double cpuP50, cpuP90, cpuP95, cpuP99;
    double cpuTimeAboveWarning;  // Percentage of time above warning
    double cpuTimeAboveCritical; // Percentage of time above critical
    
    // GPU stats
    double gpuMin, gpuMax, gpuAvg, gpuStdDev;
    double gpuP50, gpuP90, gpuP95, gpuP99;
    double gpuTimeAboveWarning;
    double gpuTimeAboveCritical;
    
    // Throttling stats
    double throttleTimePercent;   // Percentage of time throttled
    int throttleEventCount;        // Number of throttle events
    double avgThrottleDurationMs;  // Average throttle duration
    
    // Thermal events
    int thermalEventCount;
    int warningCount;
    int criticalCount;
    int emergencyCount;
    
    std::string ToJSON() const;
};

/**
 * Thermal event record
 */
struct ThermalEvent {
    int64_t startTime;
    int64_t endTime;
    int startFrame;
    int endFrame;
    ThermalAlertLevel severity;
    ThermalComponent component;
    double peakTemp;
    double avgTemp;
    double durationMs;
    std::string description;
    bool acknowledged;
};

/**
 * Cooling recommendation
 */
struct CoolingRecommendation {
    std::string component;
    double currentTemp;
    double targetTemp;
    std::vector<std::string> actions;  // Suggested actions
    int priority;                       // 1-5, 5 being highest
    std::string reason;
};

/**
 * ThermalMonitor - Comprehensive thermal monitoring and analysis
 * 
 * Features:
 * - Real-time temperature monitoring (CPU, GPU, Memory, VRM, Ambient)
 * - Thermal throttling detection and quantification
 * - Historical temperature tracking with trend analysis
 * - Configurable thresholds with hysteresis
 * - Thermal event detection and recording
 * - Automatic cooling recommendations
 * - Integration with PerformanceScorer and AlertManager
 * - Multi-platform support (Windows, Linux, macOS)
 * 
 * Usage:
 *   ThermalMonitor monitor;
 *   monitor.Start();
 *   while (running) {
 *       ThermalSnapshot snapshot = monitor.GetSnapshot();
 *       if (snapshot.alertLevel >= ThermalAlertLevel::Warning) {
 *           // Handle thermal warning
 *       }
 *   }
 *   monitor.Stop();
 */
class ThermalMonitor {
public:
    ThermalMonitor();
    ~ThermalMonitor();
    
    // Lifecycle
    bool Start();
    void Stop();
    bool IsRunning() const { return m_isRunning; }
    
    // Configuration
    void SetThresholds(const ThermalThresholds& thresholds);
    const ThermalThresholds& GetThresholds() const { return m_thresholds; }
    void SetPollIntervalMs(int ms) { m_pollIntervalMs = ms; }
    int GetPollIntervalMs() const { return m_pollIntervalMs; }
    void SetHistorySize(size_t size) { m_maxHistorySize = size; }
    
    // Temperature reading
    ThermalSnapshot GetSnapshot() const;
    TemperatureReading ReadTemperature(ThermalComponent component) const;
    std::vector<TemperatureReading> ReadAllTemperatures() const;
    
    // Current state queries
    double GetCurrentCPUTemp() const;
    double GetCurrentGPUTemp() const;
    ThermalAlertLevel GetCurrentAlertLevel() const;
    ThrottleType GetCurrentThrottleType() const;
    bool IsThrottling() const;
    bool IsThermalEvent() const;
    
    // Statistics
    ThermalStatistics GetStatistics(int64_t durationMs = 0) const;
    ThermalStatistics GetStatisticsForFrames(int frameCount) const;
    
    // History
    const std::vector<ThermalHistoryEntry>& GetHistory() const { return m_history; }
    std::vector<ThermalHistoryEntry> GetRecentHistory(int count) const;
    std::vector<ThermalHistoryEntry> GetHistoryForDuration(int64_t durationMs) const;
    void ClearHistory();
    
    // Thermal events
    const std::vector<ThermalEvent>& GetThermalEvents() const { return m_thermalEvents; }
    std::vector<ThermalEvent> GetActiveThermalEvents() const;
    bool AcknowledgeThermalEvent(int64_t eventStartTime);
    void ClearAcknowledgedEvents();
    
    // Trend analysis
    bool IsTemperatureRising() const;
    bool IsTemperatureFalling() const;
    double GetTemperatureTrend(ThermalComponent component) const;  // Celsius per second
    double PredictTemperature(ThermalComponent component, int secondsAhead) const;
    
    // Recommendations
    std::vector<CoolingRecommendation> GetCoolingRecommendations() const;
    
    // Export
    std::string ExportToJSON() const;
    std::string ExportHistoryToJSON(int64_t durationMs = 0) const;
    std::string ExportEventsToJSON() const;
    
    // Integration
    void SetFrameNumber(int frame) { m_currentFrame = frame; }
    void SetSessionId(const std::string& id) { m_sessionId = id; }
    
    // Callbacks
    using ThermalCallback = std::function<void(const ThermalSnapshot&)>;
    using EventCallback = std::function<void(const ThermalEvent&)>;
    void SetThermalCallback(ThermalCallback callback);
    void SetEventCallback(EventCallback callback);
    
private:
    // Platform-specific implementation
    bool InitializePlatform();
    void ShutdownPlatform();
    bool ReadPlatformTemperatures(ThermalSnapshot& snapshot);
    
    // Processing
    void ProcessSnapshot(const ThermalSnapshot& snapshot);
    void UpdateAlertLevel(ThermalSnapshot& snapshot);
    void DetectThrottling(ThermalSnapshot& snapshot);
    void DetectThermalEvent(const ThermalSnapshot& snapshot);
    void AddToHistory(const ThermalSnapshot& snapshot);
    void TrimHistory();
    
    // Statistics helpers
    double ComputePercentile(const std::vector<double>& values, double percentile) const;
    double ComputeMean(const std::vector<double>& values) const;
    double ComputeStdDev(const std::vector<double>& values, double mean) const;
    
    bool m_isRunning = false;
    int m_pollIntervalMs = 100;  // Default 100ms polling
    size_t m_maxHistorySize = 10000;
    int m_currentFrame = 0;
    std::string m_sessionId;
    
    ThermalThresholds m_thresholds;
    ThermalSnapshot m_currentSnapshot;
    std::vector<ThermalHistoryEntry> m_history;
    std::vector<ThermalEvent> m_thermalEvents;
    
    // Current thermal event tracking
    std::unordered_map<ThermalComponent, ThermalEvent> m_activeEvents;
    
    // Callbacks
    ThermalCallback m_thermalCallback;
    EventCallback m_eventCallback;
    
    // Platform-specific data (opaque pointer)
    void* m_platformData = nullptr;
};

} // namespace ProfilerCore
