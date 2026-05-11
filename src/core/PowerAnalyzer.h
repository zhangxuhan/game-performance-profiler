#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <chrono>
#include <mutex>
#include <atomic>

namespace ProfilerCore {

// ─── Enumerations ─────────────────────────────────────────────────────────────

/**
 * Power source type
 */
enum class PowerSourceType {
    AC,              // Plugged into AC power
    Battery,         // Running on battery
    UPS,            // Running on UPS
    Unknown
};

/**
 * Battery status
 */
enum class BatteryStatus {
    Discharging,     // Battery being drained
    Charging,        // Battery being charged
    Idle,           // Not charging or discharging
    Critical,       // Very low battery
    Unknown
};

/**
 * Power plan / performance mode
 */
enum class PowerPlan {
    Balanced,
    HighPerformance,
    PowerSaver,
    GameMode,        // Windows Game Mode
    Custom,
    Unknown
};

/**
 * Power throttling status
 */
enum class PowerThrottleState {
    None,           // No throttling
    Light,          // Minor power reduction
    Moderate,       // Noticeable performance impact
    Severe,         // Significant performance reduction
    Critical        // Emergency power saving
};

/**
 * Power alert type
 */
enum class PowerAlertType {
    BatteryLow,           // Battery below warning threshold
    BatteryCritical,      // Battery critically low
    PowerSourceChanged,   // Switched between AC/battery
    HighDrainRate,        // Abnormally high power consumption
    ThermalThrottling,    // Power reduced due to heat
    EfficiencyDrop,       // Sudden efficiency decrease
    ChargingStalled,      // Battery not charging when expected
    UpsWarning           // UPS running low
};

// ─── Data Structures ──────────────────────────────────────────────────────────

/**
 * Battery information
 */
struct BatteryInfo {
    int64_t timestamp;

    // Basic info
    bool present;
    int index;                    // Battery index (0, 1, 2... for multi-battery systems)
    std::wstring manufacturer;
    std::wstring serialNumber;
    std::wstring chemistry;       // LiIon, LiPoly, etc.

    // Capacity
    int percentFull;             // 0-100
    double wattHoursRemaining;   // Energy remaining
    double wattHoursTotal;       // Total capacity
    double designCapacityWh;     // Original design capacity

    // Health
    double healthPercent;        // 0-100, based on design vs actual capacity
    int cycleCount;              // Charge cycle count
    double wearLevel;            // 0-100, degradation level

    // Status
    BatteryStatus status;
    PowerSourceType source;
    double chargeRateW;          // Charging rate in watts (positive) or discharge rate (negative)
    double voltage;              // Current voltage
    double temperature;          // Battery temperature if available

    // Time estimates
    int estimatedTimeRemaining;  // Seconds until empty (when discharging)
    int estimatedChargeTime;     // Seconds until full (when charging)

    // Reliability
    bool isValid;
};

/**
 * Power consumption sample
 */
struct PowerSample {
    int64_t timestamp;           // Microseconds since epoch

    // CPU power
    double cpuPackagePowerW;     // CPU package power (watts)
    double cpuCorePowerW;        // CPU cores only
    double cpuUncorePowerW;      // Uncore (integrated GPU, memory controller, etc.)
    double cpuDramPowerW;        // DRAM power if available

    // GPU power
    double gpuPowerW;            // Total GPU power
    double gpuCorePowerW;        // GPU core power
    double gpuMemoryPowerW;      // GPU memory power

    // System power
    double totalSystemPowerW;    // Total system power draw
    double estimatedDrawW;       // Estimated from battery drain if AC

    // Efficiency
    double wattsPerFrame;        // Power per frame (efficiency metric)
    double framesPerWatt;        // Frames per watt (efficiency metric)
    double fpsPerWatt;           // FPS per watt

    // Context
    int frameNumber;
    double fpsAtSample;

    // Power source
    PowerSourceType source;

    bool isValid;
};

/**
 * Power efficiency metrics
 */
struct PowerEfficiency {
    int64_t timestamp;

    // FPS efficiency
    double avgFramesPerWatt;
    double peakFramesPerWatt;
    double avgWattsPerFrame;
    double minWattsPerFrame;

    // Power efficiency score (0-100)
    double efficiencyScore;      // Higher = more efficient

    // Comparison
    double efficiencyVsBaseline; // % vs optimal baseline
    double efficiencyVsAverage;  // % vs session average

    // Platform comparison
    double desktopEquivalent;    // What FPS would be on desktop AC

    // Metrics
    double totalEnergyConsumedWh;
    double averagePowerW;
    double peakPowerW;

    // Battery impact
    double estimatedBatteryImpactMin;  // Minutes of battery life used
};

/**
 * Power alert
 */
struct PowerAlert {
    int id;
    PowerAlertType type;
    int64_t timestamp;
    std::string message;
    std::string details;
    double value;
    double threshold;
    bool acknowledged;
    int64_t acknowledgedAt;
};

/**
 * Power analysis configuration
 */
struct PowerAnalyzerConfig {
    // Sampling
    bool enabled = true;
    int sampleIntervalMs = 100;         // Sample every 100ms

    // Thresholds
    double batteryWarningPercent = 20.0;
    double batteryCriticalPercent = 10.0;
    double highDrainRateW = 50.0;       // High drain warning threshold
    double efficiencyWarningFpW = 0.5;  // Frames per watt below this = warning
    double efficiencyCriticalFpW = 0.3; // Frames per watt below this = critical

    // History
    size_t maxSampleHistory = 3600;     // 1 hour at 1Hz
    size_t maxAlertHistory = 100;

    // Power plan optimization
    bool suggestPowerPlanChanges = true;
    bool detectThrottling = true;

    // RAPL support (Intel power measurement)
    bool enableRapl = true;             // Use Intel RAPL if available
    bool enableNvml = true;             // Use NVIDIA NVML if available

    // Baseline comparison
    double baselineFpsPerWatt = 1.0;    // Expected efficiency baseline
};

/**
 * Power analysis statistics
 */
struct PowerStatistics {
    int64_t timestamp;
    int sampleCount;

    // CPU power
    double avgCpuPower;
    double minCpuPower;
    double maxCpuPower;
    double totalCpuEnergyWh;

    // GPU power
    double avgGpuPower;
    double minGpuPower;
    double maxGpuPower;
    double totalGpuEnergyWh;

    // System power
    double avgSystemPower;
    double minSystemPower;
    double maxSystemPower;
    double totalEnergyWh;

    // Efficiency
    double avgFpsPerWatt;
    double maxFpsPerWatt;
    double minFpsPerWatt;
    double avgWattsPerFrame;
    double efficiencyScore;

    // Battery
    int estimatedRemainingMin;          // Battery life remaining
    double drainRatePercentPerHour;
    double drainRateWatts;
    double batteryHealthPercent;

    // Alerts
    int alertCount;
    int unacknowledgedAlerts;
};

/**
 * Power optimization recommendation
 */
struct PowerRecommendation {
    std::string id;
    std::string category;         // "CPU", "GPU", "System", "Battery"
    std::string title;
    std::string description;
    std::string action;
    double estimatedSavingsW;     // Estimated power reduction
    double estimatedImpactPercent; // FPS impact (positive or negative)
    int priority;                 // 1-5, 1 = highest
    bool applied;
};

/**
 * Complete power report
 */
struct PowerReport {
    int64_t timestamp;
    int64_t sessionDurationMs;

    // Battery info
    BatteryInfo battery;
    bool hasBattery;

    // Current state
    PowerSourceType currentSource;
    PowerPlan activePlan;
    PowerThrottleState throttleState;

    // Statistics
    PowerStatistics stats;

    // Efficiency
    PowerEfficiency efficiency;

    // Alerts
    std::vector<PowerAlert> alerts;

    // Recommendations
    std::vector<PowerRecommendation> recommendations;

    // Power source history
    std::vector<std::pair<int64_t, PowerSourceType>> sourceChanges;

    // Power plan info
    std::string activePlanName;
    bool gameModeActive;
};

// ─── Main Class ───────────────────────────────────────────────────────────────

/**
 * PowerAnalyzer - Power consumption and efficiency analysis
 *
 * Features:
 * - Battery status monitoring and health tracking
 * - Power consumption tracking (CPU, GPU, system)
 * - Power efficiency metrics (FPS per watt)
 * - Power throttling detection
 * - Battery drain rate estimation
 * - Power optimization recommendations
 * - Integration with Windows power plans
 * - Intel RAPL support for accurate CPU power
 * - NVIDIA NVML support for GPU power
 * - Battery health and degradation tracking
 */
class PowerAnalyzer {
public:
    PowerAnalyzer();
    ~PowerAnalyzer();

    // Configuration
    void SetConfig(const PowerAnalyzerConfig& config);
    const PowerAnalyzerConfig& GetConfig() const { return m_config; }
    void Reset();

    // ─── Battery Monitoring ───────────────────────────────────────────────────

    bool HasBattery() const { return m_hasBattery; }
    BatteryInfo GetBatteryInfo() const;
    std::vector<BatteryInfo> GetAllBatteries() const;
    PowerSourceType GetCurrentPowerSource() const;
    double GetBatteryPercent() const;
    int GetEstimatedBatteryMinutes() const;

    // ─── Power Sampling ──────────────────────────────────────────────────────

    void BeginFrame();
    void EndFrame(double fps, double frameTimeMs);
    void RecordFrame(int frameNumber, double fps, double frameTimeMs);

    PowerSample GetLatestSample() const;
    std::vector<PowerSample> GetRecentSamples(size_t count = 60) const;

    // ─── Power Measurements ──────────────────────────────────────────────────

    double GetCpuPowerW() const;
    double GetGpuPowerW() const;
    double GetTotalSystemPowerW() const;
    double GetPowerDrawEstimate() const;

    // ─── Efficiency Metrics ──────────────────────────────────────────────────

    double GetFpsPerWatt() const;
    double GetWattsPerFrame() const;
    PowerEfficiency GetEfficiencyMetrics() const;
    double GetEfficiencyScore() const;    // 0-100

    // ─── Statistics ──────────────────────────────────────────────────────────

    PowerStatistics GetStatistics() const;
    PowerReport GenerateReport() const;

    // ─── Alerts ──────────────────────────────────────────────────────────────

    const std::vector<PowerAlert>& GetAlerts() const { return m_alerts; }
    std::vector<PowerAlert> GetActiveAlerts() const;
    std::vector<PowerAlert> GetUnacknowledgedAlerts() const;
    bool AcknowledgeAlert(int alertId);
    void ClearAlerts();

    // ─── Recommendations ─────────────────────────────────────────────────────

    std::vector<PowerRecommendation> GetRecommendations() const;
    void ApplyRecommendation(const std::string& recommendationId);

    // ─── Power Plan Management ───────────────────────────────────────────────

    PowerPlan GetActivePowerPlan() const;
    std::string GetPowerPlanName() const;
    bool SetPowerPlan(PowerPlan plan);
    bool IsGameModeActive() const;

    // ─── Throttling Detection ────────────────────────────────────────────────

    PowerThrottleState GetThrottleState() const;
    bool IsPowerThrottling() const;

    // ─── History & Export ────────────────────────────────────────────────────

    void SetSampleInterval(int intervalMs);
    int GetSampleInterval() const { return m_config.sampleIntervalMs; }

    std::string ExportToJSON() const;
    std::string ExportStatisticsToJSON() const;

    // ─── Callbacks ───────────────────────────────────────────────────────────

    using AlertCallback = std::function<void(const PowerAlert&)>;
    using SourceChangeCallback = std::function<void(PowerSourceType, PowerSourceType)>;

    void SetAlertCallback(AlertCallback cb) { m_alertCallback = std::move(cb); }
    void SetSourceChangeCallback(SourceChangeCallback cb) { m_sourceChangeCallback = std::move(cb); }

private:
    // Windows API helpers
    bool InitializeBatteryInfo();
    void UpdateBatteryInfo();
    bool InitializeRapl();
    bool InitializeNvml();
    void CloseRapl();
    void CloseNvml();

    // Power measurement
    PowerSample SamplePower() const;
    double ReadRaplPower() const;
    double ReadNvmlPower() const;
    double EstimatePowerFromBattery() const;

    // Analysis
    void AnalyzeSample(const PowerSample& sample);
    void GenerateAlerts();
    void CheckBatteryAlerts();
    void CheckEfficiencyAlerts();
    void CheckThrottling();

    // Recommendations
    void UpdateRecommendations();
    PowerRecommendation CreateRecommendation(
        const std::string& category,
        const std::string& title,
        const std::string& description,
        const std::string& action,
        double savingsW,
        double impactPercent,
        int priority) const;

    // Utility
    int64_t GetCurrentTimestamp() const;
    int GenerateAlertId();
    std::string PowerSourceTypeToString(PowerSourceType source) const;
    std::string BatteryStatusToString(BatteryStatus status) const;
    std::string ThrottleStateToString(PowerThrottleState state) const;

private:
    PowerAnalyzerConfig m_config;

    // State
    std::atomic<bool> m_initialized{ false };
    bool m_hasBattery = false;
    PowerSourceType m_currentSource = PowerSourceType::Unknown;
    PowerPlan m_activePlan = PowerPlan::Unknown;
    PowerThrottleState m_throttleState = PowerThrottleState::None;

    // Battery info
    std::vector<BatteryInfo> m_batteries;

    // Sample history
    std::vector<PowerSample> m_sampleHistory;
    size_t m_sampleHead = 0;
    mutable std::mutex m_sampleMutex;

    // Current frame context
    int m_currentFrame = 0;
    double m_currentFps = 0.0;
    double m_currentFrameTime = 0.0;
    int64_t m_frameStartTimestamp = 0;

    // Statistics
    PowerStatistics m_stats;
    mutable std::mutex m_statsMutex;

    // Alerts
    std::vector<PowerAlert> m_alerts;
    int m_nextAlertId = 1;
    mutable std::mutex m_alertMutex;

    // Callbacks
    AlertCallback m_alertCallback;
    SourceChangeCallback m_sourceChangeCallback;

    // RAPL support (Intel)
    bool m_raplAvailable = false;
    HANDLE m_raplHandle = nullptr;

    // NVML support (NVIDIA)
    bool m_nvmlAvailable = false;
    void* m_nvmlHandle = nullptr;    // NVML context (void* to avoid header dependency)

    // Power plan tracking
    GUID* m_originalPlanGuid = nullptr;
};

} // namespace ProfilerCore
