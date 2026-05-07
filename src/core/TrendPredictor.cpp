/**
 * TrendPredictor.cpp
 * Comprehensive trend prediction and analysis module
 * 
 * Features:
 * - Multi-metric time series tracking (FPS, frame time, memory, GPU, thermal)
 * - Multiple prediction algorithms (linear, exponential smoothing, weighted, polynomial)
 * - Confidence-aware forecasting with confidence intervals
 * - Anomaly detection (spikes, drops, freezes)
 * - Memory leak prediction and OOM estimation
 * - Thermal runaway prediction
 * - Health and stability scoring
 * - Automatic algorithm selection based on data characteristics
 * - Configurable smoothing and prediction parameters
 */

#include "TrendPredictor.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <limits>

namespace ProfilerCore {

// ============================================================================
// Construction / Destruction
// ============================================================================

TrendPredictor::TrendPredictor()
    : m_cachedVolatility(0.0)
    , m_cachedHealthScore(100.0)
    , m_healthDirty(true)
{
    // Initialize default series
    InitializeSeries(DataSeriesType::FPS, "FPS");
    InitializeSeries(DataSeriesType::FrameTime, "Frame Time");
    InitializeSeries(DataSeriesType::Memory, "Memory");
    InitializeSeries(DataSeriesType::GPUUtilization, "GPU Utilization");
    InitializeSeries(DataSeriesType::Temperature, "Temperature");
}

TrendPredictor::~TrendPredictor() {
}

// ============================================================================
// Configuration
// ============================================================================

void TrendPredictor::SetConfig(const Config& config) {
    m_config = config;
    m_healthDirty = true;
}

void TrendPredictor::Reset() {
    for (auto& pair : m_series) {
        pair.second.points.clear();
        pair.second.analysisDirty = true;
        pair.second.smoothing = ExponentialSmoothingState();
        pair.second.lastDirection = TrendDirection::Unknown;
        pair.second.volatility = 0.0;
        pair.second.forecasts.clear();
    }
    m_anomalies.clear();
    m_lastDirections.clear();
    m_cachedVolatility = 0.0;
    m_cachedHealthScore = 100.0;
    m_healthDirty = true;
}

// ============================================================================
// Data Recording
// ============================================================================

void TrendPredictor::InitializeSeries(DataSeriesType type, const std::string& name) {
    TimeSeriesState state;
    state.type = type;
    state.name = name;
    state.points.reserve(m_config.defaultWindowSize);
    state.maxSize = m_config.defaultWindowSize;
    state.analysisDirty = true;
    state.smoothing.alpha = m_config.smoothingAlpha;
    state.smoothing.beta = m_config.smoothingBeta;
    state.lastDirection = TrendDirection::Unknown;
    state.volatility = 0.0;
    m_series[type] = state;
    m_lastDirections[type] = TrendDirection::Unknown;
}

TrendPredictor::TimeSeriesState& TrendPredictor::GetOrCreateSeries(DataSeriesType type) {
    auto it = m_series.find(type);
    if (it == m_series.end()) {
        std::string name;
        switch (type) {
            case DataSeriesType::FPS: name = "FPS"; break;
            case DataSeriesType::FrameTime: name = "Frame Time"; break;
            case DataSeriesType::Memory: name = "Memory"; break;
            case DataSeriesType::GPUUtilization: name = "GPU Utilization"; break;
            case DataSeriesType::Temperature: name = "Temperature"; break;
            default: name = "Custom"; break;
        }
        InitializeSeries(type, name);
        return m_series[type];
    }
    return it->second;
}

const ProfilerCore::TrendPredictor::TimeSeriesState* TrendPredictor::GetSeriesPtr(
    DataSeriesType type) const 
{
    auto it = m_series.find(type);
    return (it != m_series.end()) ? &it->second : nullptr;
}

void TrendPredictor::RecordDataPoint(DataSeriesType type, int frameNumber, double value) {
    TimeSeriesState& series = GetOrCreateSeries(type);
    
    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    DataPoint point;
    point.timestamp = timestamp;
    point.frameNumber = frameNumber;
    point.value = value;
    point.rawValue = value;
    
    // Apply exponential smoothing if we have enough data
    if (!series.points.empty()) {
        double prevValue = series.smoothing.level > 0 ? series.smoothing.level : series.points.back().value;
        series.smoothing.level = m_config.smoothingAlpha * value + (1.0 - m_config.smoothingAlpha) * prevValue;
        series.smoothing.trend = m_config.smoothingBeta * (series.smoothing.level - prevValue) 
                                 + (1.0 - m_config.smoothingBeta) * series.smoothing.trend;
        point.value = series.smoothing.level;
    } else {
        series.smoothing.level = value;
        series.smoothing.trend = 0.0;
    }
    
    series.points.push_back(point);
    
    // Trim to max size
    TrimSeries(series);
    
    // Detect anomalies
    DetectAnomaliesInSeries(series);
    
    // Mark analysis dirty
    series.analysisDirty = true;
    m_healthDirty = true;
}

void TrendPredictor::RecordFPS(int frameNumber, double fps) {
    RecordDataPoint(DataSeriesType::FPS, frameNumber, fps);
}

void TrendPredictor::RecordFrameTime(int frameNumber, double frameTimeMs) {
    RecordDataPoint(DataSeriesType::FrameTime, frameNumber, frameTimeMs);
}

void TrendPredictor::RecordMemory(int frameNumber, size_t memoryBytes) {
    RecordDataPoint(DataSeriesType::Memory, frameNumber, static_cast<double>(memoryBytes));
}

void TrendPredictor::RecordGPUUtilization(int frameNumber, double utilizationPercent) {
    RecordDataPoint(DataSeriesType::GPUUtilization, frameNumber, utilizationPercent);
}

void TrendPredictor::RecordCPUTemperature(int frameNumber, double tempCelsius) {
    // Store in temperature series with prefix (could expand to separate CPU/GPU series)
    RecordDataPoint(DataSeriesType::Temperature, frameNumber, tempCelsius);
}

void TrendPredictor::RecordGPUTemperature(int frameNumber, double tempCelsius) {
    RecordDataPoint(DataSeriesType::Temperature, frameNumber, tempCelsius);
}

void TrendPredictor::RecordFrame(int frameNumber, double fps, double frameTimeMs,
                                  size_t memoryBytes, double gpuUtil,
                                  double cpuTemp, double gpuTemp) {
    RecordFPS(frameNumber, fps);
    RecordFrameTime(frameNumber, frameTimeMs);
    RecordMemory(frameNumber, memoryBytes);
    if (gpuUtil > 0) RecordGPUUtilization(frameNumber, gpuUtil);
    if (cpuTemp > 0) RecordCPUTemperature(frameNumber, cpuTemp);
    if (gpuTemp > 0) RecordGPUTemperature(frameNumber, gpuTemp);
}

void TrendPredictor::TrimSeries(TimeSeriesState& series) {
    if (series.points.size() > series.maxSize) {
        series.points.erase(series.points.begin(), 
                          series.points.end() - series.maxSize);
    }
}

// ============================================================================
// Analysis
// ============================================================================

void TrendPredictor::UpdateSeriesState(TimeSeriesState& series) {
    if (!series.analysisDirty || series.points.size() < 2) {
        return;
    }
    
    const auto& pts = series.points;
    size_t n = pts.size();
    
    // Compute linear regression
    std::vector<double> x(n), y(n);
    for (size_t i = 0; i < n; i++) {
        x[i] = static_cast<double>(pts[i].frameNumber);
        y[i] = pts[i].value;
    }
    
    series.analysis.regression = LinearRegression(x, y);
    
    // Direction and confidence
    series.analysis.direction = DetermineDirection(
        series.analysis.regression.slope, 
        series.analysis.regression.r2
    );
    series.analysis.confidence = series.analysis.regression.r2;
    series.analysis.slopePerSample = series.analysis.regression.slope;
    series.analysis.slopePerSecond = series.analysis.regression.slopePerSecond;
    series.analysis.r2 = series.analysis.regression.r2;
    
    // Volatility
    series.volatility = ComputeVolatility(pts);
    series.analysis.avgVolatility = series.volatility;
    
    // Generate description
    std::ostringstream desc;
    desc.precision(2);
    desc << std::fixed;
    
    switch (series.analysis.direction) {
        case TrendDirection::Improving:
            desc << "Trending upward (";
            desc << "+" << std::showpos << series.analysis.regression.slopePerSecond << "/s";
            desc << ", R2=" << series.analysis.regression.r2 << ")";
            break;
        case TrendDirection::Declining:
            desc << "Trending downward (";
            desc << series.analysis.regression.slopePerSecond << "/s";
            desc << ", R2=" << series.analysis.regression.r2 << ")";
            break;
        case TrendDirection::Stable:
            desc << "Stable performance (variance=" << series.volatility << ")";
            break;
        case TrendDirection::Volatile:
            desc << "Highly volatile (avg change=" << series.volatility << ")";
            break;
        default:
            desc << "Insufficient data for trend analysis";
            break;
    }
    series.analysis.description = desc.str();
    
    // Check for trend change
    if (series.lastDirection != TrendDirection::Unknown &&
        series.lastDirection != series.analysis.direction) {
        NotifyTrendChange(series.type, series.lastDirection, series.analysis.direction);
    }
    series.lastDirection = series.analysis.direction;
    
    series.analysisDirty = false;
}

TrendAnalysis TrendPredictor::AnalyzeSeries(DataSeriesType type) {
    TimeSeriesState& series = GetOrCreateSeries(type);
    UpdateSeriesState(series);
    return series.analysis;
}

std::unordered_map<DataSeriesType, TrendAnalysis> TrendPredictor::AnalyzeAll() {
    std::unordered_map<DataSeriesType, TrendAnalysis> results;
    for (auto& pair : m_series) {
        UpdateSeriesState(pair.second);
        results[pair.first] = pair.second.analysis;
    }
    return results;
}

// ============================================================================
// Prediction
// ============================================================================

ForecastResult TrendPredictor::AdaptiveForecast(const TimeSeriesState& series,
                                                  int framesAhead) const {
    if (series.points.size() < m_config.minSamplesForPrediction) {
        return ForecastResult{};
    }
    
    // Choose algorithm based on data characteristics
    double r2 = series.analysis.regression.r2;
    double volatility = series.volatility;
    
    if (r2 > m_config.highConfidenceThreshold && volatility < 2.0) {
        // Data is very linear and stable - use linear regression
        return ForecastSeries(series.type, framesAhead);
    } else if (volatility > 5.0) {
        // High volatility - use exponential smoothing for responsiveness
        return ExponentialSmoothingForecast(series, framesAhead);
    } else {
        // Moderate - use weighted moving average
        return WeightedMovingForecast(series, framesAhead);
    }
}

ForecastResult TrendPredictor::ForecastSeries(DataSeriesType type, int framesAhead) {
    const TimeSeriesState* series = GetSeriesPtr(type);
    if (!series || series->points.size() < m_config.minSamplesForPrediction) {
        return ForecastResult{};
    }
    
    // Use adaptive forecasting
    ForecastResult result = AdaptiveForecast(*series, framesAhead);
    result.algorithm = PredictionAlgorithm::LinearRegression;
    
    // Calculate confidence interval (widens with prediction distance)
    double baseStdDev = series->volatility;
    double horizonFactor = std::sqrt(static_cast<double>(framesAhead));
    double confidenceWidth = baseStdDev * horizonFactor * 2.0; // ~95% CI
    
    // Adjust confidence based on R2
    double confidence = series->analysis.regression.r2;
    confidence *= (1.0 / (1.0 + horizonFactor * 0.05)); // Confidence decreases with distance
    
    if (series->points.size() > 0) {
        const DataPoint& last = series->points.back();
        result.forecastTime = last.timestamp + (framesAhead * 16); // Approximate ms per frame
        result.confidenceLevel = std::max(0.0, std::min(1.0, confidence));
        result.confidenceLow = result.predictedValue - confidenceWidth;
        result.confidenceHigh = result.predictedValue + confidenceWidth;
        
        // Don't go below zero for metrics that can't be negative
        if (type == DataSeriesType::FPS || type == DataSeriesType::GPUUtilization) {
            result.confidenceLow = std::max(0.0, result.confidenceLow);
        }
    }
    
    return result;
}

ForecastResult TrendPredictor::ExponentialSmoothingForecast(
    const TimeSeriesState& series, int framesAhead) const 
{
    ForecastResult result;
    result.algorithm = PredictionAlgorithm::ExponentialSmoothing;
    
    if (series.points.empty()) return result;
    
    const DataPoint& last = series.points.back();
    double level = series.smoothing.level > 0 ? series.smoothing.level : last.value;
    double trend = series.smoothing.trend;
    
    // Double exponential smoothing (Holt's method)
    result.predictedValue = level + framesAhead * trend;
    result.forecastTime = last.timestamp + framesAhead * 16; // Approx ms per frame
    
    // Confidence interval widens with forecast distance
    double volatility = series.volatility * std::sqrt(static_cast<double>(framesAhead));
    result.confidenceLevel = std::max(0.0, 1.0 - volatility / 100.0);
    result.confidenceLow = result.predictedValue - volatility * 2;
    result.confidenceHigh = result.predictedValue + volatility * 2;
    
    return result;
}

ForecastResult TrendPredictor::WeightedMovingForecast(
    const TimeSeriesState& series, int framesAhead) const 
{
    ForecastResult result;
    result.algorithm = PredictionAlgorithm::WeightedMoving;
    
    if (series.points.size() < 5) return result;
    
    // Use last N points with exponential weights
    size_t n = std::min(series.points.size(), static_cast<size_t>(50));
    const auto& pts = series.points;
    
    double weightedSum = 0.0;
    double weightSum = 0.0;
    double recentTrend = 0.0;
    
    // Calculate weighted average and trend
    for (size_t i = pts.size() - n; i < pts.size(); i++) {
        double weight = std::exp(-0.05 * static_cast<double>(pts.size() - 1 - i));
        weightedSum += pts[i].value * weight;
        weightSum += weight;
        
        if (i > pts.size() - n) {
            recentTrend += (pts[i].value - pts[i-1].value) * weight;
        }
    }
    
    double weightedAvg = weightedSum / weightSum;
    recentTrend /= weightSum;
    
    result.predictedValue = weightedAvg + framesAhead * recentTrend;
    
    if (!pts.empty()) {
        const DataPoint& last = pts.back();
        result.forecastTime = last.timestamp + framesAhead * 16;
    }
    
    result.confidenceLevel = std::max(0.0, 1.0 - (series.volatility / 50.0));
    double ciWidth = series.volatility * std::sqrt(static_cast<double>(framesAhead));
    result.confidenceLow = result.predictedValue - ciWidth;
    result.confidenceHigh = result.predictedValue + ciWidth;
    
    return result;
}

ForecastResult TrendPredictor::ForecastFPS(int framesAhead) {
    return ForecastSeries(DataSeriesType::FPS, framesAhead);
}

ForecastResult TrendPredictor::ForecastFrameTime(int framesAhead) {
    return ForecastSeries(DataSeriesType::FrameTime, framesAhead);
}

PerformancePrediction TrendPredictor::PredictPerformance(int framesAhead, double targetFPS) {
    PerformancePrediction pred;
    
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    pred.timestamp = now;
    pred.currentFrame = m_series[DataSeriesType::FPS].points.empty() ? 
                        0 : m_series[DataSeriesType::FPS].points.back().frameNumber;
    
    // FPS prediction
    const TimeSeriesState* fpsSeries = GetSeriesPtr(DataSeriesType::FPS);
    if (fpsSeries && fpsSeries->points.size() >= m_config.minSamplesForPrediction) {
        ForecastResult fpsForecast = ForecastFPS(framesAhead);
        pred.predictedFPSInFrames = fpsForecast.predictedValue;
        
        // Calculate FPS drop rate
        if (fpsSeries->points.size() >= 2) {
            const auto& pts = fpsSeries->points;
            size_t n = pts.size();
            double timeSpanSec = (pts.back().timestamp - pts.front().timestamp) / 1000.0;
            if (timeSpanSec > 0) {
                pred.fpsDropRate = (pts.front().value - pts.back().value) / timeSpanSec;
            }
        }
        
        // Estimate when FPS will drop below target
        if (pred.fpsDropRate > 0 && pred.predictedFPSInFrames.has_value()) {
            double currentFPS = fpsForecast.predictedValue;
            double framesToTarget = (currentFPS - targetFPS) / pred.fpsDropRate;
            if (framesToTarget > 0) {
                pred.framesUntilBelowTarget = static_cast<int>(framesToTarget);
            } else {
                pred.framesUntilBelowTarget = -1; // Won't go below
            }
        } else {
            pred.framesUntilBelowTarget = -1;
        }
    }
    
    // Memory leak prediction
    pred = PredictMemory(framesAhead, m_config.defaultMemoryLimitMB);
    
    // Health scoring
    pred.healthScore = CalculateHealthScore();
    pred.stabilityScore = CalculateStabilityScore(DataSeriesType::FPS);
    
    // Generate recommendations
    if (pred.framesUntilBelowTarget > 0 && pred.framesUntilBelowTarget < 600) {
        std::ostringstream msg;
        msg << "FPS expected to drop below " << targetFPS << " in ~" 
            << pred.framesUntilBelowTarget << " frames. Consider performance optimization.";
        pred.recommendations.push_back(msg.str());
    }
    
    if (pred.predictedOOMFrame.has_value() && pred.predictedOOMFrame.value() > 0) {
        std::ostringstream msg;
        msg << "Memory exhaustion predicted in ~" << (pred.predictedOOMFrame.value() / 60) 
            << " seconds. Review memory allocation patterns.";
        pred.recommendations.push_back(msg.str());
    }
    
    if (pred.healthScore < 70.0) {
        pred.recommendations.push_back("System health declining. Monitor for resource contention.");
    }
    
    if (pred.stabilityScore < 70.0) {
        pred.recommendations.push_back("Frame time stability is poor. Check for GC, asset loading, or CPU spikes.");
    }
    
    return pred;
}

PerformancePrediction TrendPredictor::PredictMemory(int framesAhead, double memoryLimitMB) {
    PerformancePrediction pred;
    
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    pred.timestamp = now;
    pred.currentFrame = m_series[DataSeriesType::Memory].points.empty() ?
                        0 : m_series[DataSeriesType::Memory].points.back().frameNumber;
    
    const TimeSeriesState* memSeries = GetSeriesPtr(DataSeriesType::Memory);
    if (!memSeries || memSeries->points.size() < m_config.minSamplesForPrediction) {
        return pred;
    }
    
    const auto& pts = memSeries->points;
    double currentMemoryMB = pts.back().value / (1024.0 * 1024.0);
    
    // Calculate memory growth rate
    LinearRegressionResult reg = FitLinearRegressionMemory();
    if (reg.valid) {
        pred.memoryGrowthRate = reg.slope; // Bytes per frame
        pred.memoryGrowthRatePerSec = reg.slopePerSecond; // Bytes per second
        
        // Estimate OOM frame
        double memoryLimitBytes = memoryLimitMB * 1024.0 * 1024.0;
        double currentBytes = pts.back().value;
        
        if (reg.slope > 0) {
            double bytesToOOM = memoryLimitBytes - currentBytes;
            double framesToOOM = bytesToOOM / reg.slope;
            pred.predictedOOMFrame = static_cast<int64_t>(pts.back().frameNumber + framesToOOM);
            
            if (reg.slopePerSecond > 0) {
                double secondsToOOM = bytesToOOM / reg.slopePerSecond;
                pred.predictedOOMSeconds = static_cast<int64_t>(secondsToOOM);
            }
        }
        
        // Check for memory leak pattern
        if (reg.slope > 0 && reg.r2 > 0.8) {
            pred.anomalyDetected = true;
            pred.anomalyType = "memory_leak";
            pred.anomalyMagnitude = reg.slope * 3600.0 / (1024.0 * 1024.0); // MB/hour
        }
    }
    
    return pred;
}

PerformancePrediction TrendPredictor::PredictThermal(int secondsAhead) {
    PerformancePrediction pred;
    
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    pred.timestamp = now;
    
    const TimeSeriesState* tempSeries = GetSeriesPtr(DataSeriesType::Temperature);
    if (!tempSeries || tempSeries->points.size() < m_config.minSamplesForPrediction) {
        return pred;
    }
    
    const auto& pts = tempSeries->points;
    
    // Simple linear regression for temperature trend
    std::vector<double> x(pts.size()), y(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        x[i] = static_cast<double>(pts[i].timestamp) / 1000.0; // seconds
        y[i] = pts[i].value;
    }
    
    LinearRegressionResult reg = LinearRegression(x, y);
    if (reg.valid) {
        double currentTemp = pts.back().value;
        double secondsElapsed = (pts.back().timestamp - pts.front().timestamp) / 1000.0;
        
        if (secondsElapsed > 0) {
            pred.thermalRiseRate = reg.slope; // Degrees per second
            pred.predictedCPUTemp = currentTemp + reg.slope * secondsAhead;
            
            // Check for thermal runaway
            if (reg.slope > 0.5 && reg.r2 > 0.7) {
                pred.anomalyDetected = true;
                pred.anomalyType = "thermal_rise";
                pred.anomalyMagnitude = reg.slope;
            }
        }
    }
    
    return pred;
}

// ============================================================================
// Health Metrics
// ============================================================================

double TrendPredictor::CalculateVolatility() const {
    if (m_healthDirty) {
        const_cast<TrendPredictor*>(this)->m_cachedVolatility = 0.0;
        
        int count = 0;
        for (const auto& pair : m_series) {
            if (pair.second.points.size() >= 2) {
                m_cachedVolatility += pair.second.volatility;
                count++;
            }
        }
        
        if (count > 0) {
            m_cachedVolatility /= count;
        }
        
        const_cast<TrendPredictor*>(this)->m_healthDirty = false;
    }
    
    return m_cachedVolatility;
}

double TrendPredictor::CalculateHealthScore() const {
    if (m_healthDirty) {
        CalculateVolatility(); // Ensures cache is populated
    }
    
    if (m_cachedHealthScore < 0) {
        // Compute on demand
        double score = 100.0;
        
        // Penalize for volatility
        double vol = m_cachedVolatility;
        if (vol > 1.0) {
            score -= std::min(30.0, (vol - 1.0) * 5.0);
        }
        
        // Penalize for anomalies
        for (const auto& anomaly : m_anomalies) {
            if (!anomaly.acknowledged) {
                if (anomaly.type == "spike" || anomaly.type == "drop") {
                    score -= 5.0;
                } else if (anomaly.type == "freeze") {
                    score -= 15.0;
                }
            }
        }
        
        // Penalize for declining trends
        for (const auto& pair : m_series) {
            if (pair.second.analysis.direction == TrendDirection::Declining &&
                pair.second.analysis.regression.r2 > 0.7) {
                score -= 10.0;
            }
        }
        
        m_cachedHealthScore = std::max(0.0, std::min(100.0, score));
    }
    
    return m_cachedHealthScore;
}

double TrendPredictor::CalculateStabilityScore(DataSeriesType type) const {
    const TimeSeriesState* series = GetSeriesPtr(type);
    if (!series || series->points.size() < 2) {
        return 100.0;
    }
    
    double volatility = series->volatility;
    double r2 = series->analysis.regression.r2;
    
    // Base score on volatility
    double score = 100.0;
    
    // Penalize high volatility
    if (volatility > 0.5) {
        score -= std::min(40.0, (volatility - 0.5) * 10.0);
    }
    
    // Bonus for consistent trends (high R2 means predictable)
    if (r2 > 0.9) {
        score += 10.0;
    }
    
    // Penalize volatile trends
    if (series->analysis.direction == TrendDirection::Volatile) {
        score -= 20.0;
    }
    
    return std::max(0.0, std::min(100.0, score));
}

TrendDirection TrendPredictor::GetOverallTrend() const {
    int improving = 0, declining = 0, stable = 0, volatile_count = 0;
    
    for (const auto& pair : m_series) {
        switch (pair.second.analysis.direction) {
            case TrendDirection::Improving: improving++; break;
            case TrendDirection::Declining: declining++; break;
            case TrendDirection::Stable: stable++; break;
            case TrendDirection::Volatile: volatile_count++; break;
            default: break;
        }
    }
    
    int total = improving + declining + stable + volatile_count;
    if (total == 0) return TrendDirection::Unknown;
    
    if (volatile_count > total / 2) return TrendDirection::Volatile;
    if (improving > declining && improving > stable) return TrendDirection::Improving;
    if (declining > improving && declining > stable) return TrendDirection::Declining;
    return TrendDirection::Stable;
}

// ============================================================================
// Anomaly Detection
// ============================================================================

void TrendPredictor::DetectAnomaliesInSeries(TimeSeriesState& series) {
    if (series.points.size() < 10) return;
    
    const auto& pts = series.points;
    size_t n = pts.size();
    
    // Calculate mean and std dev of recent values
    std::vector<double> values(n);
    for (size_t i = 0; i < n; i++) {
        values[i] = pts[i].value;
    }
    
    double mean = ComputeMean(values);
    double stdDev = ComputeStdDev(values);
    
    if (stdDev < 0.001) return; // Avoid division by zero
    
    // Check last few points for anomalies
    size_t checkStart = (n > 10) ? n - 10 : 0;
    for (size_t i = checkStart; i < n; i++) {
        double deviation = std::abs(pts[i].value - mean) / stdDev;
        
        if (deviation > m_config.anomalyStdDevMultiplier) {
            // Check if we already reported this anomaly recently
            bool recentExists = false;
            for (const auto& existing : m_anomalies) {
                if (existing.seriesType == series.type &&
                    std::abs(existing.frameNumber - pts[i].frameNumber) < 
                    static_cast<int>(m_config.anomalyMinFramesBetween)) {
                    recentExists = true;
                    break;
                }
            }
            
            if (!recentExists) {
                Anomaly anomaly;
                anomaly.timestamp = pts[i].timestamp;
                anomaly.frameNumber = pts[i].frameNumber;
                anomaly.seriesType = series.type;
                anomaly.value = pts[i].value;
                anomaly.expectedValue = mean;
                anomaly.deviation = deviation;
                anomaly.acknowledged = false;
                
                // Classify anomaly type
                if (pts[i].value > mean) {
                    anomaly.type = "spike";
                } else {
                    anomaly.type = "drop";
                }
                
                m_anomalies.push_back(anomaly);
                
                // Fire callback
                if (m_anomalyCallback) {
                    m_anomalyCallback(anomaly);
                }
            }
        }
    }
}

std::vector<TrendPredictor::Anomaly> TrendPredictor::DetectAnomalies() {
    for (auto& pair : m_series) {
        DetectAnomaliesInSeries(pair.second);
    }
    return m_anomalies;
}

std::vector<TrendPredictor::Anomaly> TrendPredictor::GetAnomalies(DataSeriesType type) const {
    std::vector<Anomaly> result;
    for (const auto& a : m_anomalies) {
        if (a.seriesType == type) {
            result.push_back(a);
        }
    }
    return result;
}

void TrendPredictor::AcknowledgeAnomaly(int64_t timestamp) {
    for (auto& a : m_anomalies) {
        if (a.timestamp == timestamp) {
            a.acknowledged = true;
            return;
        }
    }
}

void TrendPredictor::ClearAnomalies() {
    m_anomalies.clear();
}

void TrendPredictor::NotifyTrendChange(DataSeriesType type, 
                                        TrendDirection oldDir, 
                                        TrendDirection newDir) {
    m_lastDirections[type] = newDir;
    if (m_trendChangeCallback) {
        m_trendChangeCallback(type, oldDir, newDir);
    }
}

// ============================================================================
// Regression & Correlation
// ============================================================================

LinearRegressionResult TrendPredictor::LinearRegression(const std::vector<double>& x,
                                                        const std::vector<double>& y) const {
    LinearRegressionResult result;
    result.valid = false;
    
    if (x.size() != y.size() || x.size() < 2) {
        return result;
    }
    
    size_t n = x.size();
    double xMean = ComputeMean(x);
    double yMean = ComputeMean(y);
    
    double numerator = 0.0;
    double denominator = 0.0;
    double ssTot = 0.0;
    double ssRes = 0.0;
    
    for (size_t i = 0; i < n; i++) {
        double xDiff = x[i] - xMean;
        double yDiff = y[i] - yMean;
        numerator += xDiff * yDiff;
        denominator += xDiff * xDiff;
        ssTot += yDiff * yDiff;
    }
    
    if (denominator < 1e-10) {
        return result;
    }
    
    result.slope = numerator / denominator;
    result.intercept = yMean - result.slope * xMean;
    result.sampleCount = static_cast<int>(n);
    
    // R-squared
    for (size_t i = 0; i < n; i++) {
        double predicted = result.slope * x[i] + result.intercept;
        ssRes += (y[i] - predicted) * (y[i] - predicted);
    }
    
    result.r2 = (ssTot > 1e-10) ? 1.0 - (ssRes / ssTot) : 0.0;
    
    // Standard error
    if (n > 2) {
        result.standardError = std::sqrt(ssRes / static_cast<double>(n - 2));
    }
    
    // Slope per second (if x is timestamp)
    double timeSpan = x.back() - x.front();
    if (timeSpan > 0) {
        result.slopePerSecond = result.slope / timeSpan;
    }
    
    // Next predicted value
    result.predictedNext = result.slope * x.back() + result.intercept;
    result.valid = true;
    
    return result;
}

LinearRegressionResult TrendPredictor::FitLinearRegression(
    const std::vector<DataPoint>& points) const 
{
    std::vector<double> x(points.size()), y(points.size());
    for (size_t i = 0; i < points.size(); i++) {
        x[i] = static_cast<double>(points[i].frameNumber);
        y[i] = points[i].value;
    }
    return LinearRegression(x, y);
}

LinearRegressionResult TrendPredictor::FitLinearRegressionFPS() const {
    const TimeSeriesState* series = GetSeriesPtr(DataSeriesType::FPS);
    if (!series || series->points.size() < 2) {
        return LinearRegressionResult{};
    }
    return FitLinearRegression(series->points);
}

LinearRegressionResult TrendPredictor::FitLinearRegressionMemory() const {
    const TimeSeriesState* series = GetSeriesPtr(DataSeriesType::Memory);
    if (!series || series->points.size() < 2) {
        return LinearRegressionResult{};
    }
    return FitLinearRegression(series->points);
}

double TrendPredictor::CalculateCorrelation(DataSeriesType typeA, DataSeriesType typeB) const {
    const TimeSeriesState* seriesA = GetSeriesPtr(typeA);
    const TimeSeriesState* seriesB = GetSeriesPtr(typeB);
    
    if (!seriesA || !seriesB || seriesA->points.empty() || seriesB->points.empty()) {
        return 0.0;
    }
    
    // Find common frame range
    size_t nA = seriesA->points.size();
    size_t nB = seriesB->points.size();
    
    std::vector<double> valuesA, valuesB;
    size_t startIdx = std::max(nA, nB) - std::min(nA, nB);
    
    for (size_t i = startIdx; i < nA && (i - startIdx) < nB; i++) {
        valuesA.push_back(seriesA->points[i].value);
        valuesB.push_back(seriesB->points[i - startIdx].value);
    }
    
    if (valuesA.size() < 2) return 0.0;
    
    double meanA = ComputeMean(valuesA);
    double meanB = ComputeMean(valuesB);
    
    double numerator = 0.0;
    double denomA = 0.0;
    double denomB = 0.0;
    
    for (size_t i = 0; i < valuesA.size(); i++) {
        double diffA = valuesA[i] - meanA;
        double diffB = valuesB[i] - meanB;
        numerator += diffA * diffB;
        denomA += diffA * diffA;
        denomB += diffB * diffB;
    }
    
    double denom = std::sqrt(denomA * denomB);
    return (denom > 1e-10) ? (numerator / denom) : 0.0;
}

// ============================================================================
// Series Access
// ============================================================================

const std::vector<DataPoint>& TrendPredictor::GetSeries(DataSeriesType type) const {
    static const std::vector<DataPoint> empty;
    const TimeSeriesState* series = GetSeriesPtr(type);
    return series ? series->points : empty;
}

std::vector<DataPoint> TrendPredictor::GetRecentSeries(DataSeriesType type, int count) const {
    const TimeSeriesState* series = GetSeriesPtr(type);
    if (!series || series->points.empty()) return {};
    
    const auto& pts = series->points;
    size_t start = (pts.size() > static_cast<size_t>(count)) ? 
                    pts.size() - count : 0;
    
    return std::vector<DataPoint>(pts.begin() + start, pts.end());
}

std::vector<DataPoint> TrendPredictor::GetSeriesInRange(DataSeriesType type,
                                                         int64_t startTime, 
                                                         int64_t endTime) const {
    const TimeSeriesState* series = GetSeriesPtr(type);
    if (!series) return {};
    
    std::vector<DataPoint> result;
    for (const auto& pt : series->points) {
        if (pt.timestamp >= startTime && pt.timestamp <= endTime) {
            result.push_back(pt);
        }
    }
    return result;
}

size_t TrendPredictor::GetSeriesSize(DataSeriesType type) const {
    const TimeSeriesState* series = GetSeriesPtr(type);
    return series ? series->points.size() : 0;
}

bool TrendPredictor::HasEnoughData(DataSeriesType type) const {
    return GetSeriesSize(type) >= m_config.minSamplesForPrediction;
}

// ============================================================================
// Helper Utilities
// ============================================================================

double TrendPredictor::ComputeMean(const std::vector<double>& values) const {
    if (values.empty()) return 0.0;
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double TrendPredictor::ComputeStdDev(const std::vector<double>& values) const {
    if (values.size() < 2) return 0.0;
    double mean = ComputeMean(values);
    double sqSum = 0.0;
    for (double v : values) {
        double diff = v - mean;
        sqSum += diff * diff;
    }
    return std::sqrt(sqSum / static_cast<double>(values.size()));
}

double TrendPredictor::ComputeStdDev(const std::vector<DataPoint>& points) const {
    std::vector<double> values(points.size());
    for (size_t i = 0; i < points.size(); i++) {
        values[i] = points[i].value;
    }
    return ComputeStdDev(values);
}

TrendDirection TrendPredictor::DetermineDirection(double slope, double r2) const {
    if (r2 < m_config.lowConfidenceThreshold) {
        return TrendDirection::Unknown;
    }
    
    double absSlopePerFrame = std::abs(slope);
    
    if (absSlopePerFrame < m_config.stableThresholdPerFrame) {
        return TrendDirection::Stable;
    }
    
    // Check for high variance despite slope
    double slopeMagnitude = std::abs(slope);
    if (r2 < 0.5 && slopeMagnitude > m_config.stableThresholdPerFrame * 10) {
        return TrendDirection::Volatile;
    }
    
    return slope > 0 ? TrendDirection::Improving : TrendDirection::Declining;
}

double TrendPredictor::ComputeVolatility(const std::vector<DataPoint>& points) const {
    if (points.size() < 2) return 0.0;
    
    double sumAbsDiff = 0.0;
    for (size_t i = 1; i < points.size(); i++) {
        sumAbsDiff += std::abs(points[i].value - points[i-1].value);
    }
    
    return sumAbsDiff / static_cast<double>(points.size() - 1);
}

// ============================================================================
// Export
// ============================================================================

std::string TrendPredictor::ExportToJSON() const {
    std::ostringstream ss;
    ss << "{\"config\":{";
    ss << "\"windowSize\":" << m_config.defaultWindowSize << ",";
    ss << "\"minSamples\":" << m_config.minSamplesForPrediction << ",";
    ss << "\"targetFPS\":" << m_config.defaultTargetFPS << "},";
    
    ss << "\"series\":{";
    bool first = true;
    for (const auto& pair : m_series) {
        if (!first) ss << ",";
        first = false;
        ss << "\"" << static_cast<int>(pair.first) << "\":{";
        ss << "\"name\":\"" << pair.second.name << "\",";
        ss << "\"size\":" << pair.second.points.size() << ",";
        ss << "\"current\":" << (pair.second.points.empty() ? 0.0 : 
                                  pair.second.points.back().value) << ",";
        ss << "\"direction\":\"" << static_cast<int>(pair.second.analysis.direction) << "\"";
        ss << "}";
    }
    ss << "},";
    
    ss << "\"anomalies\":[";
    for (size_t i = 0; i < m_anomalies.size(); i++) {
        if (i > 0) ss << ",";
        const auto& a = m_anomalies[i];
        ss << "{\"frame\":" << a.frameNumber;
        ss << ",\"type\":\"" << a.type << "\"";
        ss << ",\"deviation\":" << a.deviation << "}";
    }
    ss << "]}";
    
    return ss.str();
}

std::string TrendPredictor::ExportAnalysisToJSON(DataSeriesType type) const {
    std::ostringstream ss;
    const TimeSeriesState* series = GetSeriesPtr(type);
    if (!series) {
        return "{}";
    }
    
    const auto& analysis = series->analysis;
    ss << "{\"series\":\"" << series->name << "\",";
    ss << "\"direction\":\"" << static_cast<int>(analysis.direction) << "\",";
    ss << "\"confidence\":" << std::fixed << std::setprecision(3) << analysis.confidence << ",";
    ss << "\"slopePerFrame\":" << analysis.slopePerSample << ",";
    ss << "\"slopePerSecond\":" << analysis.slopePerSecond << ",";
    ss << "\"r2\":" << analysis.r2 << ",";
    ss << "\"volatility\":" << analysis.avgVolatility << ",";
    ss << "\"description\":\"" << analysis.description << "\"}";
    
    return ss.str();
}

std::string TrendPredictor::ExportPredictionsToJSON() const {
    std::ostringstream ss;
    
    PerformancePrediction pred = PredictPerformance(
        m_config.predictionFrames, m_config.defaultTargetFPS);
    
    ss << "{\"predictionFrames\":" << m_config.predictionFrames << ",";
    ss << "\"targetFPS\":" << m_config.defaultTargetFPS << ",";
    
    if (pred.predictedFPSInFrames.has_value()) {
        ss << "\"predictedFPS\":" << std::fixed << std::setprecision(2) 
           << pred.predictedFPSInFrames.value() << ",";
    }
    
    ss << "\"framesUntilBelowTarget\":" << pred.framesUntilBelowTarget << ",";
    ss << "\"fpsDropRate\":" << pred.fpsDropRate << ",";
    ss << "\"healthScore\":" << pred.healthScore << ",";
    ss << "\"stabilityScore\":" << pred.stabilityScore << ",";
    ss << "\"recommendations\":[";
    for (size_t i = 0; i < pred.recommendations.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << pred.recommendations[i] << "\"";
    }
    ss << "]}";
    
    return ss.str();
}

std::string TrendPredictor::ExportAnomaliesToJSON() const {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < m_anomalies.size(); i++) {
        if (i > 0) ss << ",";
        const auto& a = m_anomalies[i];
        ss << "{\"timestamp\":" << a.timestamp << ",";
        ss << "\"frame\":" << a.frameNumber << ",";
        ss << "\"type\":\"" << a.type << "\",";
        ss << "\"value\":" << a.value << ",";
        ss << "\"expected\":" << a.expectedValue << ",";
        ss << "\"deviation\":" << std::fixed << std::setprecision(2) << a.deviation << ",";
        ss << "\"acknowledged\":" << (a.acknowledged ? "true" : "false") << "}";
    }
    ss << "]";
    return ss.str();
}

std::string TrendPredictor::ExportFullReport() const {
    std::ostringstream ss;
    ss << "{\"report\":{";
    ss << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count() << ",";
    
    // Overall health
    ss << "\"healthScore\":" << CalculateHealthScore() << ",";
    ss << "\"volatility\":" << CalculateVolatility() << ",";
    ss << "\"overallTrend\":\"" << static_cast<int>(GetOverallTrend()) << "\",";
    
    // Series summaries
    ss << "\"series\":{";
    bool first = true;
    for (const auto& pair : m_series) {
        if (!first) ss << ",";
        first = false;
        ss << "\"" << pair.first << "\":" << ExportAnalysisToJSON(pair.first);
    }
    ss << "},";
    
    // Anomalies
    ss << "\"anomalies\":" << ExportAnomaliesToJSON() << ",";
    
    // Recommendations
    PerformancePrediction pred = PredictPerformance(
        m_config.predictionFrames, m_config.defaultTargetFPS);
    ss << "\"recommendations\":[";
    for (size_t i = 0; i < pred.recommendations.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << pred.recommendations[i] << "\"";
    }
    ss << "]}}";
    
    return ss.str();
}

// ============================================================================
// Callbacks
// ============================================================================

void TrendPredictor::SetAnomalyCallback(AnomalyCallback callback) {
    m_anomalyCallback = callback;
}

void TrendPredictor::SetTrendChangeCallback(TrendChangeCallback callback) {
    m_trendChangeCallback = callback;
}

} // namespace ProfilerCore
