#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <chrono>

namespace ProfilerCore {

/**
 * Memory allocation category
 */
enum class MemoryCategory {
    Unknown,
    Texture,        // GPU textures
    Mesh,           // 3D meshes
    Audio,          // Audio buffers
    Animation,      // Animation data
    Script,         // Script/runtime objects
    Physics,        // Physics simulation
    Particle,       // Particle systems
    UI,             // UI elements
    General,        // General heap allocations
    GPU,            // GPU-only memory (VRAM)
    Temporary       // Short-lived allocations
};

/**
 * Memory allocation record
 */
struct AllocationRecord {
    int64_t id;
    size_t size;
    MemoryCategory category;
    std::string tag;           // User-defined tag
    int64_t timestamp;         // Allocation time
    const char* file;          // Source file (if tracking enabled)
    int line;                  // Source line
    bool active;               // true if not yet freed
    int64_t freedAt;           // Free timestamp (if freed)
    double lifetimeMs;         // Duration before freed
};

/**
 * Memory leak record
 */
struct MemoryLeak {
    int64_t allocationId;
    size_t size;
    MemoryCategory category;
    std::string tag;
    const char* file;
    int line;
    int64_t allocatedAt;
    int64_t detectedAt;
    double ageMs;              // Time since allocation
    int occurrenceCount;       // Similar leaks merged
};

/**
 * Memory usage statistics per category
 */
struct CategoryStats {
    MemoryCategory category;
    std::string name;
    size_t currentUsage;       // Active allocations
    size_t peakUsage;          // Historical peak
    size_t totalAllocated;     // Cumulative allocated
    size_t totalFreed;         // Cumulative freed
    int activeCount;           // Number of active allocations
    int allocationCount;       // Total allocations made
    int freeCount;             // Total frees made
    double avgSize;            // Average allocation size
    double avgLifetime;        // Average lifetime in ms
};

/**
 * Memory usage snapshot for time-series analysis
 */
struct MemorySnapshot {
    int64_t timestamp;
    int frameNumber;
    size_t totalUsage;         // Total active memory
    size_t gpuUsage;           // GPU memory (if available)
    size_t heapUsage;          // Heap memory
    size_t virtualSize;        // Virtual memory size
    int activeAllocations;     // Number of active allocations
    std::unordered_map<MemoryCategory, size_t> usageByCategory;
};

/**
 * Memory trend analysis result
 */
struct MemoryTrend {
    bool hasLeak;              // Leak detected
    bool hasFragmentation;     // Fragmentation detected
    bool hasHighWatermark;     // Near memory limit
    
    double growthRate;         // bytes per second
    double projectedOOMTime;   // seconds until OOM (if leak continues)
    size_t estimatedLeakSize;  // Estimated leaked memory
    
    int fragmentationScore;    // 0-100, higher = more fragmented
    int leakConfidence;        // 0-100, confidence in leak detection
    
    std::string trendSummary;  // Human-readable summary
};

/**
 * Memory pressure level
 */
enum class MemoryPressure {
    None = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Critical = 4
};

/**
 * Memory budget configuration
 */
struct MemoryBudget {
    size_t totalLimit;         // Total memory budget (bytes)
    size_t warningThreshold;   // Warning at this usage
    size_t criticalThreshold;  // Critical at this usage
    
    std::unordered_map<MemoryCategory, size_t> categoryLimits;
    
    bool enforceLimits;        // Reject allocations over limit
    bool logViolations;        // Log budget violations
};

/**
 * Memory analysis report
 */
struct MemoryReport {
    int64_t timestamp;
    int frameNumber;
    
    // Overall stats
    size_t totalMemory;
    size_t peakMemory;
    size_t gpuMemory;
    size_t availableMemory;
    
    // Breakdown
    std::vector<CategoryStats> categories;
    std::vector<MemoryLeak> detectedLeaks;
    
    // Trend analysis
    MemoryTrend trend;
    MemoryPressure pressure;
    
    // Recommendations
    std::vector<std::string> recommendations;
    
    // Budget status
    bool budgetViolated;
    std::vector<std::string> violatedCategories;
};

/**
 * Memory threshold configuration for alerts
 */
struct MemoryThresholds {
    size_t leakDetectionThreshold = 1024 * 1024;   // 1MB
    int leakAgeThresholdMs = 30000;                 // 30 seconds
    double growthRateWarning = 1024.0 * 1024.0;     // 1MB/s
    double growthRateCritical = 10.0 * 1024.0 * 1024.0; // 10MB/s
    int fragmentationWarningThreshold = 50;
    int fragmentationCriticalThreshold = 75;
};

/**
 * MemoryAnalyzer - Deep memory analysis and leak detection
 * 
 * Features:
 * - Allocation tracking with category tagging
 * - Memory leak detection with heuristics
 * - Memory fragmentation analysis
 * - Budget management and enforcement
 * - Time-series memory snapshots
 * - Category-based usage breakdown
 * - Trend analysis and OOM prediction
 * - Actionable recommendations
 */
class MemoryAnalyzer {
public:
    MemoryAnalyzer();
    ~MemoryAnalyzer();
    
    // Configuration
    void SetBudget(const MemoryBudget& budget);
    void SetThresholds(const MemoryThresholds& thresholds);
    void SetHistorySize(size_t snapshots);
    void EnableSourceTracking(bool enable);
    void Reset();
    
    // Allocation tracking
    int64_t TrackAllocation(size_t size, MemoryCategory category = MemoryCategory::General,
                            const std::string& tag = "",
                            const char* file = nullptr, int line = 0);
    void TrackDeallocation(int64_t allocationId);
    void TrackDeallocationByTag(const std::string& tag);
    void TrackDeallocationByCategory(MemoryCategory category);
    
    // Raw memory update (without per-allocation tracking)
    void UpdateMemoryUsage(size_t totalBytes, size_t gpuBytes = 0);
    void UpdateCategoryUsage(MemoryCategory category, size_t bytes);
    
    // Frame-based tracking
    void BeginFrame(int frameNumber);
    void EndFrame();
    
    // Snapshot management
    MemorySnapshot TakeSnapshot() const;
    const std::vector<MemorySnapshot>& GetHistory() const { return m_history; }
    
    // Analysis
    MemoryReport GenerateReport() const;
    MemoryTrend AnalyzeTrend() const;
    std::vector<MemoryLeak> DetectLeaks() const;
    
    // Category statistics
    CategoryStats GetCategoryStats(MemoryCategory category) const;
    std::vector<CategoryStats> GetAllCategoryStats() const;
    
    // Current state
    size_t GetCurrentUsage() const { return m_currentTotal; }
    size_t GetPeakUsage() const { return m_peakTotal; }
    size_t GetGpuUsage() const { return m_currentGpu; }
    int GetActiveAllocationCount() const;
    MemoryPressure GetPressureLevel() const;
    
    // Budget checking
    bool IsWithinBudget() const;
    bool IsCategoryWithinBudget(MemoryCategory category) const;
    std::vector<MemoryCategory> GetOverBudgetCategories() const;
    
    // Leak analysis
    bool HasPotentialLeaks() const;
    size_t GetEstimatedLeakedMemory() const;
    int GetLeakCount() const;
    
    // Callbacks
    using LeakCallback = std::function<void(const MemoryLeak&)>;
    using PressureCallback = std::function<void(MemoryPressure)>;
    using BudgetCallback = std::function<void(MemoryCategory, size_t, size_t)>;
    
    void SetLeakCallback(LeakCallback callback);
    void SetPressureCallback(PressureCallback callback);
    void SetBudgetCallback(BudgetCallback callback);
    
    // Export
    std::string ExportToJSON() const;
    std::string ExportLeaksToJSON() const;
    std::string ExportHistoryToJSON() const;

private:
    // Internal helpers
    void UpdatePeakUsage();
    void CheckBudgetViolation(MemoryCategory category, size_t size);
    void AnalyzeForLeaks();
    void ComputeFragmentation();
    void GenerateRecommendations(MemoryReport& report) const;
    int64_t GenerateAllocationId();
    int64_t GetCurrentTimestamp() const;
    std::string CategoryToString(MemoryCategory cat) const;
    
    // Configuration
    MemoryBudget m_budget;
    MemoryThresholds m_thresholds;
    size_t m_maxHistorySize;
    bool m_sourceTrackingEnabled;
    
    // Allocation tracking
    std::unordered_map<int64_t, AllocationRecord> m_allocations;
    std::unordered_map<std::string, std::vector<int64_t>> m_allocationsByTag;
    std::unordered_map<MemoryCategory, std::vector<int64_t>> m_allocationsByCategory;
    
    // Current state
    size_t m_currentTotal;
    size_t m_peakTotal;
    size_t m_currentGpu;
    size_t m_peakGpu;
    int m_currentFrame;
    
    // Category tracking
    std::unordered_map<MemoryCategory, CategoryStats> m_categoryStats;
    
    // Time-series history
    std::vector<MemorySnapshot> m_history;
    
    // Leak tracking
    std::vector<MemoryLeak> m_detectedLeaks;
    int64_t m_lastLeakScanTime;
    
    // ID generation
    std::atomic<int64_t> m_nextAllocationId{1};
    
    // Callbacks
    LeakCallback m_leakCallback;
    PressureCallback m_pressureCallback;
    BudgetCallback m_budgetCallback;
    
    // Previous pressure level (for change detection)
    mutable MemoryPressure m_lastPressureLevel;
};

} // namespace ProfilerCore

// Convenience macros for memory tracking
#define MEMORY_TRACK_ALLOC(analyzer, size, category, tag) \
    (analyzer)->TrackAllocation(size, category, tag, __FILE__, __LINE__)

#define MEMORY_TRACK_ALLOC_SIMPLE(analyzer, size) \
    (analyzer)->TrackAllocation(size, ProfilerCore::MemoryCategory::General, "", __FILE__, __LINE__)

#define MEMORY_TRACK_FREE(analyzer, id) \
    (analyzer)->TrackDeallocation(id)
