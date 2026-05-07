#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <optional>

namespace ProfilerCore {

/**
 * Prediction algorithm type
 */
enum class PredictionAlgorithm {
    LinearRegression = 0,       // Simple linear regression
    ExponentialSmoothing = 1,  // Exponential moving average
    WeightedMoving = 2,        // Weighted moving average
    Polynomial = 3,             // 2nd order polynomial fit
    Adaptive = 4                // Automatically select best algorithm
};

/**
 * Trend direction
 */
enum class TrendDirection {
    Unknown = 0,
    Improving = 1,
    Stable = 2,
    Declining = 3,
    Volatile = 4   // No clear direction
};

/**
 * Data series type for tracking
 */
enum class DataSeriesType {
    FPS = 0,
    FrameTime = 1,
    Memory = 2,
    GPUUtilization = 3,
    Temperature = 4,
    Custom = 99
};

/**
 * A single data point in a time series
 */
struct DataPoint {
    int64_t timestamp;    // Unix timestamp in ms
    int frameNumber;      // Frame number
    double value;         // The metric value
    double rawValue;      // Original value before smoothing
};

/**
 * Linear regression result
 */
struct LinearRegressionResult {
    double slope;              // Change per sample
    double slopePerSecond;     // Change per second
    double intercept;           // Y-intercept
    double r2;                  // R-squared (goodness of fit, 0-1)
    double standardError;       // Standard error of estimate
    bool valid;                 // Whether result is statistically valid
    size_t sampleCount;         // Number of samples used
    double predictedNext;       // Next predicted value
    double predictedInSeconds;  // Value predicted 1 second from now
};

/**
 * Exponential smoothing state
 */
struct ExponentialSmoothingState {
    double level = 0.0;     // Smoothed level
    double trend = 0.0;     // Smoothed trend
    double season = 0.0;    // Seasonal component (if enabled)
    double alpha = 0.3;     // Level smoothing factor (0-1)
    double beta = 0.1;      // Trend smoothing factor (0-1)
    double gamma = 0.0;     // Seasonal smoothing factor (0-1)
    int seasonPeriod = 0;   // Season period in samples (0 = no season)
};

/**
 * Forecast result with confidence intervals
 */
struct ForecastResult {
    int64_t forecastTime;       // Timestamp of forecast
    double predictedValue;      // Predicted value
    double confidenceLow;      // Lower bound (95% CI)
    double confidenceHigh;     // Upper bound (95% CI)
    double confidenceLevel;    // 0-1, how confident we are
    PredictionAlgorithm algorithm;
};

/**
 * Trend analysis result for a data series
 */
struct TrendAnalysis {
    DataSeriesType seriesType;
    TrendDirection direction;
    double confidence;              // 0-1 confidence in direction
    double slopePerSample;          // Change per frame
    double slopePerSecond;          // Change per second
    double r2;                      // Goodness of fit
    int volatileFrames;             // Number of erratic frames
    double avgVolatility;           // Average frame-to-frame change
    LinearRegressionResult regression;
    std::string description;        // Human-readable summary
};

/**
 * Performance prediction for FPS/memory
 */
struct PerformancePrediction {
    int64_t timestamp;
    int currentFrame;
    
    // FPS prediction
    std::optional<double> predictedFPSInFrames;   // Predicted FPS after N frames
    std::optional<double> predictedFPSInSeconds;  // Predicted FPS after N seconds
    int framesUntilBelowTarget;                  // -1 if won't go below
    double fpsDropRate;                          // FPS drop per second
    
    // Memory leak prediction
    std::optional<int64_t> predictedOOMFrame;    // Frame when OOM likely
    std::optional<int64_t> predictedOOMSeconds;  // Time until OOM
    double memoryGrowthRate;                      // Bytes per frame
    double memoryGrowthRatePerSec;               // Bytes per second
    
    // Thermal prediction
    std::optional<double> predictedCPUTemp;       // Predicted CPU temp in N seconds
    std::optional<double> predictedGPUTemp;      // Predicted GPU temp in N seconds
    double thermalRiseRate;                      // Degrees C per second
    
    // Anomaly detection
    bool anomalyDetected;
    int anomalyFrame;
    std::string anomalyType;      // "spike", "drop", "freeze"
    double anomalyMagnitude;      // How severe
    
    // Health score (0-100, higher is better)
    double healthScore;
    double stabilityScore;
    
    // Recommendations
    std::vector<std::string> recommendations;
};

/**
 * Time series state for one metric
 */
struct TimeSeriesState {
    DataSeriesType type;
    std::string name;
    std::vector<DataPoint> points;
    size_t maxSize;
    
    // Cached analysis
    TrendAnalysis analysis;
    bool analysisDirty;
    
    // Exponential smoothing state
    ExponentialSmoothingState smoothing;
    
    // Prediction history
    std::vector<ForecastResult> forecasts;
    
    TrendDirection lastDirection;
    double volatility;           // Average absolute change
};

/**
 * TrendPredictor - Comprehensive trend prediction and analysis module
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
 * 
 * Integration:
 *   - Feeds into PerformanceScorer (CalculateVolatility, CalculateHealthScore)
 *   - Feeds into AlertManager for predictive alerts
 *   - Feeds into SessionManager for event detection
 *   - Used by ReportGenerator for predictive sections
 * 
 * Usage:
 *   TrendPredictor predictor;
 *   predictor.SetConfig(config);
 *   
 *   // Record data each frame
 *   predictor.RecordFPS(frame, fps);
 *   predictor.RecordMemory(frame, memoryBytes);
 *   
 *   // Get analysis
 *   TrendAnalysis analysis = predictor.AnalyzeFPS();
 *   
 *   // Get prediction
 *   PerformancePrediction pred = predictor.PredictPerformance(100, 60.0);
 */
class TrendPredictor {
public:
    TrendPredictor();
    ~TrendPredictor();
    
    // ─── Configuration ──────────────────────────────────────────────────────
    
    struct Config {
        size_t defaultWindowSize = 300;          // Default rolling window size
        size_t minSamplesForPrediction = 30;      // Minimum samples before predicting
        size_t maxForecastFrames = 300;           // How far ahead to forecast
        double defaultTargetFPS = 60.0;
        double defaultMemoryLimitMB = 4096.0;    // OOM threshold in MB
        double defaultMemoryLimitBytes = 4096.0 * 1024.0 * 1024.0;
        
        PredictionAlgorithm defaultAlgorithm = PredictionAlgorithm::Adaptive;
        
        // Smoothing parameters
        double smoothingAlpha = 0.3;             // Level smoothing (0-1)
        double smoothingBeta = 0.1;              // Trend smoothing (0-1)
        
        // Anomaly detection
        double anomalyStdDevMultiplier = 3.0;    // Std dev threshold for anomaly
        size_t anomalyMinFramesBetween = 10;     // Min frames between anomaly alerts
        
        // Trend detection sensitivity
        double stableThresholdPerFrame = 0.01;   // Slope threshold for "stable"
        double significantChangeThreshold = 5.0; // % change for "significant"
        
        // Confidence thresholds
        double highConfidenceThreshold = 0.85;   // R2 threshold for high confidence
        double lowConfidenceThreshold = 0.50;     // R2 threshold for low confidence
        
        // Performance prediction
        int predictionFrames = 100;             // Default frames to predict
        int predictionSeconds = 10;              // Default seconds to predict
    };
    
    void SetConfig(const Config& config);
    const Config& GetConfig() const { return m_config; }
    void Reset();
    
    // ─── Data Recording ─────────────────────────────────────────────────────
    
    void RecordFPS(int frameNumber, double fps);
    void RecordFrameTime(int frameNumber, double frameTimeMs);
    void RecordMemory(int frameNumber, size_t memoryBytes);
    void RecordGPUUtilization(int frameNumber, double utilizationPercent);
    void RecordCPUTemperature(int frameNumber, double tempCelsius);
    void RecordGPUTemperature(int frameNumber, double tempCelsius);
    
    // Generic recording
    void RecordDataPoint(DataSeriesType type, int frameNumber, double value);
    
    // Batch recording
    void RecordFrame(int frameNumber, double fps, double frameTimeMs, 
                     size_t memoryBytes, double gpuUtil = 0, 
                     double cpuTemp = 0, double gpuTemp = 0);
    
    // ─── Analysis ───────────────────────────────────────────────────────────
    
    TrendAnalysis AnalyzeSeries(DataSeriesType type);
    TrendAnalysis AnalyzeFPS() { return AnalyzeSeries(DataSeriesType::FPS); }
    TrendAnalysis AnalyzeFrameTime() { return AnalyzeSeries(DataSeriesType::FrameTime); }
    TrendAnalysis AnalyzeMemory() { return AnalyzeSeries(DataSeriesType::Memory); }
    TrendAnalysis AnalyzeGPU() { return AnalyzeSeries(DataSeriesType::GPUUtilization); }
    
    // All series at once
    std::unordered_map<DataSeriesType, TrendAnalysis> AnalyzeAll();
    
    // ─── Prediction ──────────────────────────────────────────────────────────
    
    PerformancePrediction PredictPerformance(int framesAhead = 100, double targetFPS = 60.0);
    PerformancePrediction PredictMemory(int framesAhead = 100, double memoryLimitMB = 4096.0);
    PerformancePrediction PredictThermal(int secondsAhead = 60);
    
    ForecastResult ForecastSeries(DataSeriesType type, int framesAhead);
    ForecastResult ForecastFPS(int framesAhead);
    ForecastResult ForecastFrameTime(int framesAhead);
    
    // ─── Health Metrics (called by PerformanceScorer) ───────────────────────
    
    /** Volatility score: 0-100, higher = more stable */
    double CalculateVolatility() const;
    
    /** Health score: 0-100, higher = healthier overall */
    double CalculateHealthScore() const;
    
    /** Stability score for a specific series */
    double CalculateStabilityScore(DataSeriesType type) const;
    
    /** Returns overall trend direction across all metrics */
    TrendDirection GetOverallTrend() const;
    
    // ─── Anomaly Detection ──────────────────────────────────────────────────
    
    struct Anomaly {
        int64_t timestamp;
        int frameNumber;
        DataSeriesType seriesType;
        std::string type;         // "spike", "drop", "freeze", "oscillation"
        double value;
        double expectedValue;
        double deviation;         // How many std devs away
        bool acknowledged;
    };
    
    std::vector<Anomaly> DetectAnomalies();
    std::vector<Anomaly> GetAnomalies(DataSeriesType type) const;
    void AcknowledgeAnomaly(int64_t timestamp);
    void ClearAnomalies();
    
    // ─── Regression & Correlation ───────────────────────────────────────────
    
    LinearRegressionResult FitLinearRegression(const std::vector<DataPoint>& points) const;
    LinearRegressionResult FitLinearRegressionFPS() const;
    LinearRegressionResult FitLinearRegressionMemory() const;
    
    double CalculateCorrelation(DataSeriesType typeA, DataSeriesType typeB) const;
    
    // ─── Series Access ───────────────────────────────────────────────────────
    
    const std::vector<DataPoint>& GetSeries(DataSeriesType type) const;
    std::vector<DataPoint> GetRecentSeries(DataSeriesType type, int count) const;
    std::vector<DataPoint> GetSeriesInRange(DataSeriesType type, int64_t startTime, int64_t endTime) const;
    
    size_t GetSeriesSize(DataSeriesType type) const;
    bool HasEnoughData(DataSeriesType type) const;
    
    // ─── Export ─────────────────────────────────────────────────────────────
    
    std::string ExportToJSON() const;
    std::string ExportAnalysisToJSON(DataSeriesType type) const;
    std::string ExportPredictionsToJSON() const;
    std::string ExportAnomaliesToJSON() const;
    std::string ExportFullReport() const;
    
    // ─── Callbacks ─────────────────────────────────────────────────────────
    
    using AnomalyCallback = std::function<void(const Anomaly&)>;
    using TrendChangeCallback = std::function<void(DataSeriesType, TrendDirection, TrendDirection)>;
    
    void SetAnomalyCallback(AnomalyCallback callback);
    void SetTrendChangeCallback(TrendChangeCallback callback);
    
private:
    void InitializeSeries(DataSeriesType type, const std::string& name);
    TimeSeriesState& GetOrCreateSeries(DataSeriesType type);
    const TimeSeriesState* GetSeriesPtr(DataSeriesType type) const;
    
    void UpdateSeriesState(TimeSeriesState& series);
    void TrimSeries(TimeSeriesState& series);
    
    void DetectAnomaliesInSeries(TimeSeriesState& series);
    void NotifyTrendChange(DataSeriesType type, TrendDirection oldDir, TrendDirection newDir);
    
    // Algorithm implementations
    LinearRegressionResult LinearRegression(const std::vector<double>& x, 
                                            const std::vector<double>& y) const;
    ForecastResult ExponentialSmoothingForecast(const TimeSeriesState& series, 
                                                int framesAhead) const;
    ForecastResult WeightedMovingForecast(const TimeSeriesState& series,
                                          int framesAhead) const;
    ForecastResult AdaptiveForecast(const TimeSeriesState& series,
                                    int framesAhead) const;
    
    // Helper utilities
    double ComputeMean(const std::vector<double>& values) const;
    double ComputeStdDev(const std::vector<double>& values) const;
    double ComputeStdDev(const std::vector<DataPoint>& points) const;
    TrendDirection DetermineDirection(double slope, double r2) const;
    double ComputeVolatility(const std::vector<DataPoint>& points) const;
    
    Config m_config;
    
    std::unordered_map<DataSeriesType, TimeSeriesState> m_series;
    std::vector<Anomaly> m_anomalies;
    
    // Tracking for callbacks
    std::unordered_map<DataSeriesType, TrendDirection> m_lastDirections;
    
    // Callbacks
    AnomalyCallback m_anomalyCallback;
    TrendChangeCallback m_trendChangeCallback;
    
    // Cached health scores
    mutable double m_cachedVolatility;
    mutable double m_cachedHealthScore;
    mutable bool m_healthDirty;
};

} // namespace ProfilerCore
