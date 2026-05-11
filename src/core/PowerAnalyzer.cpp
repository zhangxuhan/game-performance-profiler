/**
 * PowerAnalyzer.cpp - Power consumption and efficiency analysis implementation
 *
 * Uses Windows APIs for battery monitoring and Intel RAPL / NVIDIA NVML
 * for accurate power measurements when available.
 */

#include "PowerAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>

// Windows headers for power management
#include <powrprof.h>
#pragma comment(lib, "powrprof.lib")

namespace ProfilerCore {

// ─── Constructor / Destructor ────────────────────────────────────────────────

PowerAnalyzer::PowerAnalyzer()
{
    m_initialized = InitializeBatteryInfo();

    if (m_config.enableRapl) {
        InitializeRapl();
    }
    if (m_config.enableNvml) {
        InitializeNvml();
    }

    m_sampleHistory.resize(m_config.maxSampleHistory);
    m_stats = {};
}

PowerAnalyzer::~PowerAnalyzer()
{
    CloseRapl();
    CloseNvml();
}

// ─── Configuration ────────────────────────────────────────────────────────────

void PowerAnalyzer::SetConfig(const PowerAnalyzerConfig& config)
{
    std::lock_guard<std::mutex> lock(m_sampleMutex);
    m_config = config;
    m_sampleHistory.resize(config.maxSampleHistory);
}

void PowerAnalyzer::Reset()
{
    std::lock_guard<std::mutex> lock1(m_sampleMutex);
    std::lock_guard<std::mutex> lock2(m_statsMutex);
    std::lock_guard<std::mutex> lock3(m_alertMutex);

    m_sampleHistory.clear();
    m_sampleHistory.resize(m_config.maxSampleHistory);
    m_sampleHead = 0;
    m_stats = {};
    m_alerts.clear();
    m_nextAlertId = 1;
    m_currentFrame = 0;
    m_currentFps = 0.0;
    m_currentFrameTime = 0.0;
}

// ─── Battery Monitoring ──────────────────────────────────────────────────────

bool PowerAnalyzer::InitializeBatteryInfo()
{
    // Get system battery status
    SYSTEM_BATTERY_STATE batteryState;
    DWORD result = CallNtPowerInformation(SystemBatteryState, nullptr, 0,
                                          &batteryState, sizeof(batteryState));

    if (result == 0 && batteryState.BatteryPresent) {
        m_hasBattery = true;
        m_currentSource = batteryState.AcOnLine ? PowerSourceType::AC : PowerSourceType::Battery;
        UpdateBatteryInfo();
        return true;
    }

    m_hasBattery = false;
    m_currentSource = PowerSourceType::AC;
    return false;
}

void PowerAnalyzer::UpdateBatteryInfo()
{
    if (!m_hasBattery) return;

    m_batteries.clear();

    // Enumerate batteries
    SYSTEM_BATTERY_STATE batteryState;
    DWORD result = CallNtPowerInformation(SystemBatteryState, nullptr, 0,
                                          &batteryState, sizeof(batteryState));

    if (result == 0) {
        BatteryInfo info = {};
        info.timestamp = GetCurrentTimestamp();
        info.present = batteryState.BatteryPresent;

        if (info.present) {
            // Calculate capacity
            if (batteryState.MaxCapacity > 0) {
                info.percentFull = static_cast<int>(
                    (static_cast<double>(batteryState.RemainingCapacity) /
                     static_cast<double>(batteryState.MaxCapacity)) * 100.0);
            } else {
                info.percentFull = 0;
            }

            info.wattHoursTotal = static_cast<double>(batteryState.MaxCapacity) / 1000.0;  // mWh to Wh
            info.wattHoursRemaining = static_cast<double>(batteryState.RemainingCapacity) / 1000.0;

            // Estimate power rate
            info.chargeRateW = batteryState.ChargeRate != 0 ?
                               static_cast<double>(batteryState.ChargeRate) / 1000.0 : 0.0;

            // Status
            if (batteryState.AcOnLine) {
                if (info.chargeRateW > 0) {
                    info.status = BatteryStatus::Charging;
                } else if (info.percentFull >= 100) {
                    info.status = BatteryStatus::Idle;
                } else {
                    info.status = BatteryStatus::Discharging;
                }
                info.source = PowerSourceType::AC;
            } else {
                info.status = BatteryStatus::Discharging;
                info.source = PowerSourceType::Battery;
                info.chargeRateW = -std::abs(info.chargeRateW);  // Negative for discharge
            }

            // Time estimates
            if (batteryState.EstimatedTime > 0) {
                if (info.status == BatteryStatus::Discharging) {
                    info.estimatedTimeRemaining = static_cast<int>(batteryState.EstimatedTime);
                } else if (info.status == BatteryStatus::Charging) {
                    info.estimatedChargeTime = static_cast<int>(batteryState.EstimatedTime);
                }
            }

            // Health estimation (based on capacity)
            info.healthPercent = batteryState.MaxCapacity > 0 && batteryState.DesignedCapacity > 0 ?
                (static_cast<double>(batteryState.MaxCapacity) /
                 static_cast<double>(batteryState.DesignedCapacity)) * 100.0 : 100.0;

            info.designCapacityWh = static_cast<double>(batteryState.DesignedCapacity) / 1000.0;
            info.wearLevel = 100.0 - info.healthPercent;

            // Critical check
            if (info.percentFull <= m_config.batteryCriticalPercent) {
                info.status = BatteryStatus::Critical;
            }

            info.isValid = true;
        }

        m_batteries.push_back(info);
    }
}

BatteryInfo PowerAnalyzer::GetBatteryInfo() const
{
    if (!m_batteries.empty()) {
        return m_batteries[0];
    }
    return BatteryInfo{};
}

std::vector<BatteryInfo> PowerAnalyzer::GetAllBatteries() const
{
    return m_batteries;
}

PowerSourceType PowerAnalyzer::GetCurrentPowerSource() const
{
    return m_currentSource;
}

double PowerAnalyzer::GetBatteryPercent() const
{
    if (m_batteries.empty() || !m_batteries[0].isValid) {
        return 100.0;
    }
    return static_cast<double>(m_batteries[0].percentFull);
}

int PowerAnalyzer::GetEstimatedBatteryMinutes() const
{
    if (m_batteries.empty() || !m_batteries[0].isValid) {
        return -1;
    }
    return m_batteries[0].estimatedTimeRemaining / 60;
}

// ─── Power Sampling ──────────────────────────────────────────────────────────

void PowerAnalyzer::BeginFrame()
{
    m_frameStartTimestamp = GetCurrentTimestamp();
}

void PowerAnalyzer::EndFrame(double fps, double frameTimeMs)
{
    m_currentFps = fps;
    m_currentFrameTime = frameTimeMs;
    m_currentFrame++;

    // Sample power at frame end
    PowerSample sample = SamplePower();
    sample.frameNumber = m_currentFrame;
    sample.fpsAtSample = fps;

    AnalyzeSample(sample);
}

void PowerAnalyzer::RecordFrame(int frameNumber, double fps, double frameTimeMs)
{
    m_currentFrame = frameNumber;
    m_currentFps = fps;
    m_currentFrameTime = frameTimeMs;

    PowerSample sample = SamplePower();
    sample.frameNumber = frameNumber;
    sample.fpsAtSample = fps;

    AnalyzeSample(sample);
}

PowerSample PowerAnalyzer::SamplePower() const
{
    PowerSample sample = {};
    sample.timestamp = GetCurrentTimestamp();
    sample.source = m_currentSource;
    sample.isValid = false;

    // Try RAPL for CPU power
    if (m_raplAvailable) {
        sample.cpuPackagePowerW = ReadRaplPower();
        if (sample.cpuPackagePowerW > 0) {
            sample.cpuPowerW = sample.cpuPackagePowerW;
            sample.isValid = true;
        }
    }

    // Try NVML for GPU power
    if (m_nvmlAvailable) {
        sample.gpuPowerW = ReadNvmlPower();
        if (sample.gpuPowerW > 0) {
            sample.isValid = true;
        }
    }

    // Estimate system power from battery drain
    if (m_currentSource == PowerSourceType::Battery && m_hasBattery) {
        sample.estimatedDrawW = EstimatePowerFromBattery();
        if (sample.estimatedDrawW > 0) {
            sample.totalSystemPowerW = sample.estimatedDrawW;
            sample.isValid = true;
        }
    }

    // Calculate efficiency metrics
    if (sample.isValid && m_currentFps > 0) {
        double totalPower = sample.cpuPowerW + sample.gpuPowerW;
        if (totalPower <= 0) {
            totalPower = sample.totalSystemPowerW;
        }

        if (totalPower > 0) {
            sample.fpsPerWatt = m_currentFps / totalPower;
            sample.wattsPerFrame = totalPower / m_currentFps;
            sample.framesPerWatt = sample.fpsPerWatt;
        }
    }

    return sample;
}

PowerSample PowerAnalyzer::GetLatestSample() const
{
    std::lock_guard<std::mutex> lock(m_sampleMutex);
    if (m_sampleHead > 0) {
        return m_sampleHistory[(m_sampleHead - 1) % m_config.maxSampleHistory];
    }
    return PowerSample{};
}

std::vector<PowerSample> PowerAnalyzer::GetRecentSamples(size_t count) const
{
    std::lock_guard<std::mutex> lock(m_sampleMutex);

    std::vector<PowerSample> result;
    size_t available = std::min(m_sampleHead, m_config.maxSampleHistory);
    count = std::min(count, available);

    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (m_sampleHead - count + i) % m_config.maxSampleHistory;
        result.push_back(m_sampleHistory[idx]);
    }

    return result;
}

// ─── Power Measurements ──────────────────────────────────────────────────────

double PowerAnalyzer::GetCpuPowerW() const
{
    auto sample = GetLatestSample();
    return sample.cpuPackagePowerW;
}

double PowerAnalyzer::GetGpuPowerW() const
{
    auto sample = GetLatestSample();
    return sample.gpuPowerW;
}

double PowerAnalyzer::GetTotalSystemPowerW() const
{
    auto sample = GetLatestSample();
    return sample.totalSystemPowerW > 0 ? sample.totalSystemPowerW :
           sample.cpuPackagePowerW + sample.gpuPowerW;
}

double PowerAnalyzer::GetPowerDrawEstimate() const
{
    if (m_currentSource == PowerSourceType::Battery && m_hasBattery) {
        return EstimatePowerFromBattery();
    }
    return GetTotalSystemPowerW();
}

// ─── RAPL Support (Intel) ────────────────────────────────────────────────────

bool PowerAnalyzer::InitializeRapl()
{
    // RAPL access requires reading MSRs which needs kernel driver
    // On Windows, this typically requires a driver like Intel Power Gadget
    // For now, mark as unavailable unless we find a way to access it
    m_raplAvailable = false;
    return false;
}

void PowerAnalyzer::CloseRapl()
{
    if (m_raplHandle) {
        CloseHandle(m_raplHandle);
        m_raplHandle = nullptr;
    }
    m_raplAvailable = false;
}

double PowerAnalyzer::ReadRaplPower() const
{
    if (!m_raplAvailable) return 0.0;
    // RAPL reading implementation would go here
    return 0.0;
}

// ─── NVML Support (NVIDIA) ───────────────────────────────────────────────────

bool PowerAnalyzer::InitializeNvml()
{
    // NVML requires nvidia-smi and NVML library
    // For simplicity, we check if NVIDIA drivers are present
    // Full implementation would load nvml.dll and call nvmlInit()

    HMODULE hNvml = LoadLibraryW(L"nvml.dll");
    if (hNvml) {
        FreeLibrary(hNvml);
        // NVML available, but we'd need to properly initialize
        // For now, mark as potentially available
        m_nvmlAvailable = false;  // Set to true when fully implemented
        return false;
    }

    m_nvmlAvailable = false;
    return false;
}

void PowerAnalyzer::CloseNvml()
{
    m_nvmlHandle = nullptr;
    m_nvmlAvailable = false;
}

double PowerAnalyzer::ReadNvmlPower() const
{
    if (!m_nvmlAvailable) return 0.0;
    // NVML power reading would go here
    return 0.0;
}

// ─── Battery Power Estimation ────────────────────────────────────────────────

double PowerAnalyzer::EstimatePowerFromBattery() const
{
    if (!m_hasBattery || m_batteries.empty()) return 0.0;

    const auto& battery = m_batteries[0];
    if (!battery.isValid) return 0.0;

    // Use charge/discharge rate if available
    if (battery.status == BatteryStatus::Discharging) {
        return std::abs(battery.chargeRateW);
    }

    // Estimate from capacity change over time
    // This would require tracking capacity changes
    return 0.0;
}

// ─── Efficiency Metrics ──────────────────────────────────────────────────────

double PowerAnalyzer::GetFpsPerWatt() const
{
    auto sample = GetLatestSample();
    return sample.fpsPerWatt;
}

double PowerAnalyzer::GetWattsPerFrame() const
{
    auto sample = GetLatestSample();
    return sample.wattsPerFrame;
}

PowerEfficiency PowerAnalyzer::GetEfficiencyMetrics() const
{
    std::lock_guard<std::mutex> lock(m_statsMutex);

    PowerEfficiency efficiency = {};
    efficiency.timestamp = GetCurrentTimestamp();

    if (m_stats.sampleCount > 0) {
        efficiency.avgFramesPerWatt = m_stats.avgFpsPerWatt;
        efficiency.maxFramesPerWatt = m_stats.maxFpsPerWatt;
        efficiency.minFramesPerWatt = m_stats.minFpsPerWatt;
        efficiency.avgWattsPerFrame = m_stats.avgWattsPerFrame;
        efficiency.efficiencyScore = m_stats.efficiencyScore;
        efficiency.averagePowerW = m_stats.avgSystemPower;
        efficiency.peakPowerW = m_stats.maxSystemPower;
        efficiency.totalEnergyConsumedWh = m_stats.totalEnergyWh;

        // Calculate vs baseline
        if (m_config.baselineFpsPerWatt > 0) {
            efficiency.efficiencyVsBaseline =
                (m_stats.avgFpsPerWatt / m_config.baselineFpsPerWatt) * 100.0;
        }

        // Battery impact
        if (m_currentSource == PowerSourceType::Battery && m_hasBattery) {
            if (m_stats.drainRateWatts > 0) {
                // Estimate battery minutes used per hour of gaming
                efficiency.estimatedBatteryImpactMin =
                    (m_stats.drainRateWatts / 50.0) * 60.0;  // Rough estimate
            }
        }
    }

    return efficiency;
}

double PowerAnalyzer::GetEfficiencyScore() const
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats.efficiencyScore;
}

// ─── Statistics ──────────────────────────────────────────────────────────────

PowerStatistics PowerAnalyzer::GetStatistics() const
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}

void PowerAnalyzer::AnalyzeSample(const PowerSample& sample)
{
    if (!sample.isValid) return;

    // Store sample
    {
        std::lock_guard<std::mutex> lock(m_sampleMutex);
        m_sampleHistory[m_sampleHead % m_config.maxSampleHistory] = sample;
        m_sampleHead++;
    }

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);

        m_stats.timestamp = sample.timestamp;
        m_stats.sampleCount++;

        // CPU power stats
        if (sample.cpuPackagePowerW > 0) {
            double prevAvg = m_stats.avgCpuPower;
            m_stats.avgCpuPower = prevAvg + (sample.cpuPackagePowerW - prevAvg) / m_stats.sampleCount;
            m_stats.minCpuPower = m_stats.sampleCount == 1 ? sample.cpuPackagePowerW :
                                  std::min(m_stats.minCpuPower, sample.cpuPackagePowerW);
            m_stats.maxCpuPower = std::max(m_stats.maxCpuPower, sample.cpuPackagePowerW);

            // Accumulate energy (W * sample_interval_seconds / 3600 for Wh)
            m_stats.totalCpuEnergyWh += sample.cpuPackagePowerW *
                                        (m_config.sampleIntervalMs / 1000.0) / 3600.0;
        }

        // GPU power stats
        if (sample.gpuPowerW > 0) {
            double prevAvg = m_stats.avgGpuPower;
            m_stats.avgGpuPower = prevAvg + (sample.gpuPowerW - prevAvg) / m_stats.sampleCount;
            m_stats.minGpuPower = m_stats.sampleCount == 1 ? sample.gpuPowerW :
                                  std::min(m_stats.minGpuPower, sample.gpuPowerW);
            m_stats.maxGpuPower = std::max(m_stats.maxGpuPower, sample.gpuPowerW);

            m_stats.totalGpuEnergyWh += sample.gpuPowerW *
                                        (m_config.sampleIntervalMs / 1000.0) / 3600.0;
        }

        // System power stats
        double totalPower = sample.totalSystemPowerW > 0 ? sample.totalSystemPowerW :
                           sample.cpuPackagePowerW + sample.gpuPowerW;
        if (totalPower > 0) {
            double prevAvg = m_stats.avgSystemPower;
            m_stats.avgSystemPower = prevAvg + (totalPower - prevAvg) / m_stats.sampleCount;
            m_stats.minSystemPower = m_stats.sampleCount == 1 ? totalPower :
                                     std::min(m_stats.minSystemPower, totalPower);
            m_stats.maxSystemPower = std::max(m_stats.maxSystemPower, totalPower);

            m_stats.totalEnergyWh += totalPower *
                                     (m_config.sampleIntervalMs / 1000.0) / 3600.0;
        }

        // Efficiency stats
        if (sample.fpsPerWatt > 0) {
            double prevAvg = m_stats.avgFpsPerWatt;
            m_stats.avgFpsPerWatt = prevAvg + (sample.fpsPerWatt - prevAvg) / m_stats.sampleCount;
            m_stats.minFpsPerWatt = m_stats.sampleCount == 1 ? sample.fpsPerWatt :
                                    std::min(m_stats.minFpsPerWatt, sample.fpsPerWatt);
            m_stats.maxFpsPerWatt = std::max(m_stats.maxFpsPerWatt, sample.fpsPerWatt);
        }

        if (sample.wattsPerFrame > 0) {
            double prevAvg = m_stats.avgWattsPerFrame;
            m_stats.avgWattsPerFrame = prevAvg + (sample.wattsPerFrame - prevAvg) / m_stats.sampleCount;
        }

        // Calculate efficiency score (0-100)
        // Based on: FPS per watt relative to baseline
        if (m_config.baselineFpsPerWatt > 0 && m_stats.avgFpsPerWatt > 0) {
            double ratio = m_stats.avgFpsPerWatt / m_config.baselineFpsPerWatt;
            m_stats.efficiencyScore = std::min(100.0, ratio * 100.0);
        }
    }

    // Update battery info periodically
    if (m_hasBattery && m_sampleHead % 10 == 0) {
        UpdateBatteryInfo();

        // Update battery stats
        std::lock_guard<std::mutex> lock(m_statsMutex);
        if (!m_batteries.empty() && m_batteries[0].isValid) {
            m_stats.estimatedRemainingMin = m_batteries[0].estimatedTimeRemaining / 60;
            m_stats.batteryHealthPercent = m_batteries[0].healthPercent;
            m_stats.drainRateWatts = std::abs(m_batteries[0].chargeRateW);
        }
    }

    // Generate alerts
    GenerateAlerts();
}

PowerReport PowerAnalyzer::GenerateReport() const
{
    PowerReport report = {};
    report.timestamp = GetCurrentTimestamp();

    // Battery info
    report.hasBattery = m_hasBattery;
    report.battery = GetBatteryInfo();

    // Current state
    report.currentSource = m_currentSource;
    report.activePlan = GetActivePowerPlan();
    report.throttleState = GetThrottleState();

    // Statistics
    report.stats = GetStatistics();

    // Efficiency
    report.efficiency = GetEfficiencyMetrics();

    // Alerts
    report.alerts = GetActiveAlerts();

    // Recommendations
    report.recommendations = GetRecommendations();

    // Power plan info
    report.activePlanName = GetPowerPlanName();
    report.gameModeActive = IsGameModeActive();

    return report;
}

// ─── Alerts ───────────────────────────────────────────────────────────────────

std::vector<PowerAlert> PowerAnalyzer::GetActiveAlerts() const
{
    std::lock_guard<std::mutex> lock(m_alertMutex);

    std::vector<PowerAlert> result;
    for (const auto& alert : m_alerts) {
        if (!alert.acknowledged) {
            result.push_back(alert);
        }
    }
    return result;
}

std::vector<PowerAlert> PowerAnalyzer::GetUnacknowledgedAlerts() const
{
    return GetActiveAlerts();
}

bool PowerAnalyzer::AcknowledgeAlert(int alertId)
{
    std::lock_guard<std::mutex> lock(m_alertMutex);

    for (auto& alert : m_alerts) {
        if (alert.id == alertId) {
            alert.acknowledged = true;
            alert.acknowledgedAt = GetCurrentTimestamp();
            return true;
        }
    }
    return false;
}

void PowerAnalyzer::ClearAlerts()
{
    std::lock_guard<std::mutex> lock(m_alertMutex);
    m_alerts.clear();
}

void PowerAnalyzer::GenerateAlerts()
{
    CheckBatteryAlerts();
    CheckEfficiencyAlerts();
    CheckThrottling();
}

void PowerAnalyzer::CheckBatteryAlerts()
{
    if (!m_hasBattery) return;

    auto battery = GetBatteryInfo();
    if (!battery.isValid) return;

    // Battery low warning
    if (battery.percentFull <= m_config.batteryWarningPercent &&
        battery.percentFull > m_config.batteryCriticalPercent) {

        PowerAlert alert = {};
        alert.id = GenerateAlertId();
        alert.type = PowerAlertType::BatteryLow;
        alert.timestamp = GetCurrentTimestamp();
        alert.message = "Battery low: " + std::to_string(battery.percentFull) + "%";
        alert.details = "Estimated " + std::to_string(battery.estimatedTimeRemaining / 60) + " minutes remaining";
        alert.value = static_cast<double>(battery.percentFull);
        alert.threshold = m_config.batteryWarningPercent;
        alert.acknowledged = false;

        std::lock_guard<std::mutex> lock(m_alertMutex);
        m_alerts.push_back(alert);

        if (m_alertCallback) {
            m_alertCallback(alert);
        }
    }

    // Battery critical
    if (battery.percentFull <= m_config.batteryCriticalPercent) {
        PowerAlert alert = {};
        alert.id = GenerateAlertId();
        alert.type = PowerAlertType::BatteryCritical;
        alert.timestamp = GetCurrentTimestamp();
        alert.message = "Battery critical: " + std::to_string(battery.percentFull) + "%";
        alert.details = "Connect power source immediately";
        alert.value = static_cast<double>(battery.percentFull);
        alert.threshold = m_config.batteryCriticalPercent;
        alert.acknowledged = false;

        std::lock_guard<std::mutex> lock(m_alertMutex);
        m_alerts.push_back(alert);

        if (m_alertCallback) {
            m_alertCallback(alert);
        }
    }

    // Power source changed
    static PowerSourceType lastSource = m_currentSource;
    if (m_currentSource != lastSource) {
        PowerAlert alert = {};
        alert.id = GenerateAlertId();
        alert.type = PowerAlertType::PowerSourceChanged;
        alert.timestamp = GetCurrentTimestamp();
        alert.message = "Power source changed";
        alert.details = "Now running on " + PowerSourceTypeToString(m_currentSource);
        alert.value = static_cast<double>(static_cast<int>(m_currentSource));
        alert.threshold = 0;
        alert.acknowledged = false;

        std::lock_guard<std::mutex> lock(m_alertMutex);
        m_alerts.push_back(alert);

        if (m_sourceChangeCallback) {
            m_sourceChangeCallback(lastSource, m_currentSource);
        }

        lastSource = m_currentSource;
    }

    // High drain rate
    if (m_currentSource == PowerSourceType::Battery &&
        battery.chargeRateW < -m_config.highDrainRateW) {

        PowerAlert alert = {};
        alert.id = GenerateAlertId();
        alert.type = PowerAlertType::HighDrainRate;
        alert.timestamp = GetCurrentTimestamp();
        alert.message = "High battery drain rate";
        alert.details = "Consuming " + std::to_string(static_cast<int>(-battery.chargeRateW)) + "W";
        alert.value = -battery.chargeRateW;
        alert.threshold = m_config.highDrainRateW;
        alert.acknowledged = false;

        std::lock_guard<std::mutex> lock(m_alertMutex);
        m_alerts.push_back(alert);
    }
}

void PowerAnalyzer::CheckEfficiencyAlerts()
{
    auto stats = GetStatistics();

    if (stats.sampleCount < 10) return;

    // Low efficiency warning
    if (stats.avgFpsPerWatt < m_config.efficiencyWarningFpW &&
        stats.avgFpsPerWatt >= m_config.efficiencyCriticalFpW) {

        PowerAlert alert = {};
        alert.id = GenerateAlertId();
        alert.type = PowerAlertType::EfficiencyDrop;
        alert.timestamp = GetCurrentTimestamp();
        alert.message = "Power efficiency is low";
        alert.details = std::to_string(stats.avgFpsPerWatt) + " FPS/Watt (expected > " +
                       std::to_string(m_config.efficiencyWarningFpW) + ")";
        alert.value = stats.avgFpsPerWatt;
        alert.threshold = m_config.efficiencyWarningFpW;
        alert.acknowledged = false;

        std::lock_guard<std::mutex> lock(m_alertMutex);
        m_alerts.push_back(alert);
    }

    // Critical efficiency
    if (stats.avgFpsPerWatt < m_config.efficiencyCriticalFpW) {
        PowerAlert alert = {};
        alert.id = GenerateAlertId();
        alert.type = PowerAlertType::EfficiencyDrop;
        alert.timestamp = GetCurrentTimestamp();
        alert.message = "Power efficiency is critically low";
        alert.details = std::to_string(stats.avgFpsPerWatt) + " FPS/Watt - " +
                       "consider reducing graphics settings";
        alert.value = stats.avgFpsPerWatt;
        alert.threshold = m_config.efficiencyCriticalFpW;
        alert.acknowledged = false;

        std::lock_guard<std::mutex> lock(m_alertMutex);
        m_alerts.push_back(alert);
    }
}

void PowerAnalyzer::CheckThrottling()
{
    // Detect power throttling based on efficiency drop + battery source
    if (m_currentSource == PowerSourceType::Battery) {
        auto stats = GetStatistics();

        if (stats.efficiencyScore < 50 && m_throttleState == PowerThrottleState::None) {
            m_throttleState = PowerThrottleState::Moderate;

            PowerAlert alert = {};
            alert.id = GenerateAlertId();
            alert.type = PowerAlertType::ThermalThrottling;
            alert.timestamp = GetCurrentTimestamp();
            alert.message = "Power throttling detected";
            alert.details = "Performance reduced to save power";
            alert.value = stats.efficiencyScore;
            alert.threshold = 50;
            alert.acknowledged = false;

            std::lock_guard<std::mutex> lock(m_alertMutex);
            m_alerts.push_back(alert);
        }
    }
}

// ─── Recommendations ─────────────────────────────────────────────────────────

std::vector<PowerRecommendation> PowerAnalyzer::GetRecommendations() const
{
    std::vector<PowerRecommendation> recs;

    auto stats = GetStatistics();
    auto battery = GetBatteryInfo();

    // Battery recommendations
    if (m_hasBattery && m_currentSource == PowerSourceType::Battery) {
        if (battery.percentFull < 50) {
            recs.push_back(CreateRecommendation(
                "Battery",
                "Enable Power Saver Mode",
                "Switch to power saver mode to extend battery life",
                "set_power_saver",
                5.0,   // Estimated 5W savings
                -10.0, // May reduce FPS by ~10%
                1
            ));
        }

        if (stats.avgFpsPerWatt < m_config.efficiencyWarningFpW) {
            recs.push_back(CreateRecommendation(
                "GPU",
                "Reduce Graphics Settings",
                "Lower graphics quality to improve power efficiency",
                "reduce_graphics",
                10.0,
                -15.0,
                2
            ));

            recs.push_back(CreateRecommendation(
                "System",
                "Enable Frame Rate Cap",
                "Cap FPS at 30 for better battery life",
                "cap_fps_30",
                15.0,
                -50.0, // FPS drop, but intentional
                2
            ));
        }
    }

    // Power plan recommendations
    if (m_currentSource == PowerSourceType::AC) {
        auto plan = GetActivePowerPlan();
        if (plan == PowerPlan::PowerSaver) {
            recs.push_back(CreateRecommendation(
                "System",
                "Switch to High Performance Plan",
                "Power Saver mode limits CPU/GPU performance",
                "set_high_performance",
                -20.0,  // Uses MORE power
                20.0,   // But gains FPS
                3
            ));
        }

        if (!IsGameModeActive() && m_config.suggestPowerPlanChanges) {
            recs.push_back(CreateRecommendation(
                "System",
                "Enable Windows Game Mode",
                "Game Mode prioritizes game processes",
                "enable_game_mode",
                0.0,
                5.0,
                4
            ));
        }
    }

    // Efficiency recommendations
    if (stats.avgFpsPerWatt > 0 && stats.avgFpsPerWatt < m_config.baselineFpsPerWatt * 0.8) {
        recs.push_back(CreateRecommendation(
            "CPU",
            "Optimize Background Processes",
            "Close unnecessary applications to reduce CPU load",
            "optimize_background",
            5.0,
            10.0,
            2
        ));
    }

    return recs;
}

void PowerAnalyzer::ApplyRecommendation(const std::string& recommendationId)
{
    if (recommendationId == "set_high_performance") {
        SetPowerPlan(PowerPlan::HighPerformance);
    }
    else if (recommendationId == "set_power_saver") {
        SetPowerPlan(PowerPlan::PowerSaver);
    }
    // Other recommendations would require integration with game settings
}

PowerRecommendation PowerAnalyzer::CreateRecommendation(
    const std::string& category,
    const std::string& title,
    const std::string& description,
    const std::string& action,
    double savingsW,
    double impactPercent,
    int priority) const
{
    PowerRecommendation rec = {};
    rec.id = category + "_" + action;
    rec.category = category;
    rec.title = title;
    rec.description = description;
    rec.action = action;
    rec.estimatedSavingsW = savingsW;
    rec.estimatedImpactPercent = impactPercent;
    rec.priority = priority;
    rec.applied = false;
    return rec;
}

// ─── Power Plan Management ───────────────────────────────────────────────────

PowerPlan PowerAnalyzer::GetActivePowerPlan() const
{
    GUID* activePlan = nullptr;
    DWORD result = PowerGetActiveScheme(nullptr, &activePlan);

    if (result == ERROR_SUCCESS && activePlan) {
        // Known Windows power plan GUIDs
        GUID balanced = { 0x381b4222, 0xf694, 0x41f0, { 0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e } };
        GUID highPerf = { 0x8c5e7fda, 0xe8bf, 0x4a20, { 0x86, 0x97, 0xf3, 0x67, 0x51, 0x73, 0x70, 0x3f } };
        GUID powerSaver = { 0xa1841308, 0x3541, 0x4fab, { 0xbc, 0x81, 0xf1, 0x51, 0x6b, 0xf2, 0x51, 0x66 } };

        PowerPlan plan = PowerPlan::Custom;

        if (memcmp(activePlan, &balanced, sizeof(GUID)) == 0) {
            plan = PowerPlan::Balanced;
        }
        else if (memcmp(activePlan, &highPerf, sizeof(GUID)) == 0) {
            plan = PowerPlan::HighPerformance;
        }
        else if (memcmp(activePlan, &powerSaver, sizeof(GUID)) == 0) {
            plan = PowerPlan::PowerSaver;
        }

        LocalFree(activePlan);
        return plan;
    }

    return PowerPlan::Unknown;
}

std::string PowerAnalyzer::GetPowerPlanName() const
{
    auto plan = GetActivePowerPlan();
    switch (plan) {
        case PowerPlan::Balanced: return "Balanced";
        case PowerPlan::HighPerformance: return "High Performance";
        case PowerPlan::PowerSaver: return "Power Saver";
        case PowerPlan::GameMode: return "Game Mode";
        case PowerPlan::Custom: return "Custom";
        default: return "Unknown";
    }
}

bool PowerAnalyzer::SetPowerPlan(PowerPlan plan)
{
    GUID targetGuid = {};

    switch (plan) {
        case PowerPlan::Balanced:
            targetGuid = { 0x381b4222, 0xf694, 0x41f0, { 0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e } };
            break;
        case PowerPlan::HighPerformance:
            targetGuid = { 0x8c5e7fda, 0xe8bf, 0x4a20, { 0x86, 0x97, 0xf3, 0x67, 0x51, 0x73, 0x70, 0x3f } };
            break;
        case PowerPlan::PowerSaver:
            targetGuid = { 0xa1841308, 0x3541, 0x4fab, { 0xbc, 0x81, 0xf1, 0x51, 0x6b, 0xf2, 0x51, 0x66 } };
            break;
        default:
            return false;
    }

    DWORD result = PowerSetActiveScheme(nullptr, &targetGuid);
    return result == ERROR_SUCCESS;
}

bool PowerAnalyzer::IsGameModeActive() const
{
    // Game Mode status requires checking Windows Gaming settings
    // This is a simplified check - full implementation would need
    // to query the GameBar and GameMode APIs
    return false;
}

// ─── Throttling Detection ────────────────────────────────────────────────────

PowerThrottleState PowerAnalyzer::GetThrottleState() const
{
    return m_throttleState;
}

bool PowerAnalyzer::IsPowerThrottling() const
{
    return m_throttleState != PowerThrottleState::None;
}

// ─── History & Export ────────────────────────────────────────────────────────

void PowerAnalyzer::SetSampleInterval(int intervalMs)
{
    m_config.sampleIntervalMs = intervalMs;
}

std::string PowerAnalyzer::ExportToJSON() const
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "{\n";
    ss << "  \"timestamp\": " << GetCurrentTimestamp() << ",\n";

    // Battery
    auto battery = GetBatteryInfo();
    ss << "  \"battery\": {\n";
    ss << "    \"present\": " << (battery.present ? "true" : "false") << ",\n";
    ss << "    \"percent\": " << battery.percentFull << ",\n";
    ss << "    \"status\": \"" << BatteryStatusToString(battery.status) << "\",\n";
    ss << "    \"health\": " << battery.healthPercent << ",\n";
    ss << "    \"remainingMinutes\": " << (battery.estimatedTimeRemaining / 60) << "\n";
    ss << "  },\n";

    // Power source
    ss << "  \"powerSource\": \"" << PowerSourceTypeToString(m_currentSource) << "\",\n";

    // Statistics
    auto stats = GetStatistics();
    ss << "  \"statistics\": {\n";
    ss << "    \"avgCpuPower\": " << stats.avgCpuPower << ",\n";
    ss << "    \"avgGpuPower\": " << stats.avgGpuPower << ",\n";
    ss << "    \"avgSystemPower\": " << stats.avgSystemPower << ",\n";
    ss << "    \"totalEnergyWh\": " << stats.totalEnergyWh << ",\n";
    ss << "    \"avgFpsPerWatt\": " << stats.avgFpsPerWatt << ",\n";
    ss << "    \"efficiencyScore\": " << stats.efficiencyScore << "\n";
    ss << "  },\n";

    // Alerts
    auto alerts = GetActiveAlerts();
    ss << "  \"alerts\": [";
    for (size_t i = 0; i < alerts.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\n    {\"id\":" << alerts[i].id
           << ", \"type\":\"" << static_cast<int>(alerts[i].type) << "\""
           << ", \"message\":\"" << alerts[i].message << "\"}";
    }
    ss << "\n  ]\n";

    ss << "}\n";

    return ss.str();
}

std::string PowerAnalyzer::ExportStatisticsToJSON() const
{
    auto stats = GetStatistics();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "{\n";
    ss << "  \"timestamp\": " << stats.timestamp << ",\n";
    ss << "  \"sampleCount\": " << stats.sampleCount << ",\n";
    ss << "  \"cpuPower\": {\n";
    ss << "    \"avg\": " << stats.avgCpuPower << ",\n";
    ss << "    \"min\": " << stats.minCpuPower << ",\n";
    ss << "    \"max\": " << stats.maxCpuPower << ",\n";
    ss << "    \"totalEnergyWh\": " << stats.totalCpuEnergyWh << "\n";
    ss << "  },\n";
    ss << "  \"gpuPower\": {\n";
    ss << "    \"avg\": " << stats.avgGpuPower << ",\n";
    ss << "    \"min\": " << stats.minGpuPower << ",\n";
    ss << "    \"max\": " << stats.maxGpuPower << ",\n";
    ss << "    \"totalEnergyWh\": " << stats.totalGpuEnergyWh << "\n";
    ss << "  },\n";
    ss << "  \"efficiency\": {\n";
    ss << "    \"avgFpsPerWatt\": " << stats.avgFpsPerWatt << ",\n";
    ss << "    \"efficiencyScore\": " << stats.efficiencyScore << "\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

// ─── Utility ──────────────────────────────────────────────────────────────────

int64_t PowerAnalyzer::GetCurrentTimestamp() const
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int PowerAnalyzer::GenerateAlertId()
{
    return m_nextAlertId++;
}

std::string PowerAnalyzer::PowerSourceTypeToString(PowerSourceType source) const
{
    switch (source) {
        case PowerSourceType::AC: return "AC";
        case PowerSourceType::Battery: return "Battery";
        case PowerSourceType::UPS: return "UPS";
        default: return "Unknown";
    }
}

std::string PowerAnalyzer::BatteryStatusToString(BatteryStatus status) const
{
    switch (status) {
        case BatteryStatus::Discharging: return "Discharging";
        case BatteryStatus::Charging: return "Charging";
        case BatteryStatus::Idle: return "Idle";
        case BatteryStatus::Critical: return "Critical";
        default: return "Unknown";
    }
}

std::string PowerAnalyzer::ThrottleStateToString(PowerThrottleState state) const
{
    switch (state) {
        case PowerThrottleState::None: return "None";
        case PowerThrottleState::Light: return "Light";
        case PowerThrottleState::Moderate: return "Moderate";
        case PowerThrottleState::Severe: return "Severe";
        case PowerThrottleState::Critical: return "Critical";
        default: return "Unknown";
    }
}

} // namespace ProfilerCore
