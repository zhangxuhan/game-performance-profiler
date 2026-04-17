#include "MemoryAnalyzer.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace ProfilerCore {

// ─── Constructor / Destructor ─────────────────────────────────────────────────

MemoryAnalyzer::MemoryAnalyzer()
    : m_maxHistorySize(600)  // 10 seconds at 60 FPS
    , m_sourceTrackingEnabled(false)
    , m_currentTotal(0)
    , m_peakTotal(0)
    , m_currentGpu(0)
    , m_peakGpu(0)
    , m_currentFrame(0)
    , m_lastLeakScanTime(0)
    , m_lastPressureLevel(MemoryPressure::None)
{
    // Initialize category stats
    const MemoryCategory categories[] = {
        MemoryCategory::Texture,
        MemoryCategory::Mesh,
        MemoryCategory::Audio,
        MemoryCategory::Animation,
        MemoryCategory::Script,
        MemoryCategory::Physics,
        MemoryCategory::Particle,
        MemoryCategory::UI,
        MemoryCategory::General,
        MemoryCategory::GPU,
        MemoryCategory::Temporary
    };
    
    for (auto cat : categories) {
        CategoryStats stats;
        stats.category = cat;
        stats.name = CategoryToString(cat);
        stats.currentUsage = 0;
        stats.peakUsage = 0;
        stats.totalAllocated = 0;
        stats.totalFreed = 0;
        stats.activeCount = 0;
        stats.allocationCount = 0;
        stats.freeCount = 0;
        stats.avgSize = 0.0;
        stats.avgLifetime = 0.0;
        m_categoryStats[cat] = stats;
    }
    
    // Default budget (4GB total)
    m_budget.totalLimit = 4ULL * 1024 * 1024 * 1024;
    m_budget.warningThreshold = 3ULL * 1024 * 1024 * 1024;
    m_budget.criticalThreshold = static_cast<size_t>(3.5 * 1024 * 1024 * 1024);
    m_budget.enforceLimits = false;
    m_budget.logViolations = true;
}

MemoryAnalyzer::~MemoryAnalyzer() {
    // Nothing to clean up
}

// ─── Configuration ────────────────────────────────────────────────────────────

void MemoryAnalyzer::SetBudget(const MemoryBudget& budget) {
    m_budget = budget;
}

void MemoryAnalyzer::SetThresholds(const MemoryThresholds& thresholds) {
    m_thresholds = thresholds;
}

void MemoryAnalyzer::SetHistorySize(size_t snapshots) {
    m_maxHistorySize = snapshots;
    while (m_history.size() > m_maxHistorySize) {
        m_history.erase(m_history.begin());
    }
}

void MemoryAnalyzer::EnableSourceTracking(bool enable) {
    m_sourceTrackingEnabled = enable;
}

void MemoryAnalyzer::Reset() {
    m_allocations.clear();
    m_allocationsByTag.clear();
    m_allocationsByCategory.clear();
    m_history.clear();
    m_detectedLeaks.clear();
    m_currentTotal = 0;
    m_peakTotal = 0;
    m_currentGpu = 0;
    m_peakGpu = 0;
    m_currentFrame = 0;
    m_lastLeakScanTime = 0;
    m_lastPressureLevel = MemoryPressure::None;
    
    // Reset category stats
    for (auto& pair : m_categoryStats) {
        pair.second.currentUsage = 0;
        pair.second.peakUsage = 0;
        pair.second.totalAllocated = 0;
        pair.second.totalFreed = 0;
        pair.second.activeCount = 0;
        pair.second.allocationCount = 0;
        pair.second.freeCount = 0;
        pair.second.avgSize = 0.0;
        pair.second.avgLifetime = 0.0;
    }
}

// ─── Allocation Tracking ──────────────────────────────────────────────────────

int64_t MemoryAnalyzer::TrackAllocation(size_t size, MemoryCategory category,
                                         const std::string& tag,
                                         const char* file, int line) {
    int64_t id = GenerateAllocationId();
    int64_t timestamp = GetCurrentTimestamp();
    
    AllocationRecord record;
    record.id = id;
    record.size = size;
    record.category = category;
    record.tag = tag;
    record.timestamp = timestamp;
    record.file = m_sourceTrackingEnabled ? file : nullptr;
    record.line = line;
    record.active = true;
    record.freedAt = 0;
    record.lifetimeMs = 0.0;
    
    m_allocations[id] = record;
    
    // Index by tag
    if (!tag.empty()) {
        m_allocationsByTag[tag].push_back(id);
    }
    
    // Index by category
    m_allocationsByCategory[category].push_back(id);
    
    // Update category stats
    auto& stats = m_categoryStats[category];
    stats.currentUsage += size;
    stats.totalAllocated += size;
    stats.activeCount++;
    stats.allocationCount++;
    stats.peakUsage = std::max(stats.peakUsage, stats.currentUsage);
    
    // Update running average size
    if (stats.allocationCount > 0) {
        stats.avgSize = static_cast<double>(stats.totalAllocated) / stats.allocationCount;
    }
    
    // Update total memory
    m_currentTotal += size;
    UpdatePeakUsage();
    
    // Check budget violation
    CheckBudgetViolation(category, size);
    
    return id;
}

void MemoryAnalyzer::TrackDeallocation(int64_t allocationId) {
    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end() || !it->second.active) {
        return;
    }
    
    AllocationRecord& record = it->second;
    record.active = false;
    record.freedAt = GetCurrentTimestamp();
    record.lifetimeMs = (record.freedAt - record.timestamp) / 1000.0;  // Convert to ms
    
    // Update category stats
    auto& stats = m_categoryStats[record.category];
    stats.currentUsage -= record.size;
    stats.totalFreed += record.size;
    stats.activeCount--;
    stats.freeCount++;
    
    // Update running average lifetime
    if (stats.freeCount > 0) {
        // This is a simplification; a proper implementation would track cumulative lifetime
        stats.avgLifetime = (stats.avgLifetime * (stats.freeCount - 1) + record.lifetimeMs) / stats.freeCount;
    }
    
    // Update total memory
    m_currentTotal -= record.size;
}

void MemoryAnalyzer::TrackDeallocationByTag(const std::string& tag) {
    auto it = m_allocationsByTag.find(tag);
    if (it == m_allocationsByTag.end()) {
        return;
    }
    
    // Copy the IDs since we'll be modifying the map
    std::vector<int64_t> ids = it->second;
    for (int64_t id : ids) {
        TrackDeallocation(id);
    }
    
    m_allocationsByTag.erase(it);
}

void MemoryAnalyzer::TrackDeallocationByCategory(MemoryCategory category) {
    auto it = m_allocationsByCategory.find(category);
    if (it == m_allocationsByCategory.end()) {
        return;
    }
    
    // Copy the IDs
    std::vector<int64_t> ids = it->second;
    for (int64_t id : ids) {
        auto allocIt = m_allocations.find(id);
        if (allocIt != m_allocations.end() && allocIt->second.active) {
            TrackDeallocation(id);
        }
    }
}

void MemoryAnalyzer::UpdateMemoryUsage(size_t totalBytes, size_t gpuBytes) {
    m_currentTotal = totalBytes;
    m_currentGpu = gpuBytes;
    UpdatePeakUsage();
}

void MemoryAnalyzer::UpdateCategoryUsage(MemoryCategory category, size_t bytes) {
    auto& stats = m_categoryStats[category];
    stats.currentUsage = bytes;
    stats.peakUsage = std::max(stats.peakUsage, bytes);
}

// ─── Frame-based Tracking ─────────────────────────────────────────────────────

void MemoryAnalyzer::BeginFrame(int frameNumber) {
    m_currentFrame = frameNumber;
}

void MemoryAnalyzer::EndFrame() {
    // Take a snapshot for time-series analysis
    MemorySnapshot snapshot = TakeSnapshot();
    m_history.push_back(snapshot);
    
    // Trim old history
    while (m_history.size() > m_maxHistorySize) {
        m_history.erase(m_history.begin());
    }
    
    // Periodic leak scan (every 60 frames = ~1 second)
    if (m_currentFrame % 60 == 0) {
        AnalyzeForLeaks();
    }
}

// ─── Snapshot Management ──────────────────────────────────────────────────────

MemorySnapshot MemoryAnalyzer::TakeSnapshot() const {
    MemorySnapshot snapshot;
    snapshot.timestamp = GetCurrentTimestamp();
    snapshot.frameNumber = m_currentFrame;
    snapshot.totalUsage = m_currentTotal;
    snapshot.gpuUsage = m_currentGpu;
    snapshot.heapUsage = m_currentTotal - m_currentGpu;
    snapshot.virtualSize = m_currentTotal;  // Simplified
    snapshot.activeAllocations = static_cast<int>(m_allocations.size());
    
    for (const auto& pair : m_categoryStats) {
        if (pair.second.currentUsage > 0) {
            snapshot.usageByCategory[pair.first] = pair.second.currentUsage;
        }
    }
    
    return snapshot;
}

// ─── Analysis ─────────────────────────────────────────────────────────────────

MemoryReport MemoryAnalyzer::GenerateReport() const {
    MemoryReport report;
    report.timestamp = GetCurrentTimestamp();
    report.frameNumber = m_currentFrame;
    report.totalMemory = m_currentTotal;
    report.peakMemory = m_peakTotal;
    report.gpuMemory = m_currentGpu;
    report.availableMemory = (m_budget.totalLimit > m_currentTotal) 
        ? m_budget.totalLimit - m_currentTotal : 0;
    
    // Category breakdown
    for (const auto& pair : m_categoryStats) {
        if (pair.second.currentUsage > 0 || pair.second.allocationCount > 0) {
            report.categories.push_back(pair.second);
        }
    }
    
    // Detected leaks
    report.detectedLeaks = m_detectedLeaks;
    
    // Trend analysis
    report.trend = AnalyzeTrend();
    report.pressure = GetPressureLevel();
    
    // Budget status
    report.budgetViolated = !IsWithinBudget();
    report.violatedCategories.clear();
    for (const auto& pair : m_categoryStats) {
        if (!IsCategoryWithinBudget(pair.first)) {
            report.violatedCategories.push_back(CategoryToString(pair.first));
        }
    }
    
    // Generate recommendations
    GenerateRecommendations(report);
    
    return report;
}

MemoryTrend MemoryAnalyzer::AnalyzeTrend() const {
    MemoryTrend trend;
    trend.hasLeak = false;
    trend.hasFragmentation = false;
    trend.hasHighWatermark = false;
    trend.growthRate = 0.0;
    trend.projectedOOMTime = 0.0;
    trend.estimatedLeakSize = 0;
    trend.fragmentationScore = 0;
    trend.leakConfidence = 0;
    
    if (m_history.size() < 10) {
        trend.trendSummary = "Insufficient data for trend analysis";
        return trend;
    }
    
    // Calculate growth rate using linear regression
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;
    int n = static_cast<int>(m_history.size());
    
    for (int i = 0; i < n; i++) {
        double x = static_cast<double>(i);
        double y = static_cast<double>(m_history[i].totalUsage);
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }
    
    double meanX = sumX / n;
    double meanY = sumY / n;
    double slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
    
    // Convert slope to bytes per second (assuming 60 FPS snapshots)
    trend.growthRate = slope * 60.0;  // frames to seconds
    
    // Leak detection based on sustained growth
    if (trend.growthRate > m_thresholds.growthRateWarning) {
        trend.hasLeak = true;
        trend.estimatedLeakSize = static_cast<size_t>(trend.growthRate * 10.0);  // 10 second window
        
        // Calculate confidence based on consistency
        double variance = 0.0;
        for (int i = 0; i < n; i++) {
            double expected = meanY + slope * (i - meanX);
            double actual = static_cast<double>(m_history[i].totalUsage);
            variance += (actual - expected) * (actual - expected);
        }
        double stdDev = std::sqrt(variance / n);
        double meanUsage = meanY;
        
        // Lower variance relative to mean = higher confidence
        trend.leakConfidence = std::min(100, static_cast<int>(100.0 * (1.0 - stdDev / meanUsage)));
        
        // Project OOM time
        if (trend.growthRate > 0 && m_budget.totalLimit > m_currentTotal) {
            trend.projectedOOMTime = static_cast<double>(m_budget.totalLimit - m_currentTotal) / trend.growthRate;
        }
    }
    
    // Fragmentation score (simplified: based on allocation count vs total)
    if (m_currentTotal > 0 && m_allocations.size() > 0) {
        double avgAllocSize = static_cast<double>(m_currentTotal) / m_allocations.size();
        double idealAvgSize = 64.0 * 1024.0;  // Assume 64KB is ideal average
        
        if (avgAllocSize < idealAvgSize) {
            trend.fragmentationScore = std::min(100, static_cast<int>(100.0 * (1.0 - avgAllocSize / idealAvgSize)));
            trend.hasFragmentation = trend.fragmentationScore > m_thresholds.fragmentationWarningThreshold;
        }
    }
    
    // High watermark check
    double usagePercent = static_cast<double>(m_currentTotal) / m_budget.totalLimit * 100.0;
    trend.hasHighWatermark = usagePercent > 90.0;
    
    // Generate summary
    std::ostringstream oss;
    if (trend.hasLeak) {
        oss << "Leak detected: " << std::fixed << std::setprecision(2) 
            << (trend.growthRate / 1024.0 / 1024.0) << " MB/s growth rate";
        if (trend.projectedOOMTime > 0 && trend.projectedOOMTime < 3600) {
            oss << ", OOM in ~" << static_cast<int>(trend.projectedOOMTime) << "s";
        }
    } else if (trend.growthRate > 0) {
        oss << "Stable growth: " << std::fixed << std::setprecision(2)
            << (trend.growthRate / 1024.0) << " KB/s";
    } else {
        oss << "Memory stable";
    }
    
    if (trend.hasFragmentation) {
        oss << " (fragmented)";
    }
    
    trend.trendSummary = oss.str();
    
    return trend;
}

std::vector<MemoryLeak> MemoryAnalyzer::DetectLeaks() const {
    std::vector<MemoryLeak> leaks;
    int64_t currentTime = GetCurrentTimestamp();
    
    for (const auto& pair : m_allocations) {
        const AllocationRecord& record = pair.second;
        
        if (!record.active) continue;
        
        // Check if allocation is old enough to be considered a potential leak
        double ageMs = (currentTime - record.timestamp) / 1000.0;
        if (ageMs < m_thresholds.leakAgeThresholdMs) continue;
        
        // Check if allocation is large enough
        if (record.size < m_thresholds.leakDetectionThreshold) continue;
        
        // Skip temporary allocations
        if (record.category == MemoryCategory::Temporary) continue;
        
        MemoryLeak leak;
        leak.allocationId = record.id;
        leak.size = record.size;
        leak.category = record.category;
        leak.tag = record.tag;
        leak.file = record.file;
        leak.line = record.line;
        leak.allocatedAt = record.timestamp;
        leak.detectedAt = currentTime;
        leak.ageMs = ageMs;
        leak.occurrenceCount = 1;
        
        leaks.push_back(leak);
    }
    
    // Sort by size (largest first)
    std::sort(leaks.begin(), leaks.end(), [](const MemoryLeak& a, const MemoryLeak& b) {
        return a.size > b.size;
    });
    
    return leaks;
}

// ─── Category Statistics ───────────────────────────────────────────────────────

CategoryStats MemoryAnalyzer::GetCategoryStats(MemoryCategory category) const {
    auto it = m_categoryStats.find(category);
    if (it != m_categoryStats.end()) {
        return it->second;
    }
    return CategoryStats();
}

std::vector<CategoryStats> MemoryAnalyzer::GetAllCategoryStats() const {
    std::vector<CategoryStats> result;
    for (const auto& pair : m_categoryStats) {
        result.push_back(pair.second);
    }
    return result;
}

// ─── Current State ─────────────────────────────────────────────────────────────

int MemoryAnalyzer::GetActiveAllocationCount() const {
    int count = 0;
    for (const auto& pair : m_allocations) {
        if (pair.second.active) count++;
    }
    return count;
}

MemoryPressure MemoryAnalyzer::GetPressureLevel() const {
    if (m_budget.totalLimit == 0) return MemoryPressure::None;
    
    double usageRatio = static_cast<double>(m_currentTotal) / m_budget.totalLimit;
    
    if (usageRatio >= 0.95) return MemoryPressure::Critical;
    if (usageRatio >= 0.85) return MemoryPressure::High;
    if (usageRatio >= 0.70) return MemoryPressure::Medium;
    if (usageRatio >= 0.50) return MemoryPressure::Low;
    return MemoryPressure::None;
}

// ─── Budget Checking ───────────────────────────────────────────────────────────

bool MemoryAnalyzer::IsWithinBudget() const {
    return m_currentTotal <= m_budget.totalLimit;
}

bool MemoryAnalyzer::IsCategoryWithinBudget(MemoryCategory category) const {
    auto it = m_budget.categoryLimits.find(category);
    if (it == m_budget.categoryLimits.end()) {
        return true;  // No limit set for this category
    }
    
    auto statsIt = m_categoryStats.find(category);
    if (statsIt == m_categoryStats.end()) {
        return true;
    }
    
    return statsIt->second.currentUsage <= it->second;
}

std::vector<MemoryCategory> MemoryAnalyzer::GetOverBudgetCategories() const {
    std::vector<MemoryCategory> overBudget;
    
    for (const auto& pair : m_budget.categoryLimits) {
        if (!IsCategoryWithinBudget(pair.first)) {
            overBudget.push_back(pair.first);
        }
    }
    
    return overBudget;
}

// ─── Leak Analysis ─────────────────────────────────────────────────────────────

bool MemoryAnalyzer::HasPotentialLeaks() const {
    return !m_detectedLeaks.empty();
}

size_t MemoryAnalyzer::GetEstimatedLeakedMemory() const {
    size_t total = 0;
    for (const MemoryLeak& leak : m_detectedLeaks) {
        total += leak.size * leak.occurrenceCount;
    }
    return total;
}

int MemoryAnalyzer::GetLeakCount() const {
    return static_cast<int>(m_detectedLeaks.size());
}

// ─── Callbacks ─────────────────────────────────────────────────────────────────

void MemoryAnalyzer::SetLeakCallback(LeakCallback callback) {
    m_leakCallback = callback;
}

void MemoryAnalyzer::SetPressureCallback(PressureCallback callback) {
    m_pressureCallback = callback;
}

void MemoryAnalyzer::SetBudgetCallback(BudgetCallback callback) {
    m_budgetCallback = callback;
}

// ─── Export ────────────────────────────────────────────────────────────────────

std::string MemoryAnalyzer::ExportToJSON() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"timestamp\": " << GetCurrentTimestamp() << ",\n";
    json << "  \"frameNumber\": " << m_currentFrame << ",\n";
    json << "  \"totalMemory\": " << m_currentTotal << ",\n";
    json << "  \"peakMemory\": " << m_peakTotal << ",\n";
    json << "  \"gpuMemory\": " << m_currentGpu << ",\n";
    json << "  \"activeAllocations\": " << GetActiveAllocationCount() << ",\n";
    
    json << "  \"categories\": [\n";
    bool first = true;
    for (const auto& pair : m_categoryStats) {
        if (pair.second.currentUsage > 0 || pair.second.allocationCount > 0) {
            if (!first) json << ",\n";
            first = false;
            json << "    {\n";
            json << "      \"name\": \"" << pair.second.name << "\",\n";
            json << "      \"currentUsage\": " << pair.second.currentUsage << ",\n";
            json << "      \"peakUsage\": " << pair.second.peakUsage << ",\n";
            json << "      \"activeCount\": " << pair.second.activeCount << "\n";
            json << "    }";
        }
    }
    json << "\n  ],\n";
    
    json << "  \"pressure\": " << static_cast<int>(GetPressureLevel()) << ",\n";
    json << "  \"leakCount\": " << m_detectedLeaks.size() << "\n";
    json << "}\n";
    
    return json.str();
}

std::string MemoryAnalyzer::ExportLeaksToJSON() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"timestamp\": " << GetCurrentTimestamp() << ",\n";
    json << "  \"leakCount\": " << m_detectedLeaks.size() << ",\n";
    json << "  \"totalLeakedSize\": " << GetEstimatedLeakedMemory() << ",\n";
    json << "  \"leaks\": [\n";
    
    for (size_t i = 0; i < m_detectedLeaks.size(); i++) {
        const MemoryLeak& leak = m_detectedLeaks[i];
        json << "    {\n";
        json << "      \"id\": " << leak.allocationId << ",\n";
        json << "      \"size\": " << leak.size << ",\n";
        json << "      \"category\": \"" << CategoryToString(leak.category) << "\",\n";
        json << "      \"tag\": \"" << leak.tag << "\",\n";
        json << "      \"ageMs\": " << leak.ageMs << ",\n";
        if (leak.file) {
            json << "      \"file\": \"" << leak.file << "\",\n";
            json << "      \"line\": " << leak.line << "\n";
        } else {
            json << "      \"file\": null,\n";
            json << "      \"line\": 0\n";
        }
        json << "    }";
        if (i < m_detectedLeaks.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    
    return json.str();
}

std::string MemoryAnalyzer::ExportHistoryToJSON() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"snapshotCount\": " << m_history.size() << ",\n";
    json << "  \"snapshots\": [\n";
    
    for (size_t i = 0; i < m_history.size(); i++) {
        const MemorySnapshot& snapshot = m_history[i];
        json << "    {\n";
        json << "      \"frame\": " << snapshot.frameNumber << ",\n";
        json << "      \"totalUsage\": " << snapshot.totalUsage << ",\n";
        json << "      \"gpuUsage\": " << snapshot.gpuUsage << ",\n";
        json << "      \"activeAllocations\": " << snapshot.activeAllocations << "\n";
        json << "    }";
        if (i < m_history.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    
    return json.str();
}

// ─── Private Helpers ───────────────────────────────────────────────────────────

void MemoryAnalyzer::UpdatePeakUsage() {
    m_peakTotal = std::max(m_peakTotal, m_currentTotal);
    m_peakGpu = std::max(m_peakGpu, m_currentGpu);
}

void MemoryAnalyzer::CheckBudgetViolation(MemoryCategory category, size_t size) {
    if (!m_budget.logViolations) return;
    
    bool violated = false;
    
    // Check total budget
    if (m_currentTotal > m_budget.totalLimit) {
        violated = true;
    }
    
    // Check category budget
    auto it = m_budget.categoryLimits.find(category);
    if (it != m_budget.categoryLimits.end()) {
        auto statsIt = m_categoryStats.find(category);
        if (statsIt != m_categoryStats.end() && 
            statsIt->second.currentUsage > it->second) {
            violated = true;
        }
    }
    
    if (violated && m_budgetCallback) {
        m_budgetCallback(category, m_categoryStats[category].currentUsage, 
                        m_budget.categoryLimits.count(category) > 0 
                        ? m_budget.categoryLimits.at(category) : m_budget.totalLimit);
    }
}

void MemoryAnalyzer::AnalyzeForLeaks() {
    m_detectedLeaks = DetectLeaks();
    
    // Notify about new leaks
    if (!m_detectedLeaks.empty() && m_leakCallback) {
        for (const MemoryLeak& leak : m_detectedLeaks) {
            m_leakCallback(leak);
        }
    }
    
    // Check for pressure change
    MemoryPressure currentPressure = GetPressureLevel();
    if (currentPressure != m_lastPressureLevel && m_pressureCallback) {
        m_pressureCallback(currentPressure);
    }
    m_lastPressureLevel = currentPressure;
}

void MemoryAnalyzer::GenerateRecommendations(MemoryReport& report) const {
    if (report.trend.hasLeak) {
        report.recommendations.push_back("Potential memory leak detected. Review long-lived allocations.");
        
        // Category-specific advice
        for (const MemoryLeak& leak : report.detectedLeaks) {
            switch (leak.category) {
                case MemoryCategory::Texture:
                    report.recommendations.push_back("Check texture loading/unloading logic. Consider using texture pooling.");
                    break;
                case MemoryCategory::Mesh:
                    report.recommendations.push_back("Verify mesh data is properly released when scenes change.");
                    break;
                case MemoryCategory::Script:
                    report.recommendations.push_back("Review script object lifecycle and event listener cleanup.");
                    break;
                default:
                    break;
            }
            break;  // Only add one category-specific recommendation
        }
    }
    
    if (report.trend.hasFragmentation) {
        report.recommendations.push_back("High memory fragmentation detected. Consider using memory pools or arena allocators.");
    }
    
    if (report.trend.hasHighWatermark) {
        report.recommendations.push_back("Memory usage near limit. Consider reducing asset quality or implementing dynamic loading.");
    }
    
    if (report.pressure >= MemoryPressure::High) {
        report.recommendations.push_back("Critical memory pressure. Immediate action recommended.");
    }
    
    // Check for large categories
    for (const CategoryStats& stats : report.categories) {
        if (stats.currentUsage > m_budget.totalLimit / 2) {
            report.recommendations.push_back(
                "Category '" + stats.name + "' uses >50% of total memory. Consider optimization.");
        }
    }
}

int64_t MemoryAnalyzer::GenerateAllocationId() {
    return m_nextAllocationId++;
}

int64_t MemoryAnalyzer::GetCurrentTimestamp() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

std::string MemoryAnalyzer::CategoryToString(MemoryCategory cat) const {
    switch (cat) {
        case MemoryCategory::Texture:    return "Texture";
        case MemoryCategory::Mesh:       return "Mesh";
        case MemoryCategory::Audio:      return "Audio";
        case MemoryCategory::Animation:  return "Animation";
        case MemoryCategory::Script:     return "Script";
        case MemoryCategory::Physics:    return "Physics";
        case MemoryCategory::Particle:   return "Particle";
        case MemoryCategory::UI:         return "UI";
        case MemoryCategory::General:    return "General";
        case MemoryCategory::GPU:        return "GPU";
        case MemoryCategory::Temporary:  return "Temporary";
        default:                         return "Unknown";
    }
}

} // namespace ProfilerCore
