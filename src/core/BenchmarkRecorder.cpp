#include "BenchmarkRecorder.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <numeric>
#include <random>

namespace ProfilerCore {

BenchmarkRecorder::BenchmarkRecorder()
    : m_isRecording(false)
    , m_currentSessionId("")
    , m_maxSessionHistory(50)
{
}

BenchmarkRecorder::~BenchmarkRecorder() {
}

void BenchmarkRecorder::SetComparisonThresholds(const ComparisonThresholds& thresholds) {
    m_thresholds = thresholds;
}

void BenchmarkRecorder::SetMaxSessionHistory(size_t maxSessions) {
    m_maxSessionHistory = maxSessions;
    TrimHistory();
}

void BenchmarkRecorder::SetBaselineSession(const std::string& sessionId) {
    m_baselineSessionId = sessionId;
}

std::string BenchmarkRecorder::StartSession(const BenchmarkMetadata& metadata) {
    if (m_isRecording) {
        // Cancel previous session
        CancelSession();
    }
    
    m_currentSessionId = GenerateSessionId();
    m_currentMetadata = metadata;
    m_currentMetadata.startTime = GetCurrentTimestamp();
    m_isRecording = true;
    
    return m_currentSessionId;
}

void BenchmarkRecorder::EndSession(const BenchmarkResult& result) {
    if (!m_isRecording) return;
    
    BenchmarkResult finalResult = result;
    finalResult.sessionId = m_currentSessionId;
    finalResult.metadata = m_currentMetadata;
    finalResult.metadata.endTime = GetCurrentTimestamp();
    finalResult.metadata.durationSec = static_cast<int>(
        (finalResult.metadata.endTime - finalResult.metadata.startTime) / 1000
    );
    
    m_sessions.push_back(finalResult);
    m_sessionIndex[m_currentSessionId] = m_sessions.size() - 1;
    
    // Auto-set first session as baseline if none exists
    if (m_baselineSessionId.empty()) {
        m_baselineSessionId = m_currentSessionId;
        m_sessions.back().metadata.isBaseline = true;
    }
    
    TrimHistory();
    
    m_isRecording = false;
    m_currentSessionId = "";
}

void BenchmarkRecorder::CancelSession() {
    m_isRecording = false;
    m_currentSessionId = "";
}

BenchmarkResult BenchmarkRecorder::GetSession(const std::string& sessionId) const {
    auto it = m_sessionIndex.find(sessionId);
    if (it != m_sessionIndex.end() && it->second < m_sessions.size()) {
        return m_sessions[it->second];
    }
    
    // Fallback: linear search
    for (const auto& s : m_sessions) {
        if (s.sessionId == sessionId) {
            return s;
        }
    }
    
    return BenchmarkResult();
}

BenchmarkResult BenchmarkRecorder::GetBaselineSession() const {
    if (!m_baselineSessionId.empty()) {
        return GetSession(m_baselineSessionId);
    }
    if (!m_sessions.empty()) {
        return m_sessions.front();
    }
    return BenchmarkResult();
}

std::vector<BenchmarkResult> BenchmarkRecorder::GetRecentSessions(int count) const {
    int actual = std::min(count, static_cast<int>(m_sessions.size()));
    if (actual <= 0) return {};
    
    return std::vector<BenchmarkResult>(
        m_sessions.end() - actual,
        m_sessions.end()
    );
}

std::vector<BenchmarkResult> BenchmarkRecorder::GetSessionsByName(const std::string& name) const {
    std::vector<BenchmarkResult> result;
    for (const auto& s : m_sessions) {
        if (s.metadata.name == name) {
            result.push_back(s);
        }
    }
    return result;
}

BenchmarkComparison BenchmarkRecorder::CompareSessions(
    const std::string& baselineId,
    const std::string& comparisonId) const 
{
    BenchmarkResult baseline = GetSession(baselineId);
    BenchmarkResult comparison = GetSession(comparisonId);
    
    if (baseline.sessionId.empty() || comparison.sessionId.empty()) {
        return BenchmarkComparison();
    }
    
    return ComputeComparison(baseline, comparison);
}

BenchmarkComparison BenchmarkRecorder::CompareToBaseline(const std::string& sessionId) const {
    if (m_baselineSessionId.empty()) {
        return BenchmarkComparison();
    }
    return CompareSessions(m_baselineSessionId, sessionId);
}

BenchmarkComparison BenchmarkRecorder::CompareToPrevious(const std::string& sessionId) const {
    auto it = m_sessionIndex.find(sessionId);
    if (it == m_sessionIndex.end() || it->second == 0) {
        // Try linear search
        for (size_t i = 0; i < m_sessions.size(); i++) {
            if (m_sessions[i].sessionId == sessionId) {
                if (i == 0) return BenchmarkComparison();
                return ComputeComparison(m_sessions[i - 1], m_sessions[i]);
            }
        }
        return BenchmarkComparison();
    }
    
    return ComputeComparison(m_sessions[it->second - 1], m_sessions[it->second]);
}

std::vector<BenchmarkComparison> BenchmarkRecorder::CompareAllToBaseline() const {
    std::vector<BenchmarkComparison> comparisons;
    
    if (m_baselineSessionId.empty()) return comparisons;
    
    for (const auto& s : m_sessions) {
        if (s.sessionId != m_baselineSessionId) {
            comparisons.push_back(CompareToBaseline(s.sessionId));
        }
    }
    
    return comparisons;
}

BenchmarkComparison BenchmarkRecorder::ComputeComparison(
    const BenchmarkResult& baseline,
    const BenchmarkResult& comparison) const 
{
    BenchmarkComparison comp;
    comp.baselineSessionId = baseline.sessionId;
    comp.comparisonSessionId = comparison.sessionId;
    
    // --- FPS deltas ---
    comp.avgFpsDelta = comparison.avgFps - baseline.avgFps;
    comp.avgFpsDeltaPercent = (baseline.avgFps > 0) ? 
        (comp.avgFpsDelta / baseline.avgFps * 100.0) : 0.0;
    comp.minFpsDelta = comparison.minFps - baseline.minFps;
    comp.maxFpsDelta = comparison.maxFps - baseline.maxFps;
    comp.p95FpsDelta = comparison.p95Fps - baseline.p95Fps;
    comp.p99FpsDelta = comparison.p99Fps - baseline.p99Fps;
    
    // --- Frame time deltas ---
    comp.avgFrameTimeDeltaMs = comparison.avgFrameTimeMs - baseline.avgFrameTimeMs;
    comp.avgFrameTimeDeltaPercent = (baseline.avgFrameTimeMs > 0) ?
        (comp.avgFrameTimeDeltaMs / baseline.avgFrameTimeMs * 100.0) : 0.0;
    comp.maxFrameTimeDeltaMs = comparison.maxFrameTimeMs - baseline.maxFrameTimeMs;
    
    // --- Memory deltas ---
    comp.peakMemoryDeltaBytes = static_cast<double>(comparison.peakMemoryBytes) - 
                                 static_cast<double>(baseline.peakMemoryBytes);
    comp.peakMemoryDeltaPercent = (baseline.peakMemoryBytes > 0) ?
        (comp.peakMemoryDeltaBytes / baseline.peakMemoryBytes * 100.0) : 0.0;
    comp.avgMemoryDeltaBytes = static_cast<double>(comparison.avgMemoryBytes) -
                               static_cast<double>(baseline.avgMemoryBytes);
    
    // --- Stability delta ---
    comp.stabilityDelta = comparison.stabilityScore - baseline.stabilityScore;
    
    // --- Alert deltas ---
    comp.criticalAlertDelta = comparison.criticalAlertCount - baseline.criticalAlertCount;
    comp.warningAlertDelta = comparison.warningAlertCount - baseline.warningAlertCount;
    
    // --- Significant changes ---
    comp.significantChanges = DetectSignificantChanges(comp);
    
    // --- Overall assessment ---
    comp.overallAssessment = AssessOverall(comp);
    comp.improvementScore = ComputeImprovementScore(comp);
    
    return comp;
}

std::vector<std::string> BenchmarkRecorder::DetectSignificantChanges(
    const BenchmarkComparison& comparison) const 
{
    std::vector<std::string> changes;
    
    // FPS change
    if (std::abs(comparison.avgFpsDeltaPercent) >= m_thresholds.fpsSignificantPercent) {
        std::ostringstream ss;
        if (comparison.avgFpsDeltaPercent > 0) {
            ss << "Average FPS improved by " << std::fixed << std::setprecision(1) 
               << comparison.avgFpsDeltaPercent << "%";
        } else {
            ss << "Average FPS degraded by " << std::fixed << std::setprecision(1) 
               << -comparison.avgFpsDeltaPercent << "%";
        }
        changes.push_back(ss.str());
    }
    
    // P95 FPS change
    if (comparison.p95FpsDelta != 0) {
        double p95Pct = (comparison.baselineSessionId.empty() ? 0 :
            (GetSession(comparison.baselineSessionId).p95Fps > 0 ?
             comparison.p95FpsDelta / GetSession(comparison.baselineSessionId).p95Fps * 100.0 : 0));
        if (std::abs(p95Pct) >= m_thresholds.fpsSignificantPercent) {
            std::ostringstream ss;
            ss << "P95 FPS " << (comparison.p95FpsDelta > 0 ? "improved" : "degraded")
               << " by " << std::fixed << std::setprecision(1) << std::abs(p95Pct) << "%";
            changes.push_back(ss.str());
        }
    }
    
    // Frame time change
    if (std::abs(comparison.avgFrameTimeDeltaPercent) >= m_thresholds.frameTimeSignificantPercent) {
        std::ostringstream ss;
        if (comparison.avgFrameTimeDeltaMs < 0) {
            ss << "Average frame time reduced by " << std::fixed << std::setprecision(2) 
               << -comparison.avgFrameTimeDeltaMs << "ms (" 
               << -comparison.avgFrameTimeDeltaPercent << "%)";
        } else {
            ss << "Average frame time increased by " << std::fixed << std::setprecision(2) 
               << comparison.avgFrameTimeDeltaMs << "ms (" 
               << comparison.avgFrameTimeDeltaPercent << "%)";
        }
        changes.push_back(ss.str());
    }
    
    // Memory change
    if (std::abs(comparison.peakMemoryDeltaPercent) >= m_thresholds.memorySignificantPercent) {
        std::ostringstream ss;
        double deltaMB = comparison.peakMemoryDeltaBytes / (1024.0 * 1024.0);
        if (deltaMB > 0) {
            ss << "Peak memory increased by " << std::fixed << std::setprecision(1) 
               << deltaMB << " MB";
        } else {
            ss << "Peak memory reduced by " << std::fixed << std::setprecision(1) 
               << -deltaMB << " MB";
        }
        changes.push_back(ss.str());
    }
    
    // Stability change
    if (std::abs(comparison.stabilityDelta) >= m_thresholds.stabilitySignificantPercent) {
        std::ostringstream ss;
        ss << "Stability " << (comparison.stabilityDelta > 0 ? "improved" : "degraded")
           << " by " << std::fixed << std::setprecision(1) << std::abs(comparison.stabilityDelta) << "%";
        changes.push_back(ss.str());
    }
    
    // Alert change
    if (std::abs(comparison.criticalAlertDelta) >= m_thresholds.alertSignificantDelta) {
        std::ostringstream ss;
        ss << "Critical alerts " << (comparison.criticalAlertDelta < 0 ? "reduced" : "increased")
           << " by " << std::abs(comparison.criticalAlertDelta);
        changes.push_back(ss.str());
    }
    
    if (std::abs(comparison.warningAlertDelta) >= m_thresholds.alertSignificantDelta) {
        std::ostringstream ss;
        ss << "Warnings " << (comparison.warningAlertDelta < 0 ? "reduced" : "increased")
           << " by " << std::abs(comparison.warningAlertDelta);
        changes.push_back(ss.str());
    }
    
    return changes;
}

bool BenchmarkRecorder::HasSignificantImprovement(const BenchmarkComparison& comparison) const {
    if (comparison.avgFpsDeltaPercent > m_thresholds.fpsSignificantPercent) return true;
    if (comparison.stabilityDelta > m_thresholds.stabilitySignificantPercent) return true;
    if (comparison.criticalAlertDelta < -m_thresholds.alertSignificantDelta) return true;
    if (comparison.avgFrameTimeDeltaPercent < -m_thresholds.frameTimeSignificantPercent) return true;
    return false;
}

bool BenchmarkRecorder::HasSignificantDegradation(const BenchmarkComparison& comparison) const {
    if (comparison.avgFpsDeltaPercent < -m_thresholds.fpsSignificantPercent) return true;
    if (comparison.stabilityDelta < -m_thresholds.stabilitySignificantPercent) return true;
    if (comparison.criticalAlertDelta > m_thresholds.alertSignificantDelta) return true;
    if (comparison.avgFrameTimeDeltaPercent > m_thresholds.frameTimeSignificantPercent) return true;
    return false;
}

std::string BenchmarkRecorder::AssessOverall(const BenchmarkComparison& comp) const {
    // Count positive vs negative signals
    int positive = 0;
    int negative = 0;
    
    if (comp.avgFpsDeltaPercent > 0) positive++; else if (comp.avgFpsDeltaPercent < 0) negative++;
    if (comp.stabilityDelta > 0) positive++; else if (comp.stabilityDelta < 0) negative++;
    if (comp.criticalAlertDelta < 0) positive++; else if (comp.criticalAlertDelta > 0) negative++;
    if (comp.warningAlertDelta < 0) positive++; else if (comp.warningAlertDelta > 0) negative++;
    if (comp.avgFrameTimeDeltaPercent < 0) positive++; else if (comp.avgFrameTimeDeltaPercent > 0) negative++;
    if (comp.peakMemoryDeltaPercent < 0) positive++; else if (comp.peakMemoryDeltaPercent > 0) negative++;
    
    if (positive > negative + 2) {
        return "Improved";
    } else if (negative > positive + 2) {
        return "Degraded";
    } else if (positive > negative) {
        return "Slightly Improved";
    } else if (negative > positive) {
        return "Slightly Degraded";
    } else {
        return "No Change";
    }
}

double BenchmarkRecorder::ComputeImprovementScore(const BenchmarkComparison& comp) const {
    double score = 50.0; // neutral baseline
    
    // FPS contribution (up to +/- 25 points)
    double fpsScore = comp.avgFpsDeltaPercent * 0.5; // 2% improvement = 1 point
    fpsScore = std::max(-25.0, std::min(25.0, fpsScore));
    score += fpsScore;
    
    // Stability contribution (up to +/- 15 points)
    double stabilityScore = comp.stabilityDelta * 0.3;
    stabilityScore = std::max(-15.0, std::min(15.0, stabilityScore));
    score += stabilityScore;
    
    // Alert contribution (up to +/- 20 points)
    double alertScore = -(comp.criticalAlertDelta * 3.0 + comp.warningAlertDelta * 0.5);
    alertScore = std::max(-20.0, std::min(20.0, alertScore));
    score += alertScore;
    
    // Frame time contribution (up to +/- 20 points)
    double ftScore = -comp.avgFrameTimeDeltaPercent * 0.4;
    ftScore = std::max(-20.0, std::min(20.0, ftScore));
    score += ftScore;
    
    // Memory contribution (up to +/- 10 points)
    double memScore = -comp.peakMemoryDeltaPercent * 0.2;
    memScore = std::max(-10.0, std::min(10.0, memScore));
    score += memScore;
    
    return std::max(0.0, std::min(100.0, score));
}

double BenchmarkRecorder::GetAverageFpsAcrossSessions() const {
    if (m_sessions.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& s : m_sessions) {
        sum += s.avgFps;
    }
    return sum / m_sessions.size();
}

double BenchmarkRecorder::GetBestSessionFps() const {
    if (m_sessions.empty()) return 0.0;
    double best = m_sessions[0].avgFps;
    for (const auto& s : m_sessions) {
        if (s.avgFps > best) best = s.avgFps;
    }
    return best;
}

double BenchmarkRecorder::GetWorstSessionFps() const {
    if (m_sessions.empty()) return 0.0;
    double worst = m_sessions[0].avgFps;
    for (const auto& s : m_sessions) {
        if (s.avgFps < worst) worst = s.avgFps;
    }
    return worst;
}

std::string BenchmarkRecorder::GetBestSessionId() const {
    if (m_sessions.empty()) return "";
    size_t bestIdx = 0;
    double bestFps = m_sessions[0].avgFps;
    for (size_t i = 1; i < m_sessions.size(); i++) {
        if (m_sessions[i].avgFps > bestFps) {
            bestFps = m_sessions[i].avgFps;
            bestIdx = i;
        }
    }
    return m_sessions[bestIdx].sessionId;
}

std::string BenchmarkRecorder::GetWorstSessionId() const {
    if (m_sessions.empty()) return "";
    size_t worstIdx = 0;
    double worstFps = m_sessions[0].avgFps;
    for (size_t i = 1; i < m_sessions.size(); i++) {
        if (m_sessions[i].avgFps < worstFps) {
            worstFps = m_sessions[i].avgFps;
            worstIdx = i;
        }
    }
    return m_sessions[worstIdx].sessionId;
}

std::string BenchmarkRecorder::ExportSessionsToJSON() const {
    std::ostringstream ss;
    ss << "{\"sessions\":[";
    for (size_t i = 0; i < m_sessions.size(); i++) {
        const auto& s = m_sessions[i];
        ss << "{";
        ss << "\"sessionId\":\"" << s.sessionId << "\",";
        ss << "\"metadata\":{";
        ss << "\"name\":\"" << s.metadata.name << "\",";
        ss << "\"description\":\"" << s.metadata.description << "\",";
        ss << "\"gameVersion\":\"" << s.metadata.gameVersion << "\",";
        ss << "\"buildNumber\":\"" << s.metadata.buildNumber << "\",";
        ss << "\"platform\":\"" << s.metadata.platform << "\",";
        ss << "\"hardwareInfo\":\"" << s.metadata.hardwareInfo << "\",";
        ss << "\"startTime\":" << s.metadata.startTime << ",";
        ss << "\"endTime\":" << s.metadata.endTime << ",";
        ss << "\"durationSec\":" << s.metadata.durationSec << ",";
        ss << "\"isBaseline\":" << (s.metadata.isBaseline ? "true" : "false");
        ss << "},";
        ss << "\"avgFps\":" << std::fixed << std::setprecision(2) << s.avgFps << ",";
        ss << "\"minFps\":" << s.minFps << ",";
        ss << "\"maxFps\":" << s.maxFps << ",";
        ss << "\"p50Fps\":" << s.p50Fps << ",";
        ss << "\"p90Fps\":" << s.p90Fps << ",";
        ss << "\"p95Fps\":" << s.p95Fps << ",";
        ss << "\"p99Fps\":" << s.p99Fps << ",";
        ss << "\"avgFrameTimeMs\":" << s.avgFrameTimeMs << ",";
        ss << "\"maxFrameTimeMs\":" << s.maxFrameTimeMs << ",";
        ss << "\"minFrameTimeMs\":" << s.minFrameTimeMs << ",";
        ss << "\"stdDevFrameTimeMs\":" << s.stdDevFrameTimeMs << ",";
        ss << "\"peakMemoryBytes\":" << s.peakMemoryBytes << ",";
        ss << "\"avgMemoryBytes\":" << s.avgMemoryBytes << ",";
        ss << "\"memoryGrowthRate\":" << s.memoryGrowthRate << ",";
        ss << "\"stabilityScore\":" << s.stabilityScore << ",";
        ss << "\"totalFrames\":" << s.totalFrames << ",";
        ss << "\"criticalAlertCount\":" << s.criticalAlertCount << ",";
        ss << "\"warningAlertCount\":" << s.warningAlertCount << ",";
        ss << "\"totalAlertCount\":" << s.totalAlertCount;
        ss << "}";
        if (i < m_sessions.size() - 1) ss << ",";
    }
    ss << "],\"baselineSessionId\":\"" << m_baselineSessionId << "\"}";
    return ss.str();
}

std::string BenchmarkRecorder::ExportComparisonToJSON(const BenchmarkComparison& comparison) const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"baselineSessionId\":\"" << comparison.baselineSessionId << "\",";
    ss << "\"comparisonSessionId\":\"" << comparison.comparisonSessionId << "\",";
    ss << "\"avgFpsDelta\":" << std::fixed << std::setprecision(2) << comparison.avgFpsDelta << ",";
    ss << "\"avgFpsDeltaPercent\":" << comparison.avgFpsDeltaPercent << ",";
    ss << "\"minFpsDelta\":" << comparison.minFpsDelta << ",";
    ss << "\"maxFpsDelta\":" << comparison.maxFpsDelta << ",";
    ss << "\"p95FpsDelta\":" << comparison.p95FpsDelta << ",";
    ss << "\"p99FpsDelta\":" << comparison.p99FpsDelta << ",";
    ss << "\"avgFrameTimeDeltaMs\":" << comparison.avgFrameTimeDeltaMs << ",";
    ss << "\"avgFrameTimeDeltaPercent\":" << comparison.avgFrameTimeDeltaPercent << ",";
    ss << "\"maxFrameTimeDeltaMs\":" << comparison.maxFrameTimeDeltaMs << ",";
    ss << "\"peakMemoryDeltaBytes\":" << comparison.peakMemoryDeltaBytes << ",";
    ss << "\"peakMemoryDeltaPercent\":" << comparison.peakMemoryDeltaPercent << ",";
    ss << "\"avgMemoryDeltaBytes\":" << comparison.avgMemoryDeltaBytes << ",";
    ss << "\"stabilityDelta\":" << comparison.stabilityDelta << ",";
    ss << "\"criticalAlertDelta\":" << comparison.criticalAlertDelta << ",";
    ss << "\"warningAlertDelta\":" << comparison.warningAlertDelta << ",";
    ss << "\"overallAssessment\":\"" << comparison.overallAssessment << "\",";
    ss << "\"improvementScore\":" << std::setprecision(1) << comparison.improvementScore << ",";
    ss << "\"significantChanges\":[";
    for (size_t i = 0; i < comparison.significantChanges.size(); i++) {
        ss << "\"" << comparison.significantChanges[i] << "\"";
        if (i < comparison.significantChanges.size() - 1) ss << ",";
    }
    ss << "]}";
    return ss.str();
}

std::string BenchmarkRecorder::ExportReport(const std::string& sessionId) const {
    BenchmarkResult session = GetSession(sessionId);
    if (session.sessionId.empty()) return "{}";
    
    std::ostringstream ss;
    ss << "=== Benchmark Report: " << session.metadata.name << " ===\n";
    ss << "Session ID: " << session.sessionId << "\n";
    ss << "Date: " << session.metadata.startTime << "\n";
    ss << "Platform: " << session.metadata.platform << "\n";
    ss << "Hardware: " << session.metadata.hardwareInfo << "\n";
    ss << "Game Version: " << session.metadata.gameVersion << "\n";
    ss << "Duration: " << session.metadata.durationSec << " seconds\n";
    ss << "Total Frames: " << session.totalFrames << "\n\n";
    
    ss << "--- Performance ---\n";
    ss << "  Average FPS: " << std::fixed << std::setprecision(2) << session.avgFps << "\n";
    ss << "  Min FPS: " << session.minFps << "\n";
    ss << "  Max FPS: " << session.maxFps << "\n";
    ss << "  P50 FPS: " << session.p50Fps << "\n";
    ss << "  P90 FPS: " << session.p90Fps << "\n";
    ss << "  P95 FPS: " << session.p95Fps << "\n";
    ss << "  P99 FPS: " << session.p99Fps << "\n\n";
    
    ss << "--- Frame Time ---\n";
    ss << "  Average: " << std::fixed << std::setprecision(3) << session.avgFrameTimeMs << " ms\n";
    ss << "  Min: " << session.minFrameTimeMs << " ms\n";
    ss << "  Max: " << session.maxFrameTimeMs << " ms\n";
    ss << "  Std Dev: " << session.stdDevFrameTimeMs << " ms\n\n";
    
    ss << "--- Memory ---\n";
    ss << "  Peak: " << (session.peakMemoryBytes / 1024 / 1024) << " MB\n";
    ss << "  Average: " << (session.avgMemoryBytes / 1024 / 1024) << " MB\n";
    ss << "  Growth Rate: " << std::fixed << std::setprecision(2) 
       << (session.memoryGrowthRate / 1024.0) << " KB/frame\n\n";
    
    ss << "--- Stability ---\n";
    ss << "  Score: " << std::fixed << std::setprecision(1) << session.stabilityScore << "%\n\n";
    
    ss << "--- Alerts ---\n";
    ss << "  Critical: " << session.criticalAlertCount << "\n";
    ss << "  Warnings: " << session.warningAlertCount << "\n";
    ss << "  Total: " << session.totalAlertCount << "\n";
    
    // Compare to baseline if available
    if (!m_baselineSessionId.empty() && sessionId != m_baselineSessionId) {
        BenchmarkComparison comp = CompareToBaseline(sessionId);
        ss << "\n--- Comparison to Baseline ---\n";
        ss << "  Assessment: " << comp.overallAssessment << "\n";
        ss << "  Improvement Score: " << std::fixed << std::setprecision(1) << comp.improvementScore << "\n";
        ss << "  FPS Change: " << std::showpos << std::fixed << std::setprecision(1) 
           << comp.avgFpsDeltaPercent << "%\n";
        ss << std::noshowpos;
        
        if (!comp.significantChanges.empty()) {
            ss << "  Significant Changes:\n";
            for (const auto& change : comp.significantChanges) {
                ss << "    - " << change << "\n";
            }
        }
    }
    
    return ss.str();
}

std::string BenchmarkRecorder::ExportFullReport() const {
    std::ostringstream ss;
    ss << "=== Full Benchmark Report ===\n";
    ss << "Sessions: " << m_sessions.size() << "\n";
    ss << "Baseline: " << m_baselineSessionId << "\n\n";
    
    for (const auto& s : m_sessions) {
        ss << ExportReport(s.sessionId) << "\n";
        ss << "--------------------------------------------\n\n";
    }
    
    return ss.str();
}

bool BenchmarkRecorder::SaveToFile(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    
    std::string json = ExportSessionsToJSON();
    file << json;
    return file.good();
}

bool BenchmarkRecorder::LoadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    // Simple parsing - read entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // In a full implementation, we'd parse the JSON properly
    // For now, just verify we got content
    return !content.empty();
}

void BenchmarkRecorder::ClearSessions() {
    m_sessions.clear();
    m_sessionIndex.clear();
    m_baselineSessionId = "";
    m_isRecording = false;
    m_currentSessionId = "";
}

void BenchmarkRecorder::RemoveSession(const std::string& sessionId) {
    auto it = m_sessionIndex.find(sessionId);
    if (it != m_sessionIndex.end()) {
        size_t idx = it->second;
        if (idx < m_sessions.size()) {
            m_sessions.erase(m_sessions.begin() + static_cast<long>(idx));
            m_sessionIndex.erase(it);
            
            // Rebuild index
            for (size_t i = 0; i < m_sessions.size(); i++) {
                m_sessionIndex[m_sessions[i].sessionId] = i;
            }
        }
    }
    
    if (m_baselineSessionId == sessionId) {
        m_baselineSessionId = m_sessions.empty() ? "" : m_sessions[0].sessionId;
    }
}

std::string BenchmarkRecorder::GenerateSessionId() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto epoch = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
    
    std::ostringstream ss;
    ss << "session_" << millis;
    return ss.str();
}

int64_t BenchmarkRecorder::GetCurrentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

void BenchmarkRecorder::TrimHistory() {
    while (m_sessions.size() > m_maxSessionHistory) {
        // Remove oldest non-baseline session
        if (m_sessions.front().sessionId != m_baselineSessionId) {
            m_sessionIndex.erase(m_sessions.front().sessionId);
            m_sessions.erase(m_sessions.begin());
            
            // Rebuild index
            for (size_t i = 0; i < m_sessions.size(); i++) {
                m_sessionIndex[m_sessions[i].sessionId] = i;
            }
        } else {
            // Baseline is at front, remove second oldest
            if (m_sessions.size() > 1) {
                m_sessionIndex.erase(m_sessions[1].sessionId);
                m_sessions.erase(m_sessions.begin() + 1);
                
                for (size_t i = 0; i < m_sessions.size(); i++) {
                    m_sessionIndex[m_sessions[i].sessionId] = i;
                }
            }
        }
    }
}

} // namespace ProfilerCore
